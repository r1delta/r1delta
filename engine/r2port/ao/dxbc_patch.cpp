#include "dxbc_patch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string_view>
#include <utility>

namespace dxbc {
namespace {

constexpr std::uint32_t kShaderTypePixel = 0;
constexpr std::uint32_t kOpCustomData = 53;
constexpr std::uint32_t kOpDclResource = 88;
constexpr std::uint32_t kOpDclInputPsSiv = 100;
constexpr std::uint32_t kOpDclTemps = 104;
constexpr std::uint32_t kOpLd = 45;
constexpr std::uint32_t kOpFtoi = 27;
constexpr std::uint32_t kOpMov = 54;
constexpr std::uint32_t kOpMad = 50;
constexpr std::uint32_t kOpMul = 56;
constexpr std::uint32_t kResourceDimTexture2D = 3;
constexpr std::uint32_t kResourceDimTextureCube = 6;
constexpr std::uint32_t kResourceDimTextureCubeArray = 10;
constexpr std::uint32_t kOperandTemp = 0;
constexpr std::uint32_t kOperandInput = 1;
constexpr std::uint32_t kOperandResource = 7;
constexpr std::uint32_t kOperandSampler = 6;
constexpr std::uint32_t kNamePosition = 1;
constexpr std::uint32_t kRdefTexture = 2;
constexpr std::uint32_t kRdefTbuffer = 1;
constexpr std::size_t kRdefBindingBytes = 32;
constexpr std::size_t kSignatureEntryBytes = 24;
constexpr std::size_t kDxbcHashOffset = 20;

struct Chunk {
    std::array<char, 4> fourcc{};
    std::vector<std::uint8_t> payload;
};

struct Instruction {
    std::size_t start = 0;
    std::size_t length = 0;
    std::size_t operandStart = 0;
    std::uint32_t opcode = 0;
    std::uint32_t token = 0;
};

struct IndexValue {
    bool immediate = false;
    std::uint64_t value = 0;
};

struct Operand {
    std::uint32_t type = 0;
    std::uint32_t indexDimension = 0;
    std::uint32_t componentMask = 0;
    std::vector<IndexValue> indices;
};

struct SignatureEntry {
    std::uint32_t nameOffset = 0;
    std::uint32_t semanticIndex = 0;
    std::uint32_t systemValue = 0;
    std::uint32_t componentType = 0;
    std::uint32_t reg = 0;
    std::uint8_t mask = 0;
    std::uint8_t readWriteMask = 0;
};

struct SignatureInfo {
    std::size_t countOffset = 0;
    std::size_t tableOffset = 0;
    std::size_t entryBytes = kSignatureEntryBytes;
    std::vector<SignatureEntry> entries;
    bool hasPosition = false;
    std::uint32_t positionRegister = 0;
    std::uint32_t maxRegister = 0;
    bool hasRegisters = false;
};

struct EnvSample {
    std::size_t instructionIndex = 0;
    std::uint32_t destinationRegister = 0;
};

std::uint32_t Read32(const std::uint8_t* data)
{
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8) |
        (static_cast<std::uint32_t>(data[2]) << 16) |
        (static_cast<std::uint32_t>(data[3]) << 24);
}

void Write32(std::uint8_t* data, std::uint32_t value)
{
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8);
    data[2] = static_cast<std::uint8_t>(value >> 16);
    data[3] = static_cast<std::uint8_t>(value >> 24);
}

void Append32(std::vector<std::uint32_t>& words, std::uint32_t value)
{
    words.push_back(value);
}

void AppendBytes32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    const std::size_t at = bytes.size();
    bytes.resize(at + 4);
    Write32(bytes.data() + at, value);
}

std::uint32_t FloatBits(float value)
{
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float must be 32 bit");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool Fail(PatchReport& report, std::string message)
{
    report.error = std::move(message);
    return false;
}

bool ParseContainer(const std::vector<std::uint8_t>& data,
                    std::vector<Chunk>& chunks,
                    std::uint32_t& version,
                    std::string& error)
{
    if (data.size() < 32 || std::memcmp(data.data(), "DXBC", 4) != 0) {
        error = "input is not a complete DXBC container";
        return false;
    }
    version = Read32(data.data() + 20);
    const std::uint32_t totalSize = Read32(data.data() + 24);
    const std::uint32_t chunkCount = Read32(data.data() + 28);
    if (version != 1 || totalSize != data.size() || chunkCount > (data.size() - 32) / 4) {
        error = "unsupported or inconsistent DXBC header";
        return false;
    }
    const std::size_t tableEnd = 32 + static_cast<std::size_t>(chunkCount) * 4;
    chunks.clear();
    chunks.reserve(chunkCount);
    for (std::uint32_t i = 0; i < chunkCount; ++i) {
        const std::size_t offset = Read32(data.data() + 32 + static_cast<std::size_t>(i) * 4);
        if (offset < tableEnd || offset > data.size() || data.size() - offset < 8) {
            error = "DXBC chunk offset is outside the container";
            return false;
        }
        const std::uint32_t payloadSize = Read32(data.data() + offset + 4);
        if (payloadSize > data.size() - offset - 8) {
            error = "DXBC chunk payload exceeds the container";
            return false;
        }
        Chunk chunk;
        std::memcpy(chunk.fourcc.data(), data.data() + offset, 4);
        chunk.payload.assign(data.begin() + offset + 8, data.begin() + offset + 8 + payloadSize);
        chunks.push_back(std::move(chunk));
    }
    return true;
}

bool BuildContainer(const std::vector<Chunk>& chunks,
                    std::uint32_t version,
                    std::vector<std::uint8_t>& data,
                    std::string& error)
{
    if (chunks.size() > std::numeric_limits<std::uint32_t>::max()) {
        error = "too many DXBC chunks";
        return false;
    }
    const std::size_t headerBytes = 32 + chunks.size() * 4;
    std::size_t totalSize = headerBytes;
    for (const Chunk& chunk : chunks) {
        if (chunk.payload.size() > std::numeric_limits<std::uint32_t>::max() ||
            totalSize > std::numeric_limits<std::uint32_t>::max() - 8 - chunk.payload.size()) {
            error = "patched DXBC exceeds the 32 bit container size limit";
            return false;
        }
        totalSize += 8 + chunk.payload.size();
    }
    data.assign(totalSize, 0);
    std::memcpy(data.data(), "DXBC", 4);
    Write32(data.data() + 20, version);
    Write32(data.data() + 24, static_cast<std::uint32_t>(totalSize));
    Write32(data.data() + 28, static_cast<std::uint32_t>(chunks.size()));
    std::size_t cursor = headerBytes;
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        Write32(data.data() + 32 + i * 4, static_cast<std::uint32_t>(cursor));
        std::memcpy(data.data() + cursor, chunks[i].fourcc.data(), 4);
        Write32(data.data() + cursor + 4, static_cast<std::uint32_t>(chunks[i].payload.size()));
        if (!chunks[i].payload.empty())
            std::memcpy(data.data() + cursor + 8, chunks[i].payload.data(), chunks[i].payload.size());
        cursor += 8 + chunks[i].payload.size();
    }
    return true;
}

std::size_t FindChunk(const std::vector<Chunk>& chunks, std::string_view fourcc)
{
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        if (fourcc.size() == 4 && std::memcmp(chunks[i].fourcc.data(), fourcc.data(), 4) == 0)
            return i;
    }
    return chunks.size();
}

bool ParseInstructions(const std::vector<std::uint32_t>& words,
                       std::vector<Instruction>& instructions,
                       std::string& error)
{
    if (words.size() < 2 || words[1] != words.size()) {
        error = "SHDR/SHEX length token does not match its chunk";
        return false;
    }
    instructions.clear();
    std::size_t cursor = 2;
    while (cursor < words.size()) {
        const std::uint32_t token = words[cursor];
        const std::uint32_t opcode = token & 0x7ff;
        std::size_t length = (token >> 24) & 0x7f;
        if (opcode == kOpCustomData) {
            if (cursor + 1 >= words.size()) {
                error = "custom-data instruction has no length token";
                return false;
            }
            length = words[cursor + 1];
        }
        if (length == 0 || length > words.size() - cursor) {
            error = "invalid tokenized instruction length";
            return false;
        }
        std::size_t operandStart = cursor + 1;
        if (token & 0x80000000u) {
            bool more = true;
            while (more) {
                if (operandStart >= cursor + length) {
                    error = "extended opcode token exceeds its instruction";
                    return false;
                }
                more = (words[operandStart] & 0x80000000u) != 0;
                ++operandStart;
            }
        }
        instructions.push_back({cursor, length, operandStart, opcode, token});
        cursor += length;
    }
    return cursor == words.size();
}

