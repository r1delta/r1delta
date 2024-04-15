#include "load.h"
#include <cstdlib>
#include <crtdbg.h>	
#include <new>
#include "windows.h"
#include "tier0_orig.h"
#include <iostream>
#include "cvar.h"
#include <winternl.h>  // For UNICODE_STRING.
#include <fstream>
#include <filesystem>
#include <array>
#include <intrin.h>
#include "memory.h"
#include "filesystem.h"
#include "defs.h"
#include "factory.h"
#include "core.h"
#include "load.h"
#include "patcher.h"
#include "MinHook.h"
#include "TableDestroyer.h"
#include "bitbuf.h"
#include "in6addr.h"
#include "thirdparty/silver-bun/module.h"
#include <fcntl.h>
#include <io.h>
#include <streambuf>
#include "navmesh.h"
#pragma intrinsic(_ReturnAddress)
CAI_NetworkManager__DelayedInitType CAI_NetworkManager__DelayedInitOriginal;
CAI_NetworkManager__FixupHintsType CAI_NetworkManager__FixupHintsOriginal;

const int AINET_VERSION_NUMBER = 52;
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

#pragma pack(push, 1)
struct CAI_NodeLinkDisk
{
	short srcId;
	short destId;
	char unk0;
	bool hulls[MAX_HULLS];
};
#pragma pack(pop)

#pragma pack(push, 1)
struct CAI_Node
{
	int index; // not present on disk
	float x;
	float y;
	float z;
	float hulls[MAX_HULLS];
	float yaw;

	int unk0; // always 2 in buildainfile, maps directly to unk0 in disk struct
	int unk1; // maps directly to unk1 in disk struct
	int unk2[MAX_HULLS]; // maps directly to unk2 in disk struct, despite being ints rather than shorts

	// view server.dll+393672 for context and death wish
	char unk3[MAX_HULLS]; // hell on earth, should map to unk3 on disk
	char pad[3]; // aligns next bytes
	float unk4[MAX_HULLS]; // i have no fucking clue, calculated using some kind of demon hell function float magic

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
	float hulls[MAX_HULLS];

	char unk0;
	int unk1;
	short unk2[MAX_HULLS];
	char unk3[MAX_HULLS];
	short unk4;
	short unk5;
	char unk6[8];
}; // total size of 68 bytes
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

int* pUnkStruct0Count;
UnkNodeStruct0*** pppUnkNodeStruct0s;

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

int* pUnkLinkStruct1Count;
UnkLinkStruct1*** pppUnkStruct1s;

