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

const int AINET_VERSION_NUMBER = 54;
const int AINET_SCRIPT_VERSION_NUMBER = 21;
const int PLACEHOLDER_CRC = 0;
const int MAX_HULLS = 5;

#pragma pack(push, 1)
struct CAI_NodeLink
{
	short srcId;
	short destId;
	bool hulls[MAX_HULLS];
	char unk0;
	char unk1; // maps => unk0 on disk
	char unk2[5];
	int64_t flags;
};
#pragma pack(pop)
struct Vector3f
{
	float x;
	float y;
	float z;
};



#pragma pack(push, 1)
struct CAI_Node
{
	int index; // not present on disk
	Vector3f position;
	float hulls[5];
	float yaw;

	int unk0; // always 2 in buildainfile, maps directly to unk0 in disk struct
	int flags; // maps directly to unk1 in disk struct
	int unk2[5]; // maps directly to unk2 in disk struct, despite being ints rather than shorts

	// view server.dll+393672 for context and death wish
	char unk3[5]; // hell on earth, should map to unk3 on disk
	char pad[3]; // aligns next bytes
	float unk4[5]; // i have no fucking clue, calculated using some kind of demon hell function float magic

	CAI_NodeLink** links;
	char unk5[16];
	int linkcount;
	int unk11; // bad name lmao
	short unk6; // should match up to unk4 on disk
	char unk7[16]; // padding until next bit
	short unk8; // should match up to unk5 on disk
	char unk9[8]; // padding until next bit
	char unk10[8]; // should match up to unk6 on disk
};
#pragma pack(pop)

// the way CAI_Nodes are represented in on-disk ain files
#pragma pack(push, 1)
struct CAI_NodeDisk
{
	float x;
	float y;
	float z;
	float yaw;
	float hulls[5];

	char unk0;
	int unk1;
	short unk2[5];
	char unk3[5];
	short unk4;
	short unk5;
	char unk6[8];
}; // total size of 68 bytes
#pragma pack(pop)


#pragma pack(push, 1)
struct CAI_NodeLinkDisk
{
	short srcId;
	short destId;
	char unk0;
	bool hulls[5];
};
#pragma pack(pop)
#pragma pack(push, 1)
struct CAI_Network
{
	char unk0[8];
	int linkcount;
	char unk1[84];
	int zonecount;
	char padidkanymoreman[16];
	int unk5;
	int nodecount;
	int unk5_fake;
	CAI_Node** nodes;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct UnkNodeStruct0
{
	int index;
	char unk0;
	char unk1; // maps to unk1 on disk
	char pad0[2]; // padding to +8

	float x;
	float y;
	float z;

	char pad5[4];
	int* unk2; // maps to unk5 on disk;
	char pad1[16]; // pad to +48
	int unkcount0; // maps to unkcount0 on disk

	char pad2[4]; // pad to +56
	int* unk3;
	char pad3[16]; // pad to +80
	int unkcount1;

	char pad4[132];
	char unk5;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct UnkLinkStruct1
{
	short unk0;
	short unk1;
	int unk2;
	char unk3;
	char unk4;
	char unk5;
};
#pragma pack(pop)

typedef void (*CAI_NetworkManager__DelayedInitType)(__int64 a1);
extern CAI_NetworkManager__DelayedInitType CAI_NetworkManager__DelayedInitOriginal;
typedef void (*CAI_NetworkManager__FixupHintsType)();
extern CAI_NetworkManager__FixupHintsType CAI_NetworkManager__FixupHintsOriginal;
typedef unsigned __int8 (*sub_363A50Type)(__int64 a1, int a2, int a3, int a4);
extern sub_363A50Type sub_363A50Original;
unsigned __int8 __fastcall sub_363A50(__int64 a1, int a2, int a3, int a4);
void __fastcall CAI_NetworkManager__DelayedInit(__int64 a1);
void __fastcall CAI_NetworkManager__FixupHints();
void sub_364140(int node1, int node2, const char* pszFormat, ...);
typedef void (*sub_36BC30Type)(__int64* a1);
typedef __int64 (*sub_36C150Type)(__int64 a1, int a2, int a3);
extern sub_36BC30Type sub_36BC30Original;
extern sub_36C150Type sub_36C150Original;
void sub_36BC30(__int64* a1);
__int64 sub_36C150(__int64 a1, int a2, int a3);
typedef void (*unkallocfunctype)(__int64 a1, int a2, char a3);
extern unkallocfunctype unkallocfuncoriginal;
void unkallocfunc(__int64 a1, int a2, char a3);