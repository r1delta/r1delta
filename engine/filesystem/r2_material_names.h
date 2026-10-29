#pragma once

#include <string>
#include <string_view>

// Name spaces of the materials the R2 port serves, so that no file of a stock R1 map pack can answer them.
//
// R1 resolves a material name through its whole file system, and the VPK pack of every stock map that was played
// stays mounted for the rest of the process (mp_outpost_207 and 16 more ship models/vehicle/imc_carrier/*.vmt,
// models/world/metal/bldg_wall_metal_imc_01.vmt and about 700 other names that R2 world meshes use). A stock map
// loaded earlier therefore replaced the R2 material of a converted map by an R1 model material
// (VertexLitGeneric), which draws nothing on a world mesh (mp_drydock carrier hull, mp_black_water_canal walls).
//
//   r2w/<name>  every world material of a converted BSP (bspdown rule 25 renames the texdata names); tools/ names are
//               not renamed (R1 owns the tool materials). Served by the material provider from the R2 material <name>.
//   r2m/<name>  every material of a converted R2 model (modelconv renames them).
//   r2t/<name>  every source texture a converted VMT names (r2tex's synthetic r2tex/... textures are unique already).
//               R2 and R1 reuse texture names for different data: models/world/metal/floating_wall_panel_garage_02_spc is a
//               specular colour in both, but the R2 file is a different image (mean 26 against 133), and a world/detail/*_dm_nml
//               normal map is a flat 128 127 255 in R2 and an R1 style swizzled map (0 0 65) in R1.
namespace r2port {

inline constexpr std::string_view kWorldMaterialNamespace = "r2w/";
inline constexpr std::string_view kModelMaterialNamespace = "r2m/";
inline constexpr std::string_view kTextureNamespace = "r2t/";
inline constexpr std::string_view kSyntheticTexturePrefix = "r2tex/";

// If assetName (a material or texture name without "materials/" and extension) lives in the world namespace, stores
// the R2 name behind it in `source` and returns true.
inline bool StripWorldMaterialNamespace(std::string_view assetName, std::string_view& source)
{
    if (assetName.size() <= kWorldMaterialNamespace.size()
        || assetName.compare(0, kWorldMaterialNamespace.size(), kWorldMaterialNamespace) != 0)
        return false;
    source = assetName.substr(kWorldMaterialNamespace.size());
    return true;
}

// The R2 material name behind a model (r2m/) or world (r2w/) namespace, or the name itself.
inline std::string_view MaterialSourceName(std::string_view assetName)
{
    std::string_view source;
    if (StripWorldMaterialNamespace(assetName, source))
        return source;
    if (assetName.size() > kModelMaterialNamespace.size()
        && assetName.compare(0, kModelMaterialNamespace.size(), kModelMaterialNamespace) == 0)
        return assetName.substr(kModelMaterialNamespace.size());
    return assetName;
}

// If assetName (a texture name without "materials/" and ".vtf") lives in the texture namespace, stores the R2 texture
// name behind it in `source` and returns true.
inline bool StripTextureNamespace(std::string_view assetName, std::string_view& source)
{
    if (assetName.size() <= kTextureNamespace.size()
        || assetName.compare(0, kTextureNamespace.size(), kTextureNamespace) != 0)
        return false;
    source = assetName.substr(kTextureNamespace.size());
    return true;
}

// The name R1 is told for a source texture of a converted VMT: r2tex's own synthetic textures keep their name, every
// other texture moves into the texture namespace.
inline std::string NamespacedTextureName(std::string_view textureName)
{
    if (textureName.compare(0, kSyntheticTexturePrefix.size(), kSyntheticTexturePrefix) == 0)
        return std::string(textureName);
    std::string name(kTextureNamespace);
    name.append(textureName);
    return name;
}

// Moves one texture of a converted VMT into the texture namespace: every quoted occurrence of its name (r2tex writes the
// lower case name with backslashes) is replaced. Returns the number of occurrences replaced (0 = the VMT does not name
// the texture literally, or the texture is a synthetic r2tex one).
inline std::size_t NamespaceVmtTexture(std::string& vmt, std::string_view textureName)
{
    if (textureName.empty() || textureName.compare(0, kSyntheticTexturePrefix.size(), kSyntheticTexturePrefix) == 0)
        return 0;
    std::string plain(textureName);
    std::string renamed(kTextureNamespace);
    renamed.append(textureName);
    for (char& c : plain)
        if (c == '/')
            c = '\\';
    for (char& c : renamed)
        if (c == '/')
            c = '\\';
    const std::string needle = "\"" + plain + "\"";
    const std::string replacement = "\"" + renamed + "\"";
    std::size_t count = 0;
    for (std::size_t at = vmt.find(needle); at != std::string::npos; at = vmt.find(needle, at + replacement.size())) {
        vmt.replace(at, needle.size(), replacement);
        ++count;
    }
    return count;
}

// What a world material the R2 packages do not carry resolves to: a patch that includes the same name outside the
// namespace, so whatever R1 or the active map pack has under that name still answers (debug/debugempty,
// world/dev/edge_detection_enemy and the like) exactly as it did before the namespace existed.
inline std::string WorldMaterialFallbackVmt(std::string_view source)
{
    std::string vmt = "\"patch\"\r\n{\r\n\t\"include\"\t\t\"materials/";
    vmt.append(source);
    vmt.append(".vmt\"\r\n}\r\n");
    return vmt;
}

} // namespace r2port
