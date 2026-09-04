#include "surfacerender.h"
#include "vguisurface.h"
#include "localize.h"
#include "load.h"
#include "squirrel.h"
#include "engine/logging/logging.h"
#include "script_error_telemetry.h"
#include "persistentdata.h"
#include "antilag.h"
#include "reflex.h"

#include "r1d_version.h"
#include <vector>
#include <array>
#include <string>
#include <algorithm> // for std::max
#include <chrono>
#include <cmath>
#include <atomic>
#include <cstring>
#include <wincodec.h>
#include <psapi.h>

// Forward declare or include Vector type
#include "public/vector.h"

// This struct holds the information for one floating damage number
struct DamageNumber_t
{
    int damage;
    Vector worldPos;
    float spawnTime;
    bool isCritical;
    bool isKillShot;
    float batchWindow;
    int sourceID;
};

// Global list to hold all active damage numbers
std::vector<DamageNumber_t> g_DamageNumbers;

// Font handles for drawing
vgui::HFont DamageNumberFont = 0;
vgui::HFont DamageNumberCritFont = 0;

vgui::IPanel* panel = nullptr;
vgui::ISurface* surface = nullptr;

char fpsStringData[4096] = { 0 };
bool g_bIsDrawingFPSPanel = false;
__int64 (*osub_1800165C0)(
    __int64 a1,
    __int64 a2,
    __int64 a3,
    __int64 a4,
    int a5,
    unsigned int a6,
    unsigned int a7,
    int a8,
    const char* fmt,
    ...);
__int64 (*sub_180016490)(
    __int64 a1,
    __int64 a2,
    __int64 a3,
    __int64 a4,
    int a5,
    unsigned int a6,
    unsigned int a7,
    int a8,
    const char* fmt,
    va_list va);

__int64 sub_1800165C0(
    __int64 a1,
    __int64 a2,
    __int64 a3,
    __int64 a4,
    int a5,
    unsigned int a6,
    unsigned int a7,
    int a8,
    const char* fmt,
    ...)
{
    va_list args;
    va_start(args, fmt);

    if (!g_bIsDrawingFPSPanel) {
        // if we're not drawing the FPS panel, just call the real function
        auto result = sub_180016490(a1, a2, a3, a4, a5, a6, a7, a8, fmt, args);
        va_end(args);
        return result;
    }

    // otherwise, format into a temp buffer and append to fpsStringData
    char temp[4096];
    int len = vsnprintf(temp, sizeof(temp), (std::string(fmt) + std::string("\n")).c_str(), args);
    va_end(args);

    if (len > 2) {
        size_t cur = strnlen_s(fpsStringData, sizeof(fpsStringData));
        // how many bytes we can still copy (leave room for NUL)
        size_t space = sizeof(fpsStringData) - cur - 1;
        if (space > 0) {
            // append as much as will fit
            strncat_s(fpsStringData, sizeof(fpsStringData), temp, space);
        }
    }

    // we didn’t draw anything, so return 0
    return 0;
}
ConVarR1* cvar_delta_watermark = nullptr;
ConVarR1* cvar_delta_damage_numbers = nullptr;
ConVarR1* cvar_delta_damage_numbers_lifetime = nullptr;
ConVarR1* cvar_delta_damage_numbers_size = nullptr;
ConVarR1* cvar_delta_damage_numbers_crit_size = nullptr;
ConVarR1* cvar_delta_damage_numbers_batching = nullptr;
ConVarR1* cvar_delta_damage_numbers_batching_window = nullptr;
ConVarR1* cvar_cl_showfps = nullptr;
ConVarR1* cvar_cl_showpos = nullptr;

static bool g_SurfaceRenderHooksInitialized = false;
static bool g_WatermarkHudInitComplete = false;
static bool g_WatermarkProgressBarUpdated = false;

void MarkDeltaWatermarkProgressBarUpdated()
{
    g_WatermarkProgressBarUpdated = true;
}

static bool CanDrawDeltaWatermark()
{
    return g_SurfaceRenderHooksInitialized
        && g_WatermarkHudInitComplete
        && g_WatermarkProgressBarUpdated
        && surface
        && G_localizeIface
        && cvar_delta_watermark
        && cvar_delta_watermark->m_Value.m_nValue != 0;
}

extern ConCommandR1* RegisterConCommand(const char* commandName, void (*callback)(const CCommand&), const char* helpString, int flags);

static ConVarR1* GetClShowFpsCvar()
{
    if (!cvar_cl_showfps && cvarinterface && OriginalCCVar_FindVar)
        cvar_cl_showfps = OriginalCCVar_FindVar(cvarinterface, "cl_showfps");
    return cvar_cl_showfps;
}

static ConVarR1* GetClShowPosCvar()
{
    if (!cvar_cl_showpos && cvarinterface && OriginalCCVar_FindVar)
        cvar_cl_showpos = OriginalCCVar_FindVar(cvarinterface, "cl_showpos");
    return cvar_cl_showpos;
}

static void DeltaToggleDebugCommand(const CCommand& args)
{
    (void)args;

    auto clShowFps = GetClShowFpsCvar();
    auto clShowPos = GetClShowPosCvar();
    const int showFps = clShowFps ? clShowFps->m_Value.m_nValue : 0;
    const int showPos = clShowPos ? clShowPos->m_Value.m_nValue : 0;
    const int watermark = cvar_delta_watermark ? cvar_delta_watermark->m_Value.m_nValue : 1;

    if (showFps == 0 && showPos == 0 && watermark == 1)
    {
        Cbuf_AddText(0, "cl_showfps 1; cl_showpos 1; delta_watermark 1\n", 0);
    }
    else if (showFps == 1 && showPos == 1 && watermark == 1)
    {
        Cbuf_AddText(0, "cl_showfps 1; cl_showpos 1; delta_watermark 2\n", 0);
    }
    else
    {
        Cbuf_AddText(0, "cl_showfps 0; cl_showpos 0; delta_watermark 1\n", 0);
    }
}

struct WProfileEntry
{
    const char* categoryName;
    double pendingMs;
    double lastMs;
    double averageMs;
    double displayMs;
    double renderMs;
    int colorR;
    int colorG;
    int colorB;
    int displayOrder;
};

std::vector<WProfileEntry> g_WProfileEntries;
static int g_WProfileNextDisplayOrder = 0;
static bool g_WProfileDisplayOrderInitialized = false;
static uint64_t g_WProfileFrameIndex = 0;
static double g_WProfileLastFrameMs = 0.0;
static double g_WProfileAverageFrameMs = 0.0;
static thread_local int g_WProfileFrameDepth = 0;
static std::atomic<bool> g_WProfileEnabled{false};
static SRWLOCK g_WProfileLock = SRWLOCK_INIT;

static uint32_t WProfileHashEntry(const char* categoryName)
{
    uint32_t hash = 0;
    const char* text = categoryName ? categoryName : "";
    while (*text)
    {
        hash = hash * 31u + static_cast<unsigned char>(*text++);
    }
    return hash;
}

static void WProfilePickColor(uint32_t hash, int& r, int& g, int& b)
{
    const int color = static_cast<int>((hash & 0xAAAAAAu) + 0x444444u);
    r = (color >> 16) & 0xFF;
    g = (color >> 8) & 0xFF;
    b = color & 0xFF;
}

static WProfileEntry* WProfileFindOrAdd(const char* categoryName)
{
    for (auto& entry : g_WProfileEntries)
    {
        if (!_stricmp(entry.categoryName, categoryName))
            return &entry;
    }

    auto hash = WProfileHashEntry(categoryName);
    int colorR = 255;
    int colorG = 255;
    int colorB = 255;
    WProfilePickColor(hash, colorR, colorG, colorB);
    g_WProfileEntries.push_back({
        categoryName,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        colorR,
        colorG,
        colorB,
        g_WProfileNextDisplayOrder++
    });
    return &g_WProfileEntries.back();
}

static void WProfileRecord(const char* categoryName, double elapsedMs)
{
    if (!g_WProfileEnabled.load(std::memory_order_relaxed))
        return;

    SRWGuard profileGuard(&g_WProfileLock);
    auto* entry = WProfileFindOrAdd(categoryName);
    if (!entry)
        return;

    entry->pendingMs += elapsedMs;
}

static int WProfilePercentTenths(double ms, double totalMs)
{
    if (totalMs <= 0.001)
        return 0;
    return static_cast<int>(((std::max(0.0, ms) * 1000.0) / totalMs) + 0.5);
}

static void WProfileUpdateDisplaySamples()
{
    static double lastUpdateTime = 0.0;
    static double lastRenderTime = 0.0;
    static bool initialized = false;

    const double now = Plat_FloatTime();
    const double renderDelta = lastRenderTime > 0.0 ? std::max(0.0, now - lastRenderTime) : 0.0;
    lastRenderTime = now;

    if (!initialized || now - lastUpdateTime >= 0.10)
    {
        lastUpdateTime = now;
        for (auto& entry : g_WProfileEntries)
        {
            entry.displayMs = initialized ?
                (entry.displayMs * 0.75 + entry.averageMs * 0.25) :
                entry.averageMs;
        }
        initialized = true;
    }

    const double alpha = std::min(1.0, renderDelta * 18.0);
    for (auto& entry : g_WProfileEntries)
        entry.renderMs += (entry.displayMs - entry.renderMs) * alpha;
}

static void WProfileBeginFrame(double frameMs)
{
    SRWGuard profileGuard(&g_WProfileLock);
    ++g_WProfileFrameIndex;
    double profiledMs = 0.0;
    for (auto& entry : g_WProfileEntries)
        profiledMs += std::max(0.0, entry.pendingMs);

    g_WProfileLastFrameMs = frameMs > 0.001 ? frameMs : profiledMs;
    if (g_WProfileLastFrameMs < profiledMs)
        g_WProfileLastFrameMs = profiledMs;
    g_WProfileAverageFrameMs = g_WProfileFrameIndex <= 1 ?
        g_WProfileLastFrameMs :
        (g_WProfileAverageFrameMs * 0.90 + g_WProfileLastFrameMs * 0.10);

    for (auto& entry : g_WProfileEntries)
    {
        entry.lastMs = entry.pendingMs;
        entry.averageMs = g_WProfileFrameIndex <= 1 ?
            entry.pendingMs :
            (entry.averageMs * 0.90 + entry.pendingMs * 0.10);
        entry.pendingMs = 0.0;
    }
}

struct WProfileScope
{
    const char* categoryName;
    bool enabled;
    std::chrono::high_resolution_clock::time_point start;

    WProfileScope(const char* dllName, uintptr_t rva, const char* categoryName)
        : categoryName(categoryName),
          enabled(g_WProfileEnabled.load(std::memory_order_relaxed)),
          start(enabled
              ? std::chrono::high_resolution_clock::now()
              : std::chrono::high_resolution_clock::time_point{})
    {
        (void)dllName;
        (void)rva;
    }

    ~WProfileScope()
    {
        if (!enabled)
            return;

        const auto end = std::chrono::high_resolution_clock::now();
        const auto elapsed = std::chrono::duration<double, std::milli>(end - start).count();
        WProfileRecord(categoryName, elapsed);
    }
};

#define WPROF_CONCAT2(a, b) a##b
#define WPROF_CONCAT(a, b) WPROF_CONCAT2(a, b)
#define WPROF(x, y, z) WProfileScope WPROF_CONCAT(_wprofScope, __LINE__)(x, static_cast<uintptr_t>(y), z)

