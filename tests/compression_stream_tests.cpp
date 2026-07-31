#include "engine/compression_stream.h"

#include <zstd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{

using r1delta::compression::Alpha8DecompressStatus;
using r1delta::compression::R1DZstdStream;
using r1delta::compression::StreamDispatchResult;
using r1delta::compression::kR1DZstdMarker;

static_assert(static_cast<int>(Alpha8DecompressStatus::NotFinished) == 0);
static_assert(static_cast<int>(Alpha8DecompressStatus::HasMoreOutput) == 1);
static_assert(static_cast<int>(Alpha8DecompressStatus::NeedsMoreInput) == 2);
static_assert(static_cast<int>(Alpha8DecompressStatus::Success) == 3);
static_assert(static_cast<int>(Alpha8DecompressStatus::FailedInitializing) == 4);
static_assert(static_cast<int>(Alpha8DecompressStatus::FailedDestBufferTooSmall) == 5);
static_assert(static_cast<int>(Alpha8DecompressStatus::FailedExpectedMoreRawBytes) == 6);
static_assert(static_cast<int>(Alpha8DecompressStatus::FailedBadCode) == 7);
static_assert(static_cast<int>(Alpha8DecompressStatus::InvalidParameter) == 15);

int g_failures = 0;

void Check(bool condition, const char* name)
{
	if (condition)
		return;
	++g_failures;
	std::cerr << "FAILED: " << name << '\n';
}

bool IsFailure(Alpha8DecompressStatus status)
{
	return static_cast<int>(status)
		>= static_cast<int>(Alpha8DecompressStatus::FailedInitializing);
}

std::vector<uint8_t> MakePattern(size_t size)
{
	std::vector<uint8_t> data(size);
	for (size_t i = 0; i < size; ++i)
		data[i] = static_cast<uint8_t>((i * 131u + (i >> 3u) * 17u) & 0xFFu);
	return data;
}

std::vector<uint8_t> MakeIncompressible(size_t size)
{
	std::vector<uint8_t> data(size);
	uint64_t state = 0x6A09E667F3BCC909ULL;
	for (uint8_t& byte : data)
	{
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		byte = static_cast<uint8_t>(state >> 24);
	}
	return data;
}

std::vector<uint8_t> Encode(const std::vector<uint8_t>& payload)
{
	std::vector<uint8_t> encoded(
		sizeof(kR1DZstdMarker) + ZSTD_compressBound(payload.size()));
	memcpy(encoded.data(), &kR1DZstdMarker, sizeof(kR1DZstdMarker));

	const size_t compressed = ZSTD_compress(
		encoded.data() + sizeof(kR1DZstdMarker),
		encoded.size() - sizeof(kR1DZstdMarker),
		payload.empty() ? nullptr : payload.data(),
		payload.size(),
		3);
	if (ZSTD_isError(compressed))
		throw std::runtime_error(ZSTD_getErrorName(compressed));

	encoded.resize(sizeof(kR1DZstdMarker) + compressed);
	return encoded;
}

void TestOneShotModesAndTerminalState()
{
	const std::vector<uint8_t> payload = MakePattern(256 * 1024 + 19);
	const std::vector<uint8_t> encoded = Encode(payload);

	for (bool unbuffered : { false, true })
	{
		R1DZstdStream stream;
		stream.Reset(unbuffered);
		std::vector<uint8_t> output(payload.size());
		size_t inputBytes = encoded.size();
		size_t outputBytes = output.size();
		const StreamDispatchResult result = stream.Decompress(
			encoded.data(),
			&inputBytes,
			output.data(),
			&outputBytes,
			true);

		Check(!result.useLzham, "one-shot marker dispatches to ZSTD");
		Check(result.status == Alpha8DecompressStatus::Success, "one-shot status is success");
		Check(inputBytes == encoded.size(), "one-shot reports bytes consumed");
		Check(outputBytes == payload.size(), "one-shot reports bytes written");
		Check(output == payload, "one-shot payload round trips");

		inputBytes = 9;
		outputBytes = 11;
		const StreamDispatchResult terminal = stream.Decompress(
			encoded.data(),
			&inputBytes,
			output.data(),
			&outputBytes,
			true);
		Check(terminal.status == Alpha8DecompressStatus::Success, "terminal success is sticky");
		Check(inputBytes == 0 && outputBytes == 0, "terminal call consumes and writes nothing");
	}
}

