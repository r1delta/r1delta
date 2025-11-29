// Authentication system for R1Delta
// Handles Discord auth, JWT validation, master server tokens, and user ID management

#include "auth.h"
#include "core.h"
#include "load.h"
#include "logging.h"
#include "cvar.h"
#include "defs.h"
#include "sv_filter.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <random>
#include <ctime>

#ifdef JWT
#include <l8w8jwt/decode.h>
#include "l8w8jwt/encode.h"
#include "jwt_compact.h"
#endif

static const std::string ecdsa_pub_key = "-----BEGIN PUBLIC KEY-----\n"
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEPc1fjPhVLc/prdKT5ku5xNQ9mM3v\n"
"9FHTsnwhx2tPbmVNB0TAJyKNnWVf993HPtb5+W/JAJtJCFg0txDyBBHONg==\n"
"-----END PUBLIC KEY-----\n";

// Global public IP cache
std::string G_public_ip;

namespace {
    std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\n\r");
        return (start == std::string::npos) ? "" : s.substr(start, s.find_last_not_of(" \t\n\r") - start + 1);
    }

    bool is_valid_ipv4(const std::string& ip) {
        std::istringstream iss(trim(ip));
        std::string part;
        int count = 0;
        while (std::getline(iss, part, '.')) {
            if (++count > 4 || part.empty() || part.size() > 3) return false;
            for (char c : part) if (!std::isdigit(c)) return false;
            int num = std::stoi(part);
            if (num < 0 || num > 255) return false;
        }
        return count == 4;
    }
}

void Shared_OnLocalAuthFailure() {
    Warning("%s", "Invalid auth token!\n");
    Cbuf_AddText(0, "disconnect \"Invalid auth token\";delta_start_discord_auth", 0);
}

// Helper function to get the server's public IP.
std::string get_public_ip() {
    static std::string cached_ip = []() -> std::string {
        const char* hosts[] = { "checkip.amazonaws.com", "eth0.me", "api.ipify.org" };
        for (const char* host : hosts) {
            httplib::Client client(host);
            client.set_read_timeout(1, 0);
            if (auto res = client.Get("/")) {
                std::string ip = res->body;
                // Trim whitespace.
                ip.erase(0, ip.find_first_not_of(" \t\n\r"));
                ip.erase(ip.find_last_not_of(" \t\n\r") + 1);
                if (res->status == 200 && !ip.empty())
                    return ip;
            }
        }
        return std::string("0.0.0.0");
    }();
    return cached_ip;
}

// Server_AuthCallback verifies the server auth token.
AuthResponse Server_AuthCallback(bool loopback, const char* serverIP, const char* token) {
#ifdef JWT
    AuthResponse response;
    if (token == nullptr) {
        response.success = false;
        strncpy(response.failureReason, "No auth token provided", sizeof(response.failureReason));
        return response;
    }
    if (strlen(token) < 10) {
        response.success = false;
        strncpy(response.failureReason, "Invalid auth token", sizeof(response.failureReason));
        return response;
    }
    try {
        struct l8w8jwt_decoding_params params;
        l8w8jwt_decoding_params_init(&params);
        params.alg = L8W8JWT_ALG_ES256;
        params.jwt = (char*)token;
        params.jwt_length = strlen(token);
        params.verification_key = (unsigned char*)ecdsa_pub_key.c_str();
        params.verification_key_length = ecdsa_pub_key.length();
        params.validate_exp = 0;
        params.exp_tolerance_seconds = 60;

        enum l8w8jwt_validation_result validation_result;
        l8w8jwt_claim* claims;
        size_t claims_count;
        int decode_result = l8w8jwt_decode(&params, &validation_result, &claims, &claims_count);

        if (decode_result == L8W8JWT_SUCCESS && validation_result == L8W8JWT_VALID)
        {
            Msg("Token valid\n");
        }
        else
        {
            printf("\n Example HS512 token validation failed! \n");
            response.success = false;
            strncpy(response.failureReason, "Token is invalid", sizeof(response.failureReason));
            return response;
        }

        auto dn = l8w8jwt_get_claim(claims, claims_count, "dn", strlen("dn"));
        auto pn = l8w8jwt_get_claim(claims, claims_count, "p", strlen("p"));
        auto di = l8w8jwt_get_claim(claims, claims_count, "di", strlen("di"));
        auto s = l8w8jwt_get_claim(claims, claims_count, "s", strlen("s"));
        // Extract minimal claims.
        std::string displayName = dn ? std::string((char*)dn->value, dn->value_length) : "";
        std::string pomeloName = pn ? std::string((char*)pn->value, pn->value_length) : "";
        std::string id = di ? std::string((char*)di->value, di->value_length) : "";
        // Extra check: the token's server_ip must match exactly.
        std::string tokenServerIP = s ? std::string((char*)s->value, s->value_length) : "";

        Msg("Token server IP: %s, expected: %s\n", tokenServerIP.c_str(), serverIP);
        response.success = true;
        strncpy(response.discordName, displayName.c_str(), sizeof(response.discordName) - 1);
        response.discordName[sizeof(response.discordName) - 1] = '\0';
        strncpy(response.pomeloName, pomeloName.c_str(), sizeof(response.pomeloName) - 1);
        response.pomeloName[sizeof(response.pomeloName) - 1] = '\0';
        response.discordId = std::stoll(id);
        return response;
    }
    catch (const std::exception& e) {
        response.success = false;
        strncpy(response.failureReason, e.what(), sizeof(response.failureReason) - 1);
        response.failureReason[sizeof(response.failureReason) - 1] = '\0';
        return response;
    }
#else
    AuthResponse response;
    response.success = false;
    strncpy(response.failureReason, "Discord support not enabled", sizeof(response.failureReason));
    return response;
#endif
}

