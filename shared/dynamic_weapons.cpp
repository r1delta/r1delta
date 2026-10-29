#include "dynamic_weapons.h"

#include "core.h"
#include "cvar.h"
#include "filesystem.h"
#include "load.h"
#include "logging.h"
#include <MinHook.h>
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// Registration of dynamic weapon classes into the native R1 client.dll and TFO server.dll
// weapon registries. The native structures and vtable layout are those of the pinned retail
// builds (client.dll 0x55038DB8 / server.dll 0x58E93EDF, see CheckModule); every RVA below is
// validated by prologue bytes before a hook is created. The weapon class machinery
// (CWeaponX construction, class maps, entity factories) was recovered once and is shared by
// all runtime modes: server.dll is the same file in listen, legacy dedicated and R1O
// fake-dedicated.

namespace r1delta::dynamic_weapons
{
namespace
{
[[noreturn]] void Fail(const char* reason, const char* detail = "")
{
    char line[512];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "R1Delta dynamic weapons: %s %s\n", reason, detail);
    OutputDebugStringA(line);
    Msg("%s", line);
    Error("%s", line);
    // Error is fatal in the native engine. Do not proceed if a replacement returns.
    TerminateProcess(GetCurrentProcess(), ERROR_INVALID_DATA);
    __assume(0);
}

template<typename T> T& At(void* p, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(p) + offset);
}

template<typename T> T Native(std::uintptr_t base, std::uintptr_t rva)
{
    return reinterpret_cast<T>(base + rva);
}

struct ClientClass
{
    void* create;
    void* createEvent;
    const char* networkName;
    void* recvTable;
    ClientClass* next;
    int classID;
    int padding;
    const char* mapName;
};
struct ServerClass
{
    const char* networkName;
    void* sendTable;
    ServerClass* next;
    int classID;
    int baseline;
};
static_assert(sizeof(ClientClass) == 0x38 && offsetof(ClientClass, mapName) == 0x30);
static_assert(sizeof(ServerClass) == 0x20 && offsetof(ServerClass, baseline) == 0x1c);

struct Descriptor;

// CEntityFactory has exactly three virtual methods, no virtual destructor.
struct Factory
{
    Descriptor* descriptor;
    virtual void* Create(const char* name);
    virtual void Destroy(void* networkable);
    virtual std::size_t GetSize() { return 0x1500; }
};

struct Descriptor
{
    std::string name;
    std::string networkName;
    std::string predictionName;
    int weaponID = -1;
    ClientClass clientClass{};
    ServerClass serverClass{};
    Factory factory;
    // [-1] preserves native RTTI; [-2] is our immutable descriptor backpointer.
    std::vector<void*> primary;
    std::vector<void*> networkable;
    void* createClient = nullptr;
    void* createPredicted = nullptr;

    explicit Descriptor(const std::string& value) : name(value),
        networkName("C" + value), predictionName("C_" + value)
    {
        factory.descriptor = this;
    }
};

using GetClasses = void*(__fastcall*)(void*);
using LockClasses = void(__fastcall*)(void*);

