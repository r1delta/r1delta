#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Retail R1 materialsystem_dx11.dll (timestamp 0x54874238, SizeOfImage 0x3E0000).
//
// Every CMaterial (vtable RVA 0x184DC8) carries a 24-byte shader record at CMaterial+104:
//
//   +0   uint32  key         dword_180382758 | (uint16)generation (CMaterial+196); a mismatch makes the next draw re-run
//                            the shader's state function (vtable slot 11) on the render thread
//   +4   uint32  flags       bit 31 = +16 is a heap block owned by the material (CMaterial dtor frees it only then)
//   +8   ptr     constant buffer (ID3D11Buffer*)
//   +16  union   either a pointer to a per-material table of 16-bit texture handles (UnlitGeneric, VertexLitGeneric,
//                LightmappedGeneric, Refract, Water, Engine_Post, ...: slot 9 allocates it only when this qword is 0, slot 11
//                zeroes and fills it) or ONE inline 16-bit texture handle (Basic, BasicForceWireframe = the error shader,
//                UnlitTwoTexture, Sprite, Spritecard, Modulate, Cable, ...: slot 11 stores `mov word [record+16], handle`)
//
// Nothing clears the record when a material's shader changes (CMaterial::Uncache -> release at RVA 0x383E0 -> a new
// shader). A material that was drawn with an inline-handle shader and then comes back with a table shader keeps the
// handle in the qword: UnlitGeneric slot 9 (RVA 0x2F140 `cmp qword [rdi+10h],0`) sees it as "already allocated" and slot 11
// (RVA 0x2F502 `mov rbx,[r8+10h]`, 0x2F510 `mov [rbx],rax`) writes 32 bytes through the handle.
namespace r1delta::materialsystem_dx11
{
inline constexpr std::uint32_t kRetailTimeDateStamp = 0x54874238u;
inline constexpr std::uint32_t kRetailImageSize = 0x3E0000u;

inline constexpr std::size_t kMaterialShaderRecordOffset = 104;
inline constexpr std::size_t kShaderRecordFlagsOffset = 4;
inline constexpr std::size_t kShaderRecordTableOffset = 16;
inline constexpr std::uint32_t kShaderRecordOwnsTableFlag = 0x80000000u;

// No user-mode pointer is below 64 KiB; a 16-bit texture handle always is.
inline constexpr std::uint64_t kMinimumUserPointer = 0x10000u;

inline constexpr std::uintptr_t kMaterialShaderReleaseRva = 0x383E0;
inline constexpr std::uintptr_t kUnlitGenericStateRva = 0x2F4F0;
inline constexpr std::uintptr_t kVertexLitGenericStateRva = 0x31190;
inline constexpr std::uintptr_t kLightmappedGenericStateRva = 0x28FE0;
inline constexpr std::uintptr_t kShaderRecordAllocateRva = 0x72B80;
inline constexpr std::uintptr_t kMaterialVtableRva = 0x184DC8;

// Slot 9 of the same classes allocates exactly these sizes (sub_18002F100, sub_180030BC0, sub_180028610).
inline constexpr std::size_t kUnlitGenericTableBytes = 32;
inline constexpr std::size_t kVertexLitGenericTableBytes = 32;
inline constexpr std::size_t kLightmappedGenericTableBytes = 34;

inline constexpr std::uint8_t kExpectedMaterialShaderReleasePrologue[] = {
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
	0x41, 0x10, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC0
};
inline constexpr std::uint8_t kExpectedUnlitGenericStatePrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
	0x24, 0x18, 0x56, 0x57, 0x41, 0x54, 0x48, 0x83,
	0xEC, 0x40, 0x49, 0x8B, 0x58, 0x10
};
inline constexpr std::uint8_t kExpectedVertexLitGenericStatePrologue[] = {
	0x48, 0x8B, 0xC4, 0x56, 0x57, 0x48, 0x81, 0xEC,
	0xB8, 0x00, 0x00, 0x00, 0x49, 0x8B, 0x70, 0x10
};
inline constexpr std::uint8_t kExpectedLightmappedGenericStatePrologue[] = {
	0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x4C,
	0x89, 0x40, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54,
	0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81
};
inline constexpr std::uint8_t kExpectedShaderRecordAllocatePrologue[] = {
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
	0xD9, 0x48, 0x8B, 0x0D
};

