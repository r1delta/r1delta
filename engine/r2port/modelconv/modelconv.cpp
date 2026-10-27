// Origin: C:\Users\r3muxd\tmp\r2port\modelconv. R2 static model converter bundled in r1delta under GPL-3.0.
// Copyright (c) 2022, rexx
// Copyright (c) 2023, rexx
// Licensed under GPL-3.0-only.
// Adapted from rmdlconv/src/studio/model/mdl_52.cpp, mdl_53.cpp, studio.h,
// and optimize.h. Modified 2026-10-26 for the reverse static-model path,
// bounded parsing, and structural verification.
#include "modelconv.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace r2port::modelconv {
namespace {

constexpr std::int32_t kIDST = 0x54534449;
constexpr std::int32_t kIDSV = 0x56534449;
constexpr std::int32_t kIDCV = 0x56434449;
constexpr std::int32_t kModelVersionR1 = 52;
constexpr std::int32_t kModelVersionR2 = 53;
constexpr std::string_view kModelMaterialDirectory = "r2m\\";
constexpr std::int32_t kVertexVersion = 4;
constexpr std::int32_t kVertexColorVersion = 1;
constexpr std::int32_t kOptimizedModelVersion = 7;
constexpr std::uint32_t kStudioStaticProp = 0x10;
constexpr std::uint32_t kStudioExtraWeightsOrFlexes = 0x4000;
constexpr std::uint32_t kStudioSubdivisionSurface = 0x80000;
constexpr std::uint32_t kStudioVVC = 0x01000000;
constexpr std::uint32_t kStudioUV2 = 0x02000000;
constexpr std::uint32_t kR2OnlyFlag = 0x00800000;
constexpr std::uint32_t kR1StudioFlagMask = 0x033fffff;
constexpr std::size_t kMaxRecords = 1u << 20;

struct ConvertFailure final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void Require(bool condition, const std::string& message) {
    if (!condition) throw ConvertFailure(message);
}

bool Range(std::size_t size, std::int64_t offset, std::uint64_t bytes) {
    return offset >= 0 && static_cast<std::uint64_t>(offset) <= size &&
           bytes <= static_cast<std::uint64_t>(size) - static_cast<std::uint64_t>(offset);
}

std::uint64_t ArrayBytes(std::int32_t count, std::size_t elementSize, std::string_view label) {
    Require(count >= 0, std::string(label) + " count is negative");
    Require(static_cast<std::size_t>(count) <= kMaxRecords,
            std::string(label) + " count exceeds the converter limit");
    return static_cast<std::uint64_t>(count) * elementSize;
}

template <class T>
T ReadObject(std::span<const std::uint8_t> bytes, std::int64_t offset, std::string_view label) {
    static_assert(std::is_trivially_copyable_v<T>);
    Require(Range(bytes.size(), offset, sizeof(T)), std::string(label) + " is outside its input buffer");
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

template <class T>
void WriteObject(std::vector<std::uint8_t>& bytes, std::size_t offset, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    Require(Range(bytes.size(), static_cast<std::int64_t>(offset), sizeof(T)), "internal output write is out of range");
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

std::string ReadCString(std::span<const std::uint8_t> bytes, std::int64_t offset, std::string_view label) {
    Require(offset >= 0 && static_cast<std::uint64_t>(offset) < bytes.size(),
            std::string(label) + " string offset is outside its buffer");
    const char* begin = reinterpret_cast<const char*>(bytes.data() + offset);
    const std::size_t remaining = bytes.size() - static_cast<std::size_t>(offset);
    const void* terminator = std::memchr(begin, '\0', remaining);
    Require(terminator != nullptr, std::string(label) + " string is not NUL-terminated");
    return std::string(begin, static_cast<const char*>(terminator) - begin);
}

#pragma pack(push, 4)
struct Vec3 { float v[3]; };
struct Quaternion { float v[4]; };
struct Euler { float v[3]; };
struct Matrix3x4 { float v[12]; };

struct Header53 {
    std::int32_t id, version, checksum, nameOffset;
    char name[64];
    std::int32_t length;
    Vec3 eyePosition, illumPosition, hullMin, hullMax, viewMin, viewMax;
    std::int32_t flags;
    std::int32_t numBones, boneIndex;
    std::int32_t numBoneControllers, boneControllerIndex;
    std::int32_t numHitboxSets, hitboxSetIndex;
    std::int32_t numLocalAnim, localAnimIndex;
    std::int32_t numLocalSeq, localSeqIndex;
    std::int32_t activityListVersion, eventsIndexed;
    std::int32_t numTextures, textureIndex;
    std::int32_t numCdTextures, cdTextureIndex;
    std::int32_t numSkinRef, numSkinFamilies, skinIndex;
    std::int32_t numBodyParts, bodyPartIndex;
    std::int32_t numLocalAttachments, localAttachmentIndex;
    std::int32_t numLocalNodes, localNodeIndex, localNodeNameIndex;
    std::int32_t numFlexDesc, flexDescIndex;
    std::int32_t numFlexControllers, flexControllerIndex;
    std::int32_t numFlexRules, flexRuleIndex;
    std::int32_t numIkChains, ikChainIndex;
    std::int32_t uiPanelCount, uiPanelOffset;
    std::int32_t numLocalPoseParameters, localPoseParamIndex;
    std::int32_t surfacePropIndex;
    std::int32_t keyValueIndex, keyValueSize;
    std::int32_t numLocalIkAutoPlayLocks, localIkAutoPlayLockIndex;
    float mass;
    std::int32_t contents;
    std::int32_t numIncludeModels, includeModelIndex;
    std::int32_t virtualModel;
    std::int32_t boneTableByNameIndex;
    std::uint8_t constantDirectionalLightDot, rootLOD, numAllowedRootLODs, unusedByte;
    float defaultFadeDist;
    std::int32_t numFlexControllerUI, flexControllerUIIndex;
    float vertAnimFixedPointScale;
    std::int32_t surfacePropLookup;
    std::int32_t sourceFilenameOffset;
    std::int32_t numSrcBoneTransform, srcBoneTransformIndex;
    std::int32_t illumPositionAttachmentIndex;
    std::int32_t linearBoneIndex;
    std::int32_t numBoneFlexDrivers, boneFlexDriverIndex;
    std::int32_t perTriIndex, perTriNodeCount, perTriLeafCount, perTriVertCount;
    std::int32_t unknownStringOffset;
    std::int32_t vtxOffset, vvdOffset, vvcOffset, phyOffset;
    std::int32_t vtxSize, vvdSize, vvcSize, phySize;
    std::int32_t unknownOffset, unknownCount;
    std::int32_t boneFollowerCount, boneFollowerOffset;
    std::int32_t unused[60];
};

struct Header52 {
    std::int32_t id, version, checksum;
    char name[64];
    std::int32_t length;
    Vec3 eyePosition, illumPosition, hullMin, hullMax, viewMin, viewMax;
    std::int32_t flags;
    std::int32_t numBones, boneIndex;
    std::int32_t numBoneControllers, boneControllerIndex;
    std::int32_t numHitboxSets, hitboxSetIndex;
    std::int32_t numLocalAnim, localAnimIndex;
    std::int32_t numLocalSeq, localSeqIndex;
    std::int32_t activityListVersion, eventsIndexed;
    std::int32_t numTextures, textureIndex;
    std::int32_t numCdTextures, cdTextureIndex;
    std::int32_t numSkinRef, numSkinFamilies, skinIndex;
    std::int32_t numBodyParts, bodyPartIndex;
    std::int32_t numLocalAttachments, localAttachmentIndex;
    std::int32_t numLocalNodes, localNodeIndex, localNodeNameIndex;
    std::int32_t numFlexDesc, flexDescIndex;
    std::int32_t numFlexControllers, flexControllerIndex;
    std::int32_t numFlexRules, flexRuleIndex;
    std::int32_t numIkChains, ikChainIndex;
    std::int32_t numMouths, mouthIndex;
    std::int32_t numLocalPoseParameters, localPoseParamIndex;
    std::int32_t surfacePropIndex;
    std::int32_t keyValueIndex, keyValueSize;
    std::int32_t numLocalIkAutoPlayLocks, localIkAutoPlayLockIndex;
    float mass;
    std::int32_t contents;
    std::int32_t numIncludeModels, includeModelIndex;
    std::int32_t virtualModel;
    std::int32_t animBlockNameIndex;
    std::int32_t numAnimBlocks, animBlockIndex;
    std::int32_t animBlockModel;
    std::int32_t boneTableByNameIndex;
    std::int32_t vertexBase, indexBase;
    std::uint8_t constantDirectionalLightDot, rootLOD, numAllowedRootLODs, unusedByte;
    float defaultFadeDist;
    std::int32_t numFlexControllerUI, flexControllerUIIndex;
    float vertAnimFixedPointScale;
    std::int32_t surfacePropLookup;
    std::int32_t studioHdr2Index;
    std::int32_t sourceFilenameOffset;
    std::int32_t unused2;
};

struct Header2R1 {
    std::int32_t numSrcBoneTransform, srcBoneTransformIndex;
    std::int32_t illumPositionAttachmentIndex;
    float maxEyeDeflection;
    std::int32_t linearBoneIndex;
    std::int32_t nameIndex;
    std::int32_t numBoneFlexDrivers, boneFlexDriverIndex;
    std::int32_t perTriIndex, perTriNodeCount, perTriLeafCount, perTriVertCount;
    std::int32_t unknownStringOffset;
    std::int32_t reserved[39];
};

struct Bone53 {
    std::int32_t nameIndex, parent, boneController[6];
    Vec3 position;
    Quaternion quaternion;
    Euler rotation;
    Vec3 scale, positionScale, rotationScale, scaleScale;
    Matrix3x4 poseToBone;
    Quaternion alignment;
    std::int32_t flags, procedureType, procedureIndex, physicsBone;
    std::int32_t surfacePropIndex, contents, surfacePropLookup;
    std::int16_t unknownIndex, unknownCount;
    std::int32_t unused[7];
};

struct Bone52 {
    std::int32_t nameIndex, parent, boneController[6];
    Vec3 position;
    Quaternion quaternion;
    Euler rotation;
    Vec3 positionScale, rotationScale;
    Matrix3x4 poseToBone;
    Quaternion alignment;
    std::int32_t flags, procedureType, procedureIndex, physicsBone;
    std::int32_t surfacePropIndex, contents, surfacePropLookup;
    Vec3 scale, scaleScale;
    std::int32_t unused;
};

struct HitboxSet { std::int32_t nameIndex, numHitboxes, hitboxIndex; };
struct Hitbox53 {
    std::int32_t bone, group;
    Vec3 min, max;
    std::int32_t nameIndex, critOverride, keyValueIndex, unused[6];
};
struct Hitbox52 {
    std::int32_t bone, group;
    Vec3 min, max;
    std::int32_t nameIndex, unused[8];
};
struct BodyPart { std::int32_t nameIndex, numModels, base, modelIndex; };
struct MeshVertexData { std::int32_t modelVertexData, numLODVertices[8]; };
struct Mesh53 {
    std::int32_t material, modelIndex, numVertices, vertexOffset;
    std::int32_t numFlexes, flexIndex, materialType, materialParam, meshID;
    Vec3 center;
    MeshVertexData vertexLODData;
    std::uint64_t unknownPointer;
    std::int32_t unused[6];
};
struct Mesh52 {
    std::int32_t material, modelIndex, numVertices, vertexOffset;
    std::int32_t numFlexes, flexIndex, materialType, materialParam, meshID;
    Vec3 center;
    MeshVertexData vertexLODData;
    std::int32_t unused[8];
};
struct Model53 {
    char name[64];
    std::int32_t type;
    float boundingRadius;
    std::int32_t numMeshes, meshIndex, numVertices, vertexIndex, tangentsIndex;
    std::int32_t numAttachments, attachmentIndex, numEyeballs, eyeballIndex;
    std::int32_t pad[4], colorIndex, uv2Index, unused[4];
};
struct Model52 {
    char name[64];
    std::int32_t type;
    float boundingRadius;
    std::int32_t numMeshes, meshIndex, numVertices, vertexIndex, tangentsIndex;
    std::int32_t numAttachments, attachmentIndex, numEyeballs, eyeballIndex;
    std::int32_t pad[4], colorIndex, uv2Index, unused[4];
};
struct Texture53 { std::int32_t nameIndex, unusedFlags, used, unused[8]; };
struct Texture52 {
    std::int32_t nameIndex, flags, used, unused1, material, clientMaterial, unused[10];
};
struct TextureDir { std::int32_t nameIndex; };
struct Attachment {
    std::int32_t nameIndex, flags, localBone;
    Matrix3x4 local;
    std::int32_t unused[8];
};
struct SourceBoneTransform { std::int32_t nameIndex; Matrix3x4 pre, post; };
struct LinearBone {
    std::int32_t numBones, flagsIndex, parentIndex, positionIndex, quaternionIndex;
    std::int32_t rotationIndex, poseToBoneIndex, positionScaleIndex, rotationScaleIndex;
    std::int32_t alignmentIndex, unused[6];
};
struct PerTriHeader {
    std::int16_t version, unknown;
    Vec3 min, max;
    std::int32_t unused[8];
};

struct VVDHeader {
    std::int32_t id, version, checksum, numLODs, numLODVertices[8];
    std::int32_t numFixups, fixupTableStart, vertexDataStart, tangentDataStart;
};
struct VVCHeader {
    std::int32_t id, version, checksum, numLODs, numLODVertices[8];
    std::int32_t colorDataStart, uv2DataStart;
};

struct AnimDesc53 {
    std::int32_t basePtr, nameIndex;
    float fps;
    std::int32_t flags, numFrames, numMovements, movementIndex, frameMovementIndex;
    std::int32_t animIndex, numIkRules, ikRuleIndex, numLocalHierarchy, localHierarchyIndex;
    std::int32_t sectionIndex, sectionFrames, unused[8];
};
struct Sequence53 {
    std::int32_t basePtr, labelIndex, activityNameIndex, flags, activity, activityWeight;
    std::int32_t numEvents, eventIndex;
    Vec3 bbMin, bbMax;
    std::int32_t numBlends, animIndexIndex, movementIndex, groupSize[2], paramIndex[2];
    float paramStart[2], paramEnd[2];
    std::int32_t paramParent;
    float fadeInTime, fadeOutTime;
    std::int32_t localEntryNode, localExitNode, nodeFlags;
    float entryPhase, exitPhase, lastFrame;
    std::int32_t nextSequence, pose, numIkRules, numAutoLayers, autoLayerIndex;
    std::int32_t weightListIndex, poseKeyIndex, numIkLocks, ikLockIndex;
    std::int32_t keyValueIndex, keyValueSize, cyclePoseIndex, activityModifierIndex;
    std::int32_t numActivityModifiers, ikResetMask, unknown1, unused[8];
};
struct VVDVertex {
    float weights[3];
    std::uint8_t bones[3];
    std::int8_t numBones;
    Vec3 position, normal;
    float texcoord[2];
};
struct VVDFixup { std::int32_t lod, sourceVertexID, numVertices; };

#pragma pack(push, 1)
struct VTXHeader {
    std::int32_t version, vertexCacheSize;
    std::uint16_t maxBonesPerStrip, maxBonesPerFace;
    std::int32_t maxBonesPerVertex, checksum, numLODs, materialReplacementListOffset;
    std::int32_t numBodyParts, bodyPartOffset;
};
struct VTXVertex {
    std::uint8_t boneWeightIndex[3], numBones;
    std::uint16_t originalMeshVertexID;
    std::int8_t boneID[3];
};
struct VTXStrip {
    std::int32_t numIndices, indexOffset, numVertices, vertexOffset;
    std::int16_t numBones;
    std::uint8_t flags;
    std::int32_t numBoneStateChanges, boneStateChangeOffset;
    std::int32_t numTopologyIndices, topologyOffset;
};
struct VTXStripGroup {
    std::int32_t numVertices, vertexOffset, numIndices, indexOffset, numStrips, stripOffset;
    std::uint8_t flags;
    std::int32_t numTopologyIndices, topologyOffset;
};
struct VTXMesh { std::int32_t numStripGroups, stripGroupOffset; std::uint8_t flags; };
struct VTXLOD { std::int32_t numMeshes, meshOffset; float switchPoint; };
struct VTXModel { std::int32_t numLODs, lodOffset; };
struct VTXBodyPart { std::int32_t numModels, modelOffset; };
struct VTXMaterialReplacementList { std::int32_t numReplacements, replacementOffset; };
struct VTXMaterialReplacement { std::int16_t materialID; std::int32_t nameOffset; };
#pragma pack(pop)
#pragma pack(pop)

static_assert(sizeof(Vec3) == 12);
static_assert(sizeof(Header53) == 716);
static_assert(sizeof(Header52) == 412);
static_assert(offsetof(Header52, studioHdr2Index) == 400);
static_assert(offsetof(Header52, sourceFilenameOffset) == 404);
static_assert(sizeof(Header2R1) == 208);
static_assert(sizeof(Bone53) == 244);
static_assert(sizeof(Bone52) == 216);
static_assert(sizeof(Hitbox53) == 68);
static_assert(sizeof(HitboxSet) == 12);
static_assert(sizeof(BodyPart) == 16);
static_assert(sizeof(MeshVertexData) == 36);
static_assert(sizeof(Model53) == 148);
static_assert(sizeof(Model52) == 148);
static_assert(sizeof(Mesh53) == 116);
static_assert(sizeof(Mesh52) == 116);
static_assert(sizeof(Texture53) == 44);
static_assert(sizeof(Texture52) == 64);
static_assert(sizeof(Attachment) == 92);
static_assert(sizeof(VVDHeader) == 64);
static_assert(sizeof(VVDVertex) == 48);
static_assert(sizeof(VVCHeader) == 56);
static_assert(sizeof(VTXHeader) == 36);
static_assert(sizeof(VTXVertex) == 9);
static_assert(sizeof(VTXStrip) == 35);
static_assert(sizeof(VTXStripGroup) == 33);
static_assert(sizeof(AnimDesc53) == 92);
static_assert(sizeof(Sequence53) == 232);

struct AnimDesc52 {
    std::int32_t basePtr, nameIndex;
    float fps;
    std::int32_t flags, numFrames, numMovements, movementIndex, ikRuleZeroFrameIndex, frameMovementIndex;
    std::int32_t unused1[4];
    std::int32_t animBlock, animIndex, numIkRules, ikRuleIndex, animBlockIkRuleIndex;
    std::int32_t numLocalHierarchy, localHierarchyIndex, sectionIndex, sectionFrames;
    std::int16_t zeroFrameSpan, zeroFrameCount;
    std::int32_t zeroFrameIndex;
    float zeroFrameStallTime;
};
struct Sequence52 {
    std::int32_t basePtr, labelIndex, activityNameIndex, flags, activity, activityWeight;
    std::int32_t numEvents, eventIndex;
    Vec3 bbMin, bbMax;
    std::int32_t numBlends, animIndexIndex, movementIndex, groupSize[2], paramIndex[2];
    float paramStart[2], paramEnd[2];
    std::int32_t paramParent;
    float fadeInTime, fadeOutTime;
    std::int32_t localEntryNode, localExitNode, nodeFlags;
    float entryPhase, exitPhase, lastFrame;
    std::int32_t nextSequence, pose, numIkRules, numAutoLayers, autoLayerIndex;
    std::int32_t weightListIndex, poseKeyIndex, numIkLocks, ikLockIndex;
    std::int32_t keyValueIndex, keyValueSize, cyclePoseIndex, activityModifierIndex;
    std::int32_t numActivityModifiers, ikResetMask, unknown1, unused[3];
};
static_assert(sizeof(AnimDesc52) == 100);
static_assert(sizeof(Sequence52) == 212);

void AppendAlignment(std::vector<std::uint8_t>& bytes, std::size_t alignment) {
    while ((bytes.size() % alignment) != 0) bytes.push_back(0);
}

std::int32_t AppendBytes(std::vector<std::uint8_t>& output, const void* data, std::size_t size, std::size_t alignment = 4) {
    AppendAlignment(output, alignment);
    Require(output.size() <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()),
            "converted model exceeds the 32-bit offset limit");
    const auto result = static_cast<std::int32_t>(output.size());
    if (size != 0) {
        const auto* begin = static_cast<const std::uint8_t*>(data);
        output.insert(output.end(), begin, begin + size);
    }
    return result;
}

std::int32_t AppendZeroBytes(std::vector<std::uint8_t>& output, std::size_t size, std::size_t alignment = 4) {
    AppendAlignment(output, alignment);
    Require(output.size() <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) &&
            size <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) - output.size(),
            "converted model exceeds the 32-bit offset limit");
    const auto result = static_cast<std::int32_t>(output.size());
    output.resize(output.size() + size, 0);
    return result;
}

