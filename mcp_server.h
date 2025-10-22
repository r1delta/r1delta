#pragma once

#include <string>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <memory>
#include <thread>
#include <map>
#include <deque>
#include <future>
#include <chrono>
#include <nlohmann/json.hpp>

// Forward declarations
namespace httplib {
    class Server;
    struct Request;
    struct Response;
}

class CCommand;

namespace MCPServer {

using json = nlohmann::json;

// ============================================================================
// VM Task Queue System
// ============================================================================

// Task to be executed on the engine thread
struct VMTask {
    std::function<json()> fn;
    std::promise<json>    promise;
};

// Global VM task queue variables
extern std::mutex                                  g_vmTaskMutex;
extern std::deque<std::shared_ptr<VMTask>>         g_vmTasks;
extern std::thread::id                             g_engineThreadId;

// Runs on the ENGINE thread when the console command executes
void MCPResolveSQVMFrameTasksCommand(const ::CCommand& args);

// Call from any worker thread. Runs fn on the engine thread using the command.
json RunOnVMThreadAndWait(std::function<json()> fn,
                          std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));

// Console output capture
struct ConsoleOutputCapture {
    enum class CaptureState {
        Idle,
        WaitingForStart,
        Capturing,
        Completed
    };

    std::string guid;
    std::vector<std::string> lines;
    std::deque<std::string> history;
    size_t historyLimit;
    std::mutex mutex;
    std::condition_variable cv;
    bool capturing;
    bool truncated;
    CaptureState captureState;

    ConsoleOutputCapture();
    void StartCapture(const std::string& guid);
    void StopCapture();
    void AddLine(const std::string& line);
    std::vector<std::string> GetAndClear();
    std::vector<std::string> GetLastLines(size_t count);
    bool WaitForCaptureComplete(const std::string& guid, std::chrono::milliseconds timeout);
};

// JSON-RPC 2.0 message types
enum class MessageType {
    Request,
    Response,
    Notification,
    Error
};

// MCP Protocol Version
constexpr const char* MCP_PROTOCOL_VERSION = "2025-06-18";

// Tool definition for MCP
struct Tool {
    std::string name;
    std::string title;
    std::string description;
    json inputSchema;
};

// MCP Server interface (JSON-RPC 2.0 compliant)
class Server {
public:
    static Server& GetInstance();

    // Transport methods
    void StartStdio();
    void StartHTTP(int port = 8765);
    void Stop();
    bool IsRunning() const;

    // Console output capture
    void CaptureConsoleOutput(const char* msg);
    ConsoleOutputCapture& GetOutputCapture() { return m_outputCapture; }

private:
    Server();
    ~Server();

    // JSON-RPC message handling
    json HandleMessage(const json& message);
    json HandleRequest(const json& request);
    json HandleNotification(const json& notification);

    // MCP protocol methods
    json HandleInitialize(const json& params);
    json HandleInitialized(const json& params);
    json HandleToolsList(const json& params);
    json HandleToolsCall(const json& params);

    // Tool implementations
    json ExecuteSquirrelScript(const json& args);
    json ExecuteConsoleCommand(const json& args);
    json GetGameState(const json& args);
    json SearchScriptFunctions(const json& args);
    json GetConsoleLog(const json& args);

    // HTTP transport setup
    void SetupHTTPRoutes();

    // stdio transport
    void StdioLoop();

    // Utility
    std::vector<Tool> GetAvailableTools() const;
    json CreateErrorResponse(int code, const std::string& message, const json& id = nullptr);
    json CreateSuccessResponse(const json& result, const json& id);

    bool m_running;
    bool m_initialized;
    int m_port;
    std::string m_sessionId;
    ConsoleOutputCapture m_outputCapture;
    std::unique_ptr<httplib::Server> m_httpServer;
    std::unique_ptr<std::thread> m_serverThread;
    std::mutex m_mutex;
};

// Initialize MCP server hooks and commands
void InitializeMCP();

// Echo command fix
void EchoCommand(const ::CCommand& args);

} // namespace MCPServer