#pragma pack(push, 1)
struct CAI_ScriptNode
{
	float x;
	float y;
	float z;
	uint64_t scriptdata;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct CAI_Network
{
	// +0
	char unk0[8];
	// +8
	int linkcount; // this is uninitialised and never set on ain build, fun!
	// +12
	char unk1[124];
	// +136
	int zonecount;
	// +140
	char unk2[16];
	// +156
	int unk5; // unk8 on disk
	// +160
	char unk6[4];
	// +164
	int hintcount;
	// +168
	short hints[2000]; // these probably aren't actually hints, but there's 1 of them per hint so idk
	// +4168
	//int scriptnodecount;
	//// +4172
	//CAI_ScriptNode scriptnodes[4000];
	// +84172
	int nodecount;
	// +84176
	CAI_Node** nodes;
};
#pragma pack(pop)

CGlobalVarsServer2015* g_pGlobals;
void DumpAINInfo(CAI_Network* aiNetwork)
{
	std::filesystem::path writePath("r1delta/maps/graphs");
	writePath /= (char*)g_pGlobals->mapname_pszValue;
	writePath += ".ain";

	// dump from memory
	//spdlog::info("writing ain file {}", writePath.string());
	//spdlog::info("");
	//spdlog::info("");
	//spdlog::info("");
	//spdlog::info("");
	//spdlog::info("");

	std::ofstream writeStream(writePath, std::ofstream::binary);
	//spdlog::info("writing ainet version: {}", AINET_VERSION_NUMBER);
	writeStream.write((char*)&AINET_VERSION_NUMBER, sizeof(int));

	int mapVersion = g_pGlobals->mapversion;
	//spdlog::info("writing map version: {}", mapVersion);
	writeStream.write((char*)&mapVersion, sizeof(int));
	//spdlog::info("writing placeholder crc: {}", PLACEHOLDER_CRC);
	writeStream.write((char*)&PLACEHOLDER_CRC, sizeof(int));

	int calculatedLinkcount = 0;

	// path nodes
	//spdlog::info("writing nodecount: {}", aiNetwork->nodecount);
	writeStream.write((char*)&aiNetwork->nodecount, sizeof(int));

	for (int i = 0; i < aiNetwork->nodecount; i++)
	{
		// construct on-disk node struct
		CAI_NodeDisk diskNode;
		diskNode.x = aiNetwork->nodes[i]->x;
		diskNode.y = aiNetwork->nodes[i]->y;
		diskNode.z = aiNetwork->nodes[i]->z;
		diskNode.yaw = aiNetwork->nodes[i]->yaw;
		memcpy(diskNode.hulls, aiNetwork->nodes[i]->hulls, sizeof(diskNode.hulls));
		diskNode.unk0 = (char)aiNetwork->nodes[i]->unk0;
		diskNode.unk1 = aiNetwork->nodes[i]->unk1;

		for (int j = 0; j < MAX_HULLS; j++)
		{
			diskNode.unk2[j] = (short)aiNetwork->nodes[i]->unk2[j];
			//spdlog::info((short)aiNetwork->nodes[i]->unk2[j]);
		}

		memcpy(diskNode.unk3, aiNetwork->nodes[i]->unk3, sizeof(diskNode.unk3));
		diskNode.unk4 = aiNetwork->nodes[i]->unk6;
		diskNode.unk5 =
			-1; // aiNetwork->nodes[i]->unk8; // this field is wrong, however, it's always -1 in vanilla navmeshes anyway, so no biggie
		memcpy(diskNode.unk6, aiNetwork->nodes[i]->unk10, sizeof(diskNode.unk6));

		//spdlog::info("writing node {} from {} to {:x}", aiNetwork->nodes[i]->index, (void*)aiNetwork->nodes[i], writeStream.tellp());
		writeStream.write((char*)&diskNode, sizeof(CAI_NodeDisk));

		calculatedLinkcount += aiNetwork->nodes[i]->linkcount;
	}

	// links
	//spdlog::info("linkcount: {}", aiNetwork->linkcount);
	//spdlog::info("calculated total linkcount: {}", calculatedLinkcount);

	calculatedLinkcount /= 2;
	//if (Cvar_ns_ai_dumpAINfileFromLoad->GetBool())
	//{
	//	if (aiNetwork->linkcount == calculatedLinkcount)
	//		spdlog::info("caculated linkcount is normal!");
	//	else
	//		spdlog::warn("calculated linkcount has weird value! this is expected on build!");
	//}
	//
	//spdlog::info("writing linkcount: {}", calculatedLinkcount);
	writeStream.write((char*)&calculatedLinkcount, sizeof(int));

	for (int i = 0; i < aiNetwork->nodecount; i++)
	{
		for (int j = 0; j < aiNetwork->nodes[i]->linkcount; j++)
		{
			// skip links that don't originate from current node
			if (aiNetwork->nodes[i]->links[j]->srcId != aiNetwork->nodes[i]->index)
				continue;

			CAI_NodeLinkDisk diskLink;
			diskLink.srcId = aiNetwork->nodes[i]->links[j]->srcId;
			diskLink.destId = aiNetwork->nodes[i]->links[j]->destId;
			diskLink.unk0 = aiNetwork->nodes[i]->links[j]->unk1;
			memcpy(diskLink.hulls, aiNetwork->nodes[i]->links[j]->hulls, sizeof(diskLink.hulls));

			//spdlog::info("writing link {} => {} to {:x}", diskLink.srcId, diskLink.destId, writeStream.tellp());
			writeStream.write((char*)&diskLink, sizeof(CAI_NodeLinkDisk));
		}
	}

	// don't know what this is, it's likely a block from tf1 that got deprecated? should just be 1 int per node
	//spdlog::info("writing {:x} bytes for unknown block at {:x}", aiNetwork->nodecount * sizeof(uint32_t), writeStream.tellp());
	uint32_t* unkNodeBlock = new uint32_t[aiNetwork->nodecount];
	memset(unkNodeBlock, 0, aiNetwork->nodecount * sizeof(uint32_t));
	writeStream.write((char*)unkNodeBlock, aiNetwork->nodecount * sizeof(uint32_t));
	delete[] unkNodeBlock;

	// TODO: this is traverse nodes i think? these aren't used in tf2 ains so we can get away with just writing count=0 and skipping
	// but ideally should actually dump these
	//spdlog::info("writing {} traversal nodes at {:x}...", 0, writeStream.tellp());
	short traverseNodeCount = 0;
	writeStream.write((char*)&traverseNodeCount, sizeof(short));
	// only write count since count=0 means we don't have to actually do anything here

	// TODO: ideally these should be actually dumped, but they're always 0 in tf2 from what i can tell
	//spdlog::info("writing {} bytes for unknown hull block at {:x}", MAX_HULLS * 8, writeStream.tellp());
	char* unkHullBlock = new char[MAX_HULLS * 8];
	memset(unkHullBlock, 0, MAX_HULLS * 8);
	writeStream.write(unkHullBlock, MAX_HULLS * 8);
	delete[] unkHullBlock;

	// unknown struct that's seemingly node-related
	//s//pdlog::info("writing {} unknown node structs at {:x}", *pUnkStruct0Count, writeStream.tellp());
	writeStream.write((char*)pUnkStruct0Count, sizeof(*pUnkStruct0Count));
	for (int i = 0; i < *pUnkStruct0Count; i++)
	{
		//spdlog::info("writing unknown node struct {} at {:x}", i, writeStream.tellp());
		UnkNodeStruct0* nodeStruct = (*pppUnkNodeStruct0s)[i];

		writeStream.write((char*)&nodeStruct->index, sizeof(nodeStruct->index));
		writeStream.write((char*)&nodeStruct->unk1, sizeof(nodeStruct->unk1));

		writeStream.write((char*)&nodeStruct->x, sizeof(nodeStruct->x));
		writeStream.write((char*)&nodeStruct->y, sizeof(nodeStruct->y));
		writeStream.write((char*)&nodeStruct->z, sizeof(nodeStruct->z));

		writeStream.write((char*)&nodeStruct->unkcount0, sizeof(nodeStruct->unkcount0));
		for (int j = 0; j < nodeStruct->unkcount0; j++)
		{
			short unk2Short = (short)nodeStruct->unk2[j];
			writeStream.write((char*)&unk2Short, sizeof(unk2Short));
		}

		writeStream.write((char*)&nodeStruct->unkcount1, sizeof(nodeStruct->unkcount1));
		for (int j = 0; j < nodeStruct->unkcount1; j++)
		{
			short unk3Short = (short)nodeStruct->unk3[j];
			writeStream.write((char*)&unk3Short, sizeof(unk3Short));
		}

		writeStream.write((char*)&nodeStruct->unk5, sizeof(nodeStruct->unk5));
	}

	// unknown struct that's seemingly link-related
	//spdlog::info("writing {} unknown link structs at {:x}", *pUnkLinkStruct1Count, writeStream.tellp());
	writeStream.write((char*)pUnkLinkStruct1Count, sizeof(*pUnkLinkStruct1Count));
	for (int i = 0; i < *pUnkLinkStruct1Count; i++)
	{
		// disk and memory structs are literally identical here so just directly write
		//spdlog::info("writing unknown link struct {} at {:x}", i, writeStream.tellp());
		writeStream.write((char*)(*pppUnkStruct1s)[i], sizeof(*(*pppUnkStruct1s)[i]));
	}

	// some weird int idk what this is used for
	writeStream.write((char*)&aiNetwork->unk5, sizeof(aiNetwork->unk5));

	// tf2-exclusive stuff past this point, i.e. ain v57 only
	//spdlog::info("writing {} script nodes at {:x}", aiNetwork->scriptnodecount, writeStream.tellp());
	//writeStream.write((char*)&aiNetwork->scriptnodecount, sizeof(aiNetwork->scriptnodecount));
	//for (int i = 0; i < aiNetwork->scriptnodecount; i++)
	//{
	//	// disk and memory structs are literally identical here so just directly write
	//	spdlog::info("writing script node {} at {:x}", i, writeStream.tellp());
	//	writeStream.write((char*)&aiNetwork->scriptnodes[i], sizeof(aiNetwork->scriptnodes[i]));
	//}
	//
	//spdlog::info("writing {} hints at {:x}", aiNetwork->hintcount, writeStream.tellp());
	//writeStream.write((char*)&aiNetwork->hintcount, sizeof(aiNetwork->hintcount));
	//for (int i = 0; i < aiNetwork->hintcount; i++)
	//{
	//	spdlog::info("writing hint data {} at {:x}", i, writeStream.tellp());
	//	writeStream.write((char*)&aiNetwork->hints[i], sizeof(aiNetwork->hints[i]));
	//}

	writeStream.close();
}
typedef void (*CAI_NetworkBuilder__InitZonesType)();
static uintptr_t rettorebuild;
static uintptr_t rettofree;
static uintptr_t rettofree2;
static uintptr_t rettoalloc;
static uintptr_t rettoallocbullshit;
static __int64 arrayptr1;
static __int64 arrayptr2;
static CAI_NetworkBuilder__InitZonesType CAI_NetworkBuilder__InitZones;

void CAI_NetworkManager__FixupHints() {
	if (uintptr_t(_ReturnAddress()) == rettorebuild) {
		CAI_NetworkBuilder__InitZones();
	}

	return CAI_NetworkManager__FixupHintsOriginal();
}
void allocbuffer(uintptr_t ptr) {
	auto nonexistentbuffer = (void**)(uintptr_t(GetModuleHandleA("server.dll")) + ptr);
	if (*nonexistentbuffer)
		return;
	*nonexistentbuffer = CreateGlobalMemAlloc()->Alloc(65536 * 8);
	memset(*nonexistentbuffer, 0, 65536 * 8);
}
void sub_364140(int node1, int node2, const char* pszFormat, ...)
{
	va_list args;
	va_start(args, pszFormat);

	// Print the formatted string using vprintf
	vprintf(pszFormat, args);

	va_end(args);
}
typedef void (*sub_36BC30Type)(__int64* a1);
typedef __int64 (*sub_36C150Type)(__int64 a1, int a2, int a3);
sub_36BC30Type sub_36BC30Original;
sub_36C150Type sub_36C150Original;
void sub_36BC30(__int64* a1) // free
{
	uintptr_t retaddr = uintptr_t(_ReturnAddress());
	if (retaddr == rettofree || retaddr == rettofree2) {
		sub_36BC30Original((__int64*)arrayptr1);
		sub_36BC30Original((__int64*)arrayptr2);
	}
	return sub_36BC30Original(a1);
}
__int64 sub_36C150(__int64 a1, int a2, int a3) // CUtlVector<CVarBitVec,CUtlMemory<CVarBitVec,int>>::InsertMultipleBefore, not sure if this is needed?
{
	uintptr_t retaddr = uintptr_t(_ReturnAddress());
	if (retaddr == rettoalloc) {
		sub_36C150Original(arrayptr1, a2, a3);
		sub_36C150Original(arrayptr2, a2, a3);
	}
	return sub_36C150Original(a1, a2, a3);
}
static __int64 lastnum = 0;
typedef void (*unkallocfunctype)(__int64 a1, int a2, char a3);
unkallocfunctype unkallocfuncoriginal;
void unkallocfunc(__int64 a1, int a2, char a3) // CVarBitVecBase<unsigned short>::Resize
{
	uintptr_t retaddr = uintptr_t(_ReturnAddress());
	if (retaddr == rettoallocbullshit) {
		if (!lastnum)
			lastnum = a1;
		if (a1 < lastnum) {
			lastnum = a1;
		}
		unkallocfuncoriginal((*(__int64*)(arrayptr1)) + (a1 - lastnum), a2, a3);
		unkallocfuncoriginal((*(__int64*)(arrayptr2)) + (a1 - lastnum), a2, a3);
	}
	return unkallocfuncoriginal(a1, a2, a3);
}
void __fastcall CAI_NetworkManager__DelayedInit(__int64 a1) {
	static bool* CAI_NetworkManager__gm_fNetworksLoaded = (bool*)(uintptr_t(GetModuleHandleA("server.dll")) + 0xC318A0);
	static auto CAI_NetworkBuilder__Rebuild = (void(*)(__int64 a1, CAI_Network* a2))(uintptr_t(GetModuleHandleA("server.dll")) + 0x3682A0);

	static bool allocated = false;
	if (!allocated) {
		allocated = true;
		allocbuffer(0xD41730);
		allocbuffer(0xD41710);
		allocbuffer(0xD416F0);
		allocbuffer(0xD416D0);
		allocbuffer(0xD41AD0);
		rettorebuild = uintptr_t(GetModuleHandleA("server.dll")) + 0x368594;
		rettofree = uintptr_t(GetModuleHandleA("server.dll")) + 0x368469;
		rettofree2 = uintptr_t(GetModuleHandleA("server.dll")) + 0x3685A0;
		rettoalloc = uintptr_t(GetModuleHandleA("server.dll")) + 0x36847E;
		rettoallocbullshit = uintptr_t(GetModuleHandleA("server.dll")) + 0x3684A4;
		CAI_NetworkBuilder__InitZones = CAI_NetworkBuilder__InitZonesType(uintptr_t(GetModuleHandleA("server.dll")) + 0x0367EE0);
		arrayptr1 = (__int64)(uintptr_t(GetModuleHandleA("server.dll")) + 0xD416F0);
		arrayptr2 = (__int64)(uintptr_t(GetModuleHandleA("server.dll")) + 0xD41710);
	}
	auto network = reinterpret_cast<CAI_Network * >(((_QWORD*)(a1))[200]);
	g_pGlobals = (CGlobalVarsServer2015*)(uintptr_t(GetModuleHandleA("server.dll")) + 0xC310C0);
	CAI_NetworkBuilder__Rebuild(a1, network);
	//CAI_NetworkBuilder__InitZones();
	//DumpAINInfo(network);
	return CAI_NetworkManager__DelayedInitOriginal(a1);
}