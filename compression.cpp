// r1dc_hooks.cpp
//
// Single-shot ZSTD or fallback LZHAM decompression, with minimal overhead.
//
// - If the 8-byte ZSTD marker is present, we accumulate up to 1 MB of compressed data
//   into a fixed-size buffer. Once we see no_more_input_bytes_flag == 1, we do
//   a single ZSTD_decompress() directly from that buffer into a single
//   dynamically allocated buffer, and then feed that out to the game's requests.
//
// - If it's not ZSTD, we defer everything to LZHAM's original functions.
//
// We return 3 (LZHAM_DECOMP_STATUS_SUCCESS) when we've output all data,
// so that the calling code sees the same success code it expects from LZHAM.
//
// This approach cuts out ring-buffer overhead, repeated streaming calls,
// repeated allocations, and repeated memcpys.
//
// NOTE: We keep the same function signatures as the original code so that
//       hooking is straightforward. We also keep the hooking addresses at the end,
//       but you'll need to adjust them for your environment.
//

#include "core.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>   // only needed if you prefer a dynamic approach for the decompressed buffer
#include <zstd.h>
#include "MinHook.h"  // Adjust include path as needed
#include "audio.h"
#include "load.h"
#include "logging.h"
#include <mutex>
#include <atomic>

// --------------------------------------------------------------------------
// Constants, marker
// --------------------------------------------------------------------------
constexpr uint64_t R1D_marker = 0x5244315f5f4d4150ULL; // "PAM__1DR" in little-endian
static const size_t MAX_CHUNK_COMPRESSED_SIZE = 1U << 20; // 1 MB max compressed chunk
static const size_t MAX_DECOMPRESSED_SIZE = 128U << 20; // 128 MB cap to prevent allocation bombs
static const size_t DECOMP_BUFFER_CHUNK = 1U << 20; // Grow decompression buffer in 1 MB chunks

// --------------------------------------------------------------------------
// Original function pointer types for fallback to LZHAM
// --------------------------------------------------------------------------
typedef void* (*r1dc_init_t)     (void* params);
typedef __int64  (*r1dc_reinit_t)   (void* p, void* unk1, void* unk2, void* unk3);
typedef __int64  (*r1dc_deinit_t)   (void* p);
typedef __int64  (*r1dc_decompress_t)(
    void* p,
    void* pIn_buf,
    size_t* pIn_buf_size,
    void* pOut_buf,
    size_t* pOut_buf_size,
    int no_more_input_bytes_flag
    );

// Global pointers to the original LZHAM routines (set by MinHook)
static r1dc_init_t       original_lzham_decompressor_init = nullptr;
static r1dc_reinit_t     original_lzham_decompressor_reinit = nullptr;
static r1dc_deinit_t     original_lzham_decompressor_deinit = nullptr;
static r1dc_decompress_t original_lzham_decompressor_decompress = nullptr;

// --------------------------------------------------------------------------
// r1dc context struct
// --------------------------------------------------------------------------
struct r1dc_context_t
{
    // explicitly define ctor to avoid memset hacks
    r1dc_context_t()
        : active(0),
        lzham_ctx(nullptr),
        lzham_inited(false),
        typeDetermined(false),
        isZstdChunk(false),
        decompressed(false),
        totalComp(0),
        m_decompBuf(nullptr),
        m_decompSize(0),
        m_decompPos(0)
    {
        InitializeSRWLock(&mtx);  // or SRWLOCK mtx = SRWLOCK_INIT; but don't memset it
    }

    ~r1dc_context_t() {}

    SRWLOCK mtx;
    std::atomic<uint32_t> active;

    void* lzham_ctx;
    bool  lzham_inited;

    bool  typeDetermined;
    bool  isZstdChunk;

    bool     decompressed;
    size_t   totalComp;
    uint8_t  m_compBuf[MAX_CHUNK_COMPRESSED_SIZE];
    uint8_t* m_decompBuf;
    size_t   m_decompSize;
    size_t   m_decompPos;
};


