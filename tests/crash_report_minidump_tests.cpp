#include "engine/logging/crash_report_minidump.h"

#include <Windows.h>
#include <zstd.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using namespace r1delta::logging::crash_report_minidump;

int g_failures = 0;

void Check(bool condition, const char* name)
{
	if (condition)
		return;
	++g_failures;
	std::cerr << "FAILED: " << name << '\n';
}

bool EndsWith(std::string_view text, std::string_view suffix)
{
	return text.size() >= suffix.size()
		&& text.substr(text.size() - suffix.size()) == suffix;
}

bool ParseSize(const std::string& section, const char* name, uint64_t& value)
{
	const std::string prefix = std::string("\n") + name + ": ";
	const size_t start = section.find(prefix);
	if (start == std::string::npos)
		return false;
	const size_t valueStart = start + prefix.size();
	const size_t end = section.find('\n', valueStart);
	if (end == std::string::npos || end == valueStart)
		return false;

	uint64_t parsed = 0;
	for (size_t i = valueStart; i < end; ++i)
	{
		if (section[i] < '0' || section[i] > '9')
			return false;
		const uint64_t digit = static_cast<uint64_t>(section[i] - '0');
		if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10)
			return false;
		parsed = parsed * 10 + digit;
	}
	value = parsed;
	return true;
}

bool ExtractPayload(const std::string& section, std::string& payload)
{
	const std::string begin = std::string(kBeginMarker) + "\n";
	const size_t beginOffset = section.find(begin);
	const size_t endOffset = section.find(kEndMarker);
	if (beginOffset == std::string::npos || endOffset == std::string::npos
		|| endOffset < beginOffset + begin.size())
	{
		return false;
	}

	std::string result;
	const size_t payloadStart = beginOffset + begin.size();
	for (size_t i = payloadStart; i < endOffset; ++i)
	{
		const unsigned char character = static_cast<unsigned char>(section[i]);
		if (!std::isspace(character))
			result.push_back(static_cast<char>(character));
	}
	payload.swap(result);
	return true;
}

bool DecodeAscii85(std::string_view encoded, std::vector<uint8_t>& decoded)
{
	std::vector<uint8_t> result;
	result.reserve(encoded.size() / 5 * 4 + 4);

	uint64_t tuple = 0;
	size_t digits = 0;
	for (char character : encoded)
	{
		if (character == 'z')
		{
			if (digits != 0)
				return false;
			result.insert(result.end(), 4, 0);
			continue;
		}
		if (character < '!' || character > 'u')
			return false;
		tuple = tuple * 85 + static_cast<unsigned>(character - '!');
		++digits;
		if (digits == 5)
		{
			if (tuple > std::numeric_limits<uint32_t>::max())
				return false;
			for (int shift = 24; shift >= 0; shift -= 8)
				result.push_back(static_cast<uint8_t>(tuple >> shift));
			tuple = 0;
			digits = 0;
		}
	}

	if (digits == 1)
		return false;
	if (digits > 1)
	{
		const size_t outputBytes = digits - 1;
		while (digits < 5)
		{
			tuple = tuple * 85 + 84;
			++digits;
		}
		if (tuple > std::numeric_limits<uint32_t>::max())
			return false;
		for (size_t i = 0; i < outputBytes; ++i)
			result.push_back(static_cast<uint8_t>(tuple >> (24 - i * 8)));
	}

	decoded.swap(result);
	return true;
}

std::string MakeReportPath()
{
	char temporaryDirectory[MAX_PATH];
	const DWORD length = GetTempPathA(static_cast<DWORD>(sizeof(temporaryDirectory)), temporaryDirectory);
	if (length == 0 || length >= sizeof(temporaryDirectory))
		return {};

	char filename[128];
	_snprintf_s(
		filename,
		sizeof(filename),
		_TRUNCATE,
		"r1delta_crash_report_minidump_tests_%lu_%llu.log",
		GetCurrentProcessId(),
		static_cast<unsigned long long>(GetTickCount64()));
	return std::string(temporaryDirectory) + filename;
}

