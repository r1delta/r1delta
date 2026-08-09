#include "../engine/core/ffa_targeting_logic.h"
#include "../engine/core/ffa_targeting.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>

#include <cstdio>

extern "C" unsigned char __fastcall R1DeltaResolveFfaClientRelation(
    void* first,
    void* second);
extern "C" unsigned char __fastcall R1DeltaResolveLiveFfaClientRelation(
    void* first,
    void* second);
extern "C" unsigned char __fastcall R1DeltaResolveLiveFfaServerRelation(
    void* first,
    void* second);
extern "C" unsigned char __fastcall R1DeltaIsValidFfaObserverTarget(
    void* observer,
    void* candidate);

namespace {
constexpr unsigned char kNativeRelation = static_cast<unsigned char>(
    r1delta::ffa_targeting::FfaOwnerRelation::Native);
constexpr unsigned char kFriendlyRelation = static_cast<unsigned char>(
    r1delta::ffa_targeting::FfaOwnerRelation::Friendly);
constexpr unsigned char kHostileRelation = static_cast<unsigned char>(
    r1delta::ffa_targeting::FfaOwnerRelation::Hostile);


bool Check(bool condition, const char* what)
{
    if (!condition)
        std::printf("FAIL: %s\n", what);
    return condition;
}

constexpr std::array<unsigned char, 27> kClientSmartAmmoInputsExpected{
    0x49, 0x8B, 0x45, 0x00, 0x49, 0x8B, 0xCD, 0xFF,
    0x90, 0x00, 0x03, 0x00, 0x00, 0x48, 0x8B, 0x13,
    0x48, 0x8B, 0xCB, 0x8B, 0xF8, 0xFF, 0x92, 0x00,
    0x03, 0x00, 0x00
};
constexpr std::array<unsigned char, 9> kClientSmartAmmoProjectileGateExpected{
    0x3B, 0xC7, 0x75, 0x28, 0xE9, 0x37, 0x03, 0x00, 0x00
};
constexpr std::array<unsigned char, 8> kClientSmartAmmoPlayerGateExpected{
    0x3B, 0xC7, 0x0F, 0x84, 0x14, 0x03, 0x00, 0x00
};
constexpr std::array<unsigned char, 8> kClientMinimapExpected{
    0xF3, 0x0F, 0x10, 0x97, 0x34, 0x1A, 0x00, 0x00
};
constexpr std::array<unsigned char, 7> kClientMinimapVisibilityExpected{
    0x8D, 0x48, 0x04, 0x8B, 0xD7, 0xD3, 0xE2
};
constexpr std::array<unsigned char, 24> kClientMinimapVisibilityInputsExpected{
    0x83, 0xC9, 0xFF, 0xE8, 0x36, 0x4F, 0xD6, 0xFF,
    0x48, 0x8B, 0xC8, 0x48, 0x8B, 0xF0, 0xE8, 0xAB,
    0x4E, 0xD6, 0xFF, 0xBF, 0x01, 0x00, 0x00, 0x00
};
constexpr std::array<unsigned char, 8> kClientMinimapDefaultVisibilityInputsExpected{
    0x8B, 0x74, 0x24, 0x50, 0x8B, 0x54, 0x24, 0x28
};
constexpr std::array<unsigned char, 8> kClientMinimapDefaultVisibilityExpected{
    0x3B, 0xF2, 0x0F, 0x84, 0x6E, 0x02, 0x00, 0x00
};
constexpr std::array<unsigned char, 53> kClientBossPlayerExpected{
    0x8B, 0x81, 0xC8, 0x00, 0x00, 0x00, 0x83, 0xF8,
    0xFF, 0x74, 0x27, 0x0F, 0xB7, 0xC8, 0x81, 0xF9,
    0x00, 0x40, 0x00, 0x00, 0x73, 0x1C, 0x8B, 0xD1,
    0x48, 0x8B, 0x0D, 0xF1, 0x78, 0x7F, 0x00, 0xC1,
    0xE8, 0x10, 0x48, 0xC1, 0xE2, 0x05, 0x39, 0x44,
    0x0A, 0x10, 0x75, 0x06, 0x48, 0x8B, 0x44, 0x0A,
    0x08, 0xC3, 0x33, 0xC0, 0xC3
};
constexpr std::array<unsigned char, 53> kClientOwnerExpected{
    0x8B, 0x81, 0x30, 0x02, 0x00, 0x00, 0x83, 0xF8,
    0xFF, 0x74, 0x27, 0x0F, 0xB7, 0xC8, 0x81, 0xF9,
    0x00, 0x40, 0x00, 0x00, 0x73, 0x1C, 0x8B, 0xD1,
    0x48, 0x8B, 0x0D, 0x11, 0x8E, 0xAC, 0x00, 0xC1,
    0xE8, 0x10, 0x48, 0xC1, 0xE2, 0x05, 0x39, 0x44,
    0x0A, 0x10, 0x75, 0x06, 0x48, 0x8B, 0x44, 0x0A,
    0x08, 0xC3, 0x33, 0xC0, 0xC3
};
constexpr std::array<unsigned char, 24> kClientAliveExpected{
    0xF6, 0x81, 0x48, 0x01, 0x00, 0x00, 0x01, 0x74,
    0x03, 0x32, 0xC0, 0xC3, 0x33, 0xC0, 0x38, 0x81,
    0x88, 0x04, 0x00, 0x00, 0x0F, 0x94, 0xC0, 0xC3
};
constexpr std::array<unsigned char, 13> kServerSmartAmmoCustomGateExpected{
    0x39, 0x83, 0x7C, 0x04, 0x00, 0x00,
    0x75, 0x18, 0xE9, 0x8A, 0x03, 0x00, 0x00
};
constexpr std::array<unsigned char, 12> kServerSmartAmmoPlayerGateExpected{
    0x39, 0x83, 0x7C, 0x04, 0x00, 0x00,
    0x0F, 0x84, 0x77, 0x03, 0x00, 0x00
};
constexpr std::array<unsigned char, 16> kServerSmartAmmoAcceptExpected{
    0x4C, 0x8D, 0xB3, 0xD8, 0x01, 0x00, 0x00, 0x4D,
    0x85, 0xF6, 0x0F, 0x84, 0x67, 0x03, 0x00, 0x00
};
constexpr std::array<unsigned char, 18> kServerSmartAmmoRejectExpected{
    0x4C, 0x8B, 0xB4, 0x24, 0x00, 0x01, 0x00, 0x00,
    0x0F, 0x28, 0xBC, 0x24, 0xA0, 0x00, 0x00, 0x00,
    0xB0, 0x01
};
constexpr std::array<unsigned char, 9> kServerIsPlayerExpected{
    0x48, 0x8B, 0x01, 0xFF, 0xA0, 0xB0, 0x02, 0x00, 0x00
};
constexpr std::array<unsigned char, 23> kServerAliveExpected{
    0xF6, 0x81, 0x60, 0x01, 0x00, 0x00, 0x01, 0x74,
    0x03, 0x32, 0xC0, 0xC3, 0x80, 0xB9, 0x61, 0x03,
    0x00, 0x00, 0x00, 0x0F, 0x94, 0xC0, 0xC3
};
constexpr std::array<unsigned char, 52> kServerOwnerExpected{
    0x8B, 0x91, 0x48, 0x02, 0x00, 0x00, 0x83, 0xFA,
    0xFF, 0x74, 0x26, 0x0F, 0xB7, 0xC2, 0x3D, 0x00,
    0x40, 0x00, 0x00, 0x73, 0x1C, 0x48, 0x8D, 0x04,
    0x40, 0xC1, 0xEA, 0x10, 0x48, 0x03, 0xC0, 0x48,
    0x8D, 0x0D, 0x52, 0x61, 0xCC, 0x00, 0x39, 0x54,
    0xC1, 0x08, 0x75, 0x05, 0x48, 0x8B, 0x04, 0xC1,
    0xC3, 0x33, 0xC0, 0xC3
};
constexpr std::array<unsigned char, 60> kServerBossPlayerExpected{
    0x8B, 0x91, 0x60, 0x05, 0x00, 0x00, 0x83, 0xFA,
    0xFF, 0x74, 0x2E, 0x0F, 0xB7, 0xC2, 0x3D, 0x00,
    0x40, 0x00, 0x00, 0x73, 0x24, 0x48, 0x8D, 0x0C,
    0x40, 0xC1, 0xEA, 0x10, 0x48, 0x03, 0xC9, 0x48,
    0x8D, 0x05, 0x92, 0x28, 0x98, 0x00, 0x39, 0x54,
    0xC8, 0x08, 0x75, 0x0D, 0x48, 0x8B, 0x0C, 0xC8,
    0x48, 0x85, 0xC9, 0x0F, 0x85, 0x37, 0xC3, 0x00,
    0x00, 0x33, 0xC0, 0xC3
};
constexpr std::array<unsigned char, 9> kClientIsPlayerWrapperExpected{
    0x48, 0x8B, 0x01, 0xFF, 0xA0, 0x38, 0x05, 0x00, 0x00
};
constexpr std::array<unsigned char, 39> kServerObserverInitialPreconditionExpected{
    0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xFF, 0x90, 0xB0, 0x02, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x17, 0x49, 0x3B, 0xDC, 0x74, 0x22, 0x83, 0xBB, 0x1C, 0x16, 0x00, 0x00, 0x00, 0x75, 0x19, 0x80, 0xBB, 0x40, 0x16, 0x00, 0x00, 0x00, 0x74, 0x10
};
constexpr std::array<unsigned char, 44> kServerObserverCyclePreconditionExpected{
    0x48, 0x85, 0xDB, 0x74, 0x3C, 0x48, 0x8B, 0x13, 0x48, 0x8B, 0xCB, 0xFF, 0x92, 0xB0, 0x02, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x17, 0x48, 0x3B, 0xDE, 0x74, 0x20, 0x83, 0xBB, 0x1C, 0x16, 0x00, 0x00, 0x00, 0x75, 0x17, 0x80, 0xBB, 0x40, 0x16, 0x00, 0x00, 0x00, 0x74, 0x0E
};
constexpr std::array<unsigned char, 16> kServerObserverInitialExpected{
    0x8B, 0x83, 0x7C, 0x04, 0x00, 0x00,
    0x41, 0x39, 0x84, 0x24, 0x7C, 0x04, 0x00, 0x00,
    0x74, 0x44
};
constexpr std::array<unsigned char, 14> kServerObserverCycleExpected{
    0x8B, 0x83, 0x7C, 0x04, 0x00, 0x00,
    0x39, 0x86, 0x7C, 0x04, 0x00, 0x00,
    0x74, 0x4C
};
constexpr std::uintptr_t kClientSmartAmmoProjectileInputsRva = 0x4614A2;
constexpr std::uintptr_t kClientSmartAmmoPlayerInputsRva = 0x4614C6;
constexpr std::uintptr_t kClientSmartAmmoProjectileGateRva = 0x4614BD;
constexpr std::uintptr_t kClientSmartAmmoPlayerGateRva = 0x4614E1;
constexpr std::uintptr_t kClientMinimapRva = 0x317B50;
constexpr std::uintptr_t kClientMinimapVisibilityRva = 0x3161FA;
constexpr std::uintptr_t kClientMinimapVisibilityInputsRva = 0x3161E2;
constexpr std::uintptr_t kClientMinimapDefaultVisibilityInputsRva = 0x317ED9;
constexpr std::uintptr_t kClientMinimapDefaultVisibilityRva = 0x317EE1;
constexpr std::uintptr_t kClientBossPlayerRva = 0x2F7450;
constexpr std::uintptr_t kClientOwnerRva = 0x025F30;
constexpr std::uintptr_t kClientAliveRva = 0x027F70;
constexpr std::uintptr_t kClientIsPlayerWrapperRva = 0x2F3950;
constexpr std::uintptr_t kServerSmartAmmoCustomGateRva = 0x5C0186;
constexpr std::uintptr_t kServerSmartAmmoPlayerGateRva = 0x5C019A;
constexpr std::uintptr_t kServerIsPlayerRva = 0x3C5C90;
constexpr std::uintptr_t kServerOwnerRva = 0x07C450;
constexpr std::uintptr_t kServerAliveRva = 0x3CC370;
constexpr std::uintptr_t kServerBossPlayerRva = 0x3BFD10;
constexpr std::uintptr_t kServerSmartAmmoAcceptRva = 0x5C01A6;
constexpr std::uintptr_t kServerSmartAmmoRejectRva = 0x5C051D;
constexpr std::uintptr_t kServerObserverInitialRva = 0x4FA212;
constexpr std::uintptr_t kServerObserverCycleRva = 0x4FA3DD;
constexpr std::uintptr_t kServerObserverInitialPreconditionRva = 0x4FA1EB;
constexpr std::uintptr_t kServerObserverCyclePreconditionRva = 0x4FA3B1;

template <std::size_t Size>
void CopyExpected(unsigned char* image, std::uintptr_t rva,
    const std::array<unsigned char, Size>& expected)
{
    std::memcpy(image + rva, expected.data(), expected.size());
}

constexpr std::size_t kFakeEntitySize = 0x1700;
constexpr std::uintptr_t kClientEntityListPointerRva = 0xAEED60;
constexpr std::size_t kClientEntityEntrySize = 32;
constexpr std::uintptr_t kServerEntityListRva = 0xD425C8;
constexpr std::size_t kServerEntityEntrySize = 48;

struct FakeEntity
{
    void** vtable = nullptr;
    std::array<unsigned char, kFakeEntitySize - sizeof(void*)> state{};
    bool isPlayer = false;
    FakeEntity* boss = nullptr;
    FakeEntity* owner = nullptr;
};

unsigned char& EntityByte(FakeEntity& entity, std::size_t offset)
{
    return reinterpret_cast<unsigned char*>(&entity)[offset];
}

void SetEntityHandle(FakeEntity& entity, std::size_t offset, std::uint32_t handle)
{
    std::memcpy(
        reinterpret_cast<unsigned char*>(&entity) + offset,
        &handle,
        sizeof(handle));
}

bool __fastcall FakeIsPlayer(void* entity)
{
    return static_cast<FakeEntity*>(entity)->isPlayer;
}

void SetClientUnowned(FakeEntity& entity)
{
    SetEntityHandle(entity, 0xC8, 0xFFFFFFFF);
    SetEntityHandle(entity, 0x230, 0xFFFFFFFF);
}

void SetServerUnowned(FakeEntity& entity)
{
    SetEntityHandle(entity, 0x248, 0xFFFFFFFF);
    SetEntityHandle(entity, 0x560, 0xFFFFFFFF);
}

void SetClientAlive(FakeEntity& entity, bool alive)
{
    EntityByte(entity, 0x148) = alive ? 0 : 1;
    EntityByte(entity, 0x488) = alive ? 0 : 1;
}

void SetServerAlive(FakeEntity& entity, bool alive)
{
    EntityByte(entity, 0x160) = alive ? 0 : 1;
    EntityByte(entity, 0x361) = alive ? 0 : 1;
}

std::uint32_t MakeEntityHandle(std::uint16_t index, std::uint16_t serial)
{
    return static_cast<std::uint32_t>(index)
        | (static_cast<std::uint32_t>(serial) << 16);
}

void RegisterClientEntity(
    unsigned char* entityList,
    std::uint16_t index,
    std::uint16_t serial,
    void* entity)
{
    unsigned char* entry =
        entityList + static_cast<std::size_t>(index) * kClientEntityEntrySize;
    std::memcpy(entry + 8, &entity, sizeof(entity));
    const std::uint32_t serialValue = serial;
    std::memcpy(entry + 16, &serialValue, sizeof(serialValue));
}

void RegisterServerEntity(
    unsigned char* serverImage,
    std::uint16_t index,
    std::uint16_t serial,
    void* entity)
{
    unsigned char* entry = serverImage
        + kServerEntityListRva
        + static_cast<std::size_t>(index) * kServerEntityEntrySize;
    std::memcpy(entry, &entity, sizeof(entity));
    const std::uint32_t serialValue = serial;
    std::memcpy(entry + 8, &serialValue, sizeof(serialValue));
}

void SetServerObserverTargetState(
    FakeEntity& entity,
    int observerMode,
    bool active)
{
    std::memcpy(
        reinterpret_cast<unsigned char*>(&entity) + 0x161C,
        &observerMode,
        sizeof(observerMode));
    EntityByte(entity, 0x1640) = active ? 1 : 0;
}

void PopulateClientImage(unsigned char* image)
{
    CopyExpected(image, kClientSmartAmmoProjectileInputsRva,
        kClientSmartAmmoInputsExpected);
    CopyExpected(image, kClientSmartAmmoPlayerInputsRva,
        kClientSmartAmmoInputsExpected);
    CopyExpected(image, kClientSmartAmmoProjectileGateRva,
        kClientSmartAmmoProjectileGateExpected);
    CopyExpected(image, kClientSmartAmmoPlayerGateRva,
        kClientSmartAmmoPlayerGateExpected);
    CopyExpected(image, kClientMinimapRva, kClientMinimapExpected);
    CopyExpected(image, kClientMinimapVisibilityRva,
        kClientMinimapVisibilityExpected);
    CopyExpected(image, kClientMinimapVisibilityInputsRva,
        kClientMinimapVisibilityInputsExpected);
    CopyExpected(image, kClientMinimapDefaultVisibilityInputsRva,
        kClientMinimapDefaultVisibilityInputsExpected);
    CopyExpected(image, kClientMinimapDefaultVisibilityRva,
        kClientMinimapDefaultVisibilityExpected);
    CopyExpected(image, kClientBossPlayerRva, kClientBossPlayerExpected);
    CopyExpected(image, kClientOwnerRva, kClientOwnerExpected);
    CopyExpected(image, kClientAliveRva, kClientAliveExpected);
    CopyExpected(image, kClientIsPlayerWrapperRva, kClientIsPlayerWrapperExpected);
}
void PopulateServerImage(unsigned char* image)
{
    CopyExpected(image, kServerSmartAmmoCustomGateRva,
        kServerSmartAmmoCustomGateExpected);
    CopyExpected(image, kServerSmartAmmoPlayerGateRva,
        kServerSmartAmmoPlayerGateExpected);
    CopyExpected(image, kServerIsPlayerRva, kServerIsPlayerExpected);
    CopyExpected(image, kServerOwnerRva, kServerOwnerExpected);
    CopyExpected(image, kServerAliveRva, kServerAliveExpected);
    CopyExpected(image, kServerBossPlayerRva, kServerBossPlayerExpected);
    CopyExpected(image, kServerSmartAmmoAcceptRva,
        kServerSmartAmmoAcceptExpected);
    CopyExpected(image, kServerSmartAmmoRejectRva,
        kServerSmartAmmoRejectExpected);
    CopyExpected(image, kServerObserverInitialPreconditionRva,
        kServerObserverInitialPreconditionExpected);
    CopyExpected(image, kServerObserverCyclePreconditionRva,
        kServerObserverCyclePreconditionExpected);
    CopyExpected(image, kServerObserverInitialRva,
        kServerObserverInitialExpected);
    CopyExpected(image, kServerObserverCycleRva,
        kServerObserverCycleExpected);
}

template <std::size_t Size>
bool HasJumpPatch(const unsigned char* image, std::uintptr_t rva)
{
    const unsigned char* patch = image + rva;
    if (patch[0] != 0xE9)
        return false;
    for (std::size_t index = 5; index < Size; ++index) {
        if (patch[index] != 0x90)
            return false;
    }
    return true;
}

bool RunOwnerResolutionTests()
{
    using r1delta::ffa_targeting::ResolveOwningPlayer;

    const auto isPlayer = [](void* entity) {
        return static_cast<FakeEntity*>(entity)->isPlayer;
    };
    const auto getBoss = [](void* entity) -> void* {
        return static_cast<FakeEntity*>(entity)->boss;
    };
    const auto getOwner = [](void* entity) -> void* {
        return static_cast<FakeEntity*>(entity)->owner;
    };

    bool passed = true;
    FakeEntity player{};
    player.isPlayer = true;
    passed &= Check(
        ResolveOwningPlayer(&player, isPlayer, getBoss, getOwner) == &player,
        "direct players own themselves");

    FakeEntity bossOwnedNpc{};
    bossOwnedNpc.boss = &player;
    passed &= Check(
        ResolveOwningPlayer(&bossOwnedNpc, isPlayer, getBoss, getOwner) == &player,
        "boss-player ownership resolves before generic owner");

    FakeEntity ownerMiddle{};
    FakeEntity ownerLeaf{};
    ownerMiddle.owner = &ownerLeaf;
    ownerLeaf.owner = &player;
    passed &= Check(
        ResolveOwningPlayer(&ownerMiddle, isPlayer, getBoss, getOwner) == &player,
        "generic owner chains resolve to a player");

    FakeEntity unowned{};
    passed &= Check(
        ResolveOwningPlayer(&unowned, isPlayer, getBoss, getOwner) == nullptr,
        "unowned entities preserve native relationships");
    unowned.owner = &unowned;
    passed &= Check(
        ResolveOwningPlayer(&unowned, isPlayer, getBoss, getOwner) == nullptr,
        "self-owned entities terminate safely");

    std::array<FakeEntity, 5> tooDeep{};
    for (std::size_t index = 0; index + 1 < tooDeep.size(); ++index)
        tooDeep[index].owner = &tooDeep[index + 1];
    tooDeep.back().isPlayer = true;
    passed &= Check(
        ResolveOwningPlayer(&tooDeep.front(), isPlayer, getBoss, getOwner) == nullptr,
        "owner resolution is bounded to four entities");
    return passed;
}

bool RunPatchInstallerTests()
{
    using namespace r1delta::ffa_targeting;

    bool passed = true;
    constexpr std::size_t clientImageSize = 0x470000;
    constexpr std::size_t serverImageSize = 0x5D0000;

    auto* mismatchedClient = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, clientImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    passed &= Check(mismatchedClient != nullptr, "allocate mismatched client test image");
    if (mismatchedClient) {
        PopulateClientImage(mismatchedClient);
        mismatchedClient[kClientMinimapDefaultVisibilityInputsRva] ^= 0xFF;

        passed &= Check(!InstallClientHooks(
            reinterpret_cast<std::uintptr_t>(mismatchedClient)),
            "client minimap operand revision mismatch is rejected");
        passed &= Check(std::memcmp(
            mismatchedClient + kClientSmartAmmoPlayerGateRva,
            kClientSmartAmmoPlayerGateExpected.data(),
            kClientSmartAmmoPlayerGateExpected.size()) == 0,
            "client patch validation is transactional");
        passed &= Check(std::memcmp(
            mismatchedClient + kClientSmartAmmoProjectileGateRva,
            kClientSmartAmmoProjectileGateExpected.data(),
            kClientSmartAmmoProjectileGateExpected.size()) == 0,
            "client projectile gate remains stock after validation failure");
        VirtualFree(mismatchedClient, 0, MEM_RELEASE);
    }

    auto* mismatchedClientIsPlayer = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, clientImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    passed &= Check(mismatchedClientIsPlayer != nullptr, "allocate mismatched client IsPlayer wrapper image");
    if (mismatchedClientIsPlayer) {
        PopulateClientImage(mismatchedClientIsPlayer);
        mismatchedClientIsPlayer[kClientIsPlayerWrapperRva] ^= 0xFF;
        passed &= Check(!InstallClientHooks(
            reinterpret_cast<std::uintptr_t>(mismatchedClientIsPlayer)),
            "client IsPlayer wrapper revision mismatch is rejected");
        passed &= Check(std::memcmp(
            mismatchedClientIsPlayer + kClientSmartAmmoPlayerGateRva,
            kClientSmartAmmoPlayerGateExpected.data(),
            kClientSmartAmmoPlayerGateExpected.size()) == 0,
            "client IsPlayer wrapper validation is transactional");
        VirtualFree(mismatchedClientIsPlayer, 0, MEM_RELEASE);
    }

    auto* client = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, clientImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    passed &= Check(client != nullptr, "allocate valid client test image");
    if (client) {
        PopulateClientImage(client);

        passed &= Check(InstallClientHooks(reinterpret_cast<std::uintptr_t>(client)),
            "matching client revision installs all FFA hooks");
        passed &= Check(HasJumpPatch<kClientSmartAmmoProjectileGateExpected.size()>(
            client, kClientSmartAmmoProjectileGateRva),
            "client projectile smart-ammo gate has a complete jump patch");
        passed &= Check(HasJumpPatch<kClientSmartAmmoPlayerGateExpected.size()>(
            client, kClientSmartAmmoPlayerGateRva),
            "client NPC/player smart-ammo gate has a complete jump patch");
        passed &= Check(HasJumpPatch<kClientMinimapExpected.size()>(
            client, kClientMinimapRva),
            "client minimap classifier has a complete jump patch");
        passed &= Check(HasJumpPatch<kClientMinimapVisibilityExpected.size()>(
            client, kClientMinimapVisibilityRva),
            "client FFA minimap viewer mask has a complete jump patch");
        passed &= Check(HasJumpPatch<kClientMinimapDefaultVisibilityExpected.size()>(
            client, kClientMinimapDefaultVisibilityRva),
            "client FFA minimap default-visibility gate has a complete jump patch");
        VirtualFree(client, 0, MEM_RELEASE);
    }

    auto* mismatchedServer = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, serverImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    passed &= Check(mismatchedServer != nullptr, "allocate mismatched server test image");
    if (mismatchedServer) {
        PopulateServerImage(mismatchedServer);
        mismatchedServer[kServerOwnerRva] ^= 0xFF;

        passed &= Check(!InstallServerHooks(
            reinterpret_cast<std::uintptr_t>(mismatchedServer)),
            "server owner-helper revision mismatch is rejected");
        passed &= Check(std::memcmp(
            mismatchedServer + kServerSmartAmmoCustomGateRva,
            kServerSmartAmmoCustomGateExpected.data(),
            kServerSmartAmmoCustomGateExpected.size()) == 0,
            "server helper validation occurs before patching");
        VirtualFree(mismatchedServer, 0, MEM_RELEASE);
    }

    auto* mismatchedServerInitialPre = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, serverImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    passed &= Check(mismatchedServerInitialPre != nullptr, "allocate mismatched server initial precondition image");
    if (mismatchedServerInitialPre) {
        PopulateServerImage(mismatchedServerInitialPre);
        mismatchedServerInitialPre[kServerObserverInitialPreconditionRva] ^= 0xFF;
        passed &= Check(!InstallServerHooks(
            reinterpret_cast<std::uintptr_t>(mismatchedServerInitialPre)),
            "server initial observer precondition mismatch is rejected");
        passed &= Check(std::memcmp(
            mismatchedServerInitialPre + kServerSmartAmmoCustomGateRva,
            kServerSmartAmmoCustomGateExpected.data(),
            kServerSmartAmmoCustomGateExpected.size()) == 0,
            "server initial precondition validation is transactional");
        VirtualFree(mismatchedServerInitialPre, 0, MEM_RELEASE);
    }

    auto* mismatchedServerCyclePre = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, serverImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    passed &= Check(mismatchedServerCyclePre != nullptr, "allocate mismatched server cycle precondition image");
    if (mismatchedServerCyclePre) {
        PopulateServerImage(mismatchedServerCyclePre);
        mismatchedServerCyclePre[kServerObserverCyclePreconditionRva] ^= 0xFF;
        passed &= Check(!InstallServerHooks(
            reinterpret_cast<std::uintptr_t>(mismatchedServerCyclePre)),
            "server cycle observer precondition mismatch is rejected");
        passed &= Check(std::memcmp(
            mismatchedServerCyclePre + kServerSmartAmmoCustomGateRva,
            kServerSmartAmmoCustomGateExpected.data(),
            kServerSmartAmmoCustomGateExpected.size()) == 0,
            "server cycle precondition validation is transactional");
        VirtualFree(mismatchedServerCyclePre, 0, MEM_RELEASE);
    }

    auto* server = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, serverImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    passed &= Check(server != nullptr, "allocate valid server test image");
    if (server) {
        PopulateServerImage(server);

        passed &= Check(InstallServerHooks(reinterpret_cast<std::uintptr_t>(server)),
            "matching server revision installs all FFA hooks");
        passed &= Check(HasJumpPatch<kServerSmartAmmoCustomGateExpected.size()>(
            server, kServerSmartAmmoCustomGateRva),
            "server custom-target smart-ammo gate has a complete jump patch");
        passed &= Check(HasJumpPatch<kServerSmartAmmoPlayerGateExpected.size()>(
            server, kServerSmartAmmoPlayerGateRva),
            "server player smart-ammo gate has a complete jump patch");
        passed &= Check(HasJumpPatch<kServerObserverInitialExpected.size()>(
            server, kServerObserverInitialRva),
            "initial server observer team gate has a complete jump patch");
        passed &= Check(HasJumpPatch<kServerObserverCycleExpected.size()>(
            server, kServerObserverCycleRva),
            "server observer cycling team gate has a complete jump patch");
        VirtualFree(server, 0, MEM_RELEASE);
    }

    return passed;
}

bool RunRuntimePredicateTests()
{
    constexpr std::size_t clientImageSize = 0xAF0000;
    constexpr std::size_t serverImageSize = 0xD50000;
    auto* clientImage = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, clientImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    auto* serverImage = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, serverImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));

    bool passed = true;
    passed &= Check(clientImage != nullptr, "allocate executable client predicate image");
    passed &= Check(serverImage != nullptr, "allocate executable server predicate image");
    if (!clientImage || !serverImage) {
        if (clientImage)
            VirtualFree(clientImage, 0, MEM_RELEASE);
        if (serverImage)
            VirtualFree(serverImage, 0, MEM_RELEASE);
        return false;
    }

    PopulateClientImage(clientImage);
    PopulateServerImage(serverImage);
    alignas(void*) std::array<unsigned char, kClientEntityEntrySize * 8>
        clientEntityList{};
    void* clientEntityListPointer = clientEntityList.data();
    std::memcpy(
        clientImage + kClientEntityListPointerRva,
        &clientEntityListPointer,
        sizeof(clientEntityListPointer));
    passed &= Check(r1delta::ffa_targeting::InstallClientHooks(
        reinterpret_cast<std::uintptr_t>(clientImage)),
        "install client ownership helpers for runtime tests");
    passed &= Check(r1delta::ffa_targeting::InstallServerHooks(
        reinterpret_cast<std::uintptr_t>(serverImage)),
        "install server ownership helpers for runtime tests");

    std::array<void*, (0x538 / sizeof(void*)) + 1> clientVtable{};
    clientVtable[0x538 / sizeof(void*)] = reinterpret_cast<void*>(&FakeIsPlayer);
    std::array<void*, (0x2B0 / sizeof(void*)) + 1> serverVtable{};
    serverVtable[0x2B0 / sizeof(void*)] = reinterpret_cast<void*>(&FakeIsPlayer);

    FakeEntity firstClient{};
    FakeEntity secondClient{};
    FakeEntity clientUnownedNpc{};
    FakeEntity clientOwnedNpc{};
    FakeEntity clientOwnerProxy{};
    for (FakeEntity* entity : {
        &firstClient,
        &secondClient,
        &clientUnownedNpc,
        &clientOwnedNpc,
        &clientOwnerProxy }) {
        entity->vtable = clientVtable.data();
        SetClientUnowned(*entity);
    }
    firstClient.isPlayer = true;
    secondClient.isPlayer = true;
    SetClientAlive(firstClient, true);
    SetClientAlive(secondClient, true);
    RegisterClientEntity(
        clientEntityList.data(), 1, 7, &firstClient);
    RegisterClientEntity(
        clientEntityList.data(), 2, 11, &clientOwnedNpc);
    SetEntityHandle(
        clientOwnedNpc, 0xC8, MakeEntityHandle(1, 7));
    SetEntityHandle(
        clientOwnerProxy, 0x230, MakeEntityHandle(2, 11));

    FakeEntity firstServer{};
    FakeEntity secondServer{};
    FakeEntity serverUnownedNpc{};
    FakeEntity serverOwnedNpc{};
    FakeEntity serverOwnerProxy{};
    for (FakeEntity* entity : {
        &firstServer,
        &secondServer,
        &serverUnownedNpc,
        &serverOwnedNpc,
        &serverOwnerProxy }) {
        entity->vtable = serverVtable.data();
        SetServerUnowned(*entity);
    }
    firstServer.isPlayer = true;
    secondServer.isPlayer = true;
    SetServerAlive(firstServer, true);
    SetServerAlive(secondServer, true);
    RegisterServerEntity(serverImage, 1, 13, &firstServer);
    RegisterServerEntity(serverImage, 2, 17, &serverOwnedNpc);
    SetEntityHandle(
        serverOwnedNpc, 0x560, MakeEntityHandle(1, 13));
    SetEntityHandle(
        serverOwnerProxy, 0x248, MakeEntityHandle(2, 17));
    SetServerObserverTargetState(firstServer, 0, true);
    SetServerObserverTargetState(secondServer, 0, true);
    SetServerObserverTargetState(serverUnownedNpc, 0, true);

    r1delta::ffa_targeting::SetFfaBased(false);
    passed &= Check(R1DeltaResolveLiveFfaClientRelation(
        &firstClient, &secondClient) == kNativeRelation,
        "client smart-ammo relation is native outside FFA");
    passed &= Check(R1DeltaResolveLiveFfaServerRelation(
        &firstServer, &secondServer) == kNativeRelation,
        "server smart-ammo relation is native outside FFA");
    passed &= Check(R1DeltaIsValidFfaObserverTarget(
        &firstServer, &secondServer) == 0,
        "observer predicate is disabled outside FFA");

    r1delta::ffa_targeting::SetFfaBased(true);
    passed &= Check(R1DeltaResolveFfaClientRelation(
        &firstClient, &secondClient) == kHostileRelation,
        "client minimap resolves distinct direct player owners as hostile");
    passed &= Check(R1DeltaResolveLiveFfaClientRelation(
        &firstClient, &secondClient) == kHostileRelation,
        "client smart ammo resolves distinct live player owners as hostile");
    passed &= Check(R1DeltaResolveLiveFfaServerRelation(
        &firstServer, &secondServer) == kHostileRelation,
        "server smart ammo resolves distinct live player owners as hostile");
    passed &= Check(R1DeltaResolveFfaClientRelation(
        &firstClient, &firstClient) == kFriendlyRelation,
        "client minimap resolves self as friendly");
    passed &= Check(R1DeltaResolveLiveFfaClientRelation(
        nullptr, &secondClient) == kNativeRelation,
        "client smart-ammo relation preserves null endpoints");
    passed &= Check(R1DeltaResolveLiveFfaClientRelation(
        &clientOwnedNpc, &secondClient) == kHostileRelation,
        "client boss-owned NPC is hostile to a distinct live owner");
    passed &= Check(R1DeltaResolveLiveFfaClientRelation(
        &clientOwnedNpc, &firstClient) == kFriendlyRelation,
        "client boss-owned NPC remains friendly to its owner");
    passed &= Check(R1DeltaResolveLiveFfaClientRelation(
        &clientOwnerProxy, &secondClient) == kHostileRelation,
        "client generic-owner chain resolves through an owned NPC");
    passed &= Check(R1DeltaResolveLiveFfaClientRelation(
        &clientOwnerProxy, &clientOwnedNpc) == kFriendlyRelation,
        "client entities with the same resolved owner remain friendly");
    passed &= Check(R1DeltaResolveLiveFfaClientRelation(
        &clientUnownedNpc, &secondClient) == kNativeRelation,
        "client unowned NPCs retain native relationships");
    passed &= Check(R1DeltaResolveLiveFfaServerRelation(
        &serverOwnedNpc, &secondServer) == kHostileRelation,
        "server boss-owned NPC is hostile to a distinct live owner");
    passed &= Check(R1DeltaResolveLiveFfaServerRelation(
        &serverOwnedNpc, &firstServer) == kFriendlyRelation,
        "server boss-owned NPC remains friendly to its owner");
    passed &= Check(R1DeltaResolveLiveFfaServerRelation(
        &serverOwnerProxy, &secondServer) == kHostileRelation,
        "server generic-owner chain resolves through an owned NPC");
    passed &= Check(R1DeltaResolveLiveFfaServerRelation(
        &serverOwnerProxy, &serverOwnedNpc) == kFriendlyRelation,
        "server entities with the same resolved owner remain friendly");
    passed &= Check(R1DeltaResolveLiveFfaServerRelation(
        &serverUnownedNpc, &secondServer) == kNativeRelation,
        "server unowned NPCs retain native relationships");

    SetClientAlive(firstClient, false);
    passed &= Check(R1DeltaResolveFfaClientRelation(
        &clientOwnedNpc, &secondClient) == kHostileRelation,
        "client minimap still classifies a dead distinct owner as hostile");
    passed &= Check(R1DeltaResolveFfaClientRelation(
        &clientOwnedNpc, &firstClient) == kFriendlyRelation,
        "client minimap still classifies a dead same owner as friendly");
    passed &= Check(R1DeltaResolveLiveFfaClientRelation(
        &clientOwnedNpc, &secondClient) == kNativeRelation,
        "client smart ammo preserves native teams for a dead distinct target owner");
    passed &= Check(R1DeltaResolveLiveFfaClientRelation(
        &clientOwnedNpc, &firstClient) == kFriendlyRelation,
        "client smart ammo still rejects a dead same-owner target");
    SetClientAlive(firstClient, true);
    SetClientAlive(secondClient, false);
    passed &= Check(R1DeltaResolveLiveFfaClientRelation(
        &clientOwnedNpc, &secondClient) == kNativeRelation,
        "client smart ammo preserves native teams for a dead distinct attacker owner");
    SetClientAlive(secondClient, true);

    SetServerAlive(firstServer, false);
    passed &= Check(R1DeltaResolveLiveFfaServerRelation(
        &serverOwnedNpc, &secondServer) == kNativeRelation,
        "server smart ammo preserves native teams for a dead distinct target owner");
    passed &= Check(R1DeltaResolveLiveFfaServerRelation(
        &serverOwnedNpc, &firstServer) == kFriendlyRelation,
        "server smart ammo still rejects a dead same-owner target");
    SetServerAlive(firstServer, true);
    SetServerAlive(secondServer, false);
    passed &= Check(R1DeltaResolveLiveFfaServerRelation(
        &serverOwnedNpc, &secondServer) == kNativeRelation,
        "server smart ammo preserves native teams for a dead distinct attacker owner");
    SetServerAlive(secondServer, true);

    passed &= Check(R1DeltaIsValidFfaObserverTarget(
        &firstServer, &secondServer) == 1,
        "FFA observer accepts a distinct active player target");
    passed &= Check(R1DeltaIsValidFfaObserverTarget(
        &firstServer, &serverUnownedNpc) == 0,
        "FFA observer preserves non-player slot relationships");
    passed &= Check(R1DeltaIsValidFfaObserverTarget(
        &firstServer, &firstServer) == 0,
        "FFA observer rejects self");
    SetServerObserverTargetState(secondServer, 1, true);
    passed &= Check(R1DeltaIsValidFfaObserverTarget(
        &firstServer, &secondServer) == 0,
        "FFA observer rejects targets already observing");
    SetServerObserverTargetState(secondServer, 0, false);
    passed &= Check(R1DeltaIsValidFfaObserverTarget(
        &firstServer, &secondServer) == 0,
        "FFA observer rejects inactive targets");

    r1delta::ffa_targeting::SetFfaBased(false);
    VirtualFree(clientImage, 0, MEM_RELEASE);
    VirtualFree(serverImage, 0, MEM_RELEASE);
    return passed;
}

} // namespace

