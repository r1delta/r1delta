#include "p2p_plugin.h"

#include <nlohmann/json.hpp>

#include <iterator>
#include <vector>

#include "p2p_log.h"

namespace p2p
{
namespace
{

template <typename T>
bool Resolve(HMODULE module, const char* name, T& out)
{
    out = reinterpret_cast<T>(GetProcAddress(module, name));
    return out != nullptr;
}

} // namespace

std::wstring ModuleDirectory()
{
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ModuleDirectory), &self);
    wchar_t path[MAX_PATH * 2]{};
    const DWORD len = GetModuleFileNameW(self, path, static_cast<DWORD>(std::size(path)));
    std::wstring dir(path, len);
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return L"";
    return dir.substr(0, slash + 1);
}

PluginBackend::PluginBackend(Backend kind, const wchar_t* dllName)
    : m_kind(kind), m_dllName(dllName)
{
}

bool PluginBackend::IsAvailable()
{
    if (m_module)
        return true;
    const std::wstring path = ModuleDirectory() + m_dllName;
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool PluginBackend::Load()
{
    const std::wstring path = ModuleDirectory() + m_dllName;
    m_module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m_module)
    {
        Log("%s plugin not loaded (%lu); transport disabled\n", BackendName(m_kind), GetLastError());
        return false;
    }
    // The plugin's own sockets (DNS, QUIC, WireGuard) must never be fed
    // engine datagrams by the recvfrom hook.
    RegisterForeignModule(m_module);

    const bool ok = Resolve(m_module, "r1p_abi_version", m_abiVersion) && Resolve(m_module, "r1p_init", m_init) &&
                    Resolve(m_module, "r1p_listen", m_listen) && Resolve(m_module, "r1p_connect", m_connect) &&
                    Resolve(m_module, "r1p_send", m_send) && Resolve(m_module, "r1p_recv", m_recv) &&
                    Resolve(m_module, "r1p_peer_status", m_peerStatus) &&
                    Resolve(m_module, "r1p_max_datagram", m_maxDatagram) &&
                    Resolve(m_module, "r1p_close_peer", m_closePeer) && Resolve(m_module, "r1p_poll_log", m_pollLog) &&
                    Resolve(m_module, "r1p_shutdown", m_shutdown);
    if (!ok)
    {
        Log("%s plugin is missing exports; transport disabled\n", BackendName(m_kind));
        return false;
    }
    if (m_abiVersion() != R1P_ABI_VERSION)
    {
        Log("%s plugin ABI %d != %d; transport disabled\n", BackendName(m_kind), m_abiVersion(), R1P_ABI_VERSION);
        return false;
    }
    return true;
}

bool PluginBackend::EnsureReady(const std::string& configJson)
{
    if (m_ready.load())
        return true;
    std::lock_guard lock(m_initMutex);
    if (m_ready.load())
        return true;
    if (m_loadAttempted)
        return false;
    m_loadAttempted = true;
    if (!Load())
        return false;
    const int32_t r = m_init(configJson.c_str());
    PumpLogs();
    if (r < 0)
    {
        Log("%s plugin init failed (%d)\n", BackendName(m_kind), r);
        return false;
    }
    RegisterBackend(this);
    m_ready.store(true);
    return true;
}

bool PluginBackend::Listen(uint16_t port, std::string& outAddress)
{
    if (!m_ready.load())
        return false;
    std::vector<char> buf(8192);
    const int32_t r = m_listen(port, buf.data(), static_cast<uint32_t>(buf.size()));
    PumpLogs();
    if (r < 0)
    {
        Log("%s listen failed (%d)\n", BackendName(m_kind), r);
        return false;
    }
    outAddress = buf.data();
    return !outAddress.empty();
}

bool PluginBackend::Connect(const std::string& remote, uint16_t port, uint64_t& outHandle)
{
    if (!m_ready.load())
        return false;
    uint64_t handle = 0;
    const int32_t r = m_connect(remote.c_str(), port, &handle);
    if (r < 0)
    {
        Log("%s connect failed (%d)\n", BackendName(m_kind), r);
        return false;
    }
    outHandle = handle;
    return true;
}

PluginBackend::Status PluginBackend::PeerStatus(uint64_t handle)
{
    Status status;
    if (!m_ready.load())
        return status;
    char buf[1024]{};
    if (m_peerStatus(handle, buf, sizeof(buf)) < 0)
        return status;
    try
    {
        const auto j = nlohmann::json::parse(buf);
        status.state = j.value("state", "unknown");
        status.path = j.value("path", "unknown");
        status.rttMs = j.value("rtt_ms", -1.0);
        status.error = j.value("error", "");
    }
    catch (...)
    {
    }
    return status;
}

void PluginBackend::PumpLogs()
{
    if (!m_pollLog)
        return;
    char line[1024];
    for (int i = 0; i < 64 && m_pollLog(line, sizeof(line)) == R1P_GOT; ++i)
        Log("%s\n", line);
}

bool PluginBackend::Send(uint64_t handle, const uint8_t* data, size_t size)
{
    if (!m_ready.load())
        return false;
    return m_send(handle, data, static_cast<uint32_t>(size)) == R1P_OK;
}

size_t PluginBackend::MaxDatagram(uint64_t handle)
{
    if (!m_ready.load())
        return 0;
    const int32_t r = m_maxDatagram(handle);
    return r > 0 ? static_cast<size_t>(r) : 0;
}

void PluginBackend::Pump(const BackendSink& sink)
{
    if (!m_ready.load())
        return;
    thread_local std::vector<uint8_t> buf(65536);
    for (int i = 0; i < 1024; ++i)
    {
        uint64_t peer = 0;
        uint32_t flags = 0;
        uint32_t len = 0;
        const int32_t r = m_recv(&peer, &flags, buf.data(), static_cast<uint32_t>(buf.size()), &len);
        if (r == R1P_ERR_BUFFER && len > buf.size())
        {
            buf.resize(len);
            continue;
        }
        if (r != R1P_GOT)
            break;
        uint32_t muxFlags = 0;
        if (flags & R1P_PEER_INCOMING)
            muxFlags |= kPeerIncoming;
        if (flags & R1P_PEER_CLOSED)
            muxFlags |= kPeerClosed;
        sink(peer, muxFlags, buf.data(), len);
    }
}

void PluginBackend::Close(uint64_t handle)
{
    if (m_ready.load())
        m_closePeer(handle);
}

PluginBackend& IrohPlugin()
{
    static PluginBackend plugin(Backend::Iroh, L"r1delta_iroh.dll");
    return plugin;
}

PluginBackend& TailcatPlugin()
{
    static PluginBackend plugin(Backend::Tailcat, L"r1delta_tailcat.dll");
    return plugin;
}

} // namespace p2p
