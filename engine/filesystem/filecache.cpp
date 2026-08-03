// filecache.cpp
#include "filecache.h"
#include "addonlist_state.h"
#include <thread>   // For std::thread if UpdateCache runs in background
#include <iostream> // For std::cerr (replace with Msg/Warning)

#include "logging.h"
#include "tctx.h"

FileCache::FileCache() {
    cacheMutex = SRWLOCK_INIT;
    cacheCondition = CONDITION_VARIABLE_INIT;
    executableDirectory = GetExecutableDirectory();
    if (executableDirectory.empty()) {
        // Handle error: Cannot proceed without executable path
        Warning("Failed to get executable directory! FileCache disabled.\n");
        // Potentially throw or set an error state
        scanUnavailable = true;
        initialized.store(true, std::memory_order_release); // Prevent waits if unusable
        return;
    }
    r1deltaBasePath = executableDirectory / "r1delta";
    r1deltaBasePath.make_preferred();
    r1deltaBasePathHash = fnv1a_hash(r1deltaBasePath);
}


void FileCache::ScanDirectory(const std::filesystem::path& directory,
    std::unordered_set<std::size_t>& currentCache) {
    ZoneScoped;
    std::error_code ec; // To check filesystem errors without exceptions

    // Check if directory exists first
    if (!std::filesystem::is_directory(directory, ec) || ec) {
        if (ec) {
            // Log specific error if exists
            Warning("Error accessing directory '%s': %s\n", directory.string().c_str(), ec.message().c_str());
        }
        else if (directory == r1deltaBasePath) {
            // Only warn if the primary base directories don't exist
            Msg("Directory '%s' not found for scanning.\n", directory.string().c_str());
        }
        return; // Don't try to iterate if it doesn't exist or isn't accessible
    }


    for (const auto& entry : std::filesystem::directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            Warning("Error iterating directory '%s': %s\n", directory.string().c_str(), ec.message().c_str());
            ec.clear(); // Try to continue with next entries
            continue;
        }

        std::filesystem::path currentPath = entry.path();
        currentPath.make_preferred(); // Normalize path separators (e.g., \ on Windows)
        std::string normalizedPathString = currentPath.string();

        if (entry.is_regular_file(ec) && !ec) {
            // Hash the *absolute, normalized* path string
            currentCache.insert(fnv1a_hash(normalizedPathString));
        }
        else if (entry.is_directory(ec) && !ec) {
            if (directory == r1deltaBasePath
                && ::_stricmp(currentPath.filename().string().c_str(), "addons") == 0)
                continue;
            ScanDirectory(currentPath, currentCache);
        }
        else if (ec) {
            Warning("Error checking entry type for '%s': %s\n", normalizedPathString.c_str(), ec.message().c_str());
            ec.clear(); // Try to continue
        }
    }
    if (ec) { // Catch potential error from end of iteration
        Warning("Error finishing iteration of directory '%s': %s\n", directory.string().c_str(), ec.message().c_str());
    }
}

void FileCache::BuildSnapshot(
    const std::filesystem::path& scanAddonRoot,
    std::unordered_set<std::size_t>& newCache,
    std::vector<std::string>& newAddonsFolderCache) {
    std::error_code ec;
    std::filesystem::create_directories(scanAddonRoot, ec);
    if (ec)
        Warning("Could not create directory: %s: %s\n", scanAddonRoot.string().c_str(), ec.message().c_str());

    ScanDirectory(r1deltaBasePath, newCache);

    std::vector<AddonListState::Entry> addonStates;
    const std::filesystem::path addonListPath = scanAddonRoot.parent_path() / "addonlist.txt";
    if (!AddonListState::Load(addonListPath, addonStates)) {
        Msg("Addon list '%s' is missing or invalid; addon replacements are disabled.\n",
            addonListPath.string().c_str());
        return;
    }

    for (const AddonListState::Entry& addon : addonStates) {
        if (!addon.enabled)
            continue;
        if (!AddonListState::IsSafeDirectoryName(addon.name)) {
            Warning("Ignoring unsafe addon directory name '%s'.\n", addon.name.c_str());
            continue;
        }

        std::filesystem::path addonDirectory = scanAddonRoot / addon.name;
        addonDirectory.make_preferred();
        ec.clear();
        if (!std::filesystem::is_directory(addonDirectory, ec)) {
            if (ec)
                Warning("Error accessing addon directory '%s': %s\n",
                    addonDirectory.string().c_str(), ec.message().c_str());
            continue;
        }

        newAddonsFolderCache.push_back(addonDirectory.string());
        ScanDirectory(addonDirectory, newCache);
    }
}

