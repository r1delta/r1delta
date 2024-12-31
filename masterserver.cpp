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
#include "tctx.hh"


#include "masterserver.h"
#include <winhttp.h>
#include <capnp/message.h>
#include <capnp/serialize.h>
#include "server.capnp.h"
#include <string>
#include <vector>
#include <sstream>


#if 0
// Helper function to convert wide string to string
std::string WideToString(const std::wstring_view& wide) {
	if (wide.empty()) return std::string();
	int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
	std::string ret(size, 0);
	WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), &ret[0], size, nullptr, nullptr);
	return ret;
}

// Helper function to convert string to wide string
std::wstring StringToWide(const std::string_view& str) {
	if (str.empty()) return std::wstring();
	int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
	std::wstring ret(size, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &ret[0], size);
	return ret;
}
#endif

// Helper function to convert wide string to string
// NOTE(mrsteyk): null terminated
char* WideToStringArena(Arena* arena, const std::wstring_view& wide) {
	if (wide.empty()) return arena_strdup(arena, "", 0);
	int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
	auto ret = (char*)arena_push(arena, size + 1);
	WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), ret, size, nullptr, nullptr);
	ret[size] = 0;
	return ret;
}

struct String16
{
	wchar_t* ptr;
	size_t size;
};

// Helper function to convert string to wide string
// NOTE(mrsteyk): null terminated
String16 StringToWideArena(Arena* arena, const std::string_view& str) {
	if (str.empty()) return { arena_wstrdup(arena, L"", 0), 0 };
	int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
	auto ret = (wchar_t*)arena_push(arena, (size + 1) * sizeof(wchar_t));
	MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), ret, size);
	ret[size] = 0;
	return { ret, (size_t)size };
}

uint64_t HashS16(String16 s)
{
	constexpr uint64_t prime = 1111111111111111111;
	constexpr uint64_t offset = 0x100;

	auto ret = offset;
	for (size_t i = 0; i < s.size; ++i)
	{
		ret ^= s.ptr[i];
		ret *= prime;
	}

	return ret;
}

struct U8Array
{
	uint8_t* ptr;
	size_t size;

	bool empty() {
		return !size;
	}
};

