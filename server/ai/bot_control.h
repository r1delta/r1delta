// Script-driven bot control for R1Delta
// Native side only provides "hands" (usercmd injection) and "map" (pathfinding over the .ain graph);
// the bot brain lives in Squirrel (mp/_bot_ai.nut in CORE).
#pragma once

#include <cstdint>

class CCommand; // squirrel.h uses it without including cvar.h
#include "squirrel.h"

// CUserCmd layout (308 bytes). move/buttons offsets match CPropVehicleDriveable_DriveVehicle in load.cpp,
// frametime matches server_usercmd.cpp. View angle offsets follow the R2 layout that shares the same
// move/buttons offsets; verify them in-game with bot_debug_usercmd before relying on them.
namespace BotUserCmd
{
	constexpr size_t kWorldViewAngles = 0x0C;
	constexpr size_t kLocalViewAngles = 0x1C;
	constexpr size_t kForwardMove = 52;
	constexpr size_t kSideMove = 56;
	constexpr size_t kUpMove = 60;
	constexpr size_t kButtons = 64;
	constexpr size_t kFrameTime = 160;
	constexpr size_t kSize = 308;
}

// Source-style button bits; the movement ones are confirmed by the vehicle fallback in load.cpp.
namespace BotButtons
{
	constexpr int kAttack = 0x1;
	constexpr int kJump = 0x2;
	constexpr int kDuck = 0x4;
	constexpr int kForward = 0x8;
	constexpr int kBack = 0x10;
	constexpr int kUse = 0x20;
	constexpr int kMoveLeft = 0x200;
	constexpr int kMoveRight = 0x400;
	constexpr int kAttack2 = 0x800;
	constexpr int kReload = 0x2000;
	constexpr int kSpeed = 0x20000;
}

void BotControl_ApplyToUserCmd(uintptr_t player, uintptr_t userCmd);
void BotControl_DebugDumpUserCmd(uintptr_t player, uintptr_t userCmd);
void BotControl_RecordMoveDebug(uintptr_t player, float frameTimeBefore, float frameTimeAfter);
void RegisterBotControlConVars();

// Server script functions, registered in shared/squirrel.cpp next to the other natives.
SQInteger Script_BotSetInput(HSQUIRRELVM v);
SQInteger Script_BotPressButtons(HSQUIRRELVM v);
SQInteger Script_BotClearInput(HSQUIRRELVM v);
SQInteger Script_BotCreate(HSQUIRRELVM v);
SQInteger Script_BotGetDebugInfo(HSQUIRRELVM v);
SQInteger Script_NavGetNodeCount(HSQUIRRELVM v);
SQInteger Script_NavGetNodePosition(HSQUIRRELVM v);
SQInteger Script_NavFindPath(HSQUIRRELVM v);
SQInteger Script_NavFindPathPilot(HSQUIRRELVM v);