bool TemporaryDumpExists(const std::string& reportPath)
{
	const std::string pattern = reportPath + ".minidump.*.tmp";
	WIN32_FIND_DATAA data{};
	HANDLE search = FindFirstFileA(pattern.c_str(), &data);
	if (search == INVALID_HANDLE_VALUE)
		return false;
	FindClose(search);
	return true;
}

struct CaptureResult
{
	std::string reportPath;
	std::string section;
	std::vector<uint8_t> rawDump;
	bool available = false;
};

CaptureResult* g_captureResult = nullptr;

LONG CaptureException(EXCEPTION_POINTERS* exceptionPointers)
{
	g_captureResult->available = BuildCrashReportMinidumpSection(
		exceptionPointers,
		g_captureResult->reportPath.c_str(),
		g_captureResult->section,
		&g_captureResult->rawDump);
	return EXCEPTION_EXECUTE_HANDLER;
}

void CaptureCurrentProcessDump(CaptureResult& result)
{
	g_captureResult = &result;
	__try
	{
		RaiseException(0xE0421001, 0, 0, nullptr);
	}
	__except (CaptureException(GetExceptionInformation()))
	{
	}
	g_captureResult = nullptr;
}

void TestAscii85Vectors()
{
	const std::string known = "Hello, world!";
	std::string encoded;
	Check(EncodeAscii85(
		reinterpret_cast<const uint8_t*>(known.data()),
		known.size(),
		encoded), "ASCII85 known vector encodes");
	Check(encoded == "87cURD_*#TDfTZ)+T", "ASCII85 known vector matches");

	for (size_t size = 1; size <= 3; ++size)
	{
		const std::vector<uint8_t> input{ 1, 2, 3 };
		encoded.clear();
		Check(EncodeAscii85(input.data(), size, encoded), "ASCII85 partial group encodes");
		Check(encoded.size() == size + 1, "ASCII85 partial group has canonical length");
		std::vector<uint8_t> decoded;
		Check(DecodeAscii85(encoded, decoded), "ASCII85 partial group decodes");
		Check(decoded == std::vector<uint8_t>(input.begin(), input.begin() + size),
			"ASCII85 partial group round trips");
	}
}

