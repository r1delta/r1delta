#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace r1delta::materialsystem_dx11
{
inline constexpr std::uintptr_t kTxaaQueuedResolveProducerRva = 0x5C540;
inline constexpr std::uintptr_t kTxaaQueuedResolveCallbackRva = 0x5AE20;
inline constexpr std::uintptr_t kTxaaQueueAllocateRva = 0x75C20;
inline constexpr std::uintptr_t kTxaaQueuePublishRva = 0x75510;
inline constexpr std::uintptr_t kTxaaSourceGlobalRva = 0x2983F8;
inline constexpr std::uintptr_t kTxaaQueuedResolveVtableEntryRva = 0x188178;

inline constexpr std::ptrdiff_t kQueuedRenderContextHardwareContextOffset = 0xD8;
inline constexpr std::ptrdiff_t kTxaaRenderTargetViewOffset = 0x8A0;
inline constexpr std::ptrdiff_t kTxaaQueueCurrentCommandOffset = 0x10;
inline constexpr std::ptrdiff_t kTxaaQueueCommandEndOffset = 0x18;
inline constexpr std::ptrdiff_t kTxaaResolveVirtualOffset = 0x1B0;
inline constexpr std::size_t kComAddRefVirtualSlot = 1;
inline constexpr std::size_t kComReleaseVirtualSlot = 2;
inline constexpr std::uintptr_t kTxaaQueueAlignmentMask = 7;

inline constexpr std::uint8_t kExpectedTxaaQueuedResolveProducerPrologue[] = {
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
	0x99, 0xD8, 0x00, 0x00, 0x00, 0xBA, 0x10, 0x00,
	0x00, 0x00, 0x48, 0x8D, 0x0D, 0x27, 0x62, 0xFF,
	0xFF, 0x44, 0x8D, 0x42, 0xF7, 0xE8, 0xBE, 0x96,
	0x01, 0x00, 0x48, 0x8B, 0x08, 0x48, 0x8D, 0x05
};

inline constexpr std::uint8_t kExpectedTxaaQueuedResolveCallback[] = {
	0x48, 0x8B, 0x01, 0xFF, 0xA0, 0xB0, 0x01, 0x00,
	0x00
};

inline constexpr std::uint8_t kExpectedTxaaQueueAllocatePrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
	0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
	0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
	0x48, 0x83, 0xEC, 0x20, 0x44, 0x8B, 0x0D, 0x3D,
	0x3E, 0x34, 0x00, 0x65, 0x48, 0x8B, 0x04, 0x25
};

inline constexpr std::uint8_t kExpectedTxaaQueuePublishPrologue[] = {
	0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0x0D, 0x0D,
	0x01, 0x33, 0x00, 0x48, 0x89, 0x0D, 0x0E, 0x01,
	0x33, 0x00, 0x8B, 0x05, 0x80, 0x00, 0x33, 0x00,
	0x83, 0xF8, 0x02, 0x74, 0x3E, 0x8B, 0x05, 0x75,
	0x00, 0x33, 0x00, 0x85, 0xC0, 0x75, 0x10, 0x48
};

using TxaaReferenceFunction = std::uint32_t(__fastcall*)(void* object);
using TxaaQueuedResolveCallback =
	std::uintptr_t(__fastcall*)(std::uintptr_t argument);

struct TxaaQueuedResolveCommand
{
	TxaaQueuedResolveCallback callback;
	std::uintptr_t argument;
	void* renderTargetView;
};

static_assert(sizeof(TxaaQueuedResolveCommand) == sizeof(std::uintptr_t) * 3);
static_assert(alignof(TxaaQueuedResolveCommand) == alignof(std::uintptr_t));

template <typename ReadPointer>
[[nodiscard]] inline bool ReadTxaaRenderTargetView(
	std::uintptr_t materialSystemBase,
	ReadPointer&& readPointer,
	void** renderTargetView)
{
	if (!renderTargetView)
		return false;
	*renderTargetView = nullptr;
	if (!materialSystemBase
		|| materialSystemBase > std::numeric_limits<std::uintptr_t>::max()
			- kTxaaSourceGlobalRva) {
		return false;
	}

	std::uintptr_t sourceRoot = 0;
	if (!readPointer(
			materialSystemBase + kTxaaSourceGlobalRva,
			&sourceRoot)
		|| !sourceRoot) {
		return false;
	}

	std::uintptr_t textureBlock = 0;
	if (!readPointer(sourceRoot, &textureBlock)
		|| !textureBlock
		|| textureBlock > std::numeric_limits<std::uintptr_t>::max()
			- static_cast<std::uintptr_t>(kTxaaRenderTargetViewOffset)) {
		return false;
	}

	std::uintptr_t view = 0;
	if (!readPointer(
			textureBlock
				+ static_cast<std::uintptr_t>(kTxaaRenderTargetViewOffset),
			&view)) {
		return false;
	}
	*renderTargetView = reinterpret_cast<void*>(view);
	return true;
}

[[nodiscard]] inline bool RetainTxaaQueuedResolveCommand(
	TxaaQueuedResolveCallback callback,
	std::uintptr_t argument,
	void* renderTargetView,
	TxaaReferenceFunction addRef,
	TxaaQueuedResolveCommand* command)
{
	if (!callback
		|| !argument
		|| !command
		|| (renderTargetView && !addRef)) {
		return false;
	}

	if (renderTargetView)
		addRef(renderTargetView);
	*command = { callback, argument, renderTargetView };
	return true;
}

class TxaaRenderTargetViewReleaseScope
{
public:
	TxaaRenderTargetViewReleaseScope(
		void* renderTargetView,
		TxaaReferenceFunction release) noexcept
		: m_renderTargetView(renderTargetView), m_release(release)
	{
	}

	~TxaaRenderTargetViewReleaseScope()
	{
		if (m_renderTargetView)
			m_release(m_renderTargetView);
	}

	TxaaRenderTargetViewReleaseScope(
		const TxaaRenderTargetViewReleaseScope&) = delete;
	TxaaRenderTargetViewReleaseScope& operator=(
		const TxaaRenderTargetViewReleaseScope&) = delete;

private:
	void* m_renderTargetView;
	TxaaReferenceFunction m_release;
};

inline std::uintptr_t ExecuteTxaaQueuedResolve(
	std::uintptr_t queueState,
	TxaaReferenceFunction release)
{
	auto* const command = *reinterpret_cast<TxaaQueuedResolveCommand**>(
		queueState + kTxaaQueueCurrentCommandOffset);
	std::uintptr_t result = 0;
	{
		TxaaRenderTargetViewReleaseScope releaseScope(
			command->renderTargetView,
			release);
		result = command->callback(command->argument);
	}

	const std::uintptr_t next = reinterpret_cast<std::uintptr_t>(command + 1);
	*reinterpret_cast<std::uintptr_t*>(
		queueState + kTxaaQueueCurrentCommandOffset) = next;
	*reinterpret_cast<std::uintptr_t*>(
		queueState + kTxaaQueueCommandEndOffset) = next;
	return result;
}
}