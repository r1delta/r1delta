#include "player_resource_18.h"

#include "core.h"

#include <MinHook.h>
#include <Windows.h>

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace
{
constexpr int kOldPlayerResourceElements = 17;
constexpr int kNewPlayerResourceElements = 19;
constexpr int kFirstExtendedPlayerIndex = 17;
constexpr int kLastExtendedPlayerIndex = 18;

constexpr std::size_t kClientOldObjectSize = 0x1F40;
constexpr std::size_t kClientNewObjectSize = 0x1FC0;
constexpr std::size_t kClientGameResourcesOffset = 6936;
constexpr std::size_t kClientExtendedNamesOffset = 8000;
constexpr std::size_t kClientExtendedPingOffset = 8016;
constexpr std::size_t kClientExtendedScoreOffset = 8024;
constexpr std::size_t kClientExtendedKillsOffset = 8032;
constexpr std::size_t kClientExtendedDeathsOffset = 8040;
constexpr std::size_t kClientExtendedConnectedOffset = 8048;
constexpr std::size_t kClientExtendedTeamOffset = 8056;
constexpr std::size_t kClientExtendedAliveOffset = 8064;
constexpr std::size_t kClientExtendedHealthOffset = 8072;
constexpr std::size_t kClientExtendedTitanKillsOffset = 8080;
constexpr std::size_t kClientExtendedNpcKillsOffset = 8088;
constexpr std::size_t kClientExtendedAssistsOffset = 8096;
constexpr std::size_t kClientExtendedAssaultScoreOffset = 8104;
constexpr std::size_t kClientExtendedDefenseScoreOffset = 8112;

constexpr std::size_t kServerNewObjectSize = 0xD90;
constexpr int kServerExtendedPingOffset = 2480;
constexpr int kServerExtendedScoreOffset = 2556;
constexpr int kServerExtendedKillsOffset = 2632;
constexpr int kServerExtendedDeathsOffset = 2708;
constexpr int kServerExtendedConnectedOffset = 2784;
constexpr int kServerExtendedTeamOffset = 2860;
constexpr int kServerExtendedAliveOffset = 2936;
constexpr int kServerExtendedHealthOffset = 3012;
constexpr int kServerExtendedTitanKillsOffset = 3088;
constexpr int kServerExtendedNpcKillsOffset = 3164;
constexpr int kServerExtendedAssistsOffset = 3240;
constexpr int kServerExtendedAssaultScoreOffset = 3316;
constexpr int kServerExtendedDefenseScoreOffset = 3392;

constexpr char kElement17Name[] = "017";
constexpr char kElement18Name[] = "018";

void PlayerResourceLog(const char* format, ...)
{
    char buffer[1024]{};
    va_list args;
    va_start(args, format);
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
    va_end(args);
    OutputDebugStringA(buffer);
    std::fputs(buffer, stderr);
}

bool IsReadableRange(const void* address, std::size_t size)
{
    if (!address || !size)
        return false;

    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto end = begin + size;
    if (end < begin)
        return false;

    std::uintptr_t cursor = begin;
    while (cursor < end)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)))
            return false;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;

        const auto regionEnd =
            reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (regionEnd <= cursor)
            return false;
        cursor = regionEnd < end ? regionEnd : end;
    }
    return true;
}

bool WriteMemory(void* destination, const void* source, std::size_t size)
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    std::memcpy(destination, source, size);
    FlushInstructionCache(GetCurrentProcess(), destination, size);
    DWORD ignored = 0;
    VirtualProtect(destination, size, oldProtect, &ignored);
    return true;
}

template <typename T>
bool WriteValue(T* destination, const T& value)
{
    return WriteMemory(destination, &value, sizeof(value));
}

bool PatchBytesIfMatch(
    std::uintptr_t moduleBase,
    std::uintptr_t rva,
    const void* expected,
    const void* replacement,
    std::size_t size,
    const char* description)
{
    auto* target = reinterpret_cast<unsigned char*>(moduleBase + rva);
    if (!IsReadableRange(target, size) || std::memcmp(target, expected, size) != 0)
    {
        PlayerResourceLog(
            "R1Delta: refusing player-resource patch '%s' at RVA 0x%llX: "
            "unexpected binary revision\n",
            description,
            static_cast<unsigned long long>(rva));
        return false;
    }
    if (!WriteMemory(target, replacement, size))
    {
        PlayerResourceLog(
            "R1Delta: failed player-resource patch '%s' at RVA 0x%llX "
            "(VirtualProtect error %lu)\n",
            description,
            static_cast<unsigned long long>(rva),
            GetLastError());
        return false;
    }
    return true;
}

bool PatchInstructionDword(
    std::uintptr_t moduleBase,
    std::uintptr_t instructionRva,
    std::int32_t expectedValue,
    std::int32_t replacementValue,
    const char* description)
{
    constexpr std::size_t kMaximumInstructionSize = 15;
    auto* instruction =
        reinterpret_cast<unsigned char*>(moduleBase + instructionRva);
    if (!IsReadableRange(instruction, kMaximumInstructionSize))
    {
        PlayerResourceLog(
            "R1Delta: refusing player-resource displacement patch '%s' at "
            "RVA 0x%llX: unreadable instruction\n",
            description,
            static_cast<unsigned long long>(instructionRva));
        return false;
    }

    std::size_t matchOffset = 0;
    int matches = 0;
    for (std::size_t offset = 0;
         offset + sizeof(expectedValue) <= kMaximumInstructionSize;
         ++offset)
    {
        std::int32_t candidate = 0;
        std::memcpy(&candidate, instruction + offset, sizeof(candidate));
        if (candidate == expectedValue)
        {
            matchOffset = offset;
            ++matches;
        }
    }

    if (matches != 1)
    {
        PlayerResourceLog(
            "R1Delta: refusing player-resource displacement patch '%s' at "
            "RVA 0x%llX: expected one 0x%X operand, found %d\n",
            description,
            static_cast<unsigned long long>(instructionRva),
            static_cast<unsigned int>(expectedValue),
            matches);
        return false;
    }
    return WriteValue(
        reinterpret_cast<std::int32_t*>(instruction + matchOffset),
        replacementValue);
}