using HostRunFrameFn = void(*)(float);
using HostRunFrameInputFn = void(*)(float, bool);
using HostRunFrameBoolFn = void(*)(bool);
using HostRunFrameVoidFn = void(*)();
using HostRunFrameRetFn = __int64(*)(float);
using HostRunFrameBoolRetFn = __int64(*)(bool);
using HostRunFrameVoidRetFn = __int64(*)();
using HostRunFramePtrVoidFn = void(*)(void*);
using HostRunFramePtrRetFn = __int64(*)(void*);
using HostRunFrameFloatRetFn = __int64(*)(float);
using HostRunFramePtrFloatRetFn = __int64(*)(void*, float);
using HostRunFrameStringRetFn = __int64(*)(void*, const char*);
using NetRunFrameFn = __int64(*)(double);
using SleepFn = void(WINAPI*)(DWORD);
using MsgWaitForMultipleObjectsFn = DWORD(WINAPI*)(DWORD, const HANDLE*, BOOL, DWORD, DWORD);

static HostRunFrameRetFn oHost_RunFrame = nullptr;
static HostRunFrameInputFn o_Host_RunFrame_Input = nullptr;
static HostRunFrameBoolFn o_Host_RunFrame_Server = nullptr;
static HostRunFrameBoolRetFn o_Host_RunFrame_Client = nullptr;
static HostRunFrameVoidFn oCbuf_Execute_WProf = nullptr;
static NetRunFrameFn oNET_RunFrame = nullptr;
static HostRunFrameVoidRetFn oHost_RunFrame_Render = nullptr;
static HostRunFrameVoidRetFn oVCR_EnterPausedState = nullptr;
static HostRunFrameVoidRetFn oClientDLL_Update = nullptr;
static HostRunFramePtrRetFn oCJob_Execute = nullptr;
static HostRunFrameVoidRetFn oSV_FrameExecuteThreadDeferred = nullptr;
static HostRunFramePtrRetFn oCFrameTimer_MarkFrame = nullptr;
static HostRunFrameVoidRetFn oNET_SendQueuedPackets = nullptr;
static HostRunFrameStringRetFn oTestScriptMgr_CheckPoint = nullptr;
static HostRunFrameFloatRetFn oHost_AccumulateTime = nullptr;
static HostRunFrameVoidRetFn oHost_SetGlobalTime = nullptr;
static HostRunFramePtrRetFn oCMapReslistGenerator_RunFrame = nullptr;
static HostRunFramePtrRetFn oCLog_RunFrame = nullptr;
static HostRunFramePtrFloatRetFn oCDemoPlayer_MarkFrame = nullptr;
static HostRunFrameVoidRetFn oHost_ShowIPCCallCount = nullptr;
static HostRunFramePtrVoidFn oCEngine_Frame = nullptr;
static SleepFn oSleep_WProf = nullptr;
static MsgWaitForMultipleObjectsFn oMsgWaitForMultipleObjects_WProf = nullptr;

static __int64 WProf_Host_RunFrame(float time)
{
    ReflexBeginSimulation();
    PData_RunFrame();
    const bool profile = g_WProfileEnabled.load(std::memory_order_relaxed);
    if (profile)
        ++g_WProfileFrameDepth;
    const auto result = oHost_RunFrame(time);
    if (profile)
        --g_WProfileFrameDepth;
    PData_FinishPendingSave();
    ReflexOnEngineFrameComplete();
    return result;
}

static void WINAPI WProf_Sleep(DWORD milliseconds)
{
    if (g_WProfileFrameDepth <= 0
        || !g_WProfileEnabled.load(std::memory_order_relaxed))
    {
        oSleep_WProf(milliseconds);
        return;
    }

    const auto start = std::chrono::high_resolution_clock::now();
    oSleep_WProf(milliseconds);
    const auto end = std::chrono::high_resolution_clock::now();
    const auto elapsed = std::chrono::duration<double, std::milli>(end - start).count();
    WProfileRecord("sleep", elapsed);
}

static DWORD WINAPI WProf_MsgWaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds, DWORD wakeMask)
{
    if (g_WProfileFrameDepth <= 0
        || !g_WProfileEnabled.load(std::memory_order_relaxed))
        return oMsgWaitForMultipleObjects_WProf(count, handles, waitAll, milliseconds, wakeMask);

    const auto start = std::chrono::high_resolution_clock::now();
    const DWORD result = oMsgWaitForMultipleObjects_WProf(count, handles, waitAll, milliseconds, wakeMask);
    const auto end = std::chrono::high_resolution_clock::now();
    const auto elapsed = std::chrono::duration<double, std::milli>(end - start).count();
    WProfileRecord("sleep", elapsed);
    return result;
}

static void WProf_CEngine_Frame(void* engine)
{
    const bool profile = g_WProfileEnabled.load(std::memory_order_relaxed);
    if (profile)
        ++g_WProfileFrameDepth;
    oCEngine_Frame(engine);
    if (profile)
        --g_WProfileFrameDepth;
}

static void WProf__Host_RunFrame_Input(float accumulatedExtraSamples, bool finalTick)
{
    WPROF("engine.dll", 0x131320, "input");
    o_Host_RunFrame_Input(accumulatedExtraSamples, finalTick);
}

static void WProf__Host_RunFrame_Server(bool finalTick)
{
    WPROF("engine.dll", 0x0F0010, "server");
    o_Host_RunFrame_Server(finalTick);
}

static __int64 WProf__Host_RunFrame_Client(bool frameFinished)
{
    WPROF("engine.dll", 0x131480, "client");
    return o_Host_RunFrame_Client(frameFinished);
}

static void WProf_Cbuf_Execute()
{
    WPROF("engine.dll", 0x1057C0, "cmdExecute");
    oCbuf_Execute_WProf();
}

static __int64 WProf_NET_RunFrame(double realTime)
{
    WPROF("engine.dll", 0x1F31B0, "network");
    return oNET_RunFrame(realTime);
}

static __int64 WProf_NET_SendQueuedPackets()
{
    WPROF("engine.dll", 0x1F4B70, "network");
    return oNET_SendQueuedPackets();
}

static __int64 WProf_Host_RunFrame_Render()
{
    ReflexEndSimulationAndBeginRenderSubmit();
    WPROF("engine.dll", 0x133100, "render");
    return oHost_RunFrame_Render();
}

static __int64 WProf_VCR_EnterPausedState()
{
    WPROF("engine.dll", 0x1A5F70, "vcr");
    return oVCR_EnterPausedState();
}

static __int64 WProf_ClientDLL_Update()
{
    WPROF("engine.dll", 0x036F40, "clientDll");
    return oClientDLL_Update();
}

static __int64 WProf_CJob_Execute(void* job)
{
    WPROF("engine.dll", 0x132550, "asyncServer");
    return oCJob_Execute(job);
}

static __int64 WProf_SV_FrameExecuteThreadDeferred()
{
    WPROF("engine.dll", 0x0ED4D0, "server");
    return oSV_FrameExecuteThreadDeferred();
}

static __int64 WProf_CFrameTimer_MarkFrame(void* frameTimer)
{
    WPROF("engine.dll", 0x130C40, "frameAccounting");
    return oCFrameTimer_MarkFrame(frameTimer);
}

static __int64 WProf_TestScriptMgr_CheckPoint(void* testScriptMgr, const char* checkPoint)
{
    WPROF("engine.dll", 0x1AA9B0, "testScript");
    return oTestScriptMgr_CheckPoint(testScriptMgr, checkPoint);
}

static __int64 WProf_Host_AccumulateTime(float dt)
{
    WPROF("engine.dll", 0x132960, "time");
    return oHost_AccumulateTime(dt);
}

static __int64 WProf_Host_SetGlobalTime()
{
    WPROF("engine.dll", 0x132E50, "time");
    return oHost_SetGlobalTime();
}

static __int64 WProf_CMapReslistGenerator_RunFrame(void* generator)
{
    WPROF("engine.dll", 0x157CB0, "reslist");
    return oCMapReslistGenerator_RunFrame(generator);
}

static __int64 WProf_CLog_RunFrame(void* log)
{
    WPROF("engine.dll", 0x0EA4B0, "logging");
    return oCLog_RunFrame(log);
}

static __int64 WProf_CDemoPlayer_MarkFrame(void* demoPlayer, float frameTime)
{
    WPROF("engine.dll", 0x038F70, "demo");
    return oCDemoPlayer_MarkFrame(demoPlayer, frameTime);
}

static __int64 WProf_Host_ShowIPCCallCount()
{
    WPROF("engine.dll", 0x131550, "ipc");
    return oHost_ShowIPCCallCount();
}

static void CreateWProfileHook(uintptr_t engineBase, uintptr_t rva, void* hook, void** original)
{
    MH_CreateHook(reinterpret_cast<void*>(engineBase + rva), hook, original);
}

