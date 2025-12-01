// MCP Server Implementation - Core server, protocol handling, and transport
#include "mcp_server.h"
#include "core.h"
#include "logging.h"
#include "cvar.h"
#include "squirrel.h"
#include "load.h"
#include <httplib.h>
#include <thread>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <random>
#ifdef _WIN32
#include <Windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

using json = nlohmann::json;

// Forward declarations
ConCommandR1* RegisterConCommand(const char* commandName, void (*callback)(const ::CCommand&), const char* helpString, int flags);

namespace MCPServer {

// ============================================================================
// VM Task Queue System
// ============================================================================

std::mutex                           g_vmTaskMutex;
std::deque<std::shared_ptr<VMTask>>  g_vmTasks;
std::thread::id                      g_engineThreadId;

void MCPResolveSQVMFrameTasksCommand(const ::CCommand& args) {
    g_engineThreadId = std::this_thread::get_id();

    std::deque<std::shared_ptr<VMTask>> local;
    {
        std::lock_guard<std::mutex> lock(g_vmTaskMutex);
        local.swap(g_vmTasks);
    }

    for (auto& t : local) {
        try {
            json r = t->fn();
            t->promise.set_value(std::move(r));
        } catch (...) {
            try { t->promise.set_exception(std::current_exception()); }
            catch (...) {}
        }
    }
}

json RunOnVMThreadAndWait(std::function<json()> fn, std::chrono::milliseconds timeout) {
    if (std::this_thread::get_id() == g_engineThreadId && g_engineThreadId != std::thread::id{}) {
        return fn();
    }

    auto task = std::make_shared<VMTask>();
    task->fn = std::move(fn);
    auto fut = task->promise.get_future();

    {
        std::lock_guard<std::mutex> lock(g_vmTaskMutex);
        g_vmTasks.push_back(task);
    }

    Cbuf_AddText(0, "mcp_resolve_sqvm_frame_tasks\n", 0);

    if (fut.wait_for(timeout) != std::future_status::ready) {
        throw std::runtime_error("Timed out waiting for VM task");
    }
    return fut.get();
}

// ============================================================================
// Console Output Capture Implementation
// ============================================================================

std::string GenerateGUID() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    const char* hex = "0123456789abcdef";
    std::stringstream ss;

    for (int i = 0; i < 32; i++) {
        if (i == 8 || i == 12 || i == 16 || i == 20) {
            ss << '-';
        }
        ss << hex[dis(gen)];
    }

    return ss.str();
}

std::string MakeEchoCommand(const std::string& marker) {
    std::string command = "echo ";
    command += marker;
    command.push_back('\n');
    return command;
}

std::string AppendNewlineIfNeeded(const std::string& command) {
    if (!command.empty() && command.back() == '\n') {
        return command;
    }
    std::string withTerminator = command;
    withTerminator.push_back('\n');
    return withTerminator;
}

ConsoleOutputCapture::ConsoleOutputCapture()
    : historyLimit(2000),
      capturing(false),
      truncated(false),
      captureState(CaptureState::Idle) {}

void ConsoleOutputCapture::StartCapture(const std::string& guid_param) {
    std::lock_guard<std::mutex> lock(mutex);
    this->guid = guid_param;
    lines.clear();
    truncated = false;
    capturing = false;
    captureState = CaptureState::WaitingForStart;
    cv.notify_all();
}

void ConsoleOutputCapture::StopCapture() {
    std::lock_guard<std::mutex> lock(mutex);
    capturing = false;
    guid.clear();
    captureState = CaptureState::Idle;
    cv.notify_all();
}

void ConsoleOutputCapture::AddLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex);

    const bool isStartMarkerPrefix = line.rfind("[MCP-START:", 0) == 0;
    const bool isEndMarkerPrefix = line.rfind("[MCP-END:", 0) == 0;

    if (!isStartMarkerPrefix && !isEndMarkerPrefix) {
        history.push_back(line);
        while (history.size() > historyLimit) {
            history.pop_front();
        }
    }

    if (guid.empty()) {
        return;
    }

    std::string startMarker = "[MCP-START:" + guid + "]";
    if (line.find(startMarker) != std::string::npos) {
        capturing = true;
        captureState = CaptureState::Capturing;
        cv.notify_all();
        return;
    }

    std::string endMarker = "[MCP-END:" + guid + "]";
    const bool containsEndMarker = line.find(endMarker) != std::string::npos;
    const bool isEchoCommand = line.find("echo") != std::string::npos;

    if (containsEndMarker && !isEchoCommand) {
        capturing = false;
        captureState = CaptureState::Completed;
        cv.notify_all();
        return;
    }

    if (capturing) {
        if (lines.size() < kCaptureLineLimit) {
            lines.push_back(line);
        }

        if (lines.size() >= kCaptureLineLimit && !truncated) {
            truncated = true;
            capturing = false;
            captureState = CaptureState::Completed;
            lines.push_back("[TRUNCATED DUE TO LENGTH]\n");
            cv.notify_all();
        }
    }
}

std::vector<std::string> ConsoleOutputCapture::GetAndClear() {
    std::lock_guard<std::mutex> lock(mutex);
    auto result = lines;
    lines.clear();
    truncated = false;
    return result;
}

std::vector<std::string> ConsoleOutputCapture::GetLastLines(size_t count) {
    std::lock_guard<std::mutex> lock(mutex);

    if (history.empty() || count == 0) {
        return {};
    }

    if (count > history.size()) {
        count = history.size();
    }

    std::vector<std::string> result;
    result.reserve(count);

    const size_t startIndex = history.size() - count;
    for (size_t i = startIndex; i < history.size(); ++i) {
        result.push_back(history[i]);
    }

    return result;
}

bool ConsoleOutputCapture::WaitForCaptureComplete(const std::string& guidToWait,
                                                  std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex);
    auto predicate = [this, &guidToWait]() {
        return this->guid != guidToWait || captureState == CaptureState::Completed;
    };

    if (!cv.wait_for(lock, timeout, predicate)) {
        return false;
    }

    return this->guid == guidToWait && captureState == CaptureState::Completed;
}

