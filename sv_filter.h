#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <limits> // Required for numeric_limits
#include <algorithm> // Required for std::remove_if, std::find_if
#include <cfloat> // Required for FLT_MAX
#include <cstdio> // Required for snprintf, sscanf, fopen, fprintf, fclose
#include <cstdlib> // Required for atof, atoi, strtoull
#include <cmath>   // Required for fmod
#include <iostream> // Potentially for file streams, debugging
#include "logging.h"
#include <string>
#include <in6addr.h>
#include <cstdint>
#include <vcruntime_string.h>
#include <string.h>
#include <stdio.h>
#include <WS2tcpip.h>
#include <stdlib.h>
#include "bitbuf.h"
#include "public/tier1/utlbuffer.h"
#include "netadr.h"
#define IBannablePlayerPointer  void*

// Forward declaration
struct ConVarR1;

// Main class implementation
class CBanSystem {
public:
    // Public static member to hold the ConVar pointer, accessible for registration
    static ConVarR1* m_pSvBanlistAutosave;

private:
    // --- Singleton Pattern ---
    CBanSystem() {} // Private constructor
    ~CBanSystem() {} // Private destructor
    CBanSystem(const CBanSystem&) = delete;
    CBanSystem& operator=(const CBanSystem&) = delete;

    static CBanSystem& GetInstance() {
        static CBanSystem s_Instance;
        return s_Instance;
    }

    // --- Internal Data Structures ---
    struct IpBanEntry {
        netadr_t address;
        float banTime;    // Original duration in minutes (0 for permanent)
        float expireTime; // Absolute server time when the ban expires (FLT_MAX for permanent)
    };
    struct IdBanEntry {
        uint64_t uniqueID; // Discord ID
        float banTime;     // Original duration in minutes (0 for permanent)
        float expireTime;  // Absolute server time when the ban expires (FLT_MAX for permanent)
    };

    std::vector<IpBanEntry> m_vecIpBans;
    std::vector<IdBanEntry> m_vecIdBans;

    // --- Internal Implementation Methods ---

    // Helper to check for expired bans and remove them
    void PruneExpiredBans() {
        float flCurrentTime = GetCurrentTimeInternal();

        // Prune IP Bans
        m_vecIpBans.erase(std::remove_if(m_vecIpBans.begin(), m_vecIpBans.end(),
            [flCurrentTime](const IpBanEntry& entry) {
                return entry.expireTime != FLT_MAX && entry.expireTime <= flCurrentTime;
            }), m_vecIpBans.end());

        // Prune ID Bans
        m_vecIdBans.erase(std::remove_if(m_vecIdBans.begin(), m_vecIdBans.end(),
            [flCurrentTime](const IdBanEntry& entry) {
                return entry.expireTime != FLT_MAX && entry.expireTime <= flCurrentTime;
            }), m_vecIdBans.end());
    }

    // --- IP Address Filtering ---
    void AddIPInternal(const CCommand& args) {
        if (args.ArgC() < 3) {
            Msg("Usage: addip <minutes> <ipaddress>\n");
            return;
        }
        std::string reconstructed_ip_str;
        for (int i = 2; i < args.ArgC(); ++i) {
            reconstructed_ip_str += args.Arg(i);
        }
        const char* pszIPAddress = reconstructed_ip_str.c_str();
        float flBanMinutes = atof(args.Arg(1));

        if (flBanMinutes < 0) {
            Msg("Ban time cannot be negative.\n");
            return;
        }

        netadr_t adrTarget;
        if (!adrTarget.SetFromString(pszIPAddress, false)) { // Don't use DNS for banning specific IPs
            Msg("Error: Invalid IP address format '%s'. Use IPv4 (e.g., 1.2.3.4) or IPv6 (e.g., [::1], [2001::1]).\n", pszIPAddress);
            return;
        }
        // Ensure the address type is valid for banning (IP or Loopback might be okay)
        if (adrTarget.GetType() != netadrtype_t::NA_IP && adrTarget.GetType() != netadrtype_t::NA_LOOPBACK) {
            Msg("Error: Cannot ban address type '%s'.\n", adrTarget.ToString(true));
            return;
        }

        float flCurrentTime = GetCurrentTimeInternal();
        float flExpireTime = (flBanMinutes == 0.0f) ? FLT_MAX : (flCurrentTime + flBanMinutes * 60.0f);

        // Check if IP already exists
        bool bFound = false;
        for (auto& entry : m_vecIpBans) {
            if (entry.address.CompareAdr(adrTarget)) {
                entry.banTime = flBanMinutes;
                entry.expireTime = flExpireTime;
                Msg("IP address %s ban updated: duration %.2f minutes.\n", adrTarget.ToString(true), flBanMinutes);
                bFound = true;
                break;
            }
        }

        if (!bFound) {
            // Add new entry
            // Consider adding a max ban limit check if needed
            m_vecIpBans.push_back({ adrTarget, flBanMinutes, flExpireTime });
            Msg("Added IP address %s to ban list for %.2f minutes.\n", adrTarget.ToString(true), flBanMinutes);

            // Check if any active player matches this IP and kick them
            for (IBannablePlayerPointer pPlayer : GetAllActivePlayersInternal()) {
                const netadr_t& playerAddr = GetPlayerNetAddressInternal(pPlayer);
                if (playerAddr.CompareAdr(adrTarget)) {
                    Msg("Kicking player %s (IP: %s) due to active ban.\n", GetPlayerNameInternal(pPlayer), adrTarget.ToString(true));
                    KickPlayerInternal(pPlayer, "Banned by IP address");
                    // Fire event? The spec implies events for add/remove, maybe not for the auto-kick?
                    // FireBanEventInternal("server_addban", pPlayer, 0, adrTarget.ToString(true), flBanMinutes, GetCommandIssuerInternal(), true); // If kicking counts as part of addban
                }
            }
            // Fire event for adding the ban itself
            FireBanEventInternal("server_addban", nullptr, 0, adrTarget.ToString(true), flBanMinutes, GetCommandIssuerInternal(), false);
        }
        PruneExpiredBans(); // Clean up just in case
        MaybeWriteBans(true); // Write bans if autosave is enabled
    }

