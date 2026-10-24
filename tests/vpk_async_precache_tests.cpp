#include "../engine/filesystem/shared_particle_materials.h"
#include "../engine/filesystem/vpk_async_precache_entry.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <typeinfo>
#include <vector>

namespace
{
using namespace r1delta::vpk_async_precache;
using r1delta::shared_particle_materials::LookupResource;
using namespace std::chrono_literals;

bool Check(bool condition, const char* message)
{
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}

AsyncPrecacheModuleLayout MainModuleLayout()
{
	const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
		base + static_cast<std::uintptr_t>(dos->e_lfanew));
	return { base, nt->OptionalHeader.SizeOfImage };
}

struct PendingEntryShape
{
	bool ready{};
	virtual void Method00() {}
	virtual void Method01() {}
	virtual void Method02() {}
	virtual void Method03() {}
	virtual void Method04() {}
	virtual void Method05() {}
	virtual void Method06() {}
	virtual void Method07() {}
	virtual void Method08() {}
	virtual void Method09() {}
	virtual void Method10() {}
	virtual bool Ready() { return ready; }
};

// CStudioAnim has a short image-backed vtable. Bytes at +0x58 can be adjacent
// RTTI or another image symbol, not proof that the object owns that slot.
struct CStudioAnimShape
{
	virtual std::uintptr_t Release() { return 0x51A7; }
	std::uintptr_t studioData{};
};

std::array<std::uintptr_t, 1> imageBackedImpostorVtable{};
std::uintptr_t imageBackedImpostorObject{};

void __fastcall ImpostorLeadingMethod(void*)
{
}

bool TestConstantsAndLayouts()
{
	ResourceEntryNode miss{};
	bool fullyZero = true;
	for (std::size_t i = 0; i < sizeof(miss); ++i) {
		if (reinterpret_cast<const unsigned char*>(&miss)[i] != 0)
			fullyZero = false;
	}
	constexpr std::array<std::uint8_t, 24> expectedPrologue{
		0x48, 0x89, 0x6C, 0x24, 0x10,
		0x48, 0x89, 0x74, 0x24, 0x18,
		0x48, 0x89, 0x7C, 0x24, 0x20,
		0x41, 0x54,
		0x48, 0x81, 0xEC, 0xD0, 0x01, 0x00, 0x00
	};
	return Check(kAsyncPrecacheWorkerRva == 0x74D50, "unexpected worker RVA")
		&& Check(kFindResourceHandlerRva == 0x71350, "unexpected handler RVA")
		&& Check(kFindPackEntrySlotRva == 0x746B0, "unexpected lookup RVA")
		&& Check(kCompareResourceExtensionRva == 0x4AFD0, "unexpected compare RVA")
		&& Check(kFormatPathRva == 0x4CB90, "unexpected format RVA")
		&& Check(kAsyncDecodeCallbackRva == 0x713D0, "unexpected decode RVA")
		&& Check(kResourceRecordBaseRva == 0x0FC030, "unexpected record global")
		&& Check(kFileSystemInterfaceRva == 0x0FAD28, "unexpected filesystem global")
		&& Check(kPackStoreRva == 0x20FC640, "unexpected pack-store global")
		&& Check(kWorkItemIndicesRva == 0x20FC648, "unexpected index global")
		&& Check(kPrecacheModeRva == 0x20FC650, "unexpected mode global")
		&& Check(kOutstandingWorkCountRva == 0x20FC678, "unexpected outstanding global")
		&& Check(kExpectedTimeDateStamp == 0x54874230, "unexpected retail timestamp")
		&& Check(kExpectedSizeOfImage == 0x2116000, "unexpected retail image size")
		&& Check(kPackRecordCountOffset == 0x230, "unexpected pack count offset")
		&& Check(kPackEntryNodesOffset == 0x238, "unexpected pack node offset")
		&& Check(kPackRecordsOffset == 0x240, "unexpected pack records offset")
		&& Check(kSubmitAsyncRequestVtableOffset == 0x370, "unexpected submit slot")
		&& Check(kGetAsyncQueueVtableOffset == 0x378, "unexpected queue slot")
		&& Check(kPrepareAsyncRequestVtableOffset == 0x380, "unexpected prepare slot")
		&& Check(kReadinessMethodOffset == 0x58, "unexpected readiness slot")
		&& Check(kExpectedWorkerPrologue == expectedPrologue,
			"exact worker prologue drift")
		&& Check(sizeof(ResourceRecord) == 0x50, "resource record layout drift")
		&& Check(sizeof(ResourceHandler) == 0x30, "resource handler layout drift")
		&& Check(sizeof(ResourceEntryNode) == 0x18, "entry node layout drift")
		&& Check(sizeof(AsyncRequestStorage) == 0x50, "request layout drift")
		&& Check(sizeof(AsyncPrecacheContext) == 0x30, "context layout drift")
		&& Check(fullyZero, "miss node is not fully zero initialized");
}

