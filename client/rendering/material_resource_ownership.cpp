// Client material-resource ownership for the DX11 material system.
//
// R1 retail materialsystem_dx11.dll: timestamp 54874238, image size 3E0000.
//
// Native device initialization creates four GPU buffers that the native
// shutdown path never releases: the primary scene vertex buffer, the tonemap
// histogram vertex buffer, and the two tonemap constant buffers. Native device
// initialization also creates three D3D11_QUERY_EVENT queries that the native
// final disconnect never releases. Both omissions leave live D3D11 objects with
// an external reference after the material system and the device are gone,
// which keeps the driver from completing teardown.
//
// The full-screen helper also abandons the creation reference of a stack-local
// texture record while exporting only its views. Balance that reference after
// creation, and release the seven device-owned resources at their native
// teardown boundaries while preserving pool bookkeeping and reinitialization.

#include "material_resource_ownership.h"
#include "graphics_flight_recorder.h"

#include "core.h"
#include "logging.h"

#include <MinHook.h>
#include <Windows.h>
#include <d3d11.h>
#include <intrin.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
// No-argument int64 material-system lifecycle owners.
constexpr std::uintptr_t kDeviceShutdownRva = 0x98D0;
constexpr std::uintptr_t kFinalDisconnectRva = 0x1AB0;
constexpr std::uintptr_t kCreateFullScreenTargetRva = 0x13A90;

// Vertex-buffer pool destroy: takes a pointer to a 16-byte handle, releases the
// pooled resource, returns the pool slot and clears the handle index.
constexpr std::uintptr_t kDestroyVertexBufferRva = 0x86F0;

constexpr std::uintptr_t kPrimaryVertexBufferRva = 0x281CF8;
constexpr std::uintptr_t kTonemapVertexBufferRva = 0x37FA68;
constexpr std::uintptr_t kTonemapParameterBufferRva = 0x37FA88;
constexpr std::uintptr_t kTonemapScaleBufferRva = 0x37FA90;
constexpr std::uintptr_t kFrameQueryRva = 0x2933A8;
constexpr std::uintptr_t kFrameQueryPairRva = 0x293428;
constexpr std::size_t kFrameQueryPairCount = 2;
constexpr std::uintptr_t kDxgiDeviceRva = 0x292920;
constexpr std::uintptr_t kDxgiFactoryRva = 0x292918;

// Native vertex-buffer handles are 16 bytes: +8 holds the element count, +12
// the element stride, and +14 the pooled index plus two. A zero index means the
// handle no longer owns a resource.
constexpr std::size_t kVertexBufferHandleBytes = 16;
constexpr std::size_t kVertexBufferHandleIndexOffset = 14;

constexpr unsigned char kDeviceShutdownPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
	0x57, 0x48, 0x83, 0xEC, 0x20, 0xE8, 0xDC, 0x41, 0x02, 0x00,
};
constexpr unsigned char kFinalDisconnectPrologue[] = {
	0x40, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x15, 0x54, 0x17,
	0x29, 0x00, 0x33, 0xF6, 0x85, 0xD2, 0x0F, 0x84, 0x90, 0x00,
	0x00, 0x00,
};
constexpr unsigned char kDestroyVertexBufferPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
	0x57, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB7, 0x79, 0x0E,
};
constexpr unsigned char kFullScreenTargetPrologue[] = {
	0x48, 0x81, 0xEC, 0x98, 0x00, 0x00, 0x00, 0x80,
	0xBC, 0x24, 0xC8, 0x00, 0x00, 0x00, 0x00, 0xB8,
	0x02, 0x00, 0x0A, 0x00, 0x41, 0xBA, 0x02, 0x00,
	0x02, 0x00, 0x44, 0x0F, 0x45, 0xD0, 0x80, 0xBC,
};

using DestroyVertexBufferFunction = void(__fastcall*)(void* handle);
using DeviceLifecycleFunction = std::int64_t(__fastcall*)();
using CreateFullScreenTargetFunction = ID3D11RenderTargetView*(__fastcall*)(
	int, int, int, unsigned int, std::int64_t, char, char, char,
	ID3D11ShaderResourceView**, ID3D11ShaderResourceView**);

std::uintptr_t s_materialSystemBase;
DestroyVertexBufferFunction s_destroyVertexBuffer;
DeviceLifecycleFunction s_originalDeviceShutdown;
DeviceLifecycleFunction s_originalFinalDisconnect;
CreateFullScreenTargetFunction s_originalCreateFullScreenTarget;
bool s_installed;

