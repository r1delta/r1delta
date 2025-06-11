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

#include "core.h"

#include "filesystem.h"
#include <iostream>
#include <unordered_set>
#include <string>
#include <filesystem>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <Windows.h>
#include <intrin.h>
#include "logging.h"
#include "load.h"
#include <shared_mutex>
#include <string_view>
#include "tctx.hh"
#pragma intrinsic(_ReturnAddress)

namespace fs = std::filesystem;

ReadFileFromFilesystemType readFileFromFilesystem;
ReadFromCacheType readFromCache;
ReadFileFromVPKType readFileFromVPK;

CVFileSystem* g_CVFileSystem;
CBaseFileSystem* g_CBaseFileSystem;
uintptr_t g_CVFileSystemInterface;
IFileSystem* g_CBaseFileSystemInterface;
uintptr_t g_r1oCVFileSystemInterface[173];
uintptr_t g_r1oCBaseFileSystemInterface[17];

bool V_IsAbsolutePath(const char* pStr) {
    if (!(pStr[0] && pStr[1]))
        return false;

    bool bIsAbsolute = (pStr[0] && pStr[1] == ':') ||
        ((pStr[0] == '/' || pStr[0] == '\\') && (pStr[1] == '/' || pStr[1] == '\\'));

    return bIsAbsolute;
}

bool ReadFromCacheHook(IFileSystem* filesystem, char* path, void* result) {
    if (FileCache::GetInstance().TryReplaceFile(path))
        return false;

    return readFromCache(filesystem, path, result);
}

FileHandle_t ReadFileFromVPKHook(VPKData* vpkInfo, __int64* b, char* filename) {
    if (FileCache::GetInstance().TryReplaceFile(filename)) {
        *b = -1;
        return b;
    }

    return readFileFromVPK(vpkInfo, b, filename);
}

std::atomic_flag threadStarted = ATOMIC_FLAG_INIT;

void StartFileCacheThread() {
    if (!threadStarted.test_and_set()) {
        std::thread([]() { FileCache::GetInstance().UpdateCache(); }).detach();
    }
}
class FastFileSystemHook {
private:
	struct FFSS
	{
		const char* ptr;
		size_t len;

		bool equals(FFSS other)
		{
			if (!this->len || this->len != other.len)
			{
				return false;
			}
			return !memcmp(this->ptr, other.ptr, this->len);
		}
	};
	static uint64_t hash64(FFSS s)
	{
		uint64_t h = 0x100;
		for (ptrdiff_t i = 0; i < s.len; i++) {
			unsigned char e = s.ptr[i] & 0xdf;
			// TODO(mrsteyk): some bit magic?
			if (e == 0xf)
			{
				e = '\\';
			}
			h ^= e & 255;
			h *= 1111111111111111111;
		}
		return h;
	}
	struct Payload
	{
		bool checked;
		bool exists;
	};
	static constexpr size_t TRIE_BITS = 2;
	struct Trie
	{
		Trie* child[1 << TRIE_BITS];
		FFSS key;
		Payload value;
	};
	static Trie* root;
	static SRWLOCK cacheMutex;
	static Arena* trieArena;

	static Trie* lookup(Trie** map, FFSS key) {
		ZoneScoped;

		for (uint64_t h = hash64(key); *map; h <<= TRIE_BITS) {
			if (key.equals((*map)->key)) {
				return *map;
			}
			map = &(*map)->child[h >> (64 - TRIE_BITS)];
		}
		Trie* ret;
		{
			ZoneScopedN("lookup>allocation");

			ReleaseSRWLockShared(&cacheMutex);
			AcquireSRWLockExclusive(&cacheMutex);
			*map = (Trie*)arena_push(trieArena, sizeof(Trie));
			char* ptr = (char*)arena_push(trieArena, key.len + 1);
			memcpy(ptr, key.ptr, key.len);
			(*map)->key = { .ptr = ptr, .len = key.len };
			ret = *map;
			ReleaseSRWLockExclusive(&cacheMutex);
			AcquireSRWLockShared(&cacheMutex);
		}
		return ret;
	}

