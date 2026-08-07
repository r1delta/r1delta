#include "vpk_async_precache_fix.h"

#include "engine/core/core.h"
#include "engine/logging/logging.h"

#include <MinHook.h>
#include <Windows.h>
#include <intrin.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr uintptr_t kAsyncPrecacheWorkerRva = 0x74D50;
constexpr uintptr_t kFindResourceHandlerRva = 0x71350;
constexpr uintptr_t kFindPackEntrySlotRva = 0x746B0;
constexpr uintptr_t kCompareResourceExtensionRva = 0x4AFD0;
constexpr uintptr_t kFormatPathRva = 0x4CB90;
constexpr uintptr_t kAsyncDecodeCallbackRva = 0x713D0;

constexpr uintptr_t kResourceRecordBaseRva = 0xFC030;
constexpr uintptr_t kFileSystemInterfaceRva = 0xFAD28;
constexpr uintptr_t kPackStoreRva = 0x20FC640;
constexpr uintptr_t kWorkItemIndicesRva = 0x20FC648;
constexpr uintptr_t kPrecacheModeRva = 0x20FC650;
constexpr uintptr_t kOutstandingWorkCountRva = 0x20FC678;

constexpr DWORD kExpectedTimeDateStamp = 0x54874230;
constexpr DWORD kExpectedSizeOfImage = 0x2116000;

// The pack-store teardown at this RVA aborts pending work-thread tasks, polls
// until the work-thread pool reports them done, then frees the pack store via
// the memalloc Free vtable. The poll is counter-based and may pump a worker or
// invoke teardown inline. Hold a shared lock across the outer worker and make
// ordinary teardown exclusive. A teardown invoked reentrantly is deferred until
// the outer worker exits; running it immediately would free the store under the
// same thread that is still using its entry slot.
constexpr uintptr_t kPackStoreDrainRva = 0x71C80;
constexpr uintptr_t kPackStoreFreeRva = 0x386F0;
SRWLOCK s_PrecacheWorkerLock = SRWLOCK_INIT;
thread_local unsigned int s_PrecacheWorkerDepth = 0;
thread_local bool s_DeferredPackStoreDrain = false;
thread_local bool s_DeferredPackStoreFree = false;
thread_local uintptr_t s_DeferredPackStoreFreePointer = 0;
// True while this thread holds the exclusive lock and is running the original
// drain/free; the free guard passes through in that window to avoid deadlock.
thread_local bool s_PrecacheExclusiveHeld = false;
using PackStoreDrainFn = void(__fastcall*)();
PackStoreDrainFn s_PackStoreDrainOriginal = nullptr;
using PackStoreFreeFn = void(__fastcall*)(void* store);
PackStoreFreeFn s_PackStoreFreeOriginal = nullptr;

uintptr_t s_FilesystemBase;
bool s_AsyncPrecacheFixInstalled;

struct ResourceRecord
{
	const char* directory;
	const char* filename;
	const char* extension;
	uintptr_t unknown18;
	uintptr_t asyncReadArgument;
	std::array<std::byte, 40> unknown28;
};

static_assert(sizeof(ResourceRecord) == 80);
static_assert(offsetof(ResourceRecord, extension) == 0x10);
static_assert(offsetof(ResourceRecord, asyncReadArgument) == 0x20);

struct ResourceHandler
{
	const char* extension;
	uintptr_t unknown08;
	uintptr_t unknown10;
	uintptr_t decode;
	uintptr_t finish;
	uintptr_t consumeLoadedEntry;
};

static_assert(sizeof(ResourceHandler) == 0x30);
static_assert(offsetof(ResourceHandler, decode) == 0x18);
static_assert(offsetof(ResourceHandler, finish) == 0x20);
static_assert(offsetof(ResourceHandler, consumeLoadedEntry) == 0x28);

struct AsyncRequestStorage
{
	int32_t state;
	std::array<std::byte, 0x3C> opaque;
	uint8_t tail;
	std::array<std::byte, 0x0F> padding;
};

