// Happiness has to be fought for.
// Disregard previous slogan, I'm going to kill myself - wndrr
#include <MinHook.h>
#include <cstdlib>
#include <crtdbg.h>	
#include <new>
#include "windows.h"
#include "tier0_orig.h"
#include "pch.h"
#include <iostream>
#include "cvar.h"
// Forward declaration of the IMemAlloc interface
class IMemAlloc {
public:
	virtual void* InternalAlloc(size_t nSize, const char* pFileName, int nLine) = 0;
	virtual void* Alloc(size_t nSize) = 0;
	virtual void* InternalRealloc(void* pMem, size_t nSize, const char* pFileName, int nLine) = 0;
	virtual void* Realloc(void* pMem, size_t nSize) = 0;
	virtual void InternalFree(void* pMem, const char* pFileName, int nLine) = 0;
	virtual void Free(void* pMem) = 0;
	virtual void Free2(void* pMem) = 0;
	virtual __int64 Returns0() = 0;
	virtual __int64 Returns02() = 0;
	virtual size_t GetSize(void* pMem) = 0;
	virtual void* RegionAlloc(int nRegion, size_t nSize, const char* pFileName, int nLine) = 0;
	virtual void CompactHeap() = 0;
	virtual size_t ComputeMemoryUsedBy(const char* pFileName) = 0;
	virtual bool CrtIsValidPointer(const void* pMem, unsigned int nSize, int nBlockUse) = 0;
	virtual bool CrtCheckMemory() = 0;
	virtual void CrtMemCheckpoint(void* pState) = 0;
	virtual void DumpStats() = 0;
	virtual void DumpStatsFileBase(const char* pPath) = 0;
	virtual bool IsDebugHeap() = 0;
	virtual void GetActualDbgInfo(const char** ppFileName, int* pLine) = 0;
	virtual void RegisterAllocation(const char* pFileName, int nLine, int nSize, int nBlockUse, int nSequence) = 0;
	virtual void NumberOfSystemCores() = 0;
	virtual void SetAllocFailHandler(unsigned int (*pfn)(unsigned int)) = 0;
	virtual void MemoryAllocFailed() = 0;
	virtual void CompactIncremental() = 0;
	virtual void OutOfMemory(unsigned int nSize) = 0;
	virtual void* AllocateVirtualMemorySection(unsigned int nSize) = 0;
	virtual int GetGenericMemoryStats(void** ppMemoryStats) = 0;
	virtual void InitDebugInfo(void* pMem, const char* pFileName, int nLine) = 0;
	// Destructor is virtual if there's a scalar deleting destructor entry
	virtual ~IMemAlloc() {}
};

//class CStdMemAlloc : public IMemAlloc {
//public:
//    void* InternalAlloc(size_t nSize, const char* pFileName, int nLine) override {
//        return malloc(nSize);
//    }
//
//    void* Alloc(size_t nSize) override {
//        return malloc(nSize);
//    }
//
//    void* InternalRealloc(void* pMem, size_t nSize, const char* pFileName, int nLine) override {
//        return realloc(pMem, nSize);
//    }
//
//    void* Realloc(void* pMem, size_t nSize) override {
//        return realloc(pMem, nSize);
//    }
//
//    void InternalFree(void* pMem, const char* pFileName, int nLine) override {
//        free(pMem);
//    }
//
//    void Free(void* pMem) override {
//        free(pMem);
//    }
//
//    void Free2(void* pMem) override {
//        free(pMem);
//    }
//    __int64 Returns0() override {
//        return 0;
//    }
//
//    __int64 Returns02() override {
//        return 0;
//    }
//    size_t GetSize(void* pMem) override {
//        // Not implementable with standard C library.
//        return 0;
//    }
//
//
//    void* RegionAlloc(int nRegion, size_t nSize, const char* pFileName, int nLine) override {
//        // This might require a custom implementation
//        return nullptr;
//    }
//
//    void CompactHeap() override {
//        // No standard equivalent
//    }
//
//    size_t ComputeMemoryUsedBy(const char* pFileName) override {
//        // No standard equivalent
//        return 0;
//    }
//
//    bool CrtIsValidPointer(const void* pMem, unsigned int nSize, int nBlockUse) override {
//        // Implementation depends on specific runtime checks
//        return false;
//    }
//
//    bool CrtCheckMemory() override {
//        // No standard equivalent
//        return true;
//    }
//
//    void CrtMemCheckpoint(_CrtMemState* pState) override {
//        // No standard equivalent
//    }
//
//    void DumpStats() override {
//        // No standard equivalent
//    }
//
//    void DumpStatsFileBase(const char* pPath) override {
//        // No standard equivalent
//    }
//
//    bool IsDebugHeap() override {
//        return false; // Standard library does not provide this information
//    }
//
//    void GetActualDbgInfo(const char** ppFileName, int* pLine) override {
//        // No standard equivalent
//    }
//
//    void RegisterAllocation(const char* pFileName, int nLine, int nSize, int nBlockUse, int nSequence) override {
//        // No standard equivalent
//    }
//
//    void NumberOfSystemCores() override {
//        // Requires platform-specific code
//    }
//
//    void SetAllocFailHandler(unsigned int (*pfn)(unsigned int)) override {
//        // No standard equivalent
//    }
//
//    void MemoryAllocFailed() override {
//        // No standard equivalent
//    }
//
//    void CompactIncremental() override {
//        // No standard equivalent
//    }
//
//    void OutOfMemory(unsigned int nSize) override {
//        // No standard equivalent
//    }
//
//    void* AllocateVirtualMemorySection(unsigned int nSize) override {
//        // Requires platform-specific implementation
//        return nullptr;
//    }
//
//    int GetGenericMemoryStats(void** ppMemoryStats) override {
//        // No standard equivalent
//        return 0;
//    }
//
//    void InitDebugInfo(void* pMem, const char* pFileName, int nLine) override {
//        // No standard equivalent
//    }
//};
// Global singleton pointer
IMemAlloc* g_pMemAllocSingleton = nullptr;

// Typedef for the function signature of CreateGlobalMemAlloc in tier0_r1.dll
typedef IMemAlloc* (*PFN_CreateGlobalMemAlloc)();

// CreateGlobalMemAlloc function
extern "C" __declspec(dllexport) IMemAlloc * CreateGlobalMemAlloc() {
	if (!g_pMemAllocSingleton) {
		// Load the tier0_r1.dll library
		HMODULE hModule = LoadLibraryA("tier0_r1.dll");
		if (hModule != nullptr) {
			// Get the address of CreateGlobalMemAlloc in the loaded library
			PFN_CreateGlobalMemAlloc pfnCreateGlobalMemAlloc = (PFN_CreateGlobalMemAlloc)GetProcAddress(hModule, "CreateGlobalMemAlloc");
			if (pfnCreateGlobalMemAlloc != nullptr) {
				// Call the function from the DLL and set the singleton
				g_pMemAllocSingleton = pfnCreateGlobalMemAlloc();
			}
			else {
				// Handle the error if the function is not found
				// You might want to log an error or perform other error handling here
			}
		}
		else {
			// Handle the error if the DLL is not loaded
			// You might want to log an error or perform other error handling here
		}
	}
	return g_pMemAllocSingleton;
}
__declspec(dllexport) void whatever2() { Error(); };
typedef void* (*CreateInterfaceFn)(const char* pName, int* pReturnCode);
CreateInterfaceFn oAppSystemFactory;
CreateInterfaceFn oFileSystemFactory;
CreateInterfaceFn oPhysicsFactory;
enum InitReturnVal_t
{
	INIT_FAILED = 0,
	INIT_OK,

	INIT_LAST_VAL,
};

enum AppSystemTier_t
{
	APP_SYSTEM_TIER0 = 0,
	APP_SYSTEM_TIER1,
	APP_SYSTEM_TIER2,
	APP_SYSTEM_TIER3,

	APP_SYSTEM_TIER_OTHER,
};
class IAppSystem
{
public:
	// Here's where the app systems get to learn about each other 
	virtual bool Connect(CreateInterfaceFn factory) = 0;
	virtual void Disconnect() = 0;

	// Here's where systems can access other interfaces implemented by this object
	// Returns NULL if it doesn't implement the requested interface
	virtual void* QueryInterface(const char* pInterfaceName) = 0;

	// Init, shutdown
	virtual InitReturnVal_t Init() = 0;
	virtual void Shutdown() = 0;

	// Returns all dependent libraries
	virtual const void* GetDependencies() { return NULL; }

	// Returns the tier
	virtual AppSystemTier_t GetTier() { return APP_SYSTEM_TIER_OTHER; }

	// Reconnect to a particular interface
	virtual void Reconnect(CreateInterfaceFn factory, const char* pInterfaceName) {}
};
class IEngineAPI : public IAppSystem
{
	// Functions
public:
	// This function must be called before init
	virtual bool SetStartupInfo(void* info) = 0;

	// Run the engine
	virtual int Run() = 0;

	// Sets the engine to run in a particular editor window
	virtual void SetEngineWindow(void* hWnd) = 0;

	// Sets the engine to run in a particular editor window
	virtual void PostConsoleCommand(const char* pConsoleCommand) = 0;

	// Are we running the simulation?
	virtual bool IsRunningSimulation() const = 0;

	// Start/stop running the simulation
	virtual void ActivateSimulation(bool bActive) = 0;

	// Reset the map we're on
	virtual void SetMap(const char* pMapName) = 0;
};
__int64 NULLNET_Config()
{
	return 0;
}
char* __fastcall NULLNET_GetPacket(int a1, __int64 a2, unsigned __int8 a3)
{
	return 0;
}
__int64(__fastcall* __fastcall CVEngineServer__GetNETConfig_TFO(__int64 a1, __int64 (**a2)()))()
{
	__int64(__fastcall* result)(); // rax

	result = NULLNET_Config;
	*a2 = NULLNET_Config;
	return result;
}
char* (__fastcall* __fastcall CVEngineServer__GetNETGetPacket_TFO(
	__int64 a1,
	char* (__fastcall** a2)(int a1, __int64 a2, unsigned __int8 a3)))(int a1, __int64 a2, unsigned __int8 a3)
{
	char* (__fastcall* result)(int, __int64, unsigned __int8); // rax

	result = NULLNET_GetPacket;
	*a2 = NULLNET_GetPacket;
	return result;
}
__int64(__fastcall* __fastcall CVEngineServer__GetLocalNETConfig_TFO(__int64 a1, __int64 (**a2)()))()
{
	__int64(__fastcall* result)(); // rax

	result = NULLNET_Config;
	*a2 = NULLNET_Config;
	return result;
}
char* (__fastcall* __fastcall CVEngineServer__GetLocalNETGetPacket_TFO(
	__int64 a1,
	char* (__fastcall** a2)(int a1, __int64 a2, unsigned __int8 a3)))(int a1, __int64 a2, unsigned __int8 a3)
{
	char* (__fastcall* result)(int, __int64, unsigned __int8); // rax

	result = NULLNET_GetPacket;
	*a2 = NULLNET_GetPacket;
	return result;
}
char CFileSystem_Stdio__NullSub3()
{
	return 0;
}
__int64 CFileSystem_Stdio__NullSub4()
{
	return 0i64;
}
__int64 CBaseFileSystem__GetTFOFileSystemThing()
{
	return 0;
}
__int64 __fastcall CFileSystem_Stdio__DoTFOFilesystemOp(__int64 a1, char* a2, rsize_t a3)
{
	return false;
}
void __fastcall CModelInfo__UnkTFOVoid(__int64 a1, int* a2, __int64 a3)
{

}
void __fastcall CModelInfo__UnkTFOVoid2(__int64 a1, __int64 a2)
{
}
void __fastcall CModelInfo__UnkTFOVoid3(__int64 a1, __int64 a2, unsigned __int64 a3)
{
	return;
}
void __fastcall CCVar__SetSomeFlag_Corrupt(__int64 a1, __int64 a2)
{
	return;
}
__int64 __fastcall CCVar__GetSomeFlag(__int64 thisptr)
{
	return 0;
}
char __fastcall CModelInfo__UnkTFOShouldRet0_2(__int64 a1, __int64 a2)
{
	if (a2)
		return *(char*)(a2 + 264) & 1;
	else
		return 0;
}
__int64 CModelInfo__ShouldRet0()
{
	return 0;
}
bool CModelInfo__ClientFullyConnected()
{
	return false;
}
bool byte_1824D16C0 = false;
void CModelInfo__UnkSetFlag()
{
	byte_1824D16C0 = 1;
}
void CModelInfo__UnkClearFlag()
{
	byte_1824D16C0 = 0;
}
__int64 CModelInfo__GetFlag()
{
	return (unsigned __int8)byte_1824D16C0;
}
void __fastcall CNetworkStringTableContainer__SetTickCount(__int64 a1, char a2)
{
	*(char*)(a1 + 8) = a2;
}


uintptr_t oCBaseFileSystem__Connect;
uintptr_t oCBaseFileSystem__Disconnect;
uintptr_t oCFileSystem_Stdio__QueryInterface;
uintptr_t oCBaseFileSystem__Init;
uintptr_t oCBaseFileSystem__Shutdown;
uintptr_t oCBaseFileSystem__GetDependencies;
uintptr_t oCBaseFileSystem__GetTier;
uintptr_t oCBaseFileSystem__Reconnect;
uintptr_t osub_180023F80;
uintptr_t osub_180023F90;
uintptr_t oCFileSystem_Stdio__AddSearchPath;
uintptr_t oCBaseFileSystem__RemoveSearchPath;
uintptr_t oCBaseFileSystem__RemoveAllSearchPaths;
uintptr_t oCBaseFileSystem__RemoveSearchPaths;
uintptr_t oCBaseFileSystem__MarkPathIDByRequestOnly;
uintptr_t oCBaseFileSystem__RelativePathToFullPath;
uintptr_t oCBaseFileSystem__GetSearchPath;
uintptr_t oCBaseFileSystem__AddPackFile;
uintptr_t oCBaseFileSystem__RemoveFile;
uintptr_t oCBaseFileSystem__RenameFile;
uintptr_t oCBaseFileSystem__CreateDirHierarchy;
uintptr_t oCBaseFileSystem__IsDirectory;
uintptr_t oCBaseFileSystem__FileTimeToString;
uintptr_t oCFileSystem_Stdio__SetBufferSize;
uintptr_t oCFileSystem_Stdio__IsOK;
uintptr_t oCFileSystem_Stdio__EndOfLine;
uintptr_t oCFileSystem_Stdio__ReadLine;
uintptr_t oCBaseFileSystem__FPrintf;
uintptr_t oCBaseFileSystem__LoadModule;
uintptr_t oCBaseFileSystem__UnloadModule;
uintptr_t oCBaseFileSystem__FindFirst;
uintptr_t oCBaseFileSystem__FindNext;
uintptr_t oCBaseFileSystem__FindIsDirectory;
uintptr_t oCBaseFileSystem__FindClose;
uintptr_t oCBaseFileSystem__FindFirstEx;
uintptr_t oCBaseFileSystem__FindFileAbsoluteList;
uintptr_t oCBaseFileSystem__GetLocalPath;
uintptr_t oCBaseFileSystem__FullPathToRelativePath;
uintptr_t oCBaseFileSystem__GetCurrentDirectory;
uintptr_t oCBaseFileSystem__FindOrAddFileName;
uintptr_t oCBaseFileSystem__String;
uintptr_t oCBaseFileSystem__AsyncReadMultiple;
uintptr_t oCBaseFileSystem__AsyncAppend;
uintptr_t oCBaseFileSystem__AsyncAppendFile;
uintptr_t oCBaseFileSystem__AsyncFinishAll;
uintptr_t oCBaseFileSystem__AsyncFinishAllWrites;
uintptr_t oCBaseFileSystem__AsyncFlush;
uintptr_t oCBaseFileSystem__AsyncSuspend;
uintptr_t oCBaseFileSystem__AsyncResume;
uintptr_t oCBaseFileSystem__AsyncBeginRead;
uintptr_t oCBaseFileSystem__AsyncEndRead;
uintptr_t oCBaseFileSystem__AsyncFinish;
uintptr_t oCBaseFileSystem__AsyncGetResult;
uintptr_t oCBaseFileSystem__AsyncAbort;
uintptr_t oCBaseFileSystem__AsyncStatus;
uintptr_t oCBaseFileSystem__AsyncSetPriority;
uintptr_t oCBaseFileSystem__AsyncAddRef;
uintptr_t oCBaseFileSystem__AsyncRelease;
uintptr_t osub_180024450;
uintptr_t osub_180024460;
uintptr_t onullsub_96;
uintptr_t osub_180024490;
uintptr_t osub_180024440;
uintptr_t onullsub_97;
uintptr_t osub_180009BE0;
uintptr_t osub_18000F6A0;
uintptr_t osub_180002CA0;
uintptr_t osub_180002CB0;
uintptr_t osub_1800154F0;
uintptr_t osub_180015550;
uintptr_t osub_180015420;
uintptr_t osub_180015480;
uintptr_t oCBaseFileSystem__RemoveLoggingFunc;
uintptr_t oCBaseFileSystem__GetFilesystemStatistics;
uintptr_t oCFileSystem_Stdio__OpenEx;
uintptr_t osub_18000A5D0;
uintptr_t osub_1800052A0;
uintptr_t osub_180002F10;
uintptr_t osub_18000A690;
uintptr_t osub_18000A6F0;
uintptr_t osub_1800057A0;
uintptr_t osub_180002960;
uintptr_t osub_180020110;
uintptr_t osub_180020230;
uintptr_t osub_180023660;
uintptr_t osub_1800204A0;
uintptr_t osub_180002F40;
uintptr_t osub_180004F00;
uintptr_t osub_180024020;
uintptr_t osub_180024AF0;
uintptr_t osub_180024110;
uintptr_t osub_180002580;
uintptr_t osub_180002560;
uintptr_t osub_18000A070;
uintptr_t osub_180009E80;
uintptr_t osub_180009C20;
uintptr_t osub_1800022F0;
uintptr_t osub_180002330;
uintptr_t osub_180009CF0;
uintptr_t osub_180002340;
uintptr_t osub_180002320;
uintptr_t osub_180009E00;
uintptr_t osub_180009F20;
uintptr_t osub_180009EA0;
uintptr_t osub_180009E50;
uintptr_t osub_180009FC0;
uintptr_t osub_180004E80;
uintptr_t osub_18000A000;
uintptr_t osub_180014350;
uintptr_t osub_18000F5B0;
uintptr_t osub_180002590;
uintptr_t osub_1800025D0;
uintptr_t osub_1800025E0;
uintptr_t oCFileSystem_Stdio__LoadVPKForMap;
uintptr_t oCFileSystem_Stdio__UnkFunc1;
uintptr_t oCFileSystem_Stdio__WeirdFuncThatJustDerefsRDX;
uintptr_t oCFileSystem_Stdio__GetPathTime;
uintptr_t oCFileSystem_Stdio__GetFSConstructedFlag;
uintptr_t oCFileSystem_Stdio__EnableWhitelistFileTracking;
uintptr_t osub_18000A750;
uintptr_t osub_180002B20;
uintptr_t osub_18001DC30;
uintptr_t osub_180002B30;
uintptr_t osub_180002BA0;
uintptr_t osub_180002BB0;
uintptr_t osub_180002BC0;
uintptr_t osub_180002290;
uintptr_t osub_18001CCD0;
uintptr_t osub_18001CCE0;
uintptr_t osub_18001CCF0;
uintptr_t osub_18001CD00;
uintptr_t osub_180014520;
uintptr_t osub_180002650;
uintptr_t osub_18001CD10;
uintptr_t osub_180016250;
uintptr_t osub_18000F0D0;
uintptr_t osub_1800139F0;
uintptr_t osub_180016570;
uintptr_t onullsub_86;
uintptr_t osub_18000AEC0;
uintptr_t osub_180003320;
uintptr_t osub_18000AF50;
uintptr_t osub_18000AF60;
uintptr_t osub_180005D00;
uintptr_t osub_18000AF70;
uintptr_t osub_18001B130;
uintptr_t osub_18000AF80;
uintptr_t osub_1800034D0;
uintptr_t osub_180017180;
uintptr_t osub_180003550;
uintptr_t osub_1800250D0;
uintptr_t osub_1800241B0;
uintptr_t osub_1800241C0;
uintptr_t osub_1800241F0;
uintptr_t osub_180024240;
uintptr_t osub_180024250;
uintptr_t osub_180024260;
uintptr_t osub_180024300;
uintptr_t osub_180024310;
uintptr_t osub_180024320;
uintptr_t osub_180024340;
uintptr_t osub_180024350;
uintptr_t osub_180024360;
uintptr_t osub_180024390;
uintptr_t osub_180024370;
uintptr_t osub_1800243C0;
uintptr_t osub_1800243F0;
uintptr_t osub_180024410;
uintptr_t osub_180024430;
uintptr_t fsinterface;
#define _BYTE char
char CBaseFileSystem__Connect(__int64 a1, __int64 a2) {
	return reinterpret_cast<char(*)(__int64, __int64)>(oCBaseFileSystem__Connect)(fsinterface, a2);
}