	static bool checkAndCachePath(const char* fullPath) {
		ZoneScoped;

		const char* part = fullPath;
		const size_t part_len = strlen(fullPath);
		R1DAssert(part_len < 260);
		size_t i = 0;
		while (i < part_len) {
			while (i < part_len && part[i] != '\\' && part[i] != '/') {
				i++;
			}
			if (i < part_len) i++; // Skip the separator
			Trie* node = lookup(&root, { .ptr = part, .len = i });
			if (!node->value.checked) {
				ZoneScopedN("checkAndCachePath make node");
				DWORD attributes = GetFileAttributesA(node->key.ptr);
				node->value.exists = (attributes != INVALID_FILE_ATTRIBUTES);
				node->value.checked = true;
			}
			if (!node->value.exists) {
				return false;
			}
		}
		return true;
	}
public:
	static bool shouldFailRead(const char* path) {
		ZoneScoped;

		//int dot_count = 0;
		//while (*path && dot_count <= 2)
		//	if (*path++ == '.') dot_count++;
		//if (dot_count > 2) return false;

		AcquireSRWLockShared(&cacheMutex);
		auto ret = !checkAndCachePath(path);
		ReleaseSRWLockShared(&cacheMutex);
		
		return ret;
	}
	static void resetNonexistentCache() {
		ZoneScoped;

		AcquireSRWLockExclusive(&cacheMutex);
		root = 0;
		arena_clear(trieArena);
		ReleaseSRWLockExclusive(&cacheMutex);
	}
};

FastFileSystemHook::Trie* FastFileSystemHook::root = 0;
Arena* FastFileSystemHook::trieArena = arena_alloc();
SRWLOCK FastFileSystemHook::cacheMutex = SRWLOCK_INIT;

__int64(*HandleOpenRegularFileOriginal)(__int64 a1, __int64 a2, char a3);

// The hook function remains largely the same
int64_t __fastcall HookedHandleOpenRegularFile(int64_t a1, int64_t a2, char a3) {
	char* path = reinterpret_cast<char*>(a2 + 63);
	if (FastFileSystemHook::shouldFailRead(path))
		return 0;
	return HandleOpenRegularFileOriginal(a1, a2, a3);
}


FileSystem_UpdateAddonSearchPathsType FileSystem_UpdateAddonSearchPathsTypeOriginal;
bool done = false;
__int64 __fastcall FileSystem_UpdateAddonSearchPaths(void* a1) {
    FileCache::GetInstance().RequestManualRescan();
	FastFileSystemHook::resetNonexistentCache();
    return FileSystem_UpdateAddonSearchPathsTypeOriginal(a1);
}

void replace_underscore(char* str) {
    char* pos = strstr(str, "_dir");
    if (pos) *pos = '\0';
}
// Helper function to check if a file exists.
static bool file_exists(const char* path)
{
	FILE* f = fopen(path, "r");
	if (f)
	{
		fclose(f);
		return true;
	}
	return false;
}

// Our modified hook.
int fs_sprintf_hook(char* Buffer, const char* Format, ...) {
	ZoneScoped;

	va_list args;
	va_start(args, Format);

	if (strcmp_static(Format, "%s_%03d.vpk") == 0) {
		// Pull the two parameters.
		const char* a1 = va_arg(args, const char*);
		int chunk = va_arg(args, int);

		// Only handle special chunk values if the path contains "vpk\\" or "vpk/"
		if (chunk >= 128 && (strstr(a1, "vpk\\") != NULL || strstr(a1, "vpk/") != NULL)) {
			va_end(args);
			// Check for the "more-files" situation as in the singlechunk branch.
			void* rettocheckformorefiles = (void*)(IsDedicatedServer() ? (G_vscript + 0x1783EB)
				: (G_filesystem_stdio + 0x6D6CB));
			if (rettocheckformorefiles == _ReturnAddress()) {
				Buffer[0] = 0;
				return 0;
			}
			// Instead of a file_exists check, decide based on the string contents:
			if (strstr(a1, "server_") != NULL) {
				return sprintf(Buffer, "%s", "vpk/server_mp_delta_common.bsp.pak000_000.vpk");
			}
			else if (strstr(a1, "client_") != NULL) {
				return sprintf(Buffer, "%s", "vpk/client_mp_delta_common.bsp.pak000_000.vpk");
			}
		}

		// Existing branch: if the string contains "singlechunk", do the original work.
		if (strstr(a1, "singlechunk") != NULL) {
			va_end(args);
			void* rettocheckformorefiles = (void*)(G_filesystem_stdio + (IsDedicatedServer() ? 0x1783EB : 0x6D6CB));
			if (rettocheckformorefiles == _ReturnAddress()) {
				Buffer[0] = 0;
				return 0;
			}
			return sprintf(Buffer, "%s_dir.vpk", a1);
		}
		// For any other case we reinitialize the va_list so that the normal vsprintf gets all parameters.
		va_end(args);
		va_start(args, Format);
	}

	int result = vsprintf(Buffer, Format, args);
	va_end(args);
	return result;
}