static_assert(sizeof(AsyncRequestStorage) == 0x50);
static_assert(offsetof(AsyncRequestStorage, tail) == 0x40);

struct AsyncPrecacheContext
{
	uint8_t mode;
	std::array<std::byte, 7> padding;
	uintptr_t* entrySlot;
	ResourceRecord* record;
	uintptr_t decodedResource;
	ResourceHandler* handler;
	int32_t* request;
};

static_assert(sizeof(AsyncPrecacheContext) == 0x30);
static_assert(offsetof(AsyncPrecacheContext, entrySlot) == 0x08);
static_assert(offsetof(AsyncPrecacheContext, record) == 0x10);
static_assert(offsetof(AsyncPrecacheContext, decodedResource) == 0x18);
static_assert(offsetof(AsyncPrecacheContext, handler) == 0x20);
static_assert(offsetof(AsyncPrecacheContext, request) == 0x28);

using FindResourceHandlerFn = ResourceHandler* (__fastcall*)(ResourceRecord* record);
using FindPackEntrySlotFn = uintptr_t* (__fastcall*)(void* packStore, const char* path);
using CompareResourceExtensionFn = int(__fastcall*)(const char* lhs, const char* rhs);
using FormatPathFn = int(__cdecl*)(char* destination, size_t capacity, const char* format, ...);
using IsEntryReadyFn = bool(__fastcall*)(void* entry);
using ConsumeLoadedEntryFn = uintptr_t(__fastcall*)(uintptr_t* entrySlot, void* packStore);
using FinishFn = uintptr_t(__fastcall*)(AsyncPrecacheContext* context);
using AsyncDecodeCallbackFn = void(__fastcall*)(AsyncPrecacheContext* context);
using PrepareAsyncRequestFn = void(__fastcall*)(
	void* fileSystem,
	void* packStore,
	uint32_t recordIndex,
	int32_t* request);
using GetAsyncQueueFn = uintptr_t(__fastcall*)(void* fileSystem, void* packStore);
using SubmitAsyncRequestFn = uintptr_t(__fastcall*)(
	void* fileSystem,
	void* packStore,
	uintptr_t queue,
	int32_t* request,
	uintptr_t asyncReadArgument,
	AsyncPrecacheContext* context,
	AsyncDecodeCallbackFn callback);

template <typename Function>
Function ModuleFunction(uintptr_t rva)
{
	return reinterpret_cast<Function>(s_FilesystemBase + rva);
}

template <typename Function>
Function VirtualFunction(void* object, size_t byteOffset)
{
	auto* const vtable = *reinterpret_cast<uintptr_t**>(object);
	return reinterpret_cast<Function>(
		vtable[byteOffset / sizeof(uintptr_t)]);
}

bool IsExpectedR1ClientFileSystem(uintptr_t filesystemBase)
{
	if (!filesystemBase)
		return false;

	const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(filesystemBase);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
		return false;

	const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
		filesystemBase + static_cast<uintptr_t>(dos->e_lfanew));
	return nt->Signature == IMAGE_NT_SIGNATURE
		&& nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64
		&& nt->FileHeader.TimeDateStamp == kExpectedTimeDateStamp
		&& nt->OptionalHeader.SizeOfImage == kExpectedSizeOfImage;
}

