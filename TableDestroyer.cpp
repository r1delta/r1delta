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

// (╯°□°)╯︵ ┻━┻ 
	
#include <Windows.h>
#include <iostream>
#include "defs.h"
#include "TableDestroyer.h"

ServerClassInitFunc ServerClassInit_DT_BasePlayerOriginal;
ServerClassInitFunc ServerClassInit_DT_LocalOriginal;
ServerClassInitFunc ServerClassInit_DT_LocalPlayerExclusiveOriginal;
ServerClassInitFunc ServerClassInit_DT_TitanSoulOriginal;

void DestroySendProp(SendProp* sendTablePtr, int* sendTableLengthPtr, const char* propname) {
	for (int i = 0; i < *sendTableLengthPtr; ++i) {
		//std::cout << "SendProp name: " << sendTablePtr[i].name << std::endl;
		if (strcmp(sendTablePtr[i].name, propname) == 0) {
			size_t bytesToMove = (*sendTableLengthPtr - i) * sizeof(SendProp);
			if (bytesToMove > 0) {
				memmove(&sendTablePtr[i], &sendTablePtr[i + 1], bytesToMove);
			}
			//if (std::string(sendTablePtr[i].name) != std::string("m_wallDangleDisableWeapon"))
			--(*sendTableLengthPtr);

			std::cout << propname << " obliterated." << std::endl;

			return;
		}
	}
	std::cout << "its fucked man" << std::endl;
	DebugBreak();
	std::cout << (char*)0 << "lol" << std::endl;
}

void RenameSendProp(SendProp* sendTablePtr, int* sendTableLengthPtr, const char* currentName, const char* newName) {
	for (int i = 0; i < *sendTableLengthPtr; ++i) {
		if (strcmp(sendTablePtr[i].name, currentName) == 0) {
			sendTablePtr[i].name = (char*)newName;
			std::cout << "SendProp renamed from " << currentName << " to " << newName << std::endl;
			return;
		}
	}

	std::cout << "SendProp not found: " << currentName << std::endl;
}

SendProp* FindSendProp(SendProp* sendTablePtr, int sendTableLength, const char* propName) {
	for (int i = 0; i < sendTableLength; ++i) {
		if (strcmp(sendTablePtr[i].name, propName) == 0) {
			return &sendTablePtr[i];
		}
	}
	return nullptr;
}

void MoveSendProp(SendProp* sourceTablePtr, int* sourceTableLengthPtr, const char* sourcePropName,
	SendProp* destTablePtr, int* destTableLengthPtr, const char* destPropName) {
	// Find the source send prop
	for (int i = 0; i < *sourceTableLengthPtr; ++i) {
		if (strcmp(sourceTablePtr[i].name, sourcePropName) == 0) {
			// Find the destination send prop
			for (int j = 0; j < *destTableLengthPtr; ++j) {
				if (strcmp(destTablePtr[j].name, destPropName) == 0) {
					// Move the source send prop over the destination send prop
					memcpy((void*)(&destTablePtr[j]), (void*)(&sourceTablePtr[i]), sizeof(SendProp));

					std::cout << sourcePropName << " moved over " << destPropName << std::endl;
					return;
				}
			}

			std::cout << "Destination send prop not found: " << destPropName << std::endl;
			return;
		}
	}

	std::cout << "Source send prop not found: " << sourcePropName << std::endl;
}

