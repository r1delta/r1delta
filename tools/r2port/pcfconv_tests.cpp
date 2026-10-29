// Tests of the R2 -> R1 particle converter (pcfconv.cpp) on small synthetic PCF files.
// Build: cl /nologo /std:c++17 /EHsc /W4 /WX /permissive- /I<dir with pcfconv.hpp, dmx.hpp> pcfconv_tests.cpp dmx.cpp pcfconv.cpp
#include "dmx.hpp"
#include "pcfconv.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Check(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

dmx::Attribute StringAttr(const char* name, const std::string& text)
{
    dmx::Attribute a;
    a.name = name;
    a.type = dmx::kString;
    a.values.resize(1);
    a.values[0].s = text;
    return a;
}

dmx::Attribute FloatAttr(const char* name, float value)
{
    dmx::Attribute a;
    a.name = name;
    a.type = dmx::kFloat;
    a.values.resize(1);
    a.values[0].f[0] = value;
    return a;
}

dmx::Attribute BoolAttr(const char* name, bool value)
{
    dmx::Attribute a;
    a.name = name;
    a.type = dmx::kBool;
    a.values.resize(1);
    a.values[0].i = value ? 1 : 0;
    return a;
}

dmx::Attribute ColorAttr(const char* name, int r, int g, int b)
{
    dmx::Attribute a;
    a.name = name;
    a.type = dmx::kColor;
    a.values.resize(1);
    a.values[0].c[0] = static_cast<std::uint8_t>(r);
    a.values[0].c[1] = static_cast<std::uint8_t>(g);
    a.values[0].c[2] = static_cast<std::uint8_t>(b);
    a.values[0].c[3] = 255;
    return a;
}

struct Function {
    std::string list;      // "renderers", "initializers", ...
    std::string function;  // functionName
    bool mute = false;
    std::vector<dmx::Attribute> extra;
};

struct SystemSpec {
    std::string name;
    bool hasColor = true;
    int r = 255, g = 124, b = 59;
    bool hasScale = true;
    float scale = 3.0f;
    std::vector<Function> functions;
};

void SetGuid(dmx::Element& e, std::uint8_t seed)
{
    for (int i = 0; i < 16; ++i) e.guid[i] = static_cast<std::uint8_t>(seed * 16 + i + 1);
}

// One R2 PCF holding the given systems (no children).
std::vector<std::uint8_t> BuildPcf(const std::vector<SystemSpec>& systems)
{
    dmx::Document doc;
    doc.elements.emplace_back();
    doc.elements[0].type = "DmElement";
    doc.elements[0].name = "untitled";
    SetGuid(doc.elements[0], 1);
    dmx::Attribute defs;
    defs.name = "particleSystemDefinitions";
    defs.type = dmx::kElementArray;
    std::uint8_t seed = 2;
    for (const SystemSpec& spec : systems) {
        const int systemIndex = static_cast<int>(doc.elements.size());
        doc.elements.emplace_back();
        dmx::Value ref;
        ref.i = systemIndex;
        defs.values.push_back(ref);

        std::vector<dmx::Attribute> attrs;
        attrs.push_back(StringAttr("material", "particle\\refract\\pinched_ring_distort.vmt"));
        if (spec.hasColor) attrs.push_back(ColorAttr("color", spec.r, spec.g, spec.b));
        if (spec.hasScale) attrs.push_back(FloatAttr("color HDR scale", spec.scale));
        for (const char* list : { "renderers", "operators", "initializers", "emitters", "forces", "constraints" }) {
            dmx::Attribute fns;
            fns.name = list;
            fns.type = dmx::kElementArray;
            for (const Function& f : spec.functions) {
                if (f.list != list) continue;
                dmx::Element fe;
                fe.type = "DmeParticleOperator";
                fe.name = f.function;
                SetGuid(fe, seed++);
                fe.attrs.push_back(StringAttr("functionName", f.function));
                if (f.mute) fe.attrs.push_back(BoolAttr("mute", true));
                for (const dmx::Attribute& extra : f.extra) fe.attrs.push_back(extra);
                dmx::Value fref;
                fref.i = static_cast<int>(doc.elements.size());
                fns.values.push_back(fref);
                doc.elements.push_back(std::move(fe));
            }
            attrs.push_back(std::move(fns));
        }
        dmx::Element& system = doc.elements[static_cast<std::size_t>(systemIndex)];
        system.type = "DmeParticleSystemDefinition";
        system.name = spec.name;
        SetGuid(system, seed++);
        system.attrs = std::move(attrs);
    }
    doc.elements[0].attrs.push_back(std::move(defs));
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!dmx::Write(doc, bytes, error)) {
        std::printf("FAIL: test PCF did not serialize: %s\n", error.c_str());
        ++failures;
    }
    return bytes;
}