// Original function pointers
bool (*oCBaseClientConnect)(
    __int64 a1,
    _BYTE* a2,
    int a3,
    __int64 a4,
    char a5,
    int a6,
    CUtlVector<NetMessageCvar_t>* conVars,
    char* a8,
    int a9) = nullptr;

__int64 (*oCBaseStateClientConnect)(
    __int64 a1,
    const char* public_ip,
    const char* private_ip,
    int num_players,
    char a5,
    int a6,
    _BYTE* a7,
    int a8,
    const char* a9,
    __int64* a10,
    int a11) = nullptr;

typedef char* (*GetUserIDString_t)(void* id);
GetUserIDString_t GetUserIDStringOriginal = nullptr;

typedef USERID_s* (*GetUserID_t)(__int64 base_client, USERID_s* id);
GetUserID_t GetUserIDOriginal = nullptr;

// Hooked client connection function.
bool __fastcall HookedCBaseClientConnect(
    __int64 a1,
    _BYTE* a2,
    int a3,
    __int64 a4,
    char bFakePlayer,
    int a6,
    CUtlVector<NetMessageCvar_t>* conVars,
    char* a8,
    int a9)
{
    if (bFakePlayer)
        return oCBaseClientConnect(a1, a2, a3, a4, bFakePlayer, a6, conVars, a8, a9);

    // Version check
    static auto skipVersionCheck = OriginalCCVar_FindVar(cvarinterface, "delta_skip_version_check");
    if (skipVersionCheck->m_Value.m_nValue == 0) {
        const char* serverVersion = R1D_VERSION;
        const char* clientVersion = nullptr;

        // Find client's delta_version from conVars
        if (conVars) {
            for (int i = 0; i < conVars->Count(); i++) {
                NetMessageCvar_t& var = conVars->Element(i);
                if (strcmp(var.name, "delta_version") == 0) {
                    clientVersion = var.value;
                    break;
                }
            }
        }

        // If client didn't send version, reject
        if (!clientVersion) {
            V_snprintf(a8, a9, "Client is missing delta_version. Please update R1Delta.");
            return false;
        }

        // Check version compatibility
        bool serverIsDev = (strcmp(serverVersion, "dev") == 0);
        bool clientIsDev = (strcmp(clientVersion, "dev") == 0);

        if (serverIsDev || clientIsDev) {
            // DEV servers accept all clients, DEV clients can connect to all servers
            // Allow connection
        } else {
            // Parse versions as major.minor.hotfix (with optional "v" prefix)
            int serverMajor = 0, serverMinor = 0, serverHotfix = 0;
            int clientMajor = 0, clientMinor = 0, clientHotfix = 0;

            // Try parsing with "v" prefix first, then without
            int serverParsed = sscanf(serverVersion, "v%d.%d.%d", &serverMajor, &serverMinor, &serverHotfix);
            if (serverParsed < 2) {
                serverParsed = sscanf(serverVersion, "%d.%d.%d", &serverMajor, &serverMinor, &serverHotfix);
            }

            int clientParsed = sscanf(clientVersion, "v%d.%d.%d", &clientMajor, &clientMinor, &clientHotfix);
            if (clientParsed < 2) {
                clientParsed = sscanf(clientVersion, "%d.%d.%d", &clientMajor, &clientMinor, &clientHotfix);
            }

            // Require major.minor to match exactly, allow client hotfix >= server hotfix
            if (serverParsed >= 2 && clientParsed >= 2) {
                if (serverMajor != clientMajor || serverMinor != clientMinor) {
                    V_snprintf(a8, a9, "Version mismatch: Server is running R1Delta %s, you have %s.", serverVersion, clientVersion);
                    return false;
                }
                if (clientHotfix < serverHotfix) {
                    V_snprintf(a8, a9, "Version mismatch: Server is running R1Delta %s, you have %s. Please update your client.", serverVersion, clientVersion);
                    return false;
                }
            } else if (strcmp(serverVersion, clientVersion) != 0) {
                // Fallback to exact string match for non-standard versions
                V_snprintf(a8, a9, "Version mismatch: Server is running R1Delta %s, you have %s.", serverVersion, clientVersion);
                return false;
            }
        }
    }

    static auto bUseOnlineAuth = OriginalCCVar_FindVar(cvarinterface, "delta_online_auth_enable");
    static auto iSyncUsernameWithDiscord = OriginalCCVar_FindVar(cvarinterface, "delta_discord_username_sync");
    static auto iHostPort = OriginalCCVar_FindVar(cvarinterface, "hostport");

    if (bUseOnlineAuth->m_Value.m_nValue != 1)
        return oCBaseClientConnect(a1, a2, a3, a4, bFakePlayer, a6, conVars, a8, a9);

    // Determine if this is a loopback connection.
    bool bIsLoopback = false;
    typedef const char* (__fastcall* GetClientIPFn)(__int64);
    GetClientIPFn GetClientIP = reinterpret_cast<GetClientIPFn>((*((uintptr_t**)a4))[1]);
    const char* clientIP = GetClientIP(a4);
    bIsLoopback = (std::string(clientIP) == std::string("loopback") || std::string(clientIP).starts_with("[::1]")) && !IsDedicatedServer();
    bool allow = false;
    if (conVars) {
        for (int i = 0; i < conVars->Count(); i++) {
            NetMessageCvar_t& var = conVars->Element(i);
            if (strcmp(var.name, "delta_server_auth_token") == 0) {
                allow = true;

                std::string actualServerIP = get_public_ip() + ":" + std::to_string(iHostPort->m_Value.m_nValue);

                #ifdef JWT
                AuthResponse res = Server_AuthCallback(bIsLoopback, actualServerIP.c_str(), jwt_compact::expandJWT(var.value).c_str());
                #else
                AuthResponse res = Server_AuthCallback(bIsLoopback, actualServerIP.c_str(), var.value);
                #endif
                if (CBanSystem::Filter_IsUserBanned(res.discordId)) {
                    V_snprintf(a8, a9, "%s", "Discord UserID not allowed to join this server.");
                    return false;
                }

                if (!res.success && !bIsLoopback) {
                    V_snprintf(a8, a9, "%s", res.failureReason);
                    return false;
                }
                if (IsDedicatedServer()) {
                    // a1 is 8 bytes ahead on dedicated servers
                    *(int64*)(a1 + 0x284) = res.discordId;
                }
                else
                    *(int64*)(a1 + 0x2fc) = res.discordId;

                if (res.success && iSyncUsernameWithDiscord->m_Value.m_nValue != 0) {
                    const char* newName = (iSyncUsernameWithDiscord->m_Value.m_nValue == 1) ? res.discordName : res.pomeloName;
                    // Update the client's "name" convar.
                    a2 = (_BYTE*)(newName);
                    for (int j = 0; j < conVars->Count(); j++) {
                        NetMessageCvar_t& innerVar = conVars->Element(j);
                        if (strcmp(innerVar.name, "name") == 0) {
                            conVars->Remove(j);
                            break;
                        }
                    }
                    NetMessageCvar_t newNameVar;
                    strncpy(newNameVar.name, "name", sizeof(newNameVar.name));
                    strncpy(newNameVar.value, newName, sizeof(newNameVar.value));
                    conVars->AddToTail(newNameVar);
                }
            }
        }
    }
    if (!allow && !bIsLoopback) {
        V_snprintf(a8, a9, "%s", "Invalid ConVars in CBaseClient::Connect.");
        return false;
    }
    return oCBaseClientConnect(a1, a2, a3, a4, bFakePlayer, a6, conVars, a8, a9);
}

