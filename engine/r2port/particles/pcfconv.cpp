// Origin: C:\Users\r3muxd\tmp\r2port\particles. R2 particle (PCF) converter bundled in r1delta under GPL-3.0.
#include "pcfconv.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "dmx.hpp"

namespace r2particles {
namespace {

#include "pcfconv_tables.inc"

constexpr const char* kFunctionLists[] = { "renderers", "operators", "initializers", "emitters", "forces", "constraints" };

// Attributes the R1 client has no string for (checked against client.dll): R2-only features.
// Dropped from function elements; each one is a visual or behavioural delta listed in DECISIONS.md.
constexpr const char* kR2OnlyFunctionAttributes[] = {
    "alpha erosion hardness", "fade in dist", "fade out dist", "flip texture coord u", "flip texture coord v",
    "from end of list", "init creation time with current time", "minimum count", "orientation_2_axis",
    "post-multiply alpha with alpha alternate", "scale texture by particle distance", "scroll speed",
    "tint clamp max", "tint clamp min", "twist overlap workaround", "twist squish", "update lighting while moving",
};

// System-level attributes R1 does not have.
constexpr const char* kR2OnlySystemAttributes[] = {
    "scripts", "scripted", "kill if over-limit", "warm-up time", "warm-up and pause", "phase shift - allowed",
    "freeze simulation after time",
};

std::string Lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

bool InList(const char* const* list, std::size_t count, const std::string& lowerName)
{
    for (std::size_t i = 0; i < count; ++i) if (lowerName == list[i]) return true;
    return false;
}
#define IN_LIST(array, name) InList(array, sizeof(array) / sizeof(array[0]), name)

bool Supported(const std::string& list, const std::string& lowerFunction)
{
    for (const auto& f : kSupportedFunctions)
        if (list == f.list && lowerFunction == f.name) return true;
    return false;
}

const std::string* StringValue(const dmx::Element& e, const char* attr)
{
    const dmx::Attribute* a = e.Find(attr);
    return a && a->type == dmx::kString && a->values.size() == 1 ? &a->values[0].s : nullptr;
}

// Deterministic replacement GUID: two FNV-1a 64 lanes over the source GUID and a salt, so a
// converted system never shares an id with the R1 or R2 definition it came from.
void NewGuid(const std::uint8_t* source, const std::string& salt, std::uint8_t* out)
{
    std::uint64_t h1 = 0xcbf29ce484222325ull, h2 = 0x84222325cbf29ce4ull;
    const auto mix = [&](std::uint8_t b) {
        h1 = (h1 ^ b) * 0x100000001b3ull;
        h2 = (h2 ^ static_cast<std::uint8_t>(b + 0x5b)) * 0x100000001b3ull;
        h2 ^= h2 >> 29;
    };
    for (char c : salt) mix(static_cast<std::uint8_t>(c));
    for (int i = 0; i < 16; ++i) mix(source[i]);
    std::memcpy(out, &h1, 8);
    std::memcpy(out + 8, &h2, 8);
}

struct SourceDoc {
    std::string file;
    dmx::Document doc;
    std::unordered_map<std::string, int> systemByName;  // lower-case name -> element index
};

dmx::Value ElementRef(int index)
{
    dmx::Value v;
    v.i = index;
    return v;
}

dmx::Value StringVal(const std::string& s)
{
    dmx::Value v;
    v.s = s;
    return v;
}

class Converter {
public:
    Converter(const Options& options, Report& report) : m_options(options), m_report(report)
    {
        dmx::Element root;
        root.type = "DmElement";
        root.name = "untitled";
        NewGuid(reinterpret_cast<const std::uint8_t*>("r2port-particles-root"), "root", root.guid);
        m_out.elements.push_back(std::move(root));
    }

    // Returns the output element index, or -1.
    int CopySystem(const SourceDoc& src, int index)
    {
        const dmx::Element& e = src.doc.elements[index];
        const std::string key = Lower(e.name);
        const auto done = m_byTarget.find(key);
        if (done != m_byTarget.end()) return done->second;

        const int outIndex = static_cast<int>(m_out.elements.size());
        m_out.elements.emplace_back();
        m_byTarget.emplace(key, outIndex);
        m_names.emplace(key, outIndex);

        dmx::Element copy;
        copy.type = e.type;
        copy.name = TargetName(m_options, e.name);
        NewGuid(e.guid, "system", copy.guid);
        m_report.systems.push_back(copy.name);
        m_report.status.emplace_back();
        m_report.status.back().target = copy.name;
        m_stack.push_back(m_report.status.size() - 1);

        for (const dmx::Attribute& a : e.attrs) {
            if (IN_LIST(kR2OnlySystemAttributes, a.name)) {
                ++m_report.droppedAttributes;
                Status().droppedAttributes.push_back("system:" + a.name);
                continue;
            }
            if (a.name == "children" && a.type == dmx::kElementArray) {
                copy.attrs.push_back(ConvertChildren(src, e, a));
            } else if (IsFunctionList(a.name) && a.type == dmx::kElementArray) {
                copy.attrs.push_back(ConvertFunctionList(src, e, a));
            } else if ((a.name == "cull_replacement_definition" || a.name == "fallback replacement definition")
                && a.type == dmx::kString) {
                copy.attrs.push_back(ConvertReplacement(src, a));
            } else {
                if (a.name == "material" && a.type == dmx::kString && !a.values[0].s.empty())
                    m_report.materials.insert(a.values[0].s);
                copy.attrs.push_back(a);
            }
        }
        m_stack.pop_back();
        m_out.elements[outIndex] = std::move(copy);
        return outIndex;
    }

