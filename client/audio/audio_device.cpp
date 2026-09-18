// %*++***###*##**##++**+++*++*%%%%%%%+*%+#*+%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#=%%%#**#+#%
// ==----------------------------------------------------------------------=================+
// =------------------------------------::----------------------------------===---==========+
// ---------------------------------:-:--::::-::::-------------------=======================+
// =-------------------------------::::::::-::::-:::----------==============+===+++=========+
// ----------------------------::::::--:---=====----------===========++==++++++++++++++++++++
// ----------------------------:-----:---==++++++====-==========++++++++++++++++++++++++++++*
// -------------------------------------=+++++++=============++++++++++++++++++++++++++++++**
// -------------------------------------=++++*+========++++++++++++++++++++++++++++++++++++**
// ----------------------------:::::::--=+++++=======+++++++++++++++++++++++************++++*
// ---------------------::::::::::::::::-==+++===++++++++++++++++++++++++********###%%%##*++*
// -------:::::::::::::::::::::::::::::::-=====+####**+++++++++++++++++*********#%%%@@@@%%#**
// ------:-:::::::::::::::::::::::::::::::-====*%%%%#*++++++++++++++++++********##%@@@@@%%#**
// ----------::::::::::::::::::::::::::-=--====+#%%%*++++++++++++++++++++*********##%%%%%#***
// -------------=*=-:::::::::::::::::-=++======++***+++++++++++++++++++**************###*****
// -------------=*#=-------======++++*###*+=+=++=++++++++++*+++******************************
// =-----=======+*#*+++++++*****##########+=++++++++++***************************************
// +++++++++++****#################*****#*+=+++++++++****************************************
// ++**+++++++++++++======+++++++++++++****+=+++***################**************************
// *****+=--------::-::::::::::::::::::------=*#%%%%%%%%%%%%%%%%%%%#####*********************
// ******=-----------:::::::::::---:::::::::-=#%%%%@@@@@@@@@@@@@@%%%%###********************#
// ******=---------------:::::::::::-:::::::-*%%%@@@@@@@@@@@@@@@@@%%%%##********************#
// ****#*=-----------------:::::::::::::::::-=*%%@@@@@@@@@@@@@@@@@@%%##*********************#
// ******+===-------------::::::::::::---:::--=*#%%%@@@@@@@@@@@@@%%######**#**************###
// ==++==------------------:::::::::::::-------=+**##%%%@%%%%%%%%##########*****************#
// ==--------------------------::-:::::::::::---=++**##%%%%%%%%%%%##########*************####
// =--------------------------------:---::::--:--==+**###%#%%%%%%%%%%%#####**************####
// ====--------------------------:-------::-------==+++****###########******************#####
// ===--==------------------------------------::---==+++++******************************#####
// ===-------------------------------------:::-:----=+++********************************####%
// =====---------------------------------------------=++++******************************####%
// ======------------------==------------------------==+++***************************######%%
// =========-----===--------==------------------------==++********#*#####**#######*########%%
//
// Audio Device Change Notifications

#include "audio_device.h"
#include "load.h"
#include "logging.h"
#include "core.h"
#include "audio_hook_profile.h"
#include <MinHook.h>
#include <atomic>