bool TestPendingReadyAndNotReadyInspection()
{
	PendingEntryShape notReady{};
	PendingEntryShape ready{};
	ready.ready = true;
	const AsyncPrecacheModuleLayout layout = MainModuleLayout();
	const AsyncPrecacheEntryInspection notReadyInspection =
		InspectAsyncPrecacheEntry(&notReady, layout);
	const AsyncPrecacheEntryInspection readyInspection =
		InspectAsyncPrecacheEntry(&ready, layout);
	using ReadyFn = bool(__fastcall*)(void*);
	return Check(notReadyInspection.kind == AsyncPrecacheEntryKind::Pending,
			"not-ready pending wrapper was not recognized")
		&& Check(readyInspection.kind == AsyncPrecacheEntryKind::Pending,
			"ready pending wrapper was not recognized")
		&& Check(!reinterpret_cast<ReadyFn>(notReadyInspection.readinessTarget)(&notReady),
			"not-ready wrapper reported ready")
		&& Check(reinterpret_cast<ReadyFn>(readyInspection.readinessTarget)(&ready),
			"ready wrapper reported not ready");
}

bool TestCompletedValueAndInvalidProtections()
{
	const AsyncPrecacheModuleLayout layout = MainModuleLayout();
	CStudioAnimShape completed{};
	const AsyncPrecacheEntryInspection completedInspection =
		InspectAsyncPrecacheEntry(&completed, layout);
	PendingEntryShape pending{};
	const AsyncPrecacheEntryInspection pendingInspection =
		InspectAsyncPrecacheEntry(&pending, layout);

	std::uintptr_t impostorVtable[12]{};
	impostorVtable[0] = reinterpret_cast<std::uintptr_t>(&ImpostorLeadingMethod);
	impostorVtable[kReadinessMethodOffset / sizeof(std::uintptr_t)] =
		pendingInspection.readinessTarget;
	std::uintptr_t impostorObject = reinterpret_cast<std::uintptr_t>(impostorVtable);
	const AsyncPrecacheEntryInspection impostorInspection =
		InspectAsyncPrecacheEntry(&impostorObject, layout);

	std::uintptr_t rttiObject = reinterpret_cast<std::uintptr_t>(
		&typeid(CStudioAnimShape));
	const AsyncPrecacheEntryInspection rttiInspection =
		InspectAsyncPrecacheEntry(&rttiObject, layout);

	void* noAccess = VirtualAlloc(
		nullptr,
		4096,
		MEM_RESERVE | MEM_COMMIT,
		PAGE_NOACCESS);
	const bool protectedPageAllocated = noAccess != nullptr;
	const AsyncPrecacheEntryInspection protectedInspection =
		InspectAsyncPrecacheEntry(noAccess, layout);
	if (noAccess)
		VirtualFree(noAccess, 0, MEM_RELEASE);

	void* privateExecutable = VirtualAlloc(
		nullptr,
		4096,
		MEM_RESERVE | MEM_COMMIT,
		PAGE_EXECUTE_READWRITE);
	AsyncPrecacheEntryInspection privateExecutableInspection;
	if (privateExecutable) {
		*static_cast<unsigned char*>(privateExecutable) = 0xC3;
		imageBackedImpostorVtable[0] =
			reinterpret_cast<std::uintptr_t>(privateExecutable);
		imageBackedImpostorObject =
			reinterpret_cast<std::uintptr_t>(imageBackedImpostorVtable.data());
		privateExecutableInspection = InspectAsyncPrecacheEntry(
			&imageBackedImpostorObject,
			layout);
	}

	const bool passed = Check(
			completedInspection.kind == AsyncPrecacheEntryKind::CompletedValue,
			"short CStudioAnim-shaped value was mistaken for a pending wrapper")
		&& Check(completedInspection.leadingTarget != 0,
			"completed value has no executable leading method")
		&& Check(impostorInspection.kind == AsyncPrecacheEntryKind::Invalid,
			"heap vtable impostor with a retail readiness target was accepted")
		&& Check(rttiInspection.kind == AsyncPrecacheEntryKind::Invalid,
			"RTTI data was accepted as an object vtable")
		&& Check(protectedPageAllocated, "could not allocate PAGE_NOACCESS test object")
		&& Check(protectedInspection.kind == AsyncPrecacheEntryKind::Invalid,
			"PAGE_NOACCESS object was accepted")
		&& Check(privateExecutable != nullptr,
			"could not allocate private executable target")
		&& Check(privateExecutableInspection.kind == AsyncPrecacheEntryKind::Invalid,
			"private executable leading target was accepted as image code")
		&& Check(InspectAsyncPrecacheEntry(nullptr, layout).kind
				== AsyncPrecacheEntryKind::Invalid,
			"null entry was accepted")
		&& Check(DecideAsyncPrecacheEntryKind(true, true, false, true, false)
				== AsyncPrecacheEntryKind::Invalid,
			"non-executable completed leading slot was accepted")
		&& Check(DecideAsyncPrecacheEntryKind(true, true, false, false, true)
				== AsyncPrecacheEntryKind::Invalid,
			"non-image completed vtable was accepted");
	if (privateExecutable)
		VirtualFree(privateExecutable, 0, MEM_RELEASE);
	return passed;
}

