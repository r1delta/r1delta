#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace r2port {

using FileBytes = std::shared_ptr<const std::vector<uint8_t>>;

class FileProvider {
public:
    virtual ~FileProvider() = default;
    virtual bool Exists(std::string_view normalizedPath) = 0;
    virtual std::optional<uint64_t> Size(std::string_view normalizedPath) = 0;
    virtual FileBytes Read(std::string_view normalizedPath) = 0;
    virtual std::vector<std::string> Enumerate(std::string_view normalizedPattern) { return {}; }
};

// Paths are relative to the game search root and normalized to lower-case forward slashes.
std::optional<std::string> NormalizeProviderPath(std::string_view path);
bool RegisterProvider(std::string_view prefix, std::shared_ptr<FileProvider> provider);
bool Exists(std::string_view path);
std::optional<uint64_t> Size(std::string_view path);
FileBytes ReadAll(std::string_view path);
std::vector<std::string> Enumerate(std::string_view pattern);

// Resets the shared cache and timing window when map loading starts.
void SetActiveMap(std::string_view mapName);
std::string ActiveMapName();
std::uint64_t ActiveMapElapsedMilliseconds();
void LogProviderReadMetrics(const char* phase);
bool FilesystemTraceEnabled();
void TraceFilesystemCall(
    const char* functionName,
    const char* path,
    const char* pathId,
    int64_t flags,
    const char* result,
    const char* detail = nullptr);

} // namespace r2port
