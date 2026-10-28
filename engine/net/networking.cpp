// Networking hooks for R1Delta
// Handles CNetChan, connectionless packets, alltalk fix, etc.

#include "networking.h"
#include "core.h"
#include "load.h"
#include "logging.h"
#include "netadr.h"
#include "bitbuf.h"
#include "compression.h"
#include "p2p/p2p_identity.h"
#include "engine/filesystem/r2port_provider.h"
#include "engine/filesystem/r2_map_provider.h"
#include "engine/filesystem/r2_material_provider.h"

#include <filesystem>
#include <string>

#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

// Original function pointers
const char* (*oCNetChan__GetAddress)(CNetChan* thisptr) = nullptr;
__int64 (*oStringCompare_AllTalkHookDedi)(__int64 a1, __int64 a2) = nullptr;
__int64 (*oCNetChan__SendDatagramLISTEN_Part2)(__int64 thisptr, unsigned int length, int SendToResult) = nullptr;
CL_Retry_fType CL_Retry_fOriginal = nullptr;
SVC_ServerInfo__WriteToBufferType SVC_ServerInfo__WriteToBufferOriginal = nullptr;

// Retry circuit breaker state: a wedged netchan spams one packet size
// continuously (about ten packets a second), while healthy traffic keeps
// changing size. More than STUCK_DELAY seconds of one size means stuck.
static int g_last_packet_size = -1;
static double g_same_size_since = 0.0;
static double g_last_retry_time = 0.0;
static const double STUCK_DELAY = 10.0;
static const double RETRY_DELAY = 15.0;

const char* CNetChan__GetAddress(CNetChan* thisptr) {
    const netadr_t adr(oCNetChan__GetAddress(thisptr));
    // Players connected through EOS / iroh / tailcat / TURN report the IP the
    // master server attested for them, so status, addip and ban kicks work
    // the same on every transport.
    uint32_t attestedIp = 0;
    if (p2p::LookupIdentityIpv4(reinterpret_cast<const uint8_t*>(&adr.GetIP()), attestedIp)) {
        static thread_local char s[4][16];
        static thread_local int slot = 0;
        char* out = s[(slot++) % 4];
        snprintf(out, sizeof(s[0]), "%u.%u.%u.%u", (attestedIp >> 24) & 0xFF, (attestedIp >> 16) & 0xFF,
            (attestedIp >> 8) & 0xFF, attestedIp & 0xFF);
        return out;
    }
    return adr.GetAddressString();
}

// AllTalk fix - makes sv_alltalk work on dedicated and listen servers
__int64 StringCompare_AllTalkHookDedi(__int64 a1, __int64 a2) {
    static const ConVarR1* var = nullptr;
    if (!var && OriginalCCVar_FindVar2)
        var = OriginalCCVar_FindVar2(cvarinterface, "sv_alltalk");

    // Check if this is called from the alltalk check location
    if ((((uintptr_t)(_ReturnAddress())) == G_engine_ds + 0x05FCD3) ||
        (((uintptr_t)(_ReturnAddress())) == G_engine + 0x0EE5EC)) {
        if (var && var->m_Value.m_nValue == 1)
            return false;
    }
    return strcmp((const char*)a1, (const char*)a2);
}

// Per-map-type configuration (user approved feature): when a map finishes loading (client signon state 8), <game dir>\cfg\r2port_maps.cfg is executed for a
// converted R2 map and <game dir>\cfg\r1_maps.cfg for a stock R1 map, only if the file exists (no file: nothing runs). It goes through the engine's
// command buffer (exec <name>) at level load, nothing per frame, and runs after the pvs_extraCull policy above, so a pvs_extraCull line in these
// files wins. Archive cvars set there still persist to the user's profile; settings read at shader creation (r2port_sso, R2_LM_BICUBIC,
// r2port_ao_tech startup parts) are not affected by a late exec. One log line per exec.
static void ExecR2MapTypeConfig(const std::string& mapName)
{
    const std::filesystem::path gameDirectory = r2port::GetR2GameContentDirectory();
    if (gameDirectory.empty())
        return;
    const bool converted = r2port::IsR2PortMap(mapName);
    const char* name = converted ? "r2port_maps.cfg" : "r1_maps.cfg";
    std::error_code error;
    if (!std::filesystem::is_regular_file(gameDirectory / "cfg" / name, error) || error)
        return;
    char command[96]{};
    _snprintf_s(command, sizeof(command), _TRUNCATE, "exec %s\n", name);
    Cbuf_AddText(0, command, 0);
    char message[200]{};
    _snprintf_s(message, sizeof(message), _TRUNCATE, "R1Delta: map config exec cfg/%s for %s map %s\n", name, converted ? "converted R2" : "stock R1", mapName.c_str());
    Msg("%s", message);
}

