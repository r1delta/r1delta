// Disk cache helpers shared by the R2 providers (header only).
//
//  1. A compressed container ("R2CZ") for cached files: converted VTFs, VMTs, model blobs, converted BSPs. The converted textures
//     are mostly decoded RGBA8 or DXT data; zstd level 3 gives about 4x on RGBA8, 2x on DXT1, 1.3x on DXT5 (measured on the mp_glitch
//     cache) and decodes at 0.8 to 2.5 GB/s, so a warm load reads fewer bytes than before and decodes on the conversion workers.
//     The zstd frame carries a content checksum, so a truncated or bit-rotted file is rejected and re-converted instead of served.
//  2. Cache maintenance: stale revision, build and fingerprint directories and orphaned temporary files are deleted at start-up, and the
//     total size is capped by deleting the least recently used files (R2PORT_CACHE_MAX_MB, default 4096).
//  3. A per-process write budget (WriteBudget): no new cache file is written once the cache reached its cap, the disk is nearly full or
//     writes keep failing (read-only or full game directory); reads and the game itself are unaffected.
//
// The cache is ON by default. R2PORT_CACHE=0 turns it off (memory only), R2PORT_CACHE_COMPRESS=0 turns the container off for writing
// (A/B switch; reading always accepts both forms).
#pragma once

#include <zstd.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace r2port::cache {

inline constexpr char kPackMagic[4] = { 'R', '2', 'C', 'Z' };
inline constexpr std::uint32_t kPackVersion = 1;
// Smaller files are stored as they are (VMTs, dependency lists, small VTFs): the container costs more than it saves.
inline constexpr std::size_t kMinimumPackedSize = 64 * 1024;

#pragma pack(push, 1)
struct PackHeader {
    char magic[4];
    std::uint32_t version;
    std::uint64_t rawSize;
};
#pragma pack(pop)
static_assert(sizeof(PackHeader) == 16);

inline bool CompressionEnabled()
{
    static const bool enabled = []() {
        char value[8]{};
        return !(GetEnvironmentVariableA("R2PORT_CACHE_COMPRESS", value, sizeof(value)) > 0 && value[0] == '0');
    }();
    return enabled;
}

inline bool IsPacked(const std::uint8_t* data, std::size_t size)
{
    return size >= sizeof(PackHeader) && std::memcmp(data, kPackMagic, sizeof(kPackMagic)) == 0;
}

// Builds the container for `data`. Returns false (and leaves `out` empty) when it is not worth it or compression failed. With `always`
// the container is built for every non-empty size even when it is not smaller (the content checksum is the point: files that must never be
// served damaged); R2PORT_CACHE_COMPRESS=0 still disables it.
inline bool Pack(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out, std::size_t minimumSize = kMinimumPackedSize, bool always = false)
{
    out.clear();
    if (!CompressionEnabled() || size == 0 || (!always && size < minimumSize))
        return false;
    out.resize(sizeof(PackHeader) + ZSTD_compressBound(size));
    // Level 3 up to 4 MiB, level 1 beyond (big normal and sky maps; the ratio differs by a few percent, the speed by 2x or more: a first
    // load converts hundreds of MB of textures and stores them while the rest of the map is still being converted).
    const int level = size > (4u << 20) ? 1 : 3;
    ZSTD_CCtx* context = ZSTD_createCCtx();
    if (!context) {
        out.clear();
        return false;
    }
    ZSTD_CCtx_setParameter(context, ZSTD_c_compressionLevel, level);
    ZSTD_CCtx_setParameter(context, ZSTD_c_checksumFlag, 1);
    const std::size_t packed = ZSTD_compress2(context, out.data() + sizeof(PackHeader), out.size() - sizeof(PackHeader), data, size);
    ZSTD_freeCCtx(context);
    if (ZSTD_isError(packed) || (!always && sizeof(PackHeader) + packed >= size)) {
        out.clear();
        return false;
    }
    PackHeader header{};
    std::memcpy(header.magic, kPackMagic, sizeof(header.magic));
    header.version = kPackVersion;
    header.rawSize = size;
    std::memcpy(out.data(), &header, sizeof(header));
    out.resize(sizeof(PackHeader) + packed);
    return true;
}