// Hooked state client connection function.
__int64 __fastcall HookedCBaseStateClientConnect(
    __int64 a1,
    const char* public_ip,
    const char* private_ip,
    int num_players,
    char a5,
    int a6,
    _BYTE* a7,
    int a8,
    const char* a9,
    __int64* a10,
    int a11)
{
    static auto bUseOnlineAuth = OriginalCCVar_FindVar(cvarinterface, "delta_online_auth_enable");

    if (bUseOnlineAuth->m_Value.m_nValue != 1)
        return oCBaseStateClientConnect(a1, public_ip, private_ip, num_players, a5, a6, a7, a8, a9, a10, a11);

    // Obtain the master server URL from the engine convars.
    auto ms_url = OriginalCCVar_FindVar(cvarinterface, "delta_ms_url")->m_Value.m_pszString;
    httplib::Client cli(ms_url);
    cli.set_connection_timeout(2, 0);
    cli.set_follow_location(true);

    // Use the persistent master server auth token.
    auto persistentToken = OriginalCCVar_FindVar(cvarinterface, "delta_persistent_master_auth_token")->m_Value.m_pszString;
    cli.set_bearer_token_auth(persistentToken);

    // Prepare a JSON payload with the server's public IP.
    nlohmann::json j;
    j["ip"] = public_ip;

    auto result = cli.Post("/server-token", j.dump(), "application/json");
    auto var = OriginalCCVar_FindVar(cvarinterface, "delta_server_auth_token");
    if (result && result->status == 200) {
        try {
            #ifdef JWT
            auto jsonResponse = nlohmann::json::parse(result->body);
            std::string token = jsonResponse["token"].get<std::string>();
            // Update the server auth token convar.
            SetConvarStringOriginal(var, jwt_compact::compactJWT(token).c_str());
            #endif
        }
        catch (const std::exception& e) {
            Warning("Failed to parse token from master server: %s\n", e.what());
        }
    }
    else {
        if (result) {
            Warning("Failed to get token from master server: %s\n", result->body.c_str());
        }
        else {
            Warning("Failed to get token from master server. Error: %d\n", static_cast<int>(result.error()));
        }
    }
    cli.stop();
    return oCBaseStateClientConnect(a1, public_ip, private_ip, num_players, a5, a6, a7, a8, a9, a10, a11);
}