void FileCache::CommitSnapshot(
    uint64_t generation,
    std::unordered_set<std::size_t>&& newCache,
    std::vector<std::string>&& newAddonsFolderCache) {
    cache = std::move(newCache);
    addonsFolderCache = std::move(newAddonsFolderCache);
    publishedGeneration = generation;
    addonsFolderCacheHashes.clear();
    addonsFolderCacheHashes.reserve(addonsFolderCache.size());
    for (const std::string& addonDirectory : addonsFolderCache)
        addonsFolderCacheHashes.push_back(fnv1a_hash(addonDirectory));
    initialized.store(true, std::memory_order_release);
}

bool FileCache::WaitForInitialPublication() {
    AcquireSRWLockShared(&cacheMutex);
    while (!initialized.load(std::memory_order_acquire)) {
        SleepConditionVariableSRW(
            &cacheCondition,
            &cacheMutex,
            INFINITE,
            CONDITION_VARIABLE_LOCKMODE_SHARED);
    }
    const bool available = !scanUnavailable;
    ReleaseSRWLockShared(&cacheMutex);
    return available;
}

void FileCache::UpdateCache() {
    if (executableDirectory.empty()) {
        Warning("FileCache::UpdateCache called but executable directory is unknown. Aborting.\n");
        {
            SRWGuard lock(&cacheMutex);
            scanUnavailable = true;
            initialized.store(true, std::memory_order_release);
        }
        WakeAllConditionVariable(&cacheCondition);
        return;
    }

    while (true) {
        std::filesystem::path scanAddonRoot;
        uint64_t scanGeneration = 0;
        {
            SRWGuard lock(&cacheMutex);
            while (rescanState.IsSuspended())
                SleepConditionVariableSRW(&cacheCondition, &cacheMutex, INFINITE, 0);
            scanGeneration = rescanState.Generation();
            scanAddonRoot = addonRoot;
        }
        if (scanAddonRoot.empty()) {
            std::error_code ec;
            scanAddonRoot = std::filesystem::current_path(ec);
            if (ec) {
                Warning("FileCache could not resolve the current game root: %s\n", ec.message().c_str());
                {
                    SRWGuard lock(&cacheMutex);
                    scanUnavailable = true;
                    initialized.store(true, std::memory_order_release);
                }
                WakeAllConditionVariable(&cacheCondition);
                return;
            }
            scanAddonRoot /= "r1";
            scanAddonRoot /= "addons";
            scanAddonRoot.make_preferred();
        }

        ZoneScopedN("FileCache Rescan");
        std::unordered_set<std::size_t> newCache;
        std::vector<std::string> newAddonsFolderCache;
        BuildSnapshot(scanAddonRoot, newCache, newAddonsFolderCache);

        {
            ZoneScopedN("FileCache update cacheMutex");
            SRWGuard lock(&cacheMutex);
            if (!rescanState.CanPublish(scanGeneration))
                continue;
            CommitSnapshot(
                scanGeneration,
                std::move(newCache),
                std::move(newAddonsFolderCache));
        }
        WakeAllConditionVariable(&cacheCondition);

        ZoneScopedN("FileCache Wait For Rescan");
        AcquireSRWLockExclusive(&cacheMutex);
        while (rescanState.CanPublish(scanGeneration))
            SleepConditionVariableSRW(&cacheCondition, &cacheMutex, INFINITE, 0);
        ReleaseSRWLockExclusive(&cacheMutex);
    }
}

void FileCache::RequestManualRescan()
{
    {
        SRWGuard lock(&cacheMutex);
        rescanState.RequestRescan();
    }
    WakeAllConditionVariable(&cacheCondition);
}