static bool CreateCheckedWProfileHook(
    uintptr_t engineBase,
    uintptr_t rva,
    const unsigned char* expected,
    size_t expectedSize,
    void* hook,
    void** original,
    const char* name)
{
    void* const target = reinterpret_cast<void*>(engineBase + rva);
    if (memcmp(target, expected, expectedSize) != 0)
    {
        char message[256]{};
        snprintf(
            message,
            sizeof(message),
            "[r1delta_wprofile] skipped %s at RVA 0x%llX: unexpected binary revision\n",
            name,
            static_cast<unsigned long long>(rva));
        OutputDebugStringA(message);
        return false;
    }

    const MH_STATUS createStatus = MH_CreateHook(target, hook, original);
    if ((createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
        || !original
        || !*original)
    {
        return false;
    }

    const MH_STATUS enableStatus = MH_EnableHook(target);
    return enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED;
}

static bool InstallHostFrameTimeFloorRemoval(uintptr_t engineBase)
{
    static bool installed = false;
    if (installed)
        return true;

    struct PatchSpec
    {
        uintptr_t rva;
        const char* name;
    };
    static constexpr PatchSpec patches[] = {
        { 0x132B94, "scaled frame-time floor" },
        { 0x132BFC, "unscaled frame-time floor" },
    };
    static constexpr unsigned char expectedContext[] = {
        0xF2, 0x0F, 0x5A, 0xDA, 0x0F, 0x14, 0xDB, 0x0F,
        0x5A, 0xC3, 0x66, 0x0F, 0x2F, 0xC1, 0x76, 0x06,
        0x0F, 0x14, 0xDB, 0x0F, 0x5A, 0xCB, 0xF2, 0x0F,
    };
    constexpr size_t branchOffsetInContext = 14;
    static constexpr unsigned char expectedBranch[] = { 0x76, 0x06 };
    static constexpr unsigned char replacement[] = { 0x90, 0x90 };

    for (const PatchSpec& patch : patches)
    {
        const void* const context = reinterpret_cast<const void*>(
            engineBase + patch.rva - branchOffsetInContext);
        if (std::memcmp(context, expectedContext, sizeof(expectedContext)) != 0)
        {
            char message[256]{};
            snprintf(
                message,
                sizeof(message),
                "[r1delta_timing] skipped %s at engine RVA 0x%llX: unexpected binary revision\n",
                patch.name,
                static_cast<unsigned long long>(patch.rva));
            OutputDebugStringA(message);
            return false;
        }
    }

    auto* const patchStart = reinterpret_cast<unsigned char*>(engineBase + patches[0].rva);
    constexpr size_t patchSpan = patches[1].rva + sizeof(replacement) - patches[0].rva;
    DWORD oldProtect = 0;
    if (!VirtualProtect(patchStart, patchSpan, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        OutputDebugStringA("[r1delta_timing] failed to make Host_AccumulateTime writable\n");
        return false;
    }

    // Both branches preserve the historical 100 ms upper bound. Falling through
    // only removes the 1 ms lower bound so simulation time remains real frame time.
    for (const PatchSpec& patch : patches)
        std::memcpy(reinterpret_cast<void*>(engineBase + patch.rva), replacement, sizeof(replacement));

    const bool flushed = FlushInstructionCache(GetCurrentProcess(), patchStart, patchSpan) != FALSE;
    DWORD ignoredProtect = 0;
    const bool restored = VirtualProtect(patchStart, patchSpan, oldProtect, &ignoredProtect) != FALSE;
    if (!flushed || !restored)
    {
        DWORD rollbackProtect = 0;
        if (VirtualProtect(patchStart, patchSpan, PAGE_EXECUTE_READWRITE, &rollbackProtect))
        {
            for (const PatchSpec& patch : patches)
                std::memcpy(reinterpret_cast<void*>(engineBase + patch.rva),
                    expectedBranch, sizeof(expectedBranch));
            FlushInstructionCache(GetCurrentProcess(), patchStart, patchSpan);
            DWORD rollbackIgnored = 0;
            VirtualProtect(patchStart, patchSpan, oldProtect, &rollbackIgnored);
        }
        OutputDebugStringA("[r1delta_timing] failed to publish Host_AccumulateTime floor removal\n");
        return false;
    }

    installed = true;
    OutputDebugStringA("[r1delta_timing] removed the 1 ms Host_AccumulateTime floor\n");
    return true;
}


static void SetupEngineWProfileHooks()
{
    const uintptr_t engineBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("engine.dll"));
    if (!engineBase)
        return;
    InstallHostFrameTimeFloorRemoval(engineBase);

    static constexpr unsigned char kHostRunFrameExpected[] = {
        0x48, 0x83, 0xEC, 0x48,
        0x80, 0x3D, 0x07, 0x1F, 0x79, 0x02, 0x00,
    };
    CreateCheckedWProfileHook(
        engineBase,
        0x135A00,
        kHostRunFrameExpected,
        sizeof(kHostRunFrameExpected),
        &WProf_Host_RunFrame,
        reinterpret_cast<void**>(&oHost_RunFrame),
        "host frame");
    CreateWProfileHook(engineBase, 0x131320, &WProf__Host_RunFrame_Input, reinterpret_cast<void**>(&o_Host_RunFrame_Input));
    CreateWProfileHook(engineBase, 0x0F0010, &WProf__Host_RunFrame_Server, reinterpret_cast<void**>(&o_Host_RunFrame_Server));
    CreateWProfileHook(engineBase, 0x131480, &WProf__Host_RunFrame_Client, reinterpret_cast<void**>(&o_Host_RunFrame_Client));
    CreateWProfileHook(engineBase, 0x1057C0, &WProf_Cbuf_Execute, reinterpret_cast<void**>(&oCbuf_Execute_WProf));
    CreateWProfileHook(engineBase, 0x1F31B0, &WProf_NET_RunFrame, reinterpret_cast<void**>(&oNET_RunFrame));
    CreateWProfileHook(engineBase, 0x1F4B70, &WProf_NET_SendQueuedPackets, reinterpret_cast<void**>(&oNET_SendQueuedPackets));
    static constexpr unsigned char kHostRunFrameRenderExpected[] = {
        0x40, 0x53,
        0x48, 0x83, 0xEC, 0x20,
        0x48, 0x8B, 0x05, 0xAB, 0x33, 0xD9, 0x02,
    };
    CreateCheckedWProfileHook(
        engineBase,
        0x133100,
        kHostRunFrameRenderExpected,
        sizeof(kHostRunFrameRenderExpected),
        &WProf_Host_RunFrame_Render,
        reinterpret_cast<void**>(&oHost_RunFrame_Render),
        "host render");
    CreateWProfileHook(engineBase, 0x1A5F70, &WProf_VCR_EnterPausedState, reinterpret_cast<void**>(&oVCR_EnterPausedState));
    CreateWProfileHook(engineBase, 0x036F40, &WProf_ClientDLL_Update, reinterpret_cast<void**>(&oClientDLL_Update));
    CreateWProfileHook(engineBase, 0x132550, &WProf_CJob_Execute, reinterpret_cast<void**>(&oCJob_Execute));
    CreateWProfileHook(engineBase, 0x0ED4D0, &WProf_SV_FrameExecuteThreadDeferred, reinterpret_cast<void**>(&oSV_FrameExecuteThreadDeferred));
    CreateWProfileHook(engineBase, 0x130C40, &WProf_CFrameTimer_MarkFrame, reinterpret_cast<void**>(&oCFrameTimer_MarkFrame));
    CreateWProfileHook(engineBase, 0x1AA9B0, &WProf_TestScriptMgr_CheckPoint, reinterpret_cast<void**>(&oTestScriptMgr_CheckPoint));
    CreateWProfileHook(engineBase, 0x132960, &WProf_Host_AccumulateTime, reinterpret_cast<void**>(&oHost_AccumulateTime));
    CreateWProfileHook(engineBase, 0x132E50, &WProf_Host_SetGlobalTime, reinterpret_cast<void**>(&oHost_SetGlobalTime));
    CreateWProfileHook(engineBase, 0x157CB0, &WProf_CMapReslistGenerator_RunFrame, reinterpret_cast<void**>(&oCMapReslistGenerator_RunFrame));
    CreateWProfileHook(engineBase, 0x0EA4B0, &WProf_CLog_RunFrame, reinterpret_cast<void**>(&oCLog_RunFrame));
    CreateWProfileHook(engineBase, 0x038F70, &WProf_CDemoPlayer_MarkFrame, reinterpret_cast<void**>(&oCDemoPlayer_MarkFrame));
    CreateWProfileHook(engineBase, 0x131550, &WProf_Host_ShowIPCCallCount, reinterpret_cast<void**>(&oHost_ShowIPCCallCount));
    CreateWProfileHook(engineBase, 0x1A1700, &WProf_CEngine_Frame, reinterpret_cast<void**>(&oCEngine_Frame));

    if (HMODULE kernel32 = GetModuleHandleA("kernel32.dll"))
    {
        if (void* sleepProc = reinterpret_cast<void*>(GetProcAddress(kernel32, "Sleep")))
            MH_CreateHook(sleepProc, &WProf_Sleep, reinterpret_cast<void**>(&oSleep_WProf));
    }
    if (HMODULE user32 = GetModuleHandleA("user32.dll"))
    {
        if (void* msgWaitProc = reinterpret_cast<void*>(GetProcAddress(user32, "MsgWaitForMultipleObjects")))
            MH_CreateHook(msgWaitProc, &WProf_MsgWaitForMultipleObjects, reinterpret_cast<void**>(&oMsgWaitForMultipleObjects_WProf));
    }
}

// Define the function pointer type for GetVectorInScreenSpace
typedef bool(*GetVectorInScreenSpace_t)(Vector, int&, int&, Vector*);
GetVectorInScreenSpace_t GetVectorInScreenSpace_ptr = nullptr;

__int64(*osub_18028BEA0)(__int64 a1, __int64 a2, double a3);
__int64 __fastcall sub_18028BEA0(__int64 a1, __int64 a2, double a3) {
    // Clear your buffer and read the new state
    memset(fpsStringData, 0, sizeof(fpsStringData));
    auto cl_showfps = GetClShowFpsCvar();
    auto cl_showpos = GetClShowPosCvar();
    const bool wantsFps = cl_showfps && cl_showfps->m_Value.m_nValue == 1;
    const bool wantsPos = wantsFps && cl_showpos && cl_showpos->m_Value.m_nValue == 1;
    bool isDrawing = wantsFps && CanDrawDeltaWatermark();
    g_bIsDrawingFPSPanel = isDrawing;
    // This static remembers what the last state was
    static bool wasDrawing = false;
    static auto vguimatsurface = GetModuleHandleA("vguimatsurface.dll");
    auto hookAddr = (LPVOID)(((uintptr_t)vguimatsurface) + 0x165C0);

    // Only change the hook when the state flips
    if (isDrawing != wasDrawing) {
        if (isDrawing) {
            MH_EnableHook(hookAddr);
        }
        else {
            MH_DisableHook(hookAddr);
        }
        wasDrawing = isDrawing;
    }

    // call the original
    auto ret = osub_18028BEA0(a1, a2, a3);

    // reset your flag if you’re doing one-shot draws, etc.
    g_bIsDrawingFPSPanel = false;
    return ret;
}

static constexpr int WatermarkGlyphTall = 8;
static constexpr int WatermarkLinePitch = 10;
static constexpr int WatermarkAtlasWide = 128;
static constexpr int WatermarkAtlasTall = 128;
static int g_WatermarkScale = 1;
static std::array<double, 512> g_FrameTimesMs = {};
static size_t g_FrameTimeWriteIndex = 0;
static double g_LastWatermarkTime = 0.0;
static int g_WatermarkTexture = -1;
static bool g_WatermarkTextureLoaded = false;
static int g_WatermarkCharWidths[256] = {};
static int g_WhiteTexture = -1;
static size_t g_PrivateWorkingSetHighWaterBytes = 0;
static vgui::HFont g_LegacyWatermarkFont = 0;
static vgui::HFont g_LegacyWatermarkSmallFont = 0;
static bool g_DeltaWatermarkModeOneFun = false;
static constexpr int LegacyWatermarkBaseWide = 1024;
static constexpr int LegacyWatermarkBaseTall = 768;

static float LegacyWatermarkScale(int screenWidth, int screenHeight)
{
    if (screenWidth <= 0 || screenHeight <= 0)
        return 1.0f;

    const float horizontalScale = static_cast<float>(screenWidth) / LegacyWatermarkBaseWide;
    const float verticalScale = static_cast<float>(screenHeight) / LegacyWatermarkBaseTall;
    return std::min(horizontalScale, verticalScale);
}

static int ScaleLegacyWatermarkValue(int value, float scale)
{
    return std::max(1, static_cast<int>(std::lround(value * scale)));
}

static bool RollDeltaWatermarkModeOneFun()
{
    LARGE_INTEGER counter = {};
    QueryPerformanceCounter(&counter);

    uintptr_t entropy = static_cast<uintptr_t>(counter.QuadPart);
    entropy ^= static_cast<uintptr_t>(GetTickCount64());
    entropy ^= static_cast<uintptr_t>(GetCurrentProcessId()) << 17;
    entropy ^= static_cast<uintptr_t>(GetCurrentThreadId()) << 29;
    entropy ^= reinterpret_cast<uintptr_t>(&entropy);

    entropy ^= entropy >> 33;
    entropy *= static_cast<uintptr_t>(0xff51afd7ed558ccdULL);
    entropy ^= entropy >> 33;
    entropy *= static_cast<uintptr_t>(0xc4ceb9fe1a85ec53ULL);
    entropy ^= entropy >> 33;

    return entropy % 1000 == 0;
}

static const char* VersionWithoutPrefix()
{
    const char* version = R1D_DISPLAY_VERSION;
    while (*version == 'v' || *version == 'V')
        ++version;
    return version;
}

static bool HasCapturedFpsText()
{
    for (const char* ch = fpsStringData; *ch; ++ch)
    {
        if (*ch != '\r' && *ch != '\n' && *ch != '\t' && *ch != ' ')
            return true;
    }
    return false;
}

static char* TrimAnsiLine(char* text)
{
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
        ++text;

    char* end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        *--end = '\0';

    return text;
}

static void NormalizeFpsHeaderLine(const char* line, char* out, size_t outSize)
{
    if (!line || !out || outSize == 0)
        return;

    out[0] = '\0';
    const size_t len = strlen(line);
    if (len >= 4 && !_stricmp(line + len - 3, "fps") && line[len - 4] != ' ')
    {
        snprintf(out, outSize, "%.*s fps", static_cast<int>(len - 3), line);
        return;
    }

    snprintf(out, outSize, "%s", line);
}

static bool ParseClientDelayTicksLine(const char* line, int* outTicks)
{
    if (!line || !outTicks)
        return false;

    const char* cursor = line;
    if (_strnicmp(cursor, "CL_Frames", 9) && _strnicmp(cursor, "CL Frames", 9))
        return false;

    cursor += 9;
    while (*cursor == ' ' || *cursor == '\t')
        ++cursor;
    if (*cursor != ':')
        return false;
    ++cursor;

    while (*cursor == ' ' || *cursor == '\t')
        ++cursor;

    char* end = nullptr;
    const long ticks = strtol(cursor, &end, 10);
    if (end == cursor)
        return false;

    *outTicks = static_cast<int>(ticks);
    return true;
}

static bool ParseShowPosLine(const char* line, float* outX, float* outY, float* outZ)
{
    if (!line || !outX || !outY || !outZ)
        return false;
    if (_strnicmp(line, "pos:", 4))
        return false;

    const char* cursor = line + 4;
    while (*cursor == ' ' || *cursor == '\t')
        ++cursor;

    return sscanf_s(cursor, "%f %f %f", outX, outY, outZ) == 3;
}

static bool IsShowPosPanelLine(const char* line)
{
    if (!line)
        return false;

    return !_strnicmp(line, "name:", 5) ||
        !_strnicmp(line, "pos:", 4) ||
        !_strnicmp(line, "ang:", 4) ||
        !_strnicmp(line, "vel:", 4);
}

static bool ResolveWatermarkAtlasPath(char* path, size_t pathSize)
{
    if (!path || pathSize == 0)
        return false;

    HMODULE tier0Module = GetModuleHandleA("tier0.dll");
    if (tier0Module)
    {
        char modulePath[MAX_PATH];
        if (GetModuleFileNameA(tier0Module, modulePath, sizeof(modulePath)))
        {
            char* slash = strrchr(modulePath, '\\');
            if (slash)
            {
                slash[1] = '\0';
                snprintf(path, pathSize, "%sminecraft_default.png", modulePath);
                if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
                    return true;
            }
        }
    }

    snprintf(path, pathSize, "r1delta\\bin_delta\\minecraft_default.png");
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void BuildWatermarkCharWidths(const std::vector<unsigned char>& rgba)
{
    for (int i = 0; i < 256; ++i)
    {
        const int xt = i % 16;
        const int yt = i / 16;
        int x = 7;
        while (x >= 0)
        {
            bool emptyColumn = true;
            for (int y = 0; y < WatermarkGlyphTall && emptyColumn; ++y)
            {
                const int px = xt * WatermarkGlyphTall + x;
                const int py = yt * WatermarkGlyphTall + y;
                const size_t offset = (static_cast<size_t>(py) * WatermarkAtlasWide + px) * 4;
                if (rgba[offset + 3] > 0)
                    emptyColumn = false;
            }

            if (!emptyColumn)
                break;

            --x;
        }

        if (i == ' ')
            x = 2;

        g_WatermarkCharWidths[i] = x + 2;
    }
}

static bool LoadPngRgba(const char* path, std::vector<unsigned char>& outRgba, int& outWide, int& outTall)
{
    wchar_t widePath[MAX_PATH];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath, _countof(widePath)))
        return false;

    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE)
        return false;

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr))
        hr = factory->CreateDecoderFromFilename(widePath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr))
        hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr))
        hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

    UINT wide = 0, tall = 0;
    if (SUCCEEDED(hr))
        hr = converter->GetSize(&wide, &tall);

    if (SUCCEEDED(hr))
    {
        outRgba.resize(static_cast<size_t>(wide) * tall * 4);
        hr = converter->CopyPixels(nullptr, wide * 4, static_cast<UINT>(outRgba.size()), outRgba.data());
    }

    if (converter)
        converter->Release();
    if (frame)
        frame->Release();
    if (decoder)
        decoder->Release();
    if (factory)
        factory->Release();
    if (SUCCEEDED(initResult))
        CoUninitialize();

    if (FAILED(hr))
        return false;

    outWide = static_cast<int>(wide);
    outTall = static_cast<int>(tall);
    return true;
}

