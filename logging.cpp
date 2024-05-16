#include "logging.h"
#include <windows.h>

// Import the functions from tier0.dll
typedef void (*MsgFn)(const char*, ...);
typedef void (*WarningFn)(const char*, ...);
typedef void (*WarningSpewCallStackFn)(int, const char*, ...);
typedef void (*DevMsgFn)(int, const char*, ...);
typedef void (*DevWarningFn)(int, const char*, ...);
typedef void (*ConColorMsgFn)(const Color&, const char*, ...);
typedef void (*ConMsgFn)(const char*, ...);
typedef void (*ConDMsgFn)(const char*, ...);
typedef void (*COMTimestampedLogFn)(const char*, ...);

MsgFn Msg = (MsgFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "Msg");
WarningFn Warning = (WarningFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "Warning");
WarningSpewCallStackFn Warning_SpewCallStack = (WarningSpewCallStackFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "Warning_SpewCallStack");
DevMsgFn DevMsg = (DevMsgFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "DevMsg");
DevWarningFn DevWarning = (DevWarningFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "DevWarning");
ConColorMsgFn ConColorMsg = (ConColorMsgFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "ConColorMsg");
ConMsgFn ConMsg = (ConMsgFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "ConMsg");
ConDMsgFn ConDMsg = (ConDMsgFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "ConDMsg");
COMTimestampedLogFn COM_TimestampedLog = (COMTimestampedLogFn)GetProcAddress(GetModuleHandleA("tier0.dll"), "COM_TimestampedLog");