bool ParseOperand(const std::vector<std::uint32_t>& words,
                  std::size_t& cursor,
                  std::size_t end,
                  Operand& operand,
                  std::string& error,
                  unsigned depth = 0)
{
    if (depth > 8 || cursor >= end) {
        error = "operand is truncated or excessively nested";
        return false;
    }
    const std::uint32_t token = words[cursor++];
    if (token & 0x80000000u) {
        bool more = true;
        while (more) {
            if (cursor >= end) {
                error = "extended operand token exceeds its instruction";
                return false;
            }
            const std::uint32_t extended = words[cursor++];
            more = (extended & 0x80000000u) != 0;
        }
    }
    const std::uint32_t componentCount = token & 3;
    if (componentCount == 3) {
        error = "unsupported N-component operand in a target instruction";
        return false;
    }
    if (componentCount == 2) {
        const std::uint32_t selectionMode = (token >> 2) & 3;
        operand.componentMask = selectionMode == 0 ? ((token >> 4) & 15) : 15;
    } else if (componentCount == 1) {
        operand.componentMask = 1u << ((token >> 4) & 3);
    } else {
        operand.componentMask = 0;
    }
    operand.type = (token >> 12) & 0xff;
    operand.indexDimension = (token >> 20) & 3;
    operand.indices.clear();
    operand.indices.reserve(operand.indexDimension);
    for (std::uint32_t dimension = 0; dimension < operand.indexDimension; ++dimension) {
        const std::uint32_t representation = (token >> (22 + 3 * dimension)) & 7;
        IndexValue index;
        if (representation == 0) {
            if (cursor >= end) {
                error = "operand immediate index is truncated";
                return false;
            }
            index.immediate = true;
            index.value = words[cursor++];
        } else if (representation == 1) {
            if (end - cursor < 2) {
                error = "operand 64 bit index is truncated";
                return false;
            }
            index.immediate = true;
            index.value = static_cast<std::uint64_t>(words[cursor]) |
                (static_cast<std::uint64_t>(words[cursor + 1]) << 32);
            cursor += 2;
        } else if (representation == 2) {
            Operand relative;
            if (!ParseOperand(words, cursor, end, relative, error, depth + 1))
                return false;
        } else if (representation == 3) {
            if (cursor >= end) {
                error = "operand immediate-plus-relative index is truncated";
                return false;
            }
            index.immediate = true;
            index.value = words[cursor++];
            Operand relative;
            if (!ParseOperand(words, cursor, end, relative, error, depth + 1))
                return false;
        } else if (representation == 4) {
            if (end - cursor < 2) {
                error = "operand 64 bit immediate-plus-relative index is truncated";
                return false;
            }
            index.immediate = true;
            index.value = static_cast<std::uint64_t>(words[cursor]) |
                (static_cast<std::uint64_t>(words[cursor + 1]) << 32);
            cursor += 2;
            Operand relative;
            if (!ParseOperand(words, cursor, end, relative, error, depth + 1))
                return false;
        } else {
            error = "unknown operand index representation";
            return false;
        }
        operand.indices.push_back(index);
    }
    if (operand.type == 4 || operand.type == 5) {
        const std::size_t componentValues = componentCount == 2 ? 4 : componentCount;
        const std::size_t wordsPerValue = operand.type == 5 ? 2 : 1;
        if (componentValues > (end - cursor) / wordsPerValue) {
            error = "immediate operand values exceed their instruction";
            return false;
        }
        cursor += componentValues * wordsPerValue;
    }
    return true;
}

bool GetImmediateRegister(const Operand& operand, std::uint32_t expectedType, std::uint32_t& reg)
{
    if (operand.type != expectedType || operand.indexDimension != 1 ||
        operand.indices.size() != 1 || !operand.indices[0].immediate ||
        operand.indices[0].value > std::numeric_limits<std::uint32_t>::max())
        return false;
    reg = static_cast<std::uint32_t>(operand.indices[0].value);
    return true;
}

bool IsResourceDeclaration(std::uint32_t opcode)
{
    return opcode == kOpDclResource || opcode == 161 || opcode == 162;
}

bool IsDeclaration(std::uint32_t opcode)
{
    return (opcode >= 88 && opcode <= 106) ||
        opcode == 142 || opcode == 143 || opcode == 144 || opcode == 145 ||
        (opcode >= 146 && opcode <= 162);
}

bool IsSampleOpcode(std::uint32_t opcode)
{
    return (opcode >= 69 && opcode <= 74) ||
        opcode == 108 || opcode == 125 || opcode == 126 || opcode == 127;
}

std::uint32_t MakeRegisterOperand(std::uint32_t type, std::uint32_t componentMask)
{
    return (2u | ((componentMask & 15u) << 4) | (type << 12) | (1u << 20));
}

std::uint32_t MakeSwizzleOperand(std::uint32_t type, std::uint32_t x, std::uint32_t y,
                                 std::uint32_t z, std::uint32_t w)
{
    const std::uint32_t swizzle = (x & 3) | ((y & 3) << 2) | ((z & 3) << 4) | ((w & 3) << 6);
    return 2u | (1u << 2) | (swizzle << 4) | (type << 12) | (1u << 20);
}

std::uint32_t MakeImmediateScalarOperand()
{
    return (1u | (4u << 12));
}

bool ParseSignature(const std::vector<std::uint8_t>& payload,
                    SignatureInfo& info,
                    std::string& error)
{
    if (payload.size() < 8) {
        error = "signature chunk is shorter than its header";
        return false;
    }
    const std::uint32_t count = Read32(payload.data());
    const std::uint32_t tableOffset = Read32(payload.data() + 4);
    if (count > (payload.size() / kSignatureEntryBytes) ||
        tableOffset > payload.size() ||
        static_cast<std::size_t>(count) > (payload.size() - tableOffset) / kSignatureEntryBytes) {
        error = "signature parameter table exceeds its chunk";
        return false;
    }
    info = {};
    info.countOffset = 0;
    info.tableOffset = tableOffset;
    info.entryBytes = kSignatureEntryBytes;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t at = tableOffset + static_cast<std::size_t>(i) * kSignatureEntryBytes;
        SignatureEntry entry;
        entry.nameOffset = Read32(payload.data() + at);
        entry.semanticIndex = Read32(payload.data() + at + 4);
        entry.systemValue = Read32(payload.data() + at + 8);
        entry.componentType = Read32(payload.data() + at + 12);
        entry.reg = Read32(payload.data() + at + 16);
        entry.mask = payload[at + 20];
        entry.readWriteMask = payload[at + 21];
        if (entry.nameOffset >= payload.size()) {
            error = "signature semantic name offset exceeds its chunk";
            return false;
        }
        const std::uint8_t* name = payload.data() + entry.nameOffset;
        const std::size_t remaining = payload.size() - entry.nameOffset;
        const void* terminator = std::memchr(name, 0, remaining);
        if (!terminator) {
            error = "signature semantic name is not terminated";
            return false;
        }
        if (!info.hasRegisters || entry.reg > info.maxRegister) {
            info.maxRegister = entry.reg;
            info.hasRegisters = true;
        }
        if (entry.systemValue == kNamePosition) {
            info.hasPosition = true;
            info.positionRegister = entry.reg;
        }
        info.entries.push_back(entry);
    }
    return true;
}

