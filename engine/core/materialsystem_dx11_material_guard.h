#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace r1delta::materialsystem_dx11
{
inline constexpr std::uintptr_t kMaterialSetupErrorShaderRva = 0x386E0;
inline constexpr std::uintptr_t kMaterialInitializeRva = 0x3AB60;
inline constexpr std::uintptr_t kMaterialIsErrorRva = 0x3B010;
inline constexpr std::uintptr_t kMaterialAlphaModulationRva = 0x3B2E0;
inline constexpr std::uintptr_t kMaterialReflectivityRva = 0x3B330;
inline constexpr std::uintptr_t kMaterialAlphaVtableEntryRva = 0x184F30;
inline constexpr std::uintptr_t kMaterialReflectivityVtableEntryRva = 0x184F38;
inline constexpr std::uintptr_t kMaterialBoolPropertyRvas[] = {
	0x3AFB0,
	0x3B070,
	0x3B0D0,
	0x3B120
};

inline constexpr std::ptrdiff_t kMaterialShaderOffset = 0x10;
inline constexpr std::ptrdiff_t kMaterialFlagsOffset = 0x26;
inline constexpr std::ptrdiff_t kMaterialProxyCountOffset = 0x2C;
inline constexpr std::ptrdiff_t kMaterialRenderStateOwnerOffset = 0x30;
inline constexpr std::ptrdiff_t kMaterialPrimaryContextOffset = 0x38;
inline constexpr std::ptrdiff_t kMaterialSecondaryContextOffset = 0x40;
inline constexpr std::ptrdiff_t kMaterialProxyOwnerOffset = 0x50;
inline constexpr std::uint16_t kMaterialInitializedFlag = 0x4;

inline constexpr std::uint8_t kExpectedMaterialSetupErrorShaderPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08,
	0x48, 0x89, 0x6C, 0x24, 0x10,
	0x48, 0x89, 0x74, 0x24, 0x18,
	0x57,
	0xB8, 0x60, 0x17, 0x00, 0x00,
	0xE8, 0x86, 0xE4, 0x0F, 0x00,
	0x48, 0x2B, 0xE0
};

inline constexpr std::uint8_t kExpectedMaterialInitializePrologue[] = {
	0x40, 0x57,
	0x41, 0x55,
	0x48, 0x83, 0xEC, 0x48,
	0x48, 0x89, 0x5C, 0x24, 0x60,
	0x48, 0x89, 0x6C, 0x24, 0x68,
	0x48, 0x89, 0x74, 0x24, 0x70,
	0x4C, 0x89, 0x64, 0x24, 0x40
};

inline constexpr std::uint8_t kExpectedMaterialProperty0Prologue[] = {
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20,
	0x0F, 0xB6, 0x41, 0x26, 0x48, 0x8B, 0xD9,
	0xC0, 0xE8, 0x02, 0xA8, 0x01, 0x75, 0x0D,
	0x45, 0x33, 0xC9, 0x45, 0x33, 0xC0, 0x33, 0xD2,
	0xE8, 0x8F, 0xFB, 0xFF, 0xFF
};

inline constexpr std::uint8_t kExpectedMaterialIsErrorPrologue[] = {
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20,
	0x0F, 0xB6, 0x41, 0x26, 0x48, 0x8B, 0xD9,
	0xC0, 0xE8, 0x02, 0xA8, 0x01, 0x75, 0x0D,
	0x45, 0x33, 0xC9, 0x45, 0x33, 0xC0, 0x33, 0xD2,
	0xE8, 0x2F, 0xFB, 0xFF, 0xFF
};

inline constexpr std::uint8_t kExpectedMaterialProperty1Prologue[] = {
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20,
	0x0F, 0xB6, 0x41, 0x26, 0x48, 0x8B, 0xD9,
	0xC0, 0xE8, 0x02, 0xA8, 0x01, 0x75, 0x0D,
	0x45, 0x33, 0xC9, 0x45, 0x33, 0xC0, 0x33, 0xD2,
	0xE8, 0xCF, 0xFA, 0xFF, 0xFF
};