namespace {
MMNotificationClient g_mmNotificationClient{};
IMMDeviceEnumerator* g_mmDeviceEnumerator = nullptr;
std::atomic<bool> s_notificationsEnabled{false};
std::uintptr_t s_engine = 0;
bool s_installed = false;
thread_local bool s_rebooting = false;
// Exact Client2015 x64 entries: S_Init, S_Shutdown and sound_reboot take
// no arguments and return void. No voice or effect interfaces are replaced.
using LifecycleFn = void (*)();
LifecycleFn s_init = nullptr;
LifecycleFn s_shutdown = nullptr;
LifecycleFn s_reboot = nullptr;
void Init_MMNotificationClient();
void Deinit_MMNotificationClient();

class AudioScope {
    CRITICAL_SECTION* section;
public:
    AudioScope() : section(reinterpret_cast<CRITICAL_SECTION*>(s_engine + 0x2017170)) { EnterCriticalSection(section); }
    ~AudioScope() { LeaveCriticalSection(section); }
};

bool NativeAudioReady() {
    // Failed native initialization may leave a released nonnull engine pointer.
    return !*reinterpret_cast<unsigned char*>(s_engine + 0x200DFA5) &&
        *reinterpret_cast<void**>(s_engine + 0x200DF78) != nullptr;
}

void Init() {
    if (GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015 || s_rebooting) { s_init(); return; }
    Deinit_MMNotificationClient();
    bool ready;
    {
        AudioScope lock;
        s_init();
        ready = NativeAudioReady();
    }
    if (ready) Init_MMNotificationClient();
}

void Shutdown() {
    if (GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015 || s_rebooting) { s_shutdown(); return; }
    Deinit_MMNotificationClient();
    AudioScope lock;
    s_shutdown();
}

void Reboot() {
    if (GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015) { s_reboot(); return; }
    // Reboot owns stop/drain, metadata replacement and manifest reload as one
    // audio transaction. Nested lifecycle hooks must not call COM under this lock.
    Deinit_MMNotificationClient();
    bool ready;
    {
        AudioScope lock;
        s_rebooting = true;
        s_reboot();
        s_rebooting = false;
        ready = NativeAudioReady();
    }
    if (ready) Init_MMNotificationClient();
}
}

HRESULT STDMETHODCALLTYPE MMNotificationClient::QueryInterface(REFIID riid, VOID** ppvInterface)
{
    if (!ppvInterface) return E_POINTER;
    if (IID_IUnknown == riid) {
        AddRef();
        *ppvInterface = (IUnknown*)this;
    }
    else if (__uuidof(IMMNotificationClient) == riid) {
        AddRef();
        *ppvInterface = (IMMNotificationClient*)this;
    }
    else {
        *ppvInterface = NULL;
        return E_NOINTERFACE;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MMNotificationClient::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId)
{
    if (flow == eRender && role == eMultimedia &&
        GetR1DeltaEngineMode() == R1DeltaEngineMode::Client2015 &&
        s_notificationsEnabled.load(std::memory_order_acquire)) {
        Msg("Default device changed to %ls\n", pwstrDeviceId ? pwstrDeviceId : L"(none)");
        if (G_client) {
            Cbuf_AddText(0, "sound_reboot_xaudio", 0);
        }
    }
    return S_OK;
}

namespace {
void Init_MMNotificationClient()
{
    if (g_mmDeviceEnumerator) return;
    // The native lifecycle thread already owns COM initialization. Do not
    // change its apartment or call COM from the loader notification.
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                   __uuidof(IMMDeviceEnumerator), (void**)&g_mmDeviceEnumerator);
    if (FAILED(hr)) {
        Warning("R1Delta audio: device notification enumerator failed (0x%08lx)\n", static_cast<unsigned long>(hr));
        return;
    }
    hr = g_mmDeviceEnumerator->RegisterEndpointNotificationCallback(&g_mmNotificationClient);
    if (FAILED(hr)) {
        g_mmDeviceEnumerator->Release();
        g_mmDeviceEnumerator = nullptr;
        Warning("R1Delta audio: device notification registration failed (0x%08lx)\n", static_cast<unsigned long>(hr));
    }
    else {
        s_notificationsEnabled.store(true, std::memory_order_release);
    }
}

void Deinit_MMNotificationClient()
{
    s_notificationsEnabled.store(false, std::memory_order_release);
    if (g_mmDeviceEnumerator) {
        const HRESULT hr = g_mmDeviceEnumerator->UnregisterEndpointNotificationCallback(&g_mmNotificationClient);
        if (FAILED(hr)) {
            Warning("R1Delta audio: device notification unregistration failed (0x%08lx)\n", static_cast<unsigned long>(hr));
            // Retain ownership for the next teardown attempt; never register twice.
            return;
        }
        g_mmDeviceEnumerator->Release();
        g_mmDeviceEnumerator = nullptr;
    }
}
}