// ============================================================================
// Helper Functions (kept here as they're used by tool implementations)
// ============================================================================

static std::filesystem::path GetExecutableDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return std::filesystem::current_path();
    std::filesystem::path p(buf);
    return p.parent_path();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        std::filesystem::path p = std::filesystem::weakly_canonical(buf);
        return p.parent_path();
    }
    return std::filesystem::current_path();
#elif defined(__linux__)
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf)-1);
    if (n > 0) {
        buf[n] = '\0';
        std::filesystem::path p = std::filesystem::weakly_canonical(buf);
        return p.parent_path();
    }
    return std::filesystem::current_path();
#else
    return std::filesystem::current_path();
#endif
}

static std::filesystem::path GetScriptsRoot() {
    return GetExecutableDir() / "r1delta" / "scripts";
}

// String processing helpers
namespace detail {

inline void tolower_ascii_inplace(std::string& s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
}
inline std::string tolower_ascii(std::string_view sv) {
    std::string s(sv);
    tolower_ascii_inplace(s);
    return s;
}

inline bool isIdentStart(char c) {
    return (c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}
inline bool isIdent(char c) {
    return isIdentStart(c) || (c >= '0' && c <= '9');
}

inline void skipSpace(const std::string& t, size_t& i) {
    while (i < t.size()) {
        char c = t[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++i; continue; }
        break;
    }
}

template <class Fn>
void for_each_signature(const std::string& text, Fn&& emit) {
    const std::string needle = "function";
    size_t pos = 0, n = text.size();

    while (true) {
        pos = text.find(needle, pos);
        if (pos == std::string::npos) break;

        size_t i = pos + needle.size();
        if (pos > 0 && (isIdent(text[pos-1]) )) { ++pos; continue; }
        if (i < n && isIdent(text[i])) { ++pos; continue; }

        skipSpace(text, i);
        if (i >= n || !isIdentStart(text[i])) { ++pos; continue; }

        size_t nameBeg = i;
        while (i < n) {
            if (isIdent(text[i])) { ++i; continue; }
            if (i + 1 < n && text[i] == ':' && text[i + 1] == ':') { i += 2; continue; }
            if (text[i] == '.') { ++i; continue; }
            break;
        }
        if (i == nameBeg) { ++pos; continue; }
        std::string_view name(&text[nameBeg], i - nameBeg);

        skipSpace(text, i);
        if (i >= n || text[i] != '(') { ++pos; continue; }
        size_t argsBeg = ++i;

        size_t argsEnd = text.find(')', i);
        if (argsEnd == std::string::npos) break;

        size_t j = argsEnd + 1;
        skipSpace(text, j);
        if (j >= n || text[j] != '{') { pos = j; continue; }

        std::string_view args(&text[argsBeg], argsEnd - argsBeg);
        size_t lineNumber = 1 + static_cast<size_t>(std::count(text.begin(), text.begin() + nameBeg, '\n'));
        emit(name, args, lineNumber);

        pos = j + 1;
    }
}

inline bool fast_read_whole_file(const std::filesystem::path& p, std::string& out) {
    std::ifstream in(p, std::ios::in | std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    std::streamsize sz = in.tellg();
    if (sz < 0) return false;
    out.resize(static_cast<size_t>(sz));
    in.seekg(0, std::ios::beg);
    if (sz) in.read(out.data(), sz);
    return true;
}

} // namespace detail

struct NutSignatureRecord {
    std::string signature;
    std::string sourcePath;
    size_t line;
};

struct SourceSearchResult {
    json matches;
    bool hasMore;
};

static std::unordered_map<std::string, NutSignatureRecord>
BuildNutSignatureIndex(const std::filesystem::path& root,
                       const std::unordered_set<std::string>& neededNames)
{
    std::unordered_map<std::string, NutSignatureRecord> out;
    if (!std::filesystem::exists(root)) return out;

    std::unordered_set<std::string> targets;
    targets.reserve(neededNames.size() * 2);
    for (const auto& n : neededNames) {
        targets.insert(detail::tolower_ascii(n));
    }
    const bool filter = !targets.empty();
    if (filter) out.reserve(targets.size());

    std::vector<std::filesystem::path> files;
    files.reserve(1024);
    using std::filesystem::recursive_directory_iterator;
    using std::filesystem::directory_options;
    std::error_code ec;
    for (recursive_directory_iterator it(root, directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        std::error_code fec;
        if (!it->is_regular_file(fec) || fec) continue;
        if (it->path().extension() == ".nut") files.push_back(it->path());
    }
    if (files.empty()) return out;

    const unsigned tc = std::max(1u, std::thread::hardware_concurrency());
    std::atomic<size_t> index{0};
    std::mutex outMx;

    auto worker = [&] {
        std::string text;
        std::unordered_map<std::string, NutSignatureRecord> local;
        local.reserve(128);

        while (true) {
            size_t i = index.fetch_add(1, std::memory_order_relaxed);
            if (i >= files.size()) break;

            const auto& filePath = files[i];
            if (!detail::fast_read_whole_file(filePath, text)) continue;

            std::error_code rec;
            auto rel = std::filesystem::relative(filePath, root, rec);
            std::string relPathStr = rec ? filePath.generic_string() : rel.generic_string();
            if (relPathStr.empty()) {
                relPathStr = filePath.generic_string();
            }

            detail::for_each_signature(text, [&](std::string_view name, std::string_view args, size_t line) {
                std::string nameLow = detail::tolower_ascii(name);
                if (filter && !targets.count(nameLow)) return;

                if (!local.count(nameLow)) {
                    std::string sig;
                    sig.reserve(10 + name.size() + args.size());
                    sig.append("function ").append(name).push_back('(');
                    sig.append(args).push_back(')');
                    local.emplace(
                        std::move(nameLow),
                        NutSignatureRecord{
                            std::move(sig),
                            relPathStr,
                            line
                        }
                    );
                }
            });

            if (filter && local.size() >= targets.size()) break;
        }

        if (!local.empty()) {
            std::lock_guard<std::mutex> lk(outMx);
            for (auto& kv : local) {
                out.emplace(std::move(kv));
            }
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(tc);
    for (unsigned t = 0; t < tc; ++t) pool.emplace_back(worker);
    for (auto& th : pool) th.join();

    return out;
}

static SourceSearchResult SearchNutSourcesForText(const std::filesystem::path& root,
                                                  const std::string& needle,
                                                  size_t limit,
                                                  bool caseSensitive)
{
    SourceSearchResult result{json::array(), false};
    if (needle.empty() || limit == 0) return result;
    if (!std::filesystem::exists(root)) return result;

    const std::string preparedNeedle = caseSensitive ? needle : detail::tolower_ascii(needle);

    using std::filesystem::recursive_directory_iterator;
    using std::filesystem::directory_options;
    std::error_code ec;
    for (recursive_directory_iterator it(root, directory_options::skip_permission_denied, ec), end;
         it != end && !result.hasMore;
         it.increment(ec))
    {
        if (ec) { ec.clear(); continue; }
        std::error_code fec;
        if (!it->is_regular_file(fec) || fec) continue;
        if (it->path().extension() != ".nut") continue;

        std::ifstream file(it->path());
        if (!file) continue;

        std::error_code rec;
        auto rel = std::filesystem::relative(it->path(), root, rec);
        std::string relPathStr = rec ? it->path().generic_string() : rel.generic_string();
        if (relPathStr.empty()) {
            relPathStr = it->path().generic_string();
        }

        std::string line;
        size_t lineNumber = 0;
        while (std::getline(file, line)) {
            ++lineNumber;
            const std::string haystack = caseSensitive ? line : detail::tolower_ascii(line);
            if (haystack.find(preparedNeedle) == std::string::npos) continue;

            if (result.matches.size() < limit) {
                std::string snippet = line;
                constexpr size_t kMaxSnippet = 240;
                if (snippet.size() > kMaxSnippet) {
                    if (kMaxSnippet > 3) snippet.resize(kMaxSnippet - 3);
                    snippet.append("...");
                }
                result.matches.push_back({
                    {"path", relPathStr},
                    {"line", static_cast<int>(lineNumber)},
                    {"snippet", snippet}
                });
            } else {
                result.hasMore = true;
                break;
            }
        }
    }
    return result;
}

static bool IsInGame() {
    if (IsDedicatedServer()) {
        auto* mapVar = CCVar_FindVar(cvarinterface, "host_map");
        return mapVar && mapVar->m_Value.m_pszString && mapVar->m_Value.m_pszString[0] != '\0';
    }
    if (!G_engine) return false;
    auto cl = reinterpret_cast<void*>(G_engine + 0x797070);
    unsigned int& baseLocalClient__m_nSignonState = *((_DWORD*)cl + 29);
    return baseLocalClient__m_nSignonState == 8;
}

// ============================================================================
// Echo Command Fix
// ============================================================================

void EchoCommand(const ::CCommand& args) {
    if (args.ArgC() > 1) {
        Msg("%s\n", args.ArgS());
    }
}

// ============================================================================
// MCP Server Implementation
// ============================================================================

Server::Server() : m_running(false), m_initialized(false), m_port(8765) {
    m_httpServer = std::make_unique<httplib::Server>();
}

Server::~Server() {
    Stop();
}

Server& Server::GetInstance() {
    static Server instance;
    return instance;
}

void Server::CaptureConsoleOutput(const char* msg) {
    m_outputCapture.AddLine(msg);
}

// ============================================================================
// JSON-RPC 2.0 Message Handling
// ============================================================================

json Server::CreateErrorResponse(int code, const std::string& message, const json& id) {
    json response = {
        {"jsonrpc", "2.0"},
        {"error", {
            {"code", code},
            {"message", message}
        }}
    };
    if (!id.is_null()) {
        response["id"] = id;
    }
    return response;
}

json Server::CreateSuccessResponse(const json& result, const json& id) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
}

json Server::HandleMessage(const json& message) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!message.contains("jsonrpc") || message["jsonrpc"] != "2.0") {
        return CreateErrorResponse(-32600, "Invalid Request: missing or invalid jsonrpc field");
    }

    if (!message.contains("method")) {
        if (message.contains("id")) {
            return CreateErrorResponse(-32600, "Invalid Request: server does not accept responses");
        }
        return json();
    }

    bool isRequest = message.contains("id");
    if (isRequest) {
        return HandleRequest(message);
    } else {
        return HandleNotification(message);
    }
}

json Server::HandleRequest(const json& request) {
    std::string method = request["method"];
    json params = request.value("params", json::object());
    json id = request["id"];

    if (id.is_null()) {
        return CreateErrorResponse(-32600, "Invalid Request: id must not be null");
    }

    try {
        if (method == "initialize") {
            return CreateSuccessResponse(HandleInitialize(params), id);
        }
        else if (method == "tools/list") {
            if (!m_initialized) {
                return CreateErrorResponse(-32002, "Server not initialized", id);
            }
            return CreateSuccessResponse(HandleToolsList(params), id);
        }
        else if (method == "tools/call") {
            if (!m_initialized) {
                return CreateErrorResponse(-32002, "Server not initialized", id);
            }
            return CreateSuccessResponse(HandleToolsCall(params), id);
        }
        else {
            return CreateErrorResponse(-32601, "Method not found: " + method, id);
        }
    }
    catch (const std::exception& e) {
        return CreateErrorResponse(-32603, std::string("Internal error: ") + e.what(), id);
    }
}

json Server::HandleNotification(const json& notification) {
    std::string method = notification["method"];
    json params = notification.value("params", json::object());

    try {
        if (method == "initialized") {
            HandleInitialized(params);
        }
        else if (method == "notifications/cancelled") {
            // Handle cancellation if needed
        }
        return json();
    }
    catch (const std::exception& e) {
        Warning("Error handling notification %s: %s\n", method.c_str(), e.what());
        return json();
    }
}

// ============================================================================
// MCP Protocol Methods
// ============================================================================

json Server::HandleInitialize(const json& params) {
    std::string protocolVersion = params.value("protocolVersion", "2025-06-18");
    json clientInfo = params.value("clientInfo", json::object());

    m_initialized = false;

    if (m_port > 0) {
        m_sessionId = GenerateGUID();
        Msg("MCP: New session created: %s\n", m_sessionId.c_str());
    }

    json result = {
        {"protocolVersion", MCP_PROTOCOL_VERSION},
        {"capabilities", {
            {"tools", {
                {"listChanged", false}
            }}
        }},
        {"serverInfo", {
            {"name", "r1delta-mcp-server"},
            {"version", "1.0.0"}
        }}
    };
    HandleInitialized(params);
    return result;
}

json Server::HandleInitialized(const json& params) {
    m_initialized = true;
    Msg("MCP client initialized\n");
    return json();
}

std::vector<Tool> Server::GetAvailableTools() const {
    std::vector<Tool> tools;

    tools.push_back({
        "execute_squirrel",
        "Execute Squirrel Script",
        "Execute Squirrel code in the specified VM context (server, client, or ui)",
        {
            {"type", "object"},
            {"properties", {
                {"script", {{"type", "string"}, {"description", "The Squirrel code to execute"}}},
                {"context", {{"type", "string"}, {"enum", json::array({"server", "client", "ui"})}, {"description", "The VM context to execute in"}, {"default", "server"}}},
                {"capture_output", {{"type", "boolean"}, {"description", "Whether to capture console output"}, {"default", true}}}
            }},
            {"required", json::array({"script"})}
        }
    });

    tools.push_back({
        "execute_console_command",
        "Execute Console Command",
        "Execute a console command in the game engine. Note that a lot of ConCommands do not return output in Titanfall.",
        {
            {"type", "object"},
            {"properties", {
                {"command", {{"type", "string"}, {"description", "The console command to execute"}}},
                {"capture_output", {{"type", "boolean"}, {"description", "Whether to capture console output"}, {"default", true}}}
            }},
            {"required", json::array({"command"})}
        }
    });

    tools.push_back({
        "get_game_state",
        "Get Game State",
        "Query the current game state including map, server type, and connection status",
        {{"type", "object"}, {"properties", json::object()}}
    });

    tools.push_back({
        "search_script_functions",
        "Search Script Functions",
        "Search for native and non-native script functions by name and docstring in a specific context, optionally including raw source matches.",
        {
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description", "The case-insensitive search query to match against function names and descriptions."}}},
                {"context", {{"type", "string"}, {"enum", json::array({"server", "client", "ui"})}, {"description", "The script VM context to search in."}, {"default", "server"}}},
                {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}, {"default", 50}, {"description", "Max results per page"}}},
                {"offset", {{"type", "integer"}, {"minimum", 0}, {"default", 0}, {"description", "Start index (ignored if cursor is provided)"}}},
                {"cursor", {{"type", "string"}, {"description", "Opaque cursor (use nextCursor from previous response)"}}},
                {"include_source_matches", {{"type", "boolean"}, {"description", "When true, also scan script source files for substring matches. SLOW."}, {"default", false}}},
                {"source_match_limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}, {"default", 25}, {"description", "Maximum number of source code matches to return when include_source_matches is true."}}},
                {"source_case_sensitive", {{"type", "boolean"}, {"description", "Perform source substring search using case-sensitive matching."}, {"default", false}}}
            }},
            {"required", json::array({"query"})}
        }
    });

    tools.push_back({
        "get_console_log",
        "Get Console Log",
        "Return the last 50 lines of the game console output.",
        {{"type", "object"}, {"properties", json::object()}}
    });

    return tools;
}

