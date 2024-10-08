#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "client.h"
#include "persistentdata.h"
#include "load.h"
#include "weaponxdebug.h"

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

typedef int (WSAAPI* GetAddrInfoFn)(PCSTR, PCSTR, const ADDRINFOA*, PADDRINFOA*);
GetAddrInfoFn originalGetAddrInfo = nullptr;

int WSAAPI hookedGetAddrInfo(PCSTR pNodeName, PCSTR pServiceName, const ADDRINFOA* pHints, PADDRINFOA* ppResult) {
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
void InitClient()
{
	auto client = G_client;
	auto engine = G_engine;
	MH_CreateHook((LPVOID)(client + 0x21FE50), &PredictionErrorFn, reinterpret_cast<LPVOID*>(NULL));
	//MH_CreateHook((LPVOID)(client + 0x029840), &C_BaseEntity__VPhysicsInitNormal, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(client + 0x27F2C0), &sub_18027F2C0, reinterpret_cast<LPVOID*>(&sub_18027F2C0Original));
	MH_CreateHook((LPVOID)(engine + 0x56A450), &vsnprintf_l_hk, NULL);
	MH_CreateHook((LPVOID)(client + 0x744864), &vsnprintf_l_hk, NULL);
	MH_CreateHook((LPVOID)(engine + 0x102D50), &Cbuf_AddText, reinterpret_cast<LPVOID*>(&Cbuf_AddTextOriginal));
	MH_CreateHook((LPVOID)(engine + 0x4801B0), &ConVar_PrintDescription, reinterpret_cast<LPVOID*>(&ConVar_PrintDescriptionOriginal));
	MH_CreateHook((LPVOID)(engine + 0x47FB00), &CConVar__GetSplitScreenPlayerSlot, NULL);
	MH_CreateHook((LPVOID)(engine + 0x4722E0), &sub_1804722E0, 0);

	MH_CreateHook((LPVOID)(client + 0x4A6150), &WeaponXRegisterClient, reinterpret_cast<LPVOID*>(&oWeaponXRegisterClient));
	MH_CreateHook((LPVOID)(client + 0x959F0), &CPortalPlayer__CreateMove, reinterpret_cast<LPVOID*>(&oCPortalPlayer__CreateMove));

	if (IsNoOrigin())
		MH_CreateHook((LPVOID)GetProcAddress(GetModuleHandleA("ws2_32.dll"), "getaddrinfo"), &hookedGetAddrInfo, reinterpret_cast<LPVOID*>(&originalGetAddrInfo));

	MH_EnableHook(MH_ALL_HOOKS);
}