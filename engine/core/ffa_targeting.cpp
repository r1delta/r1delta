#include "ffa_targeting.h"

#include "ffa_targeting_logic.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

extern "C"
{
void R1DeltaClientSmartAmmoFfaBridge();
void R1DeltaClientMinimapFfaBridge();
void R1DeltaClientMinimapVisibilityFfaBridge();
void R1DeltaClientMinimapDefaultVisibilityFfaBridge();
void R1DeltaServerSmartAmmoFfaBridge();
void R1DeltaServerObserverInitialFfaBridge();
void R1DeltaServerObserverCycleFfaBridge();

volatile LONG g_R1DeltaClientFfaBased = FALSE;
volatile LONG g_R1DeltaServerFfaBased = FALSE;
std::uintptr_t g_R1DeltaClientSmartAmmoAccept = 0;
std::uintptr_t g_R1DeltaClientSmartAmmoReject = 0;
std::uintptr_t g_R1DeltaClientMinimapContinue = 0;
std::uintptr_t g_R1DeltaClientMinimapVisibilityContinue = 0;
std::uintptr_t g_R1DeltaClientMinimapDefaultVisibilityShow = 0;
std::uintptr_t g_R1DeltaClientMinimapDefaultVisibilityEnemy = 0;
std::uintptr_t g_R1DeltaServerSmartAmmoAccept = 0;
std::uintptr_t g_R1DeltaServerSmartAmmoReject = 0;
std::uintptr_t g_R1DeltaServerObserverInitialAccept = 0;
std::uintptr_t g_R1DeltaServerObserverInitialReject = 0;
std::uintptr_t g_R1DeltaServerObserverCycleAccept = 0;
std::uintptr_t g_R1DeltaServerObserverCycleReject = 0;
}

