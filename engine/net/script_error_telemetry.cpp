#include "script_error_telemetry.h"

#include "core.h"
#include "cvar.h"
#include "logging.h"

#include <Windows.h>
#include <winhttp.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace ScriptErrorTelemetry
{
namespace
{

constexpr char kDefaultServiceUrl[] = "https://r1delta-script-errors.r3muxd.workers.dev";
constexpr std::size_t kContextCount = static_cast<std::size_t>(VmContext::Count);
constexpr std::size_t kMaxQueueEntries = 16;
constexpr std::size_t kMaxFrames = 32;
constexpr std::size_t kMaxCaptureInput = 8192;
constexpr std::size_t kMaxPartialLine = 4096;
constexpr std::size_t kMaxErrorLength = 1024;
constexpr std::size_t kMaxFunctionLength = 128;
constexpr std::size_t kMaxSourceLength = 192;
constexpr std::size_t kMaxServiceUrlLength = 512;
constexpr std::size_t kMaxPayloadLength = 16 * 1024;
constexpr std::size_t kMaxResponseLength = 4096;
constexpr int kResolveTimeoutMs = 1000;
constexpr int kConnectTimeoutMs = 1500;
constexpr int kSendTimeoutMs = 1500;
constexpr int kReceiveTimeoutMs = 1500;

constexpr std::string_view kErrorMarker = "SCRIPT ERROR:";
constexpr std::string_view kAssertMarker = "SCRIPT ASSERT:";

struct Frame
{
    std::string function;
    std::string source;
    int line = 0;
};

struct Report
{
    VmContext context = VmContext::Server;
    std::uint64_t sequence = 0;
    std::string processMode;
    std::string error;
    std::vector<Frame> frames;
    std::string serviceUrl;
    bool eligible = false;
    bool cascade = false;
};

enum class CapturePhase : std::uint8_t
{
    Idle,
    AwaitCallstack,
    Callstack,
    Details
};

struct CaptureState
{
    CapturePhase phase = CapturePhase::Idle;
    Report report;
    std::string partialLine;
    std::uint32_t activeErrorBlocks = 0;
};

struct NotificationState
{
    std::uint64_t sequence = 0;
    bool hasResult = false;
    bool isNew = false;
    std::array<char, 53> code{};
};

struct Settings
{
    bool ready = false;
    bool enabled = true;
    std::string serviceUrl = kDefaultServiceUrl;
};

struct Endpoint
{
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
};

// YY-Thunks links a WinHttpCloseHandle thunk (for targets below Windows 8) that
// probes the handle as a WinHttpProxyResolver struct before delegating to the
// real implementation. Wine's winhttp hands out small-integer handles, which the
// probe dereferences and crashes on. Resolving the export directly bypasses the
// thunk; the real winhttp implementation is authoritative about its own handles.
// winhttp.dll is a load-time import, so the module is always present whenever an
// HINTERNET exists.
using WinHttpCloseHandleType = BOOL(WINAPI*)(HINTERNET);
WinHttpCloseHandleType ResolveRealWinHttpCloseHandle()
{
    static std::atomic<WinHttpCloseHandleType> cached{nullptr};

    auto fn = cached.load(std::memory_order_acquire);
    if (fn)
        return fn;

    const HMODULE module = GetModuleHandleW(L"winhttp.dll");
    if (!module)
        return nullptr;

    fn = reinterpret_cast<WinHttpCloseHandleType>(GetProcAddress(module, "WinHttpCloseHandle"));
    if (fn)
        cached.store(fn, std::memory_order_release);

    return fn;
}

class InternetHandle
{
public:
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET handle) : handle_(handle) {}
    ~InternetHandle()
    {
        if (!handle_)
            return;
        const WinHttpCloseHandleType realClose = ResolveRealWinHttpCloseHandle();
        if (realClose)
            realClose(handle_);
        else
            WinHttpCloseHandle(handle_);
    }

    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;

    HINTERNET get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    HINTERNET handle_ = nullptr;
};

std::string_view Trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

bool StartsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool IsAsciiAlphaNumeric(char ch)
{
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0;
}

bool IsTokenBoundary(char ch)
{
    return !IsAsciiAlphaNumeric(ch) && ch != '_';
}

bool IsHexDigit(char ch)
{
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isxdigit(value) != 0;
}

bool IsAbsolutePathAt(std::string_view text, std::size_t position)
{
    if (position + 2 < text.size()
        && std::isalpha(static_cast<unsigned char>(text[position]))
        && text[position + 1] == ':'
        && (text[position + 2] == '/' || text[position + 2] == '\\')) {
        return position == 0 || IsTokenBoundary(text[position - 1]);
    }

    if (position + 1 < text.size() && text[position] == '\\' && text[position + 1] == '\\')
        return position == 0 || IsTokenBoundary(text[position - 1]);

    if (position + 1 < text.size() && text[position] == '/'
        && (IsAsciiAlphaNumeric(text[position + 1]) || text[position + 1] == '_' || text[position + 1] == '.')) {
        return position == 0 || text[position - 1] == ' ' || text[position - 1] == '\t'
            || text[position - 1] == '(' || text[position - 1] == '[' || text[position - 1] == '"'
            || text[position - 1] == '\'';
    }

    return false;
}

std::size_t ConsumePath(std::string_view text, std::size_t position)
{
    std::size_t end = position;
    while (end < text.size()) {
        const char ch = text[end];
        if (ch == '\r' || ch == '\n' || ch == '\t' || ch == '"' || ch == '\''
            || ch == '<' || ch == '>' || ch == '|' || ch == ',' || ch == ';') {
            break;
        }
        ++end;
    }
    return end;
}

bool IsIpv4At(std::string_view text, std::size_t position, std::size_t& end)
{
    if (position >= text.size() || !std::isdigit(static_cast<unsigned char>(text[position])))
        return false;
    if (position > 0 && (IsAsciiAlphaNumeric(text[position - 1]) || text[position - 1] == '.'))
        return false;

    std::size_t cursor = position;
    for (int octet = 0; octet < 4; ++octet) {
        const std::size_t start = cursor;
        int value = 0;
        while (cursor < text.size() && std::isdigit(static_cast<unsigned char>(text[cursor])) && cursor - start < 3) {
            value = value * 10 + (text[cursor] - '0');
            ++cursor;
        }
        if (cursor == start || value > 255)
            return false;
        if (cursor < text.size() && std::isdigit(static_cast<unsigned char>(text[cursor])))
            return false;
        if (octet != 3) {
            if (cursor >= text.size() || text[cursor] != '.')
                return false;
            ++cursor;
        }
    }

    if (cursor < text.size() && (IsAsciiAlphaNumeric(text[cursor]) || text[cursor] == '.'))
        return false;
    end = cursor;
    return true;
}

bool IsColonAddressAt(std::string_view text, std::size_t position, std::size_t& end)
{
    if (position >= text.size())
        return false;
    const bool bracketed = text[position] == '[';
    std::size_t cursor = position + (bracketed ? 1 : 0);
    if (cursor >= text.size() || (!IsHexDigit(text[cursor]) && text[cursor] != ':'))
        return false;
    if (!bracketed && position > 0 && (IsHexDigit(text[position - 1]) || text[position - 1] == ':'))
        return false;

    int colons = 0;
    int hexDigits = 0;
    while (cursor < text.size()) {
        const char ch = text[cursor];
        if (IsHexDigit(ch)) {
            ++hexDigits;
            ++cursor;
            continue;
        }
        if (ch == '.') {
            ++cursor;
            continue;
        }
        if (ch == ':') {
            ++colons;
            ++cursor;
            continue;
        }
        break;
    }

    if (colons < 2 || hexDigits == 0)
        return false;
    if (bracketed) {
        if (cursor >= text.size() || text[cursor] != ']')
            return false;
        ++cursor;
    }
    else if (cursor < text.size() && (IsHexDigit(text[cursor]) || text[cursor] == ':')) {
        return false;
    }
    end = cursor;
    return true;
}

bool IsUuidAt(std::string_view text, std::size_t position, std::size_t& end)
{
    constexpr std::array<int, 5> groupLengths = { 8, 4, 4, 4, 12 };
    std::size_t cursor = position;
    for (std::size_t group = 0; group < groupLengths.size(); ++group) {
        for (int digit = 0; digit < groupLengths[group]; ++digit) {
            if (cursor >= text.size() || !IsHexDigit(text[cursor]))
                return false;
            ++cursor;
        }
        if (group + 1 != groupLengths.size()) {
            if (cursor >= text.size() || text[cursor] != '-')
                return false;
            ++cursor;
        }
    }

    if (position > 0 && IsHexDigit(text[position - 1]))
        return false;
    if (cursor < text.size() && IsHexDigit(text[cursor]))
        return false;
    end = cursor;
    return true;
}

std::string NormalizeAscii(std::string_view text, std::size_t maxLength)
{
    std::string normalized;
    normalized.reserve(std::min(text.size(), maxLength));
    bool previousSpace = false;
    for (unsigned char value : text) {
        char ch = value >= 0x20 && value <= 0x7e ? static_cast<char>(value) : ' ';
        if (ch == '\t' || ch == '\r' || ch == '\n')
            ch = ' ';
        if (ch == ' ') {
            if (previousSpace)
                continue;
            previousSpace = true;
        }
        else {
            previousSpace = false;
        }
        if (normalized.size() == maxLength)
            break;
        normalized.push_back(ch);
    }

    while (!normalized.empty() && normalized.front() == ' ')
        normalized.erase(normalized.begin());
    while (!normalized.empty() && normalized.back() == ' ')
        normalized.pop_back();
    return normalized;
}

void RedactQuotedValues(std::string& text)
{
    for (const char quote : { '\'', '"' }) {
        std::size_t searchPosition = 0;
        while (searchPosition < text.size()) {
            const std::size_t valueStart = text.find(quote, searchPosition);
            if (valueStart == std::string::npos)
                break;
            if (valueStart > 0 && IsAsciiAlphaNumeric(text[valueStart - 1])) {
                searchPosition = valueStart + 1;
                continue;
            }

            const std::size_t valueEnd = text.find(quote, valueStart + 1);
            if (valueEnd == std::string::npos)
                break;
            if (valueEnd + 1 < text.size() && IsAsciiAlphaNumeric(text[valueEnd + 1])) {
                searchPosition = valueEnd + 1;
                continue;
            }

            text.replace(valueStart + 1, valueEnd - valueStart - 1, "<redacted>");
            searchPosition = valueStart + sizeof("'<redacted>'") - 1;
        }
    }
}

std::string SanitizeError(std::string_view text)
{
    std::string ascii = NormalizeAscii(Trim(text), kMaxErrorLength * 2);
    std::string lower = ascii;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    constexpr std::array<std::string_view, 6> detailMarkers = {
        "locals:", "local variables:", "diagprints:",
        "diagnostic prints:", "registers:", "stack slots:"
    };
    std::size_t detailPosition = std::string::npos;
    for (const std::string_view marker : detailMarkers) {
        const std::size_t position = lower.find(marker);
        if (position != std::string::npos
            && (position == 0 || std::isspace(static_cast<unsigned char>(lower[position - 1])))) {
            detailPosition = std::min(detailPosition, position);
        }
    }
    if (detailPosition != std::string::npos) {
        ascii.resize(detailPosition);
        ascii.append("<details redacted>");
    }
    RedactQuotedValues(ascii);

    std::string sanitized;
    sanitized.reserve(std::min(ascii.size(), kMaxErrorLength));

    for (std::size_t cursor = 0; cursor < ascii.size() && sanitized.size() < kMaxErrorLength;) {
        if (IsAbsolutePathAt(ascii, cursor)) {
            sanitized.append("<path>");
            cursor = ConsumePath(ascii, cursor);
            continue;
        }

        std::size_t sensitiveEnd = cursor;
        if (IsUuidAt(ascii, cursor, sensitiveEnd)) {
            sanitized.append("<id>");
            cursor = sensitiveEnd;
            continue;
        }
        if (IsIpv4At(ascii, cursor, sensitiveEnd) || IsColonAddressAt(ascii, cursor, sensitiveEnd)) {
            sanitized.append("<ip>");
            cursor = sensitiveEnd;
            continue;
        }

        if (cursor + 2 < ascii.size() && ascii[cursor] == '0'
            && (ascii[cursor + 1] == 'x' || ascii[cursor + 1] == 'X')) {
            std::size_t end = cursor + 2;
            while (end < ascii.size() && IsHexDigit(ascii[end]))
                ++end;
            if (end - (cursor + 2) >= 8) {
                sanitized.append("<id>");
                cursor = end;
                continue;
            }
        }

        if (std::isdigit(static_cast<unsigned char>(ascii[cursor]))) {
            std::size_t end = cursor;
            while (end < ascii.size() && std::isdigit(static_cast<unsigned char>(ascii[end])))
                ++end;
            if (end - cursor >= 7) {
                sanitized.append("<id>");
                cursor = end;
                continue;
            }
        }

        sanitized.push_back(ascii[cursor++]);
    }

    if (sanitized.size() > kMaxErrorLength)
        sanitized.resize(kMaxErrorLength);
    return sanitized.empty() ? std::string("unknown script error") : sanitized;
}

std::string SanitizeFunction(std::string_view text)
{
    std::string function = NormalizeAscii(Trim(text), kMaxFunctionLength);
    for (char& ch : function) {
        if (ch == '"' || ch == '\\')
            ch = '_';
    }
    return function.empty() ? std::string("<unknown>") : function;
}

std::string SanitizeSource(std::string_view text)
{
    std::string source = NormalizeAscii(Trim(text), kMaxSourceLength * 2);
    std::replace(source.begin(), source.end(), '\\', '/');

    const std::string lower = [&source]() {
        std::string value = source;
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }();

    constexpr std::string_view scriptsMarker = "scripts/vscripts/";
    const std::size_t scriptsPosition = lower.find(scriptsMarker);
    if (scriptsPosition != std::string::npos) {
        source.erase(0, scriptsPosition + scriptsMarker.size());
    }
    else {
        const bool absolute = (source.size() >= 3 && std::isalpha(static_cast<unsigned char>(source[0]))
                && source[1] == ':' && source[2] == '/')
            || StartsWith(source, "//") || StartsWith(source, "/");
        if (absolute || source.find("../") != std::string::npos) {
            const std::size_t slash = source.find_last_of('/');
            source = slash == std::string::npos ? source : source.substr(slash + 1);
        }
    }

    while (StartsWith(source, "./"))
        source.erase(0, 2);
    for (char& ch : source) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (!(std::isalnum(value) || ch == '_' || ch == '-' || ch == '.' || ch == '/' || ch == '@'))
            ch = '_';
    }
    if (source.size() > kMaxSourceLength)
        source.resize(kMaxSourceLength);
    return source.empty() ? std::string("<unknown>") : source;
}