// Native weapon registry of the R1Delta runtime, in insertion order. Index i is native weapon
// ID i + 1; ID 0 is the empty string the first initializer inserts. client.dll registers
// exactly these classes; the shipped r1delta.wpatch removes the TFO-only registrations from
// server.dll (minigun, charge cannon, valkyrie, twinb, ai_*, test weapons: "unshifting weaponx
// class ids"), so the server registry is the same list and a weapon ID means the same thing on
// both sides, including over the network. Dynamic classes are appended in sorted roster order
// on each side, which keeps that property. A server whose registry differs (patch missing) is
// fatal as soon as a manifest is present, because the IDs would silently disagree.
constexpr const char* kClientStock[] = {
    "mp_titanweapon_40mm", "mp_titanweapon_arc_cannon", "mp_titanweapon_dumbfire_rockets",
    "mp_titanweapon_mortar", "mp_titanweapon_rocket_launcher", "mp_titanweapon_shoulder_rockets",
    "mp_titanweapon_homing_rockets", "mp_titanweapon_airburst_rockets", "mp_titanweapon_salvo_rockets",
    "mp_titanweapon_shotgun", "mp_titanweapon_sniper", "mp_titanweapon_triple_threat", "mp_titanweapon_xo16",
    "mp_titanweapon_vortex_shield", "mp_titanweapon_shoulder_turret", "mp_titanweapon_orbital_strike",
    "mp_projectile_orbital_strike", "mp_projectile_cluster_burst", "mp_titanability_bubble_shield",
    "mp_titanability_cloak", "mp_titanability_emp", "mp_titanability_fusion_core",
    "mp_titanability_healthdrop", "mp_titanability_ogre_core", "mp_titanability_smoke",
    "mp_titanability_trophy", "mp_weapon_arcrifle", "mp_weapon_autopistol", "mp_weapon_anti_titan_rifle",
    "mp_weapon_arc_rifle", "mp_weapon_car", "mp_weapon_chargerifle", "mp_weapon_dmr", "mp_weapon_defender",
    "mp_weapon_frag_grenade", "mp_weapon_g2", "mp_weapon_garand", "mp_weapon_grenade_emp", "mp_weapon_hemlok",
    "mp_weapon_laser_mine", "mp_weapon_lmg", "mp_weapon_proximity_mine", "mp_weapon_mgl", "mp_weapon_pss",
    "mp_weapon_rocket_launcher", "mp_weapon_shotgun", "mp_weapon_semipistol", "mp_weapon_smart_pistol",
    "mp_weapon_smr", "mp_weapon_sniper", "mp_weapon_suck", "mp_weapon_r97", "mp_weapon_rspn101",
    "mp_weapon_satchel", "mp_weapon_vortex_rifle", "mp_weapon_wingman", "mp_ability_cloak", "mp_ability_heal",
    "mp_ability_emp", "mp_ability_sonar", "mp_turretweapon_rockets", "mp_weapon_mega_turret",
    "mp_weapon_mega_turret_aa", "mp_weapon_yh803", "mp_weapon_yh803_bullet", "mp_weapon_mega1",
    "mp_weapon_mega2", "mp_weapon_mega3", "mp_weapon_mega4", "mp_weapon_mega5", "mp_weapon_mega6",
    "mp_weapon_mega7", "mp_weapon_mega8", "mp_weapon_mega9", "mp_weapon_mega10", "weapon_rspn101",
    "weapon_mega1", "weapon_mega2", "weapon_mega3", "weapon_mega4", "weapon_mega5", "weapon_cubemap",
};
static_assert(std::size(kClientStock) == 82);

struct State
{
    std::recursive_mutex mutex;
    std::uintptr_t client = 0;
    std::uintptr_t server = 0;
    bool clientReady = false;
    bool serverReady = false;
    bool registeringClient = false;
    bool registeringServer = false;
    bool rosterRead = false;
    bool manifestPresent = false;
    std::vector<std::string> roster;
    std::string token;
    std::vector<std::unique_ptr<Descriptor>> clientTypes;
    std::vector<std::unique_ptr<Descriptor>> serverTypes;
    GetClasses clientGetClasses = nullptr;
    GetClasses serverGetClasses = nullptr;
    LockClasses serverLock = nullptr;
    unsigned char* closures = nullptr;
};

State& S()
{
    // Native registries retain these pointers until their module CRT teardown.
    // Deliberately process-lifetime: no DLL teardown-order-dependent destructor.
    static State* state = new State;
    return *state;
}

void CheckBytes(std::uintptr_t base, std::uintptr_t rva,
    std::initializer_list<unsigned char> bytes, const char* label)
{
    if (std::memcmp(reinterpret_cast<void*>(base + rva), bytes.begin(), bytes.size()))
        Fail("unsupported native entry/prologue:", label);
}

// Provenance: the pinned retail builds. The same two numbers pin the module in
// engine/r2port/wallrun/r2_wallrun.cpp. Entry prologues are checked as well.
void CheckModule(std::uintptr_t base, DWORD stamp, DWORD imageSize, const char* label)
{
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
        Fail("invalid native module PE:", label);
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
        || pe->FileHeader.TimeDateStamp != stamp || pe->OptionalHeader.SizeOfImage != imageSize)
        Fail("unsupported native module timestamp/image size:", label);
}

void Hook(std::uintptr_t target, void* replacement, void** original)
{
    const auto create = MH_CreateHook(reinterpret_cast<void*>(target), replacement, original);
    if (create != MH_OK)
        Fail("required hook creation failed:", MH_StatusToString(create));
    const auto enable = MH_EnableHook(reinterpret_cast<void*>(target));
    if (enable != MH_OK)
        Fail("required hook activation failed:", MH_StatusToString(enable));
}

