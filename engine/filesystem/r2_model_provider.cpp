#include "r2_model_provider.h"
#include "engine/filesystem/filesystem.h"

#include "engine/core/core.h"
#include "engine/filesystem/r1o_vpk.h"
#include "engine/filesystem/r2_map_provider.h"
#include "engine/filesystem/r2_material_provider.h"
#include "engine/logging/logging.h"
#include "engine/r2port/modelconv/modelconv.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace r2port {
namespace {

// Bump when the converter or the cached blob layout changes: converted models are persisted per
// source package signature and revision.
constexpr std::string_view kModelCacheRevision = "3";
constexpr std::uint32_t kBlobMagic = 0x434D3152; // "R1MC"
constexpr std::uint32_t kBlobConverted = 1;
constexpr std::uint32_t kBlobFailed = 2;

constexpr std::size_t kBspHeaderSize = 0x810;
constexpr std::uint32_t kBspLumpCount = 128;
constexpr std::uint32_t kGameLump = 0x23;
constexpr std::uint32_t kIdentSprp = 'p' | ('r' << 8) | ('p' << 16) | ('s' << 24);
constexpr std::size_t kSprpNameLength = 128;
constexpr std::array<const char*, 5> kEntitySuffixes = {
    "_env.ent", "_fx.ent", "_script.ent", "_snd.ent", "_spawn.ent",
};

void LogModelMessage(const char* message)
{
    Msg("%s", message);
    OutputDebugStringA(message);
}

std::string LowerAscii(std::string value)
{
    for (char& character : value)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return value;
}

enum class FileKind { Mdl, Vvd, Vtx, Phy, Vvc };

struct ModelPath {
    std::string stem; // models/<name> without extension
    FileKind kind;
};

bool EndsWith(std::string_view value, std::string_view suffix)
{
    return value.size() > suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

std::optional<ModelPath> ParseModelPath(std::string_view path)
{
    constexpr std::string_view prefix = "models/";
    if (path.size() <= prefix.size() + 4 || path.substr(0, prefix.size()) != prefix)
        return std::nullopt;
    struct Suffix { std::string_view text; FileKind kind; };
    static constexpr Suffix suffixes[] = {
        { ".dx11.vtx", FileKind::Vtx }, { ".vtx", FileKind::Vtx }, { ".mdl", FileKind::Mdl },
        { ".vvd", FileKind::Vvd }, { ".phy", FileKind::Phy }, { ".vvc", FileKind::Vvc },
    };
    for (const Suffix& suffix : suffixes) {
        if (EndsWith(path, suffix.text))
            return ModelPath{ std::string(path.substr(0, path.size() - suffix.text.size())), suffix.kind };
    }
    return std::nullopt;
}

struct ModelSet {
    FileBytes mdl;
    FileBytes vvd;
    FileBytes vtx;
    FileBytes vvc;
    FileBytes phy;
    std::vector<std::string> materials; // without materials/ and .vmt

    const FileBytes& File(FileKind kind) const
    {
        switch (kind) {
        case FileKind::Mdl: return mdl;
        case FileKind::Vvd: return vvd;
        case FileKind::Vtx: return vtx;
        case FileKind::Phy: return phy;
        default: return vvc;
        }
    }
};

FileBytes MakeBytes(std::vector<std::uint8_t>&& bytes)
{
    if (bytes.empty())
        return {};
    return std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
}

struct Package {
    std::string path;      // directory VPK
    std::string signature; // file name, size and modification time
};

std::string PackageSignature(const std::filesystem::path& file)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (error)
        return {};
    const auto modified = std::filesystem::last_write_time(file, error);
    if (error)
        return {};
    std::string signature = LowerAscii(file.filename().string());
    signature.push_back('-');
    signature.append(std::to_string(size));
    signature.push_back('-');
    signature.append(std::to_string(modified.time_since_epoch().count()));
    return signature;
}

bool WriteFileAtomic(const std::filesystem::path& target, const std::vector<std::uint8_t>& data)
{
    std::filesystem::path temporary = target;
    temporary += ".tmp" + std::to_string(GetCurrentProcessId()) + "." + std::to_string(GetCurrentThreadId());
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file)
            return false;
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        file.close();
        if (!file) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

bool ReadWholeFile(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return false;
    const std::streamoff size = file.tellg();
    if (size < 0 || size > (1ll << 30))
        return false;
    bytes.resize(static_cast<std::size_t>(size));
    file.seekg(0);
    return size == 0 || static_cast<bool>(file.read(reinterpret_cast<char*>(bytes.data()), size));
}

void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(value));
}

