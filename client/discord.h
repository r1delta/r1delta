#pragma once  
#include <discord-game-sdk/discord.h>  
#include "logging.h"  
#include "squirrel.h"
#include "load.h"
#define DISCORD_APPLICATION_ID 1304910395013595176  
#include <mutex>
#include <queue>
#include <variant>

enum class DiscordCommandType {
    AUTH,
};

using DiscordCommand = std::variant<DiscordCommandType, discord::Activity>;

struct DiscordCommandQueue {
    std::mutex queueMutex;
    std::queue<DiscordCommand> commands;

    void AddCommand(DiscordCommandType cmd) {
        std::lock_guard<std::mutex> lock(queueMutex);
        commands.emplace(cmd);
    }

    void AddActivity(const discord::Activity& activity) {
        std::lock_guard<std::mutex> lock(queueMutex);
        commands.emplace(activity);
    }

    bool GetNextCommand(DiscordCommand& cmd) {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (commands.empty()) {
            return false;
        }
        cmd = std::move(commands.front());
        commands.pop();
        return true;
    }
};

extern void DiscordThread();
SQInteger SendDiscordUI(HSQUIRRELVM v);
SQInteger SendDiscordClient(HSQUIRRELVM v);
extern void DiscordAuthCommand(const CCommand& args);