namespace
{
constexpr std::size_t kMaximumPatchSize = 16;
constexpr std::size_t kAbsoluteRelaySize = 14;

using EntityPredicateFn = bool(__fastcall*)(void*);
using EntityGetterFn = void*(__fastcall*)(void*);

constexpr std::size_t kClientIsPlayerVtableOffset = 0x538;
constexpr std::size_t kServerIsPlayerVtableOffset = 0x2B0;
constexpr std::uintptr_t kClientBossPlayerRva = 0x2F7450;
constexpr std::uintptr_t kClientOwnerRva = 0x025F30;
constexpr std::uintptr_t kClientAliveRva = 0x027F70;
constexpr std::uintptr_t kServerIsPlayerRva = 0x3C5C90;
constexpr std::uintptr_t kServerOwnerRva = 0x07C450;
constexpr std::uintptr_t kServerAliveRva = 0x3CC370;
constexpr std::uintptr_t kServerEntityListRva = 0xD425C8;
constexpr std::size_t kServerBossPlayerHandleOffset = 0x560;
constexpr std::size_t kServerEntityEntrySize = 48;
constexpr std::size_t kMaximumServerEntities = 0x4000;

constexpr unsigned char kClientSmartAmmoInputsExpected[] = {
	0x49, 0x8B, 0x45, 0x00, 0x49, 0x8B, 0xCD, 0xFF,
	0x90, 0x00, 0x03, 0x00, 0x00, 0x48, 0x8B, 0x13,
	0x48, 0x8B, 0xCB, 0x8B, 0xF8, 0xFF, 0x92, 0x00,
	0x03, 0x00, 0x00
};
constexpr unsigned char kClientSmartAmmoProjectileExpected[] = {
	0x3B, 0xC7, 0x75, 0x28, 0xE9, 0x37, 0x03, 0x00, 0x00
};
constexpr unsigned char kClientSmartAmmoExpected[] = {
	0x3B, 0xC7, 0x0F, 0x84, 0x14, 0x03, 0x00, 0x00
};
constexpr unsigned char kClientMinimapExpected[] = {
	0xF3, 0x0F, 0x10, 0x97, 0x34, 0x1A, 0x00, 0x00
};
constexpr unsigned char kClientMinimapVisibilityExpected[] = {
	0x8D, 0x48, 0x04, 0x8B, 0xD7, 0xD3, 0xE2
};
constexpr unsigned char kClientMinimapVisibilityInputsExpected[] = {
	0x83, 0xC9, 0xFF, 0xE8, 0x36, 0x4F, 0xD6, 0xFF,
	0x48, 0x8B, 0xC8, 0x48, 0x8B, 0xF0, 0xE8, 0xAB,
	0x4E, 0xD6, 0xFF, 0xBF, 0x01, 0x00, 0x00, 0x00
};
constexpr unsigned char kClientMinimapDefaultVisibilityInputsExpected[] = {
	0x8B, 0x74, 0x24, 0x50, 0x8B, 0x54, 0x24, 0x28
};
constexpr unsigned char kClientMinimapDefaultVisibilityExpected[] = {
	0x3B, 0xF2, 0x0F, 0x84, 0x6E, 0x02, 0x00, 0x00
};
constexpr unsigned char kClientBossPlayerExpected[] = {
	0x8B, 0x81, 0xC8, 0x00, 0x00, 0x00, 0x83, 0xF8,
	0xFF, 0x74, 0x27, 0x0F, 0xB7, 0xC8, 0x81, 0xF9,
	0x00, 0x40, 0x00, 0x00, 0x73, 0x1C, 0x8B, 0xD1,
	0x48, 0x8B, 0x0D, 0xF1, 0x78, 0x7F, 0x00, 0xC1,
	0xE8, 0x10, 0x48, 0xC1, 0xE2, 0x05, 0x39, 0x44,
	0x0A, 0x10, 0x75, 0x06, 0x48, 0x8B, 0x44, 0x0A,
	0x08, 0xC3, 0x33, 0xC0, 0xC3
};
constexpr unsigned char kClientOwnerExpected[] = {
	0x8B, 0x81, 0x30, 0x02, 0x00, 0x00, 0x83, 0xF8,
	0xFF, 0x74, 0x27, 0x0F, 0xB7, 0xC8, 0x81, 0xF9,
	0x00, 0x40, 0x00, 0x00, 0x73, 0x1C, 0x8B, 0xD1,
	0x48, 0x8B, 0x0D, 0x11, 0x8E, 0xAC, 0x00, 0xC1,
	0xE8, 0x10, 0x48, 0xC1, 0xE2, 0x05, 0x39, 0x44,
	0x0A, 0x10, 0x75, 0x06, 0x48, 0x8B, 0x44, 0x0A,
	0x08, 0xC3, 0x33, 0xC0, 0xC3
};
constexpr unsigned char kClientAliveExpected[] = {
	0xF6, 0x81, 0x48, 0x01, 0x00, 0x00, 0x01, 0x74,
	0x03, 0x32, 0xC0, 0xC3, 0x33, 0xC0, 0x38, 0x81,
	0x88, 0x04, 0x00, 0x00, 0x0F, 0x94, 0xC0, 0xC3
};
constexpr unsigned char kServerSmartAmmoCustomExpected[] = {
	0x39, 0x83, 0x7C, 0x04, 0x00, 0x00,
	0x75, 0x18, 0xE9, 0x8A, 0x03, 0x00, 0x00
};
constexpr unsigned char kServerSmartAmmoExpected[] = {
	0x39, 0x83, 0x7C, 0x04, 0x00, 0x00,
	0x0F, 0x84, 0x77, 0x03, 0x00, 0x00
};
constexpr unsigned char kServerSmartAmmoAcceptExpected[] = {
	0x4C, 0x8D, 0xB3, 0xD8, 0x01, 0x00, 0x00, 0x4D,
	0x85, 0xF6, 0x0F, 0x84, 0x67, 0x03, 0x00, 0x00
};
constexpr unsigned char kServerSmartAmmoRejectExpected[] = {
	0x4C, 0x8B, 0xB4, 0x24, 0x00, 0x01, 0x00, 0x00,
	0x0F, 0x28, 0xBC, 0x24, 0xA0, 0x00, 0x00, 0x00,
	0xB0, 0x01
};
constexpr unsigned char kServerIsPlayerExpected[] = {
	0x48, 0x8B, 0x01, 0xFF, 0xA0, 0xB0, 0x02, 0x00, 0x00
};
constexpr unsigned char kServerAliveExpected[] = {
	0xF6, 0x81, 0x60, 0x01, 0x00, 0x00, 0x01, 0x74,
	0x03, 0x32, 0xC0, 0xC3, 0x80, 0xB9, 0x61, 0x03,
	0x00, 0x00, 0x00, 0x0F, 0x94, 0xC0, 0xC3
};
constexpr unsigned char kServerOwnerExpected[] = {
	0x8B, 0x91, 0x48, 0x02, 0x00, 0x00, 0x83, 0xFA,
	0xFF, 0x74, 0x26, 0x0F, 0xB7, 0xC2, 0x3D, 0x00,
	0x40, 0x00, 0x00, 0x73, 0x1C, 0x48, 0x8D, 0x04,
	0x40, 0xC1, 0xEA, 0x10, 0x48, 0x03, 0xC0, 0x48,
	0x8D, 0x0D, 0x52, 0x61, 0xCC, 0x00, 0x39, 0x54,
	0xC1, 0x08, 0x75, 0x05, 0x48, 0x8B, 0x04, 0xC1,
	0xC3, 0x33, 0xC0, 0xC3
};
constexpr unsigned char kServerBossPlayerExpected[] = {
	0x8B, 0x91, 0x60, 0x05, 0x00, 0x00, 0x83, 0xFA,
	0xFF, 0x74, 0x2E, 0x0F, 0xB7, 0xC2, 0x3D, 0x00,
	0x40, 0x00, 0x00, 0x73, 0x24, 0x48, 0x8D, 0x0C,
	0x40, 0xC1, 0xEA, 0x10, 0x48, 0x03, 0xC9, 0x48,
	0x8D, 0x05, 0x92, 0x28, 0x98, 0x00, 0x39, 0x54,
	0xC8, 0x08, 0x75, 0x0D, 0x48, 0x8B, 0x0C, 0xC8,
	0x48, 0x85, 0xC9, 0x0F, 0x85, 0x37, 0xC3, 0x00,
	0x00, 0x33, 0xC0, 0xC3
};
constexpr std::uintptr_t kClientIsPlayerWrapperRva = 0x2F3950;
constexpr unsigned char kClientIsPlayerWrapperExpected[] = {
	0x48, 0x8B, 0x01, 0xFF, 0xA0, 0x38, 0x05, 0x00, 0x00
};
constexpr std::uintptr_t kServerObserverInitialPreconditionRva = 0x4FA1EB;
constexpr unsigned char kServerObserverInitialPreconditionExpected[] = {
	0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xFF, 0x90, 0xB0, 0x02, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x17, 0x49, 0x3B, 0xDC, 0x74, 0x22, 0x83, 0xBB, 0x1C, 0x16, 0x00, 0x00, 0x00, 0x75, 0x19, 0x80, 0xBB, 0x40, 0x16, 0x00, 0x00, 0x00, 0x74, 0x10
};
constexpr std::uintptr_t kServerObserverCyclePreconditionRva = 0x4FA3B1;
constexpr unsigned char kServerObserverCyclePreconditionExpected[] = {
	0x48, 0x85, 0xDB, 0x74, 0x3C, 0x48, 0x8B, 0x13, 0x48, 0x8B, 0xCB, 0xFF, 0x92, 0xB0, 0x02, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x17, 0x48, 0x3B, 0xDE, 0x74, 0x20, 0x83, 0xBB, 0x1C, 0x16, 0x00, 0x00, 0x00, 0x75, 0x17, 0x80, 0xBB, 0x40, 0x16, 0x00, 0x00, 0x00, 0x74, 0x0E
};

EntityGetterFn g_R1DeltaClientGetBossPlayer = nullptr;
EntityGetterFn g_R1DeltaClientGetOwner = nullptr;
EntityGetterFn g_R1DeltaServerGetOwner = nullptr;
EntityPredicateFn g_R1DeltaClientIsAlive = nullptr;
EntityPredicateFn g_R1DeltaServerIsAlive = nullptr;
std::uintptr_t g_R1DeltaServerBase = 0;
volatile LONG g_R1DeltaClientHooksReady = FALSE;
volatile LONG g_R1DeltaServerHooksReady = FALSE;
constexpr unsigned char kServerObserverInitialExpected[] = {
	0x8B, 0x83, 0x7C, 0x04, 0x00, 0x00,
	0x41, 0x39, 0x84, 0x24, 0x7C, 0x04, 0x00, 0x00,
	0x74, 0x44
};
constexpr unsigned char kServerObserverCycleExpected[] = {
	0x8B, 0x83, 0x7C, 0x04, 0x00, 0x00,
	0x39, 0x86, 0x7C, 0x04, 0x00, 0x00,
	0x74, 0x4C
};

struct PatchSpec
{
	std::uintptr_t rva;
	const unsigned char* expected;
	std::size_t size;
	void* bridge;
	const char* description;
};

struct PreparedPatch
{
	const PatchSpec* spec = nullptr;
	std::array<unsigned char, kMaximumPatchSize> replacement{};
	void* relay = nullptr;
};

void FfaTargetingLog(const char* format, ...)
{
	char buffer[512]{};
	va_list args;
	va_start(args, format);
	vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
	va_end(args);
	OutputDebugStringA(buffer);
}

bool IsReadableRange(const void* address, std::size_t size)
{
	if (!address || !size)
		return false;

	const auto begin = reinterpret_cast<std::uintptr_t>(address);
	const auto end = begin + size;
	if (end < begin)
		return false;

	std::uintptr_t cursor = begin;
	while (cursor < end) {
		MEMORY_BASIC_INFORMATION mbi{};
		if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)))
			return false;
		if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
			return false;

		const auto regionEnd = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
		if (regionEnd <= cursor)
			return false;
		cursor = std::min(regionEnd, end);
	}
	return true;
}

