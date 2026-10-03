// Drives an R1Delta overlay plugin (r1p_plugin.h) through its C ABI.
//
//   plugin_loopback <plugin.so> server <port>          prints "ADDR <addr>" then echoes
//   plugin_loopback <plugin.so> client <addr> <port>   sends datagrams, expects echoes
//
// Used on Linux and Windows to validate plugin datagram round trips.

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "../r1p_plugin.h"

namespace
{
struct Api
{
    decltype(&r1p_abi_version) abi_version;
    decltype(&r1p_init) init;
    decltype(&r1p_listen) listen;
    decltype(&r1p_connect) connect;
    decltype(&r1p_send) send;
    decltype(&r1p_recv) recv;
    decltype(&r1p_peer_status) peer_status;
    decltype(&r1p_max_datagram) max_datagram;
    decltype(&r1p_close_peer) close_peer;
    decltype(&r1p_poll_log) poll_log;
    decltype(&r1p_shutdown) shutdown;
};

template <typename T>
bool Load(void* lib, const char* name, T& out)
{
#ifdef _WIN32
    out = reinterpret_cast<T>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
    out = reinterpret_cast<T>(dlsym(lib, name));
#endif
    if (!out)
        std::fprintf(stderr, "missing export %s\n", name);
    return out != nullptr;
}

void DrainLog(const Api& api)
{
    char line[512];
    while (api.poll_log(line, sizeof(line)) == R1P_GOT)
        std::fprintf(stderr, "[plugin] %s\n", line);
}

using Clock = std::chrono::steady_clock;
} // namespace

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: %s <plugin> server <port> | client <addr> <port>\n", argv[0]);
        return 2;
    }
#ifdef _WIN32
    void* lib = LoadLibraryA(argv[1]);
#else
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
#endif
    if (!lib)
    {
#ifdef _WIN32
        std::fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError());
#else
        std::fprintf(stderr, "dlopen: %s\n", dlerror());
