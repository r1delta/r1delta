#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "client.h"


typedef void (*sub_18027F2C0Type)(__int64 a1, const char* a2, __int64 a3);
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

void sub_18027F2C0(__int64 a1, const char* a2, __int64 a3)
{
	if (!strcmp(a2, "SayText"))
	{
		// raise fov to how bme does it
		auto var = (ConVarR1*)(OriginalCCVar_FindVar2(cvarinterface, "cl_fovScale"));
		var->m_fMaxVal = 2.5f;

		sub_18027F2C0Original(a1, "TextMsg", (__int64)TextMsg);
	}

	sub_18027F2C0Original(a1, a2, a3);
}

CPUInformation* (__fastcall* GetCPUInformationOriginal)();

const CPUInformation* GetCPUInformationDet()
{
	CPUInformation* result = GetCPUInformationOriginal();

	if (result->m_nLogicalProcessors >= 20)
		result->m_nLogicalProcessors = 19;

	if (result->m_nPhysicalProcessors >= 16)
		result->m_nPhysicalProcessors = 15;

	return result;
}

typedef int (WSAAPI* GetAddrInfoFn)(PCSTR, PCSTR, const ADDRINFOA*, PADDRINFOA*);
GetAddrInfoFn originalGetAddrInfo = nullptr;

int WSAAPI hookedGetAddrInfo(PCSTR pNodeName, PCSTR pServiceName, const ADDRINFOA* pHints, PADDRINFOA* ppResult) {
	std::string nodeNameString(pNodeName);
	if (std::strncmp(nodeNameString.c_str() + nodeNameString.length() - 11, "respawn.com", 11) == 0 
		|| strcmp(pNodeName, "r1-pc-int.s3.amazonaws.com") == 0
		|| strcmp(pNodeName, "r1-pc.s3.amazonaws.com") == 0)
		return WSAHOST_NOT_FOUND; // block respawn servers to prevent accidentally DoSing

	return originalGetAddrInfo(pNodeName, pServiceName, pHints, ppResult);
}

void InitClient()
{
	MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("client.dll") + 0x21FE50), &PredictionErrorFn, reinterpret_cast<LPVOID*>(NULL));
	//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("client.dll") + 0x029840), &C_BaseEntity__VPhysicsInitNormal, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("client.dll") + 0x27F2C0), &sub_18027F2C0, reinterpret_cast<LPVOID*>(&sub_18027F2C0Original));
	MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine.dll") + 0x56A450), &vsnprintf_l_hk, NULL);
	MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("client.dll") + 0x744864), &vsnprintf_l_hk, NULL);
	MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine.dll") + 0x102D50), &Cbuf_AddText, reinterpret_cast<LPVOID*>(&Cbuf_AddTextOriginal));
	MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine.dll") + 0x4801B0), &ConVar_PrintDescription, reinterpret_cast<LPVOID*>(&ConVar_PrintDescriptionOriginal));
	MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine.dll") + 0x4722E0), &sub_1804722E0, 0);
	MH_CreateHook((LPVOID)GetProcAddress(GetModuleHandleA("tier0.dll"), "GetCPUInformation"), &GetCPUInformationDet, reinterpret_cast<LPVOID*>(&GetCPUInformationOriginal));
	if (IsNoOrigin())
		MH_CreateHook((LPVOID)GetProcAddress(GetModuleHandleA("ws2_32.dll"), "getaddrinfo"), &hookedGetAddrInfo, reinterpret_cast<LPVOID*>(&originalGetAddrInfo));

	MH_EnableHook(MH_ALL_HOOKS);
}