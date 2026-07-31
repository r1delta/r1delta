#include "r1o_vpk.h"
#include "r1o_vpk_index_cache.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

bool AreR1OFakeDediVerboseLogsEnabled();

namespace {

constexpr uint32_t kVPKHeaderMagic = 0x55AA1234;
constexpr uint16_t kVPKChunkTerminator = 0xFFFF;
constexpr uint16_t kR1DDeltaPackIndex = 0x1337;
constexpr uint64_t kR1DZstdMarker = 0x5244315F5F4D4150ULL;

using VPKChunk = R1OVPKChunk;
using VPKEntry = R1OVPKEntry;
using VPKParsedEntry = std::pair<std::string, VPKEntry>;

struct VPKMemoryFile {
	std::shared_ptr<const std::vector<uint8_t>> data;
	uint64_t position{};
};

struct ZstdApi {
	using DecompressFn = size_t(__cdecl*)(void*, size_t, const void*, size_t);
	using IsErrorFn = unsigned int(__cdecl*)(size_t);
	using GetErrorNameFn = const char*(__cdecl*)(size_t);

	HMODULE module{};
	DecompressFn decompress{};
	IsErrorFn isError{};
	GetErrorNameFn getErrorName{};
};

std::once_flag g_indexOnce;
std::once_flag g_zstdOnce;
std::unordered_map<std::string, VPKEntry> g_entries;
ZstdApi g_zstd;
std::mutex g_contentMutex;
std::unordered_map<std::string, std::weak_ptr<const std::vector<uint8_t>>> g_content;
std::mutex g_openFilesMutex;
std::unordered_map<void*, std::unique_ptr<VPKMemoryFile>> g_openFiles;
static bool ReadExact(std::ifstream& stream, void* out, size_t size)
{
	stream.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(size));
	return stream.good();
}

template <typename T>
static bool ReadValue(std::ifstream& stream, T& out)
{
	return ReadExact(stream, &out, sizeof(out));
}

template <typename T>
static bool ReadMemoryValue(const uint8_t*& cursor, const uint8_t* end, T& out)
{
	if (static_cast<size_t>(end - cursor) < sizeof(out))
		return false;
	memcpy(&out, cursor, sizeof(out));
	cursor += sizeof(out);
	return true;
}

static bool ReadNullString(const uint8_t*& cursor, const uint8_t* end, std::string& out)
{
	const void* terminator = memchr(cursor, '\0', static_cast<size_t>(end - cursor));
	if (!terminator)
		return false;

	const auto* stringEnd = static_cast<const uint8_t*>(terminator);
	out.assign(reinterpret_cast<const char*>(cursor), reinterpret_cast<const char*>(stringEnd));
	cursor = stringEnd + 1;
	return true;
}

static std::string NormalizeResourcePath(const char* path)
{
	std::string normalized = path ? path : "";
	std::replace(normalized.begin(), normalized.end(), '\\', '/');

	while (!normalized.empty() && normalized.front() == '/')
		normalized.erase(normalized.begin());

	std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return normalized;
}

static std::string GetExecutableDirectory()
{
	char buffer[MAX_PATH];
	DWORD len = GetModuleFileNameA(nullptr, buffer, sizeof(buffer));
	if (!len || len >= sizeof(buffer))
		return ".";

	char* slash = strrchr(buffer, '\\');
	if (slash)
		*slash = '\0';
	return buffer;
}

static std::string GetInstallDirectory()
{
	char buffer[MAX_PATH];
	DWORD len = GetCurrentDirectoryA(sizeof(buffer), buffer);
	if (!len || len >= sizeof(buffer))
		return GetExecutableDirectory();
	return buffer;
}

static std::string GetVPKDirectory()
{
	return GetInstallDirectory() + "\\vpk";
}

static bool StartsWithI(const char* value, const char* prefix)
{
	if (!value || !prefix)
		return false;
	return _strnicmp(value, prefix, strlen(prefix)) == 0;
}

static bool EndsWithI(const std::string& value, const char* suffix)
{
	if (!suffix)
		return false;

	const size_t suffixLength = strlen(suffix);
	return value.size() >= suffixLength
		&& _stricmp(value.c_str() + value.size() - suffixLength, suffix) == 0;
}

static bool IsServerVPKName(const std::string& name)
{
	std::string lowerName(name);
	std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return lowerName.find("server_") != std::string::npos;
}

