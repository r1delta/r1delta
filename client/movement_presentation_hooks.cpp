#include "movement_presentation_hooks.h"

#include "../shared/movement_runtime.h"
#include "slide_model_presentation.h"
#include <MinHook.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace r1delta::movement::presentation
{
namespace
{
constexpr char kClientDigest[] = "5b3f00805568eeaa6ee1cf889f12dc4570eb28a3fad2626ec0578eae3e7afd5e";
constexpr char kServerDigest[] = "50faad928e0715a51ad624d0a3eb8f00f7ffad9e66258dc95c960f9d11ff41e4";
constexpr DWORD kClientTimestamp = 0x55038DB8;
constexpr DWORD kClientImageSize = 0x039A4000;
constexpr std::uintptr_t kCalcMainActivityRva = 0x333880;
constexpr std::uintptr_t kServerCalcMainActivityRva = 0x526D80;
constexpr std::uintptr_t kClientActivityLookupRva = 0x002C80;
constexpr std::uintptr_t kClientActivityInsertRva = 0x004940;
// Client activity-list version (dword_180AD00D8 in the retail image). Every CStudioHdr activity-to-sequence mapping
// stores the version it was built at and is rebuilt lazily when the global is newer; the native insert routine
// above never bumps it, so mappings built before a runtime insertion would never learn the new activities.
constexpr std::uintptr_t kClientActivityListVersionRva = 0xAD00D8;
constexpr std::uintptr_t kServerActivityLookupRva = 0x0707B0;
constexpr std::uintptr_t kServerActivityInsertRva = 0x0706F0;
constexpr std::uint32_t kInvalidHandle = 0xFFFFFFFFu;
constexpr std::size_t kRecordCount = 128;
constexpr std::size_t kSlideActivityCount = 7;
constexpr int kNoActivity = (-2147483647 - 1);
constexpr std::uint8_t kViewModelEnabled = 1u << 0;
constexpr std::uint8_t kFovEnabled = 1u << 1;
constexpr std::uint8_t kActivityEnabled = 1u << 2;
constexpr std::uint8_t kSlideEnabled = 1u << 3;

constexpr const char* kSlideActivityNames[kSlideActivityCount] = {
    "ACT_MP_CROUCHSLIDE",
    "ACT_MP_CROUCHSLIDE_AIR",
    "ACT_MP_CROUCHSLIDE_END_FORWARD",
    "ACT_MP_CROUCHSLIDE_END_BACKWARD_LEFT",
    "ACT_MP_CROUCHSLIDE_END_BACKWARD_RIGHT",
    "ACT_MP_CROUCHSLIDE_END_LEFT",
    "ACT_MP_CROUCHSLIDE_END_RIGHT"
};

constexpr unsigned char kClientActivityLookupGuard[] = {
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0xD1, 0x48,
    0x8D, 0x0D, 0x7A, 0x93, 0xBA, 0x00, 0xE8, 0xDD
};
constexpr unsigned char kClientActivityInsertGuard[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
    0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48
};
constexpr unsigned char kServerActivityLookupGuard[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0x05, 0xFB, 0x57, 0xBD, 0x00, 0x48, 0x89, 0x4C
};
constexpr unsigned char kServerActivityInsertGuard[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
    0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57
};

constexpr int kClientLastStockActivity = 826; // ACT_ZIPLINING
constexpr int kServerLastStockActivity = 839; // ACT_TITAN_SHOP_IDLE

constexpr unsigned char kCalcMainActivityGuard[] = {
    0x40, 0x53, 0x57, 0x48, 0x83, 0xEC, 0x38, 0x48,
    0x8B, 0x81, 0xC0, 0x01, 0x00, 0x00, 0x48, 0x8B
};
constexpr unsigned char kServerCalcMainActivityGuard[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B,
    0x81, 0xB8, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xD9
};

struct ActivityModelIdentity
{
    std::uintptr_t studio{};
    std::uintptr_t header{};
    std::uintptr_t virtualModel{};
};

struct PlayerPresentation
{
    std::uintptr_t player{};
    std::uint32_t handle{kInvalidHandle};
    std::uint32_t session{};
    float viewModel[6]{};
    float fovScale{1.0f};
    int activity{};
    ActivityModelIdentity activityModel{};
    ActivityModelIdentity slideModel{};
    float lastSlideTime{-10.0f};
    float slidingYaw{};
    bool slideActive{};
    bool slideModelAdmitted{};
    std::uint8_t enabled{};
};

struct ActivityRegistry
{
    std::uintptr_t base{};
    std::array<int, kSlideActivityCount> ids{{-1, -1, -1, -1, -1, -1, -1}};
    bool ready{};
};
using CalcMainActivity = int(__fastcall*)(std::uintptr_t animationState);

struct ServerSlideRecord
{
    std::uintptr_t player{};
    std::uint32_t handle{kInvalidHandle};
    std::uint32_t session{};
    ActivityModelIdentity model{};
    float lastSlideTime{-10.0f};
    float slidingYaw{};
    bool active{};
    bool modelAdmitted{};
};

void ClearServerSlideRecord(ServerSlideRecord& record) noexcept
{
    record = {};
    record.handle = kInvalidHandle;
}

std::array<ServerSlideRecord, kRecordCount> s_serverSlideRecords{};
SRWLOCK s_serverSlideLock = SRWLOCK_INIT;
std::atomic<bool> s_serverSlidePresent{false};
std::atomic<bool> s_serverHooksReady{false};
std::atomic<std::uintptr_t> s_serverBase{0};
std::atomic<CalcMainActivity> s_serverCalcMainActivityOriginal{nullptr};
std::uintptr_t s_serverActivityTarget{};

std::array<PlayerPresentation, kRecordCount> s_players{};
SRWLOCK s_playerLock = SRWLOCK_INIT;
SRWLOCK s_installLock = SRWLOCK_INIT;
SRWLOCK s_activityLock = SRWLOCK_INIT;
ActivityRegistry s_clientActivityRegistry{};
ActivityRegistry s_serverActivityRegistry{};
std::atomic<std::uintptr_t> s_clientBase{0};
std::atomic<CalcMainActivity> s_calcMainActivityOriginal{nullptr};
std::atomic<bool> s_clientReady{false};
std::atomic<bool> s_slideModelReady{false};
std::atomic<bool> s_serverSlideModelReady{false};
std::atomic<bool> s_clientPresentationPresent{false};
std::uintptr_t s_activityTarget{};

void ClearRecord(PlayerPresentation& record) noexcept
{
    record = {};
    record.handle = kInvalidHandle;
}


bool VerifyClientPe(std::uintptr_t base) noexcept
{
    if (!base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000)
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + static_cast<std::uintptr_t>(dos->e_lfanew));
    return nt->Signature == IMAGE_NT_SIGNATURE
        && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64
        && nt->FileHeader.TimeDateStamp == kClientTimestamp
        && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC
        && nt->OptionalHeader.SizeOfImage == kClientImageSize;
}

bool HasActivityGuard(std::uintptr_t base) noexcept
{
    return base && std::memcmp(reinterpret_cast<const void*>(base + kCalcMainActivityRva),
        kCalcMainActivityGuard, sizeof(kCalcMainActivityGuard)) == 0;
}

template<class T>
T ReadField(std::uintptr_t object, std::size_t offset) noexcept
{
    return *reinterpret_cast<const T*>(object + offset);
}

template<class T>
void WriteField(std::uintptr_t object, std::size_t offset, T value) noexcept
{
    *reinterpret_cast<T*>(object + offset) = value;
}

template<class Result, class... Arguments>
Result ClientCall(std::uintptr_t base, std::uintptr_t rva, Arguments... arguments) noexcept
{
    using Function = Result(__fastcall*)(Arguments...);
    return reinterpret_cast<Function>(base + rva)(arguments...);
}

int LookupClientActivity(std::uintptr_t base, const char* name) noexcept
{
    using Function = int(__fastcall*)(const char*);
    return reinterpret_cast<Function>(base + kClientActivityLookupRva)(name);
}

int LookupServerActivity(std::uintptr_t base, const char* name) noexcept
{
    using Function = std::uintptr_t(__fastcall*)(const char*);
    const auto record = reinterpret_cast<Function>(base + kServerActivityLookupRva)(name);
    return record ? ReadField<int>(record, 0) : -1;
}

int LookupNativeActivity(bool client, std::uintptr_t base, const char* name) noexcept
{
    return client ? LookupClientActivity(base, name) : LookupServerActivity(base, name);
}

std::uintptr_t InsertNativeActivity(bool client, std::uintptr_t base,
    const char* name, int activity) noexcept
{
    using Function = std::uintptr_t(__fastcall*)(const char*, int, std::uint8_t);
    const auto rva = client ? kClientActivityInsertRva : kServerActivityInsertRva;
    return reinterpret_cast<Function>(base + rva)(name, activity, 1);
}

bool VerifyServerPe(std::uintptr_t base) noexcept
{
    if (!base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000)
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + static_cast<std::uintptr_t>(dos->e_lfanew));
    return nt->Signature == IMAGE_NT_SIGNATURE
        && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64
        && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;
}