void ServerClassInit_DT_BasePlayer() {
	ServerClassInit_DT_BasePlayerOriginal();
	ServerClassInit_DT_LocalOriginal();
	void* serverPtr = (void*)GetModuleHandleA("server.dll");
	SendProp* DT_BasePlayer = (SendProp*)(((uintptr_t)serverPtr) + 0xE9A800);
	int* DT_BasePlayerLen = (int*)(((uintptr_t)serverPtr) + 0xE04768);
	SendProp* DT_Local = (SendProp*)(((uintptr_t)serverPtr) + 0xE9E340);
	int* DT_LocalLen = (int*)(((uintptr_t)serverPtr) + 0xE04B48);
	SendProp* DT_LocalPlayerExclusive = (SendProp*)(((uintptr_t)serverPtr) + 0xE982C0);
	int* DT_LocalPlayerExclusiveLen = (int*)(((uintptr_t)serverPtr) + 0xE04878);
	DestroySendProp(DT_Local, DT_LocalLen, "m_bFireZooming");
	DestroySendProp(DT_Local, DT_LocalLen, "m_titanBuildStarted");
	DestroySendProp(DT_Local, DT_LocalLen, "m_titanDeployed");
	DestroySendProp(DT_Local, DT_LocalLen, "m_titanReady");
	DestroySendProp(DT_BasePlayer, DT_BasePlayerLen, "m_bWallRun");
	DestroySendProp(DT_BasePlayer, DT_BasePlayerLen, "m_bDoubleJump");
	DestroySendProp(DT_BasePlayer, DT_BasePlayerLen, "m_hasHacking");

	MoveSendProp(DT_Local, DT_LocalLen, "m_titanRespawnTime",
		DT_BasePlayer, DT_BasePlayerLen, "m_level");

	DestroySendProp(DT_Local, DT_LocalLen, "m_titanRespawnTime");
	RenameSendProp(DT_BasePlayer, DT_BasePlayerLen, "m_titanRespawnTime", "m_nextTitanRespawnAvailable");
	FindSendProp(DT_BasePlayer, *DT_BasePlayerLen, "m_nextTitanRespawnAvailable")->offset += FindSendProp(DT_LocalPlayerExclusive, *DT_LocalPlayerExclusiveLen, "m_Local")->offset;
}

void ServerClassInit_DT_Local() {
	//ServerClassInit_DT_LocalOriginal();


	//DestroySendProp(DT_Local, DT_LocalLen, "m_titanRespawnTime");
}

void ServerClassInit_DT_LocalPlayerExclusive() {
	ServerClassInit_DT_LocalPlayerExclusiveOriginal();
	void* serverPtr = (void*)GetModuleHandleA("server.dll");
	SendProp* DT_LocalPlayerExclusive = (SendProp*)(((uintptr_t)serverPtr) + 0xE982C0);
	int* DT_LocalPlayerExclusiveLen = (int*)(((uintptr_t)serverPtr) + 0xE04878);

	//DestroySendProp(DT_LocalPlayerExclusive, DT_LocalPlayerExclusiveLen, "m_wallDangleDisableWeapon");
}

void ServerClassInit_DT_TitanSoul() {
	ServerClassInit_DT_TitanSoulOriginal();
	void* serverPtr = (void*)GetModuleHandleA("server.dll");
	SendProp* DT_TitanSoul = (SendProp*)(((uintptr_t)serverPtr) + 0xEAB6C0);
	int* DT_TitanSoulLen = (int*)(((uintptr_t)serverPtr) + 0xE08D28);
	//DestroySendProp(DT_TitanSoul, DT_TitanSoulLen, "m_liveryCode");
	//DestroySendProp(DT_TitanSoul, DT_TitanSoulLen, "m_liveryColor0");
	//DestroySendProp(DT_TitanSoul, DT_TitanSoulLen, "m_liveryColor1");
	//DestroySendProp(DT_TitanSoul, DT_TitanSoulLen, "m_liveryColor2");
	//DestroySendProp(DT_TitanSoul, DT_TitanSoulLen, "m_tierLevel");
	// these are at the end so theres nothing after them
	*DT_TitanSoulLen -= 5;
}