bool AddPositionSignature(std::vector<std::uint8_t>& payload,
                          std::uint32_t positionRegister,
                          std::string& error)
{
    SignatureInfo info;
    if (!ParseSignature(payload, info, error))
        return false;
    const std::uint32_t count = Read32(payload.data());
    const std::size_t insertion = info.tableOffset + static_cast<std::size_t>(count) * kSignatureEntryBytes;
    if (insertion > payload.size() || payload.size() > std::numeric_limits<std::uint32_t>::max() - kSignatureEntryBytes - 12) {
        error = "signature chunk is too large to extend";
        return false;
    }
    std::vector<std::uint8_t> extended;
    extended.reserve(payload.size() + kSignatureEntryBytes + 12);
    extended.insert(extended.end(), payload.begin(), payload.begin() + insertion);
    const std::size_t entryAt = extended.size();
    extended.resize(entryAt + kSignatureEntryBytes, 0);
    extended.insert(extended.end(), payload.begin() + insertion, payload.end());
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t at = info.tableOffset + static_cast<std::size_t>(i) * kSignatureEntryBytes;
        std::uint32_t nameOffset = Read32(extended.data() + at);
        if (nameOffset >= insertion) {
            if (nameOffset > std::numeric_limits<std::uint32_t>::max() - kSignatureEntryBytes) {
                error = "signature semantic name offset overflows";
                return false;
            }
            Write32(extended.data() + at, nameOffset + static_cast<std::uint32_t>(kSignatureEntryBytes));
        }
    }
    const std::uint32_t newNameOffset = static_cast<std::uint32_t>(payload.size() + kSignatureEntryBytes);
    Write32(extended.data() + entryAt, newNameOffset);
    Write32(extended.data() + entryAt + 4, 0);
    Write32(extended.data() + entryAt + 8, kNamePosition);
    Write32(extended.data() + entryAt + 12, 3); // D3D register component type FLOAT32
    Write32(extended.data() + entryAt + 16, positionRegister);
    extended[entryAt + 20] = 0x03; // SV_Position.xy
    extended[entryAt + 21] = 0x03;
    extended[entryAt + 22] = 0;
    extended[entryAt + 23] = 0;
    const char name[] = "SV_Position";
    extended.insert(extended.end(), name, name + sizeof(name));
    Write32(extended.data(), count + 1);
    payload.swap(extended);
    return true;
}

bool ParseRdef(const std::vector<std::uint8_t>& payload,
               std::uint32_t& resourceCount,
               std::uint32_t& resourceOffset,
               bool& hasTexture31,
               std::string& error)
{
    if (payload.size() < 16) {
        error = "RDEF chunk is shorter than its binding header";
        return false;
    }
    resourceCount = Read32(payload.data() + 8);
    resourceOffset = Read32(payload.data() + 12);
    if (resourceOffset > payload.size() ||
        static_cast<std::size_t>(resourceCount) > (payload.size() - resourceOffset) / kRdefBindingBytes) {
        error = "RDEF resource binding table exceeds its chunk";
        return false;
    }
    hasTexture31 = false;
    for (std::uint32_t i = 0; i < resourceCount; ++i) {
        const std::size_t at = resourceOffset + static_cast<std::size_t>(i) * kRdefBindingBytes;
        const std::uint32_t type = Read32(payload.data() + at + 4);
        const std::uint32_t bindPoint = Read32(payload.data() + at + 20);
        if ((type == kRdefTexture || type == kRdefTbuffer) && bindPoint == 31)
            hasTexture31 = true;
    }
    return true;
}

bool AddRdefTextureBinding(std::vector<std::uint8_t>& payload,
                           std::uint32_t aoSlot,
                           std::string& error)
{
    std::uint32_t resourceCount = 0;
    std::uint32_t resourceOffset = 0;
    bool hasTexture31 = false;
    if (!ParseRdef(payload, resourceCount, resourceOffset, hasTexture31, error))
        return false;
    if (hasTexture31 || aoSlot != 31) {
        error = "AO SRV slot is already occupied or is not the supported t31 slot";
        return false;
    }
    if (resourceCount == std::numeric_limits<std::uint32_t>::max()) {
        error = "RDEF resource count overflows";
        return false;
    }
    const std::size_t tableOffset = (payload.size() + 3u) & ~std::size_t(3u);
    const std::size_t tableBytes = (static_cast<std::size_t>(resourceCount) + 1) * kRdefBindingBytes;
    if (tableOffset > std::numeric_limits<std::uint32_t>::max() - tableBytes - 16) {
        error = "RDEF extension would exceed 32 bit offsets";
        return false;
    }
    std::vector<std::uint8_t> extended = payload;
    extended.resize(tableOffset, 0);
    const std::size_t newTableStart = extended.size();
    if (resourceCount != 0) {
        const std::size_t oldBytes = static_cast<std::size_t>(resourceCount) * kRdefBindingBytes;
        extended.insert(extended.end(), payload.begin() + resourceOffset, payload.begin() + resourceOffset + oldBytes);
    }
    const std::size_t newEntryAt = extended.size();
    extended.resize(newEntryAt + kRdefBindingBytes, 0);
    const std::size_t nameOffset = extended.size();
    const char name[] = "r1d_ssso_ao";
    extended.insert(extended.end(), name, name + sizeof(name));
    const std::uint32_t newValues[8] = {
        static_cast<std::uint32_t>(nameOffset), // name offset
        kRdefTexture,                            // D3D_SIT_TEXTURE
        5,                                        // D3D_RETURN_TYPE_FLOAT
        4,                                        // D3D_SRV_DIMENSION_TEXTURE2D
        0xffffffffu,                              // non-MS resource marker in RDEF
        aoSlot,
        1,
        1                                         // used by the pixel shader
    };
    for (std::size_t i = 0; i < 8; ++i)
        Write32(extended.data() + newEntryAt + i * 4, newValues[i]);
    Write32(extended.data() + 8, resourceCount + 1);
    Write32(extended.data() + 12, static_cast<std::uint32_t>(newTableStart));
    payload.swap(extended);
    return true;
}

bool ParsePositionDeclaration(const std::vector<std::uint32_t>& words,
                              const std::vector<Instruction>& instructions,
                              bool& found,
                              std::uint32_t& positionRegister,
                              std::string& error)
{
    found = false;
    for (const Instruction& instruction : instructions) {
        if (instruction.opcode != kOpDclInputPsSiv)
            continue;
        std::size_t cursor = instruction.operandStart;
        Operand input;
        if (!ParseOperand(words, cursor, instruction.start + instruction.length, input, error))
            return false;
        if (cursor >= instruction.start + instruction.length) {
            error = "pixel system-value declaration has no semantic token";
            return false;
        }
        const std::uint32_t systemValue = words[cursor];
        if (systemValue == kNamePosition) {
            if (!GetImmediateRegister(input, kOperandInput, positionRegister)) {
                error = "SV_Position declaration uses an unsupported input operand";
                return false;
            }
            found = true;
            return true;
        }
    }
    return true;
}

std::uint32_t MakeResourceDeclarationToken()
{
    // dcl_resource_texture2d (float,float,float,float) t31
    return (4u << 24) | (kResourceDimTexture2D << 11) | kOpDclResource;
}

void AppendResourceDeclaration(std::vector<std::uint32_t>& words, std::uint32_t slot)
{
    Append32(words, MakeResourceDeclarationToken());
    Append32(words, (1u << 20) | (kOperandResource << 12)); // one immediate register index, no components
    Append32(words, slot);
    Append32(words, 0x5555u); // float return type for all four components
}

void AppendPositionDeclaration(std::vector<std::uint32_t>& words, std::uint32_t reg)
{
    // FXC's SM5 encoding for dcl_input_ps_siv linear noperspective v#.xy, position.
    Append32(words, (4u << 24) | (4u << 11) | kOpDclInputPsSiv);
    Append32(words, MakeRegisterOperand(kOperandInput, 0x03));
    Append32(words, reg);
    Append32(words, kNamePosition);
}

void AppendFtoi(std::vector<std::uint32_t>& words, std::uint32_t coord, std::uint32_t position)
{
    Append32(words, (5u << 24) | kOpFtoi);
    Append32(words, MakeRegisterOperand(kOperandTemp, 0x03));
    Append32(words, coord);
    Append32(words, MakeSwizzleOperand(kOperandInput, 0, 1, 0, 0)); // vPos.xyxx
    Append32(words, position);
}

