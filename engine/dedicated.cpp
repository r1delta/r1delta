#include "core.h"
#include "dedicated.h"
#include <intrin.h>
#include <filesystem>
#include <string>
#include "logging.h"
#include "load.h"
#include "dedicated.h"
#include "netchanwarnings.h"
#include "netadr.h"
#include "sv_filter.h"
#include "mcp_server.h"
#include "eos_network.h"
#include "net_hooks.h"
#pragma intrinsic(_ReturnAddress)

// Hook for CSys::ConsoleOutput
typedef void (*ConsoleOutputType)(void* thisptr, char* string);
ConsoleOutputType ConsoleOutputOriginal = nullptr;

void ConsoleOutputHook(void* thisptr, char* string) {
	// Call the original function first
	if (ConsoleOutputOriginal) {
		ConsoleOutputOriginal(thisptr, string);
	}

	// Report the message to MCP server if it's enabled
	if (ShouldEnableMCP() && string && *string) {
		MCPServer::Server::GetInstance().CaptureConsoleOutput(string);
	}
}

#define VTABLE_UPDATE_FORCE 1

#if BUILD_DEBUG

// NOTE(mrsteyk): this is intentionally slow to make you aware of what you are doing.
//                you must atone for your sins.
static uintptr_t
FindVTable(uintptr_t base, const char* name)
{
	auto mz = (PIMAGE_DOS_HEADER)base;
	auto pe = (PIMAGE_NT_HEADERS64)((uint8_t*)base + mz->e_lfanew);
	auto sections_num = pe->FileHeader.NumberOfSections;
	auto sections = IMAGE_FIRST_SECTION(pe);

	uintptr_t data_start = 0;
	uintptr_t data_end = 0;
	uintptr_t rdata_start = 0;
	uintptr_t rdata_end = 0;

	for (size_t i = 0; i < sections_num; i++)
	{
		auto section = sections + i;
		if (strcmp_static(section->Name, ".data") == 0)
		{
			data_start = base + section->VirtualAddress;
			data_end = data_start + section->Misc.VirtualSize;
		}
		else if (strcmp_static(section->Name, ".rdata") == 0)
		{
			rdata_start = base + section->VirtualAddress;
			rdata_end = rdata_start + section->Misc.VirtualSize;
		}

		if (data_start && data_end && rdata_start && rdata_end)
		{
			break;
		}
	}

	if (data_start && data_end && rdata_start && rdata_end)
	{
		auto name_len = strlen(name);
		uint32_t name_rva = 0;

		// NOTE(mrsteyk): strings are usually 16 byte aligned, but this is a part of a struct with 8 align.
		for (size_t pos = data_start; pos < data_end; pos += 8)
		{
			// ".?AV" name "@@";
			if (memcmp((void*)pos, ".?AV", 4))
				continue;
			if (memcmp((void*)(pos + 4), name, name_len))
				continue;
			if (memcmp((void*)(pos + 4 + name_len), "@@", 3))
				continue;

			name_rva = (pos - 0x10) - base;
			break;
		}

		if (name_rva) {
			for (size_t pos = rdata_start; pos < rdata_end; pos += 4)
			{
				if (*(uint32_t*)pos == name_rva)
				{
					if (*(uint32_t*)(pos - 0x8) == 0 && *(uint32_t*)(pos - 0xC) == 1)
					{
						uintptr_t rtti_ref = pos - 0xC;
						for (size_t posr = rdata_start; posr < rdata_end; posr += 8)
						{
							if (*(uintptr_t*)posr == rtti_ref)
							{
								return (posr + 8) - base;
							}
						}
					}
				}
			}
		}
	}

	return 0;
}

static int
FindVTables(uintptr_t engine, uintptr_t engine_ds, vtableRef2Engines* ref) {
	int result = 1;

	auto name = ref->name;
	ref->offset_engine = FindVTable(engine, name);
	ref->offset_engine_ds = FindVTable(engine_ds, name);

	if (!ref->offset_engine || !ref->offset_engine_ds) {
		result = 0;
	}

	return result;
}


void
InitDedicatedVtables() {
	uintptr_t engine = (uintptr_t)LoadLibraryW(L"engine.dll");
	uintptr_t engineDS = (uintptr_t)LoadLibraryW(L"engine_ds.dll");

	OutputDebugStringA("vtableRef2Engines netMessages[] = {\n");

	for (size_t i = 0; i < (sizeof(netMessages) / sizeof(netMessages[0])); i++) {
		auto msg = &netMessages[i];
		static char buf[512];
		if (FindVTables(engine, engineDS, msg)) {
			sprintf_s(buf, "    VTABLEREF2ENGINES(\"%s\", 0x%04llX, 0x%04llX),\n", msg->name, msg->offset_engine, msg->offset_engine_ds);
			OutputDebugStringA(buf);
		}
		else {
			sprintf_s(buf, "    VTABLEREF2ENGINES(\"%s\", 0x%04llX, 0x%04llX), // !!! ERROR !!!\n", msg->name, msg->offset_engine, msg->offset_engine_ds);
			OutputDebugStringA(buf);
		}

		auto dsVtable = engineDS + msg->offset_engine_ds;
		auto vtable = engine + msg->offset_engine;

		if (dsVtable && vtable) {
			for (int i = 0; i < 14; ++i) {
				LPVOID pTarget = ((LPVOID*)dsVtable)[i];
				LPVOID pDetour = ((LPVOID*)vtable)[i];
				MH_CreateHook(pTarget, pDetour, NULL);
			}
		}
	}

	OutputDebugStringA("};\n");
}
#endif


typedef __int64(*NET_OutOfBandPrintf_t)(int, void*, const char*, ...);
NET_OutOfBandPrintf_t Original_NET_OutOfBandPrintf = NULL;
typedef __int64 (*NET_SendPacketType)(
	__int64 a1,
	int a2,
	void* a3,
	_DWORD* a4,
	int a5,
	__int64 a6,
	char a7,
	int a8,
	char a9,
	__int64 a10,
	char a11);
NET_SendPacketType NET_SendPacketOriginal;

__int64 Detour_NET_OutOfBandPrintf(int sock, void* adr, const char* fmt, ...) {
	if (strcmp_static(fmt, "%c00000000000000") == 0) {
		const char** gamemode = reinterpret_cast<const char**>(G_server + 0xB68520);
		const char* mapname = reinterpret_cast<const char*>(G_engine_ds + 0x1C89A84);
		//MessageBoxA(NULL, *gamemode, mapname, 16);
		char msg[1200] = { 0 };
		//Msg("Gamemode %s\n", *gamemode);
		//Msg("Mapname %s\n", mapname);
		bf_write startup(msg, 1200);
		startup.WriteLong(0xFFFFFFFFi64);
		startup.WriteByte(0x4Au);
		startup.WriteString(mapname);
		startup.WriteString(*gamemode);
		return NET_SendPacketOriginal(0, sock, adr, (uint32*)(startup.GetBasePointer()), startup.GetNumBytesWritten(), 0i64, 0, 0, 0, 0i64, 1);
	}

	char buffer[1200];  // Choose appropriate size
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	// Then send the formatted string
	return Original_NET_OutOfBandPrintf(sock, adr, "%s", buffer);
}


using EncodeSpecialFloatType = char(__fastcall*)(void* prop, float value, bf_write* out);
static EncodeSpecialFloatType EncodeSpecialFloatOriginal = nullptr;

using WriteBitCoordType = void(__fastcall*)(bf_write* out, float value, const char* name);
using WriteBitCoordMPType = void(__fastcall*)(bf_write* out, float value, int coordType, const char* name);
using WriteBitCellCoordType = void(__fastcall*)(bf_write* out, float value, int bits, int coordType, const char* name);
using WriteBitFloatType = void(__fastcall*)(bf_write* out, float value, const char* name);
using WriteBitNormalType = void(__fastcall*)(bf_write* out, float value, const char* name);

static WriteBitCoordType WriteBitCoordFn = nullptr;
static WriteBitCoordMPType WriteBitCoordMPFn = nullptr;
static WriteBitCellCoordType WriteBitCellCoordFn = nullptr;
static WriteBitFloatType WriteBitFloatFn = nullptr;
static WriteBitNormalType WriteBitNormalFn = nullptr;

static void InitEncodeSpecialFloatHelpers(uintptr_t engineDS)
{
	WriteBitCellCoordFn = reinterpret_cast<WriteBitCellCoordType>(engineDS + 0x321330);
	WriteBitFloatFn = reinterpret_cast<WriteBitFloatType>(engineDS + 0x321410);
	WriteBitCoordMPFn = reinterpret_cast<WriteBitCoordMPType>(engineDS + 0x322AA0);
	WriteBitCoordFn = reinterpret_cast<WriteBitCoordType>(engineDS + 0x322CF0);
	WriteBitNormalFn = reinterpret_cast<WriteBitNormalType>(engineDS + 0x322FD0);
}

static char __fastcall EncodeSpecialFloat_Hook(void* prop, float value, bf_write* out)
{
	if (!prop || !out) {
		return 0;
	}
	const int flags = *reinterpret_cast<int*>(reinterpret_cast<BYTE*>(prop) + 0x58);
	const int handledFlags = 2 | 0x1000 | 4 | 0x20 | 0x2000 | 0x4000;
	if (flags & handledFlags) {
		return EncodeSpecialFloatOriginal ? EncodeSpecialFloatOriginal(prop, value, out) : 0;
	}
	if (!(flags & 0x8000) || !WriteBitCellCoordFn) {
		return 0;
	}
	const int bits = *reinterpret_cast<int*>(reinterpret_cast<BYTE*>(prop) + 0x14);
	const char* name = *reinterpret_cast<const char**>(reinterpret_cast<BYTE*>(prop) + 0x48);
	WriteBitCellCoordFn(out, value, bits, 2, name);
	return 1;
}

// --- Game Function Pointers ---
// Original function we are hooking
void (*oNET_Config)();

// RCON server object getter
using RconGetterFn = void* (*)();
RconGetterFn RconGetter = nullptr; // Will be initialized

// RCON server address setter
using RconSetAddressFn = void (*)(void* rconserver, const char* adr);
RconSetAddressFn RconSetAddress = nullptr; // Will be initialized

// netadr_t to sockaddr conversion function (TARGETING sub_180488710)
// Assuming it takes netadr_t* and struct sockaddr* (or compatible _WORD*)
using NetAdrToSockadrFn = void (*)(const netadr_t* netadr, struct sockaddr* sockadr);
NetAdrToSockadrFn NetAdrToSockadr = nullptr; // Will be initialized

