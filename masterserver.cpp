#pragma once
#include <ws2tcpip.h> // Required for getaddrinfo
#pragma comment(lib, "Ws2_32.lib") // Link with Ws2_32.lib for socket functions
#pragma comment(lib, "winhttp.lib")
#include "load.h"
#include <cstdlib>
#include <crtdbg.h>
#include <new>
#include "windows.h"
#include <iostream>
#include "cvar.h"
#include <random>

#include <winternl.h>  // For UNICODE_STRING.
#include <fstream>
#include <filesystem>
#include <array>
#include <intrin.h>
#include "memory.h"
#include "filesystem.h"
#include "defs.h"
#include "factory.h"
#include "core.h"
#include "load.h"
#include "patcher.h"
#include "MinHook.h"
#include "TableDestroyer.h"
#include "bitbuf.h"
#include "in6addr.h"
#include <fcntl.h>
#include <io.h>
#include <streambuf>
#include "logging.h"
#include <map>
#include "keyvalues.h"
#include "persistentdata.h"
#include "load.h"

#include <random>
#include "masterserver.h"
#include <winhttp.h>
#include <capnp/message.h>
#include <capnp/serialize.h>
#include "server.capnp.h"
#include <string>
#include <vector>
#include <sstream>

// Helper function to convert wide string to string
std::string WideToString(const std::wstring& wide) {
	if (wide.empty()) return std::string();
	int size = WideCharToMultiByte(CP_UTF8, 0, &wide[0], (int)wide.size(), nullptr, 0, nullptr, nullptr);
	std::string ret(size, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wide[0], (int)wide.size(), &ret[0], size, nullptr, nullptr);
	return ret;
}

// Helper function to convert string to wide string
std::wstring StringToWide(const std::string& str) {
	if (str.empty()) return std::wstring();
	int size = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), nullptr, 0);
	std::wstring ret(size, 0);
	MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &ret[0], size);
	return ret;
}

class WinHttpClient {
private:
	HINTERNET hSession = nullptr;
	HINTERNET hConnect = nullptr;
	HINTERNET hRequest = nullptr;

public:
	WinHttpClient() {
		hSession = WinHttpOpen(L"R1Delta", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	}

	~WinHttpClient() {
		if (hRequest) WinHttpCloseHandle(hRequest);
		if (hConnect) WinHttpCloseHandle(hConnect);
		if (hSession) WinHttpCloseHandle(hSession);
	}
	std::wstring ResolveHostToIPv4(const std::wstring& host) {
		ADDRINFOW hints = {};
		hints.ai_family = AF_INET; // Only IPv4
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		ADDRINFOW* result = nullptr;
		int status = GetAddrInfoW(host.c_str(), nullptr, &hints, &result);
		if (status != 0) {
			Warning("getaddrinfo failed with error: %d\n", status);
		}

		std::wstring ipv4_address;
		if (result != nullptr) {
			for (ADDRINFOW* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
				if (ptr->ai_family == AF_INET) {
					sockaddr_in* ipv4 = reinterpret_cast<sockaddr_in*>(ptr->ai_addr);
					wchar_t ipstr[INET_ADDRSTRLEN];
					InetNtopW(AF_INET, &(ipv4->sin_addr), ipstr, INET_ADDRSTRLEN);
					ipv4_address = ipstr;
					break;
				}
			}
			FreeAddrInfoW(result);
		}

		if (ipv4_address.empty()) {
			Warning("No IPv4 address found for the host\n", status);
		}

		return ipv4_address;
	}

	std::vector<uint8_t> SendRequest(const std::wstring& host, const std::wstring& path,
		const std::vector<uint8_t>& data, bool isPost = true) {
		if (!hSession) return {};

		std::wstring ipv4_host;

		try {
			ipv4_host = ResolveHostToIPv4(host);
		}
		catch (std::runtime_error& e) {
			Warning("Error during host resolution: %s\n", e.what());
			return {};
		}

		hConnect = WinHttpConnect(hSession, ipv4_host.c_str(), INTERNET_DEFAULT_HTTP_PORT, 0);
		if (!hConnect) {
			DWORD error = GetLastError();
			Warning("WinHttpConnect failed with error %lu\n", error);
			return {};
		}

		const wchar_t* verb = isPost ? L"POST" : L"GET";
		hRequest = WinHttpOpenRequest(hConnect, verb, path.c_str(),
			nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);

		if (!hRequest) {
			DWORD error = GetLastError();
			Warning("WinHttpOpenRequest failed with error %lu\n", error);
			return {};
		}
		// Add the Host header here
		std::wstring hostHeader = L"Host: " + host;
		WinHttpAddRequestHeaders(hRequest, hostHeader.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);

		// Set request header for Cap'n Proto content
		WinHttpAddRequestHeaders(hRequest, L"Content-Type: application/x-capnproto",
			-1, WINHTTP_ADDREQ_FLAG_ADD);

		if (isPost && !data.empty()) {
			BOOL result = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
				(LPVOID)data.data(), data.size(), data.size(), 0);
			if (!result) {
				DWORD error = GetLastError();
				Warning("WinHttpSendRequest (POST) failed with error %lu\n", error);
				return {};
			}
		}
		else {
			BOOL result = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS,
				0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
			if (!result) {
				DWORD error = GetLastError();
				Warning("WinHttpSendRequest (GET) failed with error %lu\n", error);
				return {};
			}
		}

		if (!WinHttpReceiveResponse(hRequest, nullptr)) {
			DWORD error = GetLastError();
			Warning("WinHttpReceiveResponse failed with error %lu\n", error);
			return {};
		}

		std::vector<uint8_t> response;
		DWORD bytesAvailable;
		while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
			std::vector<uint8_t> buffer(bytesAvailable, 0);
			DWORD bytesRead;
			if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
				response.insert(response.end(), buffer.begin(), buffer.begin() + bytesRead);
			}
			else {
				DWORD error = GetLastError();
				Warning("WinHttpReadData failed with error %lu\n", error);
				return {};
			}
		}
		response.push_back(0);

		return response;
	}
};

