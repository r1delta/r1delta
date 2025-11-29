#include "model_info_vtable.h"
#include "vtable.h"

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
	// R1O VModelInfoServer has additional TFO-specific functions inserted
	// Original R1 vtable indices are remapped to account for these additions
	g_r1oCVModelInfoServerInterface[0] = VTABLE_TRAMPOLINE(dtor_0, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[1] = VTABLE_TRAMPOLINE(GetModel, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[2] = VTABLE_TRAMPOLINE(GetModelIndex, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[3] = VTABLE_TRAMPOLINE(GetModelName, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[4] = VTABLE_TRAMPOLINE(GetVCollide, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[5] = VTABLE_TRAMPOLINE(GetVCollideEx, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[6] = VTABLE_TRAMPOLINE(GetVCollideEx2, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[7] = VTABLE_TRAMPOLINE(GetModelRenderBounds, g_CVModelInfoServerInterface);
	// TFO-specific stubs (indices 8-15)
	g_r1oCVModelInfoServerInterface[8] = VTABLE_TRAMPOLINE(CModelInfo__UnkSetFlag, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[9] = VTABLE_TRAMPOLINE(CModelInfo__UnkClearFlag, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[10] = VTABLE_TRAMPOLINE(CModelInfo__GetFlag, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[11] = VTABLE_TRAMPOLINE(CModelInfo__UnkTFOVoid, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[12] = VTABLE_TRAMPOLINE(CModelInfo__UnkTFOVoid2, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[13] = VTABLE_TRAMPOLINE(CModelInfo__ShouldRet0, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[14] = VTABLE_TRAMPOLINE(CModelInfo__UnkTFOVoid3, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[15] = VTABLE_TRAMPOLINE(CModelInfo__ClientFullyConnected, g_CVModelInfoServerInterface);
	// Rest of vtable shifted
	g_r1oCVModelInfoServerInterface[16] = VTABLE_TRAMPOLINE(GetModelFrameCount, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[17] = VTABLE_TRAMPOLINE(GetModelType, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[18] = VTABLE_TRAMPOLINE(CModelInfo__UnkTFOShouldRet0_2, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[19] = VTABLE_TRAMPOLINE(GetModelExtraData, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[20] = VTABLE_TRAMPOLINE(IsTranslucentTwoPass, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[21] = VTABLE_TRAMPOLINE(ModelHasMaterialProxy, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[22] = VTABLE_TRAMPOLINE(IsTranslucent, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[23] = VTABLE_TRAMPOLINE(NullSub1, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[24] = VTABLE_TRAMPOLINE(UnkFunc1, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[25] = VTABLE_TRAMPOLINE(UnkFunc2, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[26] = VTABLE_TRAMPOLINE(UnkFunc3, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[27] = VTABLE_TRAMPOLINE(UnkFunc4, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[28] = VTABLE_TRAMPOLINE(UnkFunc5, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[29] = VTABLE_TRAMPOLINE(UnkFunc6, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[30] = VTABLE_TRAMPOLINE(UnkFunc7, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[31] = VTABLE_TRAMPOLINE(UnkFunc8, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[32] = VTABLE_TRAMPOLINE(UnkFunc9, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[33] = VTABLE_TRAMPOLINE(UnkFunc10, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[34] = VTABLE_TRAMPOLINE(UnkFunc11, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[35] = VTABLE_TRAMPOLINE(UnkFunc12, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[36] = VTABLE_TRAMPOLINE(UnkFunc13, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[37] = VTABLE_TRAMPOLINE(UnkFunc14, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[38] = VTABLE_TRAMPOLINE(UnkFunc15, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[39] = VTABLE_TRAMPOLINE(UnkFunc16, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[40] = VTABLE_TRAMPOLINE(UnkFunc17, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[41] = VTABLE_TRAMPOLINE(UnkFunc18, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[42] = VTABLE_TRAMPOLINE(UnkFunc19, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[43] = VTABLE_TRAMPOLINE(UnkFunc20, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[44] = VTABLE_TRAMPOLINE(UnkFunc21, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[45] = VTABLE_TRAMPOLINE(UnkFunc22, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[46] = VTABLE_TRAMPOLINE(UnkFunc23, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[47] = VTABLE_TRAMPOLINE(NullSub2, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[48] = VTABLE_TRAMPOLINE(UnkFunc24, g_CVModelInfoServerInterface);
	g_r1oCVModelInfoServerInterface[49] = VTABLE_TRAMPOLINE(UnkFunc25, g_CVModelInfoServerInterface);
}