// --- The Hooked Function ---
// --- The Hooked Function ---
void NET_Config() {
	// Call the original function first if needed
	if (oNET_Config) {
		oNET_Config();
	}

	// Initialize function pointers if not already done
	if (!RconGetter) {
		RconGetter = (RconGetterFn)(G_engine_ds + 0x65B70);
		RconSetAddress = (RconSetAddressFn)(G_engine + 0xF4CA0);
		NetAdrToSockadr = (NetAdrToSockadrFn)(G_engine + 0x488710);
	}

	// 1. Get the RCON server object
	void* rconserver = RconGetter();
	if (!rconserver) {
		Warning("Failed to get RCON server object.\n");
		return;
	}

	// 2. Get the desired listen address
	netadr_t* desired_adr_ptr = (netadr_t*)(G_engine + 0x30EFE40);
	netadr_t desired_adr = *desired_adr_ptr; // Make a working copy
	desired_adr.SetIP((IN6_ADDR*) & in6addr_any);
	// Get hostport CVar
	void* ret = reinterpret_cast<void*>((reinterpret_cast<CreateInterfaceFn>(GetProcAddress(GetModuleHandleA("vstdlib.dll"), "CreateInterface"))("VEngineCvar007", 0)));
	if (!ret) {
		Warning("Failed to get VEngineCvar007 interface.\n");
		return;
	}
	auto v = (decltype(&OriginalCCVar_FindVar)((*(void***)ret))); // Assuming OriginalCCVar_FindVar is defined elsewhere
	if (!v || !v[15]) { // Check if FindVar function pointer is valid
		Warning("Failed to get FindVar function pointer from ICVar.\n");
		return;
	}
	ConVarR1* hostport_cvar = (v[15])((uintptr_t)ret, "hostport"); // ICvar::FindVar is index 15
	if (!hostport_cvar) {
		Warning("Failed to find CVar 'hostport'.\n");
		return;
	}
	int port = hostport_cvar->m_Value.m_nValue;
	desired_adr.SetPort(htons((unsigned short)port)); // Use htons for port

	// *** IMPORTANT: Ensure desired_adr is actually IPv6 if needed ***
	// If the global address isn't guaranteed to be IPv6, you might need to force it:
	// desired_adr.SetType(NA_IPV6); // Or NA_IP if that means IPv6 in your engine
	// desired_adr.SetIP(in6addr_any); // To bind to :: (wildcard)
	// Or parse your specific IP like 'fe80::...' into desired_adr.ip field if needed.
	// For now, assuming the global address + SetPort is sufficient based on your first log.

	Msg("Attempting to configure RCON listen socket for address: %s\n", desired_adr.ToString());

	// 3. Store the desired address in the RCON server object (might still be needed for other parts of RCON)
	RconSetAddress(rconserver, desired_adr.ToString());

	// --- REPLACEMENT for createsocket(rconserver) ---
	Msg("Initiating custom RCON socket creation (IPv6)...\n");

	// Get pointer to where the SOCKET handle is stored in rconserver
	SOCKET* pSocket = (SOCKET*)((uintptr_t)rconserver + 0x28 + 8);

	// Close existing socket if any
	if (*pSocket != INVALID_SOCKET) {
		Msg("Closing existing RCON socket (Handle: %llu)\n", *pSocket);
		closesocket(*pSocket);
		*pSocket = INVALID_SOCKET;
	}

	// Create an IPv6 TCP socket
	SOCKET new_socket = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (new_socket == INVALID_SOCKET) {
		Warning("socket(AF_INET6) failed. Error: %d\n", WSAGetLastError());
		*pSocket = INVALID_SOCKET;
		return;
	}
	Msg("Created new IPv6 socket (Handle: %llu)\n", new_socket);
	*pSocket = new_socket;

	// Configure the socket
	// (Keep your setsockopt and ioctlsocket calls here as they seem okay)
	int optval_int = 1;
	u_long optval_ulong = 1;
	bool config_ok = true;
	// Set TCP_NODELAY
	if (setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&optval_int, sizeof(optval_int)) == SOCKET_ERROR) {
		Warning("setsockopt(TCP_NODELAY) failed. Error: %d\n", WSAGetLastError());
	}
	else { Msg("Set TCP_NODELAY\n"); }
	// Set SO_REUSEADDR
	if (setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval_int, sizeof(optval_int)) == SOCKET_ERROR) {
		Warning("setsockopt(SO_REUSEADDR) failed. Error: %d\n", WSAGetLastError());
		config_ok = false;
	}
	else { Msg("Set SO_REUSEADDR\n"); }
	// Set non-blocking
	if (ioctlsocket(new_socket, FIONBIO, &optval_ulong) == SOCKET_ERROR) {
		Warning("ioctlsocket(FIONBIO) failed. Error: %d\n", WSAGetLastError());
		config_ok = false;
	}
	else { Msg("Set FIONBIO (non-blocking)\n"); }
	// Optional: Set IPV6_V6ONLY = 0 for dual-stack
	int ipv6only = 0; // Set to 0 to disable IPv6 Only mode (enable dual-stack)
	if (setsockopt(new_socket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&ipv6only, sizeof(ipv6only)) == SOCKET_ERROR) {
		Warning("setsockopt(IPV6_V6ONLY=0) failed. Error: %d. Dual-stack might not work.\n", WSAGetLastError());
		// Decide if this is fatal. Often it isn't, but IPv4 connections will fail.
		// config_ok = false; // Optional: make it fatal
	}
	else {
		Msg("Set IPV6_V6ONLY=0 (Dual-stack mode enabled)\n");
	}


	if (!config_ok) {
		Warning("Socket configuration failed. Closing socket.\n");
		closesocket(new_socket);
		*pSocket = INVALID_SOCKET;
		return;
	}

	// Prepare the sockaddr_in6 structure using the game's function
	struct sockaddr_storage name_storage;
	struct sockaddr_in6* name = (struct sockaddr_in6*)&name_storage;
	memset(&name_storage, 0, sizeof(name_storage)); // Good practice to zero it out first

	// *** THE FIX: Use desired_adr directly, not pNetAdr ***
	NetAdrToSockadr(&desired_adr, (struct sockaddr*)name);

	// Verify ToSockadr worked as expected (Check family AND port now)
	Msg("Sockaddr prepared: Family=%d, Port=%d\n", name->sin6_family, ntohs(name->sin6_port));
	if (name->sin6_family != AF_INET6) {
		Warning("NetAdrToSockadr did not set family to AF_INET6 (23)! Got %d instead.\n", name->sin6_family);
		closesocket(new_socket);
		*pSocket = INVALID_SOCKET;
		return;
	}
	// *** ADDED CHECK: If port is still 0, something is wrong with NetAdrToSockadr or desired_adr ***
	if (name->sin6_port == 0) {
		Warning("NetAdrToSockadr resulted in Port 0! Ensure desired_adr is correct and NetAdrToSockadr handles ports.\n");
		// You could try manually setting it again, but it points to a deeper issue:
		// name->sin6_port = desired_adr.GetPort(); // Assuming GetPort returns network byte order
		// OR: name->sin6_port = htons((unsigned short)port);
		closesocket(new_socket); // Probably best to abort if the conversion fails
		*pSocket = INVALID_SOCKET;
		return;
	}


	// Bind the socket to the address
	if (bind(new_socket, (struct sockaddr*)name, sizeof(struct sockaddr_in6)) == SOCKET_ERROR) {
		// Provide more detailed bind error info
		int error_code = WSAGetLastError();
		char adr_str[INET6_ADDRSTRLEN];
		inet_ntop(AF_INET6, &name->sin6_addr, adr_str, INET6_ADDRSTRLEN);
		Warning("bind() to [%s]:%d failed. Error: %d\n", adr_str, ntohs(name->sin6_port), error_code);
		closesocket(new_socket);
		*pSocket = INVALID_SOCKET;
		return;
	}
	Msg("Socket bound successfully.\n");

	// Listen for incoming connections
	if (listen(new_socket, 2) == SOCKET_ERROR) {
		Warning("listen() failed. Error: %d\n", WSAGetLastError());
		closesocket(new_socket);
		*pSocket = INVALID_SOCKET;
		return;
	}

	// *** FIX FINAL LOG MESSAGE: Report the address we *attempted* to bind ***
	Msg("RCON socket successfully created, configured, bound, and listening. Intended address: %s\n", desired_adr.ToString());
	// --- END REPLACEMENT ---
}
bool __fastcall CSocketCreator_CreateListenSocket_Hook(void* this_ptr, netadr_t* pDesiredNetAdr, bool bListenOnAllInterfaces)
{
	// Get pointer to where the SOCKET handle is stored within the object (offset +40)
	SOCKET* pSocketHandle = (SOCKET*)((uintptr_t)this_ptr + 40);

	// Get pointer to where the netadr_t is stored within the object (offset +48)
	netadr_t* pStoredNetAdr = (netadr_t*)((uintptr_t)this_ptr + 48);

	Msg("CSocketCreator_CreateListenSocket_Hook: Called. Desired Adr: %s, ListenOnAll: %s\n",
		pDesiredNetAdr->ToString(), bListenOnAllInterfaces ? "true" : "false");

	// 1. Close existing socket if any (Matches original behavior)
	if (*pSocketHandle != INVALID_SOCKET)
	{
		Msg("Closing existing socket (Handle: %llu)\n", *pSocketHandle);
		closesocket(*pSocketHandle);
		*pSocketHandle = INVALID_SOCKET; // Reset stored handle
	}

	// 2. Store the desired address details in the object (Matches original behavior)
	// Assuming netadr_t size matches the 6 DWORDs copied in assembly (24 bytes)
	// If not, adjust the memcpy size or use member-wise copy if struct is fully known.
	// size_t netadr_size_in_assembly = 24; // 6 * sizeof(DWORD)
	// memcpy(pStoredNetAdr, pDesiredNetAdr, netadr_size_in_assembly);
	// Safer: Use sizeof if you are sure netadr_t matches the original layout
	memcpy(pStoredNetAdr, pDesiredNetAdr, sizeof(netadr_t));
	Msg("Stored desired netadr internally: %s\n", pStoredNetAdr->ToString());


	// 3. Create a new socket (Use AF_INET6 for dual-stack capability)
	SOCKET new_socket = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (new_socket == INVALID_SOCKET)
	{
		Warning("socket(AF_INET6, SOCK_STREAM, TCP) failed. Error: %d\n", WSAGetLastError());
		*pSocketHandle = INVALID_SOCKET; // Ensure handle is invalid on failure
		return false; // Original returns 0 on failure
	}
	Msg("Created new IPv6 socket (Handle: %llu)\n", new_socket);
	*pSocketHandle = new_socket; // Store the new handle immediately

	// 4. Configure the socket (Replicates likely intent of sub_180339700 and adds dual-stack)
	int optval_int = 1;
	u_long optval_ulong = 1;
	bool config_ok = true;

	// Set SO_REUSEADDR (Common for listen sockets)
	if (setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval_int, sizeof(optval_int)) == SOCKET_ERROR) {
		Warning("setsockopt(SO_REUSEADDR) failed. Error: %d\n", WSAGetLastError());
		config_ok = false; // Might still work, but good to have
	}
	else { Msg("Set SO_REUSEADDR\n"); }

	// Set non-blocking (Common for game server sockets)
	if (ioctlsocket(new_socket, FIONBIO, &optval_ulong) == SOCKET_ERROR) {
		Warning("ioctlsocket(FIONBIO) failed. Error: %d\n", WSAGetLastError());
		config_ok = false; // Often critical
	}
	else { Msg("Set FIONBIO (non-blocking)\n"); }

	// Set IPV6_V6ONLY = 0 for dual-stack (Allow IPv4 connections on IPv6 socket)
	// NOTE: This is the key change for broad compatibility compared to the original IPv4-only code
	int ipv6only = 0;
	if (setsockopt(new_socket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&ipv6only, sizeof(ipv6only)) == SOCKET_ERROR) {
		// This might fail on older systems or if IPv6 is not fully configured.
		// It doesn't necessarily mean IPv6 won't work, just that IPv4 mapping might fail.
		Warning("setsockopt(IPV6_V6ONLY=0) failed. Error: %d. Dual-stack might not work (IPv4 connections may fail).\n", WSAGetLastError());
		// Decide if this is fatal. If dual-stack is essential, set config_ok = false;
		// config_ok = false; // Optional: Make it fatal if dual-stack MUST work
	}
	else {
		Msg("Set IPV6_V6ONLY=0 (Dual-stack mode enabled)\n");
	}

	// Add other options if known/needed (e.g., TCP_NODELAY, though less common for listen sockets)
	// if (setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&optval_int, sizeof(optval_int)) == SOCKET_ERROR) {
	//     Warning("setsockopt(TCP_NODELAY) failed. Error: %d\n", WSAGetLastError());
	// } else { Msg("Set TCP_NODELAY\n"); }


	if (!config_ok) {
		Warning("Socket configuration failed. Closing socket.\n");
		closesocket(new_socket);
		*pSocketHandle = INVALID_SOCKET;
		return false; // Original returns 0 on failure
	}


	// 5. Prepare the bind address (Replicates sub_180327090 and a3 check)
	struct sockaddr_storage bind_addr_storage;
	memset(&bind_addr_storage, 0, sizeof(bind_addr_storage));
	struct sockaddr_in6* bind_addr = (struct sockaddr_in6*)&bind_addr_storage; // Treat as IPv6 initially

	// Use the NetAdr stored in the object (like the original code likely did via sub_180327090)
	// Or use the input pDesiredNetAdr directly if sub_180327090 just did conversion.
	// Let's use pDesiredNetAdr for simplicity, assuming the stored copy is just informational.
	NetAdrToSockadr(pDesiredNetAdr, (struct sockaddr*)bind_addr);

	// Check if conversion was successful (implicitly checks if NetAdrToSockadr set the family)
	if (bind_addr->sin6_family != AF_INET6)
	{
		// It's possible NetAdrToSockadr created an AF_INET address if pDesiredNetAdr was IPv4.
		// However, since we created an AF_INET6 socket, we MUST bind with an AF_INET6 address.
		// If pDesiredNetAdr was IPv4, we need to use the IPv4-mapped IPv6 address.
		// If NetAdrToSockadr doesn't handle this, we might need manual conversion here,
		// OR ensure pDesiredNetAdr is *always* represented as IPv6 (e.g., ::ffff:x.x.x.x).
		// For now, assume NetAdrToSockadr gives AF_INET6 or handle the specific IPv4 case.

		if (((struct sockaddr*)bind_addr)->sa_family == AF_INET) {
			Warning("NetAdrToSockadr produced AF_INET. Attempting to use IPv4-mapped IPv6 address for binding to AF_INET6 socket.\n");
			struct sockaddr_in* ipv4_addr = (struct sockaddr_in*)bind_addr;
			IN_ADDR v4_ip = ipv4_addr->sin_addr;
			uint16_t v4_port = ipv4_addr->sin_port;

			memset(&bind_addr_storage, 0, sizeof(bind_addr_storage)); // Clear again
			bind_addr->sin6_family = AF_INET6;
			bind_addr->sin6_port = v4_port;
			// Create the mapped address ::ffff:a.b.c.d
			bind_addr->sin6_addr.s6_addr[10] = 0xff;
			bind_addr->sin6_addr.s6_addr[11] = 0xff;
			memcpy(&bind_addr->sin6_addr.s6_addr[12], &v4_ip, sizeof(v4_ip));

			char v4_ip_str[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &v4_ip, v4_ip_str, sizeof(v4_ip_str));
			Msg("Converted IPv4 %s:%u to mapped IPv6 for binding.\n", v4_ip_str, ntohs(v4_port));

		}
		else {
			// If it's not AF_INET or AF_INET6, something is wrong.
			Warning("NetAdrToSockadr did not set family to AF_INET or AF_INET6! Got %d. Cannot bind.\n", ((struct sockaddr*)bind_addr)->sa_family);
			closesocket(new_socket);
			*pSocketHandle = INVALID_SOCKET;
			return false;
		}
	}

	// Handle the 'listen on all interfaces' flag (a3 in assembly)
	// This overrides the IP address part, keeping the port from pDesiredNetAdr.
	if (bListenOnAllInterfaces)
	{
		Msg("ListenOnAllInterfaces is true, binding to IPv6 wildcard '::'.\n");
		bind_addr->sin6_addr = in6addr_any; // Set IP to :: (0.0.0.0 equivalent for IPv6)
	}

	// Verify port is set (important sanity check)
	if (bind_addr->sin6_port == 0) {
		Warning("Bind address port is 0! Ensure pDesiredNetAdr had a valid port and NetAdrToSockadr preserved it.\n");
		closesocket(new_socket);
		*pSocketHandle = INVALID_SOCKET;
		return false;
	}

	// 6. Bind the socket (Matches original behavior, but with IPv6 struct)
	char bound_ip_str[INET6_ADDRSTRLEN];
	inet_ntop(AF_INET6, &bind_addr->sin6_addr, bound_ip_str, sizeof(bound_ip_str));
	uint16_t bound_port = ntohs(bind_addr->sin6_port);
	Msg("Attempting to bind socket %llu to [%s]:%u...\n", new_socket, bound_ip_str, bound_port);

	if (bind(new_socket, (struct sockaddr*)bind_addr, sizeof(sockaddr_in6)) == SOCKET_ERROR)
	{
		int error_code = WSAGetLastError();
		Warning("bind() to [%s]:%u failed. Error: %d\n", bound_ip_str, bound_port, error_code);
		closesocket(new_socket);
		*pSocketHandle = INVALID_SOCKET;
		return false; // Original returns 0 on failure
	}
	Msg("Socket bound successfully.\n");

	// 7. Listen for incoming connections (Matches original behavior)
	int backlog = 2; // Same backlog as original assembly
	Msg("Attempting to listen with backlog %d...\n", backlog);
	if (listen(new_socket, backlog) == SOCKET_ERROR)
	{
		Warning("listen() failed. Error: %d\n", WSAGetLastError());
		closesocket(new_socket);
		*pSocketHandle = INVALID_SOCKET;
		return false; // Original returns 0 on failure
	}

	Msg("CSocketCreator_CreateListenSocket_Hook: Socket successfully created, configured, bound, and listening on [%s]:%u\n", bound_ip_str, bound_port);
	return true; // Original returns 1 on success
}

