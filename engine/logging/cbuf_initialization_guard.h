#pragma once

namespace r1delta::logging
{
template <typename Interface, typename FindVarFunction, typename FindCommandFunction>
bool TryUnhideConsoleCommands(
	bool& complete,
	Interface cvarInterface,
	FindVarFunction findVar,
	FindCommandFunction findCommand,
	int flagsToClear) noexcept
{
	if (complete)
		return true;
	if (!cvarInterface || !findVar || !findCommand)
		return false;

	auto* const updateRate = findVar(cvarInterface, "cl_updaterate");
	if (!updateRate)
		return false;
	auto* const help = findCommand(cvarInterface, "help");
	if (!help)
		return false;

	updateRate->m_nFlags &= ~flagsToClear;
	help->m_nFlags &= ~flagsToClear;
	complete = true;
	return true;
}
}