void CBaseFileSystem__Disconnect(__int64 a1) {
	reinterpret_cast<void(*)(__int64)>(oCBaseFileSystem__Disconnect)(fsinterface);
}

__int64 CFileSystem_Stdio__QueryInterface(__int64 a1, _BYTE* a2) {
	return reinterpret_cast<__int64(*)(__int64, _BYTE*)>(oCFileSystem_Stdio__QueryInterface)(fsinterface, a2);
}

__int64 CBaseFileSystem__Init(__int64 a1) {
	return reinterpret_cast<__int64(*)(__int64)>(oCBaseFileSystem__Init)(fsinterface);
}

__int64 CBaseFileSystem__Shutdown(__int64 a1) {
	return reinterpret_cast<__int64(*)(__int64)>(oCBaseFileSystem__Shutdown)(fsinterface);
}

__int64 CBaseFileSystem__GetDependencies(__int64 a1) {
	return reinterpret_cast<__int64(*)(__int64)>(oCBaseFileSystem__GetDependencies)(fsinterface);
}

__int64 CBaseFileSystem__GetTier(__int64 a1) {
	return reinterpret_cast<__int64(*)(__int64)>(oCBaseFileSystem__GetTier)(fsinterface);
}

__int64 CBaseFileSystem__Reconnect(__int64 a1, __int64 a2, __int64 a3) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64)>(oCBaseFileSystem__Reconnect)(fsinterface, a2, a3);
}


__int64 CFileSystem_Stdio__AddSearchPath(__int64 a1, const char* a2, __int64 a3, unsigned int a4) {
	return reinterpret_cast<__int64(*)(__int64, const char*, __int64, unsigned int)>(oCFileSystem_Stdio__AddSearchPath)(fsinterface, a2, a3, a4);
}

__int64 CBaseFileSystem__RemoveSearchPath(__int64 a1, const char* a2, __int64 a3) {
	return reinterpret_cast<__int64(*)(__int64, const char*, __int64)>(oCBaseFileSystem__RemoveSearchPath)(fsinterface, a2, a3);
}

void CBaseFileSystem__RemoveAllSearchPaths(__int64 a1) {
	reinterpret_cast<void(*)(__int64)>(oCBaseFileSystem__RemoveAllSearchPaths)(fsinterface);
}

__int64 CBaseFileSystem__RemoveSearchPaths(__int64 a1, char* a2) {
	return reinterpret_cast<__int64(*)(__int64, char*)>(oCBaseFileSystem__RemoveSearchPaths)(fsinterface, a2);
}

__int64 CBaseFileSystem__MarkPathIDByRequestOnly(__int64 a1, __int64 a2, unsigned __int8 a3) {
	return reinterpret_cast<__int64(*)(__int64, __int64, unsigned __int8)>(oCBaseFileSystem__MarkPathIDByRequestOnly)(fsinterface, a2, a3);
}

char* CBaseFileSystem__RelativePathToFullPath(__int64 a1, char* a2, __int64 a3, char* a4, size_t Count, int a6, char* a7) {
	return reinterpret_cast<char* (*)(__int64, char*, __int64, char*, size_t, int, char*)>(oCBaseFileSystem__RelativePathToFullPath)(fsinterface, a2, a3, a4, Count, a6, a7);
}

__int64 CBaseFileSystem__GetSearchPath(__int64 a1, __int64 a2, char a3, _BYTE* a4, __int64 a5) {
	return reinterpret_cast<__int64(*)(__int64, __int64, char, _BYTE*, __int64)>(oCBaseFileSystem__GetSearchPath)(fsinterface, a2, a3, a4, a5);
}

char CBaseFileSystem__AddPackFile(__int64 a1, const char* a2, __int64 a3) {
	return reinterpret_cast<char(*)(__int64, const char*, __int64)>(oCBaseFileSystem__AddPackFile)(fsinterface, a2, a3);
}

int* CBaseFileSystem__RemoveFile(__int64 a1, char* a2, __int64 a3) {
	return reinterpret_cast<int* (*)(__int64, char*, __int64)>(oCBaseFileSystem__RemoveFile)(fsinterface, a2, a3);
}

bool CBaseFileSystem__RenameFile(__int64 a1, _BYTE* a2, char* a3, const char* a4) {
	return reinterpret_cast<bool(*)(__int64, _BYTE*, char*, const char*)>(oCBaseFileSystem__RenameFile)(fsinterface, a2, a3, a4);
}

void CBaseFileSystem__CreateDirHierarchy(__int64 a1, char* a2, char* a3) {
	reinterpret_cast<void(*)(__int64, char* a2, char* a3)>(oCBaseFileSystem__CreateDirHierarchy)(fsinterface, a2, a3);
}

bool CBaseFileSystem__IsDirectory(__int64 a1, const char* a2, const char* a3) {
	return reinterpret_cast<bool(*)(__int64, const char*, const char*)>(oCBaseFileSystem__IsDirectory)(fsinterface, a2, a3);
}

__int64 CBaseFileSystem__FileTimeToString(__int64 a1, char* a2, signed __int64 a3, int a4) {
	return reinterpret_cast<__int64(*)(__int64, char*, signed __int64, int)>(oCBaseFileSystem__FileTimeToString)(fsinterface, a2, a3, a4);
}

__int64 CFileSystem_Stdio__SetBufferSize(__int64 a1, __int64 a2, __int64 a3) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64)>(oCFileSystem_Stdio__SetBufferSize)(fsinterface, a2, a3);
}

char CFileSystem_Stdio__IsOK(__int64 a1, __int64 a2) {
	return reinterpret_cast<char(*)(__int64, __int64)>(oCFileSystem_Stdio__IsOK)(fsinterface, a2);
}

char CFileSystem_Stdio__EndOfLine(__int64 a1, __int64 a2) {
	return reinterpret_cast<char(*)(__int64, __int64)>(oCFileSystem_Stdio__EndOfLine)(fsinterface, a2);
}

__int64 CFileSystem_Stdio__ReadLine(__int64 a1, __int64 a2, signed __int64 a3, __int64 a4) {
	return reinterpret_cast<__int64(*)(__int64, __int64, signed __int64, __int64)>(oCFileSystem_Stdio__ReadLine)(fsinterface, a2, a3, a4);
}

__int64 CBaseFileSystem__FPrintf(__int64 a1, __int64 a2, const char* a3, ...) {
	va_list args;
	va_start(args, a3);
	__int64 result = reinterpret_cast<__int64(*)(__int64, __int64, const char*, va_list)>(oCBaseFileSystem__FPrintf)(fsinterface, a2, a3, args);
	va_end(args);
	return result;
}

__int64 CBaseFileSystem__LoadModule(__int64 a1, char* a2, const char* a3) {
	return reinterpret_cast<__int64(*)(__int64, char*, const char*)>(oCBaseFileSystem__LoadModule)(fsinterface, a2, a3);
}

BOOL CBaseFileSystem__UnloadModule(__int64 a1, HMODULE a2) {
	return reinterpret_cast<BOOL(*)(__int64, HMODULE)>(oCBaseFileSystem__UnloadModule)(fsinterface, a2);
}

__int64 CBaseFileSystem__FindFirst(__int64 a1, __int64 a2, __int64 a3) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64)>(oCBaseFileSystem__FindFirst)(fsinterface, a2, a3);
}

__int64 CBaseFileSystem__FindNext(__int64 a1, unsigned __int16 a2) {
	return reinterpret_cast<__int64(*)(__int64, unsigned __int16)>(oCBaseFileSystem__FindNext)(fsinterface, a2);
}

__int64 CBaseFileSystem__FindIsDirectory(__int64 a1, unsigned __int16 a2) {
	return reinterpret_cast<__int64(*)(__int64, unsigned __int16)>(oCBaseFileSystem__FindIsDirectory)(fsinterface, a2);
}

char CBaseFileSystem__FindClose(__int64 a1, __int64 a2) {
	return reinterpret_cast<char(*)(__int64, __int64)>(oCBaseFileSystem__FindClose)(fsinterface, a2);
}

__int64 CBaseFileSystem__FindFirstEx(__int64 a1, const char* pWildCard, const char* pPathID, int* pHandle) {
	return reinterpret_cast<__int64(*)(__int64, const char*, const char*, int*)>(oCBaseFileSystem__FindFirstEx)(fsinterface, pWildCard, pPathID, pHandle);
}

__int64 CBaseFileSystem__FindFileAbsoluteList(__int64 a1, __int64 a2, __int64 a3, __int64 a4) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64, __int64)>(oCBaseFileSystem__FindFileAbsoluteList)(fsinterface, a2, a3, a4);
}

__int64 CBaseFileSystem__GetLocalPath(__int64 a1, __int64 a2, __int64 a3, __int64 a4) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64, __int64)>(oCBaseFileSystem__GetLocalPath)(fsinterface, a2, a3, a4);
}

__int64 CBaseFileSystem__FullPathToRelativePath(__int64 a1, __int64 a2, __int64 a3, __int64 a4) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64, __int64)>(oCBaseFileSystem__FullPathToRelativePath)(fsinterface, a2, a3, a4);
}

char CBaseFileSystem__GetCurrentDirectory(__int64 a1, CHAR* a2, DWORD a3) {
	return reinterpret_cast<char(*)(__int64, CHAR*, DWORD)>(oCBaseFileSystem__GetCurrentDirectory)(fsinterface, a2, a3);
}

__int64 CBaseFileSystem__FindOrAddFileName(__int64 a1, const char* pFileName) {
	return reinterpret_cast<__int64(*)(__int64, const char*)>(oCBaseFileSystem__FindOrAddFileName)(fsinterface, pFileName);
}

char CBaseFileSystem__String(__int64 a1, __int64 a2, _BYTE* a3, __int64 a4) {
	return reinterpret_cast<char(*)(__int64, __int64, _BYTE*, __int64)>(oCBaseFileSystem__String)(fsinterface, a2, a3, a4);
}

__int64 CBaseFileSystem__AsyncReadMultiple(__int64 a1, __int64 a2, __int64 a3, __int64 a4) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64, __int64)>(oCBaseFileSystem__AsyncReadMultiple)(fsinterface, a2, a3, a4);
}

__int64 CBaseFileSystem__AsyncAppend(__int64 a1,        const char* pFileName,
	const void* pSrc,
	int nSrcBytes,
	BOOL bFreeMemory,
	__int64 pControl) {
	return reinterpret_cast<__int64(*)(__int64, const char*, const void*, int, BOOL, __int64)>(oCBaseFileSystem__AsyncAppend)(fsinterface, pFileName, pSrc, nSrcBytes, bFreeMemory, pControl);
}

__int64 CBaseFileSystem__AsyncAppendFile(__int64 a1, __int64 a2, __int64 a3, __int64* a4) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64, __int64*)>(oCBaseFileSystem__AsyncAppendFile)(fsinterface, a2, a3, a4);
}

void CBaseFileSystem__AsyncFinishAll(__int64 a1, int a2) {
	reinterpret_cast<void(*)(__int64, int)>(oCBaseFileSystem__AsyncFinishAll)(fsinterface, a2);
}

void CBaseFileSystem__AsyncFinishAllWrites(__int64 a1) {
	reinterpret_cast<void(*)(__int64)>(oCBaseFileSystem__AsyncFinishAllWrites)(fsinterface);
}

__int64 CBaseFileSystem__AsyncFlush(__int64 a1) {
	return reinterpret_cast<__int64(*)(__int64)>(oCBaseFileSystem__AsyncFlush)(fsinterface);
}

char CBaseFileSystem__AsyncSuspend(__int64 a1) {
	return reinterpret_cast<char(*)(__int64)>(oCBaseFileSystem__AsyncSuspend)(fsinterface);
}

char CBaseFileSystem__AsyncResume(__int64 a1) {
	return reinterpret_cast<char(*)(__int64)>(oCBaseFileSystem__AsyncResume)(fsinterface);
}

__int64 CBaseFileSystem__AsyncBeginRead(__int64 a1, __int64 a2, __int64 a3) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64)>(oCBaseFileSystem__AsyncBeginRead)(fsinterface, a2, a3);
}

__int64 CBaseFileSystem__AsyncEndRead(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64, __int64)>(oCBaseFileSystem__AsyncEndRead)(fsinterface, a2);
}

__int64 CBaseFileSystem__AsyncFinish(__int64 a1, __int64 a2, char a3) {
	return reinterpret_cast<__int64(*)(__int64, __int64, char)>(oCBaseFileSystem__AsyncFinish)(fsinterface, a2, a3);
}

__int64 CBaseFileSystem__AsyncGetResult(__int64 a1, void* a2, __int64 a3, __int64 a4) {
	return reinterpret_cast<__int64(*)(__int64, void*, __int64, __int64)>(oCBaseFileSystem__AsyncGetResult)(fsinterface, a2, a3, a4);
}

__int64 CBaseFileSystem__AsyncAbort(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64, __int64)>(oCBaseFileSystem__AsyncAbort)(fsinterface, a2);
}

__int64 CBaseFileSystem__AsyncStatus(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64, __int64)>(oCBaseFileSystem__AsyncStatus)(fsinterface, a2);
}

__int64 CBaseFileSystem__AsyncSetPriority(__int64 a1, __int64 a2, int a3) {
	return reinterpret_cast<__int64(*)(__int64, __int64, int)>(oCBaseFileSystem__AsyncSetPriority)(fsinterface, a2, a3);
}

__int64 CBaseFileSystem__AsyncAddRef(__int64 a1, void* a2) {
	return reinterpret_cast<__int64(*)(__int64, void* a2)>(oCBaseFileSystem__AsyncAddRef)(fsinterface, a2);
}

__int64 CBaseFileSystem__AsyncRelease(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64, __int64)>(oCBaseFileSystem__AsyncRelease)(fsinterface, a2);
}

__int64 sub_180024450(__int64 a) {
	return reinterpret_cast<__int64(*)(__int64)>(osub_180024450)(fsinterface);
}

char sub_180024460(__int64 a1, __int64 a2, void* a3, _BYTE* a4) {
	return reinterpret_cast<char(*)(__int64, __int64, void*, _BYTE*)>(osub_180024460)(fsinterface, a2, a3, a4);
}

void nullsub_96() {
	
}

__int64 sub_180024490() {
	return reinterpret_cast<__int64(*)()>(osub_180024490)();
}

char sub_180024440() {
	return reinterpret_cast<char(*)()>(osub_180024440)();
}

void nullsub_97() {
}

__int64 sub_180009BE0(__int64 a1) {
	return reinterpret_cast<__int64(*)(__int64)>(osub_180009BE0)(fsinterface);
}

void sub_18000F6A0(__int64 a1) {
	reinterpret_cast<void(*)(__int64)>(osub_18000F6A0)(fsinterface);
}

void sub_180002CA0(__int64 a1, __int64 a2) {
	reinterpret_cast<void(*)(__int64, __int64)>(osub_180002CA0)(fsinterface, a2);
}

void sub_180002CB0(__int64 a1, int a2) {
	reinterpret_cast<void(*)(__int64, int)>(osub_180002CB0)(fsinterface, a2);
}

