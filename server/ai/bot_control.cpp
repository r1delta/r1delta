// Script-driven bot control for R1Delta
// - BotSetInput/BotPressButtons store per-bot input that is written into the fake client's usercmd
//   right before PlayerMove::RunCommand (hooked in server_usercmd.cpp), so bots move with real player physics.
// - NavFindPath runs A* over the loaded CAI_Network so script can steer bots along the map's node graph.

#include "bot_control.h"
#include "bot.h"
#include "core.h"
#include "load.h"
#include "logging.h"
#include "cvar.h"
#include "factory.h"
#include "squirrel.h"
#include "navmesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <queue>
#include <vector>

namespace
{
constexpr int kMaxBotSlots = 128;
constexpr size_t kPlayerEdictOffset = 64;
constexpr size_t kEdictSize = 56;
constexpr size_t kPlayerFlagsOffset = 0x168;
constexpr unsigned int kFlAtControls = 0x40;
constexpr uintptr_t kAINetworkManagerRva = 0xC31898; // same global navmesh.cpp's GetActiveAINetwork uses
constexpr size_t kManagerNetworkOffset = 1600;
constexpr unsigned char kWalkableLinkMask = 0x61; // same mask the AIN builder uses for traversable links

struct BotInput
{
	float forward = 0.0f;
	float side = 0.0f;
	float up = 0.0f;
	float pitch = 0.0f;
	float yaw = 0.0f;
	int heldButtons = 0;
	int pulseButtons = 0; // applied to exactly one usercmd, so jump/melee get a fresh press edge
	bool active = false;
};

// Per-slot counters for BotGetDebugInfo: shows whether the engine runs usercmds for the bot
// at all, and whether ours get written into them.
struct BotCmdStats
{
	unsigned int seen = 0;		// fake-client usercmds that reached PlayerMove::RunCommand
	unsigned int applied = 0;	// of those, overwritten with script input
	float lastForward = 0.0f;
	float lastSide = 0.0f;
	float lastYaw = 0.0f;
	int lastButtons = 0;
	float lastFrameTime = 0.0f;	// frametime the engine put in the command
	float effFrameTimeBefore = 0.0f;	// movement frame time before the clamp fix
	float effFrameTime = 0.0f;	// frame time RunCommand will actually move with
	unsigned int clampFixes = 0;	// commands that had zero movement time
	unsigned int playerFlags = 0;	// player+0x168 (0x20 = frozen, 0x100 = fake client)
	int blocked14EC = 0;			// player+0x14EC bit 0: PlayerRunCommand zeroes movement
	int blocked157D = 0;			// player+0x157D
	int paused = 0;					// player+0x1B8C
	float accumulated = 0.0f;		// player+0x1B7C accumulated command time (frame time clamp)
	float reference = 0.0f;			// player+0x1B80
};

BotInput s_botInputs[kMaxBotSlots];
BotCmdStats s_botCmdStats[kMaxBotSlots];
void* s_botDebugUserCmd = nullptr;
unsigned int s_debugDumpCounter = 0;

int GetConVarIntValue(void* conVar, int fallback)
{
	if (!conVar)
		return fallback;

	return IsR1ODedicatedServer()
		? static_cast<ConVarR1O*>(conVar)->m_Value.m_nValue
		: static_cast<ConVarR1*>(conVar)->m_Value.m_nValue;
}

int GetPlayerSlot(uintptr_t player)
{
	if (!player || !pGlobalVarsServer || !pGlobalVarsServer->pEdicts)
		return -1;

	const auto edict = *reinterpret_cast<__int64*>(player + kPlayerEdictOffset);
	const auto index = (edict - reinterpret_cast<__int64>(pGlobalVarsServer->pEdicts)) / static_cast<__int64>(kEdictSize);
	if (index < 0 || index >= kMaxBotSlots)
		return -1;
	return static_cast<int>(index);
}

R1SquirrelVM* ServerVM()
{
	return IsR1ODedicatedServer() ? nullptr : GetServerVMPtr();
}

bool GetFloatArg(HSQUIRRELVM v, SQInteger index, float& out)
{
	SQFloat value = 0.0f;
	if (SQ_FAILED(sq_getfloat(ServerVM(), v, index, &value)))
		return false;
	out = value;
	return true;
}

bool GetIntArg(HSQUIRRELVM v, SQInteger index, int& out)
{
	SQInteger value = 0;
	if (SQ_FAILED(sq_getinteger(ServerVM(), v, index, &value)))
		return false;
	out = static_cast<int>(value);
	return true;
}

BotInput* GetBotInputArg(HSQUIRRELVM v)
{
	void* player = sq_getentity(v, 2);
	const int slot = GetPlayerSlot(reinterpret_cast<uintptr_t>(player));
	return slot < 0 ? nullptr : &s_botInputs[slot];
}

void WriteVector(uintptr_t address, float x, float y, float z)
{
	auto* vec = reinterpret_cast<float*>(address);
	vec[0] = x;
	vec[1] = y;
	vec[2] = z;
}

//-----------------------------------------------------------------------------
// Navigation
//-----------------------------------------------------------------------------
CAI_Network* GetAINetwork()
{
	if (!G_server)
		return nullptr;
	const uintptr_t manager = *reinterpret_cast<uintptr_t*>(G_server + kAINetworkManagerRva);
	if (!manager)
		return nullptr;
	CAI_Network* network = *reinterpret_cast<CAI_Network**>(manager + kManagerNetworkOffset);
	if (!network || !network->nodes || network->nodecount <= 0)
		return nullptr;
	return network;
}

float DistanceSquared(const Vector3f& a, const Vector3f& b)
{
	const float dx = a.x - b.x;
	const float dy = a.y - b.y;
	const float dz = a.z - b.z;
	return dx * dx + dy * dy + dz * dz;
}

int FindNearestNode(const CAI_Network* network, const Vector3f& origin)
{
	int best = -1;
	float bestDistance = 3.402823466e+38F;
	for (int i = 0; i < network->nodecount; ++i)
	{
		const CAI_Node* node = network->nodes[i];
		if (!node)
			continue;
		const float distance = DistanceSquared(node->position, origin);
		if (distance < bestDistance)
		{
			bestDistance = distance;
			best = i;
		}
	}
	return best;
}

bool LinkUsableByHull(const CAI_NodeLink* link, int hull)
{
	return link && (link->hulls[hull] & kWalkableLinkMask) != 0;
}

// Links that are only NPC traverses (0x20/0x40 without the walk bit 0x01): vaults, jump-ups,
// jump-downs, wall climbs of up to ~420 units and long jumps of up to ~1070 units, each played
// as an animation by grunts/spectres. A pilot can only repeat some of them, so when limits are
// active a traverse link is usable only up to maxRise of climb and maxGap of horizontal
// distance (going down is always fine), and costs more than walking.
struct TraverseLimits
{
	bool active = false;
	float maxRise = 0.0f;
	float maxGap = 0.0f;
	float costScale = 2.0f;
};

constexpr unsigned char kWalkLinkBit = 0x01;

bool TraverseLinkAllowed(const CAI_NodeLink* link, int hull, const Vector3f& from, const Vector3f& to,
	const TraverseLimits& limits, float& costScale)
{
	costScale = 1.0f;
	if (!limits.active || (link->hulls[hull] & kWalkLinkBit) != 0)
		return true;

	const float rise = to.z - from.z;
	const float dx = to.x - from.x;
	const float dy = to.y - from.y;
	const float gap = std::sqrt(dx * dx + dy * dy);
	if (rise > limits.maxRise || gap > limits.maxGap)
		return false;

	costScale = limits.costScale;
	return true;
}

// Node ids match array indices for loaded graphs; links store ids.
std::vector<int> FindPathNodes(const CAI_Network* network, int startNode, int goalNode, int hull,
	const TraverseLimits& limits = TraverseLimits())
{
	std::vector<int> path;
	const int nodeCount = network->nodecount;
	if (startNode < 0 || goalNode < 0 || startNode >= nodeCount || goalNode >= nodeCount)
		return path;

	std::vector<float> gScore(nodeCount, 3.402823466e+38F);
	std::vector<int> cameFrom(nodeCount, -1);
	std::vector<bool> closed(nodeCount, false);

	using QueueEntry = std::pair<float, int>;
	std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> open;

	const Vector3f goalPosition = network->nodes[goalNode]->position;
	gScore[startNode] = 0.0f;
	open.push({ std::sqrt(DistanceSquared(network->nodes[startNode]->position, goalPosition)), startNode });

	while (!open.empty())
	{
		const int current = open.top().second;
		open.pop();
		if (closed[current])
			continue;
		if (current == goalNode)
			break;
		closed[current] = true;

		const CAI_Node* node = network->nodes[current];
		if (!node || !node->links)
			continue;

		for (int i = 0; i < node->linkcount; ++i)
		{
			const CAI_NodeLink* link = node->links[i];
			if (!LinkUsableByHull(link, hull))
				continue;

			const int neighbor = (link->srcId == current) ? link->destId : link->srcId;
			if (neighbor < 0 || neighbor >= nodeCount || closed[neighbor] || !network->nodes[neighbor])
				continue;

			const Vector3f& neighborPosition = network->nodes[neighbor]->position;
			float costScale = 1.0f;
			if (!TraverseLinkAllowed(link, hull, node->position, neighborPosition, limits, costScale))
				continue;

			const float tentative = gScore[current] + costScale * std::sqrt(DistanceSquared(node->position, neighborPosition));
			if (tentative >= gScore[neighbor])
				continue;

			gScore[neighbor] = tentative;
			cameFrom[neighbor] = current;
			open.push({ tentative + std::sqrt(DistanceSquared(neighborPosition, goalPosition)), neighbor });
		}
	}

	if (startNode != goalNode && cameFrom[goalNode] == -1)
		return path;

	for (int node = goalNode; node != -1; node = cameFrom[node])
		path.push_back(node);
	std::reverse(path.begin(), path.end());
	return path;
}

void PushFloatArray(HSQUIRRELVM v, const std::vector<float>& values)
{
	sq_newarray(v, 0);
	for (float value : values)
	{
		sq_pushfloat(ServerVM(), v, value);
		sq_arrayappend(v, -2);
	}
}

// A* from the node nearest start to the node nearest goal, pushed as a flat
// [x0, y0, z0, x1, ...] array (empty when no path exists).
void PushPath(HSQUIRRELVM v, const Vector3f& start, const Vector3f& goal, int hull, const TraverseLimits& limits)
{
	std::vector<float> flat;
	const CAI_Network* network = GetAINetwork();
	if (network)
	{
		const int startNode = FindNearestNode(network, start);
		const int goalNode = FindNearestNode(network, goal);
		const std::vector<int> nodes = FindPathNodes(network, startNode, goalNode, hull, limits);
		flat.reserve(nodes.size() * 3);
		for (int nodeIndex : nodes)
		{
			const Vector3f& position = network->nodes[nodeIndex]->position;
			flat.push_back(position.x);
			flat.push_back(position.y);
			flat.push_back(position.z);
		}
	}
	PushFloatArray(v, flat);
}

}