[[noreturn]] void OwnershipError(const char* detail)
{
	char buffer[320]{};
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: materialsystem_dx11 material-resource ownership "
		"invariant failed: %s\n",
		detail);
	OutputDebugStringA(buffer);
	Error("%s", buffer);
	__fastfail(FAST_FAIL_FATAL_APP_EXIT);
}

bool IsSuccessfulEnableStatus(MH_STATUS status)
{
	return status == MH_OK || status == MH_ERROR_ENABLED;
}

void LogHookStatus(
	const char* name,
	std::uintptr_t rva,
	MH_STATUS created,
	MH_STATUS enabled,
	void* target,
	void* original)
{
	char buffer[320]{};
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: materialsystem_dx11 %s ownership hook at RVA 0x%llX: "
		"create=%d enable=%d target=%p original=%p\n",
		name,
		static_cast<unsigned long long>(rva),
		static_cast<int>(created),
		static_cast<int>(enabled),
		target,
		original);
	OutputDebugStringA(buffer);
}

void InstallLifecycleHook(
	std::uintptr_t rva,
	const unsigned char* expected,
	std::size_t expectedSize,
	void* detour,
	void** original,
	const char* name)
{
	void* const target = reinterpret_cast<void*>(s_materialSystemBase + rva);
	if (std::memcmp(target, expected, expectedSize) != 0)
		OwnershipError("unexpected lifecycle code");

	const MH_STATUS created = MH_CreateHook(target, detour, original);
	const bool hasOriginal = original && *original;
	const MH_STATUS enabled = created == MH_OK && hasOriginal
		? MH_EnableHook(target)
		: MH_ERROR_NOT_CREATED;
	LogHookStatus(name, rva, created, enabled, target, hasOriginal ? *original : nullptr);
	if (created != MH_OK || !hasOriginal)
		OwnershipError("could not create the lifecycle hook");
	if (!IsSuccessfulEnableStatus(enabled))
		OwnershipError("could not enable the lifecycle hook");
}

// Native vertex-buffer pool destroy takes a pointer to the 16-byte handle,
// releases the pooled resource, returns the slot and clears the handle index.
// Call it only while that index is live; the native clear prevents a second
// release of the same handle.
void DestroyPooledVertexBuffer(std::uintptr_t handleRva)
{
	auto* const handle = reinterpret_cast<unsigned char*>(
		s_materialSystemBase + handleRva);
	const auto index = *reinterpret_cast<const std::uint16_t*>(
		handle + kVertexBufferHandleIndexOffset);
	if (!index)
		return;
	// Clears the handle index, so a repeated shutdown cannot release twice.
	s_destroyVertexBuffer(handle);
}

void ReleaseResourceSlot(std::uintptr_t slotRva)
{
	auto* const slot = reinterpret_cast<IUnknown* volatile*>(
		s_materialSystemBase + slotRva);
	auto* const resource = static_cast<IUnknown*>(
		InterlockedExchangePointer(
			reinterpret_cast<void* volatile*>(slot),
			nullptr));
	if (resource)
		resource->Release();
}

std::int64_t __fastcall ShutdownOwnedMaterialResources()
{
	r1delta::graphics_flight_recorder::ResetBoundary(
		r1delta::graphics_flight_recorder::EventKind::DeviceShutdownBegin);
	// Native shutdown first memsets the whole tonemap state block, which drops
	// the histogram vertex-buffer handle and both tonemap constant-buffer
	// pointers without releasing anything, and its only vertex/index destroy is
	// the primary index buffer. Release all four resources before that body runs.
	// Native destroy keeps the pool slot and counter bookkeeping consistent with
	// a device reinitialization; a raw COM release would not.
	DestroyPooledVertexBuffer(kPrimaryVertexBufferRva);
	DestroyPooledVertexBuffer(kTonemapVertexBufferRva);
	ReleaseResourceSlot(kTonemapParameterBufferRva);
	ReleaseResourceSlot(kTonemapScaleBufferRva);
	const auto result = s_originalDeviceShutdown();
	r1delta::graphics_flight_recorder::ResetBoundary(
		r1delta::graphics_flight_recorder::EventKind::DeviceShutdownEnd);
	return result;
}