void TestFallbackAndMarkerDetection()
{
	const std::array<uint8_t, 12> lzhamLike{
		0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 1, 2, 3, 4
	};
	std::array<uint8_t, 32> output{};

	R1DZstdStream fallback;
	size_t inputBytes = lzhamLike.size();
	size_t outputBytes = output.size();
	const StreamDispatchResult dispatch = fallback.Decompress(
		lzhamLike.data(),
		&inputBytes,
		output.data(),
		&outputBytes,
		true);
	Check(dispatch.useLzham, "non-marker input dispatches to LZHAM");
	Check(inputBytes == lzhamLike.size(), "LZHAM dispatch preserves input availability");
	Check(outputBytes == output.size(), "LZHAM dispatch preserves output capacity");

	const std::vector<uint8_t> payload = MakePattern(4096);
	const std::vector<uint8_t> encoded = Encode(payload);
	R1DZstdStream splitMarker;
	inputBytes = 4;
	outputBytes = output.size();
	const StreamDispatchResult partial = splitMarker.Decompress(
		encoded.data(),
		&inputBytes,
		output.data(),
		&outputBytes,
		false);
	Check(partial.status == Alpha8DecompressStatus::NeedsMoreInput, "partial marker requests more input");
	Check(inputBytes == 0, "partial marker is not consumed");
	Check(outputBytes == 0, "partial marker writes no output");

	std::vector<uint8_t> decoded(payload.size());
	inputBytes = encoded.size();
	outputBytes = decoded.size();
	const StreamDispatchResult retried = splitMarker.Decompress(
		encoded.data(),
		&inputBytes,
		decoded.data(),
		&outputBytes,
		true);
	Check(retried.status == Alpha8DecompressStatus::Success, "full marker retry succeeds");
	Check(decoded == payload, "full marker retry round trips");

	R1DZstdStream shortAtEof;
	inputBytes = 4;
	outputBytes = output.size();
	const StreamDispatchResult shortDispatch = shortAtEof.Decompress(
		encoded.data(),
		&inputBytes,
		output.data(),
		&outputBytes,
		true);
	Check(shortDispatch.useLzham, "short EOF input remains eligible for LZHAM");
	Check(inputBytes == 4 && outputBytes == output.size(), "short LZHAM dispatch preserves counts");
}

void TestInvalidParametersAndEofSequence()
{
	const std::array<uint8_t, 8> input{};
	size_t inputBytes = input.size();
	size_t outputBytes = 17;
	R1DZstdStream stream;
	const StreamDispatchResult invalid = stream.Decompress(
		nullptr,
		&inputBytes,
		nullptr,
		&outputBytes,
		false);
	Check(invalid.status == Alpha8DecompressStatus::InvalidParameter, "null nonempty buffers are invalid");
	Check(inputBytes == input.size() && outputBytes == 17, "invalid call preserves byte counts");

	inputBytes = input.size();
	outputBytes = 0;
	const StreamDispatchResult eofFallback = stream.Decompress(
		input.data(),
		&inputBytes,
		nullptr,
		&outputBytes,
		true);
	Check(eofFallback.useLzham, "EOF non-marker dispatches to LZHAM");

	inputBytes = input.size();
	outputBytes = 0;
	const StreamDispatchResult eofReversed = stream.Decompress(
		input.data(),
		&inputBytes,
		nullptr,
		&outputBytes,
		false);
	Check(eofReversed.status == Alpha8DecompressStatus::InvalidParameter, "EOF flag cannot transition back to false");
	Check(inputBytes == input.size() && outputBytes == 0, "invalid EOF transition preserves counts");
}