    void RemoveIPInternal(const CCommand& args) {
        // Expecting at least 2 parts: command, identifier (slot or start_of_ip)
        if (args.ArgC() < 2) {
            Msg("Usage: removeip <slot | ipaddress>\n");
            return;
        }

        const char* pszFirstIdentifierPart = args.Arg(1);
        bool bRemoved = false;

        // Check if the first part looks like a slot number (only digits)
        // AND if it's the *only* identifier part (ArgC == 2)
        bool isPotentialSlot = (strspn(pszFirstIdentifierPart, "0123456789") == strlen(pszFirstIdentifierPart));

        if (isPotentialSlot && args.ArgC() == 2) {
            // Treat as slot number (1-based)
            int nSlot = atoi(pszFirstIdentifierPart);
            PruneExpiredBans(); // Ensure list is current before using index

            if (nSlot <= 0 || (size_t)nSlot > m_vecIpBans.size()) {
                Msg("Error: Invalid slot number %d. Use 'listip' to see valid slots.\n", nSlot);
                return;
            }
            // Get the IP before removing for event logging
            netadr_t removedAdr = m_vecIpBans[nSlot - 1].address; // 0-based index
            char szRemovedIpStr[INET6_ADDRSTRLEN]; // Assuming INET6_ADDRSTRLEN is defined appropriately
            removedAdr.ToString(szRemovedIpStr, sizeof(szRemovedIpStr), true);

            m_vecIpBans.erase(m_vecIpBans.begin() + (nSlot - 1));
            Msg("Removed IP ban entry in slot %d (%s).\n", nSlot, szRemovedIpStr); // Added IP to msg
            FireBanEventInternal("server_removeban", nullptr, 0, szRemovedIpStr, 0, GetCommandIssuerInternal(), false);
            bRemoved = true;

        }
        else {
            // Treat as IP address - reconstruct it from Arg(1) onwards
            std::string reconstructed_ip_str;
            for (int i = 1; i < args.ArgC(); ++i) { // Start from index 1 for removeip identifier
                reconstructed_ip_str += args.Arg(i);
            }
            const char* pszIPAddress = reconstructed_ip_str.c_str();

            netadr_t adrTarget;
            if (!adrTarget.SetFromString(pszIPAddress, false)) {
                Msg("Error: Invalid IP address format '%s'.\n", pszIPAddress);
                return;
            }

            // Use std::find_if for potentially cleaner search and removal
            auto it = std::find_if(m_vecIpBans.begin(), m_vecIpBans.end(),
                [&adrTarget](const IpBanEntry& entry) { return entry.address.CompareAdr(adrTarget); });

            if (it != m_vecIpBans.end()) {
                char szRemovedIpStr[INET6_ADDRSTRLEN];

                Msg("Removed IP ban for %s.\n", it->address.GetAddressString()); // Use formatted IP
                FireBanEventInternal("server_removeban", nullptr, 0, szRemovedIpStr, 0, GetCommandIssuerInternal(), false);
                bRemoved = true;
                m_vecIpBans.erase(it);
            }
            else {
                // Use the potentially reconstructed input string if not found
                Msg("Error: IP address %s not found in the ban list.\n", pszIPAddress);
            }
        }
        // No need to prune again if removed by IP, but does no harm if removed by slot.
        MaybeWriteBans(true); // Write bans if autosave is enabled
    }


    void ListIPInternal(const CCommand& args) {
        PruneExpiredBans(); // Update list before displaying

        if (m_vecIpBans.empty()) {
            Msg("IP ban list is empty.\n");
            return;
        }

        Msg("IP Ban List: %zu entr%s\n", m_vecIpBans.size(), (m_vecIpBans.size() == 1 ? "y" : "ies"));
        Msg("------------------------------------\n");
        int nSlot = 1;
        float flCurrentTime = GetCurrentTimeInternal();
        for (const auto& entry : m_vecIpBans) {
            const char* szAddrStr = entry.address.GetAddressString();

            if (entry.banTime == 0.0f) {
                Msg("%d %s : permanent\n", nSlot, szAddrStr);
            }
            else {
                float flRemaining = (entry.expireTime - flCurrentTime) / 60.0f;
                if (flRemaining < 0) flRemaining = 0; // Avoid negative display if clock skewed slightly
                Msg("%d %s : %.3f min remaining (%.3f total)\n", nSlot, szAddrStr, flRemaining, entry.banTime);
            }
            nSlot++;
        }
        Msg("------------------------------------\n");
    }

    void WriteIPInternal() {
        PruneExpiredBans(); // Ensure we only save active, non-expired bans

        const char* pszBaseDir = ".";
        char szFilePath[512];
        snprintf(szFilePath, sizeof(szFilePath), "%s/r1/cfg/banned_ip.cfg", pszBaseDir);

        std::ofstream outFile(szFilePath);
        if (!outFile.is_open()) {
            Warning("Error: Could not open %s for writing.\n", szFilePath);
            return;
        }

        outFile << "sv_banlist_autosave 0\r\n"; // Disable autosave during execution

        //Msg("Writing permanent IP bans to %s...\n", szFilePath);
        int nWritten = 0;
        for (const auto& entry : m_vecIpBans) {
            if (entry.banTime == 0.0f) { // Only permanent bans
                const char* szAddrStr = entry.address.GetAddressString();
                outFile << "addip 0 " << szAddrStr << "\r\n"; // Use \r\n as specified
                nWritten++;
            }
        }
        outFile << "sv_banlist_autosave 1\r\n"; // Re-enable autosave after execution
        outFile.close();
       // Msg("Wrote %d permanent IP ban entries.\n", nWritten);
    }