// Replaces a container read from disk by its raw bytes. Plain files are left untouched (true). False: corrupt container.
inline bool Unpack(std::vector<std::uint8_t>& file)
{
    if (!IsPacked(file.data(), file.size()))
        return true;
    PackHeader header{};
    std::memcpy(&header, file.data(), sizeof(header));
    if (header.version != kPackVersion || header.rawSize == 0 || header.rawSize > (1ull << 31))
        return false;
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(header.rawSize));
    const std::size_t produced = ZSTD_decompress(raw.data(), raw.size(), file.data() + sizeof(PackHeader), file.size() - sizeof(PackHeader));
    if (ZSTD_isError(produced) || produced != raw.size())
        return false;
    file = std::move(raw);
    return true;
}

// Like Unpack, but a file that is not a container is rejected: for files this build always writes as containers (checksummed). With the
// R2PORT_CACHE_COMPRESS=0 A/B switch the files are written plain, so plain files are accepted again.
inline bool UnpackStrict(std::vector<std::uint8_t>& file)
{
    if (!CompressionEnabled())
        return Unpack(file);
    return IsPacked(file.data(), file.size()) && Unpack(file);
}

// ---- last-use refresh and maintenance -------------------------------------------------------------------------------------

inline std::uint64_t FileTimeToU64(const FILETIME& time)
{
    return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

inline std::uint64_t NowFileTime()
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    return FileTimeToU64(now);
}

// Marks a cache hit as recent for the LRU eviction: sets the write time to now when it is older than `maxAge` (100 ns units, default a day:
// one cheap call per stale file, nothing for files already used today).
inline void TouchIfOld(const std::filesystem::path& file, std::uint64_t maxAge = 24ull * 3600ull * 10000000ull)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data))
        return;
    const std::uint64_t now = NowFileTime();
    if (now - FileTimeToU64(data.ftLastWriteTime) < maxAge)
        return;
    HANDLE handle = CreateFileW(file.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return;
    FILETIME stamp;
    GetSystemTimeAsFileTime(&stamp);
    SetFileTime(handle, nullptr, nullptr, &stamp);
    CloseHandle(handle);
}

// ---- served-file hash log --------------------------------------------------------------------------------------------------

// R2PORT_LOG_HASHES=1: one line "<name> <FNV-1a 64 hex> <size>" per served converted file (materials, textures, models, the BSP) in
// <directory>/filehashes.txt, whether the bytes were converted now or read from the disk cache: two runs (cache off, cache cold, cache
// warm) are compared byte for byte with it.
inline bool HashLoggingEnabled()
{
    static const bool enabled = []() {
        char flag[8]{};
        return GetEnvironmentVariableA("R2PORT_LOG_HASHES", flag, sizeof(flag)) > 0 && flag[0] != '0';
    }();
    return enabled;
}

inline void LogFileHash(const std::filesystem::path& directory, std::string_view name, const std::uint8_t* data, std::size_t size)
{
    if (!HashLoggingEnabled() || directory.empty())
        return;
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (FILE* file = _wfopen((directory / L"filehashes.txt").c_str(), L"ab")) {
        std::fprintf(file, "%.*s %016llx %zu\n", static_cast<int>(name.size()), name.data(), static_cast<unsigned long long>(hash), size);
        std::fclose(file);
    }
}

// Identifies the build that wrote a cache: the PE time stamp and image size of the module this code is linked into (tier0.dll). Every cache
// key carries it, so a game update (a new tier0.dll) can never read converted output of the converters of the previous build, whatever
// the hand-kept revision numbers say; the old directories are deleted by the next maintenance pass.
inline const char kBuildKeyAnchor = 0;
inline const std::string& BuildKey()
{
    static const std::string key = []() {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&kBuildKeyAnchor), &module) || !module)
            return std::string("nobuild");
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const std::uint8_t*>(module) + dos->e_lfanew);
        char text[32]{};
        _snprintf_s(text, sizeof(text), _TRUNCATE, "b%08x%06x", static_cast<unsigned>(nt->FileHeader.TimeDateStamp),
            static_cast<unsigned>(nt->OptionalHeader.SizeOfImage & 0xFFFFFF));
        return std::string(text);
    }();
    return key;
}

