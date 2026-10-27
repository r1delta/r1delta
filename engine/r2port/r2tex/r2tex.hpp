// Origin: C:\Users\r3muxd\tmp\r2port\r2tex. R2 texture conversion library bundled with r1delta under GPL-3.0.
// Local adaptation: family cache size is configurable with a 256 MiB default.
#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace r2tex {

enum class TextureTransform : std::uint8_t {
    none,
    maskRg,
    maskRgInv,
    // Albedo of an opaque material with its alpha forced to 255 (R2 stores non-opacity data in albedo alpha).
    opaqueAlpha,
    // Albedo (guid) with its alpha multiplied by the R2 opacity map (slot 13, second guid in the synthetic name
    // r2tex/opamerge/<albedo guid>_<opacity guid>); RGBA8 output.
    opacityMerge,
    // Normal map (slots 1/24) re-encoded from R2 slopes to R1 unit-normal DXT1 (synthetic name r2tex/nrm/<texture name>).
    normalRgb
};

enum class LayerMode : std::uint8_t {
    current,      // default: layer mask expanded to R=G=mask (same output as maskRg)
    layersOff,
    maskRg,
    maskRgInv,
    redOnly       // legacy mapping: slot 22 passed through unexpanded (red only)
};

struct TextureRef {
    std::uint64_t guid = 0;
    std::string name;
    std::uint32_t slot = 0;
    TextureTransform transform = TextureTransform::none;
};

struct Material {
    std::string name;
    std::string vmt;
    std::vector<TextureRef> textures;
    bool usesLayers = false;
};

// Texture header facts of every R2 texture in the index (analysis).
struct TextureInfo {
    std::string name;
    std::uint32_t width = 0, height = 0, format = 0, mips = 0;
};
bool ListTextureInfo(std::vector<TextureInfo>& out, std::string* error = nullptr);

struct MaterialOptions {
    LayerMode layerMode = LayerMode::current;
};
struct TextureOptions {
    // Preserve source BC7 blocks in VTF 7.5 format 67. Disabled by default;
    // R1 texture creation and sampling still require runtime validation.
    bool bc7Passthrough = false;
    // Preserve source BC6H blocks in VTF 7.5 format 66. Disabled by default;
    // R1 texture creation and sampling still require runtime validation.
    bool bc6hPassthrough = false;
};


// Opens an R2 install and loads or builds the persistent asset index under cacheDir.
// The global database is immutable after Open; concurrent conversion calls are supported.
bool Open(const std::filesystem::path& installDir,
          const std::filesystem::path& cacheDir,
          std::string* error = nullptr);
// The cacheHit overload reports true only when a valid existing index was loaded.
// It remains false if the index was built or had to be rebuilt.
bool Open(const std::filesystem::path& installDir,
          const std::filesystem::path& cacheDir,
          std::string* error,
          bool* cacheHit);

void SetFamilyCacheLimitMiB(std::uint32_t megabytes) noexcept;

bool ConvertTexture(std::uint64_t guid, std::vector<std::uint8_t>& vtf,
                    std::string* error = nullptr);
bool ConvertTexture(std::string_view name, std::vector<std::uint8_t>& vtf,
                    std::string* error = nullptr);
void SetFamilyCacheLimitMiB(std::uint32_t megabytes) noexcept;

bool ConvertTexture(std::uint64_t guid, std::vector<std::uint8_t>& vtf,
                    const TextureOptions& options, std::string* error = nullptr);
bool ConvertTexture(std::string_view name, std::vector<std::uint8_t>& vtf,
                    const TextureOptions& options, std::string* error = nullptr);
bool ConvertTexture(const TextureRef& texture, std::vector<std::uint8_t>& vtf,
                    std::string* error = nullptr);
bool ConvertTexture(const TextureRef& texture, std::vector<std::uint8_t>& vtf,
                    const TextureOptions& options, std::string* error = nullptr);

// Existing materials use current layer mapping unless a mode is explicitly selected.
// maskRg/maskRgInv change slot 22 to a uniquely named RGBA8 mask texture.
bool ConvertMaterial(std::string_view name, Material& material,
                    const MaterialOptions& options, std::string* error = nullptr);

// Resolves a world material first, then model/UI variants (_fix, _skn, _gen).
bool ConvertMaterial(std::string_view name, Material& material,
                     std::string* error = nullptr);

// Reads the BSP texture-data name table. The optional R2 .stbsp is a mip-usage
// prioritization table, not a complete material/texture manifest, so it is not merged.
bool ListMapMaterials(const std::filesystem::path& bspPath,
                      std::vector<std::string>& names,
                      std::string* error = nullptr);
// Parses names from an already available BSP buffer without copying the BSP bytes.
bool ListMapMaterials(const std::uint8_t* bspBytes,
                      std::size_t size,
                      std::vector<std::string>& names,
                      std::string* error = nullptr);

// Package family (the unit r2tex decodes and caches) that holds the asset a request will convert. False when
// the name does not resolve; the request then misses or is cheap. Safe to call from any thread after Open.
bool FamilyOfMaterial(std::string_view name, std::uint32_t& family);
// Accepts the synthetic names ConvertMaterial emits (r2tex/opaque/, r2tex/opamerge/, r2tex/layer_masks/).
bool FamilyOfTexture(std::string_view name, std::uint32_t& family);
// True while the family's decoded paks are in the cache (a conversion in it needs no RTech decode).
bool IsFamilyResident(std::uint32_t family);
// Package families decoded since the process started (RTech decode + file read), and the wall time spent in them
// summed over threads. A converted map should load each family it touches once.
void GetFamilyLoadStats(std::uint64_t& loads, std::uint64_t& microseconds);

} // namespace r2tex
