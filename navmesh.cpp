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

#include "load.h"
#include <cstdlib>
#include <crtdbg.h>	
#include <new>
#include "windows.h"

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
#include <fcntl.h>
#include <io.h>
#include <streambuf>
#include "navmesh.h"
#include "logging.h"
#include <bitvec.h>
#include "vsdk/public/tier1/utlvector.h"
#pragma intrinsic(_ReturnAddress)
CAI_NetworkManager__DelayedInitType CAI_NetworkManager__DelayedInitOriginal;
CAI_NetworkManager__FixupHintsType CAI_NetworkManager__FixupHintsOriginal;

int* pUnkLinkStruct1Count;
UnkLinkStruct1*** pppUnkStruct1s;
int* pUnkStruct0Count;
UnkNodeStruct0*** pppUnkNodeStruct0s;

bool __fastcall sub_35FBB0(CAI_NodeLink* a1, char a2)
{
	return (a1->unk1 & (unsigned __int8)(1 << a2)) != 0;
}
char __fastcall sub_38D3C0(CAI_Node** a2, unsigned int* a3)
{
	char result; // al
	signed int v5; // esi
	__int64 v6; // rbp
	CAI_NodeLink* v7; // rbx
	__int16 destId; // cx
	CAI_Node* v9; // r8

	a3[11] |= 0x4000000u;
	v5 = 0;
	if ((int)a3[30] > 0)
	{
		v6 = 0i64;
		do
		{
			v7 = *(CAI_NodeLink**)(*((_QWORD*)a3 + 12) + v6);
			result = sub_35FBB0(v7, 0);
			if (!result && (v7->hulls[0] & 0xBF) != 0)
			{
				destId = v7->srcId;
				if (*a3 == destId)
					destId = v7->destId;
				result = destId;
				v9 = a2[destId];
				if ((v9->flags & 0x4000000) == 0)
					result = sub_38D3C0(a2, (unsigned int*)v9);
			}
			++v5;
			v6 += 8i64;
		} while (v5 < (int)a3[30]);
	}
	return result;
}


void __fastcall sub_394F90(CAI_Network* a2) // InitTraverseNode
{
	__int64 nodecount; // r13
	CAI_Node** nodes; // r15
	__int64 i; // rbp
	CAI_Node* v6; // rdi
	unsigned int unk1; // eax
	signed int v8; // esi
	__int64 v9; // r14
	CAI_NodeLink* v10; // rbx
	__int16 destId; // cx
	unsigned int* v12; // r8

	nodecount = a2->nodecount;
	nodes = a2->nodes;
	for (i = 0i64; i < nodecount; ++i)
	{
		v6 = nodes[i];
		unk1 = v6->flags;
		if ((unk1 & 0x2000000) != 0 && (unk1 & 0x4000000) == 0)
		{
			v6->flags |= 0x4000000u;
			v8 = 0;
			if (v6->linkcount > 0)
			{
				v9 = 0i64;
				do
				{
					v10 = v6->links[v9];
					if (!sub_35FBB0(v10, 0) && (v10->hulls[0] & 0xBF) != 0)
					{
						*(_DWORD*)&destId = v10->srcId;
						if (v6->index == *(_DWORD*)&destId)
							destId = v10->destId;
						v12 = (unsigned int*)nodes[destId];
						if ((v12[11] & 0x4000000) == 0)
							sub_38D3C0(nodes, v12);
					}
					++v8;
					++v9;
				} while (v8 < v6->linkcount);
			}
		}
	}
}
float __fastcall sub_3997D0(CAI_Node* a1, int a2, unsigned __int8 a3)
{
	unsigned __int8 v3; // al
	float v4; // xmm4_4
	float result; // xmm0_4

	v3 = a3;
	if (a3 >= 0xAu)
		v3 = 10;

	a1->unk3[a2] = v3;

	v4 = (float)(1.0 - (float)((float)v3 * 0.1)) + (float)(1.0 - (float)((float)v3 * 0.1));
	result = (float)((float)((float)(v4 * v4) + 1.0) * v4) * 0.2;

	a1->unk4[a2] = result;
	return result;
}
float* __fastcall sub_35E370(int a1)
{
	void* unk_B4BE78 = (void*)(G_server + 0xB41BBC);
	return (float*)&unk_B4BE78 + 64 * (__int64)a1;
}
float* __fastcall sub_35E390(int a1)
{
	void* unk_B4BE6C = (void*)(G_server + 0xB41BB0);
	return (float*)&unk_B4BE6C + 64 * (__int64)a1;
}
float __fastcall sub_35E240(int a1)
{
	void* off_B4BE60 = (void*)(G_server + 0xB41BA8);
	return *(float*)&(&off_B4BE60)[8 * (__int64)a1 + 4] - *((float*)&off_B4BE60 + 16 * (__int64)a1 + 5);
}

