#include "bitbuf.h"
#include "thirdparty/silver-bun/silver-bun.h"
#include "cvar.h"
#include "persistentdata.h"
#include "logging.h"
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

