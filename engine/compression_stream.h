#pragma once

#include <cstddef>
#include <cstdint>

namespace r1delta::compression
{

// R1's VPK codec is LZHAM Alpha 8. These values are part of that API's ABI.
enum class Alpha8DecompressStatus : int
{
	NotFinished = 0,
	HasMoreOutput = 1,
	NeedsMoreInput = 2,
	Success = 3,
	FailedInitializing = 4,
	FailedDestBufferTooSmall = 5,
	FailedExpectedMoreRawBytes = 6,
	FailedBadCode = 7,
	InvalidParameter = 15,
};

inline constexpr uint64_t kR1DZstdMarker = 0x5244315F5F4D4150ULL;

struct StreamDispatchResult
{
	bool useLzham{};
	Alpha8DecompressStatus status{ Alpha8DecompressStatus::NotFinished };
};

// Implements the R1Delta marker + ZSTD side of the Alpha 8 streaming ABI.
// Non-marked input is left completely untouched and dispatched to LZHAM.
class R1DZstdStream
{
public:
	R1DZstdStream() noexcept;
	~R1DZstdStream() noexcept;

	R1DZstdStream(const R1DZstdStream&) = delete;
	R1DZstdStream& operator=(const R1DZstdStream&) = delete;

	void Reset(bool outputUnbuffered = false) noexcept;

	StreamDispatchResult Decompress(
		const void* input,
		size_t* inputBytes,
		void* output,
		size_t* outputBytes,
		bool noMoreInput) noexcept;

	const char* LastErrorName() const noexcept;

private:
	enum class Mode : uint8_t
	{
		Undetermined,
		Lzham,
		Zstd,
		Failed,
		Complete,
	};

	enum class ErrorKind : uint8_t
	{
		None,
		Initialization,
		OutputTooSmall,
		Zstd,
		Truncated,
	};

	StreamDispatchResult Fail(
		Alpha8DecompressStatus status,
		ErrorKind errorKind,
		size_t zstdError = 0) noexcept;

	void* m_dstream{};
	Mode m_mode{ Mode::Undetermined };
	ErrorKind m_errorKind{ ErrorKind::None };
	Alpha8DecompressStatus m_terminalStatus{ Alpha8DecompressStatus::NotFinished };
	size_t m_lastZstdError{};
	bool m_eofSeen{};
	bool m_zstdReady{};
	bool m_outputUnbuffered{};
	void* m_originalOutput{};
	size_t m_originalOutputCapacity{};
	size_t m_totalOutput{};
};

} // namespace r1delta::compression
