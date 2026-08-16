#pragma once

#include <cstddef>
#include <cstdint>
#include <cwchar>

namespace r1delta::client_studio_header
{
inline constexpr std::uintptr_t kWrapperRva = 0x5FF60;
inline constexpr std::uintptr_t kLazyInitRva = 0x5C3A0;
inline constexpr std::uintptr_t kLookupRva = 0x55F000;
inline constexpr std::ptrdiff_t kRenderableOffset = 0x8;
inline constexpr std::ptrdiff_t kStudioHeaderOffset = 0x2298;
inline constexpr std::size_t kReadinessVtableSlot = 0x40 / sizeof(void*);
inline constexpr std::uint32_t kExpectedTimeDateStamp = 0x55038DB8;
inline constexpr std::uint32_t kExpectedImageSize = 0x39A4000;

inline constexpr std::uint8_t kExpectedWrapperBytes[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
	0xEC, 0x20, 0x48, 0x83, 0xB9, 0x98, 0x22, 0x00,
	0x00, 0x00, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9,
	0x75, 0x18, 0x48, 0x8B, 0x41, 0x08, 0x48, 0x83,
	0xC1, 0x08, 0xFF, 0x50, 0x40, 0x48, 0x85, 0xC0,
	0x74, 0x08, 0x48, 0x8B, 0xCB, 0xE8, 0x0E, 0xC4,
	0xFF, 0xFF, 0x48, 0x8B, 0x8B, 0x98, 0x22, 0x00,
	0x00, 0x48, 0x8B, 0xD7, 0x48, 0x8B, 0x5C, 0x24,
	0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xE9, 0x55,
	0xF0, 0x4F, 0x00,
};
static_assert(sizeof(kExpectedWrapperBytes) == 0x4B);

inline constexpr std::uint8_t kExpectedLazyInitPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x20, 0x56, 0x48, 0x83,
	0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x81, 0xC1,
	0xA8, 0x22, 0x00, 0x00,
};

inline constexpr std::uint8_t kExpectedLookupPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
	0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
	0x41, 0x54, 0x41, 0x55, 0x48, 0x83, 0xEC, 0x20,
};

using WrapperFunction = int(__fastcall*)(void* entity, const char* name);
using LazyInitFunction = void(__fastcall*)(void* entity);
using RenderableReadinessFunction = void*(__fastcall*)(void* renderable);
using LookupFunction = int(__fastcall*)(void* studioHeader, const char* name);

inline bool IsExpectedModulePath(const wchar_t* modulePath) noexcept
{
	constexpr wchar_t suffix[] = L"\\r1\\bin\\x64_retail\\client.dll";
	if (!modulePath)
		return false;

	const std::size_t pathLength = std::wcslen(modulePath);
	constexpr std::size_t suffixLength = sizeof(suffix) / sizeof(suffix[0]) - 1;
	return pathLength >= suffixLength
		&& _wcsicmp(modulePath + pathLength - suffixLength, suffix) == 0;
}

inline bool ShouldInstall(bool isClient2015, const wchar_t* modulePath) noexcept
{
	return isClient2015 && IsExpectedModulePath(modulePath);
}

inline int LookupWithNullGuard(
	void* entity,
	const char* name,
	LazyInitFunction lazyInit,
	LookupFunction lookup) noexcept
{
	if (!entity || !lazyInit || !lookup)
		return -1;

	auto* const studioHeaderSlot = reinterpret_cast<void**>(
		reinterpret_cast<std::uint8_t*>(entity) + kStudioHeaderOffset);
	void* studioHeader = *studioHeaderSlot;
	if (!studioHeader) {
		auto* const renderable = reinterpret_cast<std::uint8_t*>(entity)
			+ kRenderableOffset;
		void** const vtable = *reinterpret_cast<void***>(renderable);
		const auto readiness = reinterpret_cast<RenderableReadinessFunction>(
			vtable[kReadinessVtableSlot]);
		if (readiness(renderable))
			lazyInit(entity);
		studioHeader = *studioHeaderSlot;
	}
	return studioHeader ? lookup(studioHeader, name) : -1;
}
}
