#include <windows.h>
#include "core.h"
#include "client.h"
#include "persistentdata.h"
#include "load.h"
#include "weaponxdebug.h"
#include "netchanwarnings.h"

typedef void (*sub_18027F2C0Type)(__int64 a1, const char* a2, void* a3);
sub_18027F2C0Type sub_18027F2C0Original;

void TextMsg(bf_read* msg)
{
	TextMsgPrintType_t msg_dest = (TextMsgPrintType_t)msg->ReadByte();

	char text[256];
	msg->ReadString(text, sizeof(text));

	if (msg_dest == TextMsgPrintType_t::HUD_PRINTCONSOLE) {
		auto endpos = strlen(text);
		if (text[endpos - 1] == '\n')
			text[endpos - 1] = '\0'; // cut off repeated newline

		Msg("%s\n", text);
	}
}

void sub_18027F2C0(__int64 a1, const char* a2, void* a3)
{
	if (!strcmp_static(a2, "SayText"))
	{
		//// raise fov to how bme does it
		//auto var = (ConVarR1*)(OriginalCCVar_FindVar2(cvarinterface, "cl_fovScale"));
		//var->m_fMaxVal = 2.5f;

		sub_18027F2C0Original(a1, "TextMsg", TextMsg);
	}

	sub_18027F2C0Original(a1, a2, a3);
}

typedef int (*GetAddrInfoFn)(PCSTR, PCSTR, const void*, void*);
GetAddrInfoFn originalGetAddrInfo = nullptr;

int hookedGetAddrInfo(PCSTR pNodeName, PCSTR pServiceName, const void* pHints, void* ppResult) {
	const size_t nodelen = strlen(pNodeName);

	// block respawn servers to prevent accidentally DoSing
	if (nodelen >= 11 && strcmp_static(pNodeName + nodelen - 11, "respawn.com") == 0)
		return WSA_SECURE_HOST_NOT_FOUND;
	if (string_equal_size(pNodeName, nodelen, "r1-pc-int.s3.amazonaws.com")
		|| string_equal_size(pNodeName, nodelen, "r1-pc.s3.amazonaws.com"))
		return WSAHOST_NOT_FOUND;

	return originalGetAddrInfo(pNodeName, pServiceName, pHints, ppResult);
}

bool (*oCPortalPlayer__CreateMove)(__int64 a1, float a2, __int64 a3, char a4);
bool CPortalPlayer__CreateMove(__int64 a1, float a2, __int64 a3, char a4) {
	static auto ref = OriginalCCVar_FindVar(cvarinterface, "host_timescale");
	a2 *= ref->m_Value.m_fValue;

	return oCPortalPlayer__CreateMove(a1, a2, a3, a4);
}
__int64 (*osub_18008E820)(__int64 a1, unsigned int a2);
__int64 __fastcall sub_18008E820(__int64 a1, unsigned int a2)
{
	if (a1)
		return osub_18008E820(a1, a2);
	return 0;
}
void* (*oKeyValues__SetString__Client)(__int64 a1, char* a2, const char* a3);
void* KeyValues__SetString__Client(__int64 a1, char* a2, const char* a3)
{
	static auto target = G_engine + 0x16FABA;
	if (uintptr_t(_ReturnAddress()) == target)
		a3 = "30";
	return oKeyValues__SetString__Client(a1, a2, a3);
}
__int64 (*oSharedVehicleViewSmoothing)(
	__int64 a1,
	float* a2,
	float* a3,
	char a4,
	char a5,
	int a6,
	unsigned int* a7,
	float* a8,
	char a9);