std::vector<uint8_t> DecodeBufferedChunked(
	const std::vector<uint8_t>& encoded,
	size_t inputChunk,
	size_t outputChunk,
	Alpha8DecompressStatus& finalStatus)
{
	R1DZstdStream stream;
	stream.Reset(false);
	std::vector<uint8_t> decoded;
	size_t cursor = 0;
	finalStatus = Alpha8DecompressStatus::NotFinished;

	for (size_t iteration = 0; iteration < 100000; ++iteration)
	{
		const size_t available = (std::min)(inputChunk, encoded.size() - cursor);
		const bool eof = cursor + available == encoded.size();
		std::vector<uint8_t> output(outputChunk);
		size_t inputBytes = available;
		size_t outputBytes = output.size();
		const StreamDispatchResult result = stream.Decompress(
			available ? encoded.data() + cursor : nullptr,
			&inputBytes,
			output.empty() ? nullptr : output.data(),
			&outputBytes,
			eof);
		cursor += inputBytes;
		decoded.insert(decoded.end(), output.begin(), output.begin() + outputBytes);
		finalStatus = result.status;

		if (result.status == Alpha8DecompressStatus::Success || IsFailure(result.status))
			break;
		if (!inputBytes && !outputBytes
			&& result.status != Alpha8DecompressStatus::NeedsMoreInput
			&& result.status != Alpha8DecompressStatus::HasMoreOutput)
		{
			finalStatus = Alpha8DecompressStatus::InvalidParameter;
			break;
		}
	}

	Check(cursor == encoded.size(), "chunked buffered decode consumes the frame");
	return decoded;
}

void TestChunkedBufferedAndZeroOutput()
{
	const std::vector<uint8_t> payload = MakePattern(768 * 1024 + 31);
	const std::vector<uint8_t> encoded = Encode(payload);
	Alpha8DecompressStatus finalStatus{};
	const std::vector<uint8_t> decoded = DecodeBufferedChunked(
		encoded,
		257,
		113,
		finalStatus);
	Check(finalStatus == Alpha8DecompressStatus::Success, "chunked buffered decode succeeds");
	Check(decoded == payload, "chunked buffered payload round trips");

	R1DZstdStream zeroOutput;
	size_t inputBytes = encoded.size();
	size_t outputBytes = 0;
	const StreamDispatchResult blocked = zeroOutput.Decompress(
		encoded.data(),
		&inputBytes,
		nullptr,
		&outputBytes,
		true);
	Check(blocked.status == Alpha8DecompressStatus::HasMoreOutput, "zero output buffer reports more output");
	Check(outputBytes == 0, "zero output buffer writes nothing");

	size_t cursor = inputBytes;
	std::vector<uint8_t> recovered;
	for (size_t iteration = 0; iteration < 100000; ++iteration)
	{
		std::array<uint8_t, 4096> chunk{};
		size_t available = encoded.size() - cursor;
		size_t written = chunk.size();
		const StreamDispatchResult result = zeroOutput.Decompress(
			available ? encoded.data() + cursor : nullptr,
			&available,
			chunk.data(),
			&written,
			true);
		cursor += available;
		recovered.insert(recovered.end(), chunk.begin(), chunk.begin() + written);
		if (result.status == Alpha8DecompressStatus::Success)
			break;
		if (IsFailure(result.status))
			break;
	}
	Check(cursor == encoded.size(), "zero-output recovery consumes all input");
	Check(recovered == payload, "zero-output recovery preserves payload");
}

