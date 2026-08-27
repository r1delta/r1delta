#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace r1delta::vpk_async_precache
{
constexpr std::uintptr_t kAsyncPrecacheWorkerRva = 0x74D50;
constexpr std::uintptr_t kFindResourceHandlerRva = 0x71350;
constexpr std::uintptr_t kFindPackEntrySlotRva = 0x746B0;
constexpr std::uintptr_t kCompareResourceExtensionRva = 0x4AFD0;
constexpr std::uintptr_t kFormatPathRva = 0x4CB90;
constexpr std::uintptr_t kAsyncDecodeCallbackRva = 0x713D0;
constexpr std::uintptr_t kResourceRecordBaseRva = 0x0FC030;
constexpr std::uintptr_t kFileSystemInterfaceRva = 0x0FAD28;
constexpr std::uintptr_t kPackStoreRva = 0x20FC640;
constexpr std::uintptr_t kWorkItemIndicesRva = 0x20FC648;
constexpr std::uintptr_t kPrecacheModeRva = 0x20FC650;
constexpr std::uintptr_t kOutstandingWorkCountRva = 0x20FC678;
constexpr std::uint32_t kExpectedTimeDateStamp = 0x54874230;
constexpr std::uint32_t kExpectedSizeOfImage = 0x2116000;
constexpr std::size_t kCanonicalPathCapacity = 0x104;
constexpr std::size_t kDeferredCallbackCapacity = 4096;
constexpr std::size_t kPackRecordCountOffset = 0x230;
constexpr std::size_t kPackEntryNodesOffset = 0x238;
constexpr std::size_t kPackRecordsOffset = 0x240;
constexpr std::size_t kSubmitAsyncRequestVtableOffset = 0x370;
constexpr std::size_t kGetAsyncQueueVtableOffset = 0x378;
constexpr std::size_t kPrepareAsyncRequestVtableOffset = 0x380;
constexpr std::size_t kReadinessMethodOffset = 0x58;
inline constexpr std::array<std::uint8_t, 24> kExpectedWorkerPrologue{
	0x48, 0x89, 0x6C, 0x24, 0x10,
	0x48, 0x89, 0x74, 0x24, 0x18,
	0x48, 0x89, 0x7C, 0x24, 0x20,
	0x41, 0x54,
	0x48, 0x81, 0xEC, 0xD0, 0x01, 0x00, 0x00
};

struct ResourceRecord
{
	const char* directory;
	const char* filename;
	const char* extension;
	std::uintptr_t unknown18;
	std::uintptr_t asyncReadArgument;
	std::array<std::byte, 0x28> unknown28;
};

struct ResourceHandler
{
	const char* extension;
	std::uintptr_t unknown08;
	std::uintptr_t unknown10;
	std::uintptr_t decode;
	std::uintptr_t finish;
	std::uintptr_t consumeLoadedEntry;
};

struct ResourceEntryNode
{
	volatile std::uintptr_t entry;
	std::uint32_t unknown08;
	volatile long lock;
	std::uint32_t recordIndex;
	std::uint32_t unknown14;
};

struct AsyncRequestStorage
{
	std::int32_t state;
	std::array<std::byte, 0x3C> opaque;
	std::uint8_t tail;
	std::array<std::byte, 0x0F> padding;
};

struct AsyncPrecacheContext
{
	std::uint8_t mode;
	std::array<std::byte, 7> padding;
	ResourceEntryNode* entryNode;
	ResourceRecord* record;
	std::uintptr_t decodedResource;
	ResourceHandler* handler;
	std::int32_t* request;
};

static_assert(sizeof(ResourceRecord) == 0x50);
static_assert(offsetof(ResourceRecord, extension) == 0x10);
static_assert(offsetof(ResourceRecord, asyncReadArgument) == 0x20);
static_assert(sizeof(ResourceHandler) == 0x30);
static_assert(offsetof(ResourceHandler, decode) == 0x18);
static_assert(offsetof(ResourceHandler, finish) == 0x20);
static_assert(offsetof(ResourceHandler, consumeLoadedEntry) == 0x28);
static_assert(sizeof(ResourceEntryNode) == 0x18);
static_assert(offsetof(ResourceEntryNode, lock) == 0x0C);
static_assert(offsetof(ResourceEntryNode, recordIndex) == 0x10);
static_assert(sizeof(AsyncRequestStorage) == 0x50);
static_assert(offsetof(AsyncRequestStorage, tail) == 0x40);
static_assert(sizeof(AsyncPrecacheContext) == 0x30);
static_assert(offsetof(AsyncPrecacheContext, entryNode) == 0x08);
static_assert(offsetof(AsyncPrecacheContext, record) == 0x10);
static_assert(offsetof(AsyncPrecacheContext, decodedResource) == 0x18);
static_assert(offsetof(AsyncPrecacheContext, handler) == 0x20);
static_assert(offsetof(AsyncPrecacheContext, request) == 0x28);

struct AsyncPrecacheModuleLayout
{
	std::uintptr_t filesystemBase{};
	std::size_t filesystemSize{};
};

enum class AsyncPrecacheEntryKind
{
	Invalid,
	Pending,
	CompletedValue,
};

struct AsyncPrecacheEntryInspection
{
	AsyncPrecacheEntryKind kind{ AsyncPrecacheEntryKind::Invalid };
	std::uintptr_t vtable{};
	std::uintptr_t leadingTarget{};
	std::uintptr_t readinessTarget{};
};

constexpr AsyncPrecacheEntryKind DecideAsyncPrecacheEntryKind(
	bool objectReadable,
	bool vtableReadable,
	bool pendingVtableAndTargetInExactImage,
	bool vtableIsReadableImage,
	bool leadingTargetExecutable) noexcept
{
	if (!objectReadable || !vtableReadable)
		return AsyncPrecacheEntryKind::Invalid;
	if (pendingVtableAndTargetInExactImage)
		return AsyncPrecacheEntryKind::Pending;
	if (vtableIsReadableImage && leadingTargetExecutable)
		return AsyncPrecacheEntryKind::CompletedValue;
	return AsyncPrecacheEntryKind::Invalid;
}

AsyncPrecacheEntryInspection InspectAsyncPrecacheEntry(
	const void* entry,
	const AsyncPrecacheModuleLayout& layout) noexcept;

std::uint32_t RetailVpkPathHash(std::string_view path) noexcept;

struct AsyncPrecacheGeneration
{
	const void* packStore{};
	const void* workItemIndices{};
	const void* recordBase{};

	friend constexpr bool operator==(
		const AsyncPrecacheGeneration&,
		const AsyncPrecacheGeneration&) noexcept = default;
};

struct AsyncPrecacheClaimKey
{
	AsyncPrecacheGeneration generation{};
	std::uint32_t pathHash{};
	std::string_view path{};

	static AsyncPrecacheClaimKey FromPath(
		const AsyncPrecacheGeneration& generation,
		std::string_view path) noexcept;
};

struct AsyncPrecacheClaimState;

class AsyncPrecacheEntryClaim
{
public:
	AsyncPrecacheEntryClaim(
		const AsyncPrecacheClaimKey& key,
		bool nested);
	~AsyncPrecacheEntryClaim();

	void Release() noexcept;
	[[nodiscard]] bool Acquired() const noexcept;

	AsyncPrecacheEntryClaim(const AsyncPrecacheEntryClaim&) = delete;
	AsyncPrecacheEntryClaim& operator=(const AsyncPrecacheEntryClaim&) = delete;

private:
	std::shared_ptr<AsyncPrecacheClaimState> state_;
	AsyncPrecacheEntryClaim* previous_{};
	bool acquired_{};
};

enum class AsyncPrecacheExistingAction
{
	StartRead,
	ReturnOccupied,
	Consume,
	FatalInvalid,
};

constexpr AsyncPrecacheExistingAction DecideAsyncPrecacheExistingAction(
	bool occupied,
	bool isVtf,
	AsyncPrecacheEntryKind kind,
	bool pendingReady) noexcept
{
	if (!occupied)
		return AsyncPrecacheExistingAction::StartRead;
	if (isVtf)
		return AsyncPrecacheExistingAction::ReturnOccupied;
	if (kind == AsyncPrecacheEntryKind::Invalid)
		return AsyncPrecacheExistingAction::FatalInvalid;
	if (kind == AsyncPrecacheEntryKind::CompletedValue || pendingReady)
		return AsyncPrecacheExistingAction::Consume;
	return AsyncPrecacheExistingAction::ReturnOccupied;
}

enum class AsyncPrecacheCallbackDisposition
{
	Executed,
	Deferred,
};

constexpr bool ShouldDecrementOutstanding(
	AsyncPrecacheCallbackDisposition disposition) noexcept
{
	return disposition == AsyncPrecacheCallbackDisposition::Executed;
}

enum class DeferredCallbackEnqueueResult
{
	Enqueued,
	Overflow,
};

class AsyncPrecacheDeferredCallbacks
{
public:
	DeferredCallbackEnqueueResult Enqueue(std::uint32_t callbackIndex) noexcept;
	bool TryDequeue(std::uint32_t& callbackIndex) noexcept;
	[[nodiscard]] std::size_t Size() const noexcept;
	[[nodiscard]] bool Empty() const noexcept;

private:
	std::array<std::uint32_t, kDeferredCallbackCapacity> indices_{};
	std::size_t head_{};
	std::size_t size_{};
};
}