static bool LoadWatermarkTexture()
{
    if (g_WatermarkTextureLoaded)
        return true;

    char atlasPath[MAX_PATH];
    std::vector<unsigned char> rgba;
    int wide = 0, tall = 0;
    if (!ResolveWatermarkAtlasPath(atlasPath, sizeof(atlasPath)) || !LoadPngRgba(atlasPath, rgba, wide, tall))
        return false;
    if (wide != WatermarkAtlasWide || tall != WatermarkAtlasTall)
        return false;

    BuildWatermarkCharWidths(rgba);
    g_WatermarkTexture = surface->CreateNewTextureID(true);
    surface->DrawSetTextureRGBA(g_WatermarkTexture, rgba.data(), wide, tall);
    g_WatermarkTextureLoaded = surface->IsTextureIDValid(g_WatermarkTexture);
    return g_WatermarkTextureLoaded;
}

static int MinecraftGuiScale(int screenWidth, int screenHeight)
{
    int scale = 1;
    while (screenWidth / (scale + 1) >= 320 && screenHeight / (scale + 1) >= 240)
        ++scale;
    return scale;
}

static void DrawWatermarkTextRaw(int x, int y, int r, int g, int b, int a, const wchar_t* text)
{
    if (!text || !LoadWatermarkTexture())
        return;

    surface->DrawSetColor(r, g, b, a);
    surface->DrawSetTexture(g_WatermarkTexture);

    int cursorX = x;
    while (*text)
    {
        const wchar_t ch = *text++;
        if (ch == L'\r')
            continue;

        unsigned int atlasIndex = ch <= 255 ? static_cast<unsigned int>(ch) : static_cast<unsigned int>('?');
        if (atlasIndex < 32)
            continue;

        const int cellX = static_cast<int>(atlasIndex % 16) * WatermarkGlyphTall;
        const int cellY = static_cast<int>(atlasIndex / 16) * WatermarkGlyphTall;
        const float s0 = static_cast<float>(cellX) / WatermarkAtlasWide;
        const float t0 = static_cast<float>(cellY) / WatermarkAtlasTall;
        const float s1 = static_cast<float>(cellX + WatermarkGlyphTall) / WatermarkAtlasWide;
        const float t1 = static_cast<float>(cellY + WatermarkGlyphTall) / WatermarkAtlasTall;
        const int glyphWide = WatermarkGlyphTall * g_WatermarkScale;

        surface->DrawTexturedSubRect(cursorX, y, cursorX + glyphWide, y + WatermarkGlyphTall * g_WatermarkScale, s0, t0, s1, t1);
        cursorX += g_WatermarkCharWidths[atlasIndex] * g_WatermarkScale;
    }
}

static int MeasureWatermarkText(const wchar_t* text)
{
    if (!text || !LoadWatermarkTexture())
        return 0;

    int wide = 0;
    while (*text)
    {
        const wchar_t ch = *text++;
        if (ch == L'\r')
            continue;

        unsigned int atlasIndex = ch <= 255 ? static_cast<unsigned int>(ch) : static_cast<unsigned int>('?');
        if (atlasIndex < 32)
            continue;

        wide += g_WatermarkCharWidths[atlasIndex] * g_WatermarkScale;
    }
    return wide;
}

static void DrawWatermarkText(int x, int y, int r, int g, int b, int a, const wchar_t* text)
{
    const int shadowOffset = std::max(1, g_WatermarkScale);
    DrawWatermarkTextRaw(x + shadowOffset, y + shadowOffset, r / 4, g / 4, b / 4, a, text);
    DrawWatermarkTextRaw(x, y, r, g, b, a, text);
}

static void DrawWatermarkAnsi(int x, int y, int r, int g, int b, int a, const char* text)
{
    wchar_t wide[512];
    G_localizeIface->ConvertANSIToUnicode(text, wide, sizeof(wide));
    DrawWatermarkText(x, y, r, g, b, a, wide);
}

static void DrawWatermarkAnsiRight(int rightX, int y, int r, int g, int b, int a, const char* text)
{
    wchar_t wide[512];
    G_localizeIface->ConvertANSIToUnicode(text, wide, sizeof(wide));
    DrawWatermarkText(rightX - MeasureWatermarkText(wide), y, r, g, b, a, wide);
}

static int MeasureWatermarkAnsi(const char* text)
{
    wchar_t wide[512];
    G_localizeIface->ConvertANSIToUnicode(text ? text : "", wide, sizeof(wide));
    return MeasureWatermarkText(wide);
}

static bool EnsureWhiteTexture()
{
    if (g_WhiteTexture >= 0 && surface->IsTextureIDValid(g_WhiteTexture))
        return true;

    static const unsigned char whitePixel[4] = { 255, 255, 255, 255 };
    g_WhiteTexture = surface->CreateNewTextureID(true);
    surface->DrawSetTextureRGBA(g_WhiteTexture, whitePixel, 1, 1);
    return surface->IsTextureIDValid(g_WhiteTexture);
}

static void DrawFilledTriangle(int x0, int y0, int x1, int y1, int x2, int y2)
{
    if (!EnsureWhiteTexture())
        return;

    surface->DrawSetTexture(g_WhiteTexture);
    vgui::Vertex_t verts[3] = {
        vgui::Vertex_t(Vector2D(static_cast<float>(x0), static_cast<float>(y0))),
        vgui::Vertex_t(Vector2D(static_cast<float>(x1), static_cast<float>(y1))),
        vgui::Vertex_t(Vector2D(static_cast<float>(x2), static_cast<float>(y2)))
    };
    surface->DrawTexturedPolygon(3, verts);
}

static void DrawLagometer(int x, int y, int wide, int tall)
{
    constexpr double kTargetFrameMs = 1000.0 / 60.0;
    constexpr double kPixelsPerMs = 5.0; // Minecraft 1.0 divides nanoseconds by 200000.
    const int targetTall = static_cast<int>(kTargetFrameMs * kPixelsPerMs + 0.5);
    const int graphBottom = y + tall;
    const int graphTop = y;
    const int targetY = graphBottom - targetTall;
    const int secondTargetY = graphBottom - targetTall * 2;

    surface->DrawSetColor(0, 0, 0, 32);
    surface->DrawFilledRect(x, std::max(graphTop, targetY), x + wide, graphBottom);
    surface->DrawSetColor(32, 0, 0, 32);
    surface->DrawFilledRect(x, std::max(graphTop, secondTargetY), x + wide, std::max(graphTop, targetY));

    double averageMs = 0.0;
    int averageCount = 0;
    const int count = static_cast<int>(std::min(g_FrameTimesMs.size(), static_cast<size_t>(std::max(0, wide))));
    for (int i = 0; i < count; ++i)
    {
        const size_t sampleIndex = (g_FrameTimeWriteIndex + g_FrameTimesMs.size() - count + i) % g_FrameTimesMs.size();
        if (g_FrameTimesMs[sampleIndex] > 0.0)
        {
            averageMs += g_FrameTimesMs[sampleIndex];
            ++averageCount;
        }
    }

    if (averageCount > 0)
    {
        averageMs /= averageCount;
        const int averageTall = std::min(tall, std::max(1, static_cast<int>(averageMs * kPixelsPerMs + 0.5)));
        surface->DrawSetColor(64, 0, 0, 32);
        surface->DrawFilledRect(x, graphBottom - averageTall, x + wide, graphBottom);
    }

    for (int i = 0; i < count; ++i)
    {
        const size_t sampleIndex = (g_FrameTimeWriteIndex + g_FrameTimesMs.size() - count + i) % g_FrameTimesMs.size();
        const double ms = g_FrameTimesMs[sampleIndex];
        if (ms <= 0.0)
            continue;

        int faded = (i * 255) / std::max(1, count);
        faded = (faded * faded) / 255;
        faded = (faded * faded) / 255;

        const int barTall = std::min(tall, std::max(1, static_cast<int>(ms * kPixelsPerMs + 0.5)));
        const int barX = x + i;
        if (ms > kTargetFrameMs)
            surface->DrawSetColor(faded, 0, 0, 255);
        else
            surface->DrawSetColor(0, faded, 0, 255);
        surface->DrawLine(barX, graphBottom - barTall, barX, graphBottom);
    }
}