__int64 sub_1800154F0(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64, __int64)>(osub_1800154F0)(fsinterface, a2);
}

unsigned __int64 sub_180015550(__int64 a1, __int64 a2) {
	return reinterpret_cast<unsigned __int64(*)(__int64, __int64)>(osub_180015550)(fsinterface, a2);
}

__int64 sub_180015420(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64, __int64)>(osub_180015420)(fsinterface, a2);
}

unsigned __int64 sub_180015480(__int64 a1, __int64 a2) {
	return reinterpret_cast<unsigned __int64(*)(__int64, __int64)>(osub_180015480)(fsinterface, a2);
}

__int64 CBaseFileSystem__RemoveLoggingFunc(__int64 a1, __int64 a2, __int64 a3) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64)>(oCBaseFileSystem__RemoveLoggingFunc)(fsinterface, a2, a3);
}

__int64 CBaseFileSystem__GetFilesystemStatistics(__int64 a1) {
	return reinterpret_cast<__int64(*)(__int64)>(oCBaseFileSystem__GetFilesystemStatistics)(fsinterface);
}

char* CFileSystem_Stdio__OpenEx(__int64 a1, _BYTE* a2, const char* a3, int a4, const char* a5, __int64 a6) {
	return reinterpret_cast<char* (*)(__int64, _BYTE*, const char*, int, const char*, __int64)>(oCFileSystem_Stdio__OpenEx)(fsinterface, a2, a3, a4, a5, a6);
}

__int64 sub_18000A5D0(__int64 a1, __int64 a2, __int64 a3, __int64 a4, __int64 a5) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64, __int64, __int64)>(osub_18000A5D0)(fsinterface, a2, a3, a4, a5);
}

__int64 __fastcall sub_1800052A0(
	void* a1,
	__int64 a2,
	__int64 a3,
	void* a4,
	char a5,
	char a6,
	__int64 a7,
	__int64 a8,
	__int64(__fastcall* a9)(__int64, __int64)) {
	return reinterpret_cast<__int64(*)(__int64 a1,
		__int64 a2,
		__int64 a3,
		void* a4,
		char a5,
		char a6,
		__int64 a7,
		__int64 a8,
		__int64(__fastcall * a9)(__int64, __int64))>(osub_1800052A0)(fsinterface, a2, a3, a4, a5, a6, a7, a8, a9);
}

__int64 sub_180002F10(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64, __int64)>(osub_180002F10)(fsinterface, a2);
}

__int64 sub_18000A690(__int64 a1) {
	return reinterpret_cast<__int64(*)(__int64)>(osub_18000A690)(fsinterface);
}

__int64 sub_18000A6F0(__int64 a1) {
	return reinterpret_cast<__int64(*)(__int64)>(osub_18000A6F0)(fsinterface);
}

__int64 sub_1800057A0(__int64 a1, __int64 a2, __int64 a3, const char* a4, const char* a5) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64, const char*, const char*)>(osub_1800057A0)(fsinterface, a2, a3, a4, a5);
}

__int64 sub_180002960(__int64 a1, __int64 a2, __int64 a3, __int64 a4) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64, __int64)>(osub_180002960)(fsinterface, a2, a3, a4);
}

__int64 sub_180020110(__int64 a1, __int64 a2, __int64 a3, __int64 a4, char a5, char a6, __int64* a7) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64, __int64, char, char, __int64*)>(osub_180020110)(fsinterface, a2, a3, a4, a5, a6, a7);
}

__int64 sub_180020230(__int64 a1, __int64 a2, __int64 a3, __int64 a4, char a5, char a6, __int64 a7) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64, __int64, char, char, __int64)>(osub_180020230)(fsinterface, a2, a3, a4, a5, a6, a7);
}

__int64 sub_180023660(__int64 a1, __int64 a2, unsigned int a3, __int64 a4, int a5, __int64 a6) {
	return reinterpret_cast<__int64(*)(__int64, __int64, unsigned int, __int64, int, __int64)>(osub_180023660)(fsinterface, a2, a3, a4, a5, a6);
}

__int64 sub_1800204A0(__int64 a1, int a2, unsigned __int8 a3, int a4, __int64 a5, __int64 a6, __int64* a7) {
	return reinterpret_cast<__int64(*)(__int64, int, unsigned __int8, int, __int64, __int64, __int64*)>(osub_1800204A0)(fsinterface, a2, a3, a4, a5, a6, a7);
}

char sub_180002F40(__int64 a1, const CHAR* a2, wchar_t* a3, unsigned __int64 a4) {
	return reinterpret_cast<char(*)(__int64, const CHAR*, wchar_t*, unsigned __int64)>(osub_180002F40)(fsinterface, a2, a3, a4);
}

bool sub_180004F00(__int64 a1, __int64 a2, __int64 a3, __int64 a4, void* a5) {
	return reinterpret_cast<bool(*)(__int64, __int64, __int64, __int64, void*)>(osub_180004F00)(fsinterface, a2, a3, a4, a5);
}

bool sub_180024020(__int64 a1, __int64 a2, unsigned __int64* a3, unsigned __int64* a4, unsigned __int64* a5) {
	return reinterpret_cast<bool(*)(__int64, __int64, unsigned __int64*, unsigned __int64*, unsigned __int64*)>(osub_180024020)(fsinterface, a2, a3, a4, a5);
}

void* sub_180024AF0(__int64 a1, __int64 a2, __int64 a3, unsigned __int64 a4) {
	return reinterpret_cast<void* (*)(__int64, __int64, __int64, unsigned __int64)>(osub_180024AF0)(fsinterface, a2, a3, a4);
}

void sub_180024110(__int64 a1, void* a2) {
	reinterpret_cast<void(*)(__int64, void*)>(osub_180024110)(fsinterface, a2);
}

char sub_180002580(__int64 a1, __int64 a2) {
	return reinterpret_cast<char(*)(__int64 , __int64)>(osub_180002580)(fsinterface, a2);
}

char sub_180002560() {
	return reinterpret_cast<char(*)()>(osub_180002560)();
}

void sub_18000A070(__int64 a1, int a2) {
	reinterpret_cast<void(*)(__int64, int a2)>(osub_18000A070)(fsinterface, a2);
}


char sub_180009C20(__int64 a1, char* a2, __int64 a3) {
	return reinterpret_cast<char(*)(__int64, char* a2, __int64 a3)>(osub_180009C20)(fsinterface, a2, a3);
}

char sub_1800022F0(__int64 a1, __int64 a2, unsigned int a3, __int64 a4) {
	return reinterpret_cast<char(*)(__int64, __int64, unsigned int, __int64)>(osub_1800022F0)(fsinterface, a2, a3, a4);
}

__int64 sub_180002330(__int64 a1, __int64 a2, unsigned int a3) {
	return reinterpret_cast<__int64(*)(__int64, __int64, unsigned int)>(osub_180002330)(fsinterface, a2, a3);
}

__int64 sub_180009CF0(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64, __int64)>(osub_180009CF0)(fsinterface, a2);
}

void* sub_180002340(__int64 a1, void* a2, int a3) {
	return reinterpret_cast<void* (*)(__int64, void* a2, int a3)>(osub_180002340)(fsinterface, a2, a3);
}

char sub_180002320() {
	return reinterpret_cast<char(*)()>(osub_180002320)();
}

char sub_180009E00(__int64 a1, int a2, char* a3, unsigned int a4) {
	return reinterpret_cast<char(*)(__int64, int, char*, unsigned int)>(osub_180009E00)(fsinterface, a2, a3, a4);
}

__int64 sub_180009F20(__int64 a1, const char* a2) {
	return reinterpret_cast<__int64(*)(__int64, const char*)>(osub_180009F20)(fsinterface, a2);
}

__int64 sub_180009EA0(__int64 a1, __int64 a2, __int64 a3, char* a4, unsigned int a5) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64, char*, unsigned int)>(osub_180009EA0)(fsinterface, a2, a3, a4, a5);
}

__int64 sub_180009E50(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64, __int64)>(osub_180009E50)(fsinterface, a2);
}

__int64 sub_180009FC0(__int64 a1) {
	return reinterpret_cast<__int64(*)(__int64)>(osub_180009FC0)(fsinterface);
}

__int64 sub_180004E80(__int64 a1, const char* a2) {
	return reinterpret_cast<__int64(*)(__int64, const char*)>(osub_180004E80)(fsinterface, a2);
}

char* sub_18000A000(__int64 a1, const char* a2) {
	return reinterpret_cast<char* (*)(__int64, const char*)>(osub_18000A000)(fsinterface, a2);
}

void sub_180014350(__int64 a1) {
	reinterpret_cast<void(*)(__int64)>(osub_180014350)(fsinterface);
}

__int64 sub_18000F5B0(__int64 a1) {
	return reinterpret_cast<__int64(*)(__int64)>(osub_18000F5B0)(fsinterface);
}

__int64 sub_180002590(__int64 a1, int a2, int a3, int a4, __int64 a5, __int64 a6, __int64 a7) {
	return reinterpret_cast<__int64(*)(__int64, int, int, int, __int64, __int64, __int64)>(osub_180002590)(fsinterface, a2, a3, a4, a5, a6, a7);
}

__int64 sub_1800025D0() {
	return reinterpret_cast<__int64(*)()>(osub_1800025D0)();
}

void sub_1800025E0(__int64 a1, __int64 a2, __int64 a3, void* a4) {
	reinterpret_cast<void(*)(__int64, __int64, __int64, void*)>(osub_1800025E0)(fsinterface, a2, a3, a4);
}

char CFileSystem_Stdio__LoadVPKForMap(__int64 a1, const char* a2) {
	return reinterpret_cast<char(*)(__int64, const char*)>(oCFileSystem_Stdio__LoadVPKForMap)(fsinterface, a2);
}

char CFileSystem_Stdio__UnkFunc1(__int64 a1, const char* a2, __int64 a3, char* a4, __int64 Count) {
	return reinterpret_cast<char(*)(__int64, const char*, __int64, char*, __int64)>(oCFileSystem_Stdio__UnkFunc1)(fsinterface, a2, a3, a4, Count);
}

__int64 CFileSystem_Stdio__WeirdFuncThatJustDerefsRDX(__int64 a1, unsigned __int16* a2) {
	return reinterpret_cast<__int64(*)(__int64, unsigned __int16*)>(oCFileSystem_Stdio__WeirdFuncThatJustDerefsRDX)(fsinterface, a2);
}

__int64 CFileSystem_Stdio__GetPathTime(__int64 a1, char* a2, __int64 a3) {
	return reinterpret_cast<__int64(*)(__int64, char*, __int64)>(oCFileSystem_Stdio__GetPathTime)(fsinterface, a2, a3);
}

__int64 CFileSystem_Stdio__GetFSConstructedFlag() {
	return reinterpret_cast<__int64(*)()>(oCFileSystem_Stdio__GetFSConstructedFlag)();
}

__int64 CFileSystem_Stdio__EnableWhitelistFileTracking(__int64 a1, unsigned __int8 a2) {
	return reinterpret_cast<__int64(*)(__int64, unsigned __int8)>(oCFileSystem_Stdio__EnableWhitelistFileTracking)(fsinterface, a2);
}

void sub_18000A750(__int64 a1, __int64 a2, __int64 a3, __int64 a4) {
	reinterpret_cast<void(*)(__int64, __int64, __int64, __int64)>(osub_18000A750)(fsinterface, a2, a3, a4);
}

__int64 sub_180002B20(__int64 a1) {
	return reinterpret_cast<__int64(*)(__int64)>(osub_180002B20)(fsinterface);
}

void sub_18001DC30(__int64 a1, __int64 a2, int a3, int a4) {
	reinterpret_cast<void(*)(__int64, __int64, int, int)>(osub_18001DC30)(fsinterface, a2, a3, a4);
}

__int64 sub_180002B30(__int64 a1, __int64 a2, __int64 a3, void* a4) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64, void*)>(osub_180002B30)(fsinterface, a2, a3, a4);
}

__int64 sub_180002BA0(__int64 a1, __int64 a2, int a3) {
	return reinterpret_cast<__int64(*)(__int64, __int64 a2, int a3)>(osub_180002BA0)(fsinterface, a2, a3);
}

__int64 sub_180002BB0(__int64 a1) {
	return reinterpret_cast<__int64(*)(__int64)>(osub_180002BB0)(fsinterface);
}

void sub_180002BC0(__int64 a1, int a2) {
	reinterpret_cast<void(*)(__int64, int)>(osub_180002BC0)(fsinterface, a2);
}

void sub_180002290(__int64 a1, __int64 a2) {
	reinterpret_cast<void(*)(__int64, __int64)>(osub_180002290)(fsinterface, a2);
}

__int64 sub_18001CCD0() {
	return reinterpret_cast<__int64(*)()>(osub_18001CCD0)();
}

__int64 sub_18001CCE0() {
	return reinterpret_cast<__int64(*)()>(osub_18001CCE0)();
}

__int64 sub_18001CCF0() {
	return reinterpret_cast<__int64(*)()>(osub_18001CCF0)();
}

__int64 sub_18001CD00() {
	return reinterpret_cast<__int64(*)()>(osub_18001CD00)();
}

__int64 sub_180014520(__int64 a1, char* a2, unsigned __int64 a3) {
	return reinterpret_cast<__int64(*)(__int64, char*, unsigned __int64)>(osub_180014520)(fsinterface, a2, a3);
}

__int64 sub_180002650() {
	return reinterpret_cast<__int64(*)()>(osub_180002650)();
}

__int64 sub_18001CD10(__int64 a1) {
	return reinterpret_cast<__int64(*)(__int64)>(osub_18001CD10)(fsinterface);
}

__int64 sub_180016250(__int64 a1, __int64 a2, __int64 a3) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64)>(osub_180016250)(fsinterface, a2, a3);
}

void sub_18000F0D0(__int64 a1, char* a2) {
	reinterpret_cast<void(*)(__int64, char*)>(osub_18000F0D0)(fsinterface, a2);
}

void sub_1800139F0(__int64 a1, __int64 a2) {
	reinterpret_cast<void(*)(__int64, __int64)>(osub_1800139F0)(fsinterface, a2);
}

__int64 sub_180016570(__int64 a1) {
	return reinterpret_cast<__int64(*)(__int64)>(osub_180016570)(fsinterface);
}

void nullsub_86() {
}

char sub_18000AEC0() {
	return reinterpret_cast<char(*)()>(osub_18000AEC0)();
}

char sub_180003320() {
	return reinterpret_cast<char(*)()>(osub_180003320)();
}

__int64 sub_18000AF50() {
	return reinterpret_cast<__int64(*)(__int64)>(osub_18000AF50)(fsinterface);
}

char sub_18000AF60() {
	return reinterpret_cast<char(*)(__int64)>(osub_18000AF60)(fsinterface);
}

__int64 sub_180005D00() {
	return reinterpret_cast<__int64(*)(__int64)>(osub_180005D00)(fsinterface);
}

char sub_18000AF70() {
	return reinterpret_cast<char(*)(__int64)>(osub_18000AF70)(fsinterface);
}

char sub_18001B130() {
	return reinterpret_cast<char(*)(__int64)>(osub_18001B130)(fsinterface);
}

char sub_18000AF80(__int64 a1, int a2) {
	return reinterpret_cast<char(*)(__int64, int)>(osub_18000AF80)(fsinterface, a2);
}

void sub_1800034D0(__int64 a1, float a2) {
	reinterpret_cast<void(*)(__int64, float)>(osub_1800034D0)(fsinterface, a2);
}

char sub_180017180() {
	return reinterpret_cast<char(*)(__int64)>(osub_180017180)(fsinterface);
}

__int64 sub_180003550() {
	return reinterpret_cast<__int64(*)(__int64)>(osub_180003550)(fsinterface);
}

__int64 sub_1800250D0(__int64 a1, CHAR* a2, char* a3, __int64 a4, __int64 a5, __int64 a6) {
	return reinterpret_cast<__int64(*)(__int64, CHAR*, char*, __int64, __int64, __int64)>(osub_1800250D0)(fsinterface, a2, a3, a4, a5, a6);
}

__int64 sub_1800241B0(__int64 a1, __int64 a2, __int64 a3) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64)>(osub_1800241B0)(fsinterface, a2, a3);
}

__int64 sub_1800241C0(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64, __int64)>(osub_1800241C0)(fsinterface, a2);
}

__int64 sub_1800241F0(__int64 a1, __int64 a2, __int64 a3, unsigned int a4) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64, unsigned int)>(osub_1800241F0)(fsinterface, a2, a3, a4);
}

__int64 sub_180024240(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64, __int64)>(osub_180024240)(fsinterface, a2);
}

__int64 sub_180024250(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64, __int64)>(osub_180024250)(fsinterface, a2);
}

__int64 sub_180024260(int a1, __int64 a2, __int64 a3, __int64 a4, __int64 a5) {
	return reinterpret_cast<__int64(*)(int, __int64, __int64, __int64, __int64)>(osub_180024260)(fsinterface, a2, a3, a4, a5);
}

__int64 sub_180024300(__int64 a1, __int64 a2, __int64 a3, __int64 a4) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64, __int64)>(osub_180024300)(fsinterface, a2, a3, a4);
}

__int64 sub_180024310(__int64 a1, __int64 a2, unsigned int a3) {
	return reinterpret_cast<__int64(*)(__int64, __int64, unsigned int)>(osub_180024310)(fsinterface, a2, a3);
}

__int64 sub_180024320(__int64 a1, __int64 a2, __int64 a3, __int64 a4) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64, __int64)>(osub_180024320)(fsinterface, a2, a3, a4);
}

__int64 sub_180024340(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64, __int64)>(osub_180024340)(fsinterface, a2);
}

__int64 sub_180024350(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64, __int64)>(osub_180024350)(fsinterface, a2);
}

__int64 sub_180024360(__int64 a1, __int64 a2, __int64 a3, __int64 a4) {
	return reinterpret_cast<__int64(*)(__int64, __int64, __int64, __int64)>(osub_180024360)(fsinterface, a2, a3, a4);
}

