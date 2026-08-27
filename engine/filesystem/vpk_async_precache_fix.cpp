#include "vpk_async_precache_fix.h"
#include "vpk_async_precache_entry.h"

#include "engine/core/core.h"
#include "engine/logging/logging.h"

#include <MinHook.h>
#include <Windows.h>
#include <intrin.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace
{
using namespace r1delta::vpk_async_precache;

std::uintptr_t s_filesystemBase{};
bool s_asyncPrecacheFixInstalled{};
bool s_workerHookCreated{};
std::mutex s_installMutex;

using FindResourceHandlerFn = ResourceHandler* (__fastcall*)(ResourceRecord* record);
using FindPackEntryNodeFn = ResourceEntryNode* (__fastcall*)(void* packStore, const char* path);
using CompareResourceExtensionFn = int(__fastcall*)(const char* lhs, const char* rhs);
using FormatPathFn = int(__cdecl*)(char* destination, std::size_t capacity, const char* format, ...);
using IsEntryReadyFn = bool(__fastcall*)(void* entry);
using ConsumeLoadedEntryFn = std::uintptr_t(__fastcall*)(ResourceEntryNode* entryNode, void* packStore);
using FinishFn = std::uintptr_t(__fastcall*)(AsyncPrecacheContext* context);
using AsyncDecodeCallbackFn = void(__fastcall*)(AsyncPrecacheContext* context);
using PrepareAsyncRequestFn = void(__fastcall*)(
	void* fileSystem,
	void* packStore,
	std::uint32_t recordIndex,
	std::int32_t* request);
using GetAsyncQueueFn = std::uintptr_t(__fastcall*)(void* fileSystem, void* packStore);
using SubmitAsyncRequestFn = std::uintptr_t(__fastcall*)(
	void* fileSystem,
	void* packStore,
	std::uintptr_t queue,
	std::int32_t* request,
	std::uintptr_t asyncReadArgument,
	AsyncPrecacheContext* context,
	AsyncDecodeCallbackFn callback);

template <typename Function>
Function ModuleFunction(std::uintptr_t rva) noexcept
{
	return reinterpret_cast<Function>(s_filesystemBase + rva);
}

template <typename Function>
Function VirtualFunction(void* object, std::size_t byteOffset) noexcept
{
	auto* const vtable = *reinterpret_cast<std::uintptr_t**>(object);
	return reinterpret_cast<Function>(vtable[byteOffset / sizeof(std::uintptr_t)]);
}

class ResourceEntryNodeLock
{
public:
	explicit ResourceEntryNodeLock(ResourceEntryNode* node) noexcept
		: node_(node)
	{
		while (_InterlockedCompareExchange(&node_->lock, 1, 0) != 0)
			YieldProcessor();
	}

	~ResourceEntryNodeLock()
	{
		_InterlockedExchange(&node_->lock, 0);
	}

	ResourceEntryNodeLock(const ResourceEntryNodeLock&) = delete;
	ResourceEntryNodeLock& operator=(const ResourceEntryNodeLock&) = delete;

private:
	ResourceEntryNode* node_;
};

bool IsExecutableProtection(DWORD protection) noexcept
{
	if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
		return false;
	switch (protection & 0xFF) {
	case PAGE_EXECUTE:
	case PAGE_EXECUTE_READ:
	case PAGE_EXECUTE_READWRITE:
	case PAGE_EXECUTE_WRITECOPY:
		return true;
	default:
		return false;
	}
}

bool IsExpectedR1ClientFileSystem(std::uintptr_t filesystemBase) noexcept
{
	if (!filesystemBase)
		return false;
	const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(filesystemBase);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
		return false;
	const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
		filesystemBase + static_cast<std::uintptr_t>(dos->e_lfanew));
	return nt->Signature == IMAGE_NT_SIGNATURE
		&& nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64
		&& nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC
		&& nt->FileHeader.TimeDateStamp == kExpectedTimeDateStamp
		&& nt->OptionalHeader.SizeOfImage == kExpectedSizeOfImage;
}

bool IsExactExecutableTarget(
	std::uintptr_t filesystemBase,
	const void* target,
	std::size_t length) noexcept
{
	const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(target);
	if (address < filesystemBase)
		return false;
	const std::uintptr_t offset = address - filesystemBase;
	if (offset > kExpectedSizeOfImage || length > kExpectedSizeOfImage - offset)
		return false;
	MEMORY_BASIC_INFORMATION memory{};
	if (!VirtualQuery(target, &memory, sizeof(memory))
		|| memory.State != MEM_COMMIT
		|| memory.Type != MEM_IMAGE
		|| memory.AllocationBase != reinterpret_cast<const void*>(filesystemBase)
		|| !IsExecutableProtection(memory.Protect)) {
		return false;
	}
	const std::uintptr_t regionBase =
		reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
	return address >= regionBase
		&& address - regionBase <= memory.RegionSize
		&& length <= memory.RegionSize - (address - regionBase);
}

