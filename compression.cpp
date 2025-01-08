#include "compression.h"
#include <cstdint>
#include <cstdio>

#include "core.h"

#include <MinHook.h>
//#include "thirdparty/zstd/zstd.h"

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

constexpr size_t PERF_METRICS_SIZE = 4096;
#define PERF_METRIC_CLAMP( x ) ((x + 1ull) < PERF_METRICS_SIZE ? (x + 1ull) : PERF_METRICS_SIZE)

struct PerfMetrics {
	uint64_t metrics[PERF_METRICS_SIZE]{ 0 };
	uint32_t idx = -1;
};

PerfMetrics r1dc_decompress_speeds;
size_t r1dc_decompress_sizes[PERF_METRICS_SIZE];

static void
fill_mean_std(uint64_t metrics[PERF_METRICS_SIZE], uint32_t idx, float div, float* mean_, float* stddev_)
{
	auto last = PERF_METRIC_CLAMP(idx);

	float sum = 0;
	for (size_t i = 0; i < last; ++i)
	{
		sum += metrics[i] / div;
	}
	float mean = sum / last;
	float stddev = 0;
	for (size_t i = 0; i < last; ++i)
	{
		float e = metrics[i] / div;
		float d = (e - mean);
		stddev += d * d;
	}
	stddev = sqrtf(stddev / last);

	*mean_ = mean;
	*stddev_ = stddev;
}

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
	const float div = g_PerformanceFrequency / 1000;

	LARGE_INTEGER start, end;
	QueryPerformanceCounter(&start);
	auto ret = reinterpret_cast<decltype(&r1dc_decompress)>(lzham_decompressor_decompress)(p, pIn_buf, pIn_buf_size, pOut_buf, pOut_buf_size, no_more_input_bytes_flag);
	QueryPerformanceCounter(&end);
	auto dur = end.QuadPart - start.QuadPart;

	auto idx = _InterlockedIncrement(&r1dc_decompress_speeds.idx);
	r1dc_decompress_speeds.metrics[idx % PERF_METRICS_SIZE] = dur;
	
	auto out_size = *pOut_buf_size;
	r1dc_decompress_sizes[idx % PERF_METRICS_SIZE] = out_size;

	if (idx % 512 == 0) {
		float mean, stddev;
		fill_mean_std(r1dc_decompress_speeds.metrics, idx, div, &mean, &stddev);

		float mean_mb = 0;
		auto last = PERF_METRIC_CLAMP(idx);
		for (size_t i = 0; i < last; ++i)
		{
			mean_mb += r1dc_decompress_sizes[i] / 1024.f / 1024.f;
		}
		mean_mb /= last;
		float mean_mbs = (mean * 1000) / mean_mb;

		char tmp[128];
		auto ms = dur / div;

		float mb = out_size / 1024.f / 1024.f;
		float mbs = (ms * 1000.f) / mb;

		sprintf_s(tmp, "Decomp %.2fmb: %.2fms (%.2fmb/s) | mean %.2fms (%2.fmb/s) | std %.2fms\n", mb, ms, mbs, mean, mean_mbs, stddev);
		OutputDebugStringA(tmp);
	}

	return ret;
#endif
}

PerfMetrics open_read_speeds;

void* OpenForRead_o = nullptr;
uintptr_t __fastcall OpenForRead_hk(
	uintptr_t a1,
	const char* filepath,
	const char* mode,
	const char* pathid,
	uint64_t a5,
	uint64_t* a6,
	uint64_t a7)
{
	//fprintf(stdout, "OpenForRead: [%s]%s(%s) a7=%llX\n", pathid ? pathid : "NULLPTR", filepath, mode ? mode : "NULLMODE", a7);
	
	const float div = g_PerformanceFrequency / 1000;
	
	LARGE_INTEGER start, end;
	QueryPerformanceCounter(&start);
	// 144 byte allocations...
	auto cfh = reinterpret_cast<decltype(&OpenForRead_hk)>(OpenForRead_o)(a1, filepath, mode, pathid, a5, a6, a7);
	QueryPerformanceCounter(&end);
	auto dur = end.QuadPart - start.QuadPart;

	auto idx = _InterlockedIncrement(&open_read_speeds.idx);
	open_read_speeds.metrics[idx % PERF_METRICS_SIZE] = dur;

#if 0
	float sum = 0;
	auto last = (idx + 1ull) < open_read_speeds_count ? (idx + 1ull) : open_read_speeds_count;
	for (size_t i = 0; i < last; ++i)
	{
		sum += open_read_speeds[i] / div;
	}
	float mean = sum / last;
	float stddev = 0;
	for (size_t i = 0; i < last; ++i)
	{
		float e = open_read_speeds[i] / div;
		float d = (e - mean);
		stddev += d * d;
	}
	stddev = sqrtf(stddev/last);
#else
	float mean, stddev;
	fill_mean_std(open_read_speeds.metrics, idx, div, &mean, &stddev);
#endif

	char tmp[128];
	sprintf_s(tmp, "OpenForRead: %.2fms | mean %.2fms | std %.2fms\n", dur/div, mean, stddev);
	OutputDebugStringA(tmp);

	return cfh;
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

	//~ mrsteyk: profile...
	{
		auto fs = G_filesystem_stdio;
		auto open_for_read_ptr = LPVOID(fs + 0x19C00);
		//MH_CreateHook(open_for_read_ptr, OpenForRead_hk, &OpenForRead_o);
		//MH_CreateHook(LPVOID(fs + 0x753B0), r1dc_decompress, &lzham_decompressor_decompress);
		MH_EnableHook(MH_ALL_HOOKS);
	}
}
