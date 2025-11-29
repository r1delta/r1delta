// %*++***###*##**##++**+++*++*%%%%%%%+*%+#*+%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#=%%%#**#+#%
// ==----------------------------------------------------------------------=================+
// =------------------------------------::----------------------------------===---==========+
// ---------------------------------:-:--::::-::::-------------------=======================+
// =-------------------------------::::::::-::::-:::----------==============+===+++=========+
// ----------------------------::::::--:---=====----------===========++==++++++++++++++++++++
// ----------------------------:-----:---==++++++====-==========++++++++++++++++++++++++++++*
// -------------------------------------=+++++++=============++++++++++++++++++++++++++++++**
// -------------------------------------=++++*+========++++++++++++++++++++++++++++++++++++**
// ----------------------------:::::::--=+++++=======+++++++++++++++++++++++************++++*
// ---------------------::::::::::::::::-==+++===++++++++++++++++++++++++********###%%%##*++*
// -------:::::::::::::::::::::::::::::::-=====+####**+++++++++++++++++*********#%%%@@@@%%#**
// ------:-:::::::::::::::::::::::::::::::-====*%%%%#*++++++++++++++++++********##%@@@@@%%#**
// ----------::::::::::::::::::::::::::-=--====+#%%%*++++++++++++++++++++*********##%%%%%#***
// -------------=*=-:::::::::::::::::-=++======++***+++++++++++++++++++**************###*****
// -------------=*#=-------======++++*###*+=+=++=++++++++++*+++******************************
// =-----=======+*#*+++++++*****##########+=++++++++++***************************************
// +++++++++++****#################*****#*+=+++++++++****************************************
// ++**+++++++++++++======+++++++++++++****+=+++***################**************************
// *****+=--------::-::::::::::::::::::------=*#%%%%%%%%%%%%%%%%%%%#####*********************
// ******=-----------:::::::::::---:::::::::-=#%%%%@@@@@@@@@@@@@@%%%%###********************#
// ******=---------------:::::::::::-:::::::-*%%%@@@@@@@@@@@@@@@@@%%%%##********************#
// ****#*=-----------------:::::::::::::::::-=*%%@@@@@@@@@@@@@@@@@@%%##*********************#
// ******+===-------------::::::::::::---:::--=*#%%%@@@@@@@@@@@@@%%######**#**************###
// ==++==------------------:::::::::::::-------=+**##%%%@%%%%%%%%##########*****************#
// ==--------------------------::-:::::::::::---=++**##%%%%%%%%%%%##########*************####
// =--------------------------------:---::::--:--==+**###%#%%%%%%%%%%%#####**************####
// ====--------------------------:-------::-------==+++****###########******************#####
// ===--==------------------------------------::---==+++++******************************#####
// ===-------------------------------------:::-:----=+++********************************####%
// =====---------------------------------------------=++++******************************####%
// ======------------------==------------------------==+++***************************######%%
// =========-----===--------==------------------------==++********#*#####**#######*########%%
//
// Auto-Registering Hooks and Patches
//
// This system allows defining hooks inline without manually adding them to
// LoaderNotificationCallback. Hooks and patches register themselves at static
// init time and get applied when their target module loads.
//
// Usage:
//   HOOK("server.dll", 0x23589, void*, WhateverFunc, (void* thisptr)) {
//       return oWhateverFunc(thisptr);
//   }
//
//   PATCH("engine.dll", 0x1234, 0x90, 0x90, 0x90);  // NOP patch
//

#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include "MinHook.h"

//=============================================================================
// Hook Registry
//=============================================================================

enum class HookType {
    Always,         // Always apply
    DediOnly,       // Only on dedicated server
    ListenOnly,     // Only on listen server (client)
};

struct HookInfo {
    const char* module;
    uintptr_t offset;
    uintptr_t offsetAlt;    // Alternative offset (for dedi vs listen)
    void* hookFunc;
    void** originalFunc;
    HookType type;
};

struct PatchInfo {
    const char* module;
    uintptr_t offset;
    const uint8_t* bytes;
    size_t size;
    HookType type;
};

class HookRegistry {
public:
    static std::vector<HookInfo>& GetHooks() {
        static std::vector<HookInfo> hooks;
        return hooks;
    }

    static std::vector<PatchInfo>& GetPatches() {
        static std::vector<PatchInfo> patches;
        return patches;
    }

    static void RegisterHook(const char* module, uintptr_t offset, void* hook, void** original, HookType type = HookType::Always, uintptr_t offsetAlt = 0) {
        GetHooks().push_back({ module, offset, offsetAlt, hook, original, type });
    }

    static void RegisterPatch(const char* module, uintptr_t offset, const uint8_t* bytes, size_t size, HookType type = HookType::Always) {
        GetPatches().push_back({ module, offset, bytes, size, type });
    }

    // Call this when a module loads
    static void ApplyHooksForModule(const char* moduleName, uintptr_t base, bool isDedicatedServer) {
        for (auto& h : GetHooks()) {
            if (_stricmp(h.module, moduleName) != 0) continue;

            // Check if this hook applies to current server type
            if (h.type == HookType::DediOnly && !isDedicatedServer) continue;
            if (h.type == HookType::ListenOnly && isDedicatedServer) continue;

            // Use alternative offset if specified and we're on dedicated
            uintptr_t offset = h.offset;
            if (h.offsetAlt != 0 && isDedicatedServer) {
                offset = h.offsetAlt;
            }

            MH_CreateHook((void*)(base + offset), h.hookFunc, h.originalFunc);
        }
    }