bool FileCache::RefreshAddonSnapshotFromWorkingDirectory()
{
    std::error_code ec;
    std::filesystem::path addonPath = std::filesystem::current_path(ec);
    if (ec) {
        Warning("FileCache could not resolve the current working directory: %s\n", ec.message().c_str());
        return false;
    }

    addonPath /= "r1";
    addonPath /= "addons";
    addonPath.make_preferred();

    BeginAddonSearchPathUpdate();
    const bool published = PublishAddonSearchPathSnapshot(addonPath);
    const bool completed = EndAddonSearchPathUpdate();
    return published && completed;
}

void FileCache::BeginAddonSearchPathUpdate()
{
    WaitForInitialPublication();
    {
        SRWGuard lock(&cacheMutex);
        rescanState.BeginSearchPathUpdate();
    }
    WakeAllConditionVariable(&cacheCondition);
}

bool FileCache::PublishAddonSearchPathSnapshot(const std::filesystem::path& newAddonRoot)
{
    if (newAddonRoot.empty())
        return false;

    std::error_code ec;
    std::filesystem::path scanAddonRoot = std::filesystem::absolute(newAddonRoot, ec);
    if (ec)
        return false;
    scanAddonRoot = scanAddonRoot.lexically_normal();
    scanAddonRoot.make_preferred();

    uint64_t scanGeneration = 0;
    bool suspended = false;
    {
        SRWGuard lock(&cacheMutex);
        addonRoot = scanAddonRoot;
        suspended = rescanState.IsSuspended();
        if (suspended)
            scanGeneration = rescanState.Generation();
        else
            rescanState.RequestRescan();
    }
    if (!suspended) {
        WakeAllConditionVariable(&cacheCondition);
        return false;
    }

    std::unordered_set<std::size_t> newCache;
    std::vector<std::string> newAddonsFolderCache;
    BuildSnapshot(scanAddonRoot, newCache, newAddonsFolderCache);

    {
        SRWGuard lock(&cacheMutex);
        if (!rescanState.IsSuspended()
            || rescanState.Generation() != scanGeneration)
            return false;
        CommitSnapshot(
            scanGeneration,
            std::move(newCache),
            std::move(newAddonsFolderCache));
    }
    WakeAllConditionVariable(&cacheCondition);
    return true;
}

bool FileCache::EndAddonSearchPathUpdate()
{
    uint64_t targetGeneration = 0;
    AddonListState::SearchPathUpdateEnd completion;
    {
        SRWGuard lock(&cacheMutex);
        completion = rescanState.EndSearchPathUpdate();
        if (completion == AddonListState::SearchPathUpdateEnd::Outermost)
            targetGeneration = rescanState.Generation();
    }
    if (completion == AddonListState::SearchPathUpdateEnd::Unmatched) {
        Warning("FileCache addon search-path update completed without a matching begin.\n");
        return false;
    }
    WakeAllConditionVariable(&cacheCondition);
    if (completion == AddonListState::SearchPathUpdateEnd::Nested)
        return false;

    AcquireSRWLockExclusive(&cacheMutex);
    while (!scanUnavailable && publishedGeneration < targetGeneration)
        SleepConditionVariableSRW(&cacheCondition, &cacheMutex, INFINITE, 0);
    const bool published = !scanUnavailable && publishedGeneration >= targetGeneration;
    ReleaseSRWLockExclusive(&cacheMutex);
    if (!published)
        Warning("FileCache could not publish the final addon search-path generation.\n");
    return published;
}

// filecache.cpp (continued)