static void DrawProfilerPie(int x, int y, int radius)
{
    SRWGuard profileGuard(&g_WProfileLock);
    WProfileUpdateDisplaySamples();

    double profiledMs = 0.0;
    for (const auto& entry : g_WProfileEntries)
    {
        if (!_stricmp(entry.categoryName, "sleep"))
            continue;
        profiledMs += std::max(0.0, entry.renderMs);
    }

    const double totalMs = profiledMs;
    if (totalMs <= 0.001)
        return;

    std::vector<WProfileEntry*> sortedEntries;
    sortedEntries.reserve(g_WProfileEntries.size());
    for (auto& entry : g_WProfileEntries)
    {
        if (!_stricmp(entry.categoryName, "sleep"))
            continue;
        sortedEntries.push_back(&entry);
    }

    if (!g_WProfileDisplayOrderInitialized)
    {
        std::sort(sortedEntries.begin(), sortedEntries.end(), [totalMs](const WProfileEntry* lhs, const WProfileEntry* rhs) {
            const int lhsPct = WProfilePercentTenths(lhs->renderMs, totalMs);
            const int rhsPct = WProfilePercentTenths(rhs->renderMs, totalMs);
            if (lhsPct != rhsPct)
                return lhsPct > rhsPct;
            return _stricmp(lhs->categoryName, rhs->categoryName) < 0;
        });
        g_WProfileDisplayOrderInitialized = true;
    }
    else
    {
        std::sort(sortedEntries.begin(), sortedEntries.end(), [](const WProfileEntry* lhs, const WProfileEntry* rhs) {
            if (lhs->displayOrder != rhs->displayOrder)
                return lhs->displayOrder < rhs->displayOrder;
            return _stricmp(lhs->categoryName, rhs->categoryName) < 0;
        });

        bool swapped = true;
        while (swapped)
        {
            swapped = false;
            for (size_t i = 1; i < sortedEntries.size(); ++i)
            {
                const int abovePct = WProfilePercentTenths(sortedEntries[i - 1]->renderMs, totalMs);
                const int currentPct = WProfilePercentTenths(sortedEntries[i]->renderMs, totalMs);
                if (currentPct > abovePct + 20)
                {
                    std::swap(sortedEntries[i - 1], sortedEntries[i]);
                    swapped = true;
                }
            }
        }
    }

    for (size_t i = 0; i < sortedEntries.size(); ++i)
        sortedEntries[i]->displayOrder = static_cast<int>(i);

    constexpr size_t kMaxProfilerLegendEntries = 8;
    std::vector<WProfileEntry*> legendEntries;
    legendEntries.reserve(std::min(sortedEntries.size(), kMaxProfilerLegendEntries));
    for (auto* entry : sortedEntries)
    {
        if (WProfilePercentTenths(entry->renderMs, totalMs) <= 0)
            continue;
        legendEntries.push_back(entry);
        if (legendEntries.size() >= kMaxProfilerLegendEntries)
            break;
    }

    const int unit = std::max(1, g_WatermarkScale);
    const int legendPitch = WatermarkLinePitch * unit;
    int screenWidth = 0;
    int screenHeight = 0;
    surface->GetScreenSize(screenWidth, screenHeight);

    const int sideOffset = 8 * unit;

    int maxNameWide = MeasureWatermarkAnsi("root");
    for (const auto* entry : legendEntries)
        maxNameWide = std::max(maxNameWide, MeasureWatermarkAnsi(entry->categoryName));
    const int percentWide = MeasureWatermarkAnsi("100.00%");
    const int legendGap = 10 * unit;
    const int right = std::min(screenWidth - 2 * unit, x + static_cast<int>(radius * 1.1) + 8 * unit);
    const int minPanelWide = static_cast<int>(radius * 2.2) + 16 * unit;
    const int legendPanelWide = maxNameWide + percentWide + legendGap + 8 * unit;
    const int left = std::max(0, right - std::max(minPanelWide, legendPanelWide));
    x = (left + right) / 2;
    const int legendLeft = left + 4 * unit;
    const int legendRight = right - 4 * unit;
    const int initialLegendTop = y + static_cast<int>(radius * 0.5) + 20 * unit;
    const int legendRows = static_cast<int>(legendEntries.size()) + 1;
    const int initialBottom = std::max(y + static_cast<int>(radius * 1.55), initialLegendTop + legendRows * legendPitch + 4 * unit);
    if (initialBottom > screenHeight - 2 * unit)
        y -= initialBottom - (screenHeight - 2 * unit);

    const int top = y - static_cast<int>(radius * 0.52) - 16 * unit;
    const int legendTop = y + static_cast<int>(radius * 0.5) + 20 * unit;
    const int bottom = std::max(y + static_cast<int>(radius * 1.55), legendTop + legendRows * legendPitch + 4 * unit);

    surface->DrawSetColor(0, 0, 0, 200);
    surface->DrawFilledRect(left, top, right, bottom);

    const double pi = 3.14159265358979323846;
    const double verticalScale = 0.42;

    auto ellipseX = [&](double percent) {
        const double angle = percent * pi * 2.0 / 100.0;
        return x + static_cast<int>(std::sin(angle) * radius);
    };

    auto ellipseY = [&](double percent) {
        const double angle = percent * pi * 2.0 / 100.0;
        return y - static_cast<int>(std::cos(angle) * radius * verticalScale);
    };

    auto drawSliceTop = [&](double startPercent, double percent, int r, int g, int b) {
        if (percent <= 0.001)
            return;

        const int steps = std::max(static_cast<int>(percent / 4.0) + 1, static_cast<int>(radius * percent / 48.0));
        surface->DrawSetColor(r, g, b, 255);
        int lastX = ellipseX(startPercent);
        int lastY = ellipseY(startPercent);
        for (int i = 1; i <= steps; ++i)
        {
            const double slicePercent = startPercent + percent * (static_cast<double>(i) / steps);
            const int ex = ellipseX(slicePercent);
            const int ey = ellipseY(slicePercent);
            DrawFilledTriangle(x, y, lastX, lastY, ex, ey);
            lastX = ex;
            lastY = ey;
        }
    };

    auto drawSliceSide = [&](double startPercent, double percent, int r, int g, int b) {
        if (percent <= 0.001)
            return;

        const double endPercent = startPercent + percent;
        const double frontStart = std::max(startPercent, 25.0);
        const double frontEnd = std::min(endPercent, 75.0);
        if (frontEnd <= frontStart)
            return;

        surface->DrawSetColor((r & 0xFE) >> 1, (g & 0xFE) >> 1, (b & 0xFE) >> 1, 255);

        int x0 = ellipseX(frontStart);
        int x1 = ellipseX(frontEnd);
        if (x0 > x1)
            std::swap(x0, x1);

        for (int columnX = x0; columnX <= x1; ++columnX)
        {
            const double dx = static_cast<double>(columnX - x);
            const double radiusSquared = static_cast<double>(radius) * static_cast<double>(radius);
            const double inside = std::max(0.0, radiusSquared - dx * dx);
            const int columnY = y + static_cast<int>(std::sqrt(inside) * verticalScale + 0.5);
            surface->DrawLine(columnX, columnY, columnX, columnY + sideOffset);
        }
    };

    double startPercent = 0.0;
    for (const auto* entry : sortedEntries)
    {
        const double ms = std::max(0.0, entry->renderMs);
        const double percent = (ms / totalMs) * 100.0;
        drawSliceTop(startPercent, percent, entry->colorR, entry->colorG, entry->colorB);
        startPercent += percent;
    }

    startPercent = 0.0;
    for (const auto* entry : sortedEntries)
    {
        const double ms = std::max(0.0, entry->renderMs);
        const double percent = (ms / totalMs) * 100.0;
        drawSliceSide(startPercent, percent, entry->colorR, entry->colorG, entry->colorB);
        startPercent += percent;
    }

    char rootPercent[32];
    const double accountedTotalMs = g_WProfileAverageFrameMs > 0.001 ? g_WProfileAverageFrameMs : totalMs;
    const double accountedPercent = std::min(100.0, std::max(0.0, (profiledMs / accountedTotalMs) * 100.0));
    snprintf(rootPercent, sizeof(rootPercent), "%.2f%%", accountedPercent);
    DrawWatermarkAnsi(legendLeft, y - static_cast<int>(radius * 0.42) - 16 * unit, 255, 255, 255, 255, "root");
    DrawWatermarkAnsiRight(legendRight, y - static_cast<int>(radius * 0.42) - 16 * unit, 255, 255, 255, 255, rootPercent);

    int legendY = legendTop;
    for (const auto* entry : legendEntries)
    {
        char name[160];
        char percent[32];
        snprintf(name, sizeof(name), "%s", entry->categoryName);
        snprintf(percent, sizeof(percent), "%.2f%%", (std::max(0.0, entry->renderMs) / totalMs) * 100.0);
        DrawWatermarkAnsi(legendLeft, legendY, entry->colorR, entry->colorG, entry->colorB, 255, name);
        DrawWatermarkAnsiRight(legendRight, legendY, entry->colorR, entry->colorG, entry->colorB, 255, percent);
        legendY += WatermarkLinePitch * unit;
        if (legendY + legendPitch > bottom)
            break;
    }
}

