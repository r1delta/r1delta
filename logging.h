#pragma once
#include <stdarg.h>
#include "cvar.h"

extern "C" __declspec(dllimport) void Msg(const char* _Printf_format_string_ pMsg, ...);
extern "C" __declspec(dllimport) void Warning(const char* _Printf_format_string_ pMsg, ...);
extern "C" __declspec(dllimport) void Warning_SpewCallStack(int iMaxCallStackLength, const char* _Printf_format_string_ pMsg, ...);
extern "C" __declspec(dllimport) void DevMsg(int level, const char* _Printf_format_string_ pMsg, ...);
extern "C" __declspec(dllimport) void DevWarning(int level, const char* _Printf_format_string_ pMsg, ...);
extern "C" __declspec(dllimport) void ConColorMsg(const Color& clr, const char* _Printf_format_string_ pMsg, ...);
extern "C" __declspec(dllimport) void ConDMsg(const char* _Printf_format_string_ pMsg, ...);
extern "C" __declspec(dllimport) void COM_TimestampedLog(const char* _Printf_format_string_ fmt, ...);
void InitLoggingHooks();
typedef void (*ConVar_PrintDescriptionType)(const ConCommandBaseR1* pVar);
extern ConVar_PrintDescriptionType ConVar_PrintDescriptionOriginal;
void ConVar_PrintDescription(const ConCommandBaseR1* pVar);
bool __fastcall SVC_Print_Process_Hook(__int64 a1);
void sub_18027F2C0(__int64 a1, const char* a2, void* a3);
int __cdecl vsnprintf_l_hk(
	char* const Buffer,
	const size_t BufferCount,
	const char* const Format,
	const _locale_t Locale,
	va_list ArgList);
void Cbuf_AddText(int a1, const char* a2, unsigned int a3);
char* __fastcall sub_1804722E0(char* Destination, const char* a2, unsigned __int64 a3, __int64 a4);
typedef void (*sub_18027F2C0Type)(__int64 a1, const char* a2, void* a3);
extern sub_18027F2C0Type sub_18027F2C0Original;
typedef void (*Cbuf_AddTextType)(int a1, const char* a2, unsigned int a3);
extern Cbuf_AddTextType Cbuf_AddTextOriginal;