typedef __int64 (*AddVPKFileType)(IFileSystem* fileSystem, char* a2, char** a3, char a4, int a5, char a6);
AddVPKFileType AddVPKFileOriginal;

__int64 __fastcall AddVPKFile(IFileSystem* fileSystem, char* vpkPath, char** a3, char a4, int a5, char a6)
{
	ZoneScoped;

	// Check if the path contains "_dir"
	if (strstr(vpkPath, "_dir") != NULL)
	{
		// Look for "server_" and "client_" within the path.
		char* posServer = strstr(vpkPath, "server_");
		char* posClient = strstr(vpkPath, "client_");
		char* pos = NULL;
		bool isServer = false;

		// If both substrings are present, pick the one that occurs first.
		if (posServer && posClient)
		{
			if (posServer < posClient)
			{
				pos = posServer;
				isServer = true;
			}
			else
			{
				pos = posClient;
				isServer = false;
			}
		}
		else if (posServer)
		{
			pos = posServer;
			isServer = true;
		}
		else if (posClient)
		{
			pos = posClient;
			isServer = false;
		}

		if (pos)
		{
			// If we're on a dedicated server and the special file exists,
			// and the path contains "client_", change it to "server_"
			if (!isServer && IsDedicatedServer() &&
				file_exists("vpk/server_mp_delta_common.bsp.pak000_000.vpk"))
			{
				// Overwrite the "client_" substring with "server_".
				memcpy(pos, "server_", strlen("server_"));
			}
		}
	}

	{
		ZoneScopedN("AddVPKFileOriginal");
		// Finally, call the original function.
		return AddVPKFileOriginal(fileSystem, vpkPath, a3, a4, a5, a6);
	}
}

//
// ----------------------------------------------------------------------------
//

char CFileSystem_Stdio__NullSub3()
{
	return 0;
}

int64_t CFileSystem_Stdio__NullSub4()
{
	return 0i64;
}

int64_t CBaseFileSystem__GetTFOFileSystemThing()
{
	return 0;
}

int64_t CFileSystem_Stdio__DoTFOFilesystemOp(__int64 a1, char* a2, rsize_t a3)
{
	return false;
}
//////////////////////////////////////////////////////////
// The following patch addresses a use-after-free (UAF)
// vulnerability in the ReconcileAddonListFile function, 
// which interacts with the filesystem to build a list of
// addon directories.
//
// In the original implementation, pFileName is retrieved 
// using pFileSystem->FindFirst() and pFileSystem->FindNext(), 
// which return a pointer to memory that gets overwritten 
// by subsequent calls to FindFirst() as part of the internal 
// file system iteration mechanism. This memory is then used 
// to store directory names in a CUtlVector<CUtlString>, 
// even though the memory it points to has been freed.
//
// The behavior persists due to luck under the default Valve 
// allocator, which doesn�t always immediately overwrite freed 
// memory. The issue is exposed with the new memory allocator, 
// which overwrites freed memory with 0xDF in debug builds, 
// triggering crashes when the stale pFileName pointer is accessed.
//
// To fix this, we hook into CBaseFileSystem::Find{First,Next}Helper(),
// duplicating the returned pFileName when inside ReconcileAddonListFile.
// We store the duplicated strings in a vector for later cleanup,
// ensuring the directory names stored in CUtlVector<CUtlString> remain
// valid even after the internal file system buffer is freed.
//
// The duplicated strings are explicitly freed at the end 
// of ReconcileAddonListFile to avoid memory leaks.
//
//////////////////////////////////////////////////////////
void (*oReconcileAddonListFile)(IFileSystem* pFileSystem, const char* pModPath);
const char* (*oCBaseFileSystem__FindFirst)(CBaseFileSystem* thisptr, __int64 a2, __int64 a3);
const char* (*oCBaseFileSystem__FindNext)(CBaseFileSystem* thisptr, unsigned __int16 a2);
bool g_bIsInReconcileAddonListFile;
std::vector<const char*> g_pFileSystemStringsToCleanup = { 0 };
const char* CBaseFileSystem__FindFirst(CBaseFileSystem* thisptr, __int64 a2, __int64 a3)
{
	const char* oret = oCBaseFileSystem__FindFirst(thisptr, a2, a3);
	if (g_bIsInReconcileAddonListFile && oret) {
		const char* trueret = _strdup(oret);
		g_pFileSystemStringsToCleanup.push_back(trueret);
		return trueret;
	}
	return oret;
}
const char* CBaseFileSystem__FindNext(CBaseFileSystem* thisptr, unsigned __int16 a2)
{
	const char* oret = oCBaseFileSystem__FindNext(thisptr, a2);
	if (g_bIsInReconcileAddonListFile && oret) {
		const char* trueret = _strdup(oret);
		g_pFileSystemStringsToCleanup.push_back(trueret);
		return trueret;
	}
	return oret;
}