// Native RBTree ABI: comparator+0, nodes+8, root+20h, count+24h.
// Nodes are 20h bytes, with left/right indices+0/+4 and name payload+10h.
// Use the registry's comparator, not a second case/collision convention.
void* WeaponTree(std::uintptr_t module, bool client)
{
    return reinterpret_cast<void*>(module + (client ? 0xb28d10 : 0xb6a430));
}

int FindWeapon(void* tree, const char* name)
{
    using Less = bool(__fastcall*)(const char* const*, const char* const*);
    const auto less = At<Less>(tree, 0);
    auto* nodes = At<unsigned char*>(tree, 8);
    int node = At<int>(tree, 0x20);
    while (node != -1) {
        auto* row = nodes + std::size_t(node) * 0x20;
        const auto* key = reinterpret_cast<const char* const*>(row + 0x10);
        if (less(&name, key))
            node = At<int>(row, 0);
        else if (less(key, &name))
            node = At<int>(row, 4);
        else
            return node;
    }
    return -1;
}

int AddWeapon(std::uintptr_t module, bool client, const char* name)
{
    void* tree = WeaponTree(module, client);
    if (FindWeapon(tree, name) != -1)
        Fail("attempted duplicate native weapon identity:", name);
    using Duplicate = char*(__fastcall*)(const char*);
    struct Row { char* name; unsigned int extra; unsigned int padding; };
    Row row{Native<Duplicate>(module, client ? 0x1e70 : 0x7224fc)(name), 0, 0};
    if (!row.name)
        Fail("native weapon-name allocation failed:", name);
    using Insert = int(__fastcall*)(void*, const Row*);
    const int index = Native<Insert>(module, client ? 0x4a6150 : 0xed7a0)(tree, &row);
    if (index < 0 || FindWeapon(tree, name) != index)
        Fail("native weapon identity insertion failed:", name);
    return index;
}

// The stock registry must be exactly the table recovered from the pinned build.
void CheckStockRegistry(std::uintptr_t module, bool client, const char* const* stock, std::size_t count)
{
    void* tree = WeaponTree(module, client);
    auto* nodes = At<unsigned char*>(tree, 8);
    const int live = At<int>(tree, 0x24);
    if (live != static_cast<int>(count) + 1 || !nodes) {
        char detail[96];
        _snprintf_s(detail, sizeof(detail), _TRUNCATE, "(live entries %d, expected %zu, nodes %p)", live, count + 1, nodes);
        for (int i = 0; nodes && i < live && i < 200; ++i) {
            const char* live_name = At<const char*>(nodes + std::size_t(i) * 32, 16);
            char dump[160];
            _snprintf_s(dump, sizeof(dump), _TRUNCATE, "R1Delta dynamic weapons: live %s weapon ID %d = %s\n", client ? "client" : "server", i, live_name ? live_name : "(null)");
            OutputDebugStringA(dump);
        }
        Fail(client ? "client native weapon registry is not the R1Delta stock registry" :
            "server native weapon registry is not the R1Delta stock registry (is r1delta.wpatch applied?)", detail);
    }
    const char* invalid = At<const char*>(nodes, 16);
    if (!invalid || *invalid != '\0')
        Fail("native weapon registry slot 0 is not the invalid-weapon entry");
    for (std::size_t i = 0; i < count; ++i) {
        const char* name = At<const char*>(nodes + (i + 1) * 32, 16);
        if (!name || std::strcmp(name, stock[i]))
            Fail(client ? "unexpected client native weapon ID order at:" :
                "unexpected server native weapon ID order at:", stock[i]);
    }
}

std::string ClassName(const char* value)
{
    if (!value || !*value)
        Fail("empty weapon manifest entry");
    std::string name(value);
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos)
        name.erase(0, slash + 1);
    const auto dot = name.find_last_of('.');
    if (dot != std::string::npos)
        name.resize(dot);
    if (name.empty() || name.size() >= 40)
        Fail("weapon classname exceeds native CClassMap char[40] contract:", value);
    for (char& c : name) {
        if (c >= 'A' && c <= 'Z')
            c = char(c + ('a' - 'A'));
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            Fail("unsupported weapon manifest classname:", value);
    }
    return name;
}

