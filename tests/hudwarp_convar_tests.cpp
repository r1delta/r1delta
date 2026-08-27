#include "../client/rendering/hudwarp_convars.h"

#include <array>
#include <iostream>
#include <string_view>

namespace
{
struct FakeConVar
{
	int id;
};

struct Registration
{
	std::string_view name;
	std::string_view value;
	int flags;
	std::string_view help;
};

bool Check(bool condition, const char* message)
{
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}

bool TestRuntimeConVarRegistration()
{
	constexpr int archiveFlag = 0x80;
	FakeConVar useGpu{1};
	FakeConVar disable{2};
	std::array<Registration, 2> registrations{};
	std::size_t count = 0;

	const auto conVars = r1delta::hudwarp::RegisterRuntimeConVars<FakeConVar>(
		[&](const char* name, const char* value, int flags, const char* help) {
			registrations[count] = {name, value, flags, help};
			return count++ == 0 ? &useGpu : &disable;
		},
		archiveFlag);

	return Check(count == registrations.size(), "unexpected HUD warp convar count")
		&& Check(conVars.useGpu == &useGpu, "GPU convar pointer was not retained")
		&& Check(conVars.disable == &disable, "disable convar pointer was not retained")
		&& Check(registrations[0].name == "hudwarp_use_gpu", "GPU convar name mismatch")
		&& Check(registrations[0].value == "1", "GPU convar is not enabled by default")
		&& Check(registrations[0].flags == archiveFlag, "GPU convar flags mismatch")
		&& Check(registrations[1].name == "hudwarp_disable", "disable convar name mismatch")
		&& Check(registrations[1].value == "0", "HUD warp is disabled by default")
		&& Check(registrations[1].flags == archiveFlag, "disable convar flags mismatch");
}
}

int main()
{
	if (!TestRuntimeConVarRegistration())
		return 1;
	std::cout << "hudwarp_convar_tests passed\n";
	return 0;
}
