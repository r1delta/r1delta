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
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
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
#include "squirrel.h"
#include "logging.h"
#include <bitvec.h>
#include "vsdk/public/tier1/utlvector.h"
#pragma intrinsic(_ReturnAddress)
CAI_NetworkManager__DelayedInitType CAI_NetworkManager__DelayedInitOriginal;
CAI_DynamicLink__InitDynamicLinksType CAI_DynamicLink__InitDynamicLinksOriginal;
CAI_NetworkManager__OpenAINFileType CAI_NetworkManager__OpenAINFileOriginal;
CAI_NetworkManager__LoadNavMeshType CAI_NetworkManager__LoadNavMeshOriginal;
CAI_NetworkManager__FixupHintsType CAI_NetworkManager__FixupHintsOriginal;

int* pTraverseNodeCount;
CAI_TraverseNodeDisk** ppTraverseNodes;
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

using TFORayInitFn = bool(__fastcall*)(void*, const Vector3f*, const Vector3f*, const Vector3f*, const Vector3f*);
using TFOTraceRayFn = void(__fastcall*)(uintptr_t, void*, unsigned int, void*, unsigned int, void*);
static bool ParseAINDebugSafety(int& node, int& hull);

constexpr size_t AIN_TRACE_RESULT_SIZE = 4096;

static uintptr_t GetAINBuildHullObject(int hull)
{
	if (hull < 0 || hull >= MAX_HULLS)
		return 0;

	auto hullTable = reinterpret_cast<uintptr_t*>(G_server + 0x7AB250);
	return hullTable[hull];
}

static const Vector3f* GetAINBuildHullMaxs(int hull)
{
	uintptr_t hullObject = GetAINBuildHullObject(hull);
	if (!hullObject)
		return nullptr;
	return reinterpret_cast<const Vector3f*>(hullObject + 0x1C);
}

static const Vector3f* GetAINBuildHullMins(int hull)
{
	uintptr_t hullObject = GetAINBuildHullObject(hull);
	if (!hullObject)
		return nullptr;
	return reinterpret_cast<const Vector3f*>(hullObject + 0x10);
}

static bool TraceAINBuildHull(const Vector3f& start, const Vector3f& end, const Vector3f& mins, const Vector3f& maxs, char* traceResult)
{
	if (!traceResult)
		return false;

	uintptr_t engineTrace = *reinterpret_cast<uintptr_t*>(G_server + 0xC31110);
	if (!engineTrace)
		return false;

	uintptr_t traceVTable = *reinterpret_cast<uintptr_t*>(engineTrace);
	if (!traceVTable)
		return false;

	auto initRay = reinterpret_cast<TFORayInitFn>(G_server + 0x091880);
	auto traceRay = *reinterpret_cast<TFOTraceRayFn*>(traceVTable + 0x28);
	if (!initRay || !traceRay)
		return false;

	alignas(16) char ray[0x50] = {};
	uintptr_t filter[4] = {
		G_server + 0x884E68, // CTraceFilterSimple::`vftable'
		0,
		0,
		0
	};

	initRay(ray, &start, &end, &mins, &maxs);
	memset(traceResult, 0, AIN_TRACE_RESULT_SIZE);
	traceRay(engineTrace, ray, 0x2400B, filter, 0, traceResult);
	return true;
}


static Vector3f AINTraceEndPosition(const char* traceResult)
{
	if (!traceResult)
		return Vector3f{};

	return *reinterpret_cast<const Vector3f*>(traceResult + 0x10);
}

static float AINTraceRawFraction(const char* traceResult)
{
	return traceResult ? *reinterpret_cast<const float*>(traceResult + 0x30) : 1.0f;
}

static bool AINTraceStartSolid(const char* traceResult)
{
	return traceResult && *reinterpret_cast<const unsigned char*>(traceResult + 0x3B) != 0;
}

static unsigned char AINTraceSurfaceFlags(const char* traceResult)
{
	return traceResult ? *reinterpret_cast<const unsigned char*>(traceResult + 0x54) : 0;
}

static float GetAINBuildHullHeight(int hull)
{
	const Vector3f* mins = GetAINBuildHullMins(hull);
	const Vector3f* maxs = GetAINBuildHullMaxs(hull);
	if (!mins || !maxs)
		return 0.0f;
	return maxs->z - mins->z;
}

static float AINTraceFraction(const char* traceResult)
{
	float fraction = *reinterpret_cast<const float*>(traceResult + 0x30);
	return fminf(fmaxf(1.0f - fraction, 0.0f), 1.0f);
}