bool VerifyActivityRegistry(bool client, std::uintptr_t base) noexcept
{
    if (!base || !VerifyMovementModuleDigest(base, client ? kClientDigest : kServerDigest)
        || (client ? !VerifyClientPe(base) : !VerifyServerPe(base)))
        return false;

    const auto lookupRva = client ? kClientActivityLookupRva : kServerActivityLookupRva;
    const auto insertRva = client ? kClientActivityInsertRva : kServerActivityInsertRva;
    const auto* lookupGuard = client ? kClientActivityLookupGuard : kServerActivityLookupGuard;
    const auto* insertGuard = client ? kClientActivityInsertGuard : kServerActivityInsertGuard;
    if (std::memcmp(reinterpret_cast<const void*>(base + lookupRva),
            lookupGuard, sizeof(kClientActivityLookupGuard)) != 0
        || std::memcmp(reinterpret_cast<const void*>(base + insertRva),
            insertGuard, sizeof(kClientActivityInsertGuard)) != 0)
        return false;

    const auto maxIdOffset = client ? std::size_t{0xBABFE0} : std::size_t{0xC308B0};
    const auto tableOffset = client ? std::size_t{0xBAC010} : std::size_t{0xC45FB8};
    const int maxId = ReadField<int>(base, maxIdOffset);
    if (maxId < 0 || !ReadField<std::uintptr_t>(base, tableOffset))
        return false;

    // These are the real first/last entries in each module's native static list.
    // They establish that ClientDLL::Init / server activity-map initialization
    // ran without imposing an exact count or rejecting unrelated dynamic names.
    const char* lastName = client ? "ACT_ZIPLINING" : "ACT_TITAN_SHOP_IDLE";
    const int lastId = client ? kClientLastStockActivity : kServerLastStockActivity;
    return LookupNativeActivity(client, base, "ACT_RESET") == 0
        && LookupNativeActivity(client, base, lastName) == lastId
        && LookupNativeActivity(client, base, "ACT_MP_SLIDE") == 539
        && LookupNativeActivity(client, base, "ACT_MP_SLIDE_FORWARD") == 540
        && LookupNativeActivity(client, base, "ACT_MP_SLIDE_BACKWARD") == 541
        && LookupNativeActivity(client, base, "ACT_MP_SLIDE_LEFT") == 542
        && LookupNativeActivity(client, base, "ACT_MP_SLIDE_RIGHT") == 543;
}

bool RegisterActivitiesForModule(ActivityRegistry& registry, bool client,
    std::uintptr_t base) noexcept
{
    if (registry.base != base)
    {
        registry = {};
        registry.base = base;
    }
    if (registry.ready) return true;
    if (!VerifyActivityRegistry(client, base))
    {
        OutputDebugStringA(client
            ? "R1Delta movement: slide activity registry is not initialized or client profile mismatched.\n"
            : "R1Delta movement: slide activity registry is not initialized or server profile mismatched.\n");
        return false;
    }

    std::array<int, kSlideActivityCount> ids{};
    int maxId = ReadField<int>(base, client ? std::size_t{0xBABFE0} : std::size_t{0xC308B0});
    const int initialMaxId = maxId;

    // In a listen server the client and server share each model's studio header, and the server resolves the
    // slide sequences' activity names into the header's activity fields. The client registry must therefore
    // use the IDs the server registry holds for these names or its activity-to-sequence lookups can never match.
    std::array<int, kSlideActivityCount> authority{};
    bool useAuthority = false;
    if (client)
    {
        const auto serverBase = s_serverBase.load(std::memory_order_acquire);
        if (serverBase && RegisterActivitiesForModule(s_serverActivityRegistry, false, serverBase))
        {
            authority = s_serverActivityRegistry.ids;
            useAuthority = true;
        }
    }
    for (std::size_t i = 0; i != kSlideActivityCount; ++i)
    {
        ids[i] = LookupNativeActivity(client, base, kSlideActivityNames[i]);
        if (ids[i] < 0) continue;
        if (useAuthority && ids[i] != authority[i])
        {
            OutputDebugStringA("R1Delta movement: client slide activity IDs differ from the server registry.\n");
            return false;
        }
        if (ids[i] > maxId)
        {
            OutputDebugStringA("R1Delta movement: slide activity lookup exceeds the native registry maximum.\n");
            return false;
        }
        for (std::size_t previous = 0; previous != i; ++previous)
            if (ids[previous] == ids[i])
            {
                OutputDebugStringA("R1Delta movement: slide activity names collide in the native registry.\n");
                return false;
            }
    }

    bool inserted = false;
    for (std::size_t i = 0; i != kSlideActivityCount; ++i)
    {
        if (ids[i] >= 0) continue;
        if (maxId == (std::numeric_limits<int>::max)())
        {
            OutputDebugStringA("R1Delta movement: native activity registry ID space exhausted.\n");
            return false;
        }
        if (useAuthority && authority[i] <= initialMaxId)
        {
            OutputDebugStringA("R1Delta movement: server slide activity ID collides with the client registry range.\n");
            return false;
        }
        const int nextId = useAuthority ? authority[i] : ++maxId;
        if (nextId > maxId) maxId = nextId;
        const auto record = InsertNativeActivity(client, base, kSlideActivityNames[i], nextId);
        if (!record || ReadField<int>(record, 0) != nextId
            || !ReadField<std::uint16_t>(record, 6))
        {
            OutputDebugStringA("R1Delta movement: native slide activity insertion failed.\n");
            return false;
        }
        ids[i] = nextId;
        inserted = true;
    }

    if (inserted && client)
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(base + kClientActivityListVersionRva));

    registry.ids = ids;
    registry.ready = true;
    return true;
}

bool CopySlideActivityIds(bool client, std::array<int, kSlideActivityCount>& ids) noexcept
{
    AcquireSRWLockShared(&s_activityLock);
    const auto& registry = client ? s_clientActivityRegistry : s_serverActivityRegistry;
    const bool ready = registry.ready;
    if (ready) ids = registry.ids;
    ReleaseSRWLockShared(&s_activityLock);
    return ready;
}

template<class Result, class... Arguments>
Result PlayerAnimCall(std::uintptr_t object, std::size_t byteOffset, Arguments... arguments) noexcept
{
    const auto* table = *reinterpret_cast<std::uintptr_t* const*>(object);
    using Function = Result(__fastcall*)(std::uintptr_t, Arguments...);
    const auto function = reinterpret_cast<Function>(table[byteOffset / sizeof(std::uintptr_t)]);
    return function(object, arguments...);
}

std::uint32_t NativePlayerHandle(std::uintptr_t player) noexcept
{
    if (!player) return kInvalidHandle;
    const auto* table = *reinterpret_cast<std::uintptr_t* const*>(player);
    if (!table) return kInvalidHandle;
    using HandleAddress = const std::uint32_t*(__fastcall*)(std::uintptr_t);
    const auto function = reinterpret_cast<HandleAddress>(table[16 / sizeof(std::uintptr_t)]);
    const auto* handle = function ? function(player) : nullptr;
    return handle ? *handle : kInvalidHandle;
}

ActivityModelIdentity CurrentActivityModel(std::uintptr_t player) noexcept
{
    ActivityModelIdentity model{};
    model.studio = ReadField<std::uintptr_t>(player, 8856);
    if (model.studio)
    {
        model.header = ReadField<std::uintptr_t>(model.studio, 8);
        model.virtualModel = ReadField<std::uintptr_t>(model.studio, 16);
    }
    return model;
}

bool SameActivityModel(const ActivityModelIdentity& left,
    const ActivityModelIdentity& right) noexcept
{
    return left.studio == right.studio && left.header == right.header
        && left.virtualModel == right.virtualModel;
}

bool ResolveActivityForModel(std::uintptr_t base, std::uintptr_t player, int activity,
    ActivityModelIdentity& model) noexcept
{
    if (!base || !player) return false;
    const auto initialModel = CurrentActivityModel(player);
    if (!initialModel.studio || !initialModel.header) return false;
    const int sequence = ClientCall<int>(base, 0x062C80, player, activity);
    if (sequence < 0 || ClientCall<int>(base, 0x062ED0, player, sequence) != activity)
        return false;
    model = CurrentActivityModel(player);
    return model.studio && model.header;
}