// ---- write budget ----------------------------------------------------------------------------------------------------------

// Bounds what this process may add to the disk cache. The maintenance pass (start-up, background) sets the bytes already on disk and the
// effective cap (the configured cap, lowered so that the disk keeps at least kMinimumFreeBytes free). Before it has run the budget is
// unlimited, which is bounded by what one session converts. Writes stop for the session after kMaximumFailures failures in a row
// (read-only, full or quota-limited directory); reads are unaffected.
inline constexpr std::uint64_t kMinimumFreeBytes = 3ull << 30;
inline constexpr unsigned kMaximumFailures = 8;

struct WriteBudget {
    std::atomic<std::uint64_t> cap{ ~0ull };
    std::atomic<std::uint64_t> used{ 0 };
    std::atomic<unsigned> failures{ 0 };
    std::atomic<bool> stopped{ false };
};

inline WriteBudget& Budget()
{
    static WriteBudget budget;
    return budget;
}

inline void SetBudget(std::uint64_t existingBytes, std::uint64_t capBytes)
{
    WriteBudget& budget = Budget();
    budget.used.store(existingBytes, std::memory_order_relaxed);
    budget.cap.store(capBytes, std::memory_order_relaxed);
}

// True when `bytes` more may be written; the bytes are then counted (ReleaseWrite gives them back when the write failed).
inline bool ReserveWrite(std::uint64_t bytes)
{
    WriteBudget& budget = Budget();
    if (budget.stopped.load(std::memory_order_relaxed))
        return false;
    const std::uint64_t cap = budget.cap.load(std::memory_order_relaxed);
    if (budget.used.fetch_add(bytes, std::memory_order_relaxed) + bytes > cap) {
        budget.used.fetch_sub(bytes, std::memory_order_relaxed);
        return false;
    }
    return true;
}

inline void ReleaseWrite(std::uint64_t bytes)
{
    Budget().used.fetch_sub(bytes, std::memory_order_relaxed);
}

// Records the outcome of a write attempt. Returns true once, when this failure is the one that stops the session's writes (log it then).
inline bool NoteWriteResult(bool ok)
{
    WriteBudget& budget = Budget();
    if (ok) {
        budget.failures.store(0, std::memory_order_relaxed);
        return false;
    }
    return budget.failures.fetch_add(1, std::memory_order_relaxed) + 1 >= kMaximumFailures && !budget.stopped.exchange(true);
}

// True once per process: the first time a write was refused because the cap is reached (log it then).
inline bool TakeCapNotice()
{
    static std::atomic<bool> noted{ false };
    return !noted.exchange(true);
}

struct MaintenancePlan {
    std::filesystem::path r2portDirectory;      // <game dir>/r2port
    std::string currentFingerprint;             // cache/<fingerprint> of the installed R2 packages
    std::string currentConvertedName;           // converted/<revision>.<build key><tags> in use
    std::string currentModelRevision;           // cache/models/<revision>.<build key>
    std::uint64_t capBytes = 4ull << 30;        // total of converted files + models + converted-BSP cache + materialized map copies
    std::uint64_t sameRevisionVariantDays = 14; // other tag variants of the current revision (env/uber A/B dirs) unused this long are dropped
};

struct MaintenanceResult {
    std::uint64_t removedDirectories = 0;
    std::uint64_t removedFiles = 0;
    std::uint64_t removedBytes = 0;
    std::uint64_t evictedFiles = 0;
    std::uint64_t evictedBytes = 0;
    std::uint64_t keptBytes = 0;
};

inline std::uint64_t DirectoryBytes(const std::filesystem::path& directory)
{
    std::error_code error;
    std::uint64_t total = 0;
    for (std::filesystem::recursive_directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error)) {
        std::error_code itemError;
        if (it->is_regular_file(itemError))
            total += it->file_size(itemError);
    }
    return total;
}

