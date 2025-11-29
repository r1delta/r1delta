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
// VTable Trampoline Utilities
//
// This header provides macros and functions for handling vtable mismatches between
// DLLs compiled at different times. There are two main approaches:
//
// 1. Dummy Objects (CreateFunction / VTABLE_TRAMPOLINE):
//    - Used when the interface is pure virtual (no public members except vtable)
//    - Creates dummy objects with trampolines that redirect calls to the real object
//    - Each trampoline overwrites rcx (thisptr) before jumping to the target
//
// 2. Callgates (CreateCallgate / CALLGATE_TRAMPOLINE):
//    - Used when the object has public members or needs to be passed to multiple DLLs
//    - Checks the return address to determine the caller's DLL
//    - Redirects to different vtable indices based on the caller
//
// See: https://github.com/r1delta/r1delta/wiki/VTable-Trampolines

#pragma once

#include "framework.h"
#include <stdint.h>

// Forward declaration from utils.h
uintptr_t CreateFunction(void* func, void* real);

//=============================================================================
// Hook Declaration and Creation Macros
//=============================================================================

// Declares a hook function type and original function pointer
// Usage: DECLARE_HOOK(ReturnType, HookName, (args...))
#define DECLARE_HOOK(ret, name, args) \
    typedef ret(*name##_t) args; \
    extern name##_t name##_Original

// Defines the original function pointer
// Usage: DEFINE_HOOK(HookName)
#define DEFINE_HOOK(name) \
    name##_t name##_Original = nullptr

// Creates a hook at a fixed address
// Usage: HOOK(base + offset, HookFunction)
#define HOOK(addr, hook) \
    MH_CreateHook((LPVOID)(addr), &hook, reinterpret_cast<LPVOID*>(&hook##_Original))

// Creates a hook with no original pointer
// Usage: HOOK_NOTRAMP(base + offset, HookFunction)
#define HOOK_NOTRAMP(addr, hook) \
    MH_CreateHook((LPVOID)(addr), &hook, NULL)

// Creates a hook with different offsets for dedicated server vs client
// Usage: HOOK_DEDI(base, dedi_offset, client_offset, HookFunction)
#define HOOK_DEDI(base, dedi_off, client_off, hook) \
    MH_CreateHook((LPVOID)((base) + (IsDedicatedServer() ? (dedi_off) : (client_off))), \
                  &hook, reinterpret_cast<LPVOID*>(&hook##_Original))

// Creates a hook with different offsets for dedicated server vs client, no trampoline
// Usage: HOOK_DEDI_NOTRAMP(base, dedi_offset, client_offset, HookFunction)
#define HOOK_DEDI_NOTRAMP(base, dedi_off, client_off, hook) \
    MH_CreateHook((LPVOID)((base) + (IsDedicatedServer() ? (dedi_off) : (client_off))), \
                  &hook, NULL)

//=============================================================================
// Dummy Object VTable Macros (Option 2)
//=============================================================================
//
// Used when an interface is pure virtual (only has vtable pointer as public member).
// Creates a dummy object with a modified vtable where each entry is a trampoline
// that overwrites rcx with the real object's address before jumping to the target.
//
// This allows consumer DLLs to use the old vtable layout while the trampolines
// redirect calls to the correct functions in the real object.
//

// Creates a vtable trampoline entry that redirects to the real object
// Usage: VTABLE_TRAMPOLINE(real_func_ptr, real_object_ptr)
#define VTABLE_TRAMPOLINE(func, real) \
    CreateFunction((void*)(func), (void*)(real))

// Declares a dummy vtable array
// Usage: DECLARE_DUMMY_VTABLE(InterfaceName, size)
#define DECLARE_DUMMY_VTABLE(name, size) \
    extern uintptr_t g_r1o##name##Interface[size]

// Defines a dummy vtable array
// Usage: DEFINE_DUMMY_VTABLE(InterfaceName, size)
#define DEFINE_DUMMY_VTABLE(name, size) \
    uintptr_t g_r1o##name##Interface[size]

// Initializes a vtable entry using a function from the original vtable
// Usage: INIT_VTABLE_ENTRY(InterfaceName, index, original_vtable, real_object)
#define INIT_VTABLE_ENTRY(name, idx, orig_vtable, real) \
    g_r1o##name##Interface[idx] = VTABLE_TRAMPOLINE((orig_vtable)[idx], real)

// Initializes a vtable entry using a custom replacement function
// Usage: INIT_VTABLE_ENTRY_CUSTOM(InterfaceName, index, custom_func, real_object)
#define INIT_VTABLE_ENTRY_CUSTOM(name, idx, func, real) \
    g_r1o##name##Interface[idx] = VTABLE_TRAMPOLINE(func, real)

// Returns a pointer to a dummy object (double ref return for CreateInterface pattern)
// Usage: RETURN_DUMMY_OBJECT(InterfaceName)
#define RETURN_DUMMY_OBJECT(name) \
    do { \
        static void* _ptr_##name = &g_r1o##name##Interface; \
        return &_ptr_##name; \
    } while(0)

//=============================================================================
// Callgate VTable Macros (Option 3)
//=============================================================================
//
// Used when the object has public members (beyond vtable) or needs to be passed
// to multiple DLLs that expect different vtable layouts. Callgates check the
// return address to determine which DLL is calling and redirect accordingly.
//
// This is more complex but handles cases where dummy objects would fail due to
// consumer DLLs trying to access member fields that don't exist on the dummy.
//

// Creates a callgate trampoline that checks caller DLL before redirecting
// Parameters:
//   vftable - pointer to the original vtable
//   dll_start, dll_end - address range of the DLL that needs remapping
//   orig_idx - the vtable index the consumer DLL expects
//   new_idx - the actual vtable index in the producer DLL
//
// If caller is within [dll_start, dll_end], uses orig_idx (caller's expectation)
// Otherwise uses new_idx (the actual correct index)
uintptr_t CreateCallgate(void* vftable, uintptr_t dll_start, uintptr_t dll_end,
                         int orig_idx, int new_idx);

// Shorthand for creating a callgate
// Usage: CALLGATE_TRAMPOLINE(vtable, dll_start, dll_end, old_index, new_index)
#define CALLGATE_TRAMPOLINE(vftable, start, end, orig, new_idx) \
    CreateCallgate((void*)(vftable), (uintptr_t)(start), (uintptr_t)(end), orig, new_idx)

// Creates a callgate only if indices differ, otherwise returns original function
// Usage: CALLGATE_TRAMPOLINE_IF_NEEDED(vtable, dll_start, dll_end, old_index, new_index)
#define CALLGATE_TRAMPOLINE_IF_NEEDED(vftable, start, end, orig, new_idx) \
    ((orig) != (new_idx) ? \
        CreateCallgate((void*)(vftable), (uintptr_t)(start), (uintptr_t)(end), orig, new_idx) : \
        ((uintptr_t*)(vftable))[orig])

//=============================================================================
// Utility Macros
//=============================================================================

// Gets the module base and size for callgate range checking
// Usage: GET_MODULE_RANGE(module_handle, &start, &end)
#define GET_MODULE_RANGE(hModule, pStart, pEnd) \
    do { \
        auto _mz = (PIMAGE_DOS_HEADER)(hModule); \
        auto _pe = (PIMAGE_NT_HEADERS64)((uint8_t*)_mz + _mz->e_lfanew); \
        *(pStart) = (uintptr_t)(hModule); \
        *(pEnd) = *(pStart) + _pe->OptionalHeader.SizeOfImage; \
    } while(0)

// Gets the vtable pointer from an object
// Usage: uintptr_t* vtable = GET_VTABLE(object_ptr)
#define GET_VTABLE(obj) (*(uintptr_t**)(obj))

// Gets a function pointer from a vtable at a specific index
// Usage: void* func = VTABLE_FUNC(vtable_ptr, index)
#define VTABLE_FUNC(vtable, idx) ((void*)((vtable)[idx]))

// Overwrites an object's vtable pointer
// Usage: SET_VTABLE(object_ptr, new_vtable)
#define SET_VTABLE(obj, vtable) (*(uintptr_t**)(obj) = (uintptr_t*)(vtable))
