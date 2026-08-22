#include "../shared/script_variant_abi.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <utility>

namespace
{
using namespace r1delta::script_variant;

struct TestVariant
{
	std::uint64_t payload;
	std::int16_t type;
	std::int16_t flags;
	std::uint32_t padding;
};

static_assert(sizeof(TestVariant) == kScriptVariantSize);
static_assert(offsetof(TestVariant, payload) == kScriptVariantPayloadOffset);
static_assert(offsetof(TestVariant, type) == kScriptVariantTypeOffset);
static_assert(offsetof(TestVariant, flags) == kScriptVariantFlagsOffset);

bool Check(bool condition, const char* message)
{
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}

bool TestTypeConversionRoundTrips()
{
	constexpr std::array<std::pair<std::int16_t, std::int16_t>, 7> pairs = {{
		{ std::int16_t{-7}, std::int16_t{-7} },
		{ std::int16_t{0}, std::int16_t{0} },
		{ std::int16_t{5}, std::int16_t{5} },
		{ std::int16_t{6}, std::int16_t{7} },
		{ std::int16_t{8}, std::int16_t{9} },
		{ std::int16_t{32}, std::int16_t{33} },
		{ std::int16_t{33}, std::int16_t{34} },
	}};
	for (const auto& [r1Type, r1oType] : pairs) {
		std::int16_t converted = 0x1234;
		if (!Check(
				ConvertType(r1Type, ABI::R1, ABI::R1O, converted),
				"R1 to R1O conversion failed")
			|| !Check(converted == r1oType, "R1 to R1O conversion mismatch")) {
			return false;
		}

		std::int16_t roundTrip = 0x1234;
		if (!Check(
				ConvertType(converted, ABI::R1O, ABI::R1, roundTrip),
				"R1O to R1 conversion failed")
			|| !Check(roundTrip == r1Type, "type conversion round trip mismatch")) {
			return false;
		}
	}

	std::int16_t unchangedAbi = 0;
	return Check(
			ConvertType(6, ABI::R1O, ABI::R1O, unchangedAbi),
			"same-ABI conversion failed")
		&& Check(unchangedAbi == 6, "same-ABI conversion changed the type");
}

bool TestUnsupportedConversionsDoNotMutateOutput()
{
	std::int16_t converted = 0x1234;
	if (!Check(
			!ConvertType(6, ABI::R1O, ABI::R1, converted),
			"unsupported R1O gap type was accepted")
		|| !Check(converted == 0x1234, "failed type conversion mutated output")) {
		return false;
	}

	converted = 0x1234;
	if (!Check(
			!ConvertType(
				std::numeric_limits<std::int16_t>::max(),
				ABI::R1,
				ABI::R1O,
				converted),
			"overflowing R1 type was accepted")
		|| !Check(converted == 0x1234, "overflowing conversion mutated output")) {
		return false;
	}

	const TestVariant source{
		0x0123456789ABCDEFu,
		6,
		0x0003,
		0xA5A5A5A5u,
	};
	TestVariant destination{
		0xFEDCBA9876543210u,
		-123,
		0x0012,
		0x5A5A5A5Au,
	};
	const TestVariant originalDestination = destination;
	return Check(
			!ConvertVariant(source, ABI::R1O, ABI::R1, destination),
			"variant with unsupported R1O gap type was accepted")
		&& Check(
			std::memcmp(
				&destination,
				&originalDestination,
				sizeof(destination)) == 0,
			"failed variant conversion mutated output");
}

bool TestVariantConversionPreservesBytesExceptType()
{
	const TestVariant source{
		0x0123456789ABCDEFu,
		32,
		0x0003,
		0xA5A5A5A5u,
	};
	TestVariant converted{};
	if (!Check(
			ConvertVariant(source, ABI::R1, ABI::R1O, converted),
			"variant conversion failed")
		|| !Check(converted.payload == source.payload, "variant payload changed")
		|| !Check(converted.type == 33, "variant output type mismatch")
		|| !Check(converted.flags == source.flags, "variant flags changed")
		|| !Check(converted.padding == source.padding, "variant trailing bytes changed")) {
		return false;
	}

	TestVariant roundTrip{};
	return Check(
			ConvertVariant(converted, ABI::R1O, ABI::R1, roundTrip),
			"variant round trip conversion failed")
		&& Check(
			std::memcmp(&roundTrip, &source, sizeof(source)) == 0,
			"variant round trip did not recreate the source bytes");
}

bool TestVtableSlotMapping()
{
	if (!Check(kR1VtableSlotCount == 120, "unexpected R1 vtable size")
		|| !Check(kTfoVtableSlotCount == 122, "unexpected TFO vtable size")
		|| !Check(kTfoVtableInsertionSlot == 3, "unexpected proxy insertion slot")
		|| !Check(kTfoVtableInsertionCount == 2, "unexpected proxy insertion count")
		|| !Check(kTfoSetPerVmFlagSlot == 3, "proxy set-flag slot is not 3")
		|| !Check(kTfoGetPerVmFlagSlot == 4, "proxy get-flag slot is not 4")) {
		return false;
	}

	for (std::size_t sourceSlot = 0;
		sourceSlot < kR1VtableSlotCount;
		++sourceSlot) {
		const std::size_t expected = sourceSlot < 3
			? sourceSlot
			: sourceSlot + 2;
		const std::size_t target = TargetSlotForSource(sourceSlot);
		if (!Check(target == expected, "source-to-target slot mapping mismatch")
			|| !Check(target != 3 && target != 4, "source mapped over an insertion slot")) {
			return false;
		}
	}

	return Check(
			TargetSlotForSource(kR1VtableSlotCount) == kInvalidVtableSlot,
			"out-of-range source slot was mapped")
		&& Check(TargetSlotForSource(2) == 2, "source slot 2 moved")
		&& Check(TargetSlotForSource(3) == 5, "source slot 3 was not shifted")
		&& Check(TargetSlotForSource(4) == 6, "source slot 4 was not shifted")
		&& Check(TargetSlotForSource(119) == 121, "last source slot mismatch");
}

bool TestCompleteAdapterInventory()
{
	constexpr std::array<std::size_t, 19> expectedAdapterSlots = {
		32, 33, 34, 36, 41, 44, 45, 46, 48, 49,
		50, 52, 53, 56, 57, 59, 61, 82, 84,
	};
	if (!Check(HasCompleteVtableSlotInventory(), "vtable inventory is incomplete")
		|| !Check(
			kVtableSlotInventory.size() == kR1VtableSlotCount,
			"vtable inventory has the wrong size")) {
		return false;
	}

	std::size_t adapterCount = 0;
	for (std::size_t sourceSlot = 0;
		sourceSlot < kR1VtableSlotCount;
		++sourceSlot) {
		bool expectedAdapter = false;
		for (const std::size_t expectedSlot : expectedAdapterSlots)
			expectedAdapter = expectedAdapter || sourceSlot == expectedSlot;

		const auto& entry = kVtableSlotInventory[sourceSlot];
		const bool inventoriedAsAdapter =
			entry.disposition == SourceSlotDisposition::Adapter;
		if (inventoriedAsAdapter)
			++adapterCount;
		if (!Check(IsSourceSlotInventoried(sourceSlot), "source slot is not inventoried")
			|| !Check(entry.sourceSlot == sourceSlot, "inventory source slot mismatch")
			|| !Check(
				entry.targetSlot == TargetSlotForSource(sourceSlot),
				"inventory target slot mismatch")
			|| !Check(
				IsAdapterSourceSlot(sourceSlot) == expectedAdapter,
				"adapter predicate mismatch")
			|| !Check(
				inventoriedAsAdapter == expectedAdapter,
				"adapter inventory mismatch")) {
			return false;
		}
	}
	return Check(
		adapterCount == expectedAdapterSlots.size(),
		"adapter inventory count mismatch");
}

bool TestSlot55IsGenericNoOpCandidate()
{
	const auto& entry = kVtableSlotInventory[kGenericNoOpSourceSlot];
	return Check(kGenericNoOpSourceSlot == 55, "generic no-op slot is not 55")
		&& Check(!IsAdapterSourceSlot(55), "slot 55 was classified as an adapter")
		&& Check(IsGenericNoOpSourceSlot(55), "slot 55 is not a generic no-op")
		&& Check(
			entry.disposition == SourceSlotDisposition::GenericNoOpCandidate,
			"slot 55 inventory classification mismatch")
		&& Check(
			entry.targetSlot == 57,
			"slot 55 target mapping mismatch");
}

bool TestDescriptorLayouts()
{
	return Check(kFunctionDescriptorSize == 0x78, "function descriptor size mismatch")
		&& Check(kFunctionDescriptorReturnTypeOffset == 0x38, "return-type offset mismatch")
		&& Check(kFunctionDescriptorParameterVectorBaseOffset == 0x40, "parameter-vector offset mismatch")
		&& Check(kFunctionDescriptorParameterCountOffset == 0x58, "parameter-count offset mismatch")
		&& Check(kFunctionDescriptorBindingOffset == 0x60, "binding offset mismatch")
		&& Check(kFunctionDescriptorFunctionOffset == 0x68, "function offset mismatch")
		&& Check(kFunctionDescriptorFlagsOffset == 0x70, "flags offset mismatch")
		&& Check(kClassDescriptorSize == 0x60, "class descriptor size mismatch")
		&& Check(kClassDescriptorBaseOffset == 0x18, "class base offset mismatch")
		&& Check(kClassDescriptorFunctionVectorBaseOffset == 0x20, "class function-vector offset mismatch")
		&& Check(kClassDescriptorFunctionCountOffset == 0x38, "class function-count offset mismatch");
}

bool TestRegistrationKindBoundary()
{
	return Check(
			kTextFunctionRegistrationFlag == 0x02,
			"text registration flag mismatch")
		&& Check(
			!ShouldAdaptTypedRegistration(
				kTextFunctionRegistrationFlag),
			"text registration was classified as typed")
		&& Check(
			!ShouldAdaptTypedRegistration(
				kTextFunctionRegistrationFlag | 0x01),
			"text registration with other flags was classified as typed")
		&& Check(
			ShouldAdaptTypedRegistration(0),
			"typed registration was not adapted");
}

bool TestVtableRecreationComparison()
{
	std::array<std::uintptr_t, kR1VtableSlotCount> existing{};
	std::array<std::uintptr_t, kR1VtableSlotCount> requested{};
	for (std::size_t slot = 0; slot < existing.size(); ++slot) {
		existing[slot] = 0x100000u + slot * 0x10u;
		requested[slot] = existing[slot];
	}

	if (!Check(
			VtableDestinationsEqual(
				existing.data(),
				requested.data()),
			"equal vtable destinations did not compare equal")
		|| !Check(
			!VtableNeedsRecreation(
				existing.data(),
				requested.data()),
			"equal destinations requested vtable recreation")) {
		return false;
	}

	requested[84] += 1;
	return Check(
			!VtableDestinationsEqual(
				existing.data(),
				requested.data()),
			"mismatched vtable destinations compared equal")
		&& Check(
			VtableNeedsRecreation(
				existing.data(),
				requested.data()),
			"mismatched destinations did not request recreation")
		&& Check(
			VtableNeedsRecreation<std::uintptr_t>(
				nullptr,
				requested.data()),
			"missing existing destinations did not request recreation");
}
}

int main()
{
	const bool passed = TestTypeConversionRoundTrips()
		&& TestUnsupportedConversionsDoNotMutateOutput()
		&& TestVariantConversionPreservesBytesExceptType()
		&& TestVtableSlotMapping()
		&& TestCompleteAdapterInventory()
		&& TestSlot55IsGenericNoOpCandidate()
		&& TestDescriptorLayouts()
		&& TestRegistrationKindBoundary()
		&& TestVtableRecreationComparison();
	if (!passed)
		return 1;
	std::cout << "script_variant_abi_tests passed\n";
	return 0;
}