int sub_390AE0(CAI_Network* network)
{
	constexpr int AIN_SAFETY_TRACE_MASK = 71319553;
	auto UTIL_TraceLine = reinterpret_cast<void** (*)(Vector3f* a1, Vector3f* a2, __int64 a3, __int64 a4, int a5, char* a6)>(G_server + 0x263AF0);

	CAI_Node** nodes = network->nodes;
	int nodecount = network->nodecount;
	const bool debugSafety = HasEngineCommandLineFlag("-r1delta_ain_safety_debug");
	int debugSafetyNode = -1;
	int debugSafetyHull = -1;
	const bool debugSafetyDetail = ParseAINDebugSafety(debugSafetyNode, debugSafetyHull);

	Vector3f directions[8] = {
		{1.0f, 0.0f, 0.0f},
		{1.0f, 1.0f, 0.0f},
		{0.0f, 1.0f, 0.0f},
		{-1.0f, 1.0f, 0.0f},
		{-1.0f, 0.0f, 0.0f},
		{-1.0f, -1.0f, 0.0f},
		{0.0f, -1.0f, 0.0f},
		{1.0f, -1.0f, 0.0f},
	};

	for (int i = 0; i < 2; i++)
	{
		int nodeIndex = (i == 0) ? 0 : 4;

		float maxSafetyValue = 0.0f;
		unsigned int maxSafetyNode = -1;
		const Vector3f* nodeMin = GetAINBuildHullMins(nodeIndex);
		const Vector3f* nodeMax = GetAINBuildHullMaxs(nodeIndex);
		if (!nodeMin || !nodeMax)
			continue;

		Vector3f offset = {
			(nodeMax->x - nodeMin->x) * 0.5f,
			(nodeMax->y - nodeMin->y) * 0.5f,
			(nodeMax->z - nodeMin->z) * 0.5f
		};

		float nodeValue = GetAINBuildHullHeight(nodeIndex);

		for (int j = 0; j < nodecount; j++)
		{
			CAI_Node* node = nodes[j];

			float nodeSafetyValue = 0.0f;
			const bool debugThisSafety = debugSafetyDetail && j == debugSafetyNode && nodeIndex == debugSafetyHull;

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
				float inverseLength = 1.0f / fmaxf(directionLength, 1.1920929e-7f);

				Vector3f end = {
					start.x + ((direction.x * inverseLength) * 1300.0f),
					start.y + ((direction.y * inverseLength) * 1300.0f),
					start.z + ((direction.z * inverseLength) * 1300.0f)
				};

				char traceResult[4096] = { 0 };
				UTIL_TraceLine(&start, &end, AIN_SAFETY_TRACE_MASK, 0, 0, traceResult);

				float fraction = AINTraceFraction(traceResult);
				const float contribution = ((fraction * fraction) * fraction) * 0.77499998f;
				nodeSafetyValue += contribution;
				if (debugThisSafety)
				{
					char buffer[512];
					const float rawFraction = AINTraceRawFraction(traceResult);
					const Vector3f hit = AINTraceEndPosition(traceResult);
					_snprintf_s(
						buffer,
						sizeof(buffer),
						_TRUNCATE,
						"AINSAFETY_DETAIL: hull=%d node=%d ray=%d rawfrac=%.9g frac=%.9g contrib=%.9g start=(%.6f %.6f %.6f) end=(%.6f %.6f %.6f) hit=(%.6f %.6f %.6f) surface=0x%02x startsolid=%d\n",
						nodeIndex,
						j,
						k,
						rawFraction,
						fraction,
						contribution,
						start.x,
						start.y,
						start.z,
						end.x,
						end.y,
						end.z,
						hit.x,
						hit.y,
						hit.z,
						static_cast<unsigned int>(AINTraceSurfaceFlags(traceResult)),
						AINTraceStartSolid(traceResult) ? 1 : 0);
					OutputDebugStringA(buffer);
				}
			}

			Vector3f start = {
				node->position.x,
				node->position.y,
				node->position.z + nodeValue
			};

			Vector3f end = {
				start.x,
				start.y,
				start.z + 260.0f
			};

			char traceResult[4096] = { 0 };
			UTIL_TraceLine(&start, &end, AIN_SAFETY_TRACE_MASK, 0, 0, traceResult);

			float fraction = AINTraceFraction(traceResult);
			const float verticalContribution = (fraction * fraction) * 1.5f;
			nodeSafetyValue += verticalContribution;
			if (debugThisSafety)
			{
				char buffer[512];
				const float rawFraction = AINTraceRawFraction(traceResult);
				const Vector3f hit = AINTraceEndPosition(traceResult);
				_snprintf_s(
					buffer,
					sizeof(buffer),
					_TRUNCATE,
					"AINSAFETY_DETAIL: hull=%d node=%d vertical rawfrac=%.9g frac=%.9g contrib=%.9g start=(%.6f %.6f %.6f) end=(%.6f %.6f %.6f) hit=(%.6f %.6f %.6f) surface=0x%02x startsolid=%d\n",
					nodeIndex,
					j,
					rawFraction,
					fraction,
					verticalContribution,
					start.x,
					start.y,
					start.z,
					end.x,
					end.y,
					end.z,
					hit.x,
					hit.y,
					hit.z,
					static_cast<unsigned int>(AINTraceSurfaceFlags(traceResult)),
					AINTraceStartSolid(traceResult) ? 1 : 0);
				OutputDebugStringA(buffer);
			}

			if ((node->flags & 0x80000) != 0)
				nodeSafetyValue *= 1.2f;

			float adjustedSafetyValue = floorf(nodeSafetyValue + 0.5f);
			if (debugSafety)
			{
				char buffer[256];
				_snprintf_s(
					buffer,
					sizeof(buffer),
					_TRUNCATE,
					"AINSAFETY: hull=%d node=%d raw=%.9g adjusted=%.9g byte=%u pos=(%.6f %.6f %.6f)\n",
					nodeIndex,
					j,
					nodeSafetyValue,
					adjustedSafetyValue,
					static_cast<unsigned int>(static_cast<unsigned char>(adjustedSafetyValue)),
					node->position.x,
					node->position.y,
					node->position.z);
				OutputDebugStringA(buffer);
			}
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

static std::filesystem::path BuildAINPathForCurrentMap(const char* suffix)
{
	std::filesystem::path writePath("r1delta/maps/graphs");
	char exePath[MAX_PATH] = {};
	if (GetModuleFileNameA(nullptr, exePath, sizeof(exePath)))
	{
		std::filesystem::path exeGraphPath = std::filesystem::path(exePath).parent_path() / "r1delta/maps/graphs";
		if (std::filesystem::exists(exeGraphPath))
			writePath = exeGraphPath;
	}

	writePath /= (char*)(pGlobalVarsServer)->mapname_pszValue;
	writePath += suffix;
	return writePath;
}

static bool ReadExistingAINHeader(const std::filesystem::path& ainPath, int& mapVersion, int& crc)
{
	std::ifstream in(ainPath, std::ios::binary);
	if (!in.is_open())
		return false;

	int version = 0;
	in.read(reinterpret_cast<char*>(&version), sizeof(version));
	in.read(reinterpret_cast<char*>(&mapVersion), sizeof(mapVersion));
	in.read(reinterpret_cast<char*>(&crc), sizeof(crc));
	return in.good() && version == AINET_VERSION_NUMBER;
}

template <typename T>
static void WriteAINValue(std::ofstream& writeStream, const T& value)
{
	writeStream.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

static short GetDiskNodeUnk5(const CAI_Node* node)
{
	return (static_cast<unsigned char>(node->unk9[0]) == 0xFF) ? -1 : static_cast<unsigned char>(node->unk9[0]);
}

static int CountDiskLinks(const CAI_Network* aiNetwork)
{
	int linkCount = 0;
	for (int i = 0; i < aiNetwork->nodecount; i++)
	{
		const CAI_Node* node = aiNetwork->nodes[i];
		if (!node)
			continue;

		for (int j = 0; j < node->linkcount; j++)
		{
			const CAI_NodeLink* link = node->links[j];
			if (link && link->srcId == node->index)
				++linkCount;
		}
	}
	return linkCount;
}

static void WriteWCNodeIndexTable(std::ofstream& writeStream, const CAI_Network* aiNetwork)
{
	int* nodeIndexTable = nullptr;
	auto manager = *reinterpret_cast<uintptr_t*>(G_server + 0x0C31898);
	if (manager)
	{
		auto editOps = *reinterpret_cast<uintptr_t*>(manager + 1592);
		if (editOps)
			nodeIndexTable = *reinterpret_cast<int**>(editOps + 24);
	}

	for (int i = 0; i < aiNetwork->nodecount; i++)
	{
		const int value = nodeIndexTable ? nodeIndexTable[i] : 0;
		WriteAINValue(writeStream, value);
	}
}

static void WriteTraverseNodes(std::ofstream& writeStream)
{
	short count = 0;
	if (pTraverseNodeCount)
		count = static_cast<short>(std::max(0, *pTraverseNodeCount));

	WriteAINValue(writeStream, count);

	const CAI_TraverseNodeDisk* nodes = (ppTraverseNodes && *ppTraverseNodes) ? *ppTraverseNodes : nullptr;
	for (int i = 0; i < count; i++)
	{
		CAI_TraverseNodeDisk node{};
		if (nodes)
			node = nodes[i];
		writeStream.write(reinterpret_cast<const char*>(&node), sizeof(node));
	}
}

static void WriteHullZoneBits(std::ofstream& writeStream, const CAI_Network* aiNetwork)
{
	for (int hull = 0; hull < MAX_HULLS; hull++)
	{
		const int numZones = std::max(0, aiNetwork->zonecount[hull]);
		const uint16_t numBits = static_cast<uint16_t>(std::min(numZones * numZones, 0xFFFF));
		const uint16_t numInts = static_cast<uint16_t>((numBits + 31) >> 5);
		WriteAINValue(writeStream, numZones);
		WriteAINValue(writeStream, numBits);
		WriteAINValue(writeStream, numInts);

		const uint32_t* bits = aiNetwork->hullZoneBits[hull].pData;
		for (int i = 0; i < numInts; i++)
		{
			const uint32_t value = bits ? bits[i] : 0;
			WriteAINValue(writeStream, value);
		}
	}
}

static void WriteUnknownNodeTail(std::ofstream& writeStream)
{
	const int count = pUnkStruct0Count ? std::max(0, *pUnkStruct0Count) : 0;
	WriteAINValue(writeStream, count);

	UnkNodeStruct0** nodes = (pppUnkNodeStruct0s && *pppUnkNodeStruct0s) ? *pppUnkNodeStruct0s : nullptr;
	for (int i = 0; i < count; i++)
	{
		const UnkNodeStruct0* node = nodes ? nodes[i] : nullptr;
		if (!node)
		{
			const int zero = 0;
			const char zeroByte = 0;
			WriteAINValue(writeStream, zero);
			WriteAINValue(writeStream, zeroByte);
			WriteAINValue(writeStream, zero);
			WriteAINValue(writeStream, zero);
			WriteAINValue(writeStream, zero);
			WriteAINValue(writeStream, zero);
			WriteAINValue(writeStream, zero);
			WriteAINValue(writeStream, zeroByte);
			continue;
		}

		WriteAINValue(writeStream, node->index);
		WriteAINValue(writeStream, node->unk1);
		WriteAINValue(writeStream, node->x);
		WriteAINValue(writeStream, node->y);
		WriteAINValue(writeStream, node->z);

		const int count0 = std::max(0, node->unkcount0);
		WriteAINValue(writeStream, count0);
		for (int j = 0; j < count0; j++)
		{
			const short value = node->unk2 ? static_cast<short>(node->unk2[j]) : 0;
			WriteAINValue(writeStream, value);
		}

		const int count1 = std::max(0, node->unkcount1);
		WriteAINValue(writeStream, count1);
		for (int j = 0; j < count1; j++)
		{
			const short value = node->unk3 ? static_cast<short>(node->unk3[j]) : 0;
			WriteAINValue(writeStream, value);
		}

		WriteAINValue(writeStream, node->unk5);
	}
}

static void WriteUnknownLinkTail(std::ofstream& writeStream)
{
	const int count = pUnkLinkStruct1Count ? std::max(0, *pUnkLinkStruct1Count) : 0;
	WriteAINValue(writeStream, count);

	UnkLinkStruct1** links = (pppUnkStruct1s && *pppUnkStruct1s) ? *pppUnkStruct1s : nullptr;
	for (int i = 0; i < count; i++)
	{
		const UnkLinkStruct1* link = links ? links[i] : nullptr;
		UnkLinkStruct1Disk disk{};
		if (link)
		{
			disk.unk0 = link->unk0;
			disk.unk1 = link->unk1;
			disk.unk2 = link->unk2;
			disk.unk3 = link->unk3;
			disk.unk4 = link->unk4;
			disk.unk5 = link->unk5;
		}
		writeStream.write(reinterpret_cast<const char*>(&disk), sizeof(disk));
	}
}

static bool DumpAINInfo(CAI_Network* aiNetwork, const std::filesystem::path& writePath, const std::filesystem::path& sourcePath, bool refreshSafety)
{
	if (!aiNetwork || !aiNetwork->nodes)
	{
		Warning("dumpain: no valid AI network.\n");
		return false;
	}

	// dump from memory
	//spdlog::info("writing ain file {}", writePath.string());
	//spdlog::info("");
	//spdlog::info("");
	//spdlog::info("");
	//spdlog::info("");
	//spdlog::info("");
	if (refreshSafety)
		sub_390AE0(aiNetwork);

	std::filesystem::create_directories(writePath.parent_path());

	std::ofstream writeStream(writePath, std::ofstream::binary);
	if (!writeStream.is_open())
	{
		Warning("dumpain: failed to open '%s' for writing.\n", writePath.string().c_str());
		return false;
	}

	//spdlog::info("writing ainet version: {}", AINET_VERSION_NUMBER);
	WriteAINValue(writeStream, AINET_VERSION_NUMBER);

	int mapVersion = (pGlobalVarsServer)->mapversion;
	int crc = PLACEHOLDER_CRC;
	ReadExistingAINHeader(sourcePath, mapVersion, crc);

	//spdlog::info("writing map version: {}", mapVersion);
	WriteAINValue(writeStream, mapVersion);
	//spdlog::info("writing placeholder crc: {}", PLACEHOLDER_CRC);
	WriteAINValue(writeStream, crc);

	// path nodes
	//spdlog::info("writing nodecount: {}", aiNetwork->nodecount);
	WriteAINValue(writeStream, aiNetwork->nodecount);

	for (int i = 0; i < aiNetwork->nodecount; i++)
	{
		// construct on-disk node struct
		CAI_Node* node = aiNetwork->nodes[i];
		CAI_NodeDisk diskNode{};
		if (!node)
		{
			writeStream.write((char*)&diskNode, sizeof(CAI_NodeDisk));
			continue;
		}

		diskNode.x = node->position.x;
		diskNode.y = node->position.y;
		diskNode.z = node->position.z;
		diskNode.yaw = node->yaw;
		memcpy(diskNode.hulls, node->hulls, sizeof(diskNode.hulls));
		diskNode.unk0 = (char)node->unk0;
		diskNode.unk1 = node->flags & AINET_NODE_SAVE_MASK;

		for (int j = 0; j < MAX_HULLS; j++)
		{
			diskNode.unk2[j] = (short)node->unk2[j];
			//spdlog::info((short)aiNetwork->nodes[i]->unk2[j]);
		}

		memcpy(diskNode.unk3, node->unk3, sizeof(diskNode.unk3));
		diskNode.unk4 = node->unk6;
		diskNode.unk5 = GetDiskNodeUnk5(node);
		memcpy(diskNode.unk6, node->scriptdata, sizeof(diskNode.unk6));

		//spdlog::info("writing node {} from {} to {:x}", aiNetwork->nodes[i]->index, (void*)aiNetwork->nodes[i], writeStream.tellp());
		writeStream.write((char*)&diskNode, sizeof(CAI_NodeDisk));
	}

	// links
	//spdlog::info("linkcount: {}", aiNetwork->linkcount);
	//spdlog::info("calculated total linkcount: {}", calculatedLinkcount);
	int calculatedLinkcount = CountDiskLinks(aiNetwork);
	//if (Cvar_ns_ai_dumpAINfileFromLoad->GetBool())
	//{
	//	if (aiNetwork->linkcount == calculatedLinkcount)
	//		spdlog::info("caculated linkcount is normal!");
	//	else
	//		spdlog::warn("calculated linkcount has weird value! this is expected on build!");
	//}
	//
	//spdlog::info("writing linkcount: {}", calculatedLinkcount);
	WriteAINValue(writeStream, calculatedLinkcount);

	for (int i = 0; i < aiNetwork->nodecount; i++)
	{
		CAI_Node* node = aiNetwork->nodes[i];
		if (!node)
			continue;

		for (int j = 0; j < node->linkcount; j++)
		{
			CAI_NodeLink* link = node->links[j];
			if (!link)
				continue;

			// skip links that don't originate from current node
			if (link->srcId != node->index)
				continue;

			CAI_NodeLinkDisk diskLink;
			diskLink.srcId = link->srcId;
			diskLink.destId = link->destId;
			diskLink.unk0 = link->unk1;
			memcpy(diskLink.hulls, link->hulls, sizeof(diskLink.hulls));

			//spdlog::info("writing link {} => {} to {:x}", diskLink.srcId, diskLink.destId, writeStream.tellp());
			writeStream.write((char*)&diskLink, sizeof(CAI_NodeLinkDisk));
		}
	}

	WriteWCNodeIndexTable(writeStream, aiNetwork);
	WriteTraverseNodes(writeStream);
	WriteHullZoneBits(writeStream, aiNetwork);
	WriteUnknownNodeTail(writeStream);
	WriteUnknownLinkTail(writeStream);
	WriteAINValue(writeStream, aiNetwork->scriptVersion);

	const bool ok = writeStream.good();
	writeStream.close();
	if (!ok)
	{
		Warning("dumpain: failed while writing '%s'.\n", writePath.string().c_str());
		return false;
	}

	Msg("dumpain: wrote '%s' (%d nodes, %d links).\n", writePath.string().c_str(), aiNetwork->nodecount, calculatedLinkcount);
	return true;
}

static bool RunAINFileBuiltCallback(CAI_Network* aiNetwork)
{
	if (!aiNetwork)
	{
		Warning("buildain: no AI network for CodeCallback_AINFileBuilt.\n");
		return false;
	}

	R1SquirrelVM* serverVm = GetServerVMPtr();
	if (!serverVm || !serverVm->sqvm)
	{
		Warning("buildain: no server SQVM; cannot call CodeCallback_AINFileBuilt.\n");
		return false;
	}

	aiNetwork->scriptVersion = -1;

	HSQUIRRELVM v = serverVm->sqvm;
	const SQInteger originalTop = sq_gettop ? sq_gettop(serverVm, v) : 0;

	base_getroottable(v);
	sq_pushstring(v, "CodeCallback_AINFileBuilt", -1);

	const bool hasCallback = SQ_SUCCEEDED(sq_get_noerr(v, -2)) && sq_gettype(v, -1) == OT_CLOSURE;
	if (!hasCallback)
	{
		if (sq_settop)
			sq_settop(v, originalTop);
		Warning("buildain: could not find script function CodeCallback_AINFileBuilt.\n");
		return true;
	}

	base_getroottable(v);
	const SQRESULT result = sq_call(v, 1, SQFalse, SQTrue);
	if (sq_settop)
		sq_settop(v, originalTop);

	if (SQ_FAILED(result))
	{
		Warning("buildain: CodeCallback_AINFileBuilt failed.\n");
		return false;
	}

	return true;
}

static void LogAINBuildStage(const char* stage, const CAI_Network* aiNetwork = nullptr)
{
	char buffer[512];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"AINBUILD: %s network=%p nodes=%d scriptVersion=%d\n",
		stage,
		aiNetwork,
		aiNetwork ? aiNetwork->nodecount : -1,
		aiNetwork ? aiNetwork->scriptVersion : 0);
	OutputDebugStringA(buffer);
}

using TFOAINBuildBeginFn = __int64(__fastcall*)();
using TFOAINBuildEndFn = __int64(__fastcall*)();
using TFOAINBitVecResizeFn = void(__fastcall*)(CAI_NetworkHullBitVec*, unsigned int, char);
using TFOAINBitVecVectorPurgeFn = void(__fastcall*)(void*);
using TFOAINBitVecVectorInsertFn = __int64(__fastcall*)(void*, int, int);
using TFOAINNodeStageFn = void(__fastcall*)(void*, CAI_Network*, CAI_Node*);
using TFOAINNoArgStageFn = void(__fastcall*)();
using TFOAINCanConnectHullFn = int(__fastcall*)(void*, CAI_Node*, CAI_Node*, int);
using TFOAINCreateLinkFn = CAI_NodeLink*(__fastcall*)(CAI_Network*, int, int);
using TFOAINTraverseGrowFn = void(__fastcall*)(__int64, __int64);
using TFOFreeFn = void(__fastcall*)(void*);
using TFOEntityIteratorFn = void*(__fastcall*)(void*, void*);
using TFOAINCreateNodeFn = CAI_Node*(__fastcall*)(CAI_Network*, const Vector3f*, float);
using TFORecalcAbsTransformFn = void(__fastcall*)(void*);
using TFOTriggerContainsPointFn = bool(__fastcall*)(void*, const Vector3f*);

static const char* FindAINCommandLineTokenValue(const char* token)
{
	const char* commandLine = GetCommandLineA();
	if (!commandLine || !token)
		return nullptr;

	const char* tokenStart = strstr(commandLine, token);
	if (!tokenStart)
		return nullptr;

	tokenStart += strlen(token);
	while (*tokenStart == ' ' || *tokenStart == '\t' || *tokenStart == '=')
		++tokenStart;
	return tokenStart;
}

static bool ParseAINDebugEdge(int& src, int& dst)
{
	static bool initialized = false;
	static bool enabled = false;
	static int cachedSrc = -1;
	static int cachedDst = -1;

	if (!initialized)
	{
		initialized = true;
		const char* value = FindAINCommandLineTokenValue("-r1delta_ain_debug_edge");
		if (!value)
			value = FindAINCommandLineTokenValue("+r1delta_ain_debug_edge");

		if (value)
		{
			char* end = nullptr;
			const long parsedSrc = strtol(value, &end, 10);
			while (end && (*end == ' ' || *end == '\t' || *end == ',' || *end == ':' || *end == ';'))
				++end;
			const long parsedDst = end ? strtol(end, &end, 10) : -1;
			if (parsedSrc >= 0 && parsedSrc <= 0xFFFF && parsedDst >= 0 && parsedDst <= 0xFFFF)
			{
				cachedSrc = static_cast<int>(parsedSrc);
				cachedDst = static_cast<int>(parsedDst);
				enabled = true;
			}
		}
	}

	src = cachedSrc;
	dst = cachedDst;
	return enabled;
}

static bool ParseAINDebugSafety(int& node, int& hull)
{
	static bool initialized = false;
	static bool enabled = false;
	static int cachedNode = -1;
	static int cachedHull = -1;

	if (!initialized)
	{
		initialized = true;
		const char* value = FindAINCommandLineTokenValue("-r1delta_ain_debug_safety");
		if (!value)
			value = FindAINCommandLineTokenValue("+r1delta_ain_debug_safety");

		if (value)
		{
			char* end = nullptr;
			const long parsedNode = strtol(value, &end, 10);
			while (end && (*end == ' ' || *end == '\t' || *end == ',' || *end == ':' || *end == ';'))
				++end;
			const long parsedHull = end ? strtol(end, &end, 10) : -1;
			if (parsedNode >= 0 && parsedNode <= 0xFFFF && (parsedHull == 0 || parsedHull == 4))
			{
				cachedNode = static_cast<int>(parsedNode);
				cachedHull = static_cast<int>(parsedHull);
				enabled = true;
			}
		}
	}

	node = cachedNode;
	hull = cachedHull;
	return enabled;
}

static bool ShouldSuppressExistingAINLoad()
{
	return HasEngineCommandLineFlag("-r1delta_build_ain_no_existing")
		|| HasEngineCommandLineFlag("-r1delta_no_existing_ain");
}

static CAI_NetworkHullBitVec* GetAINBitVecVectorElements(uintptr_t vectorAddress)
{
	return *reinterpret_cast<CAI_NetworkHullBitVec**>(vectorAddress);
}

static int* GetAINBitVecVectorSize(uintptr_t vectorAddress)
{
	return reinterpret_cast<int*>(vectorAddress + 0x18);
}

static bool TestAINBitVecBit(const CAI_NetworkHullBitVec& bitVec, int bit)
{
	if (bit < 0 || bit >= bitVec.size)
		return false;

	const uint32_t* data = bitVec.pData ? bitVec.pData : &bitVec.inlineData;
	return data && (data[bit >> 5] & (1u << (bit & 31))) != 0;
}

static bool TestAINBitVecVectorBit(uintptr_t vectorAddress, int src, int dst)
{
	const int* vectorSize = GetAINBitVecVectorSize(vectorAddress);
	if (!vectorSize || src < 0 || src >= *vectorSize)
		return false;

	const CAI_NetworkHullBitVec* elements = GetAINBitVecVectorElements(vectorAddress);
	return elements && TestAINBitVecBit(elements[src], dst);
}

static void SetAINBitVecBit(CAI_NetworkHullBitVec& bitVec, int bit, bool value)
{
	if (bit < 0 || bit >= bitVec.size)
		return;

	uint32_t* data = bitVec.pData ? bitVec.pData : &bitVec.inlineData;
	if (!data)
		return;

	const uint32_t mask = 1u << (bit & 31);
	uint32_t& word = data[bit >> 5];
	if (value)
		word |= mask;
	else
		word &= ~mask;
}

static void SetAINBitVecVectorBit(uintptr_t vectorAddress, int src, int dst, bool value)
{
	int* vectorSize = GetAINBitVecVectorSize(vectorAddress);
	if (!vectorSize || src < 0 || src >= *vectorSize)
		return;

	CAI_NetworkHullBitVec* elements = GetAINBitVecVectorElements(vectorAddress);
	if (!elements)
		return;

	SetAINBitVecBit(elements[src], dst, value);
}

static bool ComputeAINHullReachability(CAI_Node* a, CAI_Node* b, int hull)
{
	if (!a || !b)
		return false;

	auto testConnection = reinterpret_cast<TFOAINCanConnectHullFn>(G_server + 0x36A380);
	return (testConnection(nullptr, a, b, hull) & 1) != 0;
}

static bool GetCachedAINHullReachability(CAI_Node* a, CAI_Node* b, int hull)
{
	if (!a || !b)
		return false;

	int low = a->index;
	int high = b->index;
	CAI_Node* lowNode = a;
	CAI_Node* highNode = b;
	if (low > high)
	{
		std::swap(low, high);
		std::swap(lowNode, highNode);
	}

	if (TestAINBitVecVectorBit(G_server + 0xD416F0, low, high))
		return TestAINBitVecVectorBit(G_server + 0xD41710, low, high);

	const bool reachable = ComputeAINHullReachability(lowNode, highNode, hull);
	SetAINBitVecVectorBit(G_server + 0xD416F0, low, high, true);
	SetAINBitVecVectorBit(G_server + 0xD41710, low, high, reachable);
	return reachable;
}

static void MarkAINPostDynamicLinkHullCache(CAI_Network* aiNetwork)
{
	if (!aiNetwork || !aiNetwork->nodes || aiNetwork->nodecount <= 0)
		return;

	for (int nodeIndex = 0; nodeIndex < aiNetwork->nodecount; ++nodeIndex)
	{
		CAI_Node* node = aiNetwork->nodes[nodeIndex];
		if (!node || !node->links || node->linkcount <= 0)
			continue;

		for (int linkIndex = 0; linkIndex < node->linkcount; ++linkIndex)
		{
			CAI_NodeLink* link = node->links[linkIndex];
			if (!link || !link->flags)
				continue;

			const int src = link->srcId;
			const int dst = link->destId;
			if (src < 0 || dst < 0 || src >= aiNetwork->nodecount || dst >= aiNetwork->nodecount)
				continue;

			SetAINBitVecVectorBit(G_server + 0xD416F0, src, dst, true);
			SetAINBitVecVectorBit(G_server + 0xD41710, src, dst, false);
		}
	}
}

static void ClearAINBitVec(CAI_NetworkHullBitVec* bitVec)
{
	if (!bitVec)
		return;

	if (bitVec->pData)
	{
		memset(bitVec->pData, 0, 4ull * bitVec->numInts);
		return;
	}

	bitVec->inlineData = 0;
}

static void ResizeAndClearAINBitVec(CAI_NetworkHullBitVec* bitVec, int bitCount)
{
	auto resizeBitVec = reinterpret_cast<TFOAINBitVecResizeFn>(G_server + 0x31CE90);
	resizeBitVec(bitVec, static_cast<unsigned int>(std::max(0, bitCount)), 0);
	ClearAINBitVec(bitVec);
}

static void ResizeAndClearAINBitVecVector(uintptr_t vectorAddress, int nodeCount)
{
	auto purgeVector = reinterpret_cast<TFOAINBitVecVectorPurgeFn>(G_server + 0x36BC30);
	auto insertVector = reinterpret_cast<TFOAINBitVecVectorInsertFn>(G_server + 0x36C150);

	purgeVector(reinterpret_cast<void*>(vectorAddress));
	insertVector(reinterpret_cast<void*>(vectorAddress), *GetAINBitVecVectorSize(vectorAddress), nodeCount);

	CAI_NetworkHullBitVec* elements = GetAINBitVecVectorElements(vectorAddress);
	if (!elements)
		return;

	for (int i = 0; i < nodeCount; ++i)
		ResizeAndClearAINBitVec(&elements[i], nodeCount);
}

static void InitializeFullAINBuildTables(CAI_Network* aiNetwork)
{
	const int nodeCount = aiNetwork ? std::max(0, aiNetwork->nodecount) : 0;

	ResizeAndClearAINBitVec(reinterpret_cast<CAI_NetworkHullBitVec*>(G_server + 0xD41750), nodeCount);
	ResizeAndClearAINBitVecVector(G_server + 0xD41730, nodeCount);
	ResizeAndClearAINBitVecVector(G_server + 0xD416D0, nodeCount);
	ResizeAndClearAINBitVecVector(G_server + 0xD416F0, nodeCount);
	ResizeAndClearAINBitVecVector(G_server + 0xD41710, nodeCount);
}

static float AINDistanceSquared(const Vector3f& a, const Vector3f& b)
{
	const float dx = a.x - b.x;
	const float dy = a.y - b.y;
	const float dz = a.z - b.z;
	return ((dz * dz) + (dy * dy)) + (dx * dx);
}
static Vector3f AINNodePosition(const CAI_Node* node)
{
	return node ? node->position : Vector3f{};
}

static Vector3f AINUnkNodePosition(const UnkNodeStruct0* node)
{
	return node ? Vector3f{ node->x, node->y, node->z } : Vector3f{};
}

static unsigned char AINNodePrimarySafety(const CAI_Node* node)
{
	if (!node)
		return 0;
	return static_cast<unsigned char>(node->unk3[0]);
}

static float AINSafetyValue(unsigned char safety)
{
	const float raw = static_cast<float>(safety);
	const float v = (1.0f - (raw * 0.1f)) + (1.0f - (raw * 0.1f));
	return ((v * v) + 1.0f) * v * 0.2f;
}

static CAI_Node* GetAINNodeByArrayIndex(const CAI_Network* aiNetwork, int index);
static CAI_Node* FindAINNodeById(CAI_Network* aiNetwork, int id);

static void SetAINUnkNodeSafety(UnkNodeStruct0* node, unsigned char safety)
{
	if (!node)
		return;

	node->unk5 = static_cast<char>(safety);
	node->unk4 = AINSafetyValue(safety);
}

static void RecomputeAINUnkNodeSafety(const CAI_Network* aiNetwork, UnkNodeStruct0* sideNode)
{
	if (!aiNetwork || !sideNode || !sideNode->unk2 || sideNode->unkcount0 <= 0)
		return;

	std::vector<unsigned char> safeties;
	safeties.reserve(static_cast<size_t>(sideNode->unkcount0));
	for (int i = 0; i < sideNode->unkcount0; ++i)
	{
		CAI_Node* member = GetAINNodeByArrayIndex(aiNetwork, sideNode->unk2[i]);
		if (!member)
			continue;

		if (member->unk2[0] == 2)
			continue;

		safeties.push_back(static_cast<unsigned char>(member->unk3[0]));
	}

	if (safeties.empty())
		return;

	std::sort(safeties.begin(), safeties.end());
	const size_t mid = safeties.size() / 2;
	unsigned char median = safeties[mid];
	if ((safeties.size() & 1) == 0)
		median = static_cast<unsigned char>(floorf(((static_cast<float>(safeties[mid - 1]) + static_cast<float>(safeties[mid])) * 0.5f) + 0.5f));

	SetAINUnkNodeSafety(sideNode, median);
}

static bool AppendAINIntVector(uintptr_t vectorBase, int value)
{
	if (!vectorBase)
		return false;

	auto growVector = reinterpret_cast<void(__fastcall*)(void*, int)>(G_server + 0x0913C0);
	int** elementsPtr = reinterpret_cast<int**>(vectorBase);
	__int64* capacity = reinterpret_cast<__int64*>(vectorBase + 0x8);
	int* count = reinterpret_cast<int*>(vectorBase + 0x18);

	if ((*count + 1) > *capacity)
		growVector(reinterpret_cast<void*>(vectorBase), 1);

	int* elements = *elementsPtr;
	if (!elements)
		return false;

	elements[(*count)++] = value;
	return true;
}

static bool AppendAINUnkNodeMember(UnkNodeStruct0* sideNode, int nodeIndex)
{
	return AppendAINIntVector(reinterpret_cast<uintptr_t>(sideNode) + 0x18, nodeIndex);
}

static void ResetAINUnkNodeTail(CAI_Network* aiNetwork)
{
	auto resetUnkNodes = reinterpret_cast<void(__fastcall*)()>(G_server + 0x36AF80);
	resetUnkNodes();

	if (!aiNetwork || !aiNetwork->nodes)
		return;

	for (int i = 0; i < aiNetwork->nodecount; ++i)
	{
		if (aiNetwork->nodes[i])
			aiNetwork->nodes[i]->unk6 = -1;
	}
}

static bool AppendAINUnkNodeTailRecord(UnkNodeStruct0* sideNode)
{
	if (!sideNode || !pUnkStruct0Count || !pppUnkNodeStruct0s)
		return false;

	auto growUnkNodeArray = reinterpret_cast<void(__fastcall*)(__int64, __int64)>(G_server + 0x36BD90);
	__int64* capacity = reinterpret_cast<__int64*>(G_server + 0xD41AD8);
	const int count = *pUnkStruct0Count;
	const __int64 needed = static_cast<__int64>(count) + 1 - *capacity;
	if (needed > 0)
		growUnkNodeArray(0, needed);

	UnkNodeStruct0** table = *pppUnkNodeStruct0s;
	if (!table)
		return false;

	table[count] = sideNode;
	*pUnkStruct0Count = count + 1;
	return true;
}

static UnkNodeStruct0* CreateAINUnkNodeTailRecord(CAI_Node* node)
{
	if (!node || !pUnkStruct0Count)
		return nullptr;

	auto tfoOperatorNew = reinterpret_cast<void*(__fastcall*)(size_t)>(G_server + 0x7068F8);
	void* memory = tfoOperatorNew(sizeof(UnkNodeStruct0));
	if (!memory)
		return nullptr;

	UnkNodeStruct0* sideNode = reinterpret_cast<UnkNodeStruct0*>(memory);
	memset(sideNode, 0, sizeof(*sideNode));

	sideNode->index = *pUnkStruct0Count;
	sideNode->unk1 = 0;
	sideNode->x = node->position.x;
	sideNode->y = node->position.y;
	sideNode->z = node->position.z;
	SetAINUnkNodeSafety(sideNode, AINNodePrimarySafety(node));

	if (!AppendAINUnkNodeMember(sideNode, node->index))
		return nullptr;

	node->unk6 = static_cast<short>(sideNode->index);
	if (!AppendAINUnkNodeTailRecord(sideNode))
		return nullptr;

	return sideNode;
}

static CAI_Node* GetAINNodeByArrayIndex(const CAI_Network* aiNetwork, int index)
{
	if (!aiNetwork || !aiNetwork->nodes || index < 0 || index >= aiNetwork->nodecount)
		return nullptr;

	if (aiNetwork->nodes[index] && aiNetwork->nodes[index]->index == index)
		return aiNetwork->nodes[index];

	for (int i = 0; i < aiNetwork->nodecount; ++i)
	{
		CAI_Node* node = aiNetwork->nodes[i];
		if (node && node->index == index)
			return node;
	}

	return nullptr;
}

static bool AINSideNodeCanAbsorb(const CAI_Network* aiNetwork, const UnkNodeStruct0* sideNode, CAI_Node* node)
{
	if (!aiNetwork || !sideNode || !node)
		return false;

	for (int i = 0; i < sideNode->unkcount0; ++i)
	{
		const int memberIndex = sideNode->unk2 ? sideNode->unk2[i] : -1;
		CAI_Node* member = GetAINNodeByArrayIndex(aiNetwork, memberIndex);
		if (!member)
			return false;

		if (!TestAINBitVecVectorBit(G_server + 0xD416D0, node->index, member->index))
			return false;

		if (node->unk2[0] != member->unk2[0])
			return false;

		if (!GetCachedAINHullReachability(node, member, 0))
			return false;
	}

	return true;
}

static float AINRoundFloat(float value)
{
	volatile float rounded = value;
	return rounded;
}

static float AINAddSideCentroidComponent(float centroid, float nodeValue, int oldCount)
{
	const float oldCountFloat = AINRoundFloat(static_cast<float>(oldCount));
	float value = AINRoundFloat(centroid * oldCountFloat);
	value = AINRoundFloat(nodeValue + value);
	const float newCountFloat = AINRoundFloat(static_cast<float>(oldCount + 1));
	const float inverseCount = AINRoundFloat(1.0f / newCountFloat);
	return AINRoundFloat(value * inverseCount);
}

static float AINRemoveSideCentroidComponent(float centroid, float nodeValue, int oldCount)
{
	const float oldCountFloat = AINRoundFloat(static_cast<float>(oldCount));
	float value = AINRoundFloat(centroid * oldCountFloat);
	value = AINRoundFloat(value - nodeValue);
	const float newCountFloat = AINRoundFloat(static_cast<float>(oldCount - 1));
	const float inverseCount = AINRoundFloat(1.0f / newCountFloat);
	return AINRoundFloat(value * inverseCount);
}

static bool MergeAINUnkNodeTailRecord(const CAI_Network* aiNetwork, UnkNodeStruct0* sideNode, CAI_Node* node)
{
	if (!aiNetwork || !sideNode || !node)
		return false;

	node->unk6 = static_cast<short>(sideNode->index);

	const int oldCount = std::max(0, sideNode->unkcount0);
	sideNode->x = AINAddSideCentroidComponent(sideNode->x, node->position.x, oldCount);
	sideNode->y = AINAddSideCentroidComponent(sideNode->y, node->position.y, oldCount);
	sideNode->z = AINAddSideCentroidComponent(sideNode->z, node->position.z, oldCount);

	if (!AppendAINUnkNodeMember(sideNode, node->index))
		return false;

	RecomputeAINUnkNodeSafety(aiNetwork, sideNode);
	return true;
}

static bool AddAINNodeToSide(CAI_Network* aiNetwork, CAI_Node* node, UnkNodeStruct0* sideNode)
{
	if (!aiNetwork || !node || !sideNode)
		return false;

	node->unk6 = static_cast<short>(sideNode->index);

	const int oldCount = std::max(0, sideNode->unkcount0);
	sideNode->x = AINAddSideCentroidComponent(sideNode->x, node->position.x, oldCount);
	sideNode->y = AINAddSideCentroidComponent(sideNode->y, node->position.y, oldCount);
	sideNode->z = AINAddSideCentroidComponent(sideNode->z, node->position.z, oldCount);

	if (!AppendAINUnkNodeMember(sideNode, node->index))
		return false;

	RecomputeAINUnkNodeSafety(aiNetwork, sideNode);
	return true;
}

static void RemoveAINNodeFromSide(CAI_Network* aiNetwork, CAI_Node* node, UnkNodeStruct0* sideNode)
{
	if (!aiNetwork || !node || !sideNode)
		return;

	node->unk6 = -1;

	const int oldCount = sideNode->unkcount0;
	if (oldCount > 1)
	{
		sideNode->x = AINRemoveSideCentroidComponent(sideNode->x, node->position.x, oldCount);
		sideNode->y = AINRemoveSideCentroidComponent(sideNode->y, node->position.y, oldCount);
		sideNode->z = AINRemoveSideCentroidComponent(sideNode->z, node->position.z, oldCount);
	}

	if (sideNode->unk2 && oldCount > 0)
	{
		for (int i = 0; i < oldCount; ++i)
		{
			if (sideNode->unk2[i] != node->index)
				continue;

			if (i != oldCount - 1)
				sideNode->unk2[i] = sideNode->unk2[oldCount - 1];
			--sideNode->unkcount0;
			break;
		}
	}

	RecomputeAINUnkNodeSafety(aiNetwork, sideNode);
}

static bool BuildAINUnkNodeTail(CAI_Network* aiNetwork)
{
	if (!aiNetwork || !aiNetwork->nodes)
		return false;

	ResetAINUnkNodeTail(aiNetwork);

	for (int i = 0; i < aiNetwork->nodecount; ++i)
	{
		CAI_Node* node = aiNetwork->nodes[i];
		if (!node)
			continue;

		if (node->unk0 != 2 || (static_cast<unsigned int>(node->flags) & 0x80000000u) != 0)
			continue;

		std::vector<int> candidates;
		for (int j = 0; j < aiNetwork->nodecount; ++j)
		{
			if (i == j)
				continue;

			CAI_Node* other = aiNetwork->nodes[j];
			if (!other || other->unk6 == -1)
				continue;

			if (!TestAINBitVecVectorBit(G_server + 0xD416D0, node->index, other->index))
				continue;

			if (std::find(candidates.begin(), candidates.end(), static_cast<int>(other->unk6)) == candidates.end())
				candidates.push_back(static_cast<int>(other->unk6));
		}

		UnkNodeStruct0** table = (pppUnkNodeStruct0s && *pppUnkNodeStruct0s) ? *pppUnkNodeStruct0s : nullptr;
		if (table && candidates.size() > 1)
		{
			const Vector3f nodePosition = AINNodePosition(node);
			std::stable_sort(candidates.begin(), candidates.end(), [table, nodePosition](int a, int b) {
				const UnkNodeStruct0* sideA = (a >= 0) ? table[a] : nullptr;
				const UnkNodeStruct0* sideB = (b >= 0) ? table[b] : nullptr;
				if (!sideA)
					return false;
				if (!sideB)
					return true;

				const float distA = AINDistanceSquared(AINUnkNodePosition(sideA), nodePosition);
				const float distB = AINDistanceSquared(AINUnkNodePosition(sideB), nodePosition);
				return distA < distB;
			});
		}

		bool merged = false;
		for (int candidate : candidates)
		{
			if (!pUnkStruct0Count || candidate < 0 || candidate >= *pUnkStruct0Count || !table)
				continue;

			UnkNodeStruct0* sideNode = table[candidate];
			if (AINSideNodeCanAbsorb(aiNetwork, sideNode, node) && MergeAINUnkNodeTailRecord(aiNetwork, sideNode, node))
			{
				merged = true;
				break;
			}
		}

		if (!merged && !CreateAINUnkNodeTailRecord(node))
		{
			Warning("buildain: failed to create unknown-node-tail record for node %d.\n", node->index);
			return false;
		}
	}

	Msg("buildain: built %d unknown-node-tail records.\n", pUnkStruct0Count ? *pUnkStruct0Count : 0);
	return true;
}

struct AINSideCandidate
{
	int sideIndex;
	float distance;
};

static UnkNodeStruct0* GetAINSideByIndex(int sideIndex)
{
	if (!pUnkStruct0Count || !pppUnkNodeStruct0s || !*pppUnkNodeStruct0s)
		return nullptr;

	if (sideIndex < 0 || sideIndex >= *pUnkStruct0Count)
		return nullptr;

	return (*pppUnkNodeStruct0s)[sideIndex];
}

static void InsertAINSideCandidateSorted(std::vector<AINSideCandidate>& candidates, int sideIndex, float distance)
{
	for (const AINSideCandidate& candidate : candidates)
	{
		if (candidate.sideIndex == sideIndex)
			return;
	}

	AINSideCandidate entry{ sideIndex, distance };
	auto insertAt = std::find_if(candidates.begin(), candidates.end(), [distance](const AINSideCandidate& candidate) {
		return distance < candidate.distance;
	});
	candidates.insert(insertAt, entry);
}

static bool RefineAINUnkNodeTail(CAI_Network* aiNetwork)
{
	if (!aiNetwork || !aiNetwork->nodes || !pUnkStruct0Count)
		return false;

	for (int i = 0; i < aiNetwork->nodecount; ++i)
	{
		CAI_Node* node = aiNetwork->nodes[i];
		if (!node)
			continue;

		if (node->unk0 != 2 || (static_cast<unsigned int>(node->flags) & 0x80000000u) != 0 || node->unk6 == -1)
			continue;

		UnkNodeStruct0* currentSide = GetAINSideByIndex(node->unk6);
		if (!currentSide)
			continue;

		const float currentDistance = AINDistanceSquared(AINUnkNodePosition(currentSide), AINNodePosition(node));
		std::vector<AINSideCandidate> candidates;
		for (int j = 0; j < aiNetwork->nodecount; ++j)
		{
			if (i == j)
				continue;

			CAI_Node* other = aiNetwork->nodes[j];
			if (!other || other->unk6 == -1 || other->unk6 == node->unk6)
				continue;

			if (!TestAINBitVecVectorBit(G_server + 0xD416D0, node->index, other->index))
				continue;

			UnkNodeStruct0* otherSide = GetAINSideByIndex(other->unk6);
			if (!otherSide)
				continue;

			const float otherDistance = AINDistanceSquared(AINUnkNodePosition(otherSide), AINNodePosition(node));
			if (otherDistance < currentDistance)
				InsertAINSideCandidateSorted(candidates, otherSide->index, otherDistance);
		}

		for (const AINSideCandidate& candidate : candidates)
		{
			UnkNodeStruct0* targetSide = GetAINSideByIndex(candidate.sideIndex);
			currentSide = GetAINSideByIndex(node->unk6);
			if (!targetSide || !currentSide)
				continue;

			if (!AINSideNodeCanAbsorb(aiNetwork, targetSide, node))
				continue;

			RemoveAINNodeFromSide(aiNetwork, node, currentSide);
			if (!AddAINNodeToSide(aiNetwork, node, targetSide))
			{
				Warning("buildain: failed to move node %d into unknown-node-tail record %d.\n", node->index, targetSide->index);
				return false;
			}
			break;
		}
	}

	Msg("buildain: refined %d unknown-node-tail records.\n", pUnkStruct0Count ? *pUnkStruct0Count : 0);
	return true;
}

static int AINLinkOtherNodeId(const CAI_NodeLink* link, int nodeId)
{
	if (!link)
		return -1;
	if (link->srcId == nodeId)
		return link->destId;
	if (link->destId == nodeId)
		return link->srcId;
	return -1;
}

static CAI_NodeLink* FindAINAnyLinkBetween(CAI_Node* node, int otherNodeId)
{
	if (!node || !node->links)
		return nullptr;

	for (int i = 0; i < node->linkcount; ++i)
	{
		CAI_NodeLink* link = node->links[i];
		if (AINLinkOtherNodeId(link, node->index) == otherNodeId)
			return link;
	}

	return nullptr;
}

static int FindAINSideMemberLocalIndex(const std::vector<int>& members, int nodeId)
{
	for (int i = 0; i < static_cast<int>(members.size()); ++i)
	{
		if (members[i] == nodeId)
			return i;
	}

	return -1;
}

static bool AINSideMatrixIsConnected(const std::vector<std::vector<unsigned char>>& matrix)
{
	const int count = static_cast<int>(matrix.size());
	if (count <= 1)
		return true;

	for (int i = 0; i < count; ++i)
	{
		for (int j = 0; j < count; ++j)
		{
			if (!matrix[i][j])
				return false;
		}
	}

	return true;
}

static void CloseAINSideConnectivityMatrix(std::vector<std::vector<unsigned char>>& matrix)
{
	const int count = static_cast<int>(matrix.size());
	for (int k = 0; k < count; ++k)
	{
		for (int i = 0; i < count; ++i)
		{
			if (!matrix[i][k])
				continue;

			for (int j = 0; j < count; ++j)
			{
				if (matrix[k][j])
					matrix[i][j] = 1;
			}
		}
	}
}

static bool UpdateAINSideConnectivityRow(std::vector<std::vector<unsigned char>>& matrix, int row, int other)
{
	if (row < 0 || other < 0 || row >= static_cast<int>(matrix.size()) || other >= static_cast<int>(matrix.size()))
		return false;

	if (matrix[row][other] && matrix[other][row])
	{
		for (int i = 0; i < static_cast<int>(matrix.size()); ++i)
		{
			if (matrix[other][i])
				matrix[row][i] = 1;
		}
	}

	for (int i = 0; i < static_cast<int>(matrix.size()); ++i)
	{
		if (!matrix[row][i])
			return false;
	}

	return true;
}

static std::vector<int> SnapshotAINSideMembers(const UnkNodeStruct0* sideNode)
{
	std::vector<int> members;
	if (!sideNode || !sideNode->unk2 || sideNode->unkcount0 <= 0)
		return members;

	members.reserve(static_cast<size_t>(sideNode->unkcount0));
	for (int i = 0; i < sideNode->unkcount0; ++i)
		members.push_back(sideNode->unk2[i]);
	return members;
}

static std::vector<std::vector<unsigned char>> BuildAINSideConnectivityMatrix(CAI_Network* aiNetwork, const std::vector<int>& members)
{
	const int count = static_cast<int>(members.size());
	std::vector<std::vector<unsigned char>> matrix(count, std::vector<unsigned char>(count, 0));
	for (int i = 0; i < count; ++i)
		matrix[i][i] = 1;

	for (int i = 0; i < count; ++i)
	{
		CAI_Node* node = FindAINNodeById(aiNetwork, members[i]);
		if (!node || !node->links || node->linkcount <= 0)
			continue;

		for (int linkIndex = 0; linkIndex < node->linkcount; ++linkIndex)
		{
			CAI_NodeLink* link = node->links[linkIndex];
			if (!link || sub_35FBB0(link, 0))
				continue;

			const int otherNodeId = AINLinkOtherNodeId(link, node->index);
			const int otherLocalIndex = FindAINSideMemberLocalIndex(members, otherNodeId);
			if (otherLocalIndex < 0)
				continue;

			matrix[i][otherLocalIndex] = 1;
			matrix[otherLocalIndex][i] = 1;
		}
	}

	CloseAINSideConnectivityMatrix(matrix);
	return matrix;
}

static bool CreateAINRepairGroundLink(CAI_Network* aiNetwork, int srcNodeId, int dstNodeId)
{
	if (!aiNetwork)
		return false;

	auto createLink = reinterpret_cast<TFOAINCreateLinkFn>(G_server + 0x363950);
	CAI_NodeLink* link = createLink(aiNetwork, srcNodeId, dstNodeId);
	if (!link)
		return false;

	link->hulls[0] = 1;
	for (int hull = 1; hull < MAX_HULLS; ++hull)
		link->hulls[hull] = 0;
	link->unk0 = 0;
	link->unk1 = 0;
	memset(link->unk2, 0, sizeof(link->unk2));
	link->flags = 0;
	return true;
}

static bool MoveAINNodeToClosestLinkedSide(CAI_Network* aiNetwork, int nodeId)
{
	CAI_Node* node = FindAINNodeById(aiNetwork, nodeId);
	if (!node || !node->links || node->linkcount <= 0)
		return false;

	const int originalSideIndex = node->unk6;
	UnkNodeStruct0* originalSide = GetAINSideByIndex(originalSideIndex);
	if (!originalSide)
		return false;

	int bestSideIndex = -1;
	float bestDistance = 3.402823466e+38F;
	for (int i = 0; i < node->linkcount; ++i)
	{
		CAI_NodeLink* link = node->links[i];
		if (!link || (link->hulls[0] & 1) == 0)
			continue;

		CAI_Node* otherNode = FindAINNodeById(aiNetwork, AINLinkOtherNodeId(link, node->index));
		if (!otherNode || otherNode->unk6 == -1 || otherNode->unk6 == originalSideIndex || otherNode->unk6 == bestSideIndex)
			continue;

		UnkNodeStruct0* otherSide = GetAINSideByIndex(otherNode->unk6);
		if (!otherSide)
			continue;

		const float distance = AINDistanceSquared(AINUnkNodePosition(otherSide), AINNodePosition(node));
		if (distance < bestDistance)
		{
			bestDistance = distance;
			bestSideIndex = otherNode->unk6;
		}
	}

	UnkNodeStruct0* bestSide = GetAINSideByIndex(bestSideIndex);
	if (!bestSide)
		return false;

	RemoveAINNodeFromSide(aiNetwork, node, originalSide);
	if (AddAINNodeToSide(aiNetwork, node, bestSide))
		return true;

	AddAINNodeToSide(aiNetwork, node, originalSide);
	return false;
}

static bool RepairAINDisconnectedSide(CAI_Network* aiNetwork, UnkNodeStruct0* sideNode, int& linksAdded, int& nodesMoved, int& sidesFlagged)
{
	if (!aiNetwork || !sideNode || sideNode->unkcount0 <= 1)
		return true;

	const std::vector<int> members = SnapshotAINSideMembers(sideNode);
	if (members.size() <= 1)
		return true;

	std::vector<std::vector<unsigned char>> matrix = BuildAINSideConnectivityMatrix(aiNetwork, members);
	if (AINSideMatrixIsConnected(matrix))
		return true;

	std::vector<int> connectedCounts(members.size(), 0);
	for (int i = 0; i < static_cast<int>(members.size()); ++i)
	{
		CAI_Node* node = FindAINNodeById(aiNetwork, members[i]);
		if (!node || node->flags < 0)
			continue;

		for (int j = 0; j < static_cast<int>(members.size()); ++j)
		{
			if (i == j)
				continue;

			if (matrix[i][j])
			{
				++connectedCounts[i];
				continue;
			}

			CAI_Node* otherNode = FindAINNodeById(aiNetwork, members[j]);
			if (!otherNode || otherNode->flags < 0)
				continue;

			CAI_NodeLink* existingLink = FindAINAnyLinkBetween(node, otherNode->index);
			const bool reachable = existingLink && sub_35FBB0(existingLink, 0)
				? false
				: GetCachedAINHullReachability(node, otherNode, 0);
			if (!reachable)
				continue;

			++connectedCounts[i];
			if (CreateAINRepairGroundLink(aiNetwork, node->index, otherNode->index))
				++linksAdded;

			matrix[i][j] = 1;
			matrix[j][i] = 1;
			if (UpdateAINSideConnectivityRow(matrix, i, j))
				return true;
		}
	}

	int bestLocalIndex = 0;
	int bestConnectedCount = 0;
	for (int i = 0; i < static_cast<int>(connectedCounts.size()); ++i)
	{
		if (connectedCounts[i] > bestConnectedCount)
		{
			bestConnectedCount = connectedCounts[i];
			bestLocalIndex = i;
		}
	}

	for (int i = static_cast<int>(members.size()) - 1; i >= 0; --i)
	{
		if (!matrix[bestLocalIndex][i])
		{
			if (MoveAINNodeToClosestLinkedSide(aiNetwork, members[i]))
				++nodesMoved;
		}
	}

	const std::vector<int> repairedMembers = SnapshotAINSideMembers(sideNode);
	std::vector<std::vector<unsigned char>> repairedMatrix = BuildAINSideConnectivityMatrix(aiNetwork, repairedMembers);
	if (!AINSideMatrixIsConnected(repairedMatrix))
	{
		sideNode->unk1 |= 1;
		++sidesFlagged;
	}

	return true;
}

static bool RepairAINPostRefineSideConnectivity(CAI_Network* aiNetwork)
{
	if (!aiNetwork || !pUnkStruct0Count || !pppUnkNodeStruct0s || !*pppUnkNodeStruct0s)
		return false;

	int linksAdded = 0;
	int nodesMoved = 0;
	int sidesFlagged = 0;
	for (int i = 0; i < *pUnkStruct0Count; ++i)
	{
		UnkNodeStruct0* sideNode = (*pppUnkNodeStruct0s)[i];
		if (!RepairAINDisconnectedSide(aiNetwork, sideNode, linksAdded, nodesMoved, sidesFlagged))
			return false;
	}

	Msg("buildain: repaired unknown-node-tail connectivity (links=%d moved=%d flagged=%d).\n", linksAdded, nodesMoved, sidesFlagged);
	return true;
}

static void ResetAINUnkLinkTail(CAI_Network* aiNetwork)
{
	auto resetUnkLinks = reinterpret_cast<void(__fastcall*)()>(G_server + 0x36AEF0);
	resetUnkLinks();

	if (!aiNetwork || !pUnkStruct0Count || !pppUnkNodeStruct0s || !*pppUnkNodeStruct0s)
		return;

	for (int i = 0; i < *pUnkStruct0Count; ++i)
	{
		UnkNodeStruct0* sideNode = (*pppUnkNodeStruct0s)[i];
		if (sideNode)
			sideNode->unkcount1 = 0;
	}
}

static UnkLinkStruct1* GetAINUnkLinkByIndex(int linkIndex)
{
	if (!pUnkLinkStruct1Count || !pppUnkStruct1s || !*pppUnkStruct1s)
		return nullptr;

	if (linkIndex < 0 || linkIndex >= *pUnkLinkStruct1Count)
		return nullptr;

	return (*pppUnkStruct1s)[linkIndex];
}

static bool AINSideHasLinkRef(const UnkNodeStruct0* sideNode, int linkIndex)
{
	if (!sideNode || !sideNode->unk3 || sideNode->unkcount1 <= 0)
		return false;

	for (int i = 0; i < sideNode->unkcount1; ++i)
	{
		if (sideNode->unk3[i] == linkIndex)
			return true;
	}

	return false;
}

static bool AppendAINSideLinkRef(UnkNodeStruct0* sideNode, int linkIndex)
{
	if (!sideNode)
		return false;

	if (AINSideHasLinkRef(sideNode, linkIndex))
		return true;

	return AppendAINIntVector(reinterpret_cast<uintptr_t>(sideNode) + 0x38, linkIndex);
}

static float AINLinkDistance(const UnkNodeStruct0* a, const UnkNodeStruct0* b)
{
	const Vector3f from = AINUnkNodePosition(a);
	const Vector3f to = AINUnkNodePosition(b);
	const float dx = to.x - from.x;
	const float dy = to.y - from.y;
	const float dz = to.z - from.z;
	const float sum = ((dx * dx) + (dy * dy)) + (dz * dz);
	return sqrtf(sum);
}

static UnkLinkStruct1* FindAINUnkLinkRecord(UnkNodeStruct0* a, UnkNodeStruct0* b, int* outIndex = nullptr)
{
	if (!a || !b || !pUnkLinkStruct1Count || !pppUnkStruct1s || !*pppUnkStruct1s)
		return nullptr;

	for (int i = 0; i < *pUnkLinkStruct1Count; ++i)
	{
		UnkLinkStruct1* link = (*pppUnkStruct1s)[i];
		if (!link)
			continue;

		if ((link->unk0 == a->index && link->unk1 == b->index) || (link->unk0 == b->index && link->unk1 == a->index))
		{
			if (outIndex)
				*outIndex = i;
			return link;
		}
	}

	return nullptr;
}

static bool AppendAINUnkLinkTailRecord(UnkLinkStruct1* linkRecord)
{
	if (!linkRecord || !pUnkLinkStruct1Count || !pppUnkStruct1s)
		return false;

	auto growUnkLinkArray = reinterpret_cast<void(__fastcall*)(__int64, __int64)>(G_server + 0x36B460);
	__int64* capacity = reinterpret_cast<__int64*>(G_server + 0xD41AF8);
	const int count = *pUnkLinkStruct1Count;
	const __int64 needed = static_cast<__int64>(count) + 1;
	if (capacity && *capacity < needed)
		growUnkLinkArray(0, needed);

	UnkLinkStruct1** table = *pppUnkStruct1s;
	if (!table)
		return false;

	table[count] = linkRecord;
	*pUnkLinkStruct1Count = count + 1;
	return true;
}

static bool CreateAINUnkLinkTailRecord(UnkNodeStruct0* fromSide, UnkNodeStruct0* toSide, bool oneWay, unsigned char moveFlags)
{
	if (!fromSide || !toSide || !pUnkLinkStruct1Count)
		return false;

	auto tfoOperatorNew = reinterpret_cast<void*(__fastcall*)(size_t)>(G_server + 0x7068F8);
	void* memory = tfoOperatorNew(sizeof(UnkLinkStruct1));
	if (!memory)
		return false;

	UnkLinkStruct1* linkRecord = reinterpret_cast<UnkLinkStruct1*>(memory);
	memset(linkRecord, 0, sizeof(*linkRecord));
	linkRecord->unk0 = static_cast<short>(fromSide->index);
	linkRecord->unk1 = static_cast<short>(toSide->index);

	linkRecord->unk2 = AINLinkDistance(fromSide, toSide);
	linkRecord->unk3 = 0;
	linkRecord->unk4 = static_cast<char>(moveFlags);
	linkRecord->unk5 = oneWay ? 0 : static_cast<char>(moveFlags);

	const int linkIndex = *pUnkLinkStruct1Count;
	if (!AppendAINUnkLinkTailRecord(linkRecord))
		return false;

	if (!AppendAINSideLinkRef(fromSide, linkIndex))
		return false;

	if (!oneWay && !AppendAINSideLinkRef(toSide, linkIndex))
		return false;

	return true;
}

static void ApplyAINUnkLinkMoveFlags(UnkLinkStruct1* linkRecord, UnkNodeStruct0* fromSide, unsigned char moveFlags)
{
	if (!linkRecord || !fromSide)
		return;

	if (linkRecord->unk0 == fromSide->index)
		linkRecord->unk4 |= static_cast<char>(moveFlags);
	else if (linkRecord->unk1 == fromSide->index)
		linkRecord->unk5 |= static_cast<char>(moveFlags);
}
static int AINUnkLinkOtherSideIndex(const UnkLinkStruct1* linkRecord, int sideIndex)
{
	if (!linkRecord)
		return -1;
	return (linkRecord->unk0 == sideIndex) ? linkRecord->unk1 : linkRecord->unk0;
}

static bool AINSideHasDirectedLinkTo(const UnkNodeStruct0* fromSide, const UnkNodeStruct0* toSide)
{
	if (!fromSide || !toSide || !fromSide->unk3 || fromSide->unkcount1 <= 0 || !pppUnkStruct1s || !*pppUnkStruct1s)
		return false;

	for (int i = 0; i < fromSide->unkcount1; ++i)
	{
		const int linkIndex = fromSide->unk3[i];
		if (linkIndex < 0 || (pUnkLinkStruct1Count && linkIndex >= *pUnkLinkStruct1Count))
			continue;

		UnkLinkStruct1* link = (*pppUnkStruct1s)[linkIndex];
		if (!link || link->unk5 == 0)
			continue;

		if (AINUnkLinkOtherSideIndex(link, fromSide->index) == toSide->index)
			return true;
	}

	return false;
}

static Vector3f AINVectorBetweenSides(const UnkNodeStruct0* fromSide, const UnkNodeStruct0* toSide)
{
	const Vector3f from = AINUnkNodePosition(fromSide);
	const Vector3f to = AINUnkNodePosition(toSide);
	return {to.x - from.x, to.y - from.y, to.z - from.z};
}

static float AINVectorLength(const Vector3f& vector)
{
	return sqrtf(((vector.x * vector.x) + (vector.y * vector.y)) + (vector.z * vector.z));
}

static void MarkAINRedundantSideLinks(UnkNodeStruct0* sideNode)
{
	if (!sideNode || !sideNode->unk3 || sideNode->unkcount1 <= 0 || !pppUnkStruct1s || !*pppUnkStruct1s)
		return;

	constexpr float kRedundantLinkCosine = 0.9238795f;

	for (int i = 0; i < sideNode->unkcount1; ++i)
	{
		const int linkIndex = sideNode->unk3[i];
		if (linkIndex < 0 || (pUnkLinkStruct1Count && linkIndex >= *pUnkLinkStruct1Count))
			continue;

		UnkLinkStruct1* link = (*pppUnkStruct1s)[linkIndex];
		if (!link || (link->unk3 & 1) != 0)
			continue;

		UnkNodeStruct0* linkedSide = GetAINSideByIndex(AINUnkLinkOtherSideIndex(link, sideNode->index));
		if (!linkedSide)
			continue;

		const Vector3f linkVector = AINVectorBetweenSides(sideNode, linkedSide);
		const float linkLength = AINVectorLength(linkVector);
		if (linkLength <= 0.0f)
			continue;

		const Vector3f linkUnit{linkVector.x / linkLength, linkVector.y / linkLength, linkVector.z / linkLength};

		for (int j = 0; j < sideNode->unkcount1; ++j)
		{
			const int otherLinkIndex = sideNode->unk3[j];
			if (otherLinkIndex == linkIndex || otherLinkIndex < 0 || (pUnkLinkStruct1Count && otherLinkIndex >= *pUnkLinkStruct1Count))
				continue;

			UnkLinkStruct1* otherLink = (*pppUnkStruct1s)[otherLinkIndex];
			if (!otherLink || (otherLink->unk3 & 1) != 0)
				continue;

			UnkNodeStruct0* otherSide = GetAINSideByIndex(AINUnkLinkOtherSideIndex(otherLink, sideNode->index));
			if (!otherSide || !AINSideHasDirectedLinkTo(otherSide, linkedSide))
				continue;

			const Vector3f otherVector = AINVectorBetweenSides(sideNode, otherSide);
			const float otherLength = AINVectorLength(otherVector);
			if (otherLength <= 0.0f)
				continue;

			const float dot = ((otherVector.x / otherLength) * linkUnit.x)
				+ ((otherVector.y / otherLength) * linkUnit.y)
				+ ((otherVector.z / otherLength) * linkUnit.z);

			if (dot < kRedundantLinkCosine)
				continue;

			if (otherLength >= linkLength)
				otherLink->unk3 |= 1;
			else
			{
				link->unk3 |= 1;
				break;
			}
		}
	}
}


static bool BuildAINUnkLinkTail(CAI_Network* aiNetwork)
{
	if (!aiNetwork || !aiNetwork->nodes || !pUnkStruct0Count)
		return false;

	ResetAINUnkLinkTail(aiNetwork);

	UnkNodeStruct0** sideTable = (pppUnkNodeStruct0s && *pppUnkNodeStruct0s) ? *pppUnkNodeStruct0s : nullptr;
	if (!sideTable)
		return false;

	const int sideCount = *pUnkStruct0Count;
	int operationBudget = std::max(10000, aiNetwork->nodecount * aiNetwork->nodecount * 16);

	for (int sideIndex = 0; sideIndex < sideCount; ++sideIndex)
	{
		UnkNodeStruct0* sideNode = sideTable[sideIndex];
		if (!sideNode || !sideNode->unk2 || sideNode->unkcount0 <= 0)
			continue;

		for (int memberIndex = 0; memberIndex < sideNode->unkcount0; ++memberIndex)
		{
			if (--operationBudget <= 0)
			{
				Warning("buildain: unknown-link-tail operation budget exhausted at side %d member %d.\n", sideNode->index, memberIndex);
				return false;
			}

			CAI_Node* node = FindAINNodeById(aiNetwork, sideNode->unk2[memberIndex]);
			if (!node || !node->links || node->linkcount <= 0)
				continue;

			if (node->linkcount > 4096)
			{
				Warning("buildain: refusing unknown-link-tail node %d with implausible linkcount %d.\n", node->index, node->linkcount);
				return false;
			}

			for (int linkIndex = 0; linkIndex < node->linkcount; ++linkIndex)
			{
				if (--operationBudget <= 0)
				{
					Warning("buildain: unknown-link-tail operation budget exhausted at side %d node %d link %d.\n", sideNode->index, node->index, linkIndex);
					return false;
				}

				CAI_NodeLink* nodeLink = node->links[linkIndex];
				if (!nodeLink)
					continue;

				const unsigned char moveFlags = nodeLink->hulls[0];
				if ((moveFlags & 0x61) == 0)
					continue;

				const int otherNodeId = (nodeLink->srcId == node->index) ? nodeLink->destId : nodeLink->srcId;
				CAI_Node* otherNode = FindAINNodeById(aiNetwork, otherNodeId);
				if (!otherNode || otherNode->unk6 == -1 || otherNode->unk6 == sideNode->index)
					continue;

				UnkNodeStruct0* otherSide = GetAINSideByIndex(otherNode->unk6);
				if (!otherSide)
					continue;

				const bool oneWay = sub_35FBB0(nodeLink, 0);
				if (oneWay && nodeLink->srcId != node->index)
					continue;

				int existingIndex = -1;
				UnkLinkStruct1* existing = FindAINUnkLinkRecord(sideNode, otherSide, &existingIndex);
				if (existing)
				{
					if (!oneWay && !AppendAINSideLinkRef(sideNode, existingIndex))
						return false;
					ApplyAINUnkLinkMoveFlags(existing, sideNode, moveFlags);
					continue;
				}

				if (!CreateAINUnkLinkTailRecord(sideNode, otherSide, oneWay, moveFlags))
				{
					Warning("buildain: failed to create unknown-link-tail record %d -> %d.\n", sideNode->index, otherSide->index);
					return false;
				}
			}
		}
		MarkAINRedundantSideLinks(sideNode);
	}

	Msg("buildain: built %d unknown-link-tail records.\n", pUnkLinkStruct1Count ? *pUnkLinkStruct1Count : 0);
	return true;
}

static uintptr_t GetAINNetworkEditOps()
{
	uintptr_t manager = *reinterpret_cast<uintptr_t*>(G_server + 0x0C31898);
	return manager ? *reinterpret_cast<uintptr_t*>(manager + 1592) : 0;
}

static void AssignAINGeneratedWCNodeIndex(CAI_Node* node)
{
	if (!node)
		return;

	uintptr_t editOps = GetAINNetworkEditOps();
	if (!editOps)
		return;

	int* nextIndex = reinterpret_cast<int*>(editOps);
	int* nodeIndexTable = *reinterpret_cast<int**>(editOps + 24);
	if (!nextIndex || !nodeIndexTable)
		return;

	nodeIndexTable[node->index] = (*nextIndex)++;
}

static const char* GetAINEntityClassname(void* entity)
{
	return entity ? *reinterpret_cast<const char**>(reinterpret_cast<uintptr_t>(entity) + 128) : nullptr;
}

static const Vector3f* GetAINEntityOrigin(void* entity)
{
	return entity ? reinterpret_cast<const Vector3f*>(reinterpret_cast<uintptr_t>(entity) + 744) : nullptr;
}

static const Vector3f* GetAINEntityAngles(void* entity)
{
	return entity ? reinterpret_cast<const Vector3f*>(reinterpret_cast<uintptr_t>(entity) + 756) : nullptr;
}

static bool IsAINPathPointClassname(const char* classname)
{
	return classname
		&& (strcmp(classname, "path_search_point") == 0 || strcmp(classname, "path_patrol_point") == 0);
}

static bool IsAINTraverseClassname(const char* classname)
{
	return classname && strcmp(classname, "traverse") == 0;
}

static bool IsAINIndoorAreaClassname(const char* classname)
{
	return classname && strcmp(classname, "trigger_indoor_area") == 0;
}

static bool HasAINNodeNearPathPoint(const CAI_Network* aiNetwork, const Vector3f& origin)
{
	if (!aiNetwork || !aiNetwork->nodes)
		return true;

	for (int i = 0; i < aiNetwork->nodecount; ++i)
	{
		const CAI_Node* node = aiNetwork->nodes[i];
		if (!node)
			continue;

		const float dx = origin.x - node->position.x;
		const float dy = origin.y - node->position.y;
		const float dz = origin.z - node->position.z;
		if ((dx * dx) + (dy * dy) < 256.0f && (dz * dz) < 10000.0f)
			return true;
	}

	return false;
}

static void RunFullAINPathPointPass(CAI_Network* aiNetwork, TFOAINNodeStageFn initGroundNodePosition)
{
	if (!aiNetwork || !initGroundNodePosition)
		return;

	auto nextEntity = reinterpret_cast<TFOEntityIteratorFn>(G_server + 0x3D3EB0);
	auto createNode = reinterpret_cast<TFOAINCreateNodeFn>(G_server + 0x363880);
	auto recalcAbsTransform = reinterpret_cast<TFORecalcAbsTransformFn>(G_server + 0x3B9630);

	for (void* entity = nextEntity(nullptr, nullptr); entity; entity = nextEntity(nullptr, entity))
	{
		if (!IsAINPathPointClassname(GetAINEntityClassname(entity)))
			continue;

		uintptr_t entityAddress = reinterpret_cast<uintptr_t>(entity);
		if ((*reinterpret_cast<unsigned int*>(entityAddress + 352) & 0x800) != 0)
			recalcAbsTransform(entity);

		const Vector3f* origin = GetAINEntityOrigin(entity);
		if (!origin || HasAINNodeNearPathPoint(aiNetwork, *origin))
			continue;

		CAI_Node* node = createNode(aiNetwork, origin, 0.0f);
		if (!node)
			continue;

		node->unk0 = 2;
		node->flags = 0;
		*reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(node) + 136) = nullptr;
		AssignAINGeneratedWCNodeIndex(node);
		initGroundNodePosition(nullptr, aiNetwork, node);
	}
}

struct AINTraverseBuildType
{
	int type;
	Vector3f firstLocal;
	Vector3f secondLocal;
	unsigned char linkHull;
	signed char linkInfo;
	bool flipSecondYaw;
};

static const AINTraverseBuildType* FindAINTraverseBuildType(int traverseType)
{
	// Respawn stores these as per-traverse-type local offsets. R1O LTO removed
	// the fresh-build wrapper, so keep the table here and still use native node
	// allocation/ground init/link allocation for the stateful parts.
	static const AINTraverseBuildType types[] = {
		{ 0,  { -100.92405f,   1.69949f, -32.97f }, {  100.92405f,  -1.69951f, -32.97f }, 0x20,  0, true },
		{ 2,  {    4.41015f,  -0.98829f,   0.03f }, {  105.77455f,   2.99434f, -71.97f }, 0x20,  0, false },
		{ 4,  {   -0.46342f,  -0.54194f,   0.03f }, {  130.77208f,   3.08706f, -167.97f }, 0x20, -1, false },
		{ 8,  {   -0.84863f,  -0.39280f,   0.03f }, {   79.00417f,  -5.99709f, -191.97f }, 0x40,  0, false },
		{ 9,  {  -16.17186f, -15.96875f,   0.03f }, {  112.18557f,  -5.36205f, -421.97f }, 0x40,  0, false },
		{ 12, {   -0.50146f,   5.59375f,   0.03f }, {  560.00427f,   5.12329f, -127.97f }, 0x40,  0, false },
		{ 13, {   -0.51072f,   1.24412f,   0.03f }, { 1071.91045f,  14.92080f, -243.97f }, 0x40,  0, false },
	};

	for (const AINTraverseBuildType& type : types)
	{
		if (type.type == traverseType)
			return &type;
	}

	return nullptr;
}


static Vector3f TransformAINTraverseLocalOffset(const Vector3f& origin, const Vector3f& angles, const Vector3f& local)
{
	const float yawRadians = angles.y * 0.01745329251994329576923690768489f;
	const float c = cosf(yawRadians);
	const float s = sinf(yawRadians);

	return Vector3f{
		origin.x + (c * local.x) - (s * local.y),
		origin.y + (s * local.x) + (c * local.y),
		origin.z + local.z
	};
}
static void ResetAINTraverseNodes()
{
	if (pTraverseNodeCount)
		*pTraverseNodeCount = 0;
}

static int AppendAINTraverseNodeRecord(const Vector3f& origin, float yaw, int traverseType)
{
	if (!pTraverseNodeCount || !ppTraverseNodes)
		return -1;

	auto growTraverseNodes = reinterpret_cast<TFOAINTraverseGrowFn>(G_server + 0x36D610);
	__int64* capacity = reinterpret_cast<__int64*>(G_server + 0xD41AA8);
	const int index = *pTraverseNodeCount;
	if (capacity && index >= *capacity)
		growTraverseNodes(0, 1);

	CAI_TraverseNodeDisk* records = *ppTraverseNodes;
	if (!records)
		return -1;

	records[index].quat[0] = origin.x;
	records[index].quat[1] = origin.y;
	records[index].quat[2] = origin.z;
	records[index].quat[3] = yaw;
	records[index].index = traverseType;
	*pTraverseNodeCount = index + 1;
	return index;
}

static CAI_Node* CreateAINTraverseEndpointNode(
	CAI_Network* aiNetwork,
	const Vector3f& position,
	float verticalOffset,
	float yaw,
	int traverseRecordIndex,
	int flags,
	int traverseType,
	TFOAINCreateNodeFn createNode)
{
	if (!aiNetwork || !createNode)
		return nullptr;

	const float traverseVerticalSlack = (traverseType == 0) ? 8.0f : 0.0f;
	float traceHalfRange = fabsf(verticalOffset) - traverseVerticalSlack;
	if (traceHalfRange <= 32.0f)
		traceHalfRange = 32.0f;

	float traceTopZ = position.z + 0.1f;
	if (verticalOffset <= 100.0f)
		traceTopZ += traceHalfRange;
	else
		traceTopZ += traceHalfRange * 0.5f;

	Vector3f traceStart{ position.x, position.y, traceTopZ };
	Vector3f traceEnd{ position.x, position.y, traceTopZ - (traceHalfRange * 2.0f) };

	const Vector3f* groundMins = GetAINBuildHullMins(0);
	const Vector3f* groundMaxs = GetAINBuildHullMaxs(0);
	if (!groundMins || !groundMaxs)
		return nullptr;

	char traceResult[AIN_TRACE_RESULT_SIZE] = {};
	if (!TraceAINBuildHull(traceStart, traceEnd, *groundMins, *groundMaxs, traceResult))
		return nullptr;

	Vector3f nodePosition = traceStart;
	const Vector3f firstTraceEnd = AINTraceEndPosition(traceResult);
	const float firstFraction = AINTraceRawFraction(traceResult);
	const unsigned char firstSurfaceFlags = AINTraceSurfaceFlags(traceResult);
	const bool clearedEnough = fabsf(traceTopZ - firstTraceEnd.z) > (traceHalfRange * 0.1f);
	if (firstFraction < 1.0f)
		nodePosition = firstTraceEnd;

	CAI_Node* node = createNode(aiNetwork, &nodePosition, yaw);
	if (!node)
		return nullptr;

	node->unk0 = 2;
	node->flags = flags;
	*reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(node) + 136) = nullptr;
	*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(node) + 148) = traverseRecordIndex;
	AssignAINGeneratedWCNodeIndex(node);

	if (firstFraction == 1.0f || (firstSurfaceFlags & 4) != 0)
		node->flags |= 0x80000000u;

	for (int hull = 0; hull < MAX_HULLS; ++hull)
	{
		const Vector3f* mins = GetAINBuildHullMins(hull);
		const Vector3f* maxs = GetAINBuildHullMaxs(hull);
		if (!mins || !maxs)
			continue;

		Vector3f hullMaxs = *maxs;
		hullMaxs.z = fmaxf(maxs->x, maxs->y);

		Vector3f hullStart{
			node->position.x,
			node->position.y,
			(node->position.z + 1.0f) - mins->z
		};
		Vector3f hullEnd{
			hullStart.x,
			hullStart.y,
			hullStart.z - 384.0f
		};

		char hullTrace[AIN_TRACE_RESULT_SIZE] = {};
		if (!TraceAINBuildHull(hullStart, hullEnd, *mins, hullMaxs, hullTrace))
			continue;

		const float fraction = AINTraceRawFraction(hullTrace);
		const Vector3f endPosition = AINTraceEndPosition(hullTrace);
		const unsigned char surfaceFlags = AINTraceSurfaceFlags(hullTrace);
		if (fraction != 1.0f && (surfaceFlags & 4) == 0)
		{
			if (!AINTraceStartSolid(hullTrace))
			{
				node->hulls[hull] = (endPosition.z - node->position.z) + 0.1f;
			}
			else
			{
				node->hulls[hull] = 0.1f - mins->z;
				if (hull == 0)
					node->flags |= 0x200000u;
			}
		}
		else if (!clearedEnough)
		{
			node->hulls[hull] = 0.0f;
			if (hull == 0)
				node->flags |= 0x200000u;
		}
		else
		{
			node->hulls[hull] = endPosition.z - traceTopZ;
			if (hull == 0)
				node->flags |= 0x80000000u;
		}
	}

	if ((node->flags & 0x80200000u) != 0)
	{
		node->flags |= 0x100000u;
		return nullptr;
	}

	return node;
}