// Renamed from sub_18006C680_t
typedef void(__fastcall* CServerRemoteAccess_WriteDataRequest_t)(__int64 a1_this, __int64 pNetworkListener, int listener, __int64 data_buffer, unsigned int data_size);
typedef int(__fastcall* CUtlBuffer_GetInt_t)(__int64 buffer_struct); // Alias for sub_180067480
typedef void(__fastcall* GetStringHelper_t)(__int64 buffer_struct, _BYTE* dest_buffer, __int64 max_len);
typedef void(__fastcall* CServerRemoteAccess_CheckPassword_t)(__int64 this_ptr, __int64 pNetworkListener, int listener, int requestID, const char* password);

// Pointers to the original functions
// Renamed from original_sub_18006C680
CServerRemoteAccess_WriteDataRequest_t original_CServerRemoteAccess_WriteDataRequest = nullptr;
CUtlBuffer_GetInt_t original_CUtlBuffer_GetInt = nullptr;

// --- Global variables for state sharing ---
bool     g_inside_WriteDataRequest = false; // Renamed flag
int      g_WriteDataRequest_getint_call_count = 0; // Renamed counter
uint64_t g_WriteDataRequest_this = 0; // Renamed context variable
uint64_t g_WriteDataRequest_listener_ptr = 0; // Renamed context variable
int      g_WriteDataRequest_listener_idx = 0; // Renamed context variable
int      g_WriteDataRequest_request_id = 0; // Renamed context variable
uint64_t g_WriteDataRequest_buffer_ptr = 0; // Renamed context variable (Pointer to the CUtlBuffer struct)

// --- Hook Functions ---

// Renamed from hook_sub_18006C680
void __fastcall hook_CServerRemoteAccess_WriteDataRequest(__int64 a1_this, __int64 pNetworkListener, int listener, __int64 data_buffer, unsigned int data_size)
{
	// Basic re-entrancy check
	if (g_inside_WriteDataRequest) {
		if (original_CServerRemoteAccess_WriteDataRequest) {
			original_CServerRemoteAccess_WriteDataRequest(a1_this, pNetworkListener, listener, data_buffer, data_size);
		}
		return;
	}

	// Set flag and store context
	g_inside_WriteDataRequest = true;
	g_WriteDataRequest_this = a1_this;
	g_WriteDataRequest_listener_ptr = pNetworkListener;
	g_WriteDataRequest_listener_idx = listener;
	g_WriteDataRequest_getint_call_count = 0;
	g_WriteDataRequest_buffer_ptr = 0; // Reset buffer pointer for this handler invocation
	g_WriteDataRequest_request_id = 0; // Reset request ID

	// Call the original function
	if (original_CServerRemoteAccess_WriteDataRequest) {
		original_CServerRemoteAccess_WriteDataRequest(a1_this, pNetworkListener, listener, data_buffer, data_size);
	}

	// Clear flag after original function returns
	g_inside_WriteDataRequest = false;
}

// No name change needed here, but internal logic uses renamed globals
int __fastcall hook_CUtlBuffer_GetInt(__int64 buffer_struct_ptr) // a1 is the buffer struct pointer
{
	// If not inside the WriteDataRequest handler bypass hook logic
	if (!g_inside_WriteDataRequest) {
		return original_CUtlBuffer_GetInt(buffer_struct_ptr);
	}

	// Capture the buffer pointer on the first GetInt call within the handler
	if (g_WriteDataRequest_buffer_ptr == 0) {
		g_WriteDataRequest_buffer_ptr = buffer_struct_ptr;
	}

	// Ensure this GetInt call is operating on the buffer we expect for this handler invocation
	if (buffer_struct_ptr != g_WriteDataRequest_buffer_ptr) {
		// Calling GetInt on a different buffer? Unexpected, bypass.
		return original_CUtlBuffer_GetInt(buffer_struct_ptr);
	}

	// Increment call count for the relevant buffer
	g_WriteDataRequest_getint_call_count++;

	// Call the original function to get the actual integer
	int original_result = original_CUtlBuffer_GetInt(buffer_struct_ptr);

	// Check buffer error state *after* original call, as it might set flags
	bool buffer_error = (*(_BYTE*)(buffer_struct_ptr + 40) != 0); // Check errorFlags (offset 0x28 / 40)

	if (buffer_error && g_WriteDataRequest_getint_call_count <= 2) {
		// If an error occurred reading the request ID or command type,
		// don't proceed with our logic. Return the error value (or 0).
		return original_result;
	}

	// --- Logic based on call count ---

	if (g_WriteDataRequest_getint_call_count == 1) {
		// First call: Capture Request ID
		g_WriteDataRequest_request_id = original_result;
		return original_result; // Return the actual request ID
	}
	else if (g_WriteDataRequest_getint_call_count == 2) {
		// Second call: Capture Command Type
		int commandType = original_result;

		if (commandType == 3) {
			// It's the password command!
			// printf("Hook detected command type 3!\n"); // Debug

			char password[512];
			password[0] = '\0';
			// Pointers to functions we need to call
			GetStringHelper_t GetStringHelper = (GetStringHelper_t)(G_engine_ds + 0x325b00);

			// Use the captured buffer pointer to get the string
			GetStringHelper(g_WriteDataRequest_buffer_ptr, (_BYTE*)password, 512);

			// Check for errors *after* GetStringHelper
			bool getString_error = (*(_BYTE*)(g_WriteDataRequest_buffer_ptr + 40) != 0);

			if (!getString_error) {
				static CServerRemoteAccess_CheckPassword_t CServerRemoteAccess_CheckPassword = (CServerRemoteAccess_CheckPassword_t)(G_engine_ds + 0x6CAC0);	
				CServerRemoteAccess_CheckPassword(g_WriteDataRequest_this, g_WriteDataRequest_listener_ptr, g_WriteDataRequest_listener_idx, g_WriteDataRequest_request_id, password);
				GetStringHelper(g_WriteDataRequest_buffer_ptr, (_BYTE*)password, 512); // handle csgo-like userid
			}
			else {
				// printf("GetStringHelper failed for command 3.\n");
			}

			// *** Return a different command type (e.g., 6) ***
			// This prevents the original WriteDataRequest from executing case 3.
			return 6;
		}
		else {
			// Command type is not 3, return the original value
			return commandType;
		}
	}
	else {
		// Subsequent calls (if any) - just return the original value
		return original_result;
	}
}
char (*oCServerRemoteAccess__LookupStringValue__6B490)(__int64 a1, unsigned __int8* a2, __int64 a3);

