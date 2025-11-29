// Networking hooks for R1Delta
// Handles CNetChan, connectionless packets, alltalk fix, etc.

#include "networking.h"
#include "core.h"
#include "load.h"
#include "logging.h"
#include "netadr.h"
#include "bitbuf.h"
#include "compression.h"

#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

// Original function pointers
const char* (*oCNetChan__GetAddress)(CNetChan* thisptr) = nullptr;
__int64 (*oStringCompare_AllTalkHookDedi)(__int64 a1, __int64 a2) = nullptr;
__int64 (*oCNetChan__SendDatagramLISTEN_Part2)(__int64 thisptr, unsigned int length, int SendToResult) = nullptr;
CL_Retry_fType CL_Retry_fOriginal = nullptr;
SVC_ServerInfo__WriteToBufferType SVC_ServerInfo__WriteToBufferOriginal = nullptr;

// Retry circuit breaker state
static int g_consecutive_packets = 0;
static const int PACKET_THRESHOLD = 1500;
static const int PACKET_SIZE = 16;
static double g_last_retry_time = 0.0;
static const double RETRY_DELAY = 15.0;

const char* CNetChan__GetAddress(CNetChan* thisptr) {
    return (netadr_t(std::string(oCNetChan__GetAddress(thisptr)).c_str())).GetAddressString();
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

// Circuit breaker for stuck connections
__int64 CNetChan__SendDatagramLISTEN_Part2_Hook(__int64 thisptr, unsigned int length, int SendToResult) {
    static auto original_fn = oCNetChan__SendDatagramLISTEN_Part2;

    // Make sure this is a client netchan
    if ((*(uint8_t*)(((uintptr_t)thisptr) + 216) > 0))
        return original_fn(thisptr, length, SendToResult);

    // Check packet size
    if (length == PACKET_SIZE) {
        g_consecutive_packets++;
    }
    else {
        g_consecutive_packets = 0;
    }

    // Check conditions for retry
    bool should_retry = false;

    // Check packet threshold
    if (g_consecutive_packets > PACKET_THRESHOLD) {
        should_retry = true;
        Warning("Circuit breaker tripped due to packets!\n");
    }

    // Handle retry if needed
    if (should_retry) {
        double current_time = Plat_FloatTime();
        if (current_time - g_last_retry_time >= RETRY_DELAY) {
            Cbuf_AddText(0, "retry\n", 0);
            g_consecutive_packets = 0;
            g_last_retry_time = current_time;
        }
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
    static auto sv_limit_quires = CCVar_FindVar(cvarinterface, "sv_limit_queries");
    static auto& sv_limit_queries_var = sv_limit_quires->m_Value.m_nValue;

    if (sv_limit_queries_var == 1 && a2->pData[4] == 'N') {
        sv_limit_queries_var = 0;
    }
    else if (sv_limit_queries_var == 0 && a2->pData[4] != 'N') {
        sv_limit_queries_var = 1;
    }
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