bool ParseFrame(std::string_view line, Frame& frame)
{
    line = Trim(line);
    constexpr std::string_view prefix = "*FUNCTION [";
    if (!StartsWith(line, prefix))
        return false;

    const std::size_t functionEnd = line.find(']', prefix.size());
    if (functionEnd == std::string_view::npos)
        return false;

    constexpr std::string_view lineMarker = " line [";
    const std::size_t lineMarkerPosition = line.rfind(lineMarker);
    if (lineMarkerPosition == std::string_view::npos || lineMarkerPosition <= functionEnd)
        return false;

    const std::size_t lineNumberStart = lineMarkerPosition + lineMarker.size();
    const std::size_t lineNumberEnd = line.find(']', lineNumberStart);
    if (lineNumberEnd == std::string_view::npos || lineNumberEnd + 1 != line.size())
        return false;

    int lineNumber = 0;
    const char* begin = line.data() + lineNumberStart;
    const char* end = line.data() + lineNumberEnd;
    const auto parsed = std::from_chars(begin, end, lineNumber);
    if (parsed.ec != std::errc() || parsed.ptr != end)
        return false;

    frame.function = SanitizeFunction(line.substr(prefix.size(), functionEnd - prefix.size()));
    frame.source = SanitizeSource(line.substr(functionEnd + 1, lineMarkerPosition - functionEnd - 1));
    frame.line = lineNumber;
    return true;
}