bool FileCache::TryReplaceFile(const char* pszRelativeFilePath) {
    ZoneScoped;

    // Ensure cache is initialized and ready
    if (!initialized.load(std::memory_order_acquire) && !WaitForInitialPublication())
        return false;

    // NOTE(mrsteyk): R1DAssert macro still evaluates the expression.
#if BUILD_DEBUG
    R1DAssert(initialized.load(std::memory_order_acquire));
#endif


    if (!pszRelativeFilePath || *pszRelativeFilePath == '\0') {
        return false; // Invalid input
    }

    // NOTE(mrsteyk): part of is_absolute check without dumb fluff. Read `is_absolute` code to find out more.
    //if ((pszRelativeFilePath[0] == '/' || pszRelativeFilePath[0] == '\\') && (pszRelativeFilePath[1] == '/' || pszRelativeFilePath[1] == '\\'))
    if (pszRelativeFilePath[0] == '/' || pszRelativeFilePath[0] == '\\')
    {
        return false;
    }
    if (pszRelativeFilePath[1] == ':')
    {
        return false;
    }

    // NOTE(mrsteyk): operate under the assumption that game also uses 260 byte paths, as evident by the original.
    //                this issue already manifested itself long ago and I swear I fixed it, idk why it popped up again.
    //                also please stop using Gemini to rewrite everything, it's annoying.
    bool found_null = false;
    size_t pszRelativeFilePath_len = 0;
    size_t pszRelativeFilePath_parts = 1;
    {
        ZoneScopedN("TryReplaceFile>260+Null Check");
        for (size_t i = 0; i < 260; ++i)
        {
            if (pszRelativeFilePath[i] == 0)
            {
                found_null = true;
                pszRelativeFilePath_len = i;
                break;
            }
            else if (pszRelativeFilePath[i] == '/' || pszRelativeFilePath[i] == '\\')
            {
                pszRelativeFilePath_parts++;
            }
            // NOTE(mrsteyk): do not handle unicode in replacement names.
            else if (!isprint((unsigned char)pszRelativeFilePath[i])) // if (pszRelativeFilePath[i] < 0)
            {
                return false;
            }
        }
    }
    if (!found_null)
    {
        return false;
    }
    if (pszRelativeFilePath[pszRelativeFilePath_len - 1] == '/' || pszRelativeFilePath[pszRelativeFilePath_len - 1] == '\\')
    {
        return false;
    }

    Arena* arena = tctx.get_arena_for_scratch();
    TempArena temp = TempArena(arena);

    struct S8 {
        const char* ptr;
        size_t size;
    };
    S8* path_parts = (S8*)arena_push(arena, sizeof(*path_parts) * pszRelativeFilePath_parts);
    size_t path_parts_idx = 0;

    const char* path_part_curr = pszRelativeFilePath;
    size_t path_part_size = 0;
    for (size_t i = 0; i < pszRelativeFilePath_len; ++i)
    {
        if (pszRelativeFilePath[i] == '/' || pszRelativeFilePath[i] == '\\')
        {
            R1DAssert(path_part_size > 0);
            if (path_parts_idx == 0 && path_part_size == 1 && path_part_curr[0] == '*')
            {
                // SKIP
            }
            else if (path_part_size == 1 && path_part_curr[0] == '.')
            {
                // SKIP
            }
            else if (path_parts_idx && path_part_size == 2 && path_part_curr[0] == '.' && path_part_curr[1] == '.')
            {
                // Remove `../` with directory change.
                path_parts_idx--;
            }
            else// if (path_part_size)
            {
                R1DAssert(path_part_size);
                path_parts[path_parts_idx] = { .ptr = path_part_curr, .size = path_part_size };
                path_parts_idx++;
            }
            // NOTE(mrsteyk): Skip separators, because buggy Hammer material names or something.
            do {
                ++i;
            } while (pszRelativeFilePath[i] == '/' || pszRelativeFilePath[i] == '\\');
            path_part_curr = pszRelativeFilePath + i;
            path_part_size = 1;
        }
        else
        {
            path_part_size++;
        }
    }
    if (path_part_size)
    {
        R1DAssert((path_part_curr + path_part_size) <= (pszRelativeFilePath + pszRelativeFilePath_len));
        path_parts[path_parts_idx] = { .ptr = path_part_curr, .size = path_part_size };
        path_parts_idx++;
    }
    if (path_parts_idx == 0)
    {
        return false;
    }
    
    size_t path_size = path_parts_idx;
    for (size_t i = 0; i < path_parts_idx; ++i)
    {
        path_size += path_parts[i].size;
    }
#if BUILD_DEBUG
    // NOTE(mrsteyk): remove conversion middleman.
    wchar_t* path = (wchar_t*)arena_push(arena, sizeof(wchar_t) * path_size);
    size_t path_idx = 0;
    for (size_t i = 0; i < path_parts_idx; ++i)
    {
        for (size_t j = 0; j < path_parts[i].size; ++j)
        {
            path[path_idx + j] = path_parts[i].ptr[j];
        }
        path_idx += path_parts[i].size;
        path[path_idx] = L'\\';
        path_idx++;
    }
    path_idx--;
    path[path_idx] = 0;

    {
        // Use std::filesystem::path for robust joining, handle input slashes automatically
        std::filesystem::path relativePath(pszRelativeFilePath);

        // --- Path normalization and potential prefix stripping ---
        // Standardize: remove leading "./" or ".\\"
        std::wstring pathStr = relativePath.lexically_normal();
        std::wstring_view svFilePath(pathStr);
        if (svFilePath.starts_with(L".\\") || svFilePath.starts_with(L"./")) {
            R1DAssert(!"Unreachable?");
            svFilePath.remove_prefix(2);
        }
        // Handle the "/*/" prefix - convert to view for checking efficiently
        if (svFilePath.starts_with(L"*/") || svFilePath.starts_with(L"*\\")) { // Check both separators
            R1DAssert(!"Unreachable?");
            svFilePath.remove_prefix(2); // Remove only 2 chars
        }
        if (svFilePath.empty())
        {
            return false;
        }
        // Recreate path object from the potentially modified view
        relativePath = std::filesystem::path(svFilePath);
        // Ensure it's still relative after potential normalization/stripping
        if (relativePath.empty() || relativePath.is_absolute()) {
            R1DAssert(!"Unreachable?");
            return false;
        }

        R1DAssert(relativePath.native() == path);
    }
#endif
    // --- Check Paths ---
    { // Shared lock scope for reading cache and addonsFolderCache
        ZoneScopedN("TryReplaceFile>Check Paths");
        SRWGuardShared lock(&cacheMutex);

        // 1. Check in r1delta base directory
        // std::filesystem::path potentialBasePath = r1deltaBasePath / relativePath;
        //potentialBasePath.make_preferred(); // Normalize separators

        auto baseHash = r1deltaBasePathHash;
        for (size_t i = 0; i < path_parts_idx; ++i)
        {
            baseHash = fnv1a_hash(std::string_view("\\", 1), baseHash);
            baseHash = fnv1a_hash(std::string_view(path_parts[i].ptr, path_parts[i].size), baseHash);
        }

        if (cache.contains(baseHash)) {
            return true;
        }

        // 2. Check in each addon directory
        for (const std::size_t addonDirStringHash : addonsFolderCacheHashes) {
            std::size_t addonHash = addonDirStringHash;
            for (size_t i = 0; i < path_parts_idx; ++i)
            {
                addonHash = fnv1a_hash(std::string_view("\\", 1), addonHash);
                addonHash = fnv1a_hash(std::string_view(path_parts[i].ptr, path_parts[i].size), addonHash);
            }

            if (cache.contains(addonHash)) {
                return true;
            }
        }
    } // Release shared lock

    return false; // Not found in any checked location
}