// ---- GAME filesystem (R1 base filesystem ABI, shared by all modes) -------------------------------
// The R1 filesystem_stdio base interface: vtable +0 Read, +2 Open, +3 Close, +7 Size(handle),
// +10 FileExists (same slots as CBaseFileSystem in engine/filesystem/filesystem.h). It is the
// native object in every mode: the client loads it directly; the server factory wrap in
// engine/core/factory.cpp publishes the same object as g_CBaseFileSystemInterface.
void* BaseFileSystem()
{
    using CreateInterface = void*(__cdecl*)(const char*, int*);
    if (const HMODULE filesystem = GetModuleHandleW(L"filesystem_stdio.dll")) {
        if (const auto factory = reinterpret_cast<CreateInterface>(GetProcAddress(filesystem, "CreateInterface")))
            if (void* base = factory("VBaseFileSystem012", nullptr))
                return base;
    }
    if (g_CBaseFileSystemInterface)
        return g_CBaseFileSystemInterface;
    Fail("native base filesystem unavailable at first class publication");
}

bool FileExists(void* baseFilesystem, const char* path)
{
    using Exists = bool(__fastcall*)(void*, const char*, const char*);
    const auto exists = reinterpret_cast<Exists>((*static_cast<void***>(baseFilesystem))[10]);
    return exists(baseFilesystem, path, "GAME");
}

bool ReadGameFile(void* baseFilesystem, const char* path, std::string& text)
{
    void** vtable = *static_cast<void***>(baseFilesystem);
    using Open = void*(__fastcall*)(void*, const char*, const char*, const char*);
    using Close = void(__fastcall*)(void*, void*);
    using Size = long long(__fastcall*)(void*, void*);
    using Read = int(__fastcall*)(void*, void*, int, void*);
    void* file = reinterpret_cast<Open>(vtable[2])(baseFilesystem, path, "rb", "GAME");
    if (!file)
        return false;
    const long long size = reinterpret_cast<Size>(vtable[7])(baseFilesystem, file);
    bool ok = size >= 0 && size <= (1 << 20);
    if (ok) {
        text.resize(static_cast<std::size_t>(size));
        ok = size == 0 || reinterpret_cast<Read>(vtable[0])(baseFilesystem, text.data(),
            static_cast<int>(size), file) == static_cast<int>(size);
    }
    reinterpret_cast<Close>(vtable[3])(baseFilesystem, file);
    return ok;
}

// ---- manifest -------------------------------------------------------------------------------------
// Grammar (the native weapon_manifest KeyValues subset): `rootkey { "file" "<weapon>" ... }`,
// bare or quoted tokens, `//` line comments. Anything else is fatal, so a typo in a mod can
// never silently drop a weapon.
struct Tokenizer
{
    const std::string& text;
    std::size_t pos = 0;

    void SkipSpace()
    {
        for (;;) {
            while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n'))
                ++pos;
            if (pos + 1 < text.size() && text[pos] == '/' && text[pos + 1] == '/') {
                while (pos < text.size() && text[pos] != '\n')
                    ++pos;
                continue;
            }
            break;
        }
    }

    // Returns false at end of input. A brace is its own token.
    bool Next(std::string& token, bool& quoted)
    {
        SkipSpace();
        token.clear();
        quoted = false;
        if (pos >= text.size())
            return false;
        const char c = text[pos];
        if (c == '"') {
            quoted = true;
            ++pos;
            while (pos < text.size() && text[pos] != '"') {
                if (text[pos] == '\n')
                    Fail("unterminated string in authored weapon_manifest.txt");
                token.push_back(text[pos++]);
            }
            if (pos >= text.size())
                Fail("unterminated string in authored weapon_manifest.txt");
            ++pos;
            return true;
        }
        if (c == '{' || c == '}') {
            token.push_back(c);
            ++pos;
            return true;
        }
        while (pos < text.size()) {
            const char d = text[pos];
            if (d == ' ' || d == '\t' || d == '\r' || d == '\n' || d == '"' || d == '{' || d == '}')
                break;
            if (d == '/' && pos + 1 < text.size() && text[pos + 1] == '/')
                break;
            token.push_back(d);
            ++pos;
        }
        return true;
    }
};

