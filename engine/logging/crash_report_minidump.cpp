#include "crash_report_minidump.h"

#include <DbgHelp.h>
#include <zstd.h>

#include <atomic>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>

#pragma comment(lib, "dbghelp.lib")

namespace r1delta::logging::crash_report_minidump
{
namespace
{

constexpr size_t kMaximumDumpSize = 256u * 1024u * 1024u;
constexpr MINIDUMP_TYPE kDumpType = static_cast<MINIDUMP_TYPE>(
	MiniDumpNormal
	| MiniDumpWithFullMemoryInfo
	| MiniDumpWithThreadInfo
	| MiniDumpWithProcessThreadData
	| MiniDumpWithUnloadedModules);
constexpr size_t kTemporaryPathCapacity = 4096;

volatile LONG g_temporaryFileSequence = 0;

struct CapturePayload
{
	BoundedMemoryRegionPlan memoryPlan{};
	CrashCaptureManifest manifest{};
	detail::CrashMemoryCaptureClaims memoryClaims{};
	std::array<uint8_t, 4096> scanBuffer{};
	std::atomic<size_t> nextMemoryRegion{ 0 };
};
std::atomic_flag g_minidumpWriteActive = ATOMIC_FLAG_INIT;

enum class Failure
{
	None,
	InvalidArguments,
	TemporaryPath,
	TemporaryFile,
	MinidumpWrite,
	MinidumpRead,
	DumpTooLarge,
	Compression,
	Ascii85,
	Allocation,
	UnexpectedException,
};

struct CaptureState
{
	HANDLE file = INVALID_HANDLE_VALUE;
	char path[kTemporaryPathCapacity]{};
	CapturePayload* payload = nullptr;
	bool ownsMinidumpWrite = false;
};

struct BuildMetrics
{
	uint64_t rawSize = 0;
	uint64_t compressedSize = 0;
	uint64_t encodedSize = 0;
};

bool IsOptionalPlannedRead(
	const CapturePayload& payload,
	uint64_t address,
	uint32_t size) noexcept
{
	if (size == 0 || address > std::numeric_limits<uint64_t>::max() - size)
		return false;
	const uint64_t end = address + size;
	for (uint32_t i = 0; i < payload.manifest.header.memoryRegionCount; ++i)
	{
		const CrashCaptureMemoryRegion& region = payload.manifest.memoryRegions[i];
		const uint64_t regionEnd = region.base + region.size;
		if (address >= region.base && end <= regionEnd)
			return true;
	}
	return false;
}

BOOL CALLBACK MinidumpCallback(
	PVOID callbackParameter,
	const PMINIDUMP_CALLBACK_INPUT input,
	PMINIDUMP_CALLBACK_OUTPUT output)
{
	if (!callbackParameter || !input || !output)
		return FALSE;

	CapturePayload& payload = *static_cast<CapturePayload*>(callbackParameter);
	if (input->CallbackType == MemoryCallback)
	{
		const size_t index =
			payload.nextMemoryRegion.fetch_add(1, std::memory_order_relaxed);
		if (index >= payload.manifest.header.memoryRegionCount)
			return FALSE;
		const CrashCaptureMemoryRegion& region =
			payload.manifest.memoryRegions[index];
		output->MemoryBase = region.base;
		output->MemorySize = region.size;
		return TRUE;
	}
	if (input->CallbackType == ReadMemoryFailureCallback)
	{
		if (!IsOptionalPlannedRead(
				payload,
				input->ReadMemoryFailure.Offset,
				input->ReadMemoryFailure.Bytes))
		{
			return FALSE;
		}
		output->Status = S_OK;
		return TRUE;
	}
	if (input->CallbackType == CancelCallback)
	{
		output->CheckCancel = FALSE;
		output->Cancel = FALSE;
	}
	return TRUE;
}

const char* FailureText(Failure failure) noexcept
{
	switch (failure)
	{
	case Failure::InvalidArguments: return "invalid capture arguments";
	case Failure::TemporaryPath: return "temporary path is too long";
	case Failure::TemporaryFile: return "temporary file creation failed";
	case Failure::MinidumpWrite: return "MiniDumpWriteDump failed";
	case Failure::MinidumpRead: return "temporary minidump read failed";
	case Failure::DumpTooLarge: return "minidump exceeded the embedding limit";
	case Failure::Compression: return "Zstandard compression failed";
	case Failure::Ascii85: return "ASCII85 encoding failed";
	case Failure::Allocation: return "memory allocation failed";
	case Failure::UnexpectedException: return "minidump embedding raised an exception";
	default: return "minidump embedding failed";
	}
}

void AppendSize(std::string& text, uint64_t value)
{
	char buffer[32];
	_ui64toa_s(value, buffer, sizeof(buffer), 10);
	text.append(buffer);
}

void BuildUnavailableSection(
	Failure failure,
	DWORD windowsError,
	const BuildMetrics& metrics,
	std::string& section)
{
	std::string result;
	result.reserve(512);
	result.append(kSectionHeader);
	result.append("\nStatus: unavailable\n");
	result.append("Format: Windows MiniDumpNormal\n");
	result.append("Additional-Flags: ");
	result.append(kAdditionalFlagsDescription);
	result.push_back('\n');
	result.append("Compression: Zstandard\n");
	result.append("Compression-Level: 19\n");
	result.append("Content-Checksum: enabled\n");
	result.append("Encoding: ASCII85\n");
	result.append("Line-Width: 80\nRaw-Size: ");
	AppendSize(result, metrics.rawSize);
	result.append("\nCompressed-Size: ");
	AppendSize(result, metrics.compressedSize);
	result.append("\nEncoded-Size: ");
	AppendSize(result, metrics.encodedSize);
	result.append("\nReason: ");
	result.append(FailureText(failure));
	if (windowsError != ERROR_SUCCESS)
	{
		result.append(" (Windows error ");
		AppendSize(result, windowsError);
		result.push_back(')');
	}
	result.push_back('\n');
	result.append(kBeginMarker);
	result.push_back('\n');
	result.append(kEndMarker);
	section.swap(result);
}

bool CreateTemporaryFile(
	const char* reportPath,
	CaptureState* state,
	Failure* failure,
	DWORD* windowsError) noexcept
{
	LARGE_INTEGER counter{};
	QueryPerformanceCounter(&counter);

	for (int attempt = 0; attempt < 16; ++attempt)
	{
		char candidate[kTemporaryPathCapacity];
		const LONG sequence = InterlockedIncrement(&g_temporaryFileSequence);
		const int length = _snprintf_s(
			candidate,
			sizeof(candidate),
			_TRUNCATE,
			"%s.minidump.%lu.%lu.%lld.%ld.tmp",
			reportPath,
			GetCurrentProcessId(),
			GetCurrentThreadId(),
			static_cast<long long>(counter.QuadPart),
			sequence);
		if (length < 0)
		{
			*failure = Failure::TemporaryPath;
			*windowsError = ERROR_FILENAME_EXCED_RANGE;
			return false;
		}

		HANDLE file = CreateFileA(
			candidate,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			CREATE_NEW,
			FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_DELETE_ON_CLOSE,
			nullptr);
		if (file != INVALID_HANDLE_VALUE)
		{
			state->file = file;
			strcpy_s(state->path, candidate);
			return true;
		}

		const DWORD error = GetLastError();
		if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
		{
			*failure = Failure::TemporaryFile;
			*windowsError = error;
			return false;
		}
	}

	*failure = Failure::TemporaryFile;
	*windowsError = ERROR_FILE_EXISTS;
	return false;
}

bool CompressDump(
	const std::vector<uint8_t>& raw,
	std::vector<uint8_t>& compressed,
	Failure* failure)
{
	const size_t bound = ZSTD_compressBound(raw.size());
	if (bound == 0)
	{
		*failure = Failure::Compression;
		return false;
	}
	compressed.resize(bound);

	ZSTD_CCtx* context = ZSTD_createCCtx();
	if (!context)
	{
		*failure = Failure::Allocation;
		return false;
	}

	const size_t levelResult = ZSTD_CCtx_setParameter(
		context,
		ZSTD_c_compressionLevel,
		kCompressionLevel);
	const size_t checksumResult = ZSTD_CCtx_setParameter(
		context,
		ZSTD_c_checksumFlag,
		1);
	size_t compressedSize = levelResult;
	if (!ZSTD_isError(levelResult))
		compressedSize = checksumResult;
	if (!ZSTD_isError(compressedSize))
	{
		compressedSize = ZSTD_compress2(
			context,
			compressed.data(),
			compressed.size(),
			raw.data(),
			raw.size());
	}
	ZSTD_freeCCtx(context);

	if (ZSTD_isError(compressedSize))
	{
		*failure = Failure::Compression;
		return false;
	}

	compressed.resize(compressedSize);
	return true;
}

void BuildAvailableSection(
	const std::string& encoded,
	const BuildMetrics& metrics,
	std::string& section)
{
	if (encoded.size() > std::numeric_limits<size_t>::max() - encoded.size() / kAscii85LineWidth - 512)
		throw std::bad_alloc();

	std::string result;
	result.reserve(encoded.size() + encoded.size() / kAscii85LineWidth + 512);
	result.append(kSectionHeader);
	result.append("\nStatus: available\n");
	result.append("Format: Windows MiniDumpNormal\n");
	result.append("Additional-Flags: ");
	result.append(kAdditionalFlagsDescription);
	result.push_back('\n');
	result.append("Compression: Zstandard\n");
	result.append("Compression-Level: 19\n");
	result.append("Content-Checksum: enabled\n");
	result.append("Encoding: ASCII85\n");
	result.append("Line-Width: 80\nRaw-Size: ");
	AppendSize(result, metrics.rawSize);
	result.append("\nCompressed-Size: ");
	AppendSize(result, metrics.compressedSize);
	result.append("\nEncoded-Size: ");
	AppendSize(result, metrics.encodedSize);
	result.push_back('\n');
	result.append(kBeginMarker);
	result.push_back('\n');

	for (size_t offset = 0; offset < encoded.size(); offset += kAscii85LineWidth)
	{
		const size_t lineSize = std::min(kAscii85LineWidth, encoded.size() - offset);
		result.append(encoded, offset, lineSize);
		result.push_back('\n');
	}
	result.append(kEndMarker);
	section.swap(result);
}

bool CaptureAndBuildImpl(
	EXCEPTION_POINTERS* exceptionPointers,
	const char* reportPath,
	std::string* section,
	std::vector<uint8_t>* capturedDump,
	CaptureState* state,
	BuildMetrics* metrics,
	Failure* failure,
	DWORD* windowsError)
{
	if (!CreateTemporaryFile(reportPath, state, failure, windowsError))
		return false;
	if (!exceptionPointers->ExceptionRecord || !exceptionPointers->ContextRecord)
	{
		*failure = Failure::InvalidArguments;
		return false;
	}

	state->payload = static_cast<CapturePayload*>(VirtualAlloc(
		nullptr,
		sizeof(CapturePayload),
		MEM_RESERVE | MEM_COMMIT,
		PAGE_READWRITE));

	MINIDUMP_USER_STREAM_INFORMATION* userStreamInformation = nullptr;
	MINIDUMP_CALLBACK_INFORMATION* callbackInformation = nullptr;
	MINIDUMP_USER_STREAM userStream{};
	MINIDUMP_USER_STREAM_INFORMATION userStreams{};
	MINIDUMP_CALLBACK_INFORMATION callbacks{};
	if (state->payload)
	{
		new (state->payload) CapturePayload{};
		detail::BuildCrashCaptureMemory(
			*exceptionPointers,
			state->payload->memoryPlan,
			state->payload->manifest,
			state->payload->memoryClaims,
			state->payload->scanBuffer.data(),
			state->payload->scanBuffer.size());

		userStream.Type = kCaptureManifestStreamType;
		userStream.BufferSize = static_cast<ULONG>(sizeof(state->payload->manifest));
		userStream.Buffer = &state->payload->manifest;
		userStreams.UserStreamCount = 1;
		userStreams.UserStreamArray = &userStream;
		userStreamInformation = &userStreams;

		callbacks.CallbackRoutine = &MinidumpCallback;
		callbacks.CallbackParam = state->payload;
		callbackInformation = &callbacks;
	}

	MINIDUMP_EXCEPTION_INFORMATION exceptionInformation{};
	exceptionInformation.ThreadId = GetCurrentThreadId();
	exceptionInformation.ExceptionPointers = exceptionPointers;
	exceptionInformation.ClientPointers = FALSE;
	if (g_minidumpWriteActive.test_and_set(std::memory_order_acquire))
	{
		*failure = Failure::MinidumpWrite;
		*windowsError = ERROR_BUSY;
		return false;
	}
	state->ownsMinidumpWrite = true;
	const BOOL dumpWritten = MiniDumpWriteDump(
		GetCurrentProcess(),
		GetCurrentProcessId(),
		state->file,
		kDumpType,
		&exceptionInformation,
		userStreamInformation,
		callbackInformation);
	const DWORD dumpError = dumpWritten ? ERROR_SUCCESS : GetLastError();
	if (state->payload)
		detail::ReleaseCrashCaptureMemoryClaims(state->payload->memoryClaims);
	g_minidumpWriteActive.clear(std::memory_order_release);
	state->ownsMinidumpWrite = false;
	if (!dumpWritten)
	{
		*failure = Failure::MinidumpWrite;
		*windowsError = dumpError;
		return false;
	}

	LARGE_INTEGER fileSize{};
	if (!GetFileSizeEx(state->file, &fileSize) || fileSize.QuadPart <= 0)
	{
		*failure = Failure::MinidumpRead;
		*windowsError = GetLastError();
		return false;
	}
	if (static_cast<uint64_t>(fileSize.QuadPart) > kMaximumDumpSize
		|| static_cast<uint64_t>(fileSize.QuadPart) > std::numeric_limits<size_t>::max())
	{
		*failure = Failure::DumpTooLarge;
		return false;
	}

	LARGE_INTEGER beginning{};
	if (!SetFilePointerEx(state->file, beginning, nullptr, FILE_BEGIN))
	{
		*failure = Failure::MinidumpRead;
		*windowsError = GetLastError();
		return false;
	}

	std::vector<uint8_t> raw(static_cast<size_t>(fileSize.QuadPart));
	size_t offset = 0;
	while (offset < raw.size())
	{
		const DWORD requested = static_cast<DWORD>(std::min<size_t>(
			raw.size() - offset,
			std::numeric_limits<DWORD>::max()));
		DWORD bytesRead = 0;
		if (!ReadFile(state->file, raw.data() + offset, requested, &bytesRead, nullptr)
			|| bytesRead == 0)
		{
			*failure = Failure::MinidumpRead;
			*windowsError = GetLastError();
			return false;
		}
		offset += bytesRead;
	}
	metrics->rawSize = raw.size();

	std::vector<uint8_t> compressed;
	if (!CompressDump(raw, compressed, failure))
		return false;
	metrics->compressedSize = compressed.size();

	std::string encoded;
	if (!EncodeAscii85(compressed.data(), compressed.size(), encoded))
	{
		*failure = Failure::Ascii85;
		return false;
	}
	metrics->encodedSize = encoded.size();
	BuildAvailableSection(encoded, *metrics, *section);

	if (capturedDump)
		capturedDump->swap(raw);
	return true;
}

bool CaptureAndBuildGuarded(
	EXCEPTION_POINTERS* exceptionPointers,
	const char* reportPath,
	std::string* section,
	std::vector<uint8_t>* capturedDump,
	BuildMetrics* metrics,
	Failure* failure,
	DWORD* windowsError)
{
	CaptureState state;
	bool result = false;
	__try
	{
		__try
		{
			result = CaptureAndBuildImpl(
				exceptionPointers,
				reportPath,
				section,
				capturedDump,
				&state,
				metrics,
				failure,
				windowsError);
		}
		__finally
		{
			if (state.ownsMinidumpWrite)
				g_minidumpWriteActive.clear(std::memory_order_release);
			if (state.file != INVALID_HANDLE_VALUE)
				CloseHandle(state.file);
			if (state.path[0] != '\0')
				DeleteFileA(state.path);
			if (state.payload)
			{
				detail::ReleaseCrashCaptureMemoryClaims(
					state.payload->memoryClaims);
				state.payload->~CapturePayload();
				VirtualFree(state.payload, 0, MEM_RELEASE);
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		*failure = Failure::UnexpectedException;
		*windowsError = GetExceptionCode();
		result = false;
	}

	return result;
}

} // namespace

std::string FormatRegisterMemoryPreview(
	const uint8_t* data,
	size_t size,
	bool truncated) noexcept
{
	try
	{
		if (!data || size == 0)
			return {};

		const size_t sampledSize = (std::min)(size, kRegisterMemoryPreviewBytes);
		const bool wasTruncated = truncated || size > sampledSize;
		constexpr char kHexDigits[] = "0123456789ABCDEF";

		std::string result;
		result.reserve(32 + sampledSize * 4);
		result.append("[hex:");
		for (size_t i = 0; i < sampledSize; ++i)
		{
			const uint8_t value = data[i];
			result.push_back(' ');
			result.push_back(kHexDigits[value >> 4]);
			result.push_back(kHexDigits[value & 0x0F]);
		}

		result.append("; ascii: \"");
		for (size_t i = 0; i < sampledSize; ++i)
		{
			const uint8_t value = data[i];
			if (value == '"' || value == '\\')
				result.push_back('\\');
			if (value >= 0x20 && value <= 0x7E)
				result.push_back(static_cast<char>(value));
			else
				result.push_back('.');
		}
		result.push_back('"');
		if (wasTruncated)
			result.append("; truncated");
		result.push_back(']');
		return result;
	}
	catch (...)
	{
		return {};
	}
}

std::string ReadRegisterMemoryPreview(const void* address) noexcept
{
	if (!address)
		return {};

	MEMORY_BASIC_INFORMATION memory{};
	if (VirtualQuery(address, &memory, sizeof(memory)) == 0
		|| memory.State != MEM_COMMIT
		|| (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
	{
		return {};
	}

	const uintptr_t value = reinterpret_cast<uintptr_t>(address);
	const uintptr_t regionBase = reinterpret_cast<uintptr_t>(memory.BaseAddress);
	if (value < regionBase)
		return {};
	const uintptr_t offset = value - regionBase;
	if (offset >= memory.RegionSize)
		return {};

	const size_t available = memory.RegionSize - static_cast<size_t>(offset);
	const size_t requested = (std::min)(available, kRegisterMemoryPreviewBytes);
	uint8_t sample[kRegisterMemoryPreviewBytes]{};
	SIZE_T bytesRead = 0;
	if (!ReadProcessMemory(
		GetCurrentProcess(),
		address,
		sample,
		requested,
		&bytesRead)
		&& bytesRead == 0)
	{
		return {};
	}
	if (bytesRead == 0 || bytesRead > requested)
		return {};

	return FormatRegisterMemoryPreview(
		sample,
		static_cast<size_t>(bytesRead),
		available > static_cast<size_t>(bytesRead));
}

bool EncodeAscii85(const uint8_t* data, size_t size, std::string& encoded) noexcept
{
	try
	{
		if (!data && size != 0)
			return false;
		if (size > std::numeric_limits<size_t>::max() - 3)
			return false;
		const size_t groups = (size + 3) / 4;
		if (groups > std::numeric_limits<size_t>::max() / 5)
			return false;

		std::string result;
		result.reserve(groups * 5);
		for (size_t offset = 0; offset < size; offset += 4)
		{
			const size_t remaining = std::min<size_t>(4, size - offset);
			uint32_t tuple = 0;
			for (size_t i = 0; i < 4; ++i)
			{
				tuple <<= 8;
				if (i < remaining)
					tuple |= data[offset + i];
			}

			char digits[5];
			for (int i = 4; i >= 0; --i)
			{
				digits[i] = static_cast<char>(tuple % 85 + '!');
				tuple /= 85;
			}
			result.append(digits, remaining == 4 ? 5 : remaining + 1);
		}
		encoded.swap(result);
		return true;
	}
	catch (...)
	{
		return false;
	}
}

bool BuildCrashReportMinidumpSection(
	EXCEPTION_POINTERS* exceptionPointers,
	const char* reportPath,
	std::string& section,
	std::vector<uint8_t>* capturedDump) noexcept
{
	Failure failure = Failure::None;
	DWORD windowsError = ERROR_SUCCESS;
	BuildMetrics metrics;

	if (!exceptionPointers || !reportPath || reportPath[0] == '\0')
	{
		failure = Failure::InvalidArguments;
		try
		{
			BuildUnavailableSection(failure, windowsError, metrics, section);
		}
		catch (...)
		{
			section.clear();
		}
		return false;
	}

	try
	{
		if (CaptureAndBuildGuarded(
			exceptionPointers,
			reportPath,
			&section,
			capturedDump,
			&metrics,
			&failure,
			&windowsError))
		{
			return true;
		}
	}
	catch (const std::bad_alloc&)
	{
		failure = Failure::Allocation;
		windowsError = ERROR_NOT_ENOUGH_MEMORY;
	}
	catch (...)
	{
		failure = Failure::UnexpectedException;
	}

	try
	{
		BuildUnavailableSection(failure, windowsError, metrics, section);
	}
	catch (...)
	{
		section.clear();
	}
	return false;
}

} // namespace r1delta::logging::crash_report_minidump