void TestCurrentProcessMinidumpRoundTrip()
{
	CaptureResult capture;
	capture.reportPath = MakeReportPath();
	Check(!capture.reportPath.empty(), "temporary report path is available");
	Check(!TemporaryDumpExists(capture.reportPath), "no temporary dump exists before capture");
	CaptureCurrentProcessDump(capture);

	Check(capture.available, "current-process minidump capture succeeds");
	Check(!capture.rawDump.empty(), "current-process minidump is nonempty");
	Check(capture.rawDump.size() >= 4
		&& memcmp(capture.rawDump.data(), "MDMP", 4) == 0,
		"captured bytes have the MDMP signature");
	Check(!TemporaryDumpExists(capture.reportPath), "temporary dump is deleted after capture");
	Check(capture.section.find("\nStatus: available\n") != std::string::npos,
		"section reports available status");
	Check(capture.section.find("\nCompression-Level: 19\n") != std::string::npos,
		"section reports compression level 19");
	Check(capture.section.find("\nContent-Checksum: enabled\n") != std::string::npos,
		"section reports content checksum");

	uint64_t rawSize = 0;
	uint64_t compressedSize = 0;
	uint64_t encodedSize = 0;
	Check(ParseSize(capture.section, "Raw-Size", rawSize), "raw size parses");
	Check(ParseSize(capture.section, "Compressed-Size", compressedSize), "compressed size parses");
	Check(ParseSize(capture.section, "Encoded-Size", encodedSize), "encoded size parses");
	Check(rawSize == capture.rawDump.size(), "raw size metadata matches capture");

	std::string payload;
	Check(ExtractPayload(capture.section, payload), "embedded payload parses");
	Check(encodedSize == payload.size(), "encoded size metadata matches payload");

	const size_t begin = capture.section.find(std::string(kBeginMarker) + "\n");
	const size_t end = capture.section.find(kEndMarker);
	if (begin != std::string::npos && end != std::string::npos)
	{
		size_t lineStart = begin + strlen(kBeginMarker) + 1;
		while (lineStart < end)
		{
			const size_t lineEnd = capture.section.find('\n', lineStart);
			Check(lineEnd != std::string::npos && lineEnd <= end,
				"ASCII85 payload line is terminated");
			if (lineEnd == std::string::npos || lineEnd > end)
				break;
			Check(lineEnd - lineStart <= kAscii85LineWidth,
				"ASCII85 payload line respects wrapping");
			lineStart = lineEnd + 1;
		}
	}

	std::vector<uint8_t> compressed;
	Check(DecodeAscii85(payload, compressed), "embedded ASCII85 decodes");
	Check(compressedSize == compressed.size(), "compressed size metadata matches frame");
	Check(ZSTD_getFrameContentSize(compressed.data(), compressed.size()) == rawSize,
		"Zstandard frame carries the raw content size");

	std::vector<uint8_t> decoded(static_cast<size_t>(rawSize));
	const size_t decompressedSize = ZSTD_decompress(
		decoded.data(),
		decoded.size(),
		compressed.data(),
		compressed.size());
	Check(!ZSTD_isError(decompressedSize), "Zstandard frame decompresses");
	Check(decompressedSize == decoded.size(), "Zstandard decompressed size matches metadata");
	Check(decoded == capture.rawDump, "embedded minidump round trips byte-identically");

	if (!compressed.empty())
	{
		std::vector<uint8_t> corrupted = compressed;
		corrupted.back() ^= 0x01;
		const size_t corruptedResult = ZSTD_decompress(
			decoded.data(),
			decoded.size(),
			corrupted.data(),
			corrupted.size());
		Check(ZSTD_isError(corruptedResult), "Zstandard content checksum rejects corruption");
	}

	const std::string textualReport = "textual crash report\n\n";
	const std::string completeReport = textualReport + capture.section;
	const size_t sectionOffset = completeReport.rfind(kSectionHeader);
	Check(sectionOffset != std::string::npos && sectionOffset == textualReport.size(),
		"minidump section follows textual report");
	Check(EndsWith(completeReport, kEndMarker), "minidump END marker is at EOF");
}

void TestUnavailableSectionAndCleanup()
{
	const std::string reportPath = MakeReportPath() + ".invalid";
	std::string section;
	std::vector<uint8_t> dump{ 1, 2, 3 };
	EXCEPTION_POINTERS invalidPointers{};
	const bool available = BuildCrashReportMinidumpSection(
		&invalidPointers,
		reportPath.c_str(),
		section,
		&dump);
	Check(!available, "invalid capture produces unavailable status");
	Check(section.find("\nStatus: unavailable\n") != std::string::npos,
		"failure section reports unavailable");
	Check(section.find("\nRaw-Size: 0\n") != std::string::npos,
		"failure section includes raw size");
	Check(section.size() < 1024, "failure section is bounded");
	Check(EndsWith(section, kEndMarker), "failure END marker is at EOF");
	Check(!TemporaryDumpExists(reportPath), "failure leaves no temporary dump");
	Check(dump == std::vector<uint8_t>({ 1, 2, 3 }),
		"failure does not replace caller capture data");
}

} // namespace

int main()
{
	TestAscii85Vectors();
	TestCurrentProcessMinidumpRoundTrip();
	TestUnavailableSectionAndCleanup();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " crash report minidump test(s) failed\n";
		return 1;
	}
	std::cout << "crash report minidump tests passed\n";
	return 0;
}
