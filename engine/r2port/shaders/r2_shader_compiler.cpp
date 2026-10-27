#include "r2_shader_compiler.h"

#include <Windows.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstring>
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

std::string ValidateVcs(const std::vector<std::uint8_t>& vcs, std::size_t expectedCombos)
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
        const std::uint32_t tag = Get32(vcs, begin);
        if ((tag & 0xC0000000u) != kStoredChunk)
            return "chunk is not stored";
        const std::uint32_t payload = tag & 0x3FFFFFFFu;
        if (begin + 4 + payload + 4 != end)
            return "chunk payload size does not fill the chunk";
        const std::uint32_t recordSize = Get32(vcs, begin + 8);
        if (8u + recordSize != payload)
            return "record size mismatch";
        if (std::memcmp(vcs.data() + begin + 12, "DXBC", 4) != 0)
            return "record is not DXBC";
        if (Get32(vcs, begin + 4 + payload) != kChunkEnd)
            return "chunk terminator missing";
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
        std::vector<std::vector<std::uint8_t>> combos;
        for (int combo = 0; combo < 4; ++combo) {
            CompileRequest request;
            request.sourceName = std::string(name) + ".hlsl";
            request.source = source;
            request.entry = "VSMain";
            request.profile = "vs_4_0";
            request.includes = includes;
            request.macros.push_back({ "R2_VCOLOR", (combo & 1) ? "1" : "0" });
            request.macros.push_back({ "R2_XFORM", (combo & 2) ? "1" : "0" });
            CompileResult result = CompileHlsl(request);
            if (!result.ok) {
                built.error = "vertex shader combo " + std::to_string(combo) + ": " + result.messages;
                return built;
            }
            if (wantsPixel) {
                const std::string link = CheckVertexPixelLinkage(result.dxbc, pixelDxbc);
                if (!link.empty()) {
                    built.error = "vertex combo " + std::to_string(combo) + " does not link to the pixel shader: " + link;
                    return built;
                }
            }
            combos.push_back(std::move(result.dxbc));
        }
        built.vertexVcs = BuildVcs(combos, true);
        built.hasVertex = true;
    }
    return built;
}

} // namespace r2port::shaders
