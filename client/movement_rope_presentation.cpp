#include "movement_rope_presentation.h"
#include "../shared/movement_runtime.h"
#include "logging.h"
#include <MinHook.h>
#include <cstring>

namespace r1delta::movement::presentation
{
namespace
{
using DrawRope = int(__fastcall*)(std::uintptr_t, int, const void*);
std::uintptr_t client{};
DrawRope originalDraw{};
void* drawTarget{};
bool installed{};

template<class T> const T& Field(std::uintptr_t object, std::size_t offset) noexcept
{
    return *reinterpret_cast<const T*>(object + offset);
}

std::uintptr_t Entity(std::uint32_t handle) noexcept
{
    const auto index = handle & 0xFFFFu;
    if (handle == 0xFFFFFFFFu || index >= 0x4000u) return 0;
    const auto list = Field<std::uintptr_t>(client, 0xAEED60);
    if (!list) return 0;
    const auto entry = list + index * 0x20u;
    return Field<std::uint32_t>(entry, 0x10) == (handle >> 16)
        ? Field<std::uintptr_t>(entry, 8) : 0;
}

bool OwnerExcluded(std::uintptr_t rope) noexcept
{
    const auto* snapshot = ReadMovementPresentation();
    if (!snapshot) return false;
    using ViewPlayer = std::uintptr_t(__fastcall*)(int);
    const auto viewPlayer = reinterpret_cast<ViewPlayer>(client + 0x7B120)(0);
    if (!viewPlayer || viewPlayer != snapshot->player) return false;

    // A routed grapple has at most four corners: six native point entities.
    // Check the real serial-qualified endpoint chain, never a name/index cache.
    for (unsigned point = 0; point < 6; ++point)
    {
        if (!rope || Field<std::uintptr_t>(rope, 0) != client + 0x8380A8) return false;
        const auto anchor = Entity(Field<std::uint32_t>(rope, 552));
        if (anchor && Field<std::uint32_t>(anchor, 452) == 6u
            && Entity(Field<std::uint32_t>(anchor, 560)) == viewPlayer)
            return true;
        const auto next = Entity(Field<std::uint32_t>(rope, 8952));
        if (next == rope) return false;
        rope = next;
    }
    return false;
}

int __fastcall Draw(std::uintptr_t renderable, int flags, const void* instance)
{
    // IClientRenderable is the +8 subobject of this exact C_RopeKeyframe.
    return OwnerExcluded(renderable - 8) ? 0 : originalDraw(renderable, flags, instance);
}
}

bool InstallRopeOwnerVisibility(std::uintptr_t module) noexcept
{
    if (installed) return client == module;
    if (!module || drawTarget || !VerifyMovementModuleDigest(module,
        "5b3f00805568eeaa6ee1cf889f12dc4570eb28a3fad2626ec0578eae3e7afd5e")) return false;
    constexpr unsigned char drawPrefix[] = {0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x83,0xC1};
    constexpr unsigned char viewPrefix[] = {0x48,0x83,0xEC,0x28,0x83,0xF9,0xFF,0x75,0x6C,0x48,0x8B,0x0D,0x48,0x0D,0x64,0x01};
    if (std::memcmp(reinterpret_cast<const void*>(module + 0xD9280), drawPrefix, sizeof(drawPrefix))
        || std::memcmp(reinterpret_cast<const void*>(module + 0x7B120), viewPrefix, sizeof(viewPrefix))) return false;
    auto* target = reinterpret_cast<void*>(module + 0xD9280);
    auto status = MH_CreateHook(target, reinterpret_cast<void*>(&Draw), reinterpret_cast<void**>(&originalDraw));
    if (status != MH_OK)
    {
        Warning("R1Delta movement: rope owner-visibility hook creation failed (%d).\n", status);
        return false;
    }
    client = module;
    drawTarget = target;
    status = MH_EnableHook(drawTarget);
    if (status != MH_OK)
    {
        Warning("R1Delta movement: rope owner-visibility hook enable failed (%d).\n", status);
        UninstallRopeOwnerVisibility();
        return false;
    }
    installed = true;
    return true;
}

bool RopeOwnerVisibilityReady(std::uintptr_t module) noexcept
{
    return installed && client == module;
}

bool UninstallRopeOwnerVisibility() noexcept
{
    if (!drawTarget) return true;
    auto status = MH_DisableHook(drawTarget);
    if (status != MH_OK && status != MH_ERROR_DISABLED && status != MH_ERROR_NOT_CREATED) return false;
    status = MH_RemoveHook(drawTarget);
    if (status != MH_OK && status != MH_ERROR_NOT_CREATED) return false;
    installed = false;
    drawTarget = nullptr;
    originalDraw = nullptr;
    client = 0;
    return true;
}
}
