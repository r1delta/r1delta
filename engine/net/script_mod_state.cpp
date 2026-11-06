#include "script_mod_state.h"

#include "core.h"
#include "crash_report_context.h"
#include "filecache.h"
#include "logging.h"

#include <Windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ScriptModState
{
namespace
{

using namespace ScriptModding;

constexpr std::size_t kMaxManifestBytes = 4 * 1024 * 1024;
constexpr std::size_t kHashChunkBytes = 256 * 1024;
constexpr std::size_t kMaxListedFiles = 6;
constexpr std::size_t kMaxListedModules = 8;
constexpr int kAddonSnapshotAttempts = 20;
constexpr DWORD kAddonSnapshotRetryMs = 25;

struct ModuleVerdict
{
    ModuleInfo module;
    ModuleKind kind = ModuleKind::System;
};

struct ProcessFacts
{
    bool manifestAvailable = false;
    std::string manifestError;
    Manifest manifest;
    ReasonMask reasons = 0;
    std::size_t contentChanged = 0;
    std::size_t contentAdded = 0;
    std::size_t contentMissing = 0;
    std::vector<std::string> contentExamples;
    std::vector<ModuleVerdict> modules; // every loaded non-system DLL
    std::string systemRoot;
    std::string gameRoot;
};

std::once_flag g_scanOnce;
std::once_flag g_warmOnce;
ProcessFacts* g_facts = nullptr;
// Published once, never freed: the crash handler reads it without any lock.
std::atomic<const ProcessFacts*> g_published{ nullptr };

class Sha256Hasher
{
public:
    Sha256Hasher()
    {
        if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            algorithm_ = nullptr;
        buffer_.resize(kHashChunkBytes);
    }
    ~Sha256Hasher()
    {
        if (algorithm_)
            BCryptCloseAlgorithmProvider(algorithm_, 0);
    }
    Sha256Hasher(const Sha256Hasher&) = delete;
    Sha256Hasher& operator=(const Sha256Hasher&) = delete;

    // Lowercase hex SHA-256 of the whole file; false when it cannot be read.
    bool HashFile(const std::filesystem::path& path, std::string& hex)
    {
        if (!algorithm_)
            return false;
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        BCRYPT_HASH_HANDLE hash = nullptr;
        bool ok = BCryptCreateHash(algorithm_, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
        while (ok) {
            DWORD read = 0;
            if (!ReadFile(file, buffer_.data(), static_cast<DWORD>(buffer_.size()), &read, nullptr)) {
                ok = false;
                break;
            }
            if (read == 0)
                break;
            ok = BCryptHashData(hash, buffer_.data(), read, 0) >= 0;
        }
        std::array<unsigned char, 32> digest{};
        if (ok)
            ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
        if (hash)
            BCryptDestroyHash(hash);
        CloseHandle(file);
        if (!ok)
            return false;

        constexpr char digits[] = "0123456789abcdef";
        hex.clear();
        hex.reserve(digest.size() * 2);
        for (const unsigned char byte : digest) {
            hex.push_back(digits[byte >> 4]);
            hex.push_back(digits[byte & 15]);
        }
        return true;
    }

private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    std::vector<unsigned char> buffer_;
};

std::string WideToUtf8(const std::wstring& wide)
{
    if (wide.empty())
        return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);
    if (length <= 0)
        return {};
    std::string utf8(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
        utf8.data(), length, nullptr, nullptr);
    return utf8;
}

std::string PathToString(const std::filesystem::path& path)
{
    return WideToUtf8(path.wstring());
}

bool ReadManifest(const std::filesystem::path& path, Manifest& manifest, std::string& error)
{
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        error = "not found";
        return false;
    }
    if (size > kMaxManifestBytes) {
        error = "file too large";
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!stream.read(text.data(), static_cast<std::streamsize>(text.size()))) {
        error = "unreadable";
        return false;
    }
    return ParseManifest(text, manifest, &error);
}

std::string FormatExampleList(const std::vector<std::string>& examples, std::size_t total)
{
    std::string text;
    for (const std::string& example : examples) {
        if (!text.empty())
            text += ", ";
        text += example;
    }
    if (total > examples.size())
        text += ", ... " + std::to_string(total - examples.size()) + " more";
    return text;
}

// Walks the content directories the manifest covers and counts differences in both directions.
void ScanContent(const std::filesystem::path& base, Sha256Hasher& hasher, ProcessFacts& facts)
{
    std::unordered_set<std::string> seen;
    std::size_t differing = 0;
    const auto note = [&](const std::string& relative) {
        ++differing;
        if (facts.contentExamples.size() < kMaxListedFiles)
            facts.contentExamples.push_back(relative);
    };

    for (const char* directory : { "scripts/vscripts", "resource" }) {
        std::error_code ec;
        std::filesystem::recursive_directory_iterator iterator(base / directory,
            std::filesystem::directory_options::skip_permission_denied, ec);
        for (; !ec && iterator != std::filesystem::recursive_directory_iterator();
             iterator.increment(ec)) {
            if (!iterator->is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }
            const std::string relative = NormalizePath(
                PathToString(iterator->path().lexically_relative(base)));
            if (!IsTrackedContentPath(relative))
                continue;

            seen.insert(relative);
            std::string hash;
            if (!hasher.HashFile(iterator->path(), hash))
                continue;
            switch (CompareContentFile(facts.manifest, relative, hash)) {
            case FileState::Modified:
                ++facts.contentChanged;
                note(relative);
                break;
            case FileState::Added:
                ++facts.contentAdded;
                note(relative);
                break;
            default:
                break;
            }
        }
    }

    for (const auto& entry : facts.manifest.content) {
        if (seen.contains(entry.first))
            continue;
        ++facts.contentMissing;
        note(entry.first);
    }
    if (differing != 0)
        facts.reasons |= kReasonCore;
}

std::vector<ModuleInfo> EnumerateModules()
{
    std::vector<ModuleInfo> modules;
    const HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE)
        return modules;

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Module32FirstW(snapshot, &entry); more; more = Module32NextW(snapshot, &entry)) {
        const std::string name = WideToUtf8(entry.szModule);
        if (name.size() > 4 && _stricmp(name.c_str() + name.size() - 4, ".exe") == 0)
            continue;
        modules.push_back({ name, WideToUtf8(entry.szExePath) });
    }
    CloseHandle(snapshot);
    return modules;
}