//-----------------------------------------------------------------------------
// Script functions
//-----------------------------------------------------------------------------
SQInteger Script_BotSetInput(HSQUIRRELVM v)
{
	BotInput* input = GetBotInputArg(v);
	if (!input)
		return sq_throwerror(v, "invalid bot player");

	float forward, side, pitch, yaw;
	int buttons;
	if (!GetFloatArg(v, 3, forward) || !GetFloatArg(v, 4, side) || !GetFloatArg(v, 5, pitch)
		|| !GetFloatArg(v, 6, yaw) || !GetIntArg(v, 7, buttons))
	{
		return sq_throwerror(v, "expected (entity, float forward, float side, float pitch, float yaw, int buttons)");
	}

	input->forward = std::clamp(forward, -1.0f, 1.0f);
	input->side = std::clamp(side, -1.0f, 1.0f);
	input->pitch = std::clamp(pitch, -89.0f, 89.0f);
	input->yaw = yaw;
	input->heldButtons = buttons;
	input->active = true;
	return 0;
}

SQInteger Script_BotPressButtons(HSQUIRRELVM v)
{
	BotInput* input = GetBotInputArg(v);
	if (!input)
		return sq_throwerror(v, "invalid bot player");

	int buttons;
	if (!GetIntArg(v, 3, buttons))
		return sq_throwerror(v, "buttons must be an integer");

	input->pulseButtons |= buttons;
	input->active = true;
	return 0;
}