std::int32_t AppendString(std::vector<std::uint8_t>& output, const std::string& value) {
    return AppendBytes(output, value.c_str(), value.size() + 1, 1);
}

void StoreRelativeString(std::vector<std::uint8_t>& output, std::size_t recordOffset,
                         std::size_t fieldOffset, const std::string& value) {
    const auto stringOffset = AppendString(output, value);
    const auto relative = static_cast<std::int64_t>(stringOffset) - static_cast<std::int64_t>(recordOffset);
    Require(relative >= std::numeric_limits<std::int32_t>::min() && relative <= std::numeric_limits<std::int32_t>::max(),
            "converted string-relative offset overflows");
    WriteObject(output, recordOffset + fieldOffset, static_cast<std::int32_t>(relative));
}

std::string SourceRecordString(std::span<const std::uint8_t> mdl, std::size_t recordOffset,
                               std::int32_t relative, std::string_view label, bool required = true) {
    if (relative == 0 && !required) return {};
    Require(relative != 0, std::string(label) + " has a null string offset");
    return ReadCString(mdl, static_cast<std::int64_t>(recordOffset) + relative, label);
}

std::string SourceGlobalString(std::span<const std::uint8_t> mdl, std::int32_t offset,
                               std::string_view label, bool required = true) {
    if (offset == 0 && !required) return {};
    Require(offset != 0, std::string(label) + " has a null string offset");
    return ReadCString(mdl, offset, label);
}

void CheckArray(std::span<const std::uint8_t> bytes, std::int32_t offset, std::int32_t count,
                std::size_t elementSize, std::string_view label, bool requiredWhenNonEmpty = true) {
    const auto total = ArrayBytes(count, elementSize, label);
    if (count == 0) return;
    Require(offset != 0 || !requiredWhenNonEmpty, std::string(label) + " has a null array offset");
    Require(Range(bytes.size(), offset, total), std::string(label) + " array is outside its buffer");
}

std::span<const std::uint8_t> EmbeddedOrExternal(std::span<const std::uint8_t> mdl,
                                                  std::int32_t offset, std::int32_t size,
                                                  std::span<const std::uint8_t> external,
                                                  std::string_view label) {
    Require(size >= 0, std::string(label) + " size is negative");
    if (size > 0) {
        Require(Range(mdl.size(), offset, static_cast<std::uint64_t>(size)),
                std::string(label) + " embedded section is outside the R2 MDL");
        return mdl.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
    }
    return external;
}

void CopyAndSetChecksum(std::span<const std::uint8_t> source, std::vector<std::uint8_t>& target,
                        std::size_t checksumOffset, std::int32_t expectedChecksum,
                        std::string_view label) {
    Require(!source.empty(), std::string(label) + " companion is missing");
    Require(Range(source.size(), 0, checksumOffset + sizeof(std::int32_t)),
            std::string(label) + " is too short to contain a checksum");
    const std::int32_t checksum = ReadObject<std::int32_t>(source, static_cast<std::int64_t>(checksumOffset), label);
    Require(checksum == expectedChecksum, std::string(label) + " checksum does not match the R2 MDL");
    target.assign(source.begin(), source.end());
    std::memcpy(target.data() + checksumOffset, &expectedChecksum, sizeof(expectedChecksum));
}

std::uint32_t MaxLODVertices(const VVDHeader& hdr) {
    std::uint32_t count = 0;
    for (std::int32_t i = 0; i < hdr.numLODs; ++i) {
        Require(hdr.numLODVertices[i] >= 0, "VVD LOD vertex count is negative");
        count = std::max(count, static_cast<std::uint32_t>(hdr.numLODVertices[i]));
    }
    return count;
}

VVDHeader ValidateVVD(std::span<const std::uint8_t> vvd, std::int32_t checksum) {
    const auto hdr = ReadObject<VVDHeader>(vvd, 0, "VVD header");
    Require(hdr.id == kIDSV && hdr.version == kVertexVersion,
            "unsupported VVD identifier or version; expected IDSV version 4");
    Require(hdr.checksum == checksum, "VVD checksum does not match MDL");
    Require(hdr.numLODs > 0 && hdr.numLODs <= 8, "VVD LOD count is outside 1..8");
    for (const auto count : hdr.numLODVertices)
        Require(count >= 0, "VVD LOD vertex count is negative");
    Require(hdr.numFixups >= 0, "VVD fixup count is negative");
    if (hdr.numFixups != 0) {
        Require(hdr.fixupTableStart >= static_cast<std::int32_t>(sizeof(VVDHeader)),
                "VVD fixup table overlaps its header");
        CheckArray(vvd, hdr.fixupTableStart, hdr.numFixups, sizeof(VVDFixup), "VVD fixup table");
    }
    const auto vertices = MaxLODVertices(hdr);
    Require(vertices != 0, "VVD has no vertices");
    Require(hdr.vertexDataStart >= static_cast<std::int32_t>(sizeof(VVDHeader)) &&
            Range(vvd.size(), hdr.vertexDataStart, static_cast<std::uint64_t>(vertices) * sizeof(VVDVertex)),
            "VVD vertex data is outside its buffer");
    Require(hdr.tangentDataStart >= 0 &&
            Range(vvd.size(), hdr.tangentDataStart, static_cast<std::uint64_t>(vertices) * 16),
            "VVD tangent data is outside its buffer");
    for (std::int32_t i = 0; i < hdr.numFixups; ++i) {
        const auto fix = ReadObject<VVDFixup>(vvd,
            hdr.fixupTableStart + static_cast<std::int64_t>(i) * sizeof(VVDFixup), "VVD fixup");
        Require(fix.lod >= 0 && fix.lod < hdr.numLODs &&
                fix.sourceVertexID >= 0 && fix.numVertices >= 0,
                "VVD fixup contains a negative or out-of-range field");
        Require(static_cast<std::uint64_t>(fix.sourceVertexID) +
                    static_cast<std::uint64_t>(fix.numVertices) <= vertices,
                "VVD fixup references vertices outside the vertex block");
    }
    for (std::uint32_t i = 0; i < vertices; ++i) {
        const auto vertex = ReadObject<VVDVertex>(vvd,
            hdr.vertexDataStart + static_cast<std::int64_t>(i) * sizeof(VVDVertex), "VVD vertex");
        Require(vertex.numBones >= 0 && vertex.numBones <= 3,
                "VVD vertex uses an unsupported bone influence count");
    }
    return hdr;
}

