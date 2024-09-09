#include "compression.h"
#include <cstdint>

#include <MinHook.h>
#include "thirdparty/zstd-1.5.6/zstd.h"

extern uintptr_t G_filesystem_stdio;

// R1DELTA
constexpr uint64_t R1D_marker = 18388560042537298;
constexpr uint32_t R1D_marker_32 = 'R1D';

void* lzham_decompressor_init;
void* lzham_decompressor_reinit;
void* lzham_decompressor_deinit;
void* lzham_decompressor_decompress;

// TODO(mrsteyk): figure out a way to hotswap algorithms based on input?
//                maybe implement my marker idea or wait forever?
#define R1DC_USE_ZS1 0

void* r1dc_init(void* params) {
#if R1DC_USE_ZS1
	return ZSTD_createDCtx();
#else
	return reinterpret_cast<decltype(&r1dc_init)>(lzham_decompressor_init)(params);
#endif
}

__int64 r1dc_reinit(void* p) {
#if R1DC_USE_ZS1
	// TODO(mrsteyk): double check if DCtx doesn't need resetting since streaming API is separate
	return 0;
#else
	return reinterpret_cast<decltype(&r1dc_reinit)>(lzham_decompressor_reinit)(p);
#endif
}

__int64 r1dc_deinit(void* p) {
#if R1DC_USE_ZS1
	// NOTE(mrsteyk): did I tell you that I hate C++?
	ZSTD_freeDCtx((ZSTD_DCtx*)p);
	return 0;
#else
	return reinterpret_cast<decltype(&r1dc_deinit)>(lzham_decompressor_deinit)(p);
#endif
}

__int64 r1dc_decompress(void* p, void* pIn_buf, size_t* pIn_buf_size, void* pOut_buf, size_t* pOut_buf_size, int no_more_input_bytes_flag) {
#if R1DC_USE_ZS1
	auto ret = 0;
	// NOTE(mrsteyk): did I tell you that I hate C++?
	auto size = ZSTD_decompressDCtx((ZSTD_DCtx*)p, pOut_buf, *pOut_buf_size, pIn_buf, *pIn_buf_size);
	if (!ZSTD_isError(size)) {
		*pOut_buf_size = size;
		ret = -1;
	}
	return ret;
#else
	return reinterpret_cast<decltype(&r1dc_decompress)>(lzham_decompressor_decompress)(p, pIn_buf, pIn_buf_size, pOut_buf, pOut_buf_size, no_more_input_bytes_flag);
#endif
}

void InitCompressionHooks() {
#if R1DC_USE_ZS1
	auto fs = G_filesystem_stdio;
	MH_CreateHook(LPVOID(fs + 0x75380), r1dc_init, &lzham_decompressor_init);
	MH_CreateHook(LPVOID(fs + 0x75390), r1dc_reinit, &lzham_decompressor_reinit);
	MH_CreateHook(LPVOID(fs + 0x753A0), r1dc_deinit, &lzham_decompressor_deinit);
	MH_CreateHook(LPVOID(fs + 0x753B0), r1dc_decompress, &lzham_decompressor_decompress);
	MH_EnableHook(MH_ALL_HOOKS);
#endif
}
