#include "vpk_async_precache_fix.h"

#include "engine/core/core.h"
#include "engine/logging/logging.h"

#include <MinHook.h>
#include <Windows.h>
#include <intrin.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr uintptr_t kAsyncPrecacheWorkerRva = 0x74D50;
constexpr uintptr_t kFindResourceHandlerRva = 0x71350;
constexpr uintptr_t kFindPackEntrySlotRva = 0x746B0;
constexpr uintptr_t kCompareResourceExtensionRva = 0x4AFD0;
constexpr uintptr_t kFormatPathRva = 0x4CB90;
constexpr uintptr_t kAsyncDecodeCallbackRva = 0x713D0;

constexpr uintptr_t kResourceRecordBaseRva = 0xFC030;
constexpr uintptr_t kFileSystemInterfaceRva = 0xFAD28;
constexpr uintptr_t kPackStoreRva = 0x20FC640;
constexpr uintptr_t kWorkItemIndicesRva = 0x20FC648;
constexpr uintptr_t kPrecacheModeRva = 0x20FC650;
constexpr uintptr_t kOutstandingWorkCountRva = 0x20FC678;

constexpr DWORD kExpectedTimeDateStamp = 0x54874230;
constexpr DWORD kExpectedSizeOfImage = 0x2116000;

uintptr_t s_FilesystemBase;
bool s_AsyncPrecacheFixInstalled;

struct ResourceRecord
{
	const char* directory;
	const char* filename;
	const char* extension;
	uintptr_t unknown18;
	uintptr_t asyncReadArgument;
	std::array<std::byte, 40> unknown28;
};

static_assert(sizeof(ResourceRecord) == 80);
static_assert(offsetof(ResourceRecord, extension) == 0x10);
static_assert(offsetof(ResourceRecord, asyncReadArgument) == 0x20);

struct ResourceHandler
{
	const char* extension;
	uintptr_t unknown08;
	uintptr_t unknown10;
	uintptr_t decode;
	uintptr_t finish;
	uintptr_t consumeLoadedEntry;
};

static_assert(sizeof(ResourceHandler) == 0x30);
static_assert(offsetof(ResourceHandler, decode) == 0x18);
static_assert(offsetof(ResourceHandler, finish) == 0x20);
static_assert(offsetof(ResourceHandler, consumeLoadedEntry) == 0x28);

struct AsyncRequestStorage
{
	int32_t state;
	std::array<std::byte, 0x3C> opaque;
	uint8_t tail;
	std::array<std::byte, 0x0F> padding;
};

static_assert(sizeof(AsyncRequestStorage) == 0x50);
static_assert(offsetof(AsyncRequestStorage, tail) == 0x40);

struct AsyncPrecacheContext
{
	uint8_t mode;
	std::array<std::byte, 7> padding;
	uintptr_t* entrySlot;
	ResourceRecord* record;
	uintptr_t decodedResource;
	ResourceHandler* handler;
	int32_t* request;
};

static_assert(sizeof(AsyncPrecacheContext) == 0x30);
static_assert(offsetof(AsyncPrecacheContext, entrySlot) == 0x08);
static_assert(offsetof(AsyncPrecacheContext, record) == 0x10);
static_assert(offsetof(AsyncPrecacheContext, decodedResource) == 0x18);
static_assert(offsetof(AsyncPrecacheContext, handler) == 0x20);
static_assert(offsetof(AsyncPrecacheContext, request) == 0x28);

using FindResourceHandlerFn = ResourceHandler* (__fastcall*)(ResourceRecord* record);
using FindPackEntrySlotFn = uintptr_t* (__fastcall*)(void* packStore, const char* path);
using CompareResourceExtensionFn = int(__fastcall*)(const char* lhs, const char* rhs);
using FormatPathFn = int(__cdecl*)(char* destination, size_t capacity, const char* format, ...);
using IsEntryReadyFn = bool(__fastcall*)(void* entry);
using ConsumeLoadedEntryFn = uintptr_t(__fastcall*)(uintptr_t* entrySlot, void* packStore);
using FinishFn = uintptr_t(__fastcall*)(AsyncPrecacheContext* context);
using AsyncDecodeCallbackFn = void(__fastcall*)(AsyncPrecacheContext* context);
using PrepareAsyncRequestFn = void(__fastcall*)(
	void* fileSystem,
	void* packStore,
	uint32_t recordIndex,
	int32_t* request);
