#pragma once

// Just enough Win32/Winsock on POSIX to run the Windows p2p sources in the
// Linux end-to-end harness (fake_engine.cpp). Never used by the game build.

#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define WSAAPI
#define closesocket close
#define WSAEWOULDBLOCK EWOULDBLOCK
#define WSAEHOSTUNREACH EHOSTUNREACH
#define WSAENOTCONN ENOTCONN
typedef unsigned long DWORD;
typedef const wchar_t* LPCWSTR;
typedef void* FARPROC;
#define MAX_PATH 260
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define LOAD_WITH_ALTERED_SEARCH_PATH 0x8
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x4
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x2
#define CP_UTF8 65001
#define IMAGE_DOS_SIGNATURE 0x5A4D

inline int WSAGetLastError() { return errno; }
inline void WSASetLastError(int e) { errno = e; }
inline DWORD GetLastError() { return static_cast<DWORD>(errno); }
inline void Sleep(unsigned ms) { usleep(ms * 1000); }

inline int compat_select(int, fd_set* r, fd_set* w, fd_set* e, timeval* t) { return ::select(FD_SETSIZE, r, w, e, t); }
#define select compat_select

// Winsock takes int* for address lengths.
inline ssize_t recvfrom(int s, void* b, size_t l, int f, sockaddr* a, int* al)
{
    socklen_t len = al ? static_cast<socklen_t>(*al) : 0;
    const ssize_t r = ::recvfrom(s, b, l, f, a, al ? &len : nullptr);
    if (al)
        *al = static_cast<int>(len);
    return r;
}
inline int getsockname(int s, sockaddr* a, int* al)
{
    socklen_t len = static_cast<socklen_t>(*al);
    const int r = ::getsockname(s, a, &len);
    *al = static_cast<int>(len);
    return r;
}

// Fake modules: a dlopen handle behind something that does not look like a
// PE image (so RegisterForeignModule ignores it).
struct IMAGE_DOS_HEADER
{
    uint16_t e_magic;
    int32_t e_lfanew;
};
struct IMAGE_OPTIONAL_HEADER
{
    uint32_t SizeOfImage;
};
struct IMAGE_NT_HEADERS
{
    IMAGE_OPTIONAL_HEADER OptionalHeader;
};
struct CompatModule
{
    IMAGE_DOS_HEADER dos{ 0, 0 };
    void* dl = nullptr;
};
typedef CompatModule* HMODULE;

inline std::string CompatNarrow(const wchar_t* w)
{
    std::string s;
    for (; w && *w; ++w)
        s.push_back(*w == L'\\' ? '/' : static_cast<char>(*w));
    return s;
}

inline HMODULE LoadLibraryExW(const wchar_t* path, void*, DWORD)
{
    std::string p = CompatNarrow(path);
    // r1delta_x.dll -> libr1delta_x.so when present (cargo naming)
    void* dl = dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!dl)
    {
        const size_t slash = p.find_last_of('/');
        std::string alt = p.substr(0, slash + 1) + "lib" + p.substr(slash + 1);
        alt.replace(alt.size() - 4, 4, ".so");
        dl = dlopen(alt.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!dl)
        {
            alt = p;
            alt.replace(alt.size() - 4, 4, ".so");
            dl = dlopen(alt.c_str(), RTLD_NOW | RTLD_LOCAL);
        }
    }
    if (!dl)
        return nullptr;
    auto* m = new CompatModule;
    m->dl = dl;
    return m;
}
inline void* GetProcAddress(HMODULE m, const char* name) { return m ? dlsym(m->dl, name) : nullptr; }
inline int GetModuleHandleExW(DWORD, LPCWSTR, HMODULE* out)
{
    *out = nullptr;
    return 1;
}
inline DWORD GetModuleFileNameW(HMODULE, wchar_t* buf, DWORD n)
{
    const char* dir = std::getenv("R1P_PLUGIN_DIR");
    std::string p = std::string(dir ? dir : ".") + "/tier0.dll";
    DWORD i = 0;
    for (; i < p.size() && i + 1 < n; ++i)
        buf[i] = static_cast<wchar_t>(p[i]);
    buf[i] = 0;
    return i;
}
inline DWORD GetFileAttributesW(const wchar_t* path)
{
    std::string p = CompatNarrow(path);
    struct stat st;
    if (stat(p.c_str(), &st) == 0)
        return 0;
    const size_t slash = p.find_last_of('/');
    std::string alt = p.substr(0, slash + 1) + "lib" + p.substr(slash + 1);
    alt.replace(alt.size() - 4, 4, ".so");
    if (stat(alt.c_str(), &st) == 0)
        return 0;
    alt = p;
    alt.replace(alt.size() - 4, 4, ".so");
    return stat(alt.c_str(), &st) == 0 ? 0 : INVALID_FILE_ATTRIBUTES;
}
inline DWORD GetEnvironmentVariableW(const wchar_t*, wchar_t*, DWORD) { return 0; }
inline int WideCharToMultiByte(unsigned, DWORD, const wchar_t* w, int n, char* out, int cap, void*, void*)
{
    if (!out)
        return n;
    for (int i = 0; i < n && i < cap; ++i)
        out[i] = w[i] == L'\\' ? '/' : static_cast<char>(w[i]);
    return n;
}