FileCache::ReadLease FileCache::AcquireReadLease() {
    if (!initialized.load(std::memory_order_acquire) && !WaitForInitialPublication())
        return ReadLease();
    return ReadLease(this, &cacheMutex);
}

bool FileCache::ResolveReplacementFile(
    const ReadLease& lease,
    const char* pszRelativeFilePath,
    char* pszResolvedPath,
    size_t resolvedPathSize) {
    if (lease.owner != this || !lease.lock
        || !pszRelativeFilePath || !pszRelativeFilePath[0]
        || !pszResolvedPath || resolvedPathSize == 0) {
        return false;
    }

    pszResolvedPath[0] = '\0';
    std::filesystem::path relativePath(pszRelativeFilePath);
    relativePath = relativePath.lexically_normal();
    if (relativePath.empty() || relativePath.is_absolute()) {
        return false;
    }

    auto component = relativePath.begin();
    if (component != relativePath.end() && *component == "*") {
        relativePath = relativePath.lexically_relative("*");
    }
    if (relativePath.empty() || relativePath.is_absolute()) {
        return false;
    }
    for (const auto& part : relativePath) {
        if (part == "..") {
            return false;
        }
    }

    auto copyRegularFile = [&](const std::filesystem::path& root) {
        std::error_code ec;
        std::filesystem::path candidate = root / relativePath;
        candidate.make_preferred();
        if (!std::filesystem::is_regular_file(candidate, ec) || ec) {
            return false;
        }

        const std::string path = candidate.string();
        if (path.size() + 1 > resolvedPathSize) {
            return false;
        }
        memcpy(pszResolvedPath, path.c_str(), path.size() + 1);
        return true;
    };

    if (copyRegularFile(r1deltaBasePath))
        return true;
    for (const std::string& addonDirectory : addonsFolderCache) {
        if (copyRegularFile(addonDirectory))
            return true;
    }
    return false;
}

