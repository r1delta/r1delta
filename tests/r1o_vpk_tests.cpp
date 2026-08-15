#include "engine/core/r1o_runtime_paths.h"
#include "engine/filesystem/r1o_vpk.h"
#include "engine/filesystem/r1o_vpk_index_cache.h"
#include "engine/filesystem/vpk_directory_repair.h"
#include "engine/filesystem/vpk_async_precache_entry.h"

#include <Windows.h>
#include <zstd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

bool AreR1OFakeDediVerboseLogsEnabled()
{
	return false;
}

namespace {

constexpr uint32_t kVPKHeaderMagic = 0x55AA1234;
constexpr uint16_t kR1DDeltaPackIndex = 0x1337;
constexpr uint64_t kR1DZstdMarker = 0x5244315F5F4D4150ULL;
constexpr uint16_t kChunkTerminator = 0xFFFF;

struct EntrySpec {
	std::string extension;
	std::string directory;
	std::string filename;
	std::vector<uint8_t> preload;
	std::vector<uint8_t> payload;
	bool compress{};
	std::optional<uint64_t> declaredDecompressedSize;
};

int failures = 0;

void Check(bool condition, const char* name)
{
	if (condition)
		return;
	++failures;
	std::cerr << "FAILED: " << name << '\n';
}

__declspec(noinline) bool FakePrecacheEntryReady(void*)
{
	return true;
}

std::array<uintptr_t, 12> fakePendingVtable{};
std::array<uintptr_t, 12> fakeCompletedVtable{};

void TestAsyncPrecacheEntryInspection()
{
	const uintptr_t executableBase = reinterpret_cast<uintptr_t>(
		GetModuleHandleW(nullptr));
	const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(
		executableBase);
	const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
		executableBase + static_cast<uintptr_t>(dos->e_lfanew));

	AsyncPrecacheModuleLayout layout{
		executableBase,
		nt->OptionalHeader.SizeOfImage,
	};

	fakePendingVtable.fill(0);
	fakePendingVtable[0x58 / sizeof(uintptr_t)] =
		reinterpret_cast<uintptr_t>(&FakePrecacheEntryReady);
	uintptr_t entry = reinterpret_cast<uintptr_t>(fakePendingVtable.data());
	AsyncPrecacheEntryInspection inspection =
		InspectAsyncPrecacheEntry(&entry, layout);
	Check(
		inspection.kind == AsyncPrecacheEntryKind::Pending,
		"filesystem executable readiness method accepted");
	Check(
		inspection.readinessTarget
			== reinterpret_cast<uintptr_t>(&FakePrecacheEntryReady),
		"readiness method target captured");

	fakeCompletedVtable.fill(0);
	entry = reinterpret_cast<uintptr_t>(fakeCompletedVtable.data());
	inspection = InspectAsyncPrecacheEntry(&entry, layout);
	Check(
		inspection.kind == AsyncPrecacheEntryKind::CompletedValue,
		"generic completed handler value bypasses readiness dispatch");

	std::array<uintptr_t, 12> foreignVtable{};
	foreignVtable[0x58 / sizeof(uintptr_t)] =
		reinterpret_cast<uintptr_t>(&FakePrecacheEntryReady);
	entry = reinterpret_cast<uintptr_t>(foreignVtable.data());
	inspection = InspectAsyncPrecacheEntry(&entry, layout);
	Check(
		inspection.kind == AsyncPrecacheEntryKind::Invalid,
		"foreign executable readiness method rejected");

	fakePendingVtable[0x58 / sizeof(uintptr_t)] =
		reinterpret_cast<uintptr_t>(fakePendingVtable.data());
	entry = reinterpret_cast<uintptr_t>(fakePendingVtable.data());
	inspection = InspectAsyncPrecacheEntry(&entry, layout);
	Check(
		inspection.kind == AsyncPrecacheEntryKind::CompletedValue,
		"valid nonpending handler value routes to slot consumer");

	Check(
		InspectAsyncPrecacheEntry(nullptr, layout).kind
			== AsyncPrecacheEntryKind::Invalid,
		"null entry rejected");

