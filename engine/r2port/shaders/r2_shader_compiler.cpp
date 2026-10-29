#include "r2_shader_compiler.h"

#include <Windows.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <string>
#include <deque>
#include <utility>
#pragma comment(lib, "d3dcompiler.lib")

namespace r2port::shaders {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint32_t kVcsVersion = 6;
constexpr std::uint32_t kStoredChunk = 0x80000000u;
constexpr std::uint32_t kChunkEnd = 0xFFFFFFFFu;
constexpr std::uint32_t kHeaderBytes = 28;
constexpr std::uint32_t kMaxChunkPayload = 0x50000;   // the loader's chunk buffer is 0x60000 bytes (sub_18001D770)

class IncludeHandler final : public ID3DInclude {
public:
    explicit IncludeHandler(IncludeResolver resolver) : m_resolver(resolver) {}

    HRESULT STDMETHODCALLTYPE Open(
        D3D_INCLUDE_TYPE, LPCSTR fileName, LPCVOID, LPCVOID* data, UINT* bytes) override
    {
        std::string text;
        if (!m_resolver || !m_resolver(fileName, text))
            return E_FAIL;
        m_storage.push_back(std::move(text));
        *data = m_storage.back().data();
        *bytes = static_cast<UINT>(m_storage.back().size());
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Close(LPCVOID) override { return S_OK; }

private:
    IncludeResolver m_resolver;
    std::deque<std::string> m_storage;  // stable element addresses: the compiler reads earlier includes later
};

void Put32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(value));
}

std::uint32_t Get32(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    std::uint32_t value = 0;
    std::memcpy(&value, data.data() + offset, sizeof(value));
    return value;
}

struct Param {
    std::string semantic;
    UINT index{};
    BYTE mask{};
};

bool ReadParams(const std::vector<std::uint8_t>& dxbc, bool inputs, std::vector<Param>& params, std::string& error)
{
    ComPtr<ID3D11ShaderReflection> reflection;
    const HRESULT hr = D3DReflect(dxbc.data(), dxbc.size(), __uuidof(ID3D11ShaderReflection),
        reinterpret_cast<void**>(reflection.GetAddressOf()));
    if (FAILED(hr)) {
        error = "D3DReflect failed";
        return false;
    }
    D3D11_SHADER_DESC desc{};
    reflection->GetDesc(&desc);
    const UINT count = inputs ? desc.InputParameters : desc.OutputParameters;
    for (UINT i = 0; i < count; ++i) {
        D3D11_SIGNATURE_PARAMETER_DESC p{};
        const HRESULT phr = inputs ? reflection->GetInputParameterDesc(i, &p)
                                   : reflection->GetOutputParameterDesc(i, &p);
        if (FAILED(phr)) {
            error = "signature read failed";
            return false;
        }
        params.push_back({ p.SemanticName ? p.SemanticName : "", p.SemanticIndex, p.Mask });
    }
    return true;
}

} // namespace