static void RunFullAINTraverseNodePass(CAI_Network* aiNetwork, TFOAINNodeStageFn initGroundNodePosition)
{
	if (!aiNetwork || !initGroundNodePosition)
		return;

	ResetAINTraverseNodes();

	auto nextEntity = reinterpret_cast<TFOEntityIteratorFn>(G_server + 0x3D3EB0);
	auto createNode = reinterpret_cast<TFOAINCreateNodeFn>(G_server + 0x363880);
	auto createLink = reinterpret_cast<TFOAINCreateLinkFn>(G_server + 0x363950);
	auto recalcAbsTransform = reinterpret_cast<TFORecalcAbsTransformFn>(G_server + 0x3B9630);

	for (void* entity = nextEntity(nullptr, nullptr); entity; entity = nextEntity(nullptr, entity))
	{
		if (!IsAINTraverseClassname(GetAINEntityClassname(entity)))
			continue;

		uintptr_t entityAddress = reinterpret_cast<uintptr_t>(entity);
		if ((*reinterpret_cast<unsigned int*>(entityAddress + 352) & 0x800) != 0)
			recalcAbsTransform(entity);

		const Vector3f* origin = GetAINEntityOrigin(entity);
		const Vector3f* angles = GetAINEntityAngles(entity);
		if (!origin || !angles)
			continue;

		const int traverseType = *reinterpret_cast<int*>(entityAddress + 1592);
		const AINTraverseBuildType* buildType = FindAINTraverseBuildType(traverseType);
		if (!buildType)
		{
			Warning("buildain: unsupported traverseType %d at %.1f %.1f %.1f; skipping traverse node pair.\n",
				traverseType, origin->x, origin->y, origin->z);
			continue;
		}

		const int traverseRecordIndex = AppendAINTraverseNodeRecord(*origin, angles->y, traverseType);
		if (traverseRecordIndex < 0)
			continue;

		const Vector3f firstPosition = TransformAINTraverseLocalOffset(*origin, *angles, buildType->firstLocal);
		const Vector3f secondPosition = TransformAINTraverseLocalOffset(*origin, *angles, buildType->secondLocal);
		const float firstYaw = angles->y;
		const float secondYaw = buildType->flipSecondYaw ? angles->y + 180.0f : angles->y;
		CAI_Node* firstNode = CreateAINTraverseEndpointNode(
			aiNetwork,
			firstPosition,
			origin->z - firstPosition.z,
			firstYaw,
			traverseRecordIndex,
			0x800000,
			traverseType,
			createNode);
		CAI_Node* secondNode = CreateAINTraverseEndpointNode(
			aiNetwork,
			secondPosition,
			origin->z - secondPosition.z,
			secondYaw,
			traverseRecordIndex,
			0x1000000,
			traverseType,
			createNode);

		if (!firstNode || !secondNode || !createLink)
			continue;

		CAI_NodeLink* link = createLink(aiNetwork, firstNode->index, secondNode->index);
		if (!link)
			continue;

		link->hulls[0] = buildType->linkHull;
		for (int hull = 1; hull < MAX_HULLS; ++hull)
			link->hulls[hull] = 0;
		link->unk1 = buildType->linkInfo;
	}
}