void AppendZeroZ(std::vector<std::uint32_t>& words, std::uint32_t coord)
{
    // mov rCoord.zw, l(0,0,0,0)  (z = mip level, w = unused but must be defined for the LD coordinate swizzle)
    Append32(words, (8u << 24) | kOpMov);
    Append32(words, MakeRegisterOperand(kOperandTemp, 0x0c));
    Append32(words, coord);
    Append32(words, (2u | (4u << 12))); // immediate32 float4
    Append32(words, 0);
    Append32(words, 0);
    Append32(words, 0);
    Append32(words, 0);
}

void AppendAoLoad(std::vector<std::uint32_t>& words, std::uint32_t coordinate, std::uint32_t ao,
                  std::uint32_t slot)
{
    // FXC SM5 LD extension tokens for an indexable Texture2D(float4).
    Append32(words, 0x89000000u | (9u << 24) | kOpLd);
    Append32(words, 0x800000c2u);
    Append32(words, 0x00155543u);
    Append32(words, MakeRegisterOperand(kOperandTemp, 0x01));
    Append32(words, ao);
    Append32(words, MakeSwizzleOperand(kOperandTemp, 0, 1, 2, 3)); // rCoord.xyzw (x, y, mip 0, 0)
    Append32(words, coordinate);
    Append32(words, MakeSwizzleOperand(kOperandResource, 0, 1, 2, 3));
    Append32(words, slot);
}

void AppendAoWeight(std::vector<std::uint32_t>& words, std::uint32_t ao, float weight, bool occlusionInput, float constantFactor)
{
    // AO input:       so = lerp(1, AO, weight) = (1 - weight) + AO * weight.
    // Occlusion input: so = 1 - weight * occ (an unbound texture reads 0, which is "no occlusion").
    // constantFactor >= 0 (test aid): so = constantFactor whatever the texture holds.
    float scale = occlusionInput ? -weight : weight;
    float bias = occlusionInput ? 1.0f : 1.0f - weight;
    if (constantFactor >= 0.0f) {
        scale = 0.0f;
        bias = constantFactor;
    }
    Append32(words, (9u << 24) | kOpMad);
    Append32(words, MakeRegisterOperand(kOperandTemp, 0x01));
    Append32(words, ao);
    Append32(words, MakeRegisterOperand(kOperandTemp, 0x01) & ~0x000000f0u | 0x0000000au); // AO.x
    Append32(words, ao);
    Append32(words, MakeImmediateScalarOperand());
    Append32(words, FloatBits(scale));
    Append32(words, MakeImmediateScalarOperand());
    Append32(words, FloatBits(bias));
}

void AppendEnvMultiply(std::vector<std::uint32_t>& words,
                       std::uint32_t destination,
                       std::uint32_t ao)
{
    Append32(words, (7u << 24) | kOpMul);
    Append32(words, MakeRegisterOperand(kOperandTemp, 0x07));
    Append32(words, destination);
    Append32(words, MakeSwizzleOperand(kOperandTemp, 0, 1, 2, 0)); // rDst.xyzx
    Append32(words, destination);
    Append32(words, MakeSwizzleOperand(kOperandTemp, 0, 0, 0, 0)); // rAO.xxxx
    Append32(words, ao);
}

bool ParseResourceTable(const Chunk& rdef,
                        std::uint32_t& resourceCount,
                        std::uint32_t& resourceOffset,
                        bool& hasSlot31,
                        std::string& error)
{
    return ParseRdef(rdef.payload, resourceCount, resourceOffset, hasSlot31, error);
}

// DXBC uses a modified MD5 finalization: hash bytes from offset 20, append the
// bit count as a dword, and use the DXBC-specific final length word.
// The algorithm follows AMD GPUOpen common-src-ShaderUtils/DX10/DXBCChecksum.cpp.
// That public implementation credits AMD (2008-2016), Seth Sowerby (2007), and
// the RSA Data Security MD5 reference implementation; this implementation is
// independently expressed below and preserves those attribution facts.
constexpr std::array<std::uint32_t, 64> kMd5Constants = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
};
constexpr std::array<std::uint32_t, 64> kMd5Shifts = {
    7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
    5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
    4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
    6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
};

std::uint32_t RotateLeft(std::uint32_t value, std::uint32_t count)
{
    return (value << count) | (value >> (32 - count));
}

void Md5Transform(std::array<std::uint32_t, 4>& state, const std::uint8_t block[64])
{
    std::uint32_t message[16];
    for (std::size_t i = 0; i < 16; ++i)
        message[i] = Read32(block + i * 4);
    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    for (std::uint32_t i = 0; i < 64; ++i) {
        std::uint32_t f = 0;
        std::uint32_t g = 0;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) & 15;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) & 15;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) & 15;
        }
        const std::uint32_t nextD = c;
        const std::uint32_t nextC = b;
        const std::uint32_t nextB = b + RotateLeft(a + f + kMd5Constants[i] + message[g], kMd5Shifts[i]);
        a = d;
        d = nextD;
        c = nextC;
        b = nextB;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void ComputeDxbcChecksum(const std::uint8_t* data, std::size_t size, std::uint8_t digest[16])
{
    std::array<std::uint32_t, 4> state = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    const std::uint8_t* message = data + kDxbcHashOffset;
    const std::size_t length = size - kDxbcHashOffset;
    const std::uint32_t bitLength = static_cast<std::uint32_t>(length * 8);
    const std::size_t fullBytes = length & ~std::size_t(63);
    for (std::size_t offset = 0; offset < fullBytes; offset += 64)
        Md5Transform(state, message + offset);
    const std::size_t tail = length - fullBytes;
    std::uint8_t finalBlock[64]{};
    if (tail >= 56) {
        if (tail)
            std::memcpy(finalBlock, message + fullBytes, tail);
        finalBlock[tail] = 0x80;
        Md5Transform(state, finalBlock);
        std::memset(finalBlock, 0, sizeof(finalBlock));
        Write32(finalBlock, bitLength);
        Write32(finalBlock + 60, (bitLength >> 2) | 1u);
        Md5Transform(state, finalBlock);
    } else {
        Write32(finalBlock, bitLength);
        if (tail)
            std::memcpy(finalBlock + 4, message + fullBytes, tail);
        finalBlock[tail + 4] = 0x80;
        Write32(finalBlock + 60, (bitLength >> 2) | 1u);
        Md5Transform(state, finalBlock);
    }
    for (std::size_t i = 0; i < 4; ++i)
        Write32(digest + i * 4, state[i]);
}

} // namespace

bool RecomputeChecksum(std::vector<std::uint8_t>& dxbc, std::string& error)
{
    if (dxbc.size() < 32 || std::memcmp(dxbc.data(), "DXBC", 4) != 0) {
        error = "cannot checksum a non-DXBC or truncated container";
        return false;
    }
    std::uint8_t digest[16]{};
    ComputeDxbcChecksum(dxbc.data(), dxbc.size(), digest);
    std::memcpy(dxbc.data() + 4, digest, sizeof(digest));
    return true;
}

bool VerifyChecksum(const std::vector<std::uint8_t>& dxbc)
{
    if (dxbc.size() < 32 || std::memcmp(dxbc.data(), "DXBC", 4) != 0)
        return false;
    std::uint8_t digest[16]{};
    ComputeDxbcChecksum(dxbc.data(), dxbc.size(), digest);
    return std::memcmp(dxbc.data() + 4, digest, sizeof(digest)) == 0;
}