void ValidateVVC(std::span<const std::uint8_t> vvc, const VVDHeader& vvd,
                 std::int32_t checksum, std::uint32_t mdlFlags) {
    const auto hdr = ReadObject<VVCHeader>(vvc, 0, "VVC header");
    Require(hdr.id == kIDCV && hdr.version == kVertexColorVersion,
            "unsupported VVC identifier or version; expected IDCV version 1");
    Require(hdr.checksum == checksum, "VVC checksum does not match MDL");
    Require(hdr.numLODs == vvd.numLODs, "VVC LOD count does not match VVD");
    for (std::int32_t i = 0; i < 8; ++i) {
        Require(hdr.numLODVertices[i] >= 0, "VVC LOD vertex count is negative");
        Require(hdr.numLODVertices[i] == vvd.numLODVertices[i],
                "VVC LOD vertex counts do not match VVD");
    }
    const auto vertices = MaxLODVertices(vvd);
    if ((mdlFlags & kStudioVVC) != 0)
        Require(hdr.colorDataStart != 0, "MDL requires vertex colors but VVC has no color data");
    if ((mdlFlags & kStudioUV2) != 0)
        Require(hdr.uv2DataStart != 0, "MDL requires secondary UVs but VVC has no UV2 data");
    if (hdr.colorDataStart != 0) {
        Require(hdr.colorDataStart >= static_cast<std::int32_t>(sizeof(VVCHeader)) &&
                Range(vvc.size(), hdr.colorDataStart, static_cast<std::uint64_t>(vertices) * 4),
                "VVC color data is outside its buffer");
    }
    if (hdr.uv2DataStart != 0) {
        Require(hdr.uv2DataStart >= static_cast<std::int32_t>(sizeof(VVCHeader)) &&
                Range(vvc.size(), hdr.uv2DataStart, static_cast<std::uint64_t>(vertices) * 8),
                "VVC secondary-UV data is outside its buffer");
    }
}

void ValidatePHY(std::span<const std::uint8_t> phy) {
    const auto headerSize = ReadObject<std::int32_t>(phy, 0, "PHY header size");
    const auto solidCount = ReadObject<std::int32_t>(phy, 8, "PHY solid count");
    Require(headerSize == 16, "unsupported PHY header size; R1 requires 16");
    Require(solidCount > 0 && static_cast<std::size_t>(solidCount) <= kMaxRecords,
            "PHY solid count is invalid");
    std::size_t offset = 16;
    for (std::int32_t i = 0; i < solidCount; ++i) {
        const auto solidSize = ReadObject<std::int32_t>(phy, static_cast<std::int64_t>(offset), "PHY solid size");
        Require(solidSize > 0 && Range(phy.size(), static_cast<std::int64_t>(offset) + 4,
                                      static_cast<std::uint64_t>(solidSize)),
                "PHY solid is outside its companion file");
        const auto solidOffset = offset + 4;
        const auto magic = ReadObject<std::uint32_t>(phy, static_cast<std::int64_t>(solidOffset), "PHY solid format");
        if (magic == 0x59485056u) { // VPHY
            Require(solidSize >= 28, "VPHY solid is shorter than its header");
            const auto modelType = ReadObject<std::uint16_t>(phy,
                static_cast<std::int64_t>(solidOffset + 6), "VPHY model type");
            const auto surfaceSize = ReadObject<std::int32_t>(phy,
                static_cast<std::int64_t>(solidOffset + 8), "VPHY surface size");
            Require(modelType == 0 && surfaceSize >= 0 &&
                    static_cast<std::uint64_t>(surfaceSize) + 28 <= static_cast<std::uint64_t>(solidSize),
                    "unsupported VPHY model type or surface-data range");
        } else {
            Require(solidSize >= 48, "legacy PHY solid is shorter than its format tag");
            const auto tag = ReadObject<std::uint32_t>(phy,
                static_cast<std::int64_t>(solidOffset + 44), "legacy PHY format tag");
            Require(tag == 0 || tag == 0x53505649u || tag == 0x49565053u,
                    "unsupported legacy PHY solid format");
        }
        offset += 4 + static_cast<std::size_t>(solidSize);
    }
}
std::int64_t RelativeAbsolute(std::size_t base, std::int32_t relative, std::string_view label) {
    const auto result = static_cast<std::int64_t>(base) + relative;
    Require(result >= 0 && static_cast<std::uint64_t>(result) <= std::numeric_limits<std::int32_t>::max(),
            std::string(label) + " relative offset is invalid");
    return result;
}

std::int64_t ArrayOffset(std::span<const std::uint8_t> bytes, std::size_t base,
                         std::int32_t relative, std::int32_t count, std::size_t elementSize,
                         std::string_view label) {
    if (count != 0) Require(relative != 0, std::string(label) + " has a null relative offset");
    const auto offset = RelativeAbsolute(base, relative, label);
    CheckArray(bytes, static_cast<std::int32_t>(offset), count, elementSize, label);
    return offset;
}

// R1 and R2 both use optimized-model version 7. Its top-level header arrays
// are absolute, while each nested array offset is relative to its owning record.
void ValidateVTX(std::span<const std::uint8_t> vtx, std::span<const std::uint8_t> mdlBytes,
                 const Header52& mdl) {
    const VTXHeader hdr = ReadObject<VTXHeader>(vtx, 0, "VTX header");
    Require(hdr.version == kOptimizedModelVersion, "unsupported VTX version; expected version 7");
    Require(hdr.checksum == mdl.checksum, "VTX checksum does not match MDL");
    Require(hdr.numLODs > 0 && hdr.numLODs <= 8, "VTX LOD count is outside 1..8");
    Require(hdr.numBodyParts == mdl.numBodyParts, "VTX bodypart count does not match MDL");
    CheckArray(vtx, hdr.bodyPartOffset, hdr.numBodyParts, sizeof(VTXBodyPart), "VTX bodypart array");

    if (hdr.materialReplacementListOffset != 0) {
        CheckArray(vtx, hdr.materialReplacementListOffset, hdr.numLODs, sizeof(VTXMaterialReplacementList),
                   "VTX material replacement lists");
        for (std::int32_t lodIndex = 0; lodIndex < hdr.numLODs; ++lodIndex) {
            const auto listOffset = static_cast<std::size_t>(hdr.materialReplacementListOffset) +
                                    static_cast<std::size_t>(lodIndex) * sizeof(VTXMaterialReplacementList);
            const auto list = ReadObject<VTXMaterialReplacementList>(vtx, listOffset, "VTX material replacement list");
            const auto entriesOffset = ArrayOffset(vtx, listOffset, list.replacementOffset, list.numReplacements,
                                                    sizeof(VTXMaterialReplacement), "VTX material replacements");
            for (std::int32_t i = 0; i < list.numReplacements; ++i) {
                const auto entryOffset = static_cast<std::size_t>(entriesOffset) +
                                         static_cast<std::size_t>(i) * sizeof(VTXMaterialReplacement);
                const auto entry = ReadObject<VTXMaterialReplacement>(vtx, entryOffset, "VTX material replacement");
                if (entry.nameOffset != 0)
                    (void)ReadCString(vtx, RelativeAbsolute(entryOffset, entry.nameOffset, "VTX material replacement name"),
                                      "VTX material replacement name");
            }
        }
    }

    for (std::int32_t bodyIndex = 0; bodyIndex < hdr.numBodyParts; ++bodyIndex) {
        const auto vtxBodyOffset = static_cast<std::size_t>(hdr.bodyPartOffset) +
                                   static_cast<std::size_t>(bodyIndex) * sizeof(VTXBodyPart);
        const auto vtxBody = ReadObject<VTXBodyPart>(vtx, vtxBodyOffset, "VTX bodypart");
        const auto mdlBodyOffset = static_cast<std::size_t>(mdl.bodyPartIndex) +
                                   static_cast<std::size_t>(bodyIndex) * sizeof(BodyPart);
        const auto mdlBody = ReadObject<BodyPart>(mdlBytes, mdlBodyOffset, "R1 bodypart");
        Require(vtxBody.numModels == mdlBody.numModels, "VTX model count does not match MDL bodypart");
        const auto vtxModelsOffset = ArrayOffset(vtx, vtxBodyOffset, vtxBody.modelOffset, vtxBody.numModels,
                                                 sizeof(VTXModel), "VTX model array");
        const auto mdlModelsOffset = RelativeAbsolute(mdlBodyOffset, mdlBody.modelIndex, "R1 model array");
        CheckArray(mdlBytes, static_cast<std::int32_t>(mdlModelsOffset), mdlBody.numModels, sizeof(Model52),
                   "R1 model array");

        for (std::int32_t modelIndex = 0; modelIndex < vtxBody.numModels; ++modelIndex) {
            const auto vtxModelOffset = static_cast<std::size_t>(vtxModelsOffset) +
                                        static_cast<std::size_t>(modelIndex) * sizeof(VTXModel);
            const auto vtxModel = ReadObject<VTXModel>(vtx, vtxModelOffset, "VTX model");
            const auto mdlModelOffset = static_cast<std::size_t>(mdlModelsOffset) +
                                        static_cast<std::size_t>(modelIndex) * sizeof(Model52);
            const auto mdlModel = ReadObject<Model52>(mdlBytes, mdlModelOffset, "R1 model");
            Require(vtxModel.numLODs >= 0 && vtxModel.numLODs <= hdr.numLODs,
                    "VTX model declares an invalid LOD count");
            const auto lodsOffset = ArrayOffset(vtx, vtxModelOffset, vtxModel.lodOffset, vtxModel.numLODs,
                                                sizeof(VTXLOD), "VTX LOD array");
            const auto meshesOffset = RelativeAbsolute(mdlModelOffset, mdlModel.meshIndex, "R1 mesh array");
            CheckArray(mdlBytes, static_cast<std::int32_t>(meshesOffset), mdlModel.numMeshes, sizeof(Mesh52),
                       "R1 mesh array");

            for (std::int32_t lodIndex = 0; lodIndex < vtxModel.numLODs; ++lodIndex) {
                const auto lodOffset = static_cast<std::size_t>(lodsOffset) +
                                       static_cast<std::size_t>(lodIndex) * sizeof(VTXLOD);
                const auto lod = ReadObject<VTXLOD>(vtx, lodOffset, "VTX LOD");
                Require(lod.numMeshes >= 0 && lod.numMeshes <= mdlModel.numMeshes,
                        "VTX LOD mesh count exceeds its MDL model");
                const auto vtxMeshesOffset = ArrayOffset(vtx, lodOffset, lod.meshOffset, lod.numMeshes,
                                                         sizeof(VTXMesh), "VTX mesh array");

                for (std::int32_t meshIndex = 0; meshIndex < lod.numMeshes; ++meshIndex) {
                    const auto vtxMeshOffset = static_cast<std::size_t>(vtxMeshesOffset) +
                                               static_cast<std::size_t>(meshIndex) * sizeof(VTXMesh);
                    const auto vtxMesh = ReadObject<VTXMesh>(vtx, vtxMeshOffset, "VTX mesh");
                    const auto mdlMeshOffset = static_cast<std::size_t>(meshesOffset) +
                                               static_cast<std::size_t>(meshIndex) * sizeof(Mesh52);
                    const auto mdlMesh = ReadObject<Mesh52>(mdlBytes, mdlMeshOffset, "R1 mesh");
                    const auto groupsOffset = ArrayOffset(vtx, vtxMeshOffset, vtxMesh.stripGroupOffset,
                                                          vtxMesh.numStripGroups, sizeof(VTXStripGroup),
                                                          "VTX stripgroup array");

                    for (std::int32_t groupIndex = 0; groupIndex < vtxMesh.numStripGroups; ++groupIndex) {
                        const auto groupOffset = static_cast<std::size_t>(groupsOffset) +
                                                 static_cast<std::size_t>(groupIndex) * sizeof(VTXStripGroup);
                        const auto group = ReadObject<VTXStripGroup>(vtx, groupOffset, "VTX stripgroup");
                        Require((group.flags & 0x05) == 0, "VTX stripgroup contains unsupported flex data");
                        const auto verticesOffset = ArrayOffset(vtx, groupOffset, group.vertexOffset, group.numVertices,
                                                                sizeof(VTXVertex), "VTX stripgroup vertices");
                        const auto indicesOffset = ArrayOffset(vtx, groupOffset, group.indexOffset, group.numIndices,
                                                               sizeof(std::uint16_t), "VTX stripgroup indices");
                        const auto stripsOffset = ArrayOffset(vtx, groupOffset, group.stripOffset, group.numStrips,
                                                              sizeof(VTXStrip), "VTX strips");
                        const auto topologyOffset = ArrayOffset(vtx, groupOffset, group.topologyOffset,
                                                               group.numTopologyIndices, sizeof(std::uint16_t),
                                                               "VTX topology indices");

                        for (std::int32_t i = 0; i < group.numIndices; ++i) {
                            const auto index = ReadObject<std::uint16_t>(vtx,
                                indicesOffset + static_cast<std::int64_t>(i) * sizeof(std::uint16_t), "VTX index");
                            Require(index < group.numVertices, "VTX index exceeds stripgroup vertex count");
                        }
                        for (std::int32_t i = 0; i < group.numVertices; ++i) {
                            const auto vertex = ReadObject<VTXVertex>(vtx,
                                verticesOffset + static_cast<std::int64_t>(i) * sizeof(VTXVertex), "VTX vertex");
                            Require(vertex.numBones <= 3, "VTX vertex uses more than three bones");
                            Require(vertex.originalMeshVertexID < mdlMesh.numVertices,
                                    "VTX vertex references outside its MDL mesh");
                        }
                        for (std::int32_t i = 0; i < group.numStrips; ++i) {
                            const auto stripOffset = static_cast<std::size_t>(stripsOffset) +
                                                     static_cast<std::size_t>(i) * sizeof(VTXStrip);
                            const auto strip = ReadObject<VTXStrip>(vtx, stripOffset, "VTX strip");
                            Require(strip.numIndices >= 0 && strip.indexOffset >= 0 &&
                                    static_cast<std::uint64_t>(strip.indexOffset) +
                                        static_cast<std::uint64_t>(strip.numIndices) <=
                                        static_cast<std::uint64_t>(group.numIndices),
                                    "VTX strip index range exceeds its stripgroup");
                            Require(strip.numVertices >= 0 && strip.vertexOffset >= 0 &&
                                    static_cast<std::uint64_t>(strip.vertexOffset) +
                                        static_cast<std::uint64_t>(strip.numVertices) <=
                                        static_cast<std::uint64_t>(group.numVertices),
                                    "VTX strip vertex range exceeds its stripgroup");
                            Require(strip.numBones >= 0 && strip.numBoneStateChanges >= 0 &&
                                    strip.numTopologyIndices >= 0, "VTX strip has a negative count");
                            ArrayOffset(vtx, stripOffset, strip.boneStateChangeOffset,
                                        strip.numBoneStateChanges, 8, "VTX bone-state changes");
                            if (strip.numTopologyIndices != 0) {
                                Require(strip.topologyOffset >= 0 &&
                                        static_cast<std::uint64_t>(strip.topologyOffset) +
                                            static_cast<std::uint64_t>(strip.numTopologyIndices) <=
                                            static_cast<std::uint64_t>(group.numTopologyIndices),
                                        "VTX strip topology range exceeds its stripgroup");
                            }
                        }
                        (void)topologyOffset;
                    }
                }
            }
        }
    }
}

