#include "load.h"
wchar_t kNtDll[] = L"ntdll.dll";
char kLdrRegisterDllNotification[] = "LdrRegisterDllNotification";
char kLdrUnregisterDllNotification[] = "LdrUnregisterDllNotification";
void* dll_notification_cookie_;