bool PatchDxbc(const std::vector<std::uint8_t>& input,
               std::vector<std::uint8_t>& output,
               PatchReport& report,
               const PatchOptions& options)
{
    report = {};
    if (options.aoSlot != 31 || !std::isfinite(options.aoWeight) || options.aoWeight < 0.0f || options.aoWeight > 1.0f)
        return Fail(report, "prototype supports t31 and an AO blend weight in [0,1]");
    std::vector<Chunk> chunks;
    std::uint32_t containerVersion = 0;
    std::string error;
    if (!ParseContainer(input, chunks, containerVersion, error))
        return Fail(report, std::move(error));
    const std::size_t shaderIndex = FindChunk(chunks, "SHEX") != chunks.size() ? FindChunk(chunks, "SHEX") : FindChunk(chunks, "SHDR");
    if (shaderIndex == chunks.size())
        return Fail(report, "DXBC has neither a SHEX nor SHDR chunk");
    if (chunks[shaderIndex].payload.size() < 8 || chunks[shaderIndex].payload.size() % 4 != 0)
        return Fail(report, "shader code chunk has an invalid size");
    std::vector<std::uint32_t> words(chunks[shaderIndex].payload.size() / 4);
    for (std::size_t i = 0; i < words.size(); ++i)
        words[i] = Read32(chunks[shaderIndex].payload.data() + i * 4);
    if (((words[0] >> 16) & 0xffff) != kShaderTypePixel || ((words[0] >> 4) & 15) != 5)
        return Fail(report, "only pixel shader model 5.0 DXBC is supported");
    std::vector<Instruction> instructions;
    if (!ParseInstructions(words, instructions, error))
        return Fail(report, std::move(error));

    std::map<std::uint32_t, std::uint32_t> resourceDimensions;
    std::uint32_t maxTexture = 0;
    bool hasTexture = false;
    std::uint32_t tempCount = 0;
    bool hasTempDeclaration = false;
    std::uint32_t maxTempRegister = 0;
    bool hasTempRegister = false;
    bool positionDeclared = false;
    std::uint32_t positionRegister = 0;
    for (const Instruction& instruction : instructions) {
        if (IsResourceDeclaration(instruction.opcode)) {
            std::size_t cursor = instruction.operandStart;
            Operand resource;
            if (!ParseOperand(words, cursor, instruction.start + instruction.length, resource, error))
                return Fail(report, std::move(error));
            std::uint32_t slot = 0;
            if (GetImmediateRegister(resource, kOperandResource, slot)) {
                hasTexture = true;
                maxTexture = std::max(maxTexture, slot);
                if (instruction.opcode == kOpDclResource)
                    resourceDimensions[slot] = (instruction.token >> 11) & 31;
                if (slot == options.aoSlot)
                    return Fail(report, "t31 is already declared in the shader token stream");
            }
        }
        if (instruction.opcode == kOpDclTemps) {
            if (hasTempDeclaration || instruction.length != 2)
                return Fail(report, "unexpected or malformed dcl_temps instruction");
            hasTempDeclaration = true;
            tempCount = words[instruction.start + 1];
        }
        if (instruction.opcode == kOpDclInputPsSiv) {
            std::size_t cursor = instruction.operandStart;
            Operand inputOperand;
            if (!ParseOperand(words, cursor, instruction.start + instruction.length, inputOperand, error))
                return Fail(report, std::move(error));
            if (cursor < instruction.start + instruction.length && words[cursor] == kNamePosition) {
                if (!GetImmediateRegister(inputOperand, kOperandInput, positionRegister))
                    return Fail(report, "SV_Position input register is not directly indexed");
                positionDeclared = true;
            }
        }
        const std::size_t end = instruction.start + instruction.length;
        for (std::size_t cursor = instruction.operandStart; cursor < end;) {
            Operand operand;
            if (!ParseOperand(words, cursor, end, operand, error))
                break; // Non-operand declaration tokens are expected in some DCL forms.
            std::uint32_t reg = 0;
            if (GetImmediateRegister(operand, kOperandTemp, reg)) {
                maxTempRegister = std::max(maxTempRegister, reg);
                hasTempRegister = true;
            }
        }
    }
    report.maxDeclaredTextureSlot = hasTexture ? maxTexture : 0;
    if (!hasTexture)
        return Fail(report, "shader has no declared texture resources");

    SignatureInfo inputSignature;
    const std::size_t isgnIndex = FindChunk(chunks, "ISGN");
    bool hasInputSignature = isgnIndex != chunks.size();
    if (hasInputSignature && !ParseSignature(chunks[isgnIndex].payload, inputSignature, error))
        return Fail(report, std::move(error));
    if (!hasInputSignature) {
        inputSignature = {};
        inputSignature.tableOffset = 8;
        inputSignature.entryBytes = kSignatureEntryBytes;
    }
    if (inputSignature.hasPosition) {
        if (positionDeclared && positionRegister != inputSignature.positionRegister)
            return Fail(report, "SV_Position signature and declaration use different registers");
        positionRegister = inputSignature.positionRegister;
    } else if (positionDeclared) {
        report.addedPositionSignature = true;
    } else {
        if (inputSignature.hasRegisters && inputSignature.maxRegister == std::numeric_limits<std::uint32_t>::max())
            return Fail(report, "pixel input register index overflows");
        positionRegister = inputSignature.hasRegisters ? inputSignature.maxRegister + 1 : 0;
        report.addedPositionSignature = true;
        report.addedPositionDeclaration = true;
    }
    if (!positionDeclared && inputSignature.hasPosition)
        report.addedPositionDeclaration = true;
    report.positionRegister = positionRegister;
    if (report.addedPositionSignature) {
        if (!hasInputSignature) {
            Chunk signature;
            signature.fourcc = {'I','S','G','N'};
            signature.payload.resize(8, 0);
            Write32(signature.payload.data() + 4, 8);
            chunks.push_back(std::move(signature));
        }
        const std::size_t currentIsgn = FindChunk(chunks, "ISGN");
        if (!AddPositionSignature(chunks[currentIsgn].payload, positionRegister, error))
            return Fail(report, std::move(error));
    }

    std::uint32_t rdefCount = 0;
    std::uint32_t rdefOffset = 0;
    bool rdefSlot31 = false;
    const std::size_t rdefIndex = FindChunk(chunks, "RDEF");
    if (rdefIndex == chunks.size())
        return Fail(report, "RDEF chunk is missing; cannot keep reflection bindings consistent");
    if (!ParseResourceTable(chunks[rdefIndex], rdefCount, rdefOffset, rdefSlot31, error))
        return Fail(report, std::move(error));
    if (rdefSlot31)
        return Fail(report, "t31 is already bound by RDEF");

    std::vector<EnvSample> envSamples;
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        const Instruction& instruction = instructions[index];
        if (!IsSampleOpcode(instruction.opcode))
            continue;
        std::size_t cursor = instruction.operandStart;
        const std::size_t end = instruction.start + instruction.length;
        Operand destination, coordinate, resource;
        if (!ParseOperand(words, cursor, end, destination, error) ||
            !ParseOperand(words, cursor, end, coordinate, error) ||
            !ParseOperand(words, cursor, end, resource, error))
            return Fail(report, std::move(error));
        std::uint32_t resourceSlot = 0;
        if (!GetImmediateRegister(resource, kOperandResource, resourceSlot))
            continue; // A non-literal resource index cannot be matched to a declared cube safely.
        const auto dimension = resourceDimensions.find(resourceSlot);
        if (dimension == resourceDimensions.end() ||
            (dimension->second != kResourceDimTextureCube && dimension->second != kResourceDimTextureCubeArray))
            continue;
        std::uint32_t destinationRegister = 0;
        if (!GetImmediateRegister(destination, kOperandTemp, destinationRegister))
            return Fail(report, "cube sample destination is not a directly indexed temporary register");
        if ((destination.componentMask & 0x07) == 0)
            return Fail(report, "cube sample destination does not write RGB components");
        envSamples.push_back({index, destinationRegister});
    }
    if (envSamples.empty())
        return Fail(report, "shader contains no sample instruction referencing a declared cube resource");
    report.cubeSamples = static_cast<std::uint32_t>(envSamples.size());

    const std::uint32_t originalTempCount = hasTempDeclaration ? tempCount : (hasTempRegister ? maxTempRegister + 1 : 0);
    if (originalTempCount > 4094)
        return Fail(report, "shader has no room for the two AO temporary registers");
    report.oldTempCount = originalTempCount;
    report.coordinateTemp = originalTempCount;
    report.aoTemp = originalTempCount + 1;

    std::vector<std::uint32_t> patchedWords;
    patchedWords.reserve(words.size() + 64 + envSamples.size() * 7);
    patchedWords.push_back(words[0]);
    patchedWords.push_back(0);
    bool setupInserted = false;
    std::size_t envIndex = 0;
    for (const Instruction& instruction : instructions) {
        if (!setupInserted && !IsDeclaration(instruction.opcode)) {
            AppendResourceDeclaration(patchedWords, options.aoSlot);
            if (report.addedPositionDeclaration)
                AppendPositionDeclaration(patchedWords, positionRegister);
            if (!hasTempDeclaration) {
                Append32(patchedWords, (2u << 24) | kOpDclTemps);
                Append32(patchedWords, originalTempCount + 2);
            }
            AppendFtoi(patchedWords, report.coordinateTemp, positionRegister);
            AppendZeroZ(patchedWords, report.coordinateTemp);
            AppendAoLoad(patchedWords, report.coordinateTemp, report.aoTemp, options.aoSlot);
            AppendAoWeight(patchedWords, report.aoTemp, options.aoWeight, options.occlusionInput, options.constantFactor);
            setupInserted = true;
        }
        const std::size_t copiedStart = patchedWords.size();
        for (std::size_t i = instruction.start; i < instruction.start + instruction.length; ++i)
            patchedWords.push_back(words[i]);
        if (instruction.opcode == kOpDclTemps)
            patchedWords[copiedStart + 1] = originalTempCount + 2;
        while (envIndex < envSamples.size() && envSamples[envIndex].instructionIndex == static_cast<std::size_t>(&instruction - instructions.data())) {
            AppendEnvMultiply(patchedWords, envSamples[envIndex].destinationRegister, report.aoTemp);
            ++envIndex;
        }
    }
    if (!setupInserted || envIndex != envSamples.size())
        return Fail(report, "failed to place the AO setup and sample multipliers in the shader program");
    if (patchedWords.size() > std::numeric_limits<std::uint32_t>::max())
        return Fail(report, "patched shader program exceeds the tokenized length field");
    patchedWords[1] = static_cast<std::uint32_t>(patchedWords.size());
    Chunk& shaderChunk = chunks[shaderIndex];
    shaderChunk.payload.resize(patchedWords.size() * 4);
    for (std::size_t i = 0; i < patchedWords.size(); ++i)
        Write32(shaderChunk.payload.data() + i * 4, patchedWords[i]);

    if (!AddRdefTextureBinding(chunks[rdefIndex].payload, options.aoSlot, error))
        return Fail(report, std::move(error));
    const std::size_t statIndex = FindChunk(chunks, "STAT");
    if (statIndex != chunks.size()) {
        std::vector<std::uint8_t>& stats = chunks[statIndex].payload;
        if (stats.size() >= 5 * 4 && stats.size() % 4 == 0) {
            Write32(stats.data(), Read32(stats.data()) + 4 + report.cubeSamples);
            Write32(stats.data() + 4, originalTempCount + 2);
            if (report.addedPositionDeclaration)
                Write32(stats.data() + 12, Read32(stats.data() + 12) + 1);
            Write32(stats.data() + 16, Read32(stats.data() + 16) + 1); // TextureLoadInstructions
        }
    }
    std::size_t added = 4 + report.cubeSamples;
    if (report.addedPositionDeclaration)
        ++added;
    if (!hasTempDeclaration)
        ++added;
    report.addedInstructionCount = static_cast<std::uint32_t>(added);
    if (!BuildContainer(chunks, containerVersion, output, error))
        return Fail(report, std::move(error));
    if (!RecomputeChecksum(output, error))
        return Fail(report, std::move(error));
    return true;
}