static void RunFullAINIndoorAreaPass(CAI_Network* aiNetwork)
{
	if (!aiNetwork || !aiNetwork->nodes)
		return;

	for (int i = 0; i < aiNetwork->nodecount; ++i)
	{
		if (aiNetwork->nodes[i])
			aiNetwork->nodes[i]->flags &= ~0x80000u;
	}

	auto nextEntity = reinterpret_cast<TFOEntityIteratorFn>(G_server + 0x3D3EB0);
	auto recalcAbsTransform = reinterpret_cast<TFORecalcAbsTransformFn>(G_server + 0x3B9630);
	auto triggerContainsPoint = reinterpret_cast<TFOTriggerContainsPointFn>(G_server + 0x242290);
	int triggerCount = 0;
	int markedCount = 0;

	for (void* entity = nextEntity(nullptr, nullptr); entity; entity = nextEntity(nullptr, entity))
	{
		if (!IsAINIndoorAreaClassname(GetAINEntityClassname(entity)))
			continue;

		uintptr_t entityAddress = reinterpret_cast<uintptr_t>(entity);
		if ((*reinterpret_cast<unsigned int*>(entityAddress + 352) & 0x800) != 0)
			recalcAbsTransform(entity);

		++triggerCount;
		for (int i = 0; i < aiNetwork->nodecount; ++i)
		{
			CAI_Node* node = aiNetwork->nodes[i];
			if (!node)
				continue;

			if (!triggerContainsPoint(entity, &node->position))
				continue;

			if ((node->flags & 0x80000u) == 0)
			{
				node->flags |= 0x80000u;
				++markedCount;
			}
		}
	}

	Msg("buildain: marked %d nodes inside %d indoor trigger areas.\n", markedCount, triggerCount);
}