bool InstallCheckedHook(
    std::uintptr_t moduleBase,
    std::uintptr_t rva,
    const unsigned char* expectedPrologue,
    std::size_t expectedSize,
    void* detour,
    void** original,
    const char* description)
{
    auto* target = reinterpret_cast<void*>(moduleBase + rva);
    if (!IsReadableRange(target, expectedSize)
        || std::memcmp(target, expectedPrologue, expectedSize) != 0)
    {
        PlayerResourceLog(
            "R1Delta: refusing player-resource hook '%s' at RVA 0x%llX: "
            "unexpected binary revision\n",
            description,
            static_cast<unsigned long long>(rva));
        return false;
    }

    const MH_STATUS createStatus = MH_CreateHook(target, detour, original);
    if (createStatus != MH_OK)
    {
        PlayerResourceLog(
            "R1Delta: failed to create player-resource hook '%s' at RVA "
            "0x%llX: MinHook status %d\n",
            description,
            static_cast<unsigned long long>(rva),
            static_cast<int>(createStatus));
        return false;
    }

    const MH_STATUS enableStatus = MH_EnableHook(target);
    if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED)
    {
        PlayerResourceLog(
            "R1Delta: failed to enable player-resource hook '%s' at RVA "
            "0x%llX: MinHook status %d\n",
            description,
            static_cast<unsigned long long>(rva),
            static_cast<int>(enableStatus));
        return false;
    }
    return true;
}

struct NetworkArrayField
{
    const char* name;
    std::uintptr_t parentRva;
    int oldOffset;
    int newOffset;
};

constexpr NetworkArrayField kServerNetworkFields[] = {
    {"m_iPing", 0xE9DC28, 1592, kServerExtendedPingOffset},
    {"m_score", 0xE9DCB0, 1660, kServerExtendedScoreOffset},
    {"m_kills", 0xE9DD38, 1728, kServerExtendedKillsOffset},
    {"m_deaths", 0xE9DDC0, 1796, kServerExtendedDeathsOffset},
    {"m_bConnected", 0xE9DE48, 1864, kServerExtendedConnectedOffset},
    {"m_iTeam", 0xE9DED0, 1932, kServerExtendedTeamOffset},
    {"m_bAlive", 0xE9DF58, 2000, kServerExtendedAliveOffset},
    {"m_iPRHealth", 0xE9DFE0, 2068, kServerExtendedHealthOffset},
    {"m_titanKills", 0xE9E068, 2136, kServerExtendedTitanKillsOffset},
    {"m_npcKills", 0xE9E0F0, 2204, kServerExtendedNpcKillsOffset},
    {"m_assists", 0xE9E178, 2272, kServerExtendedAssistsOffset},
    {"m_assaultScore", 0xE9E200, 2340, kServerExtendedAssaultScoreOffset},
    {"m_defenseScore", 0xE9E288, 2408, kServerExtendedDefenseScoreOffset},
};

constexpr NetworkArrayField kClientNetworkFields[] = {
    {"m_iPing", 0xBED030, 7080, static_cast<int>(kClientExtendedPingOffset)},
    {"m_score", 0xBED090, 7148, static_cast<int>(kClientExtendedScoreOffset)},
    {"m_kills", 0xBED0F0, 7216, static_cast<int>(kClientExtendedKillsOffset)},
    {"m_deaths", 0xBED150, 7284, static_cast<int>(kClientExtendedDeathsOffset)},
    {"m_bConnected", 0xBED1B0, 7352, static_cast<int>(kClientExtendedConnectedOffset)},
    {"m_iTeam", 0xBED210, 7420, static_cast<int>(kClientExtendedTeamOffset)},
    {"m_bAlive", 0xBED270, 7488, static_cast<int>(kClientExtendedAliveOffset)},
    {"m_iPRHealth", 0xBED2D0, 7556, static_cast<int>(kClientExtendedHealthOffset)},
    {"m_titanKills", 0xBED330, 7656, static_cast<int>(kClientExtendedTitanKillsOffset)},
    {"m_npcKills", 0xBED390, 7724, static_cast<int>(kClientExtendedNpcKillsOffset)},
    {"m_assists", 0xBED3F0, 7792, static_cast<int>(kClientExtendedAssistsOffset)},
    {"m_assaultScore", 0xBED450, 7860, static_cast<int>(kClientExtendedAssaultScoreOffset)},
    {"m_defenseScore", 0xBED4B0, 7928, static_cast<int>(kClientExtendedDefenseScoreOffset)},
};

struct ServerNativeArrayConstructionSite
{
    std::uintptr_t countInstructionRva;
    std::uintptr_t parentOffsetInstructionRva;
};

constexpr ServerNativeArrayConstructionSite
    kServerNativeArrayConstructionSites[] = {
        {0x51C9B7, 0x51C9BF},
        {0x51CA13, 0x51CA1B},
        {0x51CA70, 0x51CA78},
        {0x51CACC, 0x51CAD4},
        {0x51CB2D, 0x51CB35},
        {0x51CB89, 0x51CB91},
        {0x51CBEA, 0x51CBF2},
        {0x51CC46, 0x51CC4E},
        {0x51CCA3, 0x51CCAB},
        {0x51CCFF, 0x51CD07},
        {0x51CD5C, 0x51CD64},
        {0x51CDB8, 0x51CDC0},
        {0x51CE15, 0x51CE1D},
    };

