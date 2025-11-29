#pragma once

#include "cvar.h"

void security_fixes_init();
void security_fixes_engine(uintptr_t engine_base);
void security_fixes_server(uintptr_t engine_base, uintptr_t server_base);

// Name sanitization
typedef void (*CBaseClientSetNameType)(__int64 thisptr, const char* name);
extern CBaseClientSetNameType CBaseClientSetNameOriginal;
void __fastcall HookedCBaseClientSetName(__int64 thisptr, const char* name);

// Sign-on state validation
extern bool (*oNET_SignOnState__ReadFromBuffer)(void* thisptr, bf_read& buffer);
bool NET_SignOnState__ReadFromBuffer(void* thisptr, bf_read& buffer);

// String command validation
extern bool (*oNET_StringCmd__ReadFromBuffer)(void* thisptr, bf_read& buffer);
bool NET_StringCmd__ReadFromBuffer(void* thisptr, bf_read& buffer);

// Squirrel client command handler
extern bool (*oHandleSquirrelClientCommand)(__int64 player, CCommand* args);
bool HandleSquirrelClientCommand(__int64 player, CCommand* args);