	void* const unreadable = VirtualAlloc(
		nullptr,
		0x1000,
		MEM_RESERVE | MEM_COMMIT,
		PAGE_NOACCESS);
	Check(unreadable != nullptr, "allocate unreadable entry fixture");
	if (unreadable) {
		Check(
			InspectAsyncPrecacheEntry(unreadable, layout).kind
				== AsyncPrecacheEntryKind::Invalid,
			"unreadable entry rejected");
		VirtualFree(unreadable, 0, MEM_RELEASE);
	}
}

void TestAsyncPrecacheEntryClaimsSerializeWorkers()
{
	constexpr int workerCount = 8;
	constexpr int iterationCount = 200;
	uintptr_t entrySlot{};
	std::atomic<int> readyWorkers{};
	std::atomic<bool> startWorkers{};
	std::atomic<int> activeWorkers{};
	std::atomic<int> maximumActiveWorkers{};
	std::vector<std::thread> workers;
	workers.reserve(workerCount);

	for (int worker = 0; worker < workerCount; ++worker) {
		workers.emplace_back([&]() {
			readyWorkers.fetch_add(1, std::memory_order_release);
			while (!startWorkers.load(std::memory_order_acquire))
				std::this_thread::yield();

			for (int iteration = 0; iteration < iterationCount; ++iteration) {
				AsyncPrecacheEntryClaim claim(&entrySlot);
				const int active =
					activeWorkers.fetch_add(1, std::memory_order_acq_rel) + 1;
				int maximum = maximumActiveWorkers.load(
					std::memory_order_relaxed);
				while (active > maximum
					&& !maximumActiveWorkers.compare_exchange_weak(
						maximum,
						active,
						std::memory_order_relaxed)) {
				}
				std::this_thread::yield();
				activeWorkers.fetch_sub(1, std::memory_order_acq_rel);
				claim.Release();
				claim.Release();
			}
		});
	}

	while (readyWorkers.load(std::memory_order_acquire) != workerCount)
		std::this_thread::yield();
	startWorkers.store(true, std::memory_order_release);
	for (std::thread& worker : workers)
		worker.join();


	{
		AsyncPrecacheEntryClaim outerClaim(&entrySlot);
		Check(outerClaim.Acquired(), "outer async entry claim acquired");
		AsyncPrecacheEntryClaim nestedClaim(&entrySlot);
		Check(
			!nestedClaim.Acquired(),
			"same-thread same-slot nested claim is deferred");
	}

	AsyncPrecacheEntryClaim missingSlotClaim(nullptr);
	Check(
		missingSlotClaim.Acquired(),
		"stack-local missing entry needs no shared claim");
	missingSlotClaim.Release();
	Check(
		maximumActiveWorkers.load(std::memory_order_relaxed) == 1,
		"same-slot async workers are serialized");
}

template <typename T>
void Append(std::vector<uint8_t>& output, const T& value)
{
	const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
	output.insert(output.end(), bytes, bytes + sizeof(value));
}

void AppendString(std::vector<uint8_t>& output, const std::string& value)
{
	output.insert(output.end(), value.begin(), value.end());
	output.push_back(0);
}

std::vector<uint8_t> EncodePayload(const EntrySpec& entry)
{
	if (!entry.compress)
		return entry.payload;

	std::vector<uint8_t> encoded(sizeof(kR1DZstdMarker) + ZSTD_compressBound(entry.payload.size()));
	memcpy(encoded.data(), &kR1DZstdMarker, sizeof(kR1DZstdMarker));
	const size_t result = ZSTD_compress(
		encoded.data() + sizeof(kR1DZstdMarker),
		encoded.size() - sizeof(kR1DZstdMarker),
		entry.payload.data(),
		entry.payload.size(),
		1);
	if (ZSTD_isError(result))
		throw std::runtime_error(ZSTD_getErrorName(result));
	encoded.resize(sizeof(kR1DZstdMarker) + result);
	return encoded;
}

void WriteBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	if (!stream)
		throw std::runtime_error("failed to create test file");
	stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!stream)
		throw std::runtime_error("failed to write test file");
}