#endif
        return 2;
    }
    Api api{};
    bool ok = Load(lib, "r1p_abi_version", api.abi_version) && Load(lib, "r1p_init", api.init) &&
              Load(lib, "r1p_listen", api.listen) && Load(lib, "r1p_connect", api.connect) &&
              Load(lib, "r1p_send", api.send) && Load(lib, "r1p_recv", api.recv) &&
              Load(lib, "r1p_peer_status", api.peer_status) && Load(lib, "r1p_max_datagram", api.max_datagram) &&
              Load(lib, "r1p_close_peer", api.close_peer) && Load(lib, "r1p_poll_log", api.poll_log) &&
              Load(lib, "r1p_shutdown", api.shutdown);
    if (!ok || api.abi_version() != R1P_ABI_VERSION)
        return 2;

    const char* cfg = std::getenv("R1P_CONFIG");
    if (api.init(cfg ? cfg : "{}") < 0)
    {
        DrainLog(api);
        std::fprintf(stderr, "init failed\n");
        return 1;
    }

    const std::string mode = argv[2];
    std::vector<uint8_t> buf(65536);

    if (mode == "server")
    {
        const uint16_t port = static_cast<uint16_t>(std::atoi(argv[3]));
        char addr[4096];
        if (api.listen(port, addr, sizeof(addr)) < 0)
        {
            DrainLog(api);
            std::fprintf(stderr, "listen failed\n");
            return 1;
        }
        std::printf("ADDR %s\n", addr);
        std::fflush(stdout);
        const auto deadline = Clock::now() + std::chrono::seconds(90);
        int echoed = 0;
        auto lastPacket = Clock::now();
        while (Clock::now() < deadline)
        {
            // UDP-style transports (tailcat) never report a close; finish
            // once the client has gone quiet after being served.
            if (echoed > 0 && Clock::now() - lastPacket > std::chrono::seconds(5))
            {
                std::fprintf(stderr, "server: client quiet after %d echoes\n", echoed);
                DrainLog(api);
                api.shutdown();
                return 0;
            }
            uint64_t peer;
            uint32_t flags, len;
            const int32_t r = api.recv(&peer, &flags, buf.data(), static_cast<uint32_t>(buf.size()), &len);
            if (r == R1P_GOT)
            {
                if (flags & R1P_PEER_CLOSED)
                {
                    std::fprintf(stderr, "server: peer %llu closed after %d echoes\n", (unsigned long long)peer, echoed);
                    DrainLog(api);
                    api.shutdown();
                    return 0;
                }
                if (!(flags & R1P_PEER_INCOMING))
                    std::fprintf(stderr, "server: packet without INCOMING flag\n");
                lastPacket = Clock::now();
                if (api.send(peer, buf.data(), len) == R1P_OK)
                    ++echoed;
                continue;
            }
            DrainLog(api);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::fprintf(stderr, "server: timed out\n");
        return 1;
    }

    if (mode == "client" && argc >= 5)
    {
        const uint16_t port = static_cast<uint16_t>(std::atoi(argv[4]));
        uint64_t peer = 0;
        if (api.connect(argv[3], port, &peer) < 0)
        {
            DrainLog(api);
            std::fprintf(stderr, "connect failed\n");
            return 1;
        }
        char status[512];
        const auto connectDeadline = Clock::now() + std::chrono::seconds(30);
        for (;;)
        {
            DrainLog(api);
            if (api.peer_status(peer, status, sizeof(status)) == R1P_OK)
            {
                if (std::strstr(status, "\"connected\""))
                    break;
                if (std::strstr(status, "\"failed\"") || std::strstr(status, "\"closed\""))
                {
                    std::fprintf(stderr, "client: %s\n", status);
                    return 1;
                }
            }
            if (Clock::now() > connectDeadline)
            {
                std::fprintf(stderr, "client: connect timeout (%s)\n", status);
                return 1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        const int maxDgram = api.max_datagram(peer);
        std::fprintf(stderr, "client: connected, max datagram %d, %s\n", maxDgram, status);
        if (maxDgram < 1000)
            return 1;

        int received = 0;
        const int total = 50;
        double bestRtt = 1e9;
        for (int i = 0; i < total; ++i)
        {
            const uint32_t size = 16 + static_cast<uint32_t>((i * 97) % (maxDgram - 16));
            std::vector<uint8_t> msg(size);
            for (uint32_t j = 0; j < size; ++j)
                msg[j] = static_cast<uint8_t>(i + j);
            const auto t0 = Clock::now();
            if (api.send(peer, msg.data(), size) != R1P_OK)
            {
                std::fprintf(stderr, "client: send %d failed\n", i);
                continue;
            }
            const auto waitUntil = t0 + std::chrono::milliseconds(1000);
            while (Clock::now() < waitUntil)
            {
                uint64_t from;
                uint32_t flags, len;
                if (api.recv(&from, &flags, buf.data(), static_cast<uint32_t>(buf.size()), &len) == R1P_GOT)
                {
                    if (from == peer && len == size && std::memcmp(buf.data(), msg.data(), size) == 0)
                    {
                        ++received;
                        const double rtt = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
                        if (rtt < bestRtt)
                            bestRtt = rtt;
                        break;
                    }
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
        }
        // Oversized datagrams must be rejected, not truncated.
        std::vector<uint8_t> huge(static_cast<size_t>(maxDgram) + 100);
        const int32_t bigResult = api.send(peer, huge.data(), static_cast<uint32_t>(huge.size()));
        api.peer_status(peer, status, sizeof(status));
        std::fprintf(stderr, "client: %d/%d echoes, best rtt %.2fms, oversize send -> %d, %s\n", received, total, bestRtt,
                     bigResult, status);
        api.close_peer(peer);
        DrainLog(api);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        api.shutdown();
        return (received >= total * 9 / 10 && bigResult == R1P_ERR_TOOBIG) ? 0 : 1;
    }
    return 2;
}
