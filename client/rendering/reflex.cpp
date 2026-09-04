#include "reflex.h"
#include "antilag.h"
#include "gpu_latency.h"

#include "core.h"
#include "cvar.h"

#include <MinHook.h>
#include <Windows.h>
#include <d3d11.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace
{
constexpr int kNvApiOk = 0;
constexpr std::uint32_t kNvApiInitializeId = 0x0150E828;
constexpr std::uint32_t kNvApiSetSleepModeId = 0xAC1CA9E0;
constexpr std::uint32_t kNvApiSleepId = 0x852CD1D2;
constexpr std::uint32_t kNvApiSetLatencyMarkerId = 0xD9984C05;
constexpr std::size_t kSubmittedFrameCapacity = 64;
constexpr std::int32_t kInputEventButtonPressed = 0;
constexpr std::int32_t kInputEventButtonDoubleClicked = 2;
constexpr std::int32_t kMouseLeftButtonCode = 107;

using NvBool = std::uint8_t;

enum class NvLatencyMarkerType : std::int32_t
{
    SimulationStart = 0,
    SimulationEnd = 1,
    RenderSubmitStart = 2,
    RenderSubmitEnd = 3,
    PresentStart = 4,
    PresentEnd = 5,
    TriggerFlash = 7,
};

struct NvSetSleepModeParamsV1
{
    std::uint32_t version;
    NvBool lowLatencyMode;
    NvBool lowLatencyBoost;
    std::uint32_t minimumIntervalUs;
    NvBool useMarkersToOptimize;
    std::uint8_t reserved[31];
};

struct NvLatencyMarkerParamsV1
{
    std::uint32_t version;
    std::uint64_t frameId;
    NvLatencyMarkerType markerType;
    std::uint8_t reserved[64];
};

static_assert(sizeof(NvSetSleepModeParamsV1) == 44);
static_assert(offsetof(NvSetSleepModeParamsV1, minimumIntervalUs) == 8);
static_assert(sizeof(NvLatencyMarkerParamsV1) == 88);
static_assert(offsetof(NvLatencyMarkerParamsV1, frameId) == 8);

template <typename T>
constexpr std::uint32_t NvApiVersion(std::uint32_t version)
{
    return static_cast<std::uint32_t>(sizeof(T)) | (version << 16);
}

using NvApiQueryInterfaceFn = void* (__cdecl*)(std::uint32_t id);
using NvApiInitializeFn = int (__cdecl*)();
using NvApiSetSleepModeFn = int (__cdecl*)(IUnknown* device, NvSetSleepModeParamsV1* params);
using NvApiSleepFn = int (__cdecl*)(IUnknown* device);
using NvApiSetLatencyMarkerFn = int (__cdecl*)(IUnknown* device, NvLatencyMarkerParamsV1* params);

using PumpMessagesFn = std::int64_t (*)();
using DispatchInputEventFn = char (__fastcall*)(std::int32_t* inputEvent);
using MaterialFrameFn = std::int64_t (*)();

HMODULE s_nvApiModule = nullptr;
NvApiSetSleepModeFn s_nvApiSetSleepMode = nullptr;
NvApiSleepFn s_nvApiSleep = nullptr;
NvApiSetLatencyMarkerFn s_nvApiSetLatencyMarker = nullptr;
std::once_flag s_nvApiInitializeOnce;
std::atomic<bool> s_nvApiReady{false};
std::atomic<ID3D11Device*> s_device{nullptr};
std::atomic<std::uintptr_t> s_materialSystemBase{0};
std::atomic<int> s_parameterStatus{-1};
std::atomic<bool> s_parametersDirty{true};
SRWLOCK s_parameterLock = SRWLOCK_INIT;

ConVarR1* s_fpsMaxLowLatency = nullptr;
ConVarR1* s_useLowLatency = nullptr;
ConVarR1* s_useLowLatencyBoost = nullptr;
ConVarR1* s_useMarkersToOptimize = nullptr;
ConVarR1* s_fpsMax = nullptr;
std::atomic<bool> s_conVarsReady{false};
NvSetSleepModeParamsV1 s_lastParameters{};
bool s_haveLastParameters = false;

std::atomic<bool> s_runLowLatencyAtNextPump{false};
std::atomic<std::uint64_t> s_nextFrameId{0};
std::atomic<std::uint64_t> s_activeFrameId{0};
std::atomic<std::uint64_t> s_lastSubmittedFrameId{0};
std::array<std::uint64_t, kSubmittedFrameCapacity> s_submittedFrames{};
std::atomic<std::uint64_t> s_submittedWrite{0};
std::atomic<std::uint64_t> s_submittedRead{0};
std::atomic<int> s_markerErrorLogBudget{4};

PumpMessagesFn s_originalPumpMessages = nullptr;
DispatchInputEventFn s_originalDispatchInputEvent = nullptr;
MaterialFrameFn s_originalPresentFrame = nullptr;
MaterialFrameFn s_originalMaterialShutdown = nullptr;

void ReflexLog(const char* format, ...)
{
    char message[512]{};
    va_list args;
    va_start(args, format);
    vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
    va_end(args);
    OutputDebugStringA(message);
}

bool IsSuccessfulMinHookStatus(MH_STATUS status)
{
    return status == MH_OK
        || status == MH_ERROR_ALREADY_CREATED
        || status == MH_ERROR_ENABLED;
}

bool InstallCheckedHook(
    std::uintptr_t moduleBase,
    std::uintptr_t rva,
    const std::uint8_t* expected,
    std::size_t expectedSize,
    void* detour,
    void** original,
    const char* name)
{
    void* const target = reinterpret_cast<void*>(moduleBase + rva);
    if (std::memcmp(target, expected, expectedSize) != 0)
    {
        ReflexLog(
            "[r1delta_reflex] skipped %s at RVA 0x%llX: unexpected binary revision\n",
            name,
            static_cast<unsigned long long>(rva));
        return false;
    }

    const MH_STATUS createStatus = MH_CreateHook(target, detour, original);
    if (!IsSuccessfulMinHookStatus(createStatus) || !original || !*original)
    {
        ReflexLog(
            "[r1delta_reflex] failed to create %s hook at RVA 0x%llX: status=%d\n",
            name,
            static_cast<unsigned long long>(rva),
            static_cast<int>(createStatus));
        return false;
    }

    const MH_STATUS enableStatus = MH_EnableHook(target);
    if (!IsSuccessfulMinHookStatus(enableStatus))
    {
        ReflexLog(
            "[r1delta_reflex] failed to enable %s hook at RVA 0x%llX: status=%d\n",
            name,
            static_cast<unsigned long long>(rva),
            static_cast<int>(enableStatus));
        return false;
    }

    return true;
}

void InitializeNvApi()
{
    HMODULE const module = LoadLibraryExW(
        L"nvapi64.dll",
        nullptr,
        LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module)
    {
        ReflexLog("[r1delta_reflex] nvapi64.dll unavailable; Reflex disabled\n");
        return;
    }

    const auto queryInterface = reinterpret_cast<NvApiQueryInterfaceFn>(
        GetProcAddress(module, "nvapi_QueryInterface"));
    if (!queryInterface)
    {
        ReflexLog("[r1delta_reflex] nvapi_QueryInterface unavailable; Reflex disabled\n");
        FreeLibrary(module);
        return;
    }

    const auto initialize = reinterpret_cast<NvApiInitializeFn>(
        queryInterface(kNvApiInitializeId));
    const auto setSleepMode = reinterpret_cast<NvApiSetSleepModeFn>(
        queryInterface(kNvApiSetSleepModeId));
    const auto sleep = reinterpret_cast<NvApiSleepFn>(
        queryInterface(kNvApiSleepId));
    const auto setLatencyMarker = reinterpret_cast<NvApiSetLatencyMarkerFn>(
        queryInterface(kNvApiSetLatencyMarkerId));
    if (!initialize || !setSleepMode || !sleep || !setLatencyMarker)
    {
        ReflexLog("[r1delta_reflex] required NVAPI Reflex interfaces unavailable\n");
        FreeLibrary(module);
        return;
    }

    const int status = initialize();
    if (status != kNvApiOk)
    {
        ReflexLog("[r1delta_reflex] NvAPI_Initialize failed: status=%d\n", status);
        FreeLibrary(module);
        return;
    }

    // Keep the module loaded for the process lifetime. Calling into NVAPI or
    // unloading a dependency from DLL_PROCESS_DETACH would run under loader lock.
    s_nvApiModule = module;
    s_nvApiSetSleepMode = setSleepMode;
    s_nvApiSleep = sleep;
    s_nvApiSetLatencyMarker = setLatencyMarker;
    s_nvApiReady.store(true, std::memory_order_release);
    ReflexLog("[r1delta_reflex] NVIDIA Reflex interfaces initialized\n");
}

float DesktopRefreshRate()
{
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode)
        || mode.dmDisplayFrequency <= 1)
    {
        return 0.0f;
    }
    return static_cast<float>(mode.dmDisplayFrequency);
}