bool InvokeEntityVirtualPredicate(
	void* entity,
	std::size_t vtableByteOffset) noexcept
{
	if (!entity || (vtableByteOffset % sizeof(void*)) != 0)
		return false;

	auto** vtable = *reinterpret_cast<void***>(entity);
	if (!vtable)
		return false;

	const auto predicate = reinterpret_cast<EntityPredicateFn>(
		vtable[vtableByteOffset / sizeof(void*)]);
	return predicate && predicate(entity);
}

void* ResolveClientOwningPlayer(void* entity)
{
	return r1delta::ffa_targeting::ResolveOwningPlayer(
		entity,
		[](void* current) {
			return InvokeEntityVirtualPredicate(
				current,
				kClientIsPlayerVtableOffset);
		},
		[](void* current) -> void* {
			return g_R1DeltaClientGetBossPlayer
				? g_R1DeltaClientGetBossPlayer(current)
				: nullptr;
		},
		[](void* current) -> void* {
			return g_R1DeltaClientGetOwner
				? g_R1DeltaClientGetOwner(current)
				: nullptr;
		});
}

void* ResolveServerEntityHandle(std::uint32_t handle) noexcept
{
	if (handle == std::numeric_limits<std::uint32_t>::max()
		|| !g_R1DeltaServerBase) {
		return nullptr;
	}

	const std::uint32_t index = handle & 0xFFFF;
	if (index >= kMaximumServerEntities)
		return nullptr;

	const auto* entry = reinterpret_cast<const unsigned char*>(
		g_R1DeltaServerBase
		+ kServerEntityListRva
		+ static_cast<std::uintptr_t>(index) * kServerEntityEntrySize);
	const auto serial = *reinterpret_cast<const std::uint32_t*>(
		entry + sizeof(void*));
	if (serial != (handle >> 16))
		return nullptr;
	return *reinterpret_cast<void* const*>(entry);
}