std::vector<std::string> ParseManifest(const std::string& text)
{
    Tokenizer in{text};
    std::string token;
    bool quoted = false;
    if (!in.Next(token, quoted) || token == "{" || token == "}")
        Fail("authored weapon_manifest.txt has no root key");
    if (!in.Next(token, quoted) || quoted || token != "{")
        Fail("authored weapon_manifest.txt: expected '{' after the root key");
    std::vector<std::string> names;
    for (;;) {
        if (!in.Next(token, quoted))
            Fail("authored weapon_manifest.txt: missing closing '}'");
        if (!quoted && token == "}")
            break;
        if (!quoted && (token == "{" || token[0] == '#' || token[0] == '['))
            Fail("authored weapon_manifest.txt: unsupported token:", token.c_str());
        if (_stricmp(token.c_str(), "file"))
            Fail("authored weapon_manifest.txt expects 'file', got:", token.c_str());
        std::string value;
        if (!in.Next(value, quoted) || (!quoted && (value == "{" || value == "}")))
            Fail("authored weapon_manifest.txt: 'file' without a value");
        names.push_back(ClassName(value.c_str()));
    }
    if (in.Next(token, quoted))
        Fail("authored weapon_manifest.txt: content after the closing '}':", token.c_str());
    if (names.empty())
        Fail("authored weapon_manifest.txt declares no weapons");
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

bool DefinitionExists(void* baseFilesystem, const std::string& name)
{
    for (const char* directory : {"scripts/weapons/", "scripts/"}) {
        for (const char* extension : {".txt", ".ctx"}) {
            const std::string path = directory + name + extension;
            if (FileExists(baseFilesystem, path.c_str()))
                return true;
        }
    }
    return false;
}

std::string MakeToken(const std::vector<std::string>& names)
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const auto& name : names) {
        for (unsigned char c : name) {
            hash ^= c;
            hash *= 0x100000001b3ULL;
        }
        hash ^= 0xff;
        hash *= 0x100000001b3ULL;
    }
    char text[48];
    _snprintf_s(text, sizeof(text), _TRUNCATE, "%zu:%016llx", names.size(), static_cast<unsigned long long>(hash));
    return text;
}

// Reads the manifest for one side. Client and server read independently (they publish at
// different times and from different native filesystems); a difference between the two
// reads means the GAME mounts changed in between, which cannot be repaired in-process.
void ReadRoster(const char* side)
{
    auto& s = S();
    void* baseFilesystem = BaseFileSystem();
    std::string text;
    const bool present = FileExists(baseFilesystem, "scripts/weapon_manifest.txt");
    std::vector<std::string> names;
    if (present) {
        if (!ReadGameFile(baseFilesystem, "scripts/weapon_manifest.txt", text))
            Fail("authored GAME/scripts/weapon_manifest.txt exists but cannot be read");
        names = ParseManifest(text);
        for (const auto& name : names)
            if (!DefinitionExists(baseFilesystem, name))
                Fail("authored manifest names a missing native weapon definition:", name.c_str());
    }
    if (s.rosterRead) {
        if (present != s.manifestPresent || names != s.roster)
            Fail("weapon manifest changed between client and server class publication; finish GAME mounts before publication and restart");
        return;
    }
    s.manifestPresent = present;
    s.roster = std::move(names);
    s.token = present ? MakeToken(s.roster) : std::string("none");
    s.rosterRead = true;
    if (present)
        Msg("R1Delta dynamic weapons: %s manifest declares %zu weapons (%s)\n", side, s.roster.size(), s.token.c_str());
    else
        Msg("R1Delta dynamic weapons: no authored GAME/scripts/weapon_manifest.txt; retaining stock native classes.\n");
}

// ---- class construction ---------------------------------------------------------------------------
void Clone(std::vector<void*>& result, std::uintptr_t module, std::uintptr_t rva,
    std::size_t slots, Descriptor& descriptor)
{
    result.resize(slots + 2);
    result[0] = &descriptor;
    auto* source = reinterpret_cast<void**>(module + rva);
    // Preserve native RTTI and every unmodified method, including scalar deleting
    // destructors, datamap, prediction map and script-description getters.
    std::copy(source - 1, source + slots, result.begin() + 1);
}

Descriptor& Identity(void* object)
{
    auto** vtable = *static_cast<void***>(object);
    return *static_cast<Descriptor*>(vtable[-2]);
}

int __fastcall WeaponID(void* entity) { return Identity(entity).weaponID; }
ClientClass* __fastcall GetClientClass(void* networkable) { return &Identity(networkable).clientClass; }
ServerClass* __fastcall GetServerClass(void* entity) { return &Identity(entity).serverClass; }

void* ConstructClient(Descriptor& descriptor)
{
    auto& s = S();
    using Allocate = void*(__fastcall*)(std::size_t);
    using Construct = void*(__fastcall*)(void*);
    void* entity = Native<Allocate>(s.client, 0x2f15b0)(0x31d8);
    if (!entity)
        Fail("client weapon allocation failed:", descriptor.name.c_str());
    Native<Construct>(s.client, 0x4923c0)(entity);
    At<void*>(entity, 0) = descriptor.primary.data() + 2;
    At<void*>(entity, 0x10) = descriptor.networkable.data() + 2;
    // The other three interface vptrs remain the exact native CWeaponX tables.
    return entity;
}