static bool CopyDedicatedCommandLineValue(
	const char* token,
	char* output,
	size_t outputSize)
{
	if (!token || !*token || !output || outputSize == 0)
		return false;

	output[0] = '\0';
	const char* commandLine = GetCommandLineA();
	if (!commandLine)
		return false;

	const size_t tokenLength = strlen(token);
	for (const char* scan = commandLine; *scan; ++scan) {
		if (scan != commandLine && scan[-1] != ' ' && scan[-1] != '\t')
			continue;
		if (_strnicmp(scan, token, tokenLength) != 0)
			continue;
		if (scan[tokenLength] != ' ' && scan[tokenLength] != '\t')
			continue;

		const char* value = scan + tokenLength;
		while (*value == ' ' || *value == '\t')
			++value;
		if (!*value)
			return false;

		const char terminator = *value == '"' ? '"' : '\0';
		if (terminator)
			++value;

		size_t copied = 0;
		while (value[copied]
			&& (terminator
				? value[copied] != terminator
				: value[copied] != ' ' && value[copied] != '\t')
			&& copied + 1 < outputSize) {
			output[copied] = value[copied];
			++copied;
		}
		output[copied] = '\0';
		return copied != 0;
	}

	return false;
}

static uintptr_t GetDedicatedEngineModule();

static const char* GetDedicatedCurrentPlaylist()
{
	if (IsR1ODedicatedServer()) {
		const uintptr_t engineBase = GetDedicatedEngineModule();
		if (engineBase) {
			// TFO Playlist_SetPlaylist stores the selected playlist name in this
			// fixed 256-byte buffer.  "launchplaylist" is a command rather than
			// a ConVar in engine_r1o, so the ICvar lookup below cannot recover it.
			const char* currentPlaylist =
				reinterpret_cast<const char*>(engineBase + 0x2CC4A00);
			if (*currentPlaylist)
				return currentPlaylist;
		}

		if (cvarinterface) {
			for (const char* cvarName : { "playlist", "launchplaylist" }) {
				auto* playlist = OriginalCCVar_FindVar(cvarinterface, cvarName);
				if (playlist
					&& playlist->m_Value.m_pszString
					&& *playlist->m_Value.m_pszString) {
					return playlist->m_Value.m_pszString;
				}
			}
		}

		static char commandLinePlaylist[128];
		return CopyDedicatedCommandLineValue(
			"+launchplaylist",
			commandLinePlaylist,
			sizeof(commandLinePlaylist))
			? commandLinePlaylist
			: nullptr;
	}

	return G_engine_ds
		? reinterpret_cast<char* (*)()>(G_engine_ds + 0xB8C40)()
		: nullptr;
}

static uintptr_t GetDedicatedEngineModule()
{
	if (IsR1ODedicatedServer())
		return G_engine_r1o
			? G_engine_r1o
			: reinterpret_cast<uintptr_t>(GetModuleHandleA("engine_r1o.dll"));

	return G_engine_ds
		? G_engine_ds
		: reinterpret_cast<uintptr_t>(GetModuleHandleA("engine_ds.dll"));
}

static const char* GetDedicatedCurrentMap()
{
	if (cvarinterface) {
		auto* map = OriginalCCVar_FindVar(cvarinterface, "host_map");
		if (map && map->m_Value.m_pszString && *map->m_Value.m_pszString)
			return map->m_Value.m_pszString;
	}

	static char commandLineMap[128];
	return CopyDedicatedCommandLineValue(
		"+map",
		commandLineMap,
		sizeof(commandLineMap))
		? commandLineMap
		: nullptr;
}

static int __fastcall R1OGetPlaylistMaxPlayers();

char CServerRemoteAccess__LookupStringValue__6B490(__int64 a1, unsigned __int8* a2, __int64 a3) {
	if (strcmp_static(a2, "launchplaylist") == 0) {
		const uintptr_t engineBase = GetDedicatedEngineModule();
		const char* playlist = GetDedicatedCurrentPlaylist();
		if (!engineBase || !playlist)
			return false;

		const uintptr_t setOutputStringRva = IsR1ODedicatedServer()
			? 0x27AD00
			: 0x3268F0;
		reinterpret_cast<__int64(*)(__int64, const char*)>(
			engineBase + setOutputStringRva)(a3, playlist);
		return true;
	}
	if (IsR1ODedicatedServer() && strcmp_static(a2, "maxplayers") == 0) {
		const uintptr_t engineBase = GetDedicatedEngineModule();
		const __int64 selectedPlaylist = engineBase
			? *reinterpret_cast<const __int64*>(engineBase + 0x2CC48F0)
			: 0;
		if (engineBase && selectedPlaylist) {
			char maxPlayers[16];
			_snprintf_s(
				maxPlayers,
				sizeof(maxPlayers),
				_TRUNCATE,
				"%d",
				R1OGetPlaylistMaxPlayers());
			reinterpret_cast<__int64(*)(__int64, const char*)>(
				engineBase + 0x27AD00)(a3, maxPlayers);
			return true;
		}
	}
	return oCServerRemoteAccess__LookupStringValue__6B490
		? oCServerRemoteAccess__LookupStringValue__6B490(a1, a2, a3)
		: false;
}

using R1OCreateListenSocketFn = bool(__fastcall*)(void*, const void*);
static R1OCreateListenSocketFn R1OCreateListenSocketOriginal;

using R1OGetPlaylistMaxPlayersFn = int(__fastcall*)();
static R1OGetPlaylistMaxPlayersFn R1OGetPlaylistMaxPlayersOriginal;
static uintptr_t s_R1OPlaylistEngineBase;

static int __fastcall R1OGetPlaylistMaxPlayers()
{
	const int nativeResult = R1OGetPlaylistMaxPlayersOriginal
		? R1OGetPlaylistMaxPlayersOriginal()
		: 1;
	const uintptr_t engineBase = s_R1OPlaylistEngineBase
		? s_R1OPlaylistEngineBase
		: GetDedicatedEngineModule();
	if (!engineBase)
		return nativeResult;

	const __int64 selectedPlaylist =
		*reinterpret_cast<const __int64*>(engineBase + 0x2CC48F0);
	if (!selectedPlaylist)
		return nativeResult;

	using FindKeyFn = __int64(__fastcall*)(__int64, const char*, int);
	using GetStringFn = const char* (__fastcall*)(__int64, const char*, const char*);
	using ParseIntFn = int(__fastcall*)(const char*);
	const auto findKey = reinterpret_cast<FindKeyFn>(engineBase + 0x2714F0);
	const auto getString = reinterpret_cast<GetStringFn>(engineBase + 0x271AD0);
	const auto parseInt = reinterpret_cast<ParseIntFn>(engineBase + 0x46BBC8);

	const __int64 variables = findKey(selectedPlaylist, "vars", 0);
	if (!variables)
		return nativeResult;

	// Keep the native TFO schema authoritative. R1's playlists use the older
	// spaced spelling, so only consult it when the TFO key is absent.
	if (findKey(variables, "max_players", 0))
		return nativeResult;

	const __int64 legacyMaxPlayers = findKey(variables, "max players", 0);
	if (!legacyMaxPlayers)
		return nativeResult;

	const char* value = getString(legacyMaxPlayers, nullptr, "");
	if (!value || !*value)
		return nativeResult;

	const int parsed = parseInt(value);
	return parsed > 0 ? parsed : nativeResult;
}

void ApplyR1OPlaylistAfterServerAutorun(uintptr_t engineBase)
{
	if (!IsR1ODedicatedServer() || !engineBase)
		return;

	const char* currentPlaylist = GetDedicatedCurrentPlaylist();
	if (!currentPlaylist || !*currentPlaylist)
		return;

	// launchplaylist is consumed while the R1O server is still being built.
	// Playlist_SetPlaylist records the selection then, but its live-server
	// max-client update is gated on server state >= active. Reapply the same
	// selection after the per-map server VM and autorun pass exist; this is the
	// same native path used by a later `playlist <name>` console command.
	char playlistName[256];
	strncpy_s(playlistName, currentPlaylist, _TRUNCATE);
	using SetPlaylistFn = void(__fastcall*)(const char*);
	reinterpret_cast<SetPlaylistFn>(engineBase + 0x1A22D0)(playlistName);

	if (AreR1OFakeDediVerboseLogsEnabled()) {
		const __int64 server =
			*reinterpret_cast<const __int64*>(engineBase + 0x2659550);
		const int maxPlayers = server
			? *reinterpret_cast<const int*>(server + 460)
			: 0;
		Msg(
			"R1Delta: reapplied R1O playlist after server autorun "
			"name=%s maxplayers=%d\n",
			playlistName,
			maxPlayers);
	}
}

void InstallR1OPlaylistCompatibilityHooks(uintptr_t engineBase)
{
	if (!IsR1ODedicatedServer() || !engineBase)
		return;

	static uintptr_t installedTarget;
	const uintptr_t target = engineBase + 0x1A2900;
	if (installedTarget == target)
		return;

	const MH_STATUS createStatus = MH_CreateHook(
		reinterpret_cast<LPVOID>(target),
		&R1OGetPlaylistMaxPlayers,
		reinterpret_cast<LPVOID*>(&R1OGetPlaylistMaxPlayersOriginal));
	if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED) {
		Warning(
			"R1Delta: failed to create R1O playlist max-player compatibility hook (%d)\n",
			static_cast<int>(createStatus));
		return;
	}

	const MH_STATUS enableStatus =
		MH_EnableHook(reinterpret_cast<LPVOID>(target));
	if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
		Warning(
			"R1Delta: failed to enable R1O playlist max-player compatibility hook (%d)\n",
			static_cast<int>(enableStatus));
		return;
	}

	s_R1OPlaylistEngineBase = engineBase;
	installedTarget = target;
}