int sub_390AE0(CAI_Network* network)
{
	static const float dword_1061618 = sqrtf(1.1754944e-38);
	auto UTIL_TraceLine = reinterpret_cast<void** (*)(Vector3f* a1, Vector3f* a2, __int64 a3, __int64 a4, int a5, char* a6)>(G_server + 0x263AF0);

	CAI_Node** nodes = network->nodes;
	int nodecount = network->nodecount;

	Vector3f directions[6] = {
		{1.0f, 0.0f, 0.0f},    // stru_91C4D0
		{1.0f, 0.0f, 0.0f},    // stru_91C4D0
		{0.0f, 0.0f, -1.0f},   // stru_91C4B0
		{-1.0f, 0.0f, 0.0f},   // stru_91C4E0
		{-1.0f, 0.0f, 0.0f},   // stru_91C4E0
		{0.0f, -1.0f, 0.0f}    // stru_91C4C0
	};

	for (int i = 0; i < 2; i++)
	{
		int nodeIndex = (i == 0) ? 0 : 4;

		float maxSafetyValue = 0.0f;
		unsigned int maxSafetyNode = -1;
		float cnegative16 = -16;
		float c16 = 16;
		float* nodeMin = &cnegative16;
		float* nodeMax = &c16;

		Vector3f offset = {
			(nodeMax[0] - nodeMin[0]) * 0.5f,
			(nodeMax[1] - nodeMin[1]) * 0.5f,
			(nodeMax[2] - nodeMin[2]) * 0.5f
		};

		float nodeValue = -16.f;

		for (int j = 0; j < nodecount; j++)
		{
			CAI_Node* node = nodes[j];

			float nodeSafetyValue = 0.0f;

			for (int k = 0; k < 16; k++)
			{
				float scale = (k < 8) ? nodeValue : (nodeValue * 0.5f);
				int index = (k < 8) ? k : (k - 8);

				Vector3f direction = {
					directions[index].x,
					directions[index].y,
					directions[index].z
				};

				Vector3f start = {
					node->position.x + (scale * 0.0f) + (direction.x * offset.x),
					node->position.y + (scale * 0.0f) + (direction.y * offset.y),
					node->position.z + scale + (direction.z * offset.z)
				};

				float directionLength = sqrtf((direction.x * direction.x) + (direction.y * direction.y) + (direction.z * direction.z));
				float inverseLength = 1.0f / fmaxf(directionLength, 1e-45f);

				Vector3f end = {
					start.x + ((direction.x * inverseLength) * 1300.0f),
					start.y + ((direction.y * inverseLength) * 1300.0f),
					start.z + ((direction.z * inverseLength) * 1300.0f)
				};

				char traceResult[4096] = { 0 };
				UTIL_TraceLine(&start, &end, 71319601, 0, 0, traceResult);

				float fraction = fminf(fmaxf(1.0f - ((float*)traceResult)[0], 0.0f), 1.0f);
				nodeSafetyValue += ((fraction * fraction) * fraction) * 0.77499998f;
			}

			Vector3f start = {
				node->position.x + offset.x,
				node->position.y + offset.y,
				node->position.z + nodeValue
			};

			Vector3f end = {
				start.x,
				start.y,
				start.z + 260.0f
			};

			char traceResult[4096] = { 0 };
			UTIL_TraceLine(&start, &end, 71319601, 0, 0, traceResult);

			float fraction = fminf(fmaxf(1.0f - ((float*)traceResult)[0], 0.0f), 1.0f);
			nodeSafetyValue += (fraction * fraction) * 1.5f;

			if ((node->flags & 0x80000) != 0)
				nodeSafetyValue *= 1.2f;

			float adjustedSafetyValue = floorf(nodeSafetyValue + 0.5f);
			sub_3997D0(node, nodeIndex, adjustedSafetyValue);

			if (nodeSafetyValue > maxSafetyValue)
			{
				maxSafetyValue = nodeSafetyValue;
				maxSafetyNode = j;
			}
		}

		//char* nodeName = sub_35E3B0(nodeIndex);
		//sub_10AF20("%s - max RawSafetyValue %.2f for node %d\n", nodeName, maxSafetyValue, maxSafetyNode);
	}

	return 0;
}

