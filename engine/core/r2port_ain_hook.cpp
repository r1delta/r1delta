#include "engine/core/r2port_ain_hook.h"

#include "engine/core/core.h"
#include "engine/filesystem/r2_map_provider.h"
#include "engine/logging/logging.h"

#include <cstdio>
#include <cstring>

namespace {

// Retail R1 server.dll: int ValidateAinHeader(CAI_NetworkManager* manager, stream, ...) reads the graph header
// (version, map version, CRC) and returns 0 valid, 1 version < 46, 2 version < 52, 3 map version mismatch and
// 4 CRC mismatch (only checked when developer > 0).
constexpr std::uintptr_t kValidateAinHeaderRva = 0x364B80;
constexpr unsigned char kExpected[] = {
    0x48, 0x89, 0x5C, 0x24, 0x20, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC,
    0x20, 0x33, 0xF6, 0x48, 0x8B, 0xDA, 0x48, 0x8B };
constexpr int kCrcMismatch = 4;

using ValidateAinHeaderFn = std::int64_t(__fastcall*)(void* manager, void* stream, void* arg3);
ValidateAinHeaderFn g_original = nullptr;

std::int64_t __fastcall ValidateAinHeaderHook(void* manager, void* stream, void* arg3)
{
    const std::int64_t result = g_original(manager, stream, arg3);
    if (static_cast<int>(result) == kCrcMismatch && r2port::ConsumeServedAinPackage()) {
        Msg("R1Delta: R2 AI graph package accepted despite the developer-mode entity CRC mismatch\n");
        return 0;
    }
    return result;
}

} // namespace

void InstallServerAinCrcGuard(std::uintptr_t serverBase)
{
    if (!serverBase || g_original)
        return;
    void* target = reinterpret_cast<void*>(serverBase + kValidateAinHeaderRva);
    if (std::memcmp(target, kExpected, sizeof(kExpected)) != 0)
        return;

    const MH_STATUS status = MH_CreateHook(target, &ValidateAinHeaderHook, reinterpret_cast<LPVOID*>(&g_original));
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
        Warning("R1Delta: failed to hook the AIN header validation (%d)\n", static_cast<int>(status));
        g_original = nullptr;
        return;
    }
    const MH_STATUS enableStatus = MH_EnableHook(target);
    if (enableStatus != MH_OK)
        Warning("R1Delta: failed to enable the AIN header validation hook (%d)\n", static_cast<int>(enableStatus));
}
