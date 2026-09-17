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

// Global instances
MMNotificationClient g_mmNotificationClient{};
IMMDeviceEnumerator* g_mmDeviceEnumerator = nullptr;


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
    if (g_mmDeviceEnumerator) return;
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
    // Use the full native lifecycle; +15AF0 recreates voices inline and bypasses
    // the verified filter-capable S_Init and notification ownership hooks.
    reinterpret_cast<__int64 (*)()>(G_engine + 0x118C0)();
    Msg("Restarted XAudio...\n");
}