    // --- Discord ID Filtering ---
    void WriteIDInternal() {
        PruneExpiredBans();

        const char* pszBaseDir = ".";
        char szFilePath[512];
        snprintf(szFilePath, sizeof(szFilePath), "%s/r1/cfg/banned_user.cfg", pszBaseDir);

        std::ofstream outFile(szFilePath);
        if (!outFile.is_open()) {
            Warning("Error: Could not open %s for writing.\n", szFilePath);
            return;
        }

        outFile << "sv_banlist_autosave 0\r\n"; // Disable autosave during execution

       // Msg("Writing permanent Discord ID bans to %s...\n", szFilePath);
        int nWritten = 0;
        for (const auto& entry : m_vecIdBans) {
            if (entry.banTime == 0.0f) {
                outFile << "banid 0 " << entry.uniqueID << "\r\n"; // Use \r\n
                nWritten++;
            }
        }
        outFile << "sv_banlist_autosave 1\r\n"; // Re-enable autosave after execution
        outFile.close();
       // Msg("Wrote %d permanent Discord ID ban entries.\n", nWritten);
    }

    void RemoveAllIDsInternal(const CCommand& args) {
        size_t nRemovedCount = m_vecIdBans.size();
        m_vecIdBans.clear();
        Msg("Removed all %zu Discord ID ban entries.\n", nRemovedCount);
        MaybeWriteBans(false); // Write bans if autosave is enabled
        // Fire event? Specification doesn't mention one for removeall.
    }
    void RemoveAllIPsInternal(const CCommand& args) {
        size_t nRemovedCount = m_vecIpBans.size();
        m_vecIpBans.clear();
        Msg("Removed all %zu IP ban entries.\n", nRemovedCount);
        MaybeWriteBans(true); // Write bans if autosave is enabled
        // Fire event? Specification doesn't mention one for removeall.
    }

    void RemoveIDInternal(const CCommand& args) {
        if (args.ArgC() != 2) {
            Msg("Usage: removeid <slot | uniqueid>\n");
            return;
        }

        const char* pszIdentifier = args.Arg(1);
        bool bRemoved = false;

        // Check if it looks like a slot number (short, numeric)
        // Allow longer numbers for slots if the list is huge, but check range.
        bool bIsNumeric = (strspn(pszIdentifier, "0123456789") == strlen(pszIdentifier));
        uint64_t nIdValue = 0;
        if (bIsNumeric) {
            nIdValue = strtoull(pszIdentifier, nullptr, 10);
        }

        // Heuristic: If it's numeric and smallish, assume slot. Otherwise assume Discord ID.
        // This might fail if a Discord ID happens to be a small number, but that's unlikely.
        // A safer approach might be to *always* try slot first if numeric, then ID.
        if (bIsNumeric && strlen(pszIdentifier) < 8) // Arbitrary length threshold for slot vs ID
        {
            // Try as slot number (1-based)
            int nSlot = (int)nIdValue; // Potential truncation, but slots shouldn't exceed INT_MAX
            PruneExpiredBans(); // Update list before using index

            if (nSlot <= 0 || (size_t)nSlot > m_vecIdBans.size()) {
                // Fallback: maybe it *was* a small Discord ID? Try removing by ID.
                auto it = std::find_if(m_vecIdBans.begin(), m_vecIdBans.end(),
                    [nIdValue](const IdBanEntry& entry) { return entry.uniqueID == nIdValue; });

                if (it != m_vecIdBans.end()) {
                    uint64_t nRemovedId = it->uniqueID;
                    Msg("Removed Discord ID ban for %llu.\n", nRemovedId);
                    FireBanEventInternal("server_removeban", nullptr, nRemovedId, nullptr, 0, GetCommandIssuerInternal(), false);
                    bRemoved = true;
                    m_vecIdBans.erase(it);
                }
                else {
                    Msg("Error: Invalid slot number %d or Discord ID %llu not found.\n", nSlot, nIdValue);
                    return;
                }
            }
            else {
                // Valid slot number
                uint64_t nRemovedId = m_vecIdBans[nSlot - 1].uniqueID; // Get ID before removing
                Msg("Removed Discord ID ban entry in slot %d (ID: %llu).\n", nSlot, nRemovedId);
                FireBanEventInternal("server_removeban", nullptr, nRemovedId, nullptr, 0, GetCommandIssuerInternal(), false);
                bRemoved = true;
                m_vecIdBans.erase(m_vecIdBans.begin() + (nSlot - 1));
            }
        }
        else {
            // Treat as Discord ID (numeric or not, try to parse as uint64)
            if (!bIsNumeric) { // If it wasn't purely numeric, it can't be a standard Discord ID
                Msg("Error: Invalid Discord ID format '%s'. Must be a positive integer.\n", pszIdentifier);
                return;
            }
            // nIdValue already parsed if bIsNumeric was true

            auto it = std::find_if(m_vecIdBans.begin(), m_vecIdBans.end(),
                [nIdValue](const IdBanEntry& entry) { return entry.uniqueID == nIdValue; });

            if (it != m_vecIdBans.end()) {
                uint64_t nRemovedId = it->uniqueID;
                Msg("Removed Discord ID ban for %llu.\n", nRemovedId);
                FireBanEventInternal("server_removeban", nullptr, nRemovedId, nullptr, 0, GetCommandIssuerInternal(), false);
                bRemoved = true;
                m_vecIdBans.erase(it);
            }
            else {
                Msg("Error: Discord ID %llu not found in the ban list.\n", nIdValue);
            }
        }
        MaybeWriteBans(false); // Write bans if autosave is enabled
    }

    void ListIDInternal(const CCommand& args) {
        PruneExpiredBans();

        if (m_vecIdBans.empty()) {
            Msg("Discord ID ban list is empty.\n");
            return;
        }

        Msg("Discord ID Ban List: %zu entr%s\n", m_vecIdBans.size(), (m_vecIdBans.size() == 1 ? "y" : "ies"));
        Msg("------------------------------------\n");
        int nSlot = 1;
        float flCurrentTime = GetCurrentTimeInternal();
        for (const auto& entry : m_vecIdBans) {
            if (entry.banTime == 0.0f) {
                Msg("%d %llu : permanent\n", nSlot, entry.uniqueID);
            }
            else {
                float flRemaining = (entry.expireTime - flCurrentTime) / 60.0f;
                if (flRemaining < 0) flRemaining = 0;
                Msg("%d %llu : %.3f min remaining (%.3f total)\n", nSlot, entry.uniqueID, flRemaining, entry.banTime);
            }
            nSlot++;
        }
        Msg("------------------------------------\n");
    }