std::vector<std::string> FileCache::FindReplacementFileNames(
    const char* pszRelativeDirectory,
    const char* pszPattern) {
    std::vector<std::string> results;
    if (!pszRelativeDirectory || !pszRelativeDirectory[0]
        || !pszPattern || !pszPattern[0]) {
        return results;
    }

    if (!initialized.load(std::memory_order_acquire) && !WaitForInitialPublication())
        return results;

    std::filesystem::path relativeDirectory(pszRelativeDirectory);
    relativeDirectory = relativeDirectory.lexically_normal();
    if (relativeDirectory.empty() || relativeDirectory.is_absolute()) {
        return results;
    }
    for (const auto& part : relativeDirectory) {
        if (part == "..") {
            return results;
        }
    }

    const std::string pattern(pszPattern);
    const size_t wildcard = pattern.find('*');
    const std::string prefix = pattern.substr(0, wildcard);
    const std::string suffix =
        wildcard == std::string::npos ? std::string() : pattern.substr(wildcard + 1);
    const auto matchesPattern = [&](const std::string& name) {
        if (wildcard == std::string::npos)
            return _stricmp(name.c_str(), pattern.c_str()) == 0;
        if (name.size() < prefix.size() + suffix.size())
            return false;
        return _strnicmp(name.c_str(), prefix.c_str(), prefix.size()) == 0
            && _stricmp(name.c_str() + name.size() - suffix.size(), suffix.c_str()) == 0;
    };

    std::unordered_set<std::string, HashStrings, std::equal_to<>> seen;
    const auto scanRoot = [&](const std::filesystem::path& root) {
        std::error_code ec;
        const std::filesystem::path directory = root / relativeDirectory;
        if (!std::filesystem::is_directory(directory, ec) || ec)
            return;

        for (const auto& entry : std::filesystem::directory_iterator(
            directory,
            std::filesystem::directory_options::skip_permission_denied,
            ec)) {
            if (ec)
                break;
            if (!entry.is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }
            const std::string name = entry.path().filename().string();
            if (!matchesPattern(name))
                continue;

            std::string folded = name;
            std::transform(
                folded.begin(),
                folded.end(),
                folded.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (seen.insert(folded).second)
                results.push_back(name);
        }
    };

    {
        SRWGuardShared lock(&cacheMutex);
        scanRoot(r1deltaBasePath);
        for (const std::string& addonDirectory : addonsFolderCache)
            scanRoot(addonDirectory);
    }

    std::sort(
        results.begin(),
        results.end(),
        [](const std::string& lhs, const std::string& rhs) {
            return _stricmp(lhs.c_str(), rhs.c_str()) < 0;
        });
    return results;
}

// filecache.cpp (continued)

#if 0
bool FileCache::FileExists(const std::string& filePath) {
    ZoneScoped;

    // If the cache might contain relative paths too (it shouldn't with the current scan logic),
    // you might need path normalization here as well. Assuming filePath is expected to be
    // an absolute, normalized path for direct lookup.
    std::filesystem::path checkPath = filePath;
    checkPath.make_preferred(); // Ensure consistent format with cached paths
    std::size_t hashedPath = fnv1a_hash(checkPath);

    // Acquire lock, wait for initialization if needed, then check cache
    AcquireSRWLockShared(&cacheMutex);

    // Wait ONLY if not initialized yet.
    while (!initialized.load(std::memory_order_acquire))
    {
        SleepConditionVariableSRW(&cacheCondition, &cacheMutex, INFINITE, CONDITION_VARIABLE_LOCKMODE_SHARED);
    }

    // Initialized is true and we hold the lock
    auto ret = cache.contains(hashedPath);
    ReleaseSRWLockShared(&cacheMutex);

    return ret;
}
#endif