static void PreserveAINFullBuildSpecialLinks(CAI_Node* node)
{
	if (!node)
		return;

	if (!node->links || node->linkcount <= 0)
	{
		node->linkcount = 0;
		return;
	}

	int writeIndex = 0;
	for (int readIndex = 0; readIndex < node->linkcount; ++readIndex)
	{
		CAI_NodeLink* link = node->links[readIndex];
		if (link && (link->hulls[0] & 0x60) != 0)
			node->links[writeIndex++] = link;
	}

	node->linkcount = writeIndex;
}

static CAI_Node* FindAINNodeById(CAI_Network* aiNetwork, int id)
{
	if (!aiNetwork || !aiNetwork->nodes || id < 0)
		return nullptr;

	if (id < aiNetwork->nodecount && aiNetwork->nodes[id] && aiNetwork->nodes[id]->index == id)
		return aiNetwork->nodes[id];

	for (int i = 0; i < aiNetwork->nodecount; ++i)
	{
		CAI_Node* node = aiNetwork->nodes[i];
		if (node && node->index == id)
			return node;
	}

	return nullptr;
}

static CAI_NodeLink* FindAINLink(CAI_Node* node, int dst)
{
	if (!node || !node->links)
		return nullptr;

	for (int i = 0; i < node->linkcount; ++i)
	{
		CAI_NodeLink* link = node->links[i];
		if (link && link->srcId == node->index && link->destId == dst)
			return link;
	}

	return nullptr;
}