    void BanIDInternal(const CCommand& args) {
        // Usage: banid <minutes> <userid | uniqueid> {kick}
        if (args.ArgC() < 3 || args.ArgC() > 4) {
            Msg("Usage: banid <minutes> <userid | uniqueid> {kick}\n");
            return;
        }

        float flBanMinutes = atof(args.Arg(1));
        const char* pszIdentifier = args.Arg(2);
        bool bKickPlayer = (args.ArgC() == 4 && strcmp(args.Arg(3), "kick") == 0);

        if (flBanMinutes < 0) {
            Msg("Ban time cannot be negative.\n");
            return;
        }

        uint64_t nTargetUniqueID = 0;
        IBannablePlayerPointer pTargetPlayer = nullptr;

        // Determine if identifier is UserID (short numeric) or UniqueID (long numeric)
        bool bIsNumeric = (strspn(pszIdentifier, "0123456789") == strlen(pszIdentifier));
        if (!bIsNumeric) {
            Msg("Error: Invalid UserID or UniqueID format '%s'. Must be a positive integer.\n", pszIdentifier);
            return;
        }

        if (strlen(pszIdentifier) < 8) { // Heuristic for UserID
            int nUserID = atoi(pszIdentifier);
            pTargetPlayer = FindPlayerByUserIDInternal(nUserID);
            if (!pTargetPlayer) {
                Msg("Error: Player with UserID %d not found.\n", nUserID);
                // Cannot ban offline player by UserID, need UniqueID
                Msg("Note: To ban an offline player, use their Discord ID (UniqueID).\n");
                return;
            }
            nTargetUniqueID = GetPlayerUniqueIDInternal(pTargetPlayer);
            if (nTargetUniqueID == 0) {
                Msg("Error: Could not retrieve UniqueID for player with UserID %d.\n", nUserID);
                return;
            }
        }
        else { // Assume UniqueID
            nTargetUniqueID = strtoull(pszIdentifier, nullptr, 10);
            if (nTargetUniqueID == 0) { // Check for parsing error or actual ID 0 (unlikely)
                Msg("Error: Invalid UniqueID format '%s'.\n", pszIdentifier);
                return;
            }
            // Find the player if they are online, for potential kick and event data
            pTargetPlayer = FindPlayerByUniqueIDInternal(nTargetUniqueID);
        }

        // Now we have nTargetUniqueID, proceed with ban logic
        float flCurrentTime = GetCurrentTimeInternal();
        float flExpireTime = (flBanMinutes == 0.0f) ? FLT_MAX : (flCurrentTime + flBanMinutes * 60.0f);

        bool bFound = false;
        for (auto& entry : m_vecIdBans) {
            if (entry.uniqueID == nTargetUniqueID) {
                entry.banTime = flBanMinutes;
                entry.expireTime = flExpireTime;
                Msg("Discord ID %llu ban updated: duration %.2f minutes.\n", nTargetUniqueID, flBanMinutes);
                FireBanEventInternal("server_addban", pTargetPlayer, nTargetUniqueID, nullptr, flBanMinutes, GetCommandIssuerInternal(), false); // Update event
                bFound = true;
                break;
            }
        }

        if (!bFound) {
            m_vecIdBans.push_back({ nTargetUniqueID, flBanMinutes, flExpireTime });
            Msg("Added Discord ID %llu to ban list for %.2f minutes.\n", nTargetUniqueID, flBanMinutes);
            FireBanEventInternal("server_addban", pTargetPlayer, nTargetUniqueID, nullptr, flBanMinutes, GetCommandIssuerInternal(), bKickPlayer && pTargetPlayer); // Add event
        }

        // Kick player if requested and found online
        if (bKickPlayer && pTargetPlayer) {
            Msg("Kicking player %s (ID: %llu) due to ban command.\n", GetPlayerNameInternal(pTargetPlayer), nTargetUniqueID);
            KickPlayerInternal(pTargetPlayer, "Banned by administrator");
        }
        else if (bKickPlayer && !pTargetPlayer) {
            Msg("Note: Player with Discord ID %llu is not currently online, cannot kick.\n", nTargetUniqueID);
        }
        PruneExpiredBans(); // Clean up
        MaybeWriteBans(false); // Write bans if autosave is enabled
    }

    // --- Kick Commands ---
    void KickIDInternal(const CCommand& args) {
        // Usage: kickid <userid | uniqueid> {message}
        if (args.ArgC() < 2) {
            Msg("Usage: kickid <userid | uniqueid> {message}\n");
            return;
        }

        const char* pszIdentifier = args.Arg(1);
        const char* pszReason = (args.ArgC() > 2) ? args.ArgS() + strlen(pszIdentifier) + 1 : "Kicked by administrator"; // Get remainder of string
        // Clean up potential leading space in reason if ArgS includes it
        while (*pszReason == ' ') pszReason++;
        if (!*pszReason) pszReason = "Kicked by administrator";


        IBannablePlayerPointer pTargetPlayer = nullptr;

        bool bIsNumeric = (strspn(pszIdentifier, "0123456789") == strlen(pszIdentifier));
        if (!bIsNumeric) {
            Msg("Error: Invalid UserID or UniqueID format '%s'. Must be a positive integer.\n", pszIdentifier);
            return;
        }

        if (strlen(pszIdentifier) < 8) { // Heuristic for UserID
            int nUserID = atoi(pszIdentifier);
            pTargetPlayer = FindPlayerByUserIDInternal(nUserID);
            if (!pTargetPlayer) {
                Msg("Error: Player with UserID %d not found.\n", nUserID);
                return;
            }
        }
        else { // Assume UniqueID
            uint64_t nUniqueID = strtoull(pszIdentifier, nullptr, 10);
            if (nUniqueID == 0) {
                Msg("Error: Invalid UniqueID format '%s'.\n", pszIdentifier);
                return;
            }
            pTargetPlayer = FindPlayerByUniqueIDInternal(nUniqueID);
            if (!pTargetPlayer) {
                Msg("Error: Player with Discord ID %llu not found.\n", nUniqueID);
                return;
            }
        }

        // If player found by either method
        if (pTargetPlayer) {
            Msg("Kicking player %s (ID: %s) Reason: %s\n", GetPlayerNameInternal(pTargetPlayer), pszIdentifier, pszReason);
            KickPlayerInternal(pTargetPlayer, "%s", pszReason);
        }
        // Error messages handled within the ID type checks above
    }