    static void ApplyPatchesForModule(const char* moduleName, uintptr_t base, bool isDedicatedServer) {
        for (auto& p : GetPatches()) {
            if (_stricmp(p.module, moduleName) != 0) continue;

            // Check if this patch applies to current server type
            if (p.type == HookType::DediOnly && !isDedicatedServer) continue;
            if (p.type == HookType::ListenOnly && isDedicatedServer) continue;

            // Apply the patch
            DWORD oldProtect;
            void* addr = (void*)(base + p.offset);
            VirtualProtect(addr, p.size, PAGE_EXECUTE_READWRITE, &oldProtect);
            memcpy(addr, p.bytes, p.size);
            VirtualProtect(addr, p.size, oldProtect, &oldProtect);
        }
    }
};

//=============================================================================
// Hook Registration Helper
//=============================================================================

struct HookRegistrar {
    HookRegistrar(const char* module, uintptr_t offset, void* hook, void** original, HookType type = HookType::Always) {
        HookRegistry::RegisterHook(module, offset, hook, original, type);
    }
    HookRegistrar(const char* module, uintptr_t offset, uintptr_t offsetAlt, void* hook, void** original) {
        HookRegistry::RegisterHook(module, offset, hook, original, HookType::Always, offsetAlt);
    }
};

//=============================================================================
// Patch Registration Helper
//=============================================================================

template<size_t N>
struct PatchRegistrar {
    uint8_t bytes[N];

    template<typename... Args>
    PatchRegistrar(const char* module, uintptr_t offset, HookType type, Args... args) : bytes{ static_cast<uint8_t>(args)... } {
        static_assert(sizeof...(args) == N, "Patch byte count mismatch");
        HookRegistry::RegisterPatch(module, offset, bytes, N, type);
    }

    template<typename... Args>
    PatchRegistrar(const char* module, uintptr_t offset, Args... args) : bytes{ static_cast<uint8_t>(args)... } {
        static_assert(sizeof...(args) == N, "Patch byte count mismatch");
        HookRegistry::RegisterPatch(module, offset, bytes, N, HookType::Always);
    }
};

// Deduction guide
template<typename... Args>
PatchRegistrar(const char*, uintptr_t, Args...) -> PatchRegistrar<sizeof...(Args)>;

template<typename... Args>
PatchRegistrar(const char*, uintptr_t, HookType, Args...) -> PatchRegistrar<sizeof...(Args) - 1>;

//=============================================================================
// Hook Macros
//=============================================================================

// Basic hook - applies to both dedicated and listen servers
// Usage: HOOK("server.dll", 0x1234, void, MyFunc, (int a, int b)) { ... }
#define HOOK(module, offset, ret, name, args) \
    typedef ret (*name##_t) args; \
    inline name##_t o##name = nullptr; \
    ret name##_Hook args; \
    static HookRegistrar _hookreg_##name(module, offset, (void*)&name##_Hook, (void**)&o##name); \
    ret name##_Hook args

// Hook only for dedicated server
#define HOOK_DS(module, offset, ret, name, args) \
    typedef ret (*name##_t) args; \
    inline name##_t o##name = nullptr; \
    ret name##_Hook args; \
    static HookRegistrar _hookreg_##name(module, offset, (void*)&name##_Hook, (void**)&o##name, HookType::DediOnly); \
    ret name##_Hook args

// Hook only for listen server (client)
#define HOOK_LISTEN(module, offset, ret, name, args) \
    typedef ret (*name##_t) args; \
    inline name##_t o##name = nullptr; \
    ret name##_Hook args; \
    static HookRegistrar _hookreg_##name(module, offset, (void*)&name##_Hook, (void**)&o##name, HookType::ListenOnly); \
    ret name##_Hook args

// Hook with different offsets for dedicated vs listen
// Usage: HOOK_EITHER("engine.dll", 0x1234, 0x5678, void, MyFunc, (int a)) { ... }
//        First offset is listen, second is dedicated
#define HOOK_EITHER(module, offsetListen, offsetDedi, ret, name, args) \
    typedef ret (*name##_t) args; \
    inline name##_t o##name = nullptr; \
    ret name##_Hook args; \
    static HookRegistrar _hookreg_##name(module, offsetListen, offsetDedi, (void*)&name##_Hook, (void**)&o##name); \
    ret name##_Hook args

//=============================================================================
// Patch Macros
//=============================================================================

// Basic patch - applies to both
// Usage: PATCH("engine.dll", 0x1234, 0x90, 0x90, 0x90);
#define PATCH(module, offset, ...) \
    static PatchRegistrar _patchreg_##__COUNTER__(module, offset, __VA_ARGS__)

// Patch only for dedicated server
#define PATCH_DS(module, offset, ...) \
    static PatchRegistrar _patchreg_##__COUNTER__(module, offset, HookType::DediOnly, __VA_ARGS__)

// Patch only for listen server
#define PATCH_LISTEN(module, offset, ...) \
    static PatchRegistrar _patchreg_##__COUNTER__(module, offset, HookType::ListenOnly, __VA_ARGS__)

//=============================================================================
// Module name constants for convenience
//=============================================================================

namespace Modules {
    constexpr const char* Server = "server.dll";
    constexpr const char* Engine = "engine.dll";
    constexpr const char* EngineDS = "engine_ds.dll";
    constexpr const char* Client = "client.dll";
    constexpr const char* VScript = "vscript.dll";
    constexpr const char* VPhysics = "vphysics.dll";
    constexpr const char* FileSystemStdio = "filesystem_stdio.dll";
    constexpr const char* MaterialSystem = "materialsystem_dx11.dll";
    constexpr const char* StudioRender = "studiorender.dll";
    constexpr const char* Localize = "localize.dll";
}