void TestChunkedUnbufferedAndPointerInvariant()
{
	const std::vector<uint8_t> payload = MakePattern(512 * 1024 + 7);
	const std::vector<uint8_t> encoded = Encode(payload);
	R1DZstdStream stream;
	stream.Reset(true);
	std::vector<uint8_t> output(payload.size());
	size_t cursor = 0;
	size_t totalWritten = 0;
	Alpha8DecompressStatus finalStatus = Alpha8DecompressStatus::NotFinished;

	for (size_t iteration = 0; iteration < 100000; ++iteration)
	{
		const size_t available = (std::min<size_t>)(191, encoded.size() - cursor);
		const bool eof = cursor + available == encoded.size();
		size_t inputBytes = available;
		size_t outputBytes = output.size();
		const StreamDispatchResult result = stream.Decompress(
			available ? encoded.data() + cursor : nullptr,
			&inputBytes,
			output.data(),
			&outputBytes,
			eof);
		cursor += inputBytes;
		const bool progressed = inputBytes || outputBytes != totalWritten;
		totalWritten = outputBytes;
		finalStatus = result.status;

		if (result.status == Alpha8DecompressStatus::Success || IsFailure(result.status))
			break;
		if (!progressed)
		{
			finalStatus = Alpha8DecompressStatus::InvalidParameter;
			break;
		}
	}

	Check(finalStatus == Alpha8DecompressStatus::Success, "chunked unbuffered decode succeeds");
	Check(cursor == encoded.size(), "chunked unbuffered decode consumes the frame");
	Check(totalWritten == payload.size(), "chunked unbuffered reports cumulative output");
	Check(output == payload, "chunked unbuffered payload round trips");

	R1DZstdStream pointerCheck;
	pointerCheck.Reset(true);
	std::vector<uint8_t> first(payload.size());
	std::vector<uint8_t> second(payload.size());
	size_t inputBytes = 12;
	size_t outputBytes = first.size();
	const StreamDispatchResult started = pointerCheck.Decompress(
		encoded.data(),
		&inputBytes,
		first.data(),
		&outputBytes,
		false);
	Check(!IsFailure(started.status), "unbuffered pointer test starts");

	const size_t consumedFirst = inputBytes;
	const size_t remainingInput = encoded.size() - consumedFirst;
	inputBytes = remainingInput;
	outputBytes = second.size();
	const StreamDispatchResult changed = pointerCheck.Decompress(
		encoded.data() + consumedFirst,
		&inputBytes,
		second.data(),
		&outputBytes,
		true);
	Check(changed.status == Alpha8DecompressStatus::InvalidParameter, "unbuffered output pointer cannot change");
	Check(inputBytes == remainingInput && outputBytes == second.size(), "pointer invariant failure preserves counts");
}

void TestLargeFrameWithoutLegacyCap()
{
	const std::vector<uint8_t> payload = MakeIncompressible(2 * 1024 * 1024 + 333);
	const std::vector<uint8_t> encoded = Encode(payload);
	Check(encoded.size() > 1024 * 1024, "large test frame exceeds the old 1 MiB compressed cap");

	R1DZstdStream stream;
	stream.Reset(true);
	std::vector<uint8_t> output(payload.size());
	size_t inputBytes = encoded.size();
	size_t outputBytes = output.size();
	const StreamDispatchResult result = stream.Decompress(
		encoded.data(),
		&inputBytes,
		output.data(),
		&outputBytes,
		true);
	Check(result.status == Alpha8DecompressStatus::Success, "large compressed frame succeeds");
	Check(inputBytes == encoded.size(), "large frame reports all input consumed");
	Check(outputBytes == payload.size(), "large frame reports all output");
	Check(output == payload, "large frame round trips");
}