static bool BuildPackPath(const std::string& dirPath, uint16_t packIndex, std::string& outPath)
{
	if (packIndex == kR1DDeltaPackIndex) {
		const char* basename = strrchr(dirPath.c_str(), '\\');
		basename = basename ? basename + 1 : dirPath.c_str();
		const bool server = strstr(basename, "server_") != nullptr;
		outPath = GetVPKDirectory() + (server
			? "\\server_mp_delta_common.bsp.pak000_000.vpk"
			: "\\client_mp_delta_common.bsp.pak000_000.vpk");
		return true;
	}

	const size_t dirMarker = dirPath.rfind("_dir.vpk");
	if (dirMarker == std::string::npos)
		return false;

	char suffix[32];
	_snprintf_s(suffix, sizeof(suffix), _TRUNCATE, "_%03u.vpk", static_cast<unsigned int>(packIndex));
	outPath = dirPath.substr(0, dirMarker) + suffix;
	return true;
}

static void AddEntry(const std::string& key, VPKEntry&& entry)
{
	if (key.empty())
		return;

	// Earlier archives win so the original language/common-file precedence remains stable.
	if (g_entries.find(key) == g_entries.end())
		g_entries.emplace(key, std::move(entry));
}

static bool ParseDirectoryVPK(
	const std::filesystem::path& path,
	std::vector<VPKParsedEntry>& parsedEntries,
	bool excludeIndexedEntries)
{
	std::ifstream stream(path, std::ios::binary);
	if (!stream)
		return false;

	uint32_t marker{};
	uint16_t major{};
	uint16_t minor{};
	uint32_t treeSize{};
	uint32_t embeddedChunkSize{};
	if (!ReadValue(stream, marker) || !ReadValue(stream, major) || !ReadValue(stream, minor)
		|| !ReadValue(stream, treeSize) || !ReadValue(stream, embeddedChunkSize)) {
		return false;
	}

	if (marker != kVPKHeaderMagic || treeSize > static_cast<uint32_t>(std::numeric_limits<int>::max()))
		return false;
	if (!treeSize)
		return true;

	std::vector<uint8_t> tree(treeSize);
	if (treeSize && !ReadExact(stream, tree.data(), tree.size()))
		return false;

	const std::string dirPath = path.string();
	const uint8_t* cursor = tree.data();
	const uint8_t* treeEnd = cursor + tree.size();
	std::string extension;
	std::string directory;
	std::string filename;

	for (;;) {
		if (!ReadNullString(cursor, treeEnd, extension))
			return false;
		if (extension.empty()) {
			while (cursor < treeEnd && *cursor == 0)
				++cursor;
			return cursor == treeEnd;
		}

		for (;;) {
			if (!ReadNullString(cursor, treeEnd, directory))
				return false;
			if (directory.empty())
				break;

			for (;;) {
				if (!ReadNullString(cursor, treeEnd, filename))
					return false;
				if (filename.empty())
					break;

				std::string key;
				if (directory == " ")
					key = filename + "." + extension;
				else
					key = directory + "/" + filename + "." + extension;
				key = NormalizeResourcePath(key.c_str());
				const bool keepEntry = !excludeIndexedEntries || g_entries.find(key) == g_entries.end();

				uint32_t crc{};
				uint16_t preloadSize{};
				uint16_t packIndex{};
				if (!ReadMemoryValue(cursor, treeEnd, crc)
					|| !ReadMemoryValue(cursor, treeEnd, preloadSize)
					|| !ReadMemoryValue(cursor, treeEnd, packIndex))
					return false;
				if (static_cast<size_t>(treeEnd - cursor) < preloadSize)
					return false;

				VPKEntry entry;
				if (keepEntry) {
					entry.dirPath = dirPath;
					entry.packIndex = packIndex;
					entry.preload.assign(cursor, cursor + preloadSize);
				}
				cursor += preloadSize;

				for (;;) {
					VPKChunk chunk;
					uint16_t terminator{};
					if (!ReadMemoryValue(cursor, treeEnd, chunk.flags)
						|| !ReadMemoryValue(cursor, treeEnd, chunk.textureFlags)
						|| !ReadMemoryValue(cursor, treeEnd, chunk.offset)
						|| !ReadMemoryValue(cursor, treeEnd, chunk.compressedSize)
						|| !ReadMemoryValue(cursor, treeEnd, chunk.decompressedSize)
						|| !ReadMemoryValue(cursor, treeEnd, terminator)) {
						return false;
					}

					if (keepEntry)
						entry.chunks.push_back(chunk);
					if (terminator == kVPKChunkTerminator)
						break;
				}

				if (keepEntry)
					parsedEntries.emplace_back(std::move(key), std::move(entry));
			}
		}
	}
}