static bool AINPositionsEqual(const CAI_Node* a, const CAI_Node* b)
{
	return a && b
		&& a->position.x == b->position.x
		&& a->position.y == b->position.y
		&& a->position.z == b->position.z;
}

static bool AINNeighborTypesAllowPruneComparison(const CAI_Node* src, const CAI_Node* candidate, const CAI_Node* other)
{
	if (!src || !candidate || !other)
		return false;

	const int candidateType = candidate->unk0;
	const int otherType = other->unk0;
	if (candidateType == 3)
	{
		if (otherType != 3)
			return false;
	}
	else if (otherType == 3)
	{
		return false;
	}

	const int srcType = src->unk0;
	if ((srcType == 4 && (candidateType == 4 || otherType == 4))
		|| (AINPositionsEqual(candidate, src) && srcType == 4 && candidateType == 4)
		|| (AINPositionsEqual(other, src) && srcType == 4 && otherType == 4)
		|| (AINPositionsEqual(other, candidate) && candidateType == 4 && otherType == 4))
		return false;

	return true;
}

static float AINNodeDistance(const CAI_Node* a, const CAI_Node* b)
{
	if (!a || !b)
		return 0.0f;

	const float dx = b->position.x - a->position.x;
	const float dy = b->position.y - a->position.y;
	const float dz = b->position.z - a->position.z;
	return sqrtf((dx * dx) + (dy * dy) + (dz * dz));
}