bool TestExistingEntryDecisionTable()
{
	return Check(DecideAsyncPrecacheExistingAction(
			false, false, AsyncPrecacheEntryKind::Invalid, false)
			== AsyncPrecacheExistingAction::StartRead,
		"miss did not start a native read")
		&& Check(DecideAsyncPrecacheExistingAction(
			true, true, AsyncPrecacheEntryKind::Invalid, false)
			== AsyncPrecacheExistingAction::ReturnOccupied,
		"VTF occupied-slot behavior changed")
		&& Check(DecideAsyncPrecacheExistingAction(
			true, false, AsyncPrecacheEntryKind::Pending, false)
			== AsyncPrecacheExistingAction::ReturnOccupied,
		"not-ready pending entry was consumed")
		&& Check(DecideAsyncPrecacheExistingAction(
			true, false, AsyncPrecacheEntryKind::Pending, true)
			== AsyncPrecacheExistingAction::Consume,
		"ready pending entry bypassed slot 5")
		&& Check(DecideAsyncPrecacheExistingAction(
			true, false, AsyncPrecacheEntryKind::CompletedValue, false)
			== AsyncPrecacheExistingAction::Consume,
		"completed value did not bypass readiness into slot 5")
		&& Check(DecideAsyncPrecacheExistingAction(
			true, false, AsyncPrecacheEntryKind::Invalid, false)
			== AsyncPrecacheExistingAction::FatalInvalid,
		"invalid occupied entry did not fail closed");
}