// ---- r2port_lm_bicubic -----------------------------------------------------------------------------------------------
namespace {

constexpr std::uint32_t kOpAdd = 0;
constexpr std::uint32_t kOpCall = 4;
constexpr std::uint32_t kOpCallc = 5;
constexpr std::uint32_t kOpDiv = 14;
constexpr std::uint32_t kOpFrc = 26;
constexpr std::uint32_t kOpIf = 31;
constexpr std::uint32_t kOpEndIf = 21;
constexpr std::uint32_t kOpEndLoop = 22;
constexpr std::uint32_t kOpEndSwitch = 23;
constexpr std::uint32_t kOpLabel = 44;
constexpr std::uint32_t kOpLoop = 48;
constexpr std::uint32_t kOpResinfo = 61;
constexpr std::uint32_t kOpSample = 69;
constexpr std::uint32_t kOpSampleL = 72;
constexpr std::uint32_t kOpSwitch = 76;
constexpr std::uint32_t kLightmapSlot0 = 9;   // lightmapTexture_0: RGBA8, rgb * (a * 3.984375 + 0.015625)
constexpr std::uint32_t kLightmapSlot1 = 10;  // lightmapTexture_1: signed direction, * 2.007874 - 1
constexpr std::uint32_t kBicubicTemps = 13;

using Words = std::vector<std::uint32_t>;

void Cat(Words& to, const Words& part)
{
    to.insert(to.end(), part.begin(), part.end());
}

// Destination temp with a write mask.
Words DstT(std::uint32_t reg, std::uint32_t mask)
{
    return {MakeRegisterOperand(kOperandTemp, mask), reg};
}

// Source temp with a swizzle given as four letters (xyzw), optionally negated.
std::uint32_t Lane(char c)
{
    return c == 'x' ? 0u : c == 'y' ? 1u : c == 'z' ? 2u : 3u;
}

Words SrcT(std::uint32_t reg, const char* swizzle, bool negate = false)
{
    const std::uint32_t token = MakeSwizzleOperand(kOperandTemp, Lane(swizzle[0]), Lane(swizzle[1]), Lane(swizzle[2]), Lane(swizzle[3]));
    if (negate)
        return {token | 0x80000000u, 0x41u, reg};
    return {token, reg};
}

Words SrcIn(std::uint32_t reg, std::uint32_t a, std::uint32_t b)
{
    return {MakeSwizzleOperand(kOperandInput, a, b, a, a), reg};
}

// 4-component immediate with the same value in every lane (the lane used is the destination lane).
Words Imm(float value)
{
    const std::uint32_t bits = FloatBits(value);
    return {2u | (4u << 12), bits, bits, bits, bits};
}

void Emit(Words& out, std::uint32_t opcode, std::initializer_list<Words> operands, std::uint32_t extendedTokens = 0)
{
    std::size_t length = 1;
    for (const Words& operand : operands)
        length += operand.size();
    const bool extended = extendedTokens != 0;
    if (extended)
        length += 2;
    out.push_back(opcode | (static_cast<std::uint32_t>(length) << 24) | (extended ? 0x80000000u : 0u));
    if (extended) {
        out.push_back(0x800000c2u);  // resource dimension texture2d, another extension follows
        out.push_back(0x00155543u);  // return type float x4
    }
    for (const Words& operand : operands)
        Cat(out, operand);
}

struct LightmapSample {
    std::size_t instruction = 0;
    std::uint32_t slot = 0;
    std::uint32_t destination = 0;
    std::uint32_t coordReg = 0;
    std::uint32_t coordX = 0, coordY = 0;
    Words resource, sampler;   // operand words copied verbatim into the replacement taps
};

// Weights and tap coordinates of the cubic B-spline (SDK 2013 common_lightmappedgeneric_fxc.h / Godot PR 89919), both axes at once in
// the .xy lanes. With t = uv * size - 0.5, i = floor(t), f = frac(t):
//   w0..w3 = B-spline weights of f; g0 = w0 + w1; g1 = 1 - g0; taps at (i - 0.5 + w1 / g0) and (i + 1.5 + w3 / g1) texels.
// Result = gy0 * (gx0 * T(x0, y0) + gx1 * T(x1, y0)) + gy1 * (gx0 * T(x0, y1) + gx1 * T(x1, y1)) with gx1 = 1 - gx0.
// Temps (relative): 0 size, 1 1/size, 2 uv*size-1 then i-0.5, 3 f, 4 g0, 5 g1, 6 w1 then d0 then X0, 7 w3 then d1 then X1,
// 8 scratch, 9 tap uv (X0, X1, Y0, Y1), 10 / 11 / 12 blend scratch.
std::uint32_t EmitBicubicSetup(Words& out, std::uint32_t base, const LightmapSample& first)
{
    const auto R = [&](std::uint32_t i) { return base + i; };
    const Words uv = SrcIn(first.coordReg, first.coordX, first.coordY);
    std::uint32_t count = 0;
    const auto op = [&](std::uint32_t opcode, std::initializer_list<Words> operands, std::uint32_t ext = 0) { Emit(out, opcode, operands, ext); ++count; };
    // 0: size of t9
    {
        Words resource = first.resource;
        op(kOpResinfo, {DstT(R(0), 0x3), {MakeImmediateScalarOperand(), 0}, resource}, 1);
    }
    op(kOpDiv, {DstT(R(1), 0x3), Imm(1.0f), SrcT(R(0), "xyxx")});
    op(kOpMad, {DstT(R(3), 0x3), uv, SrcT(R(0), "xyxx"), Imm(-0.5f)});
    op(kOpFrc, {DstT(R(3), 0x3), SrcT(R(3), "xyxx")});
    op(kOpMad, {DstT(R(2), 0x3), uv, SrcT(R(0), "xyxx"), Imm(-1.0f)});
    op(kOpAdd, {DstT(R(2), 0x3), SrcT(R(2), "xyxx"), SrcT(R(3), "xyxx", true)});           // i - 0.5
    op(kOpMad, {DstT(R(4), 0x3), SrcT(R(3), "xyxx"), Imm(1.0f / 3.0f), Imm(-0.5f)});
    op(kOpMad, {DstT(R(4), 0x3), SrcT(R(4), "xyxx"), SrcT(R(3), "xyxx"), Imm(-0.5f)});
    op(kOpMad, {DstT(R(4), 0x3), SrcT(R(4), "xyxx"), SrcT(R(3), "xyxx"), Imm(5.0f / 6.0f)}); // g0
    op(kOpAdd, {DstT(R(5), 0x3), SrcT(R(4), "xyxx", true), Imm(1.0f)});                      // g1
    op(kOpMad, {DstT(R(6), 0x3), SrcT(R(3), "xyxx"), Imm(0.5f), Imm(-1.0f)});
    op(kOpMul, {DstT(R(6), 0x3), SrcT(R(6), "xyxx"), SrcT(R(3), "xyxx")});
    op(kOpMad, {DstT(R(6), 0x3), SrcT(R(6), "xyxx"), SrcT(R(3), "xyxx"), Imm(2.0f / 3.0f)}); // w1
    op(kOpMul, {DstT(R(7), 0x3), SrcT(R(3), "xyxx"), SrcT(R(3), "xyxx")});
    op(kOpMul, {DstT(R(8), 0x3), SrcT(R(3), "xyxx"), Imm(1.0f / 6.0f)});
    op(kOpMul, {DstT(R(7), 0x3), SrcT(R(7), "xyxx"), SrcT(R(8), "xyxx")});                   // w3
    op(kOpDiv, {DstT(R(6), 0x3), SrcT(R(6), "xyxx"), SrcT(R(4), "xyxx")});                   // d0 = w1 / g0
    op(kOpDiv, {DstT(R(7), 0x3), SrcT(R(7), "xyxx"), SrcT(R(5), "xyxx")});                   // d1 = w3 / g1
    op(kOpAdd, {DstT(R(6), 0x3), SrcT(R(6), "xyxx"), SrcT(R(2), "xyxx")});                   // X0 (texels)
    op(kOpAdd, {DstT(R(8), 0x3), SrcT(R(2), "xyxx"), Imm(2.0f)});
    op(kOpAdd, {DstT(R(7), 0x3), SrcT(R(7), "xyxx"), SrcT(R(8), "xyxx")});                   // X1 (texels)
    op(kOpMul, {DstT(R(9), 0x5), SrcT(R(6), "xxyy"), SrcT(R(1), "xxyy")});                   // (X0, ., Y0, .)
    op(kOpMul, {DstT(R(9), 0xa), SrcT(R(7), "xxyy"), SrcT(R(1), "xxyy")});                   // (., X1, ., Y1)
    return count;
}

std::uint32_t EmitBicubicTaps(Words& out, std::uint32_t base, const LightmapSample& s)
{
    const auto R = [&](std::uint32_t i) { return base + i; };
    std::uint32_t count = 0;
    const auto op = [&](std::uint32_t opcode, std::initializer_list<Words> operands, std::uint32_t ext = 0) { Emit(out, opcode, operands, ext); ++count; };
    const auto tap = [&](std::uint32_t dest, const char* uvSwizzle) {
        op(kOpSampleL, {DstT(dest, 0xf), SrcT(R(9), uvSwizzle), s.resource, s.sampler, {MakeImmediateScalarOperand(), 0}}, 1);
    };
    const std::uint32_t d = s.destination, e = R(10), g = R(11), h = R(12);
    tap(d, "xzxx");  // (X0, Y0)
    tap(e, "yzyy");  // (X1, Y0)
    tap(g, "xwxx");  // (X0, Y1)
    tap(h, "ywyy");  // (X1, Y1)
    op(kOpAdd, {DstT(d, 0xf), SrcT(d, "xyzw"), SrcT(e, "xyzw", true)});
    op(kOpMad, {DstT(d, 0xf), SrcT(d, "xyzw"), SrcT(R(4), "xxxx"), SrcT(e, "xyzw")});        // row y0
    op(kOpAdd, {DstT(g, 0xf), SrcT(g, "xyzw"), SrcT(h, "xyzw", true)});
    op(kOpMad, {DstT(g, 0xf), SrcT(g, "xyzw"), SrcT(R(4), "xxxx"), SrcT(h, "xyzw")});        // row y1
    op(kOpAdd, {DstT(d, 0xf), SrcT(d, "xyzw"), SrcT(g, "xyzw", true)});
    op(kOpMad, {DstT(d, 0xf), SrcT(d, "xyzw"), SrcT(R(4), "yyyy"), SrcT(g, "xyzw")});        // gy0 * row y0 + gy1 * row y1
    return count;
}

} // namespace