inline constexpr std::uint8_t kExpectedMaterialProperty2Prologue[] = {
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20,
	0x0F, 0xB6, 0x41, 0x26, 0x48, 0x8B, 0xD9,
	0xC0, 0xE8, 0x02, 0xA8, 0x01, 0x75, 0x0D,
	0x45, 0x33, 0xC9, 0x45, 0x33, 0xC0, 0x33, 0xD2,
	0xE8, 0x6F, 0xFA, 0xFF, 0xFF
};

inline constexpr std::uint8_t kExpectedMaterialProperty3Prologue[] = {
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20,
	0x0F, 0xB6, 0x41, 0x26, 0x48, 0x8B, 0xD9,
	0xC0, 0xE8, 0x02, 0xA8, 0x01, 0x75, 0x0D,
	0x45, 0x33, 0xC9, 0x45, 0x33, 0xC0, 0x33, 0xD2,
	0xE8, 0x1F, 0xFA, 0xFF, 0xFF
};

inline constexpr std::uint8_t kExpectedMaterialAlphaModulationPrologue[] = {
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20,
	0x0F, 0xB6, 0x41, 0x26, 0x48, 0x8B, 0xD9,
	0xC0, 0xE8, 0x02, 0xA8, 0x01, 0x75, 0x0D,
	0x45, 0x33, 0xC9, 0x45, 0x33, 0xC0, 0x33, 0xD2,
	0xE8, 0x5F, 0xF8, 0xFF, 0xFF
};

inline constexpr std::uint8_t kExpectedMaterialReflectivityPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x57,
	0x48, 0x83, 0xEC, 0x20,
	0x0F, 0xB6, 0x41, 0x26,
	0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9,
	0xC0, 0xE8, 0x02, 0xA8, 0x01, 0x75, 0x0D,
	0x45, 0x33, 0xC9, 0x45, 0x33, 0xC0, 0x33, 0xD2,
	0xE8, 0x08, 0xF8, 0xFF, 0xFF
};

using MaterialInitializeFunction = std::int64_t(__fastcall*)(
	std::int64_t material,
	std::int64_t vmt,
	std::int64_t vmtPatches,
	std::int64_t context);
using MaterialSetupErrorShaderFunction = void(__fastcall*)(
	std::int64_t material);
using MaterialAlphaModulationFunction = float(__fastcall*)(
	std::int64_t material);
using MaterialReflectivityFunction = void(__fastcall*)(
	std::int64_t material,
	float* output);

struct MaterialContextState
{
	bool markedInitialized{};
	std::uintptr_t shader{};
	std::uint32_t proxyCount{};
	std::uintptr_t renderStateOwner{};
	std::uintptr_t primaryContext{};
	std::uintptr_t secondaryContext{};
	std::uintptr_t proxyOwner{};

	[[nodiscard]] bool IsUsable() const noexcept
	{
		return markedInitialized && shader && primaryContext && secondaryContext;
	}

	[[nodiscard]] bool HasPristineOwners() const noexcept
	{
		return !shader
			&& !proxyCount
			&& !renderStateOwner
			&& !primaryContext
			&& !secondaryContext
			&& !proxyOwner;
	}

	[[nodiscard]] bool CanInitialize() const noexcept
	{
		return !markedInitialized && HasPristineOwners();
	}

	[[nodiscard]] bool CanBuildErrorState() const noexcept
	{
		return markedInitialized && HasPristineOwners();
	}
};

inline MaterialContextState ReadMaterialContextState(
	std::uintptr_t material) noexcept
{
	if (!material)
		return {};

	const auto* const bytes = reinterpret_cast<const std::uint8_t*>(material);
	return {
		(*reinterpret_cast<const std::uint16_t*>(
			bytes + kMaterialFlagsOffset) & kMaterialInitializedFlag) != 0,
		*reinterpret_cast<const std::uintptr_t*>(
			bytes + kMaterialShaderOffset),
		*reinterpret_cast<const std::uint32_t*>(
			bytes + kMaterialProxyCountOffset),
		*reinterpret_cast<const std::uintptr_t*>(
			bytes + kMaterialRenderStateOwnerOffset),
		*reinterpret_cast<const std::uintptr_t*>(
			bytes + kMaterialPrimaryContextOffset),
		*reinterpret_cast<const std::uintptr_t*>(
			bytes + kMaterialSecondaryContextOffset),
		*reinterpret_cast<const std::uintptr_t*>(
			bytes + kMaterialProxyOwnerOffset)
	};
}

