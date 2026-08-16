#include "logging.h"
#include "cbuf_initialization_guard.h"
#include <windows.h>
#include "cvar.h"
#include "bitbuf.h"
#include <intrin.h>
#include <MinHook.h>
#include <memory>
#include <iostream>
#include "load.h"
#include "persistentdata.h"
#include "tctx.h"
#pragma intrinsic(_ReturnAddress)
typedef void (*MsgFn)(const char*, ...);
typedef void (*WarningFn)(const char*, ...);
typedef void (*WarningSpewCallStackFn)(int, const char*, ...);
typedef void (*DevMsgFn)(int, const char*, ...);
typedef void (*DevWarningFn)(int, const char*, ...);
typedef void (*ConColorMsgFn)(const Color&, const char*, ...);
typedef void (*ConDMsgFn)(const char*, ...);
typedef void (*COMTimestampedLogFn)(const char*, ...);

//MsgFn Msg = (MsgFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "Msg");
//WarningFn Warning = (WarningFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "Warning");
#if 0
WarningSpewCallStackFn Warning_SpewCallStack = (WarningSpewCallStackFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "Warning_SpewCallStack");
DevMsgFn DevMsg = (DevMsgFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "DevMsg");
DevWarningFn DevWarning = (DevWarningFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "DevWarning");
ConColorMsgFn ConColorMsg = (ConColorMsgFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "ConColorMsg");
ConDMsgFn ConDMsg = (ConDMsgFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "ConDMsg");
COMTimestampedLogFn COM_TimestampedLog = (COMTimestampedLogFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "COM_TimestampedLog");
#else
#if 0
extern "C" __declspec(dllimport) WarningSpewCallStackFn Warning_SpewCallStack;
extern "C" __declspec(dllimport) DevMsgFn DevMsg;
extern "C" __declspec(dllimport) DevWarningFn DevWarning;
extern "C" __declspec(dllimport) ConColorMsgFn ConColorMsg;
extern "C" __declspec(dllimport) ConDMsgFn ConDMsg;
extern "C" __declspec(dllimport) COMTimestampedLogFn COM_TimestampedLog;
#endif
#endif

bool __fastcall SVC_Print_Process_Hook(__int64 a1)
{
	char* text = *(char**)(a1 + 0x20);

	auto endpos = strlen(text);
	if (text[endpos - 1] == '\n')
		text[endpos - 1] = '\0'; // cut off repeated newline

	Msg("%s\n", text);
	return true;
}

bool logging_recursive = false;

bool is_interesting_format(const char* format) {
	bool has_space = false;
	int non_format_specifier_count = 0;

	while (*format) {
		if (*format == ' ') {
			has_space = true;
		}
		else if (*format == '%') {
			// Skip the format specifier
			format++;
			while (*format && strchr("diouxXeEfFgGaAcspn%", *format) == NULL) {
				format++;
			}
		}
		else {
			non_format_specifier_count++;
		}
		if (has_space && non_format_specifier_count >= 10) {
			return true;
		}
		format++;
	}
	return false;
}