void* ResolveServerBossPlayer(void* entity) noexcept
{
	if (!entity)
		return nullptr;

	const auto* bytes = static_cast<const unsigned char*>(entity);
	const auto handle = *reinterpret_cast<const std::uint32_t*>(
		bytes + kServerBossPlayerHandleOffset);
	return ResolveServerEntityHandle(handle);
}

void* ResolveServerOwningPlayer(void* entity)
{
	return r1delta::ffa_targeting::ResolveOwningPlayer(
		entity,
		[](void* current) {
			return InvokeEntityVirtualPredicate(
				current,
				kServerIsPlayerVtableOffset);
		},
		[](void* current) {
			return ResolveServerBossPlayer(current);
		},
		[](void* current) -> void* {
			return g_R1DeltaServerGetOwner
				? g_R1DeltaServerGetOwner(current)
				: nullptr;
		});
}

template <std::size_t Size>
bool ValidateRuntimeFunction(
	std::uintptr_t moduleBase,
	std::uintptr_t rva,
	const unsigned char (&expected)[Size],
	const char* moduleName,
	const char* description)
{
	const auto* address =
		reinterpret_cast<const unsigned char*>(moduleBase + rva);
	if (IsReadableRange(address, Size)
		&& std::memcmp(address, expected, Size) == 0) {
		return true;
	}

	FfaTargetingLog(
		"R1Delta: refusing FFA helper '%s' in %s at RVA 0x%llX: unexpected binary revision.\n",
		description,
		moduleName,
		static_cast<unsigned long long>(rva));
	return false;
}

struct CodeWriteResult
{
	bool bytesWritten = false;
	bool cacheFlushed = false;
	bool protectionRestored = false;
	DWORD originalProtection = 0;
	DWORD error = ERROR_SUCCESS;

	bool Succeeded() const noexcept
	{
		return bytesWritten && cacheFlushed && protectionRestored;
	}
};

CodeWriteResult WriteCode(
	void* destination,
	const void* source,
	std::size_t size)
{
	CodeWriteResult result{};
	if (!VirtualProtect(
		destination,
		size,
		PAGE_EXECUTE_READWRITE,
		&result.originalProtection)) {
		result.error = GetLastError();
		return result;
	}
	std::memcpy(destination, source, size);
	result.bytesWritten = true;
	result.cacheFlushed =
		FlushInstructionCache(GetCurrentProcess(), destination, size) != FALSE;
	if (!result.cacheFlushed)
		result.error = GetLastError();

	DWORD ignored = 0;
	result.protectionRestored =
		VirtualProtect(
			destination,
			size,
			result.originalProtection,
			&ignored) != FALSE;
	if (!result.protectionRestored && result.error == ERROR_SUCCESS)
		result.error = GetLastError();
	return result;
}

bool RelativeDisplacement(
	std::uintptr_t instruction,
	std::uintptr_t destination,
	std::int32_t& displacement)
{
	const auto delta = static_cast<std::int64_t>(destination)
		- static_cast<std::int64_t>(instruction + 5);
	if (delta < std::numeric_limits<std::int32_t>::min()
		|| delta > std::numeric_limits<std::int32_t>::max()) {
		return false;
	}
	displacement = static_cast<std::int32_t>(delta);
	return true;
}