__int64 __fastcall CBaseEntity__SendProxy_CellOrigin(__int64 a1, _DWORD* a2, __int64 a3, float* a4)
{
	int m_Value; // eax
	float* p_x; // r10
	int v8; // r8d
	int v9; // r9d
	int m_cellwidth; // ecx
	float v11; // xmm3_4
	int v12; // eax
	float v13; // xmm1_4
	float v14; // xmm0_4
	float v15; // xmm3_4
	int v16; // eax
	float v17; // xmm1_4
	float v18; // xmm0_4
	float v19; // xmm3_4
	__int64 result; // rax
	float v21; // xmm1_4
	int cell[6]; // [rsp+20h] [rbp-18h] BYREF
	float* pStruct; // [rsp+48h] [rbp+10h] BYREF
	void* serverPtr = (void*)GetModuleHandleA("server.dll");
	auto CBaseEntity__UseStepSimulationNetworkOrigin = reinterpret_cast<char(__fastcall*)(__int64 a1, float** a2, int* a3)>((uintptr_t)(serverPtr)+0x3BC740);

	if (CBaseEntity__UseStepSimulationNetworkOrigin((__int64)a2, &pStruct, cell))
	{
		v9 = cell[2];
		v8 = cell[1];
		m_Value = cell[0];
		p_x = pStruct;
	}
	else
	{
		m_Value = a2[268];
		p_x = (float*)(a2 + 271);
		v8 = a2[269];
		v9 = a2[270];
	}
	m_cellwidth = a2[266];
	v11 = *p_x;
	v12 = abs32(m_cellwidth * m_Value - 0x4000);
	if (*p_x >= 0.0)
		v13 = v11 - (float)v12;
	else
		v13 = (float)v12 + v11;
	if (v13 >= 0.0)
		v14 = fminf((float)m_cellwidth, v13);
	else
		v14 = 0.0;
	*a4 = v14;
	v15 = p_x[1];
	v16 = abs32(m_cellwidth * v8 - 0x4000);
	if (v15 >= 0.0)
		v17 = v15 - (float)v16;
	else
		v17 = (float)v16 + v15;
	if (v17 >= 0.0)
		v18 = fminf((float)m_cellwidth, v17);
	else
		v18 = 0.0;
	a4[1] = v18;
	v19 = p_x[2];
	result = abs32(m_cellwidth * v9 - 0x4000);
	if (v19 >= 0.0)
		v21 = v19 - (float)(int)result;
	else
		v21 = (float)(int)result + v19;
	if (v21 >= 0.0)
		a4[2] = fminf((float)m_cellwidth, v21);
	else
		a4[2] = 0.0;
	return result;
}

__int64 __fastcall CBaseEntity__SendProxy_CellOriginXY(__int64 a1, _DWORD* a2, __int64 a3, float* a4)
{
	int v6; // eax
	float* v7; // r8
	int v8; // ecx
	int v9; // r9d
	float v10; // xmm3_4
	int v11; // eax
	float v12; // xmm1_4
	float v13; // xmm0_4
	float v14; // xmm3_4
	__int64 result; // rax
	float v16; // xmm1_4
	int v17[6]; // [rsp+20h] [rbp-18h] BYREF
	float* v18; // [rsp+48h] [rbp+10h] BYREF
	void* serverPtr = (void*)GetModuleHandleA("server.dll");
	auto CBaseEntity__UseStepSimulationNetworkOrigin = reinterpret_cast<char(__fastcall*)(__int64 a1, float** a2, int* a3)>((uintptr_t)(serverPtr)+0x3BC740);

	if (CBaseEntity__UseStepSimulationNetworkOrigin((__int64)a2, &v18, v17))
	{
		v8 = v17[1];
		v6 = v17[0];
		v7 = v18;
	}
	else
	{
		v6 = a2[268];
		v7 = (float*)(a2 + 271);
		v8 = a2[269];
	}
	v9 = a2[266];
	v10 = *v7;
	v11 = abs32(v9 * v6 - 0x4000);
	if (*v7 >= 0.0)
		v12 = v10 - (float)v11;
	else
		v12 = (float)v11 + v10;
	if (v12 >= 0.0)
		v13 = fminf((float)v9, v12);
	else
		v13 = 0.0;
	*a4 = v13;
	v14 = v7[1];
	result = abs32(v9 * v8 - 0x4000);
	if (v14 >= 0.0)
		v16 = v14 - (float)(int)result;
	else
		v16 = (float)(int)result + v14;
	if (v16 >= 0.0)
		a4[1] = fminf((float)v9, v16);
	else
		a4[1] = 0.0;
	return result;
}

