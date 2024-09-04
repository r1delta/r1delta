#include "bitbuf.h"
#include "thirdparty/silver-bun/silver-bun.h"
#include "cvar.h"
#include "persistentdata.h"
#include "logging.h"
#include "squirrel.h"
#include "keyvalues.h"
#include "factory.h"
bool IsValidUserInfoKey(const char* key) {
	if (!key)
		return false;
	bool isValidKey = true;
	for (const char* c = key; *c != '\0'; ++c) {
		if (!(*c >= 'A' && *c <= 'Z') &&
			!(*c >= 'a' && *c <= 'z') &&
			!(*c >= '0' && *c <= '9') &&
			*c != '_') {
			isValidKey = false;
			break;
		}
	}
	return isValidKey;
}
bool IsValidUserInfoValue(const char* value) {
	if (!value)
		return false;
	bool isValidValue = true;
	const char* blacklist = "{}()':;\"\n";

	for (const char* c = value; *c != '\0'; ++c) {
		for (const char* b = blacklist; *b != '\0'; ++b) {
			if (*c == *b) {
				isValidValue = false;
				break;
			}
		}
		if (!isValidValue) {
			break;
		}
	}

	return isValidValue;
}
void setinfopersist_cmd(const CCommand& args)
{
	static CModule engine("engine.dll");
	static auto setinfo_cmd = CMemory(engine.GetModuleBase()).OffsetSelf(0x5B520).RCast<decltype(&setinfopersist_cmd)>();
	static auto setinfo_cmd_flags = CMemory(engine.GetModuleBase()).OffsetSelf(0x05B5FF).RCast<int*>();
	static auto ccommand_constructor = CMemory(engine.GetModuleBase()).OffsetSelf(0x4806F0).RCast<void(*)(CCommand * thisptr, int nArgC, const char** ppArgV)>();
	static bool bUnprotectedFlags = false;
	if (!bUnprotectedFlags) {
		bUnprotectedFlags = true;
		DWORD out;
		VirtualProtect(setinfo_cmd_flags, sizeof(int), PAGE_EXECUTE_READWRITE, &out);
	}
	*setinfo_cmd_flags = FCVAR_PERSIST_MASK;
	if (args.ArgC() >= 3) {
		if (!IsValidUserInfoKey(args.Arg(1))) {
			Warning("Invalid user info key %s. Only alphanumeric characters and underscores are allowed.\n", args.Arg(1));
			return;
		}
		if (!IsValidUserInfoValue(args.Arg(2))) {
			Warning("Invalid user info value %s. Only uh... I forget the rules in code, help!.\n", args.Arg(1));
			return;
		}
		const char* newArgv[CCommand::COMMAND_MAX_ARGC];
		newArgv[0] = args.Arg(0);
		char modifiedKey[CCommand::COMMAND_MAX_LENGTH];
		snprintf(modifiedKey, sizeof(modifiedKey), PERSIST_COMMAND" %s", args.Arg(1));
		newArgv[1] = modifiedKey;
		for (int i = 2; i < args.ArgC(); ++i) {
			newArgv[i] = args.Arg(i);
		}
		void* pMemory = malloc(sizeof(CCommand));
		if (pMemory) {
			ccommand_constructor(static_cast<CCommand*>(pMemory), args.ArgC(), newArgv);
			setinfo_cmd(*static_cast<CCommand*>(pMemory));
			free(pMemory);
		}
		else {
			Msg("Failed to allocate memory for CCommand\n");
			setinfo_cmd(args);
		}
	}
	else if (args.ArgC() == 2) {
		auto result = OriginalCCVar_FindVar(cvarinterface, args.GetCommandString());
		if (result)
			ConVar_PrintDescription(result);
	}
	else {
		setinfo_cmd(args);
	}
	*setinfo_cmd_flags = FCVAR_USERINFO;
}
__int64 CConVar__GetSplitScreenPlayerSlot(char* fakethisptr)
{
	ConVarR1* thisptr = (ConVarR1*)(fakethisptr - 48); // man
	if ((thisptr->m_nFlags & FCVAR_PERSIST))
		return -1; // :)
	return 0LL;
}

NET_SetConVar__ReadFromBufferType NET_SetConVar__ReadFromBufferOriginal;

bool NET_SetConVar__ReadFromBuffer(NET_SetConVar* thisptr, bf_read& buffer)
{
	uint32_t numvars;
	uint8_t byteCount = buffer.ReadByte();

	if (byteCount == static_cast<uint8_t>(-1))
	{
		// New format: read UBitVar
		numvars = buffer.ReadUBitVar();
	}
	else
	{
		// Old format: byte count is the number of vars
		numvars = byteCount;
	}

	thisptr->m_ConVars.RemoveAll();
	for (uint32_t i = 0; i < numvars; i++)
	{
		NetMessageCvar_t var;
		buffer.ReadString(var.name, sizeof(var.name));
		buffer.ReadString(var.value, sizeof(var.value));
		thisptr->m_ConVars.AddToTail(var);
	}
	return !buffer.IsOverflowed();
}

