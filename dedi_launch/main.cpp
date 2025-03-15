#include <unordered_map>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#include <assert.h>
#include <direct.h>
#elif POSIX
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#define MAX_PATH PATH_MAX
#endif
#ifndef _MSC_VER && !(__MINGW32__ || __MINGW64__)
// provides __cpuidex on mingw
#include <intrin.h>
#endif
#include <string>

typedef int (*DedicatedMain_t)( HINSTANCE hInstance, HINSTANCE hPrevInstance, 
							  LPSTR lpCmdLine, int nCmdShow );


//-----------------------------------------------------------------------------
// Purpose: Return the directory where this .exe is running from
// Output : char
//-----------------------------------------------------------------------------

static char *GetBaseDir( const char *pszBuffer )
{
	static char	basedir[ MAX_PATH ];
	char szBuffer[ MAX_PATH ];
	size_t j;
	char *pBuffer = NULL;

	strcpy_s(szBuffer, sizeof(szBuffer), pszBuffer);

	pBuffer = strrchr( szBuffer,'\\' );
	if ( pBuffer )
	{
		*(pBuffer+1) = '\0';
	}

	strcpy_s(basedir, sizeof(basedir), szBuffer);

	j = strlen( basedir );
	if (j > 0)
	{
		if ( ( basedir[ j-1 ] == '\\' ) || 
			 ( basedir[ j-1 ] == '/' ) )
		{
			basedir[ j-1 ] = 0;
		}
	}

	return basedir;
}
bool DoesCpuSupportCetShadowStack()
{
	int cpuInfo[4] = { 0, 0, 0, 0 };
	__cpuidex(cpuInfo, 7, 0);
	return (cpuInfo[2] & (1 << 7)) != 0; // Check bit 7 in ECX (cpuInfo[2])
}

std::unordered_map<PROCESS_MITIGATION_POLICY, const char*> g_mitigationPolicyNames = {
	{ ProcessASLRPolicy, "ProcessASLRPolicy" },
	{ ProcessDynamicCodePolicy, "ProcessDynamicCodePolicy" },
	{ ProcessExtensionPointDisablePolicy, "ProcessExtensionPointDisablePolicy" },
	{ ProcessControlFlowGuardPolicy, "ProcessControlFlowGuardPolicy" },
	{ ProcessSignaturePolicy, "ProcessSignaturePolicy" },
	{ ProcessImageLoadPolicy, "ProcessImageLoadPolicy" },
	{ ProcessUserShadowStackPolicy, "ProcessUserShadowStackPolicy" },
};