SQInteger Script_BotClearInput(HSQUIRRELVM v)
{
	BotInput* input = GetBotInputArg(v);
	if (!input)
		return sq_throwerror(v, "invalid bot player");

	*input = {};
	return 0;
}

SQInteger Script_BotCreate(HSQUIRRELVM v)
{
	int team;
	if (!GetIntArg(v, 2, team))
		return sq_throwerror(v, "team must be an integer");

	const SQChar* requestedName = nullptr;
	if (SQ_FAILED(sq_getstring(v, 3, &requestedName)))
		return sq_throwerror(v, "name must be a string");

	char botName[32] = {};
	if (!CreateDummyBot(team, requestedName, botName, sizeof(botName)))
		botName[0] = '\0';
	sq_pushstring(v, botName, -1);
	return 1;
}

SQInteger Script_NavGetNodeCount(HSQUIRRELVM v)
{
	const CAI_Network* network = GetAINetwork();
	sq_pushinteger(ServerVM(), v, network ? network->nodecount : 0);
	return 1;
}

SQInteger Script_NavGetNodePosition(HSQUIRRELVM v)
{
	int index;
	if (!GetIntArg(v, 2, index))
		return sq_throwerror(v, "index must be an integer");

	const CAI_Network* network = GetAINetwork();
	if (!network || index < 0 || index >= network->nodecount || !network->nodes[index])
		return sq_throwerror(v, "invalid node index");

	const Vector3f& position = network->nodes[index]->position;
	PushFloatArray(v, { position.x, position.y, position.z });
	return 1;
}

