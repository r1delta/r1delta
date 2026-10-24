#include "datacache_header_ownership.h"

#include "core.h"
#include "logging.h"
#include "engine/filesystem/vpk_async_precache_entry.h"

#include <MinHook.h>
#include <Windows.h>
#include <intrin.h>
#include <cstddef>
#include <cstring>

namespace
{
using r1delta::vpk_async_precache::ResourceEntryNode;
using r1delta::vpk_async_precache::ResourceLookup;

// R1 retail datacache.dll: timestamp 54874271, image size D3000,
// PDB 351FE6A3-746E-437B-96A9-0D87C58D4FDE age 16.
constexpr std::uintptr_t kImportRva = 0x1EF40;
constexpr std::uintptr_t kAllocateRva = 0x203E0;
constexpr std::uintptr_t kActualModelNameRva = 0x1AA10;
constexpr std::uintptr_t kFileSystemGlobalRva = 0xC4118;
constexpr std::uintptr_t kShutdownRva = 0x1F900;
constexpr std::uintptr_t kHardwareReferenceVtableRva = 0x95100;
constexpr std::uintptr_t kGetPackStoreRva = 0x9E80;
constexpr std::size_t kGetPackStoreVtableOffset = 0x2F0;
constexpr unsigned char kShutdownPrologue[] = {
	0x40, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x0F, 0xB6,
	0x81, 0x00, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xF1,
};
constexpr unsigned char kGetPackStoreCode[] = {
	0x85, 0xD2, 0x78, 0x17, 0x3B, 0x91, 0xC0, 0x03, 0x00, 0x00,
	0x7D, 0x0F, 0x48, 0x8B, 0x81, 0xA8, 0x03, 0x00, 0x00,
	0x48, 0x63, 0xD2, 0x48, 0x8B, 0x04, 0xD0, 0xC3, 0x33, 0xC0, 0xC3,
};
constexpr std::size_t kHeaderHandleOffset = 0x158;
constexpr std::size_t kHeaderLengthOffset = 0x4C;
constexpr unsigned char kImportPrologue[] = {
	0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74,
	0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20,
};
// GetStudioHdr holds node+0xC, lends its decoded reference to the record,
// then calls the importer and republishes record->header with another AddRef.
constexpr unsigned char kDecodedAdmission[] = {
	0x85, 0xC0, 0x48, 0x8B, 0x45, 0xE8, 0x75, 0x18,
	0x48, 0x8B, 0x08, 0x49, 0x89, 0x0C, 0x24, 0xF0,
	0xFF, 0x41, 0x08, 0x49, 0x8B, 0x04, 0x24, 0x4C,
	0x8B, 0x48, 0x10, 0xE9, 0x26, 0x01, 0x00, 0x00,
};
constexpr unsigned char kPublication[] = {
	0x40, 0x38, 0x7D, 0x28, 0x74, 0x23, 0x48, 0x8B,
	0x45, 0xE8, 0x49, 0x8B, 0x0C, 0x24, 0x48, 0x89,
	0x08, 0x48, 0x8B, 0x45, 0xE8, 0x89, 0x78, 0x08,
	0x49, 0x8B, 0x04, 0x24, 0xF0, 0xFF, 0x40, 0x08,
	0x48, 0x8B, 0x4D, 0xE8, 0x8B, 0xC7, 0x87, 0x41, 0x0C,
};

struct StudioResourceRef
{
	void** vtable;
	volatile long references;
};

struct StudioHeaderRef
{
	void** vtable;
	volatile long references;
	std::uint32_t padding0C;
	std::byte* header;
	bool alignedAllocation;
	std::byte padding19[7];
};
static_assert(sizeof(StudioHeaderRef) == 0x20);
static_assert(offsetof(StudioHeaderRef, header) == 0x10);
static_assert(offsetof(StudioHeaderRef, alignedAllocation) == 0x18);

using ImportFunction = bool(__fastcall*)(void*, unsigned short, void*, std::byte*);
using AllocateFunction = void*(__fastcall*)(std::size_t);
using ModelNameFunction = const char*(__fastcall*)(void*, unsigned short);
using LookupFunction = bool(__fastcall*)(void*, const char*, ResourceLookup*);
using ShutdownFunction = void(__fastcall*)(void*);
using GetPackStoreFunction = std::byte*(__fastcall*)(void*, int);

std::uintptr_t s_moduleBase;
std::uintptr_t s_fileSystemBase;
ImportFunction s_importOriginal;
ShutdownFunction s_shutdownOriginal;
bool s_installed;

[[noreturn]] void OwnershipError(const char* detail)
{
	Error("R1Delta: datacache header ownership invariant failed: %s\n", detail);
	__fastfail(FAST_FAIL_FATAL_APP_EXIT);
}

template<typename Reference>
void Release(Reference* reference)
{
	if (_InterlockedDecrement(&reference->references) == 0) {
		using CanDeleteFunction = bool(__fastcall*)(Reference*);
		using DeleteFunction = void(__fastcall*)(Reference*, unsigned int);
		if (reinterpret_cast<CanDeleteFunction>(reference->vtable[1])(reference))
			reinterpret_cast<DeleteFunction>(reference->vtable[0])(reference, 1);
	}
}

StudioHeaderRef* Clone(StudioHeaderRef* source)
{
	const int length = *reinterpret_cast<const int*>(source->header + kHeaderLengthOffset);
	if (length < 0x198)
		OwnershipError("decoded studio header has an invalid length");

	const auto allocate = reinterpret_cast<AllocateFunction>(s_moduleBase + kAllocateRva);
	auto* copy = static_cast<StudioHeaderRef*>(allocate(sizeof(StudioHeaderRef)));
	auto* header = static_cast<std::byte*>(allocate(static_cast<std::size_t>(length)));
	if (!copy || !header)
		OwnershipError("could not allocate a private decoded studio header");

	std::memcpy(header, source->header, static_cast<std::size_t>(length));
	// Native CStudioHdrRef destruction frees an unaligned header through the
	// same datacache allocator, and frees the wrapper through +0x20480.
	*copy = { source->vtable, 1, 0, header, false, {} };
	return copy;
}

bool __fastcall ImportOwnedHeader(
	void* cache,
	unsigned short handle,
	void* rawData,
	std::byte* decodedHeader)
{
	if (!decodedHeader)
		return s_importOriginal(cache, handle, rawData, decodedHeader);

	auto* table = *reinterpret_cast<std::byte**>(static_cast<std::byte*>(cache) + 0x28);
	auto* record = *reinterpret_cast<std::byte**>(table + 24 * handle + 0x10);
	auto*& recordReference = *reinterpret_cast<StudioHeaderRef**>(record);
	StudioHeaderRef* const shared = recordReference;
	if (!shared || shared->header != decodedHeader)
		OwnershipError("decoded import does not match its model record");

	// Recover the node the caller already locked. Do not reacquire its lock:
	// GetStudioHdr keeps it across this call and releases it after publication.
	void* const filesystem = *reinterpret_cast<void**>(s_moduleBase + kFileSystemGlobalRva);
	auto** const vtable = *reinterpret_cast<void***>(filesystem);
	const auto lookup = reinterpret_cast<LookupFunction>(vtable[0x2F8 / sizeof(void*)]);
	const auto actualName = reinterpret_cast<ModelNameFunction>(s_moduleBase + kActualModelNameRva);
	ResourceLookup resource{};
	if (!lookup(filesystem, actualName(cache, handle), &resource)
		|| !resource.node || resource.node->lock != 1
		|| resource.node->unknown08 != 0
		|| resource.node->entry != reinterpret_cast<std::uintptr_t>(shared))
		OwnershipError("decoded import lost its locked resource-cache reference");

	const auto previousHandle = *reinterpret_cast<const unsigned short*>(
		decodedHeader + kHeaderHandleOffset);
	// Two references belong to this import: the cache node and the newly
	// borrowing record. Any additional owner must keep its original handle.
	// With no other owner the cached header can simply transfer, without a copy.
	if (previousHandle != handle && shared->references > 2) {
		recordReference = Clone(shared);
		Release(shared); // replace the record's borrowed reference
		decodedHeader = recordReference->header;
	}

	// The caller republishes and AddRefs after the import. Consume the old
	// node ownership first, whether it is moving or replacing the header.
	resource.node->entry = 0;
	Release(shared);
	return s_importOriginal(cache, handle, rawData, decodedHeader);
}

void __fastcall ShutdownOwnedModelCache(void* cache)
{
	// Native shutdown first stops model work and releases the model records.
	// Their decoded hardware objects also have an independent reference in
	// each VPK resource node. Evict that cache ownership before the renderer
	// disconnects, rather than leaving GPU buffers alive until filesystem exit.
	const bool initialized = (static_cast<const std::uint8_t*>(cache)[0x100] & 4) != 0;
	s_shutdownOriginal(cache);
	if (!initialized)
		return;
	void* const filesystem = *reinterpret_cast<void**>(s_moduleBase + kFileSystemGlobalRva);
	auto** const vtable = *reinterpret_cast<void***>(filesystem);
	if (vtable[kGetPackStoreVtableOffset / sizeof(void*)]
		!= reinterpret_cast<void*>(s_fileSystemBase + kGetPackStoreRva))
		OwnershipError("unsupported filesystem pack-store enumeration");
	const auto getStore = reinterpret_cast<GetPackStoreFunction>(
		vtable[kGetPackStoreVtableOffset / sizeof(void*)]);
	for (int pack = 0; std::byte* const store = getStore(filesystem, pack); ++pack) {
		const auto count = *reinterpret_cast<const std::uint32_t*>(
			store + r1delta::vpk_async_precache::kPackRecordCountOffset);
		auto* const nodes = *reinterpret_cast<ResourceEntryNode**>(
			store + r1delta::vpk_async_precache::kPackEntryNodesOffset);
		for (std::uint32_t index = 0; index < count; ++index) {
			auto& node = nodes[index];
			if (static_cast<std::int32_t>(node.unknown08) >= 0)
				continue;
			while (_InterlockedCompareExchange(&node.lock, 1, 0) != 0)
				_mm_pause();
			auto* reference = reinterpret_cast<StudioResourceRef*>(node.entry);
			if (static_cast<std::int32_t>(node.unknown08) < 0 && reference
				&& reference->vtable == reinterpret_cast<void**>(
					s_moduleBase + kHardwareReferenceVtableRva)) {
				node.entry = 0;
				node.unknown08 = 0;
			}
			else {
				reference = nullptr;
			}
			_InterlockedExchange(&node.lock, 0);
			if (reference)
				Release(reference);
		}
	}
}
}