std::int32_t AppendMappedBone(std::span<const std::uint8_t> source, std::size_t sourceIndex,
                              std::vector<std::uint8_t>& output, std::int32_t destinationIndex, bool degraded) {
    const Bone53 in = ReadObject<Bone53>(source, sourceIndex, "R2 bone");
    Require(degraded || in.procedureType == 0, "unsupported procedural bone; static conversion accepts only ordinary bones");
    Bone52 out{};
    out.parent = in.parent;
    if (degraded)
        std::fill(std::begin(out.boneController), std::end(out.boneController), -1);
    else
        std::copy(std::begin(in.boneController), std::end(in.boneController), std::begin(out.boneController));
    out.position = in.position;
    out.quaternion = in.quaternion;
    out.rotation = in.rotation;
    out.positionScale = in.positionScale;
    out.rotationScale = in.rotationScale;
    out.poseToBone = in.poseToBone;
    out.scale = in.scale;
    out.scaleScale = in.scaleScale;
    out.alignment = in.alignment;
    out.flags = degraded ? (in.flags & ~0x4) : in.flags;   // BONE_ALWAYS_PROCEDURAL needs procedure data
    out.procedureType = 0;
    out.procedureIndex = 0;
    out.physicsBone = in.physicsBone;
    out.contents = in.contents;
    out.surfacePropLookup = 0;
    WriteObject(output, static_cast<std::size_t>(destinationIndex), out);
    StoreRelativeString(output, static_cast<std::size_t>(destinationIndex), offsetof(Bone52, nameIndex),
                        SourceRecordString(source, sourceIndex, in.nameIndex, "bone name"));
    StoreRelativeString(output, static_cast<std::size_t>(destinationIndex), offsetof(Bone52, surfacePropIndex),
                        SourceRecordString(source, sourceIndex, in.surfacePropIndex, "bone surface property", false));
    return destinationIndex;
}

void AppendMappedHitboxes(std::span<const std::uint8_t> source, const Header53& in,
                          std::vector<std::uint8_t>& output, Header52& out) {
    if (in.numHitboxSets == 0) {
        out.hitboxSetIndex = 0;
        return;
    }
    CheckArray(source, in.hitboxSetIndex, in.numHitboxSets, sizeof(HitboxSet), "R2 hitbox-set array");
    const auto setsOffset = AppendZeroBytes(output, static_cast<std::size_t>(in.numHitboxSets) * sizeof(HitboxSet));
    out.hitboxSetIndex = setsOffset;
    for (std::int32_t i = 0; i < in.numHitboxSets; ++i) {
        const auto srcSetOffset = static_cast<std::size_t>(in.hitboxSetIndex) + static_cast<std::size_t>(i) * sizeof(HitboxSet);
        const auto srcSet = ReadObject<HitboxSet>(source, srcSetOffset, "R2 hitbox set");
        const auto dstSetOffset = static_cast<std::size_t>(setsOffset) + static_cast<std::size_t>(i) * sizeof(HitboxSet);
        HitboxSet dstSet{0, srcSet.numHitboxes, 0};
        const auto sourceBoxesOffset = ArrayOffset(source, srcSetOffset, srcSet.hitboxIndex,
                                                   srcSet.numHitboxes, sizeof(Hitbox53), "R2 hitbox array");
        WriteObject(output, dstSetOffset, dstSet);
        StoreRelativeString(output, dstSetOffset, offsetof(HitboxSet, nameIndex),
                            SourceRecordString(source, srcSetOffset, srcSet.nameIndex, "hitbox-set name"));
        dstSet.nameIndex = ReadObject<std::int32_t>(output, dstSetOffset + offsetof(HitboxSet, nameIndex), "hitbox-set name offset");
        if (srcSet.numHitboxes == 0) continue;
        const auto dstBoxesOffset = AppendZeroBytes(output, static_cast<std::size_t>(srcSet.numHitboxes) * sizeof(Hitbox52));
        dstSet.hitboxIndex = static_cast<std::int32_t>(dstBoxesOffset - dstSetOffset);
        WriteObject(output, dstSetOffset, dstSet);
        for (std::int32_t j = 0; j < srcSet.numHitboxes; ++j) {
            const auto srcBoxOffset = static_cast<std::size_t>(sourceBoxesOffset) +
                                      static_cast<std::size_t>(j) * sizeof(Hitbox53);
            const auto srcBox = ReadObject<Hitbox53>(source, srcBoxOffset, "R2 hitbox");
            Hitbox52 dstBox{};
            dstBox.bone = srcBox.bone;
            dstBox.group = srcBox.group;
            dstBox.min = srcBox.min;
            dstBox.max = srcBox.max;
            const auto dstBoxOffset = static_cast<std::size_t>(dstBoxesOffset) + static_cast<std::size_t>(j) * sizeof(Hitbox52);
            WriteObject(output, dstBoxOffset, dstBox);
            StoreRelativeString(output, dstBoxOffset, offsetof(Hitbox52, nameIndex),
                                SourceRecordString(source, srcBoxOffset, srcBox.nameIndex, "hitbox name", false));
        }
    }
}

void AppendMappedTextures(std::span<const std::uint8_t> source, const Header53& in,
                          std::vector<std::uint8_t>& output, Header52& out) {
    if (in.numTextures == 0) {
        out.textureIndex = 0;
    } else {
        CheckArray(source, in.textureIndex, in.numTextures, sizeof(Texture53), "R2 texture array");
        const auto destination = AppendZeroBytes(output, static_cast<std::size_t>(in.numTextures) * sizeof(Texture52));
        out.textureIndex = destination;
        for (std::int32_t i = 0; i < in.numTextures; ++i) {
            const auto srcOffset = static_cast<std::size_t>(in.textureIndex) + static_cast<std::size_t>(i) * sizeof(Texture53);
            const auto src = ReadObject<Texture53>(source, srcOffset, "R2 texture");
            Texture52 dst{};
            dst.flags = src.unusedFlags;
            dst.used = src.used;
            const auto dstOffset = static_cast<std::size_t>(destination) + static_cast<std::size_t>(i) * sizeof(Texture52);
            WriteObject(output, dstOffset, dst);
            StoreRelativeString(output, dstOffset, offsetof(Texture52, nameIndex),
                                SourceRecordString(source, srcOffset, src.nameIndex, "texture name"));
        }
    }
    // Every material of a converted model is renamed into the "r2m/" namespace so the provider can
    // serve it with a model shader (an R2 world material is LightmappedGeneric, which crashes R1's
    // studio renderer on entities) without clobbering the world material of the same name.
    if (in.numCdTextures == 0) {
        const auto destination = AppendZeroBytes(output, sizeof(TextureDir));
        out.cdTextureIndex = destination;
        out.numCdTextures = 1;
        WriteObject(output, static_cast<std::size_t>(destination), AppendString(output, std::string(kModelMaterialDirectory)));
    } else {
        CheckArray(source, in.cdTextureIndex, in.numCdTextures, sizeof(TextureDir), "R2 texture-directory array");
        const auto destination = AppendZeroBytes(output, static_cast<std::size_t>(in.numCdTextures) * sizeof(TextureDir));
        out.cdTextureIndex = destination;
        for (std::int32_t i = 0; i < in.numCdTextures; ++i) {
            const auto src = ReadObject<TextureDir>(source, in.cdTextureIndex + static_cast<std::int64_t>(i) * sizeof(TextureDir), "R2 texture directory");
            auto text = SourceGlobalString(source, src.nameIndex, "texture-directory name");
            while (!text.empty() && (text.front() == '/' || text.front() == '\\')) text.erase(text.begin());
            const auto stringOffset = AppendString(output, std::string(kModelMaterialDirectory) + text);
            WriteObject(output, static_cast<std::size_t>(destination) + static_cast<std::size_t>(i) * sizeof(TextureDir), stringOffset);
        }
    }
}