ActivityModelIdentity CurrentServerActivityModel(std::uintptr_t player) noexcept
{
    ActivityModelIdentity model{};
    model.studio = ReadField<std::uintptr_t>(player, 2584);
    if (model.studio)
    {
        model.header = ReadField<std::uintptr_t>(model.studio, 8);
        model.virtualModel = ReadField<std::uintptr_t>(model.studio, 16);
    }
    return model;
}

bool ResolveServerActivityForModel(std::uintptr_t base, std::uintptr_t player,
    int activity, ActivityModelIdentity& model) noexcept
{
    if (!base || !player) return false;
    const auto initialModel = CurrentServerActivityModel(player);
    if (!initialModel.studio || !initialModel.header) return false;
    const int sequence = ClientCall<int>(base, 0x088730, player, activity);
    if (sequence < 0 || ClientCall<int>(base, 0x08ECB0, player, sequence) != activity)
        return false;
    model = CurrentServerActivityModel(player);
    return model.studio && model.header;
}


PlayerPresentation* WritableRecord(std::uintptr_t player, std::uint32_t handle,
    std::uint32_t session) noexcept
{
    PlayerPresentation* empty = nullptr;
    for (auto& record : s_players)
    {
        if (record.player == player)
        {
            if (record.handle == handle && record.session == session) return &record;
            ClearRecord(record);
            record.player = player;
            record.handle = handle;
            record.session = session;
            return &record;
        }
        if (!record.player && !empty) empty = &record;
    }
    if (empty)
    {
        empty->player = player;
        empty->handle = handle;
        empty->session = session;
    }
    return empty;
}

PlayerPresentation* FindRecord(std::uintptr_t player, std::uint32_t handle,
    std::uint32_t session) noexcept
{
    for (auto& record : s_players)
        if (record.player == player && record.handle == handle && record.session == session)
            return &record;
    return nullptr;
}

bool CopyRecord(std::uintptr_t player, std::uint32_t handle, std::uint32_t session,
    PlayerPresentation& output) noexcept
{
    AcquireSRWLockShared(&s_playerLock);
    const auto* record = FindRecord(player, handle, session);
    const bool found = record != nullptr;
    if (found) output = *record;
    ReleaseSRWLockShared(&s_playerLock);
    return found;
}

bool CopyActivityRecord(std::uintptr_t player, std::uint32_t handle,
    PlayerPresentation& output) noexcept
{
    AcquireSRWLockShared(&s_playerLock);
    const PlayerPresentation* found = nullptr;
    for (const auto& record : s_players)
        if (record.player == player && record.handle == handle)
        {
            found = &record;
            break;
        }
    const bool active = found && (found->enabled & (kActivityEnabled | kSlideEnabled));
    if (active) output = *found;
    ReleaseSRWLockShared(&s_playerLock);
    return active;
}

bool HasClientPresentationLocked() noexcept
{
    for (const auto& record : s_players)
        if (record.player && (record.enabled & (kActivityEnabled | kSlideEnabled)))
            return true;
    return false;
}

void RefreshClientPresentationFlag() noexcept
{
    s_clientPresentationPresent.store(HasClientPresentationLocked(), std::memory_order_release);
}

void RemoveIfEmpty(PlayerPresentation& record) noexcept
{
    if (!record.enabled) ClearRecord(record);
}

ServerSlideRecord* FindServerSlideRecord(std::uintptr_t player, std::uint32_t handle,
    std::uint32_t session) noexcept
{
    for (auto& record : s_serverSlideRecords)
        if (record.player == player && record.handle == handle && record.session == session)
            return &record;
    return nullptr;
}

ServerSlideRecord* WritableServerSlideRecord(std::uintptr_t player,
    std::uint32_t handle, std::uint32_t session) noexcept
{
    ServerSlideRecord* empty = nullptr;
    for (auto& record : s_serverSlideRecords)
    {
        if (record.player == player)
        {
            if (record.handle == handle && record.session == session) return &record;
            ClearServerSlideRecord(record);
            record.player = player;
            record.handle = handle;
            record.session = session;
            return &record;
        }
        if (!record.player && !empty) empty = &record;
    }
    if (empty)
    {
        empty->player = player;
        empty->handle = handle;
        empty->session = session;
    }
    return empty;
}

bool CopyServerSlideRecord(std::uintptr_t player, std::uint32_t handle,
    ServerSlideRecord& output) noexcept
{
    if (!s_serverSlidePresent.load(std::memory_order_acquire)) return false;
    AcquireSRWLockShared(&s_serverSlideLock);
    const ServerSlideRecord* found = nullptr;
    for (const auto& record : s_serverSlideRecords)
        if (record.player == player && record.handle == handle)
        {
            found = &record;
            break;
        }
    const bool active = found != nullptr;
    if (active) output = *found;
    ReleaseSRWLockShared(&s_serverSlideLock);
    return active;
}

void RefreshServerSlideFlag() noexcept
{
    bool active = false;
    for (const auto& record : s_serverSlideRecords)
        if (record.player)
        {
            active = true;
            break;
        }
    s_serverSlidePresent.store(active, std::memory_order_release);
}



bool HasVtableMethod(std::uintptr_t object, std::size_t byteOffset) noexcept
{
    if (!object) return false;
    const auto table = ReadField<std::uintptr_t>(object, 0);
    return table && ReadField<std::uintptr_t>(table, byteOffset) != 0;
}

bool HasPriorityMethods(std::uintptr_t state) noexcept
{
    return HasVtableMethod(state, 96) && HasVtableMethod(state, 104)
        && HasVtableMethod(state, 120) && HasVtableMethod(state, 128);
}

bool HasServerPriorityMethods(std::uintptr_t base, std::uintptr_t state) noexcept
{
    const auto vtable = ReadField<std::uintptr_t>(state, 0);
    return HasVtableMethod(state, 96) && HasVtableMethod(state, 104)
        && HasVtableMethod(state, 128)
        && (vtable == base + 0x932330 || HasVtableMethod(state, 120));
}
float SlideAngleDifference(float destination, float source) noexcept
{
    float difference = std::fmod(destination - source, 360.0f);
    if (destination <= source)
    {
        if (difference <= -180.0f) difference += 360.0f;
    }
    else if (difference >= 180.0f)
    {
        difference -= 360.0f;
    }
    return difference;
}

bool IsSlideEndActivity(int activity, const std::array<int, kSlideActivityCount>& ids) noexcept
{
    for (std::size_t i = 2; i != kSlideActivityCount; ++i)
        if (ids[i] == activity) return true;
    return false;
}

bool SelectSlideActivity(std::uintptr_t base, std::uintptr_t state, std::uintptr_t player,
    bool sliding, float time, float& lastSlideTime, float& slidingYaw,
    const std::array<int, kSlideActivityCount>& ids, int& candidate) noexcept
{
    const bool grounded = ClientCall<bool>(base, 0x333400, state);
    if (sliding)
    {
        candidate = ids[grounded ? 0 : 1];
        if (!grounded || !(time - lastSlideTime <= 0.1f))
            slidingYaw = ReadField<float>(state, 140);
        lastSlideTime = time;
        return true;
    }

    const bool jumping = ReadField<std::uint8_t>(state, 200) != 0;
    if (!(time - lastSlideTime < 5.0f) || !grounded || jumping)
        return false;

    const int sequence = ReadField<int>(player, 8588);
    const int sequenceActivity = ClientCall<int>(base, 0x062ED0, player, sequence);
    const bool sequenceFinished = ReadField<std::uint8_t>(player, 7984) != 0;
    if (!sequenceFinished && IsSlideEndActivity(sequenceActivity, ids))
    {
        candidate = sequenceActivity;
        return true;
    }
    if (!(time - lastSlideTime < 0.1f)) return false;

    const float difference = SlideAngleDifference(ReadField<float>(state, 140), slidingYaw);
    const std::size_t selected = difference >= -135.0f
        ? (difference >= -45.0f
            ? (difference < 45.0f ? 2u : (difference >= 135.0f ? 3u : 5u))
            : 6u)
        : 4u;
    candidate = ids[selected];
    return true;
}


