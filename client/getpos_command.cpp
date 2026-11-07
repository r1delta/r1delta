// Restores the console output of client.dll's getpos / getpos_exact, which retail compiled out.
//
// R1 retail client.dll registers both commands with the callback at RVA 0x29AD40 ("dump position and angles to the
// console" / "dump origin and angles to the console"). Every Msg in it was stripped, so the commands print nothing. The
// Source client implementation it was built from prints the main view origin and angles of the active split-screen slot,
// or the local player's absolute origin and angles for getpos_exact and "getpos 2". The getpos_bind helper at RVA
// 0x29AC20 computes those values, but it indexes the main view by the engine's active split-screen slot, which is -1 for
// a command buffered outside a slot guard (it then reads the 12 bytes before the table). R1 multiplayer has one local
// player, so the main view is read for slot 0 here. Both variants print "setpos ...;setang ...": R1's setpos sets the
// absolute origin, so the exact line puts the player back where it was, while R1's setpos_exact does nothing.

#include "getpos_command.h"

#include "cvar.h"
#include "logging.h"

#include <MinHook.h>

#include <cstdlib>
#include <cstring>

namespace
{
constexpr std::uintptr_t kGetPosCommandRva = 0x29AD40;
constexpr std::uintptr_t kGetPosValuesRva = 0x29AC20;
constexpr std::uintptr_t kMainViewOriginRva = 0x1508650;   // float[3] per split-screen slot (sub_180299D40)
constexpr std::uintptr_t kMainViewAnglesRva = 0x1508E30;   // float[3] per split-screen slot

constexpr unsigned char kGetPosCommandPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x10,   // mov [rsp+10h], rbx
	0x57,                           // push rdi
	0x48, 0x83, 0xEC, 0x20,         // sub rsp, 20h
	0x48, 0x8B, 0xD9,               // mov rbx, rcx
};
constexpr unsigned char kGetPosValuesPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08,   // mov [rsp+8], rbx
	0x48, 0x89, 0x6C, 0x24, 0x10,   // mov [rsp+10h], rbp
	0x48, 0x89, 0x74, 0x24, 0x18,   // mov [rsp+18h], rsi
	0x57,                           // push rdi
};

// args, origin out, angles out, force the local player's absolute origin and angles
using GetPosValuesFunction = void(__fastcall*)(const CCommand* args, float* origin, float* angles, char forceExact);

std::uintptr_t s_clientBase;
GetPosValuesFunction s_getPosValues;

bool WantsExact(const CCommand& args)
{
	return (args.ArgC() == 2 && std::atoi(args.Arg(1)) == 2) || _stricmp(args.Arg(0), "getpos_exact") == 0;
}

void __fastcall GetPosCommand(const CCommand* args)
{
	float origin[3]{};
	float angles[3]{};
	if (WantsExact(*args)) {
		s_getPosValues(args, origin, angles, 1);
	}
	else {
		std::memcpy(origin, reinterpret_cast<const float*>(s_clientBase + kMainViewOriginRva), sizeof(origin));
		std::memcpy(angles, reinterpret_cast<const float*>(s_clientBase + kMainViewAnglesRva), sizeof(angles));
	}
	Msg("setpos %f %f %f;setang %f %f %f\n", origin[0], origin[1], origin[2], angles[0], angles[1], angles[2]);
}
} // namespace

void InstallGetPosCommandOutput(std::uintptr_t clientBase)
{
	void* const command = reinterpret_cast<void*>(clientBase + kGetPosCommandRva);
	void* const values = reinterpret_cast<void*>(clientBase + kGetPosValuesRva);
	if (std::memcmp(command, kGetPosCommandPrologue, sizeof(kGetPosCommandPrologue)) != 0
		|| std::memcmp(values, kGetPosValuesPrologue, sizeof(kGetPosValuesPrologue)) != 0) {
		Warning("R1Delta: client.dll getpos bytes mismatch; getpos output not restored\n");
		return;
	}
	s_clientBase = clientBase;
	s_getPosValues = reinterpret_cast<GetPosValuesFunction>(values);
	if (MH_CreateHook(command, reinterpret_cast<void*>(&GetPosCommand), nullptr) != MH_OK)
		Warning("R1Delta: could not hook client.dll getpos; getpos output not restored\n");
}