//int __cdecl vsnprintf_l_hk(
//	char* const Buffer,
//	const size_t BufferCount,
//	const char* const Format,
//	const _locale_t Locale,
//	va_list ArgList)
//{
//	int result;
//
//	if (Format && (!BufferCount || Buffer)) {
//		// Use vsnprintf to format the string
//		result = vsnprintf(Buffer, BufferCount, Format, ArgList);
//
//		if (result < 0 || result >= (int)BufferCount) {
//			if (BufferCount > 0) {
//				Buffer[BufferCount - 1] = '\0';  // Ensure null termination
//			}
//			return -1;
//		}
//
//		if (!logging_recursive && is_interesting_format(Format) && strlen(Buffer) < 512) {
//			logging_recursive = true;
//			Msg("%s\n", Buffer);
//			logging_recursive = false;
//		}
//		return result;
//	}
//	else {
//		return -1;
//	}
//}
typedef void (*Cbuf_AddTextType)(int a1, const char* a2, unsigned int a3);
Cbuf_AddTextType Cbuf_AddTextOriginal;
static bool s_consoleCommandsUnhidden = false;
void Cbuf_AddText(int a1, const char* a2, unsigned int a3) {
	if (IsDedicatedServer() && !Cbuf_AddTextOriginal)
		Cbuf_AddTextOriginal = (Cbuf_AddTextType)(G_engine_ds + 0x72d70);
	ZoneScoped;
	ZoneText(a2, strlen(a2));
	
	r1delta::logging::TryUnhideConsoleCommands(
		s_consoleCommandsUnhidden,
		cvarinterface,
		OriginalCCVar_FindVar,
		OriginalCCVar_FindCommand,
		FCVAR_HIDDEN | FCVAR_DEVELOPMENTONLY);
	PData_OnConsoleCommand(a2);
	bool shouldLog = true;
	if (a2 == nullptr || *a2 == '\0' || *a2 == '_' || strcmp_static(a2, "\n") == 0 || (a2[0] == 'r' && a2[1] == 'e' && a2[2] == 's' && a2[3] == 'e')) {
		shouldLog = false;
	}
	auto engine = G_engine;
	uintptr_t returnToKeyInput = engine + 0x14E668;
	uintptr_t returnToKeyInput2 = engine + 0x14E5FB;
	uintptr_t returnToKeyInput3 = engine + 0x352AB;
	uintptr_t returnAddress = (uintptr_t)(_ReturnAddress());
	if ((returnAddress == returnToKeyInput) || (returnAddress == returnToKeyInput2) || (returnAddress == returnToKeyInput3)) {
		shouldLog = false;
	}
	if (!strcmp_static(a2, "startupmenu")) // if someone can send commands into this buffer they can do far worse at that stage than disconnecting the client
		Cbuf_AddTextOriginal(a1, "net_secure 0\n", a3);
	size_t len = strlen(a2);
	if (shouldLog) {
		if (len > 0 && (a2[len - 1] == '\n' || a2[len - 1] == '\r')) {
			// what's the point if you can just msg without the newline at the end???
			Msg("] %.*s\n", (int)(len - 1), a2);
		}
		else {
			Msg("] %s\n", a2);
		}
	}
	Cbuf_AddTextOriginal(a1, a2, a3);
}
static thread_local char* outstr = nullptr;
static thread_local bool isPrintingCVarDesc = false;

static void ClearCapturedConVarDescription()
{
	if (outstr) {
		free(outstr);
		outstr = nullptr;
	}
}

static void CaptureConVarDescription(const char* description)
{
	if (!isPrintingCVarDesc || !description)
		return;

	ClearCapturedConVarDescription();
	outstr = _strdup(description);
}