void AppendU64(std::vector<std::uint8_t>& out, std::uint64_t value)
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(value));
}

class BlobReader {
public:
    explicit BlobReader(const std::vector<std::uint8_t>& data) : m_data(data) {}
    bool U32(std::uint32_t& value) { return Copy(&value, sizeof(value)); }
    bool U64(std::uint64_t& value) { return Copy(&value, sizeof(value)); }
    bool Bytes(std::uint64_t size, std::vector<std::uint8_t>& out)
    {
        if (size > m_data.size() - m_position)
            return false;
        out.assign(m_data.begin() + static_cast<std::ptrdiff_t>(m_position),
            m_data.begin() + static_cast<std::ptrdiff_t>(m_position + size));
        m_position += static_cast<std::size_t>(size);
        return true;
    }
    bool AtEnd() const { return m_position == m_data.size(); }

private:
    bool Copy(void* destination, std::size_t size)
    {
        if (size > m_data.size() - m_position)
            return false;
        std::memcpy(destination, m_data.data() + m_position, size);
        m_position += size;
        return true;
    }
    const std::vector<std::uint8_t>& m_data;
    std::size_t m_position{};
};

// Reads the model names of the first sprp sub-lump of a converted (R1) BSP.
std::vector<std::string> StaticPropModelNames(const std::vector<std::uint8_t>& bsp)
{
    std::vector<std::string> names;
    const auto read32 = [&bsp](std::size_t offset, std::uint32_t& value) {
        if (offset > bsp.size() || bsp.size() - offset < sizeof(value))
            return false;
        std::memcpy(&value, bsp.data() + offset, sizeof(value));
        return true;
    };
    if (bsp.size() < kBspHeaderSize)
        return names;
    std::uint32_t lumpOffset{}, lumpLength{};
    if (!read32(16 + kGameLump * 16, lumpOffset) || !read32(16 + kGameLump * 16 + 4, lumpLength))
        return names;
    if (lumpOffset > bsp.size() || lumpLength > bsp.size() - lumpOffset || lumpLength < 4)
        return names;
    std::uint32_t count{};
    read32(lumpOffset, count);
    if (count > 64 || 4 + static_cast<std::uint64_t>(count) * 16 > lumpLength)
        return names;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t entry = lumpOffset + 4 + static_cast<std::size_t>(i) * 16;
        std::uint32_t id{}, offset{}, length{};
        std::uint16_t flags{};
        read32(entry, id);
        read32(entry + 8, offset);
        read32(entry + 12, length);
        std::memcpy(&flags, bsp.data() + entry + 4, sizeof(flags));
        if (id != kIdentSprp || (flags & 1u) != 0)
            continue;
        if (offset > bsp.size() || length > bsp.size() - offset || length < 4)
            return names;
        std::uint32_t modelCount{};
        read32(offset, modelCount);
        if (static_cast<std::uint64_t>(modelCount) * kSprpNameLength > length - 4)
            return names;
        for (std::uint32_t model = 0; model < modelCount; ++model) {
            const char* text = reinterpret_cast<const char*>(bsp.data()) + offset + 4 + model * kSprpNameLength;
            const std::size_t size = strnlen(text, kSprpNameLength);
            if (size)
                names.emplace_back(text, size);
        }
        return names;
    }
    return names;
}

// Collects every quoted or unquoted models/...mdl token in an entity file.
void AppendEntityModelNames(const std::vector<std::uint8_t>& entities, std::vector<std::string>& names)
{
    const std::string_view text(reinterpret_cast<const char*>(entities.data()), entities.size());
    const auto isStop = [](char c) { return c == '"' || c == '\n' || c == '\r' || c == '\0' || c == ' ' || c == '\t'; };
    for (std::size_t position = 0; position + 7 < text.size(); ++position) {
        const char first = static_cast<char>(std::tolower(static_cast<unsigned char>(text[position])));
        if (first != 'm')
            continue;
        std::string head(text.substr(position, 7));
        head = LowerAscii(std::move(head));
        if (head != "models/" && head != "models\\")
            continue;
        if (position && !isStop(text[position - 1]))
            continue;
        std::size_t end = position;
        while (end < text.size() && !isStop(text[end]))
            ++end;
        std::string name(text.substr(position, end - position));
        if (name.size() > 4 && LowerAscii(name.substr(name.size() - 4)) == ".mdl")
            names.push_back(std::move(name));
        position = end;
    }
}

