#include "vphysics_shutdown_guard.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <cwchar>

namespace r1delta::vphysics
{
namespace
{
using ShutdownCallback = void(__fastcall*)(
	std::uintptr_t object,
	std::uint32_t release);

bool AddOffset(
	std::uintptr_t base,
	std::uintptr_t offset,
	std::uintptr_t& result) noexcept
{
	if (offset > std::numeric_limits<std::uintptr_t>::max() - base)
		return false;
	result = base + offset;
	return true;
}

bool IsReadableProtection(DWORD protection) noexcept
{
	if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
		return false;

	switch (protection & 0xff)
	{
	case PAGE_READONLY:
	case PAGE_READWRITE:
	case PAGE_WRITECOPY:
	case PAGE_EXECUTE_READ:
	case PAGE_EXECUTE_READWRITE:
	case PAGE_EXECUTE_WRITECOPY:
		return true;
	default:
		return false;
	}
}

bool IsWritableProtection(DWORD protection) noexcept
{
	if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
		return false;

	switch (protection & 0xff)
	{
	case PAGE_READWRITE:
	case PAGE_WRITECOPY:
	case PAGE_EXECUTE_READWRITE:
	case PAGE_EXECUTE_WRITECOPY:
		return true;
	default:
		return false;
	}
}

bool IsExecutableProtection(DWORD protection) noexcept
{
	if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
		return false;

	switch (protection & 0xff)
	{
	case PAGE_EXECUTE:
	case PAGE_EXECUTE_READ:
	case PAGE_EXECUTE_READWRITE:
	case PAGE_EXECUTE_WRITECOPY:
		return true;
	default:
		return false;
	}
}

using ProtectionPredicate = bool (*)(DWORD protection) noexcept;

bool IsRangeAccessible(
	std::uintptr_t address,
	std::size_t size,
	ProtectionPredicate predicate,
	std::uintptr_t expectedImageBase = 0) noexcept
{
	if (!address || !size
		|| size > std::numeric_limits<std::uintptr_t>::max() - address)
		return false;

	const std::uintptr_t end = address + size;
	std::uintptr_t current = address;
	while (current < end)
	{
		MEMORY_BASIC_INFORMATION memory{};
		if (VirtualQuery(
				reinterpret_cast<const void*>(current),
				&memory,
				sizeof(memory)) != sizeof(memory)
			|| memory.State != MEM_COMMIT
			|| !predicate(memory.Protect)
			|| (expectedImageBase
				&& (memory.Type != MEM_IMAGE
					|| reinterpret_cast<std::uintptr_t>(memory.AllocationBase)
						!= expectedImageBase)))
		{
			return false;
		}

		const auto regionBase = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
		if (memory.RegionSize > std::numeric_limits<std::uintptr_t>::max() - regionBase)
			return false;
		const std::uintptr_t regionEnd = regionBase + memory.RegionSize;
		if (current < regionBase || current >= regionEnd)
			return false;
		current = std::min(end, regionEnd);
	}
	return true;
}

bool IsReadableRange(std::uintptr_t address, std::size_t size) noexcept
{
	return IsRangeAccessible(address, size, &IsReadableProtection);
}

bool IsWritableRange(std::uintptr_t address, std::size_t size) noexcept
{
	return IsRangeAccessible(address, size, &IsWritableProtection);
}

bool IsExecutableAddress(std::uintptr_t address) noexcept
{
	return IsRangeAccessible(address, 1, &IsExecutableProtection);
}

template <typename T>
bool ReadValue(std::uintptr_t address, T& value) noexcept
{
	if (!IsReadableRange(address, sizeof(value)))
		return false;
	std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
	return true;
}

template <typename T>
bool WriteValue(std::uintptr_t address, const T& value) noexcept
{
	if (!IsWritableRange(address, sizeof(value)))
		return false;
	std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
	return true;
}

void SetFailure(
	ShutdownResult& result,
	ShutdownFailure failure,
	std::uintptr_t detail) noexcept
{
	if (result.failure != ShutdownFailure::None)
		return;
	result.failure = failure;
	result.detail = detail;
}

bool MatchesExpectedExecutableBytes(
	std::uintptr_t moduleBase,
	std::uintptr_t rva,
	const std::uint8_t* expected,
	std::size_t expectedSize) noexcept
{
	if (!expected || !expectedSize
		|| rva >= kR1VPhysicsSizeOfImage
		|| expectedSize > kR1VPhysicsSizeOfImage - rva)
	{
		return false;
	}

	std::uintptr_t target{};
	return AddOffset(moduleBase, rva, target)
		&& IsReadableRange(target, expectedSize)
		&& IsRangeAccessible(
			target,
			expectedSize,
			&IsExecutableProtection,
			moduleBase)
		&& std::memcmp(
			reinterpret_cast<const void*>(target),
			expected,
			expectedSize) == 0;
}
}

bool HasExpectedR1VPhysicsHeaders(std::uintptr_t moduleBase) noexcept
{
	IMAGE_DOS_HEADER dos{};
	if (!ReadValue(moduleBase, dos)
		|| dos.e_magic != IMAGE_DOS_SIGNATURE
		|| dos.e_lfanew <= 0)
	{
		return false;
	}

	const std::uintptr_t peOffset =
		static_cast<std::uintptr_t>(dos.e_lfanew);
	if (peOffset > kR1VPhysicsSizeOfImage - sizeof(IMAGE_NT_HEADERS64))
		return false;
	std::uintptr_t ntAddress{};
	if (!AddOffset(moduleBase, peOffset, ntAddress))
		return false;

	IMAGE_NT_HEADERS64 nt{};
	if (!ReadValue(ntAddress, nt))
		return false;
	return nt.Signature == IMAGE_NT_SIGNATURE
		&& nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64
		&& nt.FileHeader.TimeDateStamp == kR1VPhysicsTimeDateStamp
		&& nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC
		&& nt.OptionalHeader.SizeOfImage == kR1VPhysicsSizeOfImage;
}

bool IsExpectedR1VPhysicsModule(std::uintptr_t moduleBase) noexcept
{
	if (!HasExpectedR1VPhysicsHeaders(moduleBase))
		return false;

	MEMORY_BASIC_INFORMATION memory{};
	return VirtualQuery(
			reinterpret_cast<const void*>(moduleBase),
			&memory,
			sizeof(memory)) == sizeof(memory)
		&& memory.State == MEM_COMMIT
		&& memory.Type == MEM_IMAGE
		&& memory.BaseAddress == reinterpret_cast<void*>(moduleBase)
		&& memory.AllocationBase == reinterpret_cast<void*>(moduleBase);
}

bool IsExpectedR1VPhysicsModulePath(const wchar_t* modulePath) noexcept
{
	constexpr wchar_t suffix[] = L"\\bin\\x64_retail\\vphysics.dll";
	if (!modulePath)
		return false;
	const std::size_t pathLength = std::wcslen(modulePath);
	constexpr std::size_t suffixLength =
		sizeof(suffix) / sizeof(suffix[0]) - 1;
	return pathLength >= suffixLength
		&& _wcsicmp(modulePath + pathLength - suffixLength, suffix) == 0;
}

bool HasExpectedR1VPhysicsShutdownCode(std::uintptr_t moduleBase) noexcept
{
	return MatchesExpectedExecutableBytes(
			moduleBase,
			kR1VPhysicsPrepareQueueRva,
			kR1VPhysicsPrepareQueueExpectedPrologue,
			sizeof(kR1VPhysicsPrepareQueueExpectedPrologue))
		&& MatchesExpectedExecutableBytes(
			moduleBase,
			kR1VPhysicsPrepareResourcesRva,
			kR1VPhysicsPrepareResourcesExpectedPrologue,
			sizeof(kR1VPhysicsPrepareResourcesExpectedPrologue))
		&& MatchesExpectedExecutableBytes(
			moduleBase,
			kR1VPhysicsReleaseStorageRva,
			kR1VPhysicsReleaseStorageExpectedPrologue,
			sizeof(kR1VPhysicsReleaseStorageExpectedPrologue))
		&& MatchesExpectedExecutableBytes(
			moduleBase,
			kR1VPhysicsShutdownRva,
			kR1VPhysicsShutdownExpectedPrologue,
			sizeof(kR1VPhysicsShutdownExpectedPrologue));
}

ShutdownResult RunR1VPhysicsShutdown(
	std::uintptr_t owner,
	const ShutdownFunctions& functions)
{
	ShutdownResult result{};
	if (!functions.prepareQueue
		|| !functions.prepareResources
		|| !functions.releaseStorage
		|| !functions.deleteCriticalSection)
	{
		result.failure = ShutdownFailure::InvalidDependencies;
		return result;
	}

	std::uintptr_t criticalSectionAddress{};
	std::uintptr_t capacityAddress{};
	std::uintptr_t countAddress{};
	std::uintptr_t storageAddress{};
	std::uintptr_t inlineStorageAddress{};
	if (!owner
		|| !AddOffset(owner, kShutdownCriticalSectionOffset, criticalSectionAddress)
		|| !AddOffset(owner, kShutdownQueueCapacityOffset, capacityAddress)
		|| !AddOffset(owner, kShutdownQueueCountOffset, countAddress)
		|| !AddOffset(owner, kShutdownQueueStorageOffset, storageAddress)
		|| !AddOffset(owner, kShutdownQueueInlineStorageOffset, inlineStorageAddress)
		|| !IsWritableRange(criticalSectionAddress, sizeof(CRITICAL_SECTION))
		|| !IsWritableRange(capacityAddress, sizeof(std::uint16_t))
		|| !IsWritableRange(countAddress, sizeof(std::uint16_t))
		|| !IsWritableRange(storageAddress, sizeof(std::uintptr_t)))
	{
		result.failure = ShutdownFailure::InvalidOwner;
		result.detail = owner;
		return result;
	}

	functions.prepareQueue(owner);
	functions.prepareResources(owner);

	for (;;)
	{
		std::uint16_t count{};
		if (!ReadValue(countAddress, count))
		{
			SetFailure(result, ShutdownFailure::InvalidOwner, countAddress);
			break;
		}
		if (!count)
			break;

		std::uintptr_t storage{};
		if (!ReadValue(storageAddress, storage)
			|| !IsReadableRange(storage, sizeof(std::uintptr_t)))
		{
			SetFailure(result, ShutdownFailure::UnreadableQueueStorage, storage);
			break;
		}

		std::uintptr_t object{};
		std::memcpy(&object, reinterpret_cast<const void*>(storage), sizeof(object));
		if (!object)
		{
			SetFailure(result, ShutdownFailure::NullQueueObject, storage);
			break;
		}

		std::uintptr_t vtable{};
		if (!ReadValue(object, vtable))
		{
			SetFailure(result, ShutdownFailure::UnreadableQueueObject, object);
			break;
		}

		std::uintptr_t callbackAddress{};
		if (!ReadValue(vtable, callbackAddress))
		{
			SetFailure(result, ShutdownFailure::UnreadableQueueVtable, vtable);
			break;
		}
		if (!IsExecutableAddress(callbackAddress))
		{
			SetFailure(
				result,
				ShutdownFailure::NonExecutableQueueCallback,
				callbackAddress);
			break;
		}

		reinterpret_cast<ShutdownCallback>(callbackAddress)(object, 1);
		++result.callbacksInvoked;

	}

	std::uintptr_t storage{};
	if (!ReadValue(storageAddress, storage))
	{
		SetFailure(result, ShutdownFailure::InvalidOwner, storageAddress);
	}
	else if (storage != inlineStorageAddress)
	{
		if (storage)
		{
			if (IsReadableRange(storage, 1))
			{
				functions.releaseStorage(storage);
				result.storageReleased = true;
			}
			else
			{
				SetFailure(
					result,
					ShutdownFailure::UnreadableReleaseStorage,
					storage);
			}
		}

		const std::uintptr_t nullStorage{};
		const std::uint16_t zero{};
		if (!WriteValue(storageAddress, nullStorage)
			|| !WriteValue(capacityAddress, zero))
		{
			SetFailure(result, ShutdownFailure::InvalidOwner, storageAddress);
		}
	}

	const std::uint16_t zero{};
	if (!WriteValue(countAddress, zero))
		SetFailure(result, ShutdownFailure::InvalidOwner, countAddress);

	if (IsWritableRange(criticalSectionAddress, sizeof(CRITICAL_SECTION)))
	{
		functions.deleteCriticalSection(criticalSectionAddress);
		result.criticalSectionDeleted = true;
	}
	else
	{
		SetFailure(result, ShutdownFailure::InvalidOwner, criticalSectionAddress);
	}

	return result;
}

const char* ShutdownFailureText(ShutdownFailure failure) noexcept
{
	switch (failure)
	{
	case ShutdownFailure::None:
		return "none";
	case ShutdownFailure::InvalidDependencies:
		return "invalid dependencies";
	case ShutdownFailure::InvalidOwner:
		return "invalid shutdown owner";
	case ShutdownFailure::UnreadableQueueStorage:
		return "unreadable queue storage";
	case ShutdownFailure::NullQueueObject:
		return "null queued object";
	case ShutdownFailure::UnreadableQueueObject:
		return "unreadable queued object";
	case ShutdownFailure::UnreadableQueueVtable:
		return "unreadable queued-object vtable";
	case ShutdownFailure::NonExecutableQueueCallback:
		return "non-executable queued-object callback";
	case ShutdownFailure::UnreadableReleaseStorage:
		return "unreadable release storage";
	default:
		return "unknown";
	}
}
}