constexpr std::uintptr_t kClientNativeArrayCountInstructionRvas[] = {
    0xCE3F5,
    0xCE47B,
    0xCE501,
    0xCE587,
    0xCE60D,
    0xCE69A,
    0xCE720,
    0xCE7A6,
    0xCE82C,
    0xCE8B2,
    0xCE938,
    0xCE9BE,
    0xCEA44,
};

static_assert(
    std::size(kServerNativeArrayConstructionSites)
        == std::size(kServerNetworkFields));
static_assert(
    std::size(kClientNativeArrayCountInstructionRvas)
        == std::size(kClientNetworkFields));

bool ValidateServerSendArray(
    std::uintptr_t serverBase,
    const NetworkArrayField& field)
{
    constexpr std::size_t kSendPropSize = 0x88;
    constexpr std::size_t kSendPropNameOffset = 0x48;
    constexpr std::size_t kSendPropTableOffset = 0x70;
    constexpr std::size_t kSendPropDataOffset = 0x78;
    auto* parent = reinterpret_cast<unsigned char*>(serverBase + field.parentRva);
    if (!IsReadableRange(parent, kSendPropSize))
        return false;

    auto* childTable =
        *reinterpret_cast<unsigned char**>(parent + kSendPropTableOffset);
    if (!IsReadableRange(childTable, 16))
        return false;

    auto* oldChildren = *reinterpret_cast<unsigned char**>(childTable);
    const int oldCount = *reinterpret_cast<int*>(childTable + 8);
    const int parentOffset =
        *reinterpret_cast<int*>(parent + kSendPropDataOffset);
    if (oldCount != kNewPlayerResourceElements
        || parentOffset != field.newOffset
        || !IsReadableRange(
            oldChildren,
            kNewPlayerResourceElements * kSendPropSize))
    {
        PlayerResourceLog(
            "R1Delta: invalid native TFO SendTable construction for %s: "
            "childCount=%d parentOffset=%d\n",
            field.name,
            oldCount,
            parentOffset);
        return false;
    }

    for (int index = kFirstExtendedPlayerIndex;
         index <= kLastExtendedPlayerIndex;
         ++index)
    {
        const auto* child =
            oldChildren + index * kSendPropSize;
        const char* name =
            *reinterpret_cast<const char* const*>(
                child + kSendPropNameOffset);
        const int offset =
            *reinterpret_cast<const int*>(
                child + kSendPropDataOffset);
        const char* expectedName =
            index == kFirstExtendedPlayerIndex
            ? kElement17Name
            : kElement18Name;
        if (!name || std::strcmp(name, expectedName) != 0
            || offset != index * 4)
        {
            PlayerResourceLog(
                "R1Delta: invalid native TFO SendProp %s[%d]: "
                "name=%s offset=%d\n",
                field.name,
                index,
                name ? name : "<null>",
                offset);
            return false;
        }
    }
    return true;
}

bool AdjustClientRecvArray(
    std::uintptr_t clientBase,
    const NetworkArrayField& field)
{
    constexpr std::size_t kRecvPropSize = 0x60;
    constexpr std::size_t kRecvPropNameOffset = 0x00;
    constexpr std::size_t kRecvPropTableOffset = 0x40;
    constexpr std::size_t kRecvPropDataOffset = 0x48;
    auto* parent = reinterpret_cast<unsigned char*>(clientBase + field.parentRva);
    if (!IsReadableRange(parent, kRecvPropSize))
        return false;

    auto* childTable =
        *reinterpret_cast<unsigned char**>(parent + kRecvPropTableOffset);
    if (!IsReadableRange(childTable, 16))
        return false;

    auto* oldChildren = *reinterpret_cast<unsigned char**>(childTable);
    const int oldCount = *reinterpret_cast<int*>(childTable + 8);
    const int parentOffset =
        *reinterpret_cast<int*>(parent + kRecvPropDataOffset);
    if (oldCount != kNewPlayerResourceElements
        || parentOffset != field.oldOffset
        || !IsReadableRange(
            oldChildren,
            kNewPlayerResourceElements * kRecvPropSize))
    {
        PlayerResourceLog(
            "R1Delta: refusing native R1 RecvArray adjustment for %s: "
            "childCount=%d parentOffset=%d\n",
            field.name,
            oldCount,
            parentOffset);
        return false;
    }

    bool ok = true;
    for (int index = kFirstExtendedPlayerIndex;
         index <= kLastExtendedPlayerIndex;
         ++index)
    {
        auto* child = oldChildren + index * kRecvPropSize;
        const char* name =
            *reinterpret_cast<const char* const*>(
                child + kRecvPropNameOffset);
        const char* expectedName =
            index == kFirstExtendedPlayerIndex
            ? kElement17Name
            : kElement18Name;
        if (!name || std::strcmp(name, expectedName) != 0)
        {
            PlayerResourceLog(
                "R1Delta: invalid native R1 RecvProp %s[%d] name=%s\n",
                field.name,
                index,
                name ? name : "<null>");
            return false;
        }

        const int relativeOffset =
            field.newOffset - field.oldOffset
            + (index - kFirstExtendedPlayerIndex) * 4;
        ok &= WriteValue(
            reinterpret_cast<int*>(
                child + kRecvPropDataOffset),
            relativeOffset);
    }
    return ok;
}

std::uintptr_t s_ServerBase = 0;
std::uintptr_t s_ClientBase = 0;

const NetworkArrayField* FindClientNetworkField(const void* parent)
{
    const auto parentAddress =
        reinterpret_cast<std::uintptr_t>(parent);
    for (const auto& field : kClientNetworkFields)
    {
        if (parentAddress == s_ClientBase + field.parentRva)
            return &field;
    }
    return nullptr;
}

using StaticTableInitializerFn = std::int64_t(__fastcall*)();
StaticTableInitializerFn s_ServerPlayerResourceTableInitializerOriginal = nullptr;
StaticTableInitializerFn s_ClientPlayerResourceTableInitializerOriginal = nullptr;