bool InstallR1AudioDeviceHooks(std::uintptr_t engineBase)
{
    if (GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015) return false;
    if (s_installed) return s_engine == engineBase;
    static constexpr std::array<unsigned char, 32> digest = {
        0x02,0x14,0xA4,0x04,0x36,0xEC,0xA7,0x7F,0x16,0x0D,0x12,0x76,0x3A,0xB8,0xF3,0x92,
        0xA8,0xCE,0x22,0x68,0xBF,0xE0,0xA9,0x73,0x18,0x8E,0x42,0x6A,0xE4,0xC0,0x26,0x3C};
    if (!r1audio::VerifyModule(engineBase, 0x55038DAC, 0x3264000, digest, "R1 engine.dll notifications")) return false;
    if (*reinterpret_cast<void**>(engineBase + 0x200DF78)) {
        Warning("R1Delta audio: notification hooks must precede first S_Init\n");
        return false;
    }
    struct Hook { std::uintptr_t rva; const char* bytes; std::size_t size; void* detour; void** original; };
    const Hook hooks[] = {
        {0xEA00, "\x40\x55\x41\x55\x48\x8D\xAC\x24\x38\xFF\xFF\xFF\x48\x81\xEC\xC8\x01\x00\x00", 19, reinterpret_cast<void*>(Init), reinterpret_cast<void**>(&s_init)},
        {0x114B0, "\x48\x83\xEC\x28\x48\x8D\x0D\xB5\x5C\x00\x02", 11, reinterpret_cast<void*>(Shutdown), reinterpret_cast<void**>(&s_shutdown)},
        {0x118C0, "\x48\x83\xEC\x28\xE8\xE7\xFB\xFF\xFF\xE8\x32\xD1\xFF\xFF", 14, reinterpret_cast<void*>(Reboot), reinterpret_cast<void**>(&s_reboot)}
    };
    for (const auto& hook : hooks) {
        if (std::memcmp(reinterpret_cast<const void*>(engineBase + hook.rva), hook.bytes, hook.size)) {
            Warning("R1Delta audio: refusing mismatched lifecycle prologue +0x%llX\n", static_cast<unsigned long long>(hook.rva));
            return false;
        }
    }
    s_engine = engineBase;
    std::size_t created = 0;
    bool enabled = false;
    for (const auto& hook : hooks) {
        const auto status = MH_CreateHook(reinterpret_cast<void*>(engineBase + hook.rva), hook.detour, hook.original);
        if (status != MH_OK) {
            Warning("R1Delta audio: cannot create lifecycle hook +0x%llX (%d)\n", static_cast<unsigned long long>(hook.rva), static_cast<int>(status));
            break;
        }
        ++created;
    }
    if (created == std::size(hooks)) {
        enabled = true;
        for (const auto& hook : hooks) {
            const auto status = MH_EnableHook(reinterpret_cast<void*>(engineBase + hook.rva));
            if (status != MH_OK) {
                Warning("R1Delta audio: cannot enable lifecycle hook +0x%llX (%d)\n", static_cast<unsigned long long>(hook.rva), static_cast<int>(status));
                enabled = false;
                break;
            }
        }
    }
    if (!enabled) {
        while (created) {
            const auto& hook = hooks[--created];
            const auto status = MH_RemoveHook(reinterpret_cast<void*>(engineBase + hook.rva));
            if (status != MH_OK)
                Error("R1Delta audio: cannot roll back lifecycle hook +0x%llX (%d)\n", static_cast<unsigned long long>(hook.rva), static_cast<int>(status));
        }
        s_engine = 0;
        return false;
    }
    s_installed = true;
    Msg("R1Delta audio: installed native initialization, shutdown and reboot notification ownership\n");
    return true;
}

void ConCommand_sound_reboot_xaudio(const CCommand& args)
{
    if (!s_installed || GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015) return;
    Msg("Restarting XAudio...\n");
    // Enter the verified native reboot owner, including its lifecycle detour.
    reinterpret_cast<LifecycleFn>(s_engine + 0x118C0)();
    Msg("Restarted XAudio...\n");
}