static float AINNodeDirectionDot(const CAI_Node* src, const CAI_Node* a, const CAI_Node* b)
{
	const float ax = a->position.x - src->position.x;
	const float ay = a->position.y - src->position.y;
	const float az = a->position.z - src->position.z;
	const float bx = b->position.x - src->position.x;
	const float by = b->position.y - src->position.y;
	const float bz = b->position.z - src->position.z;
	const float aLen = sqrtf((ax * ax) + (ay * ay) + (az * az)) + 1.1754944e-38f;
	const float bLen = sqrtf((bx * bx) + (by * by) + (bz * bz)) + 1.1754944e-38f;
	return ((ax / aLen) * (bx / bLen)) + ((ay / aLen) * (by / bLen)) + ((az / aLen) * (bz / bLen));
}

static void LogAINDebugPruneCandidates(CAI_Network* aiNetwork, int srcId, int dstId, const char* stage)
{
	CAI_Node* srcNode = FindAINNodeById(aiNetwork, srcId);
	CAI_Node* dstNode = FindAINNodeById(aiNetwork, dstId);
	if (!srcNode || !dstNode)
		return;

	struct Candidate
	{
		int id = -1;
		int type = 0;
		float distance = 0.0f;
		float dot = 0.0f;
	};

	Candidate top[8]{};
	int matchCount = 0;
	const float dstDistance = AINNodeDistance(srcNode, dstNode);

	for (int i = 0; i < aiNetwork->nodecount; ++i)
	{
		CAI_Node* candidate = aiNetwork->nodes[i];
		if (!candidate || candidate->index == srcId || candidate->index == dstId)
			continue;
		if (!TestAINBitVecVectorBit(G_server + 0xD41730, srcId, candidate->index))
			continue;
		if (!AINNeighborTypesAllowPruneComparison(srcNode, dstNode, candidate))
			continue;

		const float dot = AINNodeDirectionDot(srcNode, dstNode, candidate);
		if (dot < 0.92387998f)
			continue;

		const float distance = AINNodeDistance(srcNode, candidate);
		if (distance > dstDistance)
			continue;

		++matchCount;
		Candidate current{ candidate->index, candidate->unk0, distance, dot };
		for (Candidate& slot : top)
		{
			if (slot.id < 0 || current.distance < slot.distance)
			{
				Candidate displaced = slot;
				slot = current;
				current = displaced;
			}
		}
	}

	char buffer[1024];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"AINBUILD: edge prune %s %d->%d targetDist=%.3f closerConeCount=%d top=[%d t%d %.3f %.6f],[%d t%d %.3f %.6f],[%d t%d %.3f %.6f],[%d t%d %.3f %.6f]\n",
		stage ? stage : "?",
		srcId,
		dstId,
		dstDistance,
		matchCount,
		top[0].id, top[0].type, top[0].distance, top[0].dot,
		top[1].id, top[1].type, top[1].distance, top[1].dot,
		top[2].id, top[2].type, top[2].distance, top[2].dot,
		top[3].id, top[3].type, top[3].distance, top[3].dot);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static void LogAINDebugPruneCandidatesFromCommand(CAI_Network* aiNetwork, const char* stage)
{
	int srcId = -1;
	int dstId = -1;
	if (!ParseAINDebugEdge(srcId, dstId))
		return;

	LogAINDebugPruneCandidates(aiNetwork, srcId, dstId, stage);
}

static void LogAINDebugLinkDetail(const char* stage, const char* label, const CAI_NodeLink* link)
{
	if (!link)
		return;

	char buffer[768];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"AINBUILD: edge-link %s %s ptr=%p src=%d dst=%d hull=[%u,%u,%u,%u,%u] unk0=0x%02x unk1=0x%02x unk2=[0x%02x,0x%02x,0x%02x,0x%02x,0x%02x] flags=0x%016llx\n",
		stage ? stage : "?",
		label ? label : "?",
		link,
		link->srcId,
		link->destId,
		static_cast<unsigned>(link->hulls[0]),
		static_cast<unsigned>(link->hulls[1]),
		static_cast<unsigned>(link->hulls[2]),
		static_cast<unsigned>(link->hulls[3]),
		static_cast<unsigned>(link->hulls[4]),
		static_cast<unsigned char>(link->unk0),
		static_cast<unsigned char>(link->unk1),
		static_cast<unsigned char>(link->unk2[0]),
		static_cast<unsigned char>(link->unk2[1]),
		static_cast<unsigned char>(link->unk2[2]),
		static_cast<unsigned char>(link->unk2[3]),
		static_cast<unsigned char>(link->unk2[4]),
		static_cast<unsigned long long>(link->flags));
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static void LogAINDebugEdge(CAI_Network* aiNetwork, const char* stage, bool runPredicate)
{
	int srcId = -1;
	int dstId = -1;
	if (!ParseAINDebugEdge(srcId, dstId))
		return;

	CAI_Node* srcNode = FindAINNodeById(aiNetwork, srcId);
	CAI_Node* dstNode = FindAINNodeById(aiNetwork, dstId);
	const bool neighborForward = TestAINBitVecVectorBit(G_server + 0xD41730, srcId, dstId);
	const bool neighborReverse = TestAINBitVecVectorBit(G_server + 0xD41730, dstId, srcId);
	CAI_NodeLink* forwardLink = FindAINLink(srcNode, dstId);
	CAI_NodeLink* reverseLink = FindAINLink(dstNode, srcId);

	char buffer[1024];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"AINBUILD: edge %s %d->%d src=%p dst=%p neighbor=%d/%d link=%p/%p srcType=%d dstType=%d srcFlags=0x%08x dstFlags=0x%08x srcHint=%p dstHint=%p\n",
		stage ? stage : "?",
		srcId,
		dstId,
		srcNode,
		dstNode,
		neighborForward ? 1 : 0,
		neighborReverse ? 1 : 0,
		forwardLink,
		reverseLink,
		srcNode ? srcNode->unk0 : -1,
		dstNode ? dstNode->unk0 : -1,
		srcNode ? srcNode->flags : 0,
		dstNode ? dstNode->flags : 0,
		srcNode ? *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(srcNode) + 136) : nullptr,
		dstNode ? *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(dstNode) + 136) : nullptr);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
	LogAINDebugLinkDetail(stage, "forward", forwardLink);
	LogAINDebugLinkDetail(stage, "reverse", reverseLink);

	if (!runPredicate || !srcNode || !dstNode)
		return;

	auto canConnectHull = reinterpret_cast<TFOAINCanConnectHullFn>(G_server + 0x36A380);
	int forward[MAX_HULLS] = {};
	int reverse[MAX_HULLS] = {};
	int forwardBytes[MAX_HULLS] = {};
	int reverseBytes[MAX_HULLS] = {};
	unsigned char forwardOnlyMask = 0;
	unsigned char reverseOnlyMask = 0;
	bool twoWayOrForward = false;

	for (int hull = 0; hull < MAX_HULLS; ++hull)
	{
		forward[hull] = canConnectHull(nullptr, srcNode, dstNode, hull);
		reverse[hull] = canConnectHull(nullptr, dstNode, srcNode, hull);
		forwardBytes[hull] = forward[hull] & (reverse[hull] | 0x62);
		reverseBytes[hull] = reverse[hull] & (forwardBytes[hull] | 0x62);

		if (forwardBytes[hull])
		{
			if (reverseBytes[hull])
				twoWayOrForward = true;
			else if (!reverseOnlyMask)
				forwardOnlyMask |= static_cast<unsigned char>(1u << hull);
		}
		else if (reverseBytes[hull] && !forwardOnlyMask)
		{
			reverseOnlyMask |= static_cast<unsigned char>(1u << hull);
		}
	}

	const bool originalWouldAddForward = neighborForward && !reverseOnlyMask && (twoWayOrForward || forwardOnlyMask);
	const bool originalWouldAddReverse = neighborForward && reverseOnlyMask;
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"AINBUILD: edge predicate %d->%d fwd=[%d,%d,%d,%d,%d] rev=[%d,%d,%d,%d,%d] outF=[%d,%d,%d,%d,%d] outR=[%d,%d,%d,%d,%d] twoWay=%d fwdOnlyMask=0x%02x revOnlyMask=0x%02x wouldAdd=%s\n",
		srcId,
		dstId,
		forward[0], forward[1], forward[2], forward[3], forward[4],
		reverse[0], reverse[1], reverse[2], reverse[3], reverse[4],
		forwardBytes[0], forwardBytes[1], forwardBytes[2], forwardBytes[3], forwardBytes[4],
		reverseBytes[0], reverseBytes[1], reverseBytes[2], reverseBytes[3], reverseBytes[4],
		twoWayOrForward ? 1 : 0,
		forwardOnlyMask,
		reverseOnlyMask,
		originalWouldAddForward ? "forward" : (originalWouldAddReverse ? "reverse" : "none"));
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static void RunAINDebugNeighborSeedProbe(CAI_Network* aiNetwork, TFOAINNodeStageFn seedNeighbors)
{
	int srcId = -1;
	int dstId = -1;
	if (!ParseAINDebugEdge(srcId, dstId) || !aiNetwork || !seedNeighbors)
		return;

	CAI_Node* srcNode = FindAINNodeById(aiNetwork, srcId);
	CAI_Node* dstNode = FindAINNodeById(aiNetwork, dstId);
	if (!srcNode || !dstNode)
		return;

	LogAINDebugEdge(aiNetwork, "seed-probe-before", false);
	seedNeighbors(nullptr, aiNetwork, srcNode);
	LogAINDebugEdge(aiNetwork, "seed-probe-after-src", false);
	LogAINDebugPruneCandidates(aiNetwork, srcId, dstId, "seed-probe-after-src");
	seedNeighbors(nullptr, aiNetwork, dstNode);
	LogAINDebugEdge(aiNetwork, "seed-probe-after-dst", false);

	InitializeFullAINBuildTables(aiNetwork);
	LogAINDebugEdge(aiNetwork, "seed-probe-reset", false);
}




static void CleanupFullAINBuildTables()
{
	auto purgeVector = reinterpret_cast<TFOAINBitVecVectorPurgeFn>(G_server + 0x36BC30);

	purgeVector(reinterpret_cast<void*>(G_server + 0xD41730));
	ResizeAndClearAINBitVec(reinterpret_cast<CAI_NetworkHullBitVec*>(G_server + 0xD41750), 0);
}

