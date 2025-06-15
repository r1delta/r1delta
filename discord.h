#pragma once  
#include <discord-game-sdk/discord.h>  
#include "logging.h"  
#include "squirrel.h"
#include "load.h"
#define DISCORD_APPLICATION_ID 1304910395013595176  

static discord::Core* core = nullptr;

extern void DiscordThread();
extern int64 GetDiscordId();
SQInteger SendDiscordUI(HSQUIRRELVM v);
SQInteger SendDiscordClient(HSQUIRRELVM v);