// The part of a cache directory name before its first '-': the revision (and build key) that must match exactly; what follows are the
// A/B tag variants (R2TEX_ENV, R2_UBER pack, BC7) of that revision.
inline std::string RevisionToken(const std::string& name)
{
    return name.substr(0, name.find('-'));
}

inline void RemoveTree(const std::filesystem::path& path, MaintenanceResult& result)
{
    std::error_code error;
    const std::uint64_t bytes = std::filesystem::is_directory(path, error) ? DirectoryBytes(path) : 0;
    const std::uintmax_t removed = std::filesystem::remove_all(path, error);
    if (!error || removed) {
        ++result.removedDirectories;
        result.removedFiles += removed;
        result.removedBytes += bytes;
    }
}

// Deletes stale cache directories and, when the cache is over its cap, the least recently used files. Safe to run on a background
// thread while the game uses the cache: a file that is open cannot be deleted, a deleted file is only re-converted, and files
// written in the last two hours are never evicted.
inline MaintenanceResult RunMaintenance(const MaintenancePlan& plan)
{
    MaintenanceResult result;
    std::error_code error;
    const std::filesystem::path cacheRoot = plan.r2portDirectory / "cache";
    if (!std::filesystem::is_directory(cacheRoot, error))
        return result;

    // 1. cache/<fingerprint>: the fingerprint changes when the R2 install changes, the old directory is never read again.
    for (std::filesystem::directory_iterator it(cacheRoot, error), end; !error && it != end; it.increment(error)) {
        const std::string name = it->path().filename().string();
        if (!it->is_directory(error) || name == "models" || name == plan.currentFingerprint)
            continue;
        RemoveTree(it->path(), result);
    }

    // 2. converted/<revision>.<build key><tags>: other revisions and builds are stale; other tag variants of the same revision go when unused.
    const std::filesystem::path converted = cacheRoot / plan.currentFingerprint / "converted";
    const std::string currentRevision = RevisionToken(plan.currentConvertedName);
    const std::uint64_t variantAge = plan.sameRevisionVariantDays * 24ull * 3600ull * 10000000ull;
    for (std::filesystem::directory_iterator it(converted, error), end; !error && it != end; it.increment(error)) {
        const std::string name = it->path().filename().string();
        if (!it->is_directory(error) || name == plan.currentConvertedName)
            continue;
        if (RevisionToken(name) != currentRevision) {
            RemoveTree(it->path(), result);
            continue;
        }
        WIN32_FILE_ATTRIBUTE_DATA stamp{};
        const std::filesystem::path marker = it->path() / ".lastuse";
        if (!GetFileAttributesExW(marker.c_str(), GetFileExInfoStandard, &stamp)
            && !GetFileAttributesExW(it->path().c_str(), GetFileExInfoStandard, &stamp))
            continue;
        if (NowFileTime() - FileTimeToU64(stamp.ftLastWriteTime) > variantAge)
            RemoveTree(it->path(), result);
    }
    // The directory in use records its last use so that a variant someone stopped using ages out.
    {
        const std::filesystem::path marker = converted / plan.currentConvertedName / ".lastuse";
        std::filesystem::create_directories(marker.parent_path(), error);
        HANDLE handle = CreateFileW(marker.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }

    // 3. cache/models/<revision>: other revisions are stale.
    const std::filesystem::path models = cacheRoot / "models";
    for (std::filesystem::directory_iterator it(models, error), end; !error && it != end; it.increment(error)) {
        if (it->is_directory(error) && it->path().filename().string() != plan.currentModelRevision)
            RemoveTree(it->path(), result);
    }

    // 4. size cap: least recently used files first over the converted files of the current directory, the model blobs and the
    // converted-BSP cache. r2tex.index, the shader cache and files of the last two hours are never touched.
    struct Entry {
        std::filesystem::path path;
        std::uint64_t bytes;
        std::uint64_t stamp;
    };
    std::vector<Entry> entries;
    std::uint64_t total = 0;
    const std::uint64_t staleTemporary = NowFileTime() - 3600ull * 10000000ull;
    const auto scan = [&](const std::filesystem::path& root) {
        for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error), end;
             !error && it != end; it.increment(error)) {
            std::error_code itemError;
            if (!it->is_regular_file(itemError))
                continue;
            if (it->path().filename() == ".lastuse")
                continue;
            WIN32_FILE_ATTRIBUTE_DATA data{};
            if (!GetFileAttributesExW(it->path().c_str(), GetFileExInfoStandard, &data))
                continue;
            const std::uint64_t bytes = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            const std::uint64_t stamp = FileTimeToU64(data.ftLastWriteTime);
            // A temporary file (".tmp<pid>.<tid>") older than an hour belongs to a process that died while writing: delete it.
            if (stamp < staleTemporary && it->path().filename().string().find(".tmp") != std::string::npos) {
                if (std::filesystem::remove(it->path(), itemError)) {
                    ++result.removedFiles;
                    result.removedBytes += bytes;
                }
                continue;
            }
            entries.push_back({ it->path(), bytes, stamp });
            total += bytes;
        }
    };
    scan(converted);
    scan(models);
    scan(plan.r2portDirectory / "bsp_cache");
    scan(plan.r2portDirectory.parent_path() / "r2port_cache");   // the map files the engine reads (rewritten at every load of a map)
    if (total > plan.capBytes) {
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.stamp < b.stamp; });
        const std::uint64_t target = plan.capBytes / 10 * 9;
        const std::uint64_t protectedAfter = NowFileTime() - 2ull * 3600ull * 10000000ull;
        for (const Entry& entry : entries) {
            if (total <= target || entry.stamp >= protectedAfter)
                break;
            if (std::filesystem::remove(entry.path, error)) {
                total -= entry.bytes;
                ++result.evictedFiles;
                result.evictedBytes += entry.bytes;
            }
        }
    }
    result.keptBytes = total;

    // The write budget of this process: the configured cap, lowered so that the disk keeps kMinimumFreeBytes free. When the disk is that
    // full already the cache may not grow at all.
    std::uint64_t effectiveCap = plan.capBytes;
    ULARGE_INTEGER freeBytes{};
    if (GetDiskFreeSpaceExW(plan.r2portDirectory.c_str(), &freeBytes, nullptr, nullptr)) {
        const std::uint64_t headroom = freeBytes.QuadPart > kMinimumFreeBytes ? freeBytes.QuadPart - kMinimumFreeBytes : 0;
        effectiveCap = std::min<std::uint64_t>(effectiveCap, total + headroom);
    }
    SetBudget(total, effectiveCap);
    return result;
}