void DumpAINInfo(CAI_Network* aiNetwork)
{
	std::filesystem::path writePath("r1delta/maps/graphs");
	writePath /= (char*)(pGlobalVarsServer)->mapname_pszValue;
	writePath += ".ain";

	// dump from memory
	//spdlog::info("writing ain file {}", writePath.string());
	//spdlog::info("");
	//spdlog::info("");
	//spdlog::info("");
	//spdlog::info("");
	//spdlog::info("");
	sub_390AE0(aiNetwork);
	std::ofstream writeStream(writePath, std::ofstream::binary);
	//spdlog::info("writing ainet version: {}", AINET_VERSION_NUMBER);
	writeStream.write((char*)&AINET_VERSION_NUMBER, sizeof(int));

	int mapVersion = (pGlobalVarsServer)->mapversion;
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
		diskNode.x = aiNetwork->nodes[i]->position.x;
		diskNode.y = aiNetwork->nodes[i]->position.y;
		diskNode.z = aiNetwork->nodes[i]->position.z;
		diskNode.yaw = aiNetwork->nodes[i]->yaw;
		memcpy(diskNode.hulls, aiNetwork->nodes[i]->hulls, sizeof(diskNode.hulls));
		diskNode.unk0 = (char)aiNetwork->nodes[i]->unk0;
		diskNode.unk1 = aiNetwork->nodes[i]->flags;

		for (int j = 0; j < MAX_HULLS; j++)
		{
			diskNode.unk2[j] = (short)aiNetwork->nodes[i]->unk2[j];
			//spdlog::info((short)aiNetwork->nodes[i]->unk2[j]);
		}

		memcpy(diskNode.unk3, aiNetwork->nodes[i]->unk3, sizeof(diskNode.unk3));
		diskNode.unk4 = 1;
		diskNode.unk5 =
			-1; // aiNetwork->nodes[i]->unk8; // this field is wrong, however, it's always -1 in vanilla navmeshes anyway, so no biggie
		memcpy(diskNode.unk6, aiNetwork->nodes[i]->scriptdata, sizeof(diskNode.unk6));

		//spdlog::info("writing node {} from {} to {:x}", aiNetwork->nodes[i]->index, (void*)aiNetwork->nodes[i], writeStream.tellp());
		writeStream.write((char*)&diskNode, sizeof(CAI_NodeDisk));

		calculatedLinkcount += aiNetwork->nodes[i]->linkcount;
	}

	// links
	//spdlog::info("linkcount: {}", aiNetwork->linkcount);
	//spdlog::info("calculated total linkcount: {}", calculatedLinkcount);
	calculatedLinkcount /= 2;
	calculatedLinkcount += 1; // no clue why
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
	////spdlog::info("writing {:x} bytes for unknown block at {:x}", aiNetwork->nodecount * sizeof(uint32_t), writeStream.tellp());
	//uint32_t* unkNodeBlock = new uint32_t[aiNetwork->nodecount];
	//memset(unkNodeBlock, 0, aiNetwork->nodecount * sizeof(uint32_t));
	//writeStream.write((char*)unkNodeBlock, aiNetwork->nodecount * sizeof(uint32_t));
	//delete[] unkNodeBlock;
	//
	//// TODO: this is traverse nodes i think? these aren't used in tf2 ains so we can get away with just writing count=0 and skipping
	//// but ideally should actually dump these
	////spdlog::info("writing {} traversal nodes at {:x}...", 0, writeStream.tellp());
	//short traverseNodeCount = 0;
	//writeStream.write((char*)&traverseNodeCount, sizeof(short));
	//// only write count since count=0 means we don't have to actually do anything here
	//
	//// TODO: ideally these should be actually dumped, but they're always 0 in tf2 from what i can tell
	////spdlog::info("writing {} bytes for unknown hull block at {:x}", MAX_HULLS * 8, writeStream.tellp());
	//char* unkHullBlock = new char[MAX_HULLS * 8];
	//memset(unkHullBlock, 0x00, MAX_HULLS * 8);
	//writeStream.write(unkHullBlock, MAX_HULLS * 8);
	//delete[] unkHullBlock;
	//
	//// unknown struct that's seemingly node-related
	////s//pdlog::info("writing {} unknown node structs at {:x}", *pUnkStruct0Count, writeStream.tellp());
	//// Open the "mp_box.clusterdump" file for reading
	std::ifstream clusterDumpFile("r1delta/mp_nexus.clusterdump", std::ios::binary);
	//
	//// Get the size of the cluster dump file
	clusterDumpFile.seekg(0, std::ios::end);
	std::streampos clusterDumpSize = clusterDumpFile.tellg();
	clusterDumpFile.seekg(0, std::ios::beg);
	//
	// Read the contents of the cluster dump file into a buffer
	std::vector<char> clusterDumpBuffer(clusterDumpSize);
	clusterDumpFile.read(clusterDumpBuffer.data(), clusterDumpSize);
	//
	//// Close the cluster dump file
	clusterDumpFile.close();
	//
	//// Write the contents of the cluster dump buffer to the output stream
	writeStream.write(clusterDumpBuffer.data(), clusterDumpSize);

	writeStream.close();
}
typedef void (*CAI_DynamicLink__InitDynamicLinksType)();
static uintptr_t rettorebuild;
static uintptr_t rettofree;
static uintptr_t rettofree2;
static uintptr_t rettoalloc;
static uintptr_t rettoallocbullshit;
static __int64 arrayptr1;
static __int64 arrayptr2;
static CAI_DynamicLink__InitDynamicLinksType CAI_DynamicLink__InitDynamicLinks;
static CAI_DynamicLink__InitDynamicLinksType CAI_NetworkBuilder__InitZones;
//void __fastcall TraverseExNodes(__int64 a1, CAI_Network* a2)
//{
//	__int64 nodecount; // r13
//	CAI_Node** nodes; // r15
//	__int64 i; // rbp
//	CAI_Node* v6; // rdi
//	unsigned int unk1; // eax
//	signed int v8; // esi
//	__int64 v9; // r14
//	CAI_NodeLink* v10; // rbx
//	int srcId; // ecx
//	unsigned int* v12; // r8
//
//	nodecount = a2->nodecount;
//	nodes = a2->nodes;
//	for (i = 0i64; i < nodecount; ++i)
//	{
//		v6 = nodes[i];
//		unk1 = v6->unk1;
//		if ((unk1 & 0x2000000) != 0 && (unk1 & 0x4000000) == 0)
//		{
//			v6->unk1 |= 0x4000000u;
//			v8 = 0;
//			if (v6->linkcount > 0)
//			{
//				v9 = 0i64;
//				do
//				{
//					v10 = v6->links[v9];
//					if (!sub_35FBB0((__int64)v10, 0) && (v10->hulls[0] & 0xBF) != 0)
//					{
//						srcId = v10->srcId;
//						if (v6->index == srcId)
//							srcId = v10->destId;
//						v12 = (unsigned int*)nodes[(__int16)srcId];
//						if ((v12[11] & 0x4000000) == 0)
//							sub_38D3C0(a1, (__int64)nodes, v12);
//					}
//					++v8;
//					++v9;
//				} while (v8 < v6->linkcount);
//			}
//		}
//	}
//}
CAI_Network* network;