static void ParseDirectoryBatch(
	const std::vector<std::filesystem::path>& dirs,
	size_t begin,
	size_t end)
{
	if (begin >= end)
		return;

	std::vector<std::vector<VPKParsedEntry>> parsed(end - begin);
	std::vector<uint8_t> parseSucceeded(end - begin);
	std::atomic_size_t next{};
	const size_t archiveCount = end - begin;
	const unsigned int availableWorkers = std::max(1u, std::thread::hardware_concurrency());
	const size_t workerCount = std::min<size_t>(archiveCount, std::min<size_t>(availableWorkers, 16));
	const bool excludeIndexedEntries = !g_entries.empty();

	auto worker = [&]() {
		for (;;) {
			const size_t relativeIndex = next.fetch_add(1, std::memory_order_relaxed);
			if (relativeIndex >= archiveCount)
				return;
			try {
				parseSucceeded[relativeIndex] = ParseDirectoryVPK(
					dirs[begin + relativeIndex],
					parsed[relativeIndex],
					excludeIndexedEntries) ? 1 : 0;
			}
			catch (...) {
				parseSucceeded[relativeIndex] = 0;
			}
			if (!parseSucceeded[relativeIndex])
				parsed[relativeIndex].clear();
		}
	};

	std::vector<std::thread> workers;
	workers.reserve(workerCount > 0 ? workerCount - 1 : 0);
	for (size_t i = 1; i < workerCount; ++i) {
		try {
			workers.emplace_back(worker);
		}
		catch (const std::system_error&) {
			break;
		}
	}
	worker();
	for (std::thread& thread : workers)
		thread.join();

	for (size_t relativeIndex = 0; relativeIndex < parsed.size(); ++relativeIndex) {
		if (!parseSucceeded[relativeIndex]) {
			char buffer[512];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: rejected malformed R1O VPK directory tree path=%s\n",
				dirs[begin + relativeIndex].string().c_str());
			OutputDebugStringA(buffer);
			continue;
		}
		for (auto& parsedEntry : parsed[relativeIndex])
			AddEntry(parsedEntry.first, std::move(parsedEntry.second));
	}
}