uintptr_t RunAsyncPrecacheTask(uint32_t callbackIndex)
{
	auto* const workItemIndices = *reinterpret_cast<uint32_t**>(
		s_FilesystemBase + kWorkItemIndicesRva);
	const uint32_t recordIndex = workItemIndices[callbackIndex];

	auto* const recordBase = *reinterpret_cast<ResourceRecord**>(
		s_FilesystemBase + kResourceRecordBaseRva);
	ResourceRecord* const record = recordBase + recordIndex;
	ResourceHandler* const handler =
		ModuleFunction<FindResourceHandlerFn>(kFindResourceHandlerRva)(record);
	if (!handler)
		return 0;

	void* const packStore = *reinterpret_cast<void**>(
		s_FilesystemBase + kPackStoreRva);
	auto* const packRecords = *reinterpret_cast<ResourceRecord**>(
		reinterpret_cast<uintptr_t>(packStore) + 0x240);
	const ResourceRecord& packRecord = packRecords[recordIndex];

	char path[0x104];
	ModuleFunction<FormatPathFn>(kFormatPathRva)(
		path,
		sizeof(path),
		"%s/%s.%s",
		packRecord.directory,
		packRecord.filename,
		packRecord.extension);
	path[sizeof(path) - 1] = '\0';

	// The retail function conditionally moves the lookup result over an
	// uninitialized stack qword. On a miss it dereferences that indeterminate
	// pointer. This local slot is the intended null-entry fallback and is the
	// only semantic difference from the shipped callback.
	uintptr_t missingEntry = 0;
	uintptr_t* entrySlot = &missingEntry;
	if (uintptr_t* const found =
		ModuleFunction<FindPackEntrySlotFn>(kFindPackEntrySlotRva)(packStore, path)) {
		entrySlot = found;
	}

	uintptr_t result = 0;
	if (*entrySlot) {
		result = static_cast<uintptr_t>(
			ModuleFunction<CompareResourceExtensionFn>(
				kCompareResourceExtensionRva)(record->extension, "vtf"));
		if (result != 0) {
			void* const entry = reinterpret_cast<void*>(*entrySlot);

			// Diagnostic (bounded): if *entrySlot is no longer a filesystem pack
			// entry, its vtable points into a foreign module (e.g. datacache).
			// This detects the stale-store/type-confusion window before the
			// IsEntryReady dispatch that produces the datacache DEP crash.
			static std::atomic<int> s_entryDiagBudget{ 16 };
			if (s_entryDiagBudget > 0) {
				const uintptr_t entryVtbl = *reinterpret_cast<uintptr_t*>(entry);
				const HMODULE datacache = GetModuleHandleA("datacache.dll");
				const uintptr_t dcBase = datacache ? reinterpret_cast<uintptr_t>(datacache) : 0;
				const uintptr_t fsBase = s_FilesystemBase;
				const bool foreign = dcBase &&
					entryVtbl >= dcBase && entryVtbl < dcBase + 0x100000;
				if (foreign || s_entryDiagBudget > 8) {
					--s_entryDiagBudget;
					char buffer[320];
					_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
						"R1Delta: vpk worker entry diag entry=%p vtbl=0x%llx packStore=%p "
						"fs=0x%llx dc=0x%llx path=%s foreign=%d\n",
						entry,
						static_cast<unsigned long long>(entryVtbl),
						packStore,
						static_cast<unsigned long long>(fsBase),
						static_cast<unsigned long long>(dcBase),
						path,
						foreign ? 1 : 0);
					OutputDebugStringA(buffer);
				}
			}

			const bool ready = VirtualFunction<IsEntryReadyFn>(entry, 0x58)(entry);
			result = ready ? 1 : 0;
			if (ready) {
				result = reinterpret_cast<ConsumeLoadedEntryFn>(
					handler->consumeLoadedEntry)(entrySlot, packStore);
			}
		}

		if (*entrySlot)
			return result;
	}

	void* const fileSystem = *reinterpret_cast<void**>(
		s_FilesystemBase + kFileSystemInterfaceRva);
	alignas(16) AsyncRequestStorage request;
	request.state = -1;
	request.tail = 0;

	VirtualFunction<PrepareAsyncRequestFn>(fileSystem, 0x380)(
		fileSystem,
		packStore,
		recordIndex,
		&request.state);

	AsyncPrecacheContext context{};
	context.mode = *reinterpret_cast<uint8_t*>(
		s_FilesystemBase + kPrecacheModeRva);
	context.entrySlot = entrySlot;
	context.record = record;
	context.handler = handler;
	context.request = &request.state;

	const uintptr_t queue =
		VirtualFunction<GetAsyncQueueFn>(fileSystem, 0x378)(fileSystem, packStore);
	VirtualFunction<SubmitAsyncRequestFn>(fileSystem, 0x370)(
		fileSystem,
		packStore,
		queue,
		&request.state,
		record->asyncReadArgument,
		&context,
		ModuleFunction<AsyncDecodeCallbackFn>(kAsyncDecodeCallbackRva));

	return reinterpret_cast<FinishFn>(handler->finish)(&context);
}