float NormalizedFrameRateLimit()
{
    if (!s_fpsMaxLowLatency)
        return 0.0f;

    const float requested = s_fpsMaxLowLatency->m_Value.m_fValue;
    if (!std::isfinite(requested))
        return 0.0f;
    if (requested == -1.0f)
    {
        if (!s_fpsMax && cvarinterface && OriginalCCVar_FindVar)
            s_fpsMax = OriginalCCVar_FindVar(cvarinterface, "fps_max");
        return s_fpsMax && s_fpsMax->m_Value.m_fValue == 0.0f
            ? DesktopRefreshRate()
            : 0.0f;
    }
    if (requested <= 0.0f)
        return 0.0f;
    return requested > 295.0f ? 295.0f : requested;
}

NvSetSleepModeParamsV1 CurrentParameters()
{
    NvSetSleepModeParamsV1 params{};
    params.version = NvApiVersion<NvSetSleepModeParamsV1>(1);
    params.lowLatencyMode = s_useLowLatency && s_useLowLatency->m_Value.m_nValue != 0;
    params.lowLatencyBoost = params.lowLatencyMode
        && s_useLowLatencyBoost
        && s_useLowLatencyBoost->m_Value.m_nValue != 0;
    params.useMarkersToOptimize = s_useMarkersToOptimize
        && s_useMarkersToOptimize->m_Value.m_nValue != 0;

    const float frameRate = NormalizedFrameRateLimit();
    if (frameRate > 0.0f)
        params.minimumIntervalUs = static_cast<std::uint32_t>(1000000.0f / frameRate);
    return params;
}

