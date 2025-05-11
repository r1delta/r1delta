#pragma once

#include <cstdlib>
#include "utils.h"

class CVModelInfoServer;

extern CVModelInfoServer* g_CVModelInfoServer;
extern uintptr_t g_CVModelInfoServerInterface;
extern uintptr_t g_r1oCVModelInfoServerInterface[50];

class CVModelInfoServer
{
public:
	CVModelInfoServer() = default;
	CVModelInfoServer(uintptr_t* r1vtable);
	void CreateR1OVTable();

	uintptr_t dtor_0;
	uintptr_t GetModel;
	uintptr_t GetModelIndex;
	uintptr_t GetModelName;
	uintptr_t GetVCollide;
	uintptr_t GetVCollideEx;
	uintptr_t GetVCollideEx2;
	uintptr_t GetModelRenderBounds;
	uintptr_t GetModelFrameCount;
	uintptr_t GetModelType;
	uintptr_t GetModelExtraData;
	uintptr_t IsTranslucentTwoPass;
	uintptr_t ModelHasMaterialProxy;
	uintptr_t IsTranslucent;
	uintptr_t NullSub1;
	uintptr_t UnkFunc1;
	uintptr_t UnkFunc2;
	uintptr_t UnkFunc3;
	uintptr_t UnkFunc4;
	uintptr_t UnkFunc5;
	uintptr_t UnkFunc6;
	uintptr_t UnkFunc7;
	uintptr_t UnkFunc8;
	uintptr_t UnkFunc9;
	uintptr_t UnkFunc10;
	uintptr_t UnkFunc11;
	uintptr_t UnkFunc12;
	uintptr_t UnkFunc13;
	uintptr_t UnkFunc14;
	uintptr_t UnkFunc15;
	uintptr_t UnkFunc16;
	uintptr_t UnkFunc17;
	uintptr_t UnkFunc18;
	uintptr_t UnkFunc19;
	uintptr_t UnkFunc20;
	uintptr_t UnkFunc21;
	uintptr_t UnkFunc22;
	uintptr_t UnkFunc23;
	uintptr_t NullSub2;
	uintptr_t UnkFunc24;
	uintptr_t UnkFunc25;
};