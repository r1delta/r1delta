#include "core.h"

#include "bitbuf.h"
#include "dedicated.h"
#include "logging.h"
#include "load.h"
#include "netchanwarnings.h"
#include <Windows.h>
#if BUILD_DEBUG
// Function pointer types for the hooked functions
typedef bool (__fastcall *ReadFromBufferFn)(INetMessage*, bf_read*);
typedef bool (__fastcall *WriteToBufferFn)(INetMessage*, bf_write*);
typedef bool (__fastcall *ProcessFn)(INetMessage*);

// Original function pointers
ReadFromBufferFn original_ReadFromBuffer[sizeof(netMessages) / sizeof(netMessages[0])];
WriteToBufferFn original_WriteToBuffer[sizeof(netMessages) / sizeof(netMessages[0])];
ProcessFn original_Process[sizeof(netMessages) / sizeof(netMessages[0])];

#define ENABLE_ANNOYING_NETMESSAGE_SPAM

// Helper function to get the index of the netMessage
int GetNetMessageIndex(INetMessage* thisPtr) {
    for (size_t i = 0; i < sizeof(netMessages) / sizeof(netMessages[0]); i++) {
        if (!_stricmp(thisPtr->GetName(), netMessages[i].name))
            return i;
    }
    Warning(__FUNCTION__ ": unknown NetMessage %s!", thisPtr->GetName());
    return -1;
}

bool __fastcall HookReadFromBuffer(INetMessage* thisPtr, bf_read* buffer) {
    int index = GetNetMessageIndex(thisPtr);
    if (index != -1) {
        bool result = original_ReadFromBuffer[index](thisPtr, buffer);
        if (!result) {
            Warning("ReadFromBuffer failed for %s\n", netMessages[index].name);
        }
#ifdef ENABLE_ANNOYING_NETMESSAGE_SPAM
        if (index != 18 && index != 10 && index != 4 && index != 46 && index != 38)
            Msg("%s: %s to %p\n", __FUNCTION__, thisPtr->ToString(), thisPtr->GetNetChannel());
#endif
        return result;
    }
    return false;
}

bool __fastcall HookWriteToBuffer(INetMessage* thisPtr, bf_write* buffer) {
    int index = GetNetMessageIndex(thisPtr);
    if (index != -1) {
        bool result = original_WriteToBuffer[index](thisPtr, buffer);
        if (!result) {
            Warning("WriteToBuffer failed for %s\n", netMessages[index].name);
        }
#ifdef ENABLE_ANNOYING_NETMESSAGE_SPAM
        if (index != 18 && index != 10 && index != 4 && index != 46 && index != 38)
            Msg("%s: %s to %p\n", __FUNCTION__, thisPtr->ToString(), thisPtr->GetNetChannel());
#endif
        return result;
    }
    return false;
}

bool __fastcall HookProcess(INetMessage* thisPtr) {
    int index = GetNetMessageIndex(thisPtr);
    if (index != -1) {
        bool result = original_Process[index](thisPtr);
        if (!result) {
            Warning("Process failed for %s\n", netMessages[index].name);
        }
#ifdef ENABLE_ANNOYING_NETMESSAGE_SPAM
        if (index != 18 && index != 10 && index != 4 && index != 46 && index != 38)
            Msg("%s: %s to %p\n", __FUNCTION__, thisPtr->ToString(), thisPtr->GetNetChannel());
#endif
        return result;
    }
    return false;
}

// Helper to swap a vtable entry
static void SwapVTableEntry(void** vtable, int index, void* newFunc, void** origFunc) {
    *origFunc = vtable[index];
    DWORD oldProtect;
    VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
    vtable[index] = newFunc;
    VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);
}

bool InitNetChanWarningHooks() {
    return true;
    for (size_t i = 0; i < sizeof(netMessages) / sizeof(netMessages[0]); i++) {
        void* vtable = nullptr;

        if (G_engine && netMessages[i].offset_engine != 0) {
            vtable = (void*)((uintptr_t)G_engine + netMessages[i].offset_engine);
        }

        if (vtable) {
            void** vft = (void**)vtable;
            SwapVTableEntry(vft, 4, (void*)HookReadFromBuffer, (void**)&original_ReadFromBuffer[i]);
            SwapVTableEntry(vft, 5, (void*)HookWriteToBuffer, (void**)&original_WriteToBuffer[i]);
            SwapVTableEntry(vft, 3, (void*)HookProcess, (void**)&original_Process[i]);
        }
    }
    return true;
}
#endif