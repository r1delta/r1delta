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

#pragma once
 
#include "utils.h"
#include "filecache.h"

#include <iostream>
#include <string>
#include <mutex>
#include <filesystem>
#include <unordered_set>

class CVFileSystem;
class CBaseFileSystem;
struct IFileSystem;
extern CVFileSystem* g_CVFileSystem;
extern CBaseFileSystem* g_CBaseFileSystem;
extern uintptr_t g_CVFileSystemInterface;
extern uintptr_t g_r1oCVFileSystemInterface[173];
extern uintptr_t g_r1oCBaseFileSystemInterface[17];
extern IFileSystem* g_CBaseFileSystemInterface;

struct VPKData;
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
char CZipPackFile__Prepare(__int64* a1, unsigned __int64 a2, __int64 a3);
int fs_sprintf_hook(char* Buffer, const char* Format, ...);

class CBaseFileSystem
{
public:
	CBaseFileSystem() = default;
	CBaseFileSystem(uintptr_t* r1vtable);
	void CreateR1OVTable(uintptr_t* r1vtable);

	uintptr_t Read;
	uintptr_t Write;
	uintptr_t Open; // note
	uintptr_t Close;
	uintptr_t Seek;
	uintptr_t Tell;
	uintptr_t Size;
	uintptr_t Size2;
	uintptr_t Flush;
	uintptr_t Precache;
	uintptr_t FileExists;
	uintptr_t IsFileWritable;
	uintptr_t SetFileWritable;
	uintptr_t GetFileTime;
	uintptr_t ReadFile; // note
	uintptr_t WriteFile;
	uintptr_t UnzipFile;
};

class CVFileSystem
{
public:
	CVFileSystem() = default;
	CVFileSystem(uintptr_t* r1vtable);
	void CreateR1OVTable(uintptr_t* r1vtable);

