// CBaseClient::SendSnapshot (listen engine.dll, legacy engine_ds.dll, engine_r1o.dll) writes the entity snapshot of one client into a fixed
// 160000-byte bf_write. When a FULL update (no delta frame: a joining client, or one whose acknowledged frame the server no longer has)
// does not fit, the engine calls Error("Reliable snapshot overflow.") and the whole process dies; on a listen server that is the host and every
// connected player. A delta that does not fit is dropped silently. The buffer cannot be enlarged here: it lives inside the client object, the
// full update then travels through the 262144-byte reliable stream, and the receiving side is sized for the same transfer.
//
// This module keeps the process alive: our tier0 Error() hands that one message to SnapshotOverflow_HandleError(), which (only when it was
// raised inside SendSnapshot, known from a thread-local set by the SendSnapshot detour) queues the client and returns, so the engine's own
// "return 0" runs. The client is then disconnected from the main thread (SendSnapshot may run on a worker, sv_parallel_sendsnapshot).
// On a listen server the offender can be the host's own client; its disconnect ends the session with a message instead of a crash.
#include "core.h"
#include "load.h"
#include "logging.h"

#include <MinHook.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

struct Site {
	uintptr_t rva;
	unsigned char prologue[16];
};
// Exact builds only (first 16 bytes checked).
constexpr Site kListen = { 0xD8DF0, { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xF0, 0x00, 0x00, 0x00, 0x8B, 0x6A } };
constexpr Site kDedicated = { 0x4A170, { 0x40, 0x55, 0x56, 0x57, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xF8, 0x00, 0x00, 0x00, 0x8B, 0x6A, 0x08 } };
constexpr Site kR1O = { 0x13FC80, { 0x40, 0x53, 0x55, 0x57, 0x41, 0x55, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xF0, 0x00, 0x00, 0x00, 0x4C } };

// CBaseClient: the IClient subobject is at +8 (see sv_filter.h); IClient vtable +104 Disconnect(uint8, fmt, ...), +136 GetName, +240 IsConnected,
// +264 IsFakeClient (the engine's own timeout check calls them the same way).
constexpr size_t kIClientOffset = 8;
constexpr size_t kDisconnectSlot = 13;
constexpr size_t kGetNameSlot = 17;
constexpr size_t kIsConnectedSlot = 30;
constexpr size_t kIsFakeClientSlot = 33;

thread_local uintptr_t t_snapshotClient = 0;
__int64(__fastcall* oSendSnapshot)(__int64, __int64) = nullptr;

std::mutex g_mutex;
std::vector<uintptr_t> g_pending;

__int64 __fastcall HookSendSnapshot(__int64 client, __int64 frame)
{
	const uintptr_t previous = t_snapshotClient;
	t_snapshotClient = static_cast<uintptr_t>(client);
	const __int64 result = oSendSnapshot(client, frame);
	t_snapshotClient = previous;
	return result;
}

bool CallIClientBool(uintptr_t iclient, size_t slot)
{
	void** vtable = *reinterpret_cast<void***>(iclient);
	return reinterpret_cast<bool(__fastcall*)(void*)>(vtable[slot])(reinterpret_cast<void*>(iclient));
}

// No C++ objects in here: __try cannot share a function with unwinding.
void DisconnectOverflowedClient(uintptr_t iclient)
{
	__try {
		if (!CallIClientBool(iclient, kIsConnectedSlot) || CallIClientBool(iclient, kIsFakeClientSlot))
			return;
		void** vtable = *reinterpret_cast<void***>(iclient);
		const char* name = reinterpret_cast<const char*(__fastcall*)(void*)>(vtable[kGetNameSlot])(reinterpret_cast<void*>(iclient));
		Warning("R1Delta: full-update snapshot of %s does not fit the 160000-byte buffer (Reliable snapshot overflow); disconnecting the client instead of ending the process\n", name ? name : "?");
		reinterpret_cast<void(__fastcall*)(void*, uint8_t, const char*, ...)>(vtable[kDisconnectSlot])(
			reinterpret_cast<void*>(iclient), 1, "%s", "The server's world state in your view is too large for one snapshot (Reliable snapshot overflow)");
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

} // namespace

bool SnapshotOverflow_HandleError()
{
	if (!t_snapshotClient)
		return false; // not raised from SendSnapshot: leave the engine's fatal path alone
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		for (uintptr_t queued : g_pending)
			if (queued == t_snapshotClient)
				return true;
		g_pending.push_back(t_snapshotClient);
	}
	OutputDebugStringA("R1Delta: full-update snapshot does not fit the 160000-byte buffer (Reliable snapshot overflow); dropping the client instead of ending the process\n");
	return true;
}

// Main thread, every server frame.
void SnapshotOverflow_ServerFrame()
{
	std::vector<uintptr_t> clients;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_pending.empty())
			return;
		clients.swap(g_pending);
	}
	for (uintptr_t client : clients)
		DisconnectOverflowedClient(client + kIClientOffset);
}

void SnapshotOverflow_Install(uintptr_t engineBase, bool dedicated, bool r1o)
{
	const Site& site = r1o ? kR1O : (dedicated ? kDedicated : kListen);
	void* target = reinterpret_cast<void*>(engineBase + site.rva);
	if (memcmp(target, site.prologue, sizeof(site.prologue)) != 0) {
		OutputDebugStringA("R1Delta: SendSnapshot prologue mismatch, snapshot overflow guard not installed\n");
		return;
	}
	MH_CreateHook(target, &HookSendSnapshot, reinterpret_cast<LPVOID*>(&oSendSnapshot));
}