std::uint64_t HashBytes(std::string_view data, std::uint64_t seed)
{
    std::uint64_t hash = seed;
    for (const unsigned char c : data) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

CompileResult CompileHlsl(const CompileRequest& request)
{
    CompileResult result;
    std::vector<D3D_SHADER_MACRO> macros;
    macros.reserve(request.macros.size() + 1);
    for (const ShaderMacro& macro : request.macros)
        macros.push_back({ macro.name.c_str(), macro.value.c_str() });
    macros.push_back({ nullptr, nullptr });

    IncludeHandler includeHandler(request.includes);
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(
        request.source.data(), request.source.size(), request.sourceName.c_str(),
        macros.data(), request.includes ? &includeHandler : nullptr,
        request.entry, request.profile,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0, code.GetAddressOf(), errors.GetAddressOf());
    if (errors && errors->GetBufferSize() > 0) {
        result.messages.assign(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        while (!result.messages.empty() && (result.messages.back() == '\0' || result.messages.back() == '\n'))
            result.messages.pop_back();
    }
    if (FAILED(hr) || !code) {
        char text[64];
        _snprintf_s(text, sizeof(text), _TRUNCATE, "D3DCompile failed hr=0x%08X", static_cast<unsigned>(hr));
        if (result.messages.empty())
            result.messages = text;
        return result;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(code->GetBufferPointer());
    result.dxbc.assign(bytes, bytes + code->GetBufferSize());
    result.ok = true;
    return result;
}

std::vector<std::uint8_t> BuildVcs(const std::vector<std::vector<std::uint8_t>>& combos, bool vertexShader)
{
    const std::uint32_t count = static_cast<std::uint32_t>(combos.size());
    std::vector<std::uint8_t> out;
    if (count == 0)
        return out;

    // header: version, two opaque words (stock VS files carry 4 then 1, PS files 1 then 1; the loader divides
    // the requested combo by the second), two zero words, static entry count (with the end sentinel), checksum
    // (not read by the loader).
    Put32(out, kVcsVersion);
    Put32(out, vertexShader ? 4u : 1u);
    Put32(out, 1);
    Put32(out, 0);
    Put32(out, 0);
    Put32(out, count + 1);
    Put32(out, 0);

    // static table: {combo id, file offset} per combo, then {0xFFFFFFFF, file size}; dynamic count 0
    const std::uint32_t tableEnd = kHeaderBytes + (count + 1) * 8u + 4u;
    std::uint32_t offset = tableEnd;
    std::vector<std::uint32_t> offsets;
    for (const auto& dxbc : combos) {
        offsets.push_back(offset);
        // chunk = tag + record {combo 0, size, bytes} + terminator
        offset += 4u + 8u + static_cast<std::uint32_t>(dxbc.size()) + 4u;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        Put32(out, i);
        Put32(out, offsets[i]);
    }
    Put32(out, kChunkEnd);
    Put32(out, offset);
    Put32(out, 0);  // dynamic combo count

    for (const auto& dxbc : combos) {
        Put32(out, kStoredChunk | static_cast<std::uint32_t>(8 + dxbc.size()));
        Put32(out, 0);
        Put32(out, static_cast<std::uint32_t>(dxbc.size()));
        out.insert(out.end(), dxbc.begin(), dxbc.end());
        Put32(out, kChunkEnd);
    }
    return out;
}

std::vector<std::uint8_t> BuildPixelVcsDynamic(const std::vector<std::vector<std::vector<std::uint8_t>>>& staticCombos)
{
    const std::uint32_t statics = static_cast<std::uint32_t>(staticCombos.size());
    std::vector<std::uint8_t> out;
    if (statics == 0 || staticCombos.front().empty())
        return out;
    const std::uint32_t count = static_cast<std::uint32_t>(staticCombos.front().size());
    // header like BuildVcs, but the two opaque words carry the total combo count and the dynamic combo count per static
    // combo (the loader divides a requested combo id by the second word to get the static index, and sizes the per-static
    // table of dynamic shaders with it)
    Put32(out, kVcsVersion);
    Put32(out, count * statics);
    Put32(out, count);
    Put32(out, 0);
    Put32(out, 0);
    Put32(out, statics + 1);
    Put32(out, 0);
    // The loader (materialsystem_dx11 sub_18001D770) copies every stored chunk into one fixed 393216 byte buffer, so a
    // static's records are split into several stored chunks of at most kMaxChunkPayload bytes (records never split);
    // a static's chunk list ends with the 0xFFFFFFFF terminator. 24 array-shadow records of one static are about 400 KB.
    const std::uint32_t tableEnd = kHeaderBytes + (statics + 1) * 8u + 4u;
    std::vector<std::vector<std::vector<std::uint32_t>>> groups;   // per static: record indices per chunk
    std::uint32_t offset = tableEnd;
    std::vector<std::uint32_t> offsets;
    for (const auto& records : staticCombos) {
        std::vector<std::vector<std::uint32_t>> chunks(1);
        std::uint32_t chunkBytes = 0;
        std::uint32_t total = 0;
        for (std::uint32_t r = 0; r < records.size(); ++r) {
            const std::uint32_t bytes = 8u + static_cast<std::uint32_t>(records[r].size());
            if (chunkBytes != 0 && chunkBytes + bytes > kMaxChunkPayload) {
                chunks.emplace_back();
                chunkBytes = 0;
            }
            chunks.back().push_back(r);
            chunkBytes += bytes;
            total += bytes;
        }
        total += 4u * static_cast<std::uint32_t>(chunks.size()) + 4u;   // one tag per chunk + terminator
        groups.push_back(std::move(chunks));
        offsets.push_back(offset);
        offset += total;
    }
    for (std::uint32_t i = 0; i < statics; ++i) {
        Put32(out, i);
        Put32(out, offsets[i]);
    }
    Put32(out, kChunkEnd);
    Put32(out, offset);
    Put32(out, 0);  // dynamic-to-static alias count
    for (std::uint32_t i = 0; i < statics; ++i) {
        for (const auto& chunk : groups[i]) {
            std::uint32_t payload = 0;
            for (std::uint32_t r : chunk)
                payload += 8u + static_cast<std::uint32_t>(staticCombos[i][r].size());
            Put32(out, kStoredChunk | payload);
            for (std::uint32_t r : chunk) {
                Put32(out, r);
                Put32(out, static_cast<std::uint32_t>(staticCombos[i][r].size()));
                out.insert(out.end(), staticCombos[i][r].begin(), staticCombos[i][r].end());
            }
        }
        Put32(out, kChunkEnd);
    }
    return out;
}

std::string ValidateVcs(const std::vector<std::uint8_t>& vcs, std::size_t expectedCombos, std::size_t expectedRecords)
{
    if (vcs.size() < kHeaderBytes + 8)
        return "too small";
    if (Get32(vcs, 0) != kVcsVersion)
        return "version is not 6";
    const std::uint32_t entries = Get32(vcs, 20);
    if (entries != expectedCombos + 1)
        return "static entry count mismatch";
    const std::size_t tableEnd = kHeaderBytes + static_cast<std::size_t>(entries) * 8 + 4;
    if (vcs.size() < tableEnd)
        return "table truncated";
    if (Get32(vcs, kHeaderBytes + (entries - 1) * 8) != kChunkEnd)
        return "missing end sentinel";
    if (Get32(vcs, kHeaderBytes + (entries - 1) * 8 + 4) != vcs.size())
        return "sentinel size does not match file size";
    if (Get32(vcs, kHeaderBytes + entries * 8) != 0)
        return "dynamic combo count is not zero";
    for (std::uint32_t i = 0; i < entries - 1; ++i) {
        if (Get32(vcs, kHeaderBytes + i * 8) != i)
            return "combo ids are not consecutive";
        const std::size_t begin = Get32(vcs, kHeaderBytes + i * 8 + 4);
        const std::size_t end = Get32(vcs, kHeaderBytes + (i + 1) * 8 + 4);
        if (begin < tableEnd || end <= begin || end > vcs.size() || begin + 4 > vcs.size())
            return "chunk bounds invalid";
        // chunk list: stored chunks of {dynamic id, size, DXBC} records back to back, ids consecutive from 0 across chunks,
        // each payload within the loader's buffer; the list ends with the terminator at the next static's offset
        std::size_t at = begin;
        std::uint32_t records = 0;
        while (true) {
            if (at + 4 > end)
                return "chunk list runs past the static";
            const std::uint32_t tag = Get32(vcs, at);
            if (tag == kChunkEnd) {
                if (at + 4 != end)
                    return "chunk terminator is not at the end of the static";
                break;
            }
            if ((tag & 0xC0000000u) != kStoredChunk)
                return "chunk is not stored";
            const std::uint32_t payload = tag & 0x3FFFFFFFu;
            if (payload > 393216u)
                return "chunk payload exceeds the loader buffer";
            if (at + 4 + payload + 4 > end)
                return "chunk payload size does not fill the chunk";
            std::uint32_t cursor = 0;
            while (cursor < payload) {
                if (payload - cursor < 8)
                    return "partial record header";
                const std::uint32_t id = Get32(vcs, at + 4 + cursor);
                const std::uint32_t recordSize = Get32(vcs, at + 4 + cursor + 4);
                if (id != records || recordSize < 32 || cursor + 8u + recordSize > payload)
                    return "record size mismatch";
                if (std::memcmp(vcs.data() + at + 4 + cursor + 8, "DXBC", 4) != 0)
                    return "record is not DXBC";
                cursor += 8u + recordSize;
                ++records;
            }
            at += 4 + payload;
        }
        if (records != expectedRecords)
            return "dynamic record count mismatch";
    }
    return {};
}

std::string CheckVertexPixelLinkage(
    const std::vector<std::uint8_t>& vertexDxbc, const std::vector<std::uint8_t>& pixelDxbc)
{
    std::vector<Param> vsOut;
    std::vector<Param> psIn;
    std::string error;
    if (!ReadParams(vertexDxbc, false, vsOut, error) || !ReadParams(pixelDxbc, true, psIn, error))
        return error;
    for (const Param& in : psIn) {
        if (_stricmp(in.semantic.c_str(), "SV_POSITION") == 0)
            continue;
        bool matched = false;
        for (const Param& out : vsOut) {
            if (_stricmp(out.semantic.c_str(), in.semantic.c_str()) == 0 && out.index == in.index) {
                matched = (in.mask & ~out.mask) == 0;
                break;
            }
        }
        if (!matched) {
            char text[160];
            _snprintf_s(text, sizeof(text), _TRUNCATE,
                "pixel input %s%u is not produced by the vertex shader", in.semantic.c_str(), in.index);
            return text;
        }
    }
    return {};
}

// Pixel shaders that replace a stock static combo with several dynamic combos mark themselves with this line; the
// compiler then builds one ps_5_0 record per dynamic index (see BuildPixelVcsDynamic) with the macros the source names
// there: `// R2_PS_DYNAMIC <dynamic count> <static count> [VLG]`. The optional VLG layout is shadow + 3 *
// (unshadowedScale + 2 * (instanceBuffer + 2 * fog)); the default LMG layout is lights + 2 * (shadow + 3 * fog).
constexpr std::string_view kDynamicMarker = "// R2_PS_DYNAMIC ";

BuiltShader BuildShader(std::string_view name, std::string_view source, IncludeResolver includes)
{
    BuiltShader built;
    const bool wantsPixel = source.find("PSMain") != std::string_view::npos;
    const bool wantsVertex = source.find("VSMain") != std::string_view::npos
        || source.find("R2_STANDARD_VS") != std::string_view::npos;
    if (!wantsPixel && !wantsVertex) {
        built.error = "source defines neither PSMain nor VSMain";
        return built;
    }

    const std::size_t marker = source.find(kDynamicMarker);
    if (marker != std::string_view::npos && wantsPixel && !wantsVertex) {
        // Static combo s defines R2_STATIC = s. Parse only the marker line, never the following source.
        const std::size_t specStart = marker + kDynamicMarker.size();
        const std::string spec(source.substr(specStart, source.find('\n', specStart) - specStart));
        int count = 0;
        int statics = 1;
        char layout[8] = {};
        std::sscanf(spec.c_str(), "%d %d %7s", &count, &statics, layout);
        const bool vlg = std::strcmp(layout, "VLG") == 0;
        if (count < 1 || count > 64 || statics < 1 || statics > 8 || (layout[0] && !vlg) || (vlg && count != 24)) {
            built.error = "bad R2_PS_DYNAMIC line";
            return built;
        }
        std::vector<std::vector<std::vector<std::uint8_t>>> combos(static_cast<std::size_t>(statics));
        std::vector<std::vector<CompileResult>> results(static_cast<std::size_t>(statics));
        std::vector<std::future<void>> jobs;
        for (int staticCombo = 0; staticCombo < statics; ++staticCombo) {
            results[static_cast<std::size_t>(staticCombo)].resize(static_cast<std::size_t>(count));
            for (int dynamic = 0; dynamic < count; ++dynamic) {
                jobs.push_back(std::async(std::launch::async, [&, staticCombo, dynamic]() {
                    CompileRequest request;
                    request.sourceName = std::string(name) + ".hlsl";
                    request.source = source;
                    request.entry = "PSMain";
                    request.profile = "ps_5_0";
                    request.includes = includes;
                    request.macros.push_back({ "R2_STATIC", std::to_string(staticCombo) });
                    if (vlg) {
                        request.macros.push_back({ "R2_SHADOW", std::to_string(dynamic % 3) });
                        request.macros.push_back({ "R2_UNSH", std::to_string((dynamic / 3) % 2) });
                        request.macros.push_back({ "R2_INST", std::to_string((dynamic / 6) % 2) });
                        request.macros.push_back({ "R2_FOGB", std::to_string(dynamic / 12) });
                    } else {
                        request.macros.push_back({ "R2_LIGHTS", std::to_string(dynamic % 2) });
                        request.macros.push_back({ "R2_SHADOW", std::to_string((dynamic / 2) % 3) });
                        request.macros.push_back({ "R2_FOGB", std::to_string((dynamic / 2) / 3) });
                    }
                    results[static_cast<std::size_t>(staticCombo)][static_cast<std::size_t>(dynamic)] = CompileHlsl(request);
                }));
            }
        }
        for (auto& job : jobs)
            job.get();
        for (int staticCombo = 0; staticCombo < statics; ++staticCombo) {
            for (int dynamic = 0; dynamic < count; ++dynamic) {
                CompileResult& result = results[static_cast<std::size_t>(staticCombo)][static_cast<std::size_t>(dynamic)];
                if (!result.ok) {
                    built.error = "pixel shader (static " + std::to_string(staticCombo) + " dynamic " + std::to_string(dynamic) + "): " + result.messages;
                    return built;
                }
                combos[static_cast<std::size_t>(staticCombo)].push_back(std::move(result.dxbc));
            }
        }
        built.pixelVcs = BuildPixelVcsDynamic(combos);
        built.hasPixel = true;
        built.pixelStatics = combos.size();
        built.pixelRecords = combos.front().size();
        return built;
    }


    std::vector<std::uint8_t> pixelDxbc;
    if (wantsPixel) {
        CompileRequest request;
        request.sourceName = std::string(name) + ".hlsl";
        request.source = source;
        request.entry = "PSMain";
        request.profile = "ps_4_0";
        request.includes = includes;
        CompileResult result = CompileHlsl(request);
        if (!result.ok) {
            built.error = "pixel shader: " + result.messages;
            return built;
        }
        pixelDxbc = std::move(result.dxbc);
        built.pixelVcs = BuildVcs({ pixelDxbc }, false);
        built.hasPixel = true;
    }

    if (wantsVertex) {
        // screenspace_general requests static combo 0 for every custom `$vertexshader` (the stock default VS picks
        // 0..3 from $vertexcolor / $vertexTransform; sub_18002B110 zeroes the VS combo when a name is given), so a
        // custom VS always runs transformed: combo 0 = R2_XFORM. A second file `<name>_vcol` adds the COLOR0 input.
        for (int variant = 0; variant < 2; ++variant) {
            CompileRequest request;
            request.sourceName = std::string(name) + ".hlsl";
            request.source = source;
            request.entry = "VSMain";
            request.profile = "vs_4_0";
            request.includes = includes;
            request.macros.push_back({ "R2_VCOLOR", variant ? "1" : "0" });
            request.macros.push_back({ "R2_XFORM", "1" });
            CompileResult result = CompileHlsl(request);
            if (!result.ok) {
                built.error = std::string(variant ? "vertex shader (vertex colour)" : "vertex shader") + ": " + result.messages;
                return built;
            }
            if (wantsPixel) {
                const std::string link = CheckVertexPixelLinkage(result.dxbc, pixelDxbc);
                if (!link.empty()) {
                    built.error = std::string(variant ? "vertex shader (vertex colour)" : "vertex shader")
                        + " does not link to the pixel shader: " + link;
                    return built;
                }
            }
            (variant ? built.vertexColorVcs : built.vertexVcs) = BuildVcs({ result.dxbc }, true);
        }
        built.hasVertex = true;
    }
    return built;
}

} // namespace r2port::shaders