    bool Finish(std::vector<std::uint8_t>& bytes, std::string& error)
    {
        dmx::Attribute list;
        list.name = "particleSystemDefinitions";
        list.type = dmx::kElementArray;
        for (const auto& kv : m_names) list.values.push_back(ElementRef(kv.second));  // sorted by lower-case target name
        m_out.elements[0].attrs.push_back(std::move(list));
        std::sort(m_report.systems.begin(), m_report.systems.end());
        std::sort(m_report.status.begin(), m_report.status.end(),
            [](const SystemStatus& a, const SystemStatus& b) { return a.target < b.target; });
        return dmx::Write(m_out, bytes, error);
    }

    void Note(const std::string& text) { m_report.notes.push_back(text); }

private:
    SystemStatus& Status() { return m_report.status[m_stack.back()]; }

    static bool IsFunctionList(const std::string& name)
    {
        for (const char* l : kFunctionLists) if (name == l) return true;
        return false;
    }

    dmx::Attribute ConvertChildren(const SourceDoc& src, const dmx::Element& system, const dmx::Attribute& a)
    {
        dmx::Attribute out;
        out.name = a.name;
        out.type = dmx::kElementArray;
        for (const dmx::Value& ref : a.values) {
            if (ref.external || ref.i < 0) continue;
            const dmx::Element& childLink = src.doc.elements[ref.i];
            dmx::Element link = childLink;
            NewGuid(childLink.guid, "child", link.guid);
            for (dmx::Attribute& la : link.attrs) {
                if (la.name != "child" || la.type != dmx::kElement || la.values[0].external || la.values[0].i < 0) continue;
                const dmx::Element& target = src.doc.elements[la.values[0].i];
                const int childOut = CopySystem(src, la.values[0].i);
                la.values[0] = ElementRef(childOut);
                link.name = TargetName(m_options, target.name);
            }
            out.values.push_back(ElementRef(static_cast<int>(m_out.elements.size())));
            m_out.elements.push_back(std::move(link));
        }
        (void)system;
        return out;
    }

    dmx::Attribute ConvertReplacement(const SourceDoc& src, const dmx::Attribute& a)
    {
        dmx::Attribute out = a;
        const std::string& name = a.values[0].s;
        if (name.empty()) return out;
        const auto it = src.systemByName.find(Lower(name));
        if (it == src.systemByName.end()) {
            out.values[0].s.clear();
            Note("replacement system '" + name + "' not found in " + src.file + ", cleared");
            return out;
        }
        CopySystem(src, it->second);
        out.values[0].s = TargetName(m_options, name);
        return out;
    }