bool TestRetailHashVectorsAndCollisionIdentity()
{
	constexpr const char* collisionA = "models/e0ym8c98ki.mdl";
	constexpr const char* collisionB = "models/lkolruncpp.mdl";
	const AsyncPrecacheGeneration generation{
		reinterpret_cast<void*>(0x1000),
		reinterpret_cast<void*>(0x2000),
		reinterpret_cast<void*>(0x3000)
	};
	const AsyncPrecacheClaimKey keyA =
		AsyncPrecacheClaimKey::FromPath(generation, collisionA);
	const AsyncPrecacheClaimKey keyB =
		AsyncPrecacheClaimKey::FromPath(generation, collisionB);
	AsyncPrecacheEntryClaim claimA(keyA, false);
	AsyncPrecacheEntryClaim samePath(keyA, true);
	AsyncPrecacheEntryClaim collidingPath(keyB, true);
	return Check(RetailVpkPathHash("") == 0x00000000, "empty retail hash drift")
		&& Check(RetailVpkPathHash("materials/models/test.vtf") == 0x7D0179B5,
			"VTF retail hash drift")
		&& Check(RetailVpkPathHash("models/humans/pilot.mdl") == 0xEEAAF1E0,
			"MDL retail hash drift")
		&& Check(RetailVpkPathHash("anims/mp/common.ani") == 0x85A87B85,
			"ANI retail hash drift")
		&& Check(RetailVpkPathHash(
			std::string_view("\x80\xFF" "A", 3)) == 0x6DF8898B,
			"high-bit signed-byte retail hash drift")
		&& Check(keyA.pathHash == 0xC5C0EF35 && keyA.pathHash == keyB.pathHash,
			"known retail hash collision drift")
		&& Check(claimA.Acquired(), "collision test failed to acquire outer claim")
		&& Check(!samePath.Acquired(), "nested exact-path duplicate was admitted")
		&& Check(collidingPath.Acquired(), "hash collision aliased distinct exact paths");
}

bool TestSameKeyEightThreadExclusion()
{
	const AsyncPrecacheGeneration generation{
		reinterpret_cast<void*>(0x1100),
		reinterpret_cast<void*>(0x2200),
		reinterpret_cast<void*>(0x3300)
	};
	const AsyncPrecacheClaimKey key =
		AsyncPrecacheClaimKey::FromPath(generation, "models/shared.mdl");
	std::atomic<bool> start{};
	std::atomic<int> active{};
	std::atomic<int> maximum{};
	std::atomic<int> completed{};
	std::vector<std::thread> threads;
	for (int i = 0; i < 8; ++i) {
		threads.emplace_back([&] {
			while (!start.load(std::memory_order_acquire))
				std::this_thread::yield();
			AsyncPrecacheEntryClaim claim(key, false);
			const int current = active.fetch_add(1) + 1;
			int observed = maximum.load();
			while (observed < current
				&& !maximum.compare_exchange_weak(observed, current)) {
			}
			std::this_thread::sleep_for(1ms);
			active.fetch_sub(1);
			completed.fetch_add(1);
		});
	}
	start.store(true, std::memory_order_release);
	for (std::thread& thread : threads)
		thread.join();
	return Check(completed == 8, "not every same-key contender completed")
		&& Check(maximum == 1, "same-key top-level claims overlapped");
}