void ReconcileAddonListFile(IFileSystem* pFileSystem, const char* pModPath)
{
	g_bIsInReconcileAddonListFile = true;

	oReconcileAddonListFile(pFileSystem, pModPath);

	g_bIsInReconcileAddonListFile = false;

	for (const char* ptr : g_pFileSystemStringsToCleanup)
		free((void*)ptr);

	g_pFileSystemStringsToCleanup.clear();
}

CVFileSystem::CVFileSystem(uintptr_t* r1vtable)
{
	Connect = r1vtable[0];
	Disconnect = r1vtable[1];
	QueryInterface = r1vtable[2];
	Init = r1vtable[3];
	Shutdown = r1vtable[4];
	GetDependencies = r1vtable[5];
	GetTier = r1vtable[6];
	Reconnect = r1vtable[7];
	sub_180023F80 = r1vtable[8];
	sub_180023F90 = r1vtable[9];
	AddSearchPath = r1vtable[10];
	RemoveSearchPath = r1vtable[11];
	RemoveAllSearchPaths = r1vtable[12];
	RemoveSearchPaths = r1vtable[13];
	MarkPathIDByRequestOnly = r1vtable[14];
	RelativePathToFullPath = r1vtable[15];
	GetSearchPath = r1vtable[16];
	AddPackFile = r1vtable[17];
	RemoveFile = r1vtable[18];
	RenameFile = r1vtable[19];
	CreateDirHierarchy = r1vtable[20];
	IsDirectory = r1vtable[21];
	FileTimeToString = r1vtable[22];
	SetBufferSize = r1vtable[23];
	IsOK = r1vtable[24];
	EndOfLine = r1vtable[25];
	ReadLine = r1vtable[26];
	FPrintf = r1vtable[27];
	LoadModule = r1vtable[28];
	UnloadModule = r1vtable[29];
	FindFirst = r1vtable[30];
	FindNext = r1vtable[31];
	FindIsDirectory = r1vtable[32];
	FindClose = r1vtable[33];
	FindFirstEx = r1vtable[34];
	FindFileAbsoluteList = r1vtable[35];
	GetLocalPath = r1vtable[36];
	FullPathToRelativePath = r1vtable[37];
	GetCurrentDirectory_ = r1vtable[38];
	FindOrAddFileName = r1vtable[39];
	String = r1vtable[40];
	AsyncReadMultiple = r1vtable[41];
	AsyncAppend = r1vtable[42];
	AsyncAppendFile = r1vtable[43];
	AsyncFinishAll = r1vtable[44];
	AsyncFinishAllWrites = r1vtable[45];
	AsyncFlush = r1vtable[46];
	AsyncSuspend = r1vtable[47];
	AsyncResume = r1vtable[48];
	AsyncBeginRead = r1vtable[49];
	AsyncEndRead = r1vtable[50];
	AsyncFinish = r1vtable[51];
	AsyncGetResult = r1vtable[52];
	AsyncAbort = r1vtable[53];
	AsyncStatus = r1vtable[54];
	AsyncSetPriority = r1vtable[55];
	AsyncAddRef = r1vtable[56];
	AsyncRelease = r1vtable[57];
	sub_180024450 = r1vtable[58];
	sub_180024460 = r1vtable[59];
	nullsub_96 = r1vtable[60];
	sub_180024490 = r1vtable[61];
	sub_180024440 = r1vtable[62];
	nullsub_97 = r1vtable[63];
	sub_180009BE0 = r1vtable[64];
	sub_18000F6A0 = r1vtable[65];
	sub_180002CA0 = r1vtable[66];
	sub_180002CB0 = r1vtable[67];
	sub_1800154F0 = r1vtable[68];
	sub_180015550 = r1vtable[69];
	sub_180015420 = r1vtable[70];
	sub_180015480 = r1vtable[71];
	RemoveLoggingFunc = r1vtable[72];
	GetFilesystemStatistics = r1vtable[73];
	OpenEx = r1vtable[74];
	sub_18000A5D0 = r1vtable[75];
	sub_1800052A0 = r1vtable[76];
	sub_180002F10 = r1vtable[77];
	sub_18000A690 = r1vtable[78];
	sub_18000A6F0 = r1vtable[79];
	sub_1800057A0 = r1vtable[80];
	sub_180002960 = r1vtable[81];
	sub_180020110 = r1vtable[82];
	sub_180020230 = r1vtable[83];
	sub_180023660 = r1vtable[84];
	sub_1800204A0 = r1vtable[85];
	sub_180002F40 = r1vtable[86];
	sub_180004F00 = r1vtable[87];
	sub_180024020 = r1vtable[88];
	sub_180024AF0 = r1vtable[89];
	sub_180024110 = r1vtable[90];
	sub_180002580 = r1vtable[91];
	sub_180002560 = r1vtable[92];
	sub_18000A070 = r1vtable[93];
	sub_180009E80 = r1vtable[94];
	sub_180009C20 = r1vtable[95];
	sub_1800022F0 = r1vtable[96];
	sub_180002330 = r1vtable[97];
	sub_180009CF0 = r1vtable[98];
	sub_180002340 = r1vtable[99];
	sub_180002320 = r1vtable[100];
	sub_180009E00 = r1vtable[101];
	sub_180009F20 = r1vtable[102];
	sub_180009EA0 = r1vtable[103];
	sub_180009E50 = r1vtable[104];
	sub_180009FC0 = r1vtable[105];
	sub_180004E80 = r1vtable[106];
	sub_18000A000 = r1vtable[107];
	sub_180014350 = r1vtable[108];
	sub_18000F5B0 = r1vtable[109];
	sub_180002590 = r1vtable[110];
	sub_1800025D0 = r1vtable[111];
	sub_1800025E0 = r1vtable[112];
	LoadVPKForMap = r1vtable[113];
	UnkFunc1 = r1vtable[114];
	WeirdFuncThatJustDerefsRDX = r1vtable[115];
	GetPathTime = r1vtable[116];
	GetFSConstructedFlag = r1vtable[117];
	EnableWhitelistFileTracking = r1vtable[118];
	sub_18000A750 = r1vtable[119];
	sub_180002B20 = r1vtable[120];
	sub_18001DC30 = r1vtable[121];
	sub_180002B30 = r1vtable[122];
	sub_180002BA0 = r1vtable[123];
	sub_180002BB0 = r1vtable[124];
	sub_180002BC0 = r1vtable[125];
	sub_180002290 = r1vtable[126];
	sub_18001CCD0 = r1vtable[127];
	sub_18001CCE0 = r1vtable[128];
	sub_18001CCF0 = r1vtable[129];
	sub_18001CD00 = r1vtable[130];
	sub_180014520 = r1vtable[131];
	sub_180002650 = r1vtable[132];
	sub_18001CD10 = r1vtable[133];
	sub_180016250 = r1vtable[134];
	sub_18000F0D0 = r1vtable[135];
	sub_1800139F0 = r1vtable[136];
	sub_180016570 = r1vtable[137];
	nullsub_86 = r1vtable[138];
	sub_18000AEC0 = r1vtable[139];
	sub_180003320 = r1vtable[140];
	sub_18000AF50 = r1vtable[141];
	sub_18000AF60 = r1vtable[142];
	sub_180005D00 = r1vtable[143];
	sub_18000AF70 = r1vtable[144];
	sub_18001B130 = r1vtable[145];
	sub_18000AF80 = r1vtable[146];
	sub_1800034D0 = r1vtable[147];
	sub_180017180 = r1vtable[148];
	sub_180003550 = r1vtable[149];
	sub_1800250D0 = r1vtable[150];
	sub_1800241B0 = r1vtable[151];
	sub_1800241C0 = r1vtable[152];
	sub_1800241F0 = r1vtable[153];
	sub_180024240 = r1vtable[154];
	sub_180024250 = r1vtable[155];
	sub_180024260 = r1vtable[156];
	sub_180024300 = r1vtable[157];
	sub_180024310 = r1vtable[158];
	sub_180024320 = r1vtable[159];
	sub_180024340 = r1vtable[160];
	sub_180024350 = r1vtable[161];
	sub_180024360 = r1vtable[162];
	sub_180024390 = r1vtable[163];
	sub_180024370 = r1vtable[164];
	sub_1800243C0 = r1vtable[165];
	sub_1800243F0 = r1vtable[166];
	sub_180024410 = r1vtable[167];
	sub_180024430 = r1vtable[168];

	CreateR1OVTable(r1vtable);
}