static void DrawLegacyWatermark()
{
    int screenWidth, screenHeight;
    surface->GetScreenSize(screenWidth, screenHeight);
    const float scale = LegacyWatermarkScale(screenWidth, screenHeight);
    const int fontTall = ScaleLegacyWatermarkValue(14, scale);
    const int smallFontTall = ScaleLegacyWatermarkValue(12, scale);

    if (!g_LegacyWatermarkFont) {
        g_LegacyWatermarkFont = surface->CreateFont();
        surface->SetFontGlyphSet(g_LegacyWatermarkFont, "Verdana", fontTall, 650, 0, 0, vgui::FONTFLAG_DROPSHADOW | vgui::FONTFLAG_ANTIALIAS);
    }
    if (!g_LegacyWatermarkSmallFont) {
        g_LegacyWatermarkSmallFont = surface->CreateFont();
        surface->SetFontGlyphSet(g_LegacyWatermarkSmallFont, "Verdana", smallFontTall, 100, 0, 0, vgui::FONTFLAG_DROPSHADOW | vgui::FONTFLAG_ANTIALIAS);
    }

    char ansiBuffer1[512];
    snprintf(ansiBuffer1, sizeof(ansiBuffer1), "R1Delta %s", R1D_DISPLAY_VERSION);
    wchar_t watermarkText1[512];
    G_localizeIface->ConvertANSIToUnicode(ansiBuffer1, watermarkText1, sizeof(watermarkText1));

    const char* urlString = "For testing purposes only.";
    const char* secondData = urlString;
    if (fpsStringData[0] != '\x00') {
        secondData = fpsStringData;
    }
    wchar_t watermarkText2[4096];
    G_localizeIface->ConvertANSIToUnicode(secondData, watermarkText2, sizeof(watermarkText2));

    int text1Wide, text1Tall;
    surface->GetTextSize(g_LegacyWatermarkFont, watermarkText1, text1Wide, text1Tall);

    int maxLineWidth = text1Wide;
    int totalTextHeight = text1Tall;
    const int smallFontLineSpacing = ScaleLegacyWatermarkValue(1, scale);
    const int headingGap = ScaleLegacyWatermarkValue(2, scale);

    wchar_t watermarkText2Copy[4096];
    wcsncpy_s(watermarkText2Copy, sizeof(watermarkText2Copy) / sizeof(wchar_t), watermarkText2, _TRUNCATE);

    wchar_t* ctx = nullptr;
    wchar_t* line = wcstok_s(watermarkText2Copy, L"\n", &ctx);
    int numSmallFontLines = 0;

    while (line) {
        if (wcslen(line) == 0) {
            line = wcstok_s(nullptr, L"\n", &ctx);
            continue;
        }

        int lineWide, lineTall;
        surface->GetTextSize(g_LegacyWatermarkSmallFont, line, lineWide, lineTall);

        maxLineWidth = std::max(maxLineWidth, lineWide);
        if (numSmallFontLines == 0) {
            totalTextHeight += headingGap;
        }
        else {
            totalTextHeight += smallFontLineSpacing;
        }
        totalTextHeight += lineTall;

        numSmallFontLines++;
        line = wcstok_s(nullptr, L"\n", &ctx);
    }
    maxLineWidth /= 2;

    const int textPadding = ScaleLegacyWatermarkValue(5, scale);
    int bgRectX0 = screenWidth - maxLineWidth - textPadding * 2;
    int bgRectY0 = 0;
    int bgRectX1 = screenWidth;
    int bgRectY1 = totalTextHeight + textPadding;
    bgRectX0 = std::max(0, bgRectX0);

    surface->DrawSetColor(0, 0, 0, 120);
    surface->DrawFilledRect(bgRectX0, bgRectY0, bgRectX1, bgRectY1);

    int effectCounter = 1;
    int fadeWidth = text1Wide - ScaleLegacyWatermarkValue(10, scale);
    fadeWidth = std::max(ScaleLegacyWatermarkValue(50, scale), fadeWidth);
    const int fadeTail = ScaleLegacyWatermarkValue(50, scale);
    int fadeStartX = screenWidth - maxLineWidth - textPadding * 2 - fadeWidth;
    fadeStartX = std::max(0, fadeStartX);

    for (int i = fadeWidth; i > -fadeTail; --i)
    {
        int alpha = 120 - static_cast<int>(effectCounter * 2.0f / scale);
        alpha = std::max(0, alpha);
        surface->DrawSetColor(0, 0, 0, alpha);

        int lineX0 = fadeStartX + i;
        if (lineX0 < 0) continue;
        if (lineX0 >= bgRectX0) continue;

        int lineY0 = 0;
        int lineX1 = lineX0 + 1;
        int lineY1 = bgRectY1;
        surface->DrawFilledRect(lineX0, lineY0, lineX1, lineY1);
        effectCounter++;
    }

    surface->DrawSetTextFont(g_LegacyWatermarkFont);
    surface->DrawSetTextColor(255, 128, 0, 255);
    int text1X = screenWidth - text1Wide - textPadding;
    int text1Y = textPadding / 2;
    surface->DrawSetTextPos(text1X, text1Y);
    surface->DrawPrintText(watermarkText1, wcslen(watermarkText1));

    surface->DrawSetTextFont(g_LegacyWatermarkSmallFont);
    surface->DrawSetTextColor(255, 128, 0, 220);

    int currentY = text1Tall + headingGap;

    ctx = nullptr;
    line = wcstok_s(watermarkText2, L"\n", &ctx);

    while (line) {
        if (wcslen(line) == 0) {
            line = wcstok_s(nullptr, L"\n", &ctx);
            continue;
        }

        int lineWide, lineTall;
        surface->GetTextSize(g_LegacyWatermarkSmallFont, line, lineWide, lineTall);

        int lineX = screenWidth - lineWide - textPadding;

        surface->DrawSetTextPos(lineX, currentY);
        surface->DrawPrintText(line, wcslen(line));

        currentY += lineTall + smallFontLineSpacing;

        line = wcstok_s(nullptr, L"\n", &ctx);
    }
}

static void DrawCurrentWatermark() {
    const bool showHeavyDebug =
        cvar_delta_watermark && cvar_delta_watermark->m_Value.m_nValue == 2;
    g_WProfileEnabled.store(showHeavyDebug, std::memory_order_relaxed);
    if (!CanDrawDeltaWatermark())
        return;

    int screenWidth, screenHeight;
    surface->GetScreenSize(screenWidth, screenHeight);
    g_WatermarkScale = MinecraftGuiScale(screenWidth, screenHeight);

    static bool boundDebugToggle = false;
    if (!boundDebugToggle)
    {
        Cbuf_AddText(0, "bind f3 delta_toggle_debug\n", 0);
        boundDebugToggle = true;
    }

    if (!cvar_delta_watermark->m_Value.m_nValue) return;

    const double now = Plat_FloatTime();
    if (g_LastWatermarkTime > 0.0 && showHeavyDebug)
    {
        const double frameMs = (now - g_LastWatermarkTime) * 1000.0;
        WProfileBeginFrame(frameMs);
        g_FrameTimesMs[g_FrameTimeWriteIndex] = frameMs;
        g_FrameTimeWriteIndex = (g_FrameTimeWriteIndex + 1) % g_FrameTimesMs.size();
    }
    g_LastWatermarkTime = now;

    auto clShowFps = GetClShowFpsCvar();
    auto clShowPos = GetClShowPosCvar();
    const bool wantsFps = clShowFps && clShowFps->m_Value.m_nValue == 1;
    const bool wantsPos = wantsFps && clShowPos && clShowPos->m_Value.m_nValue == 1;
    const bool hasCapturedFpsText = HasCapturedFpsText();
    const bool showDebug = wantsFps;
    char header[512];
    char firstFpsLine[128] = {};
    int clientDelayTicks = -1;
    float showPosX = 0.0f;
    float showPosY = 0.0f;
    float showPosZ = 0.0f;
    bool hasShowPos = false;
    if (wantsFps && hasCapturedFpsText)
    {
        char fpsHeaderData[sizeof(fpsStringData)];
        strcpy_s(fpsHeaderData, fpsStringData);
        char* ctx = nullptr;
        char* line = strtok_s(fpsHeaderData, "\n", &ctx);
        while (line)
        {
            char* trimmedLine = TrimAnsiLine(line);
            if (*trimmedLine)
            {
                int parsedTicks = 0;
                float parsedX = 0.0f;
                float parsedY = 0.0f;
                float parsedZ = 0.0f;
                if (wantsPos && ParseShowPosLine(trimmedLine, &parsedX, &parsedY, &parsedZ))
                {
                    showPosX = parsedX;
                    showPosY = parsedY;
                    showPosZ = parsedZ;
                    hasShowPos = true;
                }
                else if (wantsFps && ParseClientDelayTicksLine(trimmedLine, &parsedTicks))
                {
                    clientDelayTicks = parsedTicks;
                }
                else if (wantsFps && !IsShowPosPanelLine(trimmedLine) && !firstFpsLine[0])
                {
                    NormalizeFpsHeaderLine(trimmedLine, firstFpsLine, sizeof(firstFpsLine));
                }
            }
            line = strtok_s(nullptr, "\n", &ctx);
        }
    }

    if (firstFpsLine[0] && clientDelayTicks >= 0)
        snprintf(header, sizeof(header), "R1Delta %s (%s, %d client delay ticks)", VersionWithoutPrefix(), firstFpsLine, clientDelayTicks);
    else if (firstFpsLine[0])
        snprintf(header, sizeof(header), "R1Delta %s (%s)", VersionWithoutPrefix(), firstFpsLine);
    else
        snprintf(header, sizeof(header), "R1Delta %s - http://r1delta.net", VersionWithoutPrefix());

    const int unit = g_WatermarkScale;
    const int linePitch = WatermarkLinePitch * unit;
    int y = 2 * unit;
    DrawWatermarkAnsi(2 * unit, y, 255, 255, 255, 255, header);
    y += linePitch;

    if (!showDebug)
        return;

    MEMORYSTATUSEX memoryStatus = {};
    memoryStatus.dwLength = sizeof(memoryStatus);
    PROCESS_MEMORY_COUNTERS_EX2 processMemory = {};
    processMemory.cb = sizeof(processMemory);
    if (hasCapturedFpsText &&
        GlobalMemoryStatusEx(&memoryStatus) &&
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&processMemory), sizeof(processMemory)))
    {
        const int rightX = screenWidth - 2 * unit;
        int memoryY = 2 * unit;
        g_PrivateWorkingSetHighWaterBytes = std::max(g_PrivateWorkingSetHighWaterBytes, processMemory.PrivateWorkingSetSize);

        const size_t processUsedMb = static_cast<size_t>(processMemory.PrivateWorkingSetSize / 1024 / 1024);
        const uint64_t totalPhysMb = memoryStatus.ullTotalPhys / 1024ULL / 1024ULL;
        const uint64_t usedMemoryPercent = memoryStatus.ullTotalPhys > 0 ?
            (processMemory.PrivateWorkingSetSize * 100ULL) / memoryStatus.ullTotalPhys : 0;
        char memoryLine[160];
        snprintf(memoryLine, sizeof(memoryLine), "Used memory: %llu%c (%zuMB) of %lluMB",
            usedMemoryPercent, '%', processUsedMb, totalPhysMb);
        DrawWatermarkAnsiRight(rightX, memoryY, 255, 255, 255, 255, memoryLine);
        memoryY += linePitch;

        const uint64_t allocatedPercent = memoryStatus.ullTotalPhys > 0 ?
            (static_cast<uint64_t>(g_PrivateWorkingSetHighWaterBytes) * 100ULL) / memoryStatus.ullTotalPhys : 0;
        snprintf(memoryLine, sizeof(memoryLine), "Allocated memory: %llu%c (%zuMB)",
            allocatedPercent, '%',
            g_PrivateWorkingSetHighWaterBytes / 1024 / 1024);
        DrawWatermarkAnsiRight(rightX, memoryY, 255, 255, 255, 255, memoryLine);
    }

    if (hasCapturedFpsText)
    {
        char fpsLines[sizeof(fpsStringData)];
        strcpy_s(fpsLines, fpsStringData);
        char* ctx = nullptr;
        char* line = strtok_s(fpsLines, "\n", &ctx);
        bool skippedHeaderFpsLine = false;
        bool skippedClientDelayLine = false;
        while (line)
        {
            char* trimmedLine = TrimAnsiLine(line);
            if (*trimmedLine)
            {
                int parsedTicks = 0;
                if (!skippedClientDelayLine && ParseClientDelayTicksLine(trimmedLine, &parsedTicks))
                {
                    skippedClientDelayLine = true;
                }
                else if (wantsPos && IsShowPosPanelLine(trimmedLine))
                {
                }
                else if (wantsFps && skippedHeaderFpsLine)
                {
                    DrawWatermarkAnsi(2 * unit, y, 255, 255, 255, 255, trimmedLine);
                    y += linePitch;
                }
                else if (wantsFps)
                {
                    skippedHeaderFpsLine = true;
                }
            }
            line = strtok_s(nullptr, "\n", &ctx);
        }
    }

    if (wantsPos && hasShowPos)
    {
        y += linePitch;

        char posLine[96];
        snprintf(posLine, sizeof(posLine), "x: %.2f", showPosX);
        DrawWatermarkAnsi(2 * unit, y, 255, 255, 255, 255, posLine);
        y += linePitch;

        snprintf(posLine, sizeof(posLine), "y: %.2f", showPosY);
        DrawWatermarkAnsi(2 * unit, y, 255, 255, 255, 255, posLine);
        y += linePitch;

        snprintf(posLine, sizeof(posLine), "z: %.2f", showPosZ);
        DrawWatermarkAnsi(2 * unit, y, 255, 255, 255, 255, posLine);
        y += linePitch;
    }

    if (showHeavyDebug)
    {
        const int lagometerWide = std::min(512, screenWidth);
        const int lagometerTall = 170;
        DrawLagometer(0, screenHeight - lagometerTall, lagometerWide, lagometerTall);

        const int pieRadius = 40 * unit;
        DrawProfilerPie(screenWidth - pieRadius - 10 * unit, screenHeight - pieRadius * 2, pieRadius);
    }
}