__int64 SharedVehicleViewSmoothing(
	__int64 a1,
	float* a2,
	float* a3,
	char a4,
	char a5,
	int a6,
	unsigned int* a7,
	float* a8,
	char a9)
{
	if (a1 == 0)
	{
		static auto getlocalplayer = reinterpret_cast<__int64 (*)(int a1)>(G_client + 0x7B120);
		a1 = getlocalplayer(-1);
	}
	return oSharedVehicleViewSmoothing(a1, a2, a3, a4, a5, a6, a7, a8, a9);
}
typedef void* CLoadingDialog;
namespace vgui {
	typedef void* Panel;
}
struct CGameUI
{
	_BYTE gap0[24];
	char m_szPreviousStatusText[128];
};
void CGameUI__StartProgressBar(CGameUI* thisptr)
{
    static ConVarR1* enable = OriginalCCVar_FindVar(cvarinterface, "progressbar_enabled");
    if (enable->m_Value.m_nValue != 1)
        return;
	static auto CLoadingDialog__ctor = (void(*)(void* thisptr, void* parent))(G_client + 0x363A10);
	static auto vgui__PHandle__Get = (vgui::Panel * (*)(void* a1))(G_client + 0x689A70);
	static auto vgui__PHandle__Set = (vgui::Panel * (*)(void* a1, vgui::Panel* pEnt))(G_client + 0x689AE0);
	static auto CLoadingDialog__SetProgressPoint = (bool(*)(CLoadingDialog* a1, float progress))(G_client + 0x363640);
	static auto CLoadingDialog__Open = (void(*)(CLoadingDialog * a1))(G_client + 0x3631C0);
	static auto BaseModPanel = (vgui::Panel*(*)())(G_client + 0x3871D0);
	static auto GetLoadingDialogHandle = (CLoadingDialog * (*)())(G_client + 0x363720);
	auto g_hLoadingDialog = (CLoadingDialog*)(G_client + 0xB151D8);
	CLoadingDialog* v2; // eax
	vgui::Panel* v3; // eax
	CLoadingDialog* v4; // eax
	CLoadingDialog* v5; // eax

	if (!GetLoadingDialogHandle())
	{
		v2 = (CLoadingDialog*)operator new(0x3E0u * 2); // just for safety
		CLoadingDialog__ctor(v2, BaseModPanel());
		vgui__PHandle__Set(g_hLoadingDialog, v2);
	}
	thisptr->m_szPreviousStatusText[0] = 0;
	CLoadingDialog__SetProgressPoint(GetLoadingDialogHandle(), 0.0);
	CLoadingDialog__Open(GetLoadingDialogHandle());
}
bool (*oCGameUI__UpdateProgressBar)(CGameUI* thisptr, float progress, const char* statusText);
bool CGameUI__UpdateProgressBar(CGameUI* thisptr, float progress, const char* statusText) {
	auto ret = true;// oCGameUI__UpdateProgressBar(thisptr, progress, statusText);
	static auto CGameUI__ContinueProgressBar = (bool (*)(CGameUI * a1, float a2))(G_client + 0x360D60);
	static auto CGameUI__SetProgressBarText = (bool (*)(CGameUI * a1, const char* a2))(G_client + 0x360E40);
	CGameUI__ContinueProgressBar(thisptr, progress);
	if (statusText && strlen (statusText) > 2)
		CGameUI__SetProgressBarText(thisptr, statusText);
	return ret;
}

typedef __int64(__fastcall* tCHudChat__FormatAndDisplayMessage)(
    __int64 a1, const CHAR* a2, unsigned int a3, char a4, char a5);
typedef struct player_info_s {
    char	_0x0000[0x0008];	// 0x0000
    char	szName[32];			// 0x0008
    char	_0x0028[0x0228];	// 0x0028
} player_info_t;
// Matches disassembly call [vtable+0x80]
typedef bool(__fastcall* tEngineClient_GetPlayerInfo)(
    uintptr_t pEngineClient,    // RCX
    unsigned int playerIndex,   // RDX (EDX)
    player_info_s* outputBuffer          // R8
    );

// Matches disassembly call [vtable+0x58]
typedef const wchar_t* (__fastcall* tLocalize_Find)(
    uintptr_t pLocalize,        // RCX
    const char* tokenName       // RDX
    );

// --- Hook Variables ---
tCHudChat__FormatAndDisplayMessage oCHudChat__FormatAndDisplayMessage = nullptr;

// --- Helper Functions ---