// R1's client-side animating entities index sequence 0 unconditionally, so the converted model
// keeps the one-frame @ref pose and the ref sequence instead of dropping them.
void AppendMappedStaticAnimation(std::span<const std::uint8_t> source, const Header53& in,
                                 std::vector<std::uint8_t>& output, Header52& out, bool degraded) {
    const auto animSource = static_cast<std::size_t>(in.localAnimIndex);
    const auto seqSource = static_cast<std::size_t>(in.localSeqIndex);
    AnimDesc53 anim{};
    Sequence53 seq{};
    // Empty one-frame animation data: a record whose bone byte is 0xff ends the list.
    std::uint8_t sentinel[8]{};
    if (!degraded) {
        anim = ReadObject<AnimDesc53>(source, animSource, "R2 reference animation");
        seq = ReadObject<Sequence53>(source, seqSource, "R2 static reference sequence");
        const auto dataSource = RelativeAbsolute(animSource, anim.animIndex, "R2 reference animation data");
        const auto available = std::min<std::size_t>(sizeof(sentinel), source.size() - static_cast<std::size_t>(dataSource));
        std::memcpy(sentinel, source.data() + dataSource, available);
    } else {
        // The model has real animations (or a non-empty reference pose). R1 gets the same one-frame
        // empty @ref animation and `ref` sequence as a static prop: with no per-bone data every bone
        // takes its own rest position and rotation from the bone record, i.e. the bind pose.
        anim.fps = 30.0f;
        anim.numFrames = 1;
        sentinel[4] = 0xff;
        seq.flags = 0x80000;
        seq.activity = -1;
        seq.bbMin = in.hullMin;
        seq.bbMax = in.hullMax;
        seq.numBlends = 1;
        seq.groupSize[0] = seq.groupSize[1] = 1;
        seq.paramIndex[0] = seq.paramIndex[1] = -1;
        seq.fadeInTime = seq.fadeOutTime = 0.2f;
    }

    const auto animDestination = static_cast<std::size_t>(AppendZeroBytes(output, sizeof(AnimDesc52)));
    const auto seqDestination = static_cast<std::size_t>(AppendZeroBytes(output, sizeof(Sequence52)));
    out.numLocalAnim = 1;
    out.localAnimIndex = static_cast<std::int32_t>(animDestination);
    out.numLocalSeq = 1;
    out.localSeqIndex = static_cast<std::int32_t>(seqDestination);

    const auto dataDestination = AppendBytes(output, sentinel, sizeof(sentinel));

    AnimDesc52 animOut{};
    animOut.fps = anim.fps;
    animOut.flags = anim.flags;
    animOut.numFrames = anim.numFrames;
    animOut.animIndex = static_cast<std::int32_t>(dataDestination - static_cast<std::int32_t>(animDestination));
    WriteObject(output, animDestination, animOut);
    StoreRelativeString(output, animDestination, offsetof(AnimDesc52, nameIndex), "@ref");

    Sequence52 seqOut{};
    seqOut.flags = seq.flags;
    seqOut.activity = seq.activity;
    seqOut.activityWeight = seq.activityWeight;
    seqOut.bbMin = seq.bbMin;
    seqOut.bbMax = seq.bbMax;
    seqOut.numBlends = 1;
    seqOut.groupSize[0] = seqOut.groupSize[1] = 1;
    seqOut.paramIndex[0] = seq.paramIndex[0];
    seqOut.paramIndex[1] = seq.paramIndex[1];
    seqOut.paramStart[0] = seq.paramStart[0];
    seqOut.paramStart[1] = seq.paramStart[1];
    seqOut.paramEnd[0] = seq.paramEnd[0];
    seqOut.paramEnd[1] = seq.paramEnd[1];
    seqOut.paramParent = seq.paramParent;
    seqOut.fadeInTime = seq.fadeInTime;
    seqOut.fadeOutTime = seq.fadeOutTime;
    seqOut.localEntryNode = seq.localEntryNode;
    seqOut.localExitNode = seq.localExitNode;
    seqOut.nodeFlags = seq.nodeFlags;
    seqOut.entryPhase = seq.entryPhase;
    seqOut.exitPhase = seq.exitPhase;
    seqOut.lastFrame = seq.lastFrame;
    seqOut.nextSequence = seq.nextSequence;
    seqOut.pose = seq.pose;
    seqOut.cyclePoseIndex = seq.cyclePoseIndex;
    seqOut.ikResetMask = seq.ikResetMask;
    seqOut.unknown1 = seq.unknown1;

    const std::int16_t blend = 0;
    const auto blendDestination = AppendBytes(output, &blend, sizeof(blend), 2);
    seqOut.animIndexIndex = static_cast<std::int32_t>(blendDestination - static_cast<std::int32_t>(seqDestination));

    std::vector<float> weights(static_cast<std::size_t>(in.numBones), 1.0f);
    if (seq.weightListIndex != 0 && in.numBones > 0) {
        const auto weightsSource = ArrayOffset(source, seqSource, seq.weightListIndex, in.numBones,
                                               sizeof(float), "R2 sequence weight list");
        std::memcpy(weights.data(), source.data() + weightsSource, weights.size() * sizeof(float));
    }
    const auto weightsDestination = AppendBytes(output, weights.data(), weights.size() * sizeof(float));
    seqOut.weightListIndex = static_cast<std::int32_t>(weightsDestination - static_cast<std::int32_t>(seqDestination));

    WriteObject(output, seqDestination, seqOut);
    StoreRelativeString(output, seqDestination, offsetof(Sequence52, labelIndex),
                        degraded ? std::string("ref")
                                 : SourceRecordString(source, seqSource, seq.labelIndex, "R2 sequence label"));
    StoreRelativeString(output, seqDestination, offsetof(Sequence52, activityNameIndex),
                        degraded ? std::string()
                                 : SourceRecordString(source, seqSource, seq.activityNameIndex, "R2 sequence activity name", false));
}
void AppendMappedAttachments(std::span<const std::uint8_t> source, std::int32_t sourceIndex,
                             std::int32_t count, std::vector<std::uint8_t>& output,
                             Header52& out) {
    if (count == 0) {
        out.localAttachmentIndex = 0;
        return;
    }
    CheckArray(source, sourceIndex, count, sizeof(Attachment), "R2 local attachment array");
    const auto destination = AppendZeroBytes(output, static_cast<std::size_t>(count) * sizeof(Attachment));
    out.localAttachmentIndex = destination;
    for (std::int32_t i = 0; i < count; ++i) {
        const auto sourceOffset = static_cast<std::size_t>(sourceIndex) +
                                  static_cast<std::size_t>(i) * sizeof(Attachment);
        const auto destinationOffset = static_cast<std::size_t>(destination) +
                                      static_cast<std::size_t>(i) * sizeof(Attachment);
        auto attachment = ReadObject<Attachment>(source, sourceOffset, "R2 local attachment");
        const auto name = SourceRecordString(source, sourceOffset, attachment.nameIndex, "local attachment name");
        attachment.nameIndex = 0;
        WriteObject(output, destinationOffset, attachment);
        StoreRelativeString(output, destinationOffset, offsetof(Attachment, nameIndex), name);
    }
}

void AppendMappedBodyParts(std::span<const std::uint8_t> source, const Header53& in,
                           std::vector<std::uint8_t>& output, Header52& out) {
    CheckArray(source, in.bodyPartIndex, in.numBodyParts, sizeof(BodyPart), "R2 bodypart array");
    const auto bodyPartsOffset = AppendZeroBytes(output, static_cast<std::size_t>(in.numBodyParts) * sizeof(BodyPart));
    out.bodyPartIndex = bodyPartsOffset;
    for (std::int32_t b = 0; b < in.numBodyParts; ++b) {
        const auto srcBodyOffset = static_cast<std::size_t>(in.bodyPartIndex) + static_cast<std::size_t>(b) * sizeof(BodyPart);
        const auto srcBody = ReadObject<BodyPart>(source, srcBodyOffset, "R2 bodypart");
        const auto dstBodyOffset = static_cast<std::size_t>(bodyPartsOffset) + static_cast<std::size_t>(b) * sizeof(BodyPart);
        BodyPart dstBody{0, srcBody.numModels, srcBody.base, 0};
        WriteObject(output, dstBodyOffset, dstBody);
        const auto sourceModelsOffset = ArrayOffset(source, srcBodyOffset, srcBody.modelIndex,
                                                    srcBody.numModels, sizeof(Model53), "R2 model array");
        const auto modelsOffset = AppendZeroBytes(output, static_cast<std::size_t>(srcBody.numModels) * sizeof(Model52));
        dstBody.modelIndex = static_cast<std::int32_t>(modelsOffset - dstBodyOffset);
        WriteObject(output, dstBodyOffset, dstBody);
        StoreRelativeString(output, dstBodyOffset, offsetof(BodyPart, nameIndex),
                            SourceRecordString(source, srcBodyOffset, srcBody.nameIndex, "bodypart name"));
        for (std::int32_t m = 0; m < srcBody.numModels; ++m) {
            const auto srcModelOffset = static_cast<std::size_t>(sourceModelsOffset) +
                                        static_cast<std::size_t>(m) * sizeof(Model53);
            const auto srcModel = ReadObject<Model53>(source, srcModelOffset, "R2 model");
            const auto dstModelOffset = static_cast<std::size_t>(modelsOffset) + static_cast<std::size_t>(m) * sizeof(Model52);
            Model52 dstModel{};
            std::memcpy(dstModel.name, srcModel.name, sizeof(dstModel.name));
            dstModel.type = srcModel.type;
            dstModel.boundingRadius = srcModel.boundingRadius;
            dstModel.numMeshes = srcModel.numMeshes;
            dstModel.numVertices = srcModel.numVertices;
            dstModel.vertexIndex = srcModel.vertexIndex;
            dstModel.tangentsIndex = srcModel.tangentsIndex;
            dstModel.numAttachments = srcModel.numAttachments;
            std::copy(std::begin(srcModel.pad), std::end(srcModel.pad), std::begin(dstModel.pad));
            dstModel.colorIndex = srcModel.colorIndex;
            dstModel.uv2Index = srcModel.uv2Index;
            std::copy(std::begin(srcModel.unused), std::end(srcModel.unused), std::begin(dstModel.unused));
            dstModel.numEyeballs = srcModel.numEyeballs;
            Require(srcModel.numEyeballs == 0, "unsupported eyeball model data in a static prop");
            WriteObject(output, dstModelOffset, dstModel);
            const auto sourceMeshesOffset = ArrayOffset(source, srcModelOffset, srcModel.meshIndex,
                                                        srcModel.numMeshes, sizeof(Mesh53), "R2 mesh array");
            const auto meshesOffset = AppendZeroBytes(output, static_cast<std::size_t>(srcModel.numMeshes) * sizeof(Mesh52));
            dstModel.meshIndex = static_cast<std::int32_t>(meshesOffset - dstModelOffset);
            WriteObject(output, dstModelOffset, dstModel);
            for (std::int32_t meshIndex = 0; meshIndex < srcModel.numMeshes; ++meshIndex) {
                const auto srcMeshOffset = static_cast<std::size_t>(sourceMeshesOffset) +
                                           static_cast<std::size_t>(meshIndex) * sizeof(Mesh53);
                const auto srcMesh = ReadObject<Mesh53>(source, srcMeshOffset, "R2 mesh");
                Require(srcMesh.numFlexes == 0, "unsupported vertex-flex mesh; static conversion requires zero mesh flexes");
                Mesh52 dstMesh{};
                dstMesh.material = srcMesh.material;
                dstMesh.modelIndex = static_cast<std::int32_t>(dstModelOffset) - static_cast<std::int32_t>(meshesOffset + static_cast<std::size_t>(meshIndex) * sizeof(Mesh52));
                dstMesh.numVertices = srcMesh.numVertices;
                dstMesh.vertexOffset = srcMesh.vertexOffset;
                dstMesh.materialType = srcMesh.materialType;
                dstMesh.materialParam = srcMesh.materialParam;
                dstMesh.meshID = srcMesh.meshID;
                dstMesh.center = srcMesh.center;
                dstMesh.vertexLODData = srcMesh.vertexLODData;
                const auto dstMeshOffset = static_cast<std::size_t>(meshesOffset) + static_cast<std::size_t>(meshIndex) * sizeof(Mesh52);
                WriteObject(output, dstMeshOffset, dstMesh);
            }
            if (srcModel.numAttachments != 0) {
                const auto sourceAttachmentsOffset = ArrayOffset(source, srcModelOffset, srcModel.attachmentIndex,
                                                                 srcModel.numAttachments, sizeof(Attachment),
                                                                 "R2 model attachment array");
                const auto attachmentsOffset = AppendZeroBytes(output, static_cast<std::size_t>(srcModel.numAttachments) * sizeof(Attachment));
                dstModel.attachmentIndex = static_cast<std::int32_t>(attachmentsOffset - dstModelOffset);
                WriteObject(output, dstModelOffset, dstModel);
                for (std::int32_t a = 0; a < srcModel.numAttachments; ++a) {
                    const auto srcAttachmentOffset = static_cast<std::size_t>(sourceAttachmentsOffset) +
                                                     static_cast<std::size_t>(a) * sizeof(Attachment);
                    const auto attachment = ReadObject<Attachment>(source, srcAttachmentOffset, "R2 attachment");
                    const auto dstAttachmentOffset = static_cast<std::size_t>(attachmentsOffset) + static_cast<std::size_t>(a) * sizeof(Attachment);
                    Attachment copy = attachment;
                    copy.nameIndex = 0;
                    WriteObject(output, dstAttachmentOffset, copy);
                    StoreRelativeString(output, dstAttachmentOffset, offsetof(Attachment, nameIndex),
                                        SourceRecordString(source, srcAttachmentOffset, attachment.nameIndex, "attachment name"));
                }
            }
        }
    }
}

std::int32_t LinearBoneByteExtent(std::span<const std::uint8_t> source, std::int32_t absoluteOffset,
                                  const LinearBone& linear) {
    Require(linear.numBones >= 0 && linear.numBones <= 256, "linear-bone count is invalid");
    std::uint64_t end = sizeof(LinearBone);
    const std::array<std::pair<std::int32_t, std::size_t>, 9> arrays{{
        {linear.flagsIndex, sizeof(std::int32_t)}, {linear.parentIndex, sizeof(std::int32_t)},
        {linear.positionIndex, sizeof(Vec3)}, {linear.quaternionIndex, sizeof(Quaternion)},
        {linear.rotationIndex, sizeof(Euler)}, {linear.poseToBoneIndex, sizeof(Matrix3x4)},
        {linear.positionScaleIndex, sizeof(Vec3)}, {linear.rotationScaleIndex, sizeof(Vec3)},
        {linear.alignmentIndex, sizeof(Quaternion)}}};
    for (const auto& [offset, elementSize] : arrays) {
        if (offset == 0) continue;
        Require(offset >= static_cast<std::int32_t>(sizeof(LinearBone)), "linear-bone array overlaps its header");
        const auto bytes = ArrayBytes(linear.numBones, elementSize, "linear-bone array");
        Require(Range(source.size(), static_cast<std::int64_t>(absoluteOffset) + offset, bytes),
                "linear-bone array is outside the R2 MDL");
        end = std::max(end, static_cast<std::uint64_t>(offset) + bytes);
    }
    Require(end <= static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()), "linear-bone block is too large");
    return static_cast<std::int32_t>(end);
}