char* __fastcall sub_1804722E0(char* Destination, const char* a2, unsigned __int64 a3, __int64 a4)
{
	unsigned __int64 v6; // kr08_8
	signed __int64 v7; // rcx
	char* result; // rax

	v6 = strlen(Destination) + 1;
	v7 = strlen(a2);
	if (a4 > -1 && a4 < v7)
		v7 = a4;
	if (v6 - 1 + v7 >= a3)
		v7 = a3 - v6;
	if (!v7)
		return Destination;
	result = strncat(Destination, a2, v7);
	result[a3 - 1] = 0;
	CaptureConVarDescription(Destination);
	return result;
}
typedef void (*ConVar_PrintDescriptionType)(const ConCommandBaseR1* pVar);
ConVar_PrintDescriptionType ConVar_PrintDescriptionOriginal;
void ConVar_PrintDescription(const ConCommandBaseR1* pVar)
{
	//bool bMin, bMax;
	//float fMin, fMax;
	//const char* pStr;
	//
	//Color clr;
	//clr.SetColor(255, 100, 100, 255);
	//
	//char outstr[4096];
	//outstr[0] = 0;
	//
	//if (!((*(unsigned __int8 (**)(void))(*((uintptr_t*)(pVar)) + 8))()) )
	//{
	//	ConVarR1* var = (ConVarR1*)pVar;
	//	const ConVarR1* pBounded = 0;//dynamic_cast<const ConVar_ServerBounded*>(var);
	//
	//	bMin = var->m_bHasMin;
	//	bMax = var->m_bHasMax;
	//	fMin = var->m_fMinVal;
	//	fMax = var->m_fMaxVal;
	//	const char* value = NULL;
	//	char tempVal[32];
	//
	//	if (pBounded || var->m_nFlags & (FCVAR_NEVER_AS_STRING))
	//	{
	//		value = tempVal;
	//
	//		int intVal = pBounded ? pBounded->m_Value.m_nValue : var->m_Value.m_nValue;
	//		float floatVal = pBounded ? pBounded->m_Value.m_fValue : var->m_Value.m_fValue;
	//
	//		if (fabs((float)intVal - floatVal) < 0.000001)
	//		{
	//			snprintf(tempVal, sizeof(tempVal), "%d", intVal);
	//		}
	//		else
	//		{
	//			snprintf(tempVal, sizeof(tempVal), "%f", floatVal);
	//		}
	//	}
	//	else
	//	{
	//		value = var->m_Value.m_pszString;
	//	}
	//
	//	if (value)
	//	{
	//		AppendPrintf(outstr, sizeof(outstr), "\"%s\" = \"%s\"", var->m_pszName, value);
	//
	//		if (_stricmp(value, var->m_pszDefaultValue))
	//		{
	//			AppendPrintf(outstr, sizeof(outstr), " ( def. \"%s\" )", var->m_pszDefaultValue);
	//		}
	//	}
	//
	//	if (bMin)
	//	{
	//		AppendPrintf(outstr, sizeof(outstr), " min. %f", fMin);
	//	}
	//	if (bMax)
	//	{
	//		AppendPrintf(outstr, sizeof(outstr), " max. %f", fMax);
	//	}
	//
	//	// Handle virtualized cvars.
	//	if (pBounded && fabs(pBounded->m_Value.m_fValue - var->m_Value.m_fValue) > 0.0001f)
	//	{
	//		AppendPrintf(outstr, sizeof(outstr), " [%.3f server clamped to %.3f]",
	//			var->m_Value.m_fValue, pBounded->m_Value.m_fValue);
	//	}
	//}
	//else
	//{
	//	ConCommandR1* var = (ConCommandR1*)pVar;
	//
	//	AppendPrintf(outstr, sizeof(outstr), "\"%s\" ", var->m_pszName);
	//}
	//
	//ConVar_AppendFlags(pVar, outstr, sizeof(outstr));
	if (!pVar || !ConVar_PrintDescriptionOriginal)
		return;

	if (IsR1ODedicatedServer() && AreR1OFakeDediVerboseLogsEnabled()) {
		char diagnostic[384];
		_snprintf_s(
			diagnostic,
			sizeof(diagnostic),
			_TRUNCATE,
			"R1Delta: R1O recovered ConVar description name=%s help=%s\n",
			pVar->m_pszName ? pVar->m_pszName : "<unnamed>",
			pVar->m_pszHelpString ? pVar->m_pszHelpString : "");
		OutputDebugStringA(diagnostic);
	}

	ClearCapturedConVarDescription();
	isPrintingCVarDesc = true;
	ConVar_PrintDescriptionOriginal(pVar);
	isPrintingCVarDesc = false;
	//static char* lastCVarName = 0;
	//if (lastCVarName && !strcmp(pVar->m_pszName, lastCVarName))
	//	return;
	//if (lastCVarName)
	//	free(lastCVarName);
	//lastCVarName = _strdup(pVar->m_pszName);
	const char* description = outstr
		? outstr
		: (pVar->m_pszName ? pVar->m_pszName : "<unnamed>");
	const char* pStr = pVar->m_pszHelpString;
	if (pStr && *pStr)
	{
		Msg("%-80s - %.80s\n", description, pStr);
	}
	else
	{
		Msg("%-80s\n", description);
	}
	ClearCapturedConVarDescription();
}

