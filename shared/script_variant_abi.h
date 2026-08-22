#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace r1delta::script_variant
{
enum class ABI
{
	R1,
	R1O,
};

inline constexpr std::size_t kScriptVariantSize = 0x10;
inline constexpr std::size_t kScriptVariantPayloadOffset = 0x00;
inline constexpr std::size_t kScriptVariantTypeOffset = 0x08;
inline constexpr std::size_t kScriptVariantFlagsOffset = 0x0A;

[[nodiscard]] inline constexpr bool ConvertType(
	std::int16_t src,
	ABI source,
	ABI destination,
	std::int16_t& dst) noexcept
{
	std::int16_t converted = src;
	if (source != destination && src > 5) {
		if (source == ABI::R1) {
			if (src == std::numeric_limits<std::int16_t>::max())
				return false;
			converted = static_cast<std::int16_t>(src + 1);
		}
		else {
			if (src == 6)
				return false;
			converted = static_cast<std::int16_t>(src - 1);
		}
	}
	dst = converted;
	return true;
}

template <typename SourceVariant, typename DestinationVariant>
[[nodiscard]] inline bool ConvertVariant(
	const SourceVariant& src,
	ABI source,
	ABI destination,
	DestinationVariant& dst) noexcept
{
	static_assert(sizeof(SourceVariant) == kScriptVariantSize);
	static_assert(sizeof(DestinationVariant) == kScriptVariantSize);
	static_assert(std::is_trivially_copyable_v<SourceVariant>);
	static_assert(std::is_trivially_copyable_v<DestinationVariant>);

	std::array<std::byte, kScriptVariantSize> converted{};
	std::memcpy(converted.data(), &src, converted.size());

	std::int16_t sourceType{};
	std::memcpy(
		&sourceType,
		converted.data() + kScriptVariantTypeOffset,
		sizeof(sourceType));
	std::int16_t destinationType{};
	if (!ConvertType(sourceType, source, destination, destinationType))
		return false;

	std::memcpy(
		converted.data() + kScriptVariantTypeOffset,
		&destinationType,
		sizeof(destinationType));
	std::memcpy(&dst, converted.data(), converted.size());
	return true;
}

inline constexpr std::size_t kR1VtableSlotCount = 120;
inline constexpr std::size_t kTfoVtableSlotCount = 122;
inline constexpr std::size_t kTfoVtableInsertionSlot = 3;
inline constexpr std::size_t kTfoVtableInsertionCount = 2;
inline constexpr std::size_t kTfoSetPerVmFlagSlot = 3;
inline constexpr std::size_t kTfoGetPerVmFlagSlot = 4;
inline constexpr std::size_t kInvalidVtableSlot =
	std::numeric_limits<std::size_t>::max();

[[nodiscard]] inline constexpr std::size_t TargetSlotForSource(
	std::size_t sourceSlot) noexcept
{
	if (sourceSlot >= kR1VtableSlotCount)
		return kInvalidVtableSlot;
	return sourceSlot < kTfoVtableInsertionSlot
		? sourceSlot
		: sourceSlot + kTfoVtableInsertionCount;
}

enum class SourceSlotDisposition : std::uint8_t
{
	Generic,
	Adapter,
	GenericNoOpCandidate,
};

struct VtableSlotInventoryEntry
{
	std::size_t sourceSlot;
	std::size_t targetSlot;
	SourceSlotDisposition disposition;
};

inline constexpr std::array<std::size_t, 19> kAdapterSourceSlots = {
	32, 33, 34, 36, 41, 44, 45, 46, 48, 49,
	50, 52, 53, 56, 57, 59, 61, 82, 84,
};
inline constexpr std::size_t kGenericNoOpSourceSlot = 55;

[[nodiscard]] inline constexpr bool IsAdapterSourceSlot(
	std::size_t sourceSlot) noexcept
{
	for (const std::size_t adapterSlot : kAdapterSourceSlots) {
		if (sourceSlot == adapterSlot)
			return true;
	}
	return false;
}

[[nodiscard]] inline constexpr SourceSlotDisposition DispositionForSourceSlot(
	std::size_t sourceSlot) noexcept
{
	if (sourceSlot == kGenericNoOpSourceSlot)
		return SourceSlotDisposition::GenericNoOpCandidate;
	return IsAdapterSourceSlot(sourceSlot)
		? SourceSlotDisposition::Adapter
		: SourceSlotDisposition::Generic;
}

[[nodiscard]] inline constexpr bool IsGenericNoOpSourceSlot(
	std::size_t sourceSlot) noexcept
{
	return sourceSlot < kR1VtableSlotCount
		&& DispositionForSourceSlot(sourceSlot)
			== SourceSlotDisposition::GenericNoOpCandidate;
}

[[nodiscard]] inline constexpr std::array<
	VtableSlotInventoryEntry,
	kR1VtableSlotCount> MakeVtableSlotInventory() noexcept
{
	std::array<VtableSlotInventoryEntry, kR1VtableSlotCount> inventory{};
	for (std::size_t sourceSlot = 0;
		sourceSlot < inventory.size();
		++sourceSlot) {
		inventory[sourceSlot] = {
			sourceSlot,
			TargetSlotForSource(sourceSlot),
			DispositionForSourceSlot(sourceSlot),
		};
	}
	return inventory;
}

inline constexpr auto kVtableSlotInventory = MakeVtableSlotInventory();

[[nodiscard]] inline constexpr bool IsSourceSlotInventoried(
	std::size_t sourceSlot) noexcept
{
	return sourceSlot < kVtableSlotInventory.size()
		&& kVtableSlotInventory[sourceSlot].sourceSlot == sourceSlot
		&& kVtableSlotInventory[sourceSlot].targetSlot
			== TargetSlotForSource(sourceSlot);
}

[[nodiscard]] inline constexpr bool HasCompleteVtableSlotInventory() noexcept
{
	for (std::size_t sourceSlot = 0;
		sourceSlot < kR1VtableSlotCount;
		++sourceSlot) {
		if (!IsSourceSlotInventoried(sourceSlot))
			return false;
		const auto& entry = kVtableSlotInventory[sourceSlot];
		if ((entry.disposition == SourceSlotDisposition::Adapter)
			!= IsAdapterSourceSlot(sourceSlot)) {
			return false;
		}
		if ((entry.disposition
				== SourceSlotDisposition::GenericNoOpCandidate)
			!= (sourceSlot == kGenericNoOpSourceSlot)) {
			return false;
		}
	}
	return true;
}

static_assert(HasCompleteVtableSlotInventory());

template <typename Destination>
[[nodiscard]] inline constexpr bool VtableDestinationsEqual(
	const Destination* existing,
	const Destination* requested) noexcept
{
	if (!existing || !requested)
		return false;
	for (std::size_t slot = 0; slot < kR1VtableSlotCount; ++slot) {
		if (existing[slot] != requested[slot])
			return false;
	}
	return true;
}

template <typename Destination>
[[nodiscard]] inline constexpr bool VtableNeedsRecreation(
	const Destination* existing,
	const Destination* requested) noexcept
{
	return !VtableDestinationsEqual(existing, requested);
}

inline constexpr std::size_t kFunctionDescriptorSize = 0x78;
inline constexpr std::uint64_t kTextFunctionRegistrationFlag = 0x02;

[[nodiscard]] inline constexpr bool ShouldAdaptTypedRegistration(
	std::uint64_t flags) noexcept
{
	return (flags & kTextFunctionRegistrationFlag) == 0;
}

inline constexpr std::size_t kFunctionDescriptorReturnTypeOffset = 0x38;
inline constexpr std::size_t kFunctionDescriptorParameterVectorBaseOffset = 0x40;
inline constexpr std::size_t kFunctionDescriptorParameterCountOffset = 0x58;
inline constexpr std::size_t kFunctionDescriptorBindingOffset = 0x60;
inline constexpr std::size_t kFunctionDescriptorFunctionOffset = 0x68;
inline constexpr std::size_t kFunctionDescriptorFlagsOffset = 0x70;

inline constexpr std::size_t kClassDescriptorSize = 0x60;
inline constexpr std::size_t kClassDescriptorBaseOffset = 0x18;
inline constexpr std::size_t kClassDescriptorFunctionVectorBaseOffset = 0x20;
inline constexpr std::size_t kClassDescriptorFunctionCountOffset = 0x38;
}