// Chat system hooks for R1Delta
#pragma once

#include "core.h"

extern void (*oCServerGameDLL_OnSayTextMsg)(void* pThis, int clientIndex, char* text, char isTeamChat);
void __fastcall CServerGameDLL_OnSayTextMsg(void* pThis, int clientIndex, char* text, char isTeamChat);
