// Initializes a material whose VMT cannot be read as the material system's error material.
//
// R1 retail materialsystem_dx11.dll: timestamp 54874238, image size 3E0000.
//
// CMaterial::Initialize (RVA 0x3AB60) runs lazily: every accessor (GetColorModulation at RVA 0x3B3A0, IsErrorMaterial at
// RVA 0x3B010 and about fifty more) calls it while the initialized bit (+0x26 bit 2) is clear and then dereferences the
// calling thread's render context at +0x38/+0x40. A material without its own KeyValues (+0xC8) reads its VMT from disk
// at that point. When the read fails, Initialize returns false and leaves the bit clear and both contexts null, and the
// accessor that asked reads through null: "read 0x8" in GetColorModulation from a VGUI draw, "read 0x4" in
// IsErrorMaterial from particle setup. The material system creates such materials only after FindMaterial (RVA 0x58940)
// read the same VMT successfully, so this happens when the file stops being visible afterwards: a reload of the
// material (mat_reloadmaterial), or content that was mounted when the material was found and is not mounted anymore.
//
// For that exact failure the original Initialize runs once more with a copy of the error material's KeyValues, the same
// substitute the native shader setup (RVA 0x3A790) uses when a $fallbackmaterial cannot be read. The material then has a
// real Basic shader with the error texture and valid contexts.

#include "material_load_fallback.h"

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

constexpr std::uintptr_t kMaterialInitializeRva = 0x3AB60;
constexpr std::uintptr_t kErrorMaterialRva = 0x382738;
constexpr std::uintptr_t kKeyValuesAllocateRva = 0x10B980;
constexpr std::uintptr_t kKeyValuesConstructRva = 0x10C2A0;
constexpr std::uintptr_t kKeyValuesCopyRva = 0x10D970;
constexpr std::uintptr_t kKeyValuesDeleteRva = 0x10DC50;
constexpr std::size_t kKeyValuesBytes = 80;
constexpr unsigned kLoggedFallbackLimit = 32;

constexpr unsigned char kMaterialInitializePrologue[] = {
	0x40, 0x57,                         // push rdi
	0x41, 0x55,                         // push r13
	0x48, 0x83, 0xEC, 0x48,             // sub rsp, 48h
	0x48, 0x89, 0x5C, 0x24, 0x60,       // mov [rsp+60h], rbx
};

// The CMaterial fields Initialize owns (layout from the constructor at RVA 0x39DF0).
struct MaterialView
{
	void* const* vtable;
	std::uint8_t unknown08[8];
	void* shader;                       // +0x10
	std::uint8_t unknown18[14];
	std::uint16_t flags;                // +0x26
	std::uint8_t unknown28[16];
	void* contexts[2];                  // +0x38, +0x40: indexed by the thread's material-context index
	std::uint8_t unknown48[128];
	void* keyValues;                    // +0xC8: set only for materials created from KeyValues
};
static_assert(offsetof(MaterialView, shader) == 0x10);
static_assert(offsetof(MaterialView, flags) == 0x26);
static_assert(offsetof(MaterialView, contexts) == 0x38);
static_assert(offsetof(MaterialView, keyValues) == 0xC8);
constexpr std::uint16_t kMaterialInitializedFlag = 0x4;

using InitializeFunction = char(__fastcall*)(MaterialView* material, void* keyValues, void* patchKeyValues, void* patches);
using GetNameFunction = const char*(__fastcall*)(const MaterialView* material);
using KeyValuesAllocateFunction = void*(__fastcall*)(std::size_t bytes);
using KeyValuesConstructFunction = void*(__fastcall*)(void* keyValues, const char* name);
using KeyValuesCopyFunction = void(__fastcall*)(void* destination, const void* source);
using KeyValuesDeleteFunction = void(__fastcall*)(void* keyValues);

std::uintptr_t s_base;
InitializeFunction s_originalInitialize;
std::atomic<unsigned> s_loggedFallbacks;
bool s_installed;

[[noreturn]] void LoadFallbackError(const char* detail)
{
	char buffer[320]{};
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: materialsystem_dx11 material load fallback failed: %s\n",
		detail);
	OutputDebugStringA(buffer);
	Error("%s", buffer);
	__fastfail(FAST_FAIL_FATAL_APP_EXIT);
}