SQInteger Script_NavFindPath(HSQUIRRELVM v)
{
	float sx, sy, sz, ex, ey, ez;
	int hull;
	if (!GetFloatArg(v, 2, sx) || !GetFloatArg(v, 3, sy) || !GetFloatArg(v, 4, sz)
		|| !GetFloatArg(v, 5, ex) || !GetFloatArg(v, 6, ey) || !GetFloatArg(v, 7, ez) || !GetIntArg(v, 8, hull))
	{
		return sq_throwerror(v, "expected (float sx, float sy, float sz, float ex, float ey, float ez, int hull)");
	}
	if (hull < 0 || hull >= MAX_HULLS)
		return sq_throwerror(v, "invalid hull");

	PushPath(v, { sx, sy, sz }, { ex, ey, ez }, hull, TraverseLimits());
	return 1;
}

SQInteger Script_NavFindPathPilot(HSQUIRRELVM v)
{
	float sx, sy, sz, ex, ey, ez, maxRise, maxGap;
	int hull;
	if (!GetFloatArg(v, 2, sx) || !GetFloatArg(v, 3, sy) || !GetFloatArg(v, 4, sz)
		|| !GetFloatArg(v, 5, ex) || !GetFloatArg(v, 6, ey) || !GetFloatArg(v, 7, ez) || !GetIntArg(v, 8, hull)
		|| !GetFloatArg(v, 9, maxRise) || !GetFloatArg(v, 10, maxGap))
	{
		return sq_throwerror(v, "expected (float sx, float sy, float sz, float ex, float ey, float ez, int hull, float maxRise, float maxGap)");
	}
	if (hull < 0 || hull >= MAX_HULLS)
		return sq_throwerror(v, "invalid hull");

	TraverseLimits limits;
	limits.active = true;
	limits.maxRise = maxRise;
	limits.maxGap = maxGap;
	PushPath(v, { sx, sy, sz }, { ex, ey, ez }, hull, limits);
	return 1;
}