    void KickInternal(const CCommand& args) {
        // Usage: kick <name>
        if (args.ArgC() != 2) {
            Msg("Usage: kick <name>\n");
            return;
        }

        const char* pszName = args.Arg(1);
        // The CCommand ArgS might be better if name has spaces and quotes were used,
        // but Arg(1) handles the first token which is usually the name.
        // If names can contain spaces, the command issuer must use quotes: kick "Player Name"
        // CCommand should handle the tokenization correctly in that case, giving "Player Name" as Arg(1).

        std::vector<IBannablePlayerPointer> vecMatchingPlayers = FindPlayersByNameInternal(pszName);

        if (vecMatchingPlayers.empty()) {
            Msg("Error: Player matching '%s' not found.\n", pszName);
        }
        else if (vecMatchingPlayers.size() > 1) {
            Msg("Error: Multiple players found matching '%s':\n", pszName);
            for (auto* pPlayer : vecMatchingPlayers) {
                Msg(" - %s (UserID: %d)\n", GetPlayerNameInternal(pPlayer), GetPlayerUserIDInternal(pPlayer));
            }
            Msg("Please use 'kickid <userid>' for clarity.\n");
        }
        else {
            // Exactly one match
            IBannablePlayerPointer pPlayerToKick = vecMatchingPlayers[0];
            Msg("Kicking player %s.\n", GetPlayerNameInternal(pPlayerToKick));
            KickPlayerInternal(pPlayerToKick, "Kicked by administrator");
        }
    }

    // --- Remote Administration ---
    void RemoteAccess_GetUserBanListInternal(CUtlBuffer* pList) {
        if (!pList) {
            return;
        }

        PruneExpiredBans(); // Ensure list is current

        char szLineBuffer[256];

        // Order: Discord IDs first, then IPs
        int nIndex = 1;

        // --- Write Discord ID Bans ---
        for (const auto& entry : m_vecIdBans) {
            int charsWritten = snprintf(szLineBuffer, sizeof(szLineBuffer),
                "%i %llu : %.3f min\n",
                nIndex++,
                entry.uniqueID,
                entry.banTime);
            pList->PutStringWithoutNull(szLineBuffer); // we have to do it this way because the cutlbuffer flags are screwed internally
        }

        // --- Write IP Address Bans ---
        for (const auto& entry : m_vecIpBans) {
            const char* szAddrStr = entry.address.GetAddressString();
            int charsWritten = snprintf(szLineBuffer, sizeof(szLineBuffer),
                "%i %s : %.3f min\n",
                nIndex++,
                szAddrStr,
                entry.banTime);

            pList->PutStringWithoutNull(szLineBuffer);
        }
        pList->PutChar(00);
    }
    // --- Filtering ---
    bool Filter_IsUserBannedInternal(uint64_t nId) {
        PruneExpiredBans(); // Keep list fresh

        for (const auto& entry : m_vecIdBans) {
            if (entry.uniqueID == nId) {
                // Found a match, check if expired (though Prune should have handled it)
                if (entry.expireTime == FLT_MAX || entry.expireTime > GetCurrentTimeInternal()) {
                    return true; // Active ban found
                }
            }
        }
        return false; // No active ban found
    }

    bool Filter_ShouldDiscardInternal(const netadr_t& adr) {
        // Don't ban null or invalid types
        if (adr.GetType() != netadrtype_t::NA_IP && adr.GetType() != netadrtype_t::NA_LOOPBACK) {
            return false;
        }

        PruneExpiredBans();

        for (const auto& entry : m_vecIpBans) {
            // Compare only the address part, ignore port
            if (entry.address.CompareAdr(adr)) {
                // Found a match, check if expired
                if (entry.expireTime == FLT_MAX || entry.expireTime > GetCurrentTimeInternal()) {
                    return true; // Active ban found
                }
            }
        }
        return false; // No active ban found
    }

    // Helper to write bans if autosave is enabled
    void MaybeWriteBans(bool isIpBan) {
        if (m_pSvBanlistAutosave && m_pSvBanlistAutosave->m_Value.m_nValue != 0) {
            if (isIpBan) {
                WriteIPInternal();
            } else {
                WriteIDInternal();
            }
        }
    }

private:

    // --- Virtual Function Offsets (Decimal) ---
    // These seem consistent between DS and Listen based on the disassembly
    uintptr_t VFUNC_OFFSET_ISACTIVE = 256;
    uintptr_t VFUNC_OFFSET_ISCONNECTED = 240;
    uintptr_t VFUNC_OFFSET_ISSPAWNED = 248;
    uintptr_t VFUNC_OFFSET_ISFAKECLIENT = 264;
    uintptr_t VFUNC_OFFSET_GETUSERID = 120;
    uintptr_t VFUNC_OFFSET_GETNAME = 136;
    uintptr_t VFUNC_OFFSET_GETNETCHANNEL = 144;
    uintptr_t VFUNC_OFFSET_DISCONNECT = 104; // Kick

    // NetChannel VFunc Offsets (Decimal)
    uintptr_t VFUNC_OFFSET_NETCHAN_GETADDRESS_DS = 392;
    uintptr_t VFUNC_OFFSET_NETCHAN_GETADDRESS_LISTEN = 440;

    // GameEventManager VFunc Offsets (Decimal)
    uintptr_t VFUNC_OFFSET_EVENTMAN_CREATEEVENT = 48;
    uintptr_t VFUNC_OFFSET_EVENTMAN_FIREEVENT = 56;
    uintptr_t VFUNC_OFFSET_EVENTMAN_SETSTRING = 112;
    uintptr_t VFUNC_OFFSET_EVENTMAN_SETINT = 88;
    uintptr_t VFUNC_OFFSET_EVENTMAN_SETFLOAT = 104; // Assumed standard offset
    uintptr_t VFUNC_OFFSET_EVENTMAN_SETBOOL = 80;
    uintptr_t VFUNC_OFFSET_EVENTMAN_SETUINT64 = 96; // Assumed standard offset