std::string generate_uuid() {
	GUID guid;
	CoCreateGuid(&guid);

	wchar_t guidString[39];
	StringFromGUID2(guid, guidString, sizeof(guidString) / sizeof(wchar_t));

	std::wstring wide(guidString);
	std::string result = WideToString(wide);
	result = result.substr(1, result.size() - 2); // Remove braces
	return result;
}

void GetServerHeartbeat(HSQUIRRELVM v) {
	SQObject obj;
	SQInteger top = sq_gettop(nullptr, v);
	//Warning("GetServerHeartbeat: Stack top: %d\n", top);
	if (top < 2) {
		Warning("GetServerHeartbeat: Stack has less than 2 elements\n");
		return;
	}
	if (SQ_FAILED(sq_getstackobj(nullptr, v, 2, &obj))) {
		Warning("GetServerHeartbeat: Failed to get stack object at position 2\n");
		return;
	}

	if (obj._type != OT_TABLE) {
		Warning("GetServerHeartbeat: Object at stack position 2 is not a table, but type %d\n", obj._type);
		return;
	}

	auto table = obj._unVal.pTable;
	if (!table) {
		Warning("GetServerHeartbeat: Table is null\n");
		return;
	}

	//Warning("GetServerHeartbeat: Processing server heartbeat table. Table size: %d, numNodes: %d\n", table->size, table->_numOfNodes);

	// Create Cap'n Proto message
	::capnp::MallocMessageBuilder message;
	auto heartbeat = message.initRoot<ServerHeartbeat>();
	// Iterate through the hash table nodes
	for (int i = 0; i < table->_numOfNodes; i++) {
		auto node = &table->_nodes[i];
		if (node->key._type != OT_STRING) {
			//		Warning("GetServerHeartbeat: Skipping non-string key at index %d, type: %d\n", i, node->key._type);
			continue;
		}

		auto key = node->key._unVal.pString->_val;
		//	Warning("GetServerHeartbeat:   Processing key: '%s', type: %d at index %d\n", key, node->val._type, i);

			// Handle different value types
		switch (node->val._type) {
		case OT_STRING: {
			auto str = reinterpret_cast<SQString*>(node->val._unVal.pRefCounted);
			if (strcmp(key, "host_name") == 0) {
				heartbeat.setHostname(str->_val);
				//	Warning("GetServerHeartbeat:     Set host_name: '%s'\n", str->_val);
			}
			else if (strcmp(key, "map_name") == 0) {
				heartbeat.setMapName(str->_val);
				//	Warning("GetServerHeartbeat:     Set map_name: '%s'\n", str->_val);
			}
			else if (strcmp(key, "game_mode") == 0) {
				heartbeat.setGameMode(str->_val);
				//	Warning("GetServerHeartbeat:     Set game_mode: '%s'\n", str->_val);
			}
			break;
		}

		case OT_INTEGER:
			if (strcmp(key, "max_players") == 0) {
				heartbeat.setMaxPlayers(node->val._unVal.nInteger);
				//	Warning("GetServerHeartbeat:     Set max_players: %d\n", node->val._unVal.nInteger);
			}
			else if (strcmp(key, "port") == 0) {
				heartbeat.setPort(node->val._unVal.nInteger);
				//Warning("GetServerHeartbeat:     Set port: %d\n", node->val._unVal.nInteger);
			}
			break;

		case OT_ARRAY:
			if (strcmp(key, "players") == 0) {
				auto arr = node->val._unVal.pArray;
				if (!arr) {
					//	Warning("GetServerHeartbeat: Players array is null\n");
					continue;
				}
				//Warning("GetServerHeartbeat: Processing players array with %d slots\n", arr->_usedSlots);
				auto players = heartbeat.initPlayers(arr->_usedSlots);

				for (int j = 0; j < arr->_usedSlots; j++) {
					if (arr->_values[j]._type != OT_TABLE) {
						//Warning("GetServerHeartbeat: Skipping non-table player at index %d, type: %d\n", j, arr->_values[j]._type);
						continue;
					}

					auto playerTable = arr->_values[j]._unVal.pTable;
					if (!playerTable) {
						//		Warning("GetServerHeartbeat: Player table at index %d is null\n", j);
						continue;
					}
					auto player = players[j];
					//	Warning("GetServerHeartbeat: Processing player table at index %d\n", j);

					for (int k = 0; k < playerTable->_numOfNodes; k++) {
						auto pnode = &playerTable->_nodes[k];
						if (pnode->key._type != OT_STRING) {
							//Warning("GetServerHeartbeat: Skipping non-string key in player table, type: %d\n", pnode->key._type);
							continue;
						}
						auto pkey = pnode->key._unVal.pString->_val;
						//Warning("GetServerHeartbeat:   Processing player key: '%s', type: %d\n", pkey, pnode->val._type);

						switch (pnode->val._type) {
						case OT_STRING: {
							auto str = reinterpret_cast<SQString*>(pnode->val._unVal.pRefCounted);
							if (strcmp(pkey, "name") == 0) {
								player.setName(str->_val);
								//Warning("GetServerHeartbeat:     Set player name: '%s'\n", str->_val);
							}
							break;
						}
						case OT_INTEGER:
							if (strcmp(pkey, "gen") == 0) {
								player.setGen(pnode->val._unVal.nInteger);
								//Warning("GetServerHeartbeat:     Set player gen: %d\n", pnode->val._unVal.nInteger);
							}
							else if (strcmp(pkey, "lvl") == 0) {
								player.setLvl(pnode->val._unVal.nInteger);
								//Warning("GetServerHeartbeat:     Set player lvl: %d\n", pnode->val._unVal.nInteger);
							}
							else if (strcmp(pkey, "team") == 0) {
								player.setTeam(pnode->val._unVal.nInteger);
								//Warning("GetServerHeartbeat:     Set player team: %d\n", pnode->val._unVal.nInteger);
							}
							break;
						}
					}
				}
			}
			break;
		}
	}

	static auto hostport_cvar = CCVar_FindVar(cvarinterface, "hostport");
	int port = hostport_cvar->m_Value.m_nValue;
	heartbeat.setPort(port);

	// Serialize the message
	kj::Array<capnp::word> serialized = messageToFlatArray(message);
	std::vector<uint8_t> data(reinterpret_cast<uint8_t*>(serialized.begin()),
		reinterpret_cast<uint8_t*>(serialized.end()));

	// Send using WinHTTP
	std::thread([data = std::move(data)]() {
		WinHttpClient client;
		auto ms_url = CCVar_FindVar(cvarinterface, "r1d_ms");
		std::wstring host = StringToWide(ms_url->m_Value.m_pszString);

		auto response = client.SendRequest(host, L"/server/heartbeat", data, true);
		if (!response.empty() && response.size() > 2) {
			Warning("GetServerHeartbeat: MS reports: %s\n", reinterpret_cast<char*>(response.data()));
		}
		}).detach();
}
SQInteger GetServerList(HSQUIRRELVM v) {
	try {
		WinHttpClient client;
		auto ms_url = CCVar_FindVar(cvarinterface, "r1d_ms");
		std::wstring host = StringToWide(ms_url->m_Value.m_pszString);

		auto response = client.SendRequest(host, L"/server/", {}, false);

		if (!response.empty()) {
			try {
				kj::ArrayPtr<const capnp::word> words(
					reinterpret_cast<const capnp::word*>(response.data()),
					response.size() / sizeof(capnp::word));

				capnp::FlatArrayMessageReader reader(words);
				auto serverList = reader.getRoot<ServerList>();

				// Convert to Squirrel array
				sq_newarray(v, 0);

				for (auto server : serverList.getServers()) {
					sq_newtable(v);

					sq_pushstring(v, "host_name", -1);
					sq_pushstring(v, server.getHostname().cStr(), -1);
					sq_newslot(v, -3, 0);

					sq_pushstring(v, "map_name", -1);
					sq_pushstring(v, server.getMapName().cStr(), -1);
					sq_newslot(v, -3, 0);

					sq_pushstring(v, "game_mode", -1);
					sq_pushstring(v, server.getGameMode().cStr(), -1);
					sq_newslot(v, -3, 0);

					sq_pushstring(v, "max_players", -1);
					sq_pushinteger(0, v, server.getMaxPlayers());
					sq_newslot(v, -3, 0);

					sq_pushstring(v, "port", -1);
					sq_pushinteger(0, v, server.getPort());
					sq_newslot(v, -3, 0);

					sq_pushstring(v, "ip", -1);
					sq_pushstring(v, server.getIp().cStr(), -1);
					sq_newslot(v, -3, 0);

					sq_pushstring(v, "players", -1);
					sq_newarray(v, 0);

					for (auto player : server.getPlayers()) {
						sq_newtable(v);

						sq_pushstring(v, "name", -1);
						sq_pushstring(v, player.getName().cStr(), -1);
						sq_newslot(v, -3, 0);

						sq_pushstring(v, "gen", -1);
						sq_pushinteger(0, v, player.getGen());
						sq_newslot(v, -3, 0);

						sq_pushstring(v, "lvl", -1);
						sq_pushinteger(0, v, player.getLvl());
						sq_newslot(v, -3, 0);

						sq_pushstring(v, "team", -1);
						sq_pushinteger(0, v, player.getTeam());
						sq_newslot(v, -3, 0);

						sq_arrayappend(v, -2);
					}

					sq_newslot(v, -3, 0);
					sq_arrayappend(v, -2);
				}

				return 1;
			}
			catch (const kj::Exception& e) {
				// Cap'n Proto parsing error - fall through to create fake server
			}
		}
	}
	catch (const std::exception& e) {
		// Network error or other exception - fall through to create fake server
	}

	// Create fake "No Servers Found" entry
	sq_newarray(v, 0);
	sq_newtable(v);

	sq_pushstring(v, "host_name", -1);
	sq_pushstring(v, "* But nobody came.", -1);
	sq_newslot(v, -3, 0);

	sq_pushstring(v, "map_name", -1);
	sq_pushstring(v, "mp_lobby", -1);
	sq_newslot(v, -3, 0);

	sq_pushstring(v, "game_mode", -1);
	sq_pushstring(v, "-", -1);
	sq_newslot(v, -3, 0);

	sq_pushstring(v, "max_players", -1);
	sq_pushinteger(0, v, 0);
	sq_newslot(v, -3, 0);

	sq_pushstring(v, "port", -1);
	sq_pushinteger(0, v, 0);
	sq_newslot(v, -3, 0);

	sq_pushstring(v, "ip", -1);
	sq_pushstring(v, "0.0.0.0", -1);
	sq_newslot(v, -3, 0);

	sq_pushstring(v, "players", -1);
	sq_newarray(v, 0);
	sq_newslot(v, -3, 0);

	sq_arrayappend(v, -2);

	return 1;
}
pCHostState__State_GameShutdown_t oGameShutDown;
void Hk_CHostState__State_GameShutdown(void* thisptr) {
	static auto hostport_cvar = CCVar_FindVar(cvarinterface, "hostport");
	static auto host_map_cvar = CCVar_FindVar(cvarinterface, "host_map");
	if (strlen(host_map_cvar->m_Value.m_pszString) > 2) {
		std::thread([]() {
			int port = hostport_cvar->m_Value.m_nValue;
			WinHttpClient client;
			auto ms_url = CCVar_FindVar(cvarinterface, "r1d_ms");
			std::wstring host = StringToWide(ms_url->m_Value.m_pszString);
			std::wstring path = L"/server/delete?port=" + std::to_wstring(port);

			client.SendRequest(host, path, {}, true);
			}).detach();
			Cbuf_AddTextOriginal(0, "host_map \"\"\n", 0);
	}

	oGameShutDown(thisptr);
}