void AppendMappedSecondaryHeader(std::span<const std::uint8_t> source, const Header53& in,
                                 std::vector<std::uint8_t>& output, Header52& out,
                                 std::size_t hdr2Offset, Header2R1& hdr2, bool degraded) {
    hdr2.numSrcBoneTransform = in.numSrcBoneTransform;
    hdr2.illumPositionAttachmentIndex = in.illumPositionAttachmentIndex;
    hdr2.numBoneFlexDrivers = 0;
    hdr2.boneFlexDriverIndex = 0;
    hdr2.nameIndex = 0;
    if (in.numSrcBoneTransform != 0) {
        CheckArray(source, in.srcBoneTransformIndex, in.numSrcBoneTransform, sizeof(SourceBoneTransform), "R2 source-bone transform array");
        const auto destination = AppendZeroBytes(output, static_cast<std::size_t>(in.numSrcBoneTransform) * sizeof(SourceBoneTransform));
        hdr2.srcBoneTransformIndex = static_cast<std::int32_t>(destination - hdr2Offset);
        for (std::int32_t i = 0; i < in.numSrcBoneTransform; ++i) {
            const auto srcOffset = static_cast<std::size_t>(in.srcBoneTransformIndex) + static_cast<std::size_t>(i) * sizeof(SourceBoneTransform);
            auto value = ReadObject<SourceBoneTransform>(source, srcOffset, "R2 source-bone transform");
            const auto dstOffset = static_cast<std::size_t>(destination) + static_cast<std::size_t>(i) * sizeof(SourceBoneTransform);
            value.nameIndex = 0;
            WriteObject(output, dstOffset, value);
            StoreRelativeString(output, dstOffset, offsetof(SourceBoneTransform, nameIndex),
                                SourceRecordString(source, srcOffset, ReadObject<SourceBoneTransform>(source, srcOffset, "R2 source-bone transform").nameIndex,
                                                   "source-bone transform name"));
        }
    } else {
        hdr2.srcBoneTransformIndex = 0;
    }
    if (in.linearBoneIndex != 0 && !degraded) {
        const auto linear = ReadObject<LinearBone>(source, in.linearBoneIndex, "R2 linear-bone header");
        const auto extent = LinearBoneByteExtent(source, in.linearBoneIndex, linear);
        Require(Range(source.size(), in.linearBoneIndex, static_cast<std::uint64_t>(extent)), "R2 linear-bone block is outside the MDL");
        const auto destination = AppendBytes(output, source.data() + in.linearBoneIndex, static_cast<std::size_t>(extent));
        hdr2.linearBoneIndex = static_cast<std::int32_t>(destination - hdr2Offset);
    } else {
        hdr2.linearBoneIndex = 0;
    }
    // Animated R2 models carry per-triangle collision blocks of other versions; R1 reads version 2
    // only, so a degraded model without a valid block simply has no per-triangle collision.
    bool keepPerTri = in.perTriIndex != 0;
    if (degraded && keepPerTri) {
        try {
            keepPerTri = in.perTriNodeCount >= 0 && in.perTriLeafCount >= 0 && in.perTriVertCount >= 0 &&
                ReadObject<PerTriHeader>(source, in.perTriIndex, "R2 per-triangle collision header").version == 2 &&
                Range(source.size(), in.perTriIndex, sizeof(PerTriHeader) +
                    static_cast<std::uint64_t>(in.perTriNodeCount) * 0x14u +
                    static_cast<std::uint64_t>(in.perTriLeafCount) * 0x70u +
                    static_cast<std::uint64_t>(in.perTriVertCount) * 6u);
        } catch (const ConvertFailure&) {
            keepPerTri = false;
        }
    }
    if (degraded && !keepPerTri) {
        hdr2.perTriIndex = 0;
    } else if (in.perTriIndex != 0) {
        Require(in.perTriNodeCount >= 0 && in.perTriLeafCount >= 0 && in.perTriVertCount >= 0,
                "per-triangle collision counts are negative");
        const auto pertri = ReadObject<PerTriHeader>(source, in.perTriIndex, "R2 per-triangle collision header");
        Require(pertri.version == 2, "unsupported per-triangle collision version; R1 accepts version 2 only");
        const std::uint64_t dataSize = sizeof(PerTriHeader) +
            static_cast<std::uint64_t>(in.perTriNodeCount) * 0x14u +
            static_cast<std::uint64_t>(in.perTriLeafCount) * 0x70u +
            static_cast<std::uint64_t>(in.perTriVertCount) * 6u;
        Require(Range(source.size(), in.perTriIndex, dataSize), "R2 per-triangle collision block is outside the MDL");
        const auto destination = AppendBytes(output, source.data() + in.perTriIndex, static_cast<std::size_t>(dataSize));
        hdr2.perTriIndex = static_cast<std::int32_t>(destination - hdr2Offset);
        hdr2.perTriNodeCount = in.perTriNodeCount;
        hdr2.perTriLeafCount = in.perTriLeafCount;
        hdr2.perTriVertCount = in.perTriVertCount;
    } else {
        Require(in.perTriNodeCount == 0 && in.perTriLeafCount == 0 && in.perTriVertCount == 0,
                "per-triangle collision counts are present without their data block");
        hdr2.perTriIndex = 0;
    }
    if (in.unknownStringOffset != 0) {
        const auto unknown = SourceGlobalString(source, in.unknownStringOffset, "R2 secondary-header string");
        const auto stringOffset = AppendString(output, unknown);
        hdr2.unknownStringOffset = static_cast<std::int32_t>(stringOffset - hdr2Offset);
    } else {
        hdr2.unknownStringOffset = 0;
    }
    const auto modelName = SourceGlobalString(source, in.nameOffset, "R2 model name");
    const auto modelNameOffset = AppendString(output, modelName);
    hdr2.nameIndex = static_cast<std::int32_t>(modelNameOffset - hdr2Offset);
    out.studioHdr2Index = static_cast<std::int32_t>(hdr2Offset);
}

// Subset requirements that still apply when the animation data is dropped.
void CheckR2DegradedSubset(const Header53& hdr) {
    const auto flags = static_cast<std::uint32_t>(hdr.flags);
    Require((flags & kStudioExtraWeightsOrFlexes) == 0, "unsupported extra bone weights or flex flag in R2 MDL");
    Require((flags & kStudioSubdivisionSurface) == 0, "unsupported subdivision-surface mesh data");
    Require(hdr.numBodyParts > 0, "model has no bodyparts");
    Require(hdr.numBones >= 0 && hdr.numBones <= 256, "R2 bone count is outside the supported 0..256 range");
    Require(hdr.numTextures >= 0 && hdr.numTextures <= 168,
            "R1 texture count exceeds the loader limit of 168");
    Require(hdr.numSkinRef >= 0 && hdr.numSkinFamilies >= 0, "skin-table counts are negative");
    if (hdr.numSkinFamilies != 0)
        Require(hdr.numSkinRef != 0, "skin families are present without skin references");
}

void CheckR2StaticSubset(const Header53& hdr, std::span<const std::uint8_t> source) {
    Require(hdr.numLocalAnim == 1 && hdr.numLocalSeq == 1,
            "unsupported animation layout; static subset requires one reference animation and sequence");
    CheckArray(source, hdr.localAnimIndex, hdr.numLocalAnim, sizeof(AnimDesc53), "R2 animation description");
    CheckArray(source, hdr.localSeqIndex, hdr.numLocalSeq, sizeof(Sequence53), "R2 sequence description");
    const auto animOffset = static_cast<std::size_t>(hdr.localAnimIndex);
    const auto anim = ReadObject<AnimDesc53>(source, animOffset, "R2 reference animation");
    Require(SourceRecordString(source, animOffset, anim.nameIndex, "R2 animation name") == "@ref",
            "unsupported animation; expected the single @ref static pose");
    Require(anim.numFrames == 1 && anim.fps > 0.0f,
            "unsupported animation; static reference pose must have one frame and a positive frame rate");
    Require(anim.numMovements == 0 && anim.movementIndex == 0 && anim.frameMovementIndex == 0,
            "unsupported movement animation data in the static reference pose");
    Require(anim.numIkRules == 0 && anim.ikRuleIndex == 0 &&
            anim.numLocalHierarchy == 0 && anim.localHierarchyIndex == 0 &&
            anim.sectionIndex == 0 && anim.sectionFrames == 0,
            "unsupported IK, hierarchy, or section data in the static reference pose");
    Require(anim.animIndex != 0, "static reference pose has no empty animation sentinel");
    const auto animDataOffset = RelativeAbsolute(animOffset, anim.animIndex, "R2 reference animation data");
    Require(Range(source.size(), animDataOffset, 6), "R2 reference animation sentinel is outside the MDL");
    const auto sentinelBone = ReadObject<std::uint8_t>(source, animDataOffset + 4, "R2 reference animation sentinel");
    const auto sentinelFlags = ReadObject<std::uint8_t>(source, animDataOffset + 5, "R2 reference animation sentinel");
    Require(sentinelBone == 0xff && sentinelFlags == 0,
            "unsupported animated reference pose; only the empty one-frame static sentinel is supported");

    const auto seqOffset = static_cast<std::size_t>(hdr.localSeqIndex);
    const auto seq = ReadObject<Sequence53>(source, seqOffset, "R2 static reference sequence");
    Require(SourceRecordString(source, seqOffset, seq.labelIndex, "R2 sequence label") == "ref",
            "unsupported sequence; expected the single ref static sequence");
    Require(seq.numEvents == 0 && seq.numBlends == 1 && seq.groupSize[0] == 1 && seq.groupSize[1] == 1,
            "unsupported static sequence events or blend layout");
    Require(seq.numIkRules == 0 && seq.numAutoLayers == 0 && seq.numIkLocks == 0 &&
            seq.numActivityModifiers == 0 && seq.keyValueSize == 0 && seq.movementIndex == 0,
            "unsupported IK, layer, activity, keyvalue, or movement data in the static sequence");
    const auto animIndexOffset = ArrayOffset(source, seqOffset, seq.animIndexIndex, 1,
                                             sizeof(std::int16_t), "R2 sequence animation index");
    Require(ReadObject<std::int16_t>(source, animIndexOffset, "R2 sequence animation index") == 0,
            "static sequence does not select the @ref animation");

    Require(hdr.numBoneControllers == 0, "unsupported bone controllers in a static prop");
    Require(hdr.numLocalNodes == 0, "unsupported animation transition nodes in a static prop");
    Require(hdr.numFlexDesc == 0 && hdr.numFlexControllers == 0 && hdr.numFlexRules == 0,
            "unsupported flex descriptors/controllers/rules in a static prop");
    Require(hdr.numIkChains == 0, "unsupported IK chains in a static prop");
    Require(hdr.numLocalPoseParameters == 0, "unsupported pose parameters in a static prop");
    Require(hdr.numLocalIkAutoPlayLocks == 0, "unsupported IK autoplay locks in a static prop");
    Require(hdr.numIncludeModels == 0, "unsupported included models in a static prop");
    Require(hdr.uiPanelCount == 0, "unsupported R2 UI panel model data");
    Require(hdr.numBoneFlexDrivers == 0, "unsupported bone-flex driver data");
    Require(hdr.boneFollowerCount == 0, "unsupported R2 bone followers");
    const auto flags = static_cast<std::uint32_t>(hdr.flags);
    Require((flags & kStudioStaticProp) != 0, "input MDL is not marked as a static prop");
    Require((flags & kStudioExtraWeightsOrFlexes) == 0, "unsupported extra bone weights or flex flag in R2 MDL");
    Require((flags & kStudioSubdivisionSurface) == 0, "unsupported subdivision-surface mesh data");
    Require((flags & ~(kR1StudioFlagMask | kR2OnlyFlag)) == 0,
            "R2 MDL contains flags not recognized by the static converter");
    Require(hdr.numBodyParts > 0, "model has no bodyparts");
    Require(hdr.numBones >= 0 && hdr.numBones <= 256, "R2 bone count is outside the supported 0..256 range");
    Require(hdr.numTextures >= 0 && hdr.numTextures <= 168,
            "R1 texture count exceeds the loader limit of 168");
    Require(hdr.numSkinRef >= 0 && hdr.numSkinFamilies >= 0, "skin-table counts are negative");
    if (hdr.numSkinFamilies != 0)
        Require(hdr.numSkinRef != 0, "skin families are present without skin references");
}

bool IsStaticReferenceLayout(const Header53& hdr, std::span<const std::uint8_t> source) {
    try {
        CheckR2StaticSubset(hdr, source);
        return true;
    } catch (const ConvertFailure&) {
        return false;
    }
}
Header52 ConvertHeader(const Header53& in) {
    Header52 out{};
    out.id = kIDST;
    out.version = kModelVersionR1;
    out.checksum = in.checksum;
    std::memcpy(out.name, in.name, sizeof(out.name));
    out.eyePosition = in.eyePosition;
    out.illumPosition = in.illumPosition;
    out.hullMin = in.hullMin;
    out.hullMax = in.hullMax;
    out.viewMin = in.viewMin;
    out.viewMax = in.viewMax;
    out.flags = in.flags & static_cast<std::int32_t>(kR1StudioFlagMask);
    out.numBones = in.numBones;
    out.numBoneControllers = 0;
    out.numHitboxSets = in.numHitboxSets;
    out.numLocalAnim = 0;
    out.numLocalSeq = 0;
    out.activityListVersion = 0;
    out.eventsIndexed = 0;
    out.numTextures = in.numTextures;
    out.numCdTextures = in.numCdTextures;
    out.numSkinRef = in.numSkinRef;
    out.numSkinFamilies = in.numSkinFamilies;
    out.numBodyParts = in.numBodyParts;
    out.numLocalAttachments = in.numLocalAttachments;
    out.numLocalNodes = 0;
    out.numFlexDesc = out.numFlexControllers = out.numFlexRules = 0;
    out.numIkChains = 0;
    out.numMouths = 0;
    out.numLocalPoseParameters = 0;
    out.keyValueSize = in.keyValueSize;
    out.mass = in.mass;
    out.contents = in.contents;
    out.numIncludeModels = 0;
    out.constantDirectionalLightDot = in.constantDirectionalLightDot;
    out.rootLOD = in.rootLOD;
    out.numAllowedRootLODs = in.numAllowedRootLODs;
    out.defaultFadeDist = in.defaultFadeDist;
    out.vertAnimFixedPointScale = in.vertAnimFixedPointScale;
    return out;
}

} // namespace

