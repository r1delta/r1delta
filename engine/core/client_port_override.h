#pragma once

#include <charconv>
#include <system_error>

struct ConVarR1;

namespace r1delta::client_port
{
using SetStringFunction = void (*)(ConVarR1* variable, const char* value);

inline bool ApplyOverride(
	ConVarR1* variable,
	int port,
	SetStringFunction setString) noexcept
{
	if (!variable || !setString || port <= 0 || port > 65535)
		return false;

	char value[6]{};
	const auto result = std::to_chars(value, value + sizeof(value) - 1, port);
	if (result.ec != std::errc{})
		return false;

	*result.ptr = '\0';
	setString(variable, value);
	return true;
}
}
