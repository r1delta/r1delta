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

	if (IsNoOrigin())
		MH_CreateHook((LPVOID)GetProcAddress(GetModuleHandleA("ws2_32.dll"), "getaddrinfo"), &hookedGetAddrInfo, reinterpret_cast<LPVOID*>(&originalGetAddrInfo));

#if BUILD_DEBUG
	//if (!InitNetChanWarningHooks())
	//	MessageBoxA(NULL, "Failed to initialize warning hooks", "ERROR", 16);
#endif
	MH_EnableHook(MH_ALL_HOOKS);
}