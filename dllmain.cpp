// Happiness has to be fought for.

// %*++***###*##**##++**+++*++*%%%%%%%+*%+#*+%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#=%%%#**#+#%
// ==----------------------------------------------------------------------=================+
// =------------------------------------::----------------------------------===---==========+
// ---------------------------------:-:--::::-::::-------------------=======================+
// =-------------------------------::::::::-::::-:::----------==============+===+++=========+
// ----------------------------::::::--:---=====----------===========++==++++++++++++++++++++
// ----------------------------:-----:---==++++++====-==========++++++++++++++++++++++++++++*
// -------------------------------------=+++++++=============++++++++++++++++++++++++++++++**
// -------------------------------------=++++*+========++++++++++++++++++++++++++++++++++++**
// ----------------------------:::::::--=+++++=======+++++++++++++++++++++++************++++*
// ---------------------::::::::::::::::-==+++===++++++++++++++++++++++++********###%%%##*++*
// -------:::::::::::::::::::::::::::::::-=====+####**+++++++++++++++++*********#%%%@@@@%%#**
// ------:-:::::::::::::::::::::::::::::::-====*%%%%#*++++++++++++++++++********##%@@@@@%%#**
// ----------::::::::::::::::::::::::::-=--====+#%%%*++++++++++++++++++++*********##%%%%%#***
// -------------=*=-:::::::::::::::::-=++======++***+++++++++++++++++++**************###*****
// -------------=*#=-------======++++*###*+=+=++=++++++++++*+++******************************
// =-----=======+*#*+++++++*****##########+=++++++++++***************************************
// +++++++++++****#################*****#*+=+++++++++****************************************
// ++**+++++++++++++======+++++++++++++****+=+++***################**************************
// *****+=--------::-::::::::::::::::::------=*#%%%%%%%%%%%%%%%%%%%#####*********************
// ******=-----------:::::::::::---:::::::::-=#%%%%@@@@@@@@@@@@@@%%%%###********************#
// ******=---------------:::::::::::-:::::::-*%%%@@@@@@@@@@@@@@@@@%%%%##********************#
// ****#*=-----------------:::::::::::::::::-=*%%@@@@@@@@@@@@@@@@@@%%##*********************#
// ******+===-------------::::::::::::---:::--=*#%%%@@@@@@@@@@@@@%%######**#**************###
// ==++==------------------:::::::::::::-------=+**##%%%@%%%%%%%%##########*****************#
// ==--------------------------::-:::::::::::---=++**##%%%%%%%%%%%##########*************####
// =--------------------------------:---::::--:--==+**###%#%%%%%%%%%%%#####**************####
// ====--------------------------:-------::-------==+++****###########******************#####
// ===--==------------------------------------::---==+++++******************************#####
// ===-------------------------------------:::-:----=+++********************************####%
// =====---------------------------------------------=++++******************************####%
// ======------------------==------------------------==+++***************************######%%
// =========-----===--------==------------------------==++********#*#####**#######*########%%

#include "core.h"
#include <MinHook.h>
#include "load.h"
#include "core.h"
#include "patcher.h"
#include "filesystem.h"
#include "logging.h"
#include "eos_network.h"
#include <tier0/platform.h>
#include <KnownFolders.h>

//- mrsteyk: override new/delete
#include "memory.h"

#if 1
#if defined(_MSC_VER) && defined(_Ret_notnull_) && defined(_Post_writable_byte_size_)
// stay consistent with VCRT definitions
#define mi_decl_new(n)          [[nodiscard]] __declspec(allocator) __declspec(restrict) _Ret_notnull_ _Post_writable_byte_size_(n)
#define mi_decl_new_nothrow(n)  [[nodiscard]] __declspec(allocator) __declspec(restrict) _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n)
#else
#define mi_decl_new(n)          [[nodiscard]] __declspec(allocator) __declspec(restrict)
#define mi_decl_new_nothrow(n)  [[nodiscard]] __declspec(allocator) __declspec(restrict)
#endif

void operator delete(void* p) noexcept { GlobalAllocator()->mi_free(p, TAG_NEW, HEAP_DELTA); };
void operator delete[](void* p) noexcept { GlobalAllocator()->mi_free(p, TAG_NEW, HEAP_DELTA); };