void WriteArchive(
	const std::filesystem::path& path,
	const std::vector<EntrySpec>& entries,
	std::vector<uint8_t>& pack,
	const std::vector<uint8_t>& fixedTreePrefix = {})
{
	std::vector<uint8_t> tree = fixedTreePrefix;
	for (const EntrySpec& entry : entries) {
		if (entry.preload.size() > std::numeric_limits<uint16_t>::max())
			throw std::runtime_error("test preload is too large");

		AppendString(tree, entry.extension);
		AppendString(tree, entry.directory);
		AppendString(tree, entry.filename);

		Append(tree, uint32_t{});
		Append(tree, static_cast<uint16_t>(entry.preload.size()));
		Append(tree, kR1DDeltaPackIndex);

		const std::vector<uint8_t> encoded = EncodePayload(entry);
		const uint64_t offset = pack.size();
		pack.insert(pack.end(), encoded.begin(), encoded.end());

		Append(tree, uint32_t{});
		Append(tree, uint16_t{});
		Append(tree, offset);
		Append(tree, static_cast<uint64_t>(encoded.size()));
		Append(
			tree,
			entry.declaredDecompressedSize.value_or(
				static_cast<uint64_t>(entry.payload.size())));
		Append(tree, kChunkTerminator);
		tree.insert(tree.end(), entry.preload.begin(), entry.preload.end());

		AppendString(tree, "");
		AppendString(tree, "");
	}
	AppendString(tree, "");

	std::vector<uint8_t> archive;
	Append(archive, kVPKHeaderMagic);
	Append(archive, uint16_t{2});
	Append(archive, uint16_t{3});
	Append(archive, static_cast<uint32_t>(tree.size()));
	Append(archive, uint32_t{});
	archive.insert(archive.end(), tree.begin(), tree.end());
	WriteBytes(path, archive);
}

std::vector<uint8_t> ReadAll(void* handle)
{
	uint64_t size = 0;
	if (!R1OVPK_SizeFile(handle, &size))
		return {};
	std::vector<uint8_t> output(static_cast<size_t>(size));
	int bytesRead = 0;
	if (!R1OVPK_ReadFile(handle, output.data(), static_cast<int>(output.size()), &bytesRead))
		return {};
	output.resize(static_cast<size_t>(bytesRead));
	return output;
}

void TestLookupAndPrecedence()
{
	Check(R1OVPK_HasFile("SCRIPTS\\UNIT\\DUPLICATE.TXT"), "case and slash normalization");
	Check(!R1OVPK_HasFile("scripts/unit/missing.txt"), "missing entry");
	Check(!R1OVPK_HasFile("malformed/entry.txt"), "malformed archive rejected");
	Check(R1OVPK_HasFile("scripts/unit/badsize.txt"), "bad-size entry remains indexed");
	Check(R1OVPK_OpenFile("scripts/unit/badsize.txt") == nullptr, "decoded-size mismatch fails closed");

	void* handle = R1OVPK_OpenFile("scripts/unit/duplicate.txt");
	Check(handle != nullptr, "open duplicate entry");
	if (handle) {
		const std::vector<uint8_t> data = ReadAll(handle);
		Check(std::string(data.begin(), data.end()) == "server", "English server archive precedence");
		Check(R1OVPK_CloseFile(handle), "close duplicate entry");
	}
}

void TestConcurrentCompressedFirstOpen()
{
	const std::string expected = "pre-line1\nline2\n";
	std::atomic_bool ok{true};
	std::atomic_int openFailures{};
	std::atomic_int dataFailures{};
	std::atomic_int closeFailures{};
	std::vector<std::thread> workers;
	for (int i = 0; i < 12; ++i) {
		workers.emplace_back([&]() {
			void* handle = R1OVPK_OpenFile("scripts/unit/hello.txt");
			if (!handle) {
				openFailures.fetch_add(1, std::memory_order_relaxed);
				ok.store(false, std::memory_order_relaxed);
				return;
			}
			const std::vector<uint8_t> data = ReadAll(handle);
			if (std::string(data.begin(), data.end()) != expected) {
				dataFailures.fetch_add(1, std::memory_order_relaxed);
				ok.store(false, std::memory_order_relaxed);
			}
			if (!R1OVPK_CloseFile(handle)) {
				closeFailures.fetch_add(1, std::memory_order_relaxed);
				ok.store(false, std::memory_order_relaxed);
			}
		});
	}
	for (std::thread& worker : workers)
		worker.join();
	if (!ok.load(std::memory_order_relaxed)) {
		std::cerr
			<< "Concurrent first-open details: open=" << openFailures.load()
			<< " data=" << dataFailures.load()
			<< " close=" << closeFailures.load() << '\n';
	}
	Check(ok.load(std::memory_order_relaxed), "concurrent first compressed opens");
}

