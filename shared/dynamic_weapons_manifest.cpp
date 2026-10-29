#include "dynamic_weapons_manifest.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

namespace r1delta::dynamic_weapons
{
namespace
{
struct Tokenizer
{
    const std::string& text;
    std::size_t pos = 0;
    std::string error;

    void SkipSpace()
    {
        for (;;) {
            while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n'))
                ++pos;
            if (pos + 1 < text.size() && text[pos] == '/' && text[pos + 1] == '/') {
                while (pos < text.size() && text[pos] != '\n')
                    ++pos;
                continue;
            }
            break;
        }
    }

    // Returns false at end of input or on a malformed string (error set). A brace is its own token.
    bool Next(std::string& token, bool& quoted)
    {
        SkipSpace();
        token.clear();
        quoted = false;
        if (pos >= text.size())
            return false;
        const char c = text[pos];
        if (c == '"') {
            quoted = true;
            ++pos;
            while (pos < text.size() && text[pos] != '"') {
                if (text[pos] == '\n') {
                    error = "unterminated string";
                    return false;
                }
                token.push_back(text[pos++]);
            }
            if (pos >= text.size()) {
                error = "unterminated string";
                return false;
            }
            ++pos;
            return true;
        }
        if (c == '{' || c == '}') {
            token.push_back(c);
            ++pos;
            return true;
        }
        while (pos < text.size()) {
            const char d = text[pos];
            if (d == ' ' || d == '\t' || d == '\r' || d == '\n' || d == '"' || d == '{' || d == '}')
                break;
            if (d == '/' && pos + 1 < text.size() && text[pos + 1] == '/')
                break;
            token.push_back(d);
            ++pos;
        }
        return true;
    }
};

ManifestParse Failure(std::string message)
{
    ManifestParse result;
    result.error = std::move(message);
    return result;
}
}

bool NormalizeClassName(const std::string& entry, std::string& name, std::string& error)
{
    if (entry.empty()) {
        error = "empty weapon manifest entry";
        return false;
    }
    name = entry;
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos)
        name.erase(0, slash + 1);
    const auto dot = name.find_last_of('.');
    if (dot != std::string::npos)
        name.resize(dot);
    if (name.empty() || name.size() > MaxClassNameLength) {
        error = "weapon classname is empty or exceeds the native CClassMap char[40] contract: " + entry;
        return false;
    }
    for (char& c : name) {
        if (c >= 'A' && c <= 'Z')
            c = char(c + ('a' - 'A'));
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            error = "unsupported weapon manifest classname: " + entry;
            return false;
        }
    }
    return true;
}

ManifestParse ParseManifest(const std::string& text)
{
    Tokenizer in{text};
    std::string token;
    bool quoted = false;
    if (!in.Next(token, quoted))
        return Failure(in.error.empty() ? "weapon_manifest.txt has no root key" : in.error);
    if (token == "{" || token == "}")
        return Failure("weapon_manifest.txt has no root key");
    if (!in.Next(token, quoted) || quoted || token != "{")
        return Failure(in.error.empty() ? "weapon_manifest.txt: expected '{' after the root key" : in.error);
    std::vector<std::string> names;
    for (;;) {
        if (!in.Next(token, quoted))
            return Failure(in.error.empty() ? "weapon_manifest.txt: missing closing '}'" : in.error);
        if (!quoted && token == "}")
            break;
        if (!quoted && (token == "{" || token[0] == '#' || token[0] == '['))
            return Failure("weapon_manifest.txt: unsupported token: " + token);
        if (_stricmp(token.c_str(), "file"))
            return Failure("weapon_manifest.txt expects 'file', got: " + token);
        std::string value;
        if (!in.Next(value, quoted) || (!quoted && (value == "{" || value == "}")))
            return Failure(in.error.empty() ? "weapon_manifest.txt: 'file' without a value" : in.error);
        std::string name;
        std::string error;
        if (!NormalizeClassName(value, name, error))
            return Failure(error);
        names.push_back(std::move(name));
    }
    if (in.Next(token, quoted))
        return Failure("weapon_manifest.txt: content after the closing '}': " + token);
    if (!in.error.empty())
        return Failure(in.error);
    if (names.empty())
        return Failure("weapon_manifest.txt declares no weapons");
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    ManifestParse result;
    result.ok = true;
    result.names = std::move(names);
    return result;
}

std::string MakeRosterToken(const std::vector<std::string>& names)
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const auto& name : names) {
        for (unsigned char c : name) {
            hash ^= c;
            hash *= 0x100000001b3ULL;
        }
        hash ^= 0xff;
        hash *= 0x100000001b3ULL;
    }
    char text[48];
    _snprintf_s(text, sizeof(text), _TRUNCATE, "%zu:%016llx", names.size(), static_cast<unsigned long long>(hash));
    return text;
}
}