bool TestDifferentKeyParallelism()
{
	const AsyncPrecacheGeneration generation{
		reinterpret_cast<void*>(0x1200),
		reinterpret_cast<void*>(0x2400),
		reinterpret_cast<void*>(0x3600)
	};
	const auto keyA = AsyncPrecacheClaimKey::FromPath(generation, "a/path.mdl");
	const auto keyB = AsyncPrecacheClaimKey::FromPath(generation, "b/path.mdl");
	AsyncPrecacheEntryClaim claimA(keyA, false);
	std::mutex mutex;
	std::condition_variable changed;
	bool acquiredB = false;
	bool releaseB = false;
	std::thread thread([&] {
		AsyncPrecacheEntryClaim claimB(keyB, false);
		std::unique_lock lock(mutex);
		acquiredB = claimB.Acquired();
		changed.notify_all();
		changed.wait(lock, [&] { return releaseB; });
	});
	const bool acquiredA = claimA.Acquired();
	bool acquiredWhileAHeld = false;
	{
		std::unique_lock lock(mutex);
		acquiredWhileAHeld = changed.wait_for(
			lock,
			2s,
			[&] { return acquiredB; });
		releaseB = true;
	}
	claimA.Release();
	changed.notify_all();
	thread.join();
	return Check(acquiredA, "first different-key claim failed")
		&& Check(acquiredWhileAHeld,
			"different path was serialized behind unrelated claim");
}

bool TestGenerationAddressDistinction()
{
	const AsyncPrecacheGeneration base{
		reinterpret_cast<void*>(0x1300),
		reinterpret_cast<void*>(0x2600),
		reinterpret_cast<void*>(0x3900)
	};
	AsyncPrecacheEntryClaim outer(
		AsyncPrecacheClaimKey::FromPath(base, "models/generation.mdl"),
		false);
	AsyncPrecacheGeneration packChanged = base;
	packChanged.packStore = reinterpret_cast<void*>(0x1301);
	AsyncPrecacheEntryClaim packClaim(
		AsyncPrecacheClaimKey::FromPath(packChanged, "models/generation.mdl"), true);
	AsyncPrecacheGeneration indicesChanged = base;
	indicesChanged.workItemIndices = reinterpret_cast<void*>(0x2601);
	AsyncPrecacheEntryClaim indicesClaim(
		AsyncPrecacheClaimKey::FromPath(indicesChanged, "models/generation.mdl"), true);
	AsyncPrecacheGeneration recordsChanged = base;
	recordsChanged.recordBase = reinterpret_cast<void*>(0x3901);
	AsyncPrecacheEntryClaim recordsClaim(
		AsyncPrecacheClaimKey::FromPath(recordsChanged, "models/generation.mdl"), true);
	const bool outerAcquired = outer.Acquired();
	const bool packAcquired = packClaim.Acquired();
	const bool indicesAcquired = indicesClaim.Acquired();
	const bool recordsAcquired = recordsClaim.Acquired();
	recordsClaim.Release();
	indicesClaim.Release();
	packClaim.Release();
	outer.Release();
	AsyncPrecacheEntryClaim reusedAddress(
		AsyncPrecacheClaimKey::FromPath(base, "models/generation.mdl"),
		false);
	return Check(outerAcquired, "base generation claim failed")
		&& Check(packAcquired, "pack-store generation address was ignored")
		&& Check(indicesAcquired, "work-index generation address was ignored")
		&& Check(recordsAcquired, "record-base generation address was ignored")
		&& Check(reusedAddress.Acquired(), "released generation address stayed owned");
}