// --- Helper Functions ---
inline std::wstring MultiByteToWide(const char* str, int strLen = -1) {
    if (!str || strLen == 0 || (strLen == -1 && *str == '\0')) {
        return std::wstring();
    }
    // Ensure the input string length for MultiByteToWideChar is calculated correctly
    // If strLen is -1, use strlen to avoid reading past embedded nulls if they exist.
    // However, MultiByteToWideChar handles -1 itself, so this might be redundant unless
    // we suspect the input 'str' itself is not properly terminated within its buffer.
    // Let's stick to passing strLen = -1 for now, relying on the guaranteed termination
    // we added for szName.

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str, strLen, NULL, 0);
    if (size_needed <= 0) {
        return std::wstring(); // Error or empty string
    }
    std::wstring wstrTo(size_needed, L'\0');
    int result = MultiByteToWideChar(CP_UTF8, 0, str, strLen, &wstrTo[0], size_needed);
    if (result == 0) {
        return std::wstring(); // Conversion failed
    }
    // Remove potential trailing null if MultiByteToWideChar included it when strLen was -1
    if (!wstrTo.empty() && wstrTo.back() == L'\0') {
        wstrTo.pop_back();
    }
    return wstrTo;
}

inline std::string WideToMultiByte(const std::wstring& wstr, unsigned int codePage = CP_UTF8) { // Default to UTF-8
    if (wstr.empty()) {
        return std::string();
    }
    int size_needed = WideCharToMultiByte(codePage, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    if (size_needed <= 0) {
        return std::string();
    }
    std::string strTo(size_needed, '\0');
    int result = WideCharToMultiByte(codePage, 0, wstr.c_str(), (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    if (result == 0) {
        return std::string(); // Conversion failed
    }
    return strTo;
}
int recurse = 0;
// --- Hooked Function ---
__int64 __fastcall CHudChat__FormatAndDisplayMessage_Hooked(
    __int64 a1, // CHudChat* this
    const CHAR* a2, // message
    unsigned int a3, // player index/entity id
    char a4, // is team chat?
    char a5  // is dead chat?
) {
    if (recurse != 0) {
        return oCHudChat__FormatAndDisplayMessage(a1, a2, a3, a4, a5);
    }
    recurse++;
    // --- Static variables & Global Init (remain the same) ---
    static uintptr_t s_pEngineClient = 0;
    static uintptr_t s_pVGuiLocalize = 0;
    static bool s_globalsInitialized = false;
    // --- Constants for Global Retrieval (remain the same) ---
    constexpr uintptr_t IDA_ANALYSIS_BASE_ADDRESS = 0x180000000;
    constexpr uintptr_t IDA_ADDR_G_PENGINECLIENT = 0x180BF51E8;
    constexpr uintptr_t IDA_ADDR_G_PVGUILOCALIZE = 0x18380E750;
    constexpr uintptr_t OFFSET_G_PENGINECLIENT = IDA_ADDR_G_PENGINECLIENT - IDA_ANALYSIS_BASE_ADDRESS;
    constexpr uintptr_t OFFSET_G_PVGUILOCALIZE = IDA_ADDR_G_PVGUILOCALIZE - IDA_ANALYSIS_BASE_ADDRESS;

    // --- Initialize Globals on first call ---
    if (!s_globalsInitialized && G_client != 0) {
        uintptr_t addressOfEnginePtr = G_client + OFFSET_G_PENGINECLIENT;
        uintptr_t addressOfLocalizePtr = G_client + OFFSET_G_PVGUILOCALIZE;
        bool canReadEngine = !IsBadReadPtr((void*)addressOfEnginePtr, sizeof(uintptr_t));
        bool canReadLocalize = !IsBadReadPtr((void*)addressOfLocalizePtr, sizeof(uintptr_t));
        if (canReadEngine) s_pEngineClient = *(uintptr_t*)addressOfEnginePtr;
        if (canReadLocalize) s_pVGuiLocalize = *(uintptr_t*)addressOfLocalizePtr;
        s_globalsInitialized = true;
    }

    // --- Reconstruct the message string ---
    std::wstring finalMessageStringW;
    bool nameRetrievedSuccessfully = false;
    player_info_t playerInfo = {}; // Zero initialize

    // 1. Get Player Info
    if (s_pEngineClient != 0 && !IsBadReadPtr((void*)s_pEngineClient, sizeof(uintptr_t))) {
        uintptr_t engineClientVtable = *(uintptr_t*)s_pEngineClient;
        if (engineClientVtable != 0) {
            uintptr_t* pGetPlayerInfoFuncAddrLocation = (uintptr_t*)(engineClientVtable + 0x80);
            if (!IsBadReadPtr(pGetPlayerInfoFuncAddrLocation, sizeof(uintptr_t))) {
                uintptr_t getPlayerInfoFuncAddr = *pGetPlayerInfoFuncAddrLocation;
                if (getPlayerInfoFuncAddr != 0) {
                    tEngineClient_GetPlayerInfo pGetPlayerInfo = (tEngineClient_GetPlayerInfo)getPlayerInfoFuncAddr;
                    bool funcReturnedSuccess = pGetPlayerInfo(s_pEngineClient, a3, &playerInfo);
                    playerInfo.szName[sizeof(playerInfo.szName) - 1] = '\0'; // GUARANTEE termination
                    if (funcReturnedSuccess && playerInfo.szName[0] != '\0') {
                        nameRetrievedSuccessfully = true;
                    }
                }
            }
        }
    }
    const char* nameToUse = nameRetrievedSuccessfully ? playerInfo.szName : "?UNKNOWN?";

    // 2. Get Prefixes (remain the same)
    const wchar_t* deadPrefix = L"";
    const wchar_t* teamPrefix = L"";
    if (s_pVGuiLocalize != 0 && !IsBadReadPtr((void*)s_pVGuiLocalize, sizeof(uintptr_t))) {
        uintptr_t localizeVtable = *(uintptr_t*)s_pVGuiLocalize;
        if (localizeVtable != 0) {
            uintptr_t* pLocalizeFindFuncAddrLocation = (uintptr_t*)(localizeVtable + 0x58); // Offset 88
            if (!IsBadReadPtr(pLocalizeFindFuncAddrLocation, sizeof(uintptr_t))) {
                uintptr_t localizeFindFuncAddr = *pLocalizeFindFuncAddrLocation;
                if (localizeFindFuncAddr != 0) {
                    tLocalize_Find pLocalizeFind = (tLocalize_Find)localizeFindFuncAddr;
                    if (a5) {
                        const wchar_t* foundPrefix = pLocalizeFind(s_pVGuiLocalize, "#HUD_CHAT_DEAD_PREFIX");
                        if (foundPrefix) { deadPrefix = foundPrefix; }
                    }
                    if (a4) {
                        const wchar_t* foundPrefix = pLocalizeFind(s_pVGuiLocalize, "#HUD_CHAT_TEAM_PREFIX");
                        if (foundPrefix) { teamPrefix = foundPrefix; }
                    }
                }
            }
        }
    }

    // 3. Assemble the final string (WideChar)
    finalMessageStringW.clear(); // Start fresh
    finalMessageStringW.append(deadPrefix);
    finalMessageStringW.append(teamPrefix);

    // Convert and append name - check result
    std::wstring wideName = MultiByteToWide(nameToUse, -1);
    // --- Add a check here ---
    // Breakpoint: Inspect wideName. Is it valid? Does it have weird characters?
    finalMessageStringW.append(wideName);

    // --- Add a check here ---
    // Breakpoint: Inspect finalMessageStringW. Does it look correct so far?

    // *** TRY ALTERNATIVE APPENDS FOR COLON ***

    // Method 1: Character by character (most likely to bypass weird interactions)
    finalMessageStringW.push_back(L':');
    finalMessageStringW.push_back(L' ');

    // Method 2: Explicit array (if Method 1 fails)
    // const wchar_t colonSpace[] = { L':', L' ', L'\0' };
    // finalMessageStringW.append(colonSpace);

    // Method 3: Original append (keep commented out unless others fail)
    // finalMessageStringW.append(L": ");

    // --- Add a check here ---
    // Breakpoint: Inspect finalMessageStringW. Does it contain the colon and space now? Check length.

    // Append message
    if (a2 != nullptr && !IsBadReadPtr(a2, 1)) {
        finalMessageStringW.append(MultiByteToWide(a2, -1));
    }

    // 4. Convert wide string to multibyte and print using Msg()
    std::string finalMessageMB = WideToMultiByte(finalMessageStringW, CP_UTF8); // Or CP_ACP

    if (!finalMessageMB.empty()) {
        Msg("%s\n", finalMessageMB.c_str());
    }

    // --- Call the original function ---
    if (oCHudChat__FormatAndDisplayMessage) {
        return oCHudChat__FormatAndDisplayMessage(a1, a2, a3, a4, a5);
    }
    else {
        return 0;
    }
}
char (*oMsgFunc__SayText)(__int64 a1);
char __fastcall MsgFunc__SayText(__int64 a1) {
    recurse = 0;
    return oMsgFunc__SayText(a1);
}

// from bme
typedef _QWORD* (__fastcall* sub_18017E140_type)(_QWORD*, __int64, char*);
sub_18017E140_type sub_18017E140_org;
_QWORD* __fastcall sub_18017E140(_QWORD* a1, __int64 a2, char* a3)
{
    auto ret = sub_18017E140_org(a1, a2, a3);
    *(bool*)(a1[77] + 1011) = true; // enable Unicode input in CBaseHudChatEntry
    return ret;
}

void InitClient()
{
	auto client = G_client;
	auto engine = G_engine;
	MH_CreateHook((LPVOID)(client + 0x21FE50), &PredictionErrorFn, reinterpret_cast<LPVOID*>(NULL));
	//MH_CreateHook((LPVOID)(client + 0x029840), &C_BaseEntity__VPhysicsInitNormal, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(client + 0x27F2C0), &sub_18027F2C0, reinterpret_cast<LPVOID*>(&sub_18027F2C0Original));
	//MH_CreateHook((LPVOID)(engine + 0x56A450), &vsnprintf_l_hk, NULL);
	//MH_CreateHook((LPVOID)(client + 0x744864), &vsnprintf_l_hk, NULL);
	MH_CreateHook((LPVOID)(engine + 0x102D50), &Cbuf_AddText, reinterpret_cast<LPVOID*>(&Cbuf_AddTextOriginal));
	MH_CreateHook((LPVOID)(engine + 0x4801B0), &ConVar_PrintDescription, reinterpret_cast<LPVOID*>(&ConVar_PrintDescriptionOriginal));
	MH_CreateHook((LPVOID)(engine + 0x47FB00), &CConVar__GetSplitScreenPlayerSlot, NULL);
	MH_CreateHook((LPVOID)(engine + 0x4722E0), &sub_1804722E0, 0);

	//MH_CreateHook((LPVOID)(client + 0x4A6150), &WeaponXRegisterClient, reinterpret_cast<LPVOID*>(&oWeaponXRegisterClient));
	MH_CreateHook((LPVOID)(client + 0x959F0), &CPortalPlayer__CreateMove, reinterpret_cast<LPVOID*>(&oCPortalPlayer__CreateMove));
	MH_CreateHook((LPVOID)(client + 0x8E820), &sub_18008E820, reinterpret_cast<LPVOID*>(&osub_18008E820));
	MH_CreateHook((LPVOID)(engine + 0x47A410), &KeyValues__SetString__Client, reinterpret_cast<LPVOID*>(&oKeyValues__SetString__Client));
	MH_CreateHook((LPVOID)(client + 0x286F50), &SharedVehicleViewSmoothing, reinterpret_cast<LPVOID*>(&oSharedVehicleViewSmoothing));
	MH_CreateHook((LPVOID)(client + 0x360210), &CGameUI__StartProgressBar, NULL);
	MH_CreateHook((LPVOID)(client + 0x3601C0), &CGameUI__UpdateProgressBar, reinterpret_cast<LPVOID*>(&oCGameUI__UpdateProgressBar));
	MH_CreateHook((LPVOID)(client + 0x17D440), &CHudChat__FormatAndDisplayMessage_Hooked, reinterpret_cast<LPVOID*>(&oCHudChat__FormatAndDisplayMessage));
    MH_CreateHook((LPVOID)(client + 0x17DAA0), &MsgFunc__SayText, reinterpret_cast<LPVOID*>(&oMsgFunc__SayText));
    MH_CreateHook((LPVOID)(client + 0x17E140), &sub_18017E140, reinterpret_cast<LPVOID*>(&sub_18017E140_org));

	if (IsNoOrigin())
		MH_CreateHook((LPVOID)GetProcAddress(GetModuleHandleA("ws2_32.dll"), "getaddrinfo"), &hookedGetAddrInfo, reinterpret_cast<LPVOID*>(&originalGetAddrInfo));

#if BUILD_DEBUG
	//if (!InitNetChanWarningHooks())
	//	MessageBoxA(NULL, "Failed to initialize warning hooks", "ERROR", 16);
#endif
	MH_EnableHook(MH_ALL_HOOKS);
}