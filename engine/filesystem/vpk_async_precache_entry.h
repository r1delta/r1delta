#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

class AsyncPrecacheEntryClaim
{
public:
	explicit AsyncPrecacheEntryClaim(const void* entrySlot);
	~AsyncPrecacheEntryClaim();
	void Release() noexcept;
	[[nodiscard]] bool Acquired() const noexcept;

	AsyncPrecacheEntryClaim(const AsyncPrecacheEntryClaim&) = delete;
	AsyncPrecacheEntryClaim& operator=(const AsyncPrecacheEntryClaim&) = delete;

private:
	const void* entrySlot_{};
	AsyncPrecacheEntryClaim* previous_{};
	std::shared_ptr<std::recursive_mutex> mutex_;
	bool acquired_{};
};

enum class AsyncPrecacheEntryKind
{
	Invalid,
	Pending,
	CompletedValue,
};

struct AsyncPrecacheModuleLayout
{
	uintptr_t filesystemBase{};
	size_t filesystemSize{};
};

struct AsyncPrecacheEntryInspection
{
	AsyncPrecacheEntryKind kind{ AsyncPrecacheEntryKind::Invalid };
	uintptr_t vtable{};
	uintptr_t readinessTarget{};
};

AsyncPrecacheEntryInspection InspectAsyncPrecacheEntry(
	const void* entry,
	const AsyncPrecacheModuleLayout& layout) noexcept;
