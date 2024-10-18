#include "bitbuf.h"
#include "dedicated.h"
#include "logging.h"
#include "load.h"
#ifdef _DEBUG
struct /*VFT*/ INetMessage;
// Function pointer types for the hooked functions
typedef bool (*ReadFromBufferFn)(INetMessage*, bf_read*);
typedef bool (*WriteToBufferFn)(INetMessage*, bf_write*);
typedef bool (*ProcessFn)(INetMessage*);
struct /*VFT*/ INetMessage
{
    virtual (~INetMessage)();
    virtual void(SetNetChannel)(void*);
    virtual void(SetReliable)(bool);
    virtual bool(Process)();
    virtual bool(ReadFromBuffer)(bf_read*);
    virtual bool(WriteToBuffer)(bf_write*);
    virtual bool(IsUnreliable)();
    virtual bool(IsReliable)();
    virtual int(GetType)();
    virtual int(GetGroup)();
    virtual const char* (GetName)();
    virtual void* (GetNetChannel)();
    virtual const char* (ToString)();
    virtual unsigned int(GetSize)();
};
// Original function pointers
ReadFromBufferFn original_ReadFromBuffer[sizeof(netMessages) / sizeof(netMessages[0])];
WriteToBufferFn original_WriteToBuffer[sizeof(netMessages) / sizeof(netMessages[0])];
ProcessFn original_Process[sizeof(netMessages) / sizeof(netMessages[0])];
// Helper function to get the index of the netMessage
int GetNetMessageIndex(INetMessage* thisPtr) {
    for (size_t i = 0; i < sizeof(netMessages) / sizeof(netMessages[0]); i++) {
        if (!_stricmp(thisPtr->GetName(), netMessages[i].name))
            return i;
    }
    Warning(__FUNCTION__ ": unknown NetMessage %s!", thisPtr->GetName());
    return -1;
}

bool HookReadFromBuffer(INetMessage* thisPtr, bf_read* buffer) {
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

bool HookWriteToBuffer(INetMessage* thisPtr, bf_write* buffer) {
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

bool HookProcess(INetMessage* thisPtr) {
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


bool InitNetChanWarningHooks() {
    for (size_t i = 0; i < sizeof(netMessages) / sizeof(netMessages[0]); i++) {
        void* vtable = nullptr;

        if (G_engine && netMessages[i].offset_engine != 0) {
            vtable = (void*)((uintptr_t)G_engine + netMessages[i].offset_engine);
        }
        //else if (G_engine_ds && netMessages[i].offset_engine_ds != 0) {
        //    vtable = (void*)((uintptr_t)G_engine_ds + netMessages[i].offset_engine_ds);
        //}

        if (vtable) {
            void** vft = (void**)vtable;
            MH_CreateHook((ReadFromBufferFn)vft[4], (void*)HookReadFromBuffer, reinterpret_cast<LPVOID*>(&original_ReadFromBuffer[i]));
            MH_CreateHook((WriteToBufferFn)vft[5], (void*)HookWriteToBuffer, reinterpret_cast<LPVOID*>(&original_WriteToBuffer[i]));
            MH_CreateHook((ProcessFn)vft[3], (void*)HookProcess, reinterpret_cast<LPVOID*>(&original_Process[i]));
        }
    }
    MH_EnableHook(MH_ALL_HOOKS);
    return true;
}
#endif