std::string WindowsDirectory()
{
    std::array<wchar_t, MAX_PATH> buffer{};
    const UINT length = GetWindowsDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    return length ? NormalizePath(WideToUtf8(std::wstring(buffer.data(), length))) : std::string();
}

ModuleContext MakeModuleContext(const ProcessFacts& facts, Sha256Hasher* hasher)
{
    ModuleContext context;
    context.systemRoot = facts.systemRoot;
    context.gameRoot = facts.gameRoot;
    context.manifest = facts.manifestAvailable ? &facts.manifest : nullptr;
    if (hasher) {
        context.hashFile = [hasher](const std::string& path, std::string& hex) {
            std::wstring wide(static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, 0, path.data(),
                static_cast<int>(path.size()), nullptr, 0)), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()),
                wide.data(), static_cast<int>(wide.size()));
            return hasher->HashFile(wide, hex);
        };
    }
    return context;
}

void ScanModules(Sha256Hasher& hasher, ProcessFacts& facts)
{
    const ModuleContext context = MakeModuleContext(facts, &hasher);
    for (const ModuleInfo& module : EnumerateModules()) {
        const ModuleKind kind = ClassifyModule(module, context);
        if (kind == ModuleKind::System)
            continue;
        facts.modules.push_back({ module, kind });
        if (IsNativeModdingKind(kind))
            facts.reasons |= kReasonNative;
    }
}

const char* KindLabel(ModuleKind kind)
{
    switch (kind) {
    case ModuleKind::Modified: return "R1Delta module changed since release";
    case ModuleKind::ForeignGame: return "in the game folder";
    case ModuleKind::ForeignOther: return "outside the game folder";
    case ModuleKind::Unverifiable: return "unverified";
    default: return "";
    }
}

