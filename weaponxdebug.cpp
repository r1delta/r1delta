#include "filesystem.h"
#include <iostream>
#include <unordered_set>
#include <string>
#include <filesystem>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <Windows.h>
#include <intrin.h>
#include "logging.h"
#include "load.h"

__int64 (*oWeaponXRegisterClient)(__int64 a1, const char** a2);
__int64 (*oWeaponXRegisterServer)(__int64 a1, const char** a2);

__int64 __fastcall WeaponXRegisterClient(__int64 a1, const char** a2) {
	if (a2 && *a2 && strlen(*a2) > 0)
		std::cout << "CLIENT: " << *a2 << std::endl;
	return oWeaponXRegisterClient(a1, a2);
}
__int64 __fastcall WeaponXRegisterServer(__int64 a1, const char** a2) {
	if (a2 && *a2 && strlen(*a2) > 0)
		std::cout << "SERVER: " << *a2 << std::endl;
	return oWeaponXRegisterServer(a1, a2);
}
void* (*mp_weapon_wingman_ctor_orig)();
void* (*mp_weapon_wingman_dtor_orig)(void* entity, char a2);

struct BreakpointInfo {
    void* address;
    void* entity;
    int debugRegister;  // 0-3 for DR0-DR3
};

std::mutex g_breakpointMutex;
std::vector<BreakpointInfo> g_activeBreakpoints;

// Helper to find available debug register
int FindFreeDebugRegister(const CONTEXT& ctx) {
    for (int i = 0; i < 4; i++) {
        // Check if this debug register is enabled in DR7
        if (!(ctx.Dr7 & (1 << (i * 2)))) {
            return i;
        }
    }
    return -1;
}

enum class BreakpointType {
    Execute = 0,
    Write = 1,
    IO = 2,        // Usually reserved/unused
    ReadWrite = 3
};

enum class BreakpointSize {
    Byte = 0,      // 1 byte
    Word = 1,      // 2 bytes
    Qword = 2,     // 8 bytes
    Dword = 3      // 4 bytes
};

bool SetHWBreakpoint(
    void* address,
    int& assignedRegister,
    BreakpointType type = BreakpointType::Write,
    BreakpointSize size = BreakpointSize::Dword,
    bool isLocal = true
) {
    if (!address) return false;

    // Verify address alignment based on breakpoint size
    DWORD64 addr = (DWORD64)address;
    switch (size) {
    case BreakpointSize::Word:
        if (addr & 0x1) return false;  // Must be 2-byte aligned
        break;
    case BreakpointSize::Dword:
        if (addr & 0x3) return false;  // Must be 4-byte aligned
        break;
    case BreakpointSize::Qword:
        if (addr & 0x7) return false;  // Must be 8-byte aligned
        break;
    }

    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

    if (!GetThreadContext(GetCurrentThread(), &ctx)) {
        return false;
    }

    // Find a free debug register (DR0-DR3)
    int reg = -1;
    for (int i = 0; i < 4; i++) {
        // Check if this register is enabled in DR7 (both local and global bits)
        if ((ctx.Dr7 & (3ULL << (i * 2))) == 0) {
            reg = i;
            break;
        }
    }

    if (reg == -1) {
        return false;  // No free debug registers
    }

    // Set address in appropriate DR0-DR3
    switch (reg) {
    case 0: ctx.Dr0 = addr; break;
    case 1: ctx.Dr1 = addr; break;
    case 2: ctx.Dr2 = addr; break;
    case 3: ctx.Dr3 = addr; break;
    }

    // Calculate bit positions in DR7
    int enableShift = reg * 2;                    // L0-L3/G0-G3 bits
    int conditionShift = 16 + (reg * 4);          // R/W0-R/W3 bits
    int sizeShift = 18 + (reg * 4);               // LEN0-LEN3 bits

    // Clear any existing bits for this debug register
    ctx.Dr7 &= ~(3ULL << enableShift);           // Clear L/G bits
    ctx.Dr7 &= ~(3ULL << conditionShift);        // Clear R/W bits
    ctx.Dr7 &= ~(3ULL << sizeShift);             // Clear LEN bits

    // Set up DR7
    ctx.Dr7 |= ((isLocal ? 1ULL : 2ULL) << enableShift);  // Set local/global enable bit
    ctx.Dr7 |= ((DWORD64)type << conditionShift);         // Set breakpoint type
    ctx.Dr7 |= ((DWORD64)size << sizeShift);              // Set breakpoint size

    if (!SetThreadContext(GetCurrentThread(), &ctx)) {
        return false;
    }

    assignedRegister = reg;
    return true;
}

