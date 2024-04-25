#pragma once
 
#include "utils.h"

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
	IFileSystem* filesystem, const char* pPath, const char* pOptions, __int64 a4, unsigned __int32 a5, void* a6);
extern ReadFileFromFilesystemType readFileFromFilesystem;
FileHandle_t ReadFileFromFilesystemHook(IFileSystem* filesystem, const char* pPath, const char* pOptions, __int64 a4, unsigned __int32 a5, void* a6);

bool V_IsAbsolutePath(const char* pStr);
bool TryReplaceFile(const char* pszFilePath);
typedef __int64 (*FileSystem_UpdateAddonSearchPathsType)(void* a1);
extern FileSystem_UpdateAddonSearchPathsType FileSystem_UpdateAddonSearchPathsTypeOriginal;
__int64 __fastcall FileSystem_UpdateAddonSearchPaths(void* a1);
void StartFileCacheThread();
typedef __int64 (*AddVPKFileType)(IFileSystem* fileSystem, char* a2, char** a3, char a4, int a5, char a6);
extern AddVPKFileType AddVPKFileOriginal;
__int64 __fastcall AddVPKFile(IFileSystem* fileSystem, char* a2, char** a3, char a4, int a5, char a6);
typedef void (*CBaseFileSystem__CSearchPath__SetPathType)(void* thisptr, __int16* id);
extern CBaseFileSystem__CSearchPath__SetPathType CBaseFileSystem__CSearchPath__SetPathOriginal;
void __fastcall CBaseFileSystem__CSearchPath__SetPath(void* thisptr, __int16* id);
typedef char (*CZipPackFile__PrepareType)(__int64* a1, unsigned __int64 a2, __int64 a3);
extern CZipPackFile__PrepareType CZipPackFile__PrepareOriginal;
char __fastcall CZipPackFile__Prepare(__int64* a1, unsigned __int64 a2, __int64 a3);
int fs_sprintf_hook(char* Buffer, const char* Format, ...);
