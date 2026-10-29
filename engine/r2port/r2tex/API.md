# r2tex public API

C++17; include `r2tex.hpp` and link `r2tex.lib`. The public API is stable from this sync onward: changes will be additive only.

## Data types

```cpp
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
    std::uint64_t guid = 0;          // source txtr GUID
    std::string name;                // output path referenced by the VMT
    std::uint32_t slot = 0;          // R2 material slot
    TextureTransform transform = TextureTransform::none;
};

struct Material {
    std::string name;
    std::string vmt;
    std::vector<TextureRef> textures;
    bool usesLayers = false;         // true if this matl has layer slots 22/23/24/26
};

struct TextureOptions {
    bool bc7Passthrough = false;
    bool bc6hPassthrough = false;
};

struct MaterialOptions {
    LayerMode layerMode = LayerMode::current;
};
```

`Material::textures` lists only source textures referenced by the emitted VMT.
`TextureRef::name` is the virtual output path; transformed slot-22 masks keep the original
`guid` and specify `maskRg` or `maskRgInv`. `Material::vmt` is the VMT body; the API performs no
output-file writes.

## Functions

```cpp
bool Open(const std::filesystem::path& installDir,
          const std::filesystem::path& cacheDir,
          std::string* error = nullptr);

bool Open(const std::filesystem::path& installDir,
          const std::filesystem::path& cacheDir,
          std::string* error,
          bool* cacheHit);

bool ConvertTexture(std::uint64_t guid,
                    std::vector<std::uint8_t>& vtf,
                    std::string* error = nullptr);

bool ConvertTexture(std::string_view name,
                    std::vector<std::uint8_t>& vtf,
                    std::string* error = nullptr);

bool ConvertTexture(std::uint64_t guid,
                    std::vector<std::uint8_t>& vtf,
                    const TextureOptions& options,
                    std::string* error = nullptr);

bool ConvertTexture(std::string_view name,
                    std::vector<std::uint8_t>& vtf,
                    const TextureOptions& options,
                    std::string* error = nullptr);

bool ConvertTexture(const TextureRef& texture,
                    std::vector<std::uint8_t>& vtf,
                    std::string* error = nullptr);

bool ConvertTexture(const TextureRef& texture,
                    std::vector<std::uint8_t>& vtf,
                    const TextureOptions& options,
                    std::string* error = nullptr);

bool ConvertMaterial(std::string_view name,
                    Material& material,
                    std::string* error = nullptr);

bool ConvertMaterial(std::string_view name,
                    Material& material,
                    const MaterialOptions& options,
                    std::string* error = nullptr);

bool ListMapMaterials(const std::filesystem::path& bspPath,
                      std::vector<std::string>& names,
                      std::string* error = nullptr);

bool ListMapMaterials(const std::uint8_t* bspBytes,
                      std::size_t size,
                      std::vector<std::string>& names,
                      std::string* error = nullptr);
```

- `Open` loads or builds the persistent index and installs the immutable global database. Call once during provider initialization, outside a file-open callback. `cacheHit` is set false before attempting to open; on success it is true only when a valid existing index was loaded. It remains false when the index is built or rebuilt. The original three-argument overload remains available.
- `ConvertTexture` accepts a R2 texture GUID or name and replaces `vtf` with VTF bytes on success.
- The original `ConvertTexture` overloads preserve the established decoded behavior. The additive overloads accept `TextureOptions`; both passthrough switches default to false. When enabled, BC7 is written as VTF image format 67 (source sRGB flag preserved) and BC6H as format 66, with source mip blocks retained. R1 maps these formats to DXGI BC7_UNORM and BC6H_UF16; BC7 is verified in game (retail materialsystem_dx11.dll accepts format 67 end to end), but its sRGB flag only samples as sRGB with the 54 byte patch of the switch at RVA 0x16663 (`engine/r2port/r2_bc7_srgb.h`, applied by `engine/hooks/patcher.cpp`; `r2port::MatsysBc7SrgbPatched()` tells the provider). BC6H is unverified, leave it off. `bc7Passthrough` applies to a BC7 source only when its size is a multiple of 4 and `addMipChain` would not rebuild its mip chain (single mip or cut-off chain textures, e.g. 4096 px skies, keep the decoded RGBA8 path); of the transform textures only the opaque albedo alias keeps it (`forceOpaqueVtf` rewrites the BC7 alpha endpoints so the block decodes to alpha 255: bit exact colour for modes 0 to 5, up to 1/255 (mode 6) and 4/255 (mode 7) colour shift on blocks whose alpha was not 255), normal, opacity-merge, layer-mask, gloss and uber textures decode as before. The provider enables it by default (`R2PORT_BC7=0` turns it off).
- `ConvertMaterial` resolves a world/model/UI material variant and fills `material` with the VMT and
  its referenced source textures. `MaterialOptions::layerMode` defaults to `current`; `layersOff`
  uses only slot 0 on layered materials, while `maskRg`/`maskRgInv` expand slot 22 to a uniquely
  named linear RGBA8 texture with R=G=mask or inverse-mask. The CLI names are `current`, `layers-off`,
  `mask-rg`, and `mask-rg-inv`.
- Functions return `true` on success and `false` on failure. When provided, `error` receives a diagnostic and is cleared on success. Output vectors/objects are cleared on failure.

## API change log

### Provider integration reconciliation

- Added the pointer-and-size `ListMapMaterials` overload so converted BSP bytes can be parsed without a second BSP-sized copy. The path overload is unchanged.
- Added the four-argument `Open` overload with `bool* cacheHit` for provider first-run/cache logging. The original three-argument `Open` remains source-compatible.
- Added `TextureOptions` and additive `ConvertTexture` overloads for opt-in BC7/BC6H passthrough. Existing overloads and default output are unchanged.
- Added `TextureTransform`, `LayerMode`, `MaterialOptions`, `TextureRef::transform`, and
  `Material::usesLayers` for additive map-wide layer experiments. Added `ConvertMaterial` options
  and `ConvertTexture(TextureRef, ...)` overloads so a material's slot-22 mask can be expanded
  without changing the original GUID/name conversion behavior.
- No other public signatures changed. Future API changes are additive only.

### Lazy pak decode (PakIndex)

- `r2tex.index` (version 11) now persists every rpak's page layout (header, page sizes/offsets, starpak list) next to the asset table, and validates a pak by name, size and a hash of its RPak header (created time, crc, compressed size) instead of its last-write time or the install path, so an index survives a copy or touch of the install and can be prebuilt. A warm start creates a family without decoding anything; a pak is read and RTech-decoded the first time a page it owns is read (`readPageRange`, the paks of one range decode concurrently under the global pak-load cap). Only the cold index scan decodes every pak. Converted output is byte identical (FNV-1a of every converted VMT/VTF of drydock, glitch, homestead, thaw, lf_stacks compared lazy vs whole-family).
- `R2TEX_LAZY_PAKS=0` restores the whole-family decode at first use. `IsFamilyResident` is false while a decode runs in the family; `FamilyDecodedBytes` is still the conservative whole-family size.