    // --- Global Variable Offsets ---
    // Client List
    uintptr_t OFFSET_CLIENT_COUNT_DS = 0x1C89C34;
    uintptr_t OFFSET_CLIENT_LIST_BASE_DS = 0x1C89C50; // Points to first client's vtable? Adjust based on usage.
    size_t    SIZEOF_CLIENT_DS = 216640;

    uintptr_t OFFSET_CLIENT_COUNT_LISTEN = 0x296632C;
    uintptr_t OFFSET_CLIENT_LIST_BASE_LISTEN = 0x2966348; // Points to first client's vtable? Adjust based on usage.
    size_t    SIZEOF_CLIENT_LISTEN = 285440; // 35680 * 8

    // Discord Auth ID within Client Object
    uintptr_t OFFSET_AUTHID_DS = 0x284;
    uintptr_t OFFSET_AUTHID_LISTEN = 0x2fc;

    // Realtime / Current Time
    // Using Plat_FloatTime() instead of direct memory access for realtime

    // Command Source & Host Client
    uintptr_t OFFSET_CMDSOURCE_DS = 0x20312C0;
    uintptr_t OFFSET_HOSTCLIENT_PTR_DS = 0x203E618; // Pointer to the host client object

    uintptr_t OFFSET_CMDSOURCE_LISTEN = 0x2EB6AC0;
    uintptr_t OFFSET_HOSTCLIENT_PTR_LISTEN = 0x2EB6A18; // Pointer to the host client object

    // Game Event Manager Pointer
    uintptr_t OFFSET_EVENTMANAGER_PTR_DS = 0x0545D28;
    uintptr_t OFFSET_EVENTMANAGER_PTR_LISTEN = 0x07BF558;

    // Helper to call virtual functions by offset
    template <typename RetType, typename... Args>
    RetType CallVFunc(void* pThis, uintptr_t offset, Args... args) {
        if (!pThis) {
            // Handle null this pointer appropriately, maybe return default value for RetType
            return RetType();
        }
        uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(pThis);
        uintptr_t funcAddr = vtable[offset / sizeof(uintptr_t)];
        using FuncType = RetType(*)(void*, Args...);
        return reinterpret_cast<FuncType>(funcAddr)(pThis, args...);
    }

    // Helper to get the base address
    inline uintptr_t GetEngineBase() {
        return IsDedicatedServer() ? G_engine_ds : G_engine;
    }

    // Helper to get client list info
    inline void GetClientListInfo(int& count, uintptr_t& baseAddr, size_t& clientSize) {
        uintptr_t engineBase = GetEngineBase();
        if (IsDedicatedServer()) {
            count = *reinterpret_cast<int*>(engineBase + OFFSET_CLIENT_COUNT_DS);
            // The disassembly uses `v17 = (char *)&unk_1C89C50;` and `v2 = v17 - 8;`
            // This suggests 1C89C50 might be the start of the vtable pointer within the object array.
            // Let's assume the base points to the start of the first object structure.
            // If 1C89C50 is the start of the array of client *objects*:
            baseAddr = engineBase + OFFSET_CLIENT_LIST_BASE_DS;
            // If 1C89C50 is the start of an array of *pointers* to client objects:
            // baseAddr = *reinterpret_cast<uintptr_t*>(engineBase + OFFSET_CLIENT_LIST_BASE_DS); // If it's an array of pointers
            clientSize = SIZEOF_CLIENT_DS;
        }
        else {
            count = *reinterpret_cast<int*>(engineBase + OFFSET_CLIENT_COUNT_LISTEN);
            // Listen server disassembly: `v17 = &qword_2966348;` and `v2 = v17 - 1;`
            // This is less clear, but `v17 += 35680;` implies 35680 is the stride.
            // Let's assume 2966348 is the start of the array of client objects.
            baseAddr = engineBase + OFFSET_CLIENT_LIST_BASE_LISTEN;
            clientSize = SIZEOF_CLIENT_LISTEN;
        }
    }

    // Helper to check if a player is valid for operations (active, connected, etc.)
    inline bool IsPlayerValid(IBannablePlayerPointer pPlayer) {
        if (!pPlayer) return false;
        // Check IsActive, IsConnected, IsSpawned, !IsFakeClient
        bool isActive = true; //CallVFunc<bool>(pPlayer, VFUNC_OFFSET_ISACTIVE);
        bool isConnected = CallVFunc<bool>(pPlayer, VFUNC_OFFSET_ISCONNECTED);
        bool isSpawned = true; //CallVFunc<bool>(pPlayer, VFUNC_OFFSET_ISSPAWNED); probably not a great idea
        bool isFake = false; // CallVFunc<bool>(pPlayer, VFUNC_OFFSET_ISFAKECLIENT);
        return isActive && isConnected && isSpawned && !isFake;
    }

    IBannablePlayerPointer FindPlayerByUserIDInternal(int nUserID) {
        int clientCount = 0;
        uintptr_t clientBaseAddr = 0;
        size_t clientSize = 0;
        GetClientListInfo(clientCount, clientBaseAddr, clientSize);

        if (!clientBaseAddr || clientCount <= 0) {
            return nullptr;
        }

        for (int i = 0; i < clientCount; ++i) {
            // Adjust pointer based on whether clientBaseAddr is array of objects or pointers
            // Assuming array of objects for now based on size calculation:
            IBannablePlayerPointer pPlayer = reinterpret_cast<IBannablePlayerPointer>(clientBaseAddr + i * clientSize);

            // If it's an array of pointers:
            // IBannablePlayerPointer pPlayer = *reinterpret_cast<IBannablePlayerPointer*>(clientBaseAddr + i * sizeof(void*));

            if (IsPlayerValid(pPlayer)) {
                int playerUserID = CallVFunc<int>(pPlayer, VFUNC_OFFSET_GETUSERID);
                if (playerUserID == nUserID) {
                    return pPlayer;
                }
            }
        }
        return nullptr;
    }