using ClientRecvArrayConstructorFn = void*(__fastcall*)(
    void* parent,
    const char* name,
    int offset,
    int elementStride,
    std::int64_t elementCount,
    void* elementProp,
    void* arrayLengthProxy);
ClientRecvArrayConstructorFn s_ClientRecvArrayConstructorOriginal = nullptr;

void* __fastcall ClientRecvArrayConstructor(
    void* parent,
    const char* name,
    int offset,
    int elementStride,
    std::int64_t elementCount,
    void* elementProp,
    void* arrayLengthProxy)
{
    void* result = s_ClientRecvArrayConstructorOriginal
        ? s_ClientRecvArrayConstructorOriginal(
            parent,
            name,
            offset,
            elementStride,
            elementCount,
            elementProp,
            arrayLengthProxy)
        : parent;

    if (elementCount == kNewPlayerResourceElements)
    {
        if (const NetworkArrayField* field =
                FindClientNetworkField(parent))
        {
            if (!AdjustClientRecvArray(s_ClientBase, *field))
            {
                PlayerResourceLog(
                    "R1Delta: native R1 RecvArray adjustment FAILED "
                    "for %s\n",
                    field->name);
            }
        }
    }
    return result;
}

std::int64_t __fastcall ServerPlayerResourceTableInitializer()
{
    const std::int64_t result =
        s_ServerPlayerResourceTableInitializerOriginal
        ? s_ServerPlayerResourceTableInitializerOriginal()
        : 0;

    bool ok = true;
    for (const auto& field : kServerNetworkFields)
        ok &= ValidateServerSendArray(s_ServerBase, field);
    PlayerResourceLog(
        "R1Delta: native TFO CPlayerResource 19-element SendTables %s\n",
        ok ? "installed" : "FAILED");
    return result;
}

std::int64_t __fastcall ClientPlayerResourceTableInitializer()
{
    const std::int64_t result =
        s_ClientPlayerResourceTableInitializerOriginal
        ? s_ClientPlayerResourceTableInitializerOriginal()
        : 0;

    bool ok = true;
    for (const auto& field : kClientNetworkFields)
        ok &= AdjustClientRecvArray(s_ClientBase, field);
    PlayerResourceLog(
        "R1Delta: native R1 C_PlayerResource 19-element RecvTables %s\n",
        ok ? "installed" : "FAILED");
    return result;
}

using ClientPlayerResourceConstructorFn = void*(__fastcall*)(void*);
using ClientPlayerResourceThinkFn = std::int64_t(__fastcall*)(void*);
using ClientPlayerResourceNameFn = char*(__fastcall*)(void*, int);
using ClientPlayerResourceBoolFn = bool(__fastcall*)(void*, int);
using ClientPlayerResourceValueFn = std::int64_t(__fastcall*)(void*, int);

ClientPlayerResourceConstructorFn s_ClientPlayerResourceConstructorOriginal = nullptr;
ClientPlayerResourceThinkFn s_ClientPlayerResourceThinkOriginal = nullptr;
ClientPlayerResourceNameFn s_ClientPlayerResourceNameOriginal = nullptr;
ClientPlayerResourceBoolFn s_ClientPlayerResourceConnectedOriginal = nullptr;
ClientPlayerResourceBoolFn s_ClientPlayerResourceAliveOriginal = nullptr;
ClientPlayerResourceValueFn s_ClientPlayerResourceTeamOriginal = nullptr;
ClientPlayerResourceValueFn s_ClientPlayerResourcePingOriginal = nullptr;
ClientPlayerResourceValueFn s_ClientPlayerResourceScoreOriginal = nullptr;
ClientPlayerResourceValueFn s_ClientPlayerResourceKillsOriginal = nullptr;
ClientPlayerResourceValueFn s_ClientPlayerResourceDeathsOriginal = nullptr;
ClientPlayerResourceValueFn s_ClientPlayerResourceHealthOriginal = nullptr;
ClientPlayerResourceValueFn s_ClientPlayerResourceTitanKillsOriginal = nullptr;
ClientPlayerResourceValueFn s_ClientPlayerResourceNpcKillsOriginal = nullptr;
ClientPlayerResourceValueFn s_ClientPlayerResourceAssistsOriginal = nullptr;
ClientPlayerResourceValueFn s_ClientPlayerResourceAssaultScoreOriginal = nullptr;
ClientPlayerResourceValueFn s_ClientPlayerResourceDefenseScoreOriginal = nullptr;

unsigned char* ClientRawData(void* gameResources)
{
    return reinterpret_cast<unsigned char*>(gameResources)
        - kClientGameResourcesOffset;
}

bool IsExtendedPlayerIndex(int playerIndex)
{
    return playerIndex >= kFirstExtendedPlayerIndex
        && playerIndex <= kLastExtendedPlayerIndex;
}

int ExtendedElement(int playerIndex)
{
    return playerIndex - kFirstExtendedPlayerIndex;
}

int& ClientExtendedInt(
    void* gameResources,
    std::size_t fieldOffset,
    int playerIndex)
{
    return reinterpret_cast<int*>(
        ClientRawData(gameResources) + fieldOffset)[ExtendedElement(playerIndex)];
}

const char*& ClientExtendedName(void* gameResources, int playerIndex)
{
    return reinterpret_cast<const char**>(
        ClientRawData(gameResources) + kClientExtendedNamesOffset)
        [ExtendedElement(playerIndex)];
}

char* ClientUnconnectedName()
{
    return reinterpret_cast<char*>(s_ClientBase + 0xBECED0);
}

void* __fastcall ClientPlayerResourceConstructor(void* self)
{
    void* result = s_ClientPlayerResourceConstructorOriginal
        ? s_ClientPlayerResourceConstructorOriginal(self)
        : self;
    if (result)
    {
        auto* raw = reinterpret_cast<unsigned char*>(result);
        std::memset(
            raw + kClientOldObjectSize,
            0,
            kClientNewObjectSize - kClientOldObjectSize);
        auto** names = reinterpret_cast<const char**>(
            raw + kClientExtendedNamesOffset);
        names[0] = ClientUnconnectedName();
        names[1] = ClientUnconnectedName();
    }
    return result;
}