static bool __fastcall R1OCreateListenSocket(void* socketCreator, const void* desiredAddress)
{
	if (!socketCreator || !desiredAddress)
		return false;

	auto* const listenSocket = reinterpret_cast<SOCKET*>(
		reinterpret_cast<uintptr_t>(socketCreator) + 40);
	auto* const storedAddress = reinterpret_cast<unsigned char*>(
		reinterpret_cast<uintptr_t>(socketCreator) + 48);

	if (*listenSocket != 0 && *listenSocket != INVALID_SOCKET) {
		closesocket(*listenSocket);
		*listenSocket = INVALID_SOCKET;
	}
	memcpy(storedAddress, desiredAddress, 24);

	const SOCKET socketHandle = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (socketHandle == INVALID_SOCKET)
		return false;
	*listenSocket = socketHandle;

	int enabled = 1;
	u_long nonBlocking = 1;
	int ipv6Only = 0;
	if (setsockopt(
			socketHandle,
			SOL_SOCKET,
			SO_REUSEADDR,
			reinterpret_cast<const char*>(&enabled),
			sizeof(enabled)) == SOCKET_ERROR
		|| ioctlsocket(socketHandle, FIONBIO, &nonBlocking) == SOCKET_ERROR
		|| setsockopt(
			socketHandle,
			IPPROTO_IPV6,
			IPV6_V6ONLY,
			reinterpret_cast<const char*>(&ipv6Only),
			sizeof(ipv6Only)) == SOCKET_ERROR) {
		const int error = WSAGetLastError();
		closesocket(socketHandle);
		*listenSocket = INVALID_SOCKET;
		WSASetLastError(error);
		return false;
	}

	sockaddr_in6 bindAddress{};
	bindAddress.sin6_family = AF_INET6;
	bindAddress.sin6_port = *reinterpret_cast<const unsigned short*>(
		reinterpret_cast<const unsigned char*>(desiredAddress) + 20);
	bindAddress.sin6_addr = in6addr_any;
	if (!bindAddress.sin6_port
		|| bind(
			socketHandle,
			reinterpret_cast<const sockaddr*>(&bindAddress),
			sizeof(bindAddress)) == SOCKET_ERROR
		|| listen(socketHandle, 2) == SOCKET_ERROR) {
		const int error = WSAGetLastError();
		closesocket(socketHandle);
		*listenSocket = INVALID_SOCKET;
		WSASetLastError(error);
		return false;
	}

	if (AreR1OFakeDediVerboseLogsEnabled()) {
		char buffer[192];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O remote TCP listener bound port=%u socket=%llu\n",
			static_cast<unsigned int>(ntohs(bindAddress.sin6_port)),
			static_cast<unsigned long long>(socketHandle));
		OutputDebugStringA(buffer);
	}
	return true;
}

bool EnsureR1ORconListenerForPort(uintptr_t engineBase, int port)
{
	if (!IsR1ODedicatedServer()
		|| !engineBase
		|| port <= 0
		|| port > 0xFFFF) {
		return false;
	}

	const uintptr_t rconServer = engineBase + 0x6D46D0;
	auto* const listenSocket =
		reinterpret_cast<SOCKET*>(rconServer + 48);
	int existingSocketType = 0;
	int existingBoundPort = 0;
	bool existingListener = false;
	if (*listenSocket != 0 && *listenSocket != INVALID_SOCKET) {
		const SOCKET nativeSocket = *listenSocket;
		int socketTypeLength = sizeof(existingSocketType);
		sockaddr_storage boundAddress{};
		int boundAddressLength = sizeof(boundAddress);
		if (getsockopt(
				nativeSocket,
				SOL_SOCKET,
				SO_TYPE,
				reinterpret_cast<char*>(&existingSocketType),
				&socketTypeLength) == 0
			&& existingSocketType == SOCK_STREAM
			&& getsockname(
				nativeSocket,
				reinterpret_cast<sockaddr*>(&boundAddress),
				&boundAddressLength) == 0) {
			if (boundAddress.ss_family == AF_INET6) {
				existingBoundPort = ntohs(
					reinterpret_cast<const sockaddr_in6*>(&boundAddress)->sin6_port);
			}
			else if (boundAddress.ss_family == AF_INET) {
				existingBoundPort = ntohs(
					reinterpret_cast<const sockaddr_in*>(&boundAddress)->sin_port);
			}
			existingListener = existingBoundPort > 0;
		}
	}
	if (existingListener && existingBoundPort == port)
		return true;

	unsigned char desiredAddress[24]{};
	*reinterpret_cast<unsigned short*>(desiredAddress + 20) =
		htons(static_cast<unsigned short>(port));
	return R1OCreateListenSocket(
		reinterpret_cast<void*>(rconServer + 8),
		desiredAddress);
}

void InstallR1ORemoteAccessHooks(uintptr_t engineBase)
{
	if (!IsR1ODedicatedServer() || !engineBase)
		return;

	using R1OWriteDataRequestFn =
		void(__fastcall*)(__int64, __int64, int, void*, signed int);
	using R1OReadIntFn = void(__fastcall*)(__int64, int*);
	using R1OReadStringFn = void(__fastcall*)(__int64, unsigned char*, __int64);
	using R1OAllocateReplyFn = int(__fastcall*)(__int64);
	using R1OWriteIntFn = __int64(__fastcall*)(__int64, int);
	using R1OWriteFormattedFn = __int64(__fastcall*)(__int64, const char*, ...);
	using R1OWriteStringFn = __int64(__fastcall*)(__int64, const char*);

	struct R1ORemoteAccessRequestContext {
		bool active = false;
		__int64 remoteAccess = 0;
		__int64 networkListener = 0;
		int listener = -1;
		int requestId = 0;
		int readIntCalls = 0;
		__int64 inputBuffer = 0;
	};

	static R1OWriteDataRequestFn originalWriteDataRequest = nullptr;
	static R1OReadIntFn originalReadInt = nullptr;
	static thread_local R1ORemoteAccessRequestContext requestContext;

	static const auto writePasswordReply =
		[](uintptr_t moduleBase,
			const R1ORemoteAccessRequestContext& context,
			bool authenticated) {
			if (!moduleBase || !context.remoteAccess || context.listener < 0)
				return false;

			const __int64 listenerStateArray =
				*reinterpret_cast<__int64*>(context.remoteAccess + 64);
			if (!listenerStateArray)
				return false;

			*reinterpret_cast<unsigned char*>(
				listenerStateArray + 40LL * context.listener + 4) =
				authenticated ? 1 : 0;

			auto allocateReply = reinterpret_cast<R1OAllocateReplyFn>(
				moduleBase + 0x15BBC0);
			const int replyIndex = allocateReply(context.remoteAccess + 8);
			if (replyIndex < 0)
				return false;

			const __int64 replyArray =
				*reinterpret_cast<__int64*>(context.remoteAccess + 8);
			if (!replyArray)
				return false;

			const __int64 reply = replyArray + 104LL * replyIndex;
			*reinterpret_cast<int*>(reply + 88) = context.listener;

			auto writeInt = reinterpret_cast<R1OWriteIntFn>(
				moduleBase + 0x0D00A0);
			auto writeFormatted = reinterpret_cast<R1OWriteFormattedFn>(
				moduleBase + 0x27AFF0);
			auto writeString = reinterpret_cast<R1OWriteStringFn>(
				moduleBase + 0x27AD00);
			const auto appendInt = [reply, writeInt, writeFormatted](int value) {
				if ((*reinterpret_cast<unsigned char*>(reply + 41) & 1) != 0)
					writeFormatted(reply, "%d", value);
				else
					writeInt(reply, value);
			};

			// This is the stock CServerRemoteAccess password reply:
			// request id (or -1), response type 2, then two empty strings.
			appendInt(authenticated ? context.requestId : -1);
			appendInt(2);
			writeString(reply, "");
			writeString(reply, "");
			return true;
		};

	static const auto readIntHook =
		[](const __int64 buffer, int* value) {
			originalReadInt(buffer, value);
			if (!value
				|| !requestContext.active
				|| requestContext.readIntCalls >= 2) {
				return;
			}

			if (!requestContext.inputBuffer)
				requestContext.inputBuffer = buffer;
			if (requestContext.inputBuffer != buffer)
				return;

			++requestContext.readIntCalls;
			if (*reinterpret_cast<unsigned char*>(buffer + 40) != 0)
				return;

			if (requestContext.readIntCalls == 1) {
				requestContext.requestId = *value;
				return;
			}
			if (*value != 3)
				return;

			char suppliedPassword[512]{};
			auto readString = reinterpret_cast<R1OReadStringFn>(
				G_engine_r1o + 0x279D60);
			readString(
				buffer,
				reinterpret_cast<unsigned char*>(suppliedPassword),
				sizeof(suppliedPassword));
			if (*reinterpret_cast<unsigned char*>(buffer + 40) != 0)
				return;

			const char* expectedPassword = "";
			if (cvarinterface && OriginalCCVar_FindVar) {
				if (auto* password =
					OriginalCCVar_FindVar(cvarinterface, "rcon_password")) {
					if (password->m_Value.m_pszString)
						expectedPassword = password->m_Value.m_pszString;
				}
			}

			const bool authenticated =
				strcmp(suppliedPassword, expectedPassword) == 0;
			const bool replyQueued = writePasswordReply(
					G_engine_r1o,
					requestContext,
					authenticated);
			if (!replyQueued) {
				Warning(
					"R1Delta: failed to queue R1O remote-access password reply\n");
			}
			if (AreR1OFakeDediVerboseLogsEnabled()) {
				Msg(
					"R1Delta: R1O remote-access auth listener=%d request=%d "
					"authenticated=%d replyQueued=%d\n",
					requestContext.listener,
					requestContext.requestId,
					authenticated ? 1 : 0,
					replyQueued ? 1 : 0);
			}

			// Source/CS:GO-compatible clients append an optional user-id
			// string to the PASS request. Consume it before returning a benign
			// command type so the stripped TFO switch never sees case 3.
			char userId[512]{};
			readString(
				buffer,
				reinterpret_cast<unsigned char*>(userId),
				sizeof(userId));
			*value = 6;
		};

	static const auto writeDataRequestHook =
		[](__int64 remoteAccess,
			__int64 networkListener,
			int listener,
			void* data,
			signed int dataSize) {
			if (requestContext.active) {
				originalWriteDataRequest(
					remoteAccess,
					networkListener,
					listener,
					data,
					dataSize);
				return;
			}

			const R1ORemoteAccessRequestContext previous = requestContext;
			requestContext.active = true;
			requestContext.remoteAccess = remoteAccess;
			requestContext.networkListener = networkListener;
			requestContext.listener = listener;
			requestContext.requestId = 0;
			requestContext.readIntCalls = 0;
			requestContext.inputBuffer = 0;
			if (AreR1OFakeDediVerboseLogsEnabled()) {
				Msg(
					"R1Delta: R1O remote-access request listener=%d bytes=%d\n",
					listener,
					dataSize);
			}

			originalWriteDataRequest(
				remoteAccess,
				networkListener,
				listener,
				data,
				dataSize);
			requestContext = previous;
		};

	static uintptr_t installedTarget = 0;
	static uintptr_t installedListenSocketTarget = 0;
	const uintptr_t listenSocketTarget = engineBase + 0x28B100;
	if (installedListenSocketTarget != listenSocketTarget) {
		const MH_STATUS listenCreateStatus = MH_CreateHook(
			reinterpret_cast<LPVOID>(listenSocketTarget),
			&R1OCreateListenSocket,
			reinterpret_cast<LPVOID*>(&R1OCreateListenSocketOriginal));
		if (listenCreateStatus != MH_OK
			&& listenCreateStatus != MH_ERROR_ALREADY_CREATED) {
			Warning(
				"R1Delta: failed to create R1O remote TCP listener hook (%d)\n",
				static_cast<int>(listenCreateStatus));
			return;
		}

		const MH_STATUS listenEnableStatus =
			MH_EnableHook(reinterpret_cast<LPVOID>(listenSocketTarget));
		if (listenEnableStatus != MH_OK
			&& listenEnableStatus != MH_ERROR_ENABLED) {
			Warning(
				"R1Delta: failed to enable R1O remote TCP listener hook (%d)\n",
				static_cast<int>(listenEnableStatus));
			return;
		}
		installedListenSocketTarget = listenSocketTarget;
	}

	const uintptr_t lookupTarget = engineBase + 0x15AFE0;
	if (installedTarget == lookupTarget)
		return;

	const MH_STATUS createStatus = MH_CreateHook(
		reinterpret_cast<LPVOID>(lookupTarget),
		&CServerRemoteAccess__LookupStringValue__6B490,
		reinterpret_cast<LPVOID*>(&oCServerRemoteAccess__LookupStringValue__6B490));
	if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED) {
		Warning("R1Delta: failed to create R1O remote-access lookup hook (%d)\n",
			static_cast<int>(createStatus));
		return;
	}

	const MH_STATUS enableStatus =
		MH_EnableHook(reinterpret_cast<LPVOID>(lookupTarget));
	if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
		Warning("R1Delta: failed to enable R1O remote-access lookup hook (%d)\n",
			static_cast<int>(enableStatus));
		return;
	}

	const uintptr_t readIntTarget = engineBase + 0x159380;
	const MH_STATUS readCreateStatus = MH_CreateHook(
		reinterpret_cast<LPVOID>(readIntTarget),
		reinterpret_cast<LPVOID>(+readIntHook),
		reinterpret_cast<LPVOID*>(&originalReadInt));
	if (readCreateStatus != MH_OK
		&& readCreateStatus != MH_ERROR_ALREADY_CREATED) {
		Warning(
			"R1Delta: failed to create R1O remote-access CUtlBuffer read hook (%d)\n",
			static_cast<int>(readCreateStatus));
		return;
	}

	const MH_STATUS readEnableStatus =
		MH_EnableHook(reinterpret_cast<LPVOID>(readIntTarget));
	if (readEnableStatus != MH_OK && readEnableStatus != MH_ERROR_ENABLED) {
		Warning(
			"R1Delta: failed to enable R1O remote-access CUtlBuffer read hook (%d)\n",
			static_cast<int>(readEnableStatus));
		return;
	}

	const uintptr_t writeRequestTarget = engineBase + 0x15A0D0;
	const MH_STATUS writeCreateStatus = MH_CreateHook(
		reinterpret_cast<LPVOID>(writeRequestTarget),
		reinterpret_cast<LPVOID>(+writeDataRequestHook),
		reinterpret_cast<LPVOID*>(&originalWriteDataRequest));
	if (writeCreateStatus != MH_OK
		&& writeCreateStatus != MH_ERROR_ALREADY_CREATED) {
		Warning(
			"R1Delta: failed to create R1O remote-access request hook (%d)\n",
			static_cast<int>(writeCreateStatus));
		return;
	}

	const MH_STATUS writeEnableStatus =
		MH_EnableHook(reinterpret_cast<LPVOID>(writeRequestTarget));
	if (writeEnableStatus != MH_OK && writeEnableStatus != MH_ERROR_ENABLED) {
		Warning(
			"R1Delta: failed to enable R1O remote-access request hook (%d)\n",
			static_cast<int>(writeEnableStatus));
		return;
	}

	installedTarget = lookupTarget;
	if (AreR1OFakeDediVerboseLogsEnabled()) {
		OutputDebugStringA(
			"R1Delta: installed R1O remote-access compatibility hooks\n");
	}
}

