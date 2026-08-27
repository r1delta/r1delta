#pragma once

#include <Windows.h>
#include <array>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace r1delta::logging::crash_report_minidump
{

inline constexpr size_t kAdditionalMemoryBudget = 32u * 1024u * 1024u;
inline constexpr size_t kMaximumAdditionalMemoryRegions = 512;
inline constexpr size_t kMaximumRegisteredMemoryRegions = 64;
inline constexpr size_t kMaximumRegisteredMemoryRegionBytes = 4u * 1024u * 1024u;
inline constexpr size_t kMaximumCrashBreadcrumbs = 128;
inline constexpr uint32_t kCaptureManifestStreamType = 0x5231444Du;
inline constexpr uint32_t kCaptureManifestMagic = 0x4D433152u;
inline constexpr uint16_t kCaptureManifestVersion = 1;

enum class CrashMemoryRegionPriority : uint32_t
{
	Critical,
	High,
	Normal,
	Low,
};

enum CrashMemoryRegionSource : uint32_t
{
	CrashMemoryRegionRegistered = 1u << 0,
	CrashMemoryRegionRegisterPointer = 1u << 1,
	CrashMemoryRegionStackPointer = 1u << 2,
	CrashMemoryRegionExceptionParameter = 1u << 4,
	CrashMemoryRegionIndirectPointer = 1u << 3,
};

struct CrashCaptureMemoryRegion
{
	uint64_t base;
	uint32_t size;
	uint32_t sources;
};

struct CrashBreadcrumb
{
	uint64_t sequence;
	uint64_t timestamp;
	uint32_t threadId;
	uint32_t category;
	uint32_t event;
	uint32_t reserved;
	uint64_t value1;
	uint64_t value2;
};

struct CrashCaptureManifestHeader
{
	uint32_t magic;
	uint16_t version;
	uint16_t headerSize;
	uint32_t manifestSize;
	uint32_t memoryRegionSize;
	uint32_t breadcrumbSize;
	uint32_t memoryRegionCount;
	uint32_t breadcrumbCount;
	uint64_t memoryBudget;
	uint64_t plannedMemoryBytes;
	uint64_t candidateRegionCount;
	uint64_t rejectedRegionCount;
};

struct CrashCaptureManifest
{
	CrashCaptureManifestHeader header;
	std::array<CrashCaptureMemoryRegion, kMaximumAdditionalMemoryRegions> memoryRegions;
	std::array<CrashBreadcrumb, kMaximumCrashBreadcrumbs> breadcrumbs;
};

static_assert(sizeof(CrashCaptureMemoryRegion) == 16);
static_assert(sizeof(CrashBreadcrumb) == 48);
static_assert(sizeof(CrashCaptureManifestHeader) == 64);
static_assert(sizeof(CrashCaptureManifest) == 14400);
static_assert(std::is_trivially_copyable_v<CrashCaptureManifest>);
static_assert(kCaptureManifestStreamType > 0xffffu);
static_assert(sizeof(CrashCaptureManifest) <= (std::numeric_limits<ULONG>::max)());

class BoundedMemoryRegionPlan
{
public:
	explicit BoundedMemoryRegionPlan(uint64_t byteBudget = kAdditionalMemoryBudget) noexcept;

	bool Add(uint64_t base, size_t size, uint32_t sources) noexcept;

	size_t Count() const noexcept;
	uint64_t PlannedBytes() const noexcept;
	uint64_t CandidateCount() const noexcept;
	uint64_t RejectedCount() const noexcept;
	const CrashCaptureMemoryRegion& Region(size_t index) const noexcept;

private:
	uint64_t byteBudget_;
	uint64_t plannedBytes_ = 0;
	uint64_t candidateCount_ = 0;
	uint64_t rejectedCount_ = 0;
	size_t count_ = 0;
	std::array<CrashCaptureMemoryRegion, kMaximumAdditionalMemoryRegions> regions_{};
};

bool RegisterCrashMemoryRegion(
	const void* address,
	size_t size,
	CrashMemoryRegionPriority priority = CrashMemoryRegionPriority::Normal) noexcept;

bool UnregisterCrashMemoryRegion(const void* address) noexcept;

void WriteCrashBreadcrumb(
	uint32_t category,
	uint32_t event,
	uint64_t value1 = 0,
	uint64_t value2 = 0) noexcept;

namespace detail
{
struct CrashMemoryCaptureClaims
{
	uint32_t count = 0;
	std::array<uint16_t, kMaximumRegisteredMemoryRegions> slots{};
};


void BuildCrashCaptureMemory(
	const EXCEPTION_POINTERS& exceptionPointers,
	BoundedMemoryRegionPlan& plan,
	CrashCaptureManifest& manifest,
	CrashMemoryCaptureClaims& claims,
	uint8_t* scratchBuffer,
	size_t scratchBufferSize) noexcept;

void ReleaseCrashCaptureMemoryClaims(CrashMemoryCaptureClaims& claims) noexcept;

} // namespace detail

} // namespace r1delta::logging::crash_report_minidump
