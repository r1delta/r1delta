#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>

namespace r1delta::graphics_flight_recorder
{
// Diagnostic identities never own or dereference native/COM objects. A generation
// identifies one creation attempt, even when a native pool address is reused.
enum class EventKind : std::uint32_t
{
    RecorderStarted = 1,
    TextureCreateBegin, TextureCreateEnd,
    TextureReleaseBegin, TextureReleaseEnd,
    TextureUploadQueued, TextureUploadBegin, TextureUploadEnd,
    ShaderResourcePending, ShaderResourceSubmit,
    VtfLoadBegin, VtfLoadEnd,
    TxaaQueued, TxaaExecute,
    PresentBegin, PresentEnd,
    LevelShutdownBegin, LevelShutdownEnd,
    DeviceShutdownBegin, DeviceShutdownEnd,
    DeviceDisconnectBegin, DeviceDisconnectEnd,
    DeviceReady,
};

struct ResourceDescription
{
    std::uintptr_t resource = 0;
    std::uintptr_t shaderView = 0;
    std::uintptr_t targetView = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
    std::uint32_t format = 0;
    std::uint32_t mipCount = 0;
    std::uint32_t arraySize = 0;
    std::uint32_t flags = 0;
    // Native view-array ownership is not a COM view pointer. Layout bits:
    // 0 = shaderView array, 1 = targetView array, 2 = depth-stencil target.
    std::uint32_t viewLayout = 0;
    // Resource dimension: 1/2/3 for native 1D/2D/3D textures.
    std::uint32_t dimension = 0;
};

// Four caller-defined scalar payloads. Native hook callers store command/texture
// addresses, slot/handle, call-site RVA and result as documented by each event.
void Record(EventKind kind, std::uintptr_t key = 0, std::uint64_t generation = 0,
    std::uint64_t value1 = 0, std::uint64_t value2 = 0,
    std::uint64_t value3 = 0, std::uint64_t value4 = 0) noexcept;
std::uint64_t ResourceCreateBegin(std::uintptr_t key,
    const ResourceDescription& description, std::uintptr_t callerRva) noexcept;
void ResourceCreateEnd(std::uintptr_t key, std::uint64_t generation,
    const ResourceDescription& description, std::int64_t result) noexcept;
std::uint64_t ResourceGeneration(std::uintptr_t key) noexcept;
void ResourceReleaseBegin(std::uintptr_t key, std::uint64_t generation,
    std::uintptr_t callerRva) noexcept;
void ResourceReleaseEnd(std::uintptr_t key, std::uint64_t generation) noexcept;

// Initialization registers process-lifetime recorder storage with the existing
// bounded additional-memory minidump capture. Freeze never waits for writers.
bool Initialize() noexcept;
void Freeze() noexcept;
void WriteReport(std::ostream& output);
void FrameBoundary(bool begin) noexcept;
void ResetBoundary(EventKind kind, std::uintptr_t object = 0) noexcept;
} // namespace r1delta::graphics_flight_recorder

// Installs only for the byte-verified Client2015 retail DX11 module. Dedicated
// modes and unknown binaries do not touch native graphics code.
void InstallGraphicsFlightRecorder(std::uintptr_t materialSystemBase);

std::uintptr_t GraphicsFlightRecorderTextureKey(std::int16_t handle) noexcept;