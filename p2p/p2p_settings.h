#pragma once

#include <string>

namespace p2p
{

// Snapshot of the delta_p2p_* convars (see p2p.cpp for defaults/help).
struct Settings
{
    bool enable = true;

    // Server side
    bool serverUpnp = true;
    bool serverPunch = true;
    bool serverEos = true;
    bool serverIroh = true;
    bool serverTailcat = true;
    bool serverTurn = false;
    int punchSpray = 2;

    // Client side
    int connectTimeoutMs = 2500;
    int relayPenaltyMs = 20;
    std::string prefer;

    // 0 = never, 1 = when the master server issues identities, 2 = always.
    int requireIdentity = 1;
    std::string identityPublicKeyHex;

    std::string irohRelayUrl;
    std::string tailcatDerpMapUrl;
    std::string masterServerUrl;
    bool debug = false;
};

// Returns the latest snapshot. Convar strings may only be read on the engine
// thread, so the snapshot is refreshed there (RefreshSettings, called from the
// recvfrom hook at most once per second and by console commands).
Settings GetSettings();
void RefreshSettings();
// Re-reads only the numeric convars; safe from any thread (strings are not).
// The heartbeat thread uses it so a server that starts its services before the
// engine thread's next once-a-second refresh still honours delta_p2p_server_*
// values set on the command line or in a cfg.
void RefreshSettingsNumeric();

} // namespace p2p
