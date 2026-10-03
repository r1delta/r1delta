#pragma once

// Loads an overlay transport plugin DLL (p2p/plugins/r1p_plugin.h) and exposes
// it to the mux as an OverlayBackend.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "p2p_mux.h"
#include "plugins/r1p_plugin.h"

namespace p2p
{

class PluginBackend final : public OverlayBackend
{
public:
    PluginBackend(Backend kind, const wchar_t* dllName);

    // Loads + initialises the DLL once. Safe to call repeatedly/concurrently.
    bool EnsureReady(const std::string& configJson);
    bool IsReady() const { return m_ready.load(); }
    bool IsAvailable(); // DLL present next to tier0.dll

    bool Listen(uint16_t port, std::string& outAddress);
    bool Connect(const std::string& remote, uint16_t port, uint64_t& outHandle);

    struct Status
    {
        std::string state = "unknown"; // connecting|connected|failed|closed|unknown
        std::string path = "unknown";  // direct|relay|unknown
        double rttMs = -1;
        std::string error;
    };
    Status PeerStatus(uint64_t handle);

    // Forwards plugin log lines to the console.
    void PumpLogs();

    // OverlayBackend
    Backend Kind() const override { return m_kind; }
    bool UsesFraming() const override { return true; }
    bool Send(uint64_t handle, const uint8_t* data, size_t size) override;
    size_t MaxDatagram(uint64_t handle) override;
    void Pump(const BackendSink& sink) override;
    void Close(uint64_t handle) override;

private:
    bool Load();

    Backend m_kind;
    std::wstring m_dllName;
    std::mutex m_initMutex;
    std::atomic<bool> m_ready{ false };
    bool m_loadAttempted = false;
    HMODULE m_module = nullptr;

    decltype(&r1p_abi_version) m_abiVersion = nullptr;
    decltype(&r1p_init) m_init = nullptr;
    decltype(&r1p_listen) m_listen = nullptr;
    decltype(&r1p_connect) m_connect = nullptr;
    decltype(&r1p_send) m_send = nullptr;
    decltype(&r1p_recv) m_recv = nullptr;
    decltype(&r1p_peer_status) m_peerStatus = nullptr;
    decltype(&r1p_max_datagram) m_maxDatagram = nullptr;
    decltype(&r1p_close_peer) m_closePeer = nullptr;
    decltype(&r1p_poll_log) m_pollLog = nullptr;
    decltype(&r1p_shutdown) m_shutdown = nullptr;
};

PluginBackend& IrohPlugin();
PluginBackend& TailcatPlugin();

// Directory containing tier0.dll (bin_delta), with a trailing backslash.
std::wstring ModuleDirectory();

} // namespace p2p