void TestCompressedAndPreloadEntry()
{
	const std::string expected = "pre-line1\nline2\n";
	uint64_t size = 0;
	Check(R1OVPK_GetFileSize("scripts/unit/hello.txt", &size), "compressed entry size lookup");
	Check(size == expected.size(), "compressed entry decompressed size");

	void* handle = R1OVPK_OpenFile("scripts/unit/hello.txt");
	Check(handle != nullptr, "open compressed entry");
	if (!handle)
		return;

	char line[32]{};
	char* result = nullptr;
	Check(R1OVPK_ReadLine(handle, line, sizeof(line), &result), "read first line handled");
	Check(result == line && std::string(line) == "pre-line1\n", "preload and compressed chunk concatenate");
	Check(R1OVPK_ReadLine(handle, line, sizeof(line), &result), "read second line handled");
	Check(result == line && std::string(line) == "line2\n", "read second line");
	Check(R1OVPK_ReadLine(handle, line, sizeof(line), &result), "read eof line handled");
	Check(result == nullptr, "read eof line result");

	bool eof = false;
	Check(R1OVPK_IsEndOfFile(handle, &eof) && eof, "end of file state");
	Check(R1OVPK_CloseFile(handle), "close compressed entry");
}

void TestFixedMultiChunkEntry()
{
	void* handle = R1OVPK_OpenFile("scripts/unit/multichunk.txt");
	Check(handle != nullptr, "open fixed-byte multi-chunk entry");
	if (!handle)
		return;

	const std::vector<uint8_t> data = ReadAll(handle);
	Check(
		std::string(data.begin(), data.end()) == "left-right",
		"entry-level pack index applies to every chunk");
	Check(R1OVPK_CloseFile(handle), "close fixed-byte multi-chunk entry");
}

void TestSeekTellAndRead()
{
	void* handle = R1OVPK_OpenFile("scripts/unit/hello.txt");
	Check(handle != nullptr, "open seek entry");
	if (!handle)
		return;

	Check(R1OVPK_SeekFile(handle, 4, 0), "seek from start");
	uint64_t position = 0;
	Check(R1OVPK_TellFile(handle, &position) && position == 4, "tell after start seek");
	Check(R1OVPK_SeekFile(handle, 2, 1), "seek from current");
	Check(R1OVPK_TellFile(handle, &position) && position == 6, "tell after current seek");
	Check(R1OVPK_SeekFile(handle, 123, 99), "invalid origin remains handled");
	Check(R1OVPK_TellFile(handle, &position) && position == 6, "invalid origin leaves position unchanged");
	Check(R1OVPK_SeekFile(handle, -1000, 0), "negative seek clamps");
	Check(R1OVPK_TellFile(handle, &position) && position == 0, "negative seek clamps to zero");
	Check(R1OVPK_SeekFile(handle, -6, 2), "seek from end");

	char tail[8]{};
	int bytesRead = 0;
	Check(R1OVPK_ReadFile(handle, tail, 6, &bytesRead), "tail read handled");
	Check(bytesRead == 6 && std::string(tail, tail + bytesRead) == "line2\n", "tail read value");
	Check(R1OVPK_CloseFile(handle), "close seek entry");
	Check(!R1OVPK_CloseFile(handle), "closed handle not classified");
	Check(!R1OVPK_TellFile(handle, &position), "closed handle tell not classified");
}

