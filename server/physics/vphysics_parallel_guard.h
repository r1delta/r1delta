#pragma once

#include <cstdint>
#include <limits>

namespace r1delta::vphysics
{
inline constexpr std::uintptr_t kSequentialDispatcherRva = 0x103120;
inline constexpr std::uintptr_t kParallelConfigurationRva = 0x1EF1D0;
inline constexpr std::uintptr_t kParallelEnabledOffset = 0x5C;
inline constexpr std::uintptr_t kParallelEnabledRva =
	kParallelConfigurationRva + kParallelEnabledOffset;
inline constexpr std::uintptr_t kSequentialWorkerPointerRva = 0x1EF258;
inline constexpr std::uintptr_t kSequentialBatchOffset = 0x100078;
inline constexpr std::uint8_t kSequentialDispatcherExpectedPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
	0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
	0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x05, 0x8D,
	0xC5, 0x0E, 0x00, 0x33, 0xF6, 0x48, 0x89, 0x0D,
};

class ScopedSequentialDispatcherState
{
public:
	ScopedSequentialDispatcherState(
		int* parallelEnabled,
		std::uintptr_t* workerPointer,
		std::uintptr_t owner) noexcept
		: m_parallelEnabled(parallelEnabled),
		m_workerPointer(workerPointer)
	{
		if (!m_parallelEnabled
			|| !m_workerPointer
			|| owner > std::numeric_limits<std::uintptr_t>::max()
				- kSequentialBatchOffset) {
			return;
		}

		m_previousParallelEnabled = *m_parallelEnabled;
		m_previousWorkerPointer = *m_workerPointer;
		*m_workerPointer = owner + kSequentialBatchOffset;
		*m_parallelEnabled = 0;
		m_entered = true;
	}

	~ScopedSequentialDispatcherState()
	{
		if (!m_entered)
			return;
		*m_workerPointer = m_previousWorkerPointer;
		*m_parallelEnabled = m_previousParallelEnabled;
	}

	ScopedSequentialDispatcherState(
		const ScopedSequentialDispatcherState&) = delete;
	ScopedSequentialDispatcherState& operator=(
		const ScopedSequentialDispatcherState&) = delete;

	[[nodiscard]] bool Entered() const noexcept
	{
		return m_entered;
	}

private:
	int* m_parallelEnabled{};
	std::uintptr_t* m_workerPointer{};
	int m_previousParallelEnabled{};
	std::uintptr_t m_previousWorkerPointer{};
	bool m_entered{};
};
}
