#include "discord.h"
#include "crash_report_context.h"
#include <thread>
#include <chrono>
#include <string>
#include <algorithm>
#include <windows.h>
#include <tlhelp32.h>
#include "netadr.h"
#include "../p2p/p2p.h"
#include "../p2p/p2p_invite.h"
#include "../p2p/p2p_mux.h"
#include "../p2p/p2p_connect.h"
#include "httplib.h"
#include "masterserver.h"
#include "auth.h"
#include <nlohmann/json.hpp>
#include <atomic>

// Define for discord auth stuff.
#define DISCORD

// Global buffer for localized map display name (used by watermark)
char g_cl_MapDisplayName[128] = "main menu";

static std::atomic<bool> is_discord_running{false};
// Only DiscordThread and its SDK callbacks may access the core.
static discord::Core* core = nullptr;

// The SDK delivers OnCurrentUserUpdate from the first RunCallbacks call on a machine where
// Discord is already signed in, which can be before the engine has handed over the cvar
// interface and its trampolines (factory.cpp installs those from the app-system factory,
// while DiscordThread starts from the client.dll load notification). Calling an unset
// trampoline executes address 0, so every path that touches cvar state waits for the
// interface first.
static bool CvarInterfaceReady() {
	return IsCvarInterfacePublished() && SetConvarStringOriginal;
}

