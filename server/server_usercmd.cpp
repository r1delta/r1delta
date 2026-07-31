#include "server_usercmd.h"

#include "core.h"
#include "cvar.h"
#include "factory.h"
#include "load.h"
#include "logging.h"
#include "vsdk/public/tier0/platformtime.h"

#include <cfloat>
#include <climits>
#include <cmath>
#include <cstring>

namespace
{
constexpr uintptr_t kPlayerPhysicsSimulateRva = 0x4FABF0;
constexpr uintptr_t kPlayerMoveRunCommandRva = 0x514F00;
constexpr size_t kUserCmdFrameTimeOffset = 160;
constexpr size_t kPlayerFlagsOffset = 360;
constexpr size_t kPlayerAccumulatedFrameTimeOffset = 7036;
constexpr size_t kPlayerMoveTypeOffset = 0x1BC;
constexpr size_t kPlayerFrameTimeReferenceOffset = 7040;
constexpr size_t kPlayerPausedOffset = 7052;
constexpr unsigned char kMoveTypeNoclip = 9;
constexpr uintptr_t kNoclipDuringPauseParentRva = 0xB5A100;
constexpr uintptr_t kClampPlayerFrameTimeParentRva = 0xB5A980;
constexpr uintptr_t kSvCheatsRva = 0xC30900;
constexpr uintptr_t kClampFrameTimeBaseRva = 0xC310F8;
constexpr uintptr_t kClampFrameTimeOffsetRva = 0xC31100;
constexpr unsigned int kFakeClientFlag = 0x100;
constexpr size_t kMaxTrackedPlayers = 128;
constexpr unsigned int kSustainedDropThreshold = 4;

class UserCmdProcessingBudget
{
public:
	constexpr bool GrantForTick(int serverTick, int maxProcessTicks, float intervalPerTick)
	{
		if (m_lastGrantedTick == serverTick)
			return false;
		m_lastGrantedTick = serverTick;

		if (maxProcessTicks <= 0 || intervalPerTick <= 0.0f)
		{
			m_movementTimeRemaining = 0.0f;
			return true;
		}

		const double maximum = static_cast<double>(maxProcessTicks) * intervalPerTick;
		const float maximumTime = maximum >= FLT_MAX ? FLT_MAX : static_cast<float>(maximum);
		m_movementTimeRemaining += intervalPerTick;
		if (m_movementTimeRemaining > maximumTime)
			m_movementTimeRemaining = maximumTime;
		return true;
	}

	constexpr bool Consume(int maxProcessTicks, float requiredTime)
	{
		if (maxProcessTicks <= 0 || requiredTime <= 0.0f)
			return true;
		if (m_movementTimeRemaining <= 0.0f)
			return false;
		if (requiredTime > m_movementTimeRemaining + FLT_EPSILON)
		{
			m_movementTimeRemaining = 0.0f;
			return false;
		}

		m_movementTimeRemaining -= requiredTime;
		if (m_movementTimeRemaining < 0.0f)
			m_movementTimeRemaining = 0.0f;
		return true;
	}