AsyncPrecacheGeneration ReadGeneration() noexcept
{
	return {
		*reinterpret_cast<void* volatile*>(s_filesystemBase + kPackStoreRva),
		*reinterpret_cast<void* volatile*>(s_filesystemBase + kWorkItemIndicesRva),
		*reinterpret_cast<void* volatile*>(s_filesystemBase + kResourceRecordBaseRva),
	};
}

void EmitInvalidEntryBreadcrumb(
	std::uintptr_t entry,
	const AsyncPrecacheEntryInspection& inspection,
	const char* path) noexcept
{
	static std::atomic<int> diagnosticBudget{ 16 };
	if (diagnosticBudget.fetch_sub(1, std::memory_order_relaxed) <= 0)
		return;
	char buffer[512];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: fatal VPK async-precache entry invariant entry=%p vtable=0x%llx "
		"leading=0x%llx readiness=0x%llx path=%s\n",
		reinterpret_cast<void*>(entry),
		static_cast<unsigned long long>(inspection.vtable),
		static_cast<unsigned long long>(inspection.leadingTarget),
		static_cast<unsigned long long>(inspection.readinessTarget),
		path);
	OutputDebugStringA(buffer);
}

struct RunOneResult
{
	AsyncPrecacheCallbackDisposition disposition{
		AsyncPrecacheCallbackDisposition::Executed };
	std::uintptr_t value{};
};