std::uintptr_t AlignUp(std::uintptr_t value, std::uintptr_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

void* AllocateRelayNear(std::uintptr_t source, std::uintptr_t destination)
{
	SYSTEM_INFO systemInfo{};
	GetSystemInfo(&systemInfo);
	const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
	const auto pageSize = static_cast<std::size_t>(systemInfo.dwPageSize);
	const auto minimumApplicationAddress =
		reinterpret_cast<std::uintptr_t>(systemInfo.lpMinimumApplicationAddress);
	const auto maximumApplicationAddress =
		reinterpret_cast<std::uintptr_t>(systemInfo.lpMaximumApplicationAddress);
	constexpr std::uintptr_t maximumDistance =
		static_cast<std::uintptr_t>(std::numeric_limits<std::int32_t>::max());

	const auto minimumAddress = source > maximumDistance
		? std::max(minimumApplicationAddress, source - maximumDistance)
		: minimumApplicationAddress;
	const auto maximumAddress = std::min(
		maximumApplicationAddress,
		source <= maximumApplicationAddress - maximumDistance
			? source + maximumDistance
			: maximumApplicationAddress);

	std::uintptr_t cursor = minimumAddress;
	while (cursor < maximumAddress) {
		MEMORY_BASIC_INFORMATION mbi{};
		if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi))) {
			cursor = AlignUp(cursor + 1, granularity);
			continue;
		}

		const auto regionBegin = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
		const auto regionEnd = regionBegin + mbi.RegionSize;
		if (mbi.State == MEM_FREE) {
			const auto candidate = AlignUp(std::max(regionBegin, minimumAddress), granularity);
			if (candidate < maximumAddress
				&& candidate <= regionEnd
				&& pageSize <= regionEnd - candidate) {
				std::int32_t ignoredDisplacement = 0;
				if (RelativeDisplacement(source, candidate, ignoredDisplacement)) {
					void* relay = VirtualAlloc(
						reinterpret_cast<void*>(candidate),
						pageSize,
						MEM_COMMIT | MEM_RESERVE,
						PAGE_READWRITE);
					if (relay) {
						std::array<unsigned char, kAbsoluteRelaySize> code{
							0xFF, 0x25, 0x00, 0x00, 0x00, 0x00
						};
						std::memcpy(code.data() + 6, &destination, sizeof(destination));
						std::memcpy(relay, code.data(), code.size());

						DWORD oldProtect = 0;
						if (!VirtualProtect(relay, pageSize, PAGE_EXECUTE_READ, &oldProtect)) {
							VirtualFree(relay, 0, MEM_RELEASE);
							return nullptr;
						}
						if (!FlushInstructionCache(
							GetCurrentProcess(),
							relay,
							code.size())) {
							VirtualFree(relay, 0, MEM_RELEASE);
							return nullptr;
						}
						return relay;
					}
				}
			}
		}

		if (regionEnd <= cursor)
			break;
		cursor = regionEnd;
	}
	return nullptr;
}

bool PreparePatch(
	std::uintptr_t moduleBase,
	const PatchSpec& spec,
	PreparedPatch& prepared)
{
	prepared.spec = &spec;
	prepared.replacement.fill(0x90);

	const auto source = moduleBase + spec.rva;
	const auto bridge = reinterpret_cast<std::uintptr_t>(spec.bridge);
	std::uintptr_t jumpTarget = bridge;
	std::int32_t displacement = 0;
	if (!RelativeDisplacement(source, jumpTarget, displacement)) {
		prepared.relay = AllocateRelayNear(source, bridge);
		if (!prepared.relay) {
			FfaTargetingLog(
				"R1Delta: failed to allocate a near relay for FFA patch '%s' at RVA 0x%llX.\n",
				spec.description,
				static_cast<unsigned long long>(spec.rva));
			return false;
		}
		jumpTarget = reinterpret_cast<std::uintptr_t>(prepared.relay);
		if (!RelativeDisplacement(source, jumpTarget, displacement)) {
			VirtualFree(prepared.relay, 0, MEM_RELEASE);
			prepared.relay = nullptr;
			return false;
		}
	}

	prepared.replacement[0] = 0xE9;
	std::memcpy(prepared.replacement.data() + 1, &displacement, sizeof(displacement));
	return true;
}

template <std::size_t Count>
bool InstallPatchSet(
	std::uintptr_t moduleBase,
	const std::array<PatchSpec, Count>& specs,
	const char* moduleName)
{
	std::array<PreparedPatch, Count> prepared{};
	for (std::size_t index = 0; index < Count; ++index) {
		const PatchSpec& spec = specs[index];
		const auto* address = reinterpret_cast<const unsigned char*>(moduleBase + spec.rva);
		if (spec.size < 5 || spec.size > kMaximumPatchSize
			|| !IsReadableRange(address, spec.size)
			|| std::memcmp(address, spec.expected, spec.size) != 0) {
			FfaTargetingLog(
				"R1Delta: refusing FFA patch '%s' in %s at RVA 0x%llX: unexpected binary revision.\n",
				spec.description,
				moduleName,
				static_cast<unsigned long long>(spec.rva));
			return false;
		}
	}

	for (std::size_t index = 0; index < Count; ++index) {
		if (!PreparePatch(moduleBase, specs[index], prepared[index])) {
			for (std::size_t relayIndex = 0; relayIndex <= index; ++relayIndex) {
				if (prepared[relayIndex].relay)
					VirtualFree(prepared[relayIndex].relay, 0, MEM_RELEASE);
			}
			return false;
		}
	}

	std::size_t completed = 0;
	std::size_t written = 0;
	CodeWriteResult failure{};
	std::array<CodeWriteResult, Count> writeResults{};
	for (; completed < Count; ++completed) {
		const PatchSpec& spec = specs[completed];
		const CodeWriteResult result = WriteCode(
			reinterpret_cast<void*>(moduleBase + spec.rva),
			prepared[completed].replacement.data(),
			spec.size);
		writeResults[completed] = result;
		if (result.bytesWritten)
			written = completed + 1;
		if (!result.Succeeded()) {
			failure = result;
			break;
		}
	}

	if (completed != Count) {
		FfaTargetingLog(
			"R1Delta: failed to write FFA patch '%s' in %s (Win32 error %lu); rolling back.\n",
			specs[completed].description,
			moduleName,
			failure.error);

		bool rollbackVerified = true;
		while (written > 0) {
			--written;
			const PatchSpec& spec = specs[written];
			void* destination =
				reinterpret_cast<void*>(moduleBase + spec.rva);
			const CodeWriteResult rollback = WriteCode(
				destination,
				spec.expected,
				spec.size);
			const bool bytesRestored =
				rollback.bytesWritten
				&& rollback.cacheFlushed
				&& std::memcmp(destination, spec.expected, spec.size) == 0;

			DWORD ignored = 0;
			const bool originalProtectionRestored =
				writeResults[written].originalProtection != 0
				&& VirtualProtect(
					destination,
					spec.size,
					writeResults[written].originalProtection,
					&ignored) != FALSE;
			if (!bytesRestored || !originalProtectionRestored) {
				rollbackVerified = false;
				const DWORD rollbackError =
					rollback.error != ERROR_SUCCESS
						? rollback.error
						: GetLastError();
				FfaTargetingLog(
					"R1Delta: incomplete rollback of FFA patch '%s' in %s (bytes=%d, protection=%d, Win32 error %lu).\n",
					spec.description,
					moduleName,
					bytesRestored ? 1 : 0,
					originalProtectionRestored ? 1 : 0,
					rollbackError);
			}
		}

		if (rollbackVerified) {
			for (PreparedPatch& patch : prepared) {
				if (patch.relay)
					VirtualFree(patch.relay, 0, MEM_RELEASE);
			}
		}
		else {
			FfaTargetingLog(
				"R1Delta: retaining FFA relay pages for %s because a patched jump may remain live.\n",
				moduleName);
		}
		return false;
	}

	FfaTargetingLog("R1Delta: installed checked FFA targeting hooks for %s.\n", moduleName);
	return true;
}

}

