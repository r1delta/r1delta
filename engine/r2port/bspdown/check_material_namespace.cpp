// Origin: C:\Users\r3muxd\tmp\r2port\bspdown. Bundled in r1delta under GPL-3.0.
// Focused tests of rule 25 (world material namespace, Options::materialNamespace). Build instead of main.cpp, linking every other
// bspdown source (README "Build"). The full-map invariants are in check_bspdown.py (rule 25 block, --provider implies r2w/).
#include "internal.hpp"
#include <iostream>

namespace bspdown { namespace detail {
namespace {

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

struct Fixture {
    Ctx c;
    Report report;
    std::vector<std::string> names;
    explicit Fixture(const std::string& prefix) {
        c.rep = &report;
        c.opt.materialNamespace = prefix;
    }
    // names[i] is entry i of the string table; `shared` makes the last entry point at the offset of entry 0 like a deduplicated table.
    void load(bool shared = false) {
        Bytes data, tab;
        for (size_t i = 0; i < names.size(); i++) {
            uint32_t off = (uint32_t)data.size();
            if (shared && i + 1 == names.size()) off = 0;
            else { data.insert(data.end(), names[i].begin(), names[i].end()); data.push_back(0); }
            tab.resize(tab.size() + 4);
            std::memcpy(&tab[i * 4], &off, 4);
        }
        c.lump[L_TDSTRDATA] = data;
        c.lump[L_TDSTRTAB] = tab;
    }
    std::string name(size_t i) const {
        uint32_t off;
        std::memcpy(&off, &c.lump[L_TDSTRTAB][i * 4], 4);
        return std::string((const char*)&c.lump[L_TDSTRDATA][off]);
    }
};

template <class F> bool throws(F f) { try { f(); } catch (const Error&) { return true; } return false; }

void runTests() {
    int cases = 0;
    {   // R2 names are upper case with backslashes: the renamed name is lower case with forward slashes, tools/ is untouched
        Fixture f("r2w/");
        f.names = {"WORLD\\METAL\\PLATE_01", "TOOLS\\TOOLSNODRAW", "models/vehicle/imc_carrier/floating_wall_panel_garage_02", "tools/toolsskybox"};
        f.load();
        namespaceMaterials(f.c);
        require(f.name(0) == "r2w/world/metal/plate_01", "backslash name not lower-cased and prefixed");
        require(f.name(1) == "TOOLS\\TOOLSNODRAW", "tools/ name was renamed");
        require(f.name(2) == "r2w/models/vehicle/imc_carrier/floating_wall_panel_garage_02", "models/ name (the carrier collision) not prefixed");
        require(f.name(3) == "tools/toolsskybox", "lower case tools/ name was renamed");
        require(f.c.lump[L_TDSTRTAB].size() == 16, "string table size changed");
        ++cases;
    }
    {   // idempotent: a name that already carries the prefix is not prefixed twice
        Fixture f("r2w/");
        f.names = {"r2w/world/a", "R2W\\WORLD\\B"};
        f.load();
        namespaceMaterials(f.c);
        require(f.name(0) == "r2w/world/a", "already namespaced name was prefixed again");
        require(f.name(1) == "R2W\\WORLD\\B", "already namespaced (case folded) name was rewritten");
        ++cases;
    }
    {   // two table entries sharing one string stay valid, each gets its own renamed copy
        Fixture f("r2w/");
        f.names = {"WORLD\\A", "WORLD\\B", "x"};
        f.load(true);
        namespaceMaterials(f.c);
        require(f.name(0) == "r2w/world/a" && f.name(2) == "r2w/world/a" && f.name(1) == "r2w/world/b", "shared string entry broken");
        ++cases;
    }
    {   // off by default
        Fixture f("");
        f.names = {"WORLD\\A"};
        f.load();
        const Bytes before = f.c.lump[L_TDSTRDATA];
        namespaceMaterials(f.c);
        require(f.c.lump[L_TDSTRDATA] == before && f.report.lines.empty(), "empty namespace changed the lumps");
        ++cases;
    }
    {   // no renamed name can equal an R1 file name: every one starts with the prefix, and no R2 name does
        Fixture f("r2w/");
        f.names = {"WORLD\\METAL\\A", "MODELS\\WORLD\\METAL\\BLDG_WALL_METAL_IMC_01", "DEBUG\\DEBUGEMPTY"};
        f.load();
        namespaceMaterials(f.c);
        for (size_t i = 0; i < f.names.size(); i++)
            require(f.name(i).compare(0, 4, "r2w/") == 0, "world material outside the namespace");
        ++cases;
    }
    {   // bad prefixes and a corrupt table fail loudly
        for (const char* bad : {"r2w", "R2W/", "r2w\\", "../"}) {
            Fixture f(bad);
            f.names = {"WORLD\\A"};
            f.load();
            require(throws([&] { namespaceMaterials(f.c); }), "invalid namespace accepted");
        }
        Fixture f("r2w/");
        f.names = {"WORLD\\A"};
        f.load();
        const uint32_t past = 4096;
        std::memcpy(&f.c.lump[L_TDSTRTAB][0], &past, 4);
        require(throws([&] { namespaceMaterials(f.c); }), "string table entry past the string data accepted");
        ++cases;
    }
    std::cout << "rule 25 fixtures: " << cases << " cases passed\n";
}

}  // namespace
}}  // namespace bspdown::detail

int main() {
    try { bspdown::detail::runTests(); return 0; }
    catch (const std::exception& e) { std::cerr << "rule 25 fixture failure: " << e.what() << '\n'; return 1; }
}
