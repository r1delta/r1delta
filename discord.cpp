#include "discord.h"
#include <thread>
#include <chrono>
#include <string>
#include <algorithm>
#include <windows.h>
#include <tlhelp32.h>
#include "utils.h"
#include "netadr.h"
#include <regex>
#include "httplib.h"
#include "masterserver.h"

static bool is_discord_running = false;

bool parseAndValidateIpOctets(const char* ip_part, size_t ip_len, unsigned int& o1, unsigned int& o2, unsigned int& o3, unsigned int& o4) {
	// Ensure the ip_part doesn't contain invalid characters (like another ':')
	// or start/end with '.' or have consecutive '..'
	if (ip_len == 0 || ip_part[0] == '.' || ip_part[ip_len - 1] == '.') {
		return false;
	}
	for (size_t i = 0; i < ip_len; ++i) {
		if (!(isdigit(ip_part[i]) || ip_part[i] == '.')) {
			return false; // Invalid character
		}
		if (i > 0 && ip_part[i] == '.' && ip_part[i - 1] == '.') {
			return false; // Consecutive dots
		}
	}

	int chars_consumed = 0;
	// Use sscanf on the specific IP part. %n checks if the whole IP part was consumed.
	// Need a temporary buffer if ip_part is not null-terminated correctly
	// (e.g., if using strncpy without manual termination). Using std::string is safer.
	std::string ip_str(ip_part, ip_len); // Create a null-terminated string

	if (sscanf(ip_str.c_str(), "%3u.%3u.%3u.%3u%n", &o1, &o2, &o3, &o4, &chars_consumed) != 4) {
		return false; // Didn't parse exactly 4 numbers
	}

	// Ensure the *entire* IP part string was consumed by sscanf
	if (static_cast<size_t>(chars_consumed) != ip_str.length()) {
		return false; // Extra characters found within or after the IP part
	}

	// Check numeric ranges
	return (o1 <= 255 && o2 <= 255 && o3 <= 255 && o4 <= 255);
}


bool isIpPortValid(const char* address) {
	if (!address || address[0] == '\0') {
		return false;
	}

	const char* colon_ptr = strrchr(address, ':'); // Find the *last* colon
	const char* ip_part_start = address;
	size_t ip_part_len = 0;
	unsigned int o1, o2, o3, o4; // For IP octets
	unsigned long port = 0;      // For port number

	if (colon_ptr != nullptr) {
		// Potential IP:Port format
		ip_part_len = colon_ptr - address;
		const char* port_part_start = colon_ptr + 1;
		size_t port_part_len = strlen(port_part_start);

		if (ip_part_len == 0 || port_part_len == 0) {
			return false; // IP or Port part is empty (e.g., ":123" or "1.2.3.4:")
		}

		// Validate IP Part
		if (!parseAndValidateIpOctets(ip_part_start, ip_part_len, o1, o2, o3, o4)) {
			return false;
		}

		// Validate Port Part
		char* endptr;
		errno = 0; // Reset errno before calling strtoul
		port = strtoul(port_part_start, &endptr, 10);

		// Check for conversion errors (non-digit chars, overflow, underflow)
		if (errno != 0 || *endptr != '\0' || port_part_start == endptr) {
			// errno check handles overflow/underflow
			// *endptr != '\0' checks if the entire port string was consumed
			// port_part_start == endptr checks if any digits were converted at all
			return false;
		}

		// Check port range (standard ports are 1-65535)
		if (port == 0 || port > 65535) { // Port 0 is often reserved/invalid for connections
			return false;
		}

		// If we reach here, both IP and Port are valid
		return true;

	}
	else {
		// No colon found, treat the whole string as an IP address
		ip_part_len = strlen(address);
		if (ip_part_len == 0) return false; // Should have been caught earlier, but good practice

		// Validate the entire string as just an IP
		return parseAndValidateIpOctets(ip_part_start, ip_part_len, o1, o2, o3, o4);
	}
}