int sub_180024390(__int64 a1, const char* a2, struct _stat64i32* a3) {
	return reinterpret_cast<int(*)(__int64, const char*, struct _stat64i32*)>(osub_180024390)(fsinterface, a2, a3);
}

int sub_180024370(__int64 a1, const char* a2, int a3) {
	return reinterpret_cast<int(*)(__int64, const char*, int)>(osub_180024370)(fsinterface, a2, a3);
}

HANDLE sub_1800243C0(__int64 a1, const CHAR* a2, void* a3) {
	return reinterpret_cast<HANDLE(*)(__int64, const CHAR*, void*)>(osub_1800243C0)(fsinterface, a2, a3);
}

bool sub_1800243F0(__int64 a1, void* a2, struct _WIN32_FIND_DATAA* a3) {
	return reinterpret_cast<bool(*)(__int64, void*, struct _WIN32_FIND_DATAA*)>(osub_1800243F0)(fsinterface, a2, a3);
}

bool sub_180024410(__int64 a1, void* a2) {
	return reinterpret_cast<bool(*)(__int64, void*)>(osub_180024410)(fsinterface, a2);
}

__int64 sub_180024430(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64, __int64)>(osub_180024430)(fsinterface, a2);
}

uintptr_t modelinterface;
uintptr_t oCModelInfo_dtor_0;
uintptr_t oCModelInfoServer__GetModel;
uintptr_t oCModelInfoServer__GetModelIndex;
uintptr_t oCModelInfo__GetModelName;
uintptr_t oCModelInfo__GetVCollide;
uintptr_t oCModelInfo__GetVCollideEx;
uintptr_t oCModelInfo__GetVCollideEx2;
uintptr_t oCModelInfo__GetModelRenderBounds;
uintptr_t oCModelInfo__GetModelFrameCount;
uintptr_t oCModelInfo__GetModelType;
uintptr_t oCModelInfo__GetModelExtraData;
uintptr_t oCModelInfo__IsTranslucentTwoPass;
uintptr_t oCModelInfo__ModelHasMaterialProxy;
uintptr_t oCModelInfo__IsTranslucent;
uintptr_t oCModelInfo__NullSub1;
uintptr_t oCModelInfo__UnkFunc1;
uintptr_t oCModelInfo__UnkFunc2;
uintptr_t oCModelInfo__UnkFunc3;
uintptr_t oCModelInfo__UnkFunc4;
uintptr_t oCModelInfo__UnkFunc5;
uintptr_t oCModelInfo__UnkFunc6;
uintptr_t oCModelInfo__UnkFunc7;
uintptr_t oCModelInfo__UnkFunc8;
uintptr_t oCModelInfo__UnkFunc9;
uintptr_t oCModelInfo__UnkFunc10;
uintptr_t oCModelInfo__UnkFunc11;
uintptr_t oCModelInfo__UnkFunc12;
uintptr_t oCModelInfo__UnkFunc13;
uintptr_t oCModelInfo__UnkFunc14;
uintptr_t oCModelInfo__UnkFunc15;
uintptr_t oCModelInfo__UnkFunc16;
uintptr_t oCModelInfo__UnkFunc17;
uintptr_t oCModelInfo__UnkFunc18;
uintptr_t oCModelInfo__UnkFunc19;
uintptr_t oCModelInfo__UnkFunc20;
uintptr_t oCModelInfo__UnkFunc21;
uintptr_t oCModelInfo__UnkFunc22;
uintptr_t oCModelInfo__UnkFunc23;
uintptr_t oCModelInfo__NullSub2;
uintptr_t oCModelInfo__UnkFunc24;
uintptr_t oCModelInfo__UnkFunc25;
#define _QWORD __int64
#define _DWORD int
_QWORD* __fastcall CModelInfo_dtor_0(_QWORD* a1, char a2) {
	return reinterpret_cast<_QWORD * (*)(__int64 a1, char a2)>(oCModelInfo_dtor_0)((__int64)modelinterface, a2);
}

int* __fastcall CModelInfoServer__GetModel(__int64 a1, int a2) {
	return reinterpret_cast<int*(*)(__int64, int)>(oCModelInfoServer__GetModel)((__int64)modelinterface, a2);
}

int CModelInfoServer__GetModelIndex(__int64 a1, void* a2) {
	return reinterpret_cast<__int64(*)(__int64, void*)>(oCModelInfoServer__GetModelIndex)((__int64)modelinterface, a2);
}

const char* __fastcall CModelInfo__GetModelName(__int64 a1, __int64 a2) {
	return reinterpret_cast<const char* (*)(__int64 a1, __int64 a2)>(oCModelInfo__GetModelName)((__int64)modelinterface, a2);
}

__int64 __fastcall CModelInfo__GetVCollide(__int64 a1, int a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, int a2)>(oCModelInfo__GetVCollide)((__int64)modelinterface, a2);
}

__int64 __fastcall CModelInfo__GetVCollideEx(__int64* a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2)>(oCModelInfo__GetVCollideEx)((__int64)modelinterface, a2);
}

__int64 __fastcall CModelInfo__GetVCollideEx2(__int64 a1, __int64 a2, _DWORD* a3, _DWORD* a4) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2, _DWORD * a3, _DWORD * a4)>(oCModelInfo__GetVCollideEx2)((__int64)modelinterface, a2, a3, a4);
}

__int64 __fastcall CModelInfo__GetModelRenderBounds(__int64 a1, __int64 a2, _DWORD* a3, _DWORD* a4) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2, _DWORD * a3, _DWORD * a4)>(oCModelInfo__GetModelRenderBounds)((__int64)modelinterface, a2, a3, a4);
}


__int64 __fastcall CModelInfo__GetModelFrameCount(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2)>(oCModelInfo__GetModelFrameCount)((__int64)modelinterface, a2);
}

__int64 __fastcall CModelInfo__GetModelType(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2)>(oCModelInfo__GetModelType)((__int64)modelinterface, a2);
}


__int64 __fastcall CModelInfo__GetModelExtraData(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2)>(oCModelInfo__GetModelExtraData)((__int64)modelinterface, a2);
}

bool __fastcall CModelInfo__IsTranslucentTwoPass(__int64 a1, __int64 a2) {
	return reinterpret_cast<bool(*)(__int64 a1, __int64 a2)>(oCModelInfo__IsTranslucentTwoPass)((__int64)modelinterface, a2);
}

bool __fastcall CModelInfo__ModelHasMaterialProxy(__int64 a1, __int64 a2) {
	return reinterpret_cast<bool(*)(__int64 a1, __int64 a2)>(oCModelInfo__ModelHasMaterialProxy)((__int64)modelinterface, a2);
}

bool __fastcall CModelInfo__IsTranslucent(__int64 a1, __int64 a2) {
	return reinterpret_cast<bool(*)(__int64 a1, __int64 a2)>(oCModelInfo__IsTranslucent)((__int64)modelinterface, a2);
}

void CModelInfo__NullSub1() {
}

__int64 __fastcall CModelInfo__UnkFunc1(__int64 a1, __int64 a2, unsigned int a3, unsigned int a4) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2, unsigned int a3, unsigned int a4)>(oCModelInfo__UnkFunc1)((__int64)modelinterface, a2, a3, a4);
}

__int64 __fastcall CModelInfo__UnkFunc2(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2)>(oCModelInfo__UnkFunc2)((__int64)modelinterface, a2);
}

__int64 __fastcall CModelInfo__UnkFunc3(__int64 a1, __int64 a2, unsigned int a3, __int64 a4) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2, unsigned int a3, __int64 a4)>(oCModelInfo__UnkFunc3)((__int64)modelinterface, a2, a3, a4);
}

bool __fastcall CModelInfo__UnkFunc4(__int64 a1, __int64 a2) {
	return reinterpret_cast<bool(*)(__int64 a1, __int64 a2)>(oCModelInfo__UnkFunc4)((__int64)modelinterface, a2);
}

__int64 __fastcall CModelInfo__UnkFunc5(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2)>(oCModelInfo__UnkFunc5)((__int64)modelinterface, a2);
}

char __fastcall CModelInfo__UnkFunc6(__int64 a1, __int64 a2, __int64 a3) {
	return reinterpret_cast<char(*)(__int64 a1, __int64 a2, __int64 a3)>(oCModelInfo__UnkFunc6)((__int64)modelinterface, a2, a3);
}

float __fastcall CModelInfo__UnkFunc7(__int64 a1, __int64 a2) {
	return reinterpret_cast<float(*)(__int64 a1, __int64 a2)>(oCModelInfo__UnkFunc7)((__int64)modelinterface, a2);
}

__int64 CModelInfo__UnkFunc8() {
	return reinterpret_cast<__int64(*)(__int64)>(oCModelInfo__UnkFunc8)((__int64)modelinterface);
}

__int64 __fastcall CModelInfo__UnkFunc9(__int64 a1, __int64 a2, __int64* a3, __int64 a4) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2, __int64* a3, __int64 a4)>(oCModelInfo__UnkFunc9)((__int64)modelinterface, a2, a3, a4);
}

__int64 __fastcall CModelInfo__UnkFunc10(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2)>(oCModelInfo__UnkFunc10)((__int64)modelinterface, a2);
}

__int64 __fastcall CModelInfo__UnkFunc11(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2)>(oCModelInfo__UnkFunc11)((__int64)modelinterface, a2);
}

__int64 __fastcall CModelInfo__UnkFunc12(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2)>(oCModelInfo__UnkFunc12)((__int64)modelinterface, a2);
}

__int64 __fastcall CModelInfo__UnkFunc13(__int64 a1, __int64 a2, __int64 a3, int* a4, __int64 a5, int* a6) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2, __int64 a3, int* a4, __int64 a5, int* a6)>(oCModelInfo__UnkFunc13)((__int64)modelinterface, a2, a3, a4, a5, a6);
}

__int64 __fastcall CModelInfo__UnkFunc14(__int64 a1, int a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, int a2)>(oCModelInfo__UnkFunc14)((__int64)modelinterface, a2);
}

__int64 __fastcall CModelInfo__UnkFunc15(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2)>(oCModelInfo__UnkFunc15)((__int64)modelinterface, a2);
}

__int64 __fastcall CModelInfo__UnkFunc16(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2)>(oCModelInfo__UnkFunc16)((__int64)modelinterface, a2);
}

__int64 __fastcall CModelInfo__UnkFunc17(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2)>(oCModelInfo__UnkFunc17)((__int64)modelinterface, a2);
}

__int64 __fastcall CModelInfo__UnkFunc18(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2)>(oCModelInfo__UnkFunc18)((__int64)modelinterface, a2);
}

__int64 __fastcall CModelInfo__UnkFunc19(__int64 a1, unsigned int a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, unsigned int a2)>(oCModelInfo__UnkFunc19)((__int64)modelinterface, a2);
}

char __fastcall CModelInfo__UnkFunc20(__int64 a1, __int64 a2, __int64 a3, __int64 a4, __int64 a5) {
	return reinterpret_cast<char(*)(__int64 a1, __int64 a2, __int64 a3, __int64 a4, __int64 a5)>(oCModelInfo__UnkFunc20)((__int64)modelinterface, a2, a3, a4, a5);
}

__int64 __fastcall CModelInfo__UnkFunc21(__int64* a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2)>(oCModelInfo__UnkFunc21)((__int64)modelinterface, a2);
}

__int64 __fastcall CModelInfo__UnkFunc22(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2)>(oCModelInfo__UnkFunc22)((__int64)modelinterface, a2);
}

__int64 CModelInfo__UnkFunc23() {
	return reinterpret_cast<__int64(*)(__int64)>(oCModelInfo__UnkFunc23)((__int64)modelinterface);
}

void CModelInfo__NullSub2() {
	reinterpret_cast<void(*)(__int64)>(oCModelInfo__NullSub2)((__int64)modelinterface);
}

bool __fastcall CModelInfo__UnkFunc24(__int64 a1, __int64 a2) {
	return reinterpret_cast<bool(*)(__int64 a1, __int64 a2)>(oCModelInfo__UnkFunc24)((__int64)modelinterface, a2);
}

__int64 __fastcall CModelInfo__UnkFunc25(__int64 a1, __int64 a2) {
	return reinterpret_cast<__int64(*)(__int64 a1, __int64 a2)>(oCModelInfo__UnkFunc25)((__int64)modelinterface, a2);
}
uintptr_t stringtableinterface;
uintptr_t oCNetworkStringTableContainer_dtor;
uintptr_t oCNetworkStringTableContainer__CreateStringTable;
uintptr_t oCNetworkStringTableContainer__RemoveAllTables;
uintptr_t oCNetworkStringTableContainer__FindTable;
uintptr_t oCNetworkStringTableContainer__GetTable;
uintptr_t oCNetworkStringTableContainer__GetNumTables;
uintptr_t oCNetworkStringTableContainer__SetAllowClientSideAddString;
uintptr_t oCNetworkStringTableContainer__CreateDictionary;
void CNetworkStringTableContainer_dtor(__int64 a1, char a2) {
	reinterpret_cast<void(*)(__int64, char)>(oCNetworkStringTableContainer_dtor)(a1, a2);
}

__int64 CNetworkStringTableContainer__CreateStringTable(__int64 a1, const char* a2, unsigned int a3, int a4, int a5, int a6) {
	return reinterpret_cast<__int64(*)(__int64, const char*, unsigned int, int, int, int)>(oCNetworkStringTableContainer__CreateStringTable)(stringtableinterface, a2, a3, a4, a5, a6);
}

void CNetworkStringTableContainer__RemoveAllTables(__int64 a1) {
	reinterpret_cast<void(*)(__int64)>(oCNetworkStringTableContainer__RemoveAllTables)(stringtableinterface);
}

__int64 CNetworkStringTableContainer__FindTable(__int64 a1, unsigned __int8* a2) {
	return reinterpret_cast<__int64(*)(__int64, unsigned __int8*)>(oCNetworkStringTableContainer__FindTable)(stringtableinterface, a2);
}

__int64 CNetworkStringTableContainer__GetTable(__int64 a1, int a2) {
	return reinterpret_cast<__int64(*)(__int64, int)>(oCNetworkStringTableContainer__GetTable)(stringtableinterface, a2);
}

__int64 CNetworkStringTableContainer__GetNumTables(__int64 a1) {
	return reinterpret_cast<__int64(*)(__int64)>(oCNetworkStringTableContainer__GetNumTables)(stringtableinterface);
}

void CNetworkStringTableContainer__SetAllowClientSideAddString(__int64 a1, __int64 a2, char a3) {
	reinterpret_cast<void(*)(__int64, __int64, char)>(oCNetworkStringTableContainer__SetAllowClientSideAddString)(stringtableinterface, a2, a3);
}


void CNetworkStringTableContainer__CreateDictionary(__int64 a1, __int64 a2) {
	reinterpret_cast<void(*)(__int64, __int64)>(oCNetworkStringTableContainer__CreateDictionary)(stringtableinterface, a2);
}

uintptr_t oCCvar__Connect;
uintptr_t oCCvar__Disconnect;
uintptr_t oCCvar__QueryInterface;
uintptr_t oCCVar__Init;
uintptr_t oCCVar__Shutdown;
uintptr_t oCCvar__GetDependencies;
uintptr_t oCCVar__GetTier;
uintptr_t oCCVar__Reconnect;
uintptr_t oCCvar__AllocateDLLIdentifier;
uintptr_t oCCvar__UnregisterConCommands;
uintptr_t oCCvar__GetCommandLineValue;
uintptr_t oCCVar__Find;
uintptr_t oCCvar__InstallConsoleDisplayFunc;
uintptr_t oCCvar__RemoveConsoleDisplayFunc;
uintptr_t oCCvar__ConsoleColorPrintf;
uintptr_t oCCvar__ConsolePrintf;
uintptr_t oCCvar__ConsoleDPrintf;
uintptr_t oCCVar__RevertFlaggedConVars;
uintptr_t oCCvar__InstallCVarQuery;
uintptr_t oCCvar__SetMaxSplitScreenSlots;
uintptr_t oCCvar__GetMaxSplitScreenSlots;
uintptr_t oCCvar__GetConsoleDisplayFuncCount;
uintptr_t oCCvar__GetConsoleText;
uintptr_t oCCvar__IsMaterialThreadSetAllowed;
uintptr_t oCCvar__HasQueuedMaterialThreadConVarSets;
uintptr_t oCCvar__FactoryInternalIterator;
char CCvar__Connect(void* thisptr, void* (__cdecl* factory)(const char*, int*))
{
	return reinterpret_cast<char(*)(void*, void* (__cdecl*)(const char*, int*))>(oCCvar__Connect)((void*)cvarinterface, factory);
}

