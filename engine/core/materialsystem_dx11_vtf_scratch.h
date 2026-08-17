#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace r1delta::materialsystem_dx11
{
inline constexpr std::uintptr_t kVtfLoadRva = 0x6C340;
inline constexpr std::uintptr_t kVtfScratchPrepareRva = 0x67900;
inline constexpr std::uintptr_t kUtlBufferSetExternalRva = 0x116A10;
inline constexpr std::uintptr_t kVtfDecoderSelectRva = 0x6BCEC;
inline constexpr std::uintptr_t kVtfDecoderFactoryRva = 0x135240;
inline constexpr std::uintptr_t kVtfDecoderDestructorRva = 0x135270;
inline constexpr std::uintptr_t kVtfDecoderSlotsRva = 0x3A3448;
inline constexpr std::uintptr_t kVtfTlsIndexRva = 0x3B9A80;
inline constexpr std::size_t kVtfDecoderSlotCount = 2;
inline constexpr std::uint32_t kExpectedMaterialSystemTimeDateStamp = 0x54874238;
inline constexpr std::uint32_t kExpectedMaterialSystemImageSize = 0x3E0000;

inline constexpr std::uint8_t kExpectedVtfLoadPrologue[] = {
	0x40, 0x53,
	0x55,
	0x56,
	0x41, 0x54,
	0x48, 0x81, 0xEC, 0x38, 0x09, 0x00, 0x00,
	0x48, 0x8B, 0xF1,
	0x48, 0x8B, 0xDA,
	0x33, 0xED,
	0x48, 0x8D, 0x4C, 0x24, 0x21,
};

inline constexpr std::uint8_t kExpectedVtfScratchPreparePrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08,
	0x48, 0x89, 0x6C, 0x24, 0x10,
	0x48, 0x89, 0x74, 0x24, 0x20,
	0x57,
	0x48, 0x83, 0xEC, 0x30,
};

inline constexpr std::uint8_t kExpectedUtlBufferSetExternalPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08,
	0x57,
	0x48, 0x83, 0xEC, 0x20,
	0x49, 0x8B, 0xF9,
	0x48, 0x8B, 0xD9,
};
inline constexpr std::uint8_t kExpectedVtfDecoderSelectBytes[] = {
	0x44, 0x8B, 0x15, 0x8D, 0xDD, 0x34, 0x00,
	0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00,
	0x4C, 0x8B, 0xF9,
	0x4A, 0x8B, 0x04, 0xD0,
	0xB9, 0x10, 0x00, 0x00, 0x00,
	0x48, 0x89, 0x9D, 0xD8, 0x00, 0x00, 0x00,
	0x8B, 0x1C, 0x01,
	0x48, 0x8D, 0x35, 0x2F, 0x77, 0x33, 0x00,
	0x4D, 0x8B, 0xE1,
	0x4C, 0x8B, 0x34, 0xDE,
};

inline constexpr std::uint8_t kExpectedVtfDecoderFactoryPrologue[] = {
	0x48, 0x83, 0xEC, 0x28,
	0xB9, 0x28, 0x01, 0x00, 0x00,
	0xE8, 0x32, 0xD9, 0xF3, 0xFF,
	0x48, 0x85, 0xC0,
	0x74, 0x0C,
	0x48,
};

inline constexpr std::uint8_t kExpectedVtfDecoderDestructorPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08,
	0x57,
	0x48, 0x83, 0xEC, 0x20,
	0x8B, 0xDA,
	0x48, 0x8B, 0xF9,
	0xE8, 0x8C, 0xE0, 0xFF, 0xFF,
	0xF6, 0xC3, 0x01,
	0x74,
};


enum class VtfScratchError
{
	none,
	noActiveLoad,
	invalidSize,
	allocationFailed,
	invalidDecoderSlot,
	decoderAllocationFailed,
	decoderSlotChanged,
};

struct VtfScratchBuffer
{
	void* data = nullptr;
	std::size_t capacity = 0;
	VtfScratchError error = VtfScratchError::none;

	explicit operator bool() const noexcept
	{
		return error == VtfScratchError::none && data && capacity;
	}
};

class VtfScratchContext final
{
public:
	VtfScratchContext() = default;
	~VtfScratchContext();

	VtfScratchContext(const VtfScratchContext&) = delete;
	VtfScratchContext& operator=(const VtfScratchContext&) = delete;
	VtfScratchContext(VtfScratchContext&&) = delete;
	VtfScratchContext& operator=(VtfScratchContext&&) = delete;

	bool EnterLoad() noexcept;
	bool LeaveLoad() noexcept;
	VtfScratchBuffer Prepare(std::int64_t requestedBytes) noexcept;

	std::size_t Depth() const noexcept;
	std::size_t FrameCount() const noexcept;

private:
	struct Frame
	{
		void* data = nullptr;
		std::size_t capacity = 0;
	};

	static void Release(Frame& frame) noexcept;

	std::vector<Frame> frames_;
	std::size_t depth_ = 0;
};

class VtfScratchLoadScope final
{
public:
	VtfScratchLoadScope() noexcept;
	~VtfScratchLoadScope();

	VtfScratchLoadScope(const VtfScratchLoadScope&) = delete;
	VtfScratchLoadScope& operator=(const VtfScratchLoadScope&) = delete;
	VtfScratchLoadScope(VtfScratchLoadScope&&) = delete;
	VtfScratchLoadScope& operator=(VtfScratchLoadScope&&) = delete;

	bool Entered() const noexcept;
	bool Close() noexcept;

private:
	bool active_ = false;
};

using VtfDecoderCreateFunction = void* (*)();
using VtfDecoderDestroyFunction = void* (*)(void* decoder, unsigned char flags);

class VtfDecoderSlotScope final
{
public:
	VtfDecoderSlotScope(
		std::size_t loadDepth,
		void** decoderSlot,
		VtfDecoderCreateFunction createDecoder,
		VtfDecoderDestroyFunction destroyDecoder) noexcept;
	~VtfDecoderSlotScope();

	VtfDecoderSlotScope(const VtfDecoderSlotScope&) = delete;
	VtfDecoderSlotScope& operator=(const VtfDecoderSlotScope&) = delete;
	VtfDecoderSlotScope(VtfDecoderSlotScope&&) = delete;
	VtfDecoderSlotScope& operator=(VtfDecoderSlotScope&&) = delete;

	bool Entered() const noexcept;
	bool Replaced() const noexcept;
	VtfScratchError Error() const noexcept;
	bool Close() noexcept;

private:
	void** decoderSlot_ = nullptr;
	void* previousDecoder_ = nullptr;
	void* replacementDecoder_ = nullptr;
	VtfDecoderDestroyFunction destroyDecoder_ = nullptr;
	VtfScratchError error_ = VtfScratchError::none;
	bool active_ = false;
};

VtfScratchBuffer PrepareThreadVtfScratch(std::int64_t requestedBytes) noexcept;
std::size_t ThreadVtfScratchDepth() noexcept;
std::size_t ThreadVtfScratchFrameCount() noexcept;
}
