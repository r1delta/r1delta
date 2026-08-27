#pragma once

#include "crash_report_memory_capture.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace r1delta::logging::crash_report_minidump
{

inline constexpr int kCompressionLevel = 19;
inline constexpr size_t kAscii85LineWidth = 80;
inline constexpr size_t kRegisterMemoryPreviewBytes = 16;
inline constexpr char kAdditionalFlagsDescription[] =
	"MiniDumpWithFullMemoryInfo | MiniDumpWithThreadInfo | "
	"MiniDumpWithProcessThreadData | MiniDumpWithUnloadedModules";
inline constexpr char kSectionHeader[] = "=== R1Delta Crash Minidump v1 ===";
inline constexpr char kBeginMarker[] = "-----BEGIN R1DELTA CRASH MINIDUMP v1-----";
inline constexpr char kEndMarker[] = "-----END R1DELTA CRASH MINIDUMP v1-----";
inline constexpr char kUnavailableSection[] =
	"=== R1Delta Crash Minidump v1 ===\n"
	"Status: unavailable\n"
	"Format: Windows MiniDumpNormal\n"
	"Additional-Flags: MiniDumpWithFullMemoryInfo | MiniDumpWithThreadInfo | MiniDumpWithProcessThreadData | MiniDumpWithUnloadedModules\n"
	"Compression: Zstandard\n"
	"Compression-Level: 19\n"
	"Content-Checksum: enabled\n"
	"Encoding: ASCII85\n"
	"Line-Width: 80\n"
	"Raw-Size: 0\n"
	"Compressed-Size: 0\n"
	"Encoded-Size: 0\n"
	"Reason: minidump embedding failed\n"
	"-----BEGIN R1DELTA CRASH MINIDUMP v1-----\n"
	"-----END R1DELTA CRASH MINIDUMP v1-----";

std::string FormatRegisterMemoryPreview(
	const uint8_t* data,
	size_t size,
	bool truncated = false) noexcept;

std::string ReadRegisterMemoryPreview(const void* address) noexcept;

bool EncodeAscii85(const uint8_t* data, size_t size, std::string& encoded) noexcept;

bool BuildCrashReportMinidumpSection(
	EXCEPTION_POINTERS* exceptionPointers,
	const char* reportPath,
	std::string& section,
	std::vector<uint8_t>* capturedDump = nullptr) noexcept;

} // namespace r1delta::logging::crash_report_minidump
