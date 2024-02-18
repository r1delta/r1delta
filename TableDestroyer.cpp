#include <Windows.h>
#include <iostream>
#include "defs.h"

struct __declspec(align(4)) SendProp
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
	char* name;
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

void DestroySendProp(SendProp* sendTablePtr, int* sendTableLengthPtr, const char* propname) {
	for (int i = 0; i < *sendTableLengthPtr; ++i) {
		if (strcmp(sendTablePtr[i].name, propname) == 0) {
			// Calculate size of the SendProp to be deleted.
			int sizeToDelete = 0;
			if (i < *sendTableLengthPtr - 1) { // Ensure there's a next SendProp to calculate size.
				sizeToDelete = sendTablePtr[i + 1].offset - sendTablePtr[i].offset;
			}

			size_t bytesToMove = (*sendTableLengthPtr - i - 1) * sizeof(SendProp);
			if (bytesToMove > 0) {
				memcpy(&sendTablePtr[i], &sendTablePtr[i + 1], bytesToMove);
			}

			--(*sendTableLengthPtr); // Decrement the length of sendTable.

			// Update offsets for remaining SendProps.
			for (int j = i; j < *sendTableLengthPtr; ++j) {
				sendTablePtr[j].offset -= sizeToDelete;
			}

			std::cout << propname << " obliterated." << std::endl;

			return;
		}
	}
}

void SmashSendTables() {
	void* serverPtr = (void*)GetModuleHandleA("server.dll");
	SendProp* DT_BasePlayer = (SendProp*)(((uintptr_t)serverPtr) + 0xE9A800);
	int* DT_BasePlayerLen = (int*)(((uintptr_t)serverPtr) + 0xE04768);
	SendProp* DT_Local = (SendProp*)(((uintptr_t)serverPtr) + 0xE9E340);
	int* DT_LocalLen = (int*)(((uintptr_t)serverPtr) + 0xE04B48);
	SendProp* DT_TitanSoul = (SendProp*)(((uintptr_t)serverPtr) + 0xEAB6C0);
	int* DT_TitanSoulLen = (int*)(((uintptr_t)serverPtr) + 0xE08D28);

	DestroySendProp(DT_BasePlayer, DT_BasePlayerLen, "m_bWallRun");
	DestroySendProp(DT_BasePlayer, DT_BasePlayerLen, "m_bDoubleJump");
	DestroySendProp(DT_BasePlayer, DT_BasePlayerLen, "m_hasHacking");
	DestroySendProp(DT_BasePlayer, DT_BasePlayerLen, "m_level");
	DestroySendProp(DT_Local, DT_LocalLen, "m_bFireZooming");
	DestroySendProp(DT_Local, DT_LocalLen, "m_titanBuildStarted");
	DestroySendProp(DT_Local, DT_LocalLen, "m_titanDeployed");
	DestroySendProp(DT_Local, DT_LocalLen, "m_titanReady");
	DestroySendProp(DT_Local, DT_LocalLen, "m_titanRespawnTime");
	//DestroySendProp(DT_TitanSoul, DT_TitanSoulLen, "m_liveryCode");
	//DestroySendProp(DT_TitanSoul, DT_TitanSoulLen, "m_liveryColor0");
	//DestroySendProp(DT_TitanSoul, DT_TitanSoulLen, "m_liveryColor1");
	//DestroySendProp(DT_TitanSoul, DT_TitanSoulLen, "m_liveryColor2");
	//DestroySendProp(DT_TitanSoul, DT_TitanSoulLen, "m_tierLevel");
	// these are at the end so theres nothing after them
	*DT_TitanSoulLen -= 5;
}