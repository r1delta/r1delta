#include "../engine/logging/cbuf_initialization_guard.h"

#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{
using namespace r1delta::logging;

struct FakeVar
{
	int m_nFlags;
};

struct FakeCommand
{
	int m_nFlags;
};

using FindVarFunction = FakeVar* (__fastcall*)(std::uintptr_t, const char*);
using FindCommandFunction = FakeCommand* (__fastcall*)(std::uintptr_t, const char*);

int g_findVarCalls;
int g_findCommandCalls;
std::uintptr_t g_lastInterface;
const char* g_lastVarName;
const char* g_lastCommandName;
FakeVar* g_varResult;
FakeCommand* g_commandResult;

bool Check(bool condition, const char* message)
{
	if (!condition)
		std::cerr << "FAILED: " << message << '\n';
	return condition;
}

void Reset()
{
	g_findVarCalls = 0;
	g_findCommandCalls = 0;
	g_lastInterface = 0;
	g_lastVarName = nullptr;
	g_lastCommandName = nullptr;
	g_varResult = nullptr;
	g_commandResult = nullptr;
}

FakeVar* __fastcall FindVar(std::uintptr_t cvarInterface, const char* name)
{
	++g_findVarCalls;
	g_lastInterface = cvarInterface;
	g_lastVarName = name;
	return g_varResult;
}

FakeCommand* __fastcall FindCommand(std::uintptr_t cvarInterface, const char* name)
{
	++g_findCommandCalls;
	g_lastInterface = cvarInterface;
	g_lastCommandName = name;
	return g_commandResult;
}

bool TestMissingDependenciesDeferWithoutCalling()
{
	Reset();
	bool complete = false;
	bool passed = true;
	passed &= Check(!TryUnhideConsoleCommands(
		complete, std::uintptr_t{ 0 }, &FindVar, &FindCommand, 0x30),
		"missing cvar interface reported completion");
	passed &= Check(!TryUnhideConsoleCommands(
		complete, std::uintptr_t{ 1 }, FindVarFunction{}, &FindCommand, 0x30),
		"missing FindVar callback reported completion");
	passed &= Check(!TryUnhideConsoleCommands(
		complete, std::uintptr_t{ 1 }, &FindVar, FindCommandFunction{}, 0x30),
		"missing FindCommand callback reported completion");
	passed &= Check(!complete, "missing dependencies committed completion");
	passed &= Check(g_findVarCalls == 0 && g_findCommandCalls == 0,
		"missing dependencies invoked a callback");
	return passed;
}

bool TestLookupFailureRemainsRetryable()
{
	Reset();
	bool complete = false;
	FakeVar var{ 0x75 };
	FakeCommand command{ 0xB6 };
	g_commandResult = &command;

	bool passed = true;
	passed &= Check(!TryUnhideConsoleCommands(
		complete, std::uintptr_t{ 0x1234 }, &FindVar, &FindCommand, 0x30),
		"missing variable reported completion");
	passed &= Check(!complete, "missing variable committed completion");
	passed &= Check(g_findVarCalls == 1 && g_findCommandCalls == 0,
		"missing variable did not stop before command lookup");

	g_varResult = &var;
	g_commandResult = nullptr;
	passed &= Check(!TryUnhideConsoleCommands(
		complete, std::uintptr_t{ 0x1234 }, &FindVar, &FindCommand, 0x30),
		"missing command reported completion");
	passed &= Check(!complete, "missing command committed completion");
	passed &= Check(var.m_nFlags == 0x75 && command.m_nFlags == 0xB6,
		"partial lookup mutated flags");

	g_commandResult = &command;
	passed &= Check(TryUnhideConsoleCommands(
		complete, std::uintptr_t{ 0x1234 }, &FindVar, &FindCommand, 0x30),
		"ready retry did not complete");
	passed &= Check(complete, "ready retry did not commit completion");
	passed &= Check(var.m_nFlags == 0x45,
		"ready retry did not clear only requested variable flags");
	passed &= Check(command.m_nFlags == 0x86,
		"ready retry did not clear only requested command flags");
	passed &= Check(g_lastInterface == 0x1234,
		"lookups did not receive the cvar interface");
	passed &= Check(g_lastVarName && std::strcmp(g_lastVarName, "cl_updaterate") == 0,
		"variable lookup name changed");
	passed &= Check(g_lastCommandName && std::strcmp(g_lastCommandName, "help") == 0,
		"command lookup name changed");
	return passed;
}

bool TestCompletionIsIdempotent()
{
	Reset();
	bool complete = false;
	FakeVar var{ 0x31 };
	FakeCommand command{ 0x32 };
	g_varResult = &var;
	g_commandResult = &command;

	bool passed = Check(TryUnhideConsoleCommands(
		complete, std::uintptr_t{ 0x5678 }, &FindVar, &FindCommand, 0x30),
		"initial ready call did not complete");
	const int findVarCalls = g_findVarCalls;
	const int findCommandCalls = g_findCommandCalls;
	g_varResult = nullptr;
	g_commandResult = nullptr;
	passed &= Check(TryUnhideConsoleCommands(
		complete, std::uintptr_t{ 0 }, FindVarFunction{}, FindCommandFunction{}, 0x30),
		"completed state regressed when dependencies disappeared");
	passed &= Check(g_findVarCalls == findVarCalls
		&& g_findCommandCalls == findCommandCalls,
		"completed state repeated engine lookups");
	passed &= Check(var.m_nFlags == 0x01 && command.m_nFlags == 0x02,
		"completed state changed flags again");
	return passed;
}
}

int main()
{
	bool passed = true;
	passed &= TestMissingDependenciesDeferWithoutCalling();
	passed &= TestLookupFailureRemainsRetryable();
	passed &= TestCompletionIsIdempotent();
	if (!passed)
		return 1;
	std::cout << "cbuf initialization guard tests passed\n";
	return 0;
}
