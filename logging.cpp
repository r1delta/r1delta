#include "logging.h"
#include <windows.h>
#include "cvar.h"
#include "bitbuf.h"
#include <intrin.h>
#include <MinHook.h>
#include <memory>
#include "load.h"
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

bool recursive = false;

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


int __cdecl vsnprintf_l_hk(
	char* const Buffer,
	const size_t BufferCount,
	const char* const Format,
	const _locale_t Locale,
	va_list ArgList)
{
	int result;

	if (Format && (!BufferCount || Buffer)) {
		// Use vsnprintf to format the string
		result = vsnprintf(Buffer, BufferCount, Format, ArgList);

		if (result < 0 || result >= (int)BufferCount) {
			if (BufferCount > 0) {
				Buffer[BufferCount - 1] = '\0';  // Ensure null termination
			}
			return -1;
		}

		if (!recursive && is_interesting_format(Format) && strlen(Buffer) < 512) {
			recursive = true;
			Msg("%s\n", Buffer);
			recursive = false;
		}
		return result;
	}
	else {
		return -1;
	}
}
typedef void (*Cbuf_AddTextType)(int a1, const char* a2, unsigned int a3);
Cbuf_AddTextType Cbuf_AddTextOriginal;
static bool bDone = false;
void Cbuf_AddText(int a1, const char* a2, unsigned int a3) {
	if (!bDone) {
		bDone = true;
		OriginalCCVar_FindVar(cvarinterface, "cl_updaterate")->m_nFlags &= ~(FCVAR_HIDDEN | FCVAR_DEVELOPMENTONLY);
	}
	bool shouldLog = true;
	if (a2 == nullptr || *a2 == '\0' || strcmp_static(a2, "\n") == 0) {
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
	if (!strcmp(a2, "startupmenu")) // if someone can send commands into this buffer they can do far worse at that stage than disconnecting the client
		Cbuf_AddTextOriginal(a1, "net_secure 0\neverything_unlocked 1\n", a3);
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
const char* outstr;
bool isPrintingCVarDesc = false;
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
	if (isPrintingCVarDesc)
		outstr = _strdup(Destination);
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
	isPrintingCVarDesc = true;
	ConVar_PrintDescriptionOriginal(pVar);
	isPrintingCVarDesc = false;
	//static char* lastCVarName = 0;
	//if (lastCVarName && !strcmp(pVar->m_pszName, lastCVarName))
	//	return;
	//if (lastCVarName)
	//	free(lastCVarName);
	//lastCVarName = _strdup(pVar->m_pszName);
	const char* pStr = pVar->m_pszHelpString;
	if (pStr && *pStr)
	{
		Msg("%-80s - %.80s\n", outstr, pStr);
	}
	else
	{
		Msg("%-80s\n", outstr);
	}
	free((void*)outstr);
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
char* SafeFormat(const char* format, va_list args) {
	va_list args_copy;
	va_copy(args_copy, args);
	int size = vsnprintf(NULL, 0, format, args_copy);
	va_end(args_copy);

	if (size < 0) {
		return strdup("Error formatting string");
	}

	char* ret = (char*)malloc(size + 1);
	if (ret == NULL) {
		return strdup("Memory allocation error");
	}

	vsnprintf(ret, size + 1, format, args);
	return ret;
}

void MsgHook(const char* pMsg, ...) {
	va_list args;
	va_start(args, pMsg);
	char* formatted = SafeFormat(pMsg, args);
	va_end(args);
	if (!IsDedicatedServer())
	printf("%s", formatted);
	if (MsgOriginal) {
		MsgOriginal("%s", formatted);
	}
	free(formatted);
}

void WarningHook(const char* pMsg, ...) {
	va_list args;
	va_start(args, pMsg);
	char* formatted = SafeFormat(pMsg, args);
	va_end(args);

	printf("%s", formatted);
	if (WarningOriginal) {
		WarningOriginal("%s", formatted);
	}
	free(formatted);
}

void Warning_SpewCallStackHook(int iMaxCallStackLength, const char* pMsg, ...) {
	va_list args;
	va_start(args, pMsg);
	char* formatted = SafeFormat(pMsg, args);
	va_end(args);

	printf("%s", formatted);
	if (Warning_SpewCallStackOriginal) {
		Warning_SpewCallStackOriginal(iMaxCallStackLength, "%s", formatted);
	}
	free(formatted);
}

void DevMsgHook(int level, const char* pMsg, ...) {
	va_list args;
	va_start(args, pMsg);
	char* formatted = SafeFormat(pMsg, args);
	va_end(args);

	printf("%s", formatted);
	if (DevMsgOriginal) {
		DevMsgOriginal(level, "%s", formatted);
	}
	free(formatted);
}

void DevWarningHook(int level, const char* pMsg, ...) {
	va_list args;
	va_start(args, pMsg);
	char* formatted = SafeFormat(pMsg, args);
	va_end(args);

	printf("%s", formatted);
	if (DevWarningOriginal) {
		DevWarningOriginal(level, "%s", formatted);
	}
	free(formatted);
}

void ConColorMsgHook(const Color* clr, const char* pMsg, ...) {
	va_list args;
	va_start(args, pMsg);
	char* formatted = SafeFormat(pMsg, args);
	va_end(args);

	printf("%s", formatted);
	if (ConColorMsgOriginal) {
		ConColorMsgOriginal(*clr, "%s", formatted);
	}
	free(formatted);
}

void ConDMsgHook(const char* pMsg, ...) {
	va_list args;
	va_start(args, pMsg);
	char* formatted = SafeFormat(pMsg, args);
	va_end(args);

	printf("%s", formatted);
	if (ConDMsgOriginal) {
		ConDMsgOriginal("%s", formatted);
	}
	free(formatted);
}

void COM_TimestampedLogHook(const char* pMsg, ...) {
	va_list args;
	va_start(args, pMsg);
	char* formatted = SafeFormat(pMsg, args);
	va_end(args);

	printf("%s", formatted);
	if (COM_TimestampedLogOriginal) {
		COM_TimestampedLogOriginal("%s", formatted);
	}
	free(formatted);
}
extern "C" __declspec(dllexport) void Error(const char* pMsg, ...) {
	if (strcmp(pMsg, "UserMessageBegin:  Unregistered message '%s'\n") == 0 ||
		strcmp(pMsg, "MESSAGE_END called with no active message\n") == 0) {
		return;
	}

	va_list args;
	va_start(args, pMsg);
	std::string formatted = SafeFormat(pMsg, args);
	va_end(args);

	reinterpret_cast<WarningFn>(GetProcAddress(GetModuleHandleA("tier0_orig.dll"), "Error"))("%s", formatted.c_str());
}

void InitLoggingHooks()
{
	auto tier0 = GetModuleHandleA("tier0.dll");
	MH_CreateHook((LPVOID)GetProcAddress(tier0, "Msg"), &MsgHook, reinterpret_cast<LPVOID*>(&MsgOriginal));
	MH_CreateHook((LPVOID)GetProcAddress(tier0, "Warning"), &WarningHook, reinterpret_cast<LPVOID*>(&WarningOriginal));
	MH_CreateHook((LPVOID)GetProcAddress(tier0, "Warning_SpewCallStack"), &Warning_SpewCallStackHook, reinterpret_cast<LPVOID*>(&Warning_SpewCallStackOriginal));
	MH_CreateHook((LPVOID)GetProcAddress(tier0, "DevMsg"), &DevMsgHook, reinterpret_cast<LPVOID*>(&DevMsgOriginal));
	MH_CreateHook((LPVOID)GetProcAddress(tier0, "DevWarning"), &DevWarningHook, reinterpret_cast<LPVOID*>(&DevWarningOriginal));
	MH_CreateHook((LPVOID)GetProcAddress(tier0, "ConColorMsg"), &ConColorMsgHook, reinterpret_cast<LPVOID*>(&ConColorMsgOriginal));
	MH_CreateHook((LPVOID)GetProcAddress(tier0, "ConDMsgHook"), &ConDMsgHook, reinterpret_cast<LPVOID*>(&ConDMsgOriginal));
	MH_CreateHook((LPVOID)GetProcAddress(tier0, "COM_TimestampedLog"), &COM_TimestampedLogHook, reinterpret_cast<LPVOID*>(&COM_TimestampedLogOriginal));

	MH_EnableHook(MH_ALL_HOOKS);
}