void operator delete  (void* p, const std::nothrow_t&) noexcept { GlobalAllocator()->mi_free(p, TAG_NEW, HEAP_DELTA); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { GlobalAllocator()->mi_free(p, TAG_NEW, HEAP_DELTA); }

mi_decl_new(n) void* operator new(std::size_t n) noexcept(false) { return GlobalAllocator()->mi_malloc(n, TAG_NEW, HEAP_DELTA); }
mi_decl_new(n) void* operator new[](std::size_t n) noexcept(false) { return GlobalAllocator()->mi_malloc(n, TAG_NEW, HEAP_DELTA); }

mi_decl_new_nothrow(n) void* operator new  (std::size_t n, const std::nothrow_t& tag) noexcept { (void)(tag); return GlobalAllocator()->mi_malloc(n, TAG_NEW, HEAP_DELTA); }
mi_decl_new_nothrow(n) void* operator new[](std::size_t n, const std::nothrow_t& tag) noexcept { (void)(tag); return GlobalAllocator()->mi_malloc(n, TAG_NEW, HEAP_DELTA); }

#if (__cplusplus >= 201402L || _MSC_VER >= 1916)
void operator delete  (void* p, std::size_t n) noexcept { GlobalAllocator()->mi_free_size(p, n, TAG_NEW, HEAP_DELTA); };
void operator delete[](void* p, std::size_t n) noexcept { GlobalAllocator()->mi_free_size(p, n, TAG_NEW, HEAP_DELTA); };
#endif

#if (__cplusplus > 201402L || defined(__cpp_aligned_new))
void operator delete  (void* p, std::align_val_t al) noexcept { GlobalAllocator()->mi_free_aligned(p, static_cast<size_t>(al), TAG_NEW, HEAP_DELTA); }
void operator delete[](void* p, std::align_val_t al) noexcept { GlobalAllocator()->mi_free_aligned(p, static_cast<size_t>(al), TAG_NEW, HEAP_DELTA); }
void operator delete  (void* p, std::size_t n, std::align_val_t al) noexcept { GlobalAllocator()->mi_free_size_aligned(p, n, static_cast<size_t>(al), TAG_NEW, HEAP_DELTA); };
void operator delete[](void* p, std::size_t n, std::align_val_t al) noexcept { GlobalAllocator()->mi_free_size_aligned(p, n, static_cast<size_t>(al), TAG_NEW, HEAP_DELTA); };
void operator delete  (void* p, std::align_val_t al, const std::nothrow_t&) noexcept { GlobalAllocator()->mi_free_aligned(p, static_cast<size_t>(al), TAG_NEW, HEAP_DELTA); }
void operator delete[](void* p, std::align_val_t al, const std::nothrow_t&) noexcept { GlobalAllocator()->mi_free_aligned(p, static_cast<size_t>(al), TAG_NEW, HEAP_DELTA); }

void* operator new  (std::size_t n, std::align_val_t al) noexcept(false) { return GlobalAllocator()->mi_malloc_aligned(n, static_cast<size_t>(al), TAG_NEW, HEAP_DELTA); }
void* operator new[](std::size_t n, std::align_val_t al) noexcept(false) { return GlobalAllocator()->mi_malloc_aligned(n, static_cast<size_t>(al), TAG_NEW, HEAP_DELTA); }
void* operator new  (std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept { return GlobalAllocator()->mi_malloc_aligned(n, static_cast<size_t>(al), TAG_NEW, HEAP_DELTA); }
void* operator new[](std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept { return GlobalAllocator()->mi_malloc_aligned(n, static_cast<size_t>(al), TAG_NEW, HEAP_DELTA); }
#endif
#endif

#include "crashhandler.h"
#if BUILD_PROFILE
#include "tracy-0.11.1/public/TracyClient.cpp"
#endif
#include <Shlwapi.h>
#include <ShlObj_core.h>
#include <PathCch.h>
#include <shellapi.h>
#pragma comment(lib, "Pathcch.lib")
uint64_t g_PerformanceFrequency;
int G_is_dedi;
typedef const char* (__cdecl* wine_get_version_func)();
CPUInformation* (__fastcall* GetCPUInformationOriginal)();

const CPUInformation* GetCPUInformationDet()
{
	CPUInformation* result = GetCPUInformationOriginal();

	// NOTE(mrsteyk): blame wanderer for SINGLEPLAYER SUPPORT WOWOWOWOWOW SO USEFUL
	// NOTE(mrsteyk): <=20 global thread pool "IOJob" for filesystem_stdio.dll
	//                 5 WT simple threads in vstdlib.dll WT_Init (25)
	//                 1 thread in filesystem_stdio.dll (26) ???
	//                 1 "RenderThread" simple thread in materialsystem_dx11.dll (27)
	//                 3 thread pool "GlobPool" in engine.dll due to g_pThreadPool->[4]() (30)
	//                 1 thread pool "SaveJob"? in engine.dll due to SaveRestore_Init (31)
	//                 1 simple thread in engine.dll due to QueuedPacketThread (32) (it disappeared?)
	//                 CRASH

	// NOTE(mrsteyk): Some threads want logical core count, wanderer want's singleplayer
	if (result->m_nLogicalProcessors >= 20) {
		result->m_nLogicalProcessors = 19;
	}

	return result;
}

// GetTFO* stub implementations
extern "C" {
	static void __fastcall StubFunction() {
		// 0xC3 is RET instruction - this function just returns immediately
		return;
	}

	// Create vtable with 256 function pointers all pointing to StubFunction
	static void* g_stubVTable[256] = {
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction,
		(void*)StubFunction, (void*)StubFunction, (void*)StubFunction, (void*)StubFunction
	};

	// Inner object with vtable pointer
	static struct {
		void** vtable = g_stubVTable;
	} g_innerObject;



	// GetTFO* function implementations
	__declspec(dllexport) void* GetTFOEnvironment() {
		return &g_innerObject;
	}

	__declspec(dllexport) void* GetTFOFileLogLevel() {
		return &g_innerObject;
	}

	__declspec(dllexport) void* GetTFOFileLogger() {
		return &g_innerObject;
	}

	__declspec(dllexport) void* GetTFOLoadingSplash() {
		return &g_innerObject;
	}
}

static void
nvapi_stuff()
{
	auto nv = LoadLibraryExW(L"nvapi64.dll", 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!nv)
	{
		return;
	}

	using nvapi_queryiface_t = void*(__fastcall*)(uintptr_t);
	auto nvapi_queryiface = (nvapi_queryiface_t)GetProcAddress(nv, "nvapi_QueryInterface");

	using nvapi_start_call_profile_t = int32_t(__fastcall*)(uintptr_t, uintptr_t*);
	using nvapi_end_call_profile_t = int32_t(__fastcall*)(uintptr_t, uintptr_t, uint64_t);
	auto nvapi_start_call = (nvapi_start_call_profile_t)nvapi_queryiface(0x33C7358C);
	auto nvapi_end_call = (nvapi_end_call_profile_t)nvapi_queryiface(0x593E8644);

	if (!nvapi_start_call || !nvapi_end_call)
	{
		nvapi_start_call = nullptr;
		nvapi_end_call = nullptr;
	}

	int32_t nvret = 0;
#define NVAPI_CALL(id, X) { uintptr_t hprofile; if (nvapi_start_call) nvapi_start_call(id, &hprofile); nvret = X; if (nvapi_end_call) nvapi_end_call(id, hprofile, nvret); }

	constexpr uintptr_t nvapi_get_session_id = 0x694D52E;
	using nvapi_get_session_t = int32_t(__fastcall*)(uintptr_t*);
	auto nvapi_get_session = (nvapi_get_session_t)nvapi_queryiface(nvapi_get_session_id);

	constexpr uintptr_t nvapi_end_session_id = 0xDAD9CFF8;
	using nvapi_end_session_t = int32_t(__fastcall*)(uintptr_t);
	auto nvapi_end_session = (nvapi_end_session_t)nvapi_queryiface(nvapi_end_session_id);

	constexpr uintptr_t nvapi_load_settings_id = 0x375DBD6B;
	using nvapi_load_settings_t = int32_t(__fastcall*)(uintptr_t);
	auto nvapi_load_settings = (nvapi_load_settings_t)nvapi_queryiface(nvapi_load_settings_id);

	constexpr uintptr_t nvapi_profile_by_name_id = 0x7E4A9A0B;
	using nvapi_profile_by_name_t = int32_t(__fastcall*)(uintptr_t, const wchar_t*, uintptr_t*);
	auto nvapi_profile_by_name = (nvapi_profile_by_name_t)nvapi_queryiface(nvapi_profile_by_name_id);

	constexpr uintptr_t nvapi_create_app_id = 0x4347A9DE;
	using nvapi_create_app_t = int32_t(__fastcall*)(uintptr_t, uintptr_t, void*);
	auto nvapi_create_app = (nvapi_create_app_t)nvapi_queryiface(nvapi_create_app_id);

	if (!nvapi_get_session || !nvapi_end_session || !nvapi_load_settings || !nvapi_profile_by_name_id || !nvapi_create_app)
	{
		return;
	}

	uintptr_t handle = 0;
	NVAPI_CALL(nvapi_get_session_id, nvapi_get_session(&handle));
	if (nvret)
	{
		printf("NVAPI: nvapi_get_session fail 0x%X (%d)", nvret, nvret);
		return;
	}

	NVAPI_CALL(nvapi_load_settings_id, nvapi_load_settings(handle));

	uintptr_t profile = 0;
	NVAPI_CALL(nvapi_profile_by_name_id, nvapi_profile_by_name(handle, L"TitanFall", &profile));
	if (nvret)
	{
		printf("NVAPI: nvapi_profile_by_name fail 0x%X (%d)", nvret, nvret);
		return;
	}
	else {
		struct app_info_t
		{
			uint32_t struct_ver;
			uint32_t blergh;
			wchar_t app_name[2048];
			wchar_t app_name2[2048];

			wchar_t string1[2048];
			wchar_t string2[2048];

			uint32_t flags;

			wchar_t string3[2048];
		};
		static_assert(sizeof(app_info_t) == 20492);

		app_info_t ai;
		memset(&ai, 0, sizeof(ai));
		ai.struct_ver = 0x4500C;

		memcpy(ai.app_name, L"r1delta.exe", sizeof(L"r1delta.exe"));
		memcpy(ai.app_name2, L"r1delta.exe", sizeof(L"r1delta.exe"));
		
		NVAPI_CALL(nvapi_create_app_id, nvapi_create_app(handle, profile, &ai));
		if (nvret)
		{
			// NOTE(mrsteyk): this can fail if we are already added to "TitanFall" profile.
			//printf("NVAPI: nvapi_create_app fail 0x%X (%d)", nvret, nvret);
			return;
		} else {
			NVAPI_CALL(nvapi_load_settings_id, nvapi_load_settings(handle));
			if (nvret)
			{
				printf("NVAPI: nvapi_load_settings fail 0x%X (%d)", nvret, nvret);
				return;
			}
			else
			{
				// ergh, idk?
			}
		}
	}

	NVAPI_CALL(nvapi_end_session_id, nvapi_end_session(handle));
}

static int skip_dllmain = -1;
bool Plat_IsInToolMode() {
		static HMODULE matsystem = 0;
		if (!matsystem) {
			matsystem = GetModuleHandleA("materialsystem_dx11.dll");
			return false;
		}
		HMODULE module2;
		BOOL check1 = GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCTSTR)_ReturnAddress(), &module2);
		BOOL check2 = module2 == (HMODULE)matsystem;
		return check1 && check2;
}


// Define the global storage in the .cpp file
static std::string g_r1delta_launch_args;

// Exported function for C# to call - No exceptions thrown by this function itself
extern "C" __declspec(dllexport) void SetR1DeltaLaunchArgs(const char* args)
{
	OutputDebugStringA("[r1delta_core] SetR1DeltaLaunchArgs called.\n");
	if (args)
	{
		// std::string assignment can technically throw std::bad_alloc if out of memory,
		// but this is a catastrophic failure anyway. We won't wrap this.
		g_r1delta_launch_args = args;
		OutputDebugStringA("[r1delta_core] Launch args set internally to: ");
		OutputDebugStringA(args);
		OutputDebugStringA("\n");
	}
	else
	{
		g_r1delta_launch_args.clear(); // Safe operation
		OutputDebugStringA("[r1delta_core] Launch args cleared (null passed).\n");
	}
}

// --- Modified CCommandLine__CreateCmdLine hook (No exceptions, no env var) ---
void (*CCommandLine__CreateCmdLine_Original)(void* thisptr, char* commandline);
void CCommandLine__CreateCmdLine(void* thisptr, char* commandline) {
	OutputDebugStringA("[r1delta_core] CCommandLine__CreateCmdLine hook entered.\n");
	std::string finalCmdLineStr;
	std::string gameArg; // Holds the "-game ..." part

	// 1. Start with original command line (unchanged)
	if (commandline != nullptr && commandline[0] != '\0') {
		finalCmdLineStr = commandline;
		OutputDebugStringA("[r1delta_core] Original command line: ");
		OutputDebugStringA(commandline);
		OutputDebugStringA("\n");
	}

	// 2. Construct -game argument (Using WinAPI, no exceptions)
	char exePathBuffer[MAX_PATH * 2]; // Slightly larger buffer just in case
	DWORD pathLen = GetModuleFileNameA(NULL, exePathBuffer, sizeof(exePathBuffer));

	if (pathLen > 0 && pathLen < sizeof(exePathBuffer)) {
		exePathBuffer[pathLen] = '\0'; // Ensure null termination
		char exeDirBuffer[MAX_PATH * 2];
		strncpy_s(exeDirBuffer, sizeof(exeDirBuffer), exePathBuffer, _TRUNCATE);

		// Remove the filename part to get the directory
		if (PathRemoveFileSpecA(exeDirBuffer)) {
			// Construct the r1delta path
			char r1deltaPathBuffer[MAX_PATH * 2 + 10]; // Space for "\\r1delta" + quotes + game arg
			// Check buffer size before concatenating
			if (strlen(exeDirBuffer) + strlen("\\r1delta") < sizeof(r1deltaPathBuffer)) {
				strncpy_s(r1deltaPathBuffer, sizeof(r1deltaPathBuffer), exeDirBuffer, _TRUNCATE);
				strncat_s(r1deltaPathBuffer, sizeof(r1deltaPathBuffer), "\\r1delta", _TRUNCATE);

				// Ensure path doesn't exceed buffer before adding quotes/arg
				if (strlen(r1deltaPathBuffer) + strlen("-game \"\"") < sizeof(exePathBuffer)) {
					// Use a temporary buffer for the final game arg construction
					char tempGameArg[sizeof(exePathBuffer)];
					sprintf_s(tempGameArg, sizeof(tempGameArg), "-game \"%s\"", r1deltaPathBuffer);
					gameArg = tempGameArg; // Assign to std::string

					OutputDebugStringA("[r1delta_core] Constructed game arg: ");
					OutputDebugStringA(gameArg.c_str());
					OutputDebugStringA("\n");
				}
				else {
					OutputDebugStringA("[r1delta_core] Warning: Constructed r1delta path too long for buffer.\n");
				}

			}
			else {
				OutputDebugStringA("[r1delta_core] Warning: Exe directory path too long to append \\r1delta.\n");
			}
		}
		else {
			OutputDebugStringA("[r1delta_core] Warning: PathRemoveFileSpecA failed.\n");
		}
	}
	else if (pathLen == 0) {
		DWORD error = GetLastError();
		char errorMsg[100];
		sprintf_s(errorMsg, sizeof(errorMsg), "[r1delta_core] Warning: GetModuleFileNameA failed. Error: %lu\n", error);
		OutputDebugStringA(errorMsg);
	}
	else { // pathLen >= sizeof(exePathBuffer)
		OutputDebugStringA("[r1delta_core] Warning: GetModuleFileNameA buffer too small.\n");
	}


	// 3. Append "-game" argument if constructed and not already present
	// Use C-style search to avoid potential std::string exceptions if manipulated strangely
	bool gameArgPresent = (strstr(finalCmdLineStr.c_str(), "-game ") != nullptr);

	if (!gameArg.empty() && !gameArgPresent) {
		if (!finalCmdLineStr.empty()) finalCmdLineStr += " ";
		finalCmdLineStr += gameArg;
	}
	else if (gameArgPresent) {
		OutputDebugStringA("[r1delta_core] -game arg already present in original command line.\n");
	}


	// 4. Append arguments ONLY from internal storage. No environment variable check.
	if (!g_r1delta_launch_args.empty()) {
		if (!finalCmdLineStr.empty()) finalCmdLineStr += " ";
		finalCmdLineStr += g_r1delta_launch_args; // Append stored args
		OutputDebugStringA("[r1delta_core] Appended args from internal storage: ");
		OutputDebugStringA(g_r1delta_launch_args.c_str());
		OutputDebugStringA("\n");
	}
	else {
		OutputDebugStringA("[r1delta_core] No user launch arguments appended (Internal storage was empty).\n");
	}
	finalCmdLineStr += " -novid ";


	// 5. Prepare buffer for original function call (unchanged)
	// std::vector can throw bad_alloc, but again, that's catastrophic.
	std::vector<char> finalCmdLineBuffer(finalCmdLineStr.begin(), finalCmdLineStr.end());
	finalCmdLineBuffer.push_back('\0'); // Ensure null-termination

	// 6. Call original function (unchanged)
	OutputDebugStringA("[r1delta_core] Calling original CCommandLine::CreateCmdLine with final args: ");
	OutputDebugStringA(finalCmdLineBuffer.data());
	OutputDebugStringA("\n");
	CCommandLine__CreateCmdLine_Original(thisptr, finalCmdLineBuffer.data());
	OutputDebugStringA("[r1delta_core] CCommandLine__CreateCmdLine hook finished.\n");
}
HRESULT(WINAPI* SHGetFolderPathAOriginal)(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPSTR  pszPath);
STDAPI SHGetFolderPathAHook(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPSTR pszPath) {
	if (csidl != 5) {
		return SHGetFolderPathAOriginal(hwnd, csidl, hToken, dwFlags, pszPath);
	}
	LPWSTR newPath[512 * sizeof(wchar_t)] = { 0 };
	memset(pszPath, 0, MAX_PATH);
	auto ret = SHGetKnownFolderPath(FOLDERID_SavedGames, dwFlags, hToken, newPath);
	WideCharToMultiByte(CP_UTF8, 0, newPath[0], lstrlenW(*newPath), pszPath, MAX_PATH, NULL, NULL);
	return ret;
}
// Helper function to check if a directory exists
bool DirectoryExists(const WCHAR* szPath)
{
	DWORD dwAttrib = GetFileAttributesW(szPath);
	return (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}
// Function to perform the one-time migration and profile copy
void MigrateOldSaveLocation()
{
	OutputDebugStringA("[r1delta_core] Checking for old save location migration/copy...\n");

	PWSTR pszDocumentsPath = nullptr;
	PWSTR pszSavedGamesPath = nullptr;
	HRESULT hr_doc = SHGetKnownFolderPath(FOLDERID_Documents, 0, NULL, &pszDocumentsPath);
	HRESULT hr_sg = SHGetKnownFolderPath(FOLDERID_SavedGames, 0, NULL, &pszSavedGamesPath);

	bool migrationPerformed = false; // Flag to track if R1Delta was moved

	if (SUCCEEDED(hr_doc) && SUCCEEDED(hr_sg))
	{
		WCHAR szOldRespawnBase[MAX_PATH];
		WCHAR szNewRespawnBase[MAX_PATH];
		WCHAR szOldR1DeltaPath[MAX_PATH];
		WCHAR szNewR1DeltaPath[MAX_PATH];
		WCHAR szOldTitanfallPath[MAX_PATH];

		// Construct base paths
		// Using PathCchCombine for safer path construction
		PathCchCombine(szOldRespawnBase, MAX_PATH, pszDocumentsPath, L"Respawn");
		PathCchCombine(szNewRespawnBase, MAX_PATH, pszSavedGamesPath, L"Respawn");

		// Construct specific game paths
		PathCchCombine(szOldR1DeltaPath, MAX_PATH, szOldRespawnBase, L"R1Delta");
		PathCchCombine(szNewR1DeltaPath, MAX_PATH, szNewRespawnBase, L"R1Delta");
		PathCchCombine(szOldTitanfallPath, MAX_PATH, szOldRespawnBase, L"Titanfall");

		// --- 1. R1Delta Migration (Move) ---
		OutputDebugStringA("[r1delta_core] Checking R1Delta move: ");
		OutputDebugStringW(szOldR1DeltaPath);
		OutputDebugStringA(" -> ");
		OutputDebugStringW(szNewR1DeltaPath);
		OutputDebugStringA("\n");

		if (DirectoryExists(szOldR1DeltaPath))
		{
			OutputDebugStringA("[r1delta_core] Old R1Delta path exists.\n");
			if (!DirectoryExists(szNewR1DeltaPath))
			{
				OutputDebugStringA("[r1delta_core] New R1Delta path does NOT exist. Attempting migration.\n");

				// Ensure the parent directory (Saved Games\Respawn) exists
				if (!DirectoryExists(szNewRespawnBase))
				{
					if (CreateDirectoryW(szNewRespawnBase, NULL))
					{
						OutputDebugStringA("[r1delta_core] Created intermediate directory: ");
						OutputDebugStringW(szNewRespawnBase);
						OutputDebugStringA("\n");
					}
					else
					{
						DWORD dwError = GetLastError();
						char errorMsg[256];
						sprintf_s(errorMsg, sizeof(errorMsg), "[r1delta_core] Failed to create intermediate directory %ls. Error: %lu. Migration aborted.\n", szNewRespawnBase, dwError);
						OutputDebugStringA(errorMsg);
						goto cleanup; // Abort if intermediate dir fails
					}
				}

				// Attempt to move the directory
				if (MoveFileExW(szOldR1DeltaPath, szNewR1DeltaPath, MOVEFILE_WRITE_THROUGH))
				{
					OutputDebugStringA("[r1delta_core] Successfully migrated R1Delta data.\n");
					migrationPerformed = true; // Mark that we potentially created the new path
				}
				else
				{
					DWORD dwError = GetLastError();
					char errorMsg[300];
					sprintf_s(errorMsg, sizeof(errorMsg), "[r1delta_core] Failed to move directory %ls to %ls. Error: %lu\n", szOldR1DeltaPath, szNewR1DeltaPath, dwError);
					OutputDebugStringA(errorMsg);
					// Continue to Titanfall check even if move fails
				}
			}
			else
			{
				OutputDebugStringA("[r1delta_core] New R1Delta path already exists. R1Delta migration not needed.\n");
				migrationPerformed = true; // New path exists, treat as if migration happened for subsequent checks
			}
		}
		else
		{
			OutputDebugStringA("[r1delta_core] Old R1Delta path does not exist. R1Delta migration not needed.\n");
		}

		// --- 2. Titanfall Profile Copy ---
		// Only attempt this if the new R1Delta path doesn't exist *now*
		// (meaning the R1Delta migration wasn't performed or didn't exist initially)
		// AND the old Titanfall path *does* exist.

		OutputDebugStringA("[r1delta_core] Checking Titanfall copy: ");
		OutputDebugStringW(szOldTitanfallPath);
		OutputDebugStringA(" -> ");
		OutputDebugStringW(szNewR1DeltaPath);
		OutputDebugStringA("\n");

		if (!DirectoryExists(szNewR1DeltaPath)) // Check if target *still* doesn't exist
		{
			if (DirectoryExists(szOldTitanfallPath))
			{
				OutputDebugStringA("[r1delta_core] Old Titanfall path exists and New R1Delta path does NOT exist. Attempting copy.\n");

				// Ensure the parent directory (Saved Games\Respawn) exists
				if (!DirectoryExists(szNewRespawnBase))
				{
					if (CreateDirectoryW(szNewRespawnBase, NULL))
					{
						OutputDebugStringA("[r1delta_core] Created intermediate directory: ");
						OutputDebugStringW(szNewRespawnBase);
						OutputDebugStringA("\n");
					}
					else
					{
						DWORD dwError = GetLastError();
						char errorMsg[256];
						sprintf_s(errorMsg, sizeof(errorMsg), "[r1delta_core] Failed to create intermediate directory %ls for copy. Error: %lu. Copy aborted.\n", szNewRespawnBase, dwError);
						OutputDebugStringA(errorMsg);
						goto cleanup; // Abort if intermediate dir fails
					}
				}

				// SHFileOperation requires double-null-terminated strings for paths
				WCHAR szFrom[MAX_PATH + 1] = { 0 }; // +1 for second null
				WCHAR szTo[MAX_PATH + 1] = { 0 };   // +1 for second null
				wcscpy_s(szFrom, MAX_PATH, szOldTitanfallPath);
				wcscpy_s(szTo, MAX_PATH, szNewR1DeltaPath);
				// szFrom[wcslen(szFrom) + 1] = L'\0'; // Already zero-initialized
				// szTo[wcslen(szTo) + 1] = L'\0';     // Already zero-initialized

				SHFILEOPSTRUCTW sfop = { 0 };
				sfop.hwnd = NULL; // No owner window
				sfop.wFunc = FO_COPY;
				sfop.pFrom = szFrom;
				sfop.pTo = szTo;
				// FOF_NOCONFIRMATION: Overwrite existing files without asking
				// FOF_NOERRORUI: Don't display error dialogs
				// FOF_SILENT: Don't display progress
				// FOF_NOCONFIRMMKDIR: Create directories without asking
				sfop.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT | FOF_NOCONFIRMMKDIR;

				int result = SHFileOperationW(&sfop);

				if (result == 0 && !sfop.fAnyOperationsAborted)
				{
					// Verify the target directory exists after copy
					if (DirectoryExists(szNewR1DeltaPath)) {
						OutputDebugStringA("[r1delta_core] Successfully copied Titanfall data to new R1Delta location.\n");
					}
					else {
						// This case is unlikely if SHFileOperation returned 0, but good to check
						OutputDebugStringA("[r1delta_core] SHFileOperation reported success, but target directory not found after copy.\n");
					}
				}
				else
				{
					char errorMsg[300];
					// sfop.fAnyOperationsAborted might be true if the user cancelled (not applicable here due to flags)
					// or if there was an error. result contains a system error code if non-zero.
					sprintf_s(errorMsg, sizeof(errorMsg), "[r1delta_core] Failed to copy directory %ls to %ls. SHFileOperation result: %d, Aborted: %d\n", szOldTitanfallPath, szNewR1DeltaPath, result, sfop.fAnyOperationsAborted);
					OutputDebugStringA(errorMsg);
				}
			}
			else
			{
				OutputDebugStringA("[r1delta_core] Old Titanfall path does not exist. Copy not needed.\n");
			}
		}
		else
		{
			OutputDebugStringA("[r1delta_core] New R1Delta path already exists. Titanfall copy skipped.\n");
		}

	}
	else
	{
		// Original error handling for failing to get known folder paths
		if (FAILED(hr_doc)) {
			char errorMsg[150];
			sprintf_s(errorMsg, sizeof(errorMsg), "[r1delta_core] Failed to get Documents folder path. HRESULT: 0x%X\n", hr_doc);
			OutputDebugStringA(errorMsg);
		}
		if (FAILED(hr_sg)) {
			char errorMsg[150];
			sprintf_s(errorMsg, sizeof(errorMsg), "[r1delta_core] Failed to get Saved Games folder path. HRESULT: 0x%X\n", hr_sg);
			OutputDebugStringA(errorMsg);
		}
		OutputDebugStringA("[r1delta_core] Could not get required folder paths. Migration/copy check skipped.\n");
	}

cleanup:
	// Free the memory allocated by SHGetKnownFolderPath
	if (pszDocumentsPath) CoTaskMemFree(pszDocumentsPath);
	if (pszSavedGamesPath) CoTaskMemFree(pszSavedGamesPath);

	OutputDebugStringA("[r1delta_core] Migration/copy check finished.\n");
}
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
	// make sure we're game and not tools
	if (skip_dllmain == 1)
	{
		return TRUE;
	}
	else if (skip_dllmain == -1)
	{
		char path[MAX_PATH];
		if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
			char* exeName = strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path;
			if (_stricmp(exeName, "r1delta_ds.exe") != 0 &&
				_stricmp(exeName, "titanfall.exe") != 0 &&
				_stricmp(exeName, "r1delta.exe") != 0) { // TODO
				skip_dllmain = 1;
				return TRUE;
			}
		}
	}
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH: {
		skip_dllmain = 0;

		if (!IsDedicatedServer() && !IsNoConsole())
		{
			AllocConsole();
			SetConsoleTitleW(L"R1Delta");
			freopen("CONOUT$", "wt", stdout);
		}

#if BUILD_PROFILE
		TracyPlotConfig("r1dc_decompress_memory_in", tracy::PlotFormatType::Memory, false, true, 0);
		TracyPlotConfig("r1dc_decompress_memory_out", tracy::PlotFormatType::Memory, false, true, 0);
#endif

		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		g_PerformanceFrequency = freq.QuadPart;

		VirtualAlloc((void*)0xFFEEFFEEull, 1, MEM_RESERVE, PAGE_NOACCESS);

		{
			char path[MAX_PATH];
			if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
				// Extract the executable name from the path
				char* exeName = strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path;

				// Compare the executable name with "r1delta_ds.exe"
				if (_stricmp(exeName, "r1delta_ds.exe") == 0) {
					G_is_dedi = 1;
				}
				else {
					G_is_dedi = 0;

						nvapi_stuff();
					
				}
			}
			else {
				// If GetModuleFileNameA fails, assume it's not a dedicated server.
				G_is_dedi = 0;
			}
		}


		SetDllDirectoryW(L"r1delta\\bin_delta");
		SetDllDirectoryW(L"r1delta\\bin");
		InstallExceptionHandler();
		MH_Initialize();
		LoadLibraryA("shell32.dll");
		if (!IsDedicatedServer()) {
			//MigrateOldSaveLocation();
			MH_CreateHook((LPVOID)GetProcAddress(GetModuleHandleA("shell32.dll"), "SHGetFolderPathA"), &SHGetFolderPathAHook, reinterpret_cast<LPVOID*>(&SHGetFolderPathAOriginal));
		}
		MH_CreateHook((LPVOID)GetProcAddress(GetModuleHandleA("tier0_orig.dll"), "GetCPUInformation"), &GetCPUInformationDet, reinterpret_cast<LPVOID*>(&GetCPUInformationOriginal));
		MH_CreateHook((LPVOID)(((uintptr_t)GetModuleHandleA("tier0_orig.dll"))+0x03500), &CCommandLine__CreateCmdLine, reinterpret_cast<LPVOID*>(&CCommandLine__CreateCmdLine_Original));
//		if (!IsDedicatedServer())
//			MH_CreateHook((LPVOID)GetProcAddress(GetModuleHandleA("tier0_orig.dll"), "Plat_IsInToolMode"), &GetCPUInformationDet, reinterpret_cast<LPVOID*>(NULL));
		MH_EnableHook(MH_ALL_HOOKS);

		initialisePatchInstructions();


		if(!IsNoConsole() && !IsDedicatedServer())
			InitLoggingHooks();
		StartFileCacheThread();

		// Chromium
		LdrRegisterDllNotificationFunc reg_fn =
			reinterpret_cast<LdrRegisterDllNotificationFunc>(::GetProcAddress(
				::GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"));
		reg_fn(0, &LoaderNotificationCallback, 0, &dll_notification_cookie_);

		if (!G_is_dedi) {
			G_launcher = (uintptr_t)GetModuleHandleW(L"launcher.dll");
			G_vscript = G_launcher;

			HMODULE hNTDLL = GetModuleHandleA("ntdll.dll");
			auto wine_get_version = (wine_get_version_func)GetProcAddress(hNTDLL, "wine_get_version");
			if (!wine_get_version)
			{
				LDR_DLL_LOADED_NOTIFICATION_DATA* ndata = GetModuleNotificationData(L"launcher.dll");
				if (ndata)
				{
					doBinaryPatchForFile(*ndata);
					FreeModuleNotificationData(ndata);
				}
			}
		}
		else {
			G_launcher = (uintptr_t)GetModuleHandleW(L"dedicated.dll");
			G_vscript = G_launcher;
			LDR_DLL_LOADED_NOTIFICATION_DATA* ndata = GetModuleNotificationData(L"dedicated.dll");
			doBinaryPatchForFile(*ndata);
			FreeModuleNotificationData(ndata);
		}

		break; 
	}
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		eos::ShutdownNetworking();
		MH_DisableHook(MH_ALL_HOOKS);
		MH_Uninitialize();
		break;
	}
	return TRUE;
}