void EnsureR1ONetConsoleInitialized(uintptr_t engineBase)
{
	if (!engineBase)
		return;

	auto** const manager =
		reinterpret_cast<void**>(engineBase + 0x22EFDD0);
	if (*manager) {
		const auto managerAddress = reinterpret_cast<uintptr_t>(*manager);
		auto* const listenSocket =
			reinterpret_cast<SOCKET*>(managerAddress + 48);
		const bool configured =
			*reinterpret_cast<unsigned char*>(managerAddress + 360) != 0;
		if (configured && *listenSocket == INVALID_SOCKET) {
			reinterpret_cast<R1OCreateListenSocketFn>(
				engineBase + 0x28B100)(
					reinterpret_cast<void*>(managerAddress + 8),
					reinterpret_cast<const void*>(managerAddress + 336));
		}

		static bool loggedExistingManager = false;
		if (AreR1OFakeDediVerboseLogsEnabled() && !loggedExistingManager) {
			loggedExistingManager = true;
			Msg(
				"R1Delta: existing R1O CNetConsoleMgr manager=%p "
				"listenSocket=%lld configured=%d port=%u wsa=%d\n",
				*manager,
				static_cast<long long>(*listenSocket),
				configured ? 1 : 0,
				static_cast<unsigned int>(_byteswap_ushort(
					*reinterpret_cast<unsigned short*>(
						managerAddress + 356))),
				WSAGetLastError());
		}
		return;
	}

	using EngineOperatorNewFn = void* (__fastcall*)(size_t);
	using NetConsoleConstructorFn = void* (__fastcall*)(void*);
	auto engineOperatorNew =
		reinterpret_cast<EngineOperatorNewFn>(engineBase + 0x34B150);
	auto constructNetConsole =
		reinterpret_cast<NetConsoleConstructorFn>(engineBase + 0x1A0770);

	void* storage = engineOperatorNew(0x980);
	if (!storage) {
		Warning("R1Delta: failed to allocate R1O CNetConsoleMgr\n");
		return;
	}

	*manager = constructNetConsole(storage);
	if (AreR1OFakeDediVerboseLogsEnabled()) {
		Msg(
			"R1Delta: initialized R1O CNetConsoleMgr manager=%p "
			"listenSocket=%lld configured=%d port=%u wsa=%d\n",
			*manager,
			*manager
				? static_cast<long long>(*reinterpret_cast<SOCKET*>(
					reinterpret_cast<uintptr_t>(*manager) + 48))
				: -1LL,
			*manager
				&& *reinterpret_cast<unsigned char*>(
					reinterpret_cast<uintptr_t>(*manager) + 360)
				? 1
				: 0,
			*manager
				? static_cast<unsigned int>(_byteswap_ushort(
					*reinterpret_cast<unsigned short*>(
						reinterpret_cast<uintptr_t>(*manager) + 356)))
				: 0,
			WSAGetLastError());
	}
}
typedef __int64(*Host_InitDedicatedType)(__int64 a1, __int64 a2, __int64 a3);
Host_InitDedicatedType Host_InitDedicatedOriginal;
__int64 Host_InitDedicated(__int64 a1, __int64 a2, __int64 a3)
{
	//CModule engine("engine.dll", (uintptr_t)LoadLibraryA("engine.dll"));
	//CModule engineDS("engine_ds.dll");
	//InitDedicatedVtables();
	uintptr_t engine = (uintptr_t)LoadLibraryA("engine.dll");
	uintptr_t engineDS = G_engine_ds;

	InitEncodeSpecialFloatHelpers(engineDS);

	NET_CreateNetChannelOriginal = NET_CreateNetChannelType(engine + 0x1F1B10);
	MH_CreateHook(LPVOID(engineDS + 0x6B490), LPVOID(CServerRemoteAccess__LookupStringValue__6B490), reinterpret_cast<LPVOID*>(&oCServerRemoteAccess__LookupStringValue__6B490)); // NET_BufferToBufferCompress

	// Hook CSys::ConsoleOutput at G_vscript + 0x6B800 for dedicated server only
	if (IsDedicatedServer() && G_vscript) {
		MH_CreateHook(LPVOID(G_vscript + 0x6B800), LPVOID(ConsoleOutputHook), reinterpret_cast<LPVOID*>(&ConsoleOutputOriginal));
	}

	MH_CreateHook(LPVOID(engine + 0x1F4FC0), LPVOID(NET_Config), reinterpret_cast<LPVOID*>(&oNET_Config)); // NET_BufferToBufferCompress
	MH_CreateHook(LPVOID(engineDS + 0x67480), LPVOID(hook_CUtlBuffer_GetInt), reinterpret_cast<LPVOID*>(&original_CUtlBuffer_GetInt)); // NET_BufferToBufferCompress
	MH_CreateHook(LPVOID(engineDS + 0x6C680), LPVOID(hook_CServerRemoteAccess_WriteDataRequest), reinterpret_cast<LPVOID*>(&original_CServerRemoteAccess_WriteDataRequest)); // NET_BufferToBufferCompress
	MH_CreateHook(LPVOID(engineDS + 0x339870), LPVOID(CSocketCreator_CreateListenSocket_Hook), NULL); // NET_BufferToBufferCompress
	MH_CreateHook(LPVOID(engine + 0x8E4B0), LPVOID(engineDS + 0x1B9A0), NULL); // NET_BufferToBufferCompress
	
	MH_CreateHook(LPVOID(engineDS + 0x13CA10), LPVOID(engine + 0x1EC7B0), NULL); // NET_BufferToBufferCompress
	MH_CreateHook(LPVOID(engineDS + 0x13DD90), LPVOID(engine + 0x1EDB40), NULL); // NET_BufferToBufferDecompress
	MH_CreateHook(LPVOID(engineDS + 0x144C60), LPVOID(engine + 0x1F4AC0), NULL); // NET_ClearQueuedPacketsForChannel
	MH_CreateHook(LPVOID(engineDS + 0x141D60), &NET_CreateNetChannel, NULL); // NET_CreateNetChannel LPVOID(engine + 0x1F1B10)
	MH_CreateHook(LPVOID(engineDS + 0x13F7A0), LPVOID(engine + 0x1EF550), NULL); // NET_GetUDPPort
	MH_CreateHook(LPVOID(engineDS + 0x145440), LPVOID(engine + 0x1F5220), NULL); // NET_Init
	MH_CreateHook(LPVOID(engineDS + 0x13C9D0), LPVOID(engine + 0x1EC770), NULL); // NET_InitPostFork
	MH_CreateHook(LPVOID(engineDS + 0x13C8D0), LPVOID(engine + 0x1EC660), NULL); // NET_IsDedicated
	MH_CreateHook(LPVOID(engineDS + 0x13C8C0), LPVOID(engine + 0x1EC650), NULL); // NET_IsMultiplayer
	MH_CreateHook(LPVOID(engineDS + 0x13F7D0), LPVOID(engine + 0x1EF580), NULL); // NET_ListenSocket
	Original_NET_OutOfBandPrintf = NET_OutOfBandPrintf_t(engine + 0x1F47C0);
	MH_CreateHook(LPVOID(engineDS + 0x144960), LPVOID(Detour_NET_OutOfBandPrintf), NULL); // NET_OutOfBandPrintf
	MH_CreateHook(LPVOID(engineDS + 0x142ED0), LPVOID(engine + 0x1F2CF0), NULL); // NET_ProcessSocket
	MH_CreateHook(LPVOID(engineDS + 0x1458A0), LPVOID(engine + 0x1F5650), NULL); // NET_RemoveNetChannel
	MH_CreateHook(LPVOID(engineDS + 0x143390), LPVOID(engine + 0x1F31B0), NULL); // NET_RunFrame
	NET_SendPacketOriginal = NET_SendPacketType(engine + 0x1F4130);
	MH_CreateHook(LPVOID(engineDS + 0x1442D0), LPVOID(engine + 0x1F4130), NULL); // NET_SendPacket
	MH_CreateHook(LPVOID(engineDS + 0x144D10), LPVOID(engine + 0x1F4B70), NULL); // NET_SendQueuedPackets
	MH_CreateHook(LPVOID(engineDS + 0x1453D0), LPVOID(engine + 0x1F51B0), NULL); // NET_SetMultiplayer
	MH_CreateHook(LPVOID(engineDS + 0x1434C0), LPVOID(engine + 0x1F32E0), NULL); // NET_Shutdown
	MH_CreateHook(LPVOID(engineDS + 0x13FAB0), LPVOID(engine + 0x1EF860), NULL); // NET_SleepUntilMessages
	MH_CreateHook(LPVOID(engineDS + 0x13C3E0), LPVOID(engine + 0x1EC1B0), NULL); // NET_StringToAdr
	MH_CreateHook(LPVOID(engineDS + 0x13C100), LPVOID(engine + 0x1EBED0), NULL); // NET_StringToSockaddr
	MH_CreateHook(LPVOID(engineDS + 0x146D50), LPVOID(engine + 0x1F6B90), NULL); // INetMessage__WriteToBuffer
	MH_CreateHook(LPVOID(engineDS + 0x13B000), LPVOID(engine + 0x1E9EA0), NULL); // CNetChan__CNetChan__dtor
	MH_CreateHook(LPVOID(engineDS + 0x017940), LPVOID(engine + 0x028BC0), NULL); // CLC_SplitPlayerConnect__dtor
	MH_CreateHook(LPVOID(engineDS + 0x12F140), LPVOID(engine + 0x1DC830), NULL); // SendTable_WriteInfos
	MH_CreateHook(LPVOID(engineDS + 0x11B110), &EncodeSpecialFloat_Hook, reinterpret_cast<LPVOID*>(&EncodeSpecialFloatOriginal)); // EncodeSpecialFloat
	MH_CreateHook(LPVOID(engineDS + 0x497F0), LPVOID(engine + 0xD8420), NULL); // CBaseClient::ConnectionStart
	MH_CreateHook(LPVOID(engine + 0xF8050), LPVOID(engineDS + 0x69050), NULL); // CRCONServer::RunFrame
	MH_CreateHook(LPVOID(engine + 0xF4B90), LPVOID(engineDS + 0x65B70), NULL); // RCONServer
	MH_CreateHook(LPVOID(engine + 0xF4BA0), LPVOID(engineDS + 0x65B80), NULL); // RPTServer
	MH_CreateHook((LPVOID)(G_engine + 0xE13D0), &CBanSystem::Filter_ShouldDiscard, NULL);

	//MH_CreateHook(LPVOID(engineDS + 0x6ABF0), LPVOID(engine + 0xF9BB0), NULL); // CServerRemoteAccess::GetUserBanList
	//MH_CreateHook(LPVOID(engineDS + 0x53690), LPVOID(engine + 0xE1F00), NULL); // Filter_Add_f
	//MH_CreateHook(LPVOID(engineDS + 0x52B60), LPVOID(engine + 0xE13D0), NULL); // Filter_ShouldDiscard
	//MH_CreateHook(LPVOID(engineDS + 0x53AF0), LPVOID(engine + 0xE2370), NULL); // banid
	//MH_CreateHook(LPVOID(engineDS + 0x53040), LPVOID(engine + 0xE18B0), NULL); // removeid
	//MH_CreateHook(LPVOID(engineDS + 0x52C80), LPVOID(engine + 0xE14F0), NULL); // removeip
	////MH_CreateHook(LPVOID(engineDS + 0x52730), LPVOID(engine + 0xE0FA0), NULL); // writeid
	//MH_CreateHook(LPVOID(engineDS + 0x525F0), LPVOID(engine + 0xE0E60), NULL); // writeip



#if BUILD_DEBUG && VTABLE_UPDATE_FORCE
	OutputDebugStringA("const vtableRef2Engines netMessages[] = {\n");
#endif

	for (size_t i = 0; i < (sizeof(netMessages) / sizeof(netMessages[0])); i++) {
		auto msg = &netMessages[i];
#if BUILD_DEBUG
		if (VTABLE_UPDATE_FORCE || !msg->offset_engine || !msg->offset_engine_ds) {
			static char buf[512];
			if (FindVTables(engine, engineDS, msg)) {
				sprintf_s(buf, "    VTABLEREF2ENGINES(\"%s\", 0x%04llX, 0x%04llX),\n", msg->name, msg->offset_engine, msg->offset_engine_ds);
				OutputDebugStringA(buf);
			}
			else {
				sprintf_s(buf, "    VTABLEREF2ENGINES(\"%s\", 0x%04llX, 0x%04llX), // !!! ERROR !!!\n", msg->name, msg->offset_engine, msg->offset_engine_ds);
				OutputDebugStringA(buf);
			}
		}
#endif

		auto dsVtable = engineDS + msg->offset_engine_ds;
		auto vtable = engine + msg->offset_engine;

		if (dsVtable && vtable) {
			for (int i = 0; i < 14; ++i) {
				LPVOID pTarget = ((LPVOID*)dsVtable)[i];
				LPVOID pDetour = ((LPVOID*)vtable)[i];
				MH_CreateHook(pTarget, pDetour, NULL);
			}
		}
	}

#if BUILD_DEBUG && VTABLE_UPDATE_FORCE
	OutputDebugStringA("};\n");
#endif
	MH_EnableHook(MH_ALL_HOOKS);
	reinterpret_cast<char(__fastcall*)(__int64, CreateInterfaceFn)>((uintptr_t)(engine) + 0x01A04A0)(0, (CreateInterfaceFn)(engineDS + 0xE9000)); // connect nondedi engine
	reinterpret_cast<void(__fastcall*)(int, void*)>((uintptr_t)(engine) + 0x47F580)(0, 0); // register nondedi engine cvars
#if BUILD_DEBUG
	//if (!InitNetChanWarningHooks())
	//	MessageBoxA(NULL, "Failed to initialize warning hooks", "ERROR", 16);
#endif
	auto result = Host_InitDedicatedOriginal(a1, a2, a3);

	MCPServer::InstallEchoCommandFix();
	if (ShouldEnableMCP()) {
		MCPServer::InitializeMCP();
	}

	if (!eos::InitializeNetworking())
	{
		Msg("EOS: Initialization skipped or failed\n");
	}
	net_hooks::Initialize();

	OriginalCCVar_FindVar(cvarinterface, "sv_alltalk")->m_nFlags |= FCVAR_REPLICATED;

	return result;
}
char* __fastcall sub_311910(char* a1, const char* a2, signed __int64 a3)
{
	char* result; // rax

	result = strncpy(a1, a2, a3);
	if (a3 > 0)
		a1[a3 - 1] = 0;
	return result;
}
char __fastcall CBaseClient__ProcessClientInfo(__int64 a1, __int64 a2)
{
	char v4; // al
	int v5; // eax
	int v6; // eax
	int v7; // eax

	*(_DWORD*)(a1 + 940) = *(_DWORD*)(a2 + 32);
	v4 = *(_BYTE*)(a2 + 44);
	*(_DWORD*)(a1 + 976) = 0;
	*(_BYTE*)(a1 + 928) = v4;
	sub_311910((char*)(a1 + 648), (const char*)(a2 + 52), 256i64);
	*(_QWORD*)(a1 + 944) = *(unsigned int*)(a2 + 308);
	v5 = *(_DWORD*)(a2 + 312);
	*(_DWORD*)(a1 + 956) = 0;
	*(_DWORD*)(a1 + 952) = v5;
	v6 = *(_DWORD*)(a2 + 316);
	*(_DWORD*)(a1 + 964) = 0;
	*(_DWORD*)(a1 + 960) = v6;
	v7 = *(_DWORD*)(a2 + 320);
	*(_DWORD*)(a1 + 972) = 0;
	*(_DWORD*)(a1 + 968) = v7;
	if (*(_DWORD*)(a2 + 40) != (*(unsigned int(__fastcall**)(_QWORD))(**(_QWORD**)(a1 + 920) + 120i64))(*(_QWORD*)(a1 + 920)))
		(*(void(__fastcall**)(__int64))(*(_QWORD*)(a1 - 8) + 96i64))(a1 - 8);
	return 1;
}
static float		host_nexttick = 0;		// next server tick in this many ms
static float* host_state_interval_per_tick;
static float* host_remainder;
static void (*oCbuf_Execute)(void);
static void Cbuf_Execute() {
	static uintptr_t ret_from_host_runframe = G_engine_ds + 0xA181E;
	if (uintptr_t(_ReturnAddress()) == ret_from_host_runframe) {
		if (!host_state_interval_per_tick)
			host_state_interval_per_tick = (float*)(G_engine_ds + 0x547300);
		if (!host_remainder)
			host_remainder = (float*)(G_engine_ds + 0x20408C0);
		host_nexttick = *host_state_interval_per_tick - *host_remainder;
	}
	oCbuf_Execute();
}
static bool CEngine__FilterTime(void* thisptr, float dt, float* flMinFrameTime)
{
	*flMinFrameTime = host_nexttick;
	return (dt >= host_nexttick);
}
void* (*oKeyValues__SetString__Dedi)(__int64 a1, char* a2, const char* a3);
void* KeyValues__SetString__Dedi(__int64 a1, char* a2, const char* a3)
{
	static auto target = G_engine_ds + 0xC36AD;
	if (uintptr_t(_ReturnAddress()) == target)
		a3 = "30"; // force replay updaterate to 60
	return oKeyValues__SetString__Dedi(a1, a2, a3);
}
void (*oHost_Map_Helper)(const CCommand* args, int flags);
void Host_Map_Helper(const CCommand* args, int flags)
{
	static int* ss_state = (int*)(G_engine_ds + 0x1C89A78);
	if (!(*ss_state < 2)) {
		static auto changelevel_cmd = (void(*)(const CCommand*))(G_engine_ds + 0xA3850);
		changelevel_cmd(args);
		return;
	}
	oHost_Map_Helper(args, flags);
}
char* (*osub_180311910)(char* a1, const char* a2, signed __int64 a3);
char* __fastcall sub_180311910(char* a1, const char* a2, signed __int64 a3) {
	auto ret = osub_180311910(a1, a2, a3);
	if (((uintptr_t)(_ReturnAddress()) == (G_engine_ds + 0xA8B28)))
		Msg("(fs) %s\n", a2);
	return ret;
}
void InitDedicated()
{
	uintptr_t offsets[] = {
		// move g_ServerGlobalVariables_nTimestampNetworkingBase + 4
		0x552fc,
		// move g_ServerGlobalVariables_nTimestampRandomizeWindow + 4
		0x55303,
		// move g_ServerGlobalVariables_mapname_pszValue + 4
		0x5ec5f,
		// move g_ServerGlobalVariables_mapversion + 4
		0x286ba,
		0x286cb,
		0x2898c,
		0x5e7aa,
		// move g_ServerGlobalVariables_startspot + 4
		0x5ec81,
		// move g_ServerGlobalVariables_bMapLoadFailed + 4
		0xaa088,
		// move g_ServerGlobalVariables_deathmatch + 4
		0x5ec41,
		0x5ec4b,
		// move g_ServerGlobalVariables_coop + 4
		0x5ec2c,
		// move g_ServerGlobalVariables_maxEntities + 4
		0x5e823,
		0x61C10,
		// move g_ServerGlobalVariables_serverCount + 4
		0xa05d2,
		0xa178e,
		// move g_ServerGlobalVariables_pEdicts + 4
		0x5DBC7,
		0x5dc0c,
		0x5e89d,
		0x61c86
	};

	auto engine_ds = G_engine_ds;

	for (auto offset : offsets) {
		int* address = reinterpret_cast<int*>(engine_ds + offset);

		DWORD oldProtect;
		if (!VirtualProtect(address, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
			continue;
		}

		*address += 4;

		DWORD temp;
		VirtualProtect(address, sizeof(uintptr_t), oldProtect, &temp);

		FlushInstructionCache(GetCurrentProcess(), address, sizeof(uintptr_t));
	}
	MH_CreateHook((LPVOID)(engine_ds + 0xA1B90), &Host_InitDedicated, reinterpret_cast<LPVOID*>(&Host_InitDedicatedOriginal));
	MH_CreateHook((LPVOID)(engine_ds + 0x31EB20), &ConVar_PrintDescription, reinterpret_cast<LPVOID*>(&ConVar_PrintDescriptionOriginal));
	MH_CreateHook((LPVOID)(engine_ds + 0x310780), &sub_1804722E0, 0);
	MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x45C00), &CBaseClient__ProcessClientInfo, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(engine_ds + 0x756E0), &Cbuf_Execute, reinterpret_cast<LPVOID*>(&oCbuf_Execute));
	//MH_CreateHook((LPVOID)(engine_ds + 0xEF480), &CEngine__FilterTime, reinterpret_cast<LPVOID*>(NULL));
	//MH_CreateHook((LPVOID)(engine_ds + 0x318D60), &KeyValues__SetString__Dedi, reinterpret_cast<LPVOID*>(&oKeyValues__SetString__Dedi));
	MH_CreateHook((LPVOID)(engine_ds + 0x318D60), &KeyValues__SetString__Dedi, reinterpret_cast<LPVOID*>(&oKeyValues__SetString__Dedi));
	MH_CreateHook((LPVOID)(engine_ds + 0xA4AD0), &Host_Map_Helper, reinterpret_cast<LPVOID*>(&oHost_Map_Helper));
	//MH_CreateHook((LPVOID)(engine_ds + 0x311910), &sub_180311910, reinterpret_cast<LPVOID*>(&osub_180311910));
	//MH_CreateHook((LPVOID)(engine_ds + 0x360230), &vsnprintf_l_hk, NULL);
	MH_EnableHook(MH_ALL_HOOKS);
}