static bool WaitForCvarInterface(std::chrono::milliseconds timeout) {
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (!CvarInterfaceReady()) {
		if (std::chrono::steady_clock::now() >= deadline)
			return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return true;
}

// Set when the user-ready callback arrived before the cvar interface existed; the Discord
// thread retries the update from its callback loop.
static std::atomic<bool> platform_user_id_pending{false};
// Milliseconds DiscordThread spent waiting for the cvar interface (-1 = not started yet).
static std::atomic<long long> discord_cvar_wait_ms{-1};

// Console probe for crash reports: says whether the Discord path reached its cvar state and
// what platform_user_id ended up as, without needing the startup console window.
std::string GetDiscordStatusLine() {
	const bool ready = CvarInterfaceReady();
	const char* platformId = "unavailable (cvar interface not ready)";
	if (ready) {
		auto* var = OriginalCCVar_FindVar(cvarinterface, "platform_user_id");
		if (!var)
			platformId = "(convar missing)";
		else
			platformId = var->m_Value.m_pszString ? var->m_Value.m_pszString : "(empty string)";
	}
	char line[512];
	sprintf_s(line, "R1Delta discord: running=%d cvarReady=%d waitedMs=%lld pending=%d platform_user_id=%s",
		is_discord_running.load() ? 1 : 0,
		ready ? 1 : 0,
		discord_cvar_wait_ms.load(),
		platform_user_id_pending.load() ? 1 : 0,
		platformId);
	return line;
}

void DiscordStatusCommand(const CCommand&)
{
	Msg("%s\n", GetDiscordStatusLine().c_str());
}

// Crash-report variant: identical cached state without the platform_user_id lookup, so the
// crash handler never calls into the engine's cvar table.
std::string GetDiscordCrashStateLine() {
	char line[256];
	sprintf_s(line, "R1Delta discord: running=%d cvarReady=%d waitedMs=%lld pending=%d",
		is_discord_running.load() ? 1 : 0,
		CvarInterfaceReady() ? 1 : 0,
		discord_cvar_wait_ms.load(),
		platform_user_id_pending.load() ? 1 : 0);
	return line;
}

void HandleDiscordJoin(const char* secret) {
	if (!secret) {
		Msg("Discord: Invalid null join secret.\n");
		return;
	}
	const size_t length = strnlen_s(secret, 128);
	if (length == 0 || length >= 128) {
		Msg("Discord: Invalid or oversized join secret (maximum 127 bytes).\n");
		return;
	}
	const auto target = p2p::ParseInviteTarget(std::string_view(secret, length));
	if (!target) {
		Msg("Discord: Invalid join secret.\n");
		return;
	}
	const std::string command = p2p::InviteConnectCommand(*target);
	if (command.empty()) {
		Msg("Discord: Unsupported join target.\n");
		return;
	}
	Cbuf_AddText(0, command.c_str(), 0);
}

void HandleDiscordJoinRequest(const discord::User request) {
	Msg("Discord: Join request from %s\n", request.GetUsername());
}


void HandleDiscordInvite(discord::ActivityActionType type, const discord::User user, const discord::Activity activity) {

	Msg("Discord: Invite %s\n", user.GetUsername());
}

// func for get local baseClient
typedef void* (__fastcall* GetBaseClientFunc)(int slot);
GetBaseClientFunc GetBaseClient;

bool IsDiscordProcessRunning() {
	DWORD process_id = 0;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	if (snapshot != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32 pe32;
		pe32.dwSize = sizeof(PROCESSENTRY32);

		if (Process32FirstW(snapshot, &pe32)) {
			do {
				std::wstring processNameLower = pe32.szExeFile;
				std::transform(processNameLower.begin(), processNameLower.end(), processNameLower.begin(), ::tolower);
				if (processNameLower == L"discord.exe" || processNameLower == L"discordcanary.exe" || processNameLower == L"discordptb.exe") {
					CloseHandle(snapshot);
					return true;
				}
			} while (Process32Next(snapshot, &pe32));
		}
		CloseHandle(snapshot);
	}
	return false;
}

void UpdatePlatformUserId() {
	discord::User user;
	auto result = core->UserManager().GetCurrentUser(&user);
	auto platform_user_id_var = OriginalCCVar_FindVar(cvarinterface, "platform_user_id");
	if (!platform_user_id_var) {
		Warning("Discord: platform_user_id convar is missing; skipping update\n");
		return;
	}
	if (UsesProcessUniquePlatformUserId()) {
		EnsurePlatformUserIdString(platform_user_id_var);
		return;
	}
	if (result != discord::Result::Ok) {
		Msg("Discord: Failed to get current user %d \n", result);
		SetConvarStringOriginal(platform_user_id_var, std::to_string(std::rand()).c_str());
		return;
	}
	SetConvarStringOriginal(platform_user_id_var, std::to_string(user.GetId()).c_str());
}

void HandleDiscordUserReady() {
	if (!CvarInterfaceReady()) {
		if (!WaitForCvarInterface(std::chrono::milliseconds(500)))
			platform_user_id_pending.store(true);
	}
	if (!CvarInterfaceReady()) {
		Warning("Discord: cvar interface is not ready yet; platform_user_id update deferred\n");
		return;
	}
	UpdatePlatformUserId();
}


DiscordCommandQueue g_DiscordCommandQueue;
void DiscordAuthCommand(const CCommand& args) {
#ifndef DISCORD
	Warning("Build was compiled without DISCORD defined.\n");
#else
	if (!is_discord_running) {
		Msg("Discord: Discord is not running\n");
		return;
	}
	
	if (args.ArgC() != 1) {
		Warning("Usage: delta_start_discord_auth\n");
		return;
	}
	if (IsDedicatedServer()) {
		Warning("This command is not available on dedicated servers.\n");
		return;
	}

	g_DiscordCommandQueue.AddCommand(DiscordCommandType::AUTH);
#endif
}

void ProcessDiscordAuth() {
	core->ApplicationManager().GetOAuth2Token([](discord::Result discordResult, const discord::OAuth2Token& token) {
		if (discordResult != discord::Result::Ok) {
			Msg("Discord: Failed to auth: %d\n", discordResult);
			return;
		}
		if (!CvarInterfaceReady()) {
			Warning("Discord: cvar interface is not ready; aborting auth token exchange\n");
			return;
		}
		auto* msUrlVar = OriginalCCVar_FindVar(cvarinterface, "delta_ms_url");
		auto* errorVar = OriginalCCVar_FindVar(cvarinterface, "delta_persistent_master_auth_token_failed_reason");
		auto* authVar = OriginalCCVar_FindVar(cvarinterface, "delta_persistent_master_auth_token");
		if (!msUrlVar || !errorVar || !authVar) {
			Warning("Discord: auth convars are missing; aborting auth token exchange\n");
			return;
		}
		auto ms_url = msUrlVar->m_Value.m_pszString;
		httplib::Client cli(ms_url);
		cli.set_connection_timeout(2, 0);
		cli.set_follow_location(true);
		cli.set_read_timeout(2, 0);
		auto stuff = std::format("/discord-auth?token={}", token.GetAccessToken());
		auto result = cli.Get(stuff);
		nlohmann::json j;
		try {
			j = nlohmann::json::parse(result->body);
		}
		catch (const std::exception& e) {
			return;
		}
		if (j.contains("error")) {
			SetConvarStringOriginal(errorVar, j["error"].get<std::string>().c_str());
			return;
		}
		auto token_j = j["token"].get<std::string>();
		SetConvarStringOriginal(authVar, token_j.c_str());
		SetConvarStringOriginal(errorVar, "");
		Msg("Discord: Successfully authenticated\n");
		});
}

void DiscordThread() {
	RegisterR1DeltaThread("DiscordThread");
	GetBaseClient = (GetBaseClientFunc)(G_engine + 0x5F470);
	G_public_ip = get_public_ip();
	// Do not start delivering callbacks before the cvar interface exists: the user-ready
	// handler needs its trampolines, and the SDK can fire it on the first RunCallbacks.
	const auto cvarWaitStart = std::chrono::steady_clock::now();
	const bool cvarReadyAtStart = WaitForCvarInterface(std::chrono::milliseconds(30000));
	const long long cvarWaitMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - cvarWaitStart).count();
	discord_cvar_wait_ms.store(cvarWaitMs);
	if (!cvarReadyAtStart)
		Warning("Discord: cvar interface did not appear within 30s; callbacks will defer until it does\n");
	else if (cvarWaitMs > 0)
		Msg("Discord: waited %lld ms for the cvar interface before starting callbacks\n", cvarWaitMs);
	auto result = discord::Core::Create(DISCORD_APPLICATION_ID, DiscordCreateFlags_NoRequireDiscord, &core);
	if (!IsDiscordProcessRunning()) {
		Msg("Discord: Discord not running.\n");
	}
	if (result != discord::Result::Ok) {
		return;
	}
	is_discord_running = true;

	core->ActivityManager().OnActivityJoin.Connect(HandleDiscordJoin);
	core->ActivityManager().OnActivityJoinRequest.Connect(HandleDiscordJoinRequest);
	core->ActivityManager().OnActivityInvite.Connect(HandleDiscordInvite);
	core->UserManager().OnCurrentUserUpdate.Connect(HandleDiscordUserReady);

	Msg("Discord: Core created successfully\n");
	
	while (true) {
		core->RunCallbacks();
		if (platform_user_id_pending.load() && CvarInterfaceReady()) {
			platform_user_id_pending.store(false);
			UpdatePlatformUserId();
		}
		DiscordCommand cmd;
		while (g_DiscordCommandQueue.GetNextCommand(cmd)) {
			if (auto* activity = std::get_if<discord::Activity>(&cmd)) {
				core->ActivityManager().UpdateActivity(*activity, [](discord::Result result) {
					if (result != discord::Result::Ok)
						Msg("Discord: Failed to update activity: %d\n", result);
				});
				continue;
			}
			switch (std::get<DiscordCommandType>(cmd)) {
			case DiscordCommandType::AUTH:
				ProcessDiscordAuth();
				break;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}
}

struct PresenceInfo {
	std::string mapName;
	std::string gameMode;
	std::string mapDisplayName;
	int playerCount;
	int maxPlayers;
	int team;
	std::string playlist;
	std::string playlistDisplayName;
	bool init;
	float endTime;
};




std::string CreateDiscordSecret() {
	if (!GetBaseClient)
		return {};
	auto base_client = GetBaseClient(-1);
	if (!base_client)
		return {};
	const auto net_chan = *reinterpret_cast<const uintptr_t*>(
		reinterpret_cast<uintptr_t>(base_client) + 0x20);
	if (!net_chan)
		return {};
	const auto* ns_addr = reinterpret_cast<const netadr_t*>(net_chan + 0xE4);
	const auto& nativeIp = ns_addr->GetIP();
	const uint16_t port = ntohs(ns_addr->GetPort());
	p2p::Ipv6Bytes address{};
	memcpy(address.data(), &nativeIp, address.size());
	const p2p::Ipv6Bytes listenSentinel{ 0, 0, 0xff, 0xff };
	const bool localHost = ns_addr->IsLoopback()
		|| IN6_IS_ADDR_LOOPBACK(&nativeIp)
		|| address == listenSentinel;
	std::string secret;
	if (localHost) {
		// A listed server is joined through "ip:port": delta_connect then asks the master server
		// for every route (punching, relays, overlays). A hidden server never heartbeats, so the
		// master has no entry to route a joiner behind NAT to, and a server that has not reached
		// the master yet has none either: those publish their own portable overlay route instead.
		const bool hidden = MasterServerClient::IsHostedServerHidden();
		const bool listed = !hidden && MasterServerClient::IsValidHeartBeat.load();
		int hostPort = port;
		if (!hostPort && cvarinterface && OriginalCCVar_FindVar) {
			if (const auto* host_port = OriginalCCVar_FindVar(cvarinterface, "hostport"))
				hostPort = host_port->m_Value.m_nValue;
		}
		if (hostPort <= 0 || hostPort > 65535)
			return {};
		if (!listed) {
			bool pending = false;
			secret = p2p::HostInviteTarget(hostPort, &pending);
			if (pending)
				return {};
			// A visible server only invites once the master server has it.
			if (secret.empty() && !hidden)
				return {};
			static std::string s_lastHostInvite;
			if (secret != s_lastHostInvite) {
				s_lastHostInvite = secret;
				Msg("Discord: hosted-server invite route is now %s\n",
					secret.empty() ? "direct ip:port" : secret.compare(0, 11, "r1di1|iroh|") == 0 ? "iroh"
					: secret.compare(0, 13, "r1di1|tailcat") == 0 ? "tailcat" : "EOS");
			}
		}
		if (secret.empty()) {
			// Validate the public address independently: no endpoint or command payload.
			const auto publicTarget = p2p::ParseInviteTarget(G_public_ip);
			if (!publicTarget || publicTarget->route != p2p::InviteRoute::Server
				|| publicTarget->address != G_public_ip || G_public_ip.find(':') != std::string::npos)
				return {};
			secret = std::format("{}:{}", G_public_ip, hostPort);
		}
	} else {
		if (ns_addr->GetType() != netadrtype_t::NA_IP || !port)
			return {};
		secret = p2p::ClientInviteTarget(address, port);
		if (secret.empty()) {
			if (address[0] == 0x3f && address[1] == 0xfd) {
				secret = p2p::PeerInviteTarget(address, port);
				if (secret.empty()) {
					Msg("Discord: No portable invite for this live overlay peer; target unavailable, unsupported, or exceeds 127 bytes.\n");
					return {};
				}
			} else if (IN6_IS_ADDR_V4MAPPED(&nativeIp)) {
				char ipv4[INET_ADDRSTRLEN]{};
				if (!inet_ntop(AF_INET, address.data() + 12, ipv4, sizeof(ipv4)))
					return {};
				secret = std::format("{}:{}", ipv4, port);
			} else if (address[0] == 0x3f && address[1] == 0xfe) {
				secret = p2p::FormatIpv6Connect(address, port);
			} else {
				return {};
			}
		}
	}
	if (secret.size() > 127) {
		Msg("Discord: Portable invite exceeds Discord's 127-byte limit; not advertising.\n");
		return {};
	}
	if (!p2p::ParseInviteTarget(secret)) {
		Msg("Discord: Invalid portable invite target; not advertising.\n");
		return {};
	}
	return secret;
}

void DoDiscordAuth()
{

}
SQInteger SendDiscordUI(HSQUIRRELVM v)
{
	if (!is_discord_running)
		return 1;
	
	discord::Activity activity;
	memset(&activity, 0, sizeof(activity));
	
	// get the name of loaded level
	const char* levelName = nullptr;

	sq_getstring(v, 2, &levelName);
	

	if (levelName != nullptr) {
		Msg("Discord: SendDiscordUI: Level name: %s\n", levelName);
		activity.SetName("R1Delta");
		activity.SetType(discord::ActivityType::Playing);
		activity.SetState("Loading");
		activity.GetParty().GetSize().SetCurrentSize(1);
		activity.GetParty().GetSize().SetMaxSize(1);
		char levelNameStr[256];
		localilze_string(levelName, levelNameStr, 256);
		activity.SetDetails(("Loading " + std::string(levelNameStr) + "...").c_str());
		activity.GetTimestamps().SetStart(time(nullptr));
		activity.GetAssets().SetLargeImage(levelName);
		activity.GetAssets().SetLargeText(levelNameStr);
		activity.GetAssets().SetSmallImage("logo");
		activity.GetAssets().SetSmallText("R1Delta");
		activity.SetSupportedPlatforms(static_cast<uint32_t>(discord::ActivitySupportedPlatformFlags::Desktop));
	}
	else {
		activity.SetName("R1Delta");
		activity.SetType(discord::ActivityType::Playing);
		activity.SetState("Main Menu");
		activity.GetParty().GetSize().SetCurrentSize(1);
		activity.GetParty().GetSize().SetMaxSize(1);
		activity.SetDetails("Playing Titanfall");
		activity.GetTimestamps().SetStart(time(nullptr));
		activity.GetAssets().SetLargeImage("logo");
		activity.GetAssets().SetLargeText("R1Delta");
		activity.SetSupportedPlatforms(static_cast<uint32_t>(discord::ActivitySupportedPlatformFlags::Desktop));

		activity.GetParty().SetId("R1Delta");
		activity.GetParty().SetPrivacy(discord::ActivityPartyPrivacy::Private);
	}
	g_DiscordCommandQueue.AddActivity(activity);


	return 1;
}

SQInteger SendDiscordClient(HSQUIRRELVM v)
{


	//Msg("Discord: SendDiscordClient\n");
	SQObject obj;
	SQInteger top = sq_gettop(nullptr, v);
	if (top < 2) {
		Warning("GetServerHeartbeat: Stack has less than 2 elements\n");
		return 1;
	}
	if (SQ_FAILED(sq_getstackobj(nullptr, v, 2, &obj))) {
		Warning("GetServerHeartbeat: Failed to get stack object at position 2\n");
		return 1;
	}
	if (obj._type != OT_TABLE) {
		Warning("GetServerHeartbeat: Object at stack pos 2 is not a table, type %d\n", obj._type);
		return 1;
	}

	auto table = obj._unVal.pTable;
	PresenceInfo presence{};
	SQBool init;
	sq_getbool(nullptr, v, 3, &init);
	if (!table) {
		Warning("GetServerHeartbeat: Table is null\n");
		return 1;
	}

	for (int i = 0; i < table->_numOfNodes; i++) {
		auto& node = table->_nodes[i];
		if (node.key._type != OT_STRING) continue;
		auto key = node.key._unVal.pString->_val;

		switch (node.val._type) {
		case OT_STRING: {
			auto s = reinterpret_cast<SQString*>(node.val._unVal.pRefCounted);
			if (!strcmp_static(key, "map_name")) presence.mapName = s->_val;
			if (!strcmp_static(key, "game_mode")) presence.gameMode = s->_val;
			if (!strcmp_static(key, "playlist")) presence.playlist = s->_val;
			if (!strcmp_static(key, "map_display_name")) {
				presence.mapDisplayName = s->_val;

				// Capture the localized map name for the watermark
				if (s->_val && s->length > 0) {
					strncpy(g_cl_MapDisplayName, s->_val, sizeof(g_cl_MapDisplayName) - 1);
					g_cl_MapDisplayName[sizeof(g_cl_MapDisplayName) - 1] = '\0';
					// Convert to lowercase
					for (int i = 0; g_cl_MapDisplayName[i]; i++) {
						g_cl_MapDisplayName[i] = tolower(g_cl_MapDisplayName[i]);
					}
				}
			}
			if (!strcmp_static(key, "playlist_display_name")) presence.playlistDisplayName = s->_val;
			break;
		}
		case OT_INTEGER:
			if (!strcmp_static(key, "max_players")) presence.maxPlayers = node.val._unVal.nInteger;
			if (!strcmp_static(key, "player_count")) presence.playerCount = node.val._unVal.nInteger;
			if (!strcmp_static(key, "team")) presence.team = node.val._unVal.nInteger;
			break;
		case OT_FLOAT:
			if (!strcmp_static(key, "end_time")) presence.endTime = node.val._unVal.fFloat;
			break;
		case OT_BOOL:
			break;
		}
	}
	if (!is_discord_running)
		return 1;


	discord::Activity activity;
	memset(&activity, 0, sizeof(activity));
	activity.SetName("R1Delta");
	activity.SetType(discord::ActivityType::Playing);
    activity.SetDetails((presence.playlistDisplayName).c_str());
	activity.SetState(presence.gameMode.c_str());
	if(init)
		activity.GetTimestamps().SetStart(time(nullptr));
	activity.GetAssets().SetLargeImage(presence.mapName.c_str());
	activity.GetAssets().SetLargeText(presence.mapDisplayName.c_str());
	activity.GetAssets().SetSmallImage("logo");
	activity.GetAssets().SetSmallText("R1Delta");
    auto sec = CreateDiscordSecret();  
    std::string partyId = "delta_" + std::string(sec);  
    activity.GetParty().SetId(partyId.c_str());
	activity.GetParty().SetPrivacy(discord::ActivityPartyPrivacy::Private);
	activity.GetParty().GetSize().SetCurrentSize(presence.playerCount);
	activity.GetParty().GetSize().SetMaxSize(presence.maxPlayers);
	if (presence.endTime && presence.mapName != "mp_lobby") {
		auto currentTime = time(nullptr);
		auto endTime = static_cast<time_t>(presence.endTime);
		activity.GetTimestamps().SetEnd(currentTime + endTime);
	}
	if (sec != "") {
		activity.GetSecrets().SetJoin(sec.c_str());
	}

 

	g_DiscordCommandQueue.AddActivity(activity);

	

	return 1;
}