using GetAsyncQueueFn = uintptr_t(__fastcall*)(void* fileSystem, void* packStore);
using SubmitAsyncRequestFn = uintptr_t(__fastcall*)(
	void* fileSystem,
	void* packStore,
	uintptr_t queue,
	int32_t* request,
	uintptr_t asyncReadArgument,
	AsyncPrecacheContext* context,
	AsyncDecodeCallbackFn callback);

template <typename Function>
Function ModuleFunction(uintptr_t rva)
{
	return reinterpret_cast<Function>(s_FilesystemBase + rva);
}

template <typename Function>
Function VirtualFunction(void* object, size_t byteOffset)
{
	auto* const vtable = *reinterpret_cast<uintptr_t**>(object);
	return reinterpret_cast<Function>(
		vtable[byteOffset / sizeof(uintptr_t)]);
}

bool IsExpectedR1ClientFileSystem(uintptr_t filesystemBase)
{
	if (!filesystemBase)
		return false;

	const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(filesystemBase);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
		return false;

	const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
		filesystemBase + static_cast<uintptr_t>(dos->e_lfanew));
	return nt->Signature == IMAGE_NT_SIGNATURE
		&& nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64
		&& nt->FileHeader.TimeDateStamp == kExpectedTimeDateStamp
		&& nt->OptionalHeader.SizeOfImage == kExpectedSizeOfImage;
}

uintptr_t RunAsyncPrecacheTask(uint32_t callbackIndex)
{
	auto* const workItemIndices = *reinterpret_cast<uint32_t**>(
		s_FilesystemBase + kWorkItemIndicesRva);
	const uint32_t recordIndex = workItemIndices[callbackIndex];

	auto* const recordBase = *reinterpret_cast<ResourceRecord**>(
		s_FilesystemBase + kResourceRecordBaseRva);
	ResourceRecord* const record = recordBase + recordIndex;
	ResourceHandler* const handler =
		ModuleFunction<FindResourceHandlerFn>(kFindResourceHandlerRva)(record);
	if (!handler)
		return 0;

	void* const packStore = *reinterpret_cast<void**>(
		s_FilesystemBase + kPackStoreRva);
	auto* const packRecords = *reinterpret_cast<ResourceRecord**>(
		reinterpret_cast<uintptr_t>(packStore) + 0x240);
	const ResourceRecord& packRecord = packRecords[recordIndex];

	char path[0x104];
	ModuleFunction<FormatPathFn>(kFormatPathRva)(
		path,
		sizeof(path),
		"%s/%s.%s",
		packRecord.directory,
		packRecord.filename,
		packRecord.extension);
	path[sizeof(path) - 1] = '\0';

	// The retail function conditionally moves the lookup result over an
	// uninitialized stack qword. On a miss it dereferences that indeterminate
	// pointer. This local slot is the intended null-entry fallback and is the
	// only semantic difference from the shipped callback.
	uintptr_t missingEntry = 0;
	uintptr_t* entrySlot = &missingEntry;
	if (uintptr_t* const found =
		ModuleFunction<FindPackEntrySlotFn>(kFindPackEntrySlotRva)(packStore, path)) {
		entrySlot = found;
	}

	uintptr_t result = 0;
	if (*entrySlot) {
		result = static_cast<uintptr_t>(
			ModuleFunction<CompareResourceExtensionFn>(
				kCompareResourceExtensionRva)(record->extension, "vtf"));
		if (result != 0) {
			void* const entry = reinterpret_cast<void*>(*entrySlot);
			const bool ready = VirtualFunction<IsEntryReadyFn>(entry, 0x58)(entry);
			result = ready ? 1 : 0;
			if (ready) {
				result = reinterpret_cast<ConsumeLoadedEntryFn>(
					handler->consumeLoadedEntry)(entrySlot, packStore);
			}
		}

		if (*entrySlot)
			return result;
	}

	void* const fileSystem = *reinterpret_cast<void**>(
		s_FilesystemBase + kFileSystemInterfaceRva);
	alignas(16) AsyncRequestStorage request;
	request.state = -1;
	request.tail = 0;

	VirtualFunction<PrepareAsyncRequestFn>(fileSystem, 0x380)(
		fileSystem,
		packStore,
		recordIndex,
		&request.state);

	AsyncPrecacheContext context{};
	context.mode = *reinterpret_cast<uint8_t*>(
		s_FilesystemBase + kPrecacheModeRva);
	context.entrySlot = entrySlot;
	context.record = record;
	context.handler = handler;
	context.request = &request.state;

	const uintptr_t queue =
		VirtualFunction<GetAsyncQueueFn>(fileSystem, 0x378)(fileSystem, packStore);
	VirtualFunction<SubmitAsyncRequestFn>(fileSystem, 0x370)(
		fileSystem,
		packStore,
		queue,
		&request.state,
		record->asyncReadArgument,
		&context,
		ModuleFunction<AsyncDecodeCallbackFn>(kAsyncDecodeCallbackRva));

	return reinterpret_cast<FinishFn>(handler->finish)(&context);
}

