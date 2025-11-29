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
// VTable Definitions
//
// This file defines vtable layouts for interfaces that differ between:
// - R1 (Titanfall 1 retail 2015)
// - R1O (Titanfall Online - uses server.dll from TFO with R1 engine)
// - R1 Dedicated (engine_ds.dll from 2014)
//
// Usage:
//   auto fn = g_pEngineServer->Get<VEngineServer::ClientCommand>().As<void(*)(int, const char*)>();
//   fn(clientIdx, "say hello");
//

#pragma once

#include <cstdint>

//=============================================================================
// VTable Entry Wrapper - allows .As<T>() casting
//=============================================================================

struct VTableEntry {
    void* ptr;

    template<typename T>
    T As() const { return reinterpret_cast<T>(ptr); }

    explicit operator bool() const { return ptr != nullptr; }
    operator void*() const { return ptr; }
};

//=============================================================================
// VEngineServer Vtable Definition
//=============================================================================
// Interface: VEngineServer022 (engine.dll / engine_ds.dll)
// R1O server.dll expects a different vtable layout than R1's engine provides

namespace VEngineServer {
    enum Func {
        NullSub1,
        ChangeLevel,
        IsMapValid,
        GetMapCRC,
        IsDedicatedServer,
        IsInEditMode,
        GetLaunchOptions,
        GetLocalNETConfig,      // R1O-specific
        GetNETConfig,           // R1O-specific
        GetLocalNETGetPacket,   // R1O-specific
        GetNETGetPacket,        // R1O-specific
        PrecacheModel,
        PrecacheDecal,
        PrecacheGeneric,
        IsModelPrecached,
        IsDecalPrecached,
        IsGenericPrecached,
        IsSimulating,
        GetPlayerUserId,
        GetPlayerUserId2,
        GetPlayerNetworkIDString,
        IsUserIDInUse,
        GetLoadingProgressForUserID,
        GetEntityCount,
        GetPlayerNetInfo,
        IsClientValid,
        PlayerChangesTeams,
        RequestClientScreenshot,
        CreateEdict,
        RemoveEdict,
        PvAllocEntPrivateData,
        FreeEntPrivateData,
        SaveAllocMemory,
        SaveFreeMemory,
        FadeClientVolume,
        ServerCommand,
        CBuf_Execute,
        ClientCommand,
        LightStyle,
        StaticDecal,
        EntityMessageBeginEx,
        EntityMessageBegin,
        UserMessageBegin,
        MessageEnd,
        ClientPrintf,
        Con_NPrintf,
        Con_NXPrintf,
        CrosshairAngle,
        GetGameDir,
        CompareFileTime,
        LockNetworkStringTables,
        CreateFakeClient,
        GetClientConVarValue,
        ReplayReady,
        GetReplayFrame,
        ParseFile,
        CopyLocalFile,
        PlaybackTempEntity,
        LoadGameState,
        LoadAdjacentEnts,
        ClearSaveDir,
        GetMapEntitiesString,
        GetPlaylistCount,
        IsPaused,
        InsertServerCommand,
        AllocLevelStaticData,
        Pause,
        MarkTeamsAsBalanced_On,
        MarkTeamsAsBalanced_Off,
        DisconnectClient,
        GetClientXUID,
        IsActiveApp,
        Bandwidth_WriteBandwidthStatsToFile,
        IsCheckpointMapLoad,
        UpdateClientHashtag,
        COUNT
    };

    // R1O indices (what server.dll from TFO expects)
    // This is the "canonical" ordering we build our trampoline vtable to match
    constexpr int R1O[] = {
        0,   // NullSub1
        1,   // ChangeLevel
        2,   // IsMapValid
        3,   // GetMapCRC
        4,   // IsDedicatedServer
        5,   // IsInEditMode
        6,   // GetLaunchOptions
        7,   // GetLocalNETConfig (TFO-specific stub)
        8,   // GetNETConfig (TFO-specific stub)
        9,   // GetLocalNETGetPacket (TFO-specific stub)
        10,  // GetNETGetPacket (TFO-specific stub)
        11,  // PrecacheModel
        12,  // PrecacheDecal
        13,  // PrecacheGeneric
        14,  // IsModelPrecached
        15,  // IsDecalPrecached
        16,  // IsGenericPrecached
        17,  // IsSimulating
        18,  // GetPlayerUserId
        19,  // GetPlayerUserId2
        20,  // GetPlayerNetworkIDString
        21,  // IsUserIDInUse
        22,  // GetLoadingProgressForUserID
        23,  // GetEntityCount
        24,  // GetPlayerNetInfo
        25,  // IsClientValid
        26,  // PlayerChangesTeams
        27,  // RequestClientScreenshot
        28,  // CreateEdict
        29,  // RemoveEdict
        30,  // PvAllocEntPrivateData
        31,  // FreeEntPrivateData
        32,  // SaveAllocMemory
        33,  // SaveFreeMemory
        34,  // FadeClientVolume
        35,  // ServerCommand
        36,  // CBuf_Execute
        37,  // ClientCommand
        38,  // LightStyle
        39,  // StaticDecal
        40,  // EntityMessageBeginEx
        41,  // EntityMessageBegin
        42,  // UserMessageBegin
        43,  // MessageEnd
        44,  // ClientPrintf
        45,  // Con_NPrintf
        46,  // Con_NXPrintf
        51,  // CrosshairAngle
        52,  // GetGameDir
        53,  // CompareFileTime
        54,  // LockNetworkStringTables
        55,  // CreateFakeClient
        56,  // GetClientConVarValue
        57,  // ReplayReady
        58,  // GetReplayFrame
        64,  // ParseFile
        65,  // CopyLocalFile
        66,  // PlaybackTempEntity
        68,  // LoadGameState
        69,  // LoadAdjacentEnts
        70,  // ClearSaveDir
        71,  // GetMapEntitiesString
        72,  // GetPlaylistCount
        105, // IsPaused
        127, // InsertServerCommand
        138, // AllocLevelStaticData
        142, // Pause
        149, // MarkTeamsAsBalanced_On
        150, // MarkTeamsAsBalanced_Off
        155, // DisconnectClient
        183, // GetClientXUID
        184, // IsActiveApp
        186, // Bandwidth_WriteBandwidthStatsToFile
        188, // IsCheckpointMapLoad
        196, // UpdateClientHashtag
    };