bool TestNestedTryDeferAndAccountingModel()
{
	const AsyncPrecacheGeneration generation{
		reinterpret_cast<void*>(0x1400),
		reinterpret_cast<void*>(0x2800),
		reinterpret_cast<void*>(0x3C00)
	};
	const auto keyA = AsyncPrecacheClaimKey::FromPath(generation, "a.ani");
	const auto keyB = AsyncPrecacheClaimKey::FromPath(generation, "b.ani");
	const auto keyC = AsyncPrecacheClaimKey::FromPath(generation, "c.ani");

	std::mutex mutex;
	std::condition_variable changed;
	bool holderReady = false;
	bool releaseHolder = false;
	std::thread holder([&] {
		AsyncPrecacheEntryClaim held(keyB, false);
		std::unique_lock lock(mutex);
		holderReady = held.Acquired();
		changed.notify_all();
		changed.wait(lock, [&] { return releaseHolder; });
	});
	{
		std::unique_lock lock(mutex);
		changed.wait_for(lock, 2s, [&] { return holderReady; });
	}

	AsyncPrecacheEntryClaim outer(keyA, false);
	AsyncPrecacheEntryClaim sameKey(keyA, true);
	AsyncPrecacheEntryClaim contendedCrossKey(keyB, true);
	AsyncPrecacheEntryClaim freeCrossKey(keyC, true);
	AsyncPrecacheDeferredCallbacks deferred;
	const auto queued = deferred.Enqueue(77);
	int outstanding = 2;
	if (ShouldDecrementOutstanding(AsyncPrecacheCallbackDisposition::Executed))
		--outstanding;
	if (ShouldDecrementOutstanding(AsyncPrecacheCallbackDisposition::Deferred))
		--outstanding;
	std::uint32_t replayIndex{};
	const bool replayed = deferred.TryDequeue(replayIndex);
	if (replayed
		&& ShouldDecrementOutstanding(AsyncPrecacheCallbackDisposition::Executed)) {
		--outstanding;
	}

	{
		std::lock_guard lock(mutex);
		releaseHolder = true;
	}
	changed.notify_all();
	holder.join();

	return Check(outer.Acquired(), "outer claim failed")
		&& Check(!sameKey.Acquired(), "same-key nested claim did not defer")
		&& Check(!contendedCrossKey.Acquired(), "contended cross-key nested claim blocked/admitted")
		&& Check(freeCrossKey.Acquired(), "uncontended cross-key nested claim did not run")
		&& Check(queued == DeferredCallbackEnqueueResult::Enqueued,
			"nested callback was not queued")
		&& Check(replayed && replayIndex == 77, "deferred callback was not replayed FIFO")
		&& Check(outstanding == 0, "deferred callback accounting did not decrement exactly once on replay");
}

bool TestBoundedFifoOverflowContract()
{
	AsyncPrecacheDeferredCallbacks queue;
	for (std::uint32_t i = 0; i < kDeferredCallbackCapacity; ++i) {
		if (queue.Enqueue(i) != DeferredCallbackEnqueueResult::Enqueued)
			return Check(false, "bounded queue overflowed before its declared capacity");
	}
	if (!Check(queue.Size() == kDeferredCallbackCapacity, "bounded queue size drift")
		|| !Check(queue.Enqueue(0xFFFFFFFF)
			== DeferredCallbackEnqueueResult::Overflow,
			"bounded queue overflow was not reported")) {
		return false;
	}
	for (std::uint32_t expected = 0; expected < kDeferredCallbackCapacity; ++expected) {
		std::uint32_t actual{};
		if (!queue.TryDequeue(actual) || actual != expected)
			return Check(false, "deferred queue did not preserve FIFO order");
	}
	return Check(queue.Empty(), "deferred queue was not empty after replay");
}

constexpr std::array<const char*, 2> kSharedParticleMaterialPaths{
	"materials/particle/star_max01.vmt",
	"materials/particle/smoke/sniper_trail_add_ob6.vmt"
};