bool NET_SetConVar__WriteToBuffer(NET_SetConVar* thisptr, bf_write& buffer)
{
	uint32_t numvars = thisptr->m_ConVars.Count();

	if (numvars < 255)
	{
		// Old format: just write the byte
		buffer.WriteByte(numvars);
	}
	else
	{
		// New format: write -1 byte, then UBitVar
		buffer.WriteByte(static_cast<uint8_t>(-1));
		buffer.WriteUBitVar(numvars);
	}

	for (uint32_t i = 0; i < numvars; i++)
	{
		NetMessageCvar_t* var = &thisptr->m_ConVars[i];
		buffer.WriteString(var->name);
		buffer.WriteString(var->value);
	}
	return !buffer.IsOverflowed();
}

struct CBaseClient
{
	_BYTE gap0[1040];
	KeyValues* m_ConVars;
};
CBaseClient* g_pClientArray;

SQInteger Script_ClientGetPersistentData(HSQUIRRELVM v, __int64 a2, __int64 a3) {
	//sq_pushstring(v, "TEST", -1);
	//return -1;
	SQInteger nargs = sq_gettop(0, v);

	if (nargs != 2) {
		return sq_throwerror(v, "Expected 1 parameter");
	}

	const SQChar* str;
	if (SQ_FAILED(sq_getstring(v, 2, &str))) {
		return sq_throwerror(v, "Parameter 1 must be a string");
	}

	auto var = OriginalCCVar_FindVar(cvarinterface, (std::string(PERSIST_COMMAND" ") + std::string(str)).c_str());
	auto value = "0";
	auto valueLen = strlen(value);
	if (!var) {
		Warning("Couldn't find persistent variable %s; defaulting to empty\n", str);
	}
	else {
		value = var->m_Value.m_pszString;
		valueLen = var->m_Value.m_StringLength;
	}
	sq_pushstring(v, value, valueLen);
	return 1;
}
SQInteger Script_ClientGetPersistentDataAsInt(HSQUIRRELVM v) {
	SQInteger nargs = sq_gettop(0, v);

	if (nargs != 2) {
		return sq_throwerror(v, "Expected 1 parameter");
	}

	const SQChar* str;
	if (SQ_FAILED(sq_getstring(v, 2, &str))) {
		return sq_throwerror(v, "Parameter 1 must be a string");
	}

	auto var = OriginalCCVar_FindVar(cvarinterface, (std::string("__ ") + std::string(str)).c_str());
	auto value = 0;
	if (!var) {
		Warning("Couldn't find persistent variable %s; defaulting to 0", str);
	}
	else {
		value = var->m_Value.m_nValue;
	}
	sq_pushinteger(nullptr, v, value);
	return 1;
}


SQInteger Script_ServerGetUserInfoKVString(HSQUIRRELVM v)
{
	const void* pPlayer = sq_getentity(v, 2);
	if (!pPlayer)
	{
		sq_throwerror(v, "player is null");
		return -1;
	}

	const char* pKey, * pDefaultValue;
	sq_getstring(v, 3, &pKey);
	sq_getstring(v, 4, &pDefaultValue);
	if (!IsValidUserInfoValue(pKey) || !IsValidUserInfoValue(pDefaultValue)) {
		sq_throwerror(v, "Invalid user info key or value.");
		return -1;
	}

	auto v2 = *(__int64*)(((__int64)pPlayer + 64));
	auto index = ((v2 - (__int64)(pGlobalVarsServer->pEdicts)) / 56) - 1;
	if (!g_pClientArray[index].m_ConVars) {
		sq_throwerror(v, "Client has NULL m_ConVars.");
		return -1;
	}
	const char* pResult = g_pClientArray[index].m_ConVars->GetString(pKey, pDefaultValue); // (◣_◢)
	sq_pushstring(v, pResult, -1);
	return 1;
}


SQInteger Script_ServerSetUserInfoKVString(HSQUIRRELVM v)
{
	const void* pPlayer = sq_getentity(v, 2);
	if (!pPlayer)
	{
		sq_throwerror(v, "player is null");
		return -1;
	}

	const char* pKey, * pValue;
	sq_getstring(v, 3, &pKey);
	sq_getstring(v, 4, &pValue);
	if (!IsValidUserInfoValue(pKey) || !IsValidUserInfoValue(pValue)) {
		sq_throwerror(v, "Invalid user info key or value.");
		return -1;
	}

	auto v2 = *(__int64*)(((__int64)pPlayer + 64));
	auto index = ((v2 - (__int64)(pGlobalVarsServer->pEdicts)) / 56) - 1;
	if (!g_pClientArray[index].m_ConVars) {
		sq_throwerror(v, "Client has NULL m_ConVars.");
		return -1;
	}
	g_pClientArray[index].m_ConVars->SetString(pKey, pValue); // (◣_◢)
	sq_pushstring(v, pValue, -1);
	return 1;
}