void __fastcall R1ClientVPKPackStoreFreeGuard(void* store)
{
	// The start-precache path (fs+0x74F30) frees the previous pack store
	// directly through the memalloc Free wrapper when the outstanding-task
	// counter reads zero, bypassing the drain hook. A worker that is mid-task
	// (shared lock held) on another thread can therefore have its pack store
	// freed out from under it. Serialize any store free against workers the
	// same way the drain is serialized: exclusive lock, or defer when the free
	// is invoked reentrantly from inside a worker.
	if (!s_PackStoreFreeOriginal)
		return;

	const uintptr_t workItemIndices = *reinterpret_cast<uintptr_t*>(
		s_FilesystemBase + kWorkItemIndicesRva);
	if (reinterpret_cast<uintptr_t>(store) != workItemIndices) {
		// Not the pack store: this is an unrelated allocation through the
		// generic Free wrapper; pass through untouched.
		s_PackStoreFreeOriginal(store);
		return;
	}

	if (s_PrecacheExclusiveHeld) {
		// This thread is already inside the drain's original free (the drain
		// frees the store internally while holding our exclusive lock).
		s_PackStoreFreeOriginal(store);
		return;
	}

	static std::atomic<int> s_guardDiagBudget{ 8 };
	if (s_guardDiagBudget > 0) {
		--s_guardDiagBudget;
		char buffer[256];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
			"R1Delta: vpk free guard intercept store=%p depth=%u defer=%d\n",
			store, s_PrecacheWorkerDepth, s_PrecacheWorkerDepth != 0 ? 1 : 0);
		OutputDebugStringA(buffer);
	}

	if (s_PrecacheWorkerDepth != 0) {
		s_DeferredPackStoreFree = true;
		s_DeferredPackStoreFreePointer = reinterpret_cast<uintptr_t>(store);
		return;
	}

	AcquireSRWLockExclusive(&s_PrecacheWorkerLock);
	s_PrecacheExclusiveHeld = true;
	s_PackStoreFreeOriginal(store);
	s_PrecacheExclusiveHeld = false;
	ReleaseSRWLockExclusive(&s_PrecacheWorkerLock);
}

void RunDeferredPackStoreFree()
{
	// A free deferred because it was invoked reentrantly inside a worker runs
	// after the outer worker has fully stopped using the pack store, serialized
	// against every other worker. Caller holds the exclusive lock. The pointer
	// is the value captured at defer time: re-reading the global could free a
	// newer store if start-precache has already replaced it.
	if (!s_DeferredPackStoreFree)
		return;
	s_DeferredPackStoreFree = false;
	const uintptr_t store = s_DeferredPackStoreFreePointer;
	s_DeferredPackStoreFreePointer = 0;
	if (s_PackStoreFreeOriginal && store)
		s_PackStoreFreeOriginal(reinterpret_cast<void*>(store));
}