void SetMitigationPolicies()
{
	auto kernel32 = GetModuleHandleW(L"kernel32.dll");
	if (!kernel32)
		return;
	auto SetProcessMitigationPolicy = (decltype(&::SetProcessMitigationPolicy))GetProcAddress(kernel32, "SetProcessMitigationPolicy");
	auto GetProcessMitigationPolicy = (decltype(&::GetProcessMitigationPolicy))GetProcAddress(kernel32, "GetProcessMitigationPolicy");
	if (!SetProcessMitigationPolicy || !GetProcessMitigationPolicy)
		return;

	auto SetProcessMitigationPolicyEnsureOK = [SetProcessMitigationPolicy](PROCESS_MITIGATION_POLICY MitigationPolicy, PVOID lpBuffer, SIZE_T dwLength) {
		bool result = SetProcessMitigationPolicy(MitigationPolicy, lpBuffer, dwLength);
		if (!result)
		{
			auto lastError = GetLastError();
			if (MitigationPolicy == ProcessUserShadowStackPolicy && !DoesCpuSupportCetShadowStack())
				return;
			MessageBoxA(0, ("Failed mitigation: "
				+ (g_mitigationPolicyNames.find(MitigationPolicy) != g_mitigationPolicyNames.end() ? g_mitigationPolicyNames[MitigationPolicy] : std::to_string(MitigationPolicy))
				+ ", error: " + std::to_string(lastError) + "\n\nThis is a non-fatal error.").c_str(),
				"SetProcessMitigationPolicy failed", 0);
			TerminateProcess(GetCurrentProcess(), 1);
		}
		};

	PROCESS_MITIGATION_ASLR_POLICY ap;
	GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessASLRPolicy, &ap, sizeof(ap));
	ap.EnableBottomUpRandomization = true;
	ap.EnableForceRelocateImages = true;
	ap.EnableHighEntropy = true;
	ap.DisallowStrippedImages = true; // Images that have not been built with /DYNAMICBASE and do not have relocation information will fail to load if this flag and EnableForceRelocateImages are set.
	SetProcessMitigationPolicyEnsureOK(ProcessASLRPolicy, &ap, sizeof(ap));

	/*PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dcp; // breaks things
	GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessDynamicCodePolicy, &dcp, sizeof(dcp));
	dcp.ProhibitDynamicCode = true;
	SetProcessMitigationPolicyEnsureOK(ProcessDynamicCodePolicy, &dcp, sizeof(dcp));*/

	PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY epdp;
	GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessExtensionPointDisablePolicy, &epdp, sizeof(epdp));
	epdp.DisableExtensionPoints = true;
	SetProcessMitigationPolicyEnsureOK(ProcessExtensionPointDisablePolicy, &epdp, sizeof(epdp));

	/*PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY cfgp; // breaks things
	GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessControlFlowGuardPolicy, &cfgp, sizeof(cfgp));
	cfgp.EnableControlFlowGuard = true; // This field cannot be changed via SetProcessMitigationPolicy.
	//cfgp.StrictMode = true; // this needs to be disabled to load stubs with no CRT
	SetProcessMitigationPolicyEnsureOK(ProcessControlFlowGuardPolicy, &cfgp, sizeof(cfgp));*/

	PROCESS_MITIGATION_IMAGE_LOAD_POLICY ilp;
	GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessImageLoadPolicy, &ilp, sizeof(ilp));
	ilp.PreferSystem32Images = true;
	ilp.NoRemoteImages = true;
	SetProcessMitigationPolicyEnsureOK(ProcessImageLoadPolicy, &ilp, sizeof(ilp));

	PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY usspp;
	GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessUserShadowStackPolicy, &usspp, sizeof(usspp));
	if (usspp.EnableUserShadowStack)
	{
		//MessageBoxA(0, "Enabling shadow stack strict mode", "", 0);
		usspp.EnableUserShadowStackStrictMode = true;
		SetProcessMitigationPolicyEnsureOK(ProcessUserShadowStackPolicy, &usspp, sizeof(usspp));
	} //else MessageBoxA(0, "Shadow stack is not enabled!!!", "", 0);
}

int APIENTRY WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow )
{
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	*strrchr(path, '\\') = '\0';
	SetCurrentDirectoryA(path);

	// Must add 'bin' to the path....
	char* pPath = nullptr;
	size_t len;
	_dupenv_s(&pPath, &len, "PATH");

	// Use the .EXE name to determine the root directory
	char moduleName[ MAX_PATH ];
	char szBuffer[ 4096 ];
	if ( !GetModuleFileNameA( hInstance, moduleName, MAX_PATH ) )
	{
		MessageBoxA( 0, "Failed calling GetModuleFileName", "Launcher Error", MB_OK );
		return 0;
	}

	// Get the root directory the .exe is in
	char* pRootDir = GetBaseDir( moduleName );
	SetCurrentDirectoryA(pRootDir);
	_snprintf_s(szBuffer, sizeof(szBuffer), _TRUNCATE, "PATH=%s\\r1delta\\bin_delta\\;%s\\r1delta\\bin\\;%s\\bin\\x64_retail\\;.;%s", pRootDir, pRootDir, pRootDir, pPath);
	szBuffer[ sizeof(szBuffer) - 1 ] = 0;
	_putenv( szBuffer );
	if (pPath != nullptr) {
		free(pPath);
	}
	HINSTANCE launcher = LoadLibraryA( "bin\\x64_retail\\dedicated.dll" );
	if (!launcher)
	{
		char *pszError;
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&pszError, 0, NULL);

		char szBuf[1024];
		_snprintf_s(szBuf, sizeof(szBuf), _TRUNCATE, "Failed to load the launcher DLL:\n\n%s", pszError);
		szBuf[ sizeof(szBuf) - 1 ] = 0;
		MessageBoxA( 0, szBuf, "Launcher Error", MB_OK );

		LocalFree(pszError);
		return 0;
	}

	DedicatedMain_t main = (DedicatedMain_t)GetProcAddress( launcher, "DedicatedMain" );
	return main( hInstance, hPrevInstance, lpCmdLine, nCmdShow );
}