bool PatchLightmapBicubic(const std::vector<std::uint8_t>& input,
                          std::vector<std::uint8_t>& output,
                          LightmapBicubicReport& report)
{
    report = {};
    const auto fail = [&](std::string message) { report.error = std::move(message); return false; };
    std::vector<Chunk> chunks;
    std::uint32_t containerVersion = 0;
    std::string error;
    if (!ParseContainer(input, chunks, containerVersion, error))
        return fail(std::move(error));
    const std::size_t shaderIndex = FindChunk(chunks, "SHEX") != chunks.size() ? FindChunk(chunks, "SHEX") : FindChunk(chunks, "SHDR");
    if (shaderIndex == chunks.size() || chunks[shaderIndex].payload.size() < 8 || chunks[shaderIndex].payload.size() % 4 != 0)
        return fail("DXBC has no usable SHEX/SHDR chunk");
    std::vector<std::uint32_t> words(chunks[shaderIndex].payload.size() / 4);
    for (std::size_t i = 0; i < words.size(); ++i)
        words[i] = Read32(chunks[shaderIndex].payload.data() + i * 4);
    if (((words[0] >> 16) & 0xffff) != kShaderTypePixel || ((words[0] >> 4) & 15) != 5)
        return fail("only pixel shader model 5.0 DXBC is supported");
    std::vector<Instruction> instructions;
    if (!ParseInstructions(words, instructions, error))
        return fail(std::move(error));

    std::map<std::uint32_t, std::uint32_t> resourceDimensions;
    bool hasTempDeclaration = false;
    std::uint32_t tempCount = 0;
    std::vector<LightmapSample> samples;
    std::uint32_t depth = 0;
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        const Instruction& instruction = instructions[index];
        const std::size_t end = instruction.start + instruction.length;
        if (IsResourceDeclaration(instruction.opcode)) {
            std::size_t cursor = instruction.operandStart;
            Operand resource;
            if (!ParseOperand(words, cursor, end, resource, error))
                return fail(std::move(error));
            std::uint32_t slot = 0;
            if (instruction.opcode == kOpDclResource && GetImmediateRegister(resource, kOperandResource, slot))
                resourceDimensions[slot] = (instruction.token >> 11) & 31;
            continue;
        }
        if (instruction.opcode == kOpDclTemps) {
            if (hasTempDeclaration || instruction.length != 2)
                return fail("unexpected or malformed dcl_temps instruction");
            hasTempDeclaration = true;
            tempCount = words[instruction.start + 1];
            continue;
        }
        if (instruction.opcode == kOpCall || instruction.opcode == kOpCallc || instruction.opcode == kOpLabel)
            return fail("shader uses subroutines");
        if (instruction.opcode == kOpIf || instruction.opcode == kOpLoop || instruction.opcode == kOpSwitch)
            ++depth;
        else if (instruction.opcode == kOpEndIf || instruction.opcode == kOpEndLoop || instruction.opcode == kOpEndSwitch)
            --depth;
        if (!IsSampleOpcode(instruction.opcode))
            continue;
        std::size_t cursor = instruction.operandStart;
        const std::size_t destinationAt = cursor;
        Operand destination, coordinate, resource, sampler;
        if (!ParseOperand(words, cursor, end, destination, error))
            return fail(std::move(error));
        const std::size_t coordinateAt = cursor;
        if (!ParseOperand(words, cursor, end, coordinate, error))
            return fail(std::move(error));
        const std::size_t resourceAt = cursor;
        if (!ParseOperand(words, cursor, end, resource, error))
            return fail(std::move(error));
        const std::size_t samplerAt = cursor;
        if (instruction.opcode == kOpSample && !ParseOperand(words, cursor, end, sampler, error))
            return fail(std::move(error));
        std::uint32_t slot = 0;
        if (!GetImmediateRegister(resource, kOperandResource, slot) || (slot != kLightmapSlot0 && slot != kLightmapSlot1))
            continue;
        if (instruction.opcode != kOpSample)
            return fail("lightmap texture is read with a sample variant other than sample");
        if (depth != 0)
            return fail("lightmap sample inside control flow");
        const auto dimension = resourceDimensions.find(slot);
        if (dimension == resourceDimensions.end() || dimension->second != kResourceDimTexture2D)
            return fail("lightmap texture is not a declared texture2d");
        LightmapSample sample;
        sample.instruction = index;
        sample.slot = slot;
        std::uint32_t destinationRegister = 0;
        if (!GetImmediateRegister(destination, kOperandTemp, destinationRegister) || destination.componentMask != 0xf)
            return fail("lightmap sample destination is not a full temp register");
        sample.destination = destinationRegister;
        const std::uint32_t coordinateToken = words[coordinateAt];
        std::uint32_t coordinateRegister = 0;
        if ((coordinateToken & 0x80000000u) != 0 || ((coordinateToken >> 2) & 3) != 1 ||
            !GetImmediateRegister(coordinate, kOperandInput, coordinateRegister))
            return fail("lightmap coordinate is not an unmodified input register swizzle");
        sample.coordReg = coordinateRegister;
        sample.coordX = (coordinateToken >> 4) & 3;
        sample.coordY = (coordinateToken >> 6) & 3;
        sample.resource.assign(words.begin() + resourceAt, words.begin() + samplerAt);
        sample.sampler.assign(words.begin() + samplerAt, words.begin() + end);
        if (sample.resource.size() != 2 || sample.resource[0] != MakeSwizzleOperand(kOperandResource, 0, 1, 2, 3) ||
            sample.sampler.size() != 2 || sample.sampler[0] != ((kOperandSampler << 12) | (1u << 20)))
            return fail("lightmap texture or sampler operand has an unexpected encoding");
        (void)destinationAt;
        for (const LightmapSample& other : samples)
            if (other.slot == slot)
                return fail("lightmap texture is sampled more than once");
        samples.push_back(std::move(sample));
    }
    if (samples.empty())
        return fail("shader does not sample the lightmap textures");
    if (!hasTempDeclaration)
        return fail("shader has no dcl_temps");
    if (samples.size() == 2 && (samples[0].coordReg != samples[1].coordReg || samples[0].coordX != samples[1].coordX || samples[0].coordY != samples[1].coordY))
        return fail("t9 and t10 are sampled at different coordinates");
    // The size query and the tap coordinates come from t9. A shader that only samples t10 is not part of the stock corpus.
    if (samples[0].slot != kLightmapSlot0 && (samples.size() < 2 || samples[1].slot != kLightmapSlot0))
        return fail("the size source t9 is not sampled");
    const LightmapSample* sizeSource = samples[0].slot == kLightmapSlot0 ? &samples[0] : &samples[1];
    if (tempCount > 4096 - kBicubicTemps)
        return fail("shader has no room for the bicubic temporaries");
    report.oldTempCount = tempCount;
    report.addedTemps = kBicubicTemps;
    report.texturesPatched = static_cast<std::uint32_t>(samples.size());

    std::size_t firstSampleInstruction = samples[0].instruction;
    for (const LightmapSample& sample : samples)
        firstSampleInstruction = std::min(firstSampleInstruction, sample.instruction);
    std::uint32_t floatOps = 0, taps = 0, added = 0, removed = 0;
    Words patched;
    patched.reserve(words.size() + 160);
    patched.push_back(words[0]);
    patched.push_back(0);
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        const Instruction& instruction = instructions[index];
        const LightmapSample* sample = nullptr;
        for (const LightmapSample& candidate : samples)
            if (candidate.instruction == index)
                sample = &candidate;
        if (index == firstSampleInstruction) {
            LightmapSample sizeCopy = *sizeSource;
            sizeCopy.coordReg = samples[0].coordReg;
            sizeCopy.coordX = samples[0].coordX;
            sizeCopy.coordY = samples[0].coordY;
            const std::uint32_t n = EmitBicubicSetup(patched, tempCount, sizeCopy);
            added += n;
            floatOps += n - 1;      // everything but the resinfo
        }
        if (sample) {
            const std::uint32_t n = EmitBicubicTaps(patched, tempCount, *sample);
            added += n;
            taps += 4;
            floatOps += n - 4;
            ++removed;
            continue;
        }
        const std::size_t copiedStart = patched.size();
        for (std::size_t i = instruction.start; i < instruction.start + instruction.length; ++i)
            patched.push_back(words[i]);
        if (instruction.opcode == kOpDclTemps)
            patched[copiedStart + 1] = tempCount + kBicubicTemps;
    }
    patched[1] = static_cast<std::uint32_t>(patched.size());
    Chunk& shaderChunk = chunks[shaderIndex];
    shaderChunk.payload.resize(patched.size() * 4);
    for (std::size_t i = 0; i < patched.size(); ++i)
        Write32(shaderChunk.payload.data() + i * 4, patched[i]);
    report.addedInstructionCount = static_cast<std::int32_t>(added) - static_cast<std::int32_t>(removed);

    const std::size_t statIndex = FindChunk(chunks, "STAT");
    if (statIndex != chunks.size()) {
        std::vector<std::uint8_t>& stats = chunks[statIndex].payload;
        if (stats.size() >= 16 * 4 && stats.size() % 4 == 0) {
            Write32(stats.data(), static_cast<std::uint32_t>(static_cast<std::int64_t>(Read32(stats.data())) + report.addedInstructionCount));
            Write32(stats.data() + 4, tempCount + kBicubicTemps);
            Write32(stats.data() + 16, Read32(stats.data() + 16) + floatOps);
            Write32(stats.data() + 14 * 4, Read32(stats.data() + 14 * 4) - removed);   // TextureNormalInstructions
            Write32(stats.data() + 15 * 4, Read32(stats.data() + 15 * 4) + taps);      // sample_l is counted with the loads
        }
    }
    if (!BuildContainer(chunks, containerVersion, output, error))
        return fail(std::move(error));
    if (!RecomputeChecksum(output, error))
        return fail(std::move(error));
    return true;
}

} // namespace dxbc