bool TestSharedParticleMaterialPathScope()
{
	std::array<ResourceLookup, 2> canonical{};
	for (std::size_t i = 0; i < canonical.size(); ++i) {
		if (!Check(LookupResource(kSharedParticleMaterialPaths[i], canonical[i]),
			"canonical shared particle material was not found")
			|| !Check(canonical[i].record && canonical[i].node,
				"shared particle material did not publish a native resource")) {
			return false;
		}
	}
	const struct
	{
		const char* path;
		std::size_t materialIndex;
	} variants[]{
		{ "MATERIALS/PARTICLE/STAR_MAX01.VMT", 0 },
		{ "materials\\particle\\star_max01.vmt", 0 },
		{ "MaTeRiAlS\\PaRtIcLe/StAr_MaX01.VmT", 0 },
		{ "MATERIALS/PARTICLE/SMOKE/SNIPER_TRAIL_ADD_OB6.VMT", 1 },
		{ "materials\\particle\\smoke\\sniper_trail_add_ob6.vmt", 1 },
		{ "MaTeRiAlS/PaRtIcLe\\SmOkE/SnIpEr_TrAiL_AdD_Ob6.VmT", 1 }
	};
	for (const auto& variant : variants) {
		ResourceLookup result{};
		const auto& expected = canonical[variant.materialIndex];
		if (!Check(LookupResource(variant.path, result),
			"case/slash-equivalent shared particle path was rejected")
			|| !Check(result.record == expected.record && result.node == expected.node,
				"path variant did not borrow the canonical resource")) {
			return false;
		}
	}
	const char* misses[]{
		nullptr,
		"",
		"particle/star_max01.vmt",
		"/materials/particle/star_max01.vmt",
		"./materials/particle/star_max01.vmt",
		"prefix/materials/particle/star_max01.vmt",
		"materials//particle/star_max01.vmt",
		"materials/particle/./star_max01.vmt",
		"materials/particle/smoke/../star_max01.vmt",
		"materials\\particle\\smoke\\..\\star_max01.vmt",
		"materials/particle/star_max01",
		"materials/particle/star_max01.vmt.bak",
		"materials/particle/star_max01.vmt/",
		"materials/particle/star_max01.vmt ",
		"materials/particle/star_max010.vmt",
		"materials/particle/star_max01.vtf",
		"particle/smoke/sniper_trail_add_ob6.vmt",
		"materials/particle/sniper_trail_add_ob6.vmt",
		"materials/particle/smoke/sniper_trail_add_ob6.vmtx",
		"materials/particle/smoke/sniper_trail_add_ob60.vmt",
		"materials/particle/smoke/../smoke/sniper_trail_add_ob6.vmt",
		"materials\\particle\\smoke\\..\\smoke\\sniper_trail_add_ob6.vmt",
		"materials/particle/smoke/unrelated.vmt"
	};
	for (const char* path : misses) {
		ResourceLookup result{};
		if (!Check(!LookupResource(path, result),
			"shared particle material provider accepted an out-of-scope path")
			|| !Check(!result.record && !result.node,
				"failed shared particle lookup published a resource")) {
			return false;
		}
	}
	return true;
}

// The caller holds the native node lock, as matsys does while reading its
// borrowed external buffer. Snapshot bytes only to detect later scratch reuse.
bool CopyLockedSharedMaterialBuffer(
	const ResourceLookup& resource,
	std::vector<char>& bytes)
{
	const auto* text = reinterpret_cast<const char*>(resource.node->entry);
	if (!Check(text != nullptr, "shared material node has no raw text"))
		return false;
	const std::size_t length = std::strlen(text);
	if (!Check(resource.record->asyncReadArgument == length,
		"shared material length includes a terminator or truncates raw text")) {
		return false;
	}
	const std::size_t capacity = length + 2;
	if (!Check(text[capacity - 2] == '\0' && text[capacity - 1] == '\0',
		"shared material native buffer capacity lacks two trailing NULs")) {
		return false;
	}
	bytes.assign(text, text + capacity);
	return true;
}