USERID_s* GetUserIDHook(__int64 base_client, USERID_s* id) {
    id->idtype = 1; // Set idtype to 1 for USERID_TYPE_DISCORD
    id->snowflake = 1; // Initialize snowflake to 0
    if (base_client == 0) {
        return id;
    }
    auto var = OriginalCCVar_FindVar(cvarinterface, "delta_online_auth_enable");
    if (var->m_Value.m_nValue != 1) {
        if (IsDedicatedServer()) {
            id->snowflake = *(int64_t*)(base_client + 0x34B70);
        }
        else {
            id->snowflake = *(int64_t*)(base_client + 0x45810);
        }
        return id;
    }
    if (IsDedicatedServer())
        id->snowflake = *(int64_t*)(base_client + 0x284); // Get the snowflake from the base client
    else
        id->snowflake = *(int64_t*)(base_client + 0x2FC); // Get the snowflake from the base client
    return id;
}

const char* GetUserIDStringHook(USERID_s* id) {
    static char buffer[256];
    if (id->snowflake == 1) {
        sprintf(buffer, "%s", "UNKNOWN");
        return buffer;
    }
    sprintf(buffer, "%lld", id->snowflake);
    return buffer;
}

// CBaseClientState::SendConnectPacket hook
// Generates a random platform_user_id if none is set
typedef __int64(__fastcall* CBaseClientState_SendConnectPacket_t)(__int64 a1, __int64 a2, unsigned int a3);
CBaseClientState_SendConnectPacket_t CBaseClientState_SendConnectPacket_Original = nullptr;

int64 CBaseClientState_SendConnectPacket(__int64 a1, __int64 a2, unsigned int a3) {
    static auto id = OriginalCCVar_FindVar(cvarinterface, "platform_user_id");
    if (id->m_Value.m_nValue == 0) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(0, 99999);
        auto randNum = dist(gen);
        id->m_Value.m_nValue = randNum;
    }
    return CBaseClientState_SendConnectPacket_Original(a1, a2, a3);
}
