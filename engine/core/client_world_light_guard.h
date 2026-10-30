#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Stock R1 client engine.dll only (TimeDateStamp 0x55038DAC, SizeOfImage 0x3264000; same image
// check as client_brush_material_guard.h). All RVAs are into that image.
namespace r1delta::client_world_light
{
// sub_1801505E0: engine light-at-point query behind the IVEngineClient light interface that
// CParticleSystemQuery::GetLightingAtPoint (client+0x1E6E00) calls for particle operators such as
// C_INIT_RandomColor. It reads the world light-probe table (sub_1800B3940 / sub_1800AE480) before the
// stock "no world lights -> white" test in sub_18014FDB0, so it faults when the world brush data is
// not loaded.
inline constexpr std::uintptr_t kLightForPointRva = 0x1505E0;
// sub_1800B9DE0: CModelLoader world-model unload (type 1). Ends with memset(loader + 0xE8, 0, 0x1D8),
// which is the single shared world brush storage (host_state.worldbrush).
inline constexpr std::uintptr_t kMapUnloadModelRva = 0xB9DE0;
// qword_1807C0AF8: host_state.worldmodel (model_t*).
inline constexpr std::uintptr_t kWorldModelRva = 0x7C0AF8;
// qword_1807C0B00: worldmodel->brush shared data (model_t+0x140), == &CModelLoader+0xE8, a static in engine .data.
inline constexpr std::uintptr_t kWorldBrushRva = 0x7C0B00;
inline constexpr std::ptrdiff_t kBrushProbeTableOffset = 0x178;
inline constexpr std::ptrdiff_t kBrushWorldLightsOffset = 0x1B8;
inline constexpr std::ptrdiff_t kModelNameOffset = 4;
inline constexpr std::size_t kModelNameLength = 260;

inline constexpr std::uint8_t kExpectedLightForPointPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
	0x24, 0x10, 0x57, 0x48, 0x81, 0xEC, 0xC0, 0x00,
	0x00, 0x00,
};

inline constexpr std::uint8_t kExpectedMapUnloadModelPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
	0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
	0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x48, 0x83,
	0xEC, 0x20,
};

// Register layout of sub_1801505E0 (verified from the disassembly, not only the decompiler):
// rcx origin, rdx passed through, r8b flag, xmm3 float scale, then stack slots: multiplier colour
// pointer, override colour pointer, byte flag, output Vector*. Returns the output pointer.
using LightForPointFunction = float*(__fastcall*)(
	const float* origin,
	std::int64_t pass,
	std::uint8_t flag,
	float scale,
	const float* multiplier,
	const float* overrideColor,
	char byteFlag,
	float* out);

using MapUnloadModelFunction = std::int64_t(__fastcall*)(void* loader, void* model);

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

// The probe table is published by Mod_LoadLightProbes/Mod_LoadLightProbeTree during world load and
// is zeroed with the rest of the brush storage on unload. No table means there is no world to
// light a point in.
inline bool WorldLightProbesUnavailable(const void* brush) noexcept
{
	if (!brush)
		return true;
	return ReadField<const void*>(brush, kBrushProbeTableOffset) == nullptr;
}

// Same result the stock engine produces for a world without worldlights (sub_18014FDB0 early out)
// and the same value CParticleSystemQuery::GetLightingAtPoint uses when the client is not in game.
inline float* WhiteLightFallback(float* out) noexcept
{
	if (out) {
		out[0] = 1.0f;
		out[1] = 1.0f;
		out[2] = 1.0f;
	}
	return out;
}
}