    IBannablePlayerPointer FindPlayerByUniqueIDInternal(uint64_t nUniqueID) {
        int clientCount = 0;
        uintptr_t clientBaseAddr = 0;
        size_t clientSize = 0;
        GetClientListInfo(clientCount, clientBaseAddr, clientSize);

        if (!clientBaseAddr || clientCount <= 0) {
            return nullptr;
        }

        uintptr_t authIdOffset = IsDedicatedServer() ? OFFSET_AUTHID_DS : OFFSET_AUTHID_LISTEN;

        for (int i = 0; i < clientCount; ++i) {
            IBannablePlayerPointer pPlayer = reinterpret_cast<IBannablePlayerPointer>(clientBaseAddr + i * clientSize);
            // Adjust if array of pointers:
            // IBannablePlayerPointer pPlayer = *reinterpret_cast<IBannablePlayerPointer*>(clientBaseAddr + i * sizeof(void*));

            if (IsPlayerValid(pPlayer)) {
                uint64_t playerUniqueID = *reinterpret_cast<uint64_t*>((uintptr_t)pPlayer + authIdOffset);
                if (playerUniqueID == nUniqueID) {
                    return pPlayer;
                }
            }
        }
        return nullptr;
    }

    std::vector<IBannablePlayerPointer> FindPlayersByNameInternal(const char* pszNameSubstring) {
        std::vector<IBannablePlayerPointer> foundPlayers;
        if (!pszNameSubstring || *pszNameSubstring == '\0') {
            return foundPlayers;
        }

        int clientCount = 0;
        uintptr_t clientBaseAddr = 0;
        size_t clientSize = 0;
        GetClientListInfo(clientCount, clientBaseAddr, clientSize);

        if (!clientBaseAddr || clientCount <= 0) {
            return foundPlayers;
        }

        for (int i = 0; i < clientCount; ++i) {
            IBannablePlayerPointer pPlayer = reinterpret_cast<IBannablePlayerPointer>(clientBaseAddr + i * clientSize);
            // Adjust if array of pointers:
            // IBannablePlayerPointer pPlayer = *reinterpret_cast<IBannablePlayerPointer*>(clientBaseAddr + i * sizeof(void*));

            if (IsPlayerValid(pPlayer)) {
                const char* playerName = CallVFunc<const char*>(pPlayer, VFUNC_OFFSET_GETNAME);
                if (playerName && V_stristr(playerName, pszNameSubstring)) {
                    foundPlayers.push_back(pPlayer);
                }
            }
        }
        return foundPlayers;
    }

    uint64_t GetPlayerUniqueIDInternal(IBannablePlayerPointer pPlayer) {
        if (!pPlayer) return 0;
        uintptr_t authIdOffset = IsDedicatedServer() ? OFFSET_AUTHID_DS : OFFSET_AUTHID_LISTEN;
        return *reinterpret_cast<uint64_t*>((uintptr_t)pPlayer + authIdOffset);
    }

    const netadr_t& GetPlayerNetAddressInternal(IBannablePlayerPointer pPlayer) {
        static netadr_t s_dummyAddr; // Return a zeroed address on failure
        if (!pPlayer) {
            return s_dummyAddr;
        }

        void* pNetChannel = CallVFunc<void*>(pPlayer, VFUNC_OFFSET_GETNETCHANNEL);
        if (!pNetChannel) {
            return s_dummyAddr;
        }

        uintptr_t getAddrOffset = 8;
        char* pAddr = CallVFunc<char*>(pNetChannel, getAddrOffset);

        if (!pAddr) {
            return s_dummyAddr;
        }
        return netadr_t(pAddr); // Return by reference
    }

    const char* GetPlayerNameInternal(IBannablePlayerPointer pPlayer) {
        if (!pPlayer) {
            return ""; // Or "Unknown Player"
        }
        return CallVFunc<const char*>(pPlayer, VFUNC_OFFSET_GETNAME);
    }

    int GetPlayerUserIDInternal(IBannablePlayerPointer pPlayer) {
        if (!pPlayer) {
            return -1;
        }
        return CallVFunc<int>(pPlayer, VFUNC_OFFSET_GETUSERID);
    }

    void KickPlayerInternal(IBannablePlayerPointer pPlayer, const char* pszReasonFormat, ...) {
        if (!pPlayer) return;

        char szReasonBuffer[256];
        va_list args;
        va_start(args, pszReasonFormat);
        vsnprintf(szReasonBuffer, sizeof(szReasonBuffer), pszReasonFormat, args);
        va_end(args);
        szReasonBuffer[sizeof(szReasonBuffer) - 1] = '\0'; // Ensure null termination

        CallVFunc<void>(pPlayer, VFUNC_OFFSET_DISCONNECT, 1, szReasonBuffer);
    }

    std::vector<IBannablePlayerPointer> GetAllActivePlayersInternal() {
        std::vector<IBannablePlayerPointer> activePlayers;
        int clientCount = 0;
        uintptr_t clientBaseAddr = 0;
        size_t clientSize = 0;
        GetClientListInfo(clientCount, clientBaseAddr, clientSize);

        if (!clientBaseAddr || clientCount <= 0) {
            return activePlayers;
        }

        for (int i = 0; i < clientCount; ++i) {
            IBannablePlayerPointer pPlayer = reinterpret_cast<IBannablePlayerPointer>(clientBaseAddr + i * clientSize);
            // Adjust if array of pointers:
            // IBannablePlayerPointer pPlayer = *reinterpret_cast<IBannablePlayerPointer*>(clientBaseAddr + i * sizeof(void*));

            if (IsPlayerValid(pPlayer)) {
                activePlayers.push_back(pPlayer);
            }
        }
        return activePlayers;
    }

    float GetCurrentTimeInternal() const {
        // Use the standard engine function as suggested
        return Plat_FloatTime();
    }

