#include "vpk_async_precache_entry.h"

#include <Windows.h>

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace r1delta::vpk_async_precache
{
struct AsyncPrecacheClaimState
{
	AsyncPrecacheGeneration generation{};
	std::uint32_t pathHash{};
	std::string path;
	std::mutex mutex;
};

namespace
{
constexpr std::size_t kClaimRegistryPruneThreshold = 4096;
std::mutex s_claimRegistryMutex;
std::unordered_map<
	std::uint32_t,
	std::vector<std::weak_ptr<AsyncPrecacheClaimState>>> s_claimRegistry;
std::size_t s_claimRegistryEntries{};
thread_local AsyncPrecacheEntryClaim* s_currentClaim;

bool IsReadableProtection(DWORD protection) noexcept
{
	if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
		return false;
	switch (protection & 0xFF) {
	case PAGE_READONLY:
	case PAGE_READWRITE:
	case PAGE_WRITECOPY:
	case PAGE_EXECUTE_READ:
	case PAGE_EXECUTE_READWRITE:
	case PAGE_EXECUTE_WRITECOPY:
		return true;
	default:
		return false;
	}
}

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

bool IsRangeInRegion(
	std::uintptr_t address,
	std::size_t length,
	const MEMORY_BASIC_INFORMATION& memory) noexcept
{
	const std::uintptr_t regionBase =
		reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
	if (address < regionBase)
		return false;
	const std::uintptr_t offset = address - regionBase;
	return offset <= memory.RegionSize && length <= memory.RegionSize - offset;
}

bool TryReadPointer(const void* location, std::uintptr_t& value) noexcept
{
	if (!location)
		return false;
	MEMORY_BASIC_INFORMATION memory{};
	if (!VirtualQuery(location, &memory, sizeof(memory))
		|| memory.State != MEM_COMMIT
		|| !IsReadableProtection(memory.Protect)
		|| !IsRangeInRegion(
			reinterpret_cast<std::uintptr_t>(location),
			sizeof(value),
			memory)) {
		return false;
	}
	std::memcpy(&value, location, sizeof(value));
	return true;
}

bool IsReadableImageRange(std::uintptr_t address, std::size_t length) noexcept
{
	if (!address)
		return false;
	MEMORY_BASIC_INFORMATION memory{};
	return VirtualQuery(
			reinterpret_cast<const void*>(address),
			&memory,
			sizeof(memory))
		&& memory.State == MEM_COMMIT
		&& memory.Type == MEM_IMAGE
		&& IsReadableProtection(memory.Protect)
		&& IsRangeInRegion(address, length, memory);
}

bool IsExecutableImageAddress(std::uintptr_t address) noexcept
{
	if (!address)
		return false;
	MEMORY_BASIC_INFORMATION memory{};
	return VirtualQuery(
			reinterpret_cast<const void*>(address),
			&memory,
			sizeof(memory))
		&& memory.State == MEM_COMMIT
		&& memory.Type == MEM_IMAGE
		&& IsExecutableProtection(memory.Protect);
}

bool IsRangeWithinImage(
	std::uintptr_t address,
	std::size_t length,
	const AsyncPrecacheModuleLayout& layout) noexcept
{
	if (!address || !layout.filesystemBase || address < layout.filesystemBase)
		return false;
	const std::uintptr_t offset = address - layout.filesystemBase;
	return offset <= layout.filesystemSize
		&& length <= layout.filesystemSize - offset;
}
bool IsReadableRangeInExactImage(
	std::uintptr_t address,
	std::size_t length,
	const AsyncPrecacheModuleLayout& layout) noexcept
{
	if (!IsRangeWithinImage(address, length, layout))
		return false;
	MEMORY_BASIC_INFORMATION memory{};
	return VirtualQuery(
			reinterpret_cast<const void*>(address),
			&memory,
			sizeof(memory))
		&& memory.State == MEM_COMMIT
		&& memory.Type == MEM_IMAGE
		&& memory.AllocationBase == reinterpret_cast<const void*>(
			layout.filesystemBase)
		&& IsReadableProtection(memory.Protect)
		&& IsRangeInRegion(address, length, memory);
}


bool IsExecutableAddressInExactImage(
	std::uintptr_t address,
	const AsyncPrecacheModuleLayout& layout) noexcept
{
	if (!IsRangeWithinImage(address, 1, layout))
		return false;
	MEMORY_BASIC_INFORMATION memory{};
	return VirtualQuery(
			reinterpret_cast<const void*>(address),
			&memory,
			sizeof(memory))
		&& memory.State == MEM_COMMIT
		&& memory.Type == MEM_IMAGE
		&& memory.AllocationBase == reinterpret_cast<const void*>(
			layout.filesystemBase)
		&& IsExecutableProtection(memory.Protect);
}

bool StateMatches(
	const AsyncPrecacheClaimState& state,
	const AsyncPrecacheClaimKey& key) noexcept
{
	return state.generation == key.generation
		&& state.pathHash == key.pathHash
		&& state.path.size() == key.path.size()
		&& (key.path.empty()
			|| std::memcmp(
				state.path.data(),
				key.path.data(),
				key.path.size()) == 0);
}

void PruneClaimRegistry()
{
	for (auto bucket = s_claimRegistry.begin(); bucket != s_claimRegistry.end();) {
		auto& entries = bucket->second;
		for (auto entry = entries.begin(); entry != entries.end();) {
			if (entry->expired()) {
				entry = entries.erase(entry);
				--s_claimRegistryEntries;
			}
			else {
				++entry;
			}
		}
		if (entries.empty())
			bucket = s_claimRegistry.erase(bucket);
		else
			++bucket;
	}
}

std::shared_ptr<AsyncPrecacheClaimState> GetClaimState(
	const AsyncPrecacheClaimKey& key)
{
	std::lock_guard<std::mutex> registryLock(s_claimRegistryMutex);
	if (s_claimRegistryEntries > kClaimRegistryPruneThreshold)
		PruneClaimRegistry();

	auto& entries = s_claimRegistry[key.pathHash];
	for (auto entry = entries.begin(); entry != entries.end();) {
		if (std::shared_ptr<AsyncPrecacheClaimState> state = entry->lock()) {
			if (StateMatches(*state, key))
				return state;
			++entry;
		}
		else {
			entry = entries.erase(entry);
			--s_claimRegistryEntries;
		}
	}

	auto state = std::make_shared<AsyncPrecacheClaimState>();
	state->generation = key.generation;
	state->pathHash = key.pathHash;
	if (!key.path.empty())
		state->path.assign(key.path.data(), key.path.size());
	entries.emplace_back(state);
	++s_claimRegistryEntries;
	return state;
}
}

AsyncPrecacheEntryInspection InspectAsyncPrecacheEntry(
	const void* entry,
	const AsyncPrecacheModuleLayout& layout) noexcept
{
	AsyncPrecacheEntryInspection inspection;
	const bool objectReadable = TryReadPointer(entry, inspection.vtable)
		&& inspection.vtable != 0;
	if (!objectReadable)
		return inspection;

	const bool vtableReadable = TryReadPointer(
		reinterpret_cast<const void*>(inspection.vtable),
		inspection.leadingTarget);
	if (!vtableReadable)
		return inspection;

	bool pendingVtableAndTargetInExactImage = false;
	constexpr std::size_t readinessVtableLength =
		kReadinessMethodOffset + sizeof(std::uintptr_t);
	if (IsReadableRangeInExactImage(
			inspection.vtable,
			readinessVtableLength,
			layout)
		&& TryReadPointer(
			reinterpret_cast<const void*>(
				inspection.vtable + kReadinessMethodOffset),
			inspection.readinessTarget)) {
		pendingVtableAndTargetInExactImage =
			IsExecutableAddressInExactImage(
				inspection.readinessTarget,
				layout);
	}

	inspection.kind = DecideAsyncPrecacheEntryKind(
		objectReadable,
		vtableReadable,
		pendingVtableAndTargetInExactImage,
		IsReadableImageRange(inspection.vtable, sizeof(std::uintptr_t)),
		IsExecutableImageAddress(inspection.leadingTarget));
	return inspection;
}

std::uint32_t RetailVpkPathHash(std::string_view path) noexcept
{
	std::uint32_t hash = 0;
	for (const char rawByte : path) {
		const auto byte = static_cast<std::int8_t>(
			static_cast<unsigned char>(rawByte));
		const std::uint32_t sum = hash + static_cast<std::uint32_t>(byte);
		const std::uint32_t expanded = 1025u * sum;
		const std::uint32_t mixed = 9u * (expanded ^ (expanded >> 6));
		hash = 32769u * (mixed ^ (mixed >> 11));
	}
	return hash;
}

AsyncPrecacheClaimKey AsyncPrecacheClaimKey::FromPath(
	const AsyncPrecacheGeneration& generation,
	std::string_view path) noexcept
{
	return { generation, RetailVpkPathHash(path), path };
}

AsyncPrecacheEntryClaim::AsyncPrecacheEntryClaim(
	const AsyncPrecacheClaimKey& key,
	bool nested)
	: state_(GetClaimState(key))
{
	if (nested) {
		for (AsyncPrecacheEntryClaim* claim = s_currentClaim;
			claim;
			claim = claim->previous_) {
			if (claim->state_ == state_)
				return;
		}
		if (!state_->mutex.try_lock())
			return;
	}
	else {
		state_->mutex.lock();
	}

	previous_ = s_currentClaim;
	s_currentClaim = this;
	acquired_ = true;
}

AsyncPrecacheEntryClaim::~AsyncPrecacheEntryClaim()
{
	Release();
}

void AsyncPrecacheEntryClaim::Release() noexcept
{
	if (!acquired_)
		return;
	acquired_ = false;

	AsyncPrecacheEntryClaim** link = &s_currentClaim;
	while (*link && *link != this)
		link = &(*link)->previous_;
	if (*link == this)
		*link = previous_;

	state_->mutex.unlock();
	state_.reset();
	previous_ = nullptr;
}

bool AsyncPrecacheEntryClaim::Acquired() const noexcept
{
	return acquired_;
}

DeferredCallbackEnqueueResult AsyncPrecacheDeferredCallbacks::Enqueue(
	std::uint32_t callbackIndex) noexcept
{
	if (size_ == indices_.size())
		return DeferredCallbackEnqueueResult::Overflow;
	indices_[(head_ + size_) % indices_.size()] = callbackIndex;
	++size_;
	return DeferredCallbackEnqueueResult::Enqueued;
}

bool AsyncPrecacheDeferredCallbacks::TryDequeue(
	std::uint32_t& callbackIndex) noexcept
{
	if (size_ == 0)
		return false;
	callbackIndex = indices_[head_];
	head_ = (head_ + 1) % indices_.size();
	--size_;
	return true;
}

std::size_t AsyncPrecacheDeferredCallbacks::Size() const noexcept
{
	return size_;
}

bool AsyncPrecacheDeferredCallbacks::Empty() const noexcept
{
	return size_ == 0;
}
}