uintptr_t __fastcall R1ClientVPKAsyncPrecacheWorker(
	void* /*taskContext*/,
	uint32_t callbackIndex)
{
	// The work queue can pump another callback inline. The outermost worker
	// owns the shared lock; nested workers inherit that protection.
	const bool outermost = s_PrecacheWorkerDepth++ == 0;
	if (outermost)
		AcquireSRWLockShared(&s_PrecacheWorkerLock);

	const uintptr_t result = RunAsyncPrecacheTask(callbackIndex);
	_InterlockedDecrement(reinterpret_cast<volatile long*>(
		s_FilesystemBase + kOutstandingWorkCountRva));

	if (--s_PrecacheWorkerDepth == 0) {
		ReleaseSRWLockShared(&s_PrecacheWorkerLock);

		// A drain invoked inline cannot take the exclusive lock without
		// deadlocking against this thread's shared lock. Do not run it
		// unprotected: defer it until the outer worker has fully stopped using
		// the pack store, then serialize it against every other worker.
		if (s_DeferredPackStoreDrain || s_DeferredPackStoreFree) {
			const bool hadDeferredDrain = s_DeferredPackStoreDrain;
			s_DeferredPackStoreDrain = false;
			AcquireSRWLockExclusive(&s_PrecacheWorkerLock);
			s_PrecacheExclusiveHeld = true;
			if (hadDeferredDrain && s_PackStoreDrainOriginal)
				s_PackStoreDrainOriginal();
			// The drain tears down the store itself (including the deferred
			// free's pointer), so a deferred free is redundant once the drain
			// has run. Run it only when no drain was deferred.
			if (!hadDeferredDrain)
				RunDeferredPackStoreFree();
			s_PrecacheExclusiveHeld = false;
			ReleaseSRWLockExclusive(&s_PrecacheWorkerLock);
		}
	}
	return result;
}

void __fastcall R1ClientVPKPackStoreDrainHook()
{
	if (s_PrecacheWorkerDepth != 0) {
		s_DeferredPackStoreDrain = true;
		return;
	}

	AcquireSRWLockExclusive(&s_PrecacheWorkerLock);

	// The engine's original drain polls the work-thread pool, but our worker
	// executes the task callback synchronously inside that pump, so the poll can
	// conclude "done" while a queued task still references the pack store. Wait
	// for the outstanding-task counter to reach zero before freeing the store:
	// with the exclusive lock held, no worker is mid-task, so any non-zero count
	// is a queued-but-not-started task that must finish first. Bounded: a hung
	// counter must not deadlock the drain forever.
	auto* const outstanding = reinterpret_cast<volatile long*>(
		s_FilesystemBase + kOutstandingWorkCountRva);
	for (int spin = 0; spin < 200000 && *outstanding != 0; ++spin) {
		YieldProcessor();
	}
	if (*outstanding != 0) {
		char buffer[160];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
			"R1Delta: vpk pack-store drain timed out waiting for %ld outstanding tasks\n",
			*outstanding);
		OutputDebugStringA(buffer);
	}

	s_PrecacheExclusiveHeld = true;
	if (s_PackStoreDrainOriginal)
		s_PackStoreDrainOriginal();
	s_PrecacheExclusiveHeld = false;
	ReleaseSRWLockExclusive(&s_PrecacheWorkerLock);
}
}