bool UpdateParameters(ID3D11Device* expectedDevice)
{
    if (!expectedDevice
        || !s_nvApiReady.load(std::memory_order_acquire)
        || !s_conVarsReady.load(std::memory_order_acquire))
    {
        return false;
    }

    SRWGuard parameterGuard(&s_parameterLock);
    ID3D11Device* const device = s_device.load(std::memory_order_acquire);
    if (device != expectedDevice)
        return false;

    const NvSetSleepModeParamsV1 parameters = CurrentParameters();
    const bool changed = !s_haveLastParameters
        || std::memcmp(&parameters, &s_lastParameters, sizeof(parameters)) != 0;
    if (changed || s_parametersDirty.exchange(false, std::memory_order_acq_rel))
    {
        const int previousStatus = s_parameterStatus.load(std::memory_order_relaxed);
        const int status = s_nvApiSetSleepMode(device, const_cast<NvSetSleepModeParamsV1*>(&parameters));
        s_parameterStatus.store(status, std::memory_order_release);
        s_lastParameters = parameters;
        s_haveLastParameters = true;
        if (status != kNvApiOk && status != previousStatus)
            ReflexLog("[r1delta_reflex] NvAPI_D3D_SetSleepMode failed: status=%d\n", status);
    }

    return s_parameterStatus.load(std::memory_order_acquire) == kNvApiOk;
}

