// Lifetime of the DX11 material system's reserved texture-alias records.
//
// R1 retail materialsystem_dx11.dll: timestamp 54874238, image size 3E0000.
//
// Texture handles 1..31 are reserved at device initialization (RVA 0x14E20) as alias records. The native alias writer
// (RVA 0x12790) fills one by copying another handle's 72-byte record by value, without taking COM references. The
// well-known render-target setup (RVA 0x88D90) aliases handles 13 and 14 to _rt_FullFrameFB and _rt_FullFrameFB1 that
// way, and its teardown (RVA 0x7EFE0) clears them before it releases the textures. A render target can also be
// reallocated on its own, through CTexture's release (RVA 0x63E80) and the texture-creation path, for example when
// the frame buffer changes size. Nothing clears the alias on that path: it keeps the released COM objects, the next
// tonemap histogram dispatch (and any frame-buffer sampler) binds a shader-resource view whose resource is gone, and
// the driver reads through the null resource inside CSSetShaderResources.
//
// Before a record is released (RVA 0x129A0 for one handle, RVA 0x129B0 for a batch), every alias that copies it is
// cleared. When that source handle is created again, the alias the native code last asked for is written again from
// the new record. Native alias writes are tracked so that an alias the native code moved or cleared is never revived.

#include "texture_alias_lifetime.h"

#include "core.h"
#include "logging.h"
#include "materialsystem_dx11_shader_record.h"

#include <MinHook.h>
#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
using r1delta::materialsystem_dx11::kRetailImageSize;
using r1delta::materialsystem_dx11::kRetailTimeDateStamp;

constexpr std::uintptr_t kWriteTextureAliasRva = 0x12790;
constexpr std::uintptr_t kReleaseTextureRva = 0x129A0;
constexpr std::uintptr_t kReleaseTexturesRva = 0x129B0;
constexpr std::uintptr_t kCreateTextureRva = 0x163F0;
constexpr std::uintptr_t kTextureBucketTableRva = 0x2983F8;
constexpr std::uintptr_t kTextureBucketCountRva = 0x2983D4;

constexpr std::size_t kTextureRecordsPerBucket = 128;
constexpr std::int16_t kAliasHandleCount = 31;
constexpr unsigned kLoggedEventLimit = 32;

// The owned D3D11 resource leads the record; an alias holds the same pointer as its source.
struct TextureRecord
{
	void* resource;
	std::uint8_t remainder[64];
};
static_assert(sizeof(TextureRecord) == 72);

struct TextureBucket
{
	std::uint64_t allocatedSlots[kTextureRecordsPerBucket / 64];
	TextureRecord records[kTextureRecordsPerBucket];
};
static_assert(offsetof(TextureBucket, records) == 16);

// What the native code last aliased into one reserved handle, and whether a release cleared it since.
struct AliasState
{
	std::atomic<std::int16_t> sourceHandle;
	std::atomic<bool> clearedByRelease;
};

constexpr unsigned char kWriteTextureAliasPrologue[] = {
	0x83, 0x3D, 0x3D, 0x5C, 0x28, 0x00, 0x00, // cmp dword ptr [rip+0x285C3D], 0
	0x0F, 0x84, 0xD5, 0x00, 0x00, 0x00,       // jz +0x12872
};
constexpr unsigned char kReleaseTexturePrologue[] = {
	0xFF, 0x05, 0xEA, 0x59, 0x28, 0x00, // inc dword ptr [rip+0x2859EA]
	0xE9, 0x95, 0xF7, 0xFF, 0xFF,       // jmp +0x12140
};
constexpr unsigned char kReleaseTexturesPrologue[] = {
	0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x4B, 0x08, 0x48,
	0x83, 0xEC, 0x68,
};
constexpr unsigned char kCreateTexturePrologue[] = {
	0x48, 0x8B, 0xC4, 0x44, 0x89, 0x48, 0x20, 0x44,
	0x89, 0x40, 0x18, 0x89, 0x50, 0x10,
};

// The alias writer takes the zero-based reserved slot; a zero source clears the alias.
using WriteTextureAliasFunction = void(__fastcall*)(std::int16_t aliasSlot, std::int16_t sourceHandle);
using ReleaseTextureFunction = void*(__fastcall*)(std::int16_t handle);
using ReleaseTexturesFunction = std::uint64_t(__fastcall*)(
	const std::int16_t* handles, std::uint32_t count);