json Server::HandleToolsList(const json& params) {
    auto tools = GetAvailableTools();
    json toolsArray = json::array();

    for (const auto& tool : tools) {
        toolsArray.push_back({
            {"name", tool.name},
            {"title", tool.title},
            {"description", tool.description},
            {"inputSchema", tool.inputSchema}
        });
    }

    return {{"tools", toolsArray}};
}

json Server::HandleToolsCall(const json& params) {
    if (!params.contains("name")) {
        throw std::runtime_error("Missing required parameter: name");
    }

    std::string toolName = params["name"];
    json arguments = params.value("arguments", json::object());

    if (toolName == "execute_squirrel") {
        return ExecuteSquirrelScript(arguments);
    }
    else if (toolName == "execute_console_command") {
        return ExecuteConsoleCommand(arguments);
    }
    else if (toolName == "get_game_state") {
        return GetGameState(arguments);
    }
    else if (toolName == "get_console_log") {
        return GetConsoleLog(arguments);
    }
    else if (toolName == "search_script_functions") {
        return SearchScriptFunctions(arguments);
    }
    else {
        throw std::runtime_error("Unknown tool: " + toolName);
    }
}

// ============================================================================
// Tool Implementations
// ============================================================================

json Server::ExecuteSquirrelScript(const json& args) {
    if (!args.contains("script")) {
        throw std::runtime_error("Missing required argument: script");
    }

    std::string script = args["script"];
    std::string context = args.value("context", "server");
    bool capture = args.value("capture_output", true);
    if (context != "server" && IsDedicatedServer()) {
        return json{{"content", "Dedi server. Invalid VM context."}, {"isError", true}};
    }

    R1SquirrelVM* vm = nullptr;
    if (context == "server") vm = GetServerVMPtr();
    else if (context == "client") vm = GetClientVMPtr();
    else if (context == "ui") vm = GetUIVMPtr();
    else throw std::runtime_error("Invalid context. Must be 'server', 'client', or 'ui'");

    if (!vm) throw std::runtime_error("VM not available for context: " + context);

    std::string guid;
    if (capture) {
        guid = GenerateGUID();
        m_outputCapture.StartCapture(guid);
        std::string startMarker = "[MCP-START:" + guid + "]";
        std::string startCommand = MakeEchoCommand(startMarker);
        Cbuf_AddText(0, startCommand.c_str(), 0);
    }

    json executionResult = RunOnVMThreadAndWait([=]() -> json {
        static auto fatal_script_errors = OriginalCCVar_FindVar(cvarinterface, "fatal_script_errors");
        auto bak = fatal_script_errors->m_pParent->m_Value.m_nValue;
        fatal_script_errors->m_pParent->m_Value.m_nValue = 0;
        typedef SQRESULT(*SQCompileBufferFn)(HSQUIRRELVM, const SQChar*, SQInteger, const SQChar*, SQBool);
        typedef __int64(*BaseGetRootTableFn)(HSQUIRRELVM);
        typedef SQRESULT(*SQCallFn)(HSQUIRRELVM, SQInteger, SQBool, SQBool);

        auto launcher = G_launcher;
        SQCompileBufferFn sq_compilebuffer = reinterpret_cast<SQCompileBufferFn>(launcher + (IsDedicatedServer() ? 0x1A6C0 : 0x1A5E0));
        BaseGetRootTableFn base_getroottable = reinterpret_cast<BaseGetRootTableFn>(launcher + (IsDedicatedServer() ? 0x56520 : 0x56440));
        SQCallFn sq_call = reinterpret_cast<SQCallFn>(launcher + (IsDedicatedServer() ? 0x18D20 : 0x18C40));

        SQRESULT compileRes = sq_compilebuffer(vm->sqvm, script.c_str(), static_cast<SQInteger>(script.length()), "mcp", 1);

        bool success = false;
        std::string errorMsg;

        if (SQ_SUCCEEDED(compileRes)) {
            base_getroottable(vm->sqvm);
            SQRESULT callRes = sq_call(vm->sqvm, 1, SQFalse, SQTrue);
            success = SQ_SUCCEEDED(callRes);
            if (!success) errorMsg = "Script execution failed";
        } else {
            errorMsg = "Script compilation failed";
        }

        json result = {{"success", success}};
        if (!success) result["errorMessage"] = errorMsg;
        fatal_script_errors->m_pParent->m_Value.m_nValue = bak;
        return result;
    });

    bool success = executionResult.value("success", false);
    std::string errorMsg = executionResult.value("errorMessage", std::string(success ? "" : "Script execution failed"));

    json content = json::array();

    if (capture) {
        std::string endMarker = "[MCP-END:" + guid + "]";
        std::string endCommand = MakeEchoCommand(endMarker);
        Cbuf_AddText(0, endCommand.c_str(), 0);

        bool finished = m_outputCapture.WaitForCaptureComplete(guid, kCaptureWaitTimeout);
        auto output = m_outputCapture.GetAndClear();
        m_outputCapture.StopCapture();

        std::string outputText;
        for (const auto& line : output) outputText += line;

        if (!finished) {
            if (!outputText.empty() && outputText.back() != '\n') outputText.push_back('\n');
            outputText += "[MCP] Output capture timed out before completion.\n";
        }

        content.push_back({{"type", "text"}, {"text", outputText}});
    } else {
        content.push_back({{"type", "text"}, {"text", success ? "Script executed successfully" : errorMsg}});
    }

    return json{{"content", content}, {"isError", !success}};
}