void* __fastcall CreateClient(int entityIndex, int serial, Descriptor* descriptor)
{
    void* entity = ConstructClient(*descriptor);
    using Init = char(__fastcall*)(void*, int, int);
    if (!Native<Init>(S().client, 0x2f98d0)(entity, entityIndex, serial))
        Fail("native client weapon entity initialization failed:", descriptor->name.c_str());
    return static_cast<unsigned char*>(entity) + 0x10;
}

void* __fastcall CreatePredicted(Descriptor* descriptor)
{
    return ConstructClient(*descriptor);
}

void* Factory::Create(const char* name)
{
    auto& s = S();
    using Allocate = void*(__fastcall*)(std::size_t, std::size_t);
    using Construct = void*(__fastcall*)(void*);
    using PostConstruct = char(__fastcall*)(void*, const char*);
    void* entity = Native<Allocate>(s.server, 0x71c080)(1, 0x1500);
    if (!entity)
        Fail("server weapon allocation failed:", name);
    Native<Construct>(s.server, 0x5e9e80)(entity);
    At<void*>(entity, 0) = descriptor->primary.data() + 2;
    Native<PostConstruct>(s.server, 0x3a3b80)(entity, name);
    return static_cast<unsigned char*>(entity) + 0x28;
}

void Factory::Destroy(void* networkable)
{
    if (networkable) {
        using Release = void(__fastcall*)(void*);
        auto** vtable = *static_cast<void***>(networkable);
        reinterpret_cast<Release>(vtable[4])(networkable);
    }
}

void Closure(unsigned char* code, Descriptor* descriptor, void* target, bool network)
{
    // Windows x64 leaf tail calls: no stack modification or unwind metadata.
    // Network Create(int,int) adds descriptor in R8; no-arg prediction Create()
    // adds it in RCX. Pages are sealed RX before any callback is published.
    code[0] = network ? 0x49 : 0x48;
    code[1] = network ? 0xb8 : 0xb9;
    std::memcpy(code + 2, &descriptor, sizeof(descriptor));
    code[10] = 0x48;
    code[11] = 0xb8;
    std::memcpy(code + 12, &target, sizeof(target));
    code[20] = 0xff;
    code[21] = 0xe0;
}

// ---- ID agreement ---------------------------------------------------------------------------------
// Both registries start from the same stock list and both append the roster names that are not
// stock, sorted. In a process holding both modules (listen) the live IDs must agree.
void VerifyPair()
{
    auto& s = S();
    if (!s.clientReady || !s.serverReady)
        return;
    void* client = WeaponTree(s.client, true);
    void* server = WeaponTree(s.server, false);
    for (const auto& name : s.roster)
        if (FindWeapon(client, name.c_str()) != FindWeapon(server, name.c_str()))
            Fail("client and server weapon IDs differ:", name.c_str());
}

