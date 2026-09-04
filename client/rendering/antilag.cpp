#include "antilag.h"
#include "gpu_latency.h"

#include "cvar.h"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include "thirdparty/fidelityfx/ffx_antilag2_dx11.h"

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace
{
constexpr UINT kAmdVendorId = 0x1002;

AMD::AntiLag2DX11::Context s_context{};
SRWLOCK s_contextLock = SRWLOCK_INIT;
std::atomic<bool> s_available{false};
ID3D11Device* s_device = nullptr;
bool s_initializationAttempted = false;
HRESULT s_lastUpdateStatus = S_OK;

ConVarR1* s_fpsMaxLowLatency = nullptr;
ConVarR1* s_useLowLatency = nullptr;
ConVarR1* s_fpsMax = nullptr;
std::atomic<bool> s_conVarsReady{false};

void AntiLagLog(const char* format, ...)
{
    char message[512]{};
    va_list args;
    va_start(args, format);
    vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
    va_end(args);
    OutputDebugStringA(message);
}

bool IsAmdDevice(ID3D11Device* device, UINT& vendorId)
{
    vendorId = 0;
    IDXGIDevice* dxgiDevice = nullptr;
    if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))))
        return false;

    IDXGIAdapter* adapter = nullptr;
    const HRESULT adapterStatus = dxgiDevice->GetAdapter(&adapter);
    dxgiDevice->Release();
    if (FAILED(adapterStatus) || !adapter)
        return false;

    DXGI_ADAPTER_DESC description{};
    const HRESULT descriptionStatus = adapter->GetDesc(&description);
    adapter->Release();
    if (FAILED(descriptionStatus))
        return false;

    vendorId = description.VendorId;
    return vendorId == kAmdVendorId;
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

unsigned int NormalizedFrameRateLimit()
{
    if (!s_fpsMaxLowLatency && cvarinterface && OriginalCCVar_FindVar)
        s_fpsMaxLowLatency = OriginalCCVar_FindVar(cvarinterface, "fps_max_low_latency");
    if (!s_fpsMaxLowLatency)
        return 0;

    const float requested = s_fpsMaxLowLatency->m_Value.m_fValue;
    if (!std::isfinite(requested))
        return 0;
    if (requested == -1.0f)
    {
        if (!s_fpsMax && cvarinterface && OriginalCCVar_FindVar)
            s_fpsMax = OriginalCCVar_FindVar(cvarinterface, "fps_max");
        const float refreshRate = s_fpsMax && s_fpsMax->m_Value.m_fValue == 0.0f
            ? DesktopRefreshRate()
            : 0.0f;
        return refreshRate > 0.0f ? static_cast<unsigned int>(refreshRate) : 0;
    }
    if (requested <= 0.0f)
        return 0;
    return static_cast<unsigned int>(requested > 295.0f ? 295.0f : requested);
}

void ResetContextLocked()
{
    s_available.store(false, std::memory_order_release);
    if (s_context.m_pAntiLagAPI)
    {
        AMD::AntiLag2DX11::Update(&s_context, false, 0);
        const ULONG references = AMD::AntiLag2DX11::DeInitialize(&s_context);
        if (references != 0)
        {
            AntiLagLog(
                "[r1delta_antilag] driver interface retained %lu reference(s) during shutdown\n",
                references);
        }
    }
    s_device = nullptr;
    s_initializationAttempted = false;
    s_lastUpdateStatus = S_OK;
}
} // namespace

void RegisterAntiLagConVars()
{
    if (r1delta::gpu_latency::IsDisabled()
        || s_conVarsReady.load(std::memory_order_acquire))
        return;

    s_useLowLatency = RegisterConVar(
        "gfx_ffxUseLowLatency",
        "1",
        FCVAR_CLIENTDLL | FCVAR_RELEASE | FCVAR_ARCHIVE_PLAYERPROFILE,
        "Enable AMD Radeon Anti-Lag 2 low-latency mode.");
    if (cvarinterface && OriginalCCVar_FindVar)
        s_fpsMaxLowLatency = OriginalCCVar_FindVar(cvarinterface, "fps_max_low_latency");
    s_conVarsReady.store(true, std::memory_order_release);
}

void AntiLagOnDeviceReady(ID3D11Device* device)
{
    if (!device || r1delta::gpu_latency::IsDisabled())
        return;

    AcquireSRWLockExclusive(&s_contextLock);
    if (s_device != device)
        ResetContextLocked();
    if (s_initializationAttempted)
    {
        ReleaseSRWLockExclusive(&s_contextLock);
        return;
    }

    s_device = device;
    s_initializationAttempted = true;

    UINT vendorId = 0;
    if (!IsAmdDevice(device, vendorId))
    {
        AntiLagLog(
            "[r1delta_antilag] active adapter vendor 0x%04X; Anti-Lag 2 inactive\n",
            vendorId);
        ReleaseSRWLockExclusive(&s_contextLock);
        return;
    }

    const HRESULT status = AMD::AntiLag2DX11::Initialize(&s_context);
    if (status == S_OK)
    {
        s_available.store(true, std::memory_order_release);
        AntiLagLog("[r1delta_antilag] AMD Radeon Anti-Lag 2 initialized\n");
    }
    else
    {
        AntiLagLog(
            "[r1delta_antilag] Anti-Lag 2 initialization failed: HRESULT=0x%08lX\n",
            static_cast<unsigned long>(status));
    }
    ReleaseSRWLockExclusive(&s_contextLock);
}

void AntiLagBeforeInputPoll()
{
    if (!s_available.load(std::memory_order_acquire)
        || !s_conVarsReady.load(std::memory_order_acquire))
    {
        return;
    }

    AcquireSRWLockExclusive(&s_contextLock);
    if (!s_available.load(std::memory_order_relaxed))
    {
        ReleaseSRWLockExclusive(&s_contextLock);
        return;
    }

    const bool enabled = s_useLowLatency && s_useLowLatency->m_Value.m_nValue != 0;
    const HRESULT status = AMD::AntiLag2DX11::Update(
        &s_context,
        enabled,
        NormalizedFrameRateLimit());
    if (FAILED(status) && status != s_lastUpdateStatus)
    {
        AntiLagLog(
            "[r1delta_antilag] Anti-Lag 2 frame update failed: HRESULT=0x%08lX\n",
            static_cast<unsigned long>(status));
    }
    s_lastUpdateStatus = status;
    ReleaseSRWLockExclusive(&s_contextLock);
}

void AntiLagOnDeviceShutdown()
{
    AcquireSRWLockExclusive(&s_contextLock);
    ResetContextLocked();
    ReleaseSRWLockExclusive(&s_contextLock);
}