static void BuildIndex()
{
	const ULONGLONG buildStartedAt = GetTickCount64();
	const std::filesystem::path vpkDir(GetVPKDirectory());
	std::error_code error;
	if (!std::filesystem::exists(vpkDir, error) || error)
		return;

	std::vector<std::filesystem::path> dirs;
	uint64_t indexedBytes = 0;
	std::filesystem::directory_iterator item(vpkDir, error);
	const std::filesystem::directory_iterator end;
	for (; !error && item != end; item.increment(error)) {
		std::error_code typeError;
		if (!item->is_regular_file(typeError) || typeError)
			continue;

		const std::string name = item->path().filename().string();
		if (!EndsWithI(name, "pak000_dir.vpk"))
			continue;

		dirs.push_back(item->path());
	}
	if (error)
		return;

	const bool hasServerDirectories = std::any_of(dirs.begin(), dirs.end(), [](const auto& path) {
		return IsServerVPKName(path.filename().string());
	});
	if (hasServerDirectories) {
		dirs.erase(
			std::remove_if(dirs.begin(), dirs.end(), [](const auto& path) {
				return !IsServerVPKName(path.filename().string());
			}),
			dirs.end());
	}

	std::sort(dirs.begin(), dirs.end(), [](const auto& left, const auto& right) {
		const std::string l = left.filename().string();
		const std::string r = right.filename().string();
		const bool lEnglish = StartsWithI(l.c_str(), "english");
		const bool rEnglish = StartsWithI(r.c_str(), "english");
		if (lEnglish != rEnglish)
			return lEnglish;

		// Fake dedicated installs may retain the client directories beside the
		// server directories. Prefer the server set wholesale so an overlapping
		// client-common entry cannot shadow a server map or server-common entry.
		const bool lServer = IsServerVPKName(l);
		const bool rServer = IsServerVPKName(r);
		if (lServer != rServer)
			return lServer;

		const bool lCommon = l.find("_common.") != std::string::npos;
		const bool rCommon = r.find("_common.") != std::string::npos;
		if (lCommon != rCommon)
			return lCommon;

		return _stricmp(l.c_str(), r.c_str()) < 0;
	});

	std::vector<R1OVPKArchiveStamp> archives;
	std::vector<std::filesystem::path> availableDirs;
	archives.reserve(dirs.size());
	availableDirs.reserve(dirs.size());
	for (const auto& dir : dirs) {
		R1OVPKArchiveStamp archive;
		if (!R1OVPK_GetArchiveStamp(dir, archive))
			continue;
		indexedBytes += archive.size;
		availableDirs.push_back(dir);
		archives.push_back(std::move(archive));
	}
	dirs = std::move(availableDirs);

	const std::string rootKey = R1OVPK_GetIndexRootKey(vpkDir);
	const std::filesystem::path cachePath = R1OVPK_GetIndexCachePath(rootKey);
	const bool cacheHit = R1OVPK_LoadIndexCache(cachePath, rootKey, archives, g_entries);
	bool cacheStored = false;
	if (!cacheHit) {
		g_entries.reserve(65536);
		constexpr size_t kArchiveBatchSize = 32;
		for (size_t begin = 0; begin < dirs.size(); begin += kArchiveBatchSize)
			ParseDirectoryBatch(dirs, begin, std::min(dirs.size(), begin + kArchiveBatchSize));
		cacheStored = R1OVPK_StoreIndexCache(cachePath, rootKey, archives, g_entries);
	}

	printf(
		"[R1O dedicated] VPK ready: %zu archives, %zu files, %llu bytes indexed in %llums, source=%s\n",
		dirs.size(),
		g_entries.size(),
		static_cast<unsigned long long>(indexedBytes),
		static_cast<unsigned long long>(GetTickCount64() - buildStartedAt),
		cacheHit ? "cache" : "scan");
	fflush(stdout);

	if (AreR1OFakeDediVerboseLogsEnabled()) {
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O VPK index ready dir=%s archives=%zu bytes=%llu entries=%zu source=%s cache=%s stored=%d\n",
			vpkDir.string().c_str(),
			dirs.size(),
			static_cast<unsigned long long>(indexedBytes),
			g_entries.size(),
			cacheHit ? "cache" : "scan",
			cachePath.string().c_str(),
			static_cast<int>(cacheStored));
		OutputDebugStringA(buffer);
	}
}

static ZstdApi* GetZstd()
{
	std::call_once(g_zstdOnce, []() {
		char tier0Path[MAX_PATH];
		DWORD len = GetModuleFileNameA(GetModuleHandleA("tier0.dll"), tier0Path, sizeof(tier0Path));
		if (len && len < sizeof(tier0Path)) {
			char* slash = strrchr(tier0Path, '\\');
			if (slash) {
				*(slash + 1) = 0;
				strcat_s(tier0Path, "zstd.dll");
				g_zstd.module = LoadLibraryA(tier0Path);
			}
		}

		if (!g_zstd.module)
			g_zstd.module = LoadLibraryA("zstd.dll");

		if (g_zstd.module) {
			g_zstd.decompress = reinterpret_cast<ZstdApi::DecompressFn>(GetProcAddress(g_zstd.module, "ZSTD_decompress"));
			g_zstd.isError = reinterpret_cast<ZstdApi::IsErrorFn>(GetProcAddress(g_zstd.module, "ZSTD_isError"));
			g_zstd.getErrorName = reinterpret_cast<ZstdApi::GetErrorNameFn>(GetProcAddress(g_zstd.module, "ZSTD_getErrorName"));
		}

		if (AreR1OFakeDediVerboseLogsEnabled()) {
			char buffer[256];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O zstd load module=%p decompress=%p isError=%p gle=%lu\n",
				g_zstd.module,
				reinterpret_cast<void*>(g_zstd.decompress),
				reinterpret_cast<void*>(g_zstd.isError),
				GetLastError());
			OutputDebugStringA(buffer);
		}
	});

	return g_zstd.decompress && g_zstd.isError ? &g_zstd : nullptr;
}

