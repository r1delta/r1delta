#pragma once
#include <vector>
extern __int64 (*oWeaponXRegisterClient)(__int64 a1, const char** a2);
extern __int64 (*oWeaponXRegisterServer)(__int64 a1, const char** a2);
__int64 __fastcall WeaponXRegisterClient(__int64 a1, const char** a2);
__int64 __fastcall WeaponXRegisterServer(__int64 a1, const char** a2);
extern void* (*mp_weapon_wingman_ctor_orig)();
extern void*  (*mp_weapon_wingman_dtor_orig)(void* entity, char a2);
void*          mp_weapon_wingman_ctor_hk(void* factory);
void* __fastcall mp_weapon_wingman_dtor_hk(void* entity, char a2);
struct BreakpointInfo {
    void* address;
    void* entity;
};

extern std::vector<BreakpointInfo> g_activeBreakpoints;