// AddSearchPath hook for dedicated server
__int64 (*oAddSearchPathDedi)(__int64 a1, const char* a2, __int64 a3, unsigned int a4);
__int64 __fastcall AddSearchPathDedi(__int64 a1, const char* a2, __int64 a3, unsigned int a4) {
	if (!strcmp_static(a2, "r1delta")) {
		auto a = _strdup((std::filesystem::path(GetExecutableDirectory()) / "r1delta").string().c_str());
		a2 = a;
		auto ret = oAddSearchPathDedi(a1, a2, a3, a4);
		return ret;
	}
	return oAddSearchPathDedi(a1, a2, a3, a4);
}

// Server info panel hook for dedicated server admin
void (*oCServerInfoPanel__OnServerDataResponse_14730)(__int64 a1, const char* a2, const char* a3);

void InstallAdminServerHooks(uintptr_t adminServerBase)
{
	if (!adminServerBase)
		return;

	static uintptr_t installedTarget = 0;
	const uintptr_t target = adminServerBase + 0x14730;
	if (installedTarget == target)
		return;

	const MH_STATUS createStatus = MH_CreateHook(
		reinterpret_cast<LPVOID>(target),
		&CServerInfoPanel__OnServerDataResponse_14730,
		reinterpret_cast<LPVOID*>(&oCServerInfoPanel__OnServerDataResponse_14730));
	if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED) {
		Warning(
			"R1Delta: failed to create AdminServer response hook "
			"(create=%d target=%p)\n",
			static_cast<int>(createStatus),
			reinterpret_cast<void*>(target));
		return;
	}

	const MH_STATUS enableStatus =
		MH_EnableHook(reinterpret_cast<LPVOID>(target));
	if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
		Warning(
			"R1Delta: failed to enable AdminServer response hook "
			"(enable=%d target=%p)\n",
			static_cast<int>(enableStatus),
			reinterpret_cast<void*>(target));
		return;
	}

	installedTarget = target;
}