extern "C" unsigned char __fastcall R1DeltaResolveFfaClientRelation(
	void* first,
	void* second)
{
	const bool ffaBased = r1delta::ffa_targeting::IsClientFfaBased();
	const bool hooksReady = InterlockedCompareExchange(
		&g_R1DeltaClientHooksReady,
		FALSE,
		FALSE) != FALSE;
	if (!ffaBased || !hooksReady || !first || !second)
		return static_cast<unsigned char>(
			r1delta::ffa_targeting::FfaOwnerRelation::Native);

	void* firstOwner = ResolveClientOwningPlayer(first);
	void* secondOwner = ResolveClientOwningPlayer(second);
	const auto relation = r1delta::ffa_targeting::ResolveFfaOwnerRelation(
		ffaBased,
		firstOwner != nullptr,
		true,
		secondOwner != nullptr,
		true,
		firstOwner == secondOwner,
		false);
	return static_cast<unsigned char>(relation);
}

extern "C" unsigned char __fastcall R1DeltaResolveLiveFfaClientRelation(
	void* first,
	void* second)
{
	const bool ffaBased = r1delta::ffa_targeting::IsClientFfaBased();
	const bool hooksReady = InterlockedCompareExchange(
		&g_R1DeltaClientHooksReady,
		FALSE,
		FALSE) != FALSE;
	if (!ffaBased || !hooksReady || !first || !second)
		return static_cast<unsigned char>(
			r1delta::ffa_targeting::FfaOwnerRelation::Native);

	void* firstOwner = ResolveClientOwningPlayer(first);
	void* secondOwner = ResolveClientOwningPlayer(second);
	const bool firstAlive = firstOwner
		&& g_R1DeltaClientIsAlive
		&& g_R1DeltaClientIsAlive(firstOwner);
	const bool secondAlive = secondOwner
		&& g_R1DeltaClientIsAlive
		&& g_R1DeltaClientIsAlive(secondOwner);
	const auto relation = r1delta::ffa_targeting::ResolveFfaOwnerRelation(
		ffaBased,
		firstOwner != nullptr,
		firstAlive,
		secondOwner != nullptr,
		secondAlive,
		firstOwner == secondOwner,
		true);
	return static_cast<unsigned char>(relation);
}

extern "C" unsigned char __fastcall R1DeltaResolveLiveFfaServerRelation(
	void* first,
	void* second)
{
	const bool ffaBased = r1delta::ffa_targeting::IsServerFfaBased();
	const bool hooksReady = InterlockedCompareExchange(
		&g_R1DeltaServerHooksReady,
		FALSE,
		FALSE) != FALSE;
	if (!ffaBased || !hooksReady || !first || !second)
		return static_cast<unsigned char>(
			r1delta::ffa_targeting::FfaOwnerRelation::Native);

	void* firstOwner = ResolveServerOwningPlayer(first);
	void* secondOwner = ResolveServerOwningPlayer(second);
	const bool firstAlive = firstOwner
		&& g_R1DeltaServerIsAlive
		&& g_R1DeltaServerIsAlive(firstOwner);
	const bool secondAlive = secondOwner
		&& g_R1DeltaServerIsAlive
		&& g_R1DeltaServerIsAlive(secondOwner);
	const auto relation = r1delta::ffa_targeting::ResolveFfaOwnerRelation(
		ffaBased,
		firstOwner != nullptr,
		firstAlive,
		secondOwner != nullptr,
		secondAlive,
		firstOwner == secondOwner,
		true);
	return static_cast<unsigned char>(relation);
}