const char* ContextName(VmContext context)
{
    switch (context) {
    case VmContext::Server: return "SERVER";
    case VmContext::Client: return "CLIENT";
    case VmContext::Ui: return "UI";
    default: return "SERVER";
    }
}

const char* ProcessMode(VmContext context)
{
    if (IsDedicatedServer() || IsR1ODedicatedServer())
        return "DEDICATED";
    return context == VmContext::Server ? "LISTEN" : "CLIENT";
}

std::size_t ContextIndex(VmContext context)
{
    return static_cast<std::size_t>(context);
}

bool FindErrorText(std::string_view line, std::string_view& errorText)
{
    line = Trim(line);
    if (StartsWith(line, kErrorMarker)) {
        errorText = Trim(line.substr(kErrorMarker.size()));
        return true;
    }
    if (StartsWith(line, kAssertMarker)) {
        errorText = Trim(line.substr(kAssertMarker.size()));
        return true;
    }
    return false;
}

bool CouldEndWithErrorMarkerPrefix(std::string_view fragment)
{
    const std::size_t maxSuffix = std::min(fragment.size(), std::max(kErrorMarker.size(), kAssertMarker.size()));
    for (std::size_t length = 1; length <= maxSuffix; ++length) {
        const std::string_view suffix = fragment.substr(fragment.size() - length);
        if (StartsWith(kErrorMarker, suffix) || StartsWith(kAssertMarker, suffix))
            return true;
    }
    return false;
}

