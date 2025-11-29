// Authentication system for R1Delta
#pragma once

#include "core.h"
#include "factory.h"
#include "r1d_version.h"
#include "persistentdata.h"
#include <string>

struct AuthResponse {
    bool success = false;
    char failureReason[256];
    char discordName[32];
    char pomeloName[32];
    int64 discordId = 0;
};

typedef struct USERID_s
{
    int idtype;
    int64_t snowflake;
} USERID_t;

// Public IP cache
extern std::string G_public_ip;

// Auth callbacks
void Shared_OnLocalAuthFailure();
std::string get_public_ip();
AuthResponse Server_AuthCallback(bool loopback, const char* serverIP, const char* token);

// Hook function declarations
extern bool (*oCBaseClientConnect)(
    __int64 a1,
    _BYTE* a2,
    int a3,
    __int64 a4,
    char a5,
    int a6,
    CUtlVector<NetMessageCvar_t>* conVars,
    char* a8,
    int a9);

extern __int64 (*oCBaseStateClientConnect)(
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
    int a11);

bool __fastcall HookedCBaseClientConnect(
    __int64 a1,
    _BYTE* a2,
    int a3,
    __int64 a4,
    char bFakePlayer,
    int a6,
    CUtlVector<NetMessageCvar_t>* conVars,
    char* a8,
    int a9);

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
    int a11);

// User ID hooks
typedef char* (*GetUserIDString_t)(void* id);
extern GetUserIDString_t GetUserIDStringOriginal;

typedef USERID_s* (*GetUserID_t)(__int64 base_client, USERID_s* id);
extern GetUserID_t GetUserIDOriginal;

USERID_s* GetUserIDHook(__int64 base_client, USERID_s* id);
const char* GetUserIDStringHook(USERID_s* id);

// SendConnectPacket hook
typedef __int64(__fastcall* CBaseClientState_SendConnectPacket_t)(__int64 a1, __int64 a2, unsigned int a3);
extern CBaseClientState_SendConnectPacket_t CBaseClientState_SendConnectPacket_Original;
int64 CBaseClientState_SendConnectPacket(__int64 a1, __int64 a2, unsigned int a3);