using CreateTextureFunction = std::int64_t(__fastcall*)(
	std::int16_t handle, std::uint32_t width, std::uint32_t height, std::uint32_t depth,
	int imageFormat, std::uint32_t mipCount, std::uint32_t copyCount, int flags,
	std::int64_t initialData, int sourceFormat, std::int64_t debugName, char budgetGroup);

std::uintptr_t s_base;
WriteTextureAliasFunction s_originalWriteAlias;
ReleaseTextureFunction s_originalReleaseTexture;
ReleaseTexturesFunction s_originalReleaseTextures;
CreateTextureFunction s_originalCreateTexture;
AliasState s_aliases[kAliasHandleCount];
std::atomic<unsigned> s_loggedEvents;
bool s_installed;

[[noreturn]] void AliasLifetimeError(const char* detail)
{
	char buffer[320]{};
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: materialsystem_dx11 texture-alias lifetime invariant failed: %s\n",
		detail);
	OutputDebugStringA(buffer);
	Error("%s", buffer);
	__fastfail(FAST_FAIL_FATAL_APP_EXIT);
}

bool HasTextureRecords()
{
	return *reinterpret_cast<const std::uint32_t*>(s_base + kTextureBucketCountRva) != 0;
}

const TextureRecord& RecordFor(std::int16_t handle)
{
	const auto index = static_cast<std::uint32_t>(handle - 1);
	const auto* const* const buckets =
		*reinterpret_cast<const TextureBucket* const* const*>(s_base + kTextureBucketTableRva);
	return buckets[index / kTextureRecordsPerBucket]->records[index % kTextureRecordsPerBucket];
}

AliasState& AliasFor(std::int16_t aliasHandle)
{
	return s_aliases[aliasHandle - 1];
}

void LogAliasEvent(const char* action, std::int16_t aliasHandle, std::int16_t sourceHandle)
{
	if (s_loggedEvents.fetch_add(1, std::memory_order_relaxed) >= kLoggedEventLimit)
		return;
	Msg("R1Delta: materialsystem_dx11 %s texture alias %d of handle %d\n", action, aliasHandle, sourceHandle);
}

void __fastcall WriteTextureAliasDetour(std::int16_t aliasSlot, std::int16_t sourceHandle)
{
	if (aliasSlot >= 0 && aliasSlot < kAliasHandleCount) {
		AliasState& alias = s_aliases[aliasSlot];
		alias.sourceHandle.store(sourceHandle, std::memory_order_relaxed);
		alias.clearedByRelease.store(false, std::memory_order_relaxed);
	}
	s_originalWriteAlias(aliasSlot, sourceHandle);
}

void ClearAliasesOf(std::int16_t handle)
{
	if (handle <= 0 || !HasTextureRecords())
		return;
	const void* const resource = RecordFor(handle).resource;
	if (!resource)
		return;
	for (std::int16_t aliasHandle = 1; aliasHandle <= kAliasHandleCount; ++aliasHandle) {
		if (aliasHandle == handle || RecordFor(aliasHandle).resource != resource)
			continue;
		s_originalWriteAlias(static_cast<std::int16_t>(aliasHandle - 1), 0);
		AliasState& alias = AliasFor(aliasHandle);
		alias.clearedByRelease.store(
			alias.sourceHandle.load(std::memory_order_relaxed) == handle, std::memory_order_relaxed);
		LogAliasEvent("cleared", aliasHandle, handle);
	}
}

void RestoreAliasesOf(std::int16_t handle)
{
	for (std::int16_t aliasHandle = 1; aliasHandle <= kAliasHandleCount; ++aliasHandle) {
		AliasState& alias = AliasFor(aliasHandle);
		if (alias.sourceHandle.load(std::memory_order_relaxed) != handle
			|| !alias.clearedByRelease.exchange(false, std::memory_order_relaxed)) {
			continue;
		}
		s_originalWriteAlias(static_cast<std::int16_t>(aliasHandle - 1), handle);
		LogAliasEvent("restored", aliasHandle, handle);
	}
}

void* __fastcall ReleaseTextureDetour(std::int16_t handle)
{
	ClearAliasesOf(handle);
	return s_originalReleaseTexture(handle);
}

