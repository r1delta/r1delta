#include "crash_report_memory_capture.h"

#include <atomic>
#include <algorithm>
#include <cstring>
#include <limits>

namespace r1delta::logging::crash_report_minidump
{
namespace
{

constexpr size_t kPointerWindowBytes = 64u * 1024u;
constexpr size_t kMaximumStackScanBytes = 1u * 1024u * 1024u;
constexpr size_t kMaximumIndirectScanBytes = 2u * 1024u * 1024u;
constexpr size_t kMaximumStackPointerCandidates = 128;
constexpr size_t kMaximumIndirectPointerCandidates = 256;

enum class RegisteredRegionState : uint32_t
{
	Empty,
	Writing,
	Active,
	Capturing,
};

struct RegisteredMemoryRegion
{
	std::atomic<uint32_t> state{ static_cast<uint32_t>(RegisteredRegionState::Empty) };
	std::atomic<uint64_t> generation{ 0 };
	std::atomic<uintptr_t> address{ 0 };
	std::atomic<size_t> size{ 0 };
	std::atomic<uint32_t> priority{ static_cast<uint32_t>(CrashMemoryRegionPriority::Normal) };
};

struct BreadcrumbSlot
{
	std::atomic<uint32_t> writing{ 0 };
	std::atomic<uint64_t> sequence{ 0 };
	std::atomic<uint64_t> timestamp{ 0 };
	std::atomic<uint32_t> threadId{ 0 };
	std::atomic<uint32_t> category{ 0 };
	std::atomic<uint32_t> event{ 0 };
	std::atomic<uint64_t> value1{ 0 };
	std::atomic<uint64_t> value2{ 0 };
};

std::array<RegisteredMemoryRegion, kMaximumRegisteredMemoryRegions> g_registeredMemoryRegions;
std::array<BreadcrumbSlot, kMaximumCrashBreadcrumbs> g_breadcrumbs;
std::atomic<uint64_t> g_breadcrumbSequence{ 0 };

bool IsReadableMemory(const MEMORY_BASIC_INFORMATION& memory) noexcept
{
	if (memory.State != MEM_COMMIT
		|| (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
	{
		return false;
	}

	switch (memory.Protect & 0xff)
	{
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

bool IsPossibleUserPointer(uintptr_t value) noexcept
{
	return value >= 0x10000
		&& value <= 0x00007FFFFFFFFFFFu;
}

bool AddReadableRange(
	BoundedMemoryRegionPlan& plan,
	uintptr_t address,
	size_t size,
	uint32_t sources) noexcept
{
	if (!IsPossibleUserPointer(address)
		|| size == 0
		|| size - 1 > std::numeric_limits<uintptr_t>::max() - address)
	{
		return false;
	}

	bool added = false;
	const uintptr_t requestedEnd = address + size;
	uintptr_t current = address;
	while (current < requestedEnd)
	{
		MEMORY_BASIC_INFORMATION memory{};
		if (VirtualQuery(
				reinterpret_cast<const void*>(current),
				&memory,
				sizeof(memory)) == 0)
		{
			return added;
		}

		const uintptr_t regionBase = reinterpret_cast<uintptr_t>(memory.BaseAddress);
		if (memory.RegionSize > std::numeric_limits<uintptr_t>::max() - regionBase)
			return added;
		const uintptr_t regionEnd = regionBase + memory.RegionSize;
		if (regionEnd <= current)
			return added;

		const uintptr_t segmentEnd = (std::min)(requestedEnd, regionEnd);
		if (IsReadableMemory(memory)
			&& plan.Add(current, static_cast<size_t>(segmentEnd - current), sources))
		{
			added = true;
		}
		current = segmentEnd;
	}
	return added;
}

bool AddPointerWindow(
	BoundedMemoryRegionPlan& plan,
	uintptr_t pointer,
	uint32_t sources,
	size_t pageSize) noexcept
{
	if (!IsPossibleUserPointer(pointer) || pageSize == 0)
		return false;
	if (sources != CrashMemoryRegionRegistered)
	{
		const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
		const uintptr_t stackBase = reinterpret_cast<uintptr_t>(tib->StackBase);
		const uintptr_t stackLimit = reinterpret_cast<uintptr_t>(tib->StackLimit);
		if (pointer >= stackLimit && pointer < stackBase)
			return false;
	}


	MEMORY_BASIC_INFORMATION memory{};
	if (VirtualQuery(
			reinterpret_cast<const void*>(pointer),
			&memory,
			sizeof(memory)) == 0
		|| !IsReadableMemory(memory)
		|| (sources != CrashMemoryRegionRegistered
			&& memory.Type == MEM_IMAGE
			&& (memory.Protect & 0xff) == PAGE_EXECUTE_READ))
	{
		return false;
	}

	const uintptr_t regionBase = reinterpret_cast<uintptr_t>(memory.BaseAddress);
	if (memory.RegionSize > std::numeric_limits<uintptr_t>::max() - regionBase)
		return false;
	const uintptr_t regionEnd = regionBase + memory.RegionSize;
	const uintptr_t halfWindow = kPointerWindowBytes / 2;
	uintptr_t windowBase = pointer > halfWindow ? pointer - halfWindow : regionBase;
	windowBase = (std::max)(windowBase, regionBase);
	windowBase -= windowBase % pageSize;
	windowBase = (std::max)(windowBase, regionBase);
	const uintptr_t windowEnd = (std::min)(
		regionEnd,
		windowBase + (std::min)(
			kPointerWindowBytes,
			static_cast<size_t>(std::numeric_limits<uintptr_t>::max() - windowBase)));
	return plan.Add(
		windowBase,
		static_cast<size_t>(windowEnd - windowBase),
		sources);
}

void ScanPointerRange(
	BoundedMemoryRegionPlan& plan,
	uintptr_t address,
	size_t size,
	size_t scanLimit,
	size_t candidateLimit,
	size_t* candidates,
	uint32_t sources,
	size_t pageSize,
	uint8_t* buffer,
	size_t bufferSize) noexcept
{
	if (!candidates
		|| *candidates >= candidateLimit
		|| size < sizeof(uintptr_t)
		|| !buffer
		|| bufferSize < sizeof(uintptr_t))
	{
		return;
	}

	const size_t bytesToScan = (std::min)(size, scanLimit);
	size_t offset = 0;
	while (offset < bytesToScan && *candidates < candidateLimit)
	{
		const size_t requested = (std::min)(bufferSize, bytesToScan - offset);
		SIZE_T bytesRead = 0;
		if (!ReadProcessMemory(
				GetCurrentProcess(),
				reinterpret_cast<const void*>(address + offset),
				buffer,
				requested,
				&bytesRead)
			|| bytesRead < sizeof(uintptr_t))
		{
			offset += requested;
			continue;
		}

		for (size_t i = 0;
			i + sizeof(uintptr_t) <= static_cast<size_t>(bytesRead)
				&& *candidates < candidateLimit;
			i += sizeof(uintptr_t))
		{
			uintptr_t pointer = 0;
			memcpy(&pointer, buffer + i, sizeof(pointer));
			if (!IsPossibleUserPointer(pointer))
				continue;
			++*candidates;
			AddPointerWindow(plan, pointer, sources, pageSize);
		}
		offset += requested;
	}
}

void AddRegisteredMemoryRegions(
	BoundedMemoryRegionPlan& plan,
	detail::CrashMemoryCaptureClaims& claims) noexcept
{
	for (uint32_t requestedPriority =
			static_cast<uint32_t>(CrashMemoryRegionPriority::Critical);
		requestedPriority <= static_cast<uint32_t>(CrashMemoryRegionPriority::Low);
		++requestedPriority)
	{
		for (size_t slotIndex = 0;
			slotIndex < g_registeredMemoryRegions.size();
			++slotIndex)
		{
			RegisteredMemoryRegion& slot = g_registeredMemoryRegions[slotIndex];
			const uint64_t observedGeneration =
				slot.generation.load(std::memory_order_acquire);
			if ((observedGeneration & 1u) != 0
				|| slot.priority.load(std::memory_order_relaxed) != requestedPriority)
			{
				continue;
			}

			uint32_t expected = static_cast<uint32_t>(RegisteredRegionState::Active);
			if (!slot.state.compare_exchange_strong(
					expected,
					static_cast<uint32_t>(RegisteredRegionState::Capturing),
					std::memory_order_acq_rel,
					std::memory_order_relaxed))
			{
				continue;
			}

			if (slot.generation.load(std::memory_order_acquire) != observedGeneration
				|| slot.priority.load(std::memory_order_relaxed) != requestedPriority
				|| claims.count >= claims.slots.size())
			{
				slot.state.store(
					static_cast<uint32_t>(RegisteredRegionState::Active),
					std::memory_order_release);
				continue;
			}

			const uintptr_t address = slot.address.load(std::memory_order_relaxed);
			const size_t size = slot.size.load(std::memory_order_relaxed);
			if (!AddReadableRange(
					plan,
					address,
					size,
					CrashMemoryRegionRegistered))
			{
				slot.state.store(
					static_cast<uint32_t>(RegisteredRegionState::Active),
					std::memory_order_release);
				continue;
			}
			claims.slots[claims.count++] = static_cast<uint16_t>(slotIndex);
		}
	}
}

void SnapshotBreadcrumbs(CrashCaptureManifest& manifest) noexcept
{
	const uint64_t latest = g_breadcrumbSequence.load(std::memory_order_acquire);
	if (latest == 0)
		return;

	const uint64_t first = latest > kMaximumCrashBreadcrumbs
		? latest - kMaximumCrashBreadcrumbs + 1
		: 1;
	for (uint64_t sequence = first;
		sequence <= latest && manifest.header.breadcrumbCount < kMaximumCrashBreadcrumbs;
		++sequence)
	{
		BreadcrumbSlot& slot = g_breadcrumbs[(sequence - 1) % kMaximumCrashBreadcrumbs];
		if (slot.writing.load(std::memory_order_acquire) != 0)
			continue;
		const uint64_t sequenceBefore = slot.sequence.load(std::memory_order_acquire);
		if (sequenceBefore != sequence)
			continue;

		CrashBreadcrumb record{};
		record.sequence = sequence;
		record.timestamp = slot.timestamp.load(std::memory_order_relaxed);
		record.threadId = slot.threadId.load(std::memory_order_relaxed);
		record.category = slot.category.load(std::memory_order_relaxed);
		record.event = slot.event.load(std::memory_order_relaxed);
		record.value1 = slot.value1.load(std::memory_order_relaxed);
		record.value2 = slot.value2.load(std::memory_order_relaxed);
		if (slot.writing.load(std::memory_order_acquire) != 0
			|| slot.sequence.load(std::memory_order_acquire) != sequenceBefore)
		{
			continue;
		}

		manifest.breadcrumbs[manifest.header.breadcrumbCount++] = record;
	}
}

} // namespace

void detail::BuildCrashCaptureMemory(
	const EXCEPTION_POINTERS& exceptionPointers,
	BoundedMemoryRegionPlan& plan,
	CrashCaptureManifest& manifest,
	CrashMemoryCaptureClaims& claims,
	uint8_t* scratchBuffer,
	size_t scratchBufferSize) noexcept
{
	const CONTEXT& context = *exceptionPointers.ContextRecord;
	SYSTEM_INFO systemInfo{};
	GetSystemInfo(&systemInfo);
	const size_t pageSize = systemInfo.dwPageSize;
	AddRegisteredMemoryRegions(plan, claims);
	if (exceptionPointers.ExceptionRecord)
	{
		const ULONG parameterCount = (std::min)(
			exceptionPointers.ExceptionRecord->NumberParameters,
			static_cast<DWORD>(EXCEPTION_MAXIMUM_PARAMETERS));
		for (ULONG i = 0; i < parameterCount; ++i)
		{
			AddPointerWindow(
				plan,
				static_cast<uintptr_t>(
					exceptionPointers.ExceptionRecord->ExceptionInformation[i]),
				CrashMemoryRegionExceptionParameter,
				pageSize);
		}
	}

#if defined(_M_X64)
	const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
	const uintptr_t stackBase = reinterpret_cast<uintptr_t>(tib->StackBase);
	const uintptr_t stackLimit = reinterpret_cast<uintptr_t>(tib->StackLimit);
	const uintptr_t registers[] = {
		context.Rax, context.Rbx, context.Rcx, context.Rdx,
		context.Rsi, context.Rdi, context.Rbp, context.Rsp,
		context.R8, context.R9, context.R10, context.R11,
		context.R12, context.R13, context.R14, context.R15,
	};
	for (const uintptr_t pointer : registers)
	{
		if (pointer < stackLimit || pointer >= stackBase)
		{
			AddPointerWindow(
				plan,
				pointer,
				CrashMemoryRegionRegisterPointer,
				pageSize);
		}
	}

	if (context.Rsp >= stackLimit && context.Rsp < stackBase)
	{
		size_t candidates = 0;
		ScanPointerRange(
			plan,
			context.Rsp,
			static_cast<size_t>(stackBase - context.Rsp),
			kMaximumStackScanBytes,
			kMaximumStackPointerCandidates,
			&candidates,
			CrashMemoryRegionStackPointer,
			pageSize,
			scratchBuffer,
			scratchBufferSize);
	}
#endif

	const size_t directRegionCount = plan.Count();
	for (size_t i = 0; i < directRegionCount; ++i)
		manifest.memoryRegions[i] = plan.Region(i);

	size_t scannedBytes = 0;
	size_t indirectCandidates = 0;
	for (size_t i = 0;
		i < directRegionCount
			&& scannedBytes < kMaximumIndirectScanBytes
			&& indirectCandidates < kMaximumIndirectPointerCandidates;
		++i)
	{
		const CrashCaptureMemoryRegion& source = manifest.memoryRegions[i];
		const size_t remaining = kMaximumIndirectScanBytes - scannedBytes;
		const size_t bytesToScan = (std::min)(static_cast<size_t>(source.size), remaining);
		ScanPointerRange(
			plan,
			static_cast<uintptr_t>(source.base),
			bytesToScan,
			bytesToScan,
			kMaximumIndirectPointerCandidates,
			&indirectCandidates,
			CrashMemoryRegionIndirectPointer,
			pageSize,
			scratchBuffer,
			scratchBufferSize);
		scannedBytes += bytesToScan;
	}

	CrashCaptureManifestHeader& header = manifest.header;
	header.magic = kCaptureManifestMagic;
	header.version = kCaptureManifestVersion;
	header.headerSize = static_cast<uint16_t>(sizeof(header));
	header.manifestSize = static_cast<uint32_t>(sizeof(manifest));
	header.memoryRegionSize = static_cast<uint32_t>(sizeof(CrashCaptureMemoryRegion));
	header.breadcrumbSize = static_cast<uint32_t>(sizeof(CrashBreadcrumb));
	header.memoryRegionCount = static_cast<uint32_t>(plan.Count());
	header.memoryBudget = kAdditionalMemoryBudget;
	header.plannedMemoryBytes = plan.PlannedBytes();
	header.candidateRegionCount = plan.CandidateCount();
	header.rejectedRegionCount = plan.RejectedCount();
	for (size_t i = 0; i < plan.Count(); ++i)
		manifest.memoryRegions[i] = plan.Region(i);
	SnapshotBreadcrumbs(manifest);
}

void detail::ReleaseCrashCaptureMemoryClaims(
	CrashMemoryCaptureClaims& claims) noexcept
{
	for (uint32_t i = 0; i < claims.count; ++i)
	{
		const size_t slotIndex = claims.slots[i];
		if (slotIndex >= g_registeredMemoryRegions.size())
			continue;
		RegisteredMemoryRegion& slot = g_registeredMemoryRegions[slotIndex];
		uint32_t expected = static_cast<uint32_t>(RegisteredRegionState::Capturing);
		slot.state.compare_exchange_strong(
			expected,
			static_cast<uint32_t>(RegisteredRegionState::Active),
			std::memory_order_release,
			std::memory_order_relaxed);
	}
	claims.count = 0;
}

BoundedMemoryRegionPlan::BoundedMemoryRegionPlan(uint64_t byteBudget) noexcept
	: byteBudget_(byteBudget)
{
}

bool BoundedMemoryRegionPlan::Add(
	uint64_t base,
	size_t size,
	uint32_t sources) noexcept
{
	++candidateCount_;
	if (size == 0
		|| size > std::numeric_limits<uint32_t>::max()
		|| base > std::numeric_limits<uint64_t>::max() - size)
	{
		++rejectedCount_;
		return false;
	}

	uint64_t mergedBase = base;
	uint64_t mergedEnd = base + size;
	uint32_t mergedSources = sources;
	size_t first = 0;
	while (first < count_)
	{
		const CrashCaptureMemoryRegion& region = regions_[first];
		const uint64_t regionEnd = region.base + region.size;
		if (regionEnd >= mergedBase)
			break;
		++first;
	}

	size_t last = first;
	uint64_t replacedBytes = 0;
	while (last < count_ && regions_[last].base <= mergedEnd)
	{
		const CrashCaptureMemoryRegion& region = regions_[last];
		mergedBase = (std::min)(mergedBase, region.base);
		mergedEnd = (std::max)(mergedEnd, region.base + region.size);
		mergedSources |= region.sources;
		replacedBytes += region.size;
		++last;
	}

	const uint64_t mergedSize = mergedEnd - mergedBase;
	if (mergedSize > std::numeric_limits<uint32_t>::max())
	{
		++rejectedCount_;
		return false;
	}
	const uint64_t addedBytes = mergedSize - replacedBytes;
	if (plannedBytes_ > byteBudget_
		|| addedBytes > byteBudget_ - plannedBytes_)
	{
		++rejectedCount_;
		return false;
	}

	if (first == last)
	{
		if (count_ == regions_.size())
		{
			++rejectedCount_;
			return false;
		}
		for (size_t i = count_; i > first; --i)
			regions_[i] = regions_[i - 1];
		++count_;
	}
	else
	{
		const size_t removed = last - first - 1;
		for (size_t i = first + 1; i + removed < count_; ++i)
			regions_[i] = regions_[i + removed];
		count_ -= removed;
	}

	regions_[first] = {
		mergedBase,
		static_cast<uint32_t>(mergedSize),
		mergedSources,
	};
	plannedBytes_ += addedBytes;
	return true;
}

size_t BoundedMemoryRegionPlan::Count() const noexcept
{
	return count_;
}

uint64_t BoundedMemoryRegionPlan::PlannedBytes() const noexcept
{
	return plannedBytes_;
}

uint64_t BoundedMemoryRegionPlan::CandidateCount() const noexcept
{
	return candidateCount_;
}

uint64_t BoundedMemoryRegionPlan::RejectedCount() const noexcept
{
	return rejectedCount_;
}

const CrashCaptureMemoryRegion& BoundedMemoryRegionPlan::Region(size_t index) const noexcept
{
	return regions_[index];
}

bool RegisterCrashMemoryRegion(
	const void* address,
	size_t size,
	CrashMemoryRegionPriority priority) noexcept
{
	if (!address
		|| size == 0
		|| size > kMaximumRegisteredMemoryRegionBytes
		|| static_cast<uint32_t>(priority)
			> static_cast<uint32_t>(CrashMemoryRegionPriority::Low))
	{
		return false;
	}

	for (RegisteredMemoryRegion& slot : g_registeredMemoryRegions)
	{
		uint32_t expected = static_cast<uint32_t>(RegisteredRegionState::Empty);
		if (!slot.state.compare_exchange_strong(
				expected,
				static_cast<uint32_t>(RegisteredRegionState::Writing),
				std::memory_order_acq_rel,
				std::memory_order_relaxed))
		{
			continue;
		}

		slot.generation.fetch_add(1, std::memory_order_acq_rel);
		slot.address.store(reinterpret_cast<uintptr_t>(address), std::memory_order_relaxed);
		slot.size.store(size, std::memory_order_relaxed);
		slot.priority.store(static_cast<uint32_t>(priority), std::memory_order_relaxed);
		slot.generation.fetch_add(1, std::memory_order_release);
		slot.state.store(
			static_cast<uint32_t>(RegisteredRegionState::Active),
			std::memory_order_release);
		return true;
	}
	return false;
}

bool UnregisterCrashMemoryRegion(const void* address) noexcept
{
	if (!address)
		return false;
	const uintptr_t requestedAddress = reinterpret_cast<uintptr_t>(address);

	for (RegisteredMemoryRegion& slot : g_registeredMemoryRegions)
	{
		const uint64_t observedGeneration =
			slot.generation.load(std::memory_order_acquire);
		if ((observedGeneration & 1u) != 0
			|| slot.state.load(std::memory_order_acquire)
				!= static_cast<uint32_t>(RegisteredRegionState::Active)
			|| slot.address.load(std::memory_order_relaxed) != requestedAddress)
		{
			continue;
		}

		uint32_t expected = static_cast<uint32_t>(RegisteredRegionState::Active);
		if (!slot.state.compare_exchange_strong(
				expected,
				static_cast<uint32_t>(RegisteredRegionState::Writing),
				std::memory_order_acq_rel,
				std::memory_order_relaxed))
		{
			continue;
		}
		if (slot.generation.load(std::memory_order_acquire) != observedGeneration
			|| slot.address.load(std::memory_order_relaxed) != requestedAddress)
		{
			slot.state.store(
				static_cast<uint32_t>(RegisteredRegionState::Active),
				std::memory_order_release);
			continue;
		}

		slot.generation.fetch_add(1, std::memory_order_acq_rel);
		slot.address.store(0, std::memory_order_relaxed);
		slot.size.store(0, std::memory_order_relaxed);
		slot.priority.store(
			static_cast<uint32_t>(CrashMemoryRegionPriority::Normal),
			std::memory_order_relaxed);
		slot.generation.fetch_add(1, std::memory_order_release);
		slot.state.store(
			static_cast<uint32_t>(RegisteredRegionState::Empty),
			std::memory_order_release);
		return true;
	}
	return false;
}

void WriteCrashBreadcrumb(
	uint32_t category,
	uint32_t event,
	uint64_t value1,
	uint64_t value2) noexcept
{
	const uint64_t sequence =
		g_breadcrumbSequence.fetch_add(1, std::memory_order_acq_rel) + 1;
	BreadcrumbSlot& slot =
		g_breadcrumbs[(sequence - 1) % kMaximumCrashBreadcrumbs];
	uint32_t expected = 0;
	if (!slot.writing.compare_exchange_strong(
			expected,
			1,
			std::memory_order_acq_rel,
			std::memory_order_relaxed))
	{
		return;
	}
	slot.sequence.store(0, std::memory_order_release);

	LARGE_INTEGER timestamp{};
	QueryPerformanceCounter(&timestamp);
	slot.timestamp.store(
		static_cast<uint64_t>(timestamp.QuadPart),
		std::memory_order_relaxed);
	slot.threadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
	slot.category.store(category, std::memory_order_relaxed);
	slot.event.store(event, std::memory_order_relaxed);
	slot.value1.store(value1, std::memory_order_relaxed);
	slot.value2.store(value2, std::memory_order_relaxed);
	slot.sequence.store(sequence, std::memory_order_release);
	slot.writing.store(0, std::memory_order_release);
}

} // namespace r1delta::logging::crash_report_minidump
