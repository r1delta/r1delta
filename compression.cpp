// r1dc_hooks.cpp
//
// This file implements the r1dc_* hooks that replace the original LZHAM routines with
// a ZSTD–based decompressor that uses internal ring buffers for both input and output.
// Compressed data produced by packedstore.cpp is assumed to begin with an 8–byte marker
// (R1D_marker). If the marker is found, the code uses the ZSTD streaming API with a ring
// buffer to decompress the data. Otherwise, it falls back to the original LZHAM code.
//
// Note: The function ZSTD_isFrameDone is not part of the official ZSTD API. Instead, this
// implementation sets a local flag when ZSTD_decompressStream returns 0 (indicating the frame
// is complete).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>  // for (std::min)
#include <zstd.h>
#include "MinHook.h"  // Adjust include path as needed
#include "audio.h"
#include "load.h"

// --------------------------------------------------------------------------
// Compiler-specific "force inline" macro
// --------------------------------------------------------------------------
#if defined(_MSC_VER)
#define R1DC_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define R1DC_FORCE_INLINE inline __attribute__((always_inline))
#else
#define R1DC_FORCE_INLINE inline
#endif

// --------------------------------------------------------------------------
// Constants and Definitions
// --------------------------------------------------------------------------

constexpr uint64_t R1D_marker = 0x5244315f5f4d4150ULL; // Marker written by packedstore.cpp

// Define ring buffer sizes (must be powers of two)
static constexpr size_t R1DC_INBUF_SIZE = 1 << 20;
static constexpr size_t R1DC_OUTBUF_SIZE = 1 << 20;

// --------------------------------------------------------------------------
// Internal context for r1dc hooks
// --------------------------------------------------------------------------
struct r1dc_context_t
{
    // ZSTD streaming decompression context.
    ZSTD_DStream* zds;
    void* lzham_ctx;   // original LZHAM decompressor context
    bool lzham_inited; // true if the LZHAM decompressor has been initialized
    bool zstd_inited;  // true if the ZSTD DStream has been initialized

    // Ring buffer for input.
    uint8_t inputRing[R1DC_INBUF_SIZE];
    // Monotonic counters and data count.
    size_t inWritePos;   // total bytes written into the input ring
    size_t inReadPos;    // total bytes consumed from the input ring
    size_t inDataCount;  // = inWritePos - inReadPos

    // Ring buffer for output.
    uint8_t outputRing[R1DC_OUTBUF_SIZE];
    size_t outWritePos;  // total bytes written into the output ring
    size_t outReadPos;   // total bytes consumed from the output ring
    size_t outDataCount; // = outWritePos - outReadPos

    // Marker detection.
    bool     isZstdChunk;   // true if marker has been detected (i.e. we are in ZSTD mode)
    uint64_t chunkMarker;   // stores marker (should equal R1D_marker)

    // New flag: whether we have already determined the stream type.
    bool     typeDetermined;
};

// --------------------------------------------------------------------------
// Helper functions: ring buffer utilities.
// Since our ring buffers are powers of two, we can use bitmasking.
// --------------------------------------------------------------------------

// Returns a pointer to the contiguous region starting at 'pos' in the ring buffer.
// 'dataCount' is the number of valid bytes stored in the ring.
// The function sets 'contigLen' to the length of the contiguous segment.
R1DC_FORCE_INLINE uint8_t* RingBuffer_GetContiguous(uint8_t* ring, size_t ringSize, size_t pos,
    size_t dataCount, size_t& contigLen)
{
    size_t index = pos & (ringSize - 1);
    contigLen = (std::min)(dataCount, ringSize - index);
    return ring + index;
}

// Append 'len' bytes from src into the ring buffer. Update writePos and dataCount.
R1DC_FORCE_INLINE void RingBuffer_Append(uint8_t* ring, size_t ringSize,
    const uint8_t* src, size_t len,
    size_t& writePos, size_t& dataCount)
{
    size_t remaining = len;
    while (remaining > 0)
    {
        size_t index = writePos & (ringSize - 1);
        size_t space = ringSize - index;
        size_t toCopy = (std::min)(remaining, space);
        std::memcpy(ring + index, src, toCopy);
        src += toCopy;
        writePos += toCopy;
        dataCount += toCopy;
        remaining -= toCopy;
    }
}

// Consume (remove) 'len' bytes from the ring buffer by advancing the read pointer.
R1DC_FORCE_INLINE void RingBuffer_Consume(size_t len, size_t& readPos, size_t& dataCount)
{
    readPos += len;
    dataCount -= len;
}

// --------------------------------------------------------------------------
// Original function pointer types and globals for fallback.
// (These will be set by MinHook when we install our hooks.)
// --------------------------------------------------------------------------
typedef void* (*r1dc_init_t)(void* params);
typedef __int64 (*r1dc_reinit_t)(void* p);
typedef __int64 (*r1dc_deinit_t)(void* p);
typedef __int64 (*r1dc_decompress_t)(
    void* p,
    void* pIn_buf,
    size_t* pIn_buf_size,
    void* pOut_buf,
    size_t* pOut_buf_size,
    int no_more_input_bytes_flag
    );