static bool ReadChunkData(const VPKEntry& entry, const VPKChunk& chunk, std::vector<uint8_t>& out)
{
	std::string packPath;
	if (!BuildPackPath(entry.dirPath, entry.packIndex, packPath))
		return false;

	std::ifstream pack(packPath, std::ios::binary);
	if (!pack)
		return false;

	if (chunk.compressedSize > std::numeric_limits<uint32_t>::max()
		|| chunk.decompressedSize > std::numeric_limits<uint32_t>::max()
		|| chunk.offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
		return false;

	std::vector<uint8_t> compressed(static_cast<size_t>(chunk.compressedSize));
	pack.seekg(static_cast<std::streamoff>(chunk.offset), std::ios::beg);
	if (!ReadExact(pack, compressed.data(), compressed.size()))
		return false;

	if (chunk.compressedSize == chunk.decompressedSize) {
		out.insert(out.end(), compressed.begin(), compressed.end());
		return true;
	}

	if (compressed.size() >= sizeof(kR1DZstdMarker)) {
		uint64_t marker{};
		memcpy(&marker, compressed.data(), sizeof(marker));
		if (marker == kR1DZstdMarker) {
			ZstdApi* zstd = GetZstd();
			if (!zstd)
				return false;

			const size_t originalSize = out.size();
			out.resize(originalSize + static_cast<size_t>(chunk.decompressedSize));
			const size_t result = zstd->decompress(
				out.data() + originalSize,
				static_cast<size_t>(chunk.decompressedSize),
				compressed.data() + sizeof(kR1DZstdMarker),
				compressed.size() - sizeof(kR1DZstdMarker));
			if (zstd->isError(result)) {
				char buffer[256];
				_snprintf_s(
					buffer,
					sizeof(buffer),
					_TRUNCATE,
					"R1Delta: R1O zstd decompress failed status=%zu error=%s\n",
					result,
					zstd->getErrorName ? zstd->getErrorName(result) : "<unknown>");
				OutputDebugStringA(buffer);
				out.resize(originalSize);
				return false;
			}

			if (result != chunk.decompressedSize) {
				out.resize(originalSize);
				return false;
			}

			return true;
		}
	}

	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O VPK unsupported compressed chunk pack=%u offset=%llu compressed=%llu decompressed=%llu\n",
		static_cast<unsigned int>(entry.packIndex),
		static_cast<unsigned long long>(chunk.offset),
		static_cast<unsigned long long>(chunk.compressedSize),
		static_cast<unsigned long long>(chunk.decompressedSize));
	OutputDebugStringA(buffer);
	return false;
}

static bool EntryDecompressedSize(const VPKEntry& entry, uint64_t& size)
{
	size = entry.preload.size();
	for (const VPKChunk& chunk : entry.chunks) {
		if (chunk.decompressedSize > std::numeric_limits<uint64_t>::max() - size)
			return false;
		size += chunk.decompressedSize;
	}
	return size <= std::numeric_limits<uint32_t>::max();
}

static const VPKEntry* FindEntry(const char* relativeResourcePath, std::string* normalizedPath = nullptr)
{
	if (!relativeResourcePath || !relativeResourcePath[0])
		return nullptr;

	std::call_once(g_indexOnce, BuildIndex);
	std::string normalized = NormalizeResourcePath(relativeResourcePath);
	const auto found = g_entries.find(normalized);
	if (found == g_entries.end())
		return nullptr;

	if (normalizedPath)
		*normalizedPath = std::move(normalized);
	return &found->second;
}

static bool ReadEntry(const VPKEntry& entry, std::vector<uint8_t>& out)
{
	uint64_t size = 0;
	if (!EntryDecompressedSize(entry, size))
		return false;

	out = entry.preload;
	out.reserve(static_cast<size_t>(size));
	for (const VPKChunk& chunk : entry.chunks) {
		if (!ReadChunkData(entry, chunk, out))
			return false;
	}
	return out.size() == size;
}

static std::shared_ptr<const std::vector<uint8_t>> LoadEntry(
	const std::string& normalizedPath,
	const VPKEntry& entry)
{
	std::lock_guard<std::mutex> lock(g_contentMutex);
	const auto cached = g_content.find(normalizedPath);
	if (cached != g_content.end()) {
		if (auto content = cached->second.lock())
			return content;
	}

	std::vector<uint8_t> data;
	if (!ReadEntry(entry, data))
		return {};

	auto content = std::make_shared<const std::vector<uint8_t>>(std::move(data));
	g_content[normalizedPath] = content;
	return content;
}

static VPKMemoryFile* FindOpenFile(void* handle)
{
	const auto found = g_openFiles.find(handle);
	return found != g_openFiles.end() ? found->second.get() : nullptr;
}

} // namespace

bool R1OVPK_HasFile(const char* relativeResourcePath)
{
	return FindEntry(relativeResourcePath) != nullptr;
}