bool CanSetMarkers(ID3D11Device*& device)
{
    device = s_device.load(std::memory_order_acquire);
    return device
        && s_nvApiReady.load(std::memory_order_acquire)
        && s_parameterStatus.load(std::memory_order_acquire) == kNvApiOk;
}

void SetMarker(ID3D11Device* device, NvLatencyMarkerType type, std::uint64_t frameId)
{
    if (!device || !frameId)
        return;

    NvLatencyMarkerParamsV1 params{};
    params.version = NvApiVersion<NvLatencyMarkerParamsV1>(1);
    params.frameId = frameId;
    params.markerType = type;
    const int status = s_nvApiSetLatencyMarker(device, &params);
    if (status != kNvApiOk
        && s_markerErrorLogBudget.fetch_sub(1, std::memory_order_relaxed) > 0)
    {
        ReflexLog(
            "[r1delta_reflex] NvAPI_D3D_SetLatencyMarker failed: type=%d frame=%llu status=%d\n",
            static_cast<int>(type),
            static_cast<unsigned long long>(frameId),
            status);
    }
}

bool PushSubmittedFrame(std::uint64_t frameId)
{
    const std::uint64_t write = s_submittedWrite.load(std::memory_order_relaxed);
    const std::uint64_t read = s_submittedRead.load(std::memory_order_acquire);
    if (write - read >= kSubmittedFrameCapacity)
    {
        ReflexLog("[r1delta_reflex] render queue exceeded marker capacity; frame %llu untracked\n",
            static_cast<unsigned long long>(frameId));
        return false;
    }

    s_submittedFrames[write % kSubmittedFrameCapacity] = frameId;
    s_submittedWrite.store(write + 1, std::memory_order_release);
    return true;
}

std::uint64_t PopSubmittedFrame()
{
    const std::uint64_t read = s_submittedRead.load(std::memory_order_relaxed);
    const std::uint64_t write = s_submittedWrite.load(std::memory_order_acquire);
    if (read == write)
        return 0;

    const std::uint64_t frameId = s_submittedFrames[read % kSubmittedFrameCapacity];
    s_submittedRead.store(read + 1, std::memory_order_release);
    return frameId;
}

void BeforeMessagePump()
{
    if (!s_runLowLatencyAtNextPump.exchange(false, std::memory_order_acq_rel))
        return;
    AntiLagBeforeInputPoll();

    ID3D11Device* const device = s_device.load(std::memory_order_acquire);
    if (UpdateParameters(device))
        s_nvApiSleep(device);
}

std::int64_t ReflexPumpMessages()
{
    BeforeMessagePump();
    return s_originalPumpMessages();
}

char __fastcall ReflexDispatchInputEvent(std::int32_t* inputEvent)
{
    if (inputEvent
        && (inputEvent[0] == kInputEventButtonPressed
            || inputEvent[0] == kInputEventButtonDoubleClicked)
        && inputEvent[2] == kMouseLeftButtonCode)
    {
        ID3D11Device* device = nullptr;
        if (CanSetMarkers(device))
        {
            const std::uint64_t frameId = s_nextFrameId.load(std::memory_order_acquire) + 1;
            SetMarker(device, NvLatencyMarkerType::TriggerFlash, frameId);
        }
    }
    return s_originalDispatchInputEvent(inputEvent);
}

std::int64_t ReflexPresentFrame()
{
    if (!s_device.load(std::memory_order_acquire))
    {
        const std::uintptr_t materialSystemBase =
            s_materialSystemBase.load(std::memory_order_acquire);
        if (materialSystemBase)
        {
            ReflexOnDeviceReady(
                *reinterpret_cast<ID3D11Device**>(
                    materialSystemBase + 0x290D88));
        }
    }

    const std::uint64_t frameId = PopSubmittedFrame();
    ID3D11Device* device = nullptr;
    const bool mark = frameId && CanSetMarkers(device);
    if (mark)
    {
        SetMarker(device, NvLatencyMarkerType::RenderSubmitEnd, frameId);
        SetMarker(device, NvLatencyMarkerType::PresentStart, frameId);
    }

    const std::int64_t result = s_originalPresentFrame();

    if (mark)
        SetMarker(device, NvLatencyMarkerType::PresentEnd, frameId);
    return result;
}