inline std::recursive_mutex& MaterialInitializationMutex()
{
	static std::recursive_mutex mutex;
	return mutex;
}

inline unsigned int& MaterialInitializationDepth() noexcept
{
	static thread_local unsigned int depth;
	return depth;
}

class MaterialInitializationScope
{
public:
	MaterialInitializationScope() noexcept
	{
		++MaterialInitializationDepth();
	}

	~MaterialInitializationScope()
	{
		--MaterialInitializationDepth();
	}

	MaterialInitializationScope(const MaterialInitializationScope&) = delete;
	MaterialInitializationScope& operator=(const MaterialInitializationScope&) = delete;
};

[[nodiscard]] inline bool IsMaterialInitializationInProgress() noexcept
{
	return MaterialInitializationDepth() != 0;
}

inline bool IsUsableMaterialInitialization(
	const MaterialContextState& state) noexcept
{
	return state.IsUsable();
}

inline bool FinalizeMaterialInitialization(
	std::uintptr_t material,
	MaterialSetupErrorShaderFunction setupErrorShader,
	bool publishInitializedFlag = false) noexcept
{
	if (!material)
		return false;

	std::lock_guard<std::recursive_mutex> lock(MaterialInitializationMutex());
	MaterialContextState state = ReadMaterialContextState(material);
	if (state.IsUsable())
		return true;
	if (!setupErrorShader
		|| IsMaterialInitializationInProgress()
		|| !state.HasPristineOwners()
		|| (!state.markedInitialized && !publishInitializedFlag))
		return false;

	if (!state.markedInitialized) {
		auto* const flags = reinterpret_cast<std::uint16_t*>(
			material + kMaterialFlagsOffset);
		*flags |= kMaterialInitializedFlag;
	}
	{
		MaterialInitializationScope initializationScope;
		setupErrorShader(static_cast<std::int64_t>(material));
	}
	state = ReadMaterialContextState(material);
	return state.IsUsable();
}

inline bool EnsureMaterialContext(
	std::uintptr_t material,
	MaterialInitializeFunction initialize,
	MaterialSetupErrorShaderFunction setupErrorShader = nullptr) noexcept
{
	if (!material)
		return false;

	std::lock_guard<std::recursive_mutex> lock(MaterialInitializationMutex());
	MaterialContextState state = ReadMaterialContextState(material);
	if (state.IsUsable())
		return true;
	if (IsMaterialInitializationInProgress())
		return false;
	if (state.CanBuildErrorState())
		return FinalizeMaterialInitialization(material, setupErrorShader);
	if (!initialize || !state.CanInitialize())
		return false;

	initialize(static_cast<std::int64_t>(material), 0, 0, 0);
	state = ReadMaterialContextState(material);
	if (state.IsUsable())
		return true;
	if (!state.HasPristineOwners())
		return false;
	return FinalizeMaterialInitialization(
		material,
		setupErrorShader,
		true);
}

inline bool InvokeMaterialAlphaModulation(
	std::uintptr_t material,
	MaterialInitializeFunction initialize,
	MaterialAlphaModulationFunction original,
	float* output,
	MaterialSetupErrorShaderFunction setupErrorShader = nullptr) noexcept
{
	if (!material || !original || !output)
		return false;

	std::lock_guard<std::recursive_mutex> lock(MaterialInitializationMutex());
	if (!EnsureMaterialContext(material, initialize, setupErrorShader))
		return false;

	*output = original(static_cast<std::int64_t>(material));
	return true;
}

inline bool InvokeMaterialReflectivity(
	std::uintptr_t material,
	MaterialInitializeFunction initialize,
	MaterialReflectivityFunction original,
	float* output,
	MaterialSetupErrorShaderFunction setupErrorShader = nullptr) noexcept
{
	if (!material || !original || !output)
		return false;

	std::lock_guard<std::recursive_mutex> lock(MaterialInitializationMutex());
	if (!EnsureMaterialContext(material, initialize, setupErrorShader))
		return false;

	original(static_cast<std::int64_t>(material), output);
	return true;
}
}