json Server::ExecuteConsoleCommand(const json& args) {
    if (!args.contains("command")) {
        throw std::runtime_error("Missing required argument: command");
    }

    std::string command = args["command"];
    bool capture = args.value("capture_output", true);

    std::string guid;
    if (capture) {
        guid = GenerateGUID();
        m_outputCapture.StartCapture(guid);
        std::string startMarker = "[MCP-START:" + guid + "]";
        std::string startCommand = MakeEchoCommand(startMarker);
        Cbuf_AddText(0, startCommand.c_str(), 0);
    }

    if (capture) {
        std::string commandText = AppendNewlineIfNeeded(command);
        Cbuf_AddText(0, commandText.c_str(), 0);
    } else {
        Cbuf_AddText(0, command.c_str(), 0);
    }

    json content = json::array();

    if (capture) {
        std::this_thread::sleep_for(std::chrono::milliseconds(6000));

        std::string endMarker = "[MCP-END:" + guid + "]";
        std::string endCommand = MakeEchoCommand(endMarker);
        Cbuf_AddText(0, endCommand.c_str(), 0);

        bool finished = m_outputCapture.WaitForCaptureComplete(guid, kCaptureWaitTimeout);
        auto output = m_outputCapture.GetAndClear();
        m_outputCapture.StopCapture();

        std::string outputText;
        for (const auto& line : output) outputText += line;

        if (!finished) {
            if (!outputText.empty() && outputText.back() != '\n') outputText.push_back('\n');
            outputText += "[MCP] Output capture timed out before completion.\n";
        }

        content.push_back({{"type", "text"}, {"text", outputText}});
    } else {
        content.push_back({{"type", "text"}, {"text", "Command executed"}});
    }

    return {{"content", content}, {"isError", false}};
}

