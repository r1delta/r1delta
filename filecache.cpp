// filecache.cpp
#include "filecache.h"
#include <thread>   // For std::thread if UpdateCache runs in background
#include <iostream> // For std::cerr (replace with Msg/Warning)

#include "logging.h"

FileCache::FileCache() {
    executableDirectory = GetExecutableDirectory();
    if (executableDirectory.empty()) {
        // Handle error: Cannot proceed without executable path
        Warning("Failed to get executable directory! FileCache disabled.\n");
        // Potentially throw or set an error state
        initialized.store(true, std::memory_order_release); // Prevent waits if unusable
        return;
    }
    r1deltaBasePath = executableDirectory / "r1delta";
    r1deltaAddonsPath = std::filesystem::current_path() / "r1" / "addons";

    // Optionally start the UpdateCache thread here
    // std::thread(&FileCache::UpdateCache, this).detach();
}


void FileCache::ScanDirectory(const std::filesystem::path& directory,
    std::unordered_set<std::size_t>& currentCache,
    std::unordered_set<std::string, HashStrings, std::equal_to<>>* currentAddonsFolders) {
    ZoneScoped;
    std::error_code ec; // To check filesystem errors without exceptions

    // Check if directory exists first
    if (!std::filesystem::is_directory(directory, ec) || ec) {
        if (ec) {
            // Log specific error if exists
            Warning("Error accessing directory '%s': %s\n", directory.string().c_str(), ec.message().c_str());
        }
        else if (&directory == &r1deltaAddonsPath || &directory == &r1deltaBasePath) {
            // Only warn if the primary base directories don't exist
            Msg("Directory '%s' not found for scanning.\n", directory.string().c_str());
        }
        return; // Don't try to iterate if it doesn't exist or isn't accessible
    }


    // Ensure the base addon directory itself is added if requested
    if (currentAddonsFolders && directory == r1deltaAddonsPath) {
        // Store normalized version
        std::filesystem::path normAddonsPath = r1deltaAddonsPath;
        normAddonsPath.make_preferred(); // Use native separators (\ on Windows)
        currentAddonsFolders->insert(normAddonsPath.string());
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
            // If we are scanning the top-level addons directory, add this subdirectory to the addon cache
            if (currentAddonsFolders && std::filesystem::equivalent(directory, r1deltaAddonsPath, ec) && !ec)
            {
                currentAddonsFolders->insert(normalizedPathString);
                // Don't scan recursively into the addon folder itself here,
                // UpdateCache will call ScanDirectory on it separately if needed.
                // If you *want* to allow nested addon folders, remove this continue
                // and the separate ScanDirectory call for addonsDir in UpdateCache might need adjustment.
                // For the typical "flat" addons structure, this is usually correct.
                // Scan its contents below.
            }
            // Always recurse into subdirectories
            ScanDirectory(currentPath, currentCache, nullptr); // Don't pass addonsFolderCache down recursion
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

void FileCache::UpdateCache() {
    // Ensure base paths are valid
    if (executableDirectory.empty()) {
        Warning("FileCache::UpdateCache called but executable directory is unknown. Aborting.\n");
        initialized.store(true, std::memory_order_release); // Mark as "initialized" to unblock waiters, even though unusable
        cacheCondition.notify_all();
        return;
    }

    // Initial scan
    bool firstScan = true;
    while (true) {
        { // Scope for building new caches
            ZoneScopedN("FileCache Rescan");
            std::unordered_set<std::size_t> newCache;
            std::unordered_set<std::string, HashStrings, std::equal_to<>> newAddonsFolderCache;

            //Msg("Starting file cache scan...\n");

            // Create directories if they don't exist (optional, depends on desired behavior)
            std::error_code ec;
            std::filesystem::create_directories(r1deltaAddonsPath, ec);
            if (ec) Warning("Could not create directory: %s: %s\n", r1deltaAddonsPath.string().c_str(), ec.message().c_str());


            // Scan base r1delta directory (excluding addons)
            ScanDirectory(r1deltaBasePath, newCache);

            // Scan addons directory - this collects addon folders AND their files
            ScanDirectory(r1deltaAddonsPath, newCache, &newAddonsFolderCache);

            //Msg("File cache scan complete. Found %zu files, %zu addon folders.\n", newCache.size(), newAddonsFolderCache.size());

            { // Lock scope for swapping caches
                std::unique_lock<std::shared_mutex> lock(cacheMutex);
                cache = std::move(newCache);
                addonsFolderCache = std::move(newAddonsFolderCache);

                // Set initialized only AFTER the first successful scan completes
                if (firstScan) {
                    initialized.store(true, std::memory_order_release);
                    firstScan = false; // Don't notify on subsequent updates unless needed
                }
            } // Unlock mutex before notifying

            // Notify waiters ONLY on the first scan completion
            if (!firstScan) { // This means it was the first scan run
                cacheCondition.notify_all();
            }
        } // End build scope


        // Wait for manual rescan request
        { // Lock scope for waiting
            ZoneScopedN("FileCache Wait For Rescan");
            std::unique_lock<std::shared_mutex> lock(cacheMutex); // Use unique lock for condition variable wait
            cacheCondition.wait(lock, [this]() {
                // Wait until initialized (should be true after first loop) AND manual rescan requested
                return initialized.load(std::memory_order_acquire) &&
                    manualRescanRequested.load(std::memory_order_acquire);
                });

            // Reset the request flag *after* waking up
            manualRescanRequested.store(false, std::memory_order_relaxed); // Relaxed is fine, protected by mutex
        }
    }
}

// filecache.cpp (continued)

bool FileCache::TryReplaceFile(const char* pszRelativeFilePath) {
    ZoneScoped;

    // Ensure cache is initialized and ready
    if (!initialized.load(std::memory_order_acquire)) {
        // Optionally wait here, but could deadlock if UpdateCache fails.
        // Better to have FileExists/TryReplaceFile wait individually.
        // OR return false immediately if not initialized. For simplicity, let's wait like FileExists.
        std::shared_lock<std::shared_mutex> lock(cacheMutex);
        cacheCondition.wait(lock, [this]() { return initialized.load(std::memory_order_acquire); });
        // Lock is re-acquired, proceed below (or re-check initialized state)
    }
    // Re-check after wait just in case of spurious wakeup or error during init
    if (!initialized.load(std::memory_order_acquire)) {
        return false; // Cache never initialized properly
    }


    if (!pszRelativeFilePath || *pszRelativeFilePath == '\0') {
        return false; // Invalid input
    }

    // Use std::filesystem::path for robust joining, handle input slashes automatically
    std::filesystem::path relativePath(pszRelativeFilePath);

    // Immediately return false if the input is somehow absolute already
    if (relativePath.is_absolute()) {
        // Or maybe use V_IsAbsolutePath if it's more specific?
        // if (V_IsAbsolutePath(pszRelativeFilePath)) { return false; }
        return false;
    }

    // --- Path normalization and potential prefix stripping ---
    // Standardize: remove leading "./" or ".\\"
    std::string pathStr = relativePath.lexically_normal().string();
    if (pathStr.starts_with(".\\") || pathStr.starts_with("./")) {
        pathStr.erase(0, 2);
    }
    // Handle the "/*/" prefix - convert to view for checking efficiently
    std::string_view svFilePath(pathStr);
    if (svFilePath.starts_with("*/") || svFilePath.starts_with("*\\")) { // Check both separators
        svFilePath.remove_prefix(2); // Remove only 2 chars
    }
    // Recreate path object from the potentially modified view
    relativePath = std::filesystem::path(svFilePath);
    // Ensure it's still relative after potential normalization/stripping
    if (relativePath.empty() || relativePath.is_absolute()) {
        return false;
    }

    // --- Check Paths ---
    { // Shared lock scope for reading cache and addonsFolderCache
        std::shared_lock<std::shared_mutex> lock(cacheMutex);

        // 1. Check in r1delta base directory
        std::filesystem::path potentialBasePath = r1deltaBasePath / relativePath;
        potentialBasePath.make_preferred(); // Normalize separators
        std::size_t baseHash = fnv1a_hash(potentialBasePath.string());
        if (cache.contains(baseHash)) {
            return true;
        }

        // 2. Check in each addon directory
        for (const std::string& addonDirString : addonsFolderCache) {
            // addonDirString is already absolute and normalized from ScanDirectory
            std::filesystem::path potentialAddonPath = std::filesystem::path(addonDirString) / relativePath;
            // No need to call make_preferred again IF addonDirString is already preferred AND relativePath uses preferred.
            // But calling it again is safe and ensures consistency.
            potentialAddonPath.make_preferred();
            std::size_t addonHash = fnv1a_hash(potentialAddonPath.string());
            if (cache.contains(addonHash)) {
                return true;
            }
        }
    } // Release shared lock

    return false; // Not found in any checked location
}

// filecache.cpp (continued)

bool FileCache::FileExists(const std::string& filePath) {
    // If the cache might contain relative paths too (it shouldn't with the current scan logic),
    // you might need path normalization here as well. Assuming filePath is expected to be
    // an absolute, normalized path for direct lookup.
    std::filesystem::path checkPath = filePath;
    checkPath.make_preferred(); // Ensure consistent format with cached paths
    std::size_t hashedPath = fnv1a_hash(checkPath.string());

    // Acquire lock, wait for initialization if needed, then check cache
    std::shared_lock<std::shared_mutex> sharedLock(cacheMutex);

    // Wait ONLY if not initialized yet.
    cacheCondition.wait(sharedLock, [this]() {
        return initialized.load(std::memory_order_acquire);
        });

    // Initialized is true and we hold the lock
    return cache.contains(hashedPath);

} // sharedLock is automatically released here
