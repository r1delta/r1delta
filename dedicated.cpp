#include "dedicated.h"
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

typedef void (*CUtlBuffer__SetExternalBufferType)(__int64 a1, __int64 a2, __int64 a3, __int64 a4, char a5);
CUtlBuffer__SetExternalBufferType CUtlBuffer__SetExternalBufferOriginal;
char* textbuf = 0;
__int64 pdefsize = 0;
void __fastcall CUtlBuffer__SetExternalBuffer(__int64 a1, __int64 a2, __int64 a3, __int64 a4, char a5)
{
	static uintptr_t engineDS = uintptr_t(GetModuleHandleA("engine_ds.dll"));
	if (uintptr_t(_ReturnAddress()) == (engineDS + 0x169F03)) {
		if (!textbuf)
			textbuf = (char*)malloc(0x50238);
		a2 = (__int64)(textbuf);
		a3 = 0x50238;
	}
	return CUtlBuffer__SetExternalBufferOriginal(a1, a2, a3, a4, a5);
}

__int64 __fastcall sub_160130(__int64* a1, __int64* a2)
{
	__int64 result; // rax

	*a1 = (__int64)textbuf;
	result = pdefsize;
	*a2 = pdefsize;
	return result;
}

long long* __fastcall unkfunc(long long* a1)
{
	*a1 = 0i64;
	a1[1] = 0i64;
	a1[2] = 0i64;
	a1[3] = 0i64;
	return a1;
}

typedef char(__fastcall* ParsePDATAType)(__int64 a1, __int64 a2, __int64 a3, const char* a4);
ParsePDATAType ParsePDATAOriginal;
char __fastcall ParsePDEF(__int64 a1, __int64 a2, __int64 a3, const char* a4)
{
	char v10[53264];
	static auto whatthefuck = (__int64 (*)(__int64 a1, int* a2, __int64 a3, int a4, unsigned int a5, unsigned int a6, unsigned int a7))(uintptr_t(GetModuleHandleA("engine.dll")) + 0x42A450);
	a1 = (__int64)(textbuf);
	pdefsize = a2;
	auto ret = ParsePDATAOriginal(a1, a2, a3, a4);
	int whatever = a2;
	memcpy(v10, (void*)a1, sizeof(v10));
	int v4 = whatthefuck(a1, &whatever, (__int64)v10, a2, 1u, 0, 0);
	return ret;
}

void InitDedicated()
{
	uintptr_t offsets[] = {
		// move g_ServerGlobalVariables_nTimestampNetworkingBase + 4
		0x552fc,
		// move g_ServerGlobalVariables_nTimestampRandomizeWindow + 4
		0x55303,
		// move g_ServerGlobalVariables_mapname_pszValue + 4
		0x5ec5f,
		// move g_ServerGlobalVariables_mapversion + 4
		0x286ba,
		0x286cb,
		0x2898c,
		0x5e7aa,
		// move g_ServerGlobalVariables_startspot + 4
		0x5ec81,
		// move g_ServerGlobalVariables_bMapLoadFailed + 4
		0xaa088,
		// move g_ServerGlobalVariables_deathmatch + 4
		0x5ec41,
		0x5ec4b,
		// move g_ServerGlobalVariables_coop + 4
		0x5ec2c,
		// move g_ServerGlobalVariables_maxEntities + 4
		0x5e823,
		0x61C10,
		// move g_ServerGlobalVariables_serverCount + 4
		0xa05d2,
		0xa178e,
		// move g_ServerGlobalVariables_pEdicts + 4
		0x5DBC7,
		0x5dc0c,
		0x5e89d,
		0x61c86
	};
	HMODULE hModule = GetModuleHandleA("engine_ds.dll");
	//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("dedicated.dll") + 0x84720), &ReadFileFromFilesystemHook, reinterpret_cast<LPVOID*>(&readFileFromFilesystem));

	for (auto offset : offsets) {
		int* address = reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(hModule) + offset);

		DWORD oldProtect;
		if (!VirtualProtect(address, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
			continue;
		}

		*address += 4;

		DWORD temp;
		VirtualProtect(address, sizeof(uintptr_t), oldProtect, &temp);

		FlushInstructionCache(GetCurrentProcess(), address, sizeof(uintptr_t));
	}

	MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x1693D0), &ParsePDEF, reinterpret_cast<LPVOID*>(&ParsePDATAOriginal));
	MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x3259C0), &CUtlBuffer__SetExternalBuffer, reinterpret_cast<LPVOID*>(&CUtlBuffer__SetExternalBufferOriginal));
	MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x160130), &sub_160130, reinterpret_cast<LPVOID*>(NULL));
	//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("dedicated.dll") + 0x1752B0), &ReadFileFromVPKHook, reinterpret_cast<LPVOID*>(&readFileFromVPK));
	//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("dedicated.dll") + 0x750F0), &ReadFromCacheHook, reinterpret_cast<LPVOID*>(&readFromCache));

	//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x433C0), &ProcessConnectionlessPacket, reinterpret_cast<LPVOID*>(&ProcessConnectionlessPacketOriginal));
	//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0xA1B90), &Host_InitDedicated, reinterpret_cast<LPVOID*>(&Host_InitDedicatedOriginal));
	//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x45C00), &CBaseClient__ProcessClientInfo, reinterpret_cast<LPVOID*>(NULL));
	//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x4C460), &sub_4C460, reinterpret_cast<LPVOID*>(NULL));
	//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x14E980), &sub_14E980, reinterpret_cast<LPVOID*>(NULL));
	uintptr_t tier0_base = reinterpret_cast<uintptr_t>(GetModuleHandleA("tier0_r1.dll"));

	//MH_CreateHook(reinterpret_cast<LPVOID>(tier0_base + 0xC490), &HookedAlloc, reinterpret_cast<void**>(&g_originalAlloc));
	//MH_CreateHook(reinterpret_cast<LPVOID>(tier0_base + 0xC600), &HookedFree, reinterpret_cast<void**>(&g_originalFree));
	//MH_CreateHook(reinterpret_cast<LPVOID>(tier0_base + 0xDDE0), &HookedRealloc, reinterpret_cast<void**>(&g_originalRealloc));
	//MH_CreateHook(reinterpret_cast<LPVOID>(tier0_base + 0xB350), &HookedGetSize, reinterpret_cast<void**>(&g_originalGetSize));
	//MH_CreateHook(reinterpret_cast<LPVOID>(tier0_base + 0xC930), &HookedRegionAlloc, reinterpret_cast<void**>(&g_originalRegionAlloc));
	//
	MH_EnableHook(MH_ALL_HOOKS);
	//static CustomBuffer customBuffer;
	//std::cout.rdbuf(&customBuffer);

}