r1dc_init_t original_lzham_decompressor_init = nullptr;
r1dc_reinit_t original_lzham_decompressor_reinit = nullptr;
r1dc_deinit_t original_lzham_decompressor_deinit = nullptr;
r1dc_decompress_t original_lzham_decompressor_decompress = nullptr;

// --------------------------------------------------------------------------
// r1dc hook implementations with ring buffer logic.
// --------------------------------------------------------------------------

// r1dc_init: Allocate and initialize a new r1dc context.
void* r1dc_init(void* a1)
{
    r1dc_context_t* ctx = new r1dc_context_t;
    std::memset(ctx, 0, sizeof(*ctx));

    ctx->zds = ZSTD_createDStream();
    if (!ctx->zds)
    {
        fprintf(stderr, "[r1dc] Failed to create ZSTD DStream\n");
        delete ctx;
        return nullptr;
    }
    ctx->zstd_inited = false;
    ctx->isZstdChunk = false;
    ctx->chunkMarker = 0ULL;
    ctx->typeDetermined = false; // not determined until we see at least 8 bytes

    // Initialize the original LZHAM decompressor.
    ctx->lzham_ctx = original_lzham_decompressor_init(a1);
    
    return ctx;
}

// r1dc_reinit: Reset the context for a new compressed chunk.
__int64 r1dc_reinit(void* p)
{
    if (!p) return 0;
    r1dc_context_t* ctx = static_cast<r1dc_context_t*>(p);

    ctx->inWritePos = 0;
    ctx->inReadPos = 0;
    ctx->inDataCount = 0;

    ctx->outWritePos = 0;
    ctx->outReadPos = 0;
    ctx->outDataCount = 0;

    ctx->zstd_inited = false;
    ctx->isZstdChunk = false;
    ctx->chunkMarker = 0ULL;
    ctx->typeDetermined = false; // reset determination for the new chunk

    if (ctx->zds)
    {
        ZSTD_initDStream(ctx->zds);
    }

    if (ctx->lzham_ctx)
    {
        return original_lzham_decompressor_reinit(ctx->lzham_ctx);
    }

    return 0;
}

// r1dc_deinit: Free the context.
__int64 r1dc_deinit(void* p)
{
    if (!p) return 0;
    r1dc_context_t* ctx = static_cast<r1dc_context_t*>(p);
    if (ctx->zds)
        ZSTD_freeDStream(ctx->zds);
    __int64 ret = 0;
    if (ctx->lzham_ctx)
        ret = original_lzham_decompressor_deinit(ctx->lzham_ctx);

    delete ctx;
    return ret;
}