struct Converted {
    dmx::Document doc;
    r2particles::Report report;
    bool ok = false;

    const dmx::Element* System(const std::string& target) const
    {
        const dmx::Attribute* defs = doc.elements[0].Find("particleSystemDefinitions");
        if (!defs) return nullptr;
        for (const dmx::Value& v : defs->values)
            if (v.i >= 0 && doc.elements[static_cast<std::size_t>(v.i)].name == target) return &doc.elements[static_cast<std::size_t>(v.i)];
        return nullptr;
    }
    // 'color HDR scale' of the converted system, or -1 when the attribute is absent.
    float Scale(const std::string& system) const
    {
        const dmx::Element* e = System("r2p_" + system);
        const dmx::Attribute* a = e ? e->Find("color HDR scale") : nullptr;
        return a && a->type == dmx::kFloat && a->values.size() == 1 ? a->values[0].f[0] : -1.0f;
    }
    bool Clamped(const std::string& system) const
    {
        for (const r2particles::SystemStatus& s : report.status)
            if (s.target == "r2p_" + system)
                for (const std::string& note : s.approximations)
                    if (note == "system:color HDR scale clamped for Render models") return true;
        return false;
    }
};

Converted Convert(const std::vector<SystemSpec>& systems)
{
    Converted out;
    r2particles::SourcePcf source;
    source.name = "test.pcf";
    source.bytes = BuildPcf(systems);
    std::vector<r2particles::Request> requests;
    for (const SystemSpec& s : systems) requests.push_back({ s.name, "test.pcf" });
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!r2particles::ConvertPcf({ source }, requests, r2particles::Options{}, bytes, out.report, error)) {
        std::printf("FAIL: conversion failed: %s\n", error.c_str());
        ++failures;
        return out;
    }
    out.ok = dmx::Parse(bytes.data(), bytes.size(), out.doc, error);
    if (!out.ok) {
        std::printf("FAIL: converted PCF does not parse: %s\n", error.c_str());
        ++failures;
    }
    return out;
}

SystemSpec Models(const std::string& name, int r, int g, int b, float scale)
{
    SystemSpec s;
    s.name = name;
    s.r = r; s.g = g; s.b = b;
    s.scale = scale;
    s.functions.push_back({ "renderers", "Render models", false, { StringAttr("sequence 0 model", "fx\\fire_sm_ground_fx.mdl") } });
    return s;
}

} // namespace