std::int64_t __fastcall DisconnectOwnedDeviceResources()
{
	r1delta::graphics_flight_recorder::ResetBoundary(
		r1delta::graphics_flight_recorder::EventKind::DeviceDisconnectBegin);
	// Native init retains these queries, a queried DXGI device and its factory,
	// but final disconnect releases only the immediate context and D3D device.
	// Drop the extra interface ownership before the final D3D device release;
	// an outstanding DXGI device reference otherwise keeps its worker alive.
	ReleaseResourceSlot(kFrameQueryRva);
	for (std::size_t index = 0; index < kFrameQueryPairCount; ++index) {
		ReleaseResourceSlot(
			kFrameQueryPairRva + index * sizeof(void*));
	}
	ReleaseResourceSlot(kDxgiDeviceRva);
	ReleaseResourceSlot(kDxgiFactoryRva);
	const auto result = s_originalFinalDisconnect();
	r1delta::graphics_flight_recorder::ResetBoundary(
		r1delta::graphics_flight_recorder::EventKind::DeviceDisconnectEnd);
	return result;
}

ID3D11RenderTargetView* __fastcall CreateOwnedFullScreenTarget(
	int format, int width, int height, unsigned int mipCount, std::int64_t name,
	char exportSecondView, char srgb, char reserved,
	ID3D11ShaderResourceView** shaderView, ID3D11ShaderResourceView** secondView)
{
	auto* const view = s_originalCreateFullScreenTarget(
		format, width, height, mipCount, name, exportSecondView, srgb,
		reserved, shaderView, secondView);
	if (view) {
		// The native stack-local texture record exports only its views. Retire
		// the abandoned creation reference; GetResource adds the other one.
		ID3D11Resource* resource = nullptr;
		view->GetResource(&resource);
		resource->Release();
		resource->Release();
	}
	return view;
}
} // namespace

void InstallMaterialResourceOwnership(std::uintptr_t materialSystemBase)
{
	if (!materialSystemBase
		|| GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015
		|| s_installed) {
		return;
	}

	const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(
		materialSystemBase);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
		OwnershipError("unsupported image");
	const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
		materialSystemBase + dos->e_lfanew);
	const std::uintptr_t imageSize = nt->OptionalHeader.SizeOfImage;
	if (nt->Signature != IMAGE_NT_SIGNATURE
		|| nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
		|| nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
		|| nt->FileHeader.TimeDateStamp != 0x54874238
		|| imageSize != 0x3E0000
		|| std::memcmp(
			reinterpret_cast<const void*>(
				materialSystemBase + kDeviceShutdownRva),
			kDeviceShutdownPrologue,
			sizeof(kDeviceShutdownPrologue)) != 0
		|| std::memcmp(
			reinterpret_cast<const void*>(
				materialSystemBase + kFinalDisconnectRva),
			kFinalDisconnectPrologue,
			sizeof(kFinalDisconnectPrologue)) != 0
		|| std::memcmp(
			reinterpret_cast<const void*>(
				materialSystemBase + kDestroyVertexBufferRva),
			kDestroyVertexBufferPrologue,
			sizeof(kDestroyVertexBufferPrologue)) != 0) {
		OwnershipError("unsupported shutdown code");
	}
	static_assert(kPrimaryVertexBufferRva + kVertexBufferHandleBytes <= 0x3E0000);
	static_assert(kTonemapVertexBufferRva + kVertexBufferHandleBytes <= 0x3E0000);
	static_assert(kTonemapScaleBufferRva + sizeof(void*) <= 0x3E0000);
	static_assert(kFrameQueryPairRva + kFrameQueryPairCount * sizeof(void*) <= 0x3E0000);
	static_assert(kDxgiDeviceRva + sizeof(void*) <= 0x3E0000);

	s_materialSystemBase = materialSystemBase;
	s_destroyVertexBuffer = reinterpret_cast<DestroyVertexBufferFunction>(
		materialSystemBase + kDestroyVertexBufferRva);

	InstallLifecycleHook(
		kDeviceShutdownRva,
		kDeviceShutdownPrologue,
		sizeof(kDeviceShutdownPrologue),
		reinterpret_cast<void*>(&ShutdownOwnedMaterialResources),
		reinterpret_cast<void**>(&s_originalDeviceShutdown),
		"device shutdown");
	InstallLifecycleHook(
		kFinalDisconnectRva,
		kFinalDisconnectPrologue,
		sizeof(kFinalDisconnectPrologue),
		reinterpret_cast<void*>(&DisconnectOwnedDeviceResources),
		reinterpret_cast<void**>(&s_originalFinalDisconnect),
		"final disconnect");
	InstallLifecycleHook(
		kCreateFullScreenTargetRva,
		kFullScreenTargetPrologue,
		sizeof(kFullScreenTargetPrologue),
		reinterpret_cast<void*>(&CreateOwnedFullScreenTarget),
		reinterpret_cast<void**>(&s_originalCreateFullScreenTarget),
		"full-screen target creation");

	s_installed = true;
	OutputDebugStringA(
		"R1Delta: client material-resource ownership fix installed\n");
}