extern "C" unsigned char __fastcall R1DeltaIsValidFfaObserverTarget(
	void* observer,
	void* candidate)
{
	const bool ffaBased = r1delta::ffa_targeting::IsServerFfaBased();
	const bool hooksReady = InterlockedCompareExchange(
		&g_R1DeltaServerHooksReady,
		FALSE,
		FALSE) != FALSE;
	if (!ffaBased || !hooksReady || !observer || !candidate
		|| observer == candidate) {
		return 0;
	}

	const bool observerIsPlayer =
		InvokeEntityVirtualPredicate(observer, kServerIsPlayerVtableOffset);
	const bool candidateIsPlayer =
		InvokeEntityVirtualPredicate(candidate, kServerIsPlayerVtableOffset);
	if (!observerIsPlayer || !candidateIsPlayer)
		return 0;

	const auto* candidateBytes =
		static_cast<const unsigned char*>(candidate);
	const bool candidateObserverModeZero =
		*reinterpret_cast<const int*>(candidateBytes + 0x161C) == 0;
	const bool candidateActive = candidateBytes[0x1640] != 0;
	return r1delta::ffa_targeting::ShouldAcceptObserverTarget(
		false,
		ffaBased,
		observerIsPlayer,
		candidateIsPlayer,
		false,
		candidateObserverModeZero,
		candidateActive)
		? 1
		: 0;
}

