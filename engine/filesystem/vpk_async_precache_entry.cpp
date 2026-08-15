#include "vpk_async_precache_entry.h"

#include <Windows.h>

#include <cstring>
#include <mutex>
#include <unordered_map>

namespace {

constexpr size_t kEntryClaimRegistryPruneThreshold = 4096;
std::mutex s_entryClaimRegistryMutex;
std::unordered_map<
	const void*,
	std::weak_ptr<std::recursive_mutex>> s_entryClaimRegistry;
thread_local AsyncPrecacheEntryClaim* s_currentEntryClaim;

std::shared_ptr<std::recursive_mutex> GetEntryClaimMutex(const void* entrySlot)
{
	std::lock_guard<std::mutex> registryLock(s_entryClaimRegistryMutex);
	if (s_entryClaimRegistry.size() > kEntryClaimRegistryPruneThreshold) {
		for (auto entry = s_entryClaimRegistry.begin();
			entry != s_entryClaimRegistry.end();) {
			if (entry->second.expired())
				entry = s_entryClaimRegistry.erase(entry);
			else
				++entry;
		}
	}

	const auto found = s_entryClaimRegistry.find(entrySlot);
	if (found != s_entryClaimRegistry.end()) {
		if (std::shared_ptr<std::recursive_mutex> mutex = found->second.lock())
			return mutex;
	}

	auto mutex = std::make_shared<std::recursive_mutex>();
	s_entryClaimRegistry[entrySlot] = mutex;
	return mutex;
}


bool IsAddressRangeWithinImage(
	uintptr_t address,
	size_t length,
	uintptr_t imageBase,
	size_t imageSize) noexcept
{
	if (!address || !imageBase || address < imageBase)
		return false;
	const uintptr_t offset = address - imageBase;
	return offset <= imageSize && length <= imageSize - offset;
}

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

bool IsReadableImageRange(uintptr_t address, size_t length) noexcept
{
	if (!address)
		return false;
	MEMORY_BASIC_INFORMATION memory{};
	if (!VirtualQuery(
			reinterpret_cast<const void*>(address),
			&memory,
			sizeof(memory))
		|| memory.State != MEM_COMMIT
		|| memory.Type != MEM_IMAGE
		|| !IsReadableProtection(memory.Protect)) {
		return false;
	}

	const uintptr_t regionBase = reinterpret_cast<uintptr_t>(memory.BaseAddress);
	if (address < regionBase)
		return false;
	const uintptr_t regionOffset = address - regionBase;
	return regionOffset <= memory.RegionSize
		&& length <= memory.RegionSize - regionOffset;
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

bool TryReadPointer(const void* location, uintptr_t& value) noexcept
{
	if (!location)
		return false;

	MEMORY_BASIC_INFORMATION memory{};
	if (!VirtualQuery(location, &memory, sizeof(memory))
		|| memory.State != MEM_COMMIT
		|| !IsReadableProtection(memory.Protect)) {
		return false;
	}

	const uintptr_t address = reinterpret_cast<uintptr_t>(location);
	const uintptr_t regionBase = reinterpret_cast<uintptr_t>(memory.BaseAddress);
	if (address < regionBase)
		return false;
	const uintptr_t regionOffset = address - regionBase;
	if (regionOffset > memory.RegionSize
		|| sizeof(value) > memory.RegionSize - regionOffset) {
		return false;
	}

	memcpy(&value, location, sizeof(value));
	return true;
}

bool IsExecutableAddress(uintptr_t address) noexcept
{
	if (!address)
		return false;
	MEMORY_BASIC_INFORMATION memory{};
	return VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory))
		&& memory.State == MEM_COMMIT
		&& IsExecutableProtection(memory.Protect);
}

}
AsyncPrecacheEntryClaim::AsyncPrecacheEntryClaim(const void* entrySlot)
	: entrySlot_(entrySlot)
{
	if (!entrySlot) {
		acquired_ = true;
		return;
	}

	for (AsyncPrecacheEntryClaim* claim = s_currentEntryClaim;
		claim;
		claim = claim->previous_) {
		if (claim->entrySlot_ == entrySlot)
			return;
	}

	std::shared_ptr<std::recursive_mutex> mutex =
		GetEntryClaimMutex(entrySlot);
	if (s_currentEntryClaim) {
		if (!mutex->try_lock())
			return;
	}
	else {
		mutex->lock();
	}

	mutex_ = std::move(mutex);
	previous_ = s_currentEntryClaim;
	s_currentEntryClaim = this;
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
	if (!mutex_)
		return;

	AsyncPrecacheEntryClaim** link = &s_currentEntryClaim;
	while (*link && *link != this)
		link = &(*link)->previous_;
	if (*link == this)
		*link = previous_;

	mutex_->unlock();
	mutex_.reset();
	entrySlot_ = nullptr;
	previous_ = nullptr;
}

bool AsyncPrecacheEntryClaim::Acquired() const noexcept
{
	return acquired_;
}


AsyncPrecacheEntryInspection InspectAsyncPrecacheEntry(
	const void* entry,
	const AsyncPrecacheModuleLayout& layout) noexcept
{
	AsyncPrecacheEntryInspection inspection;
	if (!TryReadPointer(entry, inspection.vtable) || !inspection.vtable)
		return inspection;

	constexpr size_t readinessMethodOffset = 0x58;
	if (IsAddressRangeWithinImage(
			inspection.vtable,
			readinessMethodOffset + sizeof(uintptr_t),
			layout.filesystemBase,
			layout.filesystemSize)
		&& TryReadPointer(
			reinterpret_cast<const void*>(
				inspection.vtable + readinessMethodOffset),
			inspection.readinessTarget)
		&& IsAddressRangeWithinImage(
			inspection.readinessTarget,
			1,
			layout.filesystemBase,
			layout.filesystemSize)
		&& IsExecutableAddress(inspection.readinessTarget)) {
		inspection.kind = AsyncPrecacheEntryKind::Pending;
		return inspection;
	}

	if (IsReadableImageRange(inspection.vtable, sizeof(uintptr_t)))
		inspection.kind = AsyncPrecacheEntryKind::CompletedValue;
	return inspection;
}
