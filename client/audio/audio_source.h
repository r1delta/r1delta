#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// Exact R1 Client2015 cache record, engine SHA256
// 0214a40436eca77f160d12763ab8f392a8ce2268bfe0a973188e426ae4c0263c.
struct R1AudioCacheRecord {
    char filename[260];
    std::int32_t sampleCount; // Interleaved signed PCM16 at 44100 Hz.
    float duration;
    std::int32_t channels;
    std::uint8_t loop;
    std::uint8_t reserved[7];
    std::uint64_t streamOffset;
    const std::int16_t* prefetch;
};
static_assert(sizeof(R1AudioCacheRecord) == 296);
static_assert(offsetof(R1AudioCacheRecord, sampleCount) == 260);
static_assert(offsetof(R1AudioCacheRecord, streamOffset) == 280);
static_assert(offsetof(R1AudioCacheRecord, prefetch) == 288);

void InitializeR1AudioSources(void* nativeFileSystem);
R1AudioCacheRecord* FindR1AudioSource(const char* extensionlessName);
enum class R1AudioReadResult { NotManaged, Success, Error };
R1AudioReadResult ReadR1AudioSource(const char* filename, const char* pathID,
    std::uint64_t offset, void* destination, std::size_t bytes,
    std::size_t& bytesRead, std::string& error);
// Only after native sound shutdown has stopped voices and drained file callbacks.
void DestroyR1AudioSources();
void ReportR1AudioSources();
