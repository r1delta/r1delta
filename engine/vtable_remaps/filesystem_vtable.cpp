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
//
// VFileSystem017 VTable Mapping
//
// This file contains the vtable trampolines for the filesystem interface to bridge
// the vtable layout differences between Titanfall 1 (2015) and Titanfall Online (R1O).
// The R1O engine expects additional virtual functions that don't exist in the base game.
//
// See vtable.h for documentation on the trampoline mechanism.
//

#include "core.h"
#include "filesystem.h"
#include "vtable.h"

//=============================================================================
// Stub functions for R1O-specific filesystem methods
//=============================================================================

// TFO-specific: Returns 0 (no TFO filesystem support)
static char CFileSystem_Stdio__NullSub3()
{
	return 0;
}

// TFO-specific: Returns 0 (no TFO filesystem support)
static int64_t CFileSystem_Stdio__NullSub4()
{
	return 0i64;
}

// TFO-specific: GetTFOFileSystemThing - not implemented
static int64_t CBaseFileSystem__GetTFOFileSystemThing()
{
	return 0;
}

// TFO-specific: DoTFOFilesystemOp - not implemented
static int64_t CFileSystem_Stdio__DoTFOFilesystemOp(__int64 a1, char* a2, rsize_t a3)
{
	return false;
}

//=============================================================================
// CVFileSystem VTable Mapping
//=============================================================================