json Server::GetGameState(const json& args) {
    bool inGame = IsInGame();
    bool isDedicated = IsDedicatedServer();

    json stateData = {{"in_game", inGame}, {"is_dedicated", isDedicated}};

    auto* mapVar = CCVar_FindVar(cvarinterface, "host_map");
    if (mapVar && mapVar->m_Value.m_pszString) {
        stateData["current_map"] = mapVar->m_Value.m_pszString;
    }

    std::string stateText = "Game State:\n";
    stateText += "  In Game: " + std::string(inGame ? "Yes" : "No") + "\n";
    stateText += "  Dedicated Server: " + std::string(isDedicated ? "Yes" : "No") + "\n";
    if (stateData.contains("current_map") && IsInGame()) {
        stateText += "  Current Map: " + stateData["current_map"].get<std::string>() + "\n";
    }

    json content = json::array();
    content.push_back({{"type", "text"}, {"text", stateText}});

    return {{"content", content}, {"structuredContent", stateData}, {"isError", false}};
}

json Server::GetConsoleLog(const json& args) {
    constexpr size_t kRequestedLines = 50;
    auto lines = m_outputCapture.GetLastLines(kRequestedLines);

    json structured = {
        {"requested_line_count", kRequestedLines},
        {"returned_line_count", lines.size()},
        {"lines", lines}
    };

    std::ostringstream oss;
    if (lines.empty()) {
        oss << "Console log is empty.";
    } else {
        oss << "Console log (latest " << lines.size() << " lines):\n";
        for (const auto& line : lines) {
            oss << line;
            if (!line.empty() && line.back() != '\n') oss << '\n';
        }
    }

    json content = json::array();
    content.push_back({{"type", "text"}, {"text", oss.str()}});

    return {{"content", content}, {"structuredContent", structured}, {"isError", false}};
}