void TestRootEntryAndConcurrentOpens()
{
	void* root = R1OVPK_OpenFile("ROOT.BIN");
	Check(root != nullptr, "open root entry");
	if (root) {
		const std::vector<uint8_t> data = ReadAll(root);
		Check(data == std::vector<uint8_t>({0, 1, 2, 3}), "root entry bytes");
		Check(R1OVPK_CloseFile(root), "close root entry");
	}

	std::atomic_bool ok{true};
	std::vector<std::thread> workers;
	for (int i = 0; i < 8; ++i) {
		workers.emplace_back([&]() {
			for (int run = 0; run < 50; ++run) {
				void* handle = R1OVPK_OpenFile("scripts/unit/duplicate.txt");
				if (!handle) {
					ok.store(false, std::memory_order_relaxed);
					return;
				}
				const std::vector<uint8_t> data = ReadAll(handle);
				if (std::string(data.begin(), data.end()) != "server" || !R1OVPK_CloseFile(handle)) {
					ok.store(false, std::memory_order_relaxed);
					return;
				}
			}
		});
	}
	for (std::thread& worker : workers)
		worker.join();
	Check(ok.load(std::memory_order_relaxed), "concurrent open read close");
}

void TestIndexCache(const std::filesystem::path& root)
{
	const std::filesystem::path vpk = root / "vpk";
	std::vector<R1OVPKArchiveStamp> archives(1);
	Check(
		R1OVPK_GetArchiveStamp(vpk / "englishserver_unit.bsp.pak000_dir.vpk", archives[0]),
		"cache archive metadata");

	const std::string rootKey = R1OVPK_GetIndexRootKey(vpk);
	const std::filesystem::path cachePath = R1OVPK_GetIndexCachePath(rootKey);
	Check(std::filesystem::is_regular_file(cachePath), "index cache created");

	std::unordered_map<std::string, R1OVPKEntry> loaded;
	Check(R1OVPK_LoadIndexCache(cachePath, rootKey, archives, loaded), "index cache reload");
	Check(loaded.size() == 5, "index cache entry count");
	const auto duplicate = loaded.find("scripts/unit/duplicate.txt");
	Check(
		duplicate != loaded.end()
		&& duplicate->second.dirPath == archives[0].path.string(),
		"index cache preserves archive precedence");

	std::ifstream stream(cachePath, std::ios::binary);
	std::vector<uint8_t> original(
		(std::istreambuf_iterator<char>(stream)),
		std::istreambuf_iterator<char>());
	Check(original.size() > 32, "index cache has payload");
	if (original.size() > 32) {
		std::vector<uint8_t> corrupted = original;
		corrupted[corrupted.size() / 2] ^= 0x80;
		WriteBytes(cachePath, corrupted);

		std::unordered_map<std::string, R1OVPKEntry> unchanged;
		unchanged.emplace("sentinel", R1OVPKEntry{});
		Check(!R1OVPK_LoadIndexCache(cachePath, rootKey, archives, unchanged), "corrupt index cache rejected");
		Check(unchanged.size() == 1 && unchanged.count("sentinel") == 1, "corrupt cache does not partially mutate index");

		WriteBytes(cachePath, original);
		loaded.clear();
		Check(R1OVPK_LoadIndexCache(cachePath, rootKey, archives, loaded), "restored index cache reload");
	}

	std::vector<R1OVPKArchiveStamp> staleArchives = archives;
	++staleArchives[0].size;
	std::unordered_map<std::string, R1OVPKEntry> staleTarget;
	staleTarget.emplace("sentinel", R1OVPKEntry{});
	Check(!R1OVPK_LoadIndexCache(cachePath, rootKey, staleArchives, staleTarget), "stale archive metadata rejects cache");
	Check(staleTarget.size() == 1 && staleTarget.count("sentinel") == 1, "stale cache does not partially mutate index");
}

