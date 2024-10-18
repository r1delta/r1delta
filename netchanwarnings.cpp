#include "bitbuf.h"
#include "dedicated.h"
#include "logging.h"
#include "load.h"
#include "netchanwarnings.h"
#ifdef _DEBUG
struct /*VFT*/ INetMessage;
// Function pointer types for the hooked functions
typedef bool (*ReadFromBufferFn)(INetMessage*, bf_read*);
typedef bool (*WriteToBufferFn)(INetMessage*, bf_write*);
typedef bool (*ProcessFn)(INetMessage*);

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
    Warning(__FUNCTION__ ": unknown NetMessage %s!\n", thisPtr->GetName());
    return -1;
}

bool HookReadFromBuffer(INetMessage* thisPtr, bf_read* buffer) {
    int index = GetNetMessageIndex(thisPtr);
    if (index != -1) {
        bool result = original_ReadFromBuffer[index](thisPtr, buffer);
        if (!result) {
            Warning("ReadFromBuffer failed for %s\n", netMessages[index].name);
        }
        //if (index != 18 && index != 10 && index != 4 && index != 46 && index != 38)
        //    Msg("%s: %s to %p\n", __FUNCTION__, thisPtr->ToString(), thisPtr->GetNetChannel());

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
        //if (index != 18 && index != 10 && index != 4 && index != 46 && index != 38)
        //    Msg("%s: %s to %p\n", __FUNCTION__, thisPtr->ToString(), thisPtr->GetNetChannel());
        return result;
    }

    return false;
}
bool (*oCNetChan__SendNetMsg)(__int64 a1, INetMessage* a2, char a3, char a4);
bool CNetChan__SendNetMsg(__int64 a1, INetMessage* a2, char a3, char a4)
{
    auto ret = oCNetChan__SendNetMsg(a1, a2, a3, a4);
    int index = GetNetMessageIndex(a2);
    //if (index != 18 && index != 10 && index != 4 && index != 46 && index != 38)
    //    Msg("%s: %s to %p, ret was %s\n", __FUNCTION__, a2->ToString(), a2->GetNetChannel(), ret ? "true" : "false");
    return ret;
}
bool HookProcess(INetMessage* thisPtr) {
    int index = GetNetMessageIndex(thisPtr);
    if (index != -1) {
        bool result = original_Process[index](thisPtr);
        if (!result) {
            Warning("Process failed for %s\n", netMessages[index].name);
        }
        //if (index != 18 && index != 10 && index != 4 && index != 46 && index != 38)
        //    Msg("%s: %s to %p\n", __FUNCTION__, thisPtr->ToString(), thisPtr->GetNetChannel());
        return result;
    }
    return false;
}
//char (*oCGameClient__SendNetMsg)(float* a1, INetMessage* a2, unsigned __int8 a3, unsigned __int8 a4, unsigned __int8 a5);
//char CGameClient__SendNetMsg(float* a1, INetMessage* a2, unsigned __int8 a3, unsigned __int8 a4, unsigned __int8 a5) {
//    auto ret = oCGameClient__SendNetMsg(a1, a2, a3, a4, a5);
//    int index = GetNetMessageIndex(a2);
//    if (index != 18 && index != 10 && index != 4 && index != 46 && index != 38)
//        Msg("%s: %s to %p, ret was %s\n", __FUNCTION__, a2->ToString(), a2->GetNetChannel(), ret ? "true" : "false");
//    return ret;
//}

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
            // NOTE: this won't work for netmessages MinHooked elsewhere like clc_move and net_setconvar
            void** vft = (void**)vtable;
            MH_CreateHook((ReadFromBufferFn)vft[4], (void*)HookReadFromBuffer, reinterpret_cast<LPVOID*>(&original_ReadFromBuffer[i]));
            MH_CreateHook((WriteToBufferFn)vft[5], (void*)HookWriteToBuffer, reinterpret_cast<LPVOID*>(&original_WriteToBuffer[i]));
            MH_CreateHook((ProcessFn)vft[3], (void*)HookProcess, reinterpret_cast<LPVOID*>(&original_Process[i]));
        }
    }
    MH_CreateHook((LPVOID)(G_engine + 0x1E1550), CNetChan__SendNetMsg, reinterpret_cast<LPVOID*>(&oCNetChan__SendNetMsg));
    if (IsDedicatedServer()) {
       // MH_CreateHook((LPVOID)(G_engine_ds + 0x4C860), CGameClient__SendNetMsg, reinterpret_cast<LPVOID*>(&oCGameClient__SendNetMsg));
       // MH_CreateHook((LPVOID)(G_engine_ds + 0x496F0), CBaseClient__SendNetMsg, reinterpret_cast<LPVOID*>(&oCBaseClient__SendNetMsg));
    }
    else {
       // MH_CreateHook((LPVOID)(G_engine + 0xDB080), CGameClient__SendNetMsg, reinterpret_cast<LPVOID*>(&oCGameClient__SendNetMsg));
        //MH_CreateHook((LPVOID)(G_engine + 0xD8320), CBaseClient__SendNetMsg, reinterpret_cast<LPVOID*>(&oCBaseClient__SendNetMsg));
    }
    MH_EnableHook(MH_ALL_HOOKS);
    return true;
}
#endif