bool R1OVPK_GetFileSize(const char* relativeResourcePath, uint64_t* size)
{
	if (!size)
		return false;

	const VPKEntry* entry = FindEntry(relativeResourcePath);
	if (!entry)
		return false;

	return EntryDecompressedSize(*entry, *size);
}

void* R1OVPK_OpenFile(const char* relativeResourcePath)
{
	std::string normalizedPath;
	const VPKEntry* entry = FindEntry(relativeResourcePath, &normalizedPath);
	if (!entry)
		return nullptr;

	auto data = LoadEntry(normalizedPath, *entry);
	if (!data)
		return nullptr;

	auto file = std::make_unique<VPKMemoryFile>();
	file->data = std::move(data);
	void* handle = file.get();

	std::lock_guard<std::mutex> lock(g_openFilesMutex);
	g_openFiles.emplace(handle, std::move(file));
	return handle;
}

bool R1OVPK_ReadFile(void* handle, void* output, int bytesToRead, int* bytesRead)
{
	std::lock_guard<std::mutex> lock(g_openFilesMutex);
	VPKMemoryFile* file = FindOpenFile(handle);
	if (!file)
		return false;

	if (bytesRead)
		*bytesRead = 0;
	if (!output || bytesToRead <= 0 || file->position >= file->data->size())
		return true;

	const size_t available = file->data->size() - static_cast<size_t>(file->position);
	const size_t count = std::min(available, static_cast<size_t>(bytesToRead));
	memcpy(output, file->data->data() + file->position, count);
	file->position += count;
	if (bytesRead)
		*bytesRead = static_cast<int>(count);
	return true;
}

bool R1OVPK_CloseFile(void* handle)
{
	std::lock_guard<std::mutex> lock(g_openFilesMutex);
	return g_openFiles.erase(handle) != 0;
}

bool R1OVPK_SeekFile(void* handle, int offset, int origin)
{
	std::lock_guard<std::mutex> lock(g_openFilesMutex);
	VPKMemoryFile* file = FindOpenFile(handle);
	if (!file)
		return false;

	int64_t base = 0;
	switch (origin) {
	case 0:
		break;
	case 1:
		base = static_cast<int64_t>(file->position);
		break;
	case 2:
		base = static_cast<int64_t>(file->data->size());
		break;
	default:
		return true;
	}

	const int64_t position = base + static_cast<int64_t>(offset);
	file->position = position > 0 ? static_cast<uint64_t>(position) : 0;
	return true;
}

bool R1OVPK_TellFile(void* handle, uint64_t* position)
{
	std::lock_guard<std::mutex> lock(g_openFilesMutex);
	VPKMemoryFile* file = FindOpenFile(handle);
	if (!file)
		return false;
	if (position)
		*position = file->position;
	return true;
}

bool R1OVPK_SizeFile(void* handle, uint64_t* size)
{
	std::lock_guard<std::mutex> lock(g_openFilesMutex);
	VPKMemoryFile* file = FindOpenFile(handle);
	if (!file)
		return false;
	if (size)
		*size = file->data->size();
	return true;
}

bool R1OVPK_IsFileOk(void* handle, bool* isOk)
{
	std::lock_guard<std::mutex> lock(g_openFilesMutex);
	VPKMemoryFile* file = FindOpenFile(handle);
	if (!file)
		return false;
	if (isOk)
		*isOk = true;
	return true;
}

bool R1OVPK_IsEndOfFile(void* handle, bool* isEndOfFile)
{
	std::lock_guard<std::mutex> lock(g_openFilesMutex);
	VPKMemoryFile* file = FindOpenFile(handle);
	if (!file)
		return false;
	if (isEndOfFile)
		*isEndOfFile = file->position >= file->data->size();
	return true;
}

bool R1OVPK_ReadLine(void* handle, char* output, int maxChars, char** result)
{
	std::lock_guard<std::mutex> lock(g_openFilesMutex);
	VPKMemoryFile* file = FindOpenFile(handle);
	if (!file)
		return false;

	if (result)
		*result = nullptr;
	if (!output || maxChars <= 0)
		return true;

	output[0] = '\0';
	if (file->position >= file->data->size())
		return true;

	int count = 0;
	while (count + 1 < maxChars && file->position < file->data->size()) {
		const char c = static_cast<char>((*file->data)[static_cast<size_t>(file->position++)]);
		output[count++] = c;
		if (c == '\n')
			break;
	}
	output[count] = '\0';
	if (result)
		*result = output;
	return true;
}