// The on-disk cache of converted files is ON by default: the first load of a map converts it from the player's Titanfall 2 install, later
// loads (and later game sessions) read the converted files back. R2PORT_CACHE=0 turns it off: everything is then served from memory (the
// providers keep an LRU bounded by R2PORT_MEM_CACHE_MB, default 1536). The r2tex index (about 6 MB, fingerprinted, not a conversion result)
// and the shader cache (a few KB, the engine opens those as real files) stay on disk either way.
inline bool DiskCacheEnabled()
{
    static const bool enabled = []() {
        char value[8]{};
        return !(GetEnvironmentVariableA("R2PORT_CACHE", value, sizeof(value)) > 0 && value[0] == '0');
    }();
    return enabled;
}

inline std::size_t MemoryCacheBytes()
{
    static const std::size_t bytes = []() {
        char value[16]{};
        long long mib = 1536;
        if (GetEnvironmentVariableA("R2PORT_MEM_CACHE_MB", value, sizeof(value)) > 0 && std::atoll(value) > 0)
            mib = std::atoll(value);
        return static_cast<std::size_t>(mib) << 20;
    }();
    return bytes;
}

// R2PORT_CACHE_MAX_MB (megabytes, 0 = unlimited) or the default.
inline std::uint64_t ConfiguredCapBytes(std::uint64_t defaultMiB = 4096)
{
    char value[32]{};
    if (GetEnvironmentVariableA("R2PORT_CACHE_MAX_MB", value, sizeof(value)) > 0) {
        const long long requested = std::atoll(value);
        if (requested == 0)
            return ~0ull;
        if (requested > 0)
            return static_cast<std::uint64_t>(requested) << 20;
    }
    return defaultMiB << 20;
}

}  // namespace r2port::cache