    // R1 retail engine.dll indices (non-dedicated)
    // These are the actual vtable indices in the 2015 engine
    constexpr int R1[] = {
        0,   // NullSub1
        1,   // ChangeLevel
        2,   // IsMapValid
        3,   // GetMapCRC
        4,   // IsDedicatedServer
        5,   // IsInEditMode
        6,   // GetLaunchOptions
        -1,  // GetLocalNETConfig (doesn't exist in R1)
        -1,  // GetNETConfig (doesn't exist in R1)
        -1,  // GetLocalNETGetPacket (doesn't exist in R1)
        -1,  // GetNETGetPacket (doesn't exist in R1)
        7,   // PrecacheModel (shifted -4 from R1O)
        8,   // PrecacheDecal
        9,   // PrecacheGeneric
        10,  // IsModelPrecached
        11,  // IsDecalPrecached
        12,  // IsGenericPrecached
        13,  // IsSimulating
        14,  // GetPlayerUserId
        15,  // GetPlayerUserId2
        16,  // GetPlayerNetworkIDString
        17,  // IsUserIDInUse
        18,  // GetLoadingProgressForUserID
        19,  // GetEntityCount
        20,  // GetPlayerNetInfo
        21,  // IsClientValid
        22,  // PlayerChangesTeams
        23,  // RequestClientScreenshot
        24,  // CreateEdict
        25,  // RemoveEdict
        26,  // PvAllocEntPrivateData
        27,  // FreeEntPrivateData
        28,  // SaveAllocMemory
        29,  // SaveFreeMemory
        30,  // FadeClientVolume
        31,  // ServerCommand
        32,  // CBuf_Execute
        33,  // ClientCommand
        34,  // LightStyle
        35,  // StaticDecal
        36,  // EntityMessageBeginEx
        37,  // EntityMessageBegin
        38,  // UserMessageBegin
        39,  // MessageEnd
        40,  // ClientPrintf
        41,  // Con_NPrintf
        42,  // Con_NXPrintf
        47,  // CrosshairAngle
        48,  // GetGameDir
        49,  // CompareFileTime
        50,  // LockNetworkStringTables
        51,  // CreateFakeClient
        52,  // GetClientConVarValue
        53,  // ReplayReady
        54,  // GetReplayFrame
        60,  // ParseFile
        61,  // CopyLocalFile
        62,  // PlaybackTempEntity
        64,  // LoadGameState
        65,  // LoadAdjacentEnts
        66,  // ClearSaveDir
        67,  // GetMapEntitiesString
        68,  // GetPlaylistCount
        101, // IsPaused
        123, // InsertServerCommand
        134, // AllocLevelStaticData
        138, // Pause
        145, // MarkTeamsAsBalanced_On
        146, // MarkTeamsAsBalanced_Off
        151, // DisconnectClient
        179, // GetClientXUID
        180, // IsActiveApp
        182, // Bandwidth_WriteBandwidthStatsToFile
        184, // IsCheckpointMapLoad
        192, // UpdateClientHashtag
    };
}

//=============================================================================
// Typed VTable Wrapper Template
//=============================================================================

template<typename VTableDef, const int* IndexTable, size_t VTableSize>
class TypedVTable {
    uintptr_t* m_vtable;
    void* m_thisptr;

public:
    TypedVTable() : m_vtable(nullptr), m_thisptr(nullptr) {}

    void Init(uintptr_t* vtable, void* thisptr) {
        m_vtable = vtable;
        m_thisptr = thisptr;
    }