json Server::SearchScriptFunctions(const json& args) {
    if (!args.contains("query")) {
        Warning("search_script_functions: Missing required argument: query\n");
        json content = json::array({{{"type","text"}, {"text","Error: Missing required argument: query"}}});
        return {{"content",content}, {"isError",true}};
    }

    std::string query = args["query"];
    std::string context = args.value("context", "server");

    bool includeSourceMatches = true;
    if (args.contains("include_source_matches")) {
        try { includeSourceMatches = args["include_source_matches"].get<bool>(); } catch (...) {}
    }

    bool sourceCaseSensitive = false;
    if (args.contains("source_case_sensitive")) {
        try { sourceCaseSensitive = args["source_case_sensitive"].get<bool>(); } catch (...) {}
    }

    int sourceLimit = 25;
    if (args.contains("source_match_limit")) {
        try { sourceLimit = std::clamp(args["source_match_limit"].get<int>(), 1, 200); } catch (...) {}
    }

    R1SquirrelVM* vm = nullptr;
    if (context == "server") vm = GetServerVMPtr();
    else if (context == "client") vm = GetClientVMPtr();
    else if (context == "ui") vm = GetUIVMPtr();
    else {
        Warning("search_script_functions: Invalid context '%s'. Must be 'server', 'client', or 'ui'\n", context.c_str());
        json content = json::array({{{"type","text"}, {"text","Error: Invalid context. Must be 'server', 'client', or 'ui'"}}});
        return {{"content",content}, {"isError",true}};
    }

    if (!vm || !vm->sqvm) {
        Warning("search_script_functions: VM not available for context: %s\n", context.c_str());
        json content = json::array({{{"type","text"}, {"text","Error: VM not available for context: " + context}}});
        return {{"content",content}, {"isError",true}};
    }

    return RunOnVMThreadAndWait([=]() -> json {
        auto to_lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        };
        const std::string query_lower = to_lower(query);

        HSQUIRRELVM v = vm->sqvm;
        const auto scriptsRoot = GetScriptsRoot();

        using BaseGetRootTableFn = __int64(*)(HSQUIRRELVM);
        auto launcher = G_launcher;
        BaseGetRootTableFn base_getroottable = reinterpret_cast<BaseGetRootTableFn>(launcher + (IsDedicatedServer() ? 0x56520 : 0x56440));

        SQInteger savedTop = sq_gettop(nullptr, v);

        // Validate Documentation.functions exists
        {
            base_getroottable(v);
            sq_pushstring(v, "Documentation", -1);
            if (SQ_FAILED(sq_get(v, -2)) || sq_gettype(v, -1) != OT_TABLE) {
                sq_settop(v, savedTop);
                json content = json::array({{{"type","text"}, {"text","Error: 'Documentation' table not found in " + context + " VM."}}});
                return json{{"content",content}, {"isError",true}};
            }
            sq_pushstring(v, "functions", -1);
            if (SQ_FAILED(sq_get(v, -2)) || sq_gettype(v, -1) != OT_TABLE) {
                sq_settop(v, savedTop);
                json content = json::array({{{"type","text"}, {"text","Error: 'Documentation.functions' table not found in " + context + " VM."}}});
                return json{{"content",content}, {"isError",true}};
            }
            sq_settop(v, savedTop);
        }

        json results = json::array();
        std::unordered_map<std::string, json> doc_all;
        std::unordered_set<std::string> seen;

        // Iterate Documentation.functions
        {
            base_getroottable(v);
            sq_pushstring(v, "Documentation", -1);
            if (SQ_FAILED(sq_get(v, -2)) || sq_gettype(v, -1) != OT_TABLE) { sq_settop(v, savedTop); goto pack_results; }
            sq_pushstring(v, "functions", -1);
            if (SQ_FAILED(sq_get(v, -2)) || sq_gettype(v, -1) != OT_TABLE) { sq_settop(v, savedTop); goto pack_results; }

            sq_pushnull(v);
            while (SQ_SUCCEEDED(sq_next(v, -2))) {
                const SQChar* name_str = nullptr;
                const SQChar* sig_str = nullptr;
                const SQChar* desc_str = nullptr;

                if (sq_gettype(v, -2) == OT_STRING) sq_getstring(v, -2, &name_str);

                if (name_str && sq_gettype(v, -1) == OT_ARRAY && sq_getsize(nullptr, v, -1) >= 2) {
                    sq_pushinteger(nullptr, v, 0);
                    sq_get(v, -2);
                    if (sq_gettype(v, -1) == OT_STRING) sq_getstring(v, -1, &sig_str);
                    sq_pop(v, 1);
                    sq_pushinteger(nullptr, v, 1);
                    sq_get(v, -2);
                    if (sq_gettype(v, -1) == OT_STRING) sq_getstring(v, -1, &desc_str);
                    sq_pop(v, 1);

                    const std::string name = name_str;
                    const std::string name_lower = to_lower(name);
                    const std::string sig = sig_str ? sig_str : "";
                    const std::string desc = desc_str ? desc_str : "";

                    doc_all[name_lower] = {{"name", name}, {"signature", sig}, {"description", desc}};

                    std::string desc_l = to_lower(desc);
                    if (name_lower.find(query_lower) != std::string::npos ||
                        (!desc_l.empty() && desc_l.find(query_lower) != std::string::npos)) {
                        results.push_back(doc_all[name_lower]);
                        seen.insert(name_lower);
                    }
                }
                sq_pop(v, 2);
            }
            sq_pop(v, 1);
            sq_settop(v, savedTop);
        }

    pack_results:
        // Iterate root table for functions not in docs
        std::unordered_set<std::string> names_missing_sig;
        {
            base_getroottable(v);
            sq_pushnull(v);
            while (SQ_SUCCEEDED(sq_next(v, -2))) {
                const SQChar* name_str = nullptr;
                if (sq_gettype(v, -2) == OT_STRING) sq_getstring(v, -2, &name_str);

                if (name_str) {
                    SQObjectType vt = sq_gettype(v, -1);
                    if (vt == OT_CLOSURE || vt == OT_NATIVECLOSURE) {
                        const std::string name = name_str;
                        const std::string name_lower = to_lower(name);

                        if (name_lower.find(query_lower) != std::string::npos) {
                            if (!seen.count(name_lower)) {
                                auto it = doc_all.find(name_lower);
                                if (it != doc_all.end()) {
                                    results.push_back(it->second);
                                    if (it->second.value("signature", "").empty() && vt == OT_CLOSURE) {
                                        names_missing_sig.insert(name_lower);
                                    }
                                } else {
                                    json obj = {{"name", name}, {"signature", ""}, {"description", ""}};
                                    results.push_back(obj);
                                    if (vt == OT_CLOSURE) names_missing_sig.insert(name_lower);
                                }
                                seen.insert(name_lower);
                            }
                        }
                    }
                }
                sq_pop(v, 2);
            }
            sq_pop(v, 1);
            sq_settop(v, savedTop);
        }

        // Fill missing signatures from disk
        std::unordered_map<std::string, NutSignatureRecord> sigIndex;
        if (!names_missing_sig.empty()) {
            sigIndex = BuildNutSignatureIndex(scriptsRoot, names_missing_sig);
            if (!sigIndex.empty()) {
                for (auto& item : results) {
                    const std::string nameLower = to_lower(item.value("name", ""));
                    auto it = sigIndex.find(nameLower);
                    if (it == sigIndex.end()) continue;

                    const auto& record = it->second;
                    if (item.value("signature", "").empty()) item["signature"] = record.signature;
                    if (item.value("description", "").empty()) {
                        item["description"] = std::string("<non-native script func defined at ") + record.sourcePath + " line no " + std::to_string(record.line) + ">";
                    }
                    item["definition"] = {{"path", record.sourcePath}, {"line", record.line}};
                }
            }
        }

        SourceSearchResult sourceSearch{json::array(), false};
        if (includeSourceMatches) {
            sourceSearch = SearchNutSourcesForText(scriptsRoot, query, static_cast<size_t>(sourceLimit), sourceCaseSensitive);
        }

        // Pagination
        int limit = 50;
        int offset = 0;
        try { if (args.contains("limit")) limit = std::max(1, std::min(500, (int)args["limit"])); } catch(...) {}
        if (args.contains("cursor") && args["cursor"].is_string()) {
            try { offset = std::stoi(args["cursor"].get<std::string>()); } catch (...) { offset = 0; }
        } else if (args.contains("offset")) {
            try { offset = std::max(0, (int)args["offset"]); } catch (...) { offset = 0; }
        }

        const int total = (int)results.size();
        const int start = std::min(offset, total);
        const int end = std::min(start + limit, total);
        json pageResults = json::array();
        for (int i = start; i < end; ++i) pageResults.push_back(results[i]);

        const bool hasMore = end < total;
        const std::string nextCursor = hasMore ? std::to_string(end) : "";

        json structured = {
            {"results", pageResults}, {"count", (int)pageResults.size()}, {"total", total}, {"context", context}, {"query", query},
            {"page", {{"offset", start}, {"limit", limit}, {"nextCursor", nextCursor}, {"hasMore", hasMore}}}
        };

        if (includeSourceMatches) {
            structured["sourceSearch"] = {
                {"matches", sourceSearch.matches}, {"returned", static_cast<int>(sourceSearch.matches.size())},
                {"limit", sourceLimit}, {"hasMore", sourceSearch.hasMore}, {"caseSensitive", sourceCaseSensitive}
            };
        }

        json content = json::array();
        content.push_back({{"type","text"}, {"text", structured.dump()}});
        content.push_back({{"type","text"}, {"text", "Found " + std::to_string(total) + " functions; showing " + std::to_string(start+1) + "-" + std::to_string(end) + " (limit=" + std::to_string(limit) + ")."}});

        if (includeSourceMatches) {
            std::string sourceSummary = "Source scan returned " + std::to_string(sourceSearch.matches.size());
            if (sourceSearch.hasMore) sourceSummary += " (truncated at limit " + std::to_string(sourceLimit) + ")";
            sourceSummary += " matches.";
            if (sourceCaseSensitive) sourceSummary += " (case-sensitive)";
            content.push_back({{"type", "text"}, {"text", sourceSummary}});
        }

        return json{{"content", content}, {"structuredContent", structured}, {"isError", false}};
    });
}