void BotControl_ApplyToUserCmd(uintptr_t player, uintptr_t userCmd)
{
	const int slot = GetPlayerSlot(player);
	if (slot < 0)
		return;

	BotCmdStats& stats = s_botCmdStats[slot];
	++stats.seen;
	stats.lastFrameTime = *reinterpret_cast<const float*>(userCmd + BotUserCmd::kFrameTime);

	BotInput& input = s_botInputs[slot];
	if (!input.active)
		return;
	++stats.applied;

	// Bots are left with FL_ATCONTROLS after the match intro (humans get it cleared per player),
	// and CPlayerMove::SetupMove (server.dll+0x51489D) drops forward/side/up while it is set.
	*reinterpret_cast<unsigned int*>(player + kPlayerFlagsOffset) &= ~kFlAtControls;
	stats.lastForward = input.forward;
	stats.lastSide = input.side;
	stats.lastYaw = input.yaw;

	int buttons = input.heldButtons | input.pulseButtons;
	input.pulseButtons = 0;
	if (input.forward > 0.0f)
		buttons |= BotButtons::kForward;
	else if (input.forward < 0.0f)
		buttons |= BotButtons::kBack;
	if (input.side > 0.0f)
		buttons |= BotButtons::kMoveRight;
	else if (input.side < 0.0f)
		buttons |= BotButtons::kMoveLeft;

	WriteVector(userCmd + BotUserCmd::kWorldViewAngles, input.pitch, input.yaw, 0.0f);
	WriteVector(userCmd + BotUserCmd::kLocalViewAngles, input.pitch, input.yaw, 0.0f);
	*reinterpret_cast<float*>(userCmd + BotUserCmd::kForwardMove) = input.forward;
	*reinterpret_cast<float*>(userCmd + BotUserCmd::kSideMove) = input.side;
	*reinterpret_cast<float*>(userCmd + BotUserCmd::kUpMove) = input.up;
	*reinterpret_cast<int*>(userCmd + BotUserCmd::kButtons) = buttons;
	stats.lastButtons = buttons;
}

void BotControl_RecordMoveDebug(uintptr_t player, float frameTimeBefore, float frameTimeAfter)
{
	const int slot = GetPlayerSlot(player);
	if (slot < 0)
		return;

	BotCmdStats& stats = s_botCmdStats[slot];
	stats.effFrameTimeBefore = frameTimeBefore;
	stats.effFrameTime = frameTimeAfter;
	if (frameTimeBefore == 0.0f)
		++stats.clampFixes;
	stats.playerFlags = *reinterpret_cast<unsigned int*>(player + 0x168);
	stats.blocked14EC = *reinterpret_cast<unsigned char*>(player + 0x14EC) & 1;
	stats.blocked157D = *reinterpret_cast<unsigned char*>(player + 0x157D);
	stats.paused = *reinterpret_cast<unsigned char*>(player + 0x1B8C);
	stats.accumulated = *reinterpret_cast<float*>(player + 0x1B7C);
	stats.reference = *reinterpret_cast<float*>(player + 0x1B80);
}

