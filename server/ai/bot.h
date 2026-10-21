// Bot management system for R1Delta
#pragma once

#include "core.h"
#include "factory.h"

class CPluginBotManager
{
public:
    virtual void* GetBotController(uint16_t* pEdict);
    virtual __int64 CreateBot(const char* botname);
};

extern bool isCreatingBot;
extern int botTeamIndex;

extern __int64 (*oCPortal_Player__ChangeTeam)(__int64 thisptr, unsigned int index);
__int64 __fastcall CPortal_Player__ChangeTeam(__int64 thisptr, unsigned int index);

// Creates a fake-client bot on the given team, named `name` (or "BotNN" when null/empty).
// Writes the final name to outName when provided.
bool CreateDummyBot(int teamIndex, const char* name, char* outName, size_t outNameSize);

void AddBotDummyConCommand(const CCommand& args);
