#pragma once
#include <Windows.h>
#include "MinHook.h"
#include "netadr.h"

struct netadr {
	netadrtype_t type;
	IN6_ADDR adr;
	uint16_t port;
	bool field_16;
	bool reliable;
};

void InitSteamHooks();