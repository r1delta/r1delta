// Offline build/check of the R2 port shaders (same code the engine runs at startup).
//   r2_shader_cli.exe <outdir> [file.hlsl ...]
// Writes <outdir>\<name>_ps40.vcs / <name>_vs40.vcs for every embedded shader and every extra .hlsl file
// (name = file stem). Exit code 1 when any shader fails to compile, link or validate.
#include "engine/r2port/shaders/r2_shader_compiler.h"
#include "engine/r2port/shaders/r2_shader_sources.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace r2port::shaders;

namespace {

bool ResolveInclude(std::string_view name, std::string& text)
{
    if (name == "r2_common.hlsli") {
        text = kCommonInclude;
        return true;
    }
    return false;
}

bool WriteFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

bool Build(const std::filesystem::path& outDir, const std::string& name, const std::string& source)
{
    const BuiltShader built = BuildShader(name, source, &ResolveInclude);
    if (!built.error.empty()) {
        std::printf("FAIL %s\n%s\n", name.c_str(), built.error.c_str());
        return false;
    }
    bool ok = true;
    if (built.hasPixel) {
        const std::string problem = ValidateVcs(built.pixelVcs, built.pixelStatics, built.pixelRecords);
        ok = ok && problem.empty() && WriteFile(outDir / (name + "_ps40.vcs"), built.pixelVcs);
        std::printf("%s %s_ps40.vcs %zu bytes %s\n", problem.empty() ? "ok  " : "BAD ", name.c_str(),
            built.pixelVcs.size(), problem.c_str());
    }
    if (built.hasVertex) {
        std::string problem = ValidateVcs(built.vertexVcs, 1);
        if (problem.empty())
            problem = ValidateVcs(built.vertexColorVcs, 1);
        ok = ok && problem.empty() && WriteFile(outDir / (name + "_vs40.vcs"), built.vertexVcs)
            && WriteFile(outDir / (name + "_vcol_vs40.vcs"), built.vertexColorVcs);
        std::printf("%s %s_vs40.vcs + _vcol %zu bytes %s\n", problem.empty() ? "ok  " : "BAD ", name.c_str(),
            built.vertexVcs.size(), problem.c_str());
    }
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: r2_shader_cli <outdir> [file.hlsl ...]\n");
        return 2;
    }
    const std::filesystem::path outDir = argv[1];
    std::filesystem::create_directories(outDir);
    bool ok = true;
    for (const EmbeddedShader& shader : kEmbeddedShaders) {
        std::string text = shader.source;
        if (shader.define)
            text = std::string("#define ") + shader.define + " 1\n" + text;
        ok = Build(outDir, shader.name, text) && ok;
    }
    for (int i = 2; i < argc; ++i) {
        std::ifstream in(argv[i], std::ios::binary);
        const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        ok = Build(outDir, std::filesystem::path(argv[i]).stem().string(), source) && ok;
    }
    return ok ? 0 : 1;
}