void DisableDevice(ID3D11Device* device)
{
    if (!device || !s_nvApiReady.load(std::memory_order_acquire))
        return;

    NvSetSleepModeParamsV1 parameters{};
    parameters.version = NvApiVersion<NvSetSleepModeParamsV1>(1);
    s_nvApiSetSleepMode(device, &parameters);
}

std::int64_t ReflexMaterialShutdown()
{
    AntiLagOnDeviceShutdown();
    {
        SRWGuard parameterGuard(&s_parameterLock);
        ID3D11Device* const device = s_device.exchange(nullptr, std::memory_order_acq_rel);
        DisableDevice(device);
        s_haveLastParameters = false;
        s_parameterStatus.store(-1, std::memory_order_release);
        s_parametersDirty.store(true, std::memory_order_release);
    }

    s_activeFrameId.store(0, std::memory_order_release);
    s_lastSubmittedFrameId.store(0, std::memory_order_release);
    s_submittedRead.store(s_submittedWrite.load(std::memory_order_acquire), std::memory_order_release);
    return s_originalMaterialShutdown();
}
} // namespace

void SetupReflexEngineHooks(std::uintptr_t engineBase)
{
    if (!engineBase
        || GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015
        || r1delta::gpu_latency::IsDisabled())
    {
        return;
    }

    static constexpr std::uint8_t kPumpMessagesExpected[] = {
        0x48, 0x83, 0xEC, 0x68,
        0x48, 0x8D, 0x4C, 0x24, 0x30,
        0x45, 0x33, 0xC9,
        0x45, 0x33, 0xC0,
        0x33, 0xD2,
    };
    static constexpr std::uint8_t kDispatchInputEventExpected[] = {
        0x40, 0x53,
        0x48, 0x83, 0xEC, 0x20,
        0x48, 0x63, 0x01,
        0x48, 0x8B, 0xD9,
        0x3D, 0xD5, 0x00, 0x00, 0x00,
    };

    InstallCheckedHook(
        engineBase,
        0x1A0920,
        kPumpMessagesExpected,
        sizeof(kPumpMessagesExpected),
        reinterpret_cast<void*>(&ReflexPumpMessages),
        reinterpret_cast<void**>(&s_originalPumpMessages),
        "CEngineAPI::PumpMessages");
    InstallCheckedHook(
        engineBase,
        0x1A6D70,
        kDispatchInputEventExpected,
        sizeof(kDispatchInputEventExpected),
        reinterpret_cast<void*>(&ReflexDispatchInputEvent),
        reinterpret_cast<void**>(&s_originalDispatchInputEvent),
        "input event dispatcher");
}

void SetupReflexMaterialSystemHooks(std::uintptr_t materialSystemBase)
{
    if (!materialSystemBase
        || GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015
        || r1delta::gpu_latency::IsDisabled())
    {
        return;
    }
    s_materialSystemBase.store(materialSystemBase, std::memory_order_release);

    static constexpr std::uint8_t kPresentFrameExpected[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08,
        0x48, 0x89, 0x74, 0x24, 0x10,
        0x57,
        0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,
    };
    static constexpr std::uint8_t kMaterialShutdownExpected[] = {
        0x40, 0x53,
        0x48, 0x83, 0xEC, 0x20,
        0x48, 0x8B, 0x0D, 0x5B, 0xDC, 0x28, 0x00,
        0x48, 0x85, 0xC9,
        0x74, 0x0B,
    };

    InstallCheckedHook(
        materialSystemBase,
        0x5C90,
        kPresentFrameExpected,
        sizeof(kPresentFrameExpected),
        reinterpret_cast<void*>(&ReflexPresentFrame),
        reinterpret_cast<void**>(&s_originalPresentFrame),
        "DX11 frame present");
    InstallCheckedHook(
        materialSystemBase,
        0x56E0,
        kMaterialShutdownExpected,
        sizeof(kMaterialShutdownExpected),
        reinterpret_cast<void*>(&ReflexMaterialShutdown),
        reinterpret_cast<void**>(&s_originalMaterialShutdown),
        "DX11 material shutdown");
}