void HandleDiscordJoin(const char* secret) {

	// do somethign with it
	// make sure secret is a valid ipv4 adress
	Msg("Discord: Join secret: %s\n", secret);
	if (!isIpPortValid(secret)) {
		Msg("Discord: Invalid secret: %s\n", secret);
		return;
	}
	//remove all ;
	std::string secretStr(secret);
	secretStr.erase(std::remove(secretStr.begin(), secretStr.end(), ';'), secretStr.end());
	// remove all \n
	secretStr.erase(std::remove(secretStr.begin(), secretStr.end(), '\n'), secretStr.end());
	// remove all \r
	secretStr.erase(std::remove(secretStr.begin(), secretStr.end(), '\r'), secretStr.end());
	// remove all \t
	secretStr.erase(std::remove(secretStr.begin(), secretStr.end(), '\t'), secretStr.end());
	// remove all \"
	secretStr.erase(std::remove(secretStr.begin(), secretStr.end(), '\"'), secretStr.end());

	Cbuf_AddText(0, ("disconnect;connect " + std::string(secretStr)).c_str(), 0);
	return;
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

void DiscordThread() {
	GetBaseClient = (GetBaseClientFunc)(G_engine + 0x5F470);
	G_public_ip = get_public_ip();
	auto result = discord::Core::Create(DISCORD_APPLICATION_ID, DiscordCreateFlags_NoRequireDiscord, &core);
	if (!IsDiscordProcessRunning()) {
		Msg("Discord: Discord not running.\n");
	}
	if (result != discord::Result::Ok) {
		Msg("Discord: Failed to create core:\n");
		return;
	}
	is_discord_running = true;

	core->ActivityManager().OnActivityJoin.Connect(HandleDiscordJoin);
	core->ActivityManager().OnActivityJoinRequest.Connect(HandleDiscordJoinRequest);
	core->ActivityManager().OnActivityInvite.Connect(HandleDiscordInvite);
	//if (auto x = core->ActivityManager().RegisterCommand("%localappdata%/R1Delta/r1delta.exe") != discord::Result::Ok) {
	//	Msg("Discord: Failed to register command %d\n", x);
	//}

	Msg("Discord: Core created successfully\n");
	
	while (true) {
		core->RunCallbacks();
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
	auto base_client = GetBaseClient(-1);
	if (!base_client) {
		return "";
	}
	auto net_chan = *(uintptr_t*)((uintptr_t)(base_client) + 0x20);
	auto ns_addr = (netadr_t*)(net_chan + 0xE4);
	auto port = htons(ns_addr->GetPort());
	std::string ip = CallVFunc<char*>(0x1, (void*)net_chan);
	if (!ns_addr) {
		return "";
	}
	if (ip.empty()) {
		return "";
	}
	if (ip.compare("0:ffff::") == 0) {
		if (!MasterServerClient::IsValidHeartBeat.load()) {
			return "";
		}
		auto port = ns_addr->GetPort();
		if (!port) {
			auto host_port = CCVar_FindVar(cvarinterface, "hostport");
			port = host_port->m_Value.m_nValue;
		}
		std::string public_ip = G_public_ip;
		return std::format("{}:{}", public_ip, port);
	}
	
	return std::format("{}:{}", ip, port);

}


SQInteger SendDiscordUI(HSQUIRRELVM v)
{
	if (!is_discord_running) {
		Warning("SendDiscordUI: Discord is not running");
		return 1;
	}

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
	core->ActivityManager().UpdateActivity(activity, [](discord::Result result) {
		if (result != discord::Result::Ok) {
			Msg("Discord: Failed to update activity: %d\n", result);
		}
		else {
			Msg("Discord: Activity updated successfully\n");
		}
		});


	return 1;
}

SQInteger SendDiscordClient(HSQUIRRELVM v)
{
	if (!is_discord_running) {
		Warning("SendDiscordClient: Discord is not running\n");
		return 1;
	}

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
	PresenceInfo presence;
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
			if (!strcmp_static(key, "map_display_name")) presence.mapDisplayName = s->_val;
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

 

	core->ActivityManager().UpdateActivity(activity, [](discord::Result result) {
		if (result != discord::Result::Ok) {
			Msg("Discord: Failed to update activity: %d\n", result);
		}
		//else {
		//	Msg("Discord: Activity updated successfully\n");
		//}
		});

	

	return 1;
}
