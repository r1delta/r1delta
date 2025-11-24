#include "eos_logging_manager.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <cwchar>

#include "core.h"
#include "eos_threading.h"

#include <eos_logging.h>
#include "logging.h"

namespace
{

std::mutex g_logMutex;
std::ofstream g_logFile;
bool g_loggingInitialized = false;

bool HasCommandLineFlag(const wchar_t* flag)
{
    if (!flag)
        return false;

    // Convert wide string flag to narrow string for HasEngineCommandLineFlag
    char narrowFlag[256];
    size_t convertedChars = 0;
    wcstombs_s(&convertedChars, narrowFlag, sizeof(narrowFlag), flag, _TRUNCATE);

    return HasEngineCommandLineFlag(narrowFlag);
}

EOS_ELogLevel DetermineLogLevel()
{
#if BUILD_DEBUG
    return EOS_ELogLevel::EOS_LOG_VeryVerbose;
#else
    if (HasCommandLineFlag(L"-eoslogverbose"))
        return EOS_ELogLevel::EOS_LOG_VeryVerbose;
    return EOS_ELogLevel::EOS_LOG_Info;
#endif
}

void EnsureLogFile()
{
    //std::lock_guard lock(g_logMutex);
    //if (g_logFile.is_open())
        return;

    std::error_code ec;
    auto logPath = std::filesystem::current_path(ec);
    if (ec)
        logPath.clear();
    std::filesystem::path filePath = logPath / "r1delta_eos.log";

    g_logFile.open(filePath, std::ios::out | std::ios::app);
    if (!g_logFile.good())
    {
        auto filePathUtf8 = filePath.u8string();
        Msg("EOS: Failed to open log file at %s\n", filePathUtf8.c_str());
    }
}

const char* LevelToString(EOS_ELogLevel level)
{
    switch (level)
    {
    case EOS_ELogLevel::EOS_LOG_Fatal: return "Fatal";
    case EOS_ELogLevel::EOS_LOG_Error: return "Error";
    case EOS_ELogLevel::EOS_LOG_Warning: return "Warning";
    case EOS_ELogLevel::EOS_LOG_Info: return "Info";
    case EOS_ELogLevel::EOS_LOG_Verbose: return "Verbose";
    case EOS_ELogLevel::EOS_LOG_VeryVerbose: return "VeryVerbose";
    default: return "Unknown";
    }
}

void EOS_CALL OnLogMessageReceived(const EOS_LogMessage* message)
{
    if (!message || !message->Message)
        return;

    EnsureLogFile();

    auto now = std::chrono::system_clock::now();
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &nowTime);
#else
    localtime_r(&nowTime, &localTime);
#endif

    std::ostringstream builder;
    builder << " [" << LevelToString(message->Level) << ']'
            << " [" << (message->Category ? message->Category : "Unknown") << "] "
            << message->Message;

    const std::string line = builder.str();
    Msg("%s\n", line.c_str());

    std::lock_guard lock(g_logMutex);
    if (g_logFile.is_open())
    {
        g_logFile << line << std::endl;
    }
}

} // namespace

namespace eos
{

void Logging::Initialize()
{
    if (g_loggingInitialized)
        return;

    EOS_EResult callbackResult = EOS_EResult::EOS_UnexpectedError;
    {
        SdkLock lock(GetSdkMutex());
        callbackResult = EOS_Logging_SetCallback(&OnLogMessageReceived);
    }
    if (callbackResult != EOS_EResult::EOS_Success)
    {
        Msg("EOS: Failed to register logging callback (%d)\n", static_cast<int>(callbackResult));
        return;
    }

    const EOS_ELogLevel level = DetermineLogLevel();
    EOS_EResult levelResult = EOS_EResult::EOS_UnexpectedError;
    {
        SdkLock lock(GetSdkMutex());
        levelResult = EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, level);
    }
    if (levelResult != EOS_EResult::EOS_Success)
    {
        Msg("EOS: Failed to set log level (%d)\n", static_cast<int>(levelResult));
    }

    g_loggingInitialized = true;
}

void Logging::Shutdown()
{
    if (!g_loggingInitialized)
        return;

    {
        SdkLock lock(GetSdkMutex());
        EOS_Logging_SetCallback(nullptr);
    }

    std::lock_guard lock(g_logMutex);
    if (g_logFile.is_open())
    {
        g_logFile.flush();
        g_logFile.close();
    }

    g_loggingInitialized = false;
}

} // namespace eos