	constexpr float Remaining() const
	{
		return m_movementTimeRemaining;
	}

private:
	float m_movementTimeRemaining = 0.0f;
	int m_lastGrantedTick = INT_MIN;
};

constexpr bool ValidateUserCmdProcessingBudget()
{
	UserCmdProcessingBudget budget;
	for (int i = 0; i < 20; ++i)
		budget.GrantForTick(i, 16, 0.01f);
	if (budget.GrantForTick(19, 16, 0.01f))
		return false;
	for (int i = 0; i < 80; ++i)
	{
		if (!budget.Consume(16, 0.002f))
			return false;
	}
	if (budget.Consume(16, 0.002f))
		return false;
	budget.GrantForTick(20, 16, 0.01f);
	if (!budget.Consume(16, 0.006f) || !budget.Consume(16, 0.004f))
		return false;
	if (budget.Consume(16, 0.001f))
		return false;
	return budget.Consume(16, 0.0f) && budget.Consume(0, 1000.0f);
}
static_assert(ValidateUserCmdProcessingBudget());

constexpr float ApplyPausedNoclipFrameTime(bool paused, unsigned char moveType,
	bool cheatsEnabled, bool noclipDuringPause, float intervalPerTick, float selectedFrameTime)
{
	return paused && moveType == kMoveTypeNoclip && cheatsEnabled && noclipDuringPause
		? intervalPerTick
		: selectedFrameTime;
}

static_assert(ApplyPausedNoclipFrameTime(true, kMoveTypeNoclip, true, true, 0.01f, 0.0f) == 0.01f);
static_assert(ApplyPausedNoclipFrameTime(true, kMoveTypeNoclip, false, true, 0.01f, 0.0f) == 0.0f);
static_assert(ApplyPausedNoclipFrameTime(true, 2, true, true, 0.01f, 0.0f) == 0.0f);

struct UserCmdProcessingState
{
	uintptr_t player = 0;
	UserCmdProcessingBudget budget;
	int lastTouchedFrame = 0;
	double dropWindowStart = 0.0;
	double nextWarningTime = 0.0;
	unsigned int dropsInWindow = 0;
};

UserCmdProcessingState s_userCmdStates[kMaxTrackedPlayers];
uintptr_t s_serverBase = 0;
void* s_svMaxUserCmdProcessTicks = nullptr;
void* s_svMaxUserCmdProcessTicksWarning = nullptr;

using PlayerPhysicsSimulateType = void(__fastcall*)(uintptr_t player);
using PlayerMoveRunCommandType = __int64(__fastcall*)(uintptr_t playerMove, uintptr_t player,
	uintptr_t userCmd, uintptr_t moveHelper);

PlayerPhysicsSimulateType s_playerPhysicsSimulateOriginal = nullptr;
PlayerMoveRunCommandType s_playerMoveRunCommandOriginal = nullptr;

int GetConVarInt(void* conVar, int fallback)
{
	if (!conVar)
		return fallback;

	return IsR1ODedicatedServer()
		? static_cast<ConVarR1O*>(conVar)->m_Value.m_nValue
		: static_cast<ConVarR1*>(conVar)->m_Value.m_nValue;
}

float GetConVarFloat(void* conVar, float fallback)
{
	if (!conVar)
		return fallback;

	return IsR1ODedicatedServer()
		? static_cast<ConVarR1O*>(conVar)->m_Value.m_fValue
		: static_cast<ConVarR1*>(conVar)->m_Value.m_fValue;
}

UserCmdProcessingState& FindUserCmdState(uintptr_t player)
{
	UserCmdProcessingState* oldest = &s_userCmdStates[0];
	for (UserCmdProcessingState& state : s_userCmdStates)
	{
		if (state.player == player)
			return state;
		if (!state.player)
		{
			state.player = player;
			return state;
		}
		if (state.lastTouchedFrame < oldest->lastTouchedFrame)
			oldest = &state;
	}

	*oldest = {};
	oldest->player = player;
	return *oldest;
}

int GetMaxUserCmdProcessTicks()
{
	const int configuredTicks = GetConVarInt(s_svMaxUserCmdProcessTicks, 16);
	return configuredTicks < 0 ? 0 : configuredTicks;
}

void GrantUserCmdProcessingTime(UserCmdProcessingState& state, int maxProcessTicks, float intervalPerTick)
{
	state.lastTouchedFrame = pGlobalVarsServer ? pGlobalVarsServer->framecount : state.lastTouchedFrame;
	if (!std::isfinite(intervalPerTick))
		return;

	state.budget.GrantForTick(pGlobalVarsServer->tickcount, maxProcessTicks, intervalPerTick);
}

bool ConsumeUserCmdProcessingTime(UserCmdProcessingState& state, int maxProcessTicks, float requiredTime)
{
	state.lastTouchedFrame = pGlobalVarsServer ? pGlobalVarsServer->framecount : state.lastTouchedFrame;
	return state.budget.Consume(maxProcessTicks, requiredTime);
}

void RecordDroppedUserCmd(UserCmdProcessingState& state, uintptr_t player, float requiredTime)
{
	const float warningInterval = GetConVarFloat(s_svMaxUserCmdProcessTicksWarning, -1.0f);
	if (warningInterval < 0.0f)
		return;

	const double now = Plat_FloatTime();
	if (state.dropWindowStart == 0.0 || now - state.dropWindowStart > 1.0)
	{
		state.dropWindowStart = now;
		state.dropsInWindow = 0;
	}

	++state.dropsInWindow;
	if (state.dropsInWindow < kSustainedDropThreshold || now < state.nextWarningTime)
		return;

	Warning("R1Delta: ignored excessive usercmd processing for player %p "
		"(required %.6f sec, remaining %.6f sec)\n",
		reinterpret_cast<void*>(player), requiredTime, state.budget.Remaining());
	state.dropsInWindow = 0;
	state.dropWindowStart = now;
	state.nextWarningTime = now + warningInterval;
}

bool IsFakeClient(uintptr_t player)
{
	return (*reinterpret_cast<unsigned int*>(player + kPlayerFlagsOffset) & kFakeClientFlag) != 0;
}

int GetConVarParentInt(uintptr_t conVar)
{
	if (!conVar)
		return 0;
	const uintptr_t parent = *reinterpret_cast<uintptr_t*>(conVar + 0x40);
	return parent ? *reinterpret_cast<int*>(parent + 0x64) : 0;
}

float GetEffectiveUserCmdFrameTime(uintptr_t player, uintptr_t userCmd)
{
	const bool paused = *reinterpret_cast<unsigned char*>(player + kPlayerPausedOffset) != 0;
	float frameTime = paused
		? 0.0f
		: pGlobalVarsServer->interval_per_tick;
	const float commandFrameTime = *reinterpret_cast<float*>(userCmd + kUserCmdFrameTimeOffset);
	if (commandFrameTime != 0.0f)
		frameTime = commandFrameTime;

	const uintptr_t clampParent = *reinterpret_cast<uintptr_t*>(s_serverBase + kClampPlayerFrameTimeParentRva);
	if (clampParent && *reinterpret_cast<int*>(clampParent + 0x64) != 0)
	{
		const float baseTime = *reinterpret_cast<float*>(s_serverBase + kClampFrameTimeBaseRva);
		float slack = (baseTime - *reinterpret_cast<float*>(player + kPlayerFrameTimeReferenceOffset))
			* 0.016669999808073044f;
		slack = std::fmin(std::fmax(slack, 3.0f), 10.0f);
		const float limit = *reinterpret_cast<float*>(s_serverBase + kClampFrameTimeOffsetRva)
			+ baseTime + slack;
		const float accumulated = *reinterpret_cast<float*>(player + kPlayerAccumulatedFrameTimeOffset);
		if (!(limit >= accumulated))
			frameTime = 0.0f;
	}

	const uintptr_t svCheats = *reinterpret_cast<uintptr_t*>(s_serverBase + kSvCheatsRva);
	const uintptr_t noclipDuringPauseParent = *reinterpret_cast<uintptr_t*>(s_serverBase + kNoclipDuringPauseParentRva);
	return ApplyPausedNoclipFrameTime(
		paused,
		*reinterpret_cast<unsigned char*>(player + kPlayerMoveTypeOffset),
		GetConVarParentInt(svCheats) != 0,
		noclipDuringPauseParent && *reinterpret_cast<int*>(noclipDuringPauseParent + 0x64) != 0,
		pGlobalVarsServer->interval_per_tick,
		frameTime);
}

void __fastcall PlayerPhysicsSimulate(uintptr_t player)
{
	if (player && pGlobalVarsServer)
	{
		UserCmdProcessingState& state = FindUserCmdState(player);
		GrantUserCmdProcessingTime(state, GetMaxUserCmdProcessTicks(), pGlobalVarsServer->interval_per_tick);
	}

	s_playerPhysicsSimulateOriginal(player);
}

__int64 __fastcall PlayerMoveRunCommand(uintptr_t playerMove, uintptr_t player,
	uintptr_t userCmd, uintptr_t moveHelper)
{
	if (!player || !userCmd || !pGlobalVarsServer || IsFakeClient(player))
		return s_playerMoveRunCommandOriginal(playerMove, player, userCmd, moveHelper);

	const float requiredTime = GetEffectiveUserCmdFrameTime(player, userCmd);
	UserCmdProcessingState& state = FindUserCmdState(player);
	if (!std::isfinite(requiredTime) || requiredTime < 0.0f
		|| !ConsumeUserCmdProcessingTime(state, GetMaxUserCmdProcessTicks(), requiredTime))
	{
		RecordDroppedUserCmd(state, player, requiredTime);
		return 0;
	}

	return s_playerMoveRunCommandOriginal(playerMove, player, userCmd, moveHelper);
}

bool MatchesBytes(uintptr_t address, const unsigned char* expected, size_t length)
{
	return std::memcmp(reinterpret_cast<const void*>(address), expected, length) == 0;
}
}