// pvs_extraCull (CORE cfg/autoexec_delta.cfg sets 1, engine default 0) culls whole world meshes of converted R2 maps (the glass roof over
// the mp_eden courtyard, EdenRoofFade: pvs_extraCull 0 at runtime restores them; occluder lumps and ref boxes are not the cause). Interim fix, default
// ON until a converter-side fix is proven: while a converted R2 map is the active map the cvar is forced to 0 and the user's value is put back
// when a stock R1 map becomes active (costs the fps benefit of extraCull on converted maps). Evaluated at every client signon state change
// (a console command at level load, nothing per frame). R1DELTA_R2PORT_KEEP_PVS_EXTRACULL=1 keeps the user's value.
static void ApplyR2PvsExtraCullPolicy(const std::string& mapName)
{
    static bool keep = [] {
        char value[8]{};
        const DWORD length = GetEnvironmentVariableA("R1DELTA_R2PORT_KEEP_PVS_EXTRACULL", value, sizeof(value));
        return length && value[0] != '0';
    }();
    if (keep)
        return;
    static bool overridden = false;
    static int savedValue = 0;
    if (!OriginalCCVar_FindVar2 || !cvarinterface)
        return;
    const ConVarR1* cvar = OriginalCCVar_FindVar2(cvarinterface, "pvs_extraCull");
    if (!cvar)
        return;
    const int current = cvar->m_Value.m_nValue;
    if (r2port::IsR2PortMap(mapName)) {
        if (current == 0)
            return;
        if (!overridden) {
            savedValue = current;
            overridden = true;
        }
        Cbuf_AddText(0, "pvs_extraCull 0\n", 0);
        char message[160]{};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "R1Delta: R2 map %s: pvs_extraCull %d -> 0 (it culls whole world meshes of converted maps)\n", mapName.c_str(), current);
        Msg("%s", message);
    }
    else if (overridden) {
        overridden = false;
        char command[48]{};
        _snprintf_s(command, sizeof(command), _TRUNCATE, "pvs_extraCull %d\n", savedValue);
        Cbuf_AddText(0, command, 0);
        char message[160]{};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "R1Delta: stock map %s: pvs_extraCull restored to %d\n", mapName.c_str(), savedValue);
        Msg("%s", message);
    }
}

