// Origin: C:\Users\r3muxd\tmp\r2port\bspdown. Bundled in r1delta under GPL-3.0.
// bspdown <in_r2.bsp> <out_r1.bsp> [options]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "bspdown.hpp"

static bool readFile(const char* path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
#if defined(_WIN32)
    long long n = _ftelli64(f);
#else
    long long n = std::ftell(f);
#endif
    std::fseek(f, 0, SEEK_SET);
    out.resize((size_t)n);
    size_t got = n ? std::fread(out.data(), 1, (size_t)n, f) : 0;
    std::fclose(f);
    return got == (size_t)n;
}

static void usage(const char* a0) {
    std::fprintf(stderr,
                 "usage: %s <in_r2.bsp> <out_r1.bsp> [options]\n"
                 "  --limit <units>        clip render/collision bounds to +-units (default 16376)\n"
                 "  --rtl-bytes <1..4>     bytes per texel of the real-time light lump (default 4)\n"
                 "  --no-remap-contents    keep R2 unique-contents values\n"
                 "  --cubemaps <mode>      sort cubemap handling: none (default, all -1), split (R1 semantics, needs patch\n"
                 "                         materials maps/<map>/<material>_<x>_<y>_<z>), keep (R2 value 0)\n"
                 "  --physics <stub|none>  lump 0x1D: stub (default, replaces a lump without world solids) or none (keep input)\n"
                 "  --cubemap-scale <x>    multiplier for the R2 cubemap values (default 4)\n"
                 "  --worldlight-flag2     set world light flag bit 0x2 like stock R1 lights (default off, untested)\n"
                 "  --props <keep|hide-scatter|hide-all>  rule 16 diagnostics: scale props to 0.0001 for models with >= --scatter-min\n"
                 "                         instances / for every prop (default keep)\n"
                 "  --keep-prop-flag10     rule 16: copy the R2 prop flag bit 0x10 (R1: 'not drawn, shadow caster only'); default clears it\n"
                 "  --scatter-min <n>      instance count threshold of hide-scatter (default 500)\n"
                 "  --csm-rebase           rule 17: keep only the last shadow environment (CSM nodes, refs, shadow meshes), root at node 0\n"
                 "  --empty-shadow-meshes  rule 17: write lumps 0x7C..0x7F empty\n"
                 "  --no-cubemap-files     keep the R2 pakfile (do not write per-cubemap VTFs)\n"
                 "                         (the R2 pakfile only has one BC6H cubemaps.hdr.vtf)\n"
                 "  --keep-r2-lumps        keep lumps 0x04-0x07, 0x37, 0x7A in the output\n"
                 "  --grid-size <units>    rule 19: re-bin the collision grid to this cell size (default 2048, 0 keeps the R2 size 704)\n"
                 "  --no-grid-pad          rule 18: do not pad the collision grid with empty cells (default pads to +-32768 + 3 cells)\n"
                 "  --quiet                do not print the per-rule report\n",
                 a0);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        usage(argv[0]);
        return 2;
    }
    bspdown::Options opt;
    bool quiet = false;
    for (int i = 3; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--limit" && i + 1 < argc) opt.clipLimit = (float)std::atof(argv[++i]);
        else if (a == "--rtl-bytes" && i + 1 < argc) opt.rtlBytesPerTexel = std::atoi(argv[++i]);
        else if (a == "--no-remap-contents") opt.remapContents = false;
        else if (a == "--cubemaps" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "none") opt.cubemaps = bspdown::CubemapMode::None;
            else if (v == "split") opt.cubemaps = bspdown::CubemapMode::Split;
            else if (v == "keep") opt.cubemaps = bspdown::CubemapMode::Keep;
            else {
                usage(argv[0]);
                return 2;
            }
        }
        else if (a == "--physics" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "stub") opt.physics = bspdown::PhysicsMode::Stub;
            else if (v == "none") opt.physics = bspdown::PhysicsMode::None;
            else {
                usage(argv[0]);
                return 2;
            }
        }
        else if (a == "--cubemap-scale" && i + 1 < argc) opt.cubemapScale = (float)std::atof(argv[++i]);
        else if (a == "--worldlight-flag2") opt.worldLightFlagBit2 = true;
        else if (a == "--props" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "keep") opt.props = bspdown::Options::PropMode::Keep;
            else if (v == "hide-scatter") opt.props = bspdown::Options::PropMode::HideScatter;
            else if (v == "hide-all") opt.props = bspdown::Options::PropMode::HideAll;
            else {
                usage(argv[0]);
                return 2;
            }
        }
        else if (a == "--keep-prop-flag10") opt.keepPropFlag10 = true;
        else if (a == "--scatter-min" && i + 1 < argc) opt.scatterMinCount = (uint32_t)std::atoi(argv[++i]);
        else if (a == "--csm-rebase") opt.csmRebase = true;
        else if (a == "--empty-shadow-meshes") opt.emptyShadowMeshes = true;
        else if (a == "--no-cubemap-files") opt.cubemapFiles = false;
        else if (a == "--keep-r2-lumps") opt.dropR2OnlyLumps = false;
        else if (a == "--grid-size" && i + 1 < argc) opt.gridCellSize = (float)std::atof(argv[++i]);
        else if (a == "--no-grid-pad") opt.gridPad = false;
        else if (a == "--quiet") quiet = true;
        else {
            usage(argv[0]);
            return 2;
        }
    }
    std::vector<uint8_t> in;
    if (!readFile(argv[1], in)) {
        std::fprintf(stderr, "bspdown: cannot read '%s'\n", argv[1]);
        return 1;
    }
    bspdown::Report rep;
    std::vector<uint8_t> out;
    auto t0 = std::chrono::steady_clock::now();
    try {
        out = bspdown::DowngradeBsp(in.data(), in.size(), opt, &rep);
    } catch (const std::exception& e) {
        if (!quiet) std::fputs(rep.text().c_str(), stdout);
        std::fprintf(stderr, "bspdown: FAILED: %s\n", e.what());
        return 1;
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    FILE* f = std::fopen(argv[2], "wb");
    if (!f || std::fwrite(out.data(), 1, out.size(), f) != out.size()) {
        std::fprintf(stderr, "bspdown: cannot write '%s'\n", argv[2]);
        if (f) std::fclose(f);
        return 1;
    }
    std::fclose(f);
    if (!quiet) std::fputs(rep.text().c_str(), stdout);
    std::printf("bspdown: %s -> %s, %zu -> %zu bytes in %.2fs\n", argv[1], argv[2], in.size(), out.size(), secs);
    return 0;
}
