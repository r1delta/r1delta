// Tests of engine/filesystem/r2_material_names.h (the r2w/ and r2m/ material namespaces).
// Build: cl /nologo /std:c++17 /EHsc /W4 /WX /I<code root> material_names_tests.cpp
#include "engine/filesystem/r2_material_names.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void Check(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

} // namespace

int main()
{
    using namespace r2port;
    std::string_view source;

    // The world namespace: the path R1 asks for is materials/r2w/<R2 name>.vmt.
    Check(StripWorldMaterialNamespace("r2w/models/vehicle/imc_carrier/floating_wall_panel_garage_02", source)
            && source == "models/vehicle/imc_carrier/floating_wall_panel_garage_02",
        "the carrier material is stripped to its R2 name");
    Check(!StripWorldMaterialNamespace("models/vehicle/imc_carrier/floating_wall_panel_garage_02", source),
        "a plain name (R1's) is not a world namespace name");
    Check(!StripWorldMaterialNamespace("r2w/", source), "the bare prefix names no material");
    Check(!StripWorldMaterialNamespace("r2m/world/x", source), "the model namespace is not the world namespace");
    Check(!StripWorldMaterialNamespace("xr2w/world/x", source), "the prefix only counts at the start");

    // MaterialSourceName: what the R2 packages are asked for.
    Check(MaterialSourceName("r2w/world/a") == "world/a", "world namespace stripped");
    Check(MaterialSourceName("r2m/world/a") == "world/a", "model namespace stripped");
    Check(MaterialSourceName("world/a") == "world/a", "plain name unchanged");
    Check(MaterialSourceName("r2w/r2m/a") == "r2m/a", "only one level is stripped");

    // A world material the R2 packages do not carry resolves to an include of the plain name.
    Check(WorldMaterialFallbackVmt("world/dev/edge_detection_enemy")
            == "\"patch\"\r\n{\r\n\t\"include\"\t\t\"materials/world/dev/edge_detection_enemy.vmt\"\r\n}\r\n",
        "fallback patch text");

    // The texture namespace: R2 and R1 reuse texture names for different data.
    Check(StripTextureNamespace("r2t/world/metal/floating_wall_panel_garage_02_spc", source)
            && source == "world/metal/floating_wall_panel_garage_02_spc",
        "texture namespace stripped");
    Check(!StripTextureNamespace("world/metal/floating_wall_panel_garage_02_spc", source), "a plain texture name is R1's");
    Check(!StripTextureNamespace("r2tex/nrm/world/a", source), "r2tex/ is not r2t/");
    Check(NamespacedTextureName("world/metal/a_spc") == "r2t/world/metal/a_spc", "source texture moved into r2t/");
    Check(NamespacedTextureName("r2tex/nrm/world/metal/a_nml") == "r2tex/nrm/world/metal/a_nml", "synthetic r2tex textures keep their name");

    {
        std::string vmt = "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"world\\metal\\a_col\"\n\t\"$bumpmap\" \"r2tex\\nrm\\world\\metal\\a_nml\"\n"
                          "\t\"$reflectiontint\" \"world\\metal\\a_spc\"\n\t\"$shininess\" \"world\\metal\\a_spc\"\n}\n";
        Check(NamespaceVmtTexture(vmt, "world/metal/a_col") == 1, "one reference moved");
        Check(NamespaceVmtTexture(vmt, "world/metal/a_spc") == 2, "every reference of a texture moves");
        Check(NamespaceVmtTexture(vmt, "r2tex/nrm/world/metal/a_nml") == 0, "synthetic textures are not touched");
        Check(NamespaceVmtTexture(vmt, "world/metal/missing") == 0, "a texture the VMT does not name is reported (0)");
        Check(vmt.find("\"r2t\\world\\metal\\a_col\"") != std::string::npos
                && vmt.find("\"r2t\\world\\metal\\a_spc\"") != std::string::npos
                && vmt.find("\"r2tex\\nrm\\world\\metal\\a_nml\"") != std::string::npos
                && vmt.find("\"world\\metal\\a_col\"") == std::string::npos,
            "VMT text after the rewrite");
        // a name that merely contains another is not rewritten
        std::string longer = "\"$basetexture\" \"world\\metal\\a_col_2\"\n";
        Check(NamespaceVmtTexture(longer, "world/metal/a_col") == 0 && longer.find("r2t") == std::string::npos, "only whole quoted names move");
    }

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("r2_material_names tests passed\n");
    return 0;
}
