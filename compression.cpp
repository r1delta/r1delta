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

// --------------------------------------------------------------------------
// Constants, marker
// --------------------------------------------------------------------------
constexpr uint64_t R1D_marker = 0x5244315f5f4d4150ULL; // "PAM__1DR" in little-endian
static const size_t MAX_CHUNK_COMPRESSED_SIZE = 1U << 20; // 1 MB max compressed chunk

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
    ~r1dc_context_t() {}

    SRWLOCK mtx = SRWLOCK_INIT;          // Guard all mutable state below
    // LZHAM fallback
    void* lzham_ctx;
    bool  lzham_inited;

    // We detect whether the current chunk is ZSTD or not:
    bool  typeDetermined;
    bool  isZstdChunk;

    // For single-shot ZSTD:
    bool     decompressed;       // have we done the ZSTD decompress yet?
    size_t   totalComp;          // how many compressed bytes accumulated so far
    uint8_t  m_compBuf[MAX_CHUNK_COMPRESSED_SIZE]; // fixed-size buffer for up to 1 MB
    // Decompressed data:
    uint8_t* m_decompBuf;        // single allocation for the uncompressed chunk
    size_t   m_decompSize;       // total uncompressed size
    size_t   m_decompPos;        // how many bytes we've handed off so far
};

// --------------------------------------------------------------------------
// r1dc_init
// --------------------------------------------------------------------------
void* r1dc_init(void* params)
{
    ZoneScoped;

    r1dc_context_t* ctx = new (GlobalAllocator()->mi_malloc(sizeof(*ctx), TAG_COMPRESSION, HEAP_DELTA)) r1dc_context_t();
    std::memset(ctx, 0, sizeof(*ctx));

    // Create the fallback LZHAM context
    ctx->lzham_ctx = original_lzham_decompressor_init(params);
    ctx->lzham_inited = (ctx->lzham_ctx != nullptr);

    // No chunk type known yet
    ctx->typeDetermined = false;
    ctx->isZstdChunk = false;
    ctx->decompressed = false;
    ctx->totalComp = 0;
    ctx->m_decompBuf = nullptr;
    ctx->m_decompSize = 0;
    ctx->m_decompPos = 0;
    

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

 //   SRWGuard lock(&ctx->mtx);

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

  //  SRWGuard lock(&ctx->mtx);

    __int64 ret = 0;
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

    // Destroy mutex.
    ctx->~r1dc_context_t();
    GlobalAllocator()->mi_free(ctx);
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

   // SRWGuard lock(&ctx->mtx);

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
            return 0;
        }
    }

    // If not ZSTD, fallback to original LZHAM logic
    if (!ctx->isZstdChunk)
    {
        // Just call the original LZHAM function
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
            // Cap in case an unexpected overflow
            if (ctx->totalComp + n > MAX_CHUNK_COMPRESSED_SIZE)
            {
                R1DAssert(!"Chunk limit exceeded");

                // This is an error or extremely unexpected
                Warning("[r1dc] Error: compressed data exceeds 1MB chunk limit!\n");
                // You could return an error code here:
                //   return some negative or engine-specific error
                n = MAX_CHUNK_COMPRESSED_SIZE - ctx->totalComp;
            }

            std::memcpy(&ctx->m_compBuf[ctx->totalComp], pIn_buf, n);
            ctx->totalComp += n;
            // Mark we consumed them
            *pIn_buf_size = 0;
        }

        // If the engine says "no more input," then do the single-shot decompress
        if (no_more_input_bytes_flag)
        {
            if (ctx->totalComp > 0)
            {
                // Get the frame size if possible
                unsigned long long fSize = ZSTD_getFrameContentSize(ctx->m_compBuf, ctx->totalComp);
                if (fSize == ZSTD_CONTENTSIZE_ERROR || fSize == ZSTD_CONTENTSIZE_UNKNOWN)
                {
                    // fallback guess, or do your own logic
                    fSize = 5ULL << 20; // e.g. guess up to 5 MB
                }

                // Allocate decompression buffer
                ctx->m_decompBuf = static_cast<uint8_t*>(GlobalAllocator()->mi_malloc((size_t)fSize, TAG_COMPRESSION, HEAP_DELTA));
                ctx->m_decompSize = (size_t)fSize;

                // Single-shot decompress
                size_t const actualSize = ZSTD_decompress(
                    ctx->m_decompBuf, ctx->m_decompSize,
                    ctx->m_compBuf, ctx->totalComp
                );
                if (ZSTD_isError(actualSize))
                {
                    Warning("[r1dc] ZSTD decompress error: %s\n",
                        ZSTD_getErrorName(actualSize));
                    // Return an error code so the engine sees we failed
                    R1DAssert(!"Decompression error");
                    return -1;
                }

                // Shrink to actual size
                ctx->m_decompSize = actualSize;
            }
            // Mark that we've done the single-shot
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
            return 3;
        }
    }

    // Not done yet
    R1DAssert(!"Unreachable?");
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
