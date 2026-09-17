#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include "logging.h"
#pragma comment(lib, "bcrypt.lib")

namespace r1audio {
// Installation-only verification: matching loaded PE identity plus SHA256 of
// its actual module path. No asset paths or private symbols leave the process.
inline bool VerifyModule(std::uintptr_t base, DWORD timestamp, DWORD imageSize,
    const std::array<unsigned char, 32>& expected, const char* label) {
    if (!base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt->FileHeader.TimeDateStamp != timestamp ||
        nt->OptionalHeader.SizeOfImage != imageSize) {
        Warning("R1Delta audio: rejecting %s PE identity\n", label);
        return false;
    }
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(reinterpret_cast<HMODULE>(base), path, MAX_PATH);
    if (!length || length == MAX_PATH) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool valid = false;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0) {
        DWORD objectSize = 0, returned = 0;
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &returned, 0) >= 0) {
            std::vector<unsigned char> object(objectSize);
            if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) >= 0) {
                std::array<unsigned char, 65536> bytes;
                std::array<unsigned char, 32> digest;
                DWORD got = 0;
                bool okay = true;
                for (;;) {
                    if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr)) { okay = false; break; }
                    if (!got) break;
                    if (BCryptHashData(hash, bytes.data(), got, 0) < 0) { okay = false; break; }
                }
                valid = okay && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0 && digest == expected;
                BCryptDestroyHash(hash);
            }
        }
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    CloseHandle(file);
    if (!valid) Warning("R1Delta audio: rejecting %s SHA256 at %ls\n", label, path);
    return valid;
}
}
