#include "compression.h"
#include <cstdint>

#include <MinHook.h>

// R1DELTA
constexpr uint64_t R1D_marker = 18388560042537298;

void* lzham_decompressor_init;
void* lzham_decompressor_reinit;
void* lzham_decompressor_deinit;
void* lzham_decompressor_decompress;

// TODO(mrsteyk): figure out a way to hotswap algorithms based on input?

void* r1dc_init(void* params) {
	return reinterpret_cast<decltype(&r1dc_init)>(lzham_decompressor_init)(params);
}

__int64 r1dc_reinit(void* p) {
	return reinterpret_cast<decltype(&r1dc_reinit)>(lzham_decompressor_reinit)(p);
}

__int64 r1dc_deinit(void* p) {
	return reinterpret_cast<decltype(&r1dc_deinit)>(lzham_decompressor_deinit)(p);
}

__int64 r1dc_decompress(void* p, void* pIn_buf, size_t* pIn_buf_size, void* pOut_buf, size_t* pOut_buf_size, int no_more_input_bytes_flag) {
	return reinterpret_cast<decltype(&r1dc_decompress)>(lzham_decompressor_decompress)(p, pIn_buf, pIn_buf_size, pOut_buf, pOut_buf_size, no_more_input_bytes_flag);
}

void InitCompressionHooks() {
	auto fs = (uintptr_t)GetModuleHandleA("filesystem_stdio.dll");
	MH_CreateHook(LPVOID(fs + 0x75380), r1dc_init, &lzham_decompressor_init);
	MH_CreateHook(LPVOID(fs + 0x75390), r1dc_reinit, &lzham_decompressor_reinit);
	MH_CreateHook(LPVOID(fs + 0x753A0), r1dc_deinit, &lzham_decompressor_deinit);
	MH_CreateHook(LPVOID(fs + 0x753B0), r1dc_decompress, &lzham_decompressor_decompress);
	MH_EnableHook(MH_ALL_HOOKS);
}