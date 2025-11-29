// Precache system hooks for R1Delta
// Handles model precaching, militia body variants, etc.

#include "precache.h"
#include "core.h"
#include "load.h"
#include "logging.h"
#include "cvar.h"
#include "defs.h"

#include <array>
#include <cstring>

// String structure for militia body storage
struct string_nodebug {
    union {
        char inl[16]{ 0 };
        const char* ptr;
    };
    size_t size = 0;
    size_t big_size = 0;
};
static_assert(sizeof(string_nodebug) == 4 * 8, "string_nodebug size mismatch");

// Storage for militia body model paths
std::array<string_nodebug[2], 100> g_militia_bodies;

// Original function pointers
void* SetPreCache_o = nullptr;

__int64 __fastcall SetPreCache(__int64 a1, __int64 a2, char a3) {
    auto ret = reinterpret_cast<decltype(&SetPreCache)>(SetPreCache_o)(a1, a2, a3);

    using sub_1800F5680_t = char(__fastcall*)(const char* a1, __int64 a2, void* a3, void* a4);
    static auto sub_1800F5680 = sub_1800F5680_t(uintptr_t(GetModuleHandleA("engine_r1o.dll")) + 0xF5680);

    static auto array_start = uintptr_t(GetModuleHandleA("engine_r1o.dll")) + 0x2555C00;

    auto idx = (a1 - array_start) / 10064;
    // assert that no mod and no more than 100...
    auto elem = &g_militia_bodies[idx];
    auto militia_exists = sub_1800F5680("bodymodel_militia", a2, &elem[0]->ptr, &elem[0]->big_size);

    if (!*(_QWORD*)(a1 + 488))
        sub_1800F5680("armsmodel_imc", a2, PVOID(a1 + 472), PVOID(a1 + 488));

    auto armsmodel_militia_exists = sub_1800F5680("armsmodel_militia", a2, &elem[1]->ptr, &elem[1]->big_size);
    if (OriginalCCVar_FindVar(cvarinterface, "developer")->m_Value.m_nValue == 1)
        Cbuf_AddText(0, "developer 1\n", 0);

    return ret;
}

void CHL2_Player_Precache(uintptr_t a1, uintptr_t a2) {
    auto server_mod = G_server;
    auto byte_180C318A6 = (uint8_t*)(server_mod + 0xC318A6);

    if (*byte_180C318A6) {

        using sub_1804FE8B0_t = uintptr_t(__fastcall*)(uintptr_t, uintptr_t);
        auto sub_1804FE8B0 = sub_1804FE8B0_t(server_mod + 0x4FE8B0);

        auto EffectDispatch_ptr = (uintptr_t*)(server_mod + 0xC310C8);
        auto EffectDispatch = *EffectDispatch_ptr;

        sub_1804FE8B0(a1, a2);

        (*(void(__fastcall**)(__int64, __int64, const char*, __int64, _QWORD))(*(_QWORD*)EffectDispatch + 64i64))(
            EffectDispatch,
            1,
            "waterripple",
            0xFFFFFFFFi64,
            0i64);
        (*(void(__fastcall**)(__int64, __int64, const char*, __int64, _QWORD))(*(_QWORD*)EffectDispatch + 64i64))(
            EffectDispatch,
            1,
            "watersplash",
            0xFFFFFFFFi64,
            0i64);

        auto StaticClassSystem001_ptr = (uintptr_t*)(server_mod + 0xC31000);
        auto StaticClassSystem001 = *StaticClassSystem001_ptr;
        using PrecacheModel_t = uintptr_t(__fastcall*)(const void*);
        auto PrecacheModel = PrecacheModel_t(server_mod + 0x3B6A40);

        // Precache weapon arm models
        PrecacheModel("models/weapons/arms/atlaspov.mdl");
        PrecacheModel("models/weapons/arms/atlaspov_cockpit2.mdl");
        PrecacheModel("models/weapons/arms/human_pov_cockpit.mdl");
        PrecacheModel("models/weapons/arms/ogrepov.mdl");
        PrecacheModel("models/weapons/arms/ogrepov_cockpit.mdl");
        PrecacheModel("models/weapons/arms/petepov_workspace.mdl");
        PrecacheModel("models/weapons/arms/pov_imc_pilot_male_br.mdl");
        PrecacheModel("models/weapons/arms/pov_imc_pilot_male_cq.mdl");
        PrecacheModel("models/weapons/arms/pov_imc_pilot_male_dm.mdl");
        PrecacheModel("models/weapons/arms/pov_imc_spectre.mdl");
        PrecacheModel("models/weapons/arms/pov_male_anims.mdl");
        PrecacheModel("models/weapons/arms/pov_mcor_pilot_male_br.mdl");
        PrecacheModel("models/weapons/arms/pov_mcor_pilot_male_cq.mdl");
        PrecacheModel("models/weapons/arms/pov_mcor_pilot_male_dm.mdl");
        PrecacheModel("models/weapons/arms/pov_mcor_spectre_assault.mdl");
        PrecacheModel("models/weapons/arms/pov_pete_core.mdl");
        PrecacheModel("models/weapons/arms/pov_pilot_female_br.mdl");
        PrecacheModel("models/weapons/arms/pov_pilot_female_cq.mdl");
        PrecacheModel("models/weapons/arms/pov_pilot_female_dm.mdl");
        PrecacheModel("models/weapons/arms/stryderpov.mdl");
        PrecacheModel("models/weapons/arms/stryderpov_cockpit.mdl");

        // Precache militia body variants
        for (size_t i = 0; i < 100; i++) {
            auto v5 = (*(__int64(__fastcall**)(__int64, _QWORD))(*(_QWORD*)StaticClassSystem001 + 24i64))(StaticClassSystem001, i);
            auto elem = g_militia_bodies[i];

            for (size_t i_ = 0; i_ < 16; i_++) {
                // IMC body model
                if (*(_QWORD*)(v5 + 448))
                {
                    auto v7 = (_BYTE*)(v5 + 432);
                    if (*(_QWORD*)(v5 + 456) >= 0x10ui64)
                        v7 = *(_BYTE**)v7;
                    PrecacheModel(v7);
                }

                // MCOR body model
                if (elem[0].size)
                {
                    const char* v7 = elem[0].inl;
                    if (elem[0].big_size >= 0x10)
                        v7 = elem[0].ptr;
                    PrecacheModel(v7);
                }

                // IMC arms model
                if (*(_QWORD*)(v5 + 488))
                {
                    auto v8 = (_BYTE*)(v5 + 472);
                    if (*(_QWORD*)(v5 + 496) >= 0x10ui64)
                        v8 = *(_BYTE**)v8;
                    PrecacheModel(v8);
                }

                // MCOR arms model
                if (elem[1].size)
                {
                    const char* v7 = elem[1].inl;
                    if (elem[1].big_size >= 0x10)
                        v7 = elem[1].ptr;
                    PrecacheModel(v7);
                }
            }

            // Cockpit model
            if (*(_QWORD*)(v5 + 528))
            {
                auto v9 = (_BYTE*)(v5 + 512);
                if (*(_QWORD*)(v5 + 536) >= 0x10ui64)
                    v9 = *(_BYTE**)v9;
                PrecacheModel(v9);
            }
        }
    }
}

