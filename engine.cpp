#include "engine.h"
#include "load.h"

CVEngineServer* g_CVEngineServer = 0;
uintptr_t g_CVEngineServerInterface;
uintptr_t g_r1oCVEngineServerInterface[203];

int64_t FuncThatReturnsFF_Stub()
{
	return 0xFFFFFFFFi64;
}

bool FuncThatReturnsBool_Stub()
{
	return false;
}

void Host_Error(const char* error, ...) {
	char string[1024];
	va_list params;

	va_start(params, error);
	((void(__cdecl*)(const char*, ...))(G_engine + 0x130510))(error, params);
	va_end(error);
}

int64_t NULLNET_Config()
{
	return 0;
}

char* NULLNET_GetPacket(int a1, int64_t a2, uint8_t a3)
{
	return 0;
}

int64_t(*CVEngineServer__GetNETConfig_TFO(int64_t a1, int64_t(**a2)()))()
{
	*a2 = NULLNET_Config;
	return NULLNET_Config;
}

char* (*CVEngineServer__GetNETGetPacket_TFO(
	int64_t a1,
	char* (** a2)(int a1, int64_t a2, uint8_t a3)))(int a1, int64_t a2, uint8_t a3)
{
	*a2 = NULLNET_GetPacket;
	return NULLNET_GetPacket;
}

int64_t(*CVEngineServer__GetLocalNETConfig_TFO(int64_t a1, int64_t(**a2)()))()
{
	*a2 = NULLNET_Config;
	return NULLNET_Config;
}

char* (*CVEngineServer__GetLocalNETGetPacket_TFO(
	int64_t a1,
	char* (** a2)(int a1, int64_t a2, uint8_t a3)))(int a1, int64_t a2, uint8_t a3)
{
	*a2 = NULLNET_GetPacket;
	return NULLNET_GetPacket;
}

CVEngineServer::CVEngineServer(uintptr_t* r1vtable)
{
	if (IsDedicatedServer())
		__InitDedi(r1vtable);
	else
		__InitNormal(r1vtable);

	CreateR1OVTable();
}