void TestFailuresAndReset()
{
	const std::vector<uint8_t> payload = MakePattern(64 * 1024);
	const std::vector<uint8_t> encoded = Encode(payload);

	std::vector<uint8_t> truncated = encoded;
	truncated.resize(truncated.size() - 1);
	R1DZstdStream truncatedStream;
	std::vector<uint8_t> output(payload.size());
	size_t inputBytes = truncated.size();
	size_t outputBytes = output.size();
	const StreamDispatchResult truncatedResult = truncatedStream.Decompress(
		truncated.data(),
		&inputBytes,
		output.data(),
		&outputBytes,
		true);
	Check(
		truncatedResult.status == Alpha8DecompressStatus::FailedExpectedMoreRawBytes,
		"truncated frame reports expected-more-raw-bytes");

	std::vector<uint8_t> corrupt = encoded;
	corrupt[sizeof(kR1DZstdMarker)] = 0;
	R1DZstdStream corruptStream;
	inputBytes = corrupt.size();
	outputBytes = output.size();
	const StreamDispatchResult corruptResult = corruptStream.Decompress(
		corrupt.data(),
		&inputBytes,
		output.data(),
		&outputBytes,
		true);
	Check(corruptResult.status == Alpha8DecompressStatus::FailedBadCode, "corrupt frame reports bad code");

	R1DZstdStream tooSmall;
	tooSmall.Reset(true);
	std::array<uint8_t, 32> tiny{};
	inputBytes = encoded.size();
	outputBytes = tiny.size();
	const StreamDispatchResult smallResult = tooSmall.Decompress(
		encoded.data(),
		&inputBytes,
		tiny.data(),
		&outputBytes,
		true);
	Check(
		smallResult.status == Alpha8DecompressStatus::FailedDestBufferTooSmall,
		"unbuffered undersized destination fails explicitly");
	Check(outputBytes == 0, "undersized unbuffered failure reports no valid output");

	const std::vector<uint8_t> secondPayload = MakePattern(3333);
	const std::vector<uint8_t> secondEncoded = Encode(secondPayload);
	corruptStream.Reset(false);
	std::vector<uint8_t> secondOutput(secondPayload.size());
	inputBytes = secondEncoded.size();
	outputBytes = secondOutput.size();
	const StreamDispatchResult resetResult = corruptStream.Decompress(
		secondEncoded.data(),
		&inputBytes,
		secondOutput.data(),
		&outputBytes,
		true);
	Check(resetResult.status == Alpha8DecompressStatus::Success, "reset recovers a failed stream");
	Check(secondOutput == secondPayload, "reset stream decodes a new frame");

	const std::vector<uint8_t> empty;
	const std::vector<uint8_t> emptyEncoded = Encode(empty);
	R1DZstdStream emptyStream;
	inputBytes = emptyEncoded.size();
	outputBytes = 0;
	const StreamDispatchResult emptyResult = emptyStream.Decompress(
		emptyEncoded.data(),
		&inputBytes,
		nullptr,
		&outputBytes,
		true);
	Check(emptyResult.status == Alpha8DecompressStatus::Success, "empty frame succeeds");
	Check(inputBytes == emptyEncoded.size() && outputBytes == 0, "empty frame counts are exact");
}

void TestIndependentStreamConcurrency()
{
	const std::vector<uint8_t> payload = MakePattern(128 * 1024 + 5);
	const std::vector<uint8_t> encoded = Encode(payload);
	std::atomic_bool okay{ true };
	std::vector<std::thread> workers;

	for (int worker = 0; worker < 8; ++worker)
	{
		workers.emplace_back([&]() {
			for (int run = 0; run < 50; ++run)
			{
				R1DZstdStream stream;
				stream.Reset(true);
				std::vector<uint8_t> output(payload.size());
				size_t inputBytes = encoded.size();
				size_t outputBytes = output.size();
				const StreamDispatchResult result = stream.Decompress(
					encoded.data(),
					&inputBytes,
					output.data(),
					&outputBytes,
					true);
				if (result.status != Alpha8DecompressStatus::Success
					|| inputBytes != encoded.size()
					|| outputBytes != payload.size()
					|| output != payload)
				{
					okay.store(false, std::memory_order_relaxed);
					return;
				}
			}
		});
	}

	for (std::thread& worker : workers)
		worker.join();
	Check(okay.load(std::memory_order_relaxed), "independent streams are concurrently safe");
}

} // namespace

int main()
{
	try
	{
		TestOneShotModesAndTerminalState();
		TestFallbackAndMarkerDetection();
		TestInvalidParametersAndEofSequence();
		TestChunkedBufferedAndZeroOutput();
		TestChunkedUnbufferedAndPointerInvariant();
		TestLargeFrameWithoutLegacyCap();
		TestFailuresAndReset();
		TestIndependentStreamConcurrency();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: unexpected exception: " << error.what() << '\n';
		return 1;
	}

	if (g_failures)
	{
		std::cerr << g_failures << " compression stream test(s) failed\n";
		return 1;
	}

	std::cout << "All compression stream tests passed\n";
	return 0;
}