    template<int FuncEnum>
    VTableEntry Get() const {
        static_assert(FuncEnum >= 0 && FuncEnum < VTableDef::COUNT, "Invalid vtable function enum");
        int idx = IndexTable[FuncEnum];
        if (idx < 0 || !m_vtable) return { nullptr };
        return { (void*)m_vtable[idx] };
    }

    void* GetThisPtr() const { return m_thisptr; }
    uintptr_t* GetRawVTable() const { return m_vtable; }
};

//=============================================================================
// Hook Helper Macros
//=============================================================================

// Declares a hook: type, original pointer, and provides the hook function signature
// Usage:
//   DECLARE_HOOK(MyFunc, void, (int arg1, const char* arg2));
//   // Generates:
//   //   typedef void (*MyFunc_t)(int arg1, const char* arg2);
//   //   inline MyFunc_t MyFunc_Original = nullptr;
#define DECLARE_HOOK(name, ret, args) \
    typedef ret (*name##_t) args; \
    inline name##_t name##_Original = nullptr

// For hooks that need extern linkage (declared in header, defined elsewhere)
#define DECLARE_HOOK_EXTERN(name, ret, args) \
    typedef ret (*name##_t) args; \
    extern name##_t name##_Original

#define DEFINE_HOOK_ORIGINAL(name) \
    name##_t name##_Original = nullptr

//=============================================================================
// VFileSystem017 Vtable Definition
//=============================================================================
// Interface: VFileSystem017 (filesystem_stdio.dll)

namespace VFileSystem {
    enum Func {
        Connect,
        Disconnect,
        QueryInterface,
        Init,
        Shutdown,
        GetDependencies,
        GetTier,
        Reconnect,
        // TFO-specific (indices 8-11 in R1O)
        TFO_NullSub3,
        TFO_NullSub4,
        TFO_GetFileSystemThing,
        TFO_DoFilesystemOp,
        // Standard filesystem functions
        AddSearchPath,
        RemoveSearchPath,
        RemoveAllSearchPaths,
        RemoveSearchPaths,
        MarkPathIDByRequestOnly,
        RelativePathToFullPath,
        GetSearchPath,
        AddPackFile,
        RemoveFile,
        RenameFile,
        CreateDirHierarchy,
        IsDirectory,
        FileTimeToString,
        SetBufferSize,
        IsOK,
        EndOfLine,
        ReadLine,
        FPrintf,
        LoadModule,
        UnloadModule,
        FindFirst,
        FindNext,
        FindIsDirectory,
        FindClose,
        FindFirstEx,
        FindFileAbsoluteList,
        GetLocalPath,
        FullPathToRelativePath,
        GetCurrentDirectory,
        FindOrAddFileName,
        String,
        AsyncReadMultiple,
        AsyncAppend,
        AsyncAppendFile,
        AsyncFinishAll,
        AsyncFinishAllWrites,
        AsyncFlush,
        AsyncSuspend,
        AsyncResume,
        AsyncBeginRead,
        AsyncEndRead,
        AsyncFinish,
        AsyncGetResult,
        AsyncAbort,
        AsyncStatus,
        AsyncSetPriority,
        AsyncAddRef,
        AsyncRelease,
        RemoveLoggingFunc,
        GetFilesystemStatistics,
        OpenEx,
        LoadVPKForMap,
        GetPathTime,
        GetFSConstructedFlag,
        EnableWhitelistFileTracking,
        COUNT
    };
}

//=============================================================================
// VBaseFileSystem011 Vtable Definition
//=============================================================================
// Interface: VBaseFileSystem011 (filesystem_stdio.dll)

namespace VBaseFileSystem {
    enum Func {
        Read,
        Write,
        Open,
        Close,
        Seek,
        Tell,
        Size,
        Size2,
        Flush,
        Precache,
        FileExists,
        IsFileWritable,
        SetFileWritable,
        GetFileTime,
        ReadFile,
        WriteFile,
        UnzipFile,
        COUNT
    };
}

//=============================================================================
// VModelInfoServer Vtable Definition
//=============================================================================
// Interface: VModelInfoServer (engine.dll)

namespace VModelInfo {
    enum Func {
        Destructor,
        GetModel,
        GetModelIndex,
        GetModelName,
        GetVCollide,
        GetVCollideEx,
        GetVCollideEx2,
        GetModelRenderBounds,
        // TFO-specific stubs (indices 8-15 in R1O)
        TFO_SetFlag,
        TFO_ClearFlag,
        TFO_GetFlag,
        TFO_Void1,
        TFO_Void2,
        TFO_ShouldRet0,
        TFO_Void3,
        TFO_ClientFullyConnected,
        // Standard functions continue
        GetModelFrameCount,
        GetModelType,
        TFO_ShouldRet0_2,
        GetModelExtraData,
        IsTranslucentTwoPass,
        ModelHasMaterialProxy,
        IsTranslucent,
        COUNT
    };
}

