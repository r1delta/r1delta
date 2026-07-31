#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <MinHook.h>

// R1Delta installs hundreds of hooks over several module-load phases. Keep the
// MinHook API at call sites, but make failures observable and recover a bulk
// enable suffix if one target fails during MH_EnableHook(MH_ALL_HOOKS).
MH_STATUS WINAPI R1D_MH_Initialize(const char* sourceFile, int sourceLine);
MH_STATUS WINAPI R1D_MH_Uninitialize(const char* sourceFile, int sourceLine);
MH_STATUS WINAPI R1D_MH_CreateHook(
    LPVOID target,
    LPVOID detour,
    LPVOID* original,
    const char* sourceFile,
    int sourceLine);
MH_STATUS WINAPI R1D_MH_RemoveHook(
    LPVOID target,
    const char* sourceFile,
    int sourceLine);
MH_STATUS WINAPI R1D_MH_EnableHook(
    LPVOID target,
    const char* sourceFile,
    int sourceLine);
MH_STATUS WINAPI R1D_MH_DisableHook(
    LPVOID target,
    const char* sourceFile,
    int sourceLine);
MH_STATUS WINAPI R1D_MH_QueueEnableHook(
    LPVOID target,
    const char* sourceFile,
    int sourceLine);
MH_STATUS WINAPI R1D_MH_QueueDisableHook(
    LPVOID target,
    const char* sourceFile,
    int sourceLine);
MH_STATUS WINAPI R1D_MH_ApplyQueued(const char* sourceFile, int sourceLine);

#define MH_Initialize() \
    R1D_MH_Initialize(__FILE__, __LINE__)
#define MH_Uninitialize() \
    R1D_MH_Uninitialize(__FILE__, __LINE__)
#define MH_CreateHook(target, detour, original) \
    R1D_MH_CreateHook((target), (detour), (original), __FILE__, __LINE__)
#define MH_RemoveHook(target) \
    R1D_MH_RemoveHook((target), __FILE__, __LINE__)
#define MH_EnableHook(target) \
    R1D_MH_EnableHook((target), __FILE__, __LINE__)
#define MH_DisableHook(target) \
    R1D_MH_DisableHook((target), __FILE__, __LINE__)
#define MH_QueueEnableHook(target) \
    R1D_MH_QueueEnableHook((target), __FILE__, __LINE__)
#define MH_QueueDisableHook(target) \
    R1D_MH_QueueDisableHook((target), __FILE__, __LINE__)
#define MH_ApplyQueued() \
    R1D_MH_ApplyQueued(__FILE__, __LINE__)