template <typename Function>
Function NativeFunction(std::uintptr_t rva)
{
	return reinterpret_cast<Function>(s_base + rva);
}

// Initialize returned false without touching the material: no shader, no contexts, not marked initialized.
bool FailedToReadVmt(const MaterialView& material)
{
	return (material.flags & kMaterialInitializedFlag) == 0
		&& !material.keyValues
		&& !material.shader
		&& !material.contexts[0]
		&& !material.contexts[1];
}

void* CopyErrorMaterialKeyValues()
{
	const auto* const errorMaterial = *reinterpret_cast<const MaterialView* const*>(s_base + kErrorMaterialRva);
	if (!errorMaterial || !errorMaterial->keyValues)
		LoadFallbackError("the error material has no KeyValues");
	void* const keyValues = NativeFunction<KeyValuesConstructFunction>(kKeyValuesConstructRva)(
		NativeFunction<KeyValuesAllocateFunction>(kKeyValuesAllocateRva)(kKeyValuesBytes), "Basic");
	if (!keyValues)
		LoadFallbackError("could not allocate KeyValues");
	NativeFunction<KeyValuesCopyFunction>(kKeyValuesCopyRva)(keyValues, errorMaterial->keyValues);
	return keyValues;
}

void LogFallback(const MaterialView& material)
{
	if (s_loggedFallbacks.fetch_add(1, std::memory_order_relaxed) >= kLoggedFallbackLimit)
		return;
	const char* const name = reinterpret_cast<GetNameFunction>(material.vtable[0])(&material);
	Warning("R1Delta: material %s could not read its VMT; it uses the error material\n", name ? name : "<unnamed>");
}

char __fastcall InitializeDetour(MaterialView* material, void* keyValues, void* patchKeyValues, void* patches)
{
	const char initialized = s_originalInitialize(material, keyValues, patchKeyValues, patches);
	if (initialized || keyValues || !FailedToReadVmt(*material))
		return initialized;

	void* const fallback = CopyErrorMaterialKeyValues();
	const char fallbackInitialized = s_originalInitialize(material, fallback, nullptr, nullptr);
	NativeFunction<KeyValuesDeleteFunction>(kKeyValuesDeleteRva)(fallback);
	if (!material->contexts[0] || !material->contexts[1])
		LoadFallbackError("the error material KeyValues did not initialize the material");
	LogFallback(*material);
	return fallbackInitialized;
}

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
} // namespace

void InstallMaterialLoadFallback(std::uintptr_t materialSystemBase)
{
	if (!materialSystemBase
		|| s_installed
		|| GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015) {
		return;
	}
	if (!IsRetailImage(materialSystemBase))
		LoadFallbackError("unsupported image");
	void* const target = reinterpret_cast<void*>(materialSystemBase + kMaterialInitializeRva);
	if (std::memcmp(target, kMaterialInitializePrologue, sizeof(kMaterialInitializePrologue)) != 0)
		LoadFallbackError("unexpected CMaterial::Initialize code");

	s_base = materialSystemBase;
	const MH_STATUS created = MH_CreateHook(
		target, reinterpret_cast<void*>(&InitializeDetour), reinterpret_cast<void**>(&s_originalInitialize));
	const MH_STATUS enabled = created == MH_OK && s_originalInitialize ? MH_EnableHook(target) : MH_ERROR_NOT_CREATED;
	char buffer[256]{};
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: materialsystem_dx11 material load fallback hook at RVA 0x%llX: create=%d enable=%d\n",
		static_cast<unsigned long long>(kMaterialInitializeRva),
		static_cast<int>(created),
		static_cast<int>(enabled));
	OutputDebugStringA(buffer);
	if (created != MH_OK || !s_originalInitialize)
		LoadFallbackError("could not create the CMaterial::Initialize hook");
	if (enabled != MH_OK && enabled != MH_ERROR_ENABLED)
		LoadFallbackError("could not enable the CMaterial::Initialize hook");
	s_installed = true;
}
