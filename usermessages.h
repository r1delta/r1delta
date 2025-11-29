// User message hooks for R1Delta
#pragma once

#include "core.h"

typedef __int64 (*UserMessage_ReorderHook_t)(__int64 a1, const char* a2, int a3);
extern UserMessage_ReorderHook_t UserMessage_ReorderHook_Original;

__int64 __fastcall UserMessage_ReorderHook(__int64 a1, const char* a2, int a3);
