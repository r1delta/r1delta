// Origin: C:\Users\r3muxd\tmp\r2port\r2tex. R2 texture conversion library bundled with r1delta under GPL-3.0.
// Local adaptation: family cache size is configurable with a 256 MiB default.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace r2tex {

struct TextureRef {
    std::uint64_t guid = 0;
    std::string name;
    std::uint32_t slot = 0;
};

struct Material {
    std::string name;
    std::string vmt;
    std::vector<TextureRef> textures;
};

// Opens an R2 install and loads or builds the persistent asset index under cacheDir.
// The global database is immutable after Open; concurrent conversion calls are supported.
bool Open(const std::filesystem::path& installDir,
          const std::filesystem::path& cacheDir,
          std::string* error = nullptr);

void SetFamilyCacheLimitMiB(std::uint32_t megabytes) noexcept;

bool ConvertTexture(std::uint64_t guid, std::vector<std::uint8_t>& vtf,
                    std::string* error = nullptr);
bool ConvertTexture(std::string_view name, std::vector<std::uint8_t>& vtf,
                    std::string* error = nullptr);

// Resolves a world material first, then model/UI variants (_fix, _skn, _gen).
bool ConvertMaterial(std::string_view name, Material& material,
                     std::string* error = nullptr);

// Reads the BSP texture-data name table. The optional R2 .stbsp is a mip-usage
// prioritization table, not a complete material/texture manifest, so it is not merged.
bool ListMapMaterials(const std::filesystem::path& bspPath,
                      std::vector<std::string>& names,
                      std::string* error = nullptr);

} // namespace r2tex