__int64 CNetChan__SendDatagramLISTEN_Part2_Hook(__int64 thisptr, unsigned int length, int SendToResult) {
    static auto original_fn = oCNetChan__SendDatagramLISTEN_Part2;

    // Make sure this is a client netchan
    if ((*(uint8_t*)(((uintptr_t)thisptr) + 216) > 0))
        return original_fn(thisptr, length, SendToResult);
    typedef char* (__fastcall* GetBaseClientFn)(int slot);
    const auto base_client = reinterpret_cast<GetBaseClientFn>(G_engine + 0x5F470)(-1);
    const int signon_state = base_client
        ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(base_client) + 116) : -1;
    static int last_logged_signon_state = -1;
    if (base_client && signon_state != last_logged_signon_state) {
        const std::string map_name = r2port::ActiveMapName();
        if (!map_name.empty()) {
            char message[768]{};
            _snprintf_s(message, sizeof(message), _TRUNCATE,
                "R1Delta: R2 client signon map=%s state=%d plat-time=%.3f map-elapsed-ms=%llu\n",
                map_name.c_str(), signon_state, Plat_FloatTime(),
                static_cast<unsigned long long>(r2port::ActiveMapElapsedMilliseconds()));
            Msg("%s", message);
            OutputDebugStringA(message);
            if (signon_state == 8) {
                r2port::LogProviderReadMetrics("signon-full");
                r2port::LogR2TextureProviderMetrics("signon-full");
            }
        }
        if (!map_name.empty() && !IsDedicatedServer())
            ApplyR2PvsExtraCullPolicy(map_name);
        if (signon_state == 8 && !map_name.empty() && !IsDedicatedServer())
            ExecR2MapTypeConfig(map_name);
        last_logged_signon_state = signon_state;
    }

    // The engine's own "sent packet - %d bytes" print in the CNetChan send
    // path fires constantly during the hang and never during healthy play:
    // byte +16256 is set while the connection is still in progress (it is a
    // netchan state flag, not a convar). The hang is exactly that window with
    // a frozen byte count, so gate the breaker on the same condition as the
    // print; outside it (healthy play) the breaker can never misfire.
    if (*reinterpret_cast<uint8_t*>(thisptr + 16256) == 0) {
        g_last_packet_size = -1;
        return original_fn(thisptr, length, SendToResult);
    }

    // Never fire while the host state is ss_active (server_state_t == 2 at
    // G_engine+0x2966168, cf. the same field in CL_Retry_f): a running
    // game/server has healthy same-size traffic and a forced retry there
    // would disrupt it.
    if (*reinterpret_cast<int*>(G_engine + 0x2966168) == 2) {
        g_last_packet_size = -1;
        return original_fn(thisptr, length, SendToResult);
    }

    // The signon flow streams the same small packets at about ten a second
    // (signonstate 8 among them); tripping there aborts a healthy signon, so
    // never fire while the client is in signonstate 8. The client signon
    // state is the +116 field of the client state object GetBaseClient(-1)
    // returns (engine.dll+0x5F470); G_engine+0x2966168 is the host state
    // (server_state_t, 2 == ss_active) and can never equal 8.

    if (base_client && signon_state == 8) {
        // Restart the window so the signon stream cannot trip it right after.
        g_last_packet_size = -1;
        return original_fn(thisptr, length, SendToResult);
    }

    const double current_time = Plat_FloatTime();
    if (static_cast<int>(length) != g_last_packet_size) {
        g_last_packet_size = static_cast<int>(length);
        g_same_size_since = current_time;
    }
    else if (current_time - g_same_size_since > STUCK_DELAY &&
             current_time - g_last_retry_time >= RETRY_DELAY) {
        Warning("Circuit breaker tripped due to packets!\n");
        const std::string map_name = r2port::ActiveMapName();
        if (!map_name.empty()) {
            char message[768]{};
            _snprintf_s(message, sizeof(message), _TRUNCATE,
                "R1Delta: R2 load circuit-breaker map=%s plat-time=%.3f map-elapsed-ms=%llu\n",
                map_name.c_str(), current_time,
                static_cast<unsigned long long>(r2port::ActiveMapElapsedMilliseconds()));
            Msg("%s", message);
            OutputDebugStringA(message);
        }
        r2port::LogProviderReadMetrics("circuit-breaker");
        r2port::LogR2TextureProviderMetrics("circuit-breaker");
        // Call the handler directly: "retry" through the console resolves via
        // the alias table, so a user alias silently disabled this.
        CL_Retry_f();
        g_same_size_since = current_time;
        g_last_retry_time = current_time;
    }

    return original_fn(thisptr, length, SendToResult);
}