namespace {

using R1OConVarAppendFlagsType = void(__fastcall*)(const ConCommandBaseR1O*, char*);
R1OConVarAppendFlagsType R1OConVarAppendFlagsOriginal = nullptr;
bool s_R1OConsoleLoggingHooksInstalled = false;

void __fastcall R1OConVarAppendFlags(const ConCommandBaseR1O* pVar, char* destination)
{
	if (R1OConVarAppendFlagsOriginal)
		R1OConVarAppendFlagsOriginal(pVar, destination);
	CaptureConVarDescription(destination);
}

bool R1OLoggingBytesMatch(uintptr_t address, const unsigned char* expected, size_t expectedSize)
{
	if (!address || !expected || !expectedSize)
		return false;

	MEMORY_BASIC_INFORMATION memory{};
	if (!VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory))
		|| memory.State != MEM_COMMIT
		|| (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
		return false;

	const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize;
	return address <= regionEnd
		&& expectedSize <= regionEnd - address
		&& memcmp(reinterpret_cast<const void*>(address), expected, expectedSize) == 0;
}

bool InstallCheckedR1OLoggingHook(
	uintptr_t engineR1OBase,
	uintptr_t rva,
	const unsigned char* expected,
	size_t expectedSize,
	void* detour,
	void** original,
	const char* name)
{
	const uintptr_t target = engineR1OBase + rva;
	if (!R1OLoggingBytesMatch(target, expected, expectedSize)) {
		Warning(
			"R1Delta: R1O console hook %s skipped: engine bytes did not match at rva=0x%llX\n",
			name,
			static_cast<unsigned long long>(rva));
		return false;
	}

	const MH_STATUS createStatus = MH_CreateHook(
		reinterpret_cast<void*>(target),
		detour,
		reinterpret_cast<LPVOID*>(original));
	const MH_STATUS enableStatus =
		(createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED)
		? MH_EnableHook(reinterpret_cast<void*>(target))
		: createStatus;
	const bool installed =
		enableStatus == MH_OK
		|| enableStatus == MH_ERROR_ENABLED
		|| enableStatus == MH_ERROR_ALREADY_CREATED;

	if (AreR1OFakeDediVerboseLogsEnabled()) {
		char diagnostic[256];
		_snprintf_s(
			diagnostic,
			sizeof(diagnostic),
			_TRUNCATE,
			"R1Delta: R1O console hook %s create=%d enable=%d target=%p original=%p\n",
			name,
			static_cast<int>(createStatus),
			static_cast<int>(enableStatus),
			reinterpret_cast<void*>(target),
			original ? *original : nullptr);
		OutputDebugStringA(diagnostic);
	}

	if (!installed) {
		Warning(
			"R1Delta: R1O console hook %s failed create=%d enable=%d rva=0x%llX\n",
			name,
			static_cast<int>(createStatus),
			static_cast<int>(enableStatus),
			static_cast<unsigned long long>(rva));
	}
	return installed;
}

} // namespace

void InstallR1OConsoleLoggingHooks(uintptr_t engineR1OBase)
{
	if (!IsR1ODedicatedServer() || !engineR1OBase || s_R1OConsoleLoggingHooksInstalled)
		return;

	static constexpr unsigned char kStrippedNoOpPrefix[] = {
		0x48, 0x89, 0x54, 0x24, 0x10, 0x4C, 0x89, 0x44,
		0x24, 0x18, 0x4C, 0x89, 0x4C, 0x24, 0x20, 0xC3
	};
	static constexpr unsigned char kStatusFormatterPrefix[] = {
		0x48, 0x89, 0x4C, 0x24, 0x08, 0x48, 0x89, 0x54,
		0x24, 0x10, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x4C,
		0x89, 0x4C, 0x24, 0x20
	};
	static constexpr unsigned char kConVarPrintPrefix[] = {
		0x40, 0x53, 0x57, 0xB8, 0x78, 0x10, 0x00, 0x00
	};
	static constexpr unsigned char kAppendFlagsPrefix[] = {
		0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
		0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x41, 0x56
	};

	bool allInstalled = true;
	// sub_180185910 (the status command) deliberately selects between two
	// printers.  The stripped +0x185900 leaf is the local dedicated-console
	// path.  +0x185860 is the intact remote-client path: it formats into the
	// requesting client's output interface, which becomes svc_Print.  Hooking
	// both leaves collapses remote status output into the server console.
	allInstalled &= InstallCheckedR1OLoggingHook(
		engineR1OBase, 0x185900,
		kStrippedNoOpPrefix, sizeof(kStrippedNoOpPrefix),
		reinterpret_cast<void*>(&Status_ConMsg), nullptr,
		"status-local-console");
	allInstalled &= InstallCheckedR1OLoggingHook(
		engineR1OBase, 0x5CD80,
		kStrippedNoOpPrefix, sizeof(kStrippedNoOpPrefix),
		reinterpret_cast<void*>(&Status_ConMsg), nullptr,
		"signon-state");
	allInstalled &= InstallCheckedR1OLoggingHook(
		engineR1OBase, 0xC7760,
		kStatusFormatterPrefix, sizeof(kStatusFormatterPrefix),
		reinterpret_cast<void*>(&Status_ConMsg), nullptr,
		"status-formatter");
	allInstalled &= InstallCheckedR1OLoggingHook(
		engineR1OBase, 0x1DB720,
		kStatusFormatterPrefix, sizeof(kStatusFormatterPrefix),
		reinterpret_cast<void*>(&Status_ConMsg), nullptr,
		"stripped-debug-formatter");
	allInstalled &= InstallCheckedR1OLoggingHook(
		engineR1OBase, 0x275C30,
		kAppendFlagsPrefix, sizeof(kAppendFlagsPrefix),
		reinterpret_cast<void*>(&R1OConVarAppendFlags),
		reinterpret_cast<void**>(&R1OConVarAppendFlagsOriginal),
		"convar-append-flags");
	allInstalled &= InstallCheckedR1OLoggingHook(
		engineR1OBase, 0x275DC0,
		kConVarPrintPrefix, sizeof(kConVarPrintPrefix),
		reinterpret_cast<void*>(&ConVar_PrintDescription),
		reinterpret_cast<void**>(&ConVar_PrintDescriptionOriginal),
		"convar-print-description");

	s_R1OConsoleLoggingHooksInstalled = allInstalled;
	if (allInstalled && AreR1OFakeDediVerboseLogsEnabled())
		OutputDebugStringA("R1Delta: installed R1O status and ConVar description hooks\n");
}