void RefreshExtendedPlayerName(void* thinkThis, int playerIndex)
{
    // C_PlayerResource::ClientThink receives the object+24 base.
    auto* raw = reinterpret_cast<unsigned char*>(thinkThis) - 24;
    void* gameResources = raw + kClientGameResourcesOffset;
    const char* candidate = ClientUnconnectedName();
    bool hasPlayerInfo = false;

    const bool connected =
        ClientExtendedInt(
            gameResources,
            kClientExtendedConnectedOffset,
            playerIndex) != 0;
    auto** playerInfoProvider =
        reinterpret_cast<void**>(s_ClientBase + 0xBF51E8);
    if (connected && playerInfoProvider && *playerInfoProvider)
    {
        std::array<unsigned char, 0x268> playerInfo{};
        void* provider = *playerInfoProvider;
        auto** vtable = *reinterpret_cast<void***>(provider);
        using GetPlayerInfoFn =
            bool(__fastcall*)(void*, int, void*);
        auto getPlayerInfo =
            reinterpret_cast<GetPlayerInfoFn>(vtable[16]);
        if (getPlayerInfo
            && getPlayerInfo(provider, playerIndex, playerInfo.data()))
        {
            candidate = reinterpret_cast<const char*>(
                playerInfo.data() + 8);
            hasPlayerInfo = true;
        }
    }

    using CompareFn = int(__fastcall*)(const char*, const char*);
    using InternFn = const char*(__fastcall*)(const char*);
    const auto compare =
        reinterpret_cast<CompareFn>(s_ClientBase + 0x6564C0);
    const auto intern =
        reinterpret_cast<InternFn>(s_ClientBase + 0x16EBF0);
    const char*& stored = ClientExtendedName(gameResources, playerIndex);
    if (!stored || (compare && compare(stored, candidate) != 0))
    {
        stored = intern ? intern(candidate) : candidate;
    }

    if (hasPlayerInfo)
    {
        using GetPlayerEntityFn = void*(__fastcall*)(int);
        const auto getPlayerEntity =
            reinterpret_cast<GetPlayerEntityFn>(s_ClientBase + 0x280FE0);
        if (getPlayerEntity)
        {
            if (void* player = getPlayerEntity(playerIndex))
            {
                *reinterpret_cast<const char**>(
                    reinterpret_cast<unsigned char*>(player) + 0x3CD8) =
                    stored;
            }
        }
    }
}

std::int64_t __fastcall ClientPlayerResourceThink(void* self)
{
    const std::int64_t result = s_ClientPlayerResourceThinkOriginal
        ? s_ClientPlayerResourceThinkOriginal(self)
        : 0;
    RefreshExtendedPlayerName(self, 17);
    RefreshExtendedPlayerName(self, 18);
    return result;
}

bool __fastcall ClientPlayerResourceConnected(
    void* gameResources,
    int playerIndex)
{
    if (IsExtendedPlayerIndex(playerIndex))
    {
        return ClientExtendedInt(
            gameResources,
            kClientExtendedConnectedOffset,
            playerIndex) != 0;
    }
    if (playerIndex < 0 || playerIndex > kLastExtendedPlayerIndex)
        return false;
    return s_ClientPlayerResourceConnectedOriginal
        ? s_ClientPlayerResourceConnectedOriginal(gameResources, playerIndex)
        : false;
}

bool __fastcall ClientPlayerResourceAlive(
    void* gameResources,
    int playerIndex)
{
    if (IsExtendedPlayerIndex(playerIndex))
    {
        return ClientExtendedInt(
            gameResources,
            kClientExtendedAliveOffset,
            playerIndex) != 0;
    }
    if (playerIndex < 0 || playerIndex > kLastExtendedPlayerIndex)
        return false;
    return s_ClientPlayerResourceAliveOriginal
        ? s_ClientPlayerResourceAliveOriginal(gameResources, playerIndex)
        : false;
}

std::int64_t __fastcall ClientPlayerResourceTeam(
    void* gameResources,
    int playerIndex)
{
    if (IsExtendedPlayerIndex(playerIndex))
    {
        return static_cast<std::uint32_t>(ClientExtendedInt(
            gameResources,
            kClientExtendedTeamOffset,
            playerIndex));
    }
    if (playerIndex < 0 || playerIndex > kLastExtendedPlayerIndex)
        return 0;
    return s_ClientPlayerResourceTeamOriginal
        ? s_ClientPlayerResourceTeamOriginal(gameResources, playerIndex)
        : 0;
}

char* __fastcall ClientPlayerResourceName(
    void* gameResources,
    int playerIndex)
{
    if (IsExtendedPlayerIndex(playerIndex))
    {
        if (!ClientPlayerResourceConnected(gameResources, playerIndex))
            return ClientUnconnectedName();
        const char* name = ClientExtendedName(gameResources, playerIndex);
        return const_cast<char*>(name ? name : ClientUnconnectedName());
    }
    if (playerIndex < 0 || playerIndex > kLastExtendedPlayerIndex)
        return ClientUnconnectedName();
    return s_ClientPlayerResourceNameOriginal
        ? s_ClientPlayerResourceNameOriginal(gameResources, playerIndex)
        : ClientUnconnectedName();
}

std::int64_t ClientExtendedValue(
    void* gameResources,
    int playerIndex,
    std::size_t fieldOffset)
{
    if (!ClientPlayerResourceConnected(gameResources, playerIndex))
        return 0;
    return static_cast<std::uint32_t>(
        ClientExtendedInt(gameResources, fieldOffset, playerIndex));
}

