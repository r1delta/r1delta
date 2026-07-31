#include "compression_stream.h"

#include <algorithm>
#include <cstring>

#include <zstd.h>

namespace r1delta::compression
{
namespace
{

constexpr int kMaxWindowLog = 27; // 128 MiB; bounds decoder allocation, not total output.

Alpha8DecompressStatus StatusForInvalidParameters() noexcept
{
	return Alpha8DecompressStatus::InvalidParameter;
}

} // namespace

R1DZstdStream::R1DZstdStream() noexcept
	: m_dstream(ZSTD_createDStream())
{
	Reset();
}

R1DZstdStream::~R1DZstdStream() noexcept
{
	if (m_dstream)
		ZSTD_freeDStream(static_cast<ZSTD_DStream*>(m_dstream));
}

void R1DZstdStream::Reset(bool outputUnbuffered) noexcept
{
	m_mode = Mode::Undetermined;
	m_errorKind = ErrorKind::None;
	m_terminalStatus = Alpha8DecompressStatus::NotFinished;
	m_lastZstdError = 0;
	m_eofSeen = false;
	m_zstdReady = false;
	m_outputUnbuffered = outputUnbuffered;
	m_originalOutput = nullptr;
	m_originalOutputCapacity = 0;
	m_totalOutput = 0;

	if (!m_dstream)
	{
		m_errorKind = ErrorKind::Initialization;
		m_terminalStatus = Alpha8DecompressStatus::FailedInitializing;
		return;
	}

	const size_t resetResult = ZSTD_DCtx_reset(
		static_cast<ZSTD_DCtx*>(m_dstream),
		ZSTD_reset_session_only);
	if (ZSTD_isError(resetResult))
	{
		m_errorKind = ErrorKind::Zstd;
		m_terminalStatus = Alpha8DecompressStatus::FailedInitializing;
		m_lastZstdError = resetResult;
		return;
	}

	const size_t parameterResult = ZSTD_DCtx_setParameter(
		static_cast<ZSTD_DCtx*>(m_dstream),
		ZSTD_d_windowLogMax,
		kMaxWindowLog);
	if (ZSTD_isError(parameterResult))
	{
		m_errorKind = ErrorKind::Zstd;
		m_terminalStatus = Alpha8DecompressStatus::FailedInitializing;
		m_lastZstdError = parameterResult;
		return;
	}

	m_zstdReady = true;
}

StreamDispatchResult R1DZstdStream::Fail(
	Alpha8DecompressStatus status,
	ErrorKind errorKind,
	size_t zstdError) noexcept
{
	m_mode = Mode::Failed;
	m_errorKind = errorKind;
	m_terminalStatus = status;
	m_lastZstdError = zstdError;
	return { false, status };
}

StreamDispatchResult R1DZstdStream::Decompress(
	const void* input,
	size_t* inputBytes,
	void* output,
	size_t* outputBytes,
	bool noMoreInput) noexcept
{
	if (!inputBytes || !outputBytes)
		return { false, StatusForInvalidParameters() };

	const size_t inputAvailable = *inputBytes;
	const size_t outputCapacity = *outputBytes;
	if ((inputAvailable && !input) || (outputCapacity && !output))
		return { false, StatusForInvalidParameters() };

	if (m_eofSeen && !noMoreInput)
		return { false, StatusForInvalidParameters() };
	m_eofSeen = m_eofSeen || noMoreInput;

	if (m_mode == Mode::Lzham)
		return { true, Alpha8DecompressStatus::NotFinished };

	*inputBytes = 0;
	*outputBytes = 0;

	if (m_mode == Mode::Failed || m_mode == Mode::Complete)
		return { false, m_terminalStatus };

	size_t markerBytes = 0;
	const auto* inputData = static_cast<const uint8_t*>(input);
	if (m_mode == Mode::Undetermined)
	{
		uint8_t marker[sizeof(kR1DZstdMarker)];
		memcpy(marker, &kR1DZstdMarker, sizeof(marker));

		const size_t prefixBytes = (std::min)(inputAvailable, sizeof(marker));
		if (prefixBytes && memcmp(inputData, marker, prefixBytes) != 0)
		{
			m_mode = Mode::Lzham;
			*inputBytes = inputAvailable;
			*outputBytes = outputCapacity;
			return { true, Alpha8DecompressStatus::NotFinished };
		}

		if (inputAvailable < sizeof(marker))
		{
			if (noMoreInput)
			{
				m_mode = Mode::Lzham;
				*inputBytes = inputAvailable;
				*outputBytes = outputCapacity;
				return { true, Alpha8DecompressStatus::NotFinished };
			}
			return { false, Alpha8DecompressStatus::NeedsMoreInput };
		}

		m_mode = Mode::Zstd;
		markerBytes = sizeof(marker);
		if (!m_zstdReady)
		{
			*inputBytes = markerBytes;
			return Fail(
				Alpha8DecompressStatus::FailedInitializing,
				m_errorKind == ErrorKind::None ? ErrorKind::Initialization : m_errorKind,
				m_lastZstdError);
		}
	}

	void* zstdOutputData = output;
	size_t zstdOutputCapacity = outputCapacity;
	if (m_outputUnbuffered)
	{
		if (!m_originalOutput && output)
		{
			m_originalOutput = output;
			m_originalOutputCapacity = outputCapacity;
		}
		else if (m_originalOutput
			&& (m_originalOutput != output || m_originalOutputCapacity != outputCapacity))
		{
			*inputBytes = inputAvailable;
			*outputBytes = outputCapacity;
			return { false, StatusForInvalidParameters() };
		}

		if (m_totalOutput > outputCapacity)
		{
			*inputBytes = inputAvailable;
			*outputBytes = outputCapacity;
			return { false, StatusForInvalidParameters() };
		}

		zstdOutputCapacity = outputCapacity - m_totalOutput;
		zstdOutputData = zstdOutputCapacity
			? static_cast<uint8_t*>(output) + m_totalOutput
			: nullptr;
	}

	ZSTD_inBuffer zstdInput{
		inputData ? inputData + markerBytes : nullptr,
		inputAvailable - markerBytes,
		0,
	};
	ZSTD_outBuffer zstdOutput{
		zstdOutputData,
		zstdOutputCapacity,
		0,
	};

	const size_t result = ZSTD_decompressStream(
		static_cast<ZSTD_DStream*>(m_dstream),
		&zstdOutput,
		&zstdInput);

	*inputBytes = markerBytes + zstdInput.pos;
	m_totalOutput += zstdOutput.pos;
	*outputBytes = m_outputUnbuffered ? m_totalOutput : zstdOutput.pos;

	if (ZSTD_isError(result))
		return Fail(
			Alpha8DecompressStatus::FailedBadCode,
			ErrorKind::Zstd,
			result);

	if (result == 0)
	{
		m_mode = Mode::Complete;
		m_terminalStatus = Alpha8DecompressStatus::Success;
		return { false, Alpha8DecompressStatus::Success };
	}

	if (m_outputUnbuffered
		&& zstdOutput.pos == zstdOutput.size
		&& zstdInput.pos < zstdInput.size)
	{
		*outputBytes = 0;
		return Fail(
			Alpha8DecompressStatus::FailedDestBufferTooSmall,
			ErrorKind::OutputTooSmall);
	}

	if (!m_outputUnbuffered && zstdOutput.pos == zstdOutput.size)
	{
		if (zstdOutput.size)
			return { false, Alpha8DecompressStatus::NotFinished };
		if (zstdInput.pos < zstdInput.size || noMoreInput)
			return { false, Alpha8DecompressStatus::HasMoreOutput };
	}

	if (zstdInput.pos == zstdInput.size)
	{
		if (!noMoreInput)
			return { false, Alpha8DecompressStatus::NeedsMoreInput };
		return Fail(
			Alpha8DecompressStatus::FailedExpectedMoreRawBytes,
			ErrorKind::Truncated);
	}

	return { false, Alpha8DecompressStatus::NotFinished };
}

const char* R1DZstdStream::LastErrorName() const noexcept
{
	switch (m_errorKind)
	{
	case ErrorKind::Initialization:
		return "unable to create the ZSTD decompression stream";
	case ErrorKind::OutputTooSmall:
		return "unbuffered output buffer is too small";
	case ErrorKind::Zstd:
		return m_lastZstdError ? ZSTD_getErrorName(m_lastZstdError) : "ZSTD error";
	case ErrorKind::Truncated:
		return "truncated ZSTD frame at end of input";
	default:
		return "no error";
	}
}

} // namespace r1delta::compression