bool InstallR1ClientVPKAsyncPrecacheFix(uintptr_t filesystemBase)
{
	if (s_AsyncPrecacheFixInstalled)
		return true;
	if (!filesystemBase || IsDedicatedServer())
		return false;
	if (!IsExpectedR1ClientFileSystem(filesystemBase)) {
		Warning(
			"R1Delta: VPK async-precache fix skipped; filesystem_stdio.dll is not the expected R1 client image\n");
		return false;
	}

	void* const target = reinterpret_cast<void*>(
		filesystemBase + kAsyncPrecacheWorkerRva);
	constexpr unsigned char expectedPrologue[] = {
		0x48, 0x89, 0x6C, 0x24, 0x10,
		0x48, 0x89, 0x74, 0x24, 0x18,
		0x48, 0x89, 0x7C, 0x24, 0x20,
		0x41, 0x54,
		0x48, 0x81, 0xEC, 0xD0, 0x01, 0x00, 0x00
	};
	if (memcmp(target, expectedPrologue, sizeof(expectedPrologue)) != 0) {
		Warning(
			"R1Delta: VPK async-precache fix skipped; unexpected worker prologue at %p\n",
			target);
		return false;
	}

	s_FilesystemBase = filesystemBase;
	const MH_STATUS createStatus = MH_CreateHook(
		target,
		&R1ClientVPKAsyncPrecacheWorker,
		nullptr);
	if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED) {
		Warning(
			"R1Delta: VPK async-precache hook creation failed status=%d\n",
			static_cast<int>(createStatus));
		return false;
	}

	const MH_STATUS enableStatus = MH_EnableHook(target);
	if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
		Warning(
			"R1Delta: VPK async-precache hook enable failed status=%d\n",
			static_cast<int>(enableStatus));
		return false;
	}

	s_AsyncPrecacheFixInstalled = true;
	OutputDebugStringA(
		"R1Delta: R1 client VPK async-precache worker replacement installed and enabled\n");

	// Install the pack-store teardown barrier so the store cannot be freed
	// while our worker is reading it.
	void* const drainTarget = reinterpret_cast<void*>(
		filesystemBase + kPackStoreDrainRva);
	constexpr unsigned char expectedDrainPrologue[] = {
		0x48, 0x83, 0xEC, 0x28 // sub rsp, 28h
	};
	if (memcmp(drainTarget, expectedDrainPrologue, sizeof(expectedDrainPrologue)) != 0) {
		Warning(
			"R1Delta: VPK pack-store drain barrier skipped; unexpected drain prologue at %p\n",
			drainTarget);
		return true;
	}
	const MH_STATUS drainCreateStatus = MH_CreateHook(
		drainTarget,
		&R1ClientVPKPackStoreDrainHook,
		reinterpret_cast<LPVOID*>(&s_PackStoreDrainOriginal));
	if (drainCreateStatus != MH_OK && drainCreateStatus != MH_ERROR_ALREADY_CREATED) {
		Warning(
			"R1Delta: VPK pack-store drain barrier creation failed status=%d\n",
			static_cast<int>(drainCreateStatus));
		return true;
	}
	const MH_STATUS drainEnableStatus = MH_EnableHook(drainTarget);
	if (drainEnableStatus != MH_OK && drainEnableStatus != MH_ERROR_ENABLED) {
		Warning(
			"R1Delta: VPK pack-store drain barrier enable failed status=%d\n",
			static_cast<int>(drainEnableStatus));
	}
	else {
		OutputDebugStringA(
			"R1Delta: R1 client VPK pack-store drain barrier installed and enabled\n");
	}

	// The start-precache path frees the previous pack store directly through
	// the memalloc Free wrapper when the outstanding-task counter reads zero,
	// bypassing the drain barrier. Serialize those frees against workers too.
	void* const freeTarget = reinterpret_cast<void*>(
		filesystemBase + kPackStoreFreeRva);
	constexpr unsigned char expectedFreePrologue[] = {
		0x40,             // rex (push rbx)
		0x53,             // push rbx
		0x48, 0x83, 0xEC, 0x20 // sub rsp, 20h
	};
	if (memcmp(freeTarget, expectedFreePrologue, sizeof(expectedFreePrologue)) != 0) {
		Warning(
			"R1Delta: VPK pack-store free guard skipped; unexpected free prologue at %p\n",
			freeTarget);
		return true;
	}
	const MH_STATUS freeCreateStatus = MH_CreateHook(
		freeTarget,
		&R1ClientVPKPackStoreFreeGuard,
		reinterpret_cast<LPVOID*>(&s_PackStoreFreeOriginal));
	if (freeCreateStatus != MH_OK && freeCreateStatus != MH_ERROR_ALREADY_CREATED) {
		Warning(
			"R1Delta: VPK pack-store free guard creation failed status=%d\n",
			static_cast<int>(freeCreateStatus));
		return true;
	}
	const MH_STATUS freeEnableStatus = MH_EnableHook(freeTarget);
	if (freeEnableStatus != MH_OK && freeEnableStatus != MH_ERROR_ENABLED) {
		Warning(
			"R1Delta: VPK pack-store free guard enable failed status=%d\n",
			static_cast<int>(freeEnableStatus));
	}
	else {
		OutputDebugStringA(
			"R1Delta: R1 client VPK pack-store free guard installed and enabled\n");
	}

	return true;
}