void CAI_NetworkManager__FixupHints() {
	if (uintptr_t(_ReturnAddress()) == rettorebuild) {
		sub_390AE0(network); // init safety tolerances
		CAI_NetworkBuilder__InitZones();
		CAI_DynamicLink__InitDynamicLinks();
		sub_394F90(network);
	}
	CAI_NetworkManager__FixupHintsOriginal();	
}
void allocbuffer(uintptr_t ptr) {
	auto nonexistentbuffer = (void**)(G_server + ptr);
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
		sub_390AE0(network); // init safety tolerances
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
typedef unsigned __int8 (*sub_363A50Type)(__int64 a1, int a2, int a3, int a4);
sub_363A50Type sub_363A50Original;
unsigned __int8 __fastcall sub_363A50(__int64 a1, int a2, int a3, int a4)
{
	__int64 v4; // r10
	__int64 v5; // rax
	__int64 v7; // rdx
	int v8; // r8d
	int v9; // edx
	int v10; // r8d
	int v11; // eax

	v4 = a3;
	v5 = a2;
	if (a2 == a3)
		return 1;
	v7 = *(_QWORD*)(a1 + 128);
	v8 = *(_DWORD*)(*(_QWORD*)(v7 + 8 * v5) + 4i64 * a4 + 48);
	v9 = *(_DWORD*)(*(_QWORD*)(v7 + 8 * v4) + 4i64 * a4 + 48);
	if (v8 == 1 || v9 == 1 || v8 == 2 || v9 == 2)
		return 0;
	if (v8 == 3 || v9 == 3)
		return 1;
	v10 = (v8 - 4) * *(_DWORD*)(a1 + 4i64 * a4 + 96) + v9 - 4;
	if (!v10)
		return 0;
	v11 = *(_DWORD*)(*(_QWORD*)(a1 + 16i64 * a4 + 24) + 4 * ((__int64)v10 >> 5));
	return _bittest((const LONG*)(&v11), v10 & 0x1F);
}
void __fastcall CAI_NetworkManager__DelayedInit(__int64 a1) {
	auto server = G_server;
	bool* CAI_NetworkManager__gm_fNetworksLoaded = (bool*)(server + 0xC318A0);
	auto CAI_NetworkBuilder__Rebuild = (void(*)(__int64 a1, CAI_Network* a2))(server + 0x3682A0);

	static bool allocated = false;
	if (!allocated) {
		allocated = true;
		allocbuffer(0xD41730);
		allocbuffer(0xD41710);
		allocbuffer(0xD416F0);
		allocbuffer(0xD416D0);
		allocbuffer(0xD41AD0);
		rettorebuild = server + 0x368594;
		rettofree = server + 0x368469;
		rettofree2 = server + 0x3685A0;
		rettoalloc = server + 0x36847E;
		rettoallocbullshit = server + 0x3684A4;
		CAI_DynamicLink__InitDynamicLinks = CAI_DynamicLink__InitDynamicLinksType(server + 0x0337E80);
		CAI_NetworkBuilder__InitZones = CAI_DynamicLink__InitDynamicLinksType(server + 0x367EE0);
		
		arrayptr1 = (__int64)(server + 0xD416F0);
		arrayptr2 = (__int64)(server + 0xD41710);

		pUnkStruct0Count = (int*)(server + 0xD41AE8);
		pppUnkNodeStruct0s = (UnkNodeStruct0***)(server + 0xD41AD0);
		pUnkLinkStruct1Count = (int*)(server + 0x0D41AB8);
		pppUnkStruct1s = (UnkLinkStruct1***)(server + 0xD41AA0);

	}
	network = reinterpret_cast<CAI_Network * >(((_QWORD*)(a1))[200]);
	std::filesystem::path writePath("r1delta/maps/graphs");
	writePath /= (char*)(pGlobalVarsServer)->mapname_pszValue;
	writePath += ".ain";
	//if (std::filesystem::exists(writePath)) 
		//return CAI_NetworkManager__DelayedInitOriginal(a1);
	
	//DumpAINInfo(network);
	//uintptr_t engine = G_engine;
	//typedef void (*Cbuf_AddTextType)(int a1, const char* a2, unsigned int a3);
	//Cbuf_AddTextType Cbuf_AddText = (Cbuf_AddTextType)(engine + 0x102D50);
	//Cbuf_AddText(0, "reload\n", 0);
	CAI_NetworkManager__DelayedInitOriginal(a1);
	CAI_NetworkBuilder__Rebuild(a1, network);
}
bool ReadIntAtPosition(std::fstream& file, std::streampos pos, int& value)
{
	file.seekg(pos);
	if (!file.read(reinterpret_cast<char*>(&value), sizeof(int)))
	{
		return false;
	}
	return true;
}

// Function to write scriptdata to a specific position in the file
bool WriteScriptDataAtPosition(std::fstream& file, std::streampos pos, const char* data, size_t size)
{
	file.seekp(pos);
	if (!file.write(data, size))
	{
		return false;
	}
	return true;
}

// The console command implementation
void updatescriptdata_cmd(const CCommand& args)
{
	// Validate global pointers
	if (!pGlobalVarsServer)
	{
		Warning("Error: Global variables not initialized.\n");
		return;
	}
	CAI_Network* pAiNetwork = *reinterpret_cast<CAI_Network**>(G_server + 0xC31888);
	if (!pAiNetwork)
	{
		Warning("Error: AI Network not initialized.\n");
		return;
	}
	if (!pAiNetwork->nodes)
	{
		Warning("Error: AI Nodes not initialized.\n");
		return;
	}

	// Construct the current AIN file path
	std::filesystem::path currentAINPath("r1delta/maps/graphs");
	const char* mapName = static_cast<const char*>((pGlobalVarsServer)->mapname_pszValue);
	if (!mapName)
	{
		Warning("Error: Map name is null.\n");
		return;
	}
	currentAINPath /= mapName;
	currentAINPath += ".ain";

	// Open the AIN file in binary read/write mode
	std::fstream ainFile(currentAINPath, std::ios::in | std::ios::out | std::ios::binary);
	if (!ainFile.is_open())
	{
		Warning("Error: Failed to open AIN file at %s\n", currentAINPath.string().c_str());
		return;
	}

	// Read nodecount from the file at offset 0xC
	int fileNodeCount = 0;
	if (!ReadIntAtPosition(ainFile, 0xC, fileNodeCount))
	{
		Warning("Error: Failed to read node count from AIN file.\n");
		ainFile.close();
		return;
	}

	// Compare with in-memory node count
	if (fileNodeCount != pAiNetwork->nodecount)
	{
		Warning("Warning: Node count in file (%d) does not match in-memory node count (%d). Operation cancelled.\n",
			fileNodeCount, pAiNetwork->nodecount);
		ainFile.close();
		return;
	}

	// Iterate through each node and write scriptdata
	for (int nodeIndex = 0; nodeIndex < pAiNetwork->nodecount; ++nodeIndex)
	{
		CAI_Node* node = pAiNetwork->nodes[nodeIndex];
		if (!node)
		{
			Warning("Warning: Node at index %d is null. Skipping.\n", nodeIndex);
			continue;
		}

		// Calculate the position to write scriptdata
		// scriptdata is at (0x4C + 0x44 * nodeIndex)
		std::streampos scriptDataPos = 0x4C + static_cast<std::streampos>(0x44) * nodeIndex;

		// Write the scriptdata to the file
		if (!WriteScriptDataAtPosition(ainFile, scriptDataPos, node->scriptdata, sizeof(node->scriptdata)))
		{
			Warning("Error: Failed to write scriptdata for node %d.\n", nodeIndex);
			// Depending on requirements, you might want to continue or abort
			// Here, we'll continue
			continue;
		}
	}

	ainFile.close();
	Msg("Successfully updated scriptdata for all AI nodes in %s, reloading map\n", currentAINPath.string().c_str());
	Cbuf_AddText(0, "reload\n", 0);
}
// A manager holding a pointer to CAI_Network, presumably
class CAI_NetworkManager
{
	// ...
};

// Forward declarations to get global pointers (addresses from your notes)
using FnGetAINetworkManager = CAI_NetworkManager * (*)();
void verifyain_cmd(const CCommand& args)
{
	// 1) Get the manager:
	static FnGetAINetworkManager s_GetAINetworkManager =
		reinterpret_cast<FnGetAINetworkManager>(G_server + 0x36B220);

	CAI_NetworkManager* pManager = s_GetAINetworkManager();
	if (!pManager)
	{
		Warning("verifyain: Failed to get AI Network Manager.\n");
		return;
	}

	// 2) Get the CAI_Network pointer from memory:
	CAI_Network* pNetwork = *reinterpret_cast<CAI_Network**>(G_server + 0xC31888);
	if (!pNetwork || !pNetwork->nodes)
	{
		Warning("verifyain: No valid CAI_Network.\n");
		return;
	}

	// 3) Build path to .ain file
	std::filesystem::path ainPath("r1delta/maps/graphs");
	const char* mapName = static_cast<const char*>((pGlobalVarsServer)->mapname_pszValue);
	if (!mapName || !*mapName)
	{
		Warning("verifyain: No current map name.\n");
		return;
	}
	ainPath /= mapName;
	ainPath += ".ain";

	// 4) Open for read (binary)
	std::fstream ainFile(ainPath, std::ios::in | std::ios::binary);
	if (!ainFile.is_open())
	{
		Warning("verifyain: Could not open '%s' for reading.\n", ainPath.string().c_str());
		return;
	}

	// 5) Check that the nodecount in the file matches in memory
	int fileNodeCount = 0;
	ainFile.seekg(0xC, std::ios::beg);
	if (!ainFile.read(reinterpret_cast<char*>(&fileNodeCount), sizeof(fileNodeCount)))
	{
		Warning("verifyain: Failed to read nodecount.\n");
		ainFile.close();
		return;
	}

	if (fileNodeCount != pNetwork->nodecount)
	{
		Warning("verifyain: nodecount mismatch: file=%d, mem=%d.\n", fileNodeCount, pNetwork->nodecount);
		// We still can proceed to see how off it is, but let's bail for now:
		ainFile.close();
		return;
	}

	// We'll gather the nodes from disk:
	std::vector<CAI_NodeDisk> diskNodes(fileNodeCount);

	// The node array starts at offset 0x10
	ainFile.seekg(0x10, std::ios::beg);
	const std::streamsize nodeSize = sizeof(CAI_NodeDisk);
	if (!ainFile.read(reinterpret_cast<char*>(diskNodes.data()), fileNodeCount * nodeSize))
	{
		Warning("verifyain: Failed to read node array.\n");
		ainFile.close();
		return;
	}

	// Compare each node:
	int nodeMismatchCount = 0;
	for (int i = 0; i < fileNodeCount; ++i)
	{
		CAI_NodeDisk& dn = diskNodes[i];
		CAI_Node* pm = pNetwork->nodes[i];
		if (!pm)
		{
			Warning("verifyain: node %d in memory is NULL.\n", i);
			continue;
		}

		// Let's do a few checks:
		// 1) Position, ignoring small floating error:
		float dx = std::fabs(dn.x - pm->position.x);
		float dy = std::fabs(dn.y - pm->position.y);
		float dz = std::fabs(dn.z - pm->position.z);
		if (dx > 0.01f || dy > 0.01f || dz > 0.01f)
		{
			Warning("verifyain: node %d pos mismatch: disk=(%.2f,%.2f,%.2f) mem=(%.2f,%.2f,%.2f)\n",
				i, dn.x, dn.y, dn.z, pm->position.x, pm->position.y, pm->position.z);
			nodeMismatchCount++;
		}

		// 2) Yaw:
		if (std::fabs(dn.yaw - pm->yaw) > 0.01f)
		{
			Warning("verifyain: node %d yaw mismatch: disk=%.2f mem=%.2f\n", i, dn.yaw, pm->yaw);
			nodeMismatchCount++;
		}

		// 3) hull offsets:
		for (int h = 0; h < 5; h++)
		{
			float dval = std::fabs(dn.hulls[h] - pm->hulls[h]);
			if (dval > 0.01f)
			{
				Warning("verifyain: node %d hull[%d] mismatch: disk=%.2f mem=%.2f\n", i, h, dn.hulls[h], pm->hulls[h]);
				nodeMismatchCount++;
			}
		}

		// 4) flags:
		if (dn.unk1 != pm->flags)
		{
			Warning("verifyain: node %d flags mismatch: disk=%d mem=%d\n", i, dn.unk1, pm->flags);
			nodeMismatchCount++;
		}

		// 5) unk0 value:
		if (dn.unk0 != (char)pm->unk0)
		{
			Warning("verifyain: node %d unk0 mismatch: disk=%d mem=%d\n", i, (int)dn.unk0, pm->unk0);
			nodeMismatchCount++;
		}

		// 6) unk2 array (comparing shorts to ints):
		for (int j = 0; j < 5; j++)
		{
			if (dn.unk2[j] != (short)pm->unk2[j])
			{
				Warning("verifyain: node %d unk2[%d] mismatch: disk=%d mem=%d\n",
					i, j, (int)dn.unk2[j], pm->unk2[j]);
				nodeMismatchCount++;
			}
		}

		// 7) unk3 array:
		for (int j = 0; j < 5; j++)
		{
			if (dn.unk3[j] != pm->unk3[j])
			{
				Warning("verifyain: node %d unk3[%d] mismatch: disk=%d mem=%d\n",
					i, j, (int)dn.unk3[j], (int)pm->unk3[j]);
				nodeMismatchCount++;
			}
		}

		// 8) unk4 value:
		if (dn.unk4 != pm->unk6)
		{
			Warning("verifyain: node %d unk4 mismatch: disk=%d mem=%d\n", i, dn.unk4, pm->unk6);
			nodeMismatchCount++;
		}
		auto memunk5 = (unsigned char)(pm->unk9[0]) == 0xFF ? -1 : (uint8_t)((pm->unk9[0]));

		// 9) unk5 value:
		if (dn.unk5 != memunk5)
		{
			Warning("verifyain: node %d unk5 mismatch: disk=%d mem=%d\n", i, dn.unk5, pm->unk8);
			nodeMismatchCount++;
		}

		// 10) scriptdata (8 bytes):
		if (memcmp(dn.unk6, pm->scriptdata, 8) != 0)
		{
			Warning("verifyain: node %d scriptdata mismatch.\n", i);
			nodeMismatchCount++;
		}
	}
	// After writing nodes, set read pointer to match where we'd be after reading them
	ainFile.seekg(0x10 + (pNetwork->nodecount * sizeof(CAI_NodeDisk)), std::ios::beg);

	// Read linkCount after the node block
	int fileLinkCount = 0;
	if (!ainFile.read(reinterpret_cast<char*>(&fileLinkCount), sizeof(fileLinkCount)))
	{
		Warning("verifyain: Could not read linkcount.\n");
		ainFile.close();
		return;
	}

	// Read the link array
	const std::streamsize linkDiskSize = sizeof(CAI_NodeLinkDisk);
	std::vector<CAI_NodeLinkDisk> diskLinks(fileLinkCount);
	if (!ainFile.read(reinterpret_cast<char*>(diskLinks.data()), fileLinkCount * linkDiskSize))
	{
		Warning("verifyain: Could not read link array.\n");
		ainFile.close();
		return;
	}
	ainFile.close();

	// Compare linkcount
	if (fileLinkCount != pNetwork->linkcount)
	{
		Warning("verifyain: linkcount mismatch: file=%d mem=%d.\n", fileLinkCount, pNetwork->linkcount);
	}

	// Compare links using naive matching
	int linkMismatchCount = 0;
	for (int i = 0; i < fileLinkCount; ++i)
	{
		CAI_NodeLinkDisk& dl = diskLinks[i];
		bool foundMatch = false;

		// Search memory for a link with (srcId,destId) or (destId,srcId)
		for (int n = 0; n < pNetwork->nodecount && !foundMatch; ++n)
		{
			CAI_Node* pn = pNetwork->nodes[n];
			if (!pn)
				continue;

			for (int ln = 0; ln < pn->linkcount && !foundMatch; ln++)
			{
				CAI_NodeLink* memLink = pn->links[ln];
				if (!memLink)
					continue;

				bool samePair =
					(memLink->srcId == dl.srcId && memLink->destId == dl.destId) ||
					(memLink->srcId == dl.destId && memLink->destId == dl.srcId);

				if (samePair)
				{
					// Check the hull bits for mismatch
					for (int h = 0; h < 5; h++)
					{
						if (dl.hulls[h] != memLink->hulls[h])
						{
							Warning("verifyain: link mismatch (src=%d dest=%d) hull[%d] disk=%d mem=%d\n",
								dl.srcId, dl.destId, h, (int)dl.hulls[h], (int)memLink->hulls[h]);
							linkMismatchCount++;
						}
					}
					// Check the link info byte
					if (dl.unk0 != memLink->unk1)
					{
						Warning("verifyain: link mismatch (src=%d dest=%d) linkinfo disk=%d mem=%d\n",
							dl.srcId, dl.destId, dl.unk0, memLink->unk1);
						linkMismatchCount++;
					}

					foundMatch = true;
				}
			}
		}

		if (!foundMatch)
		{
			Warning("verifyain: disk link (src=%d dest=%d) not found in memory.\n",
				dl.srcId, dl.destId);
			linkMismatchCount++;
		}
	}

	Msg("verifyain: Done.\n");
	Msg("verifyain: Node mismatches: %d, link mismatches: %d.\n", nodeMismatchCount, linkMismatchCount);
	if (nodeMismatchCount == 0 && linkMismatchCount == 0)
	{
		Msg("verifyain: Everything matches perfectly.\n");
	}
}


static bool WriteBytesAtPosition(std::fstream& file, std::streampos pos, const char* data, size_t size)
{
	file.seekp(pos);
	if (!file.write(data, size))
		return false;
	return true;
}

using ResetBitVecFunc = void(*)(void* table);  // sub_18036BC30
using ResizeFunc = void(*)(void* table, unsigned int param1, unsigned int numNodes);  // sub_18036C150
using BitVecResizeFunc = void(*)(unsigned __int16* table, unsigned int size, int param);  // CVarBitVecBase<unsigned short>::Resize

struct CVarBitVec2 {
	uint16_t size;
	uint16_t numInts;
	uint32_t inlineData;
	void* pData;
};

// CUtlMemory<CVarBitVec>
struct CVarBitVecMemory2 {
	CVarBitVec2* m_pMemory;
	int m_nAllocationCount;
	int m_nGrowSize;
};

// CUtlVector<CVarBitVec, CUtlMemory<CVarBitVec>>
struct NeighborsTable {
	CVarBitVecMemory2 m_Memory;
	int m_Size;
};

void (*original_init_table)(void* table, void* b, void * c);
void __fastcall InitTableHook(void* table, void* b, void* c) {
	static bool initialized = false;
	// Only initialize others if this is the main table in Rebuild
		// Initialize each vector properly
		void* dstTables[] = {
			(void*)(G_server + 0xD416D0),
			(void*)(G_server + 0xD416F0),
			(void*)(G_server + 0xD41710)
		};
		// Each table is 0x20 bytes based on the SDK struct sizes
		const size_t tableSize = 0x20;
		void* srcTable = (void*)(G_server + 0xD41730);

		for (auto dstTable : dstTables) {
			memcpy(dstTable, srcTable, tableSize);
		}




	// Call original for passed table
	original_init_table(table, b, c);

}
void updateain_cmd(const CCommand& args)
{
	using FnGetAINetworkManager = CAI_NetworkManager * (*)();
	using FnStartRebuild = void(__fastcall*)(CAI_NetworkManager*);


	static FnStartRebuild s_StartRebuild =
		reinterpret_cast<FnStartRebuild>(G_server + 0x3645F0);

	// 1) Grab manager
	CAI_NetworkManager* pManager = *(CAI_NetworkManager**)(G_server + 0x0C31898);
	if (!pManager)
	{
		Warning("updateain: Failed to get AI Network Manager.\n");
		return;
	}
	//PatchNodeTableRefs();
	// 2) Call ::StartRebuild
	s_StartRebuild(pManager);

	// 3) Get network pointer
	CAI_Network* pNetwork = *reinterpret_cast<CAI_Network**>(G_server + 0xC31888);
	if (!pNetwork || !pNetwork->nodes)
	{
		Warning("updateain: No valid AI network/nodes present.\n");
		return;
	}

	// 4) Build path
	std::filesystem::path ainPath("r1delta/maps/graphs");
	const char* mapName = static_cast<const char*>((pGlobalVarsServer)->mapname_pszValue);
	if (!mapName || !*mapName)
	{
		Warning("updateain: Current map name is invalid.\n");
		return;
	}
	ainPath /= mapName;
	ainPath += ".ain";
	int memLinksWritten = 0;

	{
		std::fstream ainFile(ainPath, std::ios::in | std::ios::out | std::ios::binary);
		if (!ainFile.is_open())
		{
			Warning("updateain: Failed to open '%s' for read/write.\n", ainPath.string().c_str());
			return;
		}

		// 5) Read and verify nodecount
		int fileNodeCount = 0;
		ainFile.seekg(0xC, std::ios::beg);
		if (!ainFile.read(reinterpret_cast<char*>(&fileNodeCount), sizeof(fileNodeCount)))
		{
			Warning("updateain: Failed to read nodecount.\n");
			ainFile.close();
			return;
		}

		if (fileNodeCount != pNetwork->nodecount)
		{
			Warning("updateain: File nodecount %d != memory nodecount %d. Cancelling.\n",
				fileNodeCount, pNetwork->nodecount);
			ainFile.close();
			return;
		}

		// 6) Write nodes
		ainFile.seekp(0x10, std::ios::beg);  // Node array starts at 0x10
		const std::streamsize nodeDiskSize = sizeof(CAI_NodeDisk);
		for (int i = 0; i < pNetwork->nodecount; ++i)
		{
			// Read existing node from disk
			CAI_NodeDisk oldDisk;
			std::streampos nodePos = ainFile.tellp();
			ainFile.seekg(nodePos);
			if (!ainFile.read(reinterpret_cast<char*>(&oldDisk), nodeDiskSize))
			{
				Warning("updateain: Failed reading existing node %d\n", i);
				continue;
			}
			ainFile.seekp(nodePos);

			CAI_Node* pMemNode = pNetwork->nodes[i];
			CAI_NodeDisk newDisk{};

			// Position
			newDisk.x = pMemNode->position.x;
			newDisk.y = pMemNode->position.y;
			newDisk.z = pMemNode->position.z;
			if (std::abs(newDisk.x - oldDisk.x) > 0.01f ||
				std::abs(newDisk.y - oldDisk.y) > 0.01f ||
				std::abs(newDisk.z - oldDisk.z) > 0.01f)
			{
				Msg("Node %d: Changing pos from (%.2f,%.2f,%.2f) to (%.2f,%.2f,%.2f)\n",
					i, oldDisk.x, oldDisk.y, oldDisk.z, newDisk.x, newDisk.y, newDisk.z);
			}

			// Yaw
			newDisk.yaw = pMemNode->yaw;
			if (std::abs(newDisk.yaw - oldDisk.yaw) > 0.01f)
			{
				Msg("Node %d: Changing yaw from %.2f to %.2f\n", i, oldDisk.yaw, newDisk.yaw);
			}

			// Hull offsets
			memcpy(newDisk.hulls, pMemNode->hulls, sizeof(newDisk.hulls));
			for (int h = 0; h < 5; h++)
			{
				if (std::abs(newDisk.hulls[h] - oldDisk.hulls[h]) > 0.01f)
				{
					Msg("Node %d: Changing hull[%d] from %.2f to %.2f\n",
						i, h, oldDisk.hulls[h], newDisk.hulls[h]);
				}
			}

			// unk0
			newDisk.unk0 = static_cast<char>(pMemNode->unk0);
			if (newDisk.unk0 != oldDisk.unk0)
			{
				Msg("Node %d: Changing unk0 from %d to %d\n",
					i, (int)oldDisk.unk0, (int)newDisk.unk0);
			}

			// unk1 (flags)
			newDisk.unk1 = pMemNode->flags;
			if (newDisk.unk1 != oldDisk.unk1)
			{
				Msg("Node %d: Changing flags(unk1) from %d to %d\n",
					i, oldDisk.unk1, newDisk.unk1);
			}

			// unk2 array
			for (int j = 0; j < 5; j++)
			{
				newDisk.unk2[j] = static_cast<short>(pMemNode->unk2[j]);
				if (newDisk.unk2[j] != oldDisk.unk2[j])
				{
					Msg("Node %d: Changing unk2[%d] from %d to %d\n",
						i, j, oldDisk.unk2[j], newDisk.unk2[j]);
				}
			}

			// unk3 array
			memcpy(newDisk.unk3, pMemNode->unk3, sizeof(newDisk.unk3));
			for (int j = 0; j < 5; j++)
			{
				if (newDisk.unk3[j] != oldDisk.unk3[j])
				{
					Msg("Node %d: Changing unk3[%d] from %d to %d\n",
						i, j, (int)oldDisk.unk3[j], (int)newDisk.unk3[j]);
				}
			}

			// unk4 (from unk6)
			newDisk.unk4 = pMemNode->unk6;
			if (newDisk.unk4 != oldDisk.unk4)
			{
				Msg("Node %d: Changing unk4 from %d to %d\n",
					i, oldDisk.unk4, newDisk.unk4);
			}

			// unk5 (from unk8)
			newDisk.unk5 = (unsigned char)(pMemNode->unk9[0]) == 0xFF ? -1 : (uint8_t)((pMemNode->unk9[0]));
			if (newDisk.unk5 != oldDisk.unk5)
			{
				Msg("Node %d: Changing unk5 disk: %d mem: %d\n",
					i, oldDisk.unk5, newDisk.unk5);
			}

			// scriptdata (unk6)
			memset(pMemNode->scriptdata, 0, sizeof(pMemNode->scriptdata));
			memcpy(newDisk.unk6, pMemNode->scriptdata, sizeof(newDisk.unk6));
			if (memcmp(newDisk.unk6, oldDisk.unk6, sizeof(newDisk.unk6)) != 0)
			{
				Msg("Node %d: Changing scriptdata\n", i);
			}

			if (!ainFile.write(reinterpret_cast<const char*>(&newDisk), nodeDiskSize))
			{
				Warning("updateain: Failed writing node %d\n", i);
			}
		}


		// Explicitly position read pointer after the node block
		std::streamoff nodeBlockSize = static_cast<std::streamoff>(pNetwork->nodecount) * nodeDiskSize;
		Msg("updateain: Node block size: 0x%llX\n", static_cast<unsigned long long>(nodeBlockSize));
		Msg("updateain: Read position before explicit seekg call: 0x%llX\n", static_cast<unsigned long long>(ainFile.tellg()));
		ainFile.seekg(0x10 + nodeBlockSize, std::ios::beg);
		Msg("updateain: Read position after explicit seekg call: 0x%llX\n", static_cast<unsigned long long>(ainFile.tellg()));

		// 7) Read original link count
		int fileLinkCount = 0;
		if (!ainFile.read(reinterpret_cast<char*>(&fileLinkCount), sizeof(fileLinkCount)))
		{
			Warning("updateain: Could not read linkcount.\n");
			ainFile.close();
			return;
		}
		Msg("updateain: Read position after linkcount: 0x%llX\n", static_cast<unsigned long long>(ainFile.tellg()));
		if (fileLinkCount != pNetwork->linkcount)
		{
			Warning("updateain: File linkcount %d != memory linkcount %d. Possibly mismatched.\n",
				fileLinkCount, pNetwork->linkcount);
			ainFile.close();
			return;
		}

		// Read existing link array
		const std::streamsize linkDiskSize = sizeof(CAI_NodeLinkDisk);
		std::vector<CAI_NodeLinkDisk> fileLinks(fileLinkCount);
		if (!ainFile.read(reinterpret_cast<char*>(fileLinks.data()), fileLinkCount * linkDiskSize))
		{
			Warning("updateain: Could not read link array.\n");
			ainFile.close();
			return;
		}

		// Seek back to start of link array for writing
		ainFile.seekp(-static_cast<std::streamoff>(fileLinkCount * linkDiskSize), std::ios::cur);

		// Update and write each link
		for (int iLink = 0; iLink < fileLinkCount; ++iLink)
		{
			CAI_NodeLinkDisk& oldLink = fileLinks[iLink];
			CAI_NodeLinkDisk updatedLink = oldLink;

			bool foundMatch = false;
			for (int n = 0; n < pNetwork->nodecount && !foundMatch; ++n)
			{
				CAI_Node* pNode = pNetwork->nodes[n];
				if (!pNode) continue;

				for (int ln = 0; ln < pNode->linkcount && !foundMatch; ++ln)
				{
					CAI_NodeLink* pMemLink = pNode->links[ln];
					if (!pMemLink)
						continue;

					if ((pMemLink->srcId == oldLink.srcId && pMemLink->destId == oldLink.destId) ||
						(pMemLink->srcId == oldLink.destId && pMemLink->destId == oldLink.srcId))
					{
						updatedLink.srcId = pMemLink->srcId;
						updatedLink.destId = pMemLink->destId;
						updatedLink.unk0 = pMemLink->unk1;
						memcpy(updatedLink.hulls, pMemLink->hulls, sizeof(updatedLink.hulls));
						foundMatch = true;

						// Log changes
						if (updatedLink.srcId != oldLink.srcId || updatedLink.destId != oldLink.destId)
						{
							Msg("Link %d: Changing src/dest from %d/%d to %d/%d\n",
								iLink, oldLink.srcId, oldLink.destId, updatedLink.srcId, updatedLink.destId);
						}

						if (updatedLink.unk0 != oldLink.unk0)
						{
							Msg("Link %d: Changing unk0 from %d to %d\n",
								iLink, (int)oldLink.unk0, (int)updatedLink.unk0);
						}

						for (int h = 0; h < 5; h++)
						{
							if (updatedLink.hulls[h] != oldLink.hulls[h])
							{
								Msg("Link %d: Changing hull[%d] from %d to %d\n",
									iLink, h, (int)oldLink.hulls[h], (int)updatedLink.hulls[h]);
							}
						}
					}
				}
			}

			if (!foundMatch)
			{
				Msg("Link %d: Could not find matching memory link for disk link (src=%d dest=%d)\n",
					iLink, oldLink.srcId, oldLink.destId);
			}

			if (!ainFile.write(reinterpret_cast<const char*>(&updatedLink), linkDiskSize))
			{
				Warning("updateain: Failed writing link %d\n", iLink);
			}
			else
			{
				++memLinksWritten;
			}
		}

		ainFile.close();
	}
	// Second pass - zero out scriptdata
	{
		std::fstream ainFile(ainPath, std::ios::in | std::ios::out | std::ios::binary);
		if (!ainFile.is_open())
		{
			Warning("updateain: Failed to reopen file for scriptdata zeroing.\n");
			return;
		}

		// Read nodecount from the file at offset 0xC
		int fileNodeCount = 0;
		ainFile.seekg(0xC);
		if (!ainFile.read(reinterpret_cast<char*>(&fileNodeCount), sizeof(int)))
		{
			Warning("updateain: Failed to read node count for scriptdata zeroing.\n");
			ainFile.close();
			return;
		}

		// Compare with in-memory node count
		if (fileNodeCount != pNetwork->nodecount)
		{
			Warning("updateain: Node count mismatch during scriptdata zeroing.\n");
			ainFile.close();
			return;
		}

		// Zero buffer for scriptdata
		char zeroBuffer[8] = { 0 };  // scriptdata is 8 bytes

		// Iterate through each node and write zeroed scriptdata
		for (int nodeIndex = 0; nodeIndex < fileNodeCount; ++nodeIndex)
		{
			// Calculate the position to write scriptdata
			// scriptdata is at offset 0x4C + (0x44 * nodeIndex)
			std::streampos scriptDataPos = 0x4C + static_cast<std::streampos>(0x44) * nodeIndex;

			// Write zeros to the scriptdata position
			ainFile.seekp(scriptDataPos);
			if (!ainFile.write(zeroBuffer, sizeof(zeroBuffer)))
			{
				Warning("updateain: Failed to zero scriptdata for node %d.\n", nodeIndex);
				continue;
			}
		}

		ainFile.close();
	}
	Msg("updateain: Overwrote %d nodes & %d links in '%s'.\n",
		pNetwork->nodecount, memLinksWritten, ainPath.string().c_str());
}