void TestRuntimePathValidation(const std::filesystem::path& root)
{
	const std::filesystem::path tfo = root / "tfo path";
	std::filesystem::create_directories(tfo);
	WriteBytes(tfo / "server_local.dll", {0});

	wchar_t* savedPath = nullptr;
	wchar_t* savedTFOBin = nullptr;
	size_t ignored = 0;
	_wdupenv_s(&savedPath, &ignored, L"PATH");
	_wdupenv_s(&savedTFOBin, &ignored, L"R1DELTA_TFO_BIN");

	const std::wstring quotedPath = L"  \"" + tfo.wstring() + L"\"  ";
	SetEnvironmentVariableW(L"PATH", quotedPath.c_str());
	SetEnvironmentVariableW(L"R1DELTA_TFO_BIN", quotedPath.c_str());

	const std::wstring resolved = r1delta::r1o::ResolveTFOBinDirectoryW();
	const std::wstring server = r1delta::r1o::ResolveTFOModulePathW(L"server_local.dll");
	Check(resolved.empty(), "runtime resolver rejects an incomplete explicit runtime");
	Check(server.empty(), "runtime resolver does not return an unvalidated module");
	Check(!r1delta::r1o::TFORuntimeValidationErrorW().empty(), "runtime resolver reports validation failure");

	SetEnvironmentVariableW(L"R1DELTA_TFO_BIN", nullptr);
	Check(
		_wcsicmp(r1delta::r1o::ResolveTFOBinDirectoryW().c_str(), tfo.c_str()) != 0,
		"runtime resolver does not search PATH");
	Check(
		_wcsicmp(r1delta::r1o::ResolveTFOModulePathW(L"unrelated.dll").c_str(), L"unrelated.dll") == 0,
		"runtime resolver leaves unrelated modules unchanged");

	SetEnvironmentVariableW(L"PATH", savedPath);
	SetEnvironmentVariableW(L"R1DELTA_TFO_BIN", savedTFOBin);
	free(savedPath);
	free(savedTFOBin);
}

struct RepairChunkSpec {
	uint32_t loadFlags;
};

std::vector<uint8_t> BuildRepairDirectoryTree(
	const std::vector<RepairChunkSpec>& chunks,
	const std::vector<uint8_t>& preload = {})
{
	std::vector<uint8_t> tree;
	AppendString(tree, "vmt");
	AppendString(tree, "materials/test");
	AppendString(tree, "asset");
	Append(tree, uint32_t{0x12345678});
	Append(tree, static_cast<uint16_t>(preload.size()));
	Append(tree, kR1DDeltaPackIndex);
	tree.insert(tree.end(), preload.begin(), preload.end());
	for (size_t index = 0; index < chunks.size(); ++index) {
		const RepairChunkSpec& chunk = chunks[index];
		Append(tree, chunk.loadFlags);
		Append(tree, uint16_t{});
		Append(tree, uint64_t{0x1000});
		Append(tree, uint64_t{0x20});
		Append(tree, uint64_t{0x40});
		Append(
			tree,
			index + 1 == chunks.size()
				? kChunkTerminator
				: uint16_t{});
	}
	AppendString(tree, "");
	AppendString(tree, "");
	AppendString(tree, "");
	tree.push_back(0);
	return tree;
}

uint32_t ReadRepairChunkFlags(const std::vector<uint8_t>& tree, size_t chunkIndex)
{
	const size_t stringsAndCrc =
		strlen("vmt") + 1
		+ strlen("materials/test") + 1
		+ strlen("asset") + 1
		+ sizeof(uint32_t);
	uint16_t preloadSize = 0;
	memcpy(
		&preloadSize,
		tree.data() + stringsAndCrc,
		sizeof(preloadSize));
	const size_t chunkStart =
		stringsAndCrc + sizeof(uint16_t) + sizeof(uint16_t) + preloadSize;
	const size_t chunkStride =
		sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint64_t) * 3
		+ sizeof(uint16_t);
	uint32_t flags = 0;
	memcpy(
		&flags,
		tree.data() + chunkStart + chunkIndex * chunkStride,
		sizeof(flags));
	return flags;
}