static bool RunFullAINBuild(CAI_Network* aiNetwork, bool runScriptCallback)
{
	if (!aiNetwork || !aiNetwork->nodes || aiNetwork->nodecount <= 0)
	{
		Warning("buildain: no valid AI network to build.\n");
		return false;
	}

	auto beginBuild = reinterpret_cast<TFOAINBuildBeginFn>(G_server + 0x341380);
	auto endBuild = reinterpret_cast<TFOAINBuildEndFn>(G_server + 0x341420);
	auto prepareNetworkBuildHelper = reinterpret_cast<TFOAINNoArgStageFn>(G_server + 0x366A90);
	auto initClimbNodePosition = reinterpret_cast<TFOAINNodeStageFn>(G_server + 0x368A00);
	auto initGroundNodePosition = reinterpret_cast<TFOAINNodeStageFn>(G_server + 0x368EE0);
	auto seedNeighbors = reinterpret_cast<TFOAINNodeStageFn>(G_server + 0x369430);
	auto initNeighbors = reinterpret_cast<TFOAINNodeStageFn>(G_server + 0x369E00);
	auto forceDynamicLinkNeighbors = reinterpret_cast<TFOAINNoArgStageFn>(G_server + 0x3685E0);
	auto initLinks = reinterpret_cast<TFOAINNodeStageFn>(G_server + 0x36A730);
	auto fixupHints = reinterpret_cast<TFOAINNoArgStageFn>(G_server + 0x3669C0);
	auto initZones = reinterpret_cast<TFOAINNoArgStageFn>(G_server + 0x367EE0);

	LogAINBuildStage("full-before-build-helper", aiNetwork);
	prepareNetworkBuildHelper();

	*reinterpret_cast<__int64*>(G_server + 0xD41760) = beginBuild();
	*reinterpret_cast<CAI_Network**>(G_server + 0xD41768) = aiNetwork;

	LogAINBuildStage("full-before-node-positions", aiNetwork);
	for (int i = 0; i < aiNetwork->nodecount; ++i)
	{
		CAI_Node* node = aiNetwork->nodes[i];
		if (!node)
			continue;

		if (node->unk0 == 4)
			initClimbNodePosition(nullptr, aiNetwork, node);
		else if (node->unk0 == 2 && (node->flags & 0x8000000) == 0)
			initGroundNodePosition(nullptr, aiNetwork, node);
	}

	LogAINBuildStage("full-before-path-points", aiNetwork);
	RunFullAINPathPointPass(aiNetwork, initGroundNodePosition);

	LogAINBuildStage("full-before-traverse-nodes", aiNetwork);
	RunFullAINTraverseNodePass(aiNetwork, initGroundNodePosition);

	LogAINBuildStage("full-before-indoor-areas", aiNetwork);
	RunFullAINIndoorAreaPass(aiNetwork);

	LogAINBuildStage("full-before-safety", aiNetwork);
	sub_390AE0(aiNetwork);

	LogAINBuildStage("full-before-neighbor-tables", aiNetwork);
	InitializeFullAINBuildTables(aiNetwork);
	RunAINDebugNeighborSeedProbe(aiNetwork, seedNeighbors);

	LogAINBuildStage("full-before-neighbors", aiNetwork);
	for (int i = 0; i < aiNetwork->nodecount; ++i)
	{
		CAI_Node* node = aiNetwork->nodes[i];
		if (node)
			initNeighbors(nullptr, aiNetwork, node);
	}
	LogAINDebugEdge(aiNetwork, "after-neighbors", false);

	LogAINBuildStage("full-before-dynamic-links", aiNetwork);
	forceDynamicLinkNeighbors();
	LogAINDebugEdge(aiNetwork, "after-dynamic-links", false);

	LogAINBuildStage("full-before-links", aiNetwork);
	LogAINDebugEdge(aiNetwork, "before-links", false);
	for (int i = 0; i < aiNetwork->nodecount; ++i)
		PreserveAINFullBuildSpecialLinks(aiNetwork->nodes[i]);
	for (int i = 0; i < aiNetwork->nodecount; ++i)
	{
		CAI_Node* node = aiNetwork->nodes[i];
		if (node)
			initLinks(nullptr, aiNetwork, node);
	}
	LogAINDebugEdge(aiNetwork, "after-links", true);

	LogAINBuildStage("full-before-zones", aiNetwork);
	initZones();
	sub_394F90(aiNetwork);
	LogAINBuildStage("full-before-dynamic-link-init", aiNetwork);
	*reinterpret_cast<bool*>(G_server + 0xC3181D) = false;
	reinterpret_cast<TFOAINNoArgStageFn>(G_server + 0x337E80)();
	LogAINDebugEdge(aiNetwork, "after-dynamic-link-init", true);

	MarkAINPostDynamicLinkHullCache(aiNetwork);


	LogAINBuildStage("full-before-unknown-node-tail", aiNetwork);
	if (!BuildAINUnkNodeTail(aiNetwork))
		return false;

	LogAINBuildStage("full-before-unknown-node-tail-refine", aiNetwork);
	if (!RefineAINUnkNodeTail(aiNetwork))
		return false;
	LogAINBuildStage("full-before-unknown-node-tail-repair", aiNetwork);
	if (!RepairAINPostRefineSideConnectivity(aiNetwork))
		return false;
	LogAINBuildStage("full-before-unknown-link-tail", aiNetwork);
	if (!BuildAINUnkLinkTail(aiNetwork))
		return false;

	fixupHints();

	if (runScriptCallback)
	{
		LogAINBuildStage("full-before-script-callback", aiNetwork);
		if (!RunAINFileBuiltCallback(aiNetwork))
			return false;
	}

	LogAINBuildStage("full-before-cleanup", aiNetwork);
	CleanupFullAINBuildTables();
	endBuild();
	*reinterpret_cast<CAI_Network**>(G_server + 0xD41768) = nullptr;
	LogAINBuildStage("full-after-cleanup", aiNetwork);
	return true;
}

void DumpAINInfo(CAI_Network* aiNetwork)
{
	DumpAINInfo(aiNetwork, BuildAINPathForCurrentMap(".dump.ain"), BuildAINPathForCurrentMap(".ain"), true);
}
static uintptr_t rettorebuild;
static uintptr_t rettofree;
static uintptr_t rettofree2;
static uintptr_t rettoalloc;
static uintptr_t rettoallocbullshit;
static __int64 arrayptr1;
static __int64 arrayptr2;
static bool s_AINGlobalsInitialized;
static bool s_AINBuildRequestedAtDynamicInit;
static bool s_AINBuiltAtDynamicInit;
static bool s_AINBuildFailedAtDynamicInit;
static bool s_AINInsideFullBuild;
static CAI_DynamicLink__InitDynamicLinksType CAI_DynamicLink__InitDynamicLinksDirect;
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
		if (CAI_DynamicLink__InitDynamicLinksOriginal)
			CAI_DynamicLink__InitDynamicLinksOriginal();
		else if (CAI_DynamicLink__InitDynamicLinksDirect)
			CAI_DynamicLink__InitDynamicLinksDirect();
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

void __fastcall CAI_NetworkManager__LoadNavMesh(__int64 manager, __int64 fileBuffer, const char* path)
{
	if (ShouldSuppressExistingAINLoad())
	{
		Warning(
			"buildain: suppressing existing AIN load for '%s'; builder must create the graph from map entities.\n",
			path ? path : "<null>");
		return;
	}

	CAI_NetworkManager__LoadNavMeshOriginal(manager, fileBuffer, path);
}

bool __fastcall CAI_NetworkManager__OpenAINFile(__int64 unused, __int64 fileBuffer, const char* path)
{
	if (ShouldSuppressExistingAINLoad())
	{
		char buffer[384];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"buildain: suppressing existing AIN open for '%s'; graph build will start from map state.\n",
			path ? path : "<null>");
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
		return false;
	}

	return CAI_NetworkManager__OpenAINFileOriginal(unused, fileBuffer, path);
}

static void InitializeAINNavmeshGlobals(uintptr_t server)
{
	if (s_AINGlobalsInitialized)
		return;

	s_AINGlobalsInitialized = true;
	rettorebuild = server + 0x368594;
	rettofree = server + 0x368469;
	rettofree2 = server + 0x3685A0;
	rettoalloc = server + 0x36847E;
	rettoallocbullshit = server + 0x3684A4;
	CAI_DynamicLink__InitDynamicLinksDirect = CAI_DynamicLink__InitDynamicLinksType(server + 0x0337E80);
	CAI_NetworkBuilder__InitZones = CAI_DynamicLink__InitDynamicLinksType(server + 0x367EE0);

	arrayptr1 = (__int64)(server + 0xD416F0);
	arrayptr2 = (__int64)(server + 0xD41710);

	pTraverseNodeCount = (int*)(server + 0x0D41AB8);
	ppTraverseNodes = (CAI_TraverseNodeDisk**)(server + 0xD41AA0);
	pUnkStruct0Count = (int*)(server + 0xD41AE8);
	pppUnkNodeStruct0s = (UnkNodeStruct0***)(server + 0xD41AD0);
	pUnkLinkStruct1Count = (int*)(server + 0xD41B08);
	pppUnkStruct1s = (UnkLinkStruct1***)(server + 0xD41AF0);
}

static CAI_Network* GetActiveAINetwork()
{
	uintptr_t manager = *reinterpret_cast<uintptr_t*>(G_server + 0xC31898);
	CAI_Network* activeNetwork = manager ? *reinterpret_cast<CAI_Network**>(manager + 1600) : nullptr;
	if (activeNetwork)
		return activeNetwork;

	return network;
}

void CAI_DynamicLink__InitDynamicLinks()
{
	bool completedFullBuild = false;
	if (s_AINBuildRequestedAtDynamicInit && !s_AINBuiltAtDynamicInit && !s_AINInsideFullBuild)
	{
		InitializeAINNavmeshGlobals(G_server);
		CAI_Network* activeNetwork = GetActiveAINetwork();
		if (!activeNetwork)
		{
			s_AINBuildFailedAtDynamicInit = true;
			Warning("buildain: dynamic-init rebuild requested, but no active AI network was available.\n");
		}
		else
		{
			LogAINBuildStage("before-full-build-at-dynamic-init", activeNetwork);
			s_AINInsideFullBuild = true;
			const bool buildOk = RunFullAINBuild(activeNetwork, true);
			s_AINInsideFullBuild = false;
			if (buildOk)
			{
				s_AINBuiltAtDynamicInit = true;
				completedFullBuild = true;
				LogAINBuildStage("after-full-build-at-dynamic-init", activeNetwork);
			}
			else
			{
				s_AINBuildFailedAtDynamicInit = true;
				Warning("buildain: dynamic-init rebuild failed.\n");
			}
		}
	}

	if (completedFullBuild)
		return;

	if (CAI_DynamicLink__InitDynamicLinksOriginal)
		CAI_DynamicLink__InitDynamicLinksOriginal();
	else if (CAI_DynamicLink__InitDynamicLinksDirect)
		CAI_DynamicLink__InitDynamicLinksDirect();
}

void __fastcall CAI_NetworkManager__DelayedInit(__int64 a1) {
	auto server = G_server;
	bool* CAI_NetworkManager__gm_fNetworksLoaded = (bool*)(server + 0xC318A0);

	InitializeAINNavmeshGlobals(server);
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
	const bool dumpLoadedAIN = HasEngineCommandLineFlag("-r1delta_dump_loaded_ain");
	const bool rebuildAINOnLoad = HasEngineCommandLineFlag("-r1delta_rebuild_ain_on_load") || HasEngineCommandLineFlag("-r1delta_dump_rebuilt_ain");
	const bool dumpRebuiltAIN = HasEngineCommandLineFlag("-r1delta_dump_rebuilt_ain");
	const bool suppressExistingAIN = ShouldSuppressExistingAINLoad();

	if (dumpLoadedAIN || rebuildAINOnLoad || dumpRebuiltAIN || suppressExistingAIN)
		LogAINBuildStage("before-original-delayed-init", network);

	s_AINBuildRequestedAtDynamicInit = rebuildAINOnLoad;
	s_AINBuiltAtDynamicInit = false;
	s_AINBuildFailedAtDynamicInit = false;

	CAI_NetworkManager__DelayedInitOriginal(a1);

	s_AINBuildRequestedAtDynamicInit = false;

	if (dumpLoadedAIN)
	{
		LogAINBuildStage("dump-loaded", network);
		LogAINDebugEdge(network, "dump-loaded", false);
		DumpAINInfo(network, BuildAINPathForCurrentMap(".loaded.dump.ain"), BuildAINPathForCurrentMap(".ain"), false);
	}

	if (rebuildAINOnLoad)
	{
		if (s_AINBuildFailedAtDynamicInit)
			return;
		if (!s_AINBuiltAtDynamicInit)
		{
			Warning("buildain: delayed-init completed without hitting CAI_DynamicLink::InitDynamicLinks; rebuilt AIN was not generated.\n");
			return;
		}
	}

	if (dumpRebuiltAIN)
	{
		DumpAINInfo(network, BuildAINPathForCurrentMap(".rebuilt.dump.ain"), BuildAINPathForCurrentMap(".ain"), false);
		LogAINBuildStage("after-dump-rebuilt", network);
	}
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
		const int memFlags = pm->flags & AINET_NODE_SAVE_MASK;
		if (dn.unk1 != memFlags)
		{
			Warning("verifyain: node %d flags mismatch: disk=%d mem=%d\n", i, dn.unk1, memFlags);
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

	// 2) Get network pointer
	CAI_Network* pNetwork = *reinterpret_cast<CAI_Network**>(G_server + 0xC31888);
	if (!pNetwork || !pNetwork->nodes)
	{
		Warning("updateain: No valid AI network/nodes present.\n");
		return;
	}

	// 3) Build path
	std::filesystem::path ainPath("r1delta/maps/graphs");
	const char* mapName = static_cast<const char*>((pGlobalVarsServer)->mapname_pszValue);
	if (!mapName || !*mapName)
	{
		Warning("updateain: Current map name is invalid.\n");
		return;
	}
	ainPath /= mapName;
	ainPath += ".ain";

	const bool legacyMode = args.ArgC() >= 2 && strcmp(args.Arg(1), "legacy") == 0;
	if (!legacyMode)
	{
		if (!RunFullAINBuild(pNetwork, true))
			return;

		std::filesystem::path outputPath = BuildAINPathForCurrentMap(".dump.ain");
		if (args.ArgC() >= 2)
			outputPath = args.Arg(1);

		if (!DumpAINInfo(pNetwork, outputPath, ainPath, true))
			return;

		Msg("updateain: dumped full-built in-memory graph to '%s'. Original '%s' was left untouched.\n",
			outputPath.string().c_str(), ainPath.string().c_str());
		return;
	}

	// Legacy mode preserves the previous partial StartRebuild-based updater.
	s_StartRebuild(pManager);

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
			newDisk.unk1 = pMemNode->flags & AINET_NODE_SAVE_MASK;
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