static void* FindAdminServerConfigRow(
	__int64 panel,
	uintptr_t adminServer,
	const char* variableName)
{
	if (!panel || !adminServer || !variableName || !*variableName)
		return nullptr;

	void* listPanel = *reinterpret_cast<void**>(panel + 720);
	if (!listPanel)
		return nullptr;

	const uintptr_t listPanelVtable = *reinterpret_cast<uintptr_t*>(listPanel);
	if (!listPanelVtable)
		return nullptr;

	using GetItemCountType = int(__fastcall*)(void*);
	using GetItemType = void* (__fastcall*)(void*, int);
	using GetKeyValuesNameType = const char* (__fastcall*)(void*);

	const auto getItemCount =
		*reinterpret_cast<GetItemCountType*>(listPanelVtable + 1960);
	const auto getItem =
		*reinterpret_cast<GetItemType*>(listPanelVtable + 1968);
	const auto getKeyValuesName =
		reinterpret_cast<GetKeyValuesNameType>(adminServer + 0x18EC0);
	if (!getItemCount || !getItem || !getKeyValuesName)
		return nullptr;

	const int itemCount = getItemCount(listPanel);
	if (itemCount < 0 || itemCount > 1024)
		return nullptr;

	for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
		void* row = getItem(listPanel, itemIndex);
		if (!row)
			continue;

		const char* rowName = getKeyValuesName(row);
		if (rowName && _stricmp(rowName, variableName) == 0)
			return row;
	}

	return nullptr;
}

static bool SetAdminServerConfigRowString(
	__int64 panel,
	uintptr_t adminServer,
	const char* variableName,
	const char* propertyName,
	const char* value)
{
	void* row = FindAdminServerConfigRow(panel, adminServer, variableName);
	if (!row)
		return false;

	using SetKeyValuesStringType =
		void(__fastcall*)(void*, const char*, const char*);
	const auto setKeyValuesString =
		reinterpret_cast<SetKeyValuesStringType>(adminServer + 0x1B3A0);
	setKeyValuesString(row, propertyName, value ? value : "");
	return true;
}

void CServerInfoPanel__OnServerDataResponse_14730(__int64 a1, const char* a2, const char* a3) {
	const uintptr_t adminServer = reinterpret_cast<uintptr_t>(GetModuleHandleA("AdminServer.dll"));
	if (!adminServer || !a2 || !a3) {
		if (oCServerInfoPanel__OnServerDataResponse_14730)
			oCServerInfoPanel__OnServerDataResponse_14730(a1, a2, a3);
		return;
	}

	if (IsR1ODedicatedServer() && AreR1OFakeDediVerboseLogsEnabled()) {
		Msg(
			"R1Delta: R1O AdminServer response name=\"%s\" length=%zu value=\"%.160s\"\n",
			a2,
			strlen(a3),
			a3);
	}

	const __int64 panel = a1 - 704;

	if (oCServerInfoPanel__OnServerDataResponse_14730)
		oCServerInfoPanel__OnServerDataResponse_14730(a1, a2, a3);

	if (strcmp_static(a2, "maplist") == 0 && *a3) {
		SetAdminServerConfigRowString(
			panel,
			adminServer,
			"map",
			"stringlist",
			a3);
		const char* map = GetDedicatedCurrentMap();
		if (map && *map)
			SetAdminServerConfigRowString(
				panel,
				adminServer,
				"map",
				"value",
				map);
	}

	if (strcmp_static(a2, "map") == 0 && strlen(a3) > 3) {
		const uintptr_t engineBase = GetDedicatedEngineModule();
		const auto factory = engineBase
			? reinterpret_cast<CreateInterfaceFn>(
				GetProcAddress(reinterpret_cast<HMODULE>(engineBase), "CreateInterface"))
			: nullptr;
		void* hldsApi = factory
			? factory("VENGINE_HLDS_API_VERSION002", nullptr)
			: nullptr;

		if (hldsApi) {
			const uintptr_t vtable = *reinterpret_cast<uintptr_t*>(hldsApi);
			const auto refreshPlaylists =
				*reinterpret_cast<void(__fastcall**)(void*)>(vtable + 136);
			const auto pumpPlaylists =
				*reinterpret_cast<void(__fastcall**)(void*)>(vtable + 144);
			const auto getPlaylistCount =
				*reinterpret_cast<int(__fastcall**)(void*)>(vtable + 152);
			const auto getPlaylistName =
				*reinterpret_cast<const char* (__fastcall**)(void*, unsigned int)>(
					vtable + 160);

			refreshPlaylists(hldsApi);
			int playlistCount = getPlaylistCount(hldsApi);
			for (int attempt = 0;
				playlistCount <= 0 && attempt < 64;
				++attempt) {
				pumpPlaylists(hldsApi);
				playlistCount = getPlaylistCount(hldsApi);
			}

			if (IsR1ODedicatedServer() && AreR1OFakeDediVerboseLogsEnabled()) {
				Msg(
					"R1Delta: R1O AdminServer HLDS playlist refresh count=%d api=%p\n",
					playlistCount,
					hldsApi);
			}

			if (playlistCount >= 0 && playlistCount <= 512) {
				std::string combinedPlaylists;
				for (int index = 0; index < playlistCount; ++index) {
					const char* playlist = getPlaylistName(
						hldsApi,
						static_cast<unsigned int>(index));
					if (!playlist || !*playlist)
						continue;
					combinedPlaylists += playlist;
					combinedPlaylists += '\n';
				}
				SetAdminServerConfigRowString(
					panel,
					adminServer,
					"launchplaylist",
					"stringlist",
					combinedPlaylists.c_str());
			}
		}

		const char* playlist = GetDedicatedCurrentPlaylist();
		if (playlist && *playlist)
			SetAdminServerConfigRowString(
				panel,
				adminServer,
				"launchplaylist",
				"value",
				playlist);
	}
}
