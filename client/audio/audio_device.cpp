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
#include "MinHook.h"

// Global instances
MMNotificationClient g_mmNotificationClient{};
IMMDeviceEnumerator* g_mmDeviceEnumerator = nullptr;

// Sound restart function pointer
typedef void(__cdecl* Snd_Restart_DirectSound_t)();
static Snd_Restart_DirectSound_t Snd_Restart_DirectSound = nullptr;

// S_Init/S_Shutdown hook originals
typedef void(__cdecl* S_Init_t)();
typedef void(__cdecl* S_Shutdown_t)();
static S_Init_t oS_Init = nullptr;
static S_Shutdown_t oS_Shutdown = nullptr;

HRESULT STDMETHODCALLTYPE MMNotificationClient::QueryInterface(REFIID riid, VOID** ppvInterface)
{
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
    if (role == eMultimedia) {
        Msg("Default device changed to %ls\n", pwstrDeviceId);
        if (G_client) {
            Cbuf_AddText(0, "sound_reboot_xaudio", 0);
        }
    }
    return S_OK;
}

void Init_MMNotificationClient()
{
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                   __uuidof(IMMDeviceEnumerator), (void**)&g_mmDeviceEnumerator);
    if (SUCCEEDED(hr)) {
        g_mmDeviceEnumerator->RegisterEndpointNotificationCallback(&g_mmNotificationClient);
    }
}

void Deinit_MMNotificationClient()
{
    if (g_mmDeviceEnumerator) {
        g_mmDeviceEnumerator->UnregisterEndpointNotificationCallback(&g_mmNotificationClient);
        g_mmDeviceEnumerator->Release();
        g_mmDeviceEnumerator = nullptr;
    }
}

void ConCommand_sound_reboot_xaudio(const CCommand& args)
{
    Msg("Restarting XAudio...\n");
    Snd_Restart_DirectSound();
    Msg("Restarted XAudio...\n");
}

static void S_Init_Hook()
{
    oS_Init();
    bool g_bNoSound = *reinterpret_cast<bool*>(G_engine + 0x20144E4);
    if (!g_bNoSound)
        Init_MMNotificationClient();
}

static void S_Shutdown_Hook()
{
    oS_Shutdown();
    bool g_bNoSound = *reinterpret_cast<bool*>(G_engine + 0x20144E4);
    if (!g_bNoSound)
        Deinit_MMNotificationClient();
}

void Setup_MMNotificationClient()
{
    MH_CreateHook((LPVOID)(G_engine + 0xEA00), &S_Init_Hook, reinterpret_cast<LPVOID*>(&oS_Init));
    MH_CreateHook((LPVOID)(G_engine + 0x114B0), &S_Shutdown_Hook, reinterpret_cast<LPVOID*>(&oS_Shutdown));
    Snd_Restart_DirectSound = reinterpret_cast<Snd_Restart_DirectSound_t>(G_engine + 0x15AF0);
    MH_EnableHook(MH_ALL_HOOKS);
}
