// Happiness has to be fought for.
#include <MinHook.h>
#include "load.h"
#include "core.h"
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{

	static bool done = false;
	if (!done) {
		done = true;
		if (!IsDedicatedServer())
			AllocConsole();
		HANDLE hConsoleStream = ::CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		SetStdHandle(STD_OUTPUT_HANDLE, hConsoleStream);

		VirtualAlloc((void*)0xFFEEFFEE, 1, MEM_RESERVE, PAGE_NOACCESS);
	}
	LoadLibraryA("TextShaping.dll"); // fix "Patcher Error" dialogs having no text
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH: {
		MH_Initialize();
		LdrRegisterDllNotificationFunc reg_fn =
			reinterpret_cast<LdrRegisterDllNotificationFunc>(::GetProcAddress(
				::GetModuleHandle(kNtDll), kLdrRegisterDllNotification));
		reg_fn(0, &LoaderNotificationCallback, 0, &dll_notification_cookie_);
		break; 
	}
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		MH_DisableHook(MH_ALL_HOOKS);
		MH_Uninitialize();
		break;
	}
	return TRUE;
}