bool ConvertModelR2ToR1(const ModelInput& input, ModelOutput& output, std::string& error) {
    output = {};
    error.clear();
    try {
        const auto in = ReadObject<Header53>(input.mdl, 0, "R2 MDL header");
        Require(in.id == kIDST && in.version == kModelVersionR2, "input is not an IDST MDL version 53 file");
        Require(in.length >= static_cast<std::int32_t>(sizeof(Header53)) &&
                static_cast<std::size_t>(in.length) <= input.mdl.size(), "R2 MDL declared length is outside its input buffer");
        const auto sourceMDL = input.mdl.first(static_cast<std::size_t>(in.length));
        Require(in.nameOffset != 0, "R2 MDL has a null internal model-name offset");
        (void)SourceGlobalString(sourceMDL, in.nameOffset, "R2 model name");
        // A model with real animations (or a non-empty reference pose) is degraded to its bind pose
        // instead of being refused: see IsStaticReferenceLayout / AppendMappedStaticAnimation.
        const bool degraded = !IsStaticReferenceLayout(in, sourceMDL);
        if (degraded)
            CheckR2DegradedSubset(in);
        else
            CheckR2StaticSubset(in, sourceMDL);
        Require(in.keyValueSize >= 0, "R2 keyvalue data size is negative");
        if (in.keyValueSize != 0)
            Require(Range(static_cast<std::size_t>(in.length), in.keyValueIndex, static_cast<std::uint64_t>(in.keyValueSize)),
                    "R2 keyvalue data is outside the MDL");
        if (in.surfacePropIndex != 0)
            (void)SourceGlobalString(sourceMDL, in.surfacePropIndex, "R2 surface property");

        const auto vvdBytes = EmbeddedOrExternal(sourceMDL, in.vvdOffset, in.vvdSize, input.vvd, "R2 VVD");
        const auto vtxBytes = EmbeddedOrExternal(sourceMDL, in.vtxOffset, in.vtxSize, input.vtx, "R2 VTX");
        const auto vvcBytes = EmbeddedOrExternal(sourceMDL, in.vvcOffset, in.vvcSize, input.vvc, "R2 VVC");
        const auto phyBytes = EmbeddedOrExternal(sourceMDL, in.phyOffset, in.phySize, input.phy, "R2 PHY");
        const auto flags = static_cast<std::uint32_t>(in.flags);
        Require((flags & (kStudioVVC | kStudioUV2)) == 0 || !vvcBytes.empty(),
                "R2 MDL requires a VVC companion, but none was supplied or embedded");
        Require(in.phySize == 0 || !phyBytes.empty(), "R2 PHY section is missing");
        Require(in.numBones == 0 || in.boneIndex != 0, "R2 bone count is nonzero but bone array offset is null");
        CheckArray(sourceMDL, in.boneIndex, in.numBones, sizeof(Bone53), "R2 bone array");
        if (in.numBoneControllers != 0)
            CheckArray(sourceMDL, in.boneControllerIndex, in.numBoneControllers, 0x24, "R2 bone-controller array");
        const auto skinEntries = static_cast<std::uint64_t>(in.numSkinRef) *
                                 static_cast<std::uint64_t>(in.numSkinFamilies);
        Require(skinEntries <= kMaxRecords, "R2 skin table exceeds the converter limit");
        if (skinEntries != 0)
            CheckArray(sourceMDL, in.skinIndex, static_cast<std::int32_t>(skinEntries),
                       sizeof(std::int16_t), "R2 skin table");

        std::vector<std::uint8_t> mdl(sizeof(Header52) + sizeof(Header2R1), 0);
        Header52 outHdr = ConvertHeader(in);
        Header2R1 outHdr2{};
        const std::size_t hdr2Offset = sizeof(Header52);
        outHdr2.maxEyeDeflection = 0.0f;

        if (in.numBones != 0) {
            const auto destination = AppendZeroBytes(mdl, static_cast<std::size_t>(in.numBones) * sizeof(Bone52));
            outHdr.boneIndex = destination;
            for (std::int32_t i = 0; i < in.numBones; ++i) {
                const auto sourceOffset = static_cast<std::size_t>(in.boneIndex) + static_cast<std::size_t>(i) * sizeof(Bone53);
                const auto sourceBone = ReadObject<Bone53>(sourceMDL, sourceOffset, "R2 bone");
                Require(sourceBone.parent == -1 || (sourceBone.parent >= 0 && sourceBone.parent < in.numBones),
                        "R2 bone parent index is outside the bone array");
                AppendMappedBone(sourceMDL, sourceOffset, mdl,
                    static_cast<std::int32_t>(destination + static_cast<std::size_t>(i) * sizeof(Bone52)), degraded);
            }
        } else {
            outHdr.boneIndex = 0;
        }

        AppendMappedHitboxes(sourceMDL, in, mdl, outHdr);
        AppendMappedTextures(sourceMDL, in, mdl, outHdr);
        AppendMappedStaticAnimation(sourceMDL, in, mdl, outHdr, degraded);
        AppendMappedAttachments(sourceMDL, in.localAttachmentIndex, in.numLocalAttachments, mdl, outHdr);
        AppendMappedBodyParts(sourceMDL, in, mdl, outHdr);

        if (skinEntries != 0) {
            const auto skinBytes = static_cast<std::size_t>(skinEntries) * sizeof(std::int16_t);
            outHdr.skinIndex = AppendBytes(mdl, sourceMDL.data() + in.skinIndex, skinBytes);
        } else {
            outHdr.skinIndex = 0;
        }
        if (in.surfacePropIndex != 0) {
            outHdr.surfacePropIndex = AppendString(mdl, SourceGlobalString(sourceMDL, in.surfacePropIndex, "R2 surface property"));
        }
        if (in.keyValueSize != 0) {
            outHdr.keyValueIndex = AppendBytes(mdl, sourceMDL.data() + in.keyValueIndex, static_cast<std::size_t>(in.keyValueSize));
        } else {
            outHdr.keyValueIndex = 0;
            outHdr.keyValueSize = 0;
        }

        AppendMappedSecondaryHeader(sourceMDL, in, mdl, outHdr, hdr2Offset, outHdr2, degraded);
        WriteObject(mdl, hdr2Offset, outHdr2);
        outHdr.length = static_cast<std::int32_t>(mdl.size());
        WriteObject(mdl, 0, outHdr);

        CopyAndSetChecksum(vvdBytes, output.vvd, offsetof(VVDHeader, checksum), in.checksum, "R2 VVD");
        CopyAndSetChecksum(vtxBytes, output.vtx, offsetof(VTXHeader, checksum), in.checksum, "R2 VTX");
        if (!vvcBytes.empty())
            CopyAndSetChecksum(vvcBytes, output.vvc, offsetof(VVCHeader, checksum), in.checksum, "R2 VVC");
        const auto vvdHeader = ValidateVVD(output.vvd, in.checksum);
        if (!output.vvc.empty()) ValidateVVC(output.vvc, vvdHeader, in.checksum, flags);
        ValidateVTX(output.vtx, mdl, outHdr);
        if (!phyBytes.empty()) {
            ValidatePHY(phyBytes);
            output.phy.assign(phyBytes.begin(), phyBytes.end());
        }
        output.mdl = std::move(mdl);
        if (!VerifyModelR1(output, error)) {
            output = {};
            return false;
        }
        return true;
    } catch (const ConvertFailure& failure) {
        output = {};
        error = failure.what();
        return false;
    } catch (const std::exception& failure) {
        output = {};
        error = std::string("internal conversion error: ") + failure.what();
        return false;
    }
}

bool ListModelMaterials(std::span<const std::uint8_t> r2Mdl, std::vector<std::string>& names, std::string& error) {
    names.clear();
    error.clear();
    try {
        const auto in = ReadObject<Header53>(r2Mdl, 0, "R2 MDL header");
        Require(in.id == kIDST && in.version == kModelVersionR2, "input is not an IDST MDL version 53 file");
        Require(in.length >= static_cast<std::int32_t>(sizeof(Header53)) &&
                static_cast<std::size_t>(in.length) <= r2Mdl.size(), "R2 MDL declared length is outside its input buffer");
        const auto source = r2Mdl.first(static_cast<std::size_t>(in.length));
        Require(in.numTextures >= 0 && in.numTextures <= 168, "R2 texture count is outside 0..168");
        Require(in.numCdTextures >= 0 && in.numCdTextures <= 64, "R2 texture-directory count is outside 0..64");
        if (in.numTextures == 0)
            return true;
        CheckArray(source, in.textureIndex, in.numTextures, sizeof(Texture53), "R2 texture array");
        std::vector<std::string> directories;
        if (in.numCdTextures != 0) {
            CheckArray(source, in.cdTextureIndex, in.numCdTextures, sizeof(TextureDir), "R2 texture-directory array");
            for (std::int32_t i = 0; i < in.numCdTextures; ++i) {
                const auto dir = ReadObject<TextureDir>(source,
                    in.cdTextureIndex + static_cast<std::int64_t>(i) * sizeof(TextureDir), "R2 texture directory");
                directories.push_back(SourceGlobalString(source, dir.nameIndex, "texture-directory name"));
            }
        } else {
            directories.emplace_back();
        }
        for (std::int32_t i = 0; i < in.numTextures; ++i) {
            const auto offset = static_cast<std::size_t>(in.textureIndex) + static_cast<std::size_t>(i) * sizeof(Texture53);
            const auto texture = ReadObject<Texture53>(source, offset, "R2 texture");
            const auto textureName = SourceRecordString(source, offset, texture.nameIndex, "texture name");
            for (const auto& directory : directories) {
                std::string name = directory + textureName;
                for (char& c : name) {
                    if (c == '\\') c = '/';
                }
                while (!name.empty() && name.front() == '/') name.erase(name.begin());
                if (name.empty()) continue;
                name.insert(0, "r2m/");
                bool seen = false;
                for (const auto& existing : names) seen = seen || existing == name;
                if (!seen) names.push_back(std::move(name));
            }
        }
        return true;
    } catch (const std::exception& exception) {
        names.clear();
        error = exception.what();
        return false;
    }
}