void DrawWatermark() {
    if (!CanDrawDeltaWatermark())
        return;

    const int watermarkMode = cvar_delta_watermark->m_Value.m_nValue;
    if (watermarkMode == 1 && !g_DeltaWatermarkModeOneFun)
    {
        DrawLegacyWatermark();
        return;
    }

    DrawCurrentWatermark();
}
void DrawDamageNumbers()
{
    if (!cvar_delta_damage_numbers || !cvar_delta_damage_numbers->m_Value.m_nValue)
        return;

    if (!GetVectorInScreenSpace_ptr) // Make sure the pointer is valid
        return;

    // Initialize fonts if they haven't been already
    if (!DamageNumberFont)
    {
        DamageNumberFont = surface->CreateFont();
        surface->SetFontGlyphSet(DamageNumberFont, "ConduitITCPro-Medium", cvar_delta_damage_numbers_size->m_Value.m_nValue, 800, 0, 0, vgui::FONTFLAG_ANTIALIAS);
    }
    if (!DamageNumberCritFont)
    {
        DamageNumberCritFont = surface->CreateFont();
        surface->SetFontGlyphSet(DamageNumberCritFont, "ConduitITCPro-Medium", cvar_delta_damage_numbers_crit_size->m_Value.m_nValue, 900, 0, 0, vgui::FONTFLAG_ANTIALIAS);
    }

    float currentTime = Plat_FloatTime();
    float lifeTime = cvar_delta_damage_numbers_lifetime->m_Value.m_fValue;

    // Iterate backwards so we can safely erase elements
    for (int i = g_DamageNumbers.size() - 1; i >= 0; --i)
    {
        auto& item = g_DamageNumbers[i];

        // Remove expired numbers
        if (currentTime > item.spawnTime + lifeTime)
        {
            g_DamageNumbers.erase(g_DamageNumbers.begin() + i);
            continue;
        }

        // Batching logic (similar to TF2)
        if (cvar_delta_damage_numbers_batching && cvar_delta_damage_numbers_batching->m_Value.m_nValue &&
            item.batchWindow > 0.f && item.sourceID != -1 && i > 0)
        {
            // Check if previous item is from same source and within batch window
            auto& prevItem = g_DamageNumbers[i - 1];
            float timeDiff = item.spawnTime - prevItem.spawnTime;

            if (timeDiff <= item.batchWindow && prevItem.sourceID == item.sourceID)
            {
                // Merge this damage into the previous one
                prevItem.damage += item.damage;
                // Reset the spawn time so the batched number appears fresh and starts animating from the bottom
                prevItem.spawnTime = item.spawnTime;
                // Also update the position to the new hit location
                prevItem.worldPos = item.worldPos;

                prevItem.isCritical = prevItem.isCritical || item.isCritical;
                prevItem.isKillShot = prevItem.isKillShot || item.isKillShot;

                g_DamageNumbers.erase(g_DamageNumbers.begin() + i);
                continue;
            }
        }

        float lifeFrac = (currentTime - item.spawnTime) / lifeTime;

        // Apply exponential ease-in curve: slow start, fast end
        float easedLifeFrac = powf(lifeFrac, 2.5f);

        // Animate position upwards with exponential easing
        Vector animatedWorldPos = item.worldPos;
        animatedWorldPos.z += easedLifeFrac * 40.0f; // Moves 40 units up over its lifetime

        int x, y;
        if (!GetVectorInScreenSpace_ptr(animatedWorldPos, x, y, nullptr))
        {
            continue; // Not on screen
        }

        // Calculate fade-out alpha with exponential curve (starts fading after 50% lifetime)
        int alpha = 255;
        if (lifeFrac > 0.5f)
        {
            // Exponential fade: slow fade at first, then fast fade at the end
            float fadeLifeFrac = (lifeFrac - 0.5f) * 2.0f; // 0 to 1 over the fade period
            float easedFade = powf(fadeLifeFrac, 3.0f); // Ease-in: slow then fast (adjust power for steepness)
            alpha = static_cast<int>(255.0f * (1.0f - easedFade));
            alpha = std::max(0, alpha); // Clamp
        }

        // Prepare text and font
        wchar_t wBuf[16];
        swprintf(wBuf, 16, L"%d", item.damage);

        vgui::HFont currentFont = item.isCritical ? DamageNumberCritFont : DamageNumberFont;

        // Get text size first
        int textWide, textTall;
        surface->GetTextSize(currentFont, wBuf, textWide, textTall);

        // Center the text
        x -= textWide / 2;
        y -= textTall / 2;

        // Set font
        surface->DrawSetTextFont(currentFont);

        // Draw the outline by rendering text multiple times with offset
        int outlineOffsets[][2] = {
            {-1, -1}, {0, -1}, {1, -1},
            {-1,  0},          {1,  0},
            {-1,  1}, {0,  1}, {1,  1}
        };

        if (item.isCritical)
            surface->DrawSetTextColor(255, 128, 0, alpha);
        else
            surface->DrawSetTextColor(255, 0, 0, alpha);
        for (int j = 0; j < 8; j++)
        {
            surface->DrawSetTextPos(x + outlineOffsets[j][0], y + outlineOffsets[j][1]);
            surface->DrawPrintText(wBuf, wcslen(wBuf));
        }

        if (item.isKillShot)
            surface->DrawSetTextColor(0, 0, 0, alpha);
        else
            surface->DrawSetTextColor(255, 255, 255, alpha);

        surface->DrawSetTextPos(x, y);
        surface->DrawPrintText(wBuf, wcslen(wBuf));
    }
}

constexpr size_t SCRIPT_ERROR_STATE_COUNT = static_cast<size_t>(ScriptErrorTelemetry::VmContext::Count);
ConVarR1* cvar_delta_script_errors_notification = nullptr;
vgui::HFont ScriptErrorNotificationFont = 0;
vgui::HTexture ScriptErrorWarningTexture = 0;

struct DisplayedScriptError
{
    uint64_t sequence = 0;
    float receivedAt = 0.0f;
};

std::array<DisplayedScriptError, SCRIPT_ERROR_STATE_COUNT> DisplayedScriptErrors{};

void DrawScriptErrors() {
    if (!ScriptErrorNotificationFont) {
        ScriptErrorNotificationFont = surface->CreateFont();
        surface->SetFontGlyphSet(ScriptErrorNotificationFont, "Tahoma", 13, 800, 0, 0, vgui::FONTFLAG_ANTIALIAS);
    }
    if (!ScriptErrorWarningTexture) {
        ScriptErrorWarningTexture = surface->CreateNewTextureID();
        surface->DrawSetTextureFile(ScriptErrorWarningTexture, "ui/menu/r1delta/error", 0, false);
    }

    if (!cvar_delta_script_errors_notification || !cvar_delta_script_errors_notification->m_Value.m_nValue) return;

    int idealy = 32;
    constexpr int height = 30;
    const float now = Plat_FloatTime();
    const float endTime = now - 10.0f;
    const float recent = now - 0.5f;

    constexpr ScriptErrorTelemetry::VmContext contexts[SCRIPT_ERROR_STATE_COUNT] = {
        ScriptErrorTelemetry::VmContext::Server,
        ScriptErrorTelemetry::VmContext::Client,
        ScriptErrorTelemetry::VmContext::Ui
    };
    const wchar_t* contextNames[SCRIPT_ERROR_STATE_COUNT] = { L"Server", L"Client", L"UI" };

    for (size_t i = 0; i < SCRIPT_ERROR_STATE_COUNT; ++i) {
        ScriptErrorTelemetry::NotificationSnapshot snapshot;
        const bool hasSnapshot = ScriptErrorTelemetry::GetNotificationSnapshot(contexts[i], snapshot);
        DisplayedScriptError& displayed = DisplayedScriptErrors[i];
        if (hasSnapshot && snapshot.sequence != displayed.sequence) {
            displayed.sequence = snapshot.sequence;
            displayed.receivedAt = now;
        }
        if (displayed.receivedAt == 0.0f)
            continue;
        if (displayed.receivedAt < endTime) {
            displayed.receivedAt = 0.0f;
            continue;
        }

        int x = 32;
        int y = idealy;
        wchar_t text[128]{};
        if (hasSnapshot && snapshot.sequence == displayed.sequence && snapshot.hasResult) {
            wchar_t code[53]{};
            for (size_t codeIndex = 0; codeIndex + 1 < sizeof(code) / sizeof(code[0]) && snapshot.code[codeIndex]; ++codeIndex)
                code[codeIndex] = static_cast<unsigned char>(snapshot.code[codeIndex]);
            swprintf_s(text, sizeof(text) / sizeof(text[0]), L"Internal %ls script error (#%ls%ls)",
                contextNames[i], code, snapshot.isNew ? L"!" : L"");
        }
        else {
            swprintf_s(text, sizeof(text) / sizeof(text[0]), L"Internal %ls script error", contextNames[i]);
        }

        int textWidth, textHeight;
        surface->GetTextSize(ScriptErrorNotificationFont, text, textWidth, textHeight);
        int width = textWidth + 48;

        surface->DrawSetColor(40, 40, 40, 255);
        surface->DrawFilledRect(x + 2, y + 2, x + 2 + width, y + 2 + height);
        surface->DrawSetColor(240, 240, 240, 255);
        surface->DrawFilledRect(x, y, x + width, y + height);

        if (displayed.receivedAt > recent) {
            surface->DrawSetColor(255, 200, 0, static_cast<int>((displayed.receivedAt - recent) * 510.0f));
            surface->DrawFilledRect(x, y, x + width, y + height);
        }

        surface->DrawSetTextFont(ScriptErrorNotificationFont);
        surface->DrawSetTextColor(90, 90, 90, 255);
        surface->DrawSetTextPos(x + 34, y + 8);
        surface->DrawPrintText(text, wcslen(text));

        surface->DrawSetColor(255, 255, 255, 150 + sinf(y + now * 30) * 100);
        surface->DrawSetTexture(ScriptErrorWarningTexture);
        surface->DrawTexturedRect(x + 6, y + 6, x + 6 + 16, y + 6 + 16);

        idealy += 40;
    }
}