namespace r1delta::ffa_targeting
{
void SetClientFfaBased(bool enabled) noexcept
{
	InterlockedExchange(&g_R1DeltaClientFfaBased, enabled ? TRUE : FALSE);
}

void SetServerFfaBased(bool enabled) noexcept
{
	InterlockedExchange(&g_R1DeltaServerFfaBased, enabled ? TRUE : FALSE);
}

bool IsClientFfaBased() noexcept
{
	return InterlockedCompareExchange(
		&g_R1DeltaClientFfaBased, FALSE, FALSE) != FALSE;
}

bool IsServerFfaBased() noexcept
{
	return InterlockedCompareExchange(
		&g_R1DeltaServerFfaBased, FALSE, FALSE) != FALSE;
}

bool InstallClientHooks(std::uintptr_t clientBase)
{
	InterlockedExchange(&g_R1DeltaClientHooksReady, FALSE);
	if (!clientBase
		|| !ValidateRuntimeFunction(
			clientBase,
			kClientBossPlayerRva,
			kClientBossPlayerExpected,
			"client.dll",
			"client boss-player resolver")
		|| !ValidateRuntimeFunction(
			clientBase,
			kClientOwnerRva,
			kClientOwnerExpected,
			"client.dll",
			"client owner resolver")
		|| !ValidateRuntimeFunction(
			clientBase,
			0x4614A2,
			kClientSmartAmmoInputsExpected,
			"client.dll",
			"client projectile smart-ammo team operands")
		|| !ValidateRuntimeFunction(
			clientBase,
			0x4614C6,
			kClientSmartAmmoInputsExpected,
			"client.dll",
			"client NPC/player smart-ammo team operands")
		|| !ValidateRuntimeFunction(
			clientBase,
			0x3161E2,
			kClientMinimapVisibilityInputsExpected,
			"client.dll",
			"client minimap per-viewer visibility operands")
		|| !ValidateRuntimeFunction(
			clientBase,
			0x317ED9,
			kClientMinimapDefaultVisibilityInputsExpected,
			"client.dll",
			"client minimap default-visibility team operands")
		|| !ValidateRuntimeFunction(
			clientBase,
			kClientIsPlayerWrapperRva,
			kClientIsPlayerWrapperExpected,
			"client.dll",
			"client IsPlayer wrapper")
		|| !ValidateRuntimeFunction(
			clientBase,
			kClientAliveRva,
			kClientAliveExpected,
			"client.dll",
			"client resolved-owner liveness predicate")) {
		return false;
	}
	g_R1DeltaClientGetBossPlayer =
		reinterpret_cast<EntityGetterFn>(clientBase + kClientBossPlayerRva);
	g_R1DeltaClientGetOwner =
		reinterpret_cast<EntityGetterFn>(clientBase + kClientOwnerRva);
	g_R1DeltaClientIsAlive =
		reinterpret_cast<EntityPredicateFn>(clientBase + kClientAliveRva);
	g_R1DeltaClientSmartAmmoAccept = clientBase + 0x4614E9;
	g_R1DeltaClientSmartAmmoReject = clientBase + 0x4617FD;
	g_R1DeltaClientMinimapContinue = clientBase + 0x317B58;
	g_R1DeltaClientMinimapVisibilityContinue = clientBase + 0x316201;
	g_R1DeltaClientMinimapDefaultVisibilityShow = clientBase + 0x318157;
	g_R1DeltaClientMinimapDefaultVisibilityEnemy = clientBase + 0x317EE9;

	const std::array<PatchSpec, 5> specs{{
		{ 0x4614BD, kClientSmartAmmoProjectileExpected,
			sizeof(kClientSmartAmmoProjectileExpected),
			reinterpret_cast<void*>(&R1DeltaClientSmartAmmoFfaBridge),
			"client projectile smart-ammo team gate" },
		{ 0x4614E1, kClientSmartAmmoExpected,
			sizeof(kClientSmartAmmoExpected),
			reinterpret_cast<void*>(&R1DeltaClientSmartAmmoFfaBridge),
			"client NPC/player smart-ammo team gate" },
		{ 0x317B50, kClientMinimapExpected,
			sizeof(kClientMinimapExpected),
			reinterpret_cast<void*>(&R1DeltaClientMinimapFfaBridge),
			"client minimap relationship classification" },
		{ 0x3161FA, kClientMinimapVisibilityExpected,
			sizeof(kClientMinimapVisibilityExpected),
			reinterpret_cast<void*>(&R1DeltaClientMinimapVisibilityFfaBridge),
			"client FFA minimap per-viewer visibility mask" },
		{ 0x317EE1, kClientMinimapDefaultVisibilityExpected,
			sizeof(kClientMinimapDefaultVisibilityExpected),
			reinterpret_cast<void*>(&R1DeltaClientMinimapDefaultVisibilityFfaBridge),
			"client FFA minimap default-visibility relationship gate" },
	}};
	const bool installed = InstallPatchSet(clientBase, specs, "client.dll");
	InterlockedExchange(
		&g_R1DeltaClientHooksReady,
		installed ? TRUE : FALSE);
	return installed;
}

bool InstallServerHooks(std::uintptr_t serverBase)
{
	InterlockedExchange(&g_R1DeltaServerHooksReady, FALSE);
	if (!serverBase
		|| !ValidateRuntimeFunction(
			serverBase,
			kServerIsPlayerRva,
			kServerIsPlayerExpected,
			"server.dll",
			"server IsPlayer dispatch")
		|| !ValidateRuntimeFunction(
			serverBase,
			kServerAliveRva,
			kServerAliveExpected,
			"server.dll",
			"server resolved-owner liveness predicate")
		|| !ValidateRuntimeFunction(
			serverBase,
			kServerOwnerRva,
			kServerOwnerExpected,
			"server.dll",
			"server owner resolver")
		|| !ValidateRuntimeFunction(
			serverBase,
			0x3BFD10,
			kServerBossPlayerExpected,
			"server.dll",
			"server boss-player handle accessor")
		|| !ValidateRuntimeFunction(
			serverBase,
			0x5C01A6,
			kServerSmartAmmoAcceptExpected,
			"server.dll",
			"server smart-ammo accept continuation")
		|| !ValidateRuntimeFunction(
			serverBase,
			0x5C051D,
			kServerSmartAmmoRejectExpected,
			"server.dll",
			"server smart-ammo reject continuation")
		|| !ValidateRuntimeFunction(
			serverBase,
			kServerObserverInitialPreconditionRva,
			kServerObserverInitialPreconditionExpected,
			"server.dll",
			"server initial observer-target precondition")
		|| !ValidateRuntimeFunction(
			serverBase,
			kServerObserverCyclePreconditionRva,
			kServerObserverCyclePreconditionExpected,
			"server.dll",
			"server observer cycling precondition")) {
		return false;
	}

	g_R1DeltaServerGetOwner =
		reinterpret_cast<EntityGetterFn>(serverBase + kServerOwnerRva);
	g_R1DeltaServerIsAlive =
		reinterpret_cast<EntityPredicateFn>(serverBase + kServerAliveRva);
	g_R1DeltaServerBase = serverBase;
	g_R1DeltaServerSmartAmmoAccept = serverBase + 0x5C01A6;
	g_R1DeltaServerSmartAmmoReject = serverBase + 0x5C051D;
	g_R1DeltaServerObserverInitialAccept = serverBase + 0x4FA266;
	g_R1DeltaServerObserverInitialReject = serverBase + 0x4FA222;
	g_R1DeltaServerObserverCycleAccept = serverBase + 0x4FA437;
	g_R1DeltaServerObserverCycleReject = serverBase + 0x4FA3EB;

	const std::array<PatchSpec, 4> specs{{
		{ 0x5C0186, kServerSmartAmmoCustomExpected,
			sizeof(kServerSmartAmmoCustomExpected),
			reinterpret_cast<void*>(&R1DeltaServerSmartAmmoFfaBridge),
			"server custom/projectile smart-ammo team gate" },
		{ 0x5C019A, kServerSmartAmmoExpected,
			sizeof(kServerSmartAmmoExpected),
			reinterpret_cast<void*>(&R1DeltaServerSmartAmmoFfaBridge),
			"server NPC/player smart-ammo team gate" },
		{ 0x4FA212, kServerObserverInitialExpected,
			sizeof(kServerObserverInitialExpected),
			reinterpret_cast<void*>(&R1DeltaServerObserverInitialFfaBridge),
			"server initial observer-target team gate" },
		{ 0x4FA3DD, kServerObserverCycleExpected,
			sizeof(kServerObserverCycleExpected),
			reinterpret_cast<void*>(&R1DeltaServerObserverCycleFfaBridge),
			"server observer-target cycling team gate" },
	}};
	const bool installed = InstallPatchSet(serverBase, specs, "server.dll");
	InterlockedExchange(
		&g_R1DeltaServerHooksReady,
		installed ? TRUE : FALSE);
	return installed;
}
}