bool ValidateServerSlideModel(std::uintptr_t base, std::uintptr_t player,
    const std::array<int, kSlideActivityCount>& activities,
    ActivityModelIdentity& model) noexcept
{
    if (!s_serverSlideModelReady.load(std::memory_order_acquire)
        || !slide_model::SupportsServerPlayer(player))
        return false;
    for (const int activity : activities)
    {
        ActivityModelIdentity resolved{};
        if (!ResolveServerActivityForModel(base, player, activity, resolved))
            return false;
    }
    model = CurrentServerActivityModel(player);
    return model.studio && model.header;
}

bool ServerGameTime(std::uintptr_t base, float& time) noexcept
{
    const auto globals = ReadField<std::uintptr_t>(base, 0xC310C0);
    if (!globals) return false;
    time = ReadField<float>(globals, 16);
    return std::isfinite(time);
}

bool SelectServerSlideActivity(std::uintptr_t base, std::uintptr_t state,
    std::uintptr_t player, bool sliding, float time, float& lastSlideTime,
    float& slidingYaw, const std::array<int, kSlideActivityCount>& ids,
    int& candidate) noexcept
{
    const bool grounded = ClientCall<bool>(base, 0x526260, state);
    if (sliding)
    {
        candidate = ids[grounded ? 0 : 1];
        if (!grounded || !(time - lastSlideTime <= 0.1f))
            slidingYaw = ReadField<float>(state, 128);
        lastSlideTime = time;
        return true;
    }

    const bool jumping = ReadField<std::uint8_t>(state, 188) != 0;
    if (!(time - lastSlideTime < 5.0f) || !grounded || jumping)
        return false;

    const int sequence = ReadField<int>(player, 2104);
    const int sequenceActivity = ClientCall<int>(base, 0x08ECB0, player, sequence);
    const bool sequenceFinished = ReadField<std::uint8_t>(player, 2084) != 0;
    if (!sequenceFinished && IsSlideEndActivity(sequenceActivity, ids))
    {
        candidate = sequenceActivity;
        return true;
    }
    if (!(time - lastSlideTime < 0.1f)) return false;

    const float difference = SlideAngleDifference(ReadField<float>(state, 128), slidingYaw);
    const std::size_t selected = difference >= -135.0f
        ? (difference >= -45.0f
            ? (difference < 45.0f ? 2u : (difference >= 135.0f ? 3u : 5u))
            : 6u)
        : 4u;
    candidate = ids[selected];
    return true;
}

bool ShouldSelectServerSlide(const ServerSlideRecord& presentation,
    std::uintptr_t base, std::uintptr_t state, std::uintptr_t player, float time,
    const std::array<int, kSlideActivityCount>& activities) noexcept
{
    if (presentation.active) return true;
    if (!(time - presentation.lastSlideTime < 5.0f)
        || !ClientCall<bool>(base, 0x526260, state)
        || ReadField<std::uint8_t>(state, 188) != 0)
        return false;
    const int sequence = ReadField<int>(player, 2104);
    const int sequenceActivity = ClientCall<int>(base, 0x08ECB0, player, sequence);
    const bool finished = ReadField<std::uint8_t>(player, 2084) != 0;
    return (!finished && IsSlideEndActivity(sequenceActivity, activities))
        || time - presentation.lastSlideTime < 0.1f;
}

void ClearServerSlideFeature(std::uintptr_t player, std::uint32_t handle,
    std::uint32_t session) noexcept
{
    AcquireSRWLockExclusive(&s_serverSlideLock);
    auto* record = FindServerSlideRecord(player, handle, session);
    if (record) ClearServerSlideRecord(*record);
    RefreshServerSlideFlag();
    ReleaseSRWLockExclusive(&s_serverSlideLock);
}

void CommitServerSlideHistory(std::uintptr_t player, std::uint32_t handle,
    std::uint32_t session, float lastSlideTime, float slidingYaw) noexcept
{
    AcquireSRWLockExclusive(&s_serverSlideLock);
    auto* record = FindServerSlideRecord(player, handle, session);
    if (record)
    {
        record->lastSlideTime = lastSlideTime;
        record->slidingYaw = slidingYaw;
    }
    ReleaseSRWLockExclusive(&s_serverSlideLock);
}

std::uintptr_t ResolveServerEntity(std::uintptr_t base, std::uint32_t handle) noexcept
{
    if (handle == kInvalidHandle || (handle & 0xFFFF) >= 0x4000) return 0;
    const auto entry = base + 0xD425C8 + 48 * (handle & 0xFFFF);
    if (ReadField<std::uint32_t>(entry, 8) != (handle >> 16)) return 0;
    return ReadField<std::uintptr_t>(entry, 0);
}

int SelectAtServerPriorityCheckpoint(std::uintptr_t base, std::uintptr_t state,
    std::uintptr_t player, bool slideActive, float time, float& lastSlideTime,
    float& slidingYaw,
    const std::array<int, kSlideActivityCount>& slideActivities) noexcept
{
    const auto titan = ReadField<std::uintptr_t>(state, 440);
    if (titan && (ReadField<std::uint8_t>(titan, 9320) & 0x30)) return -1;

    int candidate = 501;
    if (PlayerAnimCall<bool>(state, 128, &candidate)) return candidate;
    if (ReadField<std::uint8_t>(player, 444) == 13)
    {
        WriteField<std::uint8_t>(state, 369, 0);
        const int traversalActivity = ReadField<int>(player, 6748);
        if (traversalActivity) return traversalActivity + 576;
    }
    if (ReadField<std::uint8_t>(state, 236)
        && ClientCall<bool>(base, 0x526810, state, &candidate))
        return candidate;

    const bool jumping = PlayerAnimCall<bool>(state, 96, &candidate);
    const bool ducking = PlayerAnimCall<bool>(state, 104, &candidate);
    const auto vtable = ReadField<std::uintptr_t>(state, 0);
    const bool zipline = vtable != base + 0x932330
        && PlayerAnimCall<bool>(state, 120, &candidate);
    const int movementCandidate = candidate;
    const bool landing = ClientCall<bool>(base, 0x526340, state, &candidate);
    const int landingCandidate = candidate;
    if (SelectServerSlideActivity(base, state, player, slideActive, time,
        lastSlideTime, slidingYaw, slideActivities, candidate))
        return candidate;
    candidate = movementCandidate;

    const std::uint32_t vehicleHandle = ReadField<std::uint32_t>(player, 6856);
    const bool vehicle = ResolveServerEntity(base, vehicleHandle) != 0;
    if (vehicle) candidate = 825 + (ReadField<int>(player, 6872) != 2);

    const auto vtablePlayer = ReadField<std::uintptr_t>(player, 0);
    const auto hasAttachment = ReadField<std::uintptr_t>(vtablePlayer, 2920);
    const bool attached = hasAttachment
        && (hasAttachment == base + 0x41CDA0
            ? ResolveServerEntity(base, ReadField<std::uint32_t>(player, 5520)) != 0
            : reinterpret_cast<bool(__fastcall*)(std::uintptr_t)>(hasAttachment)(player))
        && !ClientCall<bool>(base, 0x4D5260, player);
    if (attached) candidate = 502;
    if (landing) candidate = landingCandidate;
    if (!jumping && !ducking && !zipline && !landing && !vehicle && !attached)
        PlayerAnimCall<void>(state, 112, &candidate);
    if (ClientCall<bool>(base, 0x526260, state) || vehicle) return candidate;

    const auto outer = ReadField<std::uintptr_t>(state, 360);
    WriteField<std::uint8_t>(state, 369, 1);
    if (!PlayerAnimCall<bool>(outer, 552)
        || !ReadField<std::uint8_t>(state, 368))
        return candidate;
    WriteField<std::uint8_t>(state, 188, 0);
    WriteField<std::uint8_t>(state, 368, 1);
    return ReadField<std::uint8_t>(state, 197)
        ? ReadField<int>(state, 124) + 567 : 546;
}
// Native death and traversal handlers may return before this checkpoint. The
// normal jump/duck/zipline/landing handlers still run before slide selection.
int SelectAtNativePriorityCheckpoint(std::uintptr_t base, std::uintptr_t state,
    std::uintptr_t player, int overrideActivity, bool hasOverride,
    bool slideEnabled, bool slideActive, float time, float& lastSlideTime,
    float& slidingYaw, const std::array<int, kSlideActivityCount>& slideActivities) noexcept
{
    const auto titan = ReadField<std::uintptr_t>(state, 448);
    if (titan && (ClientCall<bool>(base, 0x34E540, titan + 18288, 4)
        || ClientCall<bool>(base, 0x34E540, titan + 18288, 5)))
        return -1;

    int candidate = 501;
    if (ClientCall<bool>(base, 0x333670, state, &candidate)) return candidate;
    if (PlayerAnimCall<bool>(state, 128, &candidate)) return candidate;

    const int moveType = ClientCall<int>(base, 0x028320, player);
    if (moveType == 13)
    {
        WriteField<std::uint8_t>(state, 377, 0);
        const int traversalActivity = ReadField<int>(player, 11708);
        if (traversalActivity) return traversalActivity + 576;
    }
    if (ClientCall<bool>(base, 0x3322E0, state, &candidate)) return candidate;

    PlayerAnimCall<bool>(state, 96, &candidate);
    PlayerAnimCall<bool>(state, 104, &candidate);
    PlayerAnimCall<bool>(state, 120, &candidate);
    ClientCall<bool>(base, 0x3334C0, state, &candidate);
    if (slideEnabled && SelectSlideActivity(base, state, player, slideActive, time,
        lastSlideTime, slidingYaw, slideActivities, candidate))
        return candidate;
    return hasOverride ? overrideActivity : kNoActivity;
}