// Dump precache stats command
void __fastcall cl_DumpPrecacheStats(__int64 CClientState, const char* name) {
    auto outhandle = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

    if (!name || !name[0]) {
        const char error[] = "Can only dump stats when active in a level\r\n";
        WriteFile(outhandle, error, sizeof(error) - 1, 0, 0);
        return;
    }

    uintptr_t items = 0;

    if (!strcmp_static(name, "modelprecache")) {
        items = CClientState + 0x19EF8;
    }
    else if (!strcmp_static(name, "genericprecache")) {
        items = CClientState + 0x1DEF8;
    }
    else if (!strcmp_static(name, "decalprecache")) {
        items = CClientState + 0x1FEF8;
    }

    using GetStringTable_t = uintptr_t(__fastcall*)(uintptr_t, const char*);
    auto GetStringTable = GetStringTable_t(G_engine + 0x22B60);
    auto table = GetStringTable(CClientState, name);

    if (!items || !table) {
        const char error[] = "!items || !table\r\n";
        WriteFile(outhandle, error, sizeof(error) - 1, 0, 0);
        return;
    }

    auto count = (*(int64_t(__fastcall**)(uintptr_t))(*(uintptr_t*)table + 24i64))(table);

    char buf[1024];
    auto towrite = sprintf_s(buf, "Precache table %s:  %d out of UNK slots used\n", name, int(count));
    WriteFile(outhandle, buf, towrite, 0, 0);

    using CL_GetPrecacheUserData_t = uintptr_t(__fastcall*)(uintptr_t, uintptr_t);
    auto CL_GetPrecacheUserData = CL_GetPrecacheUserData_t(G_engine + 0x558C0);

    for (int i = 0; i < count; i++) {
        auto slot = items + (i * 16);

        auto name = (*(const char* (__fastcall**)(__int64, _QWORD))(*(_QWORD*)table + 72i64))(table, i);

        auto p = CL_GetPrecacheUserData(table, i);

        auto refcount = (*(uint32_t*)slot) >> 3;

        if (!name || !p) {
            continue;
        }

        towrite = sprintf_s(buf, "%03i:  %s (%s):   ", i, name, "");
        WriteFile(outhandle, buf, towrite, 0, 0);
        if (refcount == 0) {
            const char msg[] = "never used\r\n";
            WriteFile(outhandle, msg, sizeof(msg) - 1, 0, 0);
        }
        else {
            towrite = sprintf_s(buf, "%i refcounts\r\n", refcount);
            WriteFile(outhandle, buf, towrite, 0, 0);
        }
    }

    FlushFileBuffers(outhandle);
}
