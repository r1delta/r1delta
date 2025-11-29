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

void AddBotDummyConCommand(const CCommand& args);