bool ClientGameTime(std::uintptr_t base, float& time) noexcept
{
    const auto globals = ReadField<std::uintptr_t>(base, 0xAEA640);
    if (!globals) return false;
    time = ReadField<float>(globals, 16);
    return std::isfinite(time);
}

bool ValidateClientSlideModel(std::uintptr_t base, std::uintptr_t player,
    const std::array<int, kSlideActivityCount>& activities,
    ActivityModelIdentity& model) noexcept
{
    if (!s_slideModelReady.load(std::memory_order_acquire)
        || !slide_model::SupportsPlayer(player))
        return false;
    for (const int activity : activities)
    {
        ActivityModelIdentity resolved{};
        if (!ResolveActivityForModel(base, player, activity, resolved))
            return false;
    }
    model = CurrentActivityModel(player);
    return model.studio && model.header;
}

bool ShouldSelectClientSlide(const PlayerPresentation& presentation,
    std::uintptr_t base, std::uintptr_t state, std::uintptr_t player, float time,
    const std::array<int, kSlideActivityCount>& activities) noexcept
{
    if (presentation.slideActive) return true;
    if (!(time - presentation.lastSlideTime < 5.0f)
        || !ClientCall<bool>(base, 0x333400, state)
        || ReadField<std::uint8_t>(state, 200) != 0)
        return false;

    const int sequence = ReadField<int>(player, 8588);
    const int sequenceActivity = ClientCall<int>(base, 0x062ED0, player, sequence);
    const bool finished = ReadField<std::uint8_t>(player, 7984) != 0;
    return (!finished && IsSlideEndActivity(sequenceActivity, activities))
        || time - presentation.lastSlideTime < 0.1f;
}

void ClearClientSlideFeature(std::uintptr_t player, std::uint32_t handle,
    std::uint32_t session) noexcept
{
    AcquireSRWLockExclusive(&s_playerLock);
    auto* record = FindRecord(player, handle, session);
    if (record)
    {
        record->enabled &= static_cast<std::uint8_t>(~kSlideEnabled);
        record->slideActive = false;
        record->slideModelAdmitted = false;
        RemoveIfEmpty(*record);
        RefreshClientPresentationFlag();
    }
    ReleaseSRWLockExclusive(&s_playerLock);
}

void CommitClientSlideHistory(std::uintptr_t player, std::uint32_t handle,
    std::uint32_t session, float lastSlideTime, float slidingYaw) noexcept
{
    AcquireSRWLockExclusive(&s_playerLock);
    auto* record = FindRecord(player, handle, session);
    if (record && (record->enabled & kSlideEnabled))
    {
        record->lastSlideTime = lastSlideTime;
        record->slidingYaw = slidingYaw;
    }
    ReleaseSRWLockExclusive(&s_playerLock);
}

int __fastcall ClientCalcMainActivity(std::uintptr_t animationState)
{
    const auto original = s_calcMainActivityOriginal.load(std::memory_order_acquire);
    const auto base = s_clientBase.load(std::memory_order_acquire);
    if (!original || !base || !s_clientReady.load(std::memory_order_acquire))
        return original ? original(animationState) : 0;
    if (!s_clientPresentationPresent.load(std::memory_order_acquire)
        || !animationState)
        return original(animationState);

    const auto player = ReadField<std::uintptr_t>(animationState, 360);
    if (!player) return original(animationState);
    const auto handle = NativePlayerHandle(player);
    if (handle == kInvalidHandle) return original(animationState);

    PlayerPresentation presentation{};
    if (!CopyActivityRecord(player, handle, presentation)
        || !HasPriorityMethods(animationState))
        return original(animationState);

    bool hasOverride = (presentation.enabled & kActivityEnabled) != 0;
    bool slideEnabled = (presentation.enabled & kSlideEnabled) != 0;
    std::array<int, kSlideActivityCount> slideActivities{};
    float time = 0.0f;

    if (slideEnabled)
    {
        bool activitiesReady = CopySlideActivityIds(true, slideActivities);
        if (!activitiesReady)
            activitiesReady = RegisterSlideActivities(base, 0)
                && CopySlideActivityIds(true, slideActivities);
        if (!ClientGameTime(base, time) || !activitiesReady)
            slideEnabled = false;
        else
        {
            const auto currentModel = CurrentActivityModel(player);
            if (!presentation.slideModelAdmitted
                || !SameActivityModel(currentModel, presentation.slideModel))
            {
                ActivityModelIdentity validatedModel{};
                if (!ValidateClientSlideModel(base, player, slideActivities, validatedModel))
                {
                    ClearClientSlideFeature(player, handle, presentation.session);
                    slideEnabled = false;
                }
                else
                {
                    presentation.slideModel = validatedModel;
                    presentation.slideModelAdmitted = true;
                    AcquireSRWLockExclusive(&s_playerLock);
                    auto* record = FindRecord(player, handle, presentation.session);
                    if (record && (record->enabled & kSlideEnabled))
                    {
                        record->slideModel = validatedModel;
                        record->slideModelAdmitted = true;
                    }
                    else
                        slideEnabled = false;
                    ReleaseSRWLockExclusive(&s_playerLock);
                }
            }
            if (slideEnabled && !ShouldSelectClientSlide(presentation, base,
                animationState, player, time, slideActivities))
            {
                slideEnabled = false;
                if (!(time - presentation.lastSlideTime < 5.0f))
                    ClearClientSlideFeature(player, handle, presentation.session);
            }
        }
    }

    if (hasOverride)
    {
        const auto currentModel = CurrentActivityModel(player);
        if (!SameActivityModel(currentModel, presentation.activityModel))
        {
            ActivityModelIdentity validatedModel{};
            if (!ResolveActivityForModel(base, player, presentation.activity, validatedModel))
            {
                AcquireSRWLockExclusive(&s_playerLock);
                auto* record = FindRecord(player, handle, presentation.session);
                if (record && record->activity == presentation.activity)
                {
                    record->enabled &= static_cast<std::uint8_t>(~kActivityEnabled);
                    RemoveIfEmpty(*record);
                    RefreshClientPresentationFlag();
                }
                ReleaseSRWLockExclusive(&s_playerLock);
                hasOverride = false;
            }
            else
            {
                AcquireSRWLockExclusive(&s_playerLock);
                auto* record = FindRecord(player, handle, presentation.session);
                if (record && record->activity == presentation.activity
                    && (record->enabled & kActivityEnabled))
                    record->activityModel = validatedModel;
                else
                    hasOverride = false;
                ReleaseSRWLockExclusive(&s_playerLock);
            }
        }
    }

    if (!slideEnabled && !hasOverride) return original(animationState);
    float lastSlideTime = presentation.lastSlideTime;
    float slidingYaw = presentation.slidingYaw;
    const int selected = SelectAtNativePriorityCheckpoint(base, animationState,
        player, presentation.activity, hasOverride, slideEnabled,
        presentation.slideActive, time, lastSlideTime, slidingYaw, slideActivities);
    if (slideEnabled)
        CommitClientSlideHistory(player, handle, presentation.session,
            lastSlideTime, slidingYaw);
    return selected == kNoActivity ? original(animationState) : selected;
}

