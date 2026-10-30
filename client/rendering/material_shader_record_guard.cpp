// Shader-record guard for the retail DX11 material system.
//
// R1 retail materialsystem_dx11.dll: timestamp 54874238, image size 3E0000.
//
// Every CMaterial has a 24-byte shader record at CMaterial+104 whose qword at +16 is a union: a heap pointer to a table of
// 16-bit texture handles for UnlitGeneric, VertexLitGeneric, LightmappedGeneric and a few more, or a single inline handle for
// Basic (and BasicForceWireframe, the error shader), UnlitTwoTexture, Sprite and others. Nothing resets it when a material
// changes shader class, so a material that was drawn as (for example) the error shader and later reloads as UnlitGeneric
// reaches UnlitGeneric's state function (RVA 0x2F4F0) with the handle in the pointer slot: slot 9 only allocates when the
// qword is 0, slot 11 then zeroes 32 bytes through it ("write to 0xf87", "write to 0xfc5" at RVA 0x2F510).
//
// Two hooks, both version- and prologue-checked:
//   1. the material's shader release (RVA 0x383E0) drops an unowned pointer slot (owning invariant);
//   2. the state function of UnlitGeneric, VertexLitGeneric and LightmappedGeneric replaces an inline handle by a fresh table
//      before the original runs (crash-site backstop; slot 11 runs only when a material's key changed, not per draw).

#include "material_shader_record_guard.h"

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
using namespace r1delta::materialsystem_dx11;

using StateFunction = std::int64_t(__fastcall*)(void* shader, void* params, std::uint8_t* record);
using ReleaseFunction = std::int64_t(__fastcall*)(std::uint8_t* material);
using NameFunction = const char*(__fastcall*)(const void* material);

constexpr unsigned kLoggedEventLimit = 32;

std::uintptr_t s_base;
ShaderRecordAllocateFunction s_allocate;
StateFunction s_originalUnlitGenericState;
StateFunction s_originalVertexLitGenericState;
StateFunction s_originalLightmappedGenericState;
ReleaseFunction s_originalRelease;
std::atomic<unsigned> s_repairs;
std::atomic<unsigned> s_releases;
bool s_installed;

// Diagnostic only, and only on the rare repair path: names the material that carried the stale handle so the producer
// (which content reloads under another shader class) can be identified from a console log.
const char* MaterialNameOrNull(const std::uint8_t* record)
{
	const auto* const material = record - kMaterialShaderRecordOffset;
	__try {
		std::uintptr_t vtable = 0;
		std::memcpy(&vtable, material, sizeof(vtable));
		if (vtable != s_base + kMaterialVtableRva)
			return nullptr;
		NameFunction getName = nullptr;
		std::memcpy(&getName, reinterpret_cast<const void*>(vtable), sizeof(getName));
		return getName ? getName(material) : nullptr;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

void LogRepair(const char* shaderClass, const std::uint8_t* record, std::uint64_t staleHandle)
{
	const unsigned index = s_repairs.fetch_add(1, std::memory_order_relaxed);
	if (index >= kLoggedEventLimit)
		return;
	const char* name = MaterialNameOrNull(record);
	Warning(
		"R1Delta: materialsystem_dx11 %s shader record held inline texture handle 0x%llX instead of a table; "
		"replaced it with a fresh table (material=%p name=%s key=0x%08X flags=0x%08X)\n",
		shaderClass,
		static_cast<unsigned long long>(staleHandle),
		static_cast<const void*>(record - kMaterialShaderRecordOffset),
		name ? name : "<unknown>",
		*reinterpret_cast<const std::uint32_t*>(record),
		*reinterpret_cast<const std::uint32_t*>(record + kShaderRecordFlagsOffset));
}

std::int64_t CallWithValidTable(
	StateFunction original,
	const char* shaderClass,
	std::size_t tableBytes,
	void* shader,
	void* params,
	std::uint8_t* record)
{
	std::uint64_t staleHandle = 0;
	switch (RepairShaderRecordTable(record, tableBytes, s_allocate, &staleHandle)) {
	case ShaderRecordRepairResult::intact:
		break;
	case ShaderRecordRepairResult::repaired:
		LogRepair(shaderClass, record, staleHandle);
		break;
	case ShaderRecordRepairResult::allocationFailed:
		Warning(
			"R1Delta: materialsystem_dx11 %s shader record held inline texture handle 0x%llX and the table allocation "
			"failed; skipping the state update\n",
			shaderClass,
			static_cast<unsigned long long>(staleHandle));
		return 0;
	}
	return original(shader, params, record);
}

std::int64_t __fastcall UnlitGenericStateGuard(void* shader, void* params, std::uint8_t* record)
{
	return CallWithValidTable(
		s_originalUnlitGenericState, "UnlitGeneric", kUnlitGenericTableBytes, shader, params, record);
}

std::int64_t __fastcall VertexLitGenericStateGuard(void* shader, void* params, std::uint8_t* record)
{
	return CallWithValidTable(
		s_originalVertexLitGenericState, "VertexLitGeneric", kVertexLitGenericTableBytes, shader, params, record);
}

std::int64_t __fastcall LightmappedGenericStateGuard(void* shader, void* params, std::uint8_t* record)
{
	return CallWithValidTable(
		s_originalLightmappedGenericState, "LightmappedGeneric", kLightmappedGenericTableBytes, shader, params, record);
}

std::int64_t __fastcall MaterialShaderReleaseGuard(std::uint8_t* material)
{
	const std::int64_t result = s_originalRelease(material);
	if (material
		&& ClearUnownedShaderRecordTable(material + kMaterialShaderRecordOffset)
		&& s_releases.fetch_add(1, std::memory_order_relaxed) < kLoggedEventLimit) {
		Msg("R1Delta: materialsystem_dx11 material %p let go of its shader with an unowned shader-record slot; cleared it\n",
			static_cast<const void*>(material));
	}
	return result;
}

bool InstallChecked(
	const char* name,
	std::uintptr_t rva,
	const std::uint8_t* expectedPrologue,
	std::size_t expectedSize,
	void* detour,
	void** original)
{
	void* const target = reinterpret_cast<void*>(s_base + rva);
	char buffer[320]{};
	if (std::memcmp(target, expectedPrologue, expectedSize) != 0) {
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: skipped materialsystem_dx11 %s shader-record guard because bytes did not match\n",
			name);
		OutputDebugStringA(buffer);
		return false;
	}

	const MH_STATUS created = MH_CreateHook(target, detour, original);
	const bool hasOriginal = original && *original;
	const bool canEnable = created == MH_OK || (created == MH_ERROR_ALREADY_CREATED && hasOriginal);
	const MH_STATUS enabled = canEnable ? MH_EnableHook(target) : created;
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: materialsystem_dx11 %s shader-record guard RVA 0x%llX: create=%d enable=%d target=%p original=%p\n",
		name,
		static_cast<unsigned long long>(rva),
		static_cast<int>(created),
		static_cast<int>(enabled),
		target,
		original ? *original : nullptr);
	OutputDebugStringA(buffer);
	return hasOriginal && (enabled == MH_OK || enabled == MH_ERROR_ENABLED);
}