    dmx::Attribute ConvertFunctionList(const SourceDoc& src, const dmx::Element& system, const dmx::Attribute& a)
    {
        dmx::Attribute out;
        out.name = a.name;
        out.type = dmx::kElementArray;
        // Does the system already have an emit_instantaneously emitter?
        bool hasInstant = false;
        if (a.name == "emitters") {
            for (const dmx::Value& ref : a.values) {
                if (ref.external || ref.i < 0) continue;
                const std::string* f = StringValue(src.doc.elements[ref.i], "functionName");
                if (f && Lower(*f) == "emit_instantaneously") hasInstant = true;
            }
        }
        for (const dmx::Value& ref : a.values) {
            if (ref.external || ref.i < 0) continue;
            dmx::Element op = src.doc.elements[ref.i];
            const std::string* fname = StringValue(op, "functionName");
            if (!fname) { Note(system.name + ": " + a.name + " element without functionName dropped"); ++m_report.droppedFunctions; Status().droppedFunctions.push_back(a.name + ":<unnamed>"); continue; }
            const std::string function = *fname;
            const std::string lower = Lower(function);
            if (!Supported(a.name, lower)) {
                if (a.name == "emitters" && lower == "emit_instantaneously by distance") {
                    if (hasInstant) {
                        Note(system.name + ": emitter 'emit_instantaneously by distance' dropped (R1 has no emit-by-distance; the system's own emit_instantaneously stays)");
                        ++m_report.droppedFunctions;
                        Status().droppedFunctions.push_back("emitters:emit_instantaneously by distance");
                        continue;
                    }
                    // Nearest R1 emitter: a fixed burst. The distance spacing cannot be reproduced.
                    std::int32_t count = 8;
                    if (const dmx::Attribute* m = op.Find("minimum count")) if (m->type == dmx::kInt && m->values.size() == 1 && m->values[0].i > 0) count = m->values[0].i;
                    dmx::Element burst;
                    burst.type = op.type;
                    burst.name = "emit_instantaneously";
                    NewGuid(op.guid, "operator", burst.guid);
                    dmx::Attribute fn; fn.name = "functionName"; fn.type = dmx::kString; fn.values.push_back(StringVal("emit_instantaneously"));
                    burst.attrs.push_back(fn);
                    dmx::Attribute num; num.name = "num_to_emit"; num.type = dmx::kInt; num.values.resize(1); num.values[0].i = count;
                    burst.attrs.push_back(num);
                    for (const dmx::Attribute& oa : op.attrs)
                        if (oa.name == "emission_start_time" || oa.name == "maximum emission per frame" || oa.name == "mute")
                            burst.attrs.push_back(oa);
                    Note(system.name + ": emitter 'emit_instantaneously by distance' replaced by emit_instantaneously num_to_emit=" + std::to_string(count) + " (spacing lost)");
                    ++m_report.approximatedFunctions;
                    Status().approximations.push_back("emitters:emit_instantaneously by distance -> emit_instantaneously");
                    out.values.push_back(ElementRef(static_cast<int>(m_out.elements.size())));
                    m_out.elements.push_back(std::move(burst));
                    continue;
                }
                Note(system.name + ": " + a.name + " '" + function + "' dropped (no R1 equivalent)");
                ++m_report.droppedFunctions;
                Status().droppedFunctions.push_back(a.name + ":" + function);
                continue;
            }
            NewGuid(op.guid, "operator", op.guid);
            std::vector<dmx::Attribute> kept;
            for (dmx::Attribute& oa : op.attrs) {
                if (IN_LIST(kR2OnlyFunctionAttributes, oa.name)) {
                    ++m_report.droppedAttributes;
                    Status().droppedAttributes.push_back(function + ":" + oa.name);
                    continue;
                }
                kept.push_back(std::move(oa));
            }
            op.attrs = std::move(kept);
            if (a.name == "renderers" && lower == "render models") {
                if (const std::string* model = StringValue(op, "sequence 0 model")) if (!model->empty()) m_report.models.insert(*model);
            }
            if (a.name == "renderers") ++Status().renderers;
            out.values.push_back(ElementRef(static_cast<int>(m_out.elements.size())));
            m_out.elements.push_back(std::move(op));
        }
        return out;
    }

    const Options& m_options;
    Report& m_report;
    dmx::Document m_out;
    std::unordered_map<std::string, int> m_byTarget;  // lower-case source name -> output index
    std::map<std::string, int> m_names;               // same, ordered for the root list
    std::vector<std::size_t> m_stack;                 // indices into Report::status of the systems being copied
};

} // namespace

std::string TargetName(const Options& options, std::string_view system)
{
    return options.prefix + std::string(system);
}

bool ConvertPcf(const std::vector<SourcePcf>& sources, const std::vector<Request>& requests,
    const Options& options, std::vector<std::uint8_t>& outPcf, Report& report, std::string& error)
{
    report = Report{};
    std::unordered_map<std::string, std::unique_ptr<SourceDoc>> docs;
    Converter converter(options, report);
    // Requests grouped per file so a file is parsed once.
    std::map<std::string, std::vector<const Request*>> byFile;
    for (const Request& r : requests) byFile[Lower(r.file)].push_back(&r);
    for (const auto& group : byFile) {
        const SourcePcf* source = nullptr;
        for (const SourcePcf& s : sources) if (Lower(s.name) == group.first) { source = &s; break; }
        if (!source) {
            for (const Request* r : group.second) { ++report.missingSystems; converter.Note(r->system + ": source file " + r->file + " not supplied"); }
            continue;
        }
        auto sd = std::make_unique<SourceDoc>();
        sd->file = source->name;
        std::string parseError;
        if (!dmx::Parse(source->bytes.data(), source->bytes.size(), sd->doc, parseError)) {
            error = source->name + ": " + parseError;
            return false;
        }
        const dmx::Attribute* defs = sd->doc.elements[0].Find("particleSystemDefinitions");
        if (!defs || defs->type != dmx::kElementArray) { error = source->name + ": no particleSystemDefinitions"; return false; }
        for (const dmx::Value& v : defs->values)
            if (!v.external && v.i >= 0) sd->systemByName.emplace(Lower(sd->doc.elements[v.i].name), v.i);
        for (const Request* r : group.second) {
            const auto it = sd->systemByName.find(Lower(r->system));
            if (it == sd->systemByName.end()) { ++report.missingSystems; converter.Note(r->system + ": not found in " + r->file); continue; }
            converter.CopySystem(*sd, it->second);
        }
        docs.emplace(group.first, std::move(sd));
    }
    return converter.Finish(outPcf, error);
}

} // namespace r2particles