bool PrintR1OConVarDescriptionByName(const char* name)
{
	if (!IsR1ODedicatedServer() || !name || !*name || !cvarinterface)
		return false;

	ConCommandBaseR1O* command = CCVar_FindCommandBase(cvarinterface, name);
	if (!command)
		return false;

	ConVar_PrintDescription(reinterpret_cast<const ConCommandBaseR1*>(command));
	return true;
}


// Function pointer declarations using decltype
decltype(&Msg) MsgOriginal = nullptr;
decltype(&Warning) WarningOriginal = nullptr;
decltype(&Warning_SpewCallStack) Warning_SpewCallStackOriginal = nullptr;
decltype(&DevMsg) DevMsgOriginal = nullptr;
decltype(&DevWarning) DevWarningOriginal = nullptr;
decltype(&ConColorMsg) ConColorMsgOriginal = nullptr;
decltype(&ConDMsg) ConDMsgOriginal = nullptr;
decltype(&COM_TimestampedLog) COM_TimestampedLogOriginal = nullptr;
using OutputDebugStringAFn = void (WINAPI*)(LPCSTR);
static OutputDebugStringAFn OutputDebugStringAOriginal = nullptr;

bool AreR1OFakeDediVerboseLogsEnabled()
{
	// Keep R1O fake-dedi playable by default. Add either flag when debugging needs
	// central R1O diagnostic output forwarding; hot-path budgets may still be
	// compiled down separately while gameplay profiling is the priority.
	static int cached = -1;
	if (cached < 0) {
		const char* cmdLine = GetCommandLineA();
		cached = cmdLine && (strstr(cmdLine, "-r1o_fake_dedi_logs") || strstr(cmdLine, "-r1o_verbose_logs"));
	}
	return cached != 0;
}

static bool ContainsAnySevereLogToken(const char* text)
{
	return text
		&& (strstr(text, "fatal")
			|| strstr(text, "Fatal")
			|| strstr(text, "FATAL")
			|| strstr(text, "error")
			|| strstr(text, "Error")
			|| strstr(text, "ERROR")
			|| strstr(text, "crash")
			|| strstr(text, "Crash")
			|| strstr(text, "CRASH")
			|| strstr(text, "SERVER SCRIPT")
			|| strstr(text, "SCRIPT ERROR")
			|| strstr(text, "[r1delta_tier0_error]"));
}

bool ShouldSuppressR1OFakeDediLogText(const char* text)
{
	if (!IsR1ODedicatedServer() || AreR1OFakeDediVerboseLogsEnabled() || !text || !*text)
		return false;

	// Keep real failures visible; the slow path is the high-volume compatibility
	// diagnostics, not rare fatal/error reports.
	if (ContainsAnySevereLogToken(text))
		return false;

	return strstr(text, "R1Delta:")
		|| strstr(text, "[r1delta_core]");
}

static bool ShouldSuppressR1OFakeDediLogFormat(const char* format, va_list args)
{
	if (!IsR1ODedicatedServer() || AreR1OFakeDediVerboseLogsEnabled() || !format)
		return false;

	if (strcmp(format, "%s") == 0 || strcmp(format, "%s\n") == 0) {
		va_list argsCopy;
		va_copy(argsCopy, args);
		const char* text = va_arg(argsCopy, const char*);
		va_end(argsCopy);
		return ShouldSuppressR1OFakeDediLogText(text);
	}

	return ShouldSuppressR1OFakeDediLogText(format);
}

static void WINAPI OutputDebugStringAHook(LPCSTR text)
{
	if (ShouldSuppressR1OFakeDediLogText(text))
		return;

	if (OutputDebugStringAOriginal)
		OutputDebugStringAOriginal(text);
}