void RegisterServerUserCmdConVars()
{
	if (s_svMaxUserCmdProcessTicks)
		return;

	if (IsR1ODedicatedServer())
	{
		s_svMaxUserCmdProcessTicks = RegisterR1ODediConVar("sv_maxusrcmdprocessticks", "16",
			FCVAR_GAMEDLL | FCVAR_RELEASE,
			"Maximum accumulated server ticks available for processing user commands (0 = unlimited).");
		s_svMaxUserCmdProcessTicksWarning = RegisterR1ODediConVar("sv_maxusrcmdprocessticks_warning", "-1",
			FCVAR_GAMEDLL | FCVAR_RELEASE,
			"Minimum seconds between sustained usercmd budget warnings (-1 = disabled).");
	}
	else
	{
		s_svMaxUserCmdProcessTicks = RegisterConVar("sv_maxusrcmdprocessticks", "16",
			FCVAR_GAMEDLL | FCVAR_RELEASE,
			"Maximum accumulated server ticks available for processing user commands (0 = unlimited).");
		s_svMaxUserCmdProcessTicksWarning = RegisterConVar("sv_maxusrcmdprocessticks_warning", "-1",
			FCVAR_GAMEDLL | FCVAR_RELEASE,
			"Minimum seconds between sustained usercmd budget warnings (-1 = disabled).");
	}
}