#define DEFINE_CLIENT_VALUE_HOOK(name, fieldOffset, originalVariable)       \
    std::int64_t __fastcall name(void* gameResources, int playerIndex)      \
    {                                                                       \
        if (IsExtendedPlayerIndex(playerIndex))                             \
            return ClientExtendedValue(                                    \
                gameResources, playerIndex, fieldOffset);                   \
        if (playerIndex < 0 || playerIndex > kLastExtendedPlayerIndex)      \
            return 0;                                                       \
        return originalVariable                                             \
            ? originalVariable(gameResources, playerIndex)                 \
            : 0;                                                           \
    }

DEFINE_CLIENT_VALUE_HOOK(
    ClientPlayerResourcePing,
    kClientExtendedPingOffset,
    s_ClientPlayerResourcePingOriginal)
DEFINE_CLIENT_VALUE_HOOK(
    ClientPlayerResourceScore,
    kClientExtendedScoreOffset,
    s_ClientPlayerResourceScoreOriginal)
DEFINE_CLIENT_VALUE_HOOK(
    ClientPlayerResourceKills,
    kClientExtendedKillsOffset,
    s_ClientPlayerResourceKillsOriginal)
DEFINE_CLIENT_VALUE_HOOK(
    ClientPlayerResourceDeaths,
    kClientExtendedDeathsOffset,
    s_ClientPlayerResourceDeathsOriginal)
DEFINE_CLIENT_VALUE_HOOK(
    ClientPlayerResourceHealth,
    kClientExtendedHealthOffset,
    s_ClientPlayerResourceHealthOriginal)
DEFINE_CLIENT_VALUE_HOOK(
    ClientPlayerResourceTitanKills,
    kClientExtendedTitanKillsOffset,
    s_ClientPlayerResourceTitanKillsOriginal)
DEFINE_CLIENT_VALUE_HOOK(
    ClientPlayerResourceNpcKills,
    kClientExtendedNpcKillsOffset,
    s_ClientPlayerResourceNpcKillsOriginal)
DEFINE_CLIENT_VALUE_HOOK(
    ClientPlayerResourceAssists,
    kClientExtendedAssistsOffset,
    s_ClientPlayerResourceAssistsOriginal)
DEFINE_CLIENT_VALUE_HOOK(
    ClientPlayerResourceAssaultScore,
    kClientExtendedAssaultScoreOffset,
    s_ClientPlayerResourceAssaultScoreOriginal)
DEFINE_CLIENT_VALUE_HOOK(
    ClientPlayerResourceDefenseScore,
    kClientExtendedDefenseScoreOffset,
    s_ClientPlayerResourceDefenseScoreOriginal)

#undef DEFINE_CLIENT_VALUE_HOOK

bool PatchServerFieldReferences(
    std::uintptr_t serverBase,
    const char* fieldName,
    int oldOffset,
    int newOffset,
    std::initializer_list<std::uintptr_t> instructionRvas)
{
    bool ok = true;
    for (const std::uintptr_t rva : instructionRvas)
    {
        ok &= PatchInstructionDword(
            serverBase,
            rva,
            oldOffset,
            newOffset,
            fieldName);
    }
    return ok;
}
}

