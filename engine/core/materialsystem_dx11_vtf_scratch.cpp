#include "materialsystem_dx11_vtf_scratch.h"

#include <Windows.h>

#include <algorithm>
#include <limits>
#include <new>

namespace r1delta::materialsystem_dx11
{
namespace
{
	constexpr std::size_t kMinimumScratchCapacity = 0x200000;
	constexpr std::size_t kRetainedScratchLimit = 0x400000;
	constexpr std::size_t kVirtualAllocationAlignment = 0x10000;
	// VirtualAlloc addresses are allocation-granularity aligned, which is
	// stricter than the sector alignment required by AllocOptimalReadBuffer.

	bool RoundCapacity(std::int64_t requestedBytes, std::size_t& capacity) noexcept
	{
		if (requestedBytes < 0)
			return false;

		const auto requested = static_cast<std::uint64_t>(requestedBytes);
		if (requested > (std::numeric_limits<std::size_t>::max)())
			return false;

		const std::size_t unaligned = (std::max)(
			static_cast<std::size_t>(requested),
			kMinimumScratchCapacity);
		if (unaligned > (std::numeric_limits<std::size_t>::max)()
			- (kVirtualAllocationAlignment - 1)) {
			return false;
		}

		capacity = (unaligned + kVirtualAllocationAlignment - 1)
			& ~(kVirtualAllocationAlignment - 1);
		return true;
	}

	VtfScratchContext& ThreadContext() noexcept
	{
		static thread_local VtfScratchContext context;
		return context;
	}
}

VtfScratchContext::~VtfScratchContext()
{
	for (Frame& frame : frames_)
		Release(frame);
}

bool VtfScratchContext::EnterLoad() noexcept
{
	if (depth_ == (std::numeric_limits<std::size_t>::max)())
		return false;

	if (depth_ == frames_.size()) {
		try {
			frames_.emplace_back();
		}
		catch (const std::bad_alloc&) {
			return false;
		}
	}

	++depth_;
	return true;
}

bool VtfScratchContext::LeaveLoad() noexcept
{
	if (!depth_)
		return false;

	Frame& frame = frames_[--depth_];
	if (frame.capacity >= kRetainedScratchLimit)
		Release(frame);
	return true;
}

VtfScratchBuffer VtfScratchContext::Prepare(std::int64_t requestedBytes) noexcept
{
	if (!depth_)
		return { nullptr, 0, VtfScratchError::noActiveLoad };

	std::size_t requiredCapacity = 0;
	if (!RoundCapacity(requestedBytes, requiredCapacity))
		return { nullptr, 0, VtfScratchError::invalidSize };

	Frame& frame = frames_[depth_ - 1];
	if (frame.capacity < requiredCapacity) {
		void* replacement = VirtualAlloc(
			nullptr,
			requiredCapacity,
			MEM_COMMIT | MEM_RESERVE,
			PAGE_READWRITE);
		if (!replacement)
			return { nullptr, 0, VtfScratchError::allocationFailed };

		Release(frame);
		frame.data = replacement;
		frame.capacity = requiredCapacity;
	}

	return { frame.data, frame.capacity, VtfScratchError::none };
}

std::size_t VtfScratchContext::Depth() const noexcept
{
	return depth_;
}

std::size_t VtfScratchContext::FrameCount() const noexcept
{
	return frames_.size();
}

void VtfScratchContext::Release(Frame& frame) noexcept
{
	if (frame.data)
		VirtualFree(frame.data, 0, MEM_RELEASE);
	frame = {};
}

VtfScratchLoadScope::VtfScratchLoadScope() noexcept
	: active_(ThreadContext().EnterLoad())
{
}

VtfScratchLoadScope::~VtfScratchLoadScope()
{
	Close();
}

bool VtfScratchLoadScope::Entered() const noexcept
{
	return active_;
}

bool VtfScratchLoadScope::Close() noexcept
{
	if (!active_)
		return false;
	active_ = false;
	return ThreadContext().LeaveLoad();
}

VtfDecoderSlotScope::VtfDecoderSlotScope(
	std::size_t loadDepth,
	void** decoderSlot,
	VtfDecoderCreateFunction createDecoder,
	VtfDecoderDestroyFunction destroyDecoder) noexcept
{
	if (!loadDepth) {
		error_ = VtfScratchError::noActiveLoad;
		return;
	}

	if (loadDepth == 1) {
		active_ = true;
		return;
	}

	if (!decoderSlot || !createDecoder || !destroyDecoder) {
		error_ = VtfScratchError::invalidDecoderSlot;
		return;
	}

	void* const replacement = createDecoder();
	if (!replacement) {
		error_ = VtfScratchError::decoderAllocationFailed;
		return;
	}

	decoderSlot_ = decoderSlot;
	previousDecoder_ = *decoderSlot;
	replacementDecoder_ = replacement;
	destroyDecoder_ = destroyDecoder;
	*decoderSlot_ = replacementDecoder_;
	active_ = true;
}

VtfDecoderSlotScope::~VtfDecoderSlotScope()
{
	Close();
}

bool VtfDecoderSlotScope::Entered() const noexcept
{
	return active_;
}

bool VtfDecoderSlotScope::Replaced() const noexcept
{
	return replacementDecoder_ != nullptr;
}

VtfScratchError VtfDecoderSlotScope::Error() const noexcept
{
	return error_;
}

bool VtfDecoderSlotScope::Close() noexcept
{
	if (!active_)
		return false;
	active_ = false;

	if (!replacementDecoder_)
		return true;

	if (*decoderSlot_ != replacementDecoder_) {
		error_ = VtfScratchError::decoderSlotChanged;
		return false;
	}

	*decoderSlot_ = previousDecoder_;
	destroyDecoder_(replacementDecoder_, 1);
	replacementDecoder_ = nullptr;
	return true;
}

VtfScratchBuffer PrepareThreadVtfScratch(std::int64_t requestedBytes) noexcept
{
	return ThreadContext().Prepare(requestedBytes);
}

std::size_t ThreadVtfScratchDepth() noexcept
{
	return ThreadContext().Depth();
}

std::size_t ThreadVtfScratchFrameCount() noexcept
{
	return ThreadContext().FrameCount();
}
}
