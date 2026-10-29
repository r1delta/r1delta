#include "r2_wallrun.h"

#include "engine/cvar.h"
#include "engine/logging/logging.h"

#include <MinHook.h>
#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace r2port::wallrun {
namespace {

// ---- supported builds -----------------------------------------------------------------------------------------------------
// Retail R1 server.dll (PE stamp 0x58E93EDF, the R1O server_local.dll is the same binary) and retail R1 client.dll (0x55038DB8).
// Both contain the same movement code: a helper that fires a line ray of (a4 + 20) units from `pos` along -normal and reports
// "hit something that is not NOCLIMB (0x100000)"; the per tick wallrun update calls it twice after the wall is latched
// (origin + 10, then origin + 16) and detaches only when both calls say no.
struct Build {
    const char* module;
    std::uint32_t timestamp;
    std::uint32_t imageSize;
    std::uintptr_t helperRva;
    std::uintptr_t site1Call;      // RVA of the `call helper` at origin + 10 (first ray)
    std::uintptr_t site2Call;      // RVA of the `call helper` at origin + 16 (second ray)
    std::uintptr_t traceIfaceRva;  // IEngineTrace* global
    std::uintptr_t entityTableRva; // server: inline table, client: slot holding the table pointer
    std::uintptr_t filterVtableRva;// server: CTraceFilterSimple vftable, client: unused
    std::uintptr_t filterCtorRva;  // client: CTraceFilterSimple constructor (filter, passEntity, group), server: unused
    std::uint32_t playerMaskOffset;
    std::uint32_t entIsPlayerVtable;   // vtable byte offsets of the two "this surface is not wallrunnable" predicates
    std::uint32_t entNoWallrunVtable;
    bool isServer;
};

constexpr unsigned char kHelperPrologue[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x48, 0x8D, 0x6C, 0x24, 0xD0, 0x48, 0x81, 0xEC, 0x30, 0x01, 0x00, 0x00,
};

constexpr Build kServerBuild = {
    "server.dll", 0x58E93EDF, 0xFB5000,
    0x153900, 0x152F86, 0x152FB3,
    0xC31110, 0xD425C8, 0x884E68, 0,
    0xC10, 688, 592, true,
};

constexpr Build kClientBuild = {
    "client.dll", 0x55038DB8, 0x39A4000,
    0x163170, 0x16B397, 0x16B3D5,
    0xBF5268, 0xAEED60, 0, 0x284280,
    0x2490, 1336, 1400, false,
};

// R2 server.dll sub_180186840 (the sweep) and sub_180186D60 (the per tick update that calls it with a3 = 16).
constexpr float kR2BoxFudge = 0.2929f;   // dword_18089F320: hull half width * 0.2929 extends the sweep
constexpr float kPostLatchHeight = 16.0f;
constexpr std::uint32_t kContentsNoClimb = 0x100000;

// ---- state ----------------------------------------------------------------------------------------------------------------
ConVarR1* g_modeVar = nullptr;
ConVarR1* g_distVar = nullptr;
ConVarR1O* g_modeVarR1O = nullptr;
ConVarR1O* g_distVarR1O = nullptr;
bool g_registered = false;

struct Hooked {
    const Build* build = nullptr;
    std::uintptr_t base = 0;
    std::uintptr_t site1Ret = 0;
    std::uintptr_t site2Ret = 0;
    using HelperFn = bool(__fastcall*)(void* ctx, float* pos, float* normal, float halfWidth);
    HelperFn original = nullptr;
    bool installed = false;
};
Hooked g_server;
Hooked g_client;

bool EnvOptOut()
{
    char env[8] = {};
    return GetEnvironmentVariableA("R2PORT_WALLRUN_R2", env, sizeof(env)) && std::strcmp(env, "0") == 0;
}

bool ModeOn()
{
    if (g_modeVar)
        return g_modeVar->m_Value.m_nValue != 0;
    if (g_modeVarR1O)
        return g_modeVarR1O->m_Value.m_nValue != 0;
    return true;
}

float AllowedDist()
{
    if (g_distVar)
        return g_distVar->m_Value.m_fValue;
    if (g_distVarR1O)
        return g_distVarR1O->m_Value.m_fValue;
    return 13.0f;
}

// ---- the R2 sweep ---------------------------------------------------------------------------------------------------------
// R2 sub_180186840(ctx, pos, a3, normal, W, allowed, out) == 2 ("wall within reach"), reproduced on the R1 engine trace.
//   k   = a3 + allowed + W * 0.2929
//   box = cube of half extent W centred on pos, swept horizontally to pos - k * (normal.x, normal.y, 0)
//   no hit                      -> no wall
//   hit contents & NOCLIMB      -> no wall
//   hit entity player / bad     -> no wall (two entity predicates, same slots R1 CanWallRun uses)
//   d = fraction * k; when d > allowed and the hit plane faces up (n.z > 0): slope correction
//       d = ((1 / clamp(hit.xy . wall.xy, 0, 1)) - 1) * W + d - hit.z / clamp(...) * a3
//   wall when d <= allowed
// Differences to R2 that are deliberate: R2 skips the entity predicates for the world entity (R1's predicates answer false for
// it anyway), and the slope correction is not gated on R2's extra trace byte at +67 (no counterpart found in the R1 trace).
struct Entity {
    static void* Lookup(const Build& b, std::uintptr_t base, std::uint32_t handle)
    {
        if (handle == 0xFFFFFFFFu || (handle & 0xFFFFu) >= 0x4000u)
            return nullptr;
        const std::uint32_t index = handle & 0xFFFFu;
        const std::uint32_t serial = handle >> 16;
        if (b.isServer) {
            const auto* entry = reinterpret_cast<const std::uint8_t*>(base + b.entityTableRva) + index * 48ull;
            if (*reinterpret_cast<const std::uint32_t*>(entry + 8) != serial)
                return nullptr;
            return *reinterpret_cast<void* const*>(entry);
        }
        const auto* table = *reinterpret_cast<const std::uint8_t* const*>(base + b.entityTableRva);
        if (!table)
            return nullptr;
        const auto* entry = table + index * 32ull;
        if (*reinterpret_cast<const std::uint32_t*>(entry + 16) != serial)
            return nullptr;
        return *reinterpret_cast<void* const*>(entry + 8);
    }
};

bool CallPredicate(void* entity, std::uint32_t vtableOffset)
{
    auto** vtable = *reinterpret_cast<void***>(entity);
    using Fn = unsigned char(__fastcall*)(void*);
    return reinterpret_cast<Fn>(vtable[vtableOffset / 8])(entity) != 0;
}

bool R2WallSweep(const Hooked& h, void* ctx, const float* pos, float a3, const float* normal, float halfWidth, float allowed)
{
    const Build& b = *h.build;
    auto* const ctxBytes = static_cast<std::uint8_t*>(ctx);
    auto* const player = *reinterpret_cast<std::uint8_t**>(ctxBytes + 8);
    auto* const move = *reinterpret_cast<std::uint8_t**>(ctxBytes + 16);
    if (!player || !move || !(halfWidth > 0.0f))
        return false;

    const float k = (a3 + allowed) + halfWidth * kR2BoxFudge;
    const float delta[3] = { -(k * normal[0]), -(k * normal[1]), 0.0f };

    alignas(16) std::uint8_t ray[0x80] = {};
    float* const rayStart = reinterpret_cast<float*>(ray + 0x00);
    float* const rayDelta = reinterpret_cast<float*>(ray + 0x10);
    float* const rayExtents = reinterpret_cast<float*>(ray + 0x30);
    rayStart[0] = pos[0];
    rayStart[1] = pos[1];
    rayStart[2] = pos[2];
    rayDelta[0] = delta[0];
    rayDelta[1] = delta[1];
    rayDelta[2] = delta[2];
    rayExtents[0] = halfWidth;
    rayExtents[1] = halfWidth;
    rayExtents[2] = halfWidth;
    ray[0x48] = 0; // IsRay (a box, not a line)
    ray[0x49] = (delta[0] != 0.0f || delta[1] != 0.0f) ? 1 : 0; // IsSwept

    void* const passEntity = Entity::Lookup(b, h.base, *reinterpret_cast<const std::uint32_t*>(move + 8));
    const std::uint32_t mask = *reinterpret_cast<const std::uint32_t*>(player + b.playerMaskOffset);

    alignas(16) std::uint8_t filter[0x40] = {};
    if (b.isServer) {
        *reinterpret_cast<std::uintptr_t*>(filter + 0x00) = h.base + b.filterVtableRva;
        *reinterpret_cast<void**>(filter + 0x08) = passEntity;
        *reinterpret_cast<std::uint32_t*>(filter + 0x18) = 8; // same collision group as the R1 ray helper
    }
    else {
        using FilterCtor = void*(__fastcall*)(void* filter, void* passEntity, int group);
        reinterpret_cast<FilterCtor>(h.base + b.filterCtorRva)(filter, passEntity, 8);
    }

    alignas(16) std::uint8_t trace[0x100] = {};
    void* const iface = *reinterpret_cast<void**>(h.base + b.traceIfaceRva);
    if (!iface)
        return false;
    using TraceRayFn = void(__fastcall*)(void* self, const void* ray, std::uint32_t mask, void* filter, int flags, void* trace);
    reinterpret_cast<TraceRayFn>((*reinterpret_cast<void***>(iface))[5])(iface, ray, mask, filter, 0, trace);

    const float fraction = *reinterpret_cast<const float*>(trace + 48);
    const std::uint32_t contents = *reinterpret_cast<const std::uint32_t*>(trace + 52);
    if (fraction == 1.0f)
        return false;
    if ((contents & kContentsNoClimb) != 0)
        return false;
    if (void* const hit = *reinterpret_cast<void**>(trace + 96)) {
        if (CallPredicate(hit, b.entIsPlayerVtable) || CallPredicate(hit, b.entNoWallrunVtable))
            return false;
    }

    const float* const hitNormal = reinterpret_cast<const float*>(trace + 32);
    float d = fraction * k;
    if (d > allowed && hitNormal[2] > 0.0f) {
        const float dot = std::fmin(std::fmax((hitNormal[1] * normal[1]) + (hitNormal[0] * normal[0]), 0.0f), 1.0f);
        const float inv = 1.0f / dot;
        d = (((inv - 1.0f) * halfWidth) + d) - ((hitNormal[2] * inv) * a3);
    }
    return d <= allowed;
}

bool __fastcall ServerHelperDetour(void* ctx, float* pos, float* normal, float halfWidth)
{
    const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (ModeOn()) {
        // Stock: ray at origin + 10, then ray at origin + 16, detach when both miss. R2: one box sweep at origin + 16. The first
        // call answers "no" without tracing so the second call, which is exactly at origin + 16, decides.
        if (returnAddress == g_server.site1Ret)
            return false;
        if (returnAddress == g_server.site2Ret)
            return R2WallSweep(g_server, ctx, pos, kPostLatchHeight, normal, halfWidth, AllowedDist());
    }
    return g_server.original(ctx, pos, normal, halfWidth);
}

bool __fastcall ClientHelperDetour(void* ctx, float* pos, float* normal, float halfWidth)
{
    const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (ModeOn()) {
        if (returnAddress == g_client.site1Ret)
            return false;
        if (returnAddress == g_client.site2Ret)
            return R2WallSweep(g_client, ctx, pos, kPostLatchHeight, normal, halfWidth, AllowedDist());
    }
    return g_client.original(ctx, pos, normal, halfWidth);
}

bool ReadableRange(std::uintptr_t address, std::size_t size)
{
    MEMORY_BASIC_INFORMATION info = {};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) || info.State != MEM_COMMIT)
        return false;
    return address + size <= reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
}