void (*oPaintTraverse)(uintptr_t thisptr, vgui::VPANEL panel, bool forceRepaint, bool allowForce);
void PaintTraverse(uintptr_t thisptr, vgui::VPANEL paintPanel, bool forceRepaint, bool allowForce) {
	static vgui::VPANEL inGameRenderPanel = 0, menuRenderPanel = 0;

	oPaintTraverse(thisptr, paintPanel, forceRepaint, allowForce);

	if (!inGameRenderPanel) {
		if (!strcmp_static(panel->GetName(paintPanel), "MatSystemTopPanel")) {
            inGameRenderPanel = paintPanel;
		}
	}
    if (!menuRenderPanel) {
        if (!strcmp_static(panel->GetName(paintPanel), "CBaseModPanel")) {
            menuRenderPanel = paintPanel;
        }
    }

    if (paintPanel == inGameRenderPanel)
    {
        if (CanDrawDeltaWatermark())
            DrawWatermark();
        DrawDamageNumbers();
    }
    if (paintPanel == inGameRenderPanel || paintPanel == menuRenderPanel) DrawScriptErrors();
}

static int s_ClientScriptErrorDetailBudget = 32;
uint64_t(*oOnClientScriptErrorHook)(uintptr_t sqstate);
uint64_t OnClientScriptErrorHook(uintptr_t sqstate) {
    uintptr_t intobj = *(uintptr_t*)(sqstate + 0xE8);
    const char* stateName = (const char*)(intobj + 0x4174);
    ScriptErrorTelemetry::VmContext context = ScriptErrorTelemetry::VmContext::Server;
    if (stateName && stateName[0] == 'C')
        context = ScriptErrorTelemetry::VmContext::Client;
    else if (stateName && stateName[0] == 'U')
        context = ScriptErrorTelemetry::VmContext::Ui;
    ScriptErrorTelemetry::BeginErrorBlock(context);

    if (s_ClientScriptErrorDetailBudget > 0) {
        --s_ClientScriptErrorDetailBudget;
        const char* err = "<unknown>";
        int stackTop = -1;
        int stackBase = -1;
        int errType = 0;
        __try {
            stackBase = *reinterpret_cast<int*>(sqstate + 84);
            stackTop = *reinterpret_cast<int*>(sqstate + 80);
            uintptr_t obj = *reinterpret_cast<uintptr_t*>(sqstate + 48) + 16ull * static_cast<unsigned int>(stackBase + 1);
            errType = *reinterpret_cast<int*>(obj);
            if (errType == 134217744)
                err = reinterpret_cast<const char*>(*reinterpret_cast<uintptr_t*>(obj + 8) + 56ull);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            err = "<exception reading script error>";
        }

        char buffer[1024];
        _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
            "R1Delta: client script error detail state=%s sqvm=%p intobj=%p top=%d base=%d errType=0x%x err=\"%s\" budget=%d\n",
            stateName ? stateName : "<null>", reinterpret_cast<void*>(sqstate), reinterpret_cast<void*>(intobj),
            stackTop, stackBase, errType, err ? err : "<null>", s_ClientScriptErrorDetailBudget);
        OutputDebugStringA(buffer);
    }

    const uint64_t result = oOnClientScriptErrorHook(sqstate);
    ScriptErrorTelemetry::EndErrorBlock(context);
    return result;
}

void(*oOnScreenSizeChanged)(uintptr_t thisptr, int w, int h);
void OnScreenSizeChanged(uintptr_t thisptr, int w, int h) {
    oOnScreenSizeChanged(thisptr, w, h);
    g_LegacyWatermarkFont = g_LegacyWatermarkSmallFont = 0;
    ScriptErrorNotificationFont = DamageNumberFont = DamageNumberCritFont = 0;
}

__int64(*oCPluginHudMessage_ctor)(uintptr_t thisptr, uintptr_t panel);
__int64 CPluginHudMessage_ctor(uintptr_t thisptr, uintptr_t ppanel) {
    auto engineVgui = ((void* (__fastcall*)())(G_engine + 0x21E670))();
    const __int64 result = oCPluginHudMessage_ctor(thisptr, (*(__int64(__fastcall**)(void*, int))(*(_QWORD*)engineVgui + 8LL))(engineVgui, 5));
    g_WatermarkHudInitComplete = true;
    return result;
}

extern ConVarR1* RegisterConVar(const char* name, const char* value, int flags, const char* helpString);

// Squirrel script function to add a damage number
SQInteger Script_AddDamageNumber(HSQUIRRELVM v) {
    if (g_DamageNumbers.size() > 50) // Prevent spam
        return 0;

    auto r1_vm = GetClientVMPtr();

    SQFloat damage;
    Vector pos;
    SQBool isCritical;
    SQBool isKillShot;
    SQInteger sourceID = -1;

    // Arg 2: damage (float)
    if (SQ_FAILED(sq_getfloat(r1_vm, v, 2, &damage)))
        return sq_throwerror(v, "Invalid argument 1: expected float damage");

    // Arg 3, 4, 5: position (vector as 3 floats)
    if (SQ_FAILED(sq_getfloat(r1_vm, v, 3, &pos.x)) ||
        SQ_FAILED(sq_getfloat(r1_vm, v, 4, &pos.y)) ||
        SQ_FAILED(sq_getfloat(r1_vm, v, 5, &pos.z)))
        return sq_throwerror(v, "Invalid argument 2,3,4: expected vector position");

    // Arg 6: isCritical (bool)
    if (SQ_FAILED(sq_getbool(r1_vm, v, 6, &isCritical)))
        return sq_throwerror(v, "Invalid argument 5: expected bool isCritical");

    // Arg 7: isKillShot (bool)
    if (SQ_FAILED(sq_getbool(r1_vm, v, 7, &isKillShot)))
        return sq_throwerror(v, "Invalid argument 6: expected bool isKillShot");

    // Arg 8: sourceID (optional int) - for batching
    sq_getinteger(r1_vm, v, 8, &sourceID);

    // Block damage numbers at or very close to the origin (invalid position)
    const float EPSILON = 0.1f;
    if (fabs(pos.x) < EPSILON && fabs(pos.y) < EPSILON && fabs(pos.z) < EPSILON)
        return 0; // Reject this damage number

    // Add the damage number to the global list
    DamageNumber_t dmgNum;
    dmgNum.damage = (int)damage;
    dmgNum.worldPos = pos;
    dmgNum.spawnTime = Plat_FloatTime();
    dmgNum.isCritical = isCritical != 0;
    dmgNum.isKillShot = isKillShot != 0;

    // Set batching parameters
    dmgNum.batchWindow = cvar_delta_damage_numbers_batching && cvar_delta_damage_numbers_batching->m_Value.m_nValue
                          ? cvar_delta_damage_numbers_batching_window->m_Value.m_fValue
                          : 0.f;
    dmgNum.sourceID = sourceID;

    g_DamageNumbers.push_back(dmgNum);

    return 0;
}
void FontSizeChangeCallback(IConVar* var, const char* pOldValue, float flOldValue) {
    DamageNumberFont = 0;
    DamageNumberCritFont = 0;

}

static void WatermarkModeChangeCallback(
    void* var,
    const char* oldValue,
    float oldFloatValue)
{
    (void)var;
    (void)oldValue;
    (void)oldFloatValue;
    g_WProfileEnabled.store(
        cvar_delta_watermark
            && cvar_delta_watermark->m_Value.m_nValue == 2,
        std::memory_order_relaxed);
}

void SetupSurfaceRenderHooks() {
    g_SurfaceRenderHooksInitialized = false;
    g_WatermarkHudInitComplete = true;
    g_DeltaWatermarkModeOneFun = false; //RollDeltaWatermarkModeOneFun();
    cvar_delta_watermark = RegisterConVar("delta_watermark", "1", FCVAR_GAMEDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show R1Delta watermark with version information");
    RegisterReflexConVars();
    RegisterAntiLagConVars();
    cvar_delta_damage_numbers = RegisterConVar("delta_damage_numbers", "0", FCVAR_GAMEDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show TF2-style floating damage numbers on hit.");
    cvar_delta_damage_numbers_lifetime = RegisterConVar("delta_damage_numbers_lifetime", "1.5", FCVAR_GAMEDLL, "How long damage numbers stay on screen.");
    cvar_delta_damage_numbers_size = RegisterConVar("delta_damage_numbers_size", "32", FCVAR_GAMEDLL, "Font size for normal damage numbers.");
    cvar_delta_damage_numbers_crit_size = RegisterConVar("delta_damage_numbers_crit_size", "36", FCVAR_GAMEDLL, "Font size for critical damage numbers.");
    cvar_delta_damage_numbers_batching = RegisterConVar("delta_damage_numbers_batching", "1", FCVAR_GAMEDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Batch damage numbers from the same source within a time window.");
    cvar_delta_damage_numbers_batching_window = RegisterConVar("delta_damage_numbers_batching_window", "3", FCVAR_GAMEDLL, "Time window for batching damage numbers (seconds).");
    RegisterConCommand("delta_toggle_debug", DeltaToggleDebugCommand, "Cycle R1Delta debug overlay modes.", FCVAR_CLIENTDLL);
    cvar_delta_watermark->m_fnChangeCallbacks.AddToTail(
        static_cast<FnChangeCallback_t>(WatermarkModeChangeCallback));
    cvar_delta_damage_numbers_size->m_fnChangeCallbacks.AddToTail((FnChangeCallback_t)FontSizeChangeCallback);
    cvar_delta_damage_numbers_crit_size->m_fnChangeCallbacks.AddToTail((FnChangeCallback_t)FontSizeChangeCallback);

	auto vguimatsurface = GetModuleHandleA("vguimatsurface.dll");
	auto vguimatsurface_CreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(vguimatsurface, "CreateInterface"));
    surface = (vgui::ISurface*)vguimatsurface_CreateInterface("VGUI_Surface031", 0);

	auto vgui = GetModuleHandleA("vgui2.dll");
	auto vgui_CreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(vgui, "CreateInterface"));
	panel = (vgui::IPanel*)vgui_CreateInterface("VGUI_Panel009", 0);

    sub_180016490 = (decltype(sub_180016490))(((uintptr_t)(vguimatsurface)) + 0x16490);
	MH_CreateHook(GetVFunc<LPVOID>(panel, 46), &PaintTraverse, reinterpret_cast<LPVOID*>(&oPaintTraverse));
    MH_CreateHook((LPVOID)(((uintptr_t)(vguimatsurface)) + 0x165C0), &sub_1800165C0, reinterpret_cast<LPVOID*>(&osub_1800165C0));
    MH_CreateHook((LPVOID)(((uintptr_t)(G_client)) + 0x28BEA0), &sub_18028BEA0, reinterpret_cast<LPVOID*>(&osub_18028BEA0));
    MH_CreateHook((LPVOID)(((uintptr_t)(vguimatsurface)) + 0x119E0), &OnScreenSizeChanged, reinterpret_cast<LPVOID*>(&oOnScreenSizeChanged));
    MH_CreateHook((LPVOID)(((uintptr_t)(G_engine)) + 0x5E860), &CPluginHudMessage_ctor, reinterpret_cast<LPVOID*>(&oCPluginHudMessage_ctor));
    SetupEngineWProfileHooks();

    // Initialize GetVectorInScreenSpace function pointer
    GetVectorInScreenSpace_ptr = reinterpret_cast<GetVectorInScreenSpace_t>(G_client + 0x0105170);

    g_SurfaceRenderHooksInitialized = true;
}

void SetupSquirrelErrorNotificationHooks() {
    ScriptErrorTelemetry::Initialize();
    cvar_delta_script_errors_notification = RegisterConVar("delta_script_errors_notification", "1", FCVAR_GAMEDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show a notification whenever a script error occurs");

    auto launcher = (uintptr_t)GetModuleHandleA("launcher.dll");
    MH_CreateHook((LPVOID)(launcher + 0x3A5E0), &OnClientScriptErrorHook, reinterpret_cast<LPVOID*>(&oOnClientScriptErrorHook));
}