std::uint64_t __fastcall ReleaseTexturesDetour(const std::int16_t* handles, std::uint32_t count)
{
	for (std::uint32_t index = 0; index < count; ++index)
		ClearAliasesOf(handles[index]);
	return s_originalReleaseTextures(handles, count);
}

std::int64_t __fastcall CreateTextureDetour(
	std::int16_t handle, std::uint32_t width, std::uint32_t height, std::uint32_t depth,
	int imageFormat, std::uint32_t mipCount, std::uint32_t copyCount, int flags,
	std::int64_t initialData, int sourceFormat, std::int64_t debugName, char budgetGroup)
{
	const std::int64_t result = s_originalCreateTexture(handle, width, height, depth, imageFormat, mipCount,
		copyCount, flags, initialData, sourceFormat, debugName, budgetGroup);
	RestoreAliasesOf(handle);
	return result;
}

struct HookTarget
{
	const char* name;
	std::uintptr_t rva;
	const unsigned char* prologue;
	std::size_t prologueSize;
	void* detour;
	void** original;
};

bool IsRetailImage(std::uintptr_t base)
{
	const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
		return false;
	const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
	return nt->Signature == IMAGE_NT_SIGNATURE
		&& nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64
		&& nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC
		&& nt->FileHeader.TimeDateStamp == kRetailTimeDateStamp
		&& nt->OptionalHeader.SizeOfImage == kRetailImageSize;
}

void InstallHook(const HookTarget& hook)
{
	void* const target = reinterpret_cast<void*>(s_base + hook.rva);
	const MH_STATUS created = MH_CreateHook(target, hook.detour, hook.original);
	const bool hasOriginal = *hook.original != nullptr;
	const MH_STATUS enabled = created == MH_OK && hasOriginal ? MH_EnableHook(target) : MH_ERROR_NOT_CREATED;
	char buffer[320]{};
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: materialsystem_dx11 texture-alias %s hook at RVA 0x%llX: create=%d enable=%d target=%p original=%p\n",
		hook.name,
		static_cast<unsigned long long>(hook.rva),
		static_cast<int>(created),
		static_cast<int>(enabled),
		target,
		*hook.original);
	OutputDebugStringA(buffer);
	if (created != MH_OK || !hasOriginal)
		AliasLifetimeError("could not create a texture-alias hook");
	if (enabled != MH_OK && enabled != MH_ERROR_ENABLED)
		AliasLifetimeError("could not enable a texture-alias hook");
}
} // namespace

void InstallTextureAliasLifetime(std::uintptr_t materialSystemBase)
{
	if (!materialSystemBase
		|| s_installed
		|| GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015) {
		return;
	}
	if (!IsRetailImage(materialSystemBase))
		AliasLifetimeError("unsupported image");

	const HookTarget hooks[] = {
		{"write", kWriteTextureAliasRva, kWriteTextureAliasPrologue, sizeof(kWriteTextureAliasPrologue),
			reinterpret_cast<void*>(&WriteTextureAliasDetour), reinterpret_cast<void**>(&s_originalWriteAlias)},
		{"release", kReleaseTextureRva, kReleaseTexturePrologue, sizeof(kReleaseTexturePrologue),
			reinterpret_cast<void*>(&ReleaseTextureDetour), reinterpret_cast<void**>(&s_originalReleaseTexture)},
		{"batch release", kReleaseTexturesRva, kReleaseTexturesPrologue, sizeof(kReleaseTexturesPrologue),
			reinterpret_cast<void*>(&ReleaseTexturesDetour), reinterpret_cast<void**>(&s_originalReleaseTextures)},
		{"create", kCreateTextureRva, kCreateTexturePrologue, sizeof(kCreateTexturePrologue),
			reinterpret_cast<void*>(&CreateTextureDetour), reinterpret_cast<void**>(&s_originalCreateTexture)},
	};
	// Clearing without restoring, or the reverse, would leave the aliases half managed: verify every target first.
	for (const HookTarget& hook : hooks) {
		if (std::memcmp(reinterpret_cast<const void*>(materialSystemBase + hook.rva), hook.prologue, hook.prologueSize) != 0)
			AliasLifetimeError(hook.name);
	}

	s_base = materialSystemBase;
	for (const HookTarget& hook : hooks)
		InstallHook(hook);
	s_installed = true;
}