bool CallTargets(std::uintptr_t callAddress, std::uintptr_t target)
{
    if (!ReadableRange(callAddress, 5))
        return false;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(callAddress);
    if (bytes[0] != 0xE8)
        return false;
    const std::int32_t rel = *reinterpret_cast<const std::int32_t*>(bytes + 1);
    return callAddress + 5 + static_cast<std::intptr_t>(rel) == target;
}

bool Install(Hooked& h, const Build& b, std::uintptr_t base, void* detour)
{
    if (h.installed || !base)
        return h.installed;
    if (EnvOptOut()) {
        Msg("R1Delta wallrun: R2PORT_WALLRUN_R2=0, %s keeps the stock wall rays\n", b.module);
        return false;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        Warning("R1Delta wallrun: %s has no DOS header; stock wall rays kept\n", b.module);
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != b.timestamp
        || nt->OptionalHeader.SizeOfImage != b.imageSize) {
        Warning("R1Delta wallrun: %s is not the supported build (stamp 0x%08X size 0x%X); stock wall rays kept\n", b.module,
            nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
        return false;
    }
    const std::uintptr_t helper = base + b.helperRva;
    if (!ReadableRange(helper, sizeof(kHelperPrologue)) || std::memcmp(reinterpret_cast<const void*>(helper), kHelperPrologue, sizeof(kHelperPrologue)) != 0) {
        Warning("R1Delta wallrun: %s wall ray helper prologue mismatch; stock wall rays kept\n", b.module);
        return false;
    }
    if (!CallTargets(base + b.site1Call, helper) || !CallTargets(base + b.site2Call, helper)) {
        Warning("R1Delta wallrun: %s post latch call sites do not match; stock wall rays kept\n", b.module);
        return false;
    }

    h.build = &b;
    h.base = base;
    h.site1Ret = base + b.site1Call + 5;
    h.site2Ret = base + b.site2Call + 5;

    const MH_STATUS created = MH_CreateHook(reinterpret_cast<LPVOID>(helper), detour, reinterpret_cast<LPVOID*>(&h.original));
    if (created != MH_OK) {
        h.original = nullptr;
        Warning("R1Delta wallrun: cannot hook %s+0x%llX (%d); stock wall rays kept\n", b.module,
            static_cast<unsigned long long>(b.helperRva), static_cast<int>(created));
        return false;
    }
    // Created from a loader notification, after the global MH_EnableHook: enable at once.
    const MH_STATUS enabled = MH_EnableHook(reinterpret_cast<LPVOID>(helper));
    if (enabled != MH_OK && enabled != MH_ERROR_ENABLED) {
        MH_RemoveHook(reinterpret_cast<LPVOID>(helper));
        h.original = nullptr;
        Warning("R1Delta wallrun: cannot enable the %s hook (%d); stock wall rays kept\n", b.module, static_cast<int>(enabled));
        return false;
    }
    h.installed = true;
    Msg("R1Delta wallrun: R2 wall sweep installed in %s (r2port_wallrun_r2 / r2port_wallrun_allowed_wall_dist)\n", b.module);
    return true;
}

} // namespace