// Read and convert 2015 connect packets to 2014 format
int32_t ReadConnectPacket2015AndWriteConnectPacket2014(bf_read& msg, bf_write& buffer)
{
    ZoneScoped;

    char type = msg.ReadByte();
    if (type != 'A')
    {
        return -1;
    }

    buffer.WriteByte('A');

    int version = msg.ReadLong();
    if (version != 1040)
    {
        return -1;
    }
    buffer.WriteLong(1036); // 2014 version

    buffer.WriteLong(msg.ReadLong()); // hostVersion
    int32_t lastChallenge = msg.ReadLong();
    buffer.WriteLong(lastChallenge); // challengeNr
    buffer.WriteLong(msg.ReadLong()); // unknown
    buffer.WriteLong(msg.ReadLong()); // unknown1
    msg.ReadLongLong(); // skip platformUserId

    char tempStr[256];
    msg.ReadString(tempStr, sizeof(tempStr));
    buffer.WriteString(tempStr); // name

    msg.ReadString(tempStr, sizeof(tempStr));
    buffer.WriteString(tempStr); // password

    msg.ReadString(tempStr, sizeof(tempStr));
    buffer.WriteString(tempStr); // unknown2

    int unknownCount = msg.ReadByte();
    buffer.WriteByte(unknownCount);
    for (int i = 0; i < unknownCount; i++)
    {
        buffer.WriteLongLong(msg.ReadLongLong()); // unknown3
    }

    msg.ReadString(tempStr, sizeof(tempStr));
    buffer.WriteString(tempStr); // serverFilter

    msg.ReadLong(); // skip playlistVersionNumber
    msg.ReadLong(); // skip persistenceVersionNumber
    msg.ReadLongLong(); // skip persistenceHash

    int numberOfPlayers = msg.ReadByte();
    buffer.WriteByte(numberOfPlayers);
    for (int i = 0; i < numberOfPlayers; i++)
    {
        // Read SplitPlayerConnect from 2015 packet
        int type = msg.ReadUBitLong(6);
        int count = msg.ReadByte();

        // Write SplitPlayerConnect to 2014 packet
        buffer.WriteUBitLong(type, 6);
        buffer.WriteByte(count);

        for (int j = 0; j < count; j++)
        {
            msg.ReadString(tempStr, sizeof(tempStr));
            buffer.WriteString(tempStr); // key

            msg.ReadString(tempStr, sizeof(tempStr));
            buffer.WriteString(tempStr); // value
        }
    }

    buffer.WriteByte(msg.ReadByte()); // lowViolence

    buffer.WriteByte(1);

    return !buffer.IsOverflowed() ? lastChallenge : -1;
}

// Dedicated server connectionless packet processor
ProcessConnectionlessPacketType ProcessConnectionlessPacketOriginal = nullptr;
static double lastReceived = 0.f;

char __fastcall ProcessConnectionlessPacketDedi(unsigned int* a1, netpacket_s* a2)
{
    ZoneScoped;

    char buffer[1200] = { 0 };
    bf_write writer(reinterpret_cast<char*>(buffer), sizeof(buffer));

    if (((char*)a2->pData + 4)[0] == 'A' && ReadConnectPacket2015AndWriteConnectPacket2014(a2->message, writer) != -1)
    {
        if (lastReceived == a2->received)
            return false;
        lastReceived = a2->received;
        bf_read converted(reinterpret_cast<char*>(buffer), writer.GetNumBytesWritten());
        a2->message = converted;
        return ProcessConnectionlessPacketOriginal(a1, a2);
    }
    else
    {
        return ProcessConnectionlessPacketOriginal(a1, a2);
    }
}

// Client connectionless packet processor - security fix for sv_limit_queries
ProcessConnectionlessPacketType ProcessConnectionlessPacketOriginalClient = nullptr;

char __fastcall ProcessConnectionlessPacketClient(unsigned int* a1, netpacket_s* a2)
{
    return ProcessConnectionlessPacketOriginalClient(a1, a2);
}

// Fix retry command for listen servers
void CL_Retry_f() {
    uintptr_t engine = G_engine;
    typedef void (*Cbuf_AddTextType)(int a1, const char* a2, unsigned int a3);
    Cbuf_AddTextType Cbuf_AddTextLocal = (Cbuf_AddTextType)(engine + 0x102D50);
    if (*(int*)(engine + 0x2966168) == 2) {
        Cbuf_AddTextLocal(0, "connect localhost\n", 0);
    }
    else {
        return CL_Retry_fOriginal();
    }
}

// SVC_ServerInfo write hook - fixes gamedir for compatibility
char __fastcall SVC_ServerInfo__WriteToBuffer(__int64 a1, __int64 a2)
{
    static const char* real_gamedir = "r1_dlc1";
    *(const char**)(a1 + 72) = real_gamedir;
    if (IsDedicatedServer()) {
        const char** gamemode = reinterpret_cast<const char**>(G_server + 0xB68520);
        *(const char**)(a1 + 88) = *gamemode;
    }
    auto ret = SVC_ServerInfo__WriteToBufferOriginal(a1, a2);
    return ret;
}
