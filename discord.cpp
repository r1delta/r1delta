#include "discord.h"
#include <thread>
#include <chrono>
#include <string>
#include <algorithm>
#include <windows.h>
#include <tlhelp32.h>
#include "utils.h"
#include "netadr.h"
static bool is_discord_running = false;

void HandleDiscordJoin(const char* secret) {

	// do somethign with it
	Msg("Discord Secert %s\n", secret);
	Cbuf_AddText(0, ("disconnect;connect " + std::string(secret)).c_str(), 0);
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

struct ns_address
{
	netadr_t m_adr; // ip:port and network type (NULL/IP/BROADCAST/etc).
};


const char* CreateDiscordSecret() {
	auto base_client = GetBaseClient(-1);
	//*(_QWORD *)(a1 + 0x20) = v6;
	if (!base_client) {
		return "";
	}
	auto net_chan = *(uintptr_t*)((uintptr_t)(base_client) + 0x20);
	auto ns_addr =(uintptr_t*)(net_chan+ 0xE4);
	typedef const char* (__fastcall* to_string_t)(uintptr_t* netadr,int t);
	auto to_string = (to_string_t)(G_engine + 0x4885B0);
	auto ip_port = to_string(ns_addr, 0);
	if (ip_port == "loopback") {
		return "loop";
	}
	return ip_port;
	//Cbuf_AddText(0, ("disconnect;connect " + std::string(ip_port)).c_str(), 0);

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
	activity.GetSecrets().SetJoin(CreateDiscordSecret());
	
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