using ShaderRecordAllocateFunction = void*(*)(std::size_t bytes);

namespace detail
{
[[nodiscard]] inline std::uint64_t ReadQword(const std::uint8_t* record, std::size_t offset) noexcept
{
	std::uint64_t value = 0;
	std::memcpy(&value, record + offset, sizeof(value));
	return value;
}

[[nodiscard]] inline std::uint32_t ReadDword(const std::uint8_t* record, std::size_t offset) noexcept
{
	std::uint32_t value = 0;
	std::memcpy(&value, record + offset, sizeof(value));
	return value;
}

inline void WriteQword(std::uint8_t* record, std::size_t offset, std::uint64_t value) noexcept
{
	std::memcpy(record + offset, &value, sizeof(value));
}

inline void WriteDword(std::uint8_t* record, std::size_t offset, std::uint32_t value) noexcept
{
	std::memcpy(record + offset, &value, sizeof(value));
}
}

[[nodiscard]] inline constexpr bool IsInlineTextureHandle(std::uint64_t tableSlot) noexcept
{
	return tableSlot != 0 && tableSlot < kMinimumUserPointer;
}

enum class ShaderRecordRepairResult
{
	intact,
	repaired,
	allocationFailed,
};

// Crash-site guard for the table shaders' state function (slot 11): when the pointer slot still holds another class's
// inline handle, give the record a fresh zeroed table and mark it owned, exactly what slot 9 would have done had it seen 0.
// `staleValue` (optional) receives the handle that was replaced.
[[nodiscard]] inline ShaderRecordRepairResult RepairShaderRecordTable(
	std::uint8_t* record,
	std::size_t tableBytes,
	ShaderRecordAllocateFunction allocate,
	std::uint64_t* staleValue = nullptr) noexcept
{
	if (!record || !allocate || tableBytes == 0)
		return ShaderRecordRepairResult::intact;
	const std::uint64_t slot = detail::ReadQword(record, kShaderRecordTableOffset);
	if (!IsInlineTextureHandle(slot))
		return ShaderRecordRepairResult::intact;
	if (staleValue)
		*staleValue = slot;

	void* const block = allocate(tableBytes);
	if (!block)
		return ShaderRecordRepairResult::allocationFailed;
	std::memset(block, 0, tableBytes);
	detail::WriteQword(
		record,
		kShaderRecordTableOffset,
		static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(block)));
	detail::WriteDword(
		record,
		kShaderRecordFlagsOffset,
		detail::ReadDword(record, kShaderRecordFlagsOffset) | kShaderRecordOwnsTableFlag);
	return ShaderRecordRepairResult::repaired;
}

// Owning-invariant fix, run when a material lets go of its shader: the pointer slot is only meaningful to the next shader
// when the material owns a heap block there (flag bit 31, the same test the CMaterial destructor uses before it frees).
// Otherwise the slot holds an inline handle (or a pointer whose low word an inline-handle shader overwrote) that must not
// survive into a different shader class. Nothing is freed: an unowned value is not an allocation. Returns true when the
// slot was cleared.
inline bool ClearUnownedShaderRecordTable(std::uint8_t* record) noexcept
{
	if (!record)
		return false;
	if ((detail::ReadDword(record, kShaderRecordFlagsOffset) & kShaderRecordOwnsTableFlag) != 0)
		return false;
	if (detail::ReadQword(record, kShaderRecordTableOffset) == 0)
		return false;
	detail::WriteQword(record, kShaderRecordTableOffset, 0);
	return true;
}
}