    void FireBanEventInternal(const char* pszEventName, IBannablePlayerPointer pSubject, uint64_t nBannedId, const char* pszBannedIpStr, float flDuration, IBannablePlayerPointer pAdmin, bool bWasKicked) {
        uintptr_t engineBase = GetEngineBase();
        uintptr_t eventManagerPtrAddr = engineBase + (IsDedicatedServer() ? OFFSET_EVENTMANAGER_PTR_DS : OFFSET_EVENTMANAGER_PTR_LISTEN);
        void* pEventManager = *reinterpret_cast<void**>(eventManagerPtrAddr);

        if (!pEventManager || !pszEventName) {
            // Log error: Event manager not found or event name is null
            return;
        }

        // IGameEvent *event = CreateEvent(name, force, server_only)
        // Let's assume force=false (0), server_only=false (0) based on disassembly usage
        void* pEvent = CallVFunc<void*>(pEventManager, VFUNC_OFFSET_EVENTMAN_CREATEEVENT, pszEventName, 0, 0);

        if (!pEvent) {
            // Log error: Failed to create event
            return;
        }

        // Populate event fields
        if (pSubject) {
            CallVFunc<void>(pEvent, VFUNC_OFFSET_EVENTMAN_SETINT, "userid", GetPlayerUserIDInternal(pSubject));
            CallVFunc<void>(pEvent, VFUNC_OFFSET_EVENTMAN_SETSTRING, "name", GetPlayerNameInternal(pSubject));
        }
        else {
            CallVFunc<void>(pEvent, VFUNC_OFFSET_EVENTMAN_SETINT, "userid", 0);
            CallVFunc<void>(pEvent, VFUNC_OFFSET_EVENTMAN_SETSTRING, "name", ""); // Use empty string for no subject
        }

        if (nBannedId > 0) {
            // Assuming SetUint64 exists at standard offset 96 (0x60)
            CallVFunc<void>(pEvent, VFUNC_OFFSET_EVENTMAN_SETUINT64, "uniqueid", nBannedId);
        }
        else {
            CallVFunc<void>(pEvent, VFUNC_OFFSET_EVENTMAN_SETUINT64, "uniqueid", (uint64_t)0);
        }

        if (pszBannedIpStr && *pszBannedIpStr) {
            CallVFunc<void>(pEvent, VFUNC_OFFSET_EVENTMAN_SETSTRING, "ip", pszBannedIpStr);
        }
        else {
            CallVFunc<void>(pEvent, VFUNC_OFFSET_EVENTMAN_SETSTRING, "ip", "");
        }

        // Use SetFloat for duration (assuming standard offset 104 / 0x68)
        CallVFunc<void>(pEvent, VFUNC_OFFSET_EVENTMAN_SETFLOAT, "duration", flDuration);

        if (pAdmin) {
            CallVFunc<void>(pEvent, VFUNC_OFFSET_EVENTMAN_SETINT, "adminid", GetPlayerUserIDInternal(pAdmin));
            CallVFunc<void>(pEvent, VFUNC_OFFSET_EVENTMAN_SETSTRING, "adminname", GetPlayerNameInternal(pAdmin));
        }
        else {
            CallVFunc<void>(pEvent, VFUNC_OFFSET_EVENTMAN_SETINT, "adminid", 0); // 0 for console
            CallVFunc<void>(pEvent, VFUNC_OFFSET_EVENTMAN_SETSTRING, "adminname", "Console");
        }

        CallVFunc<void>(pEvent, VFUNC_OFFSET_EVENTMAN_SETBOOL, "kicked", bWasKicked);

        // FireEvent(event, server_only)
        // Disassembly shows 0LL, so server_only = false
        CallVFunc<void>(pEventManager, VFUNC_OFFSET_EVENTMAN_FIREEVENT, pEvent, 0);

        // Note: The engine usually handles deleting the event after firing.
    }

    IBannablePlayerPointer GetCommandIssuerInternal() {
        uintptr_t engineBase = GetEngineBase();
        uintptr_t cmdSourceAddr = engineBase + (IsDedicatedServer() ? OFFSET_CMDSOURCE_DS : OFFSET_CMDSOURCE_LISTEN);
        int cmdSourceValue = *reinterpret_cast<int*>(cmdSourceAddr);

        // Assuming 1 corresponds to src_command (console) based on disassembly
        if (cmdSourceValue == 1) {
            return nullptr; // Console issued command
        }
        else {
            // Command came from a client (likely host_client on listen, or RCON which isn't handled here)
            uintptr_t hostClientPtrAddr = engineBase + (IsDedicatedServer() ? OFFSET_HOSTCLIENT_PTR_DS : OFFSET_HOSTCLIENT_PTR_LISTEN);
            IBannablePlayerPointer pHostClient = *reinterpret_cast<IBannablePlayerPointer*>(hostClientPtrAddr);
            return pHostClient; // Return the host client pointer
        }
    }


public:
    // --- Static Interface --- (Connects public static calls to internal singleton methods)

    static void addip(const CCommand& args) { GetInstance().AddIPInternal(args); }
    static void removeip(const CCommand& args) { GetInstance().RemoveIPInternal(args); }
    static void listip(const CCommand& args) { GetInstance().ListIPInternal(args); }
    static void writeip() { GetInstance().WriteIPInternal(); }
    static void writeid() { GetInstance().WriteIDInternal(); }
    static void removeallids(const CCommand& args) { GetInstance().RemoveAllIDsInternal(args); }
    static void removeallips(const CCommand& args) { GetInstance().RemoveAllIPsInternal(args); }
    static void removeid(const CCommand& args) { GetInstance().RemoveIDInternal(args); }
    static void listid(const CCommand& args) { GetInstance().ListIDInternal(args); }
    static void banid(const CCommand& args) { GetInstance().BanIDInternal(args); }
    static void kickid(const CCommand& args) { GetInstance().KickIDInternal(args); }
    static void kick(const CCommand& args) { GetInstance().KickInternal(args); }
    static void RemoteAccess_GetUserBanList(void* pUnusedThisptr, CUtlBuffer* pList) { GetInstance().RemoteAccess_GetUserBanListInternal(pList); }
    static bool Filter_IsUserBanned(uint64_t nId) { return GetInstance().Filter_IsUserBannedInternal(nId); }
    static bool Filter_ShouldDiscard(const netadr_t& adr) { return GetInstance().Filter_ShouldDiscardInternal(adr); }
};