int __fastcall ServerCalcMainActivity(std::uintptr_t animationState)
{
    const auto original = s_serverCalcMainActivityOriginal.load(std::memory_order_acquire);
    const auto base = s_serverBase.load(std::memory_order_acquire);
    if (!original || !base || !s_serverHooksReady.load(std::memory_order_acquire)
        || !s_serverSlidePresent.load(std::memory_order_acquire) || !animationState)
        return original ? original(animationState) : 0;

    const auto player = ReadField<std::uintptr_t>(animationState, 352);
    if (!player) return original(animationState);
    const auto handle = NativePlayerHandle(player);
    if (handle == kInvalidHandle) return original(animationState);

    ServerSlideRecord presentation{};
    if (!CopyServerSlideRecord(player, handle, presentation)
        || !HasServerPriorityMethods(base, animationState))
        return original(animationState);
    float time = 0.0f;
    std::array<int, kSlideActivityCount> slideActivities{};
    bool activitiesReady = CopySlideActivityIds(false, slideActivities);
    if (!activitiesReady)
        activitiesReady = RegisterSlideActivities(0, base)
            && CopySlideActivityIds(false, slideActivities);
    if (!ServerGameTime(base, time) || !activitiesReady)
        return original(animationState);

    ActivityModelIdentity validatedModel{};
    const auto currentModel = CurrentServerActivityModel(player);
    if (!presentation.modelAdmitted || !SameActivityModel(currentModel, presentation.model))
    {
        if (!ValidateServerSlideModel(base, player, slideActivities, validatedModel))
        {
            ClearServerSlideFeature(player, handle, presentation.session);
            return original(animationState);
        }
        presentation.model = validatedModel;
        presentation.modelAdmitted = true;
        AcquireSRWLockExclusive(&s_serverSlideLock);
        auto* record = FindServerSlideRecord(player, handle, presentation.session);
        if (!record)
        {
            ReleaseSRWLockExclusive(&s_serverSlideLock);
            return original(animationState);
        }
        record->model = validatedModel;
        record->modelAdmitted = true;
        ReleaseSRWLockExclusive(&s_serverSlideLock);
    }

    if (!ShouldSelectServerSlide(presentation, base, animationState, player,
        time, slideActivities))
    {
        if (!(time - presentation.lastSlideTime < 5.0f))
            ClearServerSlideFeature(player, handle, presentation.session);
        return original(animationState);
    }

    float lastSlideTime = presentation.lastSlideTime;
    float slidingYaw = presentation.slidingYaw;
    const int selected = SelectAtServerPriorityCheckpoint(base, animationState,
        player, presentation.active, time, lastSlideTime, slidingYaw, slideActivities);
    CommitServerSlideHistory(player, handle, presentation.session,
        lastSlideTime, slidingYaw);
    return selected == kNoActivity ? original(animationState) : selected;
}

bool SetFeature(std::uintptr_t player, std::uint32_t session, std::uint8_t feature,
    const float* values, float scalar) noexcept
{
    if (!ClientHooksReady() || !player) return false;
    const auto handle = NativePlayerHandle(player);
    if (handle == kInvalidHandle) return false;

    AcquireSRWLockExclusive(&s_playerLock);
    auto* record = WritableRecord(player, handle, session);
    if (!record)
    {
        ReleaseSRWLockExclusive(&s_playerLock);
        return false;
    }
    if (feature == kViewModelEnabled)
        std::memcpy(record->viewModel, values, sizeof(record->viewModel));
    else
        record->fovScale = scalar;
    record->enabled |= feature;
    RefreshClientPresentationFlag();
    ReleaseSRWLockExclusive(&s_playerLock);
    return true;
}

void ClearFeature(std::uintptr_t player, std::uint32_t session, std::uint8_t feature) noexcept
{
    if (!ClientHooksReady() || !player) return;
    const auto handle = NativePlayerHandle(player);
    if (handle == kInvalidHandle) return;
    AcquireSRWLockExclusive(&s_playerLock);
    auto* record = FindRecord(player, handle, session);
    if (record)
    {
        record->enabled &= static_cast<std::uint8_t>(~feature);
        RemoveIfEmpty(*record);
        RefreshClientPresentationFlag();
    }
    ReleaseSRWLockExclusive(&s_playerLock);
}

} // namespace
bool RegisterSlideActivities(std::uintptr_t clientModule,
    std::uintptr_t serverModule) noexcept
{
    if (!clientModule && !serverModule) return false;
    AcquireSRWLockExclusive(&s_activityLock);
    bool ready = true;
    if (clientModule)
    {
        const auto current = s_clientBase.load(std::memory_order_acquire);
        if ((current && current != clientModule)
            || !RegisterActivitiesForModule(s_clientActivityRegistry, true, clientModule))
            ready = false;
    }
    if (serverModule)
    {
        const auto current = s_serverBase.load(std::memory_order_acquire);
        if ((current && current != serverModule)
            || !RegisterActivitiesForModule(s_serverActivityRegistry, false, serverModule))
            ready = false;
    }
    ReleaseSRWLockExclusive(&s_activityLock);
    return ready;
}

bool SetSlideActivityOverride(CBaseEntity* entity, int session, bool active) noexcept
{
    const auto player = reinterpret_cast<std::uintptr_t>(entity);
    if (!player || session < 0 || !ClientHooksReady()) return false;
    if (session == 0)
    {
        AcquireSRWLockExclusive(&s_playerLock);
        for (auto& record : s_players)
            if (record.player == player) ClearRecord(record);
        RefreshClientPresentationFlag();
        ReleaseSRWLockExclusive(&s_playerLock);
        return !active;
    }

    const auto handle = NativePlayerHandle(player);
    if (handle == kInvalidHandle) return false;
    if (!active)
    {
        float time = 0.0f;
        const bool hasTime = ClientGameTime(s_clientBase.load(std::memory_order_acquire), time);
        AcquireSRWLockExclusive(&s_playerLock);
        auto* record = FindRecord(player, handle, static_cast<std::uint32_t>(session));
        if (record && (record->enabled & kSlideEnabled))
        {
            record->slideActive = false;
            if (record->lastSlideTime < 0.0f
                || (hasTime && !(time - record->lastSlideTime < 5.0f)))
                record->enabled &= static_cast<std::uint8_t>(~kSlideEnabled);
            RemoveIfEmpty(*record);
        }
        else if (!record)
            for (auto& stale : s_players)
                if (stale.player == player) ClearRecord(stale);
        RefreshClientPresentationFlag();
        ReleaseSRWLockExclusive(&s_playerLock);
        return true;
    }

    const auto base = s_clientBase.load(std::memory_order_acquire);
    if (!base) return false;
    const auto currentModel = CurrentActivityModel(player);
    AcquireSRWLockShared(&s_playerLock);
    const auto* existing = FindRecord(player, handle, static_cast<std::uint32_t>(session));
    const bool cached = existing && (existing->enabled & kSlideEnabled)
        && existing->slideActive && existing->slideModelAdmitted
        && SameActivityModel(currentModel, existing->slideModel);
    ReleaseSRWLockShared(&s_playerLock);
    if (cached) return true;

    std::array<int, kSlideActivityCount> activities{};
    ActivityModelIdentity model{};
    if (!RegisterSlideActivities(base, 0) || !CopySlideActivityIds(true, activities)
        || !ValidateClientSlideModel(base, player, activities, model))
    {
        ClearClientSlideFeature(player, handle, static_cast<std::uint32_t>(session));
        return false;
    }

    AcquireSRWLockExclusive(&s_playerLock);
    auto* record = WritableRecord(player, handle, static_cast<std::uint32_t>(session));
    if (!record)
    {
        ReleaseSRWLockExclusive(&s_playerLock);
        return false;
    }
    record->slideModel = model;
    record->slideModelAdmitted = true;
    record->slideActive = true;
    record->enabled |= kSlideEnabled;
    RefreshClientPresentationFlag();
    ReleaseSRWLockExclusive(&s_playerLock);
    return true;
}

