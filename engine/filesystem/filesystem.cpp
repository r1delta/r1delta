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
#include "tctx.h"
#include "persistentdata.h"
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
    auto ret = FileSystem_UpdateAddonSearchPathsTypeOriginal(a1);

	PDef::InitValidator();
	return ret;
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

	// Check for mp_mia or mp_nest2 and load mp_angel_city assets first
	const char* mapAliases[] = { "mp_mia", "mp_nest2" };
	for (const char* alias : mapAliases)
	{
		if (strstr(vpkPath, alias) != NULL)
		{
			// Create a modified path with mp_angel_city instead
			size_t pathLen = strlen(vpkPath);
			size_t aliasLen = strlen(alias);
			size_t angelCityLen = strlen("mp_angel_city");
			size_t newPathLen = pathLen - aliasLen + angelCityLen + 1;

			char* modifiedPath = (char*)_alloca(newPathLen);
			char* aliasPos = strstr(vpkPath, alias);
			size_t prefixLen = aliasPos - vpkPath;

			memcpy(modifiedPath, vpkPath, prefixLen);
			memcpy(modifiedPath + prefixLen, "mp_angel_city", angelCityLen);
			strcpy(modifiedPath + prefixLen + angelCityLen, aliasPos + aliasLen);

			// Call original with mp_angel_city path first
			AddVPKFileOriginal(fileSystem, modifiedPath, a3, a4, a5, a6);
			break;
		}
	}

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

// Mid-function patch for texture streaming to guard against invalid pointer dereference
// At offset 0x74E06: cmp qword ptr [rbx], 0 / jz loc_180074E44
// We patch this to check if rbx is valid first, and if not, jump to the fallback path
static unsigned char* g_TextureStreamingPatchAddr = nullptr;
static unsigned char* g_TextureStreamingCodeCave = nullptr;

// Allocate executable memory within ±2GB of target for relative jumps
static void* AllocateNearby(void* target, size_t size)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	uintptr_t granularity = si.dwAllocationGranularity;

	uintptr_t targetAddr = (uintptr_t)target;
	// Search within ±1GB to be safe
	uintptr_t minAddr = targetAddr > 0x40000000 ? targetAddr - 0x40000000 : granularity;
	uintptr_t maxAddr = targetAddr + 0x40000000;

	// Search downward first
	for (uintptr_t addr = (targetAddr - granularity) & ~(granularity - 1); addr >= minAddr; addr -= granularity) {
		void* result = VirtualAlloc((void*)addr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (result) return result;
	}

	// Search upward
	for (uintptr_t addr = (targetAddr + granularity) & ~(granularity - 1); addr <= maxAddr; addr += granularity) {
		void* result = VirtualAlloc((void*)addr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (result) return result;
	}

	return nullptr;
}

void InitTextureStreamingPatch(uintptr_t filesystemBase)
{
	// Address of the cmp instruction we're patching
	g_TextureStreamingPatchAddr = (unsigned char*)(filesystemBase + 0x74E06);

	// Target address for the fallback path (LABEL_9)
	unsigned char* fallbackAddr = (unsigned char*)(filesystemBase + 0x74E44);

	// Return address after our check (after the original jz)
	unsigned char* returnAddr = (unsigned char*)(filesystemBase + 0x74E0C);

	// Allocate executable memory NEAR the target for relative jumps to work
	g_TextureStreamingCodeCave = (unsigned char*)AllocateNearby(g_TextureStreamingPatchAddr, 64);
	if (!g_TextureStreamingCodeCave) return;

	// Build the code cave:
	// cmp rbx, -1
	// je fallback
	// cmp qword ptr [rbx], 0  (original instruction)
	// je fallback
	// jmp returnAddr
	unsigned char* p = g_TextureStreamingCodeCave;

	// cmp rbx, -1 (48 83 FB FF)
	*p++ = 0x48; *p++ = 0x83; *p++ = 0xFB; *p++ = 0xFF;

	// je fallback (0F 84 xx xx xx xx) - will fill in offset
	*p++ = 0x0F; *p++ = 0x84;
	int32_t fallbackOffset1 = (int32_t)(fallbackAddr - (p + 4));
	memcpy(p, &fallbackOffset1, 4); p += 4;

	// cmp qword ptr [rbx], 0 (48 83 3B 00) - original instruction
	*p++ = 0x48; *p++ = 0x83; *p++ = 0x3B; *p++ = 0x00;

	// je fallback (0F 84 xx xx xx xx)
	*p++ = 0x0F; *p++ = 0x84;
	int32_t fallbackOffset2 = (int32_t)(fallbackAddr - (p + 4));
	memcpy(p, &fallbackOffset2, 4); p += 4;

	// jmp returnAddr (E9 xx xx xx xx)
	*p++ = 0xE9;
	int32_t returnOffset = (int32_t)(returnAddr - (p + 4));
	memcpy(p, &returnOffset, 4); p += 4;

	// Now patch the original code to jump to our code cave
	DWORD oldProtect;
	VirtualProtect(g_TextureStreamingPatchAddr, 6, PAGE_EXECUTE_READWRITE, &oldProtect);

	// jmp codeCave (E9 xx xx xx xx) + nop
	g_TextureStreamingPatchAddr[0] = 0xE9;
	int32_t caveJmpOffset = (int32_t)(g_TextureStreamingCodeCave - (g_TextureStreamingPatchAddr + 5));
	memcpy(g_TextureStreamingPatchAddr + 1, &caveJmpOffset, 4);
	g_TextureStreamingPatchAddr[5] = 0x90; // NOP the remaining byte

	VirtualProtect(g_TextureStreamingPatchAddr, 6, oldProtect, &oldProtect);
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
