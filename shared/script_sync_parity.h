#pragma once
#include <cstdint>

// Script-sync parity between a client and the server it joins.
//
// Remote functions, server vars, entity vars and registered strings are sent
// over the wire as registration indices (_remote_functions.nut on both sides),
// so a client whose registrations differ from the server's calls the wrong
// remote functions and writes the wrong variables. Retail only compares one
// remote-function checksum and answers a mismatch with "retry", which loops
// forever; client.dll's "retry" is redirected to `r1drm` instead.
//
// 1. Both script VMs publish their four ordered name lists right after
//    _remote_functions.nut ran (ScriptSyncPublish, called by
//    scripts/vscripts/_r1d_script_sync.nut).
// 2. Server: when an R1Delta client's signon upload arrives (the sound-script
//    marker), the server sends that client its lists as svc_StringCmd
//    (`r1d_scriptsync begin|names|end`).
// 3. Client: once its own lists and the server's are both known it compares
//    them, names what differs and, when the server enforces it
//    (`delta_scriptsync_check 1`), disconnects with the names in the reason.

constexpr char kScriptSyncCommand[] = "r1d_scriptsync";
constexpr char kScriptSyncRetailMismatchCommand[] = "r1drm";

// Client: registers r1d_scriptsync and r1drm. The ScriptSyncPublish script
// natives are registered with the other natives in shared/squirrel.cpp.
void ScriptSyncParity_RegisterClient();
// Client: points client.dll's three remote-checksum "retry" commands at r1drm.
bool ScriptSyncParity_InstallClientRetryRedirect(std::uintptr_t clientBase);
// Client: a new signon started (the signon NET_SetConVar is being written).
void ScriptSyncParity_ClientSignon();

// Server: an R1Delta client's signon upload arrived; send it the server's lists.
void ScriptSyncParity_ServerPush(int playerSlot);