// ============================================================================
// stdio Transport
// ============================================================================

void Server::StartStdio() {
    if (m_running) {
        Warning("MCP Server already running\n");
        return;
    }

    Msg("Starting MCP Server on stdio transport...\n");
    m_running = true;

    m_serverThread = std::make_unique<std::thread>([this]() {
        StdioLoop();
    });

    Msg("MCP Server started on stdio\n");
}

void Server::StdioLoop() {
    std::string line;

    while (m_running) {
        if (!std::getline(std::cin, line)) break;

        line.erase(line.find_last_not_of(" \t\n\r") + 1);
        line.erase(0, line.find_first_not_of(" \t\n\r"));

        if (line.empty()) continue;

        try {
            json message = json::parse(line);
            json response = HandleMessage(message);

            if (!response.is_null() && !response.empty()) {
                std::string responseStr = response.dump();
                if (responseStr.find('\n') != std::string::npos) {
                    Warning("Response contains newline, skipping\n");
                    continue;
                }
                std::cout << responseStr << std::endl;
                std::cout.flush();
            }
        }
        catch (const json::parse_error& e) {
            json error = CreateErrorResponse(-32700, "Parse error: " + std::string(e.what()));
            std::cout << error.dump() << std::endl;
            std::cout.flush();
        }
        catch (const std::exception& e) {
            json error = CreateErrorResponse(-32603, "Internal error: " + std::string(e.what()));
            std::cout << error.dump() << std::endl;
            std::cout.flush();
        }
    }
}