void CVEngineServer::CreateR1OVTable()
{
	g_r1oCVEngineServerInterface[0] = CreateFunction(((void*)CVEngineServer::NullSub1), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[1] = CreateFunction(((void*)CVEngineServer::ChangeLevel), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[2] = CreateFunction(((void*)CVEngineServer::IsMapValid), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[3] = CreateFunction(((void*)CVEngineServer::GetMapCRC), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[4] = CreateFunction(((void*)CVEngineServer::IsDedicatedServer), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[5] = CreateFunction(((void*)CVEngineServer::IsInEditMode), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[6] = CreateFunction(((void*)CVEngineServer::GetLaunchOptions), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[7] = CreateFunction(((void*)(uintptr_t)(&CVEngineServer__GetLocalNETConfig_TFO)), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[8] = CreateFunction(((void*)(uintptr_t)(&CVEngineServer__GetNETConfig_TFO)), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[9] = CreateFunction(((void*)(uintptr_t)(&CVEngineServer__GetLocalNETGetPacket_TFO)), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[10] = CreateFunction(((void*)(uintptr_t)(&CVEngineServer__GetNETGetPacket_TFO)), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[11] = CreateFunction(((void*)CVEngineServer::PrecacheModel), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[12] = CreateFunction(((void*)CVEngineServer::PrecacheDecal), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[13] = CreateFunction(((void*)CVEngineServer::PrecacheGeneric), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[14] = CreateFunction(((void*)CVEngineServer::IsModelPrecached), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[15] = CreateFunction(((void*)CVEngineServer::IsDecalPrecached), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[16] = CreateFunction(((void*)CVEngineServer::IsGenericPrecached), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[17] = CreateFunction(((void*)CVEngineServer::IsSimulating), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[18] = CreateFunction(((void*)CVEngineServer::GetPlayerUserId), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[19] = CreateFunction(((void*)CVEngineServer::GetPlayerUserId), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[20] = CreateFunction(((void*)CVEngineServer::GetPlayerNetworkIDString), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[21] = CreateFunction(((void*)CVEngineServer::IsUserIDInUse), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[22] = CreateFunction(((void*)CVEngineServer::GetLoadingProgressForUserID), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[23] = CreateFunction(((void*)CVEngineServer::GetEntityCount), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[24] = CreateFunction(((void*)CVEngineServer::GetPlayerNetInfo), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[25] = CreateFunction(((void*)CVEngineServer::IsClientValid), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[26] = CreateFunction(((void*)CVEngineServer::PlayerChangesTeams), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[27] = CreateFunction(((void*)CVEngineServer::RequestClientScreenshot), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[28] = CreateFunction(((void*)CVEngineServer::CreateEdict), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[29] = CreateFunction(((void*)CVEngineServer::RemoveEdict), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[30] = CreateFunction(((void*)CVEngineServer::PvAllocEntPrivateData), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[31] = CreateFunction(((void*)CVEngineServer::FreeEntPrivateData), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[32] = CreateFunction(((void*)CVEngineServer::SaveAllocMemory), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[33] = CreateFunction(((void*)CVEngineServer::SaveFreeMemory), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[34] = CreateFunction(((void*)CVEngineServer::FadeClientVolume), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[35] = CreateFunction(((void*)CVEngineServer::ServerCommand), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[36] = CreateFunction(((void*)CVEngineServer::CBuf_Execute), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[37] = CreateFunction(((void*)CVEngineServer::ClientCommand), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[38] = CreateFunction(((void*)CVEngineServer::LightStyle), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[39] = CreateFunction(((void*)CVEngineServer::StaticDecal), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[40] = CreateFunction(((void*)CVEngineServer::EntityMessageBeginEx), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[41] = CreateFunction(((void*)CVEngineServer::EntityMessageBegin), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[42] = CreateFunction(((void*)CVEngineServer::UserMessageBegin), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[43] = CreateFunction(((void*)CVEngineServer::MessageEnd), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[44] = CreateFunction(((void*)CVEngineServer::ClientPrintf), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[45] = CreateFunction(((void*)CVEngineServer::Con_NPrintf), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[46] = CreateFunction(((void*)CVEngineServer::Con_NXPrintf), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[47] = CreateFunction(((void*)CVEngineServer::UnkFunc1), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[48] = CreateFunction(((void*)CVEngineServer::NumForEdictinfo), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[49] = CreateFunction(((void*)CVEngineServer::UnkFunc2), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[50] = CreateFunction(((void*)CVEngineServer::UnkFunc3), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[51] = CreateFunction(((void*)CVEngineServer::CrosshairAngle), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[52] = CreateFunction(((void*)CVEngineServer::GetGameDir), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[53] = CreateFunction(((void*)CVEngineServer::CompareFileTime), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[54] = CreateFunction(((void*)CVEngineServer::LockNetworkStringTables), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[55] = CreateFunction(((void*)CVEngineServer::CreateFakeClient), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[56] = CreateFunction(((void*)CVEngineServer::GetClientConVarValue), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[57] = CreateFunction(((void*)CVEngineServer::ReplayReady), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[58] = CreateFunction(((void*)CVEngineServer::GetReplayFrame), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[59] = CreateFunction(((void*)CVEngineServer::UnkFunc4), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[60] = CreateFunction(((void*)CVEngineServer::UnkFunc5), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[61] = CreateFunction(((void*)CVEngineServer::UnkFunc6), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[62] = CreateFunction(((void*)CVEngineServer::UnkFunc7), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[63] = CreateFunction(((void*)CVEngineServer::UnkFunc8), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[64] = CreateFunction(((void*)CVEngineServer::ParseFile), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[65] = CreateFunction(((void*)CVEngineServer::CopyLocalFile), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[66] = CreateFunction(((void*)CVEngineServer::PlaybackTempEntity), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[67] = CreateFunction(((void*)CVEngineServer::UnkFunc9), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[68] = CreateFunction(((void*)CVEngineServer::LoadGameState), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[69] = CreateFunction(((void*)CVEngineServer::LoadAdjacentEnts), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[70] = CreateFunction(((void*)CVEngineServer::ClearSaveDir), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[71] = CreateFunction(((void*)CVEngineServer::GetMapEntitiesString), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[72] = CreateFunction(((void*)CVEngineServer::GetPlaylistCount), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[73] = CreateFunction(((void*)CVEngineServer::GetUnknownPlaylistKV), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[74] = CreateFunction(((void*)CVEngineServer::GetPlaylistValPossible), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[75] = CreateFunction(((void*)CVEngineServer::GetPlaylistValPossibleAlt), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[76] = CreateFunction(((void*)CVEngineServer::AddPlaylistOverride), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[77] = CreateFunction(((void*)CVEngineServer::MarkPlaylistReadyForOverride), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[78] = CreateFunction(((void*)CVEngineServer::UnknownPlaylistSetup), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[79] = CreateFunction(((void*)CVEngineServer::GetUnknownPlaylistKV2), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[80] = CreateFunction(((void*)CVEngineServer::GetUnknownPlaylistKV3), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[81] = CreateFunction(((void*)CVEngineServer::GetUnknownPlaylistKV4), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[82] = CreateFunction(((void*)CVEngineServer::UnknownMapSetup), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[83] = CreateFunction(((void*)CVEngineServer::UnknownMapSetup2), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[84] = CreateFunction(((void*)CVEngineServer::UnknownMapSetup3), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[85] = CreateFunction(((void*)CVEngineServer::UnknownMapSetup4), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[86] = CreateFunction(((void*)CVEngineServer::UnknownGamemodeSetup), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[87] = CreateFunction(((void*)CVEngineServer::UnknownGamemodeSetup2), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[88] = CreateFunction(((void*)CVEngineServer::IsMatchmakingDedi), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[89] = CreateFunction(((void*)CVEngineServer::UnusedTimeFunc), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[90] = CreateFunction(((void*)CVEngineServer::IsClientSearching), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[91] = CreateFunction(((void*)CVEngineServer::UnkFunc11), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[92] = CreateFunction(((void*)CVEngineServer::IsPrivateMatch), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[93] = CreateFunction(((void*)CVEngineServer::IsCoop), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[94] = CreateFunction(((void*)CVEngineServer::GetSkillFlag_Unused), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[95] = CreateFunction(((void*)CVEngineServer::TextMessageGet), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[96] = CreateFunction(((void*)CVEngineServer::UnkFunc12), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[97] = CreateFunction(((void*)CVEngineServer::LogPrint), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[98] = CreateFunction(((void*)CVEngineServer::IsLogEnabled), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[99] = CreateFunction(((void*)CVEngineServer::IsWorldBrushValid), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[100] = CreateFunction(((void*)CVEngineServer::SolidMoved), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[101] = CreateFunction(((void*)CVEngineServer::TriggerMoved), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[102] = CreateFunction(((void*)CVEngineServer::CreateSpatialPartition), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[103] = CreateFunction(((void*)CVEngineServer::DestroySpatialPartition), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[104] = CreateFunction(((void*)CVEngineServer::UnkFunc13), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[105] = CreateFunction(((void*)CVEngineServer::IsPaused), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[106] = CreateFunction(((void*)CVEngineServer::UnkFunc14), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[107] = CreateFunction(((void*)CVEngineServer::UnkFunc15), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[108] = CreateFunction(((void*)CVEngineServer::UnkFunc16), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[109] = CreateFunction(((void*)CVEngineServer::UnkFunc17), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[110] = CreateFunction(((void*)CVEngineServer::UnkFunc18), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[111] = CreateFunction(((void*)CVEngineServer::UnkFunc19), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[112] = CreateFunction(((void*)CVEngineServer::UnkFunc20), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[113] = CreateFunction(((void*)CVEngineServer::UnkFunc21), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[114] = CreateFunction(((void*)CVEngineServer::UnkFunc22), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[115] = CreateFunction(((void*)CVEngineServer::UnkFunc23), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[116] = CreateFunction(((void*)CVEngineServer::UnkFunc24), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[117] = CreateFunction(((void*)CVEngineServer::UnkFunc25), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[118] = CreateFunction(((void*)CVEngineServer::UnkFunc26), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[119] = CreateFunction(((void*)CVEngineServer::UnkFunc27), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[120] = CreateFunction(((void*)CVEngineServer::UnkFunc28), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[121] = CreateFunction(((void*)CVEngineServer::UnkFunc29), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[122] = CreateFunction(((void*)CVEngineServer::UnkFunc30), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[123] = CreateFunction(((void*)CVEngineServer::UnkFunc31), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[124] = CreateFunction(((void*)CVEngineServer::UnkFunc32), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[125] = CreateFunction(((void*)CVEngineServer::UnkFunc33), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[126] = CreateFunction(((void*)CVEngineServer::UnkFunc34), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[127] = CreateFunction(((void*)CVEngineServer::InsertServerCommand), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[128] = CreateFunction(((void*)CVEngineServer::UnkFunc35), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[129] = CreateFunction(((void*)CVEngineServer::UnkFunc36), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[130] = CreateFunction(((void*)CVEngineServer::UnkFunc37), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[131] = CreateFunction(((void*)CVEngineServer::UnkFunc38), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[132] = CreateFunction(((void*)CVEngineServer::UnkFunc39), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[133] = CreateFunction(((void*)CVEngineServer::UnkFunc40), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[134] = CreateFunction(((void*)CVEngineServer::UnkFunc41), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[135] = CreateFunction(((void*)CVEngineServer::UnkFunc42), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[136] = CreateFunction(((void*)CVEngineServer::UnkFunc43), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[137] = CreateFunction(((void*)CVEngineServer::UnkFunc44), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[138] = CreateFunction(((void*)CVEngineServer::AllocLevelStaticData), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[139] = CreateFunction(((void*)CVEngineServer::UnkFunc45), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[140] = CreateFunction(((void*)CVEngineServer::UnkFunc46), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[141] = CreateFunction(((void*)CVEngineServer::UnkFunc47), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[142] = CreateFunction(((void*)CVEngineServer::Pause), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[143] = CreateFunction(((void*)CVEngineServer::UnkFunc48), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[144] = CreateFunction(((void*)CVEngineServer::UnkFunc49), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[145] = CreateFunction(((void*)CVEngineServer::UnkFunc50), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[146] = CreateFunction(((void*)CVEngineServer::NullSub1), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[147] = CreateFunction(((void*)CVEngineServer::UnkFunc51), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[148] = CreateFunction(((void*)CVEngineServer::UnkFunc52), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[149] = CreateFunction(((void*)CVEngineServer::MarkTeamsAsBalanced_On), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[150] = CreateFunction(((void*)CVEngineServer::MarkTeamsAsBalanced_Off), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[151] = CreateFunction(((void*)CVEngineServer::UnkFunc53), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[152] = CreateFunction(((void*)CVEngineServer::UnkFunc54), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[153] = CreateFunction(((void*)CVEngineServer::UnkFunc55), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[154] = CreateFunction(((void*)CVEngineServer::UnkFunc56), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[155] = CreateFunction(((void*)CVEngineServer::DisconnectClient), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[156] = CreateFunction(((void*)CVEngineServer::UnkFunc58), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[157] = CreateFunction(((void*)CVEngineServer::UnkFunc59), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[158] = CreateFunction(((void*)CVEngineServer::UnkFunc60), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[159] = CreateFunction(((void*)CVEngineServer::UnkFunc61), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[160] = CreateFunction(((void*)CVEngineServer::UnkFunc62), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[161] = CreateFunction(((void*)CVEngineServer::UnkFunc63), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[162] = CreateFunction(((void*)CVEngineServer::NullSub2), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[163] = CreateFunction(((void*)CVEngineServer::UnkFunc64), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[164] = CreateFunction(((void*)CVEngineServer::NullSub3), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[165] = CreateFunction(((void*)CVEngineServer::NullSub4), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[166] = CreateFunction(((void*)CVEngineServer::UnkFunc65), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[167] = CreateFunction(((void*)CVEngineServer::UnkFunc66), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[168] = CreateFunction(((void*)CVEngineServer::UnkFunc67), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[169] = CreateFunction(((void*)CVEngineServer::UnkFunc68), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[170] = CreateFunction(((void*)CVEngineServer::UnkFunc69), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[171] = CreateFunction(((void*)CVEngineServer::FuncThatReturnsFF_12), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[172] = CreateFunction(((void*)CVEngineServer::FuncThatReturnsFF_11), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[173] = CreateFunction(((void*)CVEngineServer::FuncThatReturnsFF_10), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[174] = CreateFunction(((void*)CVEngineServer::FuncThatReturnsFF_9), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[175] = CreateFunction(((void*)CVEngineServer::FuncThatReturnsFF_8), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[176] = CreateFunction(((void*)CVEngineServer::FuncThatReturnsFF_7), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[177] = CreateFunction(((void*)CVEngineServer::FuncThatReturnsFF_6), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[178] = CreateFunction(((void*)CVEngineServer::FuncThatReturnsFF_5), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[179] = CreateFunction(((void*)CVEngineServer::FuncThatReturnsFF_4), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[180] = CreateFunction(((void*)CVEngineServer::FuncThatReturnsFF_3), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[181] = CreateFunction(((void*)CVEngineServer::FuncThatReturnsFF_2), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[182] = CreateFunction(((void*)CVEngineServer::FuncThatReturnsFF_1), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[183] = CreateFunction(((void*)CVEngineServer::GetClientXUID), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[184] = CreateFunction(((void*)CVEngineServer::IsActiveApp), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[185] = CreateFunction(((void*)CVEngineServer::UnkFunc70), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[186] = CreateFunction(((void*)CVEngineServer::Bandwidth_WriteBandwidthStatsToFile), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[187] = CreateFunction(((void*)CVEngineServer::UnkFunc71), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[188] = CreateFunction(((void*)CVEngineServer::IsCheckpointMapLoad), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[189] = CreateFunction(((void*)CVEngineServer::UnkFunc72), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[190] = CreateFunction(((void*)CVEngineServer::UnkFunc73), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[191] = CreateFunction(((void*)CVEngineServer::UnkFunc74), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[192] = CreateFunction(((void*)CVEngineServer::UnkFunc75), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[193] = CreateFunction(((void*)CVEngineServer::UnkFunc76), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[194] = CreateFunction(((void*)CVEngineServer::NullSub5), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[195] = CreateFunction(((void*)CVEngineServer::NullSub6), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[196] = CreateFunction(((void*)CVEngineServer::UpdateClientHashtag), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[197] = CreateFunction(((void*)CVEngineServer::UnkFunc77), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[198] = CreateFunction(((void*)CVEngineServer::UnkFunc78), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[199] = CreateFunction(((void*)CVEngineServer::UnkFunc79), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[200] = CreateFunction(((void*)CVEngineServer::UnkFunc80), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[201] = CreateFunction(((void*)CVEngineServer::UnkFunc82), (void*)g_CVEngineServerInterface);
	g_r1oCVEngineServerInterface[202] = CreateFunction(((void*)CVEngineServer::UnkFunc81), (void*)g_CVEngineServerInterface);
}