void TestDirectoryLoadFlagRepair()
{
	Check(
		IsBrokenR1MapVPKDirectoryPath(
			"vpk\\englishclient_mp_mia.bsp.pak000_dir.vpk",
			strlen("vpk\\englishclient_mp_mia.bsp.pak000_dir.vpk")),
		"mia language archive repair target");
	Check(
		IsBrokenR1MapVPKDirectoryPath(
			"C:/game/vpk/client_mp_nest2.bsp.pak000_dir.vpk",
			strlen("C:/game/vpk/client_mp_nest2.bsp.pak000_dir.vpk")),
		"nest2 archive repair target");
	Check(
		IsBrokenR1MapVPKDirectoryPath(
			"c:\\whatever\\vpk\\client_mp_mia.bsp.pak000",
			strlen("c:\\whatever\\vpk\\client_mp_mia.bsp.pak000")),
		"native mia pack-object path repair target");
	Check(
		IsBrokenR1MapVPKDirectoryPath(
			"C:/whatever/vpk/client_mp_nest2.bsp.pak000",
			strlen("C:/whatever/vpk/client_mp_nest2.bsp.pak000")),
		"native nest2 pack-object path repair target");
	Check(
		!IsBrokenR1MapVPKDirectoryPath(
			"vpk/englishclient_mp_rise.bsp.pak000_dir.vpk",
			strlen("vpk/englishclient_mp_rise.bsp.pak000_dir.vpk")),
		"healthy map archive excluded");
	Check(
		!IsBrokenR1MapVPKDirectoryPath(
			"vpk/client_mp_delta_common.bsp.pak000_dir.vpk",
			strlen("vpk/client_mp_delta_common.bsp.pak000_dir.vpk")),
		"common archive excluded");

	std::vector<uint8_t> tree = BuildRepairDirectoryTree(
		{{0x401}, {0x501}, {0x100401}, {0x100501}, {0x101}},
		{'p', 'r', 'e'});
	const VPKDirectoryLoadFlagRepairResult repair =
		RepairBrokenR1MapVPKDirectoryLoadFlags(tree.data(), tree.size());
	Check(repair.valid, "load-flag repair validates complete tree");
	Check(repair.entryCount == 1, "load-flag repair entry count");
	Check(repair.chunkCount == 5, "load-flag repair chunk count");
	Check(repair.repairedEntryCount == 1, "load-flag repair affected entry count");
	Check(repair.repairedChunkCount == 4, "load-flag repair affected chunk count");
	Check(ReadRepairChunkFlags(tree, 0) == 0x101, "0x401 maps to 0x101");
	Check(ReadRepairChunkFlags(tree, 1) == 0x001, "0x501 maps to 0x001");
	Check(ReadRepairChunkFlags(tree, 2) == 0x100101, "texture 0x100401 maps to 0x100101");
	Check(ReadRepairChunkFlags(tree, 3) == 0x100001, "texture 0x100501 maps to 0x100001");
	Check(ReadRepairChunkFlags(tree, 4) == 0x101, "healthy flags remain unchanged");

	std::vector<uint8_t> malformed = BuildRepairDirectoryTree({{0x401}});
	const std::vector<uint8_t> original = malformed;
	malformed.pop_back();
	malformed.pop_back();
	const VPKDirectoryLoadFlagRepairResult rejected =
		RepairBrokenR1MapVPKDirectoryLoadFlags(
			malformed.data(),
			malformed.size());
	Check(!rejected.valid, "truncated repair tree rejected");
	Check(
		ReadRepairChunkFlags(malformed, 0) == 0x401,
		"rejected tree is not partially changed");
	Check(
		ReadRepairChunkFlags(original, 0) == 0x401,
		"repair fixture starts with alternate-cache flags");
}