bool IsSection(std::string_view line, std::string_view name)
{
    line = Trim(line);
    return line == name || (line.size() > name.size() && StartsWith(line, name) && line[name.size()] == ' ');
}

bool IsTerminatingSection(std::string_view line)
{
    return IsSection(line, "LOCALS") || IsSection(line, "DIAGPRINTS");
}

std::wstring Utf8ToWide(std::string_view value)
{
    if (value.empty() || value.size() > kMaxServiceUrlLength)
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0)
        return {};
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), wide.data(), length) != length) {
        return {};
    }
    return wide;
}

bool ParseEndpoint(std::string_view serviceUrl, Endpoint& endpoint)
{
    const std::wstring url = Utf8ToWide(serviceUrl);
    if (url.empty())
        return false;

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUserNameLength = static_cast<DWORD>(-1);
    components.dwPasswordLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components))
        return false;
    if (components.nScheme != INTERNET_SCHEME_HTTPS || !components.lpszHostName
        || components.dwHostNameLength == 0 || components.dwUserNameLength != 0
        || components.dwPasswordLength != 0) {
        return false;
    }

    endpoint.host.assign(components.lpszHostName, components.dwHostNameLength);
    endpoint.port = components.nPort ? components.nPort : INTERNET_DEFAULT_HTTPS_PORT;
    if (components.lpszUrlPath && components.dwUrlPathLength)
        endpoint.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (endpoint.path.empty())
        endpoint.path = L"/";
    while (endpoint.path.size() > 1 && endpoint.path.back() == L'/')
        endpoint.path.pop_back();
    constexpr std::wstring_view reportsPath = L"/v1/reports";
    if (endpoint.path.size() < reportsPath.size()
        || endpoint.path.compare(endpoint.path.size() - reportsPath.size(), reportsPath.size(), reportsPath) != 0) {
        if (endpoint.path == L"/")
            endpoint.path.clear();
        endpoint.path.append(L"/v1/reports");
    }
    return true;
}

