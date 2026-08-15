#pragma once

#include <cstddef>
#include <cstdint>

namespace r1delta::vphysics
{
inline constexpr std::uint32_t kR1VPhysicsTimeDateStamp = 0x5487422E;
inline constexpr std::uint32_t kR1VPhysicsSizeOfImage = 0x221000;
inline constexpr std::uintptr_t kR1VPhysicsPrepareQueueRva = 0xFFB50;
inline constexpr std::uint8_t kR1VPhysicsPrepareQueueExpectedPrologue[] = {
	0x48, 0x89, 0x6C, 0x24, 0x18,
	0x48, 0x89, 0x74, 0x24, 0x20,
	0x57,
	0x48, 0x83, 0xEC, 0x20,
};
inline constexpr std::uintptr_t kR1VPhysicsPrepareResourcesRva = 0xFF010;
inline constexpr std::uint8_t kR1VPhysicsPrepareResourcesExpectedPrologue[] = {
	0x33, 0xD2,
	0x4C, 0x8B, 0xC9,
	0x66, 0x3B, 0x91, 0x92, 0x00, 0x14, 0x00,
};
inline constexpr std::uintptr_t kR1VPhysicsReleaseStorageRva = 0xCA0B0;
inline constexpr std::uint8_t kR1VPhysicsReleaseStorageExpectedPrologue[] = {
	0x40, 0x53,
	0x48, 0x83, 0xEC, 0x20,
	0x48, 0x8B, 0xD9,
	0x48, 0x8B, 0x0D, 0xC0, 0x02, 0x0C, 0x00,
};
inline constexpr std::uintptr_t kR1VPhysicsShutdownRva = 0x100880;
inline constexpr std::uint8_t kR1VPhysicsShutdownExpectedPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
	0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0xBE, 0xF2,
	0xFF, 0xFF,
};

inline constexpr std::uintptr_t kShutdownCriticalSectionOffset = 0x8;
inline constexpr std::uintptr_t kShutdownQueueCapacityOffset = 0x140090;
inline constexpr std::uintptr_t kShutdownQueueCountOffset = 0x140092;
inline constexpr std::uintptr_t kShutdownQueueStorageOffset = 0x140098;
inline constexpr std::uintptr_t kShutdownQueueInlineStorageOffset = 0x1400A0;
inline constexpr std::size_t kShutdownOwnerFixtureSize =
	kShutdownQueueInlineStorageOffset + sizeof(std::uintptr_t);

enum class ShutdownFailure
{
	None,
	InvalidDependencies,
	InvalidOwner,
	UnreadableQueueStorage,
	NullQueueObject,
	UnreadableQueueObject,
	UnreadableQueueVtable,
	NonExecutableQueueCallback,
	UnreadableReleaseStorage,
};

using ShutdownStep = std::int64_t(__fastcall*)(std::uintptr_t value);
using DeleteCriticalSectionStep = void(__fastcall*)(std::uintptr_t value);

struct ShutdownFunctions
{
	ShutdownStep prepareQueue{};
	ShutdownStep prepareResources{};
	ShutdownStep releaseStorage{};
	DeleteCriticalSectionStep deleteCriticalSection{};
};

struct ShutdownResult
{
	ShutdownFailure failure{ ShutdownFailure::None };
	std::uint32_t callbacksInvoked{};
	std::uintptr_t detail{};
	bool storageReleased{};
	bool criticalSectionDeleted{};
};

[[nodiscard]] bool HasExpectedR1VPhysicsHeaders(std::uintptr_t moduleBase) noexcept;
[[nodiscard]] bool IsExpectedR1VPhysicsModule(std::uintptr_t moduleBase) noexcept;
[[nodiscard]] bool IsExpectedR1VPhysicsModulePath(const wchar_t* modulePath) noexcept;
[[nodiscard]] bool HasExpectedR1VPhysicsShutdownCode(std::uintptr_t moduleBase) noexcept;
[[nodiscard]] ShutdownResult RunR1VPhysicsShutdown(
	std::uintptr_t owner,
	const ShutdownFunctions& functions);
[[nodiscard]] const char* ShutdownFailureText(ShutdownFailure failure) noexcept;
}