void CCvar__Disconnect(void* thisptr)
{
	return reinterpret_cast<void(*)(void*)>(oCCvar__Disconnect)((void*)cvarinterface);
}
char* CCvar__QueryInterface(void* thisptr, const char* pInterfaceName)
{
	return reinterpret_cast<char*(*)(void*, const char*)>(oCCvar__QueryInterface)((void*)cvarinterface, pInterfaceName);
}
int CCvar__Init(void* thisptr) {
	return 1;
}
void CCvar__Shutdown(void* thisptr) {
	
}
__int64 CCvar__GetDependencies()
{
	return 0;
}
__int64 CCVar__GetTier()
{
	return 4;
}
void CCvar__Reconnect(void* thisptr, void* factory, const char* pInterfaceName)
{
	return reinterpret_cast<void(*)(void*, void*, const char*)>(oCCVar__Reconnect)((void*)cvarinterface, factory, pInterfaceName);
}
int CCvar__AllocateDLLIdentifier(void* thisptr)
{
	return reinterpret_cast<int (*)(void*)>(oCCvar__AllocateDLLIdentifier)((void*)cvarinterface);
}
void CCvar__UnregisterConCommands(void* thisptr, int id)
{
	return reinterpret_cast<void (*)(void*, int id)>(oCCvar__UnregisterConCommands)((void*)cvarinterface, id);
}
const char* CCvar__GetCommandLineValue(void* thisptr, const char* pVariableName)
{
	return reinterpret_cast<const char* (*)(void*, const char*)>(oCCvar__GetCommandLineValue)((void*)cvarinterface, pVariableName);
}
void __fastcall CCVar__Find(__int64 a1, __int64 a2, __int64 a3) {
	return reinterpret_cast<void (*)(void*, __int64 a2, __int64 a3)>(oCCVar__Find)((void*)cvarinterface, a2, a3);
}
void CCvar__InstallConsoleDisplayFunc(void* thisptr, void* pDisplayFunc)
{
	return reinterpret_cast<void (*)(void*, void*)>(oCCvar__InstallConsoleDisplayFunc)((void*)cvarinterface, pDisplayFunc);
}
void CCvar__RemoveConsoleDisplayFunc(void* thisptr, void* pDisplayFunc) {
	return reinterpret_cast<void (*)(void*, void*)>(oCCvar__RemoveConsoleDisplayFunc)((void*)cvarinterface, pDisplayFunc);
}
void CCvar__ConsoleColorPrintf(void* a1, const void* clr, char* pFormat, ...)
{
	va_list args;
	va_start(args, pFormat);
	reinterpret_cast<void(*)(__int64, const void*, const char*, va_list)>(oCCvar__ConsoleColorPrintf)(fsinterface, clr, pFormat, args);
	va_end(args);
}
void CCvar__ConsolePrintf(void* a1, char* pFormat, ...)
{
	va_list args;
	va_start(args, pFormat);
	reinterpret_cast<void(*)(__int64, const char*, va_list)>(oCCvar__ConsolePrintf)(fsinterface, pFormat, args);
	va_end(args);
}
void CCvar__ConsoleDPrintf(void* a1, char* pFormat, ...)
{
	va_list args;
	va_start(args, pFormat);
	reinterpret_cast<void(*)(__int64, const char*, va_list)>(oCCvar__ConsoleDPrintf)(fsinterface, pFormat, args);
	va_end(args);
}
void CCvar__RevertFlaggedConVars(void* thisptr, int nFlag)
{
	return reinterpret_cast<void (*)(void* thisptr, int nFlag)>(oCCVar__RevertFlaggedConVars)((void*)cvarinterface, nFlag);
}
void CCvar__InstallCVarQuery(void* thisptr, void* pQuery)
{
	return reinterpret_cast<void (*)(void* thisptr, void* pQuery)>(oCCvar__InstallCVarQuery)((void*)cvarinterface, pQuery);
}
void CCvar__SetMaxSplitScreenSlots(void* thisptr, int nSlots)
{
	return reinterpret_cast<void (*)(void* thisptr, int nFlag)>(oCCvar__SetMaxSplitScreenSlots)((void*)cvarinterface, nSlots);
}
int CCvar__GetMaxSplitScreenSlots(void* thisptr)
{
	return reinterpret_cast<int (*)(void* thisptr)>(oCCvar__GetMaxSplitScreenSlots)((void*)cvarinterface);
}
int CCvar_GetConsoleDisplayFuncCount(void* thisptr)
{
	return reinterpret_cast<int (*)(void* thisptr)>(oCCvar__GetConsoleDisplayFuncCount)((void*)cvarinterface);
}
void CCvar__GetConsoleText(void* thisptr, int nDisplayFuncIndex, char* pchText, unsigned int bufSize)
{
	return reinterpret_cast<void (*)(void* thisptr, int nDisplayFuncIndex, char* pchText, unsigned int bufSize)>(oCCvar__GetConsoleText)((void*)cvarinterface, nDisplayFuncIndex, pchText, bufSize);
}
bool CCvar__IsMaterialThreadSetAllowed(void* thisptr)
{
	return reinterpret_cast<bool (*)(void* thisptr)>(oCCvar__IsMaterialThreadSetAllowed)((void*)cvarinterface);
}
bool CCvar__HasQueuedMaterialThreadConVarSets(void* thisptr)
{
	return reinterpret_cast<bool (*)(void* thisptr)>(oCCvar__HasQueuedMaterialThreadConVarSets)((void*)cvarinterface);
}
void* CCvar__FactoryInternalIterator(void* thisptr) {
	return reinterpret_cast<void* (*)(void* thisptr)>(oCCvar__FactoryInternalIterator)((void*)cvarinterface);
}
static HMODULE engineR1O;
static CreateInterfaceFn R1OCreateInterface;
void* R1OFactory(const char* pName, int* pReturnCode) {
	std::cout << "looking for " << pName << std::endl;

	if (!strcmp(pName, "VEngineServer022")) {
		std::cout << "wrapping VEngineServer022" << std::endl;

		uintptr_t* r1vtable = *(uintptr_t**)oAppSystemFactory(pName, pReturnCode);
		uintptr_t CVEngineServer__ChangeLevel = r1vtable[0];
		uintptr_t CVEngineServer__IsMapValid = r1vtable[1];
		uintptr_t CVEngineServer__GetMapCRC = r1vtable[2];
		uintptr_t CVEngineServer__IsDedicatedServer = r1vtable[3];
		uintptr_t CVEngineServer__IsInEditMode = r1vtable[4];
		uintptr_t CVEngineServer__GetLaunchOptions = r1vtable[5];
		uintptr_t CVEngineServer__PrecacheModel = r1vtable[6];
		uintptr_t CVEngineServer__PrecacheDecal = r1vtable[7];
		uintptr_t CVEngineServer__PrecacheGeneric = r1vtable[8];
		uintptr_t CVEngineServer__IsModelPrecached = r1vtable[9];
		uintptr_t CVEngineServer__IsDecalPrecached = r1vtable[10];
		uintptr_t CVEngineServer__IsGenericPrecached = r1vtable[11];
		uintptr_t CVEngineServer__IsSimulating = r1vtable[12];
		uintptr_t CVEngineServer__GetPlayerUserId = r1vtable[13];
		uintptr_t CVEngineServer__GetPlayerNetworkIDString = r1vtable[14];
		uintptr_t CVEngineServer__IsUserIDInUse = r1vtable[15];
		uintptr_t CVEngineServer__GetLoadingProgressForUserID = r1vtable[16];
		uintptr_t CVEngineServer__GetEntityCount = r1vtable[17];
		uintptr_t CVEngineServer__GetPlayerNetInfo = r1vtable[18];
		uintptr_t CVEngineServer__IsClientValid = r1vtable[19];
		uintptr_t CVEngineServer__PlayerChangesTeams = r1vtable[20];
		uintptr_t CVEngineServer__RequestClientScreenshot = r1vtable[21];
		uintptr_t CVEngineServer__CreateEdict = r1vtable[22];
		uintptr_t CVEngineServer__RemoveEdict = r1vtable[23];
		uintptr_t CVEngineServer__PvAllocEntPrivateData = r1vtable[24];
		uintptr_t CVEngineServer__FreeEntPrivateData = r1vtable[25];
		uintptr_t CVEngineServer__SaveAllocMemory = r1vtable[26];
		uintptr_t CVEngineServer__SaveFreeMemory = r1vtable[27];
		uintptr_t CVEngineServer__FadeClientVolume = r1vtable[28];
		uintptr_t CVEngineServer__ServerCommand = r1vtable[29];
		uintptr_t CVEngineServer__CBuf_Execute = r1vtable[30];
		uintptr_t CVEngineServer__ClientCommand = r1vtable[31];
		uintptr_t CVEngineServer__LightStyle = r1vtable[32];
		uintptr_t CVEngineServer__StaticDecal = r1vtable[33];
		uintptr_t CVEngineServer__EntityMessageBeginEx = r1vtable[34];
		uintptr_t CVEngineServer__EntityMessageBegin = r1vtable[35];
		uintptr_t CVEngineServer__UserMessageBegin = r1vtable[36];
		uintptr_t CVEngineServer__MessageEnd = r1vtable[37];
		uintptr_t CVEngineServer__ClientPrintf = r1vtable[38];
		uintptr_t CVEngineServer__Con_NPrintf = r1vtable[39];
		uintptr_t CVEngineServer__Con_NXPrintf = r1vtable[40];
		uintptr_t CVEngineServer__UnkFunc1 = r1vtable[41];
		uintptr_t CVEngineServer__NumForEdictinfo = r1vtable[42];
		uintptr_t CVEngineServer__UnkFunc2 = r1vtable[43];
		uintptr_t CVEngineServer__UnkFunc3 = r1vtable[44];
		uintptr_t CVEngineServer__CrosshairAngle = r1vtable[45];
		uintptr_t CVEngineServer__GetGameDir = r1vtable[46];
		uintptr_t CVEngineServer__CompareFileTime = r1vtable[47];
		uintptr_t CVEngineServer__LockNetworkStringTables = r1vtable[48];
		uintptr_t CVEngineServer__CreateFakeClient = r1vtable[49];
		uintptr_t CVEngineServer__GetClientConVarValue = r1vtable[50];
		uintptr_t CVEngineServer__ReplayReady = r1vtable[51];
		uintptr_t CVEngineServer__GetReplayFrame = r1vtable[52];
		uintptr_t CVEngineServer__UnkFunc4 = r1vtable[53];
		uintptr_t CVEngineServer__UnkFunc5 = r1vtable[54];
		uintptr_t CVEngineServer__UnkFunc6 = r1vtable[55];
		uintptr_t CVEngineServer__UnkFunc7 = r1vtable[56];
		uintptr_t CVEngineServer__UnkFunc8 = r1vtable[57];
		uintptr_t CEngineClient__ParseFile = r1vtable[58];
		uintptr_t CEngineClient__CopyLocalFile = r1vtable[59];
		uintptr_t CVEngineServer__PlaybackTempEntity = r1vtable[60];
		uintptr_t CVEngineServer__UnkFunc9 = r1vtable[61];
		uintptr_t CVEngineServer__LoadGameState = r1vtable[62];
		uintptr_t CVEngineServer__LoadAdjacentEnts = r1vtable[63];
		uintptr_t CVEngineServer__ClearSaveDir = r1vtable[64];
		uintptr_t CVEngineServer__GetMapEntitiesString = r1vtable[65];
		uintptr_t CVEngineServer__GetPlaylistCount = r1vtable[66];
		uintptr_t CVEngineServer__GetUnknownPlaylistKV = r1vtable[67];
		uintptr_t CVEngineServer__GetPlaylistValPossible = r1vtable[68];
		uintptr_t CVEngineServer__GetPlaylistValPossibleAlt = r1vtable[69];
		uintptr_t CVEngineServer__AddPlaylistOverride = r1vtable[70];
		uintptr_t CVEngineServer__MarkPlaylistReadyForOverride = r1vtable[71];
		uintptr_t CVEngineServer__UnknownPlaylistSetup = r1vtable[72];
		uintptr_t CVEngineServer__GetUnknownPlaylistKV2 = r1vtable[73];
		uintptr_t CVEngineServer__GetUnknownPlaylistKV3 = r1vtable[74];
		uintptr_t CVEngineServer__GetUnknownPlaylistKV4 = r1vtable[75];
		uintptr_t CVEngineServer__UnknownMapSetup = r1vtable[76];
		uintptr_t CVEngineServer__UnknownMapSetup2 = r1vtable[77];
		uintptr_t CVEngineServer__UnknownMapSetup3 = r1vtable[78];
		uintptr_t CVEngineServer__UnknownMapSetup4 = r1vtable[79];
		uintptr_t CVEngineServer__UnknownGamemodeSetup = r1vtable[80];
		uintptr_t CVEngineServer__UnknownGamemodeSetup2 = r1vtable[81];
		uintptr_t CVEngineServer__IsMatchmakingDedi = r1vtable[82];
		uintptr_t CVEngineServer__UnusedTimeFunc = r1vtable[83];
		uintptr_t CVEngineServer__IsClientSearching = r1vtable[84];
		uintptr_t CVEngineServer__UnkFunc11 = r1vtable[85];
		uintptr_t CVEngineServer__IsPrivateMatch = r1vtable[86];
		uintptr_t CVEngineServer__IsCoop = r1vtable[87];
		uintptr_t CVEngineServer__GetSkillFlag_Unused = r1vtable[88];
		uintptr_t CVEngineServer__TextMessageGet = r1vtable[89];
		uintptr_t CVEngineServer__UnkFunc12 = r1vtable[90];
		uintptr_t CVEngineServer__LogPrint = r1vtable[91];
		uintptr_t CVEngineServer__IsLogEnabled = r1vtable[92];
		uintptr_t CVEngineServer__IsWorldBrushValid = r1vtable[93];
		uintptr_t CVEngineServer__SolidMoved = r1vtable[94];
		uintptr_t CVEngineServer__TriggerMoved = r1vtable[95];
		uintptr_t CVEngineServer__CreateSpatialPartition = r1vtable[96];
		uintptr_t CVEngineServer__DestroySpatialPartition = r1vtable[97];
		uintptr_t CVEngineServer__UnkFunc13 = r1vtable[98];
		uintptr_t CVEngineServer__IsPaused = r1vtable[99];
		uintptr_t CVEngineServer__UnkFunc14 = r1vtable[100];
		uintptr_t CVEngineServer__UnkFunc15 = r1vtable[101];
		uintptr_t CVEngineServer__UnkFunc16 = r1vtable[102];
		uintptr_t CVEngineServer__UnkFunc17 = r1vtable[103];
		uintptr_t CVEngineServer__UnkFunc18 = r1vtable[104];
		uintptr_t CVEngineServer__UnkFunc19 = r1vtable[105];
		uintptr_t CVEngineServer__UnkFunc20 = r1vtable[106];
		uintptr_t CVEngineServer__UnkFunc21 = r1vtable[107];
		uintptr_t CVEngineServer__UnkFunc22 = r1vtable[108];
		uintptr_t CVEngineServer__UnkFunc23 = r1vtable[109];
		uintptr_t CVEngineServer__UnkFunc24 = r1vtable[110];
		uintptr_t CVEngineServer__UnkFunc25 = r1vtable[111];
		uintptr_t CVEngineServer__UnkFunc26 = r1vtable[112];
		uintptr_t CVEngineServer__UnkFunc27 = r1vtable[113];
		uintptr_t CVEngineServer__UnkFunc28 = r1vtable[114];
		uintptr_t CVEngineServer__UnkFunc29 = r1vtable[115];
		uintptr_t CVEngineServer__UnkFunc30 = r1vtable[116];
		uintptr_t CVEngineServer__UnkFunc31 = r1vtable[117];
		uintptr_t CVEngineServer__UnkFunc32 = r1vtable[118];
		uintptr_t CVEngineServer__UnkFunc33 = r1vtable[119];
		uintptr_t CVEngineServer__UnkFunc34 = r1vtable[120];
		uintptr_t CVEngineServer__InsertServerCommand = r1vtable[121];
		uintptr_t CVEngineServer__UnkFunc35 = r1vtable[122];
		uintptr_t CVEngineServer__UnkFunc36 = r1vtable[123];
		uintptr_t CVEngineServer__UnkFunc37 = r1vtable[124];
		uintptr_t CVEngineServer__UnkFunc38 = r1vtable[125];
		uintptr_t CVEngineServer__UnkFunc39 = r1vtable[126];
		uintptr_t CVEngineServer__UnkFunc40 = r1vtable[127];
		uintptr_t CVEngineServer__UnkFunc41 = r1vtable[128];
		uintptr_t CVEngineServer__UnkFunc42 = r1vtable[129];
		uintptr_t CVEngineServer__UnkFunc43 = r1vtable[130];
		uintptr_t CVEngineServer__UnkFunc44 = r1vtable[131];
		uintptr_t CVEngineServer__AllocLevelStaticData = r1vtable[132];
		uintptr_t CVEngineServer__UnkFunc45 = r1vtable[133];
		uintptr_t CVEngineServer__UnkFunc46 = r1vtable[134];
		uintptr_t CVEngineServer__UnkFunc47 = r1vtable[135];
		uintptr_t CVEngineServer__Pause = r1vtable[136];
		uintptr_t CVEngineServer__UnkFunc48 = r1vtable[137];
		uintptr_t CVEngineServer__UnkFunc49 = r1vtable[138];
		uintptr_t CVEngineServer__UnkFunc50 = r1vtable[139];
		uintptr_t CVEngineServer__NullSub1 = r1vtable[140];
		uintptr_t CVEngineServer__UnkFunc51 = r1vtable[141];
		uintptr_t CVEngineServer__UnkFunc52 = r1vtable[142];
		uintptr_t CVEngineServer__MarkTeamsAsBalanced_On = r1vtable[143];
		uintptr_t CVEngineServer__MarkTeamsAsBalanced_Off = r1vtable[144];
		uintptr_t CVEngineServer__UnkFunc53 = r1vtable[145];
		uintptr_t CVEngineServer__UnkFunc54 = r1vtable[146];
		uintptr_t CVEngineServer__UnkFunc55 = r1vtable[147];
		uintptr_t CVEngineServer__UnkFunc56 = r1vtable[148];
		uintptr_t CVEngineServer__UnkFunc57 = r1vtable[149];
		uintptr_t CVEngineServer__UnkFunc58 = r1vtable[150];
		uintptr_t CVEngineServer__UnkFunc59 = r1vtable[151];
		uintptr_t CVEngineServer__UnkFunc60 = r1vtable[152];
		uintptr_t CVEngineServer__UnkFunc61 = r1vtable[153];
		uintptr_t CVEngineServer__UnkFunc62 = r1vtable[154];
		uintptr_t CVEngineServer__UnkFunc63 = r1vtable[155];
		uintptr_t CVEngineServer__NullSub2 = r1vtable[156];
		uintptr_t CVEngineServer__UnkFunc64 = r1vtable[157];
		uintptr_t CVEngineServer__NullSub3 = r1vtable[158];
		uintptr_t CVEngineServer__NullSub4 = r1vtable[159];
		uintptr_t CVEngineServer__UnkFunc65 = r1vtable[160];
		uintptr_t CVEngineServer__UnkFunc66 = r1vtable[161];
		uintptr_t CVEngineServer__UnkFunc67 = r1vtable[162];
		uintptr_t CVEngineServer__UnkFunc68 = r1vtable[163];
		uintptr_t CVEngineServer__UnkFunc69 = r1vtable[164];
		uintptr_t CVEngineServer__UnkFunc_R1EXCLUSIVE = r1vtable[165];
		uintptr_t CVEngineServer__FuncThatReturnsFF_12 = r1vtable[166];
		uintptr_t CVEngineServer__FuncThatReturnsFF_11 = r1vtable[167];
		uintptr_t CVEngineServer__FuncThatReturnsFF_10 = r1vtable[168];
		uintptr_t CVEngineServer__FuncThatReturnsFF_9 = r1vtable[169];
		uintptr_t CVEngineServer__FuncThatReturnsFF_8 = r1vtable[170];
		uintptr_t CVEngineServer__FuncThatReturnsFF_7 = r1vtable[171];
		uintptr_t CVEngineServer__FuncThatReturnsFF_6 = r1vtable[172];
		uintptr_t CVEngineServer__FuncThatReturnsFF_5 = r1vtable[173];
		uintptr_t CVEngineServer__FuncThatReturnsFF_4 = r1vtable[174];
		uintptr_t CVEngineServer__FuncThatReturnsFF_3 = r1vtable[175];
		uintptr_t CVEngineServer__FuncThatReturnsFF_2 = r1vtable[176];
		uintptr_t CVEngineServer__FuncThatReturnsFF_1 = r1vtable[177];
		uintptr_t CVEngineServer__GetClientXUID = r1vtable[178];
		uintptr_t CVEngineServer__IsActiveApp = r1vtable[179];
		uintptr_t CVEngineServer__UnkFunc70 = r1vtable[180];
		uintptr_t CVEngineServer__Bandwidth_WriteBandwidthStatsToFile = r1vtable[181];
		uintptr_t CVEngineServer__UnkFunc71 = r1vtable[182];
		uintptr_t CVEngineServer__IsCheckpointMapLoad = r1vtable[183];
		uintptr_t CVEngineServer__UnkFunc72 = r1vtable[184];
		uintptr_t CVEngineServer__UnkFunc73 = r1vtable[185];
		uintptr_t CVEngineServer__UnkFunc74 = r1vtable[186];
		uintptr_t CVEngineServer__UnkFunc75 = r1vtable[187];
		uintptr_t CVEngineServer__UnkFunc76 = r1vtable[188];
		uintptr_t CVEngineServer__NullSub5 = r1vtable[189];
		uintptr_t CVEngineServer__NullSub6 = r1vtable[190];
		uintptr_t sub_1800FE440 = r1vtable[191];
		uintptr_t sub_1800FE450 = r1vtable[192];
		uintptr_t sub_1800FE3C0 = r1vtable[193];
		uintptr_t sub_1800FE3D0 = r1vtable[194];
		uintptr_t sub_1800FE3E0 = r1vtable[195];
		uintptr_t sub_1800FE400 = r1vtable[196];
		uintptr_t sub_1800FE410 = r1vtable[197];
		uintptr_t sub_1800FE420 = r1vtable[198];
		uintptr_t sub_1800FE460 = r1vtable[199];
		uintptr_t sub_1800FE470 = r1vtable[200];
		uintptr_t sub_1800FE480 = r1vtable[201];
		uintptr_t sub_1800FE490 = r1vtable[202];
		uintptr_t sub_1800FE4A0 = r1vtable[203];
		uintptr_t sub_1800FE4B0 = r1vtable[204];
		uintptr_t sub_1800FE4C0 = r1vtable[205];
		uintptr_t sub_1800FE4D0 = r1vtable[206];
		uintptr_t sub_1800FE4E0 = r1vtable[207];
		uintptr_t sub_1800FE4F0 = r1vtable[208];
		uintptr_t sub_1800FE500 = r1vtable[209];
		uintptr_t sub_1800FE510 = r1vtable[210];
		uintptr_t sub_1800FE530 = r1vtable[211];
		uintptr_t sub_1800FE540 = r1vtable[212];
		uintptr_t sub_1800FE550 = r1vtable[213];
		uintptr_t sub_1800FE560 = r1vtable[214];
		uintptr_t CVEngineServer__UpdateClientHashtag = r1vtable[215];
		uintptr_t CVEngineServer__UnkFunc77 = r1vtable[216];
		uintptr_t CVEngineServer__UnkFunc78 = r1vtable[217];
		uintptr_t CVEngineServer__UnkFunc79 = r1vtable[218];
		uintptr_t CVEngineServer__UnkFunc80 = r1vtable[219];
		uintptr_t CVEngineServer__UnkFunc81 = r1vtable[220];
		uintptr_t CVEngineServer__UnkFunc82 = r1vtable[221];
		static uintptr_t r1ovtable[] = { 
			CVEngineServer__NullSub1,
			CVEngineServer__ChangeLevel,
			CVEngineServer__IsMapValid,
			CVEngineServer__GetMapCRC,
			CVEngineServer__IsDedicatedServer,
			CVEngineServer__IsInEditMode,
			CVEngineServer__GetLaunchOptions,
			(uintptr_t)(&CVEngineServer__GetLocalNETConfig_TFO),
			(uintptr_t)(&CVEngineServer__GetNETConfig_TFO),
			(uintptr_t)(&CVEngineServer__GetLocalNETGetPacket_TFO),
			(uintptr_t)(&CVEngineServer__GetNETGetPacket_TFO),
			CVEngineServer__PrecacheModel,
			CVEngineServer__PrecacheDecal,
			CVEngineServer__PrecacheGeneric,
			CVEngineServer__IsModelPrecached,
			CVEngineServer__IsDecalPrecached,
			CVEngineServer__IsGenericPrecached,
			CVEngineServer__IsSimulating,
			CVEngineServer__GetPlayerUserId,
			CVEngineServer__GetPlayerUserId,
			CVEngineServer__GetPlayerNetworkIDString,
			CVEngineServer__IsUserIDInUse,
			CVEngineServer__GetLoadingProgressForUserID,
			CVEngineServer__GetEntityCount,
			CVEngineServer__GetPlayerNetInfo,
			CVEngineServer__IsClientValid,
			CVEngineServer__PlayerChangesTeams,
			CVEngineServer__RequestClientScreenshot,
			CVEngineServer__CreateEdict,
			CVEngineServer__RemoveEdict,
			CVEngineServer__PvAllocEntPrivateData,
			CVEngineServer__FreeEntPrivateData,
			CVEngineServer__SaveAllocMemory,
			CVEngineServer__SaveFreeMemory,
			CVEngineServer__FadeClientVolume,
			CVEngineServer__ServerCommand,
			CVEngineServer__CBuf_Execute,
			CVEngineServer__ClientCommand,
			CVEngineServer__LightStyle,
			CVEngineServer__StaticDecal,
			CVEngineServer__EntityMessageBeginEx,
			CVEngineServer__EntityMessageBegin,
			CVEngineServer__UserMessageBegin,
			CVEngineServer__MessageEnd,
			CVEngineServer__ClientPrintf,
			CVEngineServer__Con_NPrintf,
			CVEngineServer__Con_NXPrintf,
			CVEngineServer__UnkFunc1,
			CVEngineServer__NumForEdictinfo,
			CVEngineServer__UnkFunc2,
			CVEngineServer__UnkFunc3,
			CVEngineServer__CrosshairAngle,
			CVEngineServer__GetGameDir,
			CVEngineServer__CompareFileTime,
			CVEngineServer__LockNetworkStringTables,
			CVEngineServer__CreateFakeClient,
			CVEngineServer__GetClientConVarValue,
			CVEngineServer__ReplayReady,
			CVEngineServer__GetReplayFrame,
			CVEngineServer__UnkFunc4,
			CVEngineServer__UnkFunc5,
			CVEngineServer__UnkFunc6,
			CVEngineServer__UnkFunc7,
			CVEngineServer__UnkFunc8,
			CEngineClient__ParseFile,
			CEngineClient__CopyLocalFile,
			CVEngineServer__PlaybackTempEntity,
			CVEngineServer__UnkFunc9,
			CVEngineServer__LoadGameState,
			CVEngineServer__LoadAdjacentEnts,
			CVEngineServer__ClearSaveDir,
			CVEngineServer__GetMapEntitiesString,
			CVEngineServer__GetPlaylistCount,
			CVEngineServer__GetUnknownPlaylistKV,
			CVEngineServer__GetPlaylistValPossible,
			CVEngineServer__GetPlaylistValPossibleAlt,
			CVEngineServer__AddPlaylistOverride,
			CVEngineServer__MarkPlaylistReadyForOverride,
			CVEngineServer__UnknownPlaylistSetup,
			CVEngineServer__GetUnknownPlaylistKV2,
			CVEngineServer__GetUnknownPlaylistKV3,
			CVEngineServer__GetUnknownPlaylistKV4,
			CVEngineServer__UnknownMapSetup,
			CVEngineServer__UnknownMapSetup2,
			CVEngineServer__UnknownMapSetup3,
			CVEngineServer__UnknownMapSetup4,
			CVEngineServer__UnknownGamemodeSetup,
			CVEngineServer__UnknownGamemodeSetup2,
			CVEngineServer__IsMatchmakingDedi,
			CVEngineServer__UnusedTimeFunc,
			CVEngineServer__IsClientSearching,
			CVEngineServer__UnkFunc11,
			CVEngineServer__IsPrivateMatch,
			CVEngineServer__IsCoop,
			CVEngineServer__GetSkillFlag_Unused,
			CVEngineServer__TextMessageGet,
			CVEngineServer__UnkFunc12,
			CVEngineServer__LogPrint,
			CVEngineServer__IsLogEnabled,
			CVEngineServer__IsWorldBrushValid,
			CVEngineServer__SolidMoved,
			CVEngineServer__TriggerMoved,
			CVEngineServer__CreateSpatialPartition,
			CVEngineServer__DestroySpatialPartition,
			CVEngineServer__UnkFunc13,
			CVEngineServer__IsPaused,
			CVEngineServer__UnkFunc14,
			CVEngineServer__UnkFunc15,
			CVEngineServer__UnkFunc16,
			CVEngineServer__UnkFunc17,
			CVEngineServer__UnkFunc18,
			CVEngineServer__UnkFunc19,
			CVEngineServer__UnkFunc20,
			CVEngineServer__UnkFunc21,
			CVEngineServer__UnkFunc22,
			CVEngineServer__UnkFunc23,
			CVEngineServer__UnkFunc24,
			CVEngineServer__UnkFunc25,
			CVEngineServer__UnkFunc26,
			CVEngineServer__UnkFunc27,
			CVEngineServer__UnkFunc28,
			CVEngineServer__UnkFunc29,
			CVEngineServer__UnkFunc30,
			CVEngineServer__UnkFunc31,
			CVEngineServer__UnkFunc32,
			CVEngineServer__UnkFunc33,
			CVEngineServer__UnkFunc34,
			CVEngineServer__InsertServerCommand,
			CVEngineServer__UnkFunc35,
			CVEngineServer__UnkFunc36,
			CVEngineServer__UnkFunc37,
			CVEngineServer__UnkFunc38,
			CVEngineServer__UnkFunc39,
			CVEngineServer__UnkFunc40,
			CVEngineServer__UnkFunc41,
			CVEngineServer__UnkFunc42,
			CVEngineServer__UnkFunc43,
			CVEngineServer__UnkFunc44,
			CVEngineServer__AllocLevelStaticData,
			CVEngineServer__UnkFunc45,
			CVEngineServer__UnkFunc46,
			CVEngineServer__UnkFunc47,
			CVEngineServer__Pause,
			CVEngineServer__UnkFunc48,
			CVEngineServer__UnkFunc49,
			CVEngineServer__UnkFunc50,
			CVEngineServer__NullSub1,
			CVEngineServer__UnkFunc51,
			CVEngineServer__UnkFunc52,
			CVEngineServer__MarkTeamsAsBalanced_On,
			CVEngineServer__MarkTeamsAsBalanced_Off,
			CVEngineServer__UnkFunc53,
			CVEngineServer__UnkFunc54,
			CVEngineServer__UnkFunc55,
			CVEngineServer__UnkFunc56,
			CVEngineServer__UnkFunc57,
			CVEngineServer__UnkFunc58,
			CVEngineServer__UnkFunc59,
			CVEngineServer__UnkFunc60,
			CVEngineServer__UnkFunc61,
			CVEngineServer__UnkFunc62,
			CVEngineServer__UnkFunc63,
			CVEngineServer__NullSub2,
			CVEngineServer__UnkFunc64,
			CVEngineServer__NullSub3,
			CVEngineServer__NullSub4,
			CVEngineServer__UnkFunc65,
			CVEngineServer__UnkFunc66,
			CVEngineServer__UnkFunc67,
			CVEngineServer__UnkFunc68,
			CVEngineServer__UnkFunc69,
			CVEngineServer__FuncThatReturnsFF_12,
			CVEngineServer__FuncThatReturnsFF_11,
			CVEngineServer__FuncThatReturnsFF_10,
			CVEngineServer__FuncThatReturnsFF_9,
			CVEngineServer__FuncThatReturnsFF_8,
			CVEngineServer__FuncThatReturnsFF_7,
			CVEngineServer__FuncThatReturnsFF_6,
			CVEngineServer__FuncThatReturnsFF_5,
			CVEngineServer__FuncThatReturnsFF_4,
			CVEngineServer__FuncThatReturnsFF_3,
			CVEngineServer__FuncThatReturnsFF_2,
			CVEngineServer__FuncThatReturnsFF_1,
			CVEngineServer__GetClientXUID,
			CVEngineServer__IsActiveApp,
			CVEngineServer__UnkFunc70,
			CVEngineServer__Bandwidth_WriteBandwidthStatsToFile,
			CVEngineServer__UnkFunc71,
			CVEngineServer__IsCheckpointMapLoad,
			CVEngineServer__UnkFunc72,
			CVEngineServer__UnkFunc73,
			CVEngineServer__UnkFunc74,
			CVEngineServer__UnkFunc75,
			CVEngineServer__UnkFunc76,
			CVEngineServer__NullSub5,
			CVEngineServer__NullSub6,
			CVEngineServer__UpdateClientHashtag,
			CVEngineServer__UnkFunc77,
			CVEngineServer__UnkFunc78,
			CVEngineServer__UnkFunc79,
			CVEngineServer__UnkFunc80,
			CVEngineServer__UnkFunc81,
			CVEngineServer__UnkFunc82
		};
		static void* whatever2 = &r1ovtable;
		return &whatever2;
	}
	if (!strcmp(pName, "VFileSystem017")) {
		std::cout << "wrapping VFileSystem017" << std::endl;

		uintptr_t* r1vtable = *(uintptr_t**)oFileSystemFactory(pName, pReturnCode);
		fsinterface = (uintptr_t)oFileSystemFactory(pName, pReturnCode);
		oCBaseFileSystem__Connect = r1vtable[0];
		oCBaseFileSystem__Disconnect = r1vtable[1];
		oCFileSystem_Stdio__QueryInterface = r1vtable[2];
		oCBaseFileSystem__Init = r1vtable[3];
		oCBaseFileSystem__Shutdown = r1vtable[4];
		oCBaseFileSystem__GetDependencies = r1vtable[5];
		oCBaseFileSystem__GetTier = r1vtable[6];
		oCBaseFileSystem__Reconnect = r1vtable[7];
		osub_180023F80 = r1vtable[8];
		osub_180023F90 = r1vtable[9];
		oCFileSystem_Stdio__AddSearchPath = r1vtable[10];
		oCBaseFileSystem__RemoveSearchPath = r1vtable[11];
		oCBaseFileSystem__RemoveAllSearchPaths = r1vtable[12];
		oCBaseFileSystem__RemoveSearchPaths = r1vtable[13];
		oCBaseFileSystem__MarkPathIDByRequestOnly = r1vtable[14];
		oCBaseFileSystem__RelativePathToFullPath = r1vtable[15];
		oCBaseFileSystem__GetSearchPath = r1vtable[16];
		oCBaseFileSystem__AddPackFile = r1vtable[17];
		oCBaseFileSystem__RemoveFile = r1vtable[18];
		oCBaseFileSystem__RenameFile = r1vtable[19];
		oCBaseFileSystem__CreateDirHierarchy = r1vtable[20];
		oCBaseFileSystem__IsDirectory = r1vtable[21];
		oCBaseFileSystem__FileTimeToString = r1vtable[22];
		oCFileSystem_Stdio__SetBufferSize = r1vtable[23];
		oCFileSystem_Stdio__IsOK = r1vtable[24];
		oCFileSystem_Stdio__EndOfLine = r1vtable[25];
		oCFileSystem_Stdio__ReadLine = r1vtable[26];
		oCBaseFileSystem__FPrintf = r1vtable[27];
		oCBaseFileSystem__LoadModule = r1vtable[28];
		oCBaseFileSystem__UnloadModule = r1vtable[29];
		oCBaseFileSystem__FindFirst = r1vtable[30];
		oCBaseFileSystem__FindNext = r1vtable[31];
		oCBaseFileSystem__FindIsDirectory = r1vtable[32];
		oCBaseFileSystem__FindClose = r1vtable[33];
		oCBaseFileSystem__FindFirstEx = r1vtable[34];
		oCBaseFileSystem__FindFileAbsoluteList = r1vtable[35];
		oCBaseFileSystem__GetLocalPath = r1vtable[36];
		oCBaseFileSystem__FullPathToRelativePath = r1vtable[37];
		oCBaseFileSystem__GetCurrentDirectory = r1vtable[38];
		oCBaseFileSystem__FindOrAddFileName = r1vtable[39];
		oCBaseFileSystem__String = r1vtable[40];
		oCBaseFileSystem__AsyncReadMultiple = r1vtable[41];
		oCBaseFileSystem__AsyncAppend = r1vtable[42];
		oCBaseFileSystem__AsyncAppendFile = r1vtable[43];
		oCBaseFileSystem__AsyncFinishAll = r1vtable[44];
		oCBaseFileSystem__AsyncFinishAllWrites = r1vtable[45];
		oCBaseFileSystem__AsyncFlush = r1vtable[46];
		oCBaseFileSystem__AsyncSuspend = r1vtable[47];
		oCBaseFileSystem__AsyncResume = r1vtable[48];
		oCBaseFileSystem__AsyncBeginRead = r1vtable[49];
		oCBaseFileSystem__AsyncEndRead = r1vtable[50];
		oCBaseFileSystem__AsyncFinish = r1vtable[51];
		oCBaseFileSystem__AsyncGetResult = r1vtable[52];
		oCBaseFileSystem__AsyncAbort = r1vtable[53];
		oCBaseFileSystem__AsyncStatus = r1vtable[54];
		oCBaseFileSystem__AsyncSetPriority = r1vtable[55];
		oCBaseFileSystem__AsyncAddRef = r1vtable[56];
		oCBaseFileSystem__AsyncRelease = r1vtable[57];
		osub_180024450 = r1vtable[58];
		osub_180024460 = r1vtable[59];
		onullsub_96 = r1vtable[60];
		osub_180024490 = r1vtable[61];
		osub_180024440 = r1vtable[62];
		onullsub_97 = r1vtable[63];
		osub_180009BE0 = r1vtable[64];
		osub_18000F6A0 = r1vtable[65];
		osub_180002CA0 = r1vtable[66];
		osub_180002CB0 = r1vtable[67];
		osub_1800154F0 = r1vtable[68];
		osub_180015550 = r1vtable[69];
		osub_180015420 = r1vtable[70];
		osub_180015480 = r1vtable[71];
		oCBaseFileSystem__RemoveLoggingFunc = r1vtable[72];
		oCBaseFileSystem__GetFilesystemStatistics = r1vtable[73];
		oCFileSystem_Stdio__OpenEx = r1vtable[74];
		osub_18000A5D0 = r1vtable[75];
		osub_1800052A0 = r1vtable[76];
		osub_180002F10 = r1vtable[77];
		osub_18000A690 = r1vtable[78];
		osub_18000A6F0 = r1vtable[79];
		osub_1800057A0 = r1vtable[80];
		osub_180002960 = r1vtable[81];
		osub_180020110 = r1vtable[82];
		osub_180020230 = r1vtable[83];
		osub_180023660 = r1vtable[84];
		osub_1800204A0 = r1vtable[85];
		osub_180002F40 = r1vtable[86];
		osub_180004F00 = r1vtable[87];
		osub_180024020 = r1vtable[88];
		osub_180024AF0 = r1vtable[89];
		osub_180024110 = r1vtable[90];
		osub_180002580 = r1vtable[91];
		osub_180002560 = r1vtable[92];
		osub_18000A070 = r1vtable[93];
		osub_180009E80 = r1vtable[94];
		osub_180009C20 = r1vtable[95];
		osub_1800022F0 = r1vtable[96];
		osub_180002330 = r1vtable[97];
		osub_180009CF0 = r1vtable[98];
		osub_180002340 = r1vtable[99];
		osub_180002320 = r1vtable[100];
		osub_180009E00 = r1vtable[101];
		osub_180009F20 = r1vtable[102];
		osub_180009EA0 = r1vtable[103];
		osub_180009E50 = r1vtable[104];
		osub_180009FC0 = r1vtable[105];
		osub_180004E80 = r1vtable[106];
		osub_18000A000 = r1vtable[107];
		osub_180014350 = r1vtable[108];
		osub_18000F5B0 = r1vtable[109];
		osub_180002590 = r1vtable[110];
		osub_1800025D0 = r1vtable[111];
		osub_1800025E0 = r1vtable[112];
		oCFileSystem_Stdio__LoadVPKForMap = r1vtable[113];
		oCFileSystem_Stdio__UnkFunc1 = r1vtable[114];
		oCFileSystem_Stdio__WeirdFuncThatJustDerefsRDX = r1vtable[115];
		oCFileSystem_Stdio__GetPathTime = r1vtable[116];
		oCFileSystem_Stdio__GetFSConstructedFlag = r1vtable[117];
		oCFileSystem_Stdio__EnableWhitelistFileTracking = r1vtable[118];
		osub_18000A750 = r1vtable[119];
		osub_180002B20 = r1vtable[120];
		osub_18001DC30 = r1vtable[121];
		osub_180002B30 = r1vtable[122];
		osub_180002BA0 = r1vtable[123];
		osub_180002BB0 = r1vtable[124];
		osub_180002BC0 = r1vtable[125];
		osub_180002290 = r1vtable[126];
		osub_18001CCD0 = r1vtable[127];
		osub_18001CCE0 = r1vtable[128];
		osub_18001CCF0 = r1vtable[129];
		osub_18001CD00 = r1vtable[130];
		osub_180014520 = r1vtable[131];
		osub_180002650 = r1vtable[132];
		osub_18001CD10 = r1vtable[133];
		osub_180016250 = r1vtable[134];
		osub_18000F0D0 = r1vtable[135];
		osub_1800139F0 = r1vtable[136];
		osub_180016570 = r1vtable[137];
		onullsub_86 = r1vtable[138];
		osub_18000AEC0 = r1vtable[139];
		osub_180003320 = r1vtable[140];
		osub_18000AF50 = r1vtable[141];
		osub_18000AF60 = r1vtable[142];
		osub_180005D00 = r1vtable[143];
		osub_18000AF70 = r1vtable[144];
		osub_18001B130 = r1vtable[145];
		osub_18000AF80 = r1vtable[146];
		osub_1800034D0 = r1vtable[147];
		osub_180017180 = r1vtable[148];
		osub_180003550 = r1vtable[149];
		osub_1800250D0 = r1vtable[150];
		osub_1800241B0 = r1vtable[151];
		osub_1800241C0 = r1vtable[152];
		osub_1800241F0 = r1vtable[153];
		osub_180024240 = r1vtable[154];
		osub_180024250 = r1vtable[155];
		osub_180024260 = r1vtable[156];
		osub_180024300 = r1vtable[157];
		osub_180024310 = r1vtable[158];
		osub_180024320 = r1vtable[159];
		osub_180024340 = r1vtable[160];
		osub_180024350 = r1vtable[161];
		osub_180024360 = r1vtable[162];
		osub_180024390 = r1vtable[163];
		osub_180024370 = r1vtable[164];
		osub_1800243C0 = r1vtable[165];
		osub_1800243F0 = r1vtable[166];
		osub_180024410 = r1vtable[167];
		osub_180024430 = r1vtable[168];

		static uintptr_t r1ovtable[] = {
			(uintptr_t)(&CBaseFileSystem__Connect),
			(uintptr_t)(&CBaseFileSystem__Disconnect),
			(uintptr_t)(&CFileSystem_Stdio__QueryInterface),
			(uintptr_t)(&CBaseFileSystem__Init),
			(uintptr_t)(&CBaseFileSystem__Shutdown),
			(uintptr_t)(&CBaseFileSystem__GetDependencies),
			(uintptr_t)(&CBaseFileSystem__GetTier),
			(uintptr_t)(&CBaseFileSystem__Reconnect),
			(uintptr_t)(&CFileSystem_Stdio__NullSub3),
			(uintptr_t)(&CBaseFileSystem__GetTFOFileSystemThing),
			(uintptr_t)(&CFileSystem_Stdio__DoTFOFilesystemOp),
			(uintptr_t)(&CFileSystem_Stdio__NullSub4),
			(uintptr_t)(&CFileSystem_Stdio__AddSearchPath),
			(uintptr_t)(&CBaseFileSystem__RemoveSearchPath),
			(uintptr_t)(&CBaseFileSystem__RemoveAllSearchPaths),
			(uintptr_t)(&CBaseFileSystem__RemoveSearchPaths),
			(uintptr_t)(&CBaseFileSystem__MarkPathIDByRequestOnly),
			(uintptr_t)(&CBaseFileSystem__RelativePathToFullPath),
			(uintptr_t)(&CBaseFileSystem__GetSearchPath),
			(uintptr_t)(&CBaseFileSystem__AddPackFile),
			(uintptr_t)(&CBaseFileSystem__RemoveFile),
			(uintptr_t)(&CBaseFileSystem__RenameFile),
			(uintptr_t)(&CBaseFileSystem__CreateDirHierarchy),
			(uintptr_t)(&CBaseFileSystem__IsDirectory),
			(uintptr_t)(&CBaseFileSystem__FileTimeToString),
			(uintptr_t)(&CFileSystem_Stdio__SetBufferSize),
			(uintptr_t)(&CFileSystem_Stdio__IsOK),
			(uintptr_t)(&CFileSystem_Stdio__EndOfLine),
			(uintptr_t)(&CFileSystem_Stdio__ReadLine),
			(uintptr_t)(&CBaseFileSystem__FPrintf),
			(uintptr_t)(&CBaseFileSystem__LoadModule),
			(uintptr_t)(&CBaseFileSystem__UnloadModule),
			(uintptr_t)(&CBaseFileSystem__FindFirst),
			(uintptr_t)(&CBaseFileSystem__FindNext),
			(uintptr_t)(&CBaseFileSystem__FindIsDirectory),
			(uintptr_t)(&CBaseFileSystem__FindClose),
			(uintptr_t)(&CBaseFileSystem__FindFirstEx),
			(uintptr_t)(&CBaseFileSystem__FindFileAbsoluteList),
			(uintptr_t)(&CBaseFileSystem__GetLocalPath),
			(uintptr_t)(&CBaseFileSystem__FullPathToRelativePath),
			(uintptr_t)(&CBaseFileSystem__GetCurrentDirectory),
			(uintptr_t)(&CBaseFileSystem__FindOrAddFileName),
			(uintptr_t)(&CBaseFileSystem__String),
			(uintptr_t)(&CBaseFileSystem__AsyncReadMultiple),
			(uintptr_t)(&CBaseFileSystem__AsyncAppend),
			(uintptr_t)(&CBaseFileSystem__AsyncAppendFile),
			(uintptr_t)(&CBaseFileSystem__AsyncFinishAll),
			(uintptr_t)(&CBaseFileSystem__AsyncFinishAllWrites),
			(uintptr_t)(&CBaseFileSystem__AsyncFlush),
			(uintptr_t)(&CBaseFileSystem__AsyncSuspend),
			(uintptr_t)(&CBaseFileSystem__AsyncResume),
			(uintptr_t)(&CBaseFileSystem__AsyncBeginRead),
			(uintptr_t)(&CBaseFileSystem__AsyncEndRead),
			(uintptr_t)(&CBaseFileSystem__AsyncFinish),
			(uintptr_t)(&CBaseFileSystem__AsyncGetResult),
			(uintptr_t)(&CBaseFileSystem__AsyncAbort),
			(uintptr_t)(&CBaseFileSystem__AsyncStatus),
			(uintptr_t)(&CBaseFileSystem__AsyncSetPriority),
			(uintptr_t)(&CBaseFileSystem__AsyncAddRef),
			(uintptr_t)(&CBaseFileSystem__AsyncRelease),
			(uintptr_t)(&sub_180024450),
			(uintptr_t)(&sub_180024460),
			(uintptr_t)(&nullsub_96),
			(uintptr_t)(&sub_180024490),
			(uintptr_t)(&sub_180024440),
			(uintptr_t)(&nullsub_97),
			(uintptr_t)(&sub_180009BE0),
			(uintptr_t)(&sub_18000F6A0),
			(uintptr_t)(&sub_180002CA0),
			(uintptr_t)(&sub_180002CB0),
			(uintptr_t)(&sub_1800154F0),
			(uintptr_t)(&sub_180015550),
			(uintptr_t)(&sub_180015420),
			(uintptr_t)(&sub_180015480),
			(uintptr_t)(&CBaseFileSystem__RemoveLoggingFunc),
			(uintptr_t)(&CBaseFileSystem__GetFilesystemStatistics),
			(uintptr_t)(&CFileSystem_Stdio__OpenEx),
			(uintptr_t)(&sub_18000A5D0),
			(uintptr_t)(&sub_1800052A0),
			(uintptr_t)(&sub_180002F10),
			(uintptr_t)(&sub_18000A690),
			(uintptr_t)(&sub_18000A6F0),
			(uintptr_t)(&sub_1800057A0),
			(uintptr_t)(&sub_180002960),
			(uintptr_t)(&sub_180020110),
			(uintptr_t)(&sub_180020230),
			(uintptr_t)(&sub_180023660),
			(uintptr_t)(&sub_1800204A0),
			(uintptr_t)(&sub_180002F40),
			(uintptr_t)(&sub_180004F00),
			(uintptr_t)(&sub_180024020),
			(uintptr_t)(&sub_180024AF0),
			(uintptr_t)(&sub_180024110),
			(uintptr_t)(&sub_180002580),
			(uintptr_t)(&sub_180002560),
			(uintptr_t)(&sub_18000A070),
			(uintptr_t)(&CFileSystem_Stdio__NullSub4),
			(uintptr_t)(&sub_180009C20),
			(uintptr_t)(&sub_1800022F0),
			(uintptr_t)(&sub_180002330),
			(uintptr_t)(&sub_180009CF0),
			(uintptr_t)(&sub_180002340),
			(uintptr_t)(&sub_180002320),
			(uintptr_t)(&sub_180009E00),
			(uintptr_t)(&sub_180009F20),
			(uintptr_t)(&sub_180009EA0),
			(uintptr_t)(&sub_180009E50),
			(uintptr_t)(&sub_180009FC0),
			(uintptr_t)(&sub_180004E80),
			(uintptr_t)(&sub_18000A000),
			(uintptr_t)(&sub_180014350),
			(uintptr_t)(&sub_18000F5B0),
			(uintptr_t)(&sub_180002590),
			(uintptr_t)(&sub_1800025D0),
			(uintptr_t)(&sub_1800025E0),
			(uintptr_t)(&CFileSystem_Stdio__LoadVPKForMap),
			(uintptr_t)(&CFileSystem_Stdio__UnkFunc1),
			(uintptr_t)(&CFileSystem_Stdio__WeirdFuncThatJustDerefsRDX),
			(uintptr_t)(&CFileSystem_Stdio__GetPathTime),
			(uintptr_t)(&CFileSystem_Stdio__GetFSConstructedFlag),
			(uintptr_t)(&CFileSystem_Stdio__EnableWhitelistFileTracking),
			(uintptr_t)(&sub_18000A750),
			(uintptr_t)(&sub_180002B20),
			(uintptr_t)(&sub_18001DC30),
			(uintptr_t)(&sub_180002B30),
			(uintptr_t)(&sub_180002BA0),
			(uintptr_t)(&sub_180002BB0),
			(uintptr_t)(&sub_180002BC0),
			(uintptr_t)(&sub_180002290),
			(uintptr_t)(&sub_18001CCD0),
			(uintptr_t)(&sub_18001CCE0),
			(uintptr_t)(&sub_18001CCF0),
			(uintptr_t)(&sub_18001CD00),
			(uintptr_t)(&sub_180014520),
			(uintptr_t)(&sub_180002650),
			(uintptr_t)(&sub_18001CD10),
			(uintptr_t)(&sub_180016250),
			(uintptr_t)(&sub_18000F0D0),
			(uintptr_t)(&sub_1800139F0),
			(uintptr_t)(&sub_180016570),
			(uintptr_t)(&nullsub_86),
			(uintptr_t)(&sub_18000AEC0),
			(uintptr_t)(&sub_180003320),
			(uintptr_t)(&sub_18000AF50),
			(uintptr_t)(&sub_18000AF60),
			(uintptr_t)(&sub_180005D00),
			(uintptr_t)(&sub_18000AF70),
			(uintptr_t)(&sub_18001B130),
			(uintptr_t)(&sub_18000AF80),
			(uintptr_t)(&sub_1800034D0),
			(uintptr_t)(&sub_180017180),
			(uintptr_t)(&sub_180003550),
			(uintptr_t)(&sub_1800250D0),
			(uintptr_t)(&sub_1800241B0),
			(uintptr_t)(&sub_1800241C0),
			(uintptr_t)(&sub_1800241F0),
			(uintptr_t)(&sub_180024240),
			(uintptr_t)(&sub_180024250),
			(uintptr_t)(&sub_180024260),
			(uintptr_t)(&sub_180024300),
			(uintptr_t)(&sub_180024310),
			(uintptr_t)(&sub_180024320),
			(uintptr_t)(&sub_180024340),
			(uintptr_t)(&sub_180024350),
			(uintptr_t)(&sub_180024360),
			(uintptr_t)(&sub_180024390),
			(uintptr_t)(&sub_180024370),
			(uintptr_t)(&sub_1800243C0),
			(uintptr_t)(&sub_1800243F0),
			(uintptr_t)(&sub_180024410),
			(uintptr_t)(&sub_180024430),
			(uintptr_t)(&CFileSystem_Stdio__NullSub4)
		};
		struct fsptr {
			void* ptr1;
			void* ptr2;
			void* ptr3;
			void* ptr4;
			void* ptr5;
		};
		static void* whatever3 = &r1ovtable;
		static fsptr a;
		a.ptr1 = whatever3;
		a.ptr2 = (void*)((uintptr_t)(GetModuleHandleA("filesystem_stdio.dll"))+0xD2078);
		a.ptr3 = (void*)((uintptr_t)(GetModuleHandleA("filesystem_stdio.dll")) + 0xD2078);;
		a.ptr4 = (void*)((uintptr_t)(GetModuleHandleA("filesystem_stdio.dll")) + 0xD2078);;
		a.ptr5 = (void*)((uintptr_t)(GetModuleHandleA("filesystem_stdio.dll")) + 0xD2078);;
		return &a.ptr1;
	}
	if (!strcmp(pName, "VModelInfoServer002")) {
		std::cout << "wrapping VModelInfoServer002" << std::endl;
		modelinterface = (uintptr_t)oAppSystemFactory(pName, pReturnCode);

		uintptr_t* r1vtable = *(uintptr_t**)oAppSystemFactory(pName, pReturnCode);
		oCModelInfo_dtor_0 = r1vtable[0];
		oCModelInfoServer__GetModel = r1vtable[1];
		oCModelInfoServer__GetModelIndex = r1vtable[2];
		oCModelInfo__GetModelName = r1vtable[3];
		oCModelInfo__GetVCollide = r1vtable[4];
		oCModelInfo__GetVCollideEx = r1vtable[5];
		oCModelInfo__GetVCollideEx2 = r1vtable[6];
		oCModelInfo__GetModelRenderBounds = r1vtable[7];
		oCModelInfo__GetModelFrameCount = r1vtable[8];
		oCModelInfo__GetModelType = r1vtable[9];
		oCModelInfo__GetModelExtraData = r1vtable[10];
		oCModelInfo__IsTranslucentTwoPass = r1vtable[11];
		oCModelInfo__ModelHasMaterialProxy = r1vtable[12];
		oCModelInfo__IsTranslucent = r1vtable[13];
		oCModelInfo__NullSub1 = r1vtable[14];
		oCModelInfo__UnkFunc1 = r1vtable[15];
		oCModelInfo__UnkFunc2 = r1vtable[16];
		oCModelInfo__UnkFunc3 = r1vtable[17];
		oCModelInfo__UnkFunc4 = r1vtable[18];
		oCModelInfo__UnkFunc5 = r1vtable[19];
		oCModelInfo__UnkFunc6 = r1vtable[20];
		oCModelInfo__UnkFunc7 = r1vtable[21];
		oCModelInfo__UnkFunc8 = r1vtable[22];
		oCModelInfo__UnkFunc9 = r1vtable[23];
		oCModelInfo__UnkFunc10 = r1vtable[24];
		oCModelInfo__UnkFunc11 = r1vtable[25];
		oCModelInfo__UnkFunc12 = r1vtable[26];
		oCModelInfo__UnkFunc13 = r1vtable[27];
		oCModelInfo__UnkFunc14 = r1vtable[28];
		oCModelInfo__UnkFunc15 = r1vtable[29];
		oCModelInfo__UnkFunc16 = r1vtable[30];
		oCModelInfo__UnkFunc17 = r1vtable[31];
		oCModelInfo__UnkFunc18 = r1vtable[32];
		oCModelInfo__UnkFunc19 = r1vtable[33];
		oCModelInfo__UnkFunc20 = r1vtable[34];
		oCModelInfo__UnkFunc21 = r1vtable[35];
		oCModelInfo__UnkFunc22 = r1vtable[36];
		oCModelInfo__UnkFunc23 = r1vtable[37];
		oCModelInfo__NullSub2 = r1vtable[38];
		oCModelInfo__UnkFunc24 = r1vtable[39];
		oCModelInfo__UnkFunc25 = r1vtable[40];
		static uintptr_t r1ovtable[] = {
			(uintptr_t)(&CModelInfo_dtor_0),
			(uintptr_t)(&CModelInfoServer__GetModel),
			(uintptr_t)(&CModelInfoServer__GetModelIndex),
			(uintptr_t)(&CModelInfo__GetModelName),
			(uintptr_t)(&CModelInfo__GetVCollide),
			(uintptr_t)(&CModelInfo__GetVCollideEx),
			(uintptr_t)(&CModelInfo__GetVCollideEx2),
			(uintptr_t)(&CModelInfo__GetModelRenderBounds),
			(uintptr_t)(&CModelInfo__UnkSetFlag),
			(uintptr_t)(&CModelInfo__UnkClearFlag),
			(uintptr_t)(&CModelInfo__GetFlag),
			(uintptr_t)(&CModelInfo__UnkTFOVoid),
			(uintptr_t)(&CModelInfo__UnkTFOVoid2),
			(uintptr_t)(&CModelInfo__ShouldRet0),
			(uintptr_t)(&CModelInfo__UnkTFOVoid3),
			(uintptr_t)(&CModelInfo__ClientFullyConnected),
			(uintptr_t)(&CModelInfo__GetModelFrameCount),
			(uintptr_t)(&CModelInfo__GetModelType),
			(uintptr_t)(&CModelInfo__UnkTFOShouldRet0_2),
			(uintptr_t)(&CModelInfo__GetModelExtraData),
			(uintptr_t)(&CModelInfo__IsTranslucentTwoPass),
			(uintptr_t)(&CModelInfo__ModelHasMaterialProxy),
			(uintptr_t)(&CModelInfo__IsTranslucent),
			(uintptr_t)(&CModelInfo__NullSub1),
			(uintptr_t)(&CModelInfo__UnkFunc1),
			(uintptr_t)(&CModelInfo__UnkFunc2),
			(uintptr_t)(&CModelInfo__UnkFunc3),
			(uintptr_t)(&CModelInfo__UnkFunc4),
			(uintptr_t)(&CModelInfo__UnkFunc5),
			(uintptr_t)(&CModelInfo__UnkFunc6),
			(uintptr_t)(&CModelInfo__UnkFunc7),
			(uintptr_t)(&CModelInfo__UnkFunc8),
			(uintptr_t)(&CModelInfo__UnkFunc9),
			(uintptr_t)(&CModelInfo__UnkFunc10),
			(uintptr_t)(&CModelInfo__UnkFunc11),
			(uintptr_t)(&CModelInfo__UnkFunc12),
			(uintptr_t)(&CModelInfo__UnkFunc13),
			(uintptr_t)(&CModelInfo__UnkFunc14),
			(uintptr_t)(&CModelInfo__UnkFunc15),
			(uintptr_t)(&CModelInfo__UnkFunc16),
			(uintptr_t)(&CModelInfo__UnkFunc17),
			(uintptr_t)(&CModelInfo__UnkFunc18),
			(uintptr_t)(&CModelInfo__UnkFunc19),
			(uintptr_t)(&CModelInfo__UnkFunc20),
			(uintptr_t)(&CModelInfo__UnkFunc21),
			(uintptr_t)(&CModelInfo__UnkFunc22),
			(uintptr_t)(&CModelInfo__UnkFunc23),
			(uintptr_t)(&CModelInfo__NullSub2),
			(uintptr_t)(&CModelInfo__UnkFunc24),
			(uintptr_t)(&CModelInfo__UnkFunc25)
		};
		static void* whatever4 = &r1ovtable;
		return &whatever4;
	}
	if (!strcmp(pName, "VEngineServerStringTable001")) {
		std::cout << "wrapping VEngineServerStringTable001" << std::endl;
		stringtableinterface = (uintptr_t)oAppSystemFactory(pName, pReturnCode);
		uintptr_t* r1vtable = *(uintptr_t**)oAppSystemFactory(pName, pReturnCode);
		
		oCNetworkStringTableContainer_dtor = r1vtable[0];
		oCNetworkStringTableContainer__CreateStringTable = r1vtable[1];
		oCNetworkStringTableContainer__RemoveAllTables = r1vtable[2];
		oCNetworkStringTableContainer__FindTable = r1vtable[3];
		oCNetworkStringTableContainer__GetTable = r1vtable[4];
		oCNetworkStringTableContainer__GetNumTables = r1vtable[5];
		oCNetworkStringTableContainer__SetAllowClientSideAddString = r1vtable[6];
		oCNetworkStringTableContainer__CreateDictionary = r1vtable[7];
		static uintptr_t r1ovtable[] = {
			(uintptr_t)(&CNetworkStringTableContainer_dtor),
			(uintptr_t)(&CNetworkStringTableContainer__CreateStringTable),
			(uintptr_t)(&CNetworkStringTableContainer__RemoveAllTables),
			(uintptr_t)(&CNetworkStringTableContainer__FindTable),
			(uintptr_t)(&CNetworkStringTableContainer__GetTable),
			(uintptr_t)(&CNetworkStringTableContainer__GetNumTables),
			(uintptr_t)(&CNetworkStringTableContainer__SetAllowClientSideAddString),
			(uintptr_t)(&CNetworkStringTableContainer__SetTickCount),
			(uintptr_t)(&CNetworkStringTableContainer__CreateDictionary)
		};
		static void* whatever5 = &r1ovtable;
		return &whatever5;
	}
	if (!strcmp(pName, "VEngineCvar007")) {
		std::cout << "wrapping VEngineCvar007" << std::endl;

		uintptr_t* r1vtable = *(uintptr_t**)oAppSystemFactory(pName, pReturnCode);
		cvarinterface = (uintptr_t)oAppSystemFactory(pName, pReturnCode);

		oCCvar__Connect = r1vtable[0];
		oCCvar__Disconnect = r1vtable[1];
		oCCvar__QueryInterface = r1vtable[2];
		oCCVar__Init = r1vtable[3];
		oCCVar__Shutdown = r1vtable[4];
		oCCvar__GetDependencies = r1vtable[5];
		oCCVar__GetTier = r1vtable[6];
		oCCVar__Reconnect = r1vtable[7];
		oCCvar__AllocateDLLIdentifier = r1vtable[8];
		OriginalCCVar_RegisterConCommand = reinterpret_cast<decltype(OriginalCCVar_RegisterConCommand)>(r1vtable[9]);
		OriginalCCVar_UnregisterConCommand = reinterpret_cast<decltype(OriginalCCVar_UnregisterConCommand)>(r1vtable[10]);
		oCCvar__UnregisterConCommands = r1vtable[11];
		oCCvar__GetCommandLineValue = r1vtable[12];
		//uintptr_t CCvar__FindCommandBase = r1vtable[13];
		//uintptr_t CCvar__FindCommandBase2 = r1vtable[14];
		//uintptr_t CCvar__FindVar = r1vtable[15];
		//uintptr_t CCvar__FindVar2 = r1vtable[16];
		//uintptr_t CCvar__FindCommand = r1vtable[17];
		//uintptr_t CCvar__FindCommand2 = r1vtable[18];
		OriginalCCVar_FindCommandBase = reinterpret_cast<decltype(OriginalCCVar_FindCommandBase)>(r1vtable[13]);
		OriginalCCVar_FindCommandBase2 = reinterpret_cast<decltype(OriginalCCVar_FindCommandBase2)>(r1vtable[14]);
		OriginalCCVar_FindVar = reinterpret_cast<decltype(OriginalCCVar_FindVar)>(r1vtable[15]);
		OriginalCCVar_FindVar2 = reinterpret_cast<decltype(OriginalCCVar_FindVar2)>(r1vtable[16]);
		OriginalCCVar_FindCommand = reinterpret_cast<decltype(OriginalCCVar_FindCommand)>(r1vtable[17]);
		OriginalCCVar_FindCommand2 = reinterpret_cast<decltype(OriginalCCVar_FindCommand2)>(r1vtable[18]);

		oCCVar__Find = r1vtable[19];
		//uintptr_t CCvar__InstallGlobalChangeCallback = r1vtable[20];
		//uintptr_t CCvar__RemoveGlobalChangeCallback = r1vtable[21];
		OriginalCCVar_CallGlobalChangeCallbacks = reinterpret_cast<decltype(OriginalCCVar_CallGlobalChangeCallbacks)>(r1vtable[22]);
		oCCvar__InstallConsoleDisplayFunc = r1vtable[23];
		oCCvar__RemoveConsoleDisplayFunc = r1vtable[24];
		oCCvar__ConsoleColorPrintf = r1vtable[25];
		oCCvar__ConsolePrintf = r1vtable[26];
		oCCvar__ConsoleDPrintf = r1vtable[27];
		oCCVar__RevertFlaggedConVars = r1vtable[28];
		oCCvar__InstallCVarQuery = r1vtable[29];
		oCCvar__SetMaxSplitScreenSlots = r1vtable[30];
		oCCvar__GetMaxSplitScreenSlots = r1vtable[31];
		oCCvar__GetConsoleDisplayFuncCount = r1vtable[32];
		oCCvar__GetConsoleText = r1vtable[33];
		oCCvar__IsMaterialThreadSetAllowed = r1vtable[34];
		OriginalCCVar_QueueMaterialThreadSetValue1 = reinterpret_cast<decltype(OriginalCCVar_QueueMaterialThreadSetValue1)>(r1vtable[35]);
		OriginalCCVar_QueueMaterialThreadSetValue2 = reinterpret_cast<decltype(OriginalCCVar_QueueMaterialThreadSetValue2)>(r1vtable[36]);
		OriginalCCVar_QueueMaterialThreadSetValue3 = reinterpret_cast<decltype(OriginalCCVar_QueueMaterialThreadSetValue3)>(r1vtable[37]);
		oCCvar__HasQueuedMaterialThreadConVarSets = r1vtable[38];
		//uintptr_t CCvar__ProcessQueuedMaterialThreadConVarSets = r1vtable[39];
		oCCvar__FactoryInternalIterator = r1vtable[40];


		static uintptr_t r1ovtable[] = {
			(uintptr_t)(&CCvar__Connect),
			(uintptr_t)(&CCvar__Disconnect),
			(uintptr_t)(&CCvar__QueryInterface),
			(uintptr_t)(&CCvar__Init),
			(uintptr_t)(&CCvar__Shutdown),
			(uintptr_t)(&CCvar__GetDependencies),
			(uintptr_t)(&CCVar__GetTier),
			(uintptr_t)(&CCvar__Reconnect),
			(uintptr_t)(&CCvar__AllocateDLLIdentifier),
			(uintptr_t)(&CCVar__SetSomeFlag_Corrupt),
			(uintptr_t)(&CCVar__GetSomeFlag),
			(uintptr_t)(&CCVar_RegisterConCommand),
			(uintptr_t)(&CCVar_UnregisterConCommand),
			(uintptr_t)(&CCvar__UnregisterConCommands),
			(uintptr_t)(&CCvar__GetCommandLineValue),
			(uintptr_t)(&CCVar_FindCommandBase),
			(uintptr_t)(&CCVar_FindCommandBase2),
			(uintptr_t)(&CCVar_FindVar),
			(uintptr_t)(&CCVar_FindVar2),
			(uintptr_t)(&CCVar_FindCommand),
			(uintptr_t)(&CCVar_FindCommand2),
			(uintptr_t)(&CCVar__Find),
			(uintptr_t)(&CCvar__InstallGlobalChangeCallback),
			(uintptr_t)(&CCvar__RemoveGlobalChangeCallback),
			(uintptr_t)(&CCVar_CallGlobalChangeCallbacks),
			(uintptr_t)(&CCvar__InstallConsoleDisplayFunc),
			(uintptr_t)(&CCvar__RemoveConsoleDisplayFunc),
			(uintptr_t)(&CCvar__ConsoleColorPrintf),
			(uintptr_t)(&CCvar__ConsolePrintf),
			(uintptr_t)(&CCvar__ConsoleDPrintf),
			(uintptr_t)(&CCvar__RevertFlaggedConVars),
			(uintptr_t)(&CCvar__InstallCVarQuery),
			(uintptr_t)(&CCvar__SetMaxSplitScreenSlots),
			(uintptr_t)(&CCvar__GetMaxSplitScreenSlots),
			(uintptr_t)(&CCvar_GetConsoleDisplayFuncCount),
			(uintptr_t)(&CCvar__GetConsoleText),
			(uintptr_t)(&CCvar__IsMaterialThreadSetAllowed),
			(uintptr_t)(&CCVar_QueueMaterialThreadSetValue1),
			(uintptr_t)(&CCVar_QueueMaterialThreadSetValue2),
			(uintptr_t)(&CCVar_QueueMaterialThreadSetValue3),
			(uintptr_t)(&CCvar__HasQueuedMaterialThreadConVarSets),
			(uintptr_t)(&CCvar__ProcessQueuedMaterialThreadConVarSets),
			(uintptr_t)(&CCvar__FactoryInternalIterator)


		};
		static void* whatever6 = &r1ovtable;
		return &whatever6;
	}
	if (!strcmp(pName, "VENGINE_DEDICATEDEXPORTS_API_VERSION003")) {
		std::cout << "forging dediexports" << std::endl;
		return (void*)1;
	}
	void* result = oAppSystemFactory(pName, pReturnCode);
	if (result) {
		std::cout << "found " << pName << "  in appsystem factory" << std::endl;
		return result;
	}
	static bool bSetUpR1OEngine = false;
	if (!bSetUpR1OEngine) {
		std::cout << "engine is not set up, looking for " << pName << std::endl;
		bSetUpR1OEngine = true;
		engineR1O = LoadLibraryA("engine_r1o.dll");
		R1OCreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(engineR1O, "CreateInterface"));
		reinterpret_cast<char(__fastcall*)(__int64, CreateInterfaceFn)>((uintptr_t)(engineR1O)+0x1C6B30)(0, R1OFactory);
	}
	else {
		std::cout << "engine is set up, looking for " << pName << std::endl;
	}
	return R1OCreateInterface(pName, pReturnCode);
}