bool IsValidResponseCode(std::string_view code)
{
    if (code.size() < 8 || code.size() > 52)
        return false;
    return std::all_of(code.begin(), code.end(), [](char ch) {
        constexpr std::string_view alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
        return alphabet.find(ch) != std::string_view::npos;
    });
}

std::string BuildPayload(Report& report)
{
    nlohmann::json payload;
    payload["schema"] = 1;
    payload["vmContext"] = ContextName(report.context);
    payload["processMode"] = report.processMode;
    payload["cascade"] = report.cascade;
    payload["error"] = report.error;
    payload["frames"] = nlohmann::json::array();
    for (const Frame& frame : report.frames) {
        payload["frames"].push_back({
            { "function", frame.function },
            { "source", frame.source },
            { "line", frame.line }
        });
    }

    std::string serialized = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    while (serialized.size() > kMaxPayloadLength && !payload["frames"].empty()) {
        payload["frames"].erase(payload["frames"].end() - 1);
        serialized = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    while (serialized.size() > kMaxPayloadLength && report.error.size() > 64) {
        report.error.resize(report.error.size() - 64);
        payload["error"] = report.error;
        serialized = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    return serialized.size() <= kMaxPayloadLength ? serialized : std::string();
}

bool PostReport(Report& report, std::string& code, bool& isNew)
{
    Endpoint endpoint;
    if (!ParseEndpoint(report.serviceUrl, endpoint))
        return false;

    std::string payload = BuildPayload(report);
    if (payload.empty() || payload.size() > kMaxPayloadLength
        || payload.size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max())) {
        return false;
    }

    InternetHandle session(WinHttpOpen(L"R1Delta script error telemetry/1",
        WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session)
        return false;
    if (!WinHttpSetTimeouts(session.get(), kResolveTimeoutMs, kConnectTimeoutMs,
            kSendTimeoutMs, kReceiveTimeoutMs)) {
        return false;
    }
    DWORD connectRetries = 0;
    if (!WinHttpSetOption(session.get(), WINHTTP_OPTION_CONNECT_RETRIES,
            &connectRetries, sizeof(connectRetries))) {
        return false;
    }

    InternetHandle connection(WinHttpConnect(session.get(), endpoint.host.c_str(), endpoint.port, 0));
    if (!connection)
        return false;

    InternetHandle request(WinHttpOpenRequest(connection.get(), L"POST", endpoint.path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request)
        return false;

    DWORD disabledFeatures = WINHTTP_DISABLE_AUTHENTICATION | WINHTTP_DISABLE_COOKIES
        | WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_KEEP_ALIVE;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE,
            &disabledFeatures, sizeof(disabledFeatures))) {
        return false;
    }

    constexpr wchar_t headers[] = L"Content-Type: application/json\r\nAccept: application/json\r\nCache-Control: no-store\r\n";
    if (!WinHttpSendRequest(request.get(), headers, static_cast<DWORD>(-1),
            payload.data(), static_cast<DWORD>(payload.size()), static_cast<DWORD>(payload.size()), 0)) {
        return false;
    }
    if (!WinHttpReceiveResponse(request.get(), nullptr))
        return false;

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX)
        || statusCode != 200) {
        return false;
    }

    std::string response;
    response.reserve(256);
    std::array<char, 1024> buffer{};
    for (int readCount = 0; readCount < 5; ++readCount) {
        DWORD bytesRead = 0;
        if (!WinHttpReadData(request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead))
            return false;
        if (bytesRead == 0)
            break;
        if (response.size() + bytesRead > kMaxResponseLength)
            return false;
        response.append(buffer.data(), bytesRead);
        if (readCount == 4)
            return false;
    }

    const nlohmann::json parsed = nlohmann::json::parse(response, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("code")
        || !parsed["code"].is_string() || !parsed.contains("isNew") || !parsed["isNew"].is_boolean()) {
        return false;
    }

    const std::string& parsedCode = parsed["code"].get_ref<const std::string&>();
    if (!IsValidResponseCode(parsedCode))
        return false;
    code = parsedCode;
    isNew = parsed["isNew"].get<bool>();
    return true;
}