void RegisterConVars()
{
    if (g_registered)
        return;
    g_registered = true;
    g_modeVar = RegisterConVar("r2port_wallrun_r2", EnvOptOut() ? "0" : "1", FCVAR_REPLICATED,
        "1 = R2 wall check while wallrunning (one box sweep at origin + 16 must find a wall within r2port_wallrun_allowed_wall_dist of the hull), "
        "0 = stock R1 (two 36 unit line rays, detach when both miss). Needs the supported client.dll / server.dll build, otherwise it does nothing. "
        "Env R2PORT_WALLRUN_R2=0 stops the hook from being installed at all.");
    g_distVar = RegisterConVar("r2port_wallrun_allowed_wall_dist", "13", FCVAR_REPLICATED,
        "R2 wallrun_allowed_wall_dist: how far (units) the wall may be from the hull side before the wallrun ends (R2 default 13).");
}

void RegisterR1ODediConVars()
{
    if (!g_registered) {
        g_registered = true;
        g_modeVarR1O = RegisterR1ODediConVar("r2port_wallrun_r2", EnvOptOut() ? "0" : "1", FCVAR_REPLICATED,
            "1 = R2 wall check while wallrunning, 0 = stock R1 wall rays.");
        g_distVarR1O = RegisterR1ODediConVar("r2port_wallrun_allowed_wall_dist", "13", FCVAR_REPLICATED,
            "R2 wallrun_allowed_wall_dist (units, default 13).");
    }
}

void InstallServer(std::uintptr_t serverBase)
{
    Install(g_server, kServerBuild, serverBase, reinterpret_cast<void*>(&ServerHelperDetour));
}

void InstallClient(std::uintptr_t clientBase)
{
    Install(g_client, kClientBuild, clientBase, reinterpret_cast<void*>(&ClientHelperDetour));
}

} // namespace r2port::wallrun