class R2ModelProvider final : public FileProvider {
public:
    R2ModelProvider()
    {
        m_worker = std::thread([this]() { PrefetchLoop(); });
    }

    ~R2ModelProvider() override
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
        }
        m_queueAvailable.notify_all();
        if (m_worker.joinable())
            m_worker.join();
    }

    bool Exists(std::string_view normalizedPath) override
    {
        return static_cast<bool>(File(normalizedPath));
    }

    bool MayContain(std::string_view normalizedPath) const override
    {
        return static_cast<bool>(File(normalizedPath));
    }

    std::optional<std::uint64_t> Size(std::string_view normalizedPath) override
    {
        const FileBytes file = File(normalizedPath);
        return file ? std::optional<std::uint64_t>(file->size()) : std::nullopt;
    }

    FileBytes Read(std::string_view normalizedPath) override
    {
        const auto parsed = ParseModelPath(normalizedPath);
        if (!parsed)
            return {};
        const auto set = GetSet(parsed->stem);
        if (!set)
            return {};
        const FileBytes& file = set->File(parsed->kind);
        if (!file)
            return {};
        m_filesServed.fetch_add(1, std::memory_order_relaxed);
        if (parsed->kind == FileKind::Mdl) {
            // R1 resolves the model's materials right after it parses the MDL; an unknown
            // material path is a cached miss, so make them known before returning it.
            RegisterMaterials(*set);
            bool expected = false;
            if (m_loggedServed.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
                char message[512]{};
                _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "R1Delta: R2 model provider served first model path=%.*s bytes=%zu\n",
                    static_cast<int>(std::min<std::size_t>(normalizedPath.size(), 300)),
                    normalizedPath.data(), file->size());
                LogModelMessage(message);
            }
        }
        return file;
    }

    void Prefetch(std::string_view mapName, FileBytes bsp)
    {
        if (!bsp || !IsR2PortMap(mapName))
            return;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_jobs.push_back(PrefetchJob{ std::string(mapName), std::move(bsp) });
        }
        m_queueAvailable.notify_one();
    }

    void LogMetrics(const char* phase) const
    {
        std::string map;
        std::size_t entries{};
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            map = m_map;
            entries = m_entries.size();
        }
        char message[1024]{};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "R1Delta: R2 model timing phase=%s map=%s entries=%zu converted=%llu disk-hits=%llu failed=%llu absent=%llu r1-owned=%llu files-served=%llu convert-total-ms=%llu convert-max-ms=%llu prefetch-models=%llu prefetch-ms=%llu\n",
            phase ? phase : "<unknown>", map.empty() ? "<none>" : map.c_str(), entries,
            static_cast<unsigned long long>(m_converted.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_diskHits.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_failed.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_absent.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_r1Owned.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_filesServed.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_buildTotalUs.load(std::memory_order_relaxed) / 1000),
            static_cast<unsigned long long>(m_buildMaxUs.load(std::memory_order_relaxed) / 1000),
            static_cast<unsigned long long>(m_prefetchModels.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_prefetchMs.load(std::memory_order_relaxed)));
        LogModelMessage(message);
    }