// Helper to remove specific hardware breakpoint
void RemoveHWBreakpoint(int debugRegister) {
    Warning(__FUNCTION__ "\n");
    if (debugRegister < 0 || debugRegister > 3) return;

    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

    if (!GetThreadContext(GetCurrentThread(), &ctx)) {
        return;
    }

    // Clear the appropriate debug register
    switch (debugRegister) {
    case 0: ctx.Dr0 = 0; break;
    case 1: ctx.Dr1 = 0; break;
    case 2: ctx.Dr2 = 0; break;
    case 3: ctx.Dr3 = 0; break;
    }

    // Clear the corresponding DR7 bits
    int shift = debugRegister * 2;
    int conditionShift = 16 + (debugRegister * 4);
    ctx.Dr7 &= ~(1ULL << shift);                    // Disable bit
    ctx.Dr7 &= ~(3ULL << (conditionShift + 0));     // Clear condition bits

    SetThreadContext(GetCurrentThread(), &ctx);
}

LONG CALLBACK ExceptionHandler(EXCEPTION_POINTERS* ExceptionInfo) {
    if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
        void* faultingAddress = (void*)ExceptionInfo->ContextRecord->Rip;

        std::lock_guard<std::mutex> lock(g_breakpointMutex);

        // Check if this is one of our breakpoints
        for (const auto& bp : g_activeBreakpoints) {
            DWORD64 breakpointAddr = 0;

            // Get the address from the appropriate debug register
            switch (bp.debugRegister) {
            case 0: breakpointAddr = ExceptionInfo->ContextRecord->Dr0; break;
            case 1: breakpointAddr = ExceptionInfo->ContextRecord->Dr1; break;
            case 2: breakpointAddr = ExceptionInfo->ContextRecord->Dr2; break;
            case 3: breakpointAddr = ExceptionInfo->ContextRecord->Dr3; break;
            }

            if (breakpointAddr == (DWORD64)bp.address) {
                // Get module info for faulting address
                MODULEINFO modInfo;
                char moduleName[MAX_PATH];
                void* module = NULL;

                GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                    (LPCTSTR)faultingAddress,
                    (HMODULE*)&module);

                GetModuleInformation(GetCurrentProcess(),
                    (HMODULE)module,
                    &modInfo,
                    sizeof(MODULEINFO));

                GetModuleFileNameA((HMODULE)module, moduleName, MAX_PATH);

                // Calculate RVA
                DWORD64 rva = (DWORD64)faultingAddress - (DWORD64)modInfo.lpBaseOfDll;

                // Capture callstack
                void* callstack[16];
                USHORT frames = CaptureStackBackTrace(0, 16, callstack, NULL);

                // Build output string
                char output[4096];
                int written = snprintf(output, sizeof(output),
                    "Weapon state changed from module %s at RVA 0x%llX\nCallstack:\n",
                    moduleName, rva);

                // Process each callstack frame
                for (USHORT i = 0; i < frames && written < sizeof(output); i++) {
                    void* addr = callstack[i];

                    // Get module for this address
                    char frameModName[MAX_PATH] = "<unknown>";
                    DWORD64 frameRva = (DWORD64)addr;

                    HMODULE frameModule = NULL;
                    if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                        (LPCTSTR)addr,
                        &frameModule)) {

                        MODULEINFO frameModInfo;
                        GetModuleInformation(GetCurrentProcess(),
                            frameModule,
                            &frameModInfo,
                            sizeof(MODULEINFO));

                        GetModuleFileNameA(frameModule, frameModName, MAX_PATH);

                        // Extract filename only from path
                        char* lastSlash = strrchr(frameModName, '\\');
                        if (lastSlash) {
                            memmove(frameModName, lastSlash + 1, strlen(lastSlash + 1) + 1);
                        }

                        frameRva = (DWORD64)addr - (DWORD64)frameModInfo.lpBaseOfDll;
                    }

                    written += snprintf(output + written, sizeof(output) - written,
                        "[%2d] %s+0x%llX\n", i, frameModName, frameRva);
                }

                Warning("%s", output);

                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
class IHandleEntity;
class ServerClass;
class edict_t;
void* mp_weapon_wingman_ctor_hk(void* factory) {
    void* fakeentity = mp_weapon_wingman_ctor_orig();
    void* entity = *(void**)(((uintptr_t)fakeentity) + 16);
    if (entity) {
        void* weapStateAddr = (void*)((uintptr_t)entity + 0xA90);

        std::lock_guard<std::mutex> lock(g_breakpointMutex);

        int debugRegister;
        if (SetHWBreakpoint(weapStateAddr, debugRegister)) {
            g_activeBreakpoints.push_back({ weapStateAddr, entity, debugRegister });

            // Add exception handler if not already added
            static bool handlerAdded = false;
            if (!handlerAdded) {
                AddVectoredExceptionHandler(1, ExceptionHandler);
                handlerAdded = true;
            }
        }
    }
    else {
        Warning("!entity\n");
    }

    return fakeentity;
}
void* __fastcall mp_weapon_wingman_dtor_hk(void* entity, char a2) {
    if (entity) {
        std::lock_guard<std::mutex> lock(g_breakpointMutex);

        for (auto it = g_activeBreakpoints.begin(); it != g_activeBreakpoints.end(); ++it) {
            if (it->entity == entity) {
                RemoveHWBreakpoint(it->debugRegister);
                g_activeBreakpoints.erase(it);
                break;
            }
        }
    }
    else {
        Warning("!entity\n");
    }

    return mp_weapon_wingman_dtor_orig(entity, a2);
}