void CVFileSystem::CreateR1OVTable(uintptr_t* r1vtable)
{
	g_r1oCVFileSystemInterface[0] = CreateFunction((void*)Connect, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[1] = CreateFunction((void*)Disconnect, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[2] = CreateFunction((void*)QueryInterface, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[3] = CreateFunction((void*)Init, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[4] = CreateFunction((void*)Shutdown, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[5] = CreateFunction((void*)GetDependencies, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[6] = CreateFunction((void*)GetTier, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[7] = CreateFunction((void*)Reconnect, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[8] = CreateFunction((void*)CFileSystem_Stdio__NullSub3, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[9] = CreateFunction((void*)CFileSystem_Stdio__NullSub4, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[10] = CreateFunction((void*)CBaseFileSystem__GetTFOFileSystemThing, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[11] = CreateFunction((void*)CFileSystem_Stdio__DoTFOFilesystemOp, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[12] = CreateFunction((void*)AddSearchPath, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[13] = CreateFunction((void*)RemoveSearchPath, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[14] = CreateFunction((void*)RemoveAllSearchPaths, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[15] = CreateFunction((void*)RemoveSearchPaths, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[16] = CreateFunction((void*)MarkPathIDByRequestOnly, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[17] = CreateFunction((void*)RelativePathToFullPath, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[18] = CreateFunction((void*)GetSearchPath, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[19] = CreateFunction((void*)AddPackFile, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[20] = CreateFunction((void*)RemoveFile, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[21] = CreateFunction((void*)RenameFile, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[22] = CreateFunction((void*)CreateDirHierarchy, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[23] = CreateFunction((void*)IsDirectory, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[24] = CreateFunction((void*)FileTimeToString, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[25] = CreateFunction((void*)SetBufferSize, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[26] = CreateFunction((void*)IsOK, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[27] = CreateFunction((void*)EndOfLine, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[28] = CreateFunction((void*)ReadLine, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[29] = CreateFunction((void*)FPrintf, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[30] = CreateFunction((void*)LoadModule, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[31] = CreateFunction((void*)UnloadModule, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[32] = CreateFunction((void*)FindFirst, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[33] = CreateFunction((void*)FindNext, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[34] = CreateFunction((void*)FindIsDirectory, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[35] = CreateFunction((void*)FindClose, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[36] = CreateFunction((void*)FindFirstEx, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[37] = CreateFunction((void*)FindFileAbsoluteList, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[38] = CreateFunction((void*)GetLocalPath, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[39] = CreateFunction((void*)FullPathToRelativePath, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[40] = CreateFunction((void*)GetCurrentDirectory_, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[41] = CreateFunction((void*)FindOrAddFileName, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[42] = CreateFunction((void*)String, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[43] = CreateFunction((void*)AsyncReadMultiple, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[44] = CreateFunction((void*)AsyncAppend, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[45] = CreateFunction((void*)AsyncAppendFile, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[46] = CreateFunction((void*)AsyncFinishAll, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[47] = CreateFunction((void*)AsyncFinishAllWrites, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[48] = CreateFunction((void*)AsyncFlush, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[49] = CreateFunction((void*)AsyncSuspend, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[50] = CreateFunction((void*)AsyncResume, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[51] = CreateFunction((void*)AsyncBeginRead, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[52] = CreateFunction((void*)AsyncEndRead, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[53] = CreateFunction((void*)AsyncFinish, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[54] = CreateFunction((void*)AsyncGetResult, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[55] = CreateFunction((void*)AsyncAbort, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[56] = CreateFunction((void*)AsyncStatus, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[57] = CreateFunction((void*)AsyncSetPriority, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[58] = CreateFunction((void*)AsyncAddRef, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[59] = CreateFunction((void*)AsyncRelease, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[60] = CreateFunction((void*)sub_180024450, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[61] = CreateFunction((void*)sub_180024460, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[62] = CreateFunction((void*)nullsub_96, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[63] = CreateFunction((void*)sub_180024490, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[64] = CreateFunction((void*)sub_180024440, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[65] = CreateFunction((void*)nullsub_97, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[66] = CreateFunction((void*)sub_180009BE0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[67] = CreateFunction((void*)sub_18000F6A0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[68] = CreateFunction((void*)sub_180002CA0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[69] = CreateFunction((void*)sub_180002CB0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[70] = CreateFunction((void*)sub_1800154F0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[71] = CreateFunction((void*)sub_180015550, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[72] = CreateFunction((void*)sub_180015420, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[73] = CreateFunction((void*)sub_180015480, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[74] = CreateFunction((void*)RemoveLoggingFunc, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[75] = CreateFunction((void*)GetFilesystemStatistics, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[76] = CreateFunction((void*)OpenEx, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[77] = CreateFunction((void*)sub_18000A5D0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[78] = CreateFunction((void*)sub_1800052A0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[79] = CreateFunction((void*)sub_180002F10, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[80] = CreateFunction((void*)sub_18000A690, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[81] = CreateFunction((void*)sub_18000A6F0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[82] = CreateFunction((void*)sub_1800057A0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[83] = CreateFunction((void*)sub_180002960, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[84] = CreateFunction((void*)sub_180020110, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[85] = CreateFunction((void*)sub_180020230, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[86] = CreateFunction((void*)sub_180023660, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[87] = CreateFunction((void*)sub_1800204A0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[88] = CreateFunction((void*)sub_180002F40, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[89] = CreateFunction((void*)sub_180004F00, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[90] = CreateFunction((void*)sub_180024020, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[91] = CreateFunction((void*)sub_180024AF0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[92] = CreateFunction((void*)sub_180024110, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[93] = CreateFunction((void*)sub_180002580, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[94] = CreateFunction((void*)sub_180002560, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[95] = CreateFunction((void*)sub_18000A070, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[96] = CreateFunction((void*)CFileSystem_Stdio__NullSub4, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[97] = CreateFunction((void*)sub_180009C20, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[98] = CreateFunction((void*)sub_1800022F0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[99] = CreateFunction((void*)sub_180002330, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[100] = CreateFunction((void*)sub_180009CF0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[101] = CreateFunction((void*)sub_180002340, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[102] = CreateFunction((void*)sub_180002320, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[103] = CreateFunction((void*)sub_180009E00, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[104] = CreateFunction((void*)sub_180009F20, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[105] = CreateFunction((void*)sub_180009EA0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[106] = CreateFunction((void*)sub_180009E50, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[107] = CreateFunction((void*)sub_180009FC0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[108] = CreateFunction((void*)sub_180004E80, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[109] = CreateFunction((void*)sub_18000A000, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[110] = CreateFunction((void*)sub_180014350, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[111] = CreateFunction((void*)sub_18000F5B0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[112] = CreateFunction((void*)sub_180002590, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[113] = CreateFunction((void*)sub_1800025D0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[114] = CreateFunction((void*)sub_1800025E0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[115] = CreateFunction((void*)LoadVPKForMap, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[116] = CreateFunction((void*)UnkFunc1, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[117] = CreateFunction((void*)WeirdFuncThatJustDerefsRDX, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[118] = CreateFunction((void*)GetPathTime, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[119] = CreateFunction((void*)GetFSConstructedFlag, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[120] = CreateFunction((void*)EnableWhitelistFileTracking, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[121] = CreateFunction((void*)sub_18000A750, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[122] = CreateFunction((void*)sub_180002B20, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[124] = CreateFunction((void*)sub_18001DC30, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[125] = CreateFunction((void*)sub_180002B30, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[126] = CreateFunction((void*)sub_180002BA0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[127] = CreateFunction((void*)sub_180002BB0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[128] = CreateFunction((void*)sub_180002BC0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[129] = CreateFunction((void*)sub_180002290, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[130] = CreateFunction((void*)sub_18001CCD0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[131] = CreateFunction((void*)sub_18001CCE0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[132] = CreateFunction((void*)sub_18001CCF0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[133] = CreateFunction((void*)sub_18001CD00, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[134] = CreateFunction((void*)sub_180014520, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[135] = CreateFunction((void*)sub_180002650, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[136] = CreateFunction((void*)sub_18001CD10, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[137] = CreateFunction((void*)sub_180016250, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[138] = CreateFunction((void*)sub_18000F0D0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[139] = CreateFunction((void*)sub_1800139F0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[140] = CreateFunction((void*)sub_180016570, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[141] = CreateFunction((void*)nullsub_86, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[142] = CreateFunction((void*)sub_18000AEC0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[143] = CreateFunction((void*)sub_180003320, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[144] = CreateFunction((void*)sub_18000AF50, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[145] = CreateFunction((void*)sub_18000AF60, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[146] = CreateFunction((void*)sub_180005D00, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[147] = CreateFunction((void*)sub_18000AF70, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[148] = CreateFunction((void*)sub_18001B130, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[149] = CreateFunction((void*)sub_18000AF80, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[150] = CreateFunction((void*)sub_1800034D0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[151] = CreateFunction((void*)sub_180017180, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[152] = CreateFunction((void*)sub_180003550, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[153] = CreateFunction((void*)sub_1800250D0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[154] = CreateFunction((void*)sub_1800241B0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[155] = CreateFunction((void*)sub_1800241C0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[156] = CreateFunction((void*)sub_1800241F0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[157] = CreateFunction((void*)sub_180024240, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[158] = CreateFunction((void*)sub_180024250, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[159] = CreateFunction((void*)sub_180024260, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[160] = CreateFunction((void*)sub_180024300, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[161] = CreateFunction((void*)sub_180024310, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[162] = CreateFunction((void*)sub_180024320, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[163] = CreateFunction((void*)sub_180024340, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[164] = CreateFunction((void*)sub_180024350, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[165] = CreateFunction((void*)sub_180024360, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[166] = CreateFunction((void*)sub_180024390, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[167] = CreateFunction((void*)sub_180024370, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[168] = CreateFunction((void*)sub_1800243C0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[169] = CreateFunction((void*)sub_1800243F0, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[170] = CreateFunction((void*)sub_180024410, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[171] = CreateFunction((void*)sub_180024430, (void*)g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[172] = CreateFunction((void*)CFileSystem_Stdio__NullSub4, r1vtable);
}

//
// ----------------------------------------------------------------------------
//

CBaseFileSystem::CBaseFileSystem(uintptr_t* r1vtable)
{
	Read = r1vtable[0];
	Write = r1vtable[1];
	Open = r1vtable[2];
	Close = r1vtable[3];
	Seek = r1vtable[4];
	Tell = r1vtable[5];
	Size = r1vtable[6];
	Size2 = r1vtable[7];
	Flush = r1vtable[8];
	Precache = r1vtable[9];
	FileExists = r1vtable[10];
	IsFileWritable = r1vtable[11];
	SetFileWritable = r1vtable[12];
	GetFileTime = r1vtable[13];
	ReadFile = r1vtable[14];
	WriteFile = r1vtable[15];
	UnzipFile = r1vtable[16];

	CreateR1OVTable(r1vtable);
}

void CBaseFileSystem::CreateR1OVTable(uintptr_t* r1vtable)
{
	g_r1oCBaseFileSystemInterface[0] = CreateFunction((void*)Read, (void*)g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[1] = CreateFunction((void*)Write, (void*)g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[2] = CreateFunction((void*)Open, (void*)g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[3] = CreateFunction((void*)Close, (void*)g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[4] = CreateFunction((void*)Seek, (void*)g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[5] = CreateFunction((void*)Tell, (void*)g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[6] = CreateFunction((void*)Size, (void*)g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[7] = CreateFunction((void*)Size2, (void*)g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[8] = CreateFunction((void*)Flush, (void*)g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[9] = CreateFunction((void*)Precache, (void*)g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[10] = CreateFunction((void*)FileExists, (void*)g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[11] = CreateFunction((void*)IsFileWritable, (void*)g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[12] = CreateFunction((void*)SetFileWritable, (void*)g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[13] = CreateFunction((void*)GetFileTime, (void*)g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[14] = CreateFunction((void*)ReadFile, (void*)g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[15] = CreateFunction((void*)WriteFile, (void*)g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[16] = CreateFunction((void*)UnzipFile, (void*)g_CBaseFileSystemInterface);
}