// r1dc_decompress: Decompress data using ring buffer logic and ZSTD streaming.
// The caller provides input data (pIn_buf with size *pIn_buf_size) and an output
// buffer (pOut_buf with capacity *pOut_buf_size). On success, the output size is
// updated and -1 is returned.
__int64 r1dc_decompress(
    void* p,
    void* pIn_buf,
    size_t* pIn_buf_size,
    void* pOut_buf,
    size_t* pOut_buf_size,
    int no_more_input_bytes_flag)
{
    if (!p) return 0;
    r1dc_context_t* ctx = static_cast<r1dc_context_t*>(p);

    // We'll work with a local pointer to the input buffer.
    uint8_t* callerIn = static_cast<uint8_t*>(pIn_buf);
    size_t   callerInSize = *pIn_buf_size;

    // ----------------------------------------------------------------------
    // Step 1: Determine stream type (if not already done).
    // In fallback (LZHAM) mode we must not disturb the caller's input data.
    // ----------------------------------------------------------------------
    if (!ctx->typeDetermined)
    {
        if (callerInSize < sizeof(uint64_t))
        {
            // Not enough input to decide.
            // If no more input is coming then we must assume fallback.
            if (no_more_input_bytes_flag)
            {
                ctx->typeDetermined = true;
                ctx->isZstdChunk = false;
            }
            else
            {
                return 0; // wait for more data
            }
        }
        else
        {
            // Peek at the first 8 bytes.
            uint64_t markerVal = *reinterpret_cast<const uint64_t*>(callerIn);
            ctx->typeDetermined = true;
            if (markerVal == R1D_marker)
            {
                ctx->isZstdChunk = true;
                ctx->chunkMarker = markerVal;
                // In ZSTD mode, we consume the marker bytes.
                callerIn += sizeof(uint64_t);
                callerInSize -= sizeof(uint64_t);
            }
            else
            {
                ctx->isZstdChunk = false;
            }
        }
    }

    // ----------------------------------------------------------------------
    // Step 2: Fallback to LZHAM if the stream is not a ZSTD chunk.
    // In fallback mode we do not alter the caller's input buffer.
    // ----------------------------------------------------------------------
    if (!ctx->isZstdChunk)
    {
        return original_lzham_decompressor_decompress(
            ctx->lzham_ctx, pIn_buf, pIn_buf_size,
            pOut_buf, pOut_buf_size,
            no_more_input_bytes_flag);
    }

    // ----------------------------------------------------------------------
    // Step 3: In ZSTD mode, append all caller input data to our internal input ring buffer.
    // (Since we have already consumed any marker bytes, we append the rest.)
    // ----------------------------------------------------------------------
    if (callerInSize > 0)
    {
        // (We assume there's enough free space in the ring.)
        RingBuffer_Append(ctx->inputRing, R1DC_INBUF_SIZE,
            callerIn, callerInSize,
            ctx->inWritePos, ctx->inDataCount);
        // Mark caller input as fully consumed.
        *pIn_buf_size = 0;
    }

    // ----------------------------------------------------------------------
    // Step 4: Initialize the ZSTD stream if not already done.
    // ----------------------------------------------------------------------
    if (!ctx->zstd_inited)
    {
        size_t initRes = ZSTD_initDStream(ctx->zds);
        if (ZSTD_isError(initRes))
        {
            fprintf(stderr, "[r1dc] ZSTD_initDStream error: %s\n", ZSTD_getErrorName(initRes));
            return 0;
        }
        ctx->zstd_inited = true;
    }

    // ----------------------------------------------------------------------
    // Step 5: Decompression loop.
    // ----------------------------------------------------------------------
    bool frameComplete = false;
    while (ctx->inDataCount > 0 && (ctx->outDataCount < R1DC_OUTBUF_SIZE))
    {
        // Get contiguous region from the input ring.
        size_t inContig = 0;
        uint8_t* inPtr = RingBuffer_GetContiguous(ctx->inputRing, R1DC_INBUF_SIZE,
            ctx->inReadPos, ctx->inDataCount, inContig);
        ZSTD_inBuffer inBuf = { inPtr, inContig, 0 };

        // Get contiguous free space in the output ring.
        size_t outContig = 0;
        uint8_t* outPtr = RingBuffer_GetContiguous(ctx->outputRing, R1DC_OUTBUF_SIZE,
            ctx->outWritePos, (R1DC_OUTBUF_SIZE - ctx->outDataCount), outContig);
        ZSTD_outBuffer outBuf = { outPtr, outContig, 0 };

        // Decompress
        size_t ret = ZSTD_decompressStream(ctx->zds, &outBuf, &inBuf);
        if (ZSTD_isError(ret))
        {
            fprintf(stderr, "[r1dc] Decompression error: %s\n", ZSTD_getErrorName(ret));
            return 0;
        }

        // Update input ring: consumed inBuf.pos bytes.
        RingBuffer_Consume(inBuf.pos, ctx->inReadPos, ctx->inDataCount);

        // Update output ring: produced outBuf.pos bytes.
        ctx->outWritePos += outBuf.pos;
        ctx->outDataCount += outBuf.pos;

        if (ret == 0)
        {
            // The current frame is complete.
            frameComplete = true;
            break;
        }

        // If no progress was made, break to avoid infinite loop.
        if (inBuf.pos == 0 && outBuf.pos == 0)
            break;
    }

    // ----------------------------------------------------------------------
    // Step 6: Copy decompressed data from the output ring buffer to the caller’s buffer.
    // ----------------------------------------------------------------------
    uint8_t* callerOut = static_cast<uint8_t*>(pOut_buf);
    size_t   callerOutCap = *pOut_buf_size;
    size_t   copied = 0;

    while (copied < callerOutCap && ctx->outDataCount > 0)
    {
        size_t contig = 0;
        uint8_t* outContigPtr = RingBuffer_GetContiguous(ctx->outputRing, R1DC_OUTBUF_SIZE,
            ctx->outReadPos, ctx->outDataCount, contig);
        size_t toCopy = (std::min)(callerOutCap - copied, contig);
        std::memcpy(callerOut + copied, outContigPtr, toCopy);
        copied += toCopy;
        RingBuffer_Consume(toCopy, ctx->outReadPos, ctx->outDataCount);
    }
    *pOut_buf_size = copied;

    // ----------------------------------------------------------------------
    // Step 7: If the decompression frame is complete (frameComplete == true)
    //         and no data remains in the ring buffers, return -1.
    //         Otherwise, return 0.
    // ----------------------------------------------------------------------
    if (frameComplete && (ctx->inDataCount == 0) && (ctx->outDataCount == 0))
        return -1;
    else
        return 0;
}

// --------------------------------------------------------------------------
// Hook Registration: Replace the original LZHAM functions with our r1dc hooks.
// (Adjust the address offsets as appropriate for your module.)
// --------------------------------------------------------------------------
void InitCompressionHooks()
{
    auto fs = G_filesystem_stdio;

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

    MH_CreateHook(LPVOID(fs + 0x23860),
        Hooked_CBaseFileSystem__SyncRead,
        reinterpret_cast<LPVOID*>(&Original_CBaseFileSystem__SyncRead));
    MH_CreateHook(LPVOID(fs + 0x23490),
        CFileAsyncReadJob_dtor,
        reinterpret_cast<LPVOID*>(&Original_CFileAsyncReadJob_dtor));
    MH_EnableHook(MH_ALL_HOOKS);
}
