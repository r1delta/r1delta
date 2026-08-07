// BugTrap-x64.dll stub for the R1O fake-dedicated server.
//
// The NEXON/TFO materialsystem_dx11.dll and launcher.dll statically import
// BugTrap-x64.dll (BT_SendSnapshot / BT_SetPreErrHandler), so the PE loader
// requires the module to be present even though R1Delta suppresses the real
// BugTrap crash reporter. The Wine dedicated-server image does not ship the
// Nexon BugTrap binary, which made the static import unresolvable and killed
// dedicated.dll's module-load pass (qword_180293290 never written -> NULL
// deref at dedicated+0x69B05). This stub satisfies the import with harmless
// no-ops; R1Delta's own crash handler remains the active one.
//
// Export names and ordinals mirror the real BugTrap-x64.dll so any module
// that binds by ordinal also resolves.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdint>

typedef LONG(WINAPI* PBT_ERRHANDLER)(EXCEPTION_POINTERS*);

#define BT_BOOL_FN(name) \
    extern "C" BOOL WINAPI name() { return TRUE; }
#define BT_BOOL_FN1(name) \
    extern "C" BOOL WINAPI name(void* /*a1*/) { return TRUE; }
#define BT_BOOL_FN2(name) \
    extern "C" BOOL WINAPI name(void* /*a1*/, void* /*a2*/) { return TRUE; }
#define BT_BOOL_FN3(name) \
    extern "C" BOOL WINAPI name(void* /*a1*/, void* /*a2*/, void* /*a3*/) { return TRUE; }
#define BT_BOOL_FN1D(name) \
    extern "C" BOOL WINAPI name(unsigned int /*a1*/) { return TRUE; }
#define BT_VOID_FN2(name) \
    extern "C" void WINAPI name(void* /*a1*/, unsigned int /*a2*/) {}
#define BT_VOID_FN1D(name) \
    extern "C" void WINAPI name(unsigned int /*a1*/) {}
#define BT_DWORD_FN(name) \
    extern "C" unsigned int WINAPI name() { return 0; }
#define BT_DWORD_FN1D(name) \
    extern "C" unsigned int WINAPI name(unsigned int /*a1*/) { return 0; }
#define BT_PTR_FN(name) \
    extern "C" void* WINAPI name() { return nullptr; }
#define BT_PTR_FN1D(name) \
    extern "C" void* WINAPI name(unsigned int /*a1*/) { return nullptr; }
#define BT_FILTER_FN(name) \
    extern "C" LONG WINAPI name(EXCEPTION_POINTERS* /*ep*/) { return EXCEPTION_CONTINUE_SEARCH; }

// --- file / registry attachment -------------------------------------------
BT_BOOL_FN1(BT_AddLogFile)
BT_BOOL_FN1(BT_AddRegFile)
BT_BOOL_FN(BT_ClearLog)
BT_BOOL_FN(BT_ClearLogFiles)
BT_BOOL_FN1(BT_CloseLogFile)
BT_BOOL_FN1(BT_DeleteLogFile)
BT_BOOL_FN2(BT_ExportRegistryKey)
BT_BOOL_FN1(BT_FlushLogFile)
BT_BOOL_FN(BT_OpenLogFile)

// --- log entries -----------------------------------------------------------
BT_VOID_FN2(BT_AppLogEntry)
extern "C" void WINAPI BT_AppLogEntryF(unsigned int /*a1*/, const char* /*a2*/, ...) {}
extern "C" void WINAPI BT_AppLogEntryV(unsigned int /*a1*/, const char* /*a2*/, va_list /*a3*/) {}
BT_VOID_FN2(BT_InsLogEntry)
extern "C" void WINAPI BT_InsLogEntryF(unsigned int /*a1*/, const char* /*a2*/, ...) {}
extern "C" void WINAPI BT_InsLogEntryV(unsigned int /*a1*/, const char* /*a2*/, va_list /*a3*/) {}

