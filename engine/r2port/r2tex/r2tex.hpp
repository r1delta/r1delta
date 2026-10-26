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
    maskRgInv
};

enum class LayerMode : std::uint8_t {
    current,
    layersOff,
    maskRg,
    maskRgInv
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

} // namespace r2tex
