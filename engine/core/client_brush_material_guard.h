#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace r1delta::client_brush_material
{
inline constexpr std::uintptr_t kMaterialCountRva = 0xB9A80;
inline constexpr std::ptrdiff_t kModelTypeOffset = 0x110;
inline constexpr std::ptrdiff_t kModelSharedDataOffset = 0x140;
inline constexpr std::ptrdiff_t kModelFirstSurfaceOffset = 0x148;
inline constexpr std::ptrdiff_t kModelSurfaceCountOffset = 0x14C;
inline constexpr std::ptrdiff_t kSharedSurfaceTableOffset = 0x70;
inline constexpr std::uintptr_t kSurfaceStride = 0x20;
inline constexpr std::uintptr_t kSurfaceMaterialOffset = 0x18;
inline constexpr std::int32_t kBrushModelType = 1;
inline constexpr std::uint32_t kExpectedTimeDateStamp = 0x55038DAC;
inline constexpr std::uint32_t kExpectedImageSize = 0x3264000;

inline constexpr std::uint8_t kExpectedMaterialCountPrologue[] = {
	0x41, 0x54, 0x48, 0x83, 0xEC, 0x60, 0x8B, 0x91,
	0x10, 0x01, 0x00, 0x00, 0x4C, 0x8B, 0xE1, 0xFF,
	0xCA, 0x74, 0x09, 0x33, 0xC0, 0x48, 0x83, 0xC4,
};

using MaterialCountFunction = int(__fastcall*)(const void* model);

template <typename T>
inline T ReadField(const void* base, std::ptrdiff_t offset) noexcept
{
	T value{};
	std::memcpy(
		&value,
		reinterpret_cast<const std::uint8_t*>(base) + offset,
		sizeof(value));
	return value;
}

// During client level initialization, inline brush models can become visible to
// entity OnDataChanged callbacks before the shared world surface table is
// published. The stock material counter dereferences that table unconditionally
// when nummodelsurfaces is nonzero. Report the material as unavailable so the
// existing func_breakablesurf fallback uses debug/debugempty during initialization.
inline bool ShouldReturnNoMaterials(const void* model) noexcept
{
	if (ReadField<std::int32_t>(model, kModelTypeOffset) != kBrushModelType)
		return false;
	if (ReadField<std::int32_t>(model, kModelSurfaceCountOffset) == 0)
		return false;

	const void* const shared = ReadField<const void*>(
		model, kModelSharedDataOffset);
	if (!shared)
		return true;
	return ReadField<const void*>(shared, kSharedSurfaceTableOffset) == nullptr;
}

inline int CountMaterialsWithGuard(
	const void* model,
	MaterialCountFunction original) noexcept
{
	if (!model || !original)
		return 0;
	if (ShouldReturnNoMaterials(model))
		return 0;
	return original(model);
}

inline bool IsExpectedModulePath(const wchar_t* modulePath) noexcept
{
	constexpr wchar_t suffix[] = L"\\bin\\x64_retail\\engine.dll";
	if (!modulePath)
		return false;

	const std::size_t pathLength = std::wcslen(modulePath);
	constexpr std::size_t suffixLength = sizeof(suffix) / sizeof(suffix[0]) - 1;
	return pathLength >= suffixLength
		&& _wcsicmp(modulePath + pathLength - suffixLength, suffix) == 0;
}

inline bool ShouldInstall(bool isClient2015, const wchar_t* modulePath) noexcept
{
	return isClient2015 && IsExpectedModulePath(modulePath);
}
}