class Service
{
public:
    void Initialize()
    {
        std::lock_guard<std::mutex> lock(cvarMutex_);
        EnsureCvarsLocked();
    }

    void BeginErrorBlock(VmContext context)
    {
        if (ContextIndex(context) >= kContextCount)
            return;

        std::lock_guard<std::mutex> lock(stateMutex_);
        CaptureState& state = captures_[ContextIndex(context)];
        if (state.activeErrorBlocks++ == 0) {
            state.phase = CapturePhase::Idle;
            state.report = Report{};
            state.partialLine.clear();
        }
    }

    void Capture(VmContext context, const char* text)
    {
        if (!text || ContextIndex(context) >= kContextCount)
            return;

        const std::size_t length = strnlen_s(text, kMaxCaptureInput);
        if (length == 0)
            return;
        const Settings settings = ReadSettings();
        std::vector<Report> completed;

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            CaptureState& state = captures_[ContextIndex(context)];
            if (state.activeErrorBlocks == 0)
                return;
            std::size_t cursor = 0;

            if (!state.partialLine.empty()) {
                const std::size_t newline = std::string_view(text, length).find('\n');
                const std::size_t partLength = newline == std::string_view::npos ? length : newline;
                AppendPartial(state.partialLine, std::string_view(text, partLength));
                if (newline == std::string_view::npos)
                    return;
                ProcessLine(context, state, state.partialLine, settings, completed);
                state.partialLine.clear();
                cursor = newline + 1;
            }

            while (cursor < length) {
                const std::size_t newline = std::string_view(text + cursor, length - cursor).find('\n');
                if (newline == std::string_view::npos) {
                    const std::string_view tail(text + cursor, length - cursor);
                    if (IsTerminatingSection(tail) && state.phase != CapturePhase::Idle) {
                        ProcessLine(context, state, tail, settings, completed);
                    }
                    else if (ShouldKeepPartial(state, tail)) {
                        state.partialLine.assign(tail.substr(0, kMaxPartialLine));
                    }
                    break;
                }

                ProcessLine(context, state, std::string_view(text + cursor, newline), settings, completed);
                cursor += newline + 1;
            }
        }

        for (Report& report : completed)
            Enqueue(std::move(report));
    }

    void EndErrorBlock(VmContext context)
    {
        if (ContextIndex(context) >= kContextCount)
            return;

        std::vector<Report> completed;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            CaptureState& state = captures_[ContextIndex(context)];
            if (state.activeErrorBlocks == 0 || --state.activeErrorBlocks != 0)
                return;
            FinishReport(state, completed);
            state.partialLine.clear();
        }

        for (Report& report : completed)
            Enqueue(std::move(report));
    }

    bool GetNotification(VmContext context, NotificationSnapshot& snapshot)
    {
        if (ContextIndex(context) >= kContextCount)
            return false;
        std::lock_guard<std::mutex> lock(stateMutex_);
        const NotificationState& state = notifications_[ContextIndex(context)];
        snapshot.sequence = state.sequence;
        snapshot.hasResult = state.hasResult;
        snapshot.isNew = state.isNew;
        snapshot.code = state.code;
        return state.sequence != 0;
    }