bool SetServerSlideActivityOverride(std::uintptr_t player, int session, bool active) noexcept
{
    if (!player || session < 0 || !ServerHooksReady()) return false;
    if (session == 0)
    {
        AcquireSRWLockExclusive(&s_serverSlideLock);
        for (auto& record : s_serverSlideRecords)
            if (record.player == player) ClearServerSlideRecord(record);
        RefreshServerSlideFlag();
        ReleaseSRWLockExclusive(&s_serverSlideLock);
        return !active;
    }

    const auto handle = NativePlayerHandle(player);
    if (handle == kInvalidHandle) return false;
    const auto sessionId = static_cast<std::uint32_t>(session);
    if (!active)
    {
        AcquireSRWLockExclusive(&s_serverSlideLock);
        auto* record = FindServerSlideRecord(player, handle, sessionId);
        if (record)
        {
            record->active = false;
            if (record->lastSlideTime < 0.0f)
                ClearServerSlideRecord(*record);
        }
        else
            for (auto& stale : s_serverSlideRecords)
                if (stale.player == player) ClearServerSlideRecord(stale);
        RefreshServerSlideFlag();
        ReleaseSRWLockExclusive(&s_serverSlideLock);
        return true;
    }

    const auto base = s_serverBase.load(std::memory_order_acquire);
    if (!base) return false;
    const auto currentModel = CurrentServerActivityModel(player);
    AcquireSRWLockShared(&s_serverSlideLock);
    const auto* existing = FindServerSlideRecord(player, handle, sessionId);
    const bool cached = existing && existing->active && existing->modelAdmitted
        && SameActivityModel(currentModel, existing->model);
    ReleaseSRWLockShared(&s_serverSlideLock);
    if (cached) return true;

    std::array<int, kSlideActivityCount> activities{};
    ActivityModelIdentity model{};
    if (!RegisterSlideActivities(0, base) || !CopySlideActivityIds(false, activities)
        || !ValidateServerSlideModel(base, player, activities, model))
    {
        ClearServerSlideFeature(player, handle, sessionId);
        return false;
    }

    AcquireSRWLockExclusive(&s_serverSlideLock);
    auto* record = WritableServerSlideRecord(player, handle, sessionId);
    if (!record)
    {
        ReleaseSRWLockExclusive(&s_serverSlideLock);
        return false;
    }
    record->model = model;
    record->modelAdmitted = true;
    record->active = true;
    RefreshServerSlideFlag();
    ReleaseSRWLockExclusive(&s_serverSlideLock);
    return true;
}

bool InstallClientHooks(std::uintptr_t clientModule) noexcept
{
    if (!clientModule) return false;
    AcquireSRWLockExclusive(&s_installLock);
    const auto existingBase = s_clientBase.load(std::memory_order_acquire);
    if (s_clientReady.load(std::memory_order_acquire))
    {
        const bool same = existingBase == clientModule;
        ReleaseSRWLockExclusive(&s_installLock);
        return same;
    }

    if (!VerifyMovementModuleDigest(clientModule, kClientDigest)
        || !VerifyClientPe(clientModule)
        || !HasActivityGuard(clientModule))
    {
        OutputDebugStringA("R1Delta movement presentation: refusing unrecognized client image or CalcMainActivity entry.\n");
        ReleaseSRWLockExclusive(&s_installLock);
        return false;
    }

    if (!slide_model::InstallClient(clientModule))
    {
        OutputDebugStringA("R1Delta movement presentation: client slide retarget hook installation failed.\n");
        ReleaseSRWLockExclusive(&s_installLock);
        return false;
    }
    s_slideModelReady.store(true, std::memory_order_release);

    const auto target = clientModule + kCalcMainActivityRva;
    void* trampoline = nullptr;
    const MH_STATUS create = MH_CreateHook(reinterpret_cast<LPVOID>(target),
        reinterpret_cast<LPVOID>(&ClientCalcMainActivity), &trampoline);
    if (create != MH_OK || !trampoline)
    {
        slide_model::ShutdownClient();
        s_slideModelReady.store(false, std::memory_order_release);
        OutputDebugStringA("R1Delta movement presentation: CalcMainActivity hook creation failed or target is already detoured.\n");
        ReleaseSRWLockExclusive(&s_installLock);
        return false;
    }

    s_clientBase.store(clientModule, std::memory_order_release);
    s_activityTarget = target;
    s_calcMainActivityOriginal.store(reinterpret_cast<CalcMainActivity>(trampoline), std::memory_order_release);
    const MH_STATUS enable = MH_EnableHook(reinterpret_cast<LPVOID>(target));
    if (enable != MH_OK && enable != MH_ERROR_ENABLED)
    {
        MH_DisableHook(reinterpret_cast<LPVOID>(target));
        const MH_STATUS remove = MH_RemoveHook(reinterpret_cast<LPVOID>(target));
        if (remove == MH_OK)
        {
            s_calcMainActivityOriginal.store(nullptr, std::memory_order_release);
            s_activityTarget = 0;
            s_clientBase.store(0, std::memory_order_release);
            slide_model::ShutdownClient();
            s_slideModelReady.store(false, std::memory_order_release);
        }
        OutputDebugStringA("R1Delta movement presentation: CalcMainActivity hook enable failed.\n");
        ReleaseSRWLockExclusive(&s_installLock);
        return false;
    }

    s_clientReady.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&s_installLock);
    return true;
}

bool ClientHooksReady() noexcept
{
    return s_clientReady.load(std::memory_order_acquire);
}

bool InstallServerHooks(std::uintptr_t serverModule) noexcept
{
    if (!serverModule) return false;
    AcquireSRWLockExclusive(&s_installLock);
    const auto existingBase = s_serverBase.load(std::memory_order_acquire);
    if (s_serverHooksReady.load(std::memory_order_acquire))
    {
        const bool same = existingBase == serverModule;
        ReleaseSRWLockExclusive(&s_installLock);
        return same;
    }
    if (!VerifyMovementModuleDigest(serverModule, kServerDigest)
        || !VerifyServerPe(serverModule)
        || std::memcmp(reinterpret_cast<const void*>(serverModule + kServerCalcMainActivityRva),
            kServerCalcMainActivityGuard, sizeof(kServerCalcMainActivityGuard)) != 0)
    {
        OutputDebugStringA("R1Delta movement presentation: refusing unrecognized server image or CalcMainActivity entry.\n");
        ReleaseSRWLockExclusive(&s_installLock);
        return false;
    }
    if (!slide_model::InstallServer(serverModule))
    {
        OutputDebugStringA("R1Delta movement presentation: server slide retarget hook installation failed.\n");
        ReleaseSRWLockExclusive(&s_installLock);
        return false;
    }
    s_serverSlideModelReady.store(true, std::memory_order_release);

    const auto target = serverModule + kServerCalcMainActivityRva;
    void* trampoline = nullptr;
    const MH_STATUS create = MH_CreateHook(reinterpret_cast<LPVOID>(target),
        reinterpret_cast<LPVOID>(&ServerCalcMainActivity), &trampoline);
    if (create != MH_OK || !trampoline)
    {
        slide_model::ShutdownServer();
        s_serverSlideModelReady.store(false, std::memory_order_release);
        OutputDebugStringA("R1Delta movement presentation: server CalcMainActivity hook creation failed or target is already detoured.\n");
        ReleaseSRWLockExclusive(&s_installLock);
        return false;
    }

    s_serverBase.store(serverModule, std::memory_order_release);
    s_serverActivityTarget = target;
    s_serverCalcMainActivityOriginal.store(reinterpret_cast<CalcMainActivity>(trampoline),
        std::memory_order_release);
    const MH_STATUS enable = MH_EnableHook(reinterpret_cast<LPVOID>(target));
    if (enable != MH_OK && enable != MH_ERROR_ENABLED)
    {
        MH_DisableHook(reinterpret_cast<LPVOID>(target));
        const MH_STATUS remove = MH_RemoveHook(reinterpret_cast<LPVOID>(target));
        if (remove == MH_OK)
        {
            s_serverCalcMainActivityOriginal.store(nullptr, std::memory_order_release);
            s_serverActivityTarget = 0;
            s_serverBase.store(0, std::memory_order_release);
            slide_model::ShutdownServer();
            s_serverSlideModelReady.store(false, std::memory_order_release);
        }
        OutputDebugStringA("R1Delta movement presentation: server CalcMainActivity hook enable failed.\n");
        ReleaseSRWLockExclusive(&s_installLock);
        return false;
    }
    s_serverHooksReady.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&s_installLock);
    return true;
}

bool ServerHooksReady() noexcept
{
    return s_serverHooksReady.load(std::memory_order_acquire);
}

bool ShutdownServerHooks() noexcept
{
    AcquireSRWLockExclusive(&s_installLock);
    s_serverHooksReady.store(false, std::memory_order_release);
    if (s_serverActivityTarget)
    {
        const auto target = reinterpret_cast<LPVOID>(s_serverActivityTarget);
        const MH_STATUS disable = MH_DisableHook(target);
        if (disable != MH_OK && disable != MH_ERROR_DISABLED)
        {
            ReleaseSRWLockExclusive(&s_installLock);
            return false;
        }
        if (MH_RemoveHook(target) != MH_OK)
        {
            ReleaseSRWLockExclusive(&s_installLock);
            return false;
        }
        s_serverActivityTarget = 0;
        s_serverCalcMainActivityOriginal.store(nullptr, std::memory_order_release);
        s_serverBase.store(0, std::memory_order_release);
    }
    slide_model::ShutdownServer();
    s_serverSlideModelReady.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&s_serverSlideLock);
    for (auto& record : s_serverSlideRecords) ClearServerSlideRecord(record);
    s_serverSlidePresent.store(false, std::memory_order_release);
    ReleaseSRWLockExclusive(&s_serverSlideLock);
    AcquireSRWLockExclusive(&s_activityLock);
    s_serverActivityRegistry = {};
    ReleaseSRWLockExclusive(&s_activityLock);
    ReleaseSRWLockExclusive(&s_installLock);
    return true;
}