	uintptr_t Connect;
	uintptr_t Disconnect;
	uintptr_t QueryInterface;
	uintptr_t Init;
	uintptr_t Shutdown;
	uintptr_t GetDependencies;
	uintptr_t GetTier;
	uintptr_t Reconnect;
	uintptr_t sub_180023F80;
	uintptr_t sub_180023F90;
	uintptr_t AddSearchPath;
	uintptr_t RemoveSearchPath;
	uintptr_t RemoveAllSearchPaths;
	uintptr_t RemoveSearchPaths;
	uintptr_t MarkPathIDByRequestOnly;
	uintptr_t RelativePathToFullPath;
	uintptr_t GetSearchPath;
	uintptr_t AddPackFile;
	uintptr_t RemoveFile;
	uintptr_t RenameFile;
	uintptr_t CreateDirHierarchy;
	uintptr_t IsDirectory;
	uintptr_t FileTimeToString;
	uintptr_t SetBufferSize;
	uintptr_t IsOK;
	uintptr_t EndOfLine;
	uintptr_t ReadLine;
	uintptr_t FPrintf;
	uintptr_t LoadModule;
	uintptr_t UnloadModule;
	uintptr_t FindFirst;
	uintptr_t FindNext;
	uintptr_t FindIsDirectory;
	uintptr_t FindClose;
	uintptr_t FindFirstEx;
	uintptr_t FindFileAbsoluteList;
	uintptr_t GetLocalPath;
	uintptr_t FullPathToRelativePath;
	uintptr_t GetCurrentDirectory_;
	uintptr_t FindOrAddFileName;
	uintptr_t String;
	uintptr_t AsyncReadMultiple;
	uintptr_t AsyncAppend;
	uintptr_t AsyncAppendFile;
	uintptr_t AsyncFinishAll;
	uintptr_t AsyncFinishAllWrites;
	uintptr_t AsyncFlush;
	uintptr_t AsyncSuspend;
	uintptr_t AsyncResume;
	uintptr_t AsyncBeginRead;
	uintptr_t AsyncEndRead;
	uintptr_t AsyncFinish;
	uintptr_t AsyncGetResult;
	uintptr_t AsyncAbort;
	uintptr_t AsyncStatus;
	uintptr_t AsyncSetPriority;
	uintptr_t AsyncAddRef;
	uintptr_t AsyncRelease;
	uintptr_t sub_180024450;
	uintptr_t sub_180024460;
	uintptr_t nullsub_96;
	uintptr_t sub_180024490;
	uintptr_t sub_180024440;
	uintptr_t nullsub_97;
	uintptr_t sub_180009BE0;
	uintptr_t sub_18000F6A0;
	uintptr_t sub_180002CA0;
	uintptr_t sub_180002CB0;
	uintptr_t sub_1800154F0;
	uintptr_t sub_180015550;
	uintptr_t sub_180015420;
	uintptr_t sub_180015480;
	uintptr_t RemoveLoggingFunc;
	uintptr_t GetFilesystemStatistics;
	uintptr_t OpenEx;
	uintptr_t sub_18000A5D0;
	uintptr_t sub_1800052A0;
	uintptr_t sub_180002F10;
	uintptr_t sub_18000A690;
	uintptr_t sub_18000A6F0;
	uintptr_t sub_1800057A0;
	uintptr_t sub_180002960;
	uintptr_t sub_180020110;
	uintptr_t sub_180020230;
	uintptr_t sub_180023660;
	uintptr_t sub_1800204A0;
	uintptr_t sub_180002F40;
	uintptr_t sub_180004F00;
	uintptr_t sub_180024020;
	uintptr_t sub_180024AF0;
	uintptr_t sub_180024110;
	uintptr_t sub_180002580;
	uintptr_t sub_180002560;
	uintptr_t sub_18000A070;
	uintptr_t sub_180009E80;
	uintptr_t sub_180009C20; // note
	uintptr_t sub_1800022F0;
	uintptr_t sub_180002330;
	uintptr_t sub_180009CF0;
	uintptr_t sub_180002340;
	uintptr_t sub_180002320;
	uintptr_t sub_180009E00;
	uintptr_t sub_180009F20;
	uintptr_t sub_180009EA0;
	uintptr_t sub_180009E50;
	uintptr_t sub_180009FC0;
	uintptr_t sub_180004E80;
	uintptr_t sub_18000A000;
	uintptr_t sub_180014350;
	uintptr_t sub_18000F5B0;
	uintptr_t sub_180002590;
	uintptr_t sub_1800025D0;
	uintptr_t sub_1800025E0;
	uintptr_t LoadVPKForMap;
	uintptr_t UnkFunc1;
	uintptr_t WeirdFuncThatJustDerefsRDX;
	uintptr_t GetPathTime;
	uintptr_t GetFSConstructedFlag;
	uintptr_t EnableWhitelistFileTracking;
	uintptr_t sub_18000A750;
	uintptr_t sub_180002B20;
	uintptr_t sub_18001DC30;
	uintptr_t sub_180002B30;
	uintptr_t sub_180002BA0;
	uintptr_t sub_180002BB0;
	uintptr_t sub_180002BC0;
	uintptr_t sub_180002290;
	uintptr_t sub_18001CCD0;
	uintptr_t sub_18001CCE0;
	uintptr_t sub_18001CCF0;
	uintptr_t sub_18001CD00;
	uintptr_t sub_180014520;
	uintptr_t sub_180002650;
	uintptr_t sub_18001CD10;
	uintptr_t sub_180016250;
	uintptr_t sub_18000F0D0;
	uintptr_t sub_1800139F0;
	uintptr_t sub_180016570;
	uintptr_t nullsub_86;
	uintptr_t sub_18000AEC0;
	uintptr_t sub_180003320;
	uintptr_t sub_18000AF50;
	uintptr_t sub_18000AF60;
	uintptr_t sub_180005D00;
	uintptr_t sub_18000AF70;
	uintptr_t sub_18001B130;
	uintptr_t sub_18000AF80;
	uintptr_t sub_1800034D0;
	uintptr_t sub_180017180;
	uintptr_t sub_180003550;
	uintptr_t sub_1800250D0;
	uintptr_t sub_1800241B0;
	uintptr_t sub_1800241C0;
	uintptr_t sub_1800241F0;
	uintptr_t sub_180024240;
	uintptr_t sub_180024250;
	uintptr_t sub_180024260;
	uintptr_t sub_180024300;
	uintptr_t sub_180024310;
	uintptr_t sub_180024320;
	uintptr_t sub_180024340;
	uintptr_t sub_180024350;
	uintptr_t sub_180024360;
	uintptr_t sub_180024390;
	uintptr_t sub_180024370;
	uintptr_t sub_1800243C0;
	uintptr_t sub_1800243F0;
	uintptr_t sub_180024410;
	uintptr_t sub_180024430;
};