// --------------------------------------------------------------------------
// r1dc_init
// --------------------------------------------------------------------------
void* r1dc_init(void* params)
{
    ZoneScoped;

    void* mem = GlobalAllocator()->mi_malloc(sizeof(r1dc_context_t), TAG_COMPRESSION, HEAP_DELTA);
    auto* ctx = new (mem) r1dc_context_t();

    ctx->lzham_ctx = original_lzham_decompressor_init(params);
    ctx->lzham_inited = (ctx->lzham_ctx != nullptr);

    return ctx;
}


// --------------------------------------------------------------------------
// r1dc_reinit: Called each time the engine wants to start a new chunk
// --------------------------------------------------------------------------
__int64 r1dc_reinit(void* p, void* unk1, void* unk2, void* unk3)
{
    ZoneScoped;

    if (!p) return 0;
    r1dc_context_t* ctx = static_cast<r1dc_context_t*>(p);

    SRWGuard lock(&ctx->mtx);

    // Reentrancy check: ensure no other thread is using this context
    R1DAssert(ctx->active.load() == 0 && "Context still in use during reinit");

    // Reset ZSTD detection
    ctx->typeDetermined = false;
    ctx->isZstdChunk = false;
    ctx->decompressed = false;
    ctx->totalComp = 0;
    ctx->m_decompPos = 0;

    // Free any old decompression buffer from the previous chunk
    if (ctx->m_decompBuf)
    {
        GlobalAllocator()->mi_free(ctx->m_decompBuf, TAG_COMPRESSION, HEAP_DELTA);
        ctx->m_decompBuf = nullptr;
    }
    ctx->m_decompSize = 0;

    // Also re-init the fallback LZHAM context
    if (ctx->lzham_ctx) {
        return original_lzham_decompressor_reinit(ctx->lzham_ctx, unk1, unk2, unk3);
    }
    return 0;
}

// --------------------------------------------------------------------------
// r1dc_deinit
// --------------------------------------------------------------------------
__int64 r1dc_deinit(void* p)
{
    ZoneScoped;

    if (!p) return 0;
    r1dc_context_t* ctx = static_cast<r1dc_context_t*>(p);

    __int64 ret = 0;

    {
        SRWGuard lock(&ctx->mtx);

        // Reentrancy check: ensure no other thread is using this context
        R1DAssert(ctx->active.load() == 0 && "Context still in use during deinit");

        // Deinit the fallback LZHAM
        if (ctx->lzham_ctx)
        {
            ret = original_lzham_decompressor_deinit(ctx->lzham_ctx);
            ctx->lzham_ctx = nullptr;
        }

        // Free any leftover decompression buffer
        if (ctx->m_decompBuf)
        {
            GlobalAllocator()->mi_free(ctx->m_decompBuf, TAG_COMPRESSION, HEAP_DELTA);
            ctx->m_decompBuf = nullptr;
        }
        // lock is released here
    }

    // Now it is safe to destroy and free the context
    ctx->~r1dc_context_t();
    GlobalAllocator()->mi_free(ctx, TAG_COMPRESSION, HEAP_DELTA);
    return ret;
}


