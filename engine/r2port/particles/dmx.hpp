// Origin: C:\Users\r3muxd\tmp\r2port\particles. R2 particle (PCF) converter bundled in r1delta under GPL-3.0.
// Minimal reader/writer for Source DMX binary 5 / pcf 2, the container both Titanfall (R1) and
// Titanfall 2 (R2) particle files use. Header, symbol table, element dictionary and attribute
// layouts follow the retail files; see README.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dmx {

enum Type : std::uint8_t {
    kElement = 1, kInt, kFloat, kBool, kString, kVoid, kTime, kColor,
    kVector2, kVector3, kVector4, kQAngle, kQuaternion, kVMatrix,
    kArrayBase = 14,  // array type id = value type id + 14
    kElementArray = 15,
    kMaxType = 28,
};

struct Value {
    std::int32_t i = 0;             // int, time, bool, element index (-1 null), external flag in ext
    float f[16] = {};               // float and tuple types
    std::uint8_t c[4] = {};         // color
    std::string s;                  // string, external element id text
    std::vector<std::uint8_t> blob; // void
    bool external = false;          // element reference with GUID text instead of index
};

struct Attribute {
    std::string name;
    std::uint8_t type = 0;
    std::vector<Value> values;      // exactly one for scalar types
    bool IsArray() const { return type > kArrayBase; }
};

struct Element {
    std::string type;
    std::string name;
    std::uint8_t guid[16] = {};
    std::vector<Attribute> attrs;
    Attribute* Find(const std::string& attrName);
    const Attribute* Find(const std::string& attrName) const;
};

struct Document {
    std::vector<Element> elements;  // elements[0] is the root
};

bool Parse(const std::uint8_t* data, std::size_t size, Document& doc, std::string& error);
// Writes elements[0] as root. Every element is serialized, so callers prune first.
bool Write(const Document& doc, std::vector<std::uint8_t>& out, std::string& error);

} // namespace dmx