// ============================================================================
// HTTP Transport
// ============================================================================

void Server::StartHTTP(int port) {
    if (m_running) {
        Warning("MCP Server already running\n");
        return;
    }

    m_port = port;
    SetupHTTPRoutes();

    m_serverThread = std::make_unique<std::thread>([this]() {
        Msg("Starting MCP Server on port %d (Streamable HTTP)...\n", m_port);
        m_running = true;

        if (!m_httpServer->listen("127.0.0.1", m_port)) {
            Warning("Failed to start MCP Server on port %d\n", m_port);
            m_running = false;
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (m_running) {
        Msg("MCP Server started successfully on http://127.0.0.1:%d\n", m_port);
    }
}

void Server::SetupHTTPRoutes() {
    m_httpServer->Post("/", [this](const httplib::Request& req, httplib::Response& res) {
        if (req.has_header("Origin")) {
            std::string origin = req.get_header_value("Origin");
            if (origin.find("localhost") == std::string::npos &&
                origin.find("127.0.0.1") == std::string::npos) {
                res.status = 403;
                res.set_content("{\"error\":\"Forbidden origin\"}", "application/json");
                return;
            }
        }

        std::string protocolVersion = "2025-03-26";
        if (req.has_header("MCP-Protocol-Version")) {
            protocolVersion = req.get_header_value("MCP-Protocol-Version");
            if (protocolVersion != "2025-06-18") {
                res.status = 400;
                res.set_content("{\"error\":\"Unsupported protocol version\"}", "application/json");
                return;
            }
        }

        try {
            json message = json::parse(req.body);
            bool isInitialize = message.contains("method") && message["method"] == "initialize";

            if (!isInitialize && !m_sessionId.empty()) {
                if (!req.has_header("Mcp-Session-Id")) {
                    res.status = 400;
                    res.set_content("{\"error\":\"Missing session ID\"}", "application/json");
                    return;
                }
                std::string sessionId = req.get_header_value("Mcp-Session-Id");
                if (sessionId != m_sessionId) {
                    res.status = 404;
                    res.set_content("{\"error\":\"Session not found\"}", "application/json");
                    return;
                }
            }

            json response = HandleMessage(message);

            bool isNotificationOrResponse = !message.contains("id") || (message.contains("result") || message.contains("error"));

            if (isNotificationOrResponse && response.empty()) {
                res.status = 202;
                return;
            }

            res.set_content(response.dump(), "application/json");

            if (message.contains("method") && message["method"] == "initialize" && !m_sessionId.empty()) {
                res.set_header("Mcp-Session-Id", m_sessionId);
            }
        }
        catch (const json::parse_error& e) {
            res.status = 400;
            json error = CreateErrorResponse(-32700, "Parse error");
            res.set_content(error.dump(), "application/json");
        }
        catch (const std::exception& e) {
            res.status = 500;
            json error = CreateErrorResponse(-32603, "Internal error");
            res.set_content(error.dump(), "application/json");
        }
    });

    m_httpServer->Get("/health", [](const httplib::Request&, httplib::Response& res) {
        json response = {{"status", "ok"}, {"service", "R1Delta MCP Server"}};
        res.set_content(response.dump(), "application/json");
    });
}

// ============================================================================
// Server Lifecycle
// ============================================================================

void Server::Stop() {
    if (!m_running) return;

    Msg("Stopping MCP Server...\n");
    m_running = false;
    m_initialized = false;

    if (m_httpServer) m_httpServer->stop();
    if (m_serverThread && m_serverThread->joinable()) m_serverThread->join();

    Msg("MCP Server stopped.\n");
}

bool Server::IsRunning() const {
    return m_running;
}

// ============================================================================
// Console Commands
// ============================================================================

static void MCPStartStdioCommand(const ::CCommand& args) {
    Server::GetInstance().StartStdio();
}

static void MCPStartHTTPCommand(const ::CCommand& args) {
    int port = 8765;
    if (args.ArgC() > 1) {
        port = atoi(args.Arg(1));
        if (port <= 0 || port > 65535) {
            Warning("Invalid port number. Using default port 8765.\n");
            port = 8765;
        }
    }
    Server::GetInstance().StartHTTP(port);
}

static void MCPStopCommand(const ::CCommand& args) {
    Server::GetInstance().Stop();
}

static void MCPStatusCommand(const ::CCommand& args) {
    if (Server::GetInstance().IsRunning()) {
        Msg("MCP Server is running\n");
    } else {
        Msg("MCP Server is not running\n");
    }
}

// ============================================================================
// Initialization
// ============================================================================

void InstallEchoCommandFix() {
    static std::once_flag once;
    std::call_once(once, []() {
        auto* existingEcho = CCVar_FindCommand(cvarinterface, "echo");
        if (existingEcho) {
            CCVar_UnregisterConCommand(cvarinterface, existingEcho);
        }
        RegisterConCommand("echo", EchoCommand, "Echo text to console", FCVAR_CLIENTDLL | FCVAR_SERVER_CAN_EXECUTE);
    });
}

void InitializeMCP() {
    Msg("Initializing MCP Server...\n");

    InstallEchoCommandFix();

    RegisterConCommand("mcp_start_stdio", MCPStartStdioCommand, "Start MCP server on stdio transport", FCVAR_NONE);
    RegisterConCommand("mcp_start_http", MCPStartHTTPCommand, "Start MCP server on HTTP transport (optional: port number)", FCVAR_NONE);
    RegisterConCommand("mcp_stop", MCPStopCommand, "Stop MCP server", FCVAR_NONE);
    RegisterConCommand("mcp_status", MCPStatusCommand, "Check MCP server status", FCVAR_NONE);
    RegisterConCommand("mcp_resolve_sqvm_frame_tasks", MCPResolveSQVMFrameTasksCommand, "Run queued MCP SQVM tasks on the engine thread", FCVAR_HIDDEN);

    Msg("MCP Server initialized. Use 'mcp_start_stdio' or 'mcp_start_http [port]' to start.\n");
}

} // namespace MCPServer