void InstallServerUserCmdHooks(uintptr_t serverBase)
{
	static bool installed = false;
	if (installed || !serverBase)
		return;

	static constexpr unsigned char physicsSimulatePrologue[] = {
		0x48, 0x8B, 0xC4, 0x57, 0x41, 0x54, 0x41, 0x55,
		0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x70
	};
	static constexpr unsigned char runCommandPrologue[] = {
		0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48,
		0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18, 0x55
	};

	const uintptr_t physicsSimulate = serverBase + kPlayerPhysicsSimulateRva;
	const uintptr_t runCommand = serverBase + kPlayerMoveRunCommandRva;
	if (!MatchesBytes(physicsSimulate, physicsSimulatePrologue, sizeof(physicsSimulatePrologue))
		|| !MatchesBytes(runCommand, runCommandPrologue, sizeof(runCommandPrologue)))
	{
		Warning("R1Delta: server usercmd budget hooks skipped due to server.dll version mismatch\n");
		return;
	}

	const MH_STATUS physicsStatus = MH_CreateHook(reinterpret_cast<LPVOID>(physicsSimulate),
		&PlayerPhysicsSimulate, reinterpret_cast<LPVOID*>(&s_playerPhysicsSimulateOriginal));
	const MH_STATUS runCommandStatus = MH_CreateHook(reinterpret_cast<LPVOID>(runCommand),
		&PlayerMoveRunCommand, reinterpret_cast<LPVOID*>(&s_playerMoveRunCommandOriginal));
	if (physicsStatus != MH_OK || runCommandStatus != MH_OK)
	{
		if (physicsStatus == MH_OK)
			MH_RemoveHook(reinterpret_cast<LPVOID>(physicsSimulate));
		if (runCommandStatus == MH_OK)
			MH_RemoveHook(reinterpret_cast<LPVOID>(runCommand));
		s_playerPhysicsSimulateOriginal = nullptr;
		s_playerMoveRunCommandOriginal = nullptr;
		Warning("R1Delta: failed to install server usercmd budget hooks (physics=%d runcommand=%d)\n",
			physicsStatus, runCommandStatus);
		return;
	}

	s_serverBase = serverBase;
	installed = true;
}