// ---- lifecycle ------------------------------------------------------------------------------------
void RegisterClient()
{
    auto& s = S();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if (s.clientReady)
        return;
    if (!s.client || s.registeringClient)
        Fail("client registration unavailable/reentrant");
    s.registeringClient = true;
    ReadRoster("client");
    if (!s.roster.empty())
        CheckStockRegistry(s.client, true, kClientStock, std::size(kClientStock));
    using ClassMap = void*(__fastcall*)();
    void* map = Native<ClassMap>(s.client, 0x10df30)();
    auto** mapVtable = *static_cast<void***>(map);
    using Lookup = const char*(__fastcall*)(void*, const char*);
    const auto lookup = reinterpret_cast<Lookup>(mapVtable[2]);
    using ClassSize = int(__fastcall*)(void*, const char*);
    const auto classSize = reinterpret_cast<ClassSize>(mapVtable[4]);
    for (const auto& name : s.roster) {
        if (FindWeapon(WeaponTree(s.client, true), name.c_str()) != -1)
            continue; // Existing native registry identity wins; loose data can override it.
        auto descriptor = std::make_unique<Descriptor>(name);
        if (lookup(map, descriptor->predictionName.c_str()) || classSize(map, name.c_str()) >= 0)
            Fail("manifest collides with non-weapon predicted class:", name.c_str());
        for (auto* cls = *reinterpret_cast<ClientClass**>(s.client + 0xc4bf60); cls; cls = cls->next)
            if (!_stricmp(cls->networkName, descriptor->networkName.c_str()))
                Fail("manifest collides with existing client network class:", name.c_str());
        Clone(descriptor->primary, s.client, 0x92b1f8, 321, *descriptor);
        Clone(descriptor->networkable, s.client, 0x92b158, 18, *descriptor);
        descriptor->primary[2 + 320] = reinterpret_cast<void*>(&WeaponID);
        descriptor->networkable[2 + 2] = reinterpret_cast<void*>(&GetClientClass);
        descriptor->weaponID = AddWeapon(s.client, true, name.c_str());
        s.clientTypes.push_back(std::move(descriptor));
    }
    if (!s.clientTypes.empty()) {
        const std::size_t bytes = s.clientTypes.size() * 64;
        s.closures = static_cast<unsigned char*>(VirtualAlloc(nullptr, bytes,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!s.closures)
            Fail("client factory closure allocation failed");
        for (std::size_t i = 0; i < s.clientTypes.size(); ++i) {
            auto& descriptor = *s.clientTypes[i];
            descriptor.createClient = s.closures + i * 64;
            descriptor.createPredicted = s.closures + i * 64 + 32;
            Closure(static_cast<unsigned char*>(descriptor.createClient), &descriptor,
                reinterpret_cast<void*>(&CreateClient), true);
            Closure(static_cast<unsigned char*>(descriptor.createPredicted), &descriptor,
                reinterpret_cast<void*>(&CreatePredicted), false);
        }
        DWORD previous;
        if (!VirtualProtect(s.closures, bytes, PAGE_EXECUTE_READ, &previous)
            || !FlushInstructionCache(GetCurrentProcess(), s.closures, bytes))
            Fail("client factory closure RX publication failed");
    }
    using RegisterClass = void*(__fastcall*)(ClientClass*, const char*, void*, void*, void*);
    using AddClassMap = void(__fastcall*)(void*, const char*, const char*, int, void*);
    for (const auto& owner : s.clientTypes) {
        auto& descriptor = *owner;
        Native<RegisterClass>(s.client, 0x208a0)(&descriptor.clientClass,
            descriptor.networkName.c_str(), descriptor.createClient, nullptr,
            reinterpret_cast<void*>(s.client + 0x17ea6a0));
        descriptor.clientClass.mapName = descriptor.name.c_str();
        reinterpret_cast<AddClassMap>(mapVtable[1])(map, descriptor.name.c_str(),
            descriptor.predictionName.c_str(), 0x31d8, descriptor.createPredicted);
        Msg("R1Delta dynamic weapon client: %s id=%d\n", descriptor.name.c_str(), descriptor.weaponID);
    }
    s.clientReady = true;
    s.registeringClient = false;
    VerifyPair();
}

void RegisterServer()
{
    auto& s = S();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if (s.serverReady)
        return;
    if (!s.server || s.registeringServer)
        Fail("server registration unavailable/reentrant");
    if (*reinterpret_cast<unsigned char*>(s.server + 0xc3113e))
        Fail("server class list already locked; refusing late registration");
    s.registeringServer = true;
    ReadRoster("server");
    if (!s.roster.empty())
        CheckStockRegistry(s.server, false, kClientStock, std::size(kClientStock));
    using Dictionary = void*(__fastcall*)();
    void* dictionary = Native<Dictionary>(s.server, 0x25a6c0)();
    auto** vtable = *static_cast<void***>(dictionary);
    using FindFactory = void*(__fastcall*)(void*, const char*);
    using InstallFactory = void(__fastcall*)(void*, Factory*, const char*);
    using RegisterClass = void*(__fastcall*)(ServerClass*, const char*, void*);
    for (const auto& name : s.roster) {
        if (FindWeapon(WeaponTree(s.server, false), name.c_str()) >= 0)
            continue; // Preserve every existing native identity/factory convention.
        if (reinterpret_cast<FindFactory>(vtable[3])(dictionary, name.c_str()))
            Fail("weapon manifest collides with non-weapon server entity:", name.c_str());
        auto descriptor = std::make_unique<Descriptor>(name);
        for (auto* cls = *reinterpret_cast<ServerClass**>(s.server + 0xc31270); cls; cls = cls->next)
            if (!_stricmp(cls->networkName, descriptor->networkName.c_str()))
                Fail("manifest collides with existing server network class:", name.c_str());
        Clone(descriptor->primary, s.server, 0x97af08, 347, *descriptor);
        descriptor->primary[2 + 9] = reinterpret_cast<void*>(&GetServerClass);
        descriptor->primary[2 + 346] = reinterpret_cast<void*>(&WeaponID);
        descriptor->weaponID = AddWeapon(s.server, false, name.c_str());
        Native<RegisterClass>(s.server, 0x7f7e0)(&descriptor->serverClass,
            descriptor->networkName.c_str(), reinterpret_cast<void*>(s.server + 0xe0a670));
        reinterpret_cast<InstallFactory>(vtable[0])(dictionary, &descriptor->factory, descriptor->name.c_str());
        Msg("R1Delta dynamic weapon server: %s id=%d\n", descriptor->name.c_str(), descriptor->weaponID);
        s.serverTypes.push_back(std::move(descriptor));
    }
    s.serverReady = true;
    s.registeringServer = false;
    VerifyPair();
}

void* __fastcall ClientClasses(void* self)
{
    RegisterClient();
    return S().clientGetClasses(self);
}
void* __fastcall ServerClasses(void* self)
{
    RegisterServer();
    return S().serverGetClasses(self);
}
void __fastcall ServerLock(void* self)
{
    RegisterServer();
    S().serverLock(self);
}
}