uintptr_t __fastcall R1ClientVPKAsyncPrecacheWorker(
	void* /*taskContext*/,
	uint32_t callbackIndex)
{
	const uintptr_t result = RunAsyncPrecacheTask(callbackIndex);
	_InterlockedDecrement(reinterpret_cast<volatile long*>(
		s_FilesystemBase + kOutstandingWorkCountRva));
	return result;
}

}

bool InstallR1ClientVPKAsyncPrecacheFix(uintptr_t filesystemBase)
{
	if (s_AsyncPrecacheFixInstalled)
		return true;
	if (!filesystemBase || IsDedicatedServer())
		return false;
	if (!IsExpectedR1ClientFileSystem(filesystemBase)) {
		Warning(
			"R1Delta: VPK async-precache fix skipped; filesystem_stdio.dll is not the expected R1 client image\n");
		return false;
	}

	void* const target = reinterpret_cast<void*>(
		filesystemBase + kAsyncPrecacheWorkerRva);
	constexpr unsigned char expectedPrologue[] = {
		0x48, 0x89, 0x6C, 0x24, 0x10,
		0x48, 0x89, 0x74, 0x24, 0x18,
		0x48, 0x89, 0x7C, 0x24, 0x20,
		0x41, 0x54,
		0x48, 0x81, 0xEC, 0xD0, 0x01, 0x00, 0x00
	};
	if (memcmp(target, expectedPrologue, sizeof(expectedPrologue)) != 0) {
		Warning(
			"R1Delta: VPK async-precache fix skipped; unexpected worker prologue at %p\n",
			target);
		return false;
	}

	s_FilesystemBase = filesystemBase;
	const MH_STATUS createStatus = MH_CreateHook(
		target,
		&R1ClientVPKAsyncPrecacheWorker,
		nullptr);
	if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED) {
		Warning(
			"R1Delta: VPK async-precache hook creation failed status=%d\n",
			static_cast<int>(createStatus));
		return false;
	}

	const MH_STATUS enableStatus = MH_EnableHook(target);
	if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
		Warning(
			"R1Delta: VPK async-precache hook enable failed status=%d\n",
			static_cast<int>(enableStatus));
		return false;
	}

	s_AsyncPrecacheFixInstalled = true;
	OutputDebugStringA(
		"R1Delta: R1 client VPK async-precache worker replacement installed and enabled\n");
	return true;
}