bool TestSharedParticleMaterialBorrowedBufferLifetime()
{
	ResourceLookup first{};
	if (!Check(LookupResource(kSharedParticleMaterialPaths[0], first),
		"first shared material lookup failed")
		|| !Check(first.record && first.node, "first shared material borrow is incomplete")
		|| !Check(InterlockedCompareExchange(&first.node->lock, 1, 0) == 0,
			"native consumer could not lock the first shared material")) {
		return false;
	}
	const auto* firstText = reinterpret_cast<const char*>(first.node->entry);
	std::vector<char> firstBytes;
	if (!CopyLockedSharedMaterialBuffer(first, firstBytes)) {
		InterlockedExchange(&first.node->lock, 0);
		return false;
	}

	ResourceLookup second{};
	const char* secondText{};
	std::vector<char> secondBytes;
	bool secondRead = false;
	// A joined worker provides a deterministic handoff, not a timing race. The
	// first consumer retains its lock and borrow throughout both lookups.
	std::thread otherConsumer([&] {
		ResourceLookup same{};
		if (!Check(LookupResource(kSharedParticleMaterialPaths[0], same),
			"shared material lookup failed while its native consumer held the lock")
			|| !Check(same.record == first.record && same.node == first.node,
				"shared material borrow depends on the requesting thread")
			|| !Check(LookupResource(kSharedParticleMaterialPaths[1], second),
				"second shared material lookup failed")
			|| !Check(second.record && second.node,
				"second shared material borrow is incomplete")
			|| !Check(InterlockedCompareExchange(&second.node->lock, 1, 0) == 0,
				"second material cannot be consumed while the first is locked")) {
			return;
		}
		secondText = reinterpret_cast<const char*>(second.node->entry);
		secondRead = CopyLockedSharedMaterialBuffer(second, secondBytes);
		InterlockedExchange(&second.node->lock, 0);
	});
	otherConsumer.join();
	const bool retained = secondRead
		&& Check(first.record != second.record && first.node != second.node
			&& firstText != secondText,
			"different shared materials reused borrowed resource storage")
		&& Check(InterlockedCompareExchange(&first.node->lock, 1, 1) == 1,
			"a lookup reset a shared material's active native consumer lock")
		&& Check(first.record->asyncReadArgument == firstBytes.size() - 2,
			"another lookup changed the first borrowed material's buffer length")
		&& Check(first.node->entry == reinterpret_cast<std::uintptr_t>(firstText),
			"another lookup replaced the first borrowed material's raw data")
		&& Check(std::memcmp(firstText, firstBytes.data(), firstBytes.size()) == 0,
			"another lookup overwrote the first borrowed material's buffer");
	InterlockedExchange(&first.node->lock, 0);
	if (!retained)
		return false;

	ResourceLookup again{};
	if (!Check(LookupResource(kSharedParticleMaterialPaths[1], again),
		"shared material lookup failed after the borrowing thread exited")
		|| !Check(again.record == second.record && again.node == second.node,
			"shared material storage did not survive the borrowing thread")
		|| !Check(InterlockedCompareExchange(&again.node->lock, 1, 0) == 0,
			"native consumer could not reacquire the second shared material")) {
		return false;
	}
	const bool secondRetained =
		Check(again.record->asyncReadArgument == secondBytes.size() - 2,
			"second shared material did not retain its own buffer length")
		&& Check(again.node->entry == reinterpret_cast<std::uintptr_t>(secondText),
			"second shared material did not retain its borrowed raw data")
		&& Check(std::memcmp(secondText, secondBytes.data(), secondBytes.size()) == 0,
			"second borrowed buffer changed after its native consumer released it");
	InterlockedExchange(&again.node->lock, 0);
	return secondRetained;
}
}

int main()
{
	const bool passed = TestConstantsAndLayouts()
		&& TestPendingReadyAndNotReadyInspection()
		&& TestCompletedValueAndInvalidProtections()
		&& TestExistingEntryDecisionTable()
		&& TestRetailHashVectorsAndCollisionIdentity()
		&& TestSameKeyEightThreadExclusion()
		&& TestDifferentKeyParallelism()
		&& TestGenerationAddressDistinction()
		&& TestNestedTryDeferAndAccountingModel()
		&& TestBoundedFifoOverflowContract()
		&& TestSharedParticleMaterialPathScope()
		&& TestSharedParticleMaterialBorrowedBufferLifetime();
	if (!passed)
		return EXIT_FAILURE;
	std::cout << "vpk_async_precache_tests passed\n";
	return EXIT_SUCCESS;
}