CVFileSystem::CVFileSystem(uintptr_t* r1vtable)
{
	// Store original vtable function pointers
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
	// R1O vtable has 4 additional functions at indices 8-11 compared to R1
	// We insert stubs for TFO-specific functions and shift the rest

	g_r1oCVFileSystemInterface[0] = VTABLE_TRAMPOLINE(Connect, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[1] = VTABLE_TRAMPOLINE(Disconnect, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[2] = VTABLE_TRAMPOLINE(QueryInterface, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[3] = VTABLE_TRAMPOLINE(Init, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[4] = VTABLE_TRAMPOLINE(Shutdown, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[5] = VTABLE_TRAMPOLINE(GetDependencies, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[6] = VTABLE_TRAMPOLINE(GetTier, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[7] = VTABLE_TRAMPOLINE(Reconnect, g_CVFileSystemInterface);
	// TFO-specific stubs (indices 8-11)
	g_r1oCVFileSystemInterface[8] = VTABLE_TRAMPOLINE(CFileSystem_Stdio__NullSub3, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[9] = VTABLE_TRAMPOLINE(CFileSystem_Stdio__NullSub4, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[10] = VTABLE_TRAMPOLINE(CBaseFileSystem__GetTFOFileSystemThing, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[11] = VTABLE_TRAMPOLINE(CFileSystem_Stdio__DoTFOFilesystemOp, g_CVFileSystemInterface);
	// Rest of vtable shifted by 4
	g_r1oCVFileSystemInterface[12] = VTABLE_TRAMPOLINE(AddSearchPath, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[13] = VTABLE_TRAMPOLINE(RemoveSearchPath, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[14] = VTABLE_TRAMPOLINE(RemoveAllSearchPaths, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[15] = VTABLE_TRAMPOLINE(RemoveSearchPaths, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[16] = VTABLE_TRAMPOLINE(MarkPathIDByRequestOnly, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[17] = VTABLE_TRAMPOLINE(RelativePathToFullPath, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[18] = VTABLE_TRAMPOLINE(GetSearchPath, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[19] = VTABLE_TRAMPOLINE(AddPackFile, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[20] = VTABLE_TRAMPOLINE(RemoveFile, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[21] = VTABLE_TRAMPOLINE(RenameFile, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[22] = VTABLE_TRAMPOLINE(CreateDirHierarchy, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[23] = VTABLE_TRAMPOLINE(IsDirectory, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[24] = VTABLE_TRAMPOLINE(FileTimeToString, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[25] = VTABLE_TRAMPOLINE(SetBufferSize, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[26] = VTABLE_TRAMPOLINE(IsOK, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[27] = VTABLE_TRAMPOLINE(EndOfLine, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[28] = VTABLE_TRAMPOLINE(ReadLine, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[29] = VTABLE_TRAMPOLINE(FPrintf, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[30] = VTABLE_TRAMPOLINE(LoadModule, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[31] = VTABLE_TRAMPOLINE(UnloadModule, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[32] = VTABLE_TRAMPOLINE(FindFirst, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[33] = VTABLE_TRAMPOLINE(FindNext, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[34] = VTABLE_TRAMPOLINE(FindIsDirectory, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[35] = VTABLE_TRAMPOLINE(FindClose, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[36] = VTABLE_TRAMPOLINE(FindFirstEx, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[37] = VTABLE_TRAMPOLINE(FindFileAbsoluteList, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[38] = VTABLE_TRAMPOLINE(GetLocalPath, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[39] = VTABLE_TRAMPOLINE(FullPathToRelativePath, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[40] = VTABLE_TRAMPOLINE(GetCurrentDirectory_, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[41] = VTABLE_TRAMPOLINE(FindOrAddFileName, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[42] = VTABLE_TRAMPOLINE(String, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[43] = VTABLE_TRAMPOLINE(AsyncReadMultiple, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[44] = VTABLE_TRAMPOLINE(AsyncAppend, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[45] = VTABLE_TRAMPOLINE(AsyncAppendFile, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[46] = VTABLE_TRAMPOLINE(AsyncFinishAll, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[47] = VTABLE_TRAMPOLINE(AsyncFinishAllWrites, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[48] = VTABLE_TRAMPOLINE(AsyncFlush, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[49] = VTABLE_TRAMPOLINE(AsyncSuspend, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[50] = VTABLE_TRAMPOLINE(AsyncResume, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[51] = VTABLE_TRAMPOLINE(AsyncBeginRead, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[52] = VTABLE_TRAMPOLINE(AsyncEndRead, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[53] = VTABLE_TRAMPOLINE(AsyncFinish, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[54] = VTABLE_TRAMPOLINE(AsyncGetResult, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[55] = VTABLE_TRAMPOLINE(AsyncAbort, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[56] = VTABLE_TRAMPOLINE(AsyncStatus, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[57] = VTABLE_TRAMPOLINE(AsyncSetPriority, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[58] = VTABLE_TRAMPOLINE(AsyncAddRef, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[59] = VTABLE_TRAMPOLINE(AsyncRelease, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[60] = VTABLE_TRAMPOLINE(sub_180024450, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[61] = VTABLE_TRAMPOLINE(sub_180024460, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[62] = VTABLE_TRAMPOLINE(nullsub_96, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[63] = VTABLE_TRAMPOLINE(sub_180024490, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[64] = VTABLE_TRAMPOLINE(sub_180024440, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[65] = VTABLE_TRAMPOLINE(nullsub_97, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[66] = VTABLE_TRAMPOLINE(sub_180009BE0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[67] = VTABLE_TRAMPOLINE(sub_18000F6A0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[68] = VTABLE_TRAMPOLINE(sub_180002CA0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[69] = VTABLE_TRAMPOLINE(sub_180002CB0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[70] = VTABLE_TRAMPOLINE(sub_1800154F0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[71] = VTABLE_TRAMPOLINE(sub_180015550, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[72] = VTABLE_TRAMPOLINE(sub_180015420, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[73] = VTABLE_TRAMPOLINE(sub_180015480, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[74] = VTABLE_TRAMPOLINE(RemoveLoggingFunc, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[75] = VTABLE_TRAMPOLINE(GetFilesystemStatistics, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[76] = VTABLE_TRAMPOLINE(OpenEx, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[77] = VTABLE_TRAMPOLINE(sub_18000A5D0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[78] = VTABLE_TRAMPOLINE(sub_1800052A0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[79] = VTABLE_TRAMPOLINE(sub_180002F10, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[80] = VTABLE_TRAMPOLINE(sub_18000A690, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[81] = VTABLE_TRAMPOLINE(sub_18000A6F0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[82] = VTABLE_TRAMPOLINE(sub_1800057A0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[83] = VTABLE_TRAMPOLINE(sub_180002960, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[84] = VTABLE_TRAMPOLINE(sub_180020110, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[85] = VTABLE_TRAMPOLINE(sub_180020230, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[86] = VTABLE_TRAMPOLINE(sub_180023660, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[87] = VTABLE_TRAMPOLINE(sub_1800204A0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[88] = VTABLE_TRAMPOLINE(sub_180002F40, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[89] = VTABLE_TRAMPOLINE(sub_180004F00, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[90] = VTABLE_TRAMPOLINE(sub_180024020, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[91] = VTABLE_TRAMPOLINE(sub_180024AF0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[92] = VTABLE_TRAMPOLINE(sub_180024110, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[93] = VTABLE_TRAMPOLINE(sub_180002580, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[94] = VTABLE_TRAMPOLINE(sub_180002560, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[95] = VTABLE_TRAMPOLINE(sub_18000A070, g_CVFileSystemInterface);
	// Index 96 uses a stub instead of sub_180009E80
	g_r1oCVFileSystemInterface[96] = VTABLE_TRAMPOLINE(CFileSystem_Stdio__NullSub4, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[97] = VTABLE_TRAMPOLINE(sub_180009C20, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[98] = VTABLE_TRAMPOLINE(sub_1800022F0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[99] = VTABLE_TRAMPOLINE(sub_180002330, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[100] = VTABLE_TRAMPOLINE(sub_180009CF0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[101] = VTABLE_TRAMPOLINE(sub_180002340, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[102] = VTABLE_TRAMPOLINE(sub_180002320, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[103] = VTABLE_TRAMPOLINE(sub_180009E00, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[104] = VTABLE_TRAMPOLINE(sub_180009F20, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[105] = VTABLE_TRAMPOLINE(sub_180009EA0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[106] = VTABLE_TRAMPOLINE(sub_180009E50, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[107] = VTABLE_TRAMPOLINE(sub_180009FC0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[108] = VTABLE_TRAMPOLINE(sub_180004E80, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[109] = VTABLE_TRAMPOLINE(sub_18000A000, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[110] = VTABLE_TRAMPOLINE(sub_180014350, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[111] = VTABLE_TRAMPOLINE(sub_18000F5B0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[112] = VTABLE_TRAMPOLINE(sub_180002590, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[113] = VTABLE_TRAMPOLINE(sub_1800025D0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[114] = VTABLE_TRAMPOLINE(sub_1800025E0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[115] = VTABLE_TRAMPOLINE(LoadVPKForMap, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[116] = VTABLE_TRAMPOLINE(UnkFunc1, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[117] = VTABLE_TRAMPOLINE(WeirdFuncThatJustDerefsRDX, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[118] = VTABLE_TRAMPOLINE(GetPathTime, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[119] = VTABLE_TRAMPOLINE(GetFSConstructedFlag, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[120] = VTABLE_TRAMPOLINE(EnableWhitelistFileTracking, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[121] = VTABLE_TRAMPOLINE(sub_18000A750, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[122] = VTABLE_TRAMPOLINE(sub_180002B20, g_CVFileSystemInterface);
	// Note: index 123 is skipped in original
	g_r1oCVFileSystemInterface[124] = VTABLE_TRAMPOLINE(sub_18001DC30, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[125] = VTABLE_TRAMPOLINE(sub_180002B30, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[126] = VTABLE_TRAMPOLINE(sub_180002BA0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[127] = VTABLE_TRAMPOLINE(sub_180002BB0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[128] = VTABLE_TRAMPOLINE(sub_180002BC0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[129] = VTABLE_TRAMPOLINE(sub_180002290, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[130] = VTABLE_TRAMPOLINE(sub_18001CCD0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[131] = VTABLE_TRAMPOLINE(sub_18001CCE0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[132] = VTABLE_TRAMPOLINE(sub_18001CCF0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[133] = VTABLE_TRAMPOLINE(sub_18001CD00, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[134] = VTABLE_TRAMPOLINE(sub_180014520, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[135] = VTABLE_TRAMPOLINE(sub_180002650, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[136] = VTABLE_TRAMPOLINE(sub_18001CD10, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[137] = VTABLE_TRAMPOLINE(sub_180016250, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[138] = VTABLE_TRAMPOLINE(sub_18000F0D0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[139] = VTABLE_TRAMPOLINE(sub_1800139F0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[140] = VTABLE_TRAMPOLINE(sub_180016570, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[141] = VTABLE_TRAMPOLINE(nullsub_86, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[142] = VTABLE_TRAMPOLINE(sub_18000AEC0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[143] = VTABLE_TRAMPOLINE(sub_180003320, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[144] = VTABLE_TRAMPOLINE(sub_18000AF50, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[145] = VTABLE_TRAMPOLINE(sub_18000AF60, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[146] = VTABLE_TRAMPOLINE(sub_180005D00, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[147] = VTABLE_TRAMPOLINE(sub_18000AF70, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[148] = VTABLE_TRAMPOLINE(sub_18001B130, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[149] = VTABLE_TRAMPOLINE(sub_18000AF80, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[150] = VTABLE_TRAMPOLINE(sub_1800034D0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[151] = VTABLE_TRAMPOLINE(sub_180017180, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[152] = VTABLE_TRAMPOLINE(sub_180003550, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[153] = VTABLE_TRAMPOLINE(sub_1800250D0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[154] = VTABLE_TRAMPOLINE(sub_1800241B0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[155] = VTABLE_TRAMPOLINE(sub_1800241C0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[156] = VTABLE_TRAMPOLINE(sub_1800241F0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[157] = VTABLE_TRAMPOLINE(sub_180024240, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[158] = VTABLE_TRAMPOLINE(sub_180024250, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[159] = VTABLE_TRAMPOLINE(sub_180024260, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[160] = VTABLE_TRAMPOLINE(sub_180024300, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[161] = VTABLE_TRAMPOLINE(sub_180024310, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[162] = VTABLE_TRAMPOLINE(sub_180024320, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[163] = VTABLE_TRAMPOLINE(sub_180024340, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[164] = VTABLE_TRAMPOLINE(sub_180024350, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[165] = VTABLE_TRAMPOLINE(sub_180024360, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[166] = VTABLE_TRAMPOLINE(sub_180024390, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[167] = VTABLE_TRAMPOLINE(sub_180024370, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[168] = VTABLE_TRAMPOLINE(sub_1800243C0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[169] = VTABLE_TRAMPOLINE(sub_1800243F0, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[170] = VTABLE_TRAMPOLINE(sub_180024410, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[171] = VTABLE_TRAMPOLINE(sub_180024430, g_CVFileSystemInterface);
	g_r1oCVFileSystemInterface[172] = VTABLE_TRAMPOLINE(CFileSystem_Stdio__NullSub4, r1vtable);
}

//=============================================================================
// CBaseFileSystem VTable Mapping
//=============================================================================

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
	g_r1oCBaseFileSystemInterface[0] = VTABLE_TRAMPOLINE(Read, g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[1] = VTABLE_TRAMPOLINE(Write, g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[2] = VTABLE_TRAMPOLINE(Open, g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[3] = VTABLE_TRAMPOLINE(Close, g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[4] = VTABLE_TRAMPOLINE(Seek, g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[5] = VTABLE_TRAMPOLINE(Tell, g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[6] = VTABLE_TRAMPOLINE(Size, g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[7] = VTABLE_TRAMPOLINE(Size2, g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[8] = VTABLE_TRAMPOLINE(Flush, g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[9] = VTABLE_TRAMPOLINE(Precache, g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[10] = VTABLE_TRAMPOLINE(FileExists, g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[11] = VTABLE_TRAMPOLINE(IsFileWritable, g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[12] = VTABLE_TRAMPOLINE(SetFileWritable, g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[13] = VTABLE_TRAMPOLINE(GetFileTime, g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[14] = VTABLE_TRAMPOLINE(ReadFile, g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[15] = VTABLE_TRAMPOLINE(WriteFile, g_CBaseFileSystemInterface);
	g_r1oCBaseFileSystemInterface[16] = VTABLE_TRAMPOLINE(UnzipFile, g_CBaseFileSystemInterface);
}
