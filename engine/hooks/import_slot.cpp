// Rewrites a loaded module's import address table slots by the address they are bound to.
#include "import_slot.h"

#include <Windows.h>

namespace
{
const IMAGE_DATA_DIRECTORY* FindImportDirectory(std::uintptr_t moduleBase)
{
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(moduleBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return nullptr;
    const IMAGE_DATA_DIRECTORY& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    return imports.VirtualAddress && imports.Size ? &imports : nullptr;
}

bool WriteSlot(IMAGE_THUNK_DATA* slot, const void* replacement)
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(&slot->u1.Function, sizeof(slot->u1.Function), PAGE_READWRITE, &oldProtect))
        return false;
    // One aligned pointer store: a thread calling through the slot sees the old or the new target, never a mix.
    InterlockedExchangePointer(reinterpret_cast<PVOID*>(&slot->u1.Function), const_cast<void*>(replacement));
    VirtualProtect(&slot->u1.Function, sizeof(slot->u1.Function), oldProtect, &oldProtect);
    return true;
}
}

size_t ReplaceBoundImport(std::uintptr_t moduleBase, const void* boundTarget, const void* replacement)
{
    if (!moduleBase || !boundTarget || !replacement)
        return 0;
    const IMAGE_DATA_DIRECTORY* directory = FindImportDirectory(moduleBase);
    if (!directory)
        return 0;

    size_t replaced = 0;
    const auto bound = reinterpret_cast<ULONG_PTR>(boundTarget);
    for (auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(moduleBase + directory->VirtualAddress);
         descriptor->Name; ++descriptor) {
        for (auto* slot = reinterpret_cast<IMAGE_THUNK_DATA*>(moduleBase + descriptor->FirstThunk);
             slot->u1.Function; ++slot) {
            if (slot->u1.Function == bound && WriteSlot(slot, replacement))
                ++replaced;
        }
    }
    return replaced;
}
