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

#pragma once

#pragma warning(push)
#pragma warning(disable)

// NOTE(mrsteyk): if you ever see it as 1, I did something wrong :/
#define BUILD_PROFILE 0
#if BUILD_PROFILE
#include <winsock2.h>
#endif

#include <windows.h>
#pragma warning(pop)
#include <filesystem>
#include <string>

#if !defined(BUILD_DEBUG)
# if defined(NDEBUG)
#  define BUILD_DEBUG 0
# elif defined(_DEBUG)
#  define BUILD_DEBUG 1
# else
#  error Could not determine BUILD_DEBUG value
# endif
#endif

// NOTE(mrsteyk): because we know the size beforehand we just memcmp including null terminator
#define strcmp_static(P, S) memcmp((P), (S), sizeof(S))
#define string_equal_size(P, L, S) ((L + 1) == sizeof(S) && strcmp_static(P, S) == 0)

#define IsPow2(a) ((a) && ((((a)-1) & (a)) == 0))
#define AlignPow2(p, a) (((p)+(a)-1)&(~((a)-1)))
#if BUILD_DEBUG
// NOTE(mrsteyk): force semicolon at the end
#define R1DAssert(e) do { if (!(e)) __debugbreak(); } while(0)
#else
#define R1DAssert(e) (e)
#endif

extern uint64_t g_PerformanceFrequency;

extern int G_is_dedi;

#define IsDedicatedServer() (G_is_dedi)

__forceinline bool IsNoOrigin() {
//    const wchar_t* cmdLine = GetCommandLineW();
    return false;
}

__forceinline bool IsNoConsole() {
    const wchar_t* cmdLine = GetCommandLineW();
    return !wcsstr(cmdLine, L"-allocconsole");
}

#define ENGINE_DLL_BASE (G_is_dedi ? G_engine_ds : G_engine)
#define ENGINE_DLL_BASE_(dedi) ((dedi) ? G_engine_ds : G_engine)

struct HashStrings
{
    using ht = std::hash<std::string_view>;
    using is_transparent = void;

    auto operator()(const char* ptr) const noexcept
    {
        return ht{}(ptr);
    }
    auto operator()(const std::string_view& sv) const noexcept
    {
        return ht{}(sv);
    }
    auto operator()(const std::string& s) const noexcept
    {
        return ht{}(s);
    }
};

#if 0
// TODO(mrsteyk): I hate C++. This didn't work.
struct EqualToStdString
{
    using is_transparent = void;

    auto operator()(const std::string& a, const char*& ptr) const noexcept
    {
        return a._Equal(ptr);
    }
    auto operator()(const std::string& a, const std::string_view& sv) const noexcept
    {
        return a == sv;
    }
    auto operator()(const std::string& a, const std::string& s) const noexcept
    {
        return a == s;
    }
};
#endif


#if BUILD_PROFILE
#define TRACY_ENABLE
#pragma warning(push)
#pragma warning(disable)
#include "tracy-0.11.1/public/tracy/Tracy.hpp"
#pragma warning(pop)
#else
#define TracyNoop

#define ZoneNamed(x,y)
#define ZoneNamedN(x,y,z)
#define ZoneNamedC(x,y,z)
#define ZoneNamedNC(x,y,z,w)

#define ZoneTransient(x,y)
#define ZoneTransientN(x,y,z)

#define ZoneScoped
#define ZoneScopedN(x)
#define ZoneScopedC(x)
#define ZoneScopedNC(x,y)

#define ZoneText(x,y)
#define ZoneTextV(x,y,z)
#define ZoneTextF(x,...)
#define ZoneTextVF(x,y,...)
#define ZoneName(x,y)
#define ZoneNameV(x,y,z)
#define ZoneNameF(x,...)
#define ZoneNameVF(x,y,...)
#define ZoneColor(x)
#define ZoneColorV(x,y)
#define ZoneValue(x)
#define ZoneValueV(x,y)
#define ZoneIsActive false
#define ZoneIsActiveV(x) false

#define FrameMark
#define FrameMarkNamed(x)
#define FrameMarkStart(x)
#define FrameMarkEnd(x)

#define FrameImage(x,y,z,w,a)

#define TracyLockable( type, varname ) type varname
#define TracyLockableN( type, varname, desc ) type varname
#define TracySharedLockable( type, varname ) type varname
#define TracySharedLockableN( type, varname, desc ) type varname
#define LockableBase( type ) type
#define SharedLockableBase( type ) type
#define LockMark(x) (void)x
#define LockableName(x,y,z)

#define TracyPlot(x,y)
#define TracyPlotConfig(x,y,z,w,a)

#define TracyMessage(x,y)
#define TracyMessageL(x)
#define TracyMessageC(x,y,z)
#define TracyMessageLC(x,y)
#define TracyAppInfo(x,y)

#define TracyAlloc(x,y)
#define TracyFree(x)
#define TracySecureAlloc(x,y)
#define TracySecureFree(x)

#define TracyAllocN(x,y,z)
#define TracyFreeN(x,y)
#define TracySecureAllocN(x,y,z)
#define TracySecureFreeN(x,y)

#define ZoneNamedS(x,y,z)
#define ZoneNamedNS(x,y,z,w)
#define ZoneNamedCS(x,y,z,w)
#define ZoneNamedNCS(x,y,z,w,a)

#define ZoneTransientS(x,y,z)
#define ZoneTransientNS(x,y,z,w)

#define ZoneScopedS(x)
#define ZoneScopedNS(x,y)
#define ZoneScopedCS(x,y)
#define ZoneScopedNCS(x,y,z)

#define TracyAllocS(x,y,z)
#define TracyFreeS(x,y)
#define TracySecureAllocS(x,y,z)
#define TracySecureFreeS(x,y)

#define TracyAllocNS(x,y,z,w)
#define TracyFreeNS(x,y,z)
#define TracySecureAllocNS(x,y,z,w)
#define TracySecureFreeNS(x,y,z)

#define TracyMessageS(x,y,z)
#define TracyMessageLS(x,y)
#define TracyMessageCS(x,y,z,w)
#define TracyMessageLCS(x,y,z)

#define TracySourceCallbackRegister(x,y)
#define TracyParameterRegister(x,y)
#define TracyParameterSetup(x,y,z,w)
#define TracyIsConnected false
#define TracyIsStarted false
#define TracySetProgramName(x)

#define TracyFiberEnter(x)
#define TracyFiberEnterHint(x,y)
#define TracyFiberLeave
#endif

inline std::filesystem::path GetExecutableDirectory() {
    std::vector<wchar_t> buffer(MAX_PATH); // Start with MAX_PATH
    DWORD pathLen = 0;

    // Try to get the path, increasing buffer size if necessary
    // See: https://docs.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamew
    while (true) {
        pathLen = GetModuleFileNameW(NULL, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (pathLen == 0) {
            // Error getting path
            throw std::runtime_error("Failed to get executable path. Error code: " + std::to_string(GetLastError()));
        }
        else if (pathLen < buffer.size()) {
            // Success, path fits in buffer
            break;
        }
        else {
            // Buffer too small, double its size and retry
            buffer.resize(buffer.size() * 2);
            if (buffer.size() > 32767) { // Avoid excessively large buffers (limit for GetModuleFileName)
                throw std::runtime_error("Executable path exceeds maximum length.");
            }
        }
    }

    std::filesystem::path executablePath(buffer.data(), buffer.data() + pathLen);
    return executablePath.parent_path(); // Return the directory containing the executable
}