RunOneResult RunOne(std::uint32_t callbackIndex, bool nested)
{
	void* const workItemIndicesPointer = *reinterpret_cast<void* volatile*>(
		s_filesystemBase + kWorkItemIndicesRva);
	if (!workItemIndicesPointer) {
		Error("R1Delta: VPK async-precache work-index generation is null\n");
		return {};
	}
	const auto* const workItemIndices =
		static_cast<const std::uint32_t*>(workItemIndicesPointer);
	const std::uint32_t recordIndex = workItemIndices[callbackIndex];

	void* const recordBasePointer = *reinterpret_cast<void* volatile*>(
		s_filesystemBase + kResourceRecordBaseRva);
	if (!recordBasePointer) {
		Error("R1Delta: VPK async-precache record generation is null\n");
		return {};
	}
	auto* const recordBase = static_cast<ResourceRecord*>(recordBasePointer);
	ResourceRecord* const record = recordBase + recordIndex;
	ResourceHandler* const handler =
		ModuleFunction<FindResourceHandlerFn>(kFindResourceHandlerRva)(record);
	if (!handler)
		return {};

	void* const packStore = *reinterpret_cast<void* volatile*>(
		s_filesystemBase + kPackStoreRva);
	if (!packStore) {
		Error("R1Delta: VPK async-precache pack-store generation is null\n");
		return {};
	}
	const AsyncPrecacheGeneration generation{
		packStore,
		workItemIndicesPointer,
		recordBasePointer,
	};
	auto* const packRecords = *reinterpret_cast<ResourceRecord**>(
		reinterpret_cast<std::uintptr_t>(packStore) + kPackRecordsOffset);
	if (!packRecords) {
		Error("R1Delta: VPK async-precache pack-store record table is null\n");
		return {};
	}
	const ResourceRecord& packRecord = packRecords[recordIndex];

	char path[kCanonicalPathCapacity];
	ModuleFunction<FormatPathFn>(kFormatPathRva)(
		path,
		sizeof(path),
		"%s/%s.%s",
		packRecord.directory,
		packRecord.filename,
		packRecord.extension);
	path[sizeof(path) - 1] = '\0';

	const AsyncPrecacheClaimKey claimKey = AsyncPrecacheClaimKey::FromPath(
		generation,
		std::string_view(path, std::strlen(path)));
	AsyncPrecacheEntryClaim claim(claimKey, nested);
	if (!claim.Acquired())
		return { AsyncPrecacheCallbackDisposition::Deferred, 0 };

	// The path claim closes lookup/replacement races without holding the native
	// node lock across readiness or handler callbacks that reacquire that lock.
	if (!(ReadGeneration() == generation)) {
		Error(
			"R1Delta: VPK async-precache generation changed while its path was claimed: %s\n",
			path);
		__fastfail(FAST_FAIL_FATAL_APP_EXIT);
	}

	ResourceEntryNode missingNode{};
	ResourceEntryNode* const sharedNode =
		ModuleFunction<FindPackEntryNodeFn>(kFindPackEntrySlotRva)(packStore, path);
	if (!(ReadGeneration() == generation)) {
		Error(
			"R1Delta: VPK async-precache generation changed during lookup: %s\n",
			path);
		__fastfail(FAST_FAIL_FATAL_APP_EXIT);
	}
	ResourceEntryNode* const node = sharedNode ? sharedNode : &missingNode;

	std::uintptr_t existingEntry{};
	AsyncPrecacheEntryInspection inspection;
	const int extensionComparison =
		ModuleFunction<CompareResourceExtensionFn>(
			kCompareResourceExtensionRva)(record->extension, "vtf");
	{
		ResourceEntryNodeLock nodeLock(node);
		existingEntry = node->entry;
		if (existingEntry && extensionComparison != 0) {
			// Native finish/consume also owns node +0x0C. Inspect the object and
			// vtable while holding that lock so a concurrent finish cannot clear
			// and release the value between the slot snapshot and classification.
			inspection = InspectAsyncPrecacheEntry(
				reinterpret_cast<const void*>(existingEntry),
				{ s_filesystemBase, kExpectedSizeOfImage });
		}
	}

	std::uintptr_t result{};
	if (existingEntry) {
		result = static_cast<std::uintptr_t>(extensionComparison);
		if (extensionComparison != 0) {
			bool ready = false;
			if (inspection.kind == AsyncPrecacheEntryKind::Pending) {
				ready = reinterpret_cast<IsEntryReadyFn>(
					inspection.readinessTarget)(
						reinterpret_cast<void*>(existingEntry));
				result = ready ? 1 : 0;
			}
			else if (inspection.kind == AsyncPrecacheEntryKind::CompletedValue) {
				ready = true;
				result = 1;
			}
			else {
				EmitInvalidEntryBreadcrumb(existingEntry, inspection, path);
				Error(
					"R1Delta: invalid occupied VPK async-precache entry for %s "
					"(entry=%p vtable=0x%llx)\n",
					path,
					reinterpret_cast<void*>(existingEntry),
					static_cast<unsigned long long>(inspection.vtable));
				__fastfail(FAST_FAIL_FATAL_APP_EXIT);
			}

			if (!(ReadGeneration() == generation)) {
				Error(
					"R1Delta: VPK async-precache generation changed after readiness: %s\n",
					path);
				__fastfail(FAST_FAIL_FATAL_APP_EXIT);
			}
			if (ready) {
				result = reinterpret_cast<ConsumeLoadedEntryFn>(
					handler->consumeLoadedEntry)(node, packStore);
			}
		}
	}

	if (!(ReadGeneration() == generation)) {
		Error(
			"R1Delta: VPK async-precache generation changed before empty recheck: %s\n",
			path);
		__fastfail(FAST_FAIL_FATAL_APP_EXIT);
	}

	{
		ResourceEntryNodeLock nodeLock(node);
		if (node->entry)
			return { AsyncPrecacheCallbackDisposition::Executed, result };
	}

	// CachedRead can pump another work item. Drop the short ownership claim
	// before prepare/submit/decode/finish so cross-path callbacks cannot cycle.
	claim.Release();

	void* const fileSystem = *reinterpret_cast<void* volatile*>(
		s_filesystemBase + kFileSystemInterfaceRva);
	if (!fileSystem) {
		Error("R1Delta: VPK async-precache filesystem interface is null\n");
		return {};
	}
	alignas(16) AsyncRequestStorage request;
	request.state = -1;
	request.tail = 0;

	VirtualFunction<PrepareAsyncRequestFn>(
		fileSystem,
		kPrepareAsyncRequestVtableOffset)(
		fileSystem,
		packStore,
		recordIndex,
		&request.state);

	AsyncPrecacheContext context{};
	context.mode = *reinterpret_cast<volatile std::uint8_t*>(
		s_filesystemBase + kPrecacheModeRva);
	context.entryNode = node;
	context.record = record;
	context.handler = handler;
	context.request = &request.state;

	const std::uintptr_t queue =
		VirtualFunction<GetAsyncQueueFn>(
			fileSystem,
			kGetAsyncQueueVtableOffset)(fileSystem, packStore);
	VirtualFunction<SubmitAsyncRequestFn>(
		fileSystem,
		kSubmitAsyncRequestVtableOffset)(
		fileSystem,
		packStore,
		queue,
		&request.state,
		record->asyncReadArgument,
		&context,
		ModuleFunction<AsyncDecodeCallbackFn>(kAsyncDecodeCallbackRva));

	return {
		AsyncPrecacheCallbackDisposition::Executed,
		reinterpret_cast<FinishFn>(handler->finish)(&context)
	};
}