bool ShutdownClientHooks() noexcept
{
    AcquireSRWLockExclusive(&s_installLock);
    if (!s_activityTarget)
    {
        s_clientReady.store(false, std::memory_order_release);
        slide_model::ShutdownClient();
        s_slideModelReady.store(false, std::memory_order_release);
        AcquireSRWLockExclusive(&s_activityLock);
        s_clientActivityRegistry = {};
        ReleaseSRWLockExclusive(&s_activityLock);
        ReleaseSRWLockExclusive(&s_installLock);
        Reset();
        return true;
    }

    s_clientReady.store(false, std::memory_order_release);
    const auto target = reinterpret_cast<LPVOID>(s_activityTarget);
    const MH_STATUS disable = MH_DisableHook(target);
    if (disable != MH_OK && disable != MH_ERROR_DISABLED)
    {
        ReleaseSRWLockExclusive(&s_installLock);
        return false;
    }
    if (MH_RemoveHook(target) != MH_OK)
    {
        ReleaseSRWLockExclusive(&s_installLock);
        return false;
    }

    s_activityTarget = 0;
    s_calcMainActivityOriginal.store(nullptr, std::memory_order_release);
    s_clientBase.store(0, std::memory_order_release);
    slide_model::ShutdownClient();
    s_slideModelReady.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&s_activityLock);
    s_clientActivityRegistry = {};
    ReleaseSRWLockExclusive(&s_activityLock);
    ReleaseSRWLockExclusive(&s_installLock);
    Reset();
    return true;
}


void ResetSession(std::uint32_t session) noexcept
{
    AcquireSRWLockExclusive(&s_playerLock);
    for (auto& record : s_players)
        if (record.player && record.session == session) ClearRecord(record);
    RefreshClientPresentationFlag();
    ReleaseSRWLockExclusive(&s_playerLock);
}

void Reset() noexcept
{
    AcquireSRWLockExclusive(&s_playerLock);
    for (auto& record : s_players) ClearRecord(record);
    RefreshClientPresentationFlag();
    ReleaseSRWLockExclusive(&s_playerLock);
    AcquireSRWLockExclusive(&s_serverSlideLock);
    for (auto& record : s_serverSlideRecords) ClearServerSlideRecord(record);
    RefreshServerSlideFlag();
    ReleaseSRWLockExclusive(&s_serverSlideLock);
}

bool SetViewModelTransform(std::uintptr_t player, std::uint32_t session,
    const float localTransform[6]) noexcept
{
    if (!localTransform) return false;
    for (unsigned index = 0; index != 6; ++index)
        if (!std::isfinite(localTransform[index])) return false;
    return SetFeature(player, session, kViewModelEnabled, localTransform, 1.0f);
}

void ClearViewModelTransform(std::uintptr_t player, std::uint32_t session) noexcept
{
    ClearFeature(player, session, kViewModelEnabled);
}

bool ApplyViewModelInputTransform(std::uintptr_t player, std::uint32_t session,
    float origin[3], float angles[3]) noexcept
{
    if (!ClientHooksReady() || !player || !origin || !angles) return false;
    const auto handle = NativePlayerHandle(player);
    if (handle == kInvalidHandle) return false;
    PlayerPresentation presentation{};
    if (!CopyRecord(player, handle, session, presentation)
        || !(presentation.enabled & kViewModelEnabled)) return false;
    for (unsigned index = 0; index != 3; ++index)
        if (!std::isfinite(origin[index]) || !std::isfinite(angles[index])) return false;

    const float radians = 0.01745329251994329577f;
    const float pitch = angles[0] * radians;
    const float yaw = angles[1] * radians;
    const float roll = angles[2] * radians;
    const float sp = std::sin(pitch), cp = std::cos(pitch);
    const float sy = std::sin(yaw), cy = std::cos(yaw);
    const float sr = std::sin(roll), cr = std::cos(roll);
    const float forwardX = cp * cy, forwardY = cp * sy, forwardZ = -sp;
    const float rightX = -sr * sp * cy + cr * sy;
    const float rightY = -sr * sp * sy - cr * cy, rightZ = -sr * cp;
    const float upX = cr * sp * cy + sr * sy;
    const float upY = cr * sp * sy - sr * cy, upZ = cr * cp;
    const float forward = presentation.viewModel[0];
    const float right = presentation.viewModel[1];
    const float up = presentation.viewModel[2];
    const float nextOrigin[3] = {
        origin[0] + forward * forwardX + right * rightX + up * upX,
        origin[1] + forward * forwardY + right * rightY + up * upY,
        origin[2] + forward * forwardZ + up * upZ
    };
    const float nextAngles[3] = {
        angles[0] + presentation.viewModel[3],
        angles[1] + presentation.viewModel[4],
        angles[2] + presentation.viewModel[5]
    };
    for (unsigned index = 0; index != 3; ++index)
        if (!std::isfinite(nextOrigin[index]) || !std::isfinite(nextAngles[index])) return false;
    std::memcpy(origin, nextOrigin, sizeof(nextOrigin));
    std::memcpy(angles, nextAngles, sizeof(nextAngles));
    return true;
}

bool SetFovScale(std::uintptr_t player, std::uint32_t session, float scale) noexcept
{
    if (!std::isfinite(scale) || scale <= 0.0f) return false;
    return SetFeature(player, session, kFovEnabled, nullptr, scale);
}

void ClearFovScale(std::uintptr_t player, std::uint32_t session) noexcept
{
    ClearFeature(player, session, kFovEnabled);
}

bool ApplyFovScale(std::uintptr_t player, std::uint32_t session, float* fov) noexcept
{
    if (!ClientHooksReady() || !player || !fov || !std::isfinite(*fov)) return false;
    const auto handle = NativePlayerHandle(player);
    if (handle == kInvalidHandle) return false;
    PlayerPresentation presentation{};
    if (!CopyRecord(player, handle, session, presentation)
        || !(presentation.enabled & kFovEnabled)) return false;
    const float scaled = *fov * presentation.fovScale;
    if (!std::isfinite(scaled)) return false;
    *fov = *fov > 0.0f ? std::clamp(scaled, 1.0f, 179.0f) : scaled;
    return true;
}

bool SetMainActivityOverride(std::uintptr_t player, std::uint32_t session, int activity) noexcept
{
    if (!ClientHooksReady() || !player) return false;
    const auto base = s_clientBase.load(std::memory_order_acquire);
    const auto handle = NativePlayerHandle(player);
    if (!base || handle == kInvalidHandle) return false;

    ActivityModelIdentity model = CurrentActivityModel(player);
    AcquireSRWLockShared(&s_playerLock);
    const auto* existing = FindRecord(player, handle, session);
    const bool cached = existing && (existing->enabled & kActivityEnabled)
        && existing->activity == activity && SameActivityModel(existing->activityModel, model);
    ReleaseSRWLockShared(&s_playerLock);
    if (!cached && !ResolveActivityForModel(base, player, activity, model))
    {
        ClearFeature(player, session, kActivityEnabled);
        return false;
    }
    if (cached) return true;

    AcquireSRWLockExclusive(&s_playerLock);
    auto* record = WritableRecord(player, handle, session);
    if (!record)
    {
        ReleaseSRWLockExclusive(&s_playerLock);
        return false;
    }
    record->activity = activity;
    record->activityModel = model;
    record->enabled |= kActivityEnabled;
    RefreshClientPresentationFlag();
    ReleaseSRWLockExclusive(&s_playerLock);
    return true;
}

void ClearMainActivityOverride(std::uintptr_t player, std::uint32_t session) noexcept
{
    ClearFeature(player, session, kActivityEnabled);
}

void ClearPlayer(std::uintptr_t player, std::uint32_t session) noexcept
{
    if (!ClientHooksReady() || !player) return;
    const auto handle = NativePlayerHandle(player);
    if (handle == kInvalidHandle) return;
    AcquireSRWLockExclusive(&s_playerLock);
    auto* record = FindRecord(player, handle, session);
    if (record) ClearRecord(*record);
    RefreshClientPresentationFlag();
    ReleaseSRWLockExclusive(&s_playerLock);
}
} // namespace r1delta::movement::presentation