// --------------------------------------------------------------------------
// r1dc_decompress: Our main hooking function.
//
// Return codes that the engine expects from LZHAM:
//  - 3 => LZHAM_DECOMP_STATUS_SUCCESS (the entire chunk was decompressed).
//  - 0 => not done yet, need more input or the caller will keep pulling.
//  - (negative or anything else => error).
//
// We'll replicate that: once we've fed all decompressed bytes, we return 3.
//
// For ZSTD:
//  - On the first call(s), accumulate compressed data up to totalComp.
//  - Once no_more_input_bytes_flag == 1, do a single shot ZSTD_decompress.
//  - Then we feed the decompressed data out in subsequent calls, if any.
//
// If the chunk is NOT ZSTD, just fallback to original LZHAM.
//
__int64 r1dc_decompress(
    void* p,
    void* pIn_buf,
    size_t* pIn_buf_size,
    void* pOut_buf,
    size_t* pOut_buf_size,
    int no_more_input_bytes_flag)
{
    ZoneScoped;

    if (!p) return 0;
    r1dc_context_t* ctx = static_cast<r1dc_context_t*>(p);

    SRWGuard lock(&ctx->mtx);

    // Reentrancy check: track active usage
    uint32_t prevActive = ctx->active.fetch_add(1);
    R1DAssert(prevActive == 0 && "Concurrent decompress calls on same context");

    // If we haven't decided whether it's ZSTD or not:
    if (!ctx->typeDetermined)
    {
        // If we have at least 8 bytes, check for marker
        if (*pIn_buf_size >= sizeof(uint64_t))
        {
            uint64_t maybeMarker = 0;
            std::memcpy(&maybeMarker, pIn_buf, sizeof(uint64_t));
            if (maybeMarker == R1D_marker)
            {
                // Mark it as ZSTD chunk
                ctx->typeDetermined = true;
                ctx->isZstdChunk = true;
                // Skip the marker in the input
                uint8_t* inputBytes = static_cast<uint8_t*>(pIn_buf);
                inputBytes += sizeof(uint64_t);
                *pIn_buf_size -= sizeof(uint64_t);
                pIn_buf = inputBytes;
            }
            else
            {
                ctx->typeDetermined = true;
                ctx->isZstdChunk = false;
            }
        }
        else if (no_more_input_bytes_flag)
        {
            // We got so few bytes we couldn't even check the marker => fallback
            ctx->typeDetermined = true;
            ctx->isZstdChunk = false;
        }
        else
        {
            // Not enough data yet to confirm => wait for next call
            // No data is consumed here
            ctx->active.fetch_sub(1);
            return 0;
        }
    }

    // If not ZSTD, fallback to original LZHAM logic
    if (!ctx->isZstdChunk)
    {
        // Just call the original LZHAM function
        ctx->active.fetch_sub(1);
        return original_lzham_decompressor_decompress(
            ctx->lzham_ctx,
            pIn_buf,
            pIn_buf_size,
            pOut_buf,
            pOut_buf_size,
            no_more_input_bytes_flag
        );
    }

    // ----------------------
    // ZSTD path
    // ----------------------

    // (A) If we haven't yet decompressed, we accumulate compressed data
    //     until no_more_input_bytes_flag == 1
    if (!ctx->decompressed)
    {
        // Copy the incoming bytes into our fixed buffer (up to 1MB)
        if (*pIn_buf_size > 0)
        {
            size_t n = *pIn_buf_size;
            // Check for chunk size overflow - bail early instead of clamping
            if (ctx->totalComp + n > MAX_CHUNK_COMPRESSED_SIZE)
            {
                Warning("[r1dc] Error: compressed data exceeds 1MB chunk limit! (%zu + %zu > %zu)\n",
                    ctx->totalComp, n, MAX_CHUNK_COMPRESSED_SIZE);
                R1DAssert(!"Chunk limit exceeded");
                ctx->active.fetch_sub(1);
                return -1; // Return error instead of continuing with partial data
            }

            std::memcpy(&ctx->m_compBuf[ctx->totalComp], pIn_buf, n);
            ctx->totalComp += n;
            // Mark we consumed them
            *pIn_buf_size = 0;
        }

        // If the engine says "no more input," then decompress using streaming
        if (no_more_input_bytes_flag)
        {
            if (ctx->totalComp > 0)
            {
                // Get the frame size if known
                unsigned long long fSize = ZSTD_getFrameContentSize(ctx->m_compBuf, ctx->totalComp);

                // Determine initial buffer size
                size_t initialSize = DECOMP_BUFFER_CHUNK;
                if (fSize != ZSTD_CONTENTSIZE_ERROR && fSize != ZSTD_CONTENTSIZE_UNKNOWN)
                {
                    // Known size - check against cap
                    if (fSize > MAX_DECOMPRESSED_SIZE)
                    {
                        Warning("[r1dc] Frame size %llu exceeds max allowed %zu\n", fSize, MAX_DECOMPRESSED_SIZE);
                        R1DAssert(!"Frame size exceeds cap");
                        ctx->active.fetch_sub(1);
                        return -1;
                    }
                    initialSize = (size_t)fSize;
                }

                // Allocate initial buffer
                ctx->m_decompBuf = static_cast<uint8_t*>(GlobalAllocator()->mi_malloc(initialSize, TAG_COMPRESSION, HEAP_DELTA));
                size_t bufferCapacity = initialSize;
                ctx->m_decompSize = 0; // Actual bytes decompressed

                // Create streaming decompression context
                ZSTD_DStream* dstream = ZSTD_createDStream();
                if (!dstream)
                {
                    Warning("[r1dc] Failed to create ZSTD_DStream\n");
                    GlobalAllocator()->mi_free(ctx->m_decompBuf, TAG_COMPRESSION, HEAP_DELTA);
                    ctx->m_decompBuf = nullptr;
                    ctx->active.fetch_sub(1);
                    return -1;
                }

                ZSTD_initDStream(dstream);

                // Prepare input
                ZSTD_inBuffer input = { ctx->m_compBuf, ctx->totalComp, 0 };

                // Decompress with growable buffer
                bool error = false;
                while (input.pos < input.size)
                {
                    // Ensure we have output space
                    if (ctx->m_decompSize >= bufferCapacity)
                    {
                        // Need to grow - check cap
                        size_t newCapacity = bufferCapacity + DECOMP_BUFFER_CHUNK;
                        if (newCapacity > MAX_DECOMPRESSED_SIZE)
                        {
                            Warning("[r1dc] Decompressed size exceeds max allowed %zu\n", MAX_DECOMPRESSED_SIZE);
                            error = true;
                            break;
                        }

                        // Realloc
                        uint8_t* newBuf = static_cast<uint8_t*>(GlobalAllocator()->mi_malloc(newCapacity, TAG_COMPRESSION, HEAP_DELTA));
                        std::memcpy(newBuf, ctx->m_decompBuf, ctx->m_decompSize);
                        GlobalAllocator()->mi_free(ctx->m_decompBuf, TAG_COMPRESSION, HEAP_DELTA);
                        ctx->m_decompBuf = newBuf;
                        bufferCapacity = newCapacity;
                    }

                    // Decompress
                    ZSTD_outBuffer output = { ctx->m_decompBuf + ctx->m_decompSize, bufferCapacity - ctx->m_decompSize, 0 };
                    size_t ret = ZSTD_decompressStream(dstream, &output, &input);

                    if (ZSTD_isError(ret))
                    {
                        Warning("[r1dc] ZSTD decompress stream error: %s\n", ZSTD_getErrorName(ret));
                        error = true;
                        break;
                    }

                    ctx->m_decompSize += output.pos;
                }

                ZSTD_freeDStream(dstream);

                if (error)
                {
                    GlobalAllocator()->mi_free(ctx->m_decompBuf, TAG_COMPRESSION, HEAP_DELTA);
                    ctx->m_decompBuf = nullptr;
                    ctx->m_decompSize = 0;
                    R1DAssert(!"Decompression error");
                    ctx->active.fetch_sub(1);
                    return -1;
                }
            }
            // Mark that we've done the decompression
            ctx->decompressed = true;
        }
    }

    // (B) Now deliver from m_decompBuf to the caller
    size_t wantOut = *pOut_buf_size;
    *pOut_buf_size = 0;

    if (ctx->decompressed)
    {
        const size_t remaining = (ctx->m_decompSize > ctx->m_decompPos)
            ? (ctx->m_decompSize - ctx->m_decompPos) : 0;
        if (remaining > 0 && wantOut > 0)
        {
            const size_t toCopy = (std::min)(remaining, wantOut);
            std::memcpy(pOut_buf,
                &ctx->m_decompBuf[ctx->m_decompPos],
                toCopy);
            ctx->m_decompPos += toCopy;
            *pOut_buf_size = toCopy;
        }

        // If we've delivered everything, return LZHAM_DECOMP_STATUS_SUCCESS == 3
        if (ctx->m_decompPos >= ctx->m_decompSize)
        {
            ctx->active.fetch_sub(1);
            return 3;
        }
    }

    // Not done yet
    ctx->active.fetch_sub(1);
    return 0;
}
int (*osub_180019350)(__int64 a1, char* a2, unsigned __int8* a3, unsigned int a4, char a5, int a6);
#define PATHSEPARATOR(c) ((c) == '\\' || (c) == '/')
void  V_StripFilename(char* path)
{
    int             length;

    length = V_strlen(path) - 1;
    if (length <= 0)
        return;

    while (length > 0 &&
        !PATHSEPARATOR(path[length]))
    {
        length--;
    }

    path[length] = 0;
}