bool VerifyModelR1(const ModelOutput& model, std::string& error) {
    error.clear();
    try {
        const std::span<const std::uint8_t> mdl(model.mdl);
        const auto hdr = ReadObject<Header52>(mdl, 0, "R1 MDL header");
        Require(hdr.id == kIDST && hdr.version == kModelVersionR1,
                "output is not an IDST MDL version 52 file");
        Require(hdr.length >= static_cast<std::int32_t>(sizeof(Header52)) &&
                static_cast<std::size_t>(hdr.length) == mdl.size(),
                "R1 MDL length does not match the output buffer");
        Require(hdr.numLocalAnim == 1 && hdr.numLocalSeq == 1,
                "R1 output must contain exactly the static reference animation and sequence");
        CheckArray(mdl, hdr.localAnimIndex, hdr.numLocalAnim, sizeof(AnimDesc52), "R1 animation description");
        CheckArray(mdl, hdr.localSeqIndex, hdr.numLocalSeq, sizeof(Sequence52), "R1 sequence description");
        {
            const auto animOffset = static_cast<std::size_t>(hdr.localAnimIndex);
            const auto anim = ReadObject<AnimDesc52>(mdl, animOffset, "R1 reference animation");
            (void)SourceRecordString(mdl, animOffset, anim.nameIndex, "R1 animation name");
            Require(anim.numFrames == 1 && anim.numMovements == 0 && anim.numIkRules == 0 &&
                    anim.sectionIndex == 0 && anim.animIndex != 0,
                    "R1 reference animation is not a single-frame static pose");
            Require(Range(mdl.size(), RelativeAbsolute(animOffset, anim.animIndex, "R1 animation data"), 8),
                    "R1 reference animation data is outside the MDL");
            const auto seqOffset = static_cast<std::size_t>(hdr.localSeqIndex);
            const auto seq = ReadObject<Sequence52>(mdl, seqOffset, "R1 reference sequence");
            (void)SourceRecordString(mdl, seqOffset, seq.labelIndex, "R1 sequence label");
            Require(seq.numBlends == 1 && seq.groupSize[0] == 1 && seq.groupSize[1] == 1 && seq.numEvents == 0 &&
                    seq.numIkRules == 0 && seq.numAutoLayers == 0 && seq.numIkLocks == 0,
                    "R1 reference sequence is not a single static blend");
            const auto blendOffset = ArrayOffset(mdl, seqOffset, seq.animIndexIndex, 1, sizeof(std::int16_t),
                                                 "R1 sequence animation index");
            Require(ReadObject<std::int16_t>(mdl, blendOffset, "R1 sequence animation index") == 0,
                    "R1 reference sequence does not select the reference animation");
            (void)ArrayOffset(mdl, seqOffset, seq.weightListIndex, hdr.numBones, sizeof(float), "R1 sequence weight list");
        }
        Require(hdr.numBoneControllers == 0 && hdr.numIkChains == 0 &&
                hdr.numLocalPoseParameters == 0,
                "R1 output contains unsupported controllers, IK, or pose parameters");
        Require(hdr.numFlexDesc == 0 && hdr.numFlexControllers == 0 &&
                hdr.numFlexRules == 0 && hdr.numFlexControllerUI == 0,
                "R1 output contains unsupported flex data");
        Require(hdr.numBones >= 0 && hdr.numBones <= 256 && hdr.numTextures <= 168,
                "R1 bone or texture count exceeds the supported loader limits");
        Require(hdr.numSkinRef >= 0 && hdr.numSkinFamilies >= 0,
                "R1 skin counts are negative");
        const auto skinEntries = static_cast<std::uint64_t>(hdr.numSkinRef) *
                                 static_cast<std::uint64_t>(hdr.numSkinFamilies);
        Require(skinEntries <= kMaxRecords, "R1 skin table exceeds the verifier limit");

        CheckArray(mdl, hdr.boneIndex, hdr.numBones, sizeof(Bone52), "R1 bone array");
        CheckArray(mdl, hdr.hitboxSetIndex, hdr.numHitboxSets, sizeof(HitboxSet), "R1 hitbox-set array");
        CheckArray(mdl, hdr.textureIndex, hdr.numTextures, sizeof(Texture52), "R1 texture array");
        CheckArray(mdl, hdr.cdTextureIndex, hdr.numCdTextures, sizeof(TextureDir), "R1 texture-directory array");
        CheckArray(mdl, hdr.localAttachmentIndex, hdr.numLocalAttachments, sizeof(Attachment),
                   "R1 local attachment array");
        CheckArray(mdl, hdr.bodyPartIndex, hdr.numBodyParts, sizeof(BodyPart), "R1 bodypart array");
        CheckArray(mdl, hdr.skinIndex, static_cast<std::int32_t>(skinEntries),
                   sizeof(std::int16_t), "R1 skin table");
        if (hdr.surfacePropIndex != 0) (void)SourceGlobalString(mdl, hdr.surfacePropIndex, "R1 surface property");
        if (hdr.keyValueSize != 0) {
            Require(hdr.keyValueSize > 0 &&
                    Range(mdl.size(), hdr.keyValueIndex, static_cast<std::uint64_t>(hdr.keyValueSize)),
                    "R1 keyvalue data is outside the MDL");
        }

        for (std::int32_t i = 0; i < hdr.numBones; ++i) {
            const auto offset = static_cast<std::size_t>(hdr.boneIndex) +
                                static_cast<std::size_t>(i) * sizeof(Bone52);
            const auto bone = ReadObject<Bone52>(mdl, offset, "R1 bone");
            Require(bone.parent == -1 || (bone.parent >= 0 && bone.parent < hdr.numBones),
                    "R1 bone parent index is outside the bone array");
            Require(bone.procedureType == 0, "R1 output contains an unsupported procedural bone");
            (void)SourceRecordString(mdl, offset, bone.nameIndex, "R1 bone name");
            if (bone.surfacePropIndex != 0)
                (void)SourceRecordString(mdl, offset, bone.surfacePropIndex, "R1 bone surface property", false);
        }
        for (std::int32_t i = 0; i < hdr.numHitboxSets; ++i) {
            const auto setOffset = static_cast<std::size_t>(hdr.hitboxSetIndex) +
                                   static_cast<std::size_t>(i) * sizeof(HitboxSet);
            const auto set = ReadObject<HitboxSet>(mdl, setOffset, "R1 hitbox set");
            (void)SourceRecordString(mdl, setOffset, set.nameIndex, "R1 hitbox-set name");
            const auto boxesOffset = ArrayOffset(mdl, setOffset, set.hitboxIndex,
                                                 set.numHitboxes, sizeof(Hitbox52), "R1 hitbox array");
            for (std::int32_t j = 0; j < set.numHitboxes; ++j) {
                const auto offset = static_cast<std::size_t>(boxesOffset) +
                                    static_cast<std::size_t>(j) * sizeof(Hitbox52);
                const auto box = ReadObject<Hitbox52>(mdl, offset, "R1 hitbox");
                Require(box.bone >= 0 && box.bone < hdr.numBones,
                        "R1 hitbox references an invalid bone");
                if (box.nameIndex != 0)
                    (void)SourceRecordString(mdl, offset, box.nameIndex, "R1 hitbox name", false);
            }
        }
        for (std::int32_t i = 0; i < hdr.numTextures; ++i) {
            const auto offset = static_cast<std::size_t>(hdr.textureIndex) +
                                static_cast<std::size_t>(i) * sizeof(Texture52);
            const auto texture = ReadObject<Texture52>(mdl, offset, "R1 texture");
            (void)SourceRecordString(mdl, offset, texture.nameIndex, "R1 texture name");
        }
        for (std::int32_t i = 0; i < hdr.numCdTextures; ++i) {
            const auto directory = ReadObject<TextureDir>(mdl,
                hdr.cdTextureIndex + static_cast<std::int64_t>(i) * sizeof(TextureDir),
                "R1 texture directory");
            (void)SourceGlobalString(mdl, directory.nameIndex, "R1 texture-directory name");
        }
        for (std::int32_t i = 0; i < hdr.numLocalAttachments; ++i) {
            const auto offset = static_cast<std::size_t>(hdr.localAttachmentIndex) +
                                static_cast<std::size_t>(i) * sizeof(Attachment);
            const auto attachment = ReadObject<Attachment>(mdl, offset, "R1 local attachment");
            (void)SourceRecordString(mdl, offset, attachment.nameIndex, "R1 local attachment name");
            Require(attachment.localBone >= 0 && attachment.localBone < hdr.numBones,
                    "R1 local attachment references an invalid bone");
        }

        Require(hdr.studioHdr2Index != 0 &&
                Range(mdl.size(), hdr.studioHdr2Index, sizeof(Header2R1)),
                "R1 secondary header is missing or outside the MDL");
        const auto hdr2Offset = static_cast<std::size_t>(hdr.studioHdr2Index);
        const auto hdr2 = ReadObject<Header2R1>(mdl, hdr2Offset, "R1 secondary header");
        (void)SourceRecordString(mdl, hdr2Offset, hdr2.nameIndex, "R1 secondary-header model name");
        Require(hdr2.numBoneFlexDrivers == 0, "R1 output contains unsupported bone-flex drivers");
        if (hdr2.numSrcBoneTransform != 0) {
            const auto transformsOffset = ArrayOffset(mdl, hdr2Offset, hdr2.srcBoneTransformIndex,
                hdr2.numSrcBoneTransform, sizeof(SourceBoneTransform), "R1 source-bone transform array");
            for (std::int32_t i = 0; i < hdr2.numSrcBoneTransform; ++i) {
                const auto offset = static_cast<std::size_t>(transformsOffset) +
                                    static_cast<std::size_t>(i) * sizeof(SourceBoneTransform);
                const auto transform = ReadObject<SourceBoneTransform>(mdl, offset, "R1 source-bone transform");
                (void)SourceRecordString(mdl, offset, transform.nameIndex, "R1 source-bone transform name");
            }
        }
        if (hdr2.linearBoneIndex != 0) {
            const auto offset = RelativeAbsolute(hdr2Offset, hdr2.linearBoneIndex, "R1 linear-bone header");
            const auto linear = ReadObject<LinearBone>(mdl, offset, "R1 linear-bone header");
            Require(linear.numBones == hdr.numBones,
                    "R1 linear-bone count does not match the MDL bone count");
            (void)LinearBoneByteExtent(mdl, static_cast<std::int32_t>(offset), linear);
        }
        if (hdr2.perTriIndex != 0) {
            Require(hdr2.perTriNodeCount >= 0 && hdr2.perTriLeafCount >= 0 &&
                    hdr2.perTriVertCount >= 0, "R1 per-triangle collision counts are negative");
            const auto offset = RelativeAbsolute(hdr2Offset, hdr2.perTriIndex, "R1 per-triangle collision block");
            const auto perTri = ReadObject<PerTriHeader>(mdl, offset, "R1 per-triangle collision header");
            Require(perTri.version == 2, "R1 per-triangle collision version is not 2");
            const auto bytes = sizeof(PerTriHeader) +
                static_cast<std::uint64_t>(hdr2.perTriNodeCount) * 0x14u +
                static_cast<std::uint64_t>(hdr2.perTriLeafCount) * 0x70u +
                static_cast<std::uint64_t>(hdr2.perTriVertCount) * 6u;
            Require(Range(mdl.size(), offset, bytes),
                    "R1 per-triangle collision block is outside the MDL");
        } else {
            Require(hdr2.perTriNodeCount == 0 && hdr2.perTriLeafCount == 0 &&
                    hdr2.perTriVertCount == 0,
                    "R1 per-triangle collision counts exist without data");
        }
        if (hdr2.unknownStringOffset != 0)
            (void)ReadCString(mdl, RelativeAbsolute(hdr2Offset, hdr2.unknownStringOffset,
                                                   "R1 secondary-header string"),
                              "R1 secondary-header string");

        for (std::int32_t bodyIndex = 0; bodyIndex < hdr.numBodyParts; ++bodyIndex) {
            const auto bodyOffset = static_cast<std::size_t>(hdr.bodyPartIndex) +
                                    static_cast<std::size_t>(bodyIndex) * sizeof(BodyPart);
            const auto body = ReadObject<BodyPart>(mdl, bodyOffset, "R1 bodypart");
            (void)SourceRecordString(mdl, bodyOffset, body.nameIndex, "R1 bodypart name");
            const auto modelsOffset = ArrayOffset(mdl, bodyOffset, body.modelIndex,
                                                  body.numModels, sizeof(Model52), "R1 model array");
            for (std::int32_t modelIndex = 0; modelIndex < body.numModels; ++modelIndex) {
                const auto modelOffset = static_cast<std::size_t>(modelsOffset) +
                                         static_cast<std::size_t>(modelIndex) * sizeof(Model52);
                const auto modelData = ReadObject<Model52>(mdl, modelOffset, "R1 model");
                Require(std::memchr(modelData.name, '\0', sizeof(modelData.name)) != nullptr,
                        "R1 model name is not NUL-terminated");
                Require(modelData.numVertices >= 0 && modelData.numAttachments >= 0 &&
                        modelData.numEyeballs == 0,
                        "R1 model has unsupported vertex, attachment, or eyeball counts");
                const auto meshesOffset = ArrayOffset(mdl, modelOffset, modelData.meshIndex,
                                                      modelData.numMeshes, sizeof(Mesh52), "R1 mesh array");
                for (std::int32_t meshIndex = 0; meshIndex < modelData.numMeshes; ++meshIndex) {
                    const auto meshOffset = static_cast<std::size_t>(meshesOffset) +
                                            static_cast<std::size_t>(meshIndex) * sizeof(Mesh52);
                    const auto mesh = ReadObject<Mesh52>(mdl, meshOffset, "R1 mesh");
                    Require(mesh.modelIndex == static_cast<std::int32_t>(modelOffset) -
                                              static_cast<std::int32_t>(meshOffset),
                            "R1 mesh back-reference does not point to its owning model");
                    Require(mesh.numVertices >= 0 && mesh.vertexOffset >= 0 &&
                            static_cast<std::uint64_t>(mesh.vertexOffset) +
                                static_cast<std::uint64_t>(mesh.numVertices) <=
                                static_cast<std::uint64_t>(modelData.numVertices),
                            "R1 mesh vertex range exceeds its model vertex count");
                    Require(mesh.numFlexes == 0, "R1 mesh contains unsupported flex data");
                    Require(hdr.numTextures > 0 && mesh.material >= 0 && mesh.material < hdr.numTextures,
                            "R1 mesh material index is outside the texture table");
                }
                if (modelData.numAttachments != 0) {
                    const auto attachmentsOffset = ArrayOffset(mdl, modelOffset, modelData.attachmentIndex,
                        modelData.numAttachments, sizeof(Attachment), "R1 model attachment array");
                    for (std::int32_t i = 0; i < modelData.numAttachments; ++i) {
                        const auto offset = static_cast<std::size_t>(attachmentsOffset) +
                                            static_cast<std::size_t>(i) * sizeof(Attachment);
                        const auto attachment = ReadObject<Attachment>(mdl, offset, "R1 model attachment");
                        (void)SourceRecordString(mdl, offset, attachment.nameIndex, "R1 model attachment name");
                        Require(attachment.localBone >= 0 && attachment.localBone < hdr.numBones,
                                "R1 model attachment references an invalid bone");
                    }
                }
            }
        }

        Require(!model.vvd.empty() && !model.vtx.empty(),
                "R1 output is missing its required VVD or VTX companion");
        const auto vvdHeader = ValidateVVD(model.vvd, hdr.checksum);
        const auto flags = static_cast<std::uint32_t>(hdr.flags);
        if ((flags & (kStudioVVC | kStudioUV2)) != 0)
            Require(!model.vvc.empty(), "R1 output is missing its required VVC companion");
        if (!model.vvc.empty()) ValidateVVC(model.vvc, vvdHeader, hdr.checksum, flags);
        ValidateVTX(model.vtx, mdl, hdr);
        if (!model.phy.empty()) ValidatePHY(model.phy);
        return true;
    } catch (const ConvertFailure& failure) {
        error = failure.what();
        return false;
    } catch (const std::exception& failure) {
        error = std::string("internal verification error: ") + failure.what();
        return false;
    }
}
} // namespace r2port::modelconv