#if 0
char* SafeFormat(const char* format, va_list args) {
	va_list args_copy;
	va_copy(args_copy, args);
	int size = vsnprintf(NULL, 0, format, args_copy);
	va_end(args_copy);

	if (size < 0) {
		return _strdup("Error formatting string");
	}

	char* ret = (char*)malloc(size + 1);
	if (ret == NULL) {
		return _strdup("Memory allocation error");
	}

	vsnprintf(ret, size + 1, format, args);
	return ret;
}
#endif

char* SafeFormatArena(Arena* arena, const char* format, va_list args) {
	va_list args_copy;
	va_copy(args_copy, args);
	int size = vsnprintf(NULL, 0, format, args_copy);
	va_end(args_copy);

	if (size < 0) {
		return arena_strdup(arena, "Error formatting string");
	}

	size_t str_size = size_t(size) + 1;
	char* ret = (char*)memset(arena_push(arena, str_size), 0, str_size);
	// TODO(mrsteyk): idk why this check is here with strdup usage...
	//                handle more gracefully.
	if (ret == NULL) {
		return arena_strdup(arena, "Memory allocation error");
	}

	vsnprintf(ret, str_size, format, args);
	return ret;
}

void MsgHook(const char* pMsg, ...) {
	ZoneScoped;

	auto arena = tctx.get_arena_for_scratch();
	auto temp = TempArena(arena);

	va_list args;
	va_start(args, pMsg);
	if (ShouldSuppressR1OFakeDediLogFormat(pMsg, args)) {
		va_end(args);
		return;
	}
	char* formatted = SafeFormatArena(arena, pMsg, args);
	va_end(args);

	if (ShouldSuppressR1OFakeDediLogText(formatted))
		return;

	if (!IsDedicatedServer()) printf("%s", formatted);
	if (MsgOriginal) {
		MsgOriginal("%s", formatted);
	}
}

void WarningHook(const char* pMsg, ...) {
	ZoneScoped;

	auto arena = tctx.get_arena_for_scratch();
	auto temp = TempArena(arena);

	va_list args;
	va_start(args, pMsg);
	if (ShouldSuppressR1OFakeDediLogFormat(pMsg, args)) {
		va_end(args);
		return;
	}
	char* formatted = SafeFormatArena(arena, pMsg, args);
	va_end(args);

	if (ShouldSuppressR1OFakeDediLogText(formatted))
		return;

	if (!IsDedicatedServer()) printf("%s", formatted);
	if (WarningOriginal) {
		WarningOriginal("%s", formatted);
	}
}

void Warning_SpewCallStackHook(int iMaxCallStackLength, const char* pMsg, ...) {
	ZoneScoped;

	auto arena = tctx.get_arena_for_scratch();
	auto temp = TempArena(arena);

	va_list args;
	va_start(args, pMsg);
	if (ShouldSuppressR1OFakeDediLogFormat(pMsg, args)) {
		va_end(args);
		return;
	}
	char* formatted = SafeFormatArena(arena, pMsg, args);
	va_end(args);

	if (ShouldSuppressR1OFakeDediLogText(formatted))
		return;

	if (!IsDedicatedServer()) printf("%s", formatted);
	if (Warning_SpewCallStackOriginal) {
		Warning_SpewCallStackOriginal(iMaxCallStackLength, "%s", formatted);
	}
}

void DevMsgHook(int level, const char* pMsg, ...) {
	ZoneScoped;

	auto arena = tctx.get_arena_for_scratch();
	auto temp = TempArena(arena);

	va_list args;
	va_start(args, pMsg);
	if (ShouldSuppressR1OFakeDediLogFormat(pMsg, args)) {
		va_end(args);
		return;
	}
	char* formatted = SafeFormatArena(arena, pMsg, args);
	va_end(args);

	if (ShouldSuppressR1OFakeDediLogText(formatted))
		return;

	if (!IsDedicatedServer()) printf("%s", formatted);
	if (DevMsgOriginal) {
		DevMsgOriginal(level, "%s", formatted);
	}
}

void DevWarningHook(int level, const char* pMsg, ...) {
	ZoneScoped;

	auto arena = tctx.get_arena_for_scratch();
	auto temp = TempArena(arena);

	va_list args;
	va_start(args, pMsg);
	if (ShouldSuppressR1OFakeDediLogFormat(pMsg, args)) {
		va_end(args);
		return;
	}
	char* formatted = SafeFormatArena(arena, pMsg, args);
	va_end(args);

	if (ShouldSuppressR1OFakeDediLogText(formatted))
		return;

	if (!IsDedicatedServer()) printf("%s", formatted);
	if (DevWarningOriginal) {
		DevWarningOriginal(level, "%s", formatted);
	}
}

