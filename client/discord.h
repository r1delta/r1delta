#pragma once  
#include <discord-game-sdk/discord.h>  
#include "logging.h"  
#include "squirrel.h"
#include "load.h"
#define DISCORD_APPLICATION_ID 1304910395013595176  
#include <mutex>
#include <queue>

enum class DiscordCommandType {
    AUTH,
};

struct DiscordCommandQueue {
    std::mutex queueMutex;
    std::queue<DiscordCommandType> commands;

    void AddCommand(DiscordCommandType cmd) {
        std::lock_guard<std::mutex> lock(queueMutex);
        commands.push(cmd);
    }

    bool GetNextCommand(DiscordCommandType& cmd) {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (commands.empty()) {
            return false;
        }
        cmd = commands.front();
        commands.pop();
        return true;
    }
};
static discord::Core* core = nullptr;

extern void DiscordThread();
extern int64 GetDiscordId();
SQInteger SendDiscordUI(HSQUIRRELVM v);
SQInteger SendDiscordClient(HSQUIRRELVM v);
extern void DiscordAuthCommand(const CCommand& args);


typedef void (*SetConvarString_t)(ConVarR1* var, const char* value);

extern SetConvarString_t SetConvarStringOriginal;