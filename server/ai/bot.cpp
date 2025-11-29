// Bot management system for R1Delta
// Handles dummy bot creation and team assignment

#include "bot.h"
#include "core.h"
#include "load.h"
#include "logging.h"
#include "cvar.h"

#include <cstdio>

static int g_botCounter = 1; // Counter for sequential bot names
bool isCreatingBot = false;
int botTeamIndex = 0;

__int64 (*oCPortal_Player__ChangeTeam)(__int64 thisptr, unsigned int index) = nullptr;

__int64 __fastcall CPortal_Player__ChangeTeam(__int64 thisptr, unsigned int index)
{
    if (isCreatingBot)
        index = botTeamIndex;
    return oCPortal_Player__ChangeTeam(thisptr, index);
}

void AddBotDummyConCommand(const CCommand& args)
{
    // Expected usage: bot_dummy -team <team index>
    if (args.ArgC() != 3)
    {
        Warning("Usage: bot_dummy -team <team index>\n");
        return;
    }

    // Check for the '-team' flag
    if (strcmp_static(args.Arg(1), "-team") != 0)
    {
        Warning("Usage: bot_dummy -team <team index>\n");
        return;
    }

    // Parse the team index
    int teamIndex = 0;
    try
    {
        teamIndex = std::stoi(args.Arg(2));
    }
    catch (const std::invalid_argument&)
    {
        Warning("Invalid team index: %s\n", args.Arg(2));
        return;
    }
    catch (const std::out_of_range&)
    {
        Warning("Team index out of range: %s\n", args.Arg(2));
        return;
    }

    botTeamIndex = teamIndex;

    // Generate sequential bot name
    char botName[16]; // Buffer for "BotXX" + null terminator
    snprintf(botName, sizeof(botName), "Bot%02d", g_botCounter);

    HMODULE serverModule = GetModuleHandleA("server.dll");
    if (!serverModule)
    {
        Warning("Failed to get handle for server.dll\n");
        return;
    }

    typedef CPluginBotManager* (*CreateInterfaceFn)(const char* name, int* returnCode);
    CreateInterfaceFn CreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(serverModule, "CreateInterface"));
    if (!CreateInterface)
    {
        Warning("Failed to get CreateInterface function from server.dll\n");
        return;
    }

    int returnCode = 0;
    CPluginBotManager* pBotManager = CreateInterface("BotManager001", &returnCode);
    if (!pBotManager)
    {
        Warning("Failed to retrieve BotManager001 interface\n");
        return;
    }

    isCreatingBot = true;
    __int64 pBot = pBotManager->CreateBot(botName);
    isCreatingBot = false;

    if (!pBot)
    {
        Warning("Failed to create dummy bot with name: %s\n", botName);
        return;
    }
    else
    {
        // Increment counter only if bot creation was successful
        g_botCounter++;
    }

    typedef void (*ClientFullyConnectedFn)(__int64 thisptr, __int64 entity);
    ClientFullyConnectedFn CServerGameClients_ClientFullyConnected = (ClientFullyConnectedFn)(G_server + 0x1499E0);
    CServerGameClients_ClientFullyConnected(0, pBot);

    Msg("Dummy bot '%s' has been successfully created and assigned to team %d.\n", botName, teamIndex);
}