typedef char(__fastcall* CServerGameDLL__DLLInitType)(void* thisptr, CreateInterfaceFn appSystemFactory,
	CreateInterfaceFn physicsFactory, CreateInterfaceFn fileSystemFactory,
	void* pGlobals);
CServerGameDLL__DLLInitType CServerGameDLL__DLLInitOriginal;
char __fastcall CServerGameDLL__DLLInit(void* thisptr, CreateInterfaceFn appSystemFactory,
	CreateInterfaceFn physicsFactory, CreateInterfaceFn fileSystemFactory,
	void* pGlobals)
{
	oAppSystemFactory = appSystemFactory;
	oFileSystemFactory = fileSystemFactory;
	oPhysicsFactory = physicsFactory;
	return CServerGameDLL__DLLInitOriginal(thisptr, R1OFactory, R1OFactory, R1OFactory, pGlobals);
}
extern "C" __declspec(dllexport) void StackToolsNotify_LoadedLibrary(char* pModuleName)
{
	std::cout << "loaded " << pModuleName << std::endl;
	if (std::string(pModuleName).find("server.dll") != std::string::npos) {
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("server.dll") + 0x143A10), &CServerGameDLL__DLLInit, (LPVOID*)&CServerGameDLL__DLLInitOriginal);
		MH_EnableHook(MH_ALL_HOOKS);
		std::cout << "did hooks" << std::endl;
	}

}
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
	static bool done = false;
	if (!done) {
		done = true;
		AllocConsole();
		HANDLE hConsoleStream = ::CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		SetStdHandle(STD_OUTPUT_HANDLE, hConsoleStream);
	}
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		MH_Initialize();
		break;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		MH_DisableHook(MH_ALL_HOOKS);
		MH_Uninitialize();
		break;
	}
	return TRUE;
}