void ConColorMsgHook(const Color* clr, const char* pMsg, ...) {
	ZoneScoped;

	auto arena = tctx.get_arena_for_scratch();
	auto temp = TempArena(arena);

	va_list args;
	va_start(args, pMsg);
	if (ShouldSuppressR1OFakeDediLogFormat(pMsg, args)) {
		va_end(args);
		return;
	}
	char* formatted = SafeFormatArena(arena, pMsg, args);
	va_end(args);

	if (ShouldSuppressR1OFakeDediLogText(formatted))
		return;

	if (!IsDedicatedServer()) printf("%s", formatted);
	if (ConColorMsgOriginal) {
		ConColorMsgOriginal(*clr, "%s", formatted);
	}
}

void ConDMsgHook(const char* pMsg, ...) {
	ZoneScoped;

	auto arena = tctx.get_arena_for_scratch();
	auto temp = TempArena(arena);

	va_list args;
	va_start(args, pMsg);
	if (ShouldSuppressR1OFakeDediLogFormat(pMsg, args)) {
		va_end(args);
		return;
	}
	char* formatted = SafeFormatArena(arena, pMsg, args);
	va_end(args);

	if (ShouldSuppressR1OFakeDediLogText(formatted))
		return;

	if (!IsDedicatedServer()) printf("%s", formatted);
	if (ConDMsgOriginal) {
		ConDMsgOriginal("%s", formatted);
	}
}

void COM_TimestampedLogHook(const char* pMsg, ...) {
	ZoneScoped;

	auto arena = tctx.get_arena_for_scratch();
	auto temp = TempArena(arena);

	va_list args;
	va_start(args, pMsg);
	if (ShouldSuppressR1OFakeDediLogFormat(pMsg, args)) {
		va_end(args);
		return;
	}
	char* formatted = SafeFormatArena(arena, pMsg, args);
	va_end(args);

	if (ShouldSuppressR1OFakeDediLogText(formatted))
		return;

	if (!IsDedicatedServer()) printf("%s", formatted);
	if (COM_TimestampedLogOriginal) {
		COM_TimestampedLogOriginal("%s", formatted);
	}
}
extern "C" __declspec(dllexport) void Error(const char* pMsg, ...) {
	ZoneScoped;
	
	if (strcmp_static(pMsg, "UserMessageBegin:  Unregistered message '%s'\n") == 0 ||
		strcmp_static(pMsg, "MESSAGE_END called with no active message\n") == 0) {
		return;
	}



#if 0
	va_list args;
	va_start(args, pMsg);
	std::string formatted = SafeFormat(pMsg, args);
	va_end(args);

	reinterpret_cast<WarningFn>(GetProcAddress(GetModuleHandleA("tier0_orig.dll"), "Error"))("%s", formatted.c_str());
#else
	auto arena = tctx.get_arena_for_scratch();
	auto temp = TempArena(arena);

	va_list args;
	va_start(args, pMsg);
	char* formatted = SafeFormatArena(arena, pMsg, args);
	va_end(args);

	printf("%s", formatted);
	OutputDebugStringA("[r1delta_tier0_error] ");
	OutputDebugStringA(formatted);

	reinterpret_cast<WarningFn>(GetProcAddress(GetModuleHandleA("tier0_orig.dll"), "Error"))("%s", formatted);
#endif
}