void InstallClient(std::uintptr_t clientBase)
{
    auto& s = S();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if (s.client) {
        if (s.client != clientBase)
            Fail("client module reload is unsupported; restart required");
        return;
    }
    CheckModule(clientBase, 0x55038db8, 0x39a4000, "client.dll");
    CheckBytes(clientBase, 0x4923c0, {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x30}, "CWeaponX client constructor");
    CheckBytes(clientBase, 0xf7780, {0x48,0x8b,0x05,0xd9,0x47,0xb5,0x00,0xc3}, "client GetAllClasses");
    if (*reinterpret_cast<unsigned short*>(clientBase + 0xb0ee42))
        Fail("client weapon data loaded before dynamic weapon installation");
    s.client = clientBase;
    Hook(clientBase + 0xf7780, reinterpret_cast<void*>(&ClientClasses), reinterpret_cast<void**>(&s.clientGetClasses));
}

void InstallServer(std::uintptr_t serverBase)
{
    auto& s = S();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if (s.server) {
        if (s.server != serverBase)
            Fail("server module reload is unsupported; restart required");
        return;
    }
    CheckModule(serverBase, 0x58e93edf, 0xfb5000, "server.dll");
    CheckBytes(serverBase, 0x5e9e80, {0x48,0x89,0x4c,0x24,0x08,0x57,0x48,0x83,0xec,0x40,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff}, "CWeaponX server constructor");
    CheckBytes(serverBase, 0x146980, {0x48,0x8b,0x05,0xe9,0xa8,0xae,0x00,0xc3}, "server GetAllClasses");
    CheckBytes(serverBase, 0x1469a0, {0xc6,0x05,0x97,0xa7,0xae,0x00,0x01,0xc3}, "server class lock");
    if (*reinterpret_cast<unsigned short*>(serverBase + 0xb6b5a2))
        Fail("server weapon data loaded before dynamic weapon installation");
    s.server = serverBase;
    Hook(serverBase + 0x146980, reinterpret_cast<void*>(&ServerClasses), reinterpret_cast<void**>(&s.serverGetClasses));
    Hook(serverBase + 0x1469a0, reinterpret_cast<void*>(&ServerLock), reinterpret_cast<void**>(&s.serverLock));
}

std::string RosterToken()
{
    auto& s = S();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    return s.token;
}

void ClientConnecting()
{
    auto& s = S();
    std::string token;
    {
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        token = s.token.empty() ? std::string("none") : s.token;
    }
    ConVarR1* var = OriginalCCVar_FindVar(cvarinterface, "delta_weapon_roster");
    if (var)
        SetConvarStringOriginal(var, token.c_str());
}

bool ServerAcceptsClientRoster(const char* clientToken, char* reason, int reasonSize)
{
    auto& s = S();
    std::string server;
    {
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        server = s.token.empty() ? std::string("none") : s.token;
    }
    const std::string client = clientToken && *clientToken ? clientToken : "none";
    if (client == server)
        return true;
    if (reason && reasonSize > 0)
        _snprintf_s(reason, reasonSize, _TRUNCATE,
            "Weapon manifest mismatch: the server registers %s custom weapons, you have %s. "
            "Install the same mods / weapon_manifest.txt as the server.",
            server.c_str(), client.c_str());
    return false;
}
}