void InstallClientDataCacheHeaderOwnership(std::uintptr_t moduleBase)
{
	if (GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015 || s_installed)
		return;

	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
	if (!moduleBase || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
		OwnershipError("unsupported datacache image");
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(moduleBase + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE
		|| nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
		|| nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
		|| nt->FileHeader.TimeDateStamp != 0x54874271
		|| nt->OptionalHeader.SizeOfImage != 0xD3000
		|| std::memcmp(reinterpret_cast<void*>(moduleBase + kImportRva),
			kImportPrologue, sizeof(kImportPrologue)) != 0
		|| std::memcmp(reinterpret_cast<void*>(moduleBase + 0x1F425),
			kDecodedAdmission, sizeof(kDecodedAdmission)) != 0
		|| std::memcmp(reinterpret_cast<void*>(moduleBase + 0x1F57A),
			kPublication, sizeof(kPublication)) != 0)
		OwnershipError("unsupported datacache header admission code");
	if (std::memcmp(reinterpret_cast<void*>(moduleBase + kShutdownRva),
		kShutdownPrologue, sizeof(kShutdownPrologue)) != 0)
		OwnershipError("unsupported model-cache shutdown code");
	const auto filesystemBase = reinterpret_cast<std::uintptr_t>(
		GetModuleHandleW(L"filesystem_stdio.dll"));
	const auto* fsDos = reinterpret_cast<const IMAGE_DOS_HEADER*>(filesystemBase);
	if (!filesystemBase || fsDos->e_magic != IMAGE_DOS_SIGNATURE || fsDos->e_lfanew <= 0)
		OwnershipError("unsupported filesystem image");
	const auto* fsNt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
		filesystemBase + fsDos->e_lfanew);
	if (fsNt->Signature != IMAGE_NT_SIGNATURE
		|| fsNt->FileHeader.TimeDateStamp != r1delta::vpk_async_precache::kExpectedTimeDateStamp
		|| fsNt->OptionalHeader.SizeOfImage != r1delta::vpk_async_precache::kExpectedSizeOfImage
		|| std::memcmp(reinterpret_cast<void*>(filesystemBase + kGetPackStoreRva),
			kGetPackStoreCode, sizeof(kGetPackStoreCode)) != 0)
		OwnershipError("unsupported filesystem resource-cache layout");
	s_fileSystemBase = filesystemBase;

	s_moduleBase = moduleBase;
	void* const target = reinterpret_cast<void*>(moduleBase + kImportRva);
	const MH_STATUS created = MH_CreateHook(target, &ImportOwnedHeader,
		reinterpret_cast<LPVOID*>(&s_importOriginal));
	if (created != MH_OK)
		OwnershipError("could not create the decoded-header import hook");
	void* const shutdownTarget = reinterpret_cast<void*>(moduleBase + kShutdownRva);
	if (MH_CreateHook(shutdownTarget, &ShutdownOwnedModelCache,
		reinterpret_cast<LPVOID*>(&s_shutdownOriginal)) != MH_OK)
		OwnershipError("could not create the model-cache ownership shutdown hook");
	if (MH_EnableHook(target) != MH_OK)
		OwnershipError("could not enable the decoded-header import hook");
	if (MH_EnableHook(shutdownTarget) != MH_OK)
		OwnershipError("could not enable the model-cache ownership shutdown hook");
	s_installed = true;
	OutputDebugStringA("R1Delta: client datacache decoded-header ownership fix installed\n");
}