int sub_180019350(__int64 a1, char* a2, unsigned __int8* a3, unsigned int a4, char a5, int a6)
{

    if (V_stristr(a2, "PLATFORM") || *a2 == '.' || strlen(a2) < 3 || V_stristr((char*)a3, "EXECUTABLE_PATH")) {
        //return 0;
        //reinterpret_cast<void(*)(__int64 a1, char* a2, unsigned __int8* a3)>(G_filesystem_stdio + 0x16CB0)(a1, a2, a3);
        

        a2 = (char*)"\x00";
    }
    auto ret = osub_180019350(a1, a2, a3, a4, a5, a6);

    //if (V_stristr((char*)a2, "PLATFORM") || V_stristr((char*)a3, "EXECUTABLE_PATH")) {
    //    reinterpret_cast<void(*)(__int64 a1, unsigned __int8* a2)>(G_filesystem_stdio + 0x16EC0)(a1, a3);
    //}
    return ret;
}
// --------------------------------------------------------------------------
// Hook setup: Adjust addresses as needed
// --------------------------------------------------------------------------
void InitCompressionHooks()
{
    auto fs = G_filesystem_stdio ? G_filesystem_stdio : G_vscript;

    if (!IsDedicatedServer())
    {
        MH_CreateHook(
            reinterpret_cast<LPVOID>(fs + 0x75380),
            r1dc_init,
            (void**)&original_lzham_decompressor_init
        );
        MH_CreateHook(
            reinterpret_cast<LPVOID>(fs + 0x75390),
            r1dc_reinit,
            (void**)&original_lzham_decompressor_reinit
        );
        MH_CreateHook(
            reinterpret_cast<LPVOID>(fs + 0x753A0),
            r1dc_deinit,
            (void**)&original_lzham_decompressor_deinit
        );
        MH_CreateHook(
            reinterpret_cast<LPVOID>(fs + 0x753B0),
            r1dc_decompress,
            (void**)&original_lzham_decompressor_decompress
        );

        // Example of hooking some file read routines...
        MH_CreateHook(LPVOID(fs + 0x23860),
            Hooked_CBaseFileSystem__SyncRead,
            reinterpret_cast<LPVOID*>(&Original_CBaseFileSystem__SyncRead));
        MH_CreateHook(LPVOID(fs + 0x23490),
            CFileAsyncReadJob_dtor,
            reinterpret_cast<LPVOID*>(&Original_CFileAsyncReadJob_dtor));
        MH_CreateHook(LPVOID(fs + 0x19350),
            sub_180019350,
            reinterpret_cast<LPVOID*>(&osub_180019350));
    }
    else
    {
        MH_CreateHook(
            reinterpret_cast<LPVOID>(fs + 0x180090),
            r1dc_init,
            (void**)&original_lzham_decompressor_init
        );
        MH_CreateHook(
            reinterpret_cast<LPVOID>(fs + 0x1800A0),
            r1dc_reinit,
            (void**)&original_lzham_decompressor_reinit
        );
        MH_CreateHook(
            reinterpret_cast<LPVOID>(fs + 0x1800B0),
            r1dc_deinit,
            (void**)&original_lzham_decompressor_deinit
        );
        MH_CreateHook(
            reinterpret_cast<LPVOID>(fs + 0x1800C0),
            r1dc_decompress,
            (void**)&original_lzham_decompressor_decompress
        );
    }

    MH_EnableHook(MH_ALL_HOOKS);
}