bool InstallTFOServerPlayerResource18(std::uintptr_t serverBase)
{
    if (!serverBase)
        return false;

    s_ServerBase = serverBase;
    bool ok = true;

    const unsigned char expectedCount[] = {0x45, 0x8D, 0x77, 0x11};
    const unsigned char replacementCount[] = {0x45, 0x8D, 0x77, 0x13};
    ok &= PatchBytesIfMatch(
        serverBase,
        0x51D054,
        expectedCount,
        replacementCount,
        sizeof(expectedCount),
        "CPlayerResource reset element count");

    const unsigned char expectedSizeGetter[] = {0xB8, 0xB0, 0x09, 0x00, 0x00};
    const unsigned char replacementSizeGetter[] = {0xB8, 0x90, 0x0D, 0x00, 0x00};
    const unsigned char expectedSizeEdx[] = {0xBA, 0xB0, 0x09, 0x00, 0x00};
    const unsigned char replacementSizeEdx[] = {0xBA, 0x90, 0x0D, 0x00, 0x00};
    ok &= PatchBytesIfMatch(
        serverBase,
        0x51D9B0,
        expectedSizeGetter,
        replacementSizeGetter,
        sizeof(expectedSizeGetter),
        "CPlayerResource entity size getter");
    ok &= PatchBytesIfMatch(
        serverBase,
        0x51DADB,
        expectedSizeEdx,
        replacementSizeEdx,
        sizeof(expectedSizeEdx),
        "CPlayerResource factory allocation size");
    ok &= PatchBytesIfMatch(
        serverBase,
        0x51DB86,
        expectedSizeEdx,
        replacementSizeEdx,
        sizeof(expectedSizeEdx),
        "CPlayerResource placement delete size");

    ok &= PatchServerFieldReferences(serverBase, "m_iPing", 1592, kServerExtendedPingOffset,
        {0x51D060, 0x51D090, 0x51D899, 0x51D8C0, 0x51D8D4, 0x51D8F8});
    ok &= PatchServerFieldReferences(serverBase, "m_score", 1660, kServerExtendedScoreOffset,
        {0x51D098, 0x51D0C5, 0x51D569, 0x51D596});
    ok &= PatchServerFieldReferences(serverBase, "m_kills", 1728, kServerExtendedKillsOffset,
        {0x51D0CD, 0x51D0FA, 0x51D5A4, 0x51D5D1});
    ok &= PatchServerFieldReferences(serverBase, "m_deaths", 1796, kServerExtendedDeathsOffset,
        {0x51D102, 0x51D12F, 0x51D5DF, 0x51D60C});
    ok &= PatchServerFieldReferences(serverBase, "m_bConnected", 1864, kServerExtendedConnectedOffset,
        {0x51D137, 0x51D164, 0x51D614, 0x51D642, 0x51D905, 0x51D932});
    ok &= PatchServerFieldReferences(serverBase, "m_iTeam", 1932, kServerExtendedTeamOffset,
        {0x51D16C, 0x51D199, 0x51D654, 0x51D681});
    ok &= PatchServerFieldReferences(serverBase, "m_bAlive", 2000, kServerExtendedAliveOffset,
        {0x51D1A1, 0x51D1CE, 0x51D6B6, 0x51D6E3});
    ok &= PatchServerFieldReferences(serverBase, "m_iPRHealth", 2068, kServerExtendedHealthOffset,
        {0x51D6F7, 0x51D724});
    ok &= PatchServerFieldReferences(serverBase, "m_titanKills", 2136, kServerExtendedTitanKillsOffset,
        {0x51D1D6, 0x51D203, 0x51D732, 0x51D75F});
    ok &= PatchServerFieldReferences(serverBase, "m_npcKills", 2204, kServerExtendedNpcKillsOffset,
        {0x51D20B, 0x51D238, 0x51D76D, 0x51D79A});
    ok &= PatchServerFieldReferences(serverBase, "m_assists", 2272, kServerExtendedAssistsOffset,
        {0x51D240, 0x51D26D, 0x51D7A8, 0x51D7D5});
    ok &= PatchServerFieldReferences(serverBase, "m_assaultScore", 2340, kServerExtendedAssaultScoreOffset,
        {0x51D275, 0x51D2A2, 0x51D7E3, 0x51D810});
    ok &= PatchServerFieldReferences(serverBase, "m_defenseScore", 2408, kServerExtendedDefenseScoreOffset,
        {0x51D2AA, 0x51D2D7, 0x51D81E, 0x51D84B});

    for (std::size_t index = 0;
         index < std::size(kServerNetworkFields);
         ++index)
    {
        const auto& field = kServerNetworkFields[index];
        const auto& site =
            kServerNativeArrayConstructionSites[index];
        ok &= PatchInstructionDword(
            serverBase,
            site.countInstructionRva,
            kOldPlayerResourceElements,
            kNewPlayerResourceElements,
            field.name);
        ok &= PatchInstructionDword(
            serverBase,
            site.parentOffsetInstructionRva,
            field.oldOffset,
            field.newOffset,
            field.name);
    }

    const unsigned char expectedInitializer[] = {
        0x48, 0x8B, 0xC4, 0x55, 0x48, 0x8D, 0x68, 0x98,
        0x48, 0x81, 0xEC, 0x60, 0x01, 0x00, 0x00, 0x48
    };
    ok &= InstallCheckedHook(
        serverBase,
        0x51C8E0,
        expectedInitializer,
        sizeof(expectedInitializer),
        reinterpret_cast<void*>(&ServerPlayerResourceTableInitializer),
        reinterpret_cast<void**>(&s_ServerPlayerResourceTableInitializerOriginal),
        "TFO DT_PlayerResource initializer");

    PlayerResourceLog(
        "R1Delta: TFO CPlayerResource 18-player storage patches %s\n",
        ok ? "installed" : "FAILED");
    return ok;
}

