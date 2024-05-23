#include "model_info.h"

CVModelInfoServer* g_CVModelInfoServer;
uintptr_t g_CVModelInfoServerInterface;
uintptr_t g_r1oCVModelInfoServerInterface[50];

bool byte_1824D16C0 = false;

void CModelInfo__UnkTFOVoid(int64_t a1, int* a2, int64_t a3)
{
	return;
}

void CModelInfo__UnkTFOVoid2(int64_t a1, int64_t a2)
{
	return;
}

void CModelInfo__UnkTFOVoid3(int64_t a1, int64_t a2, uint64_t a3)
{
	return;
}

char CModelInfo__UnkTFOShouldRet0_2(int64_t a1, int64_t a2)
{
	if (a2)
		return *(char*)(a2 + 264) & 1;
	else
		return 0;
}

int64_t CModelInfo__ShouldRet0()
{
	return 0;
}

bool CModelInfo__ClientFullyConnected()
{
	return false;
}

void CModelInfo__UnkSetFlag()
{
	byte_1824D16C0 = 1;
}
void CModelInfo__UnkClearFlag()
{
	byte_1824D16C0 = 0;
}

int64_t CModelInfo__GetFlag()
{
	return (uint8_t)byte_1824D16C0;
}

CVModelInfoServer::CVModelInfoServer(uintptr_t* r1vtable)
{
	dtor_0 = r1vtable[0];
	GetModel = r1vtable[1];
	GetModelIndex = r1vtable[2];
	GetModelName = r1vtable[3];
	GetVCollide = r1vtable[4];
	GetVCollideEx = r1vtable[5];
	GetVCollideEx2 = r1vtable[6];
	GetModelRenderBounds = r1vtable[7];
	GetModelFrameCount = r1vtable[8];
	GetModelType = r1vtable[9];
	GetModelExtraData = r1vtable[10];
	IsTranslucentTwoPass = r1vtable[11];
	ModelHasMaterialProxy = r1vtable[12];
	IsTranslucent = r1vtable[13];
	NullSub1 = r1vtable[14];
	UnkFunc1 = r1vtable[15];
	UnkFunc2 = r1vtable[16];
	UnkFunc3 = r1vtable[17];
	UnkFunc4 = r1vtable[18];
	UnkFunc5 = r1vtable[19];
	UnkFunc6 = r1vtable[20];
	UnkFunc7 = r1vtable[21];
	UnkFunc8 = r1vtable[22];
	UnkFunc9 = r1vtable[23];
	UnkFunc10 = r1vtable[24];
	UnkFunc11 = r1vtable[25];
	UnkFunc12 = r1vtable[26];
	UnkFunc13 = r1vtable[27];
	UnkFunc14 = r1vtable[28];
	UnkFunc15 = r1vtable[29];
	UnkFunc16 = r1vtable[30];
	UnkFunc17 = r1vtable[31];
	UnkFunc18 = r1vtable[32];
	UnkFunc19 = r1vtable[33];
	UnkFunc20 = r1vtable[34];
	UnkFunc21 = r1vtable[35];
	UnkFunc22 = r1vtable[36];
	UnkFunc23 = r1vtable[37];
	NullSub2 = r1vtable[38];
	UnkFunc24 = r1vtable[39];
	UnkFunc25 = r1vtable[40];

	CreateR1OVTable();
}

void CVModelInfoServer::CreateR1OVTable()
{
	g_r1oCVModelInfoServerInterface[0] = CreateFunction((void*)dtor_0, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[1] = CreateFunction((void*)GetModel, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[2] = CreateFunction((void*)GetModelIndex, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[3] = CreateFunction((void*)GetModelName, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[4] = CreateFunction((void*)GetVCollide, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[5] = CreateFunction((void*)GetVCollideEx, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[6] = CreateFunction((void*)GetVCollideEx2, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[7] = CreateFunction((void*)GetModelRenderBounds, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[8] = CreateFunction(CModelInfo__UnkSetFlag, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[9] = CreateFunction(CModelInfo__UnkClearFlag, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[10] = CreateFunction(CModelInfo__GetFlag, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[11] = CreateFunction(CModelInfo__UnkTFOVoid, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[12] = CreateFunction(CModelInfo__UnkTFOVoid2, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[13] = CreateFunction(CModelInfo__ShouldRet0, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[14] = CreateFunction(CModelInfo__UnkTFOVoid3, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[15] = CreateFunction(CModelInfo__ClientFullyConnected, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[16] = CreateFunction((void*)GetModelFrameCount, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[17] = CreateFunction((void*)GetModelType, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[18] = CreateFunction(CModelInfo__UnkTFOShouldRet0_2, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[19] = CreateFunction((void*)GetModelExtraData, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[20] = CreateFunction((void*)IsTranslucentTwoPass, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[21] = CreateFunction((void*)ModelHasMaterialProxy, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[22] = CreateFunction((void*)IsTranslucent, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[23] = CreateFunction((void*)NullSub1, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[24] = CreateFunction((void*)UnkFunc1, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[25] = CreateFunction((void*)UnkFunc2, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[26] = CreateFunction((void*)UnkFunc3, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[27] = CreateFunction((void*)UnkFunc4, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[28] = CreateFunction((void*)UnkFunc5, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[29] = CreateFunction((void*)UnkFunc6, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[30] = CreateFunction((void*)UnkFunc7, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[31] = CreateFunction((void*)UnkFunc8, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[32] = CreateFunction((void*)UnkFunc9, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[33] = CreateFunction((void*)UnkFunc10, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[34] = CreateFunction((void*)UnkFunc11, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[35] = CreateFunction((void*)UnkFunc12, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[36] = CreateFunction((void*)UnkFunc13, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[37] = CreateFunction((void*)UnkFunc14, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[38] = CreateFunction((void*)UnkFunc15, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[39] = CreateFunction((void*)UnkFunc16, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[40] = CreateFunction((void*)UnkFunc17, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[41] = CreateFunction((void*)UnkFunc18, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[42] = CreateFunction((void*)UnkFunc19, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[43] = CreateFunction((void*)UnkFunc20, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[44] = CreateFunction((void*)UnkFunc21, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[45] = CreateFunction((void*)UnkFunc22, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[46] = CreateFunction((void*)UnkFunc23, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[47] = CreateFunction((void*)NullSub2, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[48] = CreateFunction((void*)UnkFunc24, (void*)g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[49] = CreateFunction((void*)UnkFunc25, (void*)g_CVModelInfoServerInterface);
}
