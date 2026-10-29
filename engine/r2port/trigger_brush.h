#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// R2 trigger volumes -> R1 brush models.
//
// R2 does not put trigger brushes in the BSP. Every trigger entity (trigger_out_of_bounds, trigger_hurt,
// trigger_multiple, trigger_soundscape, trigger_indoor_area, trigger_capture_point, ...) carries its
// convex brushes in the entity text as
//     "*trigger_brush_<b>_plane_<p>" "nx ny nz d"      planes relative to the entity origin
//     "*trigger_bounds_mins" / "*trigger_bounds_maxs"   their union AABB
// and has no "model" key; the R2 engine builds the collision at spawn. R1 only knows triggers that
// reference a BSP brush model ("model" "*N"), so a converted map spawns every trigger without a volume:
// no trigger_out_of_bounds warning/kill, no trigger_hurt kill floor, no soundscape/indoor-area zones.
//
// Bake() appends one R1 brush model per trigger entity to an already converted (version 29) BSP and
// rewrites the entity text to reference it, exactly the data layout stock R1 maps use for their trigger
// volumes (mp_rise: model cell -> single-primitive geosets -> type 0 brush records, contents index of
// CONTENTS_SOLID 0x1, side texture TOOLS\TOOLSTRIGGER):
//   0x0E  models              +1 record (symmetric mins/maxs about the entity origin, no render meshes)
//   0x56  grid cells          +1 model cell {first geoset, count}
//   0x57/0x58 geosets+bounds  +1 geoset per brush (word = brush index << 8 | contents index)
//   0x5C  brushes             +1 record per brush (AABB origin/extents, non-axial side count)
//   0x01  planes              +1 plane per non-axial side
//   0x5D  side plane offsets  +1 u16 per non-axial side (offset encoding of the R1 brush plane reader)
//   0x5E  side properties     +6 axial + N non-axial u16 per brush (texdata index)
// Existing records are never modified; every index added is appended after the existing ones.
namespace r2port::trigger_brush {

// Bump whenever Bake can produce different output for the same input. The R2 map provider folds it
// into the fingerprint of its on-disk cache so cached BSPs and entity files are rebuilt.
inline constexpr std::uint32_t kBakeVersion = 1;

inline constexpr std::size_t kBspLumpCount = 128;

struct Report {
    std::uint64_t entitiesBaked = 0;
    std::uint64_t brushes = 0;
    std::uint64_t nonAxialPlanes = 0;
    std::uint64_t entitiesSkippedNoBrush = 0;   // unusable/empty planes: left as they were
    std::uint64_t entitiesSkippedHasModel = 0;  // already references a model: left as it was
    std::uint32_t firstModel = 0;               // index of the first model added
    std::uint32_t modelsAdded = 0;
    // Non-empty when the BSP could not take the models; the result is then unchanged.
    std::string failure;
};

struct Result {
    bool baked = false;
    // Replacement payloads by lump id; empty vector = lump unchanged.
    std::array<std::vector<std::uint8_t>, kBspLumpCount> lumps;
    // One text per input partition; identical to the input when !baked.
    std::vector<std::string> texts;
    Report report;
};

// `bsp` is the complete converted R1 (rBSP version 29) image. `partitions` are the entity texts that
// R1 spawns for the map in a fixed order that the caller must keep stable: lump 0 first, then the
// partition .ent files. Texts must already carry the entity rewrite (final class names).
// The lump 0 replacement in Result::lumps[0] is texts[0] verbatim.
Result Bake(const std::vector<std::uint8_t>& bsp, const std::vector<std::string_view>& partitions);

} // namespace r2port::trigger_brush
