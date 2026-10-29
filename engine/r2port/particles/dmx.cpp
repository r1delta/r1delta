// Origin: C:\Users\r3muxd\tmp\r2port\particles. R2 particle (PCF) converter bundled in r1delta under GPL-3.0.
#include "dmx.hpp"

#include <cstring>
#include <unordered_map>

namespace dmx {
namespace {

constexpr char kHeader[] = "<!-- dmx encoding binary 5 format pcf 2 -->\n";  // followed by a NUL

struct Reader {
    const std::uint8_t* p;
    std::size_t n;
    std::size_t pos = 0;
    bool ok = true;
    bool Need(std::size_t count) { if (!ok || count > n - pos) ok = false; return ok; }
    std::int32_t I32() { std::int32_t v = 0; if (Need(4)) { std::memcpy(&v, p + pos, 4); pos += 4; } return v; }
    float F32() { float v = 0; if (Need(4)) { std::memcpy(&v, p + pos, 4); pos += 4; } return v; }
    std::uint8_t U8() { std::uint8_t v = 0; if (Need(1)) v = p[pos++]; return v; }
    std::string CStr() {
        std::string s;
        if (!ok) return s;
        const void* nul = std::memchr(p + pos, 0, n - pos);
        if (!nul) { ok = false; return s; }
        const std::size_t len = static_cast<const std::uint8_t*>(nul) - (p + pos);
        s.assign(reinterpret_cast<const char*>(p + pos), len);
        pos += len + 1;
        return s;
    }
};

int TupleSize(int valueType)
{
    switch (valueType) {
    case kVector2: return 2;
    case kVector3: case kQAngle: return 3;
    case kVector4: case kQuaternion: return 4;
    case kVMatrix: return 16;
    default: return 0;
    }
}

void ReadScalar(Reader& r, int type, const std::vector<std::string>& syms, Value& v)
{
    switch (type) {
    case kElement:
        v.i = r.I32();
        if (v.i == -2) { v.external = true; v.s = r.CStr(); }
        break;
    case kInt: case kTime: v.i = r.I32(); break;
    case kFloat: v.f[0] = r.F32(); break;
    case kBool: v.i = r.U8(); break;
    case kString: {
        const std::int32_t idx = r.I32();
        if (idx < 0 || static_cast<std::size_t>(idx) >= syms.size()) { r.ok = false; break; }
        v.s = syms[idx];
        break;
    }
    case kVoid: {
        const std::int32_t len = r.I32();
        if (len < 0 || !r.Need(static_cast<std::size_t>(len))) { r.ok = false; break; }
        v.blob.assign(r.p + r.pos, r.p + r.pos + len);
        r.pos += len;
        break;
    }
    case kColor:
        for (int k = 0; k < 4; ++k) v.c[k] = r.U8();
        break;
    default: {
        const int count = TupleSize(type);
        if (!count) { r.ok = false; break; }
        for (int k = 0; k < count; ++k) v.f[k] = r.F32();
    }
    }
}

struct Writer {
    std::vector<std::uint8_t>& out;
    explicit Writer(std::vector<std::uint8_t>& o) : out(o) {}
    void Raw(const void* d, std::size_t len) { const auto* b = static_cast<const std::uint8_t*>(d); out.insert(out.end(), b, b + len); }
    void I32(std::int32_t v) { Raw(&v, 4); }
    void F32(float v) { Raw(&v, 4); }
    void U8(std::uint8_t v) { out.push_back(v); }
    void CStr(const std::string& s) { Raw(s.data(), s.size()); out.push_back(0); }
};

struct SymbolTable {
    std::vector<std::string> list;
    std::unordered_map<std::string, int> index;
    int Add(const std::string& s)
    {
        const auto it = index.find(s);
        if (it != index.end()) return it->second;
        const int id = static_cast<int>(list.size());
        index.emplace(s, id);
        list.push_back(s);
        return id;
    }
};

void WriteScalar(Writer& w, int type, const Value& v, SymbolTable& syms)
{
    switch (type) {
    case kElement:
        if (v.external) { w.I32(-2); w.CStr(v.s); } else w.I32(v.i);
        break;
    case kInt: case kTime: w.I32(v.i); break;
    case kFloat: w.F32(v.f[0]); break;
    case kBool: w.U8(static_cast<std::uint8_t>(v.i)); break;
    case kString: w.I32(syms.Add(v.s)); break;
    case kVoid: w.I32(static_cast<std::int32_t>(v.blob.size())); w.Raw(v.blob.data(), v.blob.size()); break;
    case kColor: w.Raw(v.c, 4); break;
    default: for (int k = 0; k < TupleSize(type); ++k) w.F32(v.f[k]);
    }
}

} // namespace

Attribute* Element::Find(const std::string& attrName)
{
    for (auto& a : attrs) if (a.name == attrName) return &a;
    return nullptr;
}

const Attribute* Element::Find(const std::string& attrName) const
{
    for (const auto& a : attrs) if (a.name == attrName) return &a;
    return nullptr;
}

bool Parse(const std::uint8_t* data, std::size_t size, Document& doc, std::string& error)
{
    doc.elements.clear();
    constexpr std::size_t headerLen = sizeof(kHeader);  // includes the NUL
    if (size < headerLen || std::memcmp(data, kHeader, headerLen) != 0) { error = "not a DMX binary 5 pcf 2 file"; return false; }
    Reader r{ data, size, headerLen };
    const std::int32_t symbolCount = r.I32();
    if (symbolCount < 0 || static_cast<std::size_t>(symbolCount) > size) { error = "bad symbol count"; return false; }
    std::vector<std::string> syms;
    syms.reserve(symbolCount);
    for (int k = 0; k < symbolCount && r.ok; ++k) syms.push_back(r.CStr());
    const std::int32_t elementCount = r.I32();
    if (!r.ok || elementCount <= 0 || static_cast<std::size_t>(elementCount) > size) { error = "bad element count"; return false; }
    doc.elements.resize(elementCount);
    for (auto& e : doc.elements) {
        const std::int32_t t = r.I32(), nm = r.I32();
        if (!r.Need(16)) break;
        std::memcpy(e.guid, r.p + r.pos, 16);
        r.pos += 16;
        if (t < 0 || static_cast<std::size_t>(t) >= syms.size() || nm < -1 || nm >= symbolCount) { r.ok = false; break; }
        e.type = syms[t];
        if (nm >= 0) e.name = syms[nm];
    }
    for (auto& e : doc.elements) {
        if (!r.ok) break;
        const std::int32_t attrCount = r.I32();
        if (attrCount < 0 || static_cast<std::size_t>(attrCount) > size) { r.ok = false; break; }
        e.attrs.resize(attrCount);
        for (auto& a : e.attrs) {
            const std::int32_t nameIdx = r.I32();
            a.type = r.U8();
            if (!r.ok || nameIdx < 0 || nameIdx >= symbolCount || a.type < 1 || a.type > kMaxType) { r.ok = false; break; }
            a.name = syms[nameIdx];
            if (a.type <= kArrayBase) {
                a.values.resize(1);
                ReadScalar(r, a.type, syms, a.values[0]);
            } else {
                const std::int32_t count = r.I32();
                if (count < 0 || static_cast<std::size_t>(count) > size) { r.ok = false; break; }
                const int valueType = a.type - kArrayBase;
                a.values.resize(count);
                for (auto& v : a.values) {
                    if (valueType == kString) v.s = r.CStr();
                    else ReadScalar(r, valueType, syms, v);
                    if (!r.ok) break;
                }
            }
            if (!r.ok) break;
            const int valueType = a.type <= kArrayBase ? a.type : a.type - kArrayBase;
            if (valueType == kElement)
                for (const auto& v : a.values)
                    if (!v.external && (v.i < -1 || v.i >= elementCount)) { r.ok = false; break; }
        }
    }
    if (!r.ok) { error = "truncated or malformed DMX data"; return false; }
    if (r.pos != size) { error = "trailing bytes after the last attribute"; return false; }
    return true;
}

bool Write(const Document& doc, std::vector<std::uint8_t>& out, std::string& error)
{
    out.clear();
    if (doc.elements.empty()) { error = "empty document"; return false; }
    SymbolTable syms;
    std::vector<std::uint8_t> dict, body;
    Writer wd(dict), wb(body);
    for (const auto& e : doc.elements) {
        wd.I32(syms.Add(e.type));
        wd.I32(syms.Add(e.name));
        wd.Raw(e.guid, 16);
    }
    for (const auto& e : doc.elements) {
        wb.I32(static_cast<std::int32_t>(e.attrs.size()));
        for (const auto& a : e.attrs) {
            wb.I32(syms.Add(a.name));
            wb.U8(a.type);
            if (a.type <= kArrayBase) {
                if (a.values.size() != 1) { error = "scalar attribute without one value: " + a.name; return false; }
                WriteScalar(wb, a.type, a.values[0], syms);
            } else {
                wb.I32(static_cast<std::int32_t>(a.values.size()));
                const int valueType = a.type - kArrayBase;
                for (const auto& v : a.values) {
                    if (valueType == kString) wb.CStr(v.s);
                    else WriteScalar(wb, valueType, v, syms);
                }
            }
        }
    }
    Writer w(out);
    w.Raw(kHeader, sizeof(kHeader));
    w.I32(static_cast<std::int32_t>(syms.list.size()));
    for (const auto& s : syms.list) w.CStr(s);
    w.I32(static_cast<std::int32_t>(doc.elements.size()));
    out.insert(out.end(), dict.begin(), dict.end());
    out.insert(out.end(), body.begin(), body.end());
    return true;
}

} // namespace dmx
