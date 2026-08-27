#include "engine/logging/crash_report_minidump.h"

#include <Windows.h>
#include <DbgHelp.h>
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

bool FindMinidumpStream(
	const std::vector<uint8_t>& dump,
	uint32_t expectedStreamType,
	const uint8_t** streamData,
	size_t* streamSize)
{
	if (dump.size() < sizeof(MINIDUMP_HEADER))
		return false;

	MINIDUMP_HEADER header{};
	memcpy(&header, dump.data(), sizeof(header));
	if (header.Signature != MINIDUMP_SIGNATURE)
		return false;

	const size_t directoryOffset = header.StreamDirectoryRva;
	const size_t streamCount = header.NumberOfStreams;
	if (directoryOffset > dump.size()
		|| streamCount > (dump.size() - directoryOffset) / sizeof(MINIDUMP_DIRECTORY))
	{
		return false;
	}

	for (size_t i = 0; i < streamCount; ++i)
	{
		MINIDUMP_DIRECTORY directory{};
		memcpy(
			&directory,
			dump.data() + directoryOffset + i * sizeof(directory),
			sizeof(directory));
		if (directory.StreamType != expectedStreamType)
			continue;

		const size_t offset = directory.Location.Rva;
		const size_t size = directory.Location.DataSize;
		if (offset > dump.size() || size > dump.size() - offset)
			return false;
		if (streamData)
			*streamData = dump.data() + offset;
		if (streamSize)
			*streamSize = size;
		return true;
	}
	return false;
}

bool HasMinidumpStream(
	const std::vector<uint8_t>& dump,
	uint32_t expectedStreamType)
{
	return FindMinidumpStream(
		dump,
		expectedStreamType,
		nullptr,
		nullptr);
}

