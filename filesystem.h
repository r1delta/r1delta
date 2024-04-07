#pragma once
 
#include "windowsutil.h"

struct VPKData;
struct IFileSystem;
typedef void* FileHandle_t;
// hook forward declares
typedef FileHandle_t(*ReadFileFromVPKType)(VPKData* vpkInfo, __int64* b, char* filename);
extern ReadFileFromVPKType readFileFromVPK;
FileHandle_t ReadFileFromVPKHook(VPKData* vpkInfo, __int64* b, char* filename);

typedef bool (*ReadFromCacheType)(IFileSystem* filesystem, char* path, void* result);
extern ReadFromCacheType readFromCache;
bool ReadFromCacheHook(IFileSystem* filesystem, char* path, void* result);

typedef FileHandle_t(*ReadFileFromFilesystemType)(
	IFileSystem* filesystem, const char* pPath, const char* pOptions, int64_t a4, uint32_t a5, void* a6);
extern ReadFileFromFilesystemType readFileFromFilesystem;
FileHandle_t ReadFileFromFilesystemHook(IFileSystem* filesystem, const char* pPath, const char* pOptions, int64_t a4, uint32_t a5, void* a6);

bool V_IsAbsolutePath(const char* pStr);
bool TryReplaceFile(const char* pszFilePath);
typedef __int64 (*FileSystem_UpdateAddonSearchPathsType)(void* a1);
extern FileSystem_UpdateAddonSearchPathsType FileSystem_UpdateAddonSearchPathsTypeOriginal;
__int64 __fastcall FileSystem_UpdateAddonSearchPaths(void* a1);
void StartFileCacheThread();