SQInteger Script_BotGetDebugInfo(HSQUIRRELVM v)
{
	void* player = sq_getentity(v, 2);
	const int slot = GetPlayerSlot(reinterpret_cast<uintptr_t>(player));
	char text[256];
	if (slot < 0)
	{
		snprintf(text, sizeof(text), "slot invalido (ent=%p)", player);
	}
	else
	{
		const BotInput& input = s_botInputs[slot];
		const BotCmdStats& stats = s_botCmdStats[slot];
		// Short lines: the HUD message gets cut off around 70 characters.
		snprintf(text, sizeof(text),
			"cmds=%u aplic=%u fwd=%.2f btn=0x%X\n"
			"ft_cmd=%.4f ft_antes=%.4f ft_mov=%.4f\n"
			"zerados=%u pausa=%d flags=0x%X b14ec=%d b157d=%d\n"
			"acum=%.2f ref=%.2f",
			stats.seen, stats.applied, stats.lastForward, stats.lastButtons,
			stats.lastFrameTime, stats.effFrameTimeBefore, stats.effFrameTime,
			stats.clampFixes, stats.paused,
			stats.playerFlags, stats.blocked14EC, stats.blocked157D,
			stats.accumulated, stats.reference);
	}
	sq_pushstring(v, text, -1);
	return 1;
}

void BotControl_DebugDumpUserCmd(uintptr_t player, uintptr_t userCmd)
{
	const int interval = GetConVarIntValue(s_botDebugUserCmd, 0);
	if (interval <= 0 || (s_debugDumpCounter++ % static_cast<unsigned int>(interval)) != 0)
		return;

	// Dump the first 0x48 bytes as floats plus buttons so view-angle/move offsets can be confirmed
	// by moving the mouse and pressing keys on a human client.
	const float* floats = reinterpret_cast<const float*>(userCmd);
	Msg("usercmd slot=%d buttons=0x%08X fwd=%.2f side=%.2f up=%.2f frametime=%.4f\n",
		GetPlayerSlot(player),
		*reinterpret_cast<const int*>(userCmd + BotUserCmd::kButtons),
		*reinterpret_cast<const float*>(userCmd + BotUserCmd::kForwardMove),
		*reinterpret_cast<const float*>(userCmd + BotUserCmd::kSideMove),
		*reinterpret_cast<const float*>(userCmd + BotUserCmd::kUpMove),
		*reinterpret_cast<const float*>(userCmd + BotUserCmd::kFrameTime));
	Msg("  +0x00..0x30: %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n",
		floats[0], floats[1], floats[2], floats[3], floats[4], floats[5], floats[6],
		floats[7], floats[8], floats[9], floats[10], floats[11], floats[12]);
}

void RegisterBotControlConVars()
{
	if (s_botDebugUserCmd)
		return;

	const char* debugHelp = "Log every Nth human usercmd to verify bot usercmd offsets (0 = off).";
	const char* fillHelp = "Fill the match with script-driven bots until humans + bots reach this count (0 = off). Managed by script";
	const char* difficultyHelp = "Bot skill from 0 (easy) to 3 (hard). Managed by script";
	if (IsR1ODedicatedServer())
	{
		s_botDebugUserCmd = RegisterR1ODediConVar("bot_debug_usercmd", "0", FCVAR_GAMEDLL | FCVAR_CHEAT, debugHelp);
		RegisterR1ODediConVar("delta_bot_fill_target", "0", FCVAR_GAMEDLL, fillHelp);
		RegisterR1ODediConVar("delta_bot_difficulty", "1", FCVAR_GAMEDLL, difficultyHelp);
	}
	else
	{
		s_botDebugUserCmd = RegisterConVar("bot_debug_usercmd", "0", FCVAR_GAMEDLL | FCVAR_CHEAT, debugHelp);
		RegisterConVar("delta_bot_fill_target", "0", FCVAR_GAMEDLL, fillHelp);
		RegisterConVar("delta_bot_difficulty", "1", FCVAR_GAMEDLL, difficultyHelp);
	}
}