bool IsExpectedRetailImage(std::uintptr_t base)
{
	const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000)
		return false;
	const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
	return nt->Signature == IMAGE_NT_SIGNATURE
		&& nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64
		&& nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC
		&& nt->FileHeader.TimeDateStamp == kRetailTimeDateStamp
		&& nt->OptionalHeader.SizeOfImage == kRetailImageSize;
}
} // namespace

void InstallMaterialShaderRecordGuard(std::uintptr_t materialSystemBase)
{
	if (!materialSystemBase
		|| s_installed
		|| GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015) {
		return;
	}
	if (!IsExpectedRetailImage(materialSystemBase)) {
		OutputDebugStringA("R1Delta: skipped materialsystem_dx11 shader-record guard: not the retail 0x54874238 / 0x3E0000 image\n");
		return;
	}
	const void* const allocateTarget = reinterpret_cast<const void*>(materialSystemBase + kShaderRecordAllocateRva);
	if (std::memcmp(
			allocateTarget,
			kExpectedShaderRecordAllocatePrologue,
			sizeof(kExpectedShaderRecordAllocatePrologue)) != 0) {
		OutputDebugStringA("R1Delta: skipped materialsystem_dx11 shader-record guard: allocator bytes did not match\n");
		return;
	}

	s_base = materialSystemBase;
	s_allocate = reinterpret_cast<ShaderRecordAllocateFunction>(materialSystemBase + kShaderRecordAllocateRva);

	// Diagnostic switch: without the release hook the state-function guards are the only protection, so every stale inline
	// handle that reaches them is repaired and logged with the material's name.
	const bool release = !HasEngineCommandLineFlag("-r1delta_no_shader_record_release_guard")
		&& InstallChecked(
			"release",
			kMaterialShaderReleaseRva,
			kExpectedMaterialShaderReleasePrologue,
			sizeof(kExpectedMaterialShaderReleasePrologue),
			reinterpret_cast<void*>(&MaterialShaderReleaseGuard),
			reinterpret_cast<void**>(&s_originalRelease));
	const bool unlit = InstallChecked(
		"UnlitGeneric",
		kUnlitGenericStateRva,
		kExpectedUnlitGenericStatePrologue,
		sizeof(kExpectedUnlitGenericStatePrologue),
		reinterpret_cast<void*>(&UnlitGenericStateGuard),
		reinterpret_cast<void**>(&s_originalUnlitGenericState));
	const bool vertexLit = InstallChecked(
		"VertexLitGeneric",
		kVertexLitGenericStateRva,
		kExpectedVertexLitGenericStatePrologue,
		sizeof(kExpectedVertexLitGenericStatePrologue),
		reinterpret_cast<void*>(&VertexLitGenericStateGuard),
		reinterpret_cast<void**>(&s_originalVertexLitGenericState));
	const bool lightmapped = InstallChecked(
		"LightmappedGeneric",
		kLightmappedGenericStateRva,
		kExpectedLightmappedGenericStatePrologue,
		sizeof(kExpectedLightmappedGenericStatePrologue),
		reinterpret_cast<void*>(&LightmappedGenericStateGuard),
		reinterpret_cast<void**>(&s_originalLightmappedGenericState));
	s_installed = release || unlit || vertexLit || lightmapped;
}