int main()
{
    using namespace r1delta::ffa_targeting;

    bool passed = true;

    passed &= Check(!AreOpposingFfaPlayers(false, true, true, false),
        "non-FFA distinct players keep the engine team result");
    passed &= Check(AreOpposingFfaPlayers(true, true, true, false),
        "FFA distinct players are opponents");
    passed &= Check(!AreOpposingFfaPlayers(true, true, true, true),
        "a player is never their own opponent");
    passed &= Check(!AreOpposingFfaPlayers(true, false, true, false),
        "non-player targets retain native relationships");
    passed &= Check(!AreOpposingFfaPlayers(true, true, false, false),
        "non-player viewers retain native relationships");

    passed &= Check(ShouldAcceptSmartAmmoTarget(
        false, true, true, true, true, true, false),
        "FFA smart ammo accepts distinct live owners despite shared engine teams");
    passed &= Check(!ShouldAcceptSmartAmmoTarget(
        false, false, true, true, true, true, false),
        "non-FFA smart ammo preserves native same-team rejection");
    passed &= Check(!ShouldAcceptSmartAmmoTarget(
        false, true, true, true, true, true, true),
        "FFA smart ammo rejects the same owner's entity");
    passed &= Check(!ShouldAcceptSmartAmmoTarget(
        true, true, true, true, true, true, true),
        "same ownership overrides contradictory native teams");
    passed &= Check(!ShouldAcceptSmartAmmoTarget(
        false, true, false, true, true, true, false),
        "unowned smart-ammo candidates preserve native same-team rejection");
    passed &= Check(ShouldAcceptSmartAmmoTarget(
        true, true, false, true, true, true, false),
        "unowned smart-ammo candidates preserve native different-team acceptance");
    passed &= Check(!ShouldAcceptSmartAmmoTarget(
        false, true, true, false, true, true, false),
        "dead distinct target owners preserve native same-team rejection");
    passed &= Check(ShouldAcceptSmartAmmoTarget(
        true, true, true, false, true, true, false),
        "dead distinct target owners preserve native different-team acceptance");
    passed &= Check(!ShouldAcceptSmartAmmoTarget(
        false, true, true, true, true, false, false),
        "dead distinct attacker owners preserve native same-team rejection");
    passed &= Check(!ShouldAcceptSmartAmmoTarget(
        true, true, true, false, true, false, true),
        "dead same ownership still overrides contradictory native teams");
    passed &= Check(ShouldAcceptSmartAmmoTarget(
        true, false, false, false, false, false, true),
        "non-FFA different-team smart ammo preserves native acceptance");
    passed &= Check(ShouldAcceptObserverTarget(
        false, true, true, true, false, true, true),
        "FFA observers can target a distinct active same-team player");
    passed &= Check(!ShouldAcceptObserverTarget(
        false, false, true, true, false, true, true),
        "non-FFA observers retain the native team gate");
    passed &= Check(!ShouldAcceptObserverTarget(
        false, true, true, false, false, true, true),
        "FFA observer bypass does not admit non-player targets");
    passed &= Check(!ShouldAcceptObserverTarget(
        false, true, true, true, true, true, true),
        "FFA observer bypass does not admit self");
    passed &= Check(!ShouldAcceptObserverTarget(
        false, true, true, true, false, false, true),
        "FFA observer bypass does not admit targets already observing");
    passed &= Check(!ShouldAcceptObserverTarget(
        false, true, true, true, false, true, false),
        "FFA observer bypass does not admit inactive targets");
    passed &= Check(ShouldAcceptObserverTarget(
        true, false, false, false, true, false, false),
        "native same-team observer targets remain accepted");
    passed &= Check(ShouldRouteTeamChatToSenderOnly(true, true),
        "FFA team chat is private to its sender");
    passed &= Check(!ShouldRouteTeamChatToSenderOnly(true, false),
        "FFA global chat retains native broadcast");
    passed &= Check(!ShouldRouteTeamChatToSenderOnly(false, true),
        "non-FFA team chat retains native routing");

    passed &= Check(ShouldSuppressGameplayVoice(true, false, 2),
        "FFA gameplay voice is private when all-talk is disabled");
    passed &= Check(ShouldSuppressGameplayVoice(true, false, 3),
        "both gameplay factions use FFA voice isolation");
    passed &= Check(!ShouldSuppressGameplayVoice(true, true, 2),
        "explicit all-talk broadcasts FFA gameplay voice");
    passed &= Check(!ShouldSuppressGameplayVoice(false, false, 2),
        "non-FFA voice retains native routing");
    passed &= Check(!ShouldSuppressGameplayVoice(true, false, 1),
        "spectator voice retains native routing");


    constexpr int originalClassification = 7;
    constexpr int bossPlayerClassification = 3;
    constexpr int partyMemberClassification = 4;
    passed &= Check(ResolveMinimapClassification(
        originalClassification, false, true, true, false) == originalClassification,
        "non-FFA minimap classification is unchanged");
    passed &= Check(ResolveMinimapClassification(
        kFriendlyMinimapClassification, true, true, true, false)
            == kEnemyMinimapClassification,
        "distinct owners override a false friendly minimap classification");
    passed &= Check(ResolveMinimapClassification(
        bossPlayerClassification, true, true, true, false)
            == kEnemyMinimapClassification,
        "distinct owners override another player's boss material");
    passed &= Check(ResolveMinimapClassification(
        kEnemyMinimapClassification, true, true, true, true)
            == kFriendlyMinimapClassification,
        "same ownership repairs a false enemy minimap classification");
    passed &= Check(ResolveMinimapClassification(
        bossPlayerClassification, true, true, true, true)
            == bossPlayerClassification,
        "same ownership preserves the local boss-player material");
    passed &= Check(ResolveMinimapClassification(
        partyMemberClassification, true, true, true, true)
            == partyMemberClassification,
        "same ownership preserves structural party materials");
    passed &= Check(ResolveMinimapClassification(
        originalClassification, true, false, true, false)
            == originalClassification,
        "unresolved minimap ownership preserves native classification");
    passed &= RunOwnerResolutionTests();
    passed &= RunPatchInstallerTests();
    passed &= RunRuntimePredicateTests();

    if (!passed)
        return 1;

    std::printf("FFA targeting tests passed\n");
    return 0;
}
