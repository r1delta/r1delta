#pragma once
struct __declspec(align(8)) SendProp
{
	_QWORD qword0;
	_QWORD qword8;
	_QWORD qword10;
	_QWORD qword18;
	_QWORD qword20;
	_QWORD qword28;
	_DWORD dword30;
	_DWORD dword34;
	_QWORD qword38;
	_QWORD qword40;
	const char* name;
	_DWORD dword50;
	_BYTE byte54;
	__declspec(align(4)) _DWORD dword58;
	_BYTE gap5C[4];
	_QWORD qword60;
	_QWORD qword68;
	_QWORD qword70;
	_DWORD offset;
	_DWORD pad1;
	_DWORD pad2;
};
static_assert(sizeof(SendProp) == 0x88);
typedef void(*ServerClassInitFunc)();
extern ServerClassInitFunc ServerClassInit_DT_BasePlayerOriginal;
extern ServerClassInitFunc ServerClassInit_DT_LocalOriginal;
extern ServerClassInitFunc ServerClassInit_DT_LocalPlayerExclusiveOriginal;
extern ServerClassInitFunc ServerClassInit_DT_TitanSoulOriginal;

void ServerClassInit_DT_BasePlayer();
void ServerClassInit_DT_Local();
void ServerClassInit_DT_LocalPlayerExclusive();
void ServerClassInit_DT_TitanSoul();

__int64 __fastcall CBaseEntity__SendProxy_CellOrigin(__int64 a1, _DWORD* a2, __int64 a3, float* a4);
__int64 __fastcall CBaseEntity__SendProxy_CellOriginXY(__int64 a1, _DWORD* a2, __int64 a3, float* a4);
__int64 __fastcall CBaseEntity__SendProxy_CellOriginZ(__int64 a1, __int64 a2, __int64 a3,  float* a4);
__int64 __fastcall sub_3A1E00(__int64 a1, __int64 a2, unsigned int* a3, _DWORD* a4);
__int64 __fastcall sub_3A1E40(__int64 a1, __int64 a2, unsigned int* a3, _DWORD* a4);
__int64 __fastcall sub_3A1E80(__int64 a1, __int64 a2, unsigned int* a3, _DWORD* a4);
void DestroySendProp(SendProp* sendTablePtr, int* sendTableLengthPtr, const char* propname);