private:
    struct Entry {
        bool done{};
        std::shared_ptr<const ModelSet> set; // null: not an R2 model or conversion failed
    };

    struct PrefetchJob {
        std::string map;
        FileBytes bsp;
    };

    FileBytes File(std::string_view normalizedPath) const
    {
        const auto parsed = ParseModelPath(normalizedPath);
        if (!parsed)
            return {};
        const auto set = GetSet(parsed->stem);
        return set ? set->File(parsed->kind) : FileBytes{};
    }

    // Must hold m_mutex. The packages searched depend on the active map; a map change drops
    // the in-memory entries.
    void RefreshMapLocked() const
    {
        const std::string active = ActiveMapName();
        if (active == m_map)
            return;
        m_map = active;
        m_entries.clear();
        m_packages.clear();
        if (!IsR2PortMap(active))
            return;
        const std::filesystem::path install = GetR2InstallDirectory();
        if (install.empty())
            return;
        for (const std::string& name : { active, std::string("mp_common") }) {
            const std::filesystem::path file =
                install / "vpk" / ("englishclient_" + name + ".bsp.pak000_dir.vpk");
            std::string signature = PackageSignature(file);
            if (!signature.empty())
                m_packages.push_back({ file.string(), std::move(signature) });
        }
    }

    std::shared_ptr<const ModelSet> GetSet(const std::string& stem) const
    {
        std::shared_ptr<Entry> entry;
        std::vector<Package> packages;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            RefreshMapLocked();
            if (m_packages.empty())
                return {};
            const auto found = m_entries.find(stem);
            if (found != m_entries.end()) {
                entry = found->second;
                m_entryDone.wait(lock, [&entry]() { return entry->done; });
                return entry->set;
            }
            entry = std::make_shared<Entry>();
            m_entries.emplace(stem, entry);
            packages = m_packages;
        }

        std::shared_ptr<const ModelSet> set;
        try {
            set = Build(stem, packages);
        }
        catch (...) {
            m_failed.fetch_add(1, std::memory_order_relaxed);
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            entry->set = set;
            entry->done = true;
        }
        m_entryDone.notify_all();
        return set;
    }

    std::filesystem::path BlobPath(const Package& package, const std::string& stem) const
    {
        const std::filesystem::path directory = GetR2MapPatchDirectory();
        if (directory.empty())
            return {};
        return directory / "cache" / "models" / std::string(kModelCacheRevision) / package.signature
            / (stem + ".r1m");
    }

    std::shared_ptr<const ModelSet> LoadBlob(const std::filesystem::path& file, bool& failed) const
    {
        failed = false;
        std::vector<std::uint8_t> data;
        if (file.empty() || !ReadWholeFile(file, data))
            return {};
        BlobReader reader(data);
        std::uint32_t magic{}, kind{};
        if (!reader.U32(magic) || magic != kBlobMagic || !reader.U32(kind))
            return {};
        if (kind == kBlobFailed) {
            failed = true;
            return {};
        }
        if (kind != kBlobConverted)
            return {};
        std::uint64_t sizes[5]{};
        for (std::uint64_t& size : sizes) {
            if (!reader.U64(size))
                return {};
        }
        std::uint32_t materialCount{};
        if (!reader.U32(materialCount) || materialCount > 1024)
            return {};
        auto set = std::make_shared<ModelSet>();
        for (std::uint32_t i = 0; i < materialCount; ++i) {
            std::uint32_t length{};
            std::vector<std::uint8_t> text;
            if (!reader.U32(length) || length > 1024 || !reader.Bytes(length, text))
                return {};
            set->materials.emplace_back(reinterpret_cast<const char*>(text.data()), text.size());
        }
        std::vector<std::uint8_t> parts[5];
        for (int i = 0; i < 5; ++i) {
            if (!reader.Bytes(sizes[i], parts[i]))
                return {};
        }
        if (!reader.AtEnd() || parts[0].empty() || parts[1].empty() || parts[2].empty())
            return {};
        set->mdl = MakeBytes(std::move(parts[0]));
        set->vvd = MakeBytes(std::move(parts[1]));
        set->vtx = MakeBytes(std::move(parts[2]));
        set->vvc = MakeBytes(std::move(parts[3]));
        set->phy = MakeBytes(std::move(parts[4]));
        return set;
    }

    void StoreBlob(const std::filesystem::path& file, const ModelSet* set) const
    {
        if (file.empty())
            return;
        try {
            std::error_code error;
            std::filesystem::create_directories(file.parent_path(), error);
            if (error)
                return;
            std::vector<std::uint8_t> blob;
            AppendU32(blob, kBlobMagic);
            if (!set) {
                AppendU32(blob, kBlobFailed);
                WriteFileAtomic(file, blob);
                return;
            }
            AppendU32(blob, kBlobConverted);
            const FileBytes* files[5] = { &set->mdl, &set->vvd, &set->vtx, &set->vvc, &set->phy };
            for (const FileBytes* part : files)
                AppendU64(blob, *part ? (*part)->size() : 0);
            AppendU32(blob, static_cast<std::uint32_t>(set->materials.size()));
            for (const std::string& material : set->materials) {
                AppendU32(blob, static_cast<std::uint32_t>(material.size()));
                blob.insert(blob.end(), material.begin(), material.end());
            }
            for (const FileBytes* part : files) {
                if (*part)
                    blob.insert(blob.end(), (*part)->begin(), (*part)->end());
            }
            WriteFileAtomic(file, blob);
        }
        catch (...) {
        }
    }

    std::shared_ptr<const ModelSet> Build(const std::string& stem, const std::vector<Package>& packages) const
    {
        const std::string mdlPath = stem + ".mdl";
        const Package* source = nullptr;
        for (const Package& package : packages) {
            if (R1OVPK_HasDirectoryVPKFile(package.path.c_str(), mdlPath.c_str())) {
                source = &package;
                break;
            }
        }
        if (!source) {
            m_absent.fetch_add(1, std::memory_order_relaxed);
            return {};
        }

        // R1 owns the whole model when it has the MDL: a converted R2 companion (VVC, PHY) next to
        // an R1 MDL does not match it and crashes the client.
        if (R2PortNativeFileExists(mdlPath.c_str())) {
            m_r1Owned.fetch_add(1, std::memory_order_relaxed);
            return {};
        }

        const auto started = std::chrono::steady_clock::now();
        const std::filesystem::path blobPath = BlobPath(*source, stem);
        bool cachedFailure{};
        if (auto cached = LoadBlob(blobPath, cachedFailure)) {
            m_diskHits.fetch_add(1, std::memory_order_relaxed);
            RecordBuild(started);
            return cached;
        }
        if (cachedFailure) {
            m_diskHits.fetch_add(1, std::memory_order_relaxed);
            m_failed.fetch_add(1, std::memory_order_relaxed);
            return {};
        }

        const FileBytes sourceMdl = R1OVPK_ReadDirectoryVPKFile(source->path.c_str(), mdlPath.c_str());
        if (!sourceMdl) {
            ReportFailure(stem, "the R2 MDL could not be read from its VPK");
            return {};
        }

        modelconv::ModelInput input;
        input.mdl = std::span<const std::uint8_t>(sourceMdl->data(), sourceMdl->size());
        modelconv::ModelOutput output;
        std::string error;
        if (!modelconv::ConvertModelR2ToR1(input, output, error)
            || !modelconv::VerifyModelR1(output, error)) {
            ReportFailure(stem, error.c_str());
            StoreBlob(blobPath, nullptr);
            return {};
        }

        auto set = std::make_shared<ModelSet>();
        if (!modelconv::ListModelMaterials(input.mdl, set->materials, error)) {
            ReportFailure(stem, error.c_str());
            StoreBlob(blobPath, nullptr);
            return {};
        }
        set->mdl = MakeBytes(std::move(output.mdl));
        set->vvd = MakeBytes(std::move(output.vvd));
        set->vtx = MakeBytes(std::move(output.vtx));
        set->vvc = MakeBytes(std::move(output.vvc));
        set->phy = MakeBytes(std::move(output.phy));
        if (!set->mdl || !set->vvd || !set->vtx) {
            ReportFailure(stem, "the converter produced an empty MDL, VVD or VTX");
            StoreBlob(blobPath, nullptr);
            return {};
        }
        m_converted.fetch_add(1, std::memory_order_relaxed);
        RecordBuild(started);
        StoreBlob(blobPath, set.get());
        return set;
    }

    void RecordBuild(std::chrono::steady_clock::time_point started) const
    {
        const auto signedElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        const auto elapsedUs = static_cast<std::uint64_t>(std::max<std::int64_t>(signedElapsed, 0));
        m_buildTotalUs.fetch_add(elapsedUs, std::memory_order_relaxed);
        std::uint64_t previousMax = m_buildMaxUs.load(std::memory_order_relaxed);
        while (elapsedUs > previousMax
            && !m_buildMaxUs.compare_exchange_weak(previousMax, elapsedUs, std::memory_order_relaxed)) {
        }
    }

    void ReportFailure(const std::string& stem, const char* reason) const
    {
        m_failed.fetch_add(1, std::memory_order_relaxed);
        char message[768]{};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "R1Delta: R2 model provider could not convert %s.mdl: %s\n", stem.c_str(),
            reason && reason[0] ? reason : "unknown error");
        LogModelMessage(message);
    }

    void RegisterMaterials(const ModelSet& set) const
    {
        if (IsDedicatedServer() || set.materials.empty())
            return;
        PrefetchR2ModelMaterials(set.materials);
    }

    void PrefetchLoop()
    {
        for (;;) {
            PrefetchJob job;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_queueAvailable.wait(lock, [this]() { return m_stopping || !m_jobs.empty(); });
                if (m_stopping)
                    return;
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
            }
            try {
                RunPrefetch(job);
            }
            catch (...) {
                LogModelMessage("R1Delta: R2 model prefetch failed unexpectedly\n");
            }
        }
    }

    void RunPrefetch(const PrefetchJob& job)
    {
        const auto started = std::chrono::steady_clock::now();
        std::vector<std::string> names = StaticPropModelNames(*job.bsp);
        const std::size_t propNames = names.size();

        const std::filesystem::path install = GetR2InstallDirectory();
        std::size_t entityFiles = 0;
        if (!install.empty()) {
            const std::string package =
                (install / "vpk" / ("englishclient_" + job.map + ".bsp.pak000_dir.vpk")).string();
            for (const char* suffix : kEntitySuffixes) {
                const std::string path = "maps/" + job.map + suffix;
                if (const FileBytes entities = R1OVPK_ReadDirectoryVPKFile(package.c_str(), path.c_str())) {
                    AppendEntityModelNames(*entities, names);
                    ++entityFiles;
                }
            }
        }

        std::unordered_set<std::string> seen;
        std::vector<std::string> stems;
        for (const std::string& name : names) {
            std::string normalized = LowerAscii(name);
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            if (!EndsWith(normalized, ".mdl"))
                continue;
            normalized.resize(normalized.size() - 4);
            if (seen.insert(normalized).second)
                stems.push_back(std::move(normalized));
        }

        // Entities can name a model the R2 install does not have; GetSet reports those as absent.
        std::size_t available = 0;
        for (const std::string& stem : stems) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_stopping)
                    return;
            }
            // The active map is set when the BSP is served, before this job runs.
            if (const auto set = GetSet(stem)) {
                ++available;
                RegisterMaterials(*set);
            }
        }

        const auto elapsedMs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
        m_prefetchModels.store(available, std::memory_order_relaxed);
        m_prefetchMs.store(elapsedMs, std::memory_order_relaxed);
        char message[512]{};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "R1Delta: R2 model prefetch map=%s static-prop-names=%zu entity-files=%zu unique-models=%zu converted-or-cached=%zu time=%llu ms\n",
            job.map.c_str(), propNames, entityFiles, stems.size(), available,
            static_cast<unsigned long long>(elapsedMs));
        LogModelMessage(message);
        LogMetrics("model-prefetch-done");
    }

    mutable std::mutex m_mutex;
    mutable std::condition_variable m_entryDone;
    mutable std::string m_map;
    mutable std::unordered_map<std::string, std::shared_ptr<Entry>> m_entries;
    mutable std::vector<Package> m_packages;
    std::condition_variable m_queueAvailable;
    std::deque<PrefetchJob> m_jobs;
    bool m_stopping{};
    std::thread m_worker;

    mutable std::atomic<std::uint64_t> m_converted{};
    mutable std::atomic<std::uint64_t> m_diskHits{};
    mutable std::atomic<std::uint64_t> m_failed{};
    mutable std::atomic<std::uint64_t> m_absent{};
    mutable std::atomic<std::uint64_t> m_r1Owned{};
    mutable std::atomic<std::uint64_t> m_buildTotalUs{};
    mutable std::atomic<std::uint64_t> m_buildMaxUs{};
    std::atomic<std::uint64_t> m_filesServed{};
    std::atomic<std::uint64_t> m_prefetchModels{};
    std::atomic<std::uint64_t> m_prefetchMs{};
    std::atomic<bool> m_loggedServed{};
};

std::shared_ptr<R2ModelProvider> g_modelProvider;
std::once_flag g_modelProviderRegistration;

} // namespace

void RegisterR2ModelProvider()
{
    std::call_once(g_modelProviderRegistration, []() {
        // Differential switch for crash triage: R1DELTA_R2PORT_NO_MODELS=1 serves no R2 models.
        char disabled[8]{};
        if (GetEnvironmentVariableA("R1DELTA_R2PORT_NO_MODELS", disabled, sizeof(disabled)) && disabled[0] == '1')
            return;
        auto provider = std::make_shared<R2ModelProvider>();
        if (RegisterProvider("models", provider))
            g_modelProvider = std::move(provider);
    });
}

void PrefetchR2MapModels(std::string_view mapName, FileBytes convertedBsp)
{
    if (g_modelProvider)
        g_modelProvider->Prefetch(mapName, std::move(convertedBsp));
}

void LogR2ModelProviderMetrics(const char* phase)
{
    if (g_modelProvider)
        g_modelProvider->LogMetrics(phase);
}

} // namespace r2port