void RegisterReflexConVars()
{
    if (r1delta::gpu_latency::IsDisabled()
        || s_conVarsReady.load(std::memory_order_acquire))
        return;

    s_fpsMaxLowLatency = RegisterConVar(
        "fps_max_low_latency",
        "0",
        FCVAR_CLIENTDLL | FCVAR_RELEASE,
        "Frame rate limiter shared by supported low-latency SDKs. -1 uses desktop refresh when fps_max is unlimited; 0 disables the SDK limiter.");
    s_useLowLatency = RegisterConVar(
        "gfx_nvnUseLowLatency",
        "1",
        FCVAR_CLIENTDLL | FCVAR_RELEASE | FCVAR_ARCHIVE_PLAYERPROFILE,
        "Enable NVIDIA Reflex Low Latency mode.");
    s_useLowLatencyBoost = RegisterConVar(
        "gfx_nvnUseLowLatencyBoost",
        "0",
        FCVAR_CLIENTDLL | FCVAR_RELEASE | FCVAR_ARCHIVE_PLAYERPROFILE,
        "Enable NVIDIA Reflex Low Latency Boost.");
    s_useMarkersToOptimize = RegisterConVar(
        "gfx_nvnUseMarkersToOptimize",
        "0",
        FCVAR_CLIENTDLL | FCVAR_RELEASE,
        "Allow NVIDIA Reflex latency markers to optimize scheduling. Disabled by default because it can cause rubber-banding on some hardware.");

    if (s_fpsMaxLowLatency)
    {
        s_fpsMaxLowLatency->m_bHasMin = true;
        s_fpsMaxLowLatency->m_fMinVal = -1.0f;
        s_fpsMaxLowLatency->m_bHasMax = true;
        s_fpsMaxLowLatency->m_fMaxVal = 295.0f;
    }

    s_parametersDirty.store(true, std::memory_order_release);
    s_conVarsReady.store(
        s_fpsMaxLowLatency
            && s_useLowLatency
            && s_useLowLatencyBoost
            && s_useMarkersToOptimize,
        std::memory_order_release);
}

void ReflexOnDeviceReady(ID3D11Device* device)
{
    if (!device || r1delta::gpu_latency::IsDisabled())
        return;

    std::call_once(s_nvApiInitializeOnce, InitializeNvApi);
    if (!s_nvApiReady.load(std::memory_order_acquire))
        return;

    {
        SRWGuard parameterGuard(&s_parameterLock);
        s_device.store(device, std::memory_order_release);
        s_haveLastParameters = false;
        s_parameterStatus.store(-1, std::memory_order_release);
        s_parametersDirty.store(true, std::memory_order_release);
    }
}

void ReflexBeginSimulation()
{
    ID3D11Device* device = nullptr;
    if (!CanSetMarkers(device))
    {
        s_activeFrameId.store(0, std::memory_order_release);
        return;
    }

    const std::uint64_t frameId = s_nextFrameId.fetch_add(1, std::memory_order_acq_rel) + 1;
    s_activeFrameId.store(frameId, std::memory_order_release);
    SetMarker(device, NvLatencyMarkerType::SimulationStart, frameId);
}

void ReflexEndSimulationAndBeginRenderSubmit()
{
    const std::uint64_t frameId = s_activeFrameId.exchange(0, std::memory_order_acq_rel);
    if (!frameId)
        return;

    const std::uint64_t previous = s_lastSubmittedFrameId.exchange(frameId, std::memory_order_acq_rel);
    if (previous == frameId)
        return;

    ID3D11Device* device = nullptr;
    if (!CanSetMarkers(device))
        return;

    SetMarker(device, NvLatencyMarkerType::SimulationEnd, frameId);
    SetMarker(device, NvLatencyMarkerType::RenderSubmitStart, frameId);
    PushSubmittedFrame(frameId);
}

void ReflexOnEngineFrameComplete()
{
    s_runLowLatencyAtNextPump.store(true, std::memory_order_release);
}