class WinHttpClient {
private:
	HINTERNET hSession = nullptr;
	HINTERNET hConnect = nullptr;
	HINTERNET hRequest = nullptr;
	size_t ipv4_addr_hash = 0;
	size_t ipv4_addr_hash_len = 0;
	wchar_t ipv4_addr[INET_ADDRSTRLEN]{ 0 };

public:
	WinHttpClient() {
		hSession = WinHttpOpen(L"R1Delta", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		DWORD set = WINHTTP_DECOMPRESSION_FLAG_ALL;
		WinHttpSetOption(hSession, WINHTTP_OPTION_DECOMPRESSION, &set, sizeof(set));
#if 0
		set = WINHTTP_PROTOCOL_FLAG_HTTP2 | WINHTTP_PROTOCOL_FLAG_HTTP3;
		WinHttpSetOption(hSession, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &set, sizeof(set));
#endif
	}

	~WinHttpClient() {
		if (hRequest) WinHttpCloseHandle(hRequest);
		if (hConnect) WinHttpCloseHandle(hConnect);
		if (hSession) WinHttpCloseHandle(hSession);
	}

	void ResolveHostToIPv4(const wchar_t* host) {

		ADDRINFOW hints = {};
		hints.ai_family = AF_INET; // Only IPv4
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;


		ADDRINFOW* result = nullptr;
		int status = GetAddrInfoW(host, nullptr, &hints, &result);
		if (status != 0) {
			Warning("getaddrinfo failed with error: %d\n", status);
		}

		if (result != nullptr) {
			for (ADDRINFOW* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
				if (ptr->ai_family == AF_INET) {
					sockaddr_in* ipv4 = reinterpret_cast<sockaddr_in*>(ptr->ai_addr);
					wchar_t ipstr[INET_ADDRSTRLEN];
					static_assert(sizeof(ipstr) == sizeof(ipv4_addr));
					InetNtopW(AF_INET, &(ipv4->sin_addr), ipstr, INET_ADDRSTRLEN);
					memcpy(ipv4_addr, ipstr, sizeof(ipv4_addr));
					break;
				}
			}
			FreeAddrInfoW(result);
		}

		if (!ipv4_addr[0]) {
			Warning("No IPv4 address found for the host\n", status);
		}
	}

	// NOTE(mrsteyk): Whoever decided to always return IPv6 not supported is an idiot.
	U8Array SendRequest(Arena* perm, const String16 host, const wchar_t* path,
		const U8Array data, bool isPost = true, bool ipv4_force = true)
	{
		if (!hSession) return {0, 0};

		auto arena = tctx.get_arena_for_scratch(&perm, 1);
		auto temp = TempArena(arena);

		// NOTE(mrsteyk): please don't do this every single fucking time, thanks.
		// TODO(mrsteyk): cvar change callback to rebind this class to a different host?
		if (ipv4_force) {
			size_t ahash = 0;
			size_t asize = host.size;
			// NOTE(mrsteyk): !ipv4_addr[0] check does nothing useful, other fields will not match as well if that's the case. 
			if (ipv4_addr_hash_len != asize || ipv4_addr_hash != (ahash = HashS16(host))) {
				try {
					ResolveHostToIPv4(host.ptr);
					if (ipv4_addr[0])
					{
						ipv4_addr_hash_len = asize;
						ipv4_addr_hash = ahash ? ahash : HashS16(host);
					}
				}
				catch (std::runtime_error& e) {
					Warning("Error during host resolution: %s\n", e.what());
					return {};
				}
			}

			hConnect = WinHttpConnect(hSession, ipv4_addr, INTERNET_DEFAULT_HTTP_PORT, 0);
		}
		else {
			hConnect = WinHttpConnect(hSession, host.ptr, INTERNET_DEFAULT_HTTP_PORT, 0);

		}

		if (!hConnect) {
			DWORD error = GetLastError();
			Warning("WinHttpConnect failed with error %lu\n", error);
			return {};
		}

		const wchar_t* verb = isPost ? L"POST" : L"GET";
		hRequest = WinHttpOpenRequest(hConnect, verb, path,
			nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);

		if (!hRequest) {
			DWORD error = GetLastError();
			Warning("WinHttpOpenRequest failed with error %lu\n", error);
			return {};
		}
		// Add the Host header here
		const wchar_t host_header_prefix[] = L"Host: ";
		constexpr size_t host_header_prefix_characters = sizeof(host_header_prefix) / sizeof(host_header_prefix[0]) - 1;
		
		size_t host_header_size = host_header_prefix_characters + host.size;
		auto hostHeader = (wchar_t*)arena_push(arena, (host_header_size + 1) * sizeof(host_header_prefix[0]));
		memcpy(hostHeader, host_header_prefix, host_header_prefix_characters * sizeof(hostHeader[0]));
		memcpy(hostHeader + host_header_prefix_characters, host.ptr, host.size * sizeof(hostHeader[0]));
		hostHeader[host_header_size] = 0;
		WinHttpAddRequestHeaders(hRequest, hostHeader, (DWORD)host_header_size, WINHTTP_ADDREQ_FLAG_ADD);

		// Set request header for Cap'n Proto content
		WinHttpAddRequestHeaders(hRequest, L"Content-Type: application/x-capnproto",
			-1, WINHTTP_ADDREQ_FLAG_ADD);

		if (isPost && data.size) {
			BOOL result = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
				(LPVOID)data.ptr, (DWORD)data.size, (DWORD)data.size, 0);
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

		//std::vector<uint8_t> response;
		size_t response_capacity = (16 << 10);
		U8Array response;
		response.ptr = (uint8_t*)arena_push(perm, response_capacity);
		response.size = 0;
		DWORD bytesAvailable;
		while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
			// TODO(mrsteyk): rewrite this.
			//std::vector<uint8_t> buffer(bytesAvailable, 0);
			if ((response.size + bytesAvailable + 1) > response_capacity)
			{
				arena_pop_by(perm, response_capacity);
				response_capacity *= 2;
				auto new_buffer = (uint8_t*)arena_push(perm, response_capacity);
				Assert(response.ptr == new_buffer);
			}
			DWORD bytesRead;
			if (WinHttpReadData(hRequest, response.ptr + response.size, bytesAvailable, &bytesRead)) {
				//response.insert(response.end(), buffer.begin(), buffer.begin() + bytesRead);
				response.size += bytesRead;
			}
			else {
				DWORD error = GetLastError();
				Warning("WinHttpReadData failed with error %lu\n", error);
				return {};
			}
		}
		//response.push_back(0);
		response.ptr[response.size] = 0;

		return response;
	}

};