void ComputeProcessFacts()
{
    auto* facts = new ProcessFacts();
    try {
        const std::filesystem::path base = FileCache::GetInstance().GetBaseDirectory();
        facts->systemRoot = WindowsDirectory();
        facts->gameRoot = NormalizePath(PathToString(GetExecutableDirectory()));
        facts->manifestAvailable = ReadManifest(base / std::string(kManifestFileName),
            facts->manifest, facts->manifestError);

        Sha256Hasher hasher;
        if (facts->manifestAvailable)
            ScanContent(base, hasher, *facts);
        ScanModules(hasher, *facts);
    }
    catch (...) {
        facts->manifestAvailable = false;
        facts->manifestError = "scan failed";
    }
    g_facts = facts;
    g_published.store(facts, std::memory_order_release);

    std::string nativeModules;
    for (const ModuleVerdict& verdict : facts->modules) {
        if (!IsNativeModdingKind(verdict.kind))
            continue;
        if (!nativeModules.empty())
            nativeModules += ", ";
        nativeModules += verdict.module.name + " (" + KindLabel(verdict.kind) + ")";
    }
    // The engine's Msg formatter has no %zu: counts go out as unsigned int.
    Msg("Script modding scan: manifest %s%s; r1delta tree %u changed, %u added, %u missing; "
        "%u non-system modules, native modules changed or foreign: %s.\n",
        facts->manifestAvailable ? "ok" : "unavailable",
        facts->manifestAvailable ? "" : (" (" + facts->manifestError + ")").c_str(),
        static_cast<unsigned>(facts->contentChanged), static_cast<unsigned>(facts->contentAdded),
        static_cast<unsigned>(facts->contentMissing), static_cast<unsigned>(facts->modules.size()),
        nativeModules.empty() ? "none" : nativeModules.c_str());
}

const ProcessFacts& EnsureProcessFacts()
{
    std::call_once(g_scanOnce, ComputeProcessFacts);
    return *g_facts;
}

std::vector<std::string> SnapshotEnabledAddons()
{
    static std::mutex mutex;
    static std::vector<std::string> lastKnown;

    std::vector<std::string> addons;
    for (int attempt = 0; attempt < kAddonSnapshotAttempts; ++attempt) {
        if (FileCache::GetInstance().TrySnapshotEnabledAddons(addons)) {
            std::lock_guard<std::mutex> lock(mutex);
            lastKnown = addons;
            return addons;
        }
        Sleep(kAddonSnapshotRetryMs);
    }
    std::lock_guard<std::mutex> lock(mutex);
    return lastKnown;
}

struct CachedHash
{
    std::filesystem::file_time_type writeTime;
    std::uintmax_t size = 0;
    std::string hex;
};

std::mutex g_hashCacheMutex;
std::unordered_map<std::string, CachedHash> g_hashCache;

// SHA-256 of base/relative, reusing the previous digest while the file's time and size are unchanged.
bool HashCoreFile(Sha256Hasher& hasher, const std::filesystem::path& base,
    const std::string& relative, std::string& hex)
{
    const std::filesystem::path path = base / relative;
    std::error_code ec;
    const auto writeTime = std::filesystem::last_write_time(path, ec);
    if (ec)
        return false;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec)
        return false;

    {
        std::lock_guard<std::mutex> lock(g_hashCacheMutex);
        const auto cached = g_hashCache.find(relative);
        if (cached != g_hashCache.end() && cached->second.writeTime == writeTime
            && cached->second.size == size) {
            hex = cached->second.hex;
            return true;
        }
    }
    if (!hasher.HashFile(path, hex))
        return false;
    std::lock_guard<std::mutex> lock(g_hashCacheMutex);
    g_hashCache[relative] = { writeTime, size, hex };
    return true;
}

} // namespace

void Warm() noexcept
{
    try {
        std::call_once(g_warmOnce, [] {
            std::thread([] {
                RegisterR1DeltaThread("ScriptModScan");
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                try {
                    EnsureProcessFacts();
                }
                catch (...) {
                }
            }).detach();
        });
    }
    catch (...) {
    }
}