void InitLoggingHooks()
{
	auto tier0 = GetModuleHandleA("tier0.dll");
	auto createExportHook = [tier0](
		const char* exportName,
		LPVOID detour,
		LPVOID* original)
	{
		if (LPVOID target = reinterpret_cast<LPVOID>(
			GetProcAddress(tier0, exportName)))
		{
			MH_CreateHook(target, detour, original);
		}
	};

	createExportHook("Msg", reinterpret_cast<LPVOID>(&MsgHook), reinterpret_cast<LPVOID*>(&MsgOriginal));
	createExportHook("Warning", reinterpret_cast<LPVOID>(&WarningHook), reinterpret_cast<LPVOID*>(&WarningOriginal));
	createExportHook("Warning_SpewCallStack", reinterpret_cast<LPVOID>(&Warning_SpewCallStackHook), reinterpret_cast<LPVOID*>(&Warning_SpewCallStackOriginal));
	createExportHook("DevMsg", reinterpret_cast<LPVOID>(&DevMsgHook), reinterpret_cast<LPVOID*>(&DevMsgOriginal));
	createExportHook("DevWarning", reinterpret_cast<LPVOID>(&DevWarningHook), reinterpret_cast<LPVOID*>(&DevWarningOriginal));
	createExportHook("ConColorMsg", reinterpret_cast<LPVOID>(&ConColorMsgHook), reinterpret_cast<LPVOID*>(&ConColorMsgOriginal));
	createExportHook("ConDMsg", reinterpret_cast<LPVOID>(&ConDMsgHook), reinterpret_cast<LPVOID*>(&ConDMsgOriginal));
	createExportHook("COM_TimestampedLog", reinterpret_cast<LPVOID>(&COM_TimestampedLogHook), reinterpret_cast<LPVOID*>(&COM_TimestampedLogOriginal));

	if (!OutputDebugStringAOriginal) {
		HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
		if (kernel32)
			MH_CreateHook((LPVOID)GetProcAddress(kernel32, "OutputDebugStringA"), &OutputDebugStringAHook, reinterpret_cast<LPVOID*>(&OutputDebugStringAOriginal));
	}

	MH_EnableHook(MH_ALL_HOOKS);
}

// Status message hook for recovered stripped debug logging
void Status_ConMsg(const char* text, ...)
{
    char formatted[2048];
    va_list list;

    va_start(list, text);
    vsprintf_s(formatted, text, list);
    va_end(list);

	if (IsR1ODedicatedServer() && AreR1OFakeDediVerboseLogsEnabled()) {
		char diagnostic[2304];
		_snprintf_s(
			diagnostic,
			sizeof(diagnostic),
			_TRUNCATE,
			"R1Delta: R1O recovered console message: %s",
			formatted);
		OutputDebugStringA(diagnostic);
	}

    auto endpos = strlen(formatted);
    if (endpos && formatted[endpos - 1] == '\n')
        formatted[endpos - 1] = '\0';

    Msg("%s\n", formatted);
}

// Hook for Q_vsnprintf - recovers stripped debug logging
signed __int64 __fastcall LogStrippedDbgMessage_vsnprintf(char* a1, signed __int64 a2, const char* a3, va_list a4)
{
    static bool recursive2 = false;
    signed __int64 result;

    if (a2 <= 0)
        return 0i64;
    result = vsnprintf(a1, a2, a3, a4);
    if ((int)result < 0i64 || (int)result >= a2)
    {
        result = a2 - 1;
        a1[a2 - 1] = 0;
    }
    if (!recursive2) {
        recursive2 = true;
        Msg("%s\n", a1);
        recursive2 = false;
    }
    return result;
}

// Hook for Q_snprintf - recovers stripped debug logging
void LogStrippedDbgMessage_snprintf(char* a1, signed __int64 a2, const char* a3, ...)
{
    int v5;
    va_list ArgList;

    va_start(ArgList, a3);
    if (a2 > 0)
    {
        v5 = vsnprintf(a1, a2, a3, ArgList);
        if (v5 < 0i64 || v5 >= a2)
            a1[a2 - 1] = 0;
    }
    std::cout << a1 << std::endl;
}

// UTIL_LogPrintf hook for logging and broadcasting
void (*oUTIL_LogPrintf)(const char* fmt, ...) = nullptr;

void UTIL_LogPrintf(char* fmt, ...)
{
    char tempString[1024];
    va_list params;
    static int verboseLogBudget = 64;

    va_start(params, fmt);
    V_vsnprintf(tempString, 1024, fmt, params);
    va_end(params);

    if (IsR1ODedicatedServer() &&
        AreR1OFakeDediVerboseLogsEnabled() &&
        verboseLogBudget > 0) {
        --verboseLogBudget;
        char diagnostic[1280];
        _snprintf_s(
            diagnostic,
            sizeof(diagnostic),
            _TRUNCATE,
            "R1Delta: UTIL_LogPrintf hook invoked text=\"%.900s\"\n",
            tempString);
        OutputDebugStringA(diagnostic);
    }

    if (oUTIL_LogPrintf)
        oUTIL_LogPrintf("%s", tempString);
    if (IsDedicatedServer())
        Msg("%s", tempString);
    if (G_server) {
        auto UTIL_ClientPrintAll = reinterpret_cast<void(*)(unsigned int, char*)>(G_server + 0x25D5B0);
        UTIL_ClientPrintAll(2, tempString);
    }
}
