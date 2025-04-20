#include "discord.h"
#include <thread>
#include <chrono>
#include <string>
#include <algorithm>
#include <windows.h>
#include <tlhelp32.h>
static bool is_discord_running = false;
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
	auto result = discord::Core::Create(DISCORD_APPLICATION_ID, DiscordCreateFlags_NoRequireDiscord, &core);
	if (!IsDiscordProcessRunning()) {
		Msg("Discord: Discord not running.\n");
	}
	if (result != discord::Result::Ok) {
		Msg("Discord: Failed to create core:\n");
		return;
	}

	is_discord_running = true;

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
};

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

	Msg("Discord: SendDiscordClient\n");
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
		}
	}


	discord::Activity activity;
	memset(&activity, 0, sizeof(activity));
	activity.SetName("R1Delta");
	activity.SetType(discord::ActivityType::Playing);
    activity.SetDetails((presence.playlistDisplayName).c_str());
	activity.SetState(presence.gameMode.c_str());
	activity.GetTimestamps().SetStart(time(nullptr));
	activity.GetAssets().SetLargeImage(presence.mapName.c_str());
	activity.GetAssets().SetLargeText(presence.mapDisplayName.c_str());
	activity.GetAssets().SetSmallImage("logo");
	activity.GetAssets().SetSmallText("R1Delta");
	activity.GetParty().SetId("R1Delta");
	activity.GetParty().SetPrivacy(discord::ActivityPartyPrivacy::Private);
	activity.GetParty().GetSize().SetCurrentSize(presence.playerCount);
	activity.GetParty().GetSize().SetMaxSize(presence.maxPlayers);
	
	Msg("Playlist: %s\n", presence.playlist.c_str());

	//activity.SetSupportedPlatforms(static_cast<uint32_t>(discord::ActivitySupportedPlatformFlags::Desktop));

	/*if (presence.mapName == "mp_lobby") {
		activity.SetState("In the Lobby");
	}*/

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