__int64 __fastcall CBaseEntity__SendProxy_CellOriginZ(__int64 a1, __int64 a2, __int64 a3, float* a4)
{
	int v6; // eax
	__int64 v7; // rcx
	int v8; // r8d
	float v9; // xmm3_4
	__int64 result; // rax
	float v11; // xmm1_4
	_DWORD v12[6]; // [rsp+20h] [rbp-18h] BYREF
	__int64 v13; // [rsp+48h] [rbp+10h] BYREF
	void* serverPtr = (void*)GetModuleHandleA("server.dll");
	auto CBaseEntity__UseStepSimulationNetworkOrigin = reinterpret_cast<char(__fastcall*)(__int64 a1, float** a2, int* a3)>((uintptr_t)(serverPtr)+0x3BC740);

	if (CBaseEntity__UseStepSimulationNetworkOrigin(a2, (float**)(&v13), (int*)v12))
	{
		v6 = v12[2];
		v7 = v13;
	}
	else
	{
		v6 = *(_DWORD*)(a2 + 1080);
		v7 = a2 + 1084;
	}
	v8 = *(_DWORD*)(a2 + 1064);
	v9 = *(float*)(v7 + 8);
	result = abs32(v8 * v6 - 0x4000);
	if (v9 >= 0.0)
		v11 = v9 - (float)(int)result;
	else
		v11 = (float)(int)result + v9;
	if (v11 >= 0.0)
		*a4 = fminf((float)v8, v11);
	else
		*a4 = 0.0;
	return result;
}

__int64 __fastcall sub_3A1E00(__int64 a1, __int64 a2, unsigned int* a3, _DWORD* a4)
{
	bool v6; // zf
	__int64 result; // rax
	unsigned int v8[6]; // [rsp+20h] [rbp-18h] BYREF
	__int64 v9; // [rsp+48h] [rbp+10h] BYREF
	void* serverPtr = (void*)GetModuleHandleA("server.dll");
	auto CBaseEntity__UseStepSimulationNetworkOrigin = reinterpret_cast<char(__fastcall*)(__int64 a1, float** a2, int* a3)>((uintptr_t)(serverPtr)+0x3BC740);

	v6 = CBaseEntity__UseStepSimulationNetworkOrigin(a2, (float**)(&v9), (int*)v8) == 0;
	result = v8[0];
	if (v6)
		result = *a3;
	*a4 = result;
	return result;
}

__int64 __fastcall sub_3A1E40(__int64 a1, __int64 a2, unsigned int* a3, _DWORD* a4)
{
	bool v6; // zf
	__int64 result; // rax
	_DWORD v8[6]; // [rsp+20h] [rbp-18h] BYREF
	__int64 v9; // [rsp+48h] [rbp+10h] BYREF
	void* serverPtr = (void*)GetModuleHandleA("server.dll");
	auto CBaseEntity__UseStepSimulationNetworkOrigin = reinterpret_cast<char(__fastcall*)(__int64 a1, float** a2, int* a3)>((uintptr_t)(serverPtr)+0x3BC740);

	v6 = CBaseEntity__UseStepSimulationNetworkOrigin(a2, (float**)(&v9), (int*)v8) == 0;
	result = v8[1];
	if (v6)
		result = *a3;
	*a4 = result;
	return result;
}

__int64 __fastcall sub_3A1E80(__int64 a1, __int64 a2, unsigned int* a3, _DWORD* a4)
{
	bool v6; // zf
	__int64 result; // rax
	_DWORD v8[6]; // [rsp+20h] [rbp-18h] BYREF
	__int64 v9; // [rsp+48h] [rbp+10h] BYREF
	void* serverPtr = (void*)GetModuleHandleA("server.dll");
	auto CBaseEntity__UseStepSimulationNetworkOrigin = reinterpret_cast<char(__fastcall*)(__int64 a1, float** a2, int* a3)>((uintptr_t)(serverPtr)+0x3BC740);

	v6 = CBaseEntity__UseStepSimulationNetworkOrigin(a2, (float**)(&v9), (int*)v8) == 0;
	result = v8[2];
	if (v6)
		result = *a3;
	*a4 = result;
	return result;
}
