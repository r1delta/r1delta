#include <Windows.h>
#include <iostream>
#include "defs.h"
#include "TableDestroyer.h"
ServerClassInitFunc ServerClassInit_DT_BasePlayerOriginal;
ServerClassInitFunc ServerClassInit_DT_LocalOriginal;
ServerClassInitFunc ServerClassInit_DT_TitanSoulOriginal;
void DestroySendProp(SendProp* sendTablePtr, int* sendTableLengthPtr, const char* propname) {
	for (int i = 0; i < *sendTableLengthPtr; ++i) {
		std::cout << "SendProp name: " << sendTablePtr[i].name << std::endl;
		if (strcmp(sendTablePtr[i].name, propname) == 0) {
			size_t bytesToMove = (*sendTableLengthPtr - i) * sizeof(SendProp);
			if (bytesToMove > 0) {
				memmove(&sendTablePtr[i], &sendTablePtr[i + 1], bytesToMove);
			}

			--(*sendTableLengthPtr);

			std::cout << propname << " obliterated." << std::endl;

			return;
		}
	}
}

void ServerClassInit_DT_BasePlayer() {
	ServerClassInit_DT_BasePlayerOriginal();
	void* serverPtr = (void*)GetModuleHandleA("server.dll");
	SendProp* DT_BasePlayer = (SendProp*)(((uintptr_t)serverPtr) + 0xE9A800);
	int* DT_BasePlayerLen = (int*)(((uintptr_t)serverPtr) + 0xE04768);

	DestroySendProp(DT_BasePlayer, DT_BasePlayerLen, "m_bWallRun");
	DestroySendProp(DT_BasePlayer, DT_BasePlayerLen, "m_bDoubleJump");
	DestroySendProp(DT_BasePlayer, DT_BasePlayerLen, "m_hasHacking");
	DestroySendProp(DT_BasePlayer, DT_BasePlayerLen, "m_level");
}
void ServerClassInit_DT_Local() {
	ServerClassInit_DT_LocalOriginal();
	void* serverPtr = (void*)GetModuleHandleA("server.dll");
	SendProp* DT_Local = (SendProp*)(((uintptr_t)serverPtr) + 0xE9E340);
	int* DT_LocalLen = (int*)(((uintptr_t)serverPtr) + 0xE04B48);

	DestroySendProp(DT_Local, DT_LocalLen, "m_bFireZooming");
	DestroySendProp(DT_Local, DT_LocalLen, "m_titanBuildStarted");
	DestroySendProp(DT_Local, DT_LocalLen, "m_titanDeployed");
	DestroySendProp(DT_Local, DT_LocalLen, "m_titanReady");
	DestroySendProp(DT_Local, DT_LocalLen, "m_titanRespawnTime");
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