int main()
{
    const auto near = [](float a, float b) { return std::fabs(a - b) < 0.0015f; };

    // The drydock fire floor: colour 255 124 59 x scale 3 wrapped to violet in R1's model renderer; the scale must come down to 1.
    SystemSpec fire = Models("fire_grnd_mdl", 255, 124, 59, 3.0f);
    // A model system whose scaled colour still fits in a byte keeps its scale (80 x 3.1875 = 255).
    SystemSpec fits = Models("fits", 80, 40, 20, 3.0f);
    // Scale limited to the brightest channel: 100 -> 2.55.
    SystemSpec partial = Models("partial", 100, 50, 25, 4.0f);
    // The same colour and scale on a sprite system is left to R1's sprite renderer, which handles the scale.
    SystemSpec sprite;
    sprite.name = "sprite";
    sprite.functions.push_back({ "renderers", "render_animated_sprites", false, {} });
    // No system colour: R1's default is white, so any scale above 1 would wrap.
    SystemSpec noColor = Models("no_color", 0, 0, 0, 2.0f);
    noColor.hasColor = false;
    // Scale at or below 1 is never touched.
    SystemSpec dim = Models("dim", 255, 255, 255, 0.5f);
    SystemSpec one = Models("one", 255, 255, 255, 1.0f);
    // No scale attribute: nothing to clamp and none added.
    SystemSpec noScale = Models("no_scale", 255, 124, 59, 3.0f);
    noScale.hasScale = false;
    // Colours the particles can still receive count: an unmuted Color Random can hand out 255 200 100.
    SystemSpec liveInit = Models("live_init", 10, 10, 10, 3.0f);
    liveInit.functions.push_back({ "initializers", "Color Random", false, { ColorAttr("color1", 255, 200, 100), ColorAttr("color2", 200, 100, 50) } });
    SystemSpec mutedInit = Models("muted_init", 10, 10, 10, 3.0f);
    mutedInit.functions.push_back({ "initializers", "Color Random", true, { ColorAttr("color1", 255, 200, 100), ColorAttr("color2", 200, 100, 50) } });
    // A second renderer next to the model one does not hide it.
    SystemSpec mixed = Models("mixed", 255, 124, 59, 3.0f);
    mixed.functions.push_back({ "renderers", "render_animated_sprites", false, {} });

    const Converted c = Convert({ fire, fits, partial, sprite, noColor, dim, one, noScale, liveInit, mutedInit, mixed });
    if (!c.ok) return 1;

    Check(c.Scale("fire_grnd_mdl") == 1.0f, "fire floor model: scale 3 on colour 255 124 59 is lowered to 1");
    Check(c.Clamped("fire_grnd_mdl"), "fire floor model: the clamp is listed in the system status");
    {
        const dmx::Element* e = c.System("r2p_fire_grnd_mdl");
        const dmx::Attribute* color = e ? e->Find("color") : nullptr;
        Check(color && color->values[0].c[0] == 255 && color->values[0].c[1] == 124 && color->values[0].c[2] == 59,
            "fire floor model: the system colour is untouched");
    }
    Check(c.Scale("fits") == 3.0f && !c.Clamped("fits"), "scaled colour that fits in a byte keeps its scale");
    Check(near(c.Scale("partial"), 2.55f) && c.Clamped("partial"), "scale is limited to 255 / brightest channel");
    Check(c.Scale("sprite") == 3.0f && !c.Clamped("sprite"), "sprite system keeps its scale");
    Check(c.Scale("no_color") == 1.0f, "no system colour: white default limits the scale to 1");
    Check(c.Scale("dim") == 0.5f && !c.Clamped("dim"), "scale below 1 is untouched");
    Check(c.Scale("one") == 1.0f && !c.Clamped("one"), "scale 1 is untouched");
    Check(c.Scale("no_scale") < 0.0f, "no scale attribute is not invented");
    Check(c.Scale("live_init") == 1.0f, "unmuted Color Random colour 255 200 100 limits the scale");
    Check(c.Scale("muted_init") == 3.0f, "a muted initializer does not limit the scale");
    Check(c.Scale("mixed") == 1.0f, "model renderer next to a sprite renderer still counts");

    bool noted = false;
    for (const std::string& n : c.report.notes)
        if (n.find("r2p_fire_grnd_mdl") != std::string::npos && n.find("color HDR scale") != std::string::npos) noted = true;
    Check(noted, "the clamp is reported in the notes");

    if (failures == 0) std::printf("pcfconv tests passed\n");
    return failures == 0 ? 0 : 1;
}