private:
    void EnsureCvarsLocked()
    {
        constexpr int flags = FCVAR_ARCHIVE_PLAYERPROFILE;
        if (IsR1ODedicatedServer()) {
            if (!reportingR1O_)
                reportingR1O_ = RegisterR1ODediConVar("delta_script_error_reporting", "1", flags,
                    "Anonymously report sanitized script errors to R1Delta");
            if (!serviceUrlR1O_)
                serviceUrlR1O_ = RegisterR1ODediConVar("delta_script_error_service_url", kDefaultServiceUrl, flags,
                    "HTTPS service used for anonymous R1Delta script error reports");
            return;
        }

        if ((!IsDedicatedServer() && !G_engine) || (IsDedicatedServer() && !G_engine_ds))
            return;
        if (!reportingR1_) {
            reportingR1_ = cvarinterface && OriginalCCVar_FindVar
                ? OriginalCCVar_FindVar(cvarinterface, "delta_script_error_reporting") : nullptr;
            if (!reportingR1_)
                reportingR1_ = RegisterConVar("delta_script_error_reporting", "1", flags,
                    "Anonymously report sanitized script errors to R1Delta");
        }
        if (!serviceUrlR1_) {
            serviceUrlR1_ = cvarinterface && OriginalCCVar_FindVar
                ? OriginalCCVar_FindVar(cvarinterface, "delta_script_error_service_url") : nullptr;
            if (!serviceUrlR1_)
                serviceUrlR1_ = RegisterConVar("delta_script_error_service_url", kDefaultServiceUrl, flags,
                    "HTTPS service used for anonymous R1Delta script error reports");
        }
    }

    template <typename ConVarType>
    static const CVValue_t* ValueOf(ConVarType* variable)
    {
        if (!variable)
            return nullptr;
        ConVarType* parent = variable->m_pParent ? variable->m_pParent : variable;
        return &parent->m_Value;
    }

    Settings ReadSettings()
    {
        std::lock_guard<std::mutex> lock(cvarMutex_);
        EnsureCvarsLocked();

        Settings settings;
        const CVValue_t* reporting = IsR1ODedicatedServer()
            ? ValueOf(reportingR1O_) : ValueOf(reportingR1_);
        const CVValue_t* serviceUrl = IsR1ODedicatedServer()
            ? ValueOf(serviceUrlR1O_) : ValueOf(serviceUrlR1_);
        settings.ready = reporting && serviceUrl;
        if (reporting)
            settings.enabled = reporting->m_nValue != 0;
        if (serviceUrl && serviceUrl->m_pszString) {
            const std::size_t length = strnlen_s(serviceUrl->m_pszString, kMaxServiceUrlLength + 1);
            if (length > 0 && length <= kMaxServiceUrlLength)
                settings.serviceUrl.assign(serviceUrl->m_pszString, length);
        }
        return settings;
    }

    static void AppendPartial(std::string& destination, std::string_view source)
    {
        if (destination.size() >= kMaxPartialLine)
            return;
        const std::size_t available = kMaxPartialLine - destination.size();
        destination.append(source.data(), std::min(source.size(), available));
    }

    static bool ShouldKeepPartial(const CaptureState& state, std::string_view fragment)
    {
        fragment = Trim(fragment);
        if (fragment.empty())
            return false;
        std::string_view ignored;
        if (FindErrorText(fragment, ignored) || CouldEndWithErrorMarkerPrefix(fragment))
            return true;
        if (state.phase == CapturePhase::AwaitCallstack)
            return StartsWith("CALLSTACK", fragment);
        if (state.phase == CapturePhase::Callstack)
            return StartsWith("*FUNCTION [", fragment)
                || StartsWith("LOCALS", fragment) || StartsWith("DIAGPRINTS", fragment);
        return false;
    }

    void BeginReport(VmContext context, CaptureState& state, std::string_view errorText,
        const Settings& settings)
    {
        state.phase = CapturePhase::AwaitCallstack;
        state.report = Report{};
        state.report.context = context;
        state.report.sequence = ++nextSequence_;
        state.report.processMode = ProcessMode(context);
        const std::uint64_t now = GetTickCount64();
        state.report.cascade = lastErrorAtMs_ != 0 && now - lastErrorAtMs_ <= 30'000;
        lastErrorAtMs_ = now;
        state.report.error = SanitizeError(errorText);
        state.report.serviceUrl = settings.serviceUrl;
        state.report.eligible = settings.ready && settings.enabled;
        state.report.frames.reserve(kMaxFrames);

        NotificationState& notification = notifications_[ContextIndex(context)];
        notification.sequence = state.report.sequence;
        notification.hasResult = false;
        notification.isNew = false;
        notification.code.fill('\0');
    }

    static void FinishReport(CaptureState& state, std::vector<Report>& completed)
    {
        if (state.phase != CapturePhase::Idle && state.report.eligible)
            completed.push_back(std::move(state.report));
        state.report = Report{};
        state.phase = CapturePhase::Idle;
    }

    void ProcessLine(VmContext context, CaptureState& state, std::string_view line,
        const Settings& settings, std::vector<Report>& completed)
    {
        line = Trim(line);
        if (state.phase == CapturePhase::Details)
            return;

        std::string_view errorText;
        if (FindErrorText(line, errorText)) {
            FinishReport(state, completed);
            BeginReport(context, state, errorText, settings);
            if (Trim(errorText) == "[unknown]")
                FinishReport(state, completed);
            return;
        }

        if (state.phase == CapturePhase::Idle)
            return;
        if (IsTerminatingSection(line)) {
            FinishReport(state, completed);
            state.phase = CapturePhase::Details;
            return;
        }
        if (state.phase == CapturePhase::AwaitCallstack) {
            if (line == "CALLSTACK")
                state.phase = CapturePhase::Callstack;
            return;
        }
        if (line.empty())
            return;

        if (StartsWith(line, "*FUNCTION [")) {
            if (state.report.eligible && state.report.frames.size() < kMaxFrames) {
                Frame frame;
                if (ParseFrame(line, frame))
                    state.report.frames.push_back(std::move(frame));
            }
            return;
        }

        FinishReport(state, completed);
    }

    void Enqueue(Report report)
    {
        if (!report.eligible)
            return;

        std::lock_guard<std::mutex> lock(queueMutex_);
        if (queue_.size() == kMaxQueueEntries)
            queue_.pop_front();
        queue_.push_back(std::move(report));
        if (!workerStarted_) {
            try {
                worker_ = std::thread(&Service::WorkerLoop, this);
                workerStarted_ = true;
            }
            catch (...) {
                queue_.clear();
                return;
            }
        }
        queueReady_.notify_one();
    }

    void WorkerLoop()
    {
        for (;;) {
            Report report;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                queueReady_.wait(lock, [this] { return !queue_.empty(); });
                report = std::move(queue_.front());
                queue_.pop_front();
            }

            std::string code;
            bool isNew = false;
            try {
                const Settings settings = ReadSettings();
                if (!settings.ready || !settings.enabled)
                    continue;
                if (!PostReport(report, code, isNew))
                    continue;
            }
            catch (...) {
                continue;
            }

            Msg("Script error report uploaded as #%s%s.\n",
                code.c_str(), isNew ? "" : " (existing report)");

            std::lock_guard<std::mutex> lock(stateMutex_);
            NotificationState& notification = notifications_[ContextIndex(report.context)];
            if (notification.sequence != report.sequence)
                continue;
            notification.hasResult = true;
            notification.isNew = isNew;
            notification.code.fill('\0');
            std::memcpy(notification.code.data(), code.data(),
                std::min(code.size(), notification.code.size() - 1));
        }
    }

    std::mutex cvarMutex_;
    ConVarR1* reportingR1_ = nullptr;
    ConVarR1* serviceUrlR1_ = nullptr;
    ConVarR1O* reportingR1O_ = nullptr;
    ConVarR1O* serviceUrlR1O_ = nullptr;

    std::mutex stateMutex_;
    std::array<CaptureState, kContextCount> captures_{};
    std::array<NotificationState, kContextCount> notifications_{};
    std::uint64_t nextSequence_ = 0;
    std::uint64_t lastErrorAtMs_ = 0;

    std::mutex queueMutex_;
    std::condition_variable queueReady_;
    std::deque<Report> queue_;
    std::thread worker_;
    bool workerStarted_ = false;
};

Service& GetService()
{
    // The injected DLL has process lifetime. Joining a WinHTTP worker from DllMain would run under
    // loader lock and can deadlock, so this singleton is intentionally heap-lifetime and OS-reclaimed.
    static Service* service = new Service();
    return *service;
}

} // namespace

void Initialize() noexcept
{
    try {
        GetService().Initialize();
    }
    catch (...) {
    }
}

void BeginErrorBlock(VmContext context) noexcept
{
    try {
        GetService().BeginErrorBlock(context);
    }
    catch (...) {
    }
}

void CapturePrint(VmContext context, const char* text) noexcept
{
    try {
        GetService().Capture(context, text);
    }
    catch (...) {
    }
}

void EndErrorBlock(VmContext context) noexcept
{
    try {
        GetService().EndErrorBlock(context);
    }
    catch (...) {
    }
}

bool GetNotificationSnapshot(VmContext context, NotificationSnapshot& snapshot) noexcept
{
    snapshot = NotificationSnapshot{};
    try {
        return GetService().GetNotification(context, snapshot);
    }
    catch (...) {
        return false;
    }
}

} // namespace ScriptErrorTelemetry