bool InstallR1ClientPlayerResource18(std::uintptr_t clientBase)
{
    if (!clientBase || IsDedicatedServer())
        return false;

    s_ClientBase = clientBase;
    bool ok = true;

    const unsigned char expectedAllocation[] = {0xB9, 0x40, 0x1F, 0x00, 0x00};
    const unsigned char replacementAllocation[] = {0xB9, 0xC0, 0x1F, 0x00, 0x00};
    ok &= PatchBytesIfMatch(
        clientBase,
        0xCFE91,
        expectedAllocation,
        replacementAllocation,
        sizeof(expectedAllocation),
        "C_PlayerResource allocation size");

    const unsigned char expectedThinkBound[] = {0x3B, 0x5A, 0x20};
    const unsigned char replacementThinkBound[] = {0x83, 0xFB, 0x10};
    ok &= PatchBytesIfMatch(
        clientBase,
        0xCEDBA,
        expectedThinkBound,
        replacementThinkBound,
        sizeof(expectedThinkBound),
        "C_PlayerResource original-name loop bound");

    for (std::size_t index = 0;
         index < std::size(kClientNetworkFields);
         ++index)
    {
        ok &= PatchInstructionDword(
            clientBase,
            kClientNativeArrayCountInstructionRvas[index],
            kOldPlayerResourceElements,
            kNewPlayerResourceElements,
            kClientNetworkFields[index].name);
    }

    const unsigned char expectedInitializer[] = {
        0x4C, 0x8B, 0xDC, 0x55, 0x49, 0x8D, 0x6B, 0xA1,
        0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00, 0x8B
    };
    const unsigned char expectedRecvArrayConstructor[] = {
        0x4C, 0x89, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x54,
        0x24, 0x10, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x53
    };
    const unsigned char expectedConstructor[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
        0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57
    };
    const unsigned char expectedThink[] = {
        0x48, 0x8B, 0xC4, 0x53, 0x41, 0x54, 0x48, 0x81,
        0xEC, 0x78, 0x02, 0x00, 0x00, 0x48, 0x8B, 0x15
    };
    const unsigned char expectedName[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x56, 0x48, 0x83,
        0xEC, 0x20, 0x48, 0x8B, 0x01, 0x48, 0x63, 0xF2
    };
    const unsigned char expectedConnected[] = {
        0x80, 0x3D, 0xC1, 0xD5, 0xB1, 0x00, 0x00, 0x48,
        0x8B, 0xC1, 0x74, 0x13, 0x48, 0x63, 0xC2, 0x48
    };
    const unsigned char expectedAlive[] = {
        0x80, 0x3D, 0x81, 0xDB, 0xB1, 0x00, 0x00, 0x48,
        0x8B, 0xC1, 0x74, 0x13, 0x48, 0x63, 0xC2, 0x48
    };
    const unsigned char expectedTeam[] = {
        0x80, 0x3D, 0x51, 0xDB, 0xB1, 0x00, 0x00, 0x48,
        0x63, 0xC2, 0x74, 0x0B, 0x48, 0x8D, 0x0D, 0x49
    };
    const unsigned char expectedValue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
        0xEC, 0x20, 0x48, 0x8B, 0x01, 0x48, 0x63, 0xDA
    };

    ok &= InstallCheckedHook(
        clientBase,
        0x13C5A0,
        expectedRecvArrayConstructor,
        sizeof(expectedRecvArrayConstructor),
        reinterpret_cast<void*>(&ClientRecvArrayConstructor),
        reinterpret_cast<void**>(&s_ClientRecvArrayConstructorOriginal),
        "R1 RecvProp array constructor");
    ok &= InstallCheckedHook(clientBase, 0xCE320, expectedInitializer, sizeof(expectedInitializer),
        reinterpret_cast<void*>(&ClientPlayerResourceTableInitializer),
        reinterpret_cast<void**>(&s_ClientPlayerResourceTableInitializerOriginal),
        "R1 DT_PlayerResource initializer");
    ok &= InstallCheckedHook(clientBase, 0xCFB80, expectedConstructor, sizeof(expectedConstructor),
        reinterpret_cast<void*>(&ClientPlayerResourceConstructor),
        reinterpret_cast<void**>(&s_ClientPlayerResourceConstructorOriginal),
        "R1 C_PlayerResource constructor");
    ok &= InstallCheckedHook(clientBase, 0xCECB0, expectedThink, sizeof(expectedThink),
        reinterpret_cast<void*>(&ClientPlayerResourceThink),
        reinterpret_cast<void**>(&s_ClientPlayerResourceThinkOriginal),
        "R1 C_PlayerResource::ClientThink");
    ok &= InstallCheckedHook(clientBase, 0xCEE20, expectedName, sizeof(expectedName),
        reinterpret_cast<void*>(&ClientPlayerResourceName),
        reinterpret_cast<void**>(&s_ClientPlayerResourceNameOriginal),
        "R1 IGameResources::GetPlayerName");
    ok &= InstallCheckedHook(clientBase, 0xCF490, expectedConnected, sizeof(expectedConnected),
        reinterpret_cast<void*>(&ClientPlayerResourceConnected),
        reinterpret_cast<void**>(&s_ClientPlayerResourceConnectedOriginal),
        "R1 IGameResources::IsConnected");
    ok &= InstallCheckedHook(clientBase, 0xCEED0, expectedAlive, sizeof(expectedAlive),
        reinterpret_cast<void*>(&ClientPlayerResourceAlive),
        reinterpret_cast<void**>(&s_ClientPlayerResourceAliveOriginal),
        "R1 IGameResources::IsAlive");
    ok &= InstallCheckedHook(clientBase, 0xCEF00, expectedTeam, sizeof(expectedTeam),
        reinterpret_cast<void*>(&ClientPlayerResourceTeam),
        reinterpret_cast<void**>(&s_ClientPlayerResourceTeamOriginal),
        "R1 IGameResources::GetTeam");

    struct ValueHook
    {
        std::uintptr_t rva;
        void* detour;
        void** original;
        const char* name;
    };
    const ValueHook valueHooks[] = {
        {0xCF080, reinterpret_cast<void*>(&ClientPlayerResourcePing), reinterpret_cast<void**>(&s_ClientPlayerResourcePingOriginal), "GetPing"},
        {0xCF0E0, reinterpret_cast<void*>(&ClientPlayerResourceScore), reinterpret_cast<void**>(&s_ClientPlayerResourceScoreOriginal), "GetScore"},
        {0xCF140, reinterpret_cast<void*>(&ClientPlayerResourceKills), reinterpret_cast<void**>(&s_ClientPlayerResourceKillsOriginal), "GetKills"},
        {0xCF1A0, reinterpret_cast<void*>(&ClientPlayerResourceDeaths), reinterpret_cast<void**>(&s_ClientPlayerResourceDeathsOriginal), "GetDeaths"},
        {0xCF200, reinterpret_cast<void*>(&ClientPlayerResourceHealth), reinterpret_cast<void**>(&s_ClientPlayerResourceHealthOriginal), "GetHealth"},
        {0xCF260, reinterpret_cast<void*>(&ClientPlayerResourceTitanKills), reinterpret_cast<void**>(&s_ClientPlayerResourceTitanKillsOriginal), "GetTitanKills"},
        {0xCF2C0, reinterpret_cast<void*>(&ClientPlayerResourceNpcKills), reinterpret_cast<void**>(&s_ClientPlayerResourceNpcKillsOriginal), "GetNPCKills"},
        {0xCF320, reinterpret_cast<void*>(&ClientPlayerResourceAssists), reinterpret_cast<void**>(&s_ClientPlayerResourceAssistsOriginal), "GetAssists"},
        {0xCF380, reinterpret_cast<void*>(&ClientPlayerResourceAssaultScore), reinterpret_cast<void**>(&s_ClientPlayerResourceAssaultScoreOriginal), "GetAssaultScore"},
        {0xCF3E0, reinterpret_cast<void*>(&ClientPlayerResourceDefenseScore), reinterpret_cast<void**>(&s_ClientPlayerResourceDefenseScoreOriginal), "GetDefenseScore"},
    };
    for (const auto& hook : valueHooks)
    {
        ok &= InstallCheckedHook(
            clientBase,
            hook.rva,
            expectedValue,
            sizeof(expectedValue),
            hook.detour,
            hook.original,
            hook.name);
    }

    PlayerResourceLog(
        "R1Delta: R1 C_PlayerResource 18-player client extension %s\n",
        ok ? "installed" : "FAILED");
    return ok;
}