// --- log query -------------------------------------------------------------
BT_BOOL_FN(BT_GetLogEchoMode)
BT_BOOL_FN1(BT_GetLogFileEntry)
BT_BOOL_FN3(BT_GetLogFileName)
BT_DWORD_FN(BT_GetLogFilesCount)
BT_DWORD_FN(BT_GetLogFlags)
BT_DWORD_FN(BT_GetLogLevel)
BT_DWORD_FN(BT_GetLogSizeInBytes)
BT_DWORD_FN(BT_GetLogSizeInEntries)
BT_BOOL_FN1D(BT_SetLogEchoMode)
BT_DWORD_FN1D(BT_SetLogFlags)
BT_DWORD_FN1D(BT_SetLogLevel)
BT_DWORD_FN1D(BT_SetLogSizeInBytes)
BT_DWORD_FN1D(BT_SetLogSizeInEntries)

// --- activity / app identity ----------------------------------------------
BT_DWORD_FN(BT_GetActivityType)
BT_BOOL_FN1D(BT_SetActivityType)
BT_BOOL_FN2(BT_GetAppName)
BT_BOOL_FN2(BT_GetAppVersion)
BT_BOOL_FN1(BT_SetAppName)
BT_BOOL_FN1(BT_SetAppVersion)
BT_BOOL_FN1D(BT_ReadVersionInfo)

// --- dump / report configuration ------------------------------------------
BT_DWORD_FN(BT_GetDumpType)
BT_DWORD_FN1D(BT_SetDumpType)
BT_DWORD_FN(BT_GetExitMode)
BT_DWORD_FN1D(BT_SetExitMode)
BT_DWORD_FN(BT_GetFlags)
BT_DWORD_FN1D(BT_SetFlags)
BT_PTR_FN(BT_GetModule)
BT_BOOL_FN1(BT_SetModule)

// --- dialog / user message -------------------------------------------------
BT_PTR_FN(BT_GetDialogMessage)
BT_PTR_FN(BT_GetUserMessage)
BT_BOOL_FN1(BT_SetDialogMessage)
BT_BOOL_FN1(BT_SetUserMessage)
BT_BOOL_FN1D(BT_SetUserMessageFromCode)

// --- support contact -------------------------------------------------------
BT_PTR_FN(BT_GetMailProfile)
BT_PTR_FN(BT_GetNotificationEMail)
BT_PTR_FN(BT_GetReportFilePath)
BT_DWORD_FN(BT_GetReportFormat)
BT_PTR_FN(BT_GetSupportEMail)
BT_PTR_FN(BT_GetSupportHost)
BT_DWORD_FN(BT_GetSupportPort)
BT_PTR_FN(BT_GetSupportURL)
BT_BOOL_FN1(BT_SetMailProfile)
BT_BOOL_FN1(BT_SetNotificationEMail)
BT_BOOL_FN1(BT_SetReportFilePath)
BT_DWORD_FN1D(BT_SetReportFormat)
BT_BOOL_FN1(BT_SetSupportEMail)
BT_BOOL_FN1(BT_SetSupportHost)
BT_BOOL_FN1D(BT_SetSupportPort)
BT_BOOL_FN1(BT_SetSupportServer)
BT_BOOL_FN1(BT_SetSupportURL)

// --- error handlers --------------------------------------------------------
BT_PTR_FN(BT_GetPostErrHandler)
BT_PTR_FN(BT_GetPreErrHandler)
BT_BOOL_FN1(BT_SetPostErrHandler)
BT_BOOL_FN1(BT_SetPreErrHandler)

// --- filters ---------------------------------------------------------------
BT_FILTER_FN(BT_CallCppFilter)
BT_FILTER_FN(BT_CallNetFilter)
BT_FILTER_FN(BT_CallSehFilter)
BT_FILTER_FN(BT_CppFilter)
BT_FILTER_FN(BT_NetFilter)
BT_FILTER_FN(BT_SehFilter)
BT_BOOL_FN(BT_InstallSehFilter)
BT_BOOL_FN(BT_UninstallSehFilter)
BT_BOOL_FN(BT_InterceptSUEF)

// --- snapshot / report -----------------------------------------------------
BT_BOOL_FN1D(BT_SendSnapshot)
extern "C" BOOL WINAPI BT_SendSnapshotEx(unsigned int /*a1*/, ...) { return TRUE; }
BT_BOOL_FN1D(BT_SaveSnapshot)
extern "C" BOOL WINAPI BT_SaveSnapshotEx(unsigned int /*a1*/, ...) { return TRUE; }
BT_BOOL_FN1D(BT_MailSnapshot)
extern "C" BOOL WINAPI BT_MailSnapshotEx(unsigned int /*a1*/, ...) { return TRUE; }

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}