#if 0
std::string generate_uuid() {
	GUID guid;
	CoCreateGuid(&guid);

	wchar_t guidString[39];
	StringFromGUID2(guid, guidString, sizeof(guidString) / sizeof(wchar_t));

	std::wstring_view wide(guidString);
	std::string result = WideToString(wide);
	result = result.substr(1, result.size() - 2); // Remove braces
	return result;
}
#endif

SQInteger GetServerHeartbeat(HSQUIRRELVM v) {
	SQObject obj;
	SQInteger top = sq_gettop(nullptr, v);
	//Warning("GetServerHeartbeat: Stack top: %d\n", top);
	if (top < 2) {
		Warning("GetServerHeartbeat: Stack has less than 2 elements\n");
		return 1;
	}
	if (SQ_FAILED(sq_getstackobj(nullptr, v, 2, &obj))) {
		Warning("GetServerHeartbeat: Failed to get stack object at position 2\n");
		return 1;
	}

	if (obj._type != OT_TABLE) {
		Warning("GetServerHeartbeat: Object at stack position 2 is not a table, but type %d\n", obj._type);
		return 1;
	}

	auto table = obj._unVal.pTable;
	if (!table) {
		Warning("GetServerHeartbeat: Table is null\n");
		return 1;
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
			if (strcmp_static(key, "host_name") == 0) {
				heartbeat.setHostname(str->_val);
			//	Warning("GetServerHeartbeat:     Set host_name: '%s'\n", str->_val);
			}
			else if (strcmp_static(key, "map_name") == 0) {
				heartbeat.setMapName(str->_val);
			//	Warning("GetServerHeartbeat:     Set map_name: '%s'\n", str->_val);
			}
			else if (strcmp_static(key, "game_mode") == 0) {
				heartbeat.setGameMode(str->_val);
			//	Warning("GetServerHeartbeat:     Set game_mode: '%s'\n", str->_val);
			}
			break;
		}

		case OT_INTEGER:
			if (strcmp_static(key, "max_players") == 0) {
				heartbeat.setMaxPlayers(node->val._unVal.nInteger);
			//	Warning("GetServerHeartbeat:     Set max_players: %d\n", node->val._unVal.nInteger);
			}
			break;

		case OT_ARRAY:
			if (strcmp_static(key, "players") == 0) {
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
							if (strcmp_static(pkey, "name") == 0) {
								player.setName(str->_val);
								//Warning("GetServerHeartbeat:     Set player name: '%s'\n", str->_val);
							}
							break;
						}
						case OT_INTEGER:
							if (strcmp_static(pkey, "gen") == 0) {
								player.setGen(pnode->val._unVal.nInteger);
								//Warning("GetServerHeartbeat:     Set player gen: %d\n", pnode->val._unVal.nInteger);
							}
							else if (strcmp_static(pkey, "lvl") == 0) {
								player.setLvl(pnode->val._unVal.nInteger);
								//Warning("GetServerHeartbeat:     Set player lvl: %d\n", pnode->val._unVal.nInteger);
							}
							else if (strcmp_static(pkey, "team") == 0) {
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
	//std::vector<uint8_t> data(reinterpret_cast<uint8_t*>(serialized.begin()),
	//	reinterpret_cast<uint8_t*>(serialized.end()));

	auto bytes = serialized.asBytes();
	auto ssize = bytes.size();
	//auto asize = AlignPow2(ssize + ARENA_DEFAULT_RESERVE, ARENA_DEFAULT_COMMIT);
	Arena* thread_arena = arena_alloc();
	U8Array data;
	data.ptr = (uint8_t*)arena_push(thread_arena, ssize);
	data.size = ssize;
	memcpy(data.ptr, bytes.begin(), ssize);

	// TODO(mrsteyk): worker thread with ring buffer.
	// Send using WinHTTP
	std::thread([](Arena* thread_arena, U8Array sdata) {
		WinHttpClient client;
		auto ms_url = CCVar_FindVar(cvarinterface, "r1d_ms");
		auto host = StringToWideArena(thread_arena, ms_url->m_Value.m_pszString);

		auto response = client.SendRequest(thread_arena, host, L"/server/heartbeat", sdata, true, true);
		if (!response.empty() && response.size > 2) {
			static bool shown = false;
			bool isFailedResponse = !strcmp_static(response.ptr, "Challenge response failed");

			if (isFailedResponse && !shown) {
				shown = true;
				if (!IsDedicatedServer()) {
					int hostport = OriginalCCVar_FindVar2(cvarinterface, "hostport")->m_Value.m_nValue;
					char cmd[64];
					snprintf(cmd, sizeof(cmd), "script_ui ShowPortForwardWarning(%d)\n", hostport);
					Cbuf_AddText(0, cmd, 0);
				}
				else {
					Warning("GetServerHeartbeat: MS reports: %s\n", reinterpret_cast<char*>(response.ptr));
				}
			}
			else if (!isFailedResponse) {
				Warning("GetServerHeartbeat: MS reports: %s\n", reinterpret_cast<char*>(response.ptr));
			}

		}

		arena_release(thread_arena);
	}, thread_arena, data).detach();

	return 1;
}
SQInteger GetServerList(HSQUIRRELVM v) {
	auto arena = tctx.get_arena_for_scratch();
	auto temp = TempArena(arena);

	try {
		WinHttpClient client;
		auto ms_url = CCVar_FindVar(cvarinterface, "r1d_ms");
		auto host = StringToWideArena(arena, ms_url->m_Value.m_pszString);

		auto response = client.SendRequest(arena, host, L"/server/", {}, false);

		if (!response.empty()) {
			try {
				kj::ArrayPtr<const capnp::word> words(
					reinterpret_cast<const capnp::word*>(response.ptr),
					response.size / sizeof(capnp::word));

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
			catch (const kj::Exception&) {
				// Cap'n Proto parsing error - fall through to create fake server
			}
		}
	}
	catch (const std::exception&) {
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
			auto arena = tctx.get_arena_for_scratch();
			auto temp = TempArena(arena);

			int port = hostport_cvar->m_Value.m_nValue;
			WinHttpClient client;
			auto ms_url = CCVar_FindVar(cvarinterface, "r1d_ms");
			auto host = StringToWideArena(arena, ms_url->m_Value.m_pszString);
			const wchar_t path_prefix[] = L"/server/delete?port=";
			constexpr size_t path_prefix_size = sizeof(path_prefix)/sizeof(path_prefix[0]) - 1;

			size_t path_size = path_prefix_size + 11;
			auto path = (wchar_t*)arena_push(arena, (path_size + 1) * sizeof(wchar_t));
			R1DAssert(swprintf(path, path_size, L"/server/delete?port=%d", port) != -1);

			client.SendRequest(arena, host, path, {}, true, true);
			}).detach();
			host_map_cvar->m_Value.m_StringLength = 0;
			host_map_cvar->m_Value.m_pszString[0] = '\0';
			
	}

		oGameShutDown(thisptr);
}