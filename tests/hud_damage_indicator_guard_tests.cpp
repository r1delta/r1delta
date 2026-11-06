#include "../client/hud_damage_indicator_guard.h"

#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{
using namespace r1delta::hud_damage_indicator;

bool Check(bool condition, const char* message)
{
	if (!condition)
		std::cerr << "FAILED: " << message << '\n';
	return condition;
}

// Stands in for a CHudDamageIndicator: only the slot index the constructor stores matters.
struct FakeIndicator
{
	alignas(16) std::uint8_t bytes[kSlotOffset + sizeof(int)] = {};

	explicit FakeIndicator(int slot) { std::memcpy(bytes + kSlotOffset, &slot, sizeof(slot)); }
};

// The level-shutdown case: the panel that owns slot 0 is destroyed and the slot must not keep
// pointing at it.
bool TestOwnedSlotIsReleased()
{
	FakeIndicator indicator(0);
	void* slots[2] = { &indicator, nullptr };
	ReleaseSlot(slots, &indicator);
	return Check(slots[0] == nullptr, "destroyed indicator still registered in its slot");
}

// A newer panel already took the slot (constructor ran before the old destructor): keep it.
bool TestNewerIndicatorIsKept()
{
	FakeIndicator stale(0);
	FakeIndicator current(0);
	void* slots[2] = { &current, nullptr };
	ReleaseSlot(slots, &stale);
	return Check(slots[0] == &current, "destroying an old indicator unregistered the current one");
}

bool TestOtherSlotsAreUntouched()
{
	FakeIndicator first(0);
	FakeIndicator second(1);
	void* slots[2] = { &first, &second };
	ReleaseSlot(slots, &second);

	bool passed = true;
	passed &= Check(slots[1] == nullptr, "indicator in slot 1 not released");
	passed &= Check(slots[0] == &first, "releasing slot 1 touched slot 0");
	return passed;
}

bool TestNegativeSlotIsIgnored()
{
	FakeIndicator indicator(-1);
	void* slots[2] = { &indicator, &indicator };
	ReleaseSlot(slots, &indicator);
	return Check(slots[0] == &indicator && slots[1] == &indicator, "negative slot index wrote the table");
}
} // namespace

int main()
{
	bool passed = true;
	passed &= TestOwnedSlotIsReleased();
	passed &= TestNewerIndicatorIsKept();
	passed &= TestOtherSlotsAreUntouched();
	passed &= TestNegativeSlotIsIgnored();

	if (!passed)
		return 1;

	std::cout << "hud_damage_indicator_guard_tests passed\n";
	return 0;
}