bool ReadCapturedMemory(
	const std::vector<uint8_t>& dump,
	uint64_t address,
	void* destination,
	size_t size)
{
	const uint8_t* stream = nullptr;
	size_t streamSize = 0;
	if (FindMinidumpStream(dump, MemoryListStream, &stream, &streamSize)
		&& streamSize >= offsetof(MINIDUMP_MEMORY_LIST, MemoryRanges))
	{
		uint32_t count = 0;
		memcpy(&count, stream, sizeof(count));
		const size_t descriptorsOffset = offsetof(MINIDUMP_MEMORY_LIST, MemoryRanges);
		if (count <= (streamSize - descriptorsOffset) / sizeof(MINIDUMP_MEMORY_DESCRIPTOR))
		{
			for (uint32_t i = 0; i < count; ++i)
			{
				MINIDUMP_MEMORY_DESCRIPTOR descriptor{};
				memcpy(
					&descriptor,
					stream + descriptorsOffset + i * sizeof(descriptor),
					sizeof(descriptor));
				if (address < descriptor.StartOfMemoryRange)
					continue;
				const uint64_t offset = address - descriptor.StartOfMemoryRange;
				if (offset > descriptor.Memory.DataSize
					|| size > descriptor.Memory.DataSize - static_cast<size_t>(offset))
				{
					continue;
				}
				const size_t dumpOffset =
					static_cast<size_t>(descriptor.Memory.Rva) + static_cast<size_t>(offset);
				if (dumpOffset > dump.size() || size > dump.size() - dumpOffset)
					return false;
				memcpy(destination, dump.data() + dumpOffset, size);
				return true;
			}
		}
	}

	if (!FindMinidumpStream(dump, Memory64ListStream, &stream, &streamSize)
		|| streamSize < offsetof(MINIDUMP_MEMORY64_LIST, MemoryRanges))
	{
		return false;
	}

	uint64_t rangeCount = 0;
	uint64_t dataOffset = 0;
	memcpy(&rangeCount, stream, sizeof(rangeCount));
	memcpy(&dataOffset, stream + sizeof(rangeCount), sizeof(dataOffset));
	const size_t descriptorsOffset = offsetof(MINIDUMP_MEMORY64_LIST, MemoryRanges);
	if (rangeCount
		> (streamSize - descriptorsOffset) / sizeof(MINIDUMP_MEMORY_DESCRIPTOR64))
	{
		return false;
	}

	for (uint64_t i = 0; i < rangeCount; ++i)
	{
		MINIDUMP_MEMORY_DESCRIPTOR64 descriptor{};
		memcpy(
			&descriptor,
			stream + descriptorsOffset + static_cast<size_t>(i) * sizeof(descriptor),
			sizeof(descriptor));
		if (address >= descriptor.StartOfMemoryRange)
		{
			const uint64_t offset = address - descriptor.StartOfMemoryRange;
			if (offset <= descriptor.DataSize && size <= descriptor.DataSize - offset)
			{
				if (dataOffset > dump.size()
					|| offset > dump.size() - static_cast<size_t>(dataOffset)
					|| size > dump.size() - static_cast<size_t>(dataOffset + offset))
				{
					return false;
				}
				memcpy(
					destination,
					dump.data() + static_cast<size_t>(dataOffset + offset),
					size);
				return true;
			}
		}
		if (descriptor.DataSize > std::numeric_limits<uint64_t>::max() - dataOffset)
			return false;
		dataOffset += descriptor.DataSize;
	}
	return false;
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

void TestBoundedMemoryRegionPlan()
{
	BoundedMemoryRegionPlan plan(0x4000);
	Check(
		plan.Add(0x1000, 0x1000, CrashMemoryRegionRegistered),
		"first bounded region is accepted");
	Check(
		plan.Add(0x1800, 0x1000, CrashMemoryRegionRegisterPointer),
		"overlapping bounded region is accepted");
	Check(
		plan.Add(0x2800, 0x800, CrashMemoryRegionStackPointer),
		"adjacent bounded region is accepted");
	Check(plan.Count() == 1, "overlapping and adjacent regions are merged");
	Check(plan.PlannedBytes() == 0x2000, "merged region counts unique bytes");
	if (plan.Count() == 1)
	{
		const CrashCaptureMemoryRegion& region = plan.Region(0);
		Check(region.base == 0x1000, "merged region base is stable");
		Check(region.size == 0x2000, "merged region size is stable");
		Check(
			region.sources
				== (CrashMemoryRegionRegistered
					| CrashMemoryRegionRegisterPointer
					| CrashMemoryRegionStackPointer),
			"merged region retains every source");
	}

	Check(
		!plan.Add(0x5000, 0x3000, CrashMemoryRegionIndirectPointer),
		"region exceeding byte budget is rejected");
	Check(
		!plan.Add(
			std::numeric_limits<uint64_t>::max() - 1,
			4,
			CrashMemoryRegionIndirectPointer),
		"overflowing region is rejected");
	Check(plan.CandidateCount() == 5, "region candidate count is complete");
	Check(plan.RejectedCount() == 2, "region rejection count is complete");

	BoundedMemoryRegionPlan capacityPlan(std::numeric_limits<uint64_t>::max());
	for (size_t i = 0; i < kMaximumAdditionalMemoryRegions; ++i)
	{
		Check(
			capacityPlan.Add(
				0x1000 + i * 0x2000,
				0x1000,
				CrashMemoryRegionRegistered),
			"region within fixed capacity is accepted");
	}
	Check(
		!capacityPlan.Add(
			0x1000 + kMaximumAdditionalMemoryRegions * 0x2000,
			0x1000,
			CrashMemoryRegionRegistered),
		"region beyond fixed capacity is rejected");
	Check(
		capacityPlan.Count() == kMaximumAdditionalMemoryRegions,
		"fixed region capacity remains bounded");
}

void TestRegisteredMemoryClaimLifetime()
{
	void* page = VirtualAlloc(
		nullptr,
		4096,
		MEM_RESERVE | MEM_COMMIT,
		PAGE_READWRITE);
	Check(page != nullptr, "claim lifetime page allocates");
	if (!page)
		return;

	Check(
		RegisterCrashMemoryRegion(
			page,
			4096,
			CrashMemoryRegionPriority::Critical),
		"claim lifetime region registers");

	CONTEXT context{};
	context.ContextFlags = CONTEXT_ALL;
	EXCEPTION_RECORD record{};
	EXCEPTION_POINTERS exceptionPointers{ &record, &context };
	BoundedMemoryRegionPlan plan;
	CrashCaptureManifest manifest{};
	detail::CrashMemoryCaptureClaims claims{};
	std::array<uint8_t, 4096> scratch{};
	detail::BuildCrashCaptureMemory(
		exceptionPointers,
		plan,
		manifest,
		claims,
		scratch.data(),
		scratch.size());

	Check(claims.count == 1, "capture claims registered storage");
	Check(
		!UnregisterCrashMemoryRegion(page),
		"claimed storage cannot unregister during capture");
	detail::ReleaseCrashCaptureMemoryClaims(claims);
	Check(claims.count == 0, "capture claim release resets ownership");
	Check(
		UnregisterCrashMemoryRegion(page),
		"registered storage unregisters after capture");
	VirtualFree(page, 0, MEM_RELEASE);
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

void TestRegisterMemoryPreviews()
{
	const uint8_t printable[]{ 'H', 'e', 'l', 'l', 'o', '/', 'R', '1' };
	Check(
		FormatRegisterMemoryPreview(printable, sizeof(printable))
			== "[hex: 48 65 6C 6C 6F 2F 52 31; ascii: \"Hello/R1\"]",
		"printable register preview includes exact hex and ASCII");

	const uint8_t embeddedNull[]{ 'A', 0x00, 'B', 0x00 };
	Check(
		FormatRegisterMemoryPreview(embeddedNull, sizeof(embeddedNull))
			== "[hex: 41 00 42 00; ascii: \"A.B.\"]",
		"embedded NUL bytes remain visible in register preview hex");

	const uint8_t highBit[]{ 0x80, 0xC0, 0xFF };
	Check(
		FormatRegisterMemoryPreview(highBit, sizeof(highBit))
			== "[hex: 80 C0 FF; ascii: \"...\"]",
		"high-bit register preview bytes remain exact");

	const uint8_t controls[]{ 0x09, 0x0A, 0x1F, 0x20, 0x7E, 0x7F };
	Check(
		FormatRegisterMemoryPreview(controls, sizeof(controls))
			== "[hex: 09 0A 1F 20 7E 7F; ascii: \"... ~.\"]",
		"control bytes are exact in hex and sanitized in ASCII");

	const uint8_t reportedBytes[]{ 0x58, 0xC0, 0x35, 0x2F, 0x7F, 0x00, 0x00, 0x00 };
	Check(
		FormatRegisterMemoryPreview(reportedBytes, sizeof(reportedBytes))
			== "[hex: 58 C0 35 2F 7F 00 00 00; ascii: \"X.5/....\"]",
		"reported register bytes render losslessly");

	uint8_t oversized[kRegisterMemoryPreviewBytes + 1]{};
	for (size_t i = 0; i < sizeof(oversized); ++i)
		oversized[i] = static_cast<uint8_t>(i);
	Check(
		FormatRegisterMemoryPreview(oversized, sizeof(oversized))
			== "[hex: 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F; "
				"ascii: \"................\"; truncated]",
		"register preview is capped and marks truncation");

	Check(
		FormatRegisterMemoryPreview(nullptr, sizeof(reportedBytes)).empty(),
		"null preview buffer is rejected");
	Check(
		ReadRegisterMemoryPreview(nullptr).empty(),
		"null register address is not dereferenced");

	void* unreadable = VirtualAlloc(
		nullptr,
		4096,
		MEM_RESERVE | MEM_COMMIT,
		PAGE_NOACCESS);
	Check(unreadable != nullptr, "unreadable preview test page allocates");
	if (unreadable)
	{
		Check(
			ReadRegisterMemoryPreview(unreadable).empty(),
			"unreadable register address produces no preview");
		VirtualFree(unreadable, 0, MEM_RELEASE);
	}

	uint8_t* readable = static_cast<uint8_t*>(VirtualAlloc(
		nullptr,
		4096,
		MEM_RESERVE | MEM_COMMIT,
		PAGE_READWRITE));
	Check(readable != nullptr, "readable preview test page allocates");
	if (readable)
	{
		memset(readable, 0xAA, kRegisterMemoryPreviewBytes);
		memcpy(readable, reportedBytes, sizeof(reportedBytes));
		Check(
			ReadRegisterMemoryPreview(readable)
				== "[hex: 58 C0 35 2F 7F 00 00 00 AA AA AA AA AA AA AA AA; "
					"ascii: \"X.5/............\"; truncated]",
			"safe register memory read preserves the bounded sample");
		VirtualFree(readable, 0, MEM_RELEASE);
	}
}

void TestCurrentProcessMinidumpRoundTrip()
{
	constexpr uint8_t sentinel[] = {
		0x52, 0x31, 0x44, 0x2D, 0x42, 0x4F, 0x55, 0x4E,
		0x44, 0x45, 0x44, 0x2D, 0x44, 0x55, 0x4D, 0x50,
	};
	constexpr size_t sentinelOffset = 0x180;
	uint8_t* capturedRegion = static_cast<uint8_t*>(VirtualAlloc(
		nullptr,
		4096,
		MEM_RESERVE | MEM_COMMIT,
		PAGE_READWRITE));
	Check(capturedRegion != nullptr, "registered capture page allocates");
	if (capturedRegion)
	{
		memset(capturedRegion, 0xA5, 4096);
		memcpy(capturedRegion + sentinelOffset, sentinel, sizeof(sentinel));
		Check(
			RegisterCrashMemoryRegion(
				capturedRegion,
				4096,
				CrashMemoryRegionPriority::Critical),
			"generic crash memory region registers");
	}
	WriteCrashBreadcrumb(0x43415054, 0x4D454D31, 0x1122334455667788, 0x8877665544332211);
	CaptureResult capture;
	capture.reportPath = MakeReportPath();
	Check(!capture.reportPath.empty(), "temporary report path is available");
	Check(!TemporaryDumpExists(capture.reportPath), "no temporary dump exists before capture");
	CaptureCurrentProcessDump(capture);
	if (capturedRegion)
		Check(UnregisterCrashMemoryRegion(capturedRegion), "generic crash memory region unregisters");

	Check(capture.available, "current-process minidump capture succeeds");
	Check(!capture.rawDump.empty(), "current-process minidump is nonempty");
	Check(capture.rawDump.size() >= 4
		&& memcmp(capture.rawDump.data(), "MDMP", 4) == 0,
		"captured bytes have the MDMP signature");
	Check(HasMinidumpStream(capture.rawDump, MemoryInfoListStream),
		"captured dump includes full memory information");
	Check(HasMinidumpStream(capture.rawDump, ThreadInfoListStream),
		"captured dump includes extended thread information");
	Check(HasMinidumpStream(capture.rawDump, kCaptureManifestStreamType),
		"captured dump includes bounded capture manifest");
	Check(!TemporaryDumpExists(capture.reportPath), "temporary dump is deleted after capture");
	Check(capture.section.find("\nStatus: available\n") != std::string::npos,
		"section reports available status");
	const std::string expectedFlags =
		std::string("\nAdditional-Flags: ") + kAdditionalFlagsDescription + "\n";
	Check(capture.section.find(expectedFlags) != std::string::npos,
		"section reports every requested metadata flag");
	Check(capture.section.find("\nCompression-Level: 19\n") != std::string::npos,
		"section reports compression level 19");
	Check(capture.section.find("\nContent-Checksum: enabled\n") != std::string::npos,
		"section reports content checksum");

	const uint8_t* manifestData = nullptr;
	size_t manifestSize = 0;
	CrashCaptureManifest manifest{};
	const bool hasManifest = FindMinidumpStream(
		capture.rawDump,
		kCaptureManifestStreamType,
		&manifestData,
		&manifestSize);
	Check(hasManifest, "bounded capture manifest is readable");
	Check(manifestSize == sizeof(manifest), "bounded capture manifest size matches");
	if (hasManifest && manifestSize == sizeof(manifest))
	{
		memcpy(&manifest, manifestData, sizeof(manifest));
		Check(manifest.header.magic == kCaptureManifestMagic, "capture manifest magic matches");
		Check(manifest.header.version == kCaptureManifestVersion, "capture manifest version matches");
		Check(manifest.header.manifestSize == sizeof(manifest), "capture manifest self-size matches");
		Check(
			manifest.header.memoryRegionCount <= kMaximumAdditionalMemoryRegions,
			"capture manifest region count is bounded");
		Check(
			manifest.header.plannedMemoryBytes <= kAdditionalMemoryBudget,
			"capture manifest byte budget is enforced");
		Check(
			manifest.header.candidateRegionCount >= manifest.header.memoryRegionCount,
			"capture manifest candidate accounting is complete");

		bool registeredRegionRecorded = false;
		for (uint32_t i = 0; i < manifest.header.memoryRegionCount; ++i)
		{
			const CrashCaptureMemoryRegion& region = manifest.memoryRegions[i];
			if (capturedRegion
				&& reinterpret_cast<uintptr_t>(capturedRegion) >= region.base
				&& reinterpret_cast<uintptr_t>(capturedRegion) < region.base + region.size
				&& (region.sources & CrashMemoryRegionRegistered) != 0)
			{
				registeredRegionRecorded = true;
			}
		}
		Check(registeredRegionRecorded, "registered memory appears in capture manifest");

		bool breadcrumbRecorded = false;
		for (uint32_t i = 0; i < manifest.header.breadcrumbCount; ++i)
		{
			const CrashBreadcrumb& breadcrumb = manifest.breadcrumbs[i];
			if (breadcrumb.category == 0x43415054
				&& breadcrumb.event == 0x4D454D31
				&& breadcrumb.value1 == 0x1122334455667788
				&& breadcrumb.value2 == 0x8877665544332211)
			{
				breadcrumbRecorded = true;
			}
		}
		Check(breadcrumbRecorded, "generic breadcrumb appears in capture manifest");
	}

	if (capturedRegion)
	{
		std::array<uint8_t, sizeof(sentinel)> capturedSentinel{};
		Check(
			ReadCapturedMemory(
				capture.rawDump,
				reinterpret_cast<uintptr_t>(capturedRegion + sentinelOffset),
				capturedSentinel.data(),
				capturedSentinel.size()),
			"registered memory bytes are present in minidump");
		Check(
			memcmp(capturedSentinel.data(), sentinel, sizeof(sentinel)) == 0,
			"registered memory bytes remain exact");
	}

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
	if (capturedRegion)
		VirtualFree(capturedRegion, 0, MEM_RELEASE);
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
	const std::string expectedFlags =
		std::string("\nAdditional-Flags: ") + kAdditionalFlagsDescription + "\n";
	Check(section.find(expectedFlags) != std::string::npos,
		"failure section reports every requested metadata flag");
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
	TestRegisteredMemoryClaimLifetime();
	TestBoundedMemoryRegionPlan();
	TestAscii85Vectors();
	TestRegisterMemoryPreviews();
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