void CreateFixture(const std::filesystem::path& root)
{
	const std::filesystem::path vpk = root / "vpk";
	std::filesystem::create_directories(vpk);
	std::vector<uint8_t> pack;
	std::vector<uint8_t> serverPack{'l', 'e', 'f', 't', '-', 'r', 'i', 'g', 'h', 't'};

	// Literal Titanfall directory-tree bytes, deliberately independent from
	// WriteArchive. The 0x1337 pack index belongs to the entry, followed by two
	// 30-byte chunk descriptors separated by 0x0000 and terminated by 0xFFFF.
	// A parser that treats the separator as another pack index cannot read this.
	const std::vector<uint8_t> fixedMultiChunkTree{
		0x74, 0x78, 0x74, 0x00, 0x73, 0x63, 0x72, 0x69, 0x70, 0x74, 0x73, 0x2F,
		0x75, 0x6E, 0x69, 0x74, 0x00, 0x6D, 0x75, 0x6C, 0x74, 0x69, 0x63, 0x68,
		0x75, 0x6E, 0x6B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x37, 0x13,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
	};

	WriteArchive(
		vpk / "client_unit.bsp.pak000_dir.vpk",
		{{"txt", "scripts/unit", "duplicate", {}, {'o', 't', 'h', 'e', 'r'}, false}},
		pack);
	WriteArchive(
		vpk / "englishclient_unit.bsp.pak000_dir.vpk",
		{
			{"txt", "scripts/unit", "duplicate", {}, {'e', 'n', 'g', 'l', 'i', 's', 'h'}, false},
			{"txt", "scripts/unit", "hello", {'p', 'r', 'e', '-'}, {'l', 'i', 'n', 'e', '1', '\n', 'l', 'i', 'n', 'e', '2', '\n'}, true},
			{"txt", "scripts/unit", "badsize", {}, {'b', 'a', 'd'}, true, uint64_t{4}},
			{"bin", " ", "root", {}, {0, 1, 2, 3}, false},
		},
		pack);
	WriteArchive(
		vpk / "englishserver_unit.bsp.pak000_dir.vpk",
		{
			{"txt", "scripts/unit", "duplicate", {}, {'s', 'e', 'r', 'v', 'e', 'r'}, false},
			{"txt", "scripts/unit", "hello", {'p', 'r', 'e', '-'}, {'l', 'i', 'n', 'e', '1', '\n', 'l', 'i', 'n', 'e', '2', '\n'}, true},
			{"txt", "scripts/unit", "badsize", {}, {'b', 'a', 'd'}, true, uint64_t{4}},
			{"bin", " ", "root", {}, {0, 1, 2, 3}, false},
		},
		serverPack,
		fixedMultiChunkTree);
	WriteArchive(
		vpk / "englishserver_ignored.bsp.pak000_dir.vpk.part",
		{{"txt", "scripts/unit", "partial_download", {}, {'b', 'a', 'd'}, false}},
		serverPack);

	std::vector<uint8_t> malformed;
	Append(malformed, kVPKHeaderMagic);
	Append(malformed, uint16_t{2});
	Append(malformed, uint16_t{3});
	Append(malformed, uint32_t{1});
	Append(malformed, uint32_t{});
	malformed.push_back('x');
	WriteBytes(vpk / "zzzmalformed.pak000_dir.vpk", malformed);
	WriteBytes(vpk / "client_mp_delta_common.bsp.pak000_000.vpk", pack);
	WriteBytes(vpk / "server_mp_delta_common.bsp.pak000_000.vpk", serverPack);
}

}

int main()
{
	const std::filesystem::path original = std::filesystem::current_path();
	const std::filesystem::path root = std::filesystem::temp_directory_path()
		/ ("r1delta_vpk_tests_" + std::to_string(GetCurrentProcessId()));

	try {
		std::filesystem::remove_all(root);
		CreateFixture(root);
		const std::string cacheDirectory = (root / "cache").string();
		SetEnvironmentVariableA("R1DELTA_VPK_CACHE_DIR", cacheDirectory.c_str());
		std::filesystem::current_path(root);

		TestAsyncPrecacheEntryInspection();
		TestAsyncPrecacheEntryClaimsSerializeWorkers();
		TestConcurrentCompressedFirstOpen();
		TestLookupAndPrecedence();
		TestCompressedAndPreloadEntry();
		TestFixedMultiChunkEntry();
		TestSeekTellAndRead();
		TestRootEntryAndConcurrentOpens();
		TestIndexCache(root);
		TestRuntimePathValidation(root);
		TestDirectoryLoadFlagRepair();

		std::filesystem::current_path(original);
		std::filesystem::remove_all(root);
	}
	catch (const std::exception& error) {
		std::filesystem::current_path(original);
		std::filesystem::remove_all(root);
		std::cerr << "FAILED: unexpected exception: " << error.what() << '\n';
		return 1;
	}

	if (failures) {
		std::cerr << failures << " R1O VPK test(s) failed\n";
		return 1;
	}

	std::cout << "All R1O VPK tests passed\n";
	return 0;
}