struct WorkerThreadState
{
	unsigned int depth{};
	AsyncPrecacheDeferredCallbacks deferred;
};
thread_local WorkerThreadState s_workerThreadState;

void DecrementOutstanding() noexcept
{
	_InterlockedDecrement(reinterpret_cast<volatile long*>(
		s_filesystemBase + kOutstandingWorkCountRva));
}

void EnqueueDeferredOrFatal(std::uint32_t callbackIndex)
{
	if (s_workerThreadState.deferred.Enqueue(callbackIndex)
		== DeferredCallbackEnqueueResult::Overflow) {
		Error(
			"R1Delta: fatal VPK async-precache deferred callback FIFO overflow "
			"(capacity=%zu callback=%u)\n",
			kDeferredCallbackCapacity,
			callbackIndex);
		__fastfail(FAST_FAIL_FATAL_APP_EXIT);
	}
}

std::uintptr_t __fastcall R1ClientVPKAsyncPrecacheWorker(
	void* /*taskContext*/,
	std::uint32_t callbackIndex)
{
	const bool outermost = s_workerThreadState.depth++ == 0;
	const RunOneResult initial = RunOne(callbackIndex, !outermost);
	if (initial.disposition == AsyncPrecacheCallbackDisposition::Deferred)
		EnqueueDeferredOrFatal(callbackIndex);
	else
		DecrementOutstanding();

	if (outermost) {
		std::uint32_t deferredIndex{};
		while (s_workerThreadState.deferred.TryDequeue(deferredIndex)) {
			const RunOneResult replay = RunOne(deferredIndex, false);
			if (replay.disposition == AsyncPrecacheCallbackDisposition::Deferred) {
				Error(
					"R1Delta: top-level VPK async-precache replay unexpectedly deferred "
					"callback %u\n",
					deferredIndex);
				__fastfail(FAST_FAIL_FATAL_APP_EXIT);
			}
			else {
				DecrementOutstanding();
			}
		}
	}

	--s_workerThreadState.depth;
	return initial.value;
}
}

bool InstallR1ClientVPKAsyncPrecacheFix(std::uintptr_t filesystemBase)
{
	std::lock_guard<std::mutex> installLock(s_installMutex);
	if (s_asyncPrecacheFixInstalled)
		return true;
	if (HasEngineCommandLineFlag("-r1delta_disable_vpk_async_precache_fix"))
		return false;
	if (GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015)
		return false;
	if (!IsExpectedR1ClientFileSystem(filesystemBase)) {
		Warning(
			"R1Delta: VPK async-precache fix skipped; filesystem_stdio.dll is not "
			"the exact R1 retail client image\n");
		return false;
	}

	void* const target = reinterpret_cast<void*>(
		filesystemBase + kAsyncPrecacheWorkerRva);
	if (!IsExactExecutableTarget(
			filesystemBase,
			target,
			kExpectedWorkerPrologue.size())
		|| std::memcmp(
			target,
			kExpectedWorkerPrologue.data(),
			kExpectedWorkerPrologue.size()) != 0) {
		Warning(
			"R1Delta: VPK async-precache fix skipped; worker target/prologue at %p "
			"does not match the exact executable entry\n",
			target);
		return false;
	}

	s_filesystemBase = filesystemBase;
	const MH_STATUS createStatus = MH_CreateHook(
		target,
		&R1ClientVPKAsyncPrecacheWorker,
		nullptr);
	if (createStatus == MH_OK) {
		s_workerHookCreated = true;
	}
	else if (createStatus == MH_ERROR_ALREADY_CREATED && !s_workerHookCreated) {
		Warning(
			"R1Delta: VPK async-precache worker target is already owned by an "
			"unrelated hook\n");
		return false;
	}
	else if (createStatus != MH_ERROR_ALREADY_CREATED) {
		Warning(
			"R1Delta: VPK async-precache worker hook creation failed status=%d\n",
			static_cast<int>(createStatus));
		return false;
	}

	const MH_STATUS enableStatus = MH_EnableHook(target);
	if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
		Warning(
			"R1Delta: VPK async-precache worker hook enable failed status=%d\n",
			static_cast<int>(enableStatus));
		return false;
	}

	s_asyncPrecacheFixInstalled = true;
	OutputDebugStringA(
		"R1Delta: exact R1 retail VPK async-precache worker replacement installed and enabled\n");
	return true;
}