Classification Classify(std::vector<ScriptErrorTelemetry::Frame>& frames) noexcept
{
    Classification result;
    try {
        const ProcessFacts& facts = EnsureProcessFacts();
        result.manifestAvailable = facts.manifestAvailable;
        result.reasons = facts.reasons;

        const std::vector<std::string> addons = SnapshotEnabledAddons();
        if (!addons.empty())
            result.reasons |= kReasonAddons;

        const std::filesystem::path base = FileCache::GetInstance().GetBaseDirectory();
        Sha256Hasher hasher;
        FrameFacts frameFacts;
        frameFacts.addonProvides = [&addons](const std::string& relative) {
            std::error_code ec;
            return std::any_of(addons.begin(), addons.end(), [&](const std::string& directory) {
                return std::filesystem::is_regular_file(std::filesystem::path(directory) / relative, ec);
            });
        };
        if (facts.manifestAvailable) {
            frameFacts.hashCoreFile = [&](const std::string& relative, std::string& hex) {
                return HashCoreFile(hasher, base, relative, hex);
            };
        }

        for (ScriptErrorTelemetry::Frame& frame : frames) {
            const FrameVerdict verdict = ClassifyFrame(facts.manifest, frame.source, frameFacts);
            frame.modded = verdict.modded;
            result.reasons |= verdict.reasons;
        }
    }
    catch (...) {
    }
    return result;
}

std::string GetCrashReportText() noexcept
{
    try {
        const ProcessFacts* facts = g_published.load(std::memory_order_acquire);
        if (!facts)
            return "Modded: unknown (startup scan has not finished)\n";

        ReasonMask reasons = facts->reasons;
        std::vector<std::string> addons;
        const bool addonsKnown = FileCache::GetInstance().TrySnapshotEnabledAddons(addons);
        if (addonsKnown && !addons.empty())
            reasons |= kReasonAddons;

        // Modules loaded since the startup scan (late LoadLibrary, injected hooks) are judged by
        // name and location only: the crash handler does not hash DLLs.
        std::unordered_map<std::string, ModuleKind> known;
        for (const ModuleVerdict& verdict : facts->modules)
            known[NormalizePath(verdict.module.path)] = verdict.kind;
        const ModuleContext context = MakeModuleContext(*facts, nullptr);

        std::vector<ModuleVerdict> foreign;
        for (const ModuleInfo& module : EnumerateModules()) {
            const auto scanned = known.find(NormalizePath(module.path));
            const ModuleKind kind = scanned != known.end() ? scanned->second : ClassifyModule(module, context);
            if (kind == ModuleKind::Modified || kind == ModuleKind::ForeignGame
                || kind == ModuleKind::ForeignOther) {
                foreign.push_back({ module, kind });
            }
            if (IsNativeModdingKind(kind))
                reasons |= kReasonNative;
        }

        std::string text;
        if (!facts->manifestAvailable)
            text += "Modded: unknown for content (manifest unavailable: " + facts->manifestError + ")";
        else
            text += reasons ? "Modded: yes" : "Modded: no";
        if (reasons)
            text += " (" + ReasonText(reasons) + ")";
        text += "\n";

        if (facts->manifestAvailable) {
            text += "R1Delta content: " + std::to_string(facts->contentChanged) + " changed, "
                + std::to_string(facts->contentAdded) + " added, "
                + std::to_string(facts->contentMissing) + " missing vs the release manifest";
            const std::size_t differing = facts->contentChanged + facts->contentAdded + facts->contentMissing;
            if (differing != 0)
                text += " [" + FormatExampleList(facts->contentExamples, differing) + "]";
            text += "\n";
        }

        text += "Foreign native modules: ";
        if (foreign.empty()) {
            text += "none\n";
        }
        else {
            const std::size_t listed = std::min(foreign.size(), kMaxListedModules);
            for (std::size_t i = 0; i < listed; ++i) {
                if (i != 0)
                    text += "; ";
                text += foreign[i].module.path + " (" + KindLabel(foreign[i].kind) + ")";
            }
            if (foreign.size() > listed)
                text += "; ... " + std::to_string(foreign.size() - listed) + " more";
            text += "\n";
        }
        return text;
    }
    catch (...) {
        return "Modded: unknown (report text failed)\n";
    }
}

} // namespace ScriptModState
