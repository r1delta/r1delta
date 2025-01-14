#include <windows.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <filesystem>
#include <chrono>

#include <opus/opus.h>
#include "load.h"
#include "logging.h"
#include <opus/opusfile.h>
#include <samplerate.h>

static std::chrono::steady_clock::time_point g_LastCleanupTime = std::chrono::steady_clock::now();
static const std::chrono::seconds CLEANUP_INTERVAL(5); // Cleanup every 5 seconds

// Core filesystem enums
enum FSAsyncStatus_t {
	FSASYNC_ERR_FILEOPEN = -1,
	FSASYNC_OK = 0,
	FSASYNC_ERR_READING = -4
};

enum FSAsyncFlags_t {
	FSASYNC_FLAGS_NULLTERMINATE = (1 << 3)
};

// Core structures
struct FileAsyncRequest_t {
	const char* pszFilename;    // +0
	void* pData;                // +8
	__int64 nOffset;            // +16
	__int64 nBytes;             // +24
	void(__cdecl* pfnCallback)(const FileAsyncRequest_t*, int, FSAsyncStatus_t);  // +32
	void* pContext;             // +40
	int priority;           // +48
	uint32_t flags;             // +52
	const char* pszPathID;      // +56  - moved here to match disassembly
	void* hSpecificAsyncFile;   // +64
	void* (__cdecl* pfnAlloc)(const char*, unsigned int);  // +72
};
static_assert(offsetof(FileAsyncRequest_t, pszPathID) == 56);

struct AsyncOpenedFile_t {
	char pad[16];
	__int64 hFile;
};

class CBaseFileSystem {
public:
	std::uint64_t vtable;
	std::uint64_t innerVTable;  // at offset 8, just the vtable pointer
	char padding[0x260];
	int fwLevel;
};

typedef int(__fastcall* ThreadGetPriorityFn)(std::uint64_t);
typedef void(__fastcall* ThreadSetPriorityFn)(std::uint64_t, int);

ThreadGetPriorityFn OriginalThreadGetPriority = nullptr;
ThreadSetPriorityFn OriginalThreadSetPriority = nullptr;

void LoadTier0Functions() {
	HMODULE tier0Module = GetModuleHandleA("tier0_orig.dll");

	OriginalThreadGetPriority = reinterpret_cast<ThreadGetPriorityFn>(
		GetProcAddress(tier0Module, "ThreadGetPriority"));

	OriginalThreadSetPriority = reinterpret_cast<ThreadSetPriorityFn>(
		GetProcAddress(tier0Module, "ThreadSetPriority"));
}

extern "C" {
	int __fastcall ThreadGetPriority(std::uint64_t threadId) {
		if (!OriginalThreadGetPriority) {
			LoadTier0Functions();
		}
		return OriginalThreadGetPriority(threadId);
	}

	void __fastcall ThreadSetPriority(std::uint64_t threadId, int priority) {
		if (!OriginalThreadSetPriority) {
			LoadTier0Functions();
		}
		OriginalThreadSetPriority(threadId, priority);
	}
}

// Critical section helpers
static void EnterCriticalSection_(LPCRITICAL_SECTION cs) {
	::EnterCriticalSection(cs);
}

static void LeaveCriticalSection_(LPCRITICAL_SECTION cs) {
	::LeaveCriticalSection(cs);
}

// Function pointers for required operations
constexpr std::uint64_t FS_INVALID_ASYNC_FILE = 0xFFFFULL;
typedef void* FileHandle_t;
// Core filesystem operations through vtable
namespace FSOperations {
	static void* OpenFile(CBaseFileSystem* fs, const char* filename, const char* mode, int flags, const char* pathID, std::uint64_t resolvedFilename) {
		return reinterpret_cast<void* (__fastcall*)(CBaseFileSystem*, const char*, const char*, int, const char*, std::uint64_t)>(*reinterpret_cast<std::uint64_t*>(fs->vtable + 592LL))(fs, filename, mode, flags, pathID, resolvedFilename);
	}

	static __int64 GetFileSize(CBaseFileSystem* fs, FileHandle_t file) {
		std::uint64_t innerThis = reinterpret_cast<std::uint64_t>(fs) + 8;
		auto vtable = *reinterpret_cast<std::uint64_t*>(innerThis);
		return reinterpret_cast<__int64(__fastcall*)(std::uint64_t, void*)>(*reinterpret_cast<std::uint64_t*>(vtable + 56LL))(innerThis, file);
	}

	static void SeekFile(CBaseFileSystem* fs, FileHandle_t file, __int64 offset, int origin) {
		std::uint64_t innerThis = reinterpret_cast<std::uint64_t>(fs) + 8;
		auto vtable = *reinterpret_cast<std::uint64_t*>(innerThis);
		reinterpret_cast<void(__fastcall*)(std::uint64_t, void*, __int64, int)>(*reinterpret_cast<std::uint64_t*>(vtable + 32LL))(innerThis, file, offset, origin);
	}

	static bool GetOptimalIO(CBaseFileSystem* fs, FileHandle_t file, std::uint64_t* pAlign, std::uint64_t zero1, std::uint64_t zero2) {
		return reinterpret_cast<bool(__fastcall*)(CBaseFileSystem*, void*, std::uint64_t*, std::uint64_t, std::uint64_t)>(*reinterpret_cast<std::uint64_t*>(fs->vtable + 704LL))(fs, file, pAlign, zero1, zero2);
	}

	static void* AllocOptimalReadBuffer(CBaseFileSystem* fs, FileHandle_t file, __int64 size, std::uint64_t offset) {
		return reinterpret_cast<void* (__fastcall*)(CBaseFileSystem*, void*, __int64, std::uint64_t)>(*reinterpret_cast<std::uint64_t*>(fs->vtable + 712LL))(fs, file, size, offset);
	}

	static __int64 ReadFile(CBaseFileSystem* fs, void* dest, std::uint64_t destSize, std::uint64_t size, FileHandle_t file) {
		auto result = reinterpret_cast<__int64(__fastcall*)(CBaseFileSystem*, void*, std::uint64_t, std::uint64_t, void*)>(
			*reinterpret_cast<std::uint64_t*>(fs->vtable + 600LL))(fs, dest, destSize, size, file);
		return result;
	}

	static void CloseFile(CBaseFileSystem* fs, FileHandle_t file) {
		std::uint64_t innerThis = reinterpret_cast<std::uint64_t>(fs) + 8;
		auto vtable = *reinterpret_cast<std::uint64_t*>(innerThis);
		reinterpret_cast<void(__fastcall*)(std::uint64_t, void*)>(*reinterpret_cast<std::uint64_t*>(vtable + 24LL))(innerThis, file);
	}

	static void SetBufferSize(CBaseFileSystem* fs, FileHandle_t file, std::uint64_t size) {
		return reinterpret_cast<void(__fastcall*)(CBaseFileSystem*, void*, std::uint64_t)>(*reinterpret_cast<std::uint64_t*>(fs->vtable + 184LL))(fs, file, size);
	}

	static bool FileExists(CBaseFileSystem* fs, const char* filename, const char* pathID) {
		std::uint64_t innerThis = reinterpret_cast<std::uint64_t>(fs) + 8;
		auto vtable = *reinterpret_cast<std::uint64_t*>(innerThis);
		return reinterpret_cast<__int64(__fastcall*)(std::uint64_t, const char*, const char*)>(*reinterpret_cast<std::uint64_t*>(vtable + 104LL))(innerThis, filename, pathID);

	}
}

// -------------- Opus Integration --------------

// Structure to hold the Opus decoding context
struct OpusDecodeContext {
	bool isValid = false;
	int channels = 2;   // final WAV channel count
	int rate = 44100;  // final WAV sample rate
	std::vector<uint8_t> decodedBuffer; // Entire wave in memory, including 44-byte header
	std::chrono::steady_clock::time_point lastAccessed;
};

// We key contexts by the .wav name
static std::mutex g_OpusMapMutex;
static std::unordered_map<std::string, OpusDecodeContext> g_OpusContexts;



// Structure to wrap CBaseFileSystem and FileHandle_t for libopusfile callbacks
struct FsStreamWrapper {
	CBaseFileSystem* fs;
	FileHandle_t file;
};

// Function to create custom callbacks for libopusfile
static int op_fs_read(void* stream, unsigned char* ptr, int nbytes) {
	FsStreamWrapper* wrapper = reinterpret_cast<FsStreamWrapper*>(stream);
	__int64 bytesRead = FSOperations::ReadFile(wrapper->fs, ptr, nbytes, nbytes, wrapper->file);
	if (bytesRead < 0) {
		return -1; // Indicate error
	}
	return (int)bytesRead;
}

static int op_fs_seek(void* stream, opus_int64 offset, int whence) {
	FsStreamWrapper* wrapper = reinterpret_cast<FsStreamWrapper*>(stream);
	FSOperations::SeekFile(wrapper->fs, wrapper->file, offset, whence);
	return 0;
}

static opus_int64 op_fs_tell(void* stream) {
	FsStreamWrapper* wrapper = reinterpret_cast<FsStreamWrapper*>(stream);
	std::uint64_t innerThis = reinterpret_cast<std::uint64_t>(wrapper->fs) + 8;
	auto vtable = *reinterpret_cast<std::uint64_t*>(innerThis);
	// Create function pointer type first
	using TellFn = __int64(__fastcall*)(std::uint64_t, void*);
	TellFn tellFn = reinterpret_cast<TellFn>(*reinterpret_cast<std::uint64_t*>(vtable + 40LL));
	return tellFn(innerThis, wrapper->file);
}

static int op_fs_close(void* stream) {
	FsStreamWrapper* wrapper = reinterpret_cast<FsStreamWrapper*>(stream);
	FSOperations::CloseFile(wrapper->fs, wrapper->file);
	delete wrapper; // Free the wrapper struct
	return 0;
}

static OpusFileCallbacks g_CustomOpusCallbacks = {
	op_fs_read,
	op_fs_seek,
	op_fs_tell,
	op_fs_close
};

// --------------------------------------------------------------------
// Helper: read entire file from FS into a std::vector<uint8_t>
// --------------------------------------------------------------------
static bool ReadEntireFile(
	CBaseFileSystem* fs,
	FileHandle_t file,
	std::vector<uint8_t>& outData)
{
	const __int64 fileSize = FSOperations::GetFileSize(fs, file);
	if (fileSize <= 0)
		return false;

	outData.resize((size_t)fileSize);
	FSOperations::SeekFile(fs, file, 0, /*SEEK_SET=*/0);
	__int64 bytesRead = FSOperations::ReadFile(fs, outData.data(), fileSize, fileSize, file);
	return (bytesRead == fileSize);
}

// --------------------------------------------------------------------
// WAV header builder
// --------------------------------------------------------------------
static void BuildFakeWavHeader(
	std::vector<uint8_t>& header,
	int channels,
	int sampleRate,
	uint32_t dataSize)
{
	// 44-byte standard PCM wave header
	header.resize(44);
	const uint32_t chunkSize = dataSize + 36;           // (overall - 8) if we skip the "RIFF" & chunk-size fields
	const uint16_t bitsPerSample = 16;
	const uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
	const uint16_t blockAlign = channels * (bitsPerSample / 8);

	// RIFF
	memcpy(&header[0], "RIFF", 4);
	memcpy(&header[4], &chunkSize, 4);
	memcpy(&header[8], "WAVEfmt ", 8);

	// SubChunk1
	uint32_t subChunk1Size = 16;
	memcpy(&header[16], &subChunk1Size, 4);
	uint16_t audioFormat = 1;  // PCM
	memcpy(&header[20], &audioFormat, 2);
	memcpy(&header[22], &channels, 2);
	memcpy(&header[24], &sampleRate, 4);
	memcpy(&header[28], &byteRate, 4);
	memcpy(&header[32], &blockAlign, 2);
	memcpy(&header[34], &bitsPerSample, 2);

	// SubChunk2
	memcpy(&header[36], "data", 4);
	memcpy(&header[40], &dataSize, 4);
}
// Helper function to open a .opus file and set up the decode context
// --------------------------------------------------------------------
// Full decode: 
//   1) open the .opus
//   2) decode *all* frames to 44.1kHz PCM (int16)
//   3) build a 44-byte WAV header
//   4) stash final data in ctx.decodedBuffer
// --------------------------------------------------------------------
static bool DecodeEntireOpus(
	CBaseFileSystem* fs,
	FileHandle_t file,
	OpusDecodeContext& ctx)
{
	// We'll do a memory read for opusfile. 
	// Step 1: read entire .opus file into a memory buffer:
	std::vector<uint8_t> opusData;
	if (!ReadEntireFile(fs, file, opusData)) {
		FSOperations::CloseFile(fs, file);
		return false;
	}
	FSOperations::CloseFile(fs, file);

	// Step 2: open it via op_open_memory (libopusfile >= 0.9).
	// If your older libopusfile lacks op_open_memory, use custom callbacks.
	int error = 0;
	OggOpusFile* opusFile = op_open_memory(opusData.data(), opusData.size(), &error);
	if (!opusFile) {
		// fallback: if your version doesn't have op_open_memory, you'd do custom callbacks
		return false;
	}

	// Get channels from the last link’s OpusHead
	const OpusHead* head = op_head(opusFile, -1);
	int channels = (head) ? head->channel_count : 2;

	// Step 3: decode all samples at 48kHz and then resample if you like.
	// For simplicity, let’s just decode direct to 48kHz stereo via op_read_stereo():
	// If you truly need 44.1kHz, you can use libsamplerate. Shown below is a 
	// direct approach for 48kHz -> 44.1kHz, but feel free to decode at 48kHz 
	// to reduce complexity. We'll show 44.1kHz example here for consistency.
	int finalRate = 44100;
	int finalChannels = 2;

	// We'll decode in chunks, storing 48kHz int16 in a temp buffer,
	// then pass to libsamplerate to get 44.1k. 
	// Or decode everything *first* at 48k, then a single pass through src_process() 
	// if you prefer. For brevity, let's do it in one pass.

	// 1) read entire PCM into a std::vector<int16_t> at 48k
	std::vector<int16_t> pcm48k;
	pcm48k.reserve(4 * 1024 * 1024); // some big reserve

	while (true) {
		// op_read_stereo returns stereo @ 48k in 16-bit signed
		static const int SAMPLES_PER_READ = 12000;
		int16_t tempBuf[2 * SAMPLES_PER_READ];

		long framesDecoded = op_read_stereo(opusFile, tempBuf, SAMPLES_PER_READ);
		if (framesDecoded <= 0)
			break; // 0 or <0 -> no more data or error

		pcm48k.insert(pcm48k.end(), tempBuf, tempBuf + (framesDecoded * 2));
	}

	// We have all 48k PCM in pcm48k. Let's resample to 44.1k with libsamplerate:
	SRC_STATE* src = nullptr;
	{
		int srcErr = 0;
		src = src_new(SRC_SINC_BEST_QUALITY, finalChannels, &srcErr);
		if (!src) {
			op_free(opusFile);
			return false;
		}
	}
	// Prepare input as float
	std::vector<float> floatIn(pcm48k.size());
	for (size_t i = 0; i < pcm48k.size(); i++)
		floatIn[i] = pcm48k[i] / 32768.0f;

	// Estimate output size: ratio = 44100/48000 = 0.91875
	const double ratio = double(finalRate) / 48000.0;
	size_t maxOutputFrames = (size_t)(std::ceil(pcm48k.size() / 2 * ratio)) + 1000;
	std::vector<float> floatOut(maxOutputFrames * finalChannels, 0.0f);

	SRC_DATA srcData = { 0 };
	srcData.data_in = floatIn.data();
	srcData.input_frames = (long)(pcm48k.size() / 2);
	srcData.data_out = floatOut.data();
	srcData.output_frames = (long)maxOutputFrames;
	srcData.src_ratio = ratio;
	srcData.end_of_input = 1;

	int processErr = src_process(src, &srcData);
	if (processErr) {
		src_delete(src);
		op_free(opusFile);
		return false;
	}

	// Convert floatOut back to int16
	const long outFrames = srcData.output_frames_gen;
	std::vector<int16_t> finalPcm(outFrames * finalChannels);
	for (long i = 0; i < outFrames * finalChannels; i++) {
		float s = floatOut[i];
		if (s > 1.0f)  s = 1.0f;
		if (s < -1.0f) s = -1.0f;
		finalPcm[i] = (int16_t)(s * 32767.f);
	}

	// Clean up
	op_free(opusFile);
	src_delete(src);

	// Step 4: Build a WAV header + store final data in ctx.decodedBuffer
	const size_t totalPcmBytes = finalPcm.size() * sizeof(int16_t);
	std::vector<uint8_t> header;
	BuildFakeWavHeader(header, finalChannels, finalRate, (uint32_t)totalPcmBytes);

	ctx.decodedBuffer.reserve(header.size() + totalPcmBytes);
	ctx.decodedBuffer.insert(ctx.decodedBuffer.end(), header.begin(), header.end());
	const uint8_t* pcmBytes = reinterpret_cast<const uint8_t*>(finalPcm.data());
	ctx.decodedBuffer.insert(ctx.decodedBuffer.end(), pcmBytes, pcmBytes + totalPcmBytes);

	// We now have a valid WAV in memory
	ctx.channels = finalChannels;
	ctx.rate = finalRate;
	ctx.isValid = true;

	return true;
}



// Helper function to check if a file path ends with .wav
static bool EndsWithWav(const char* filename) {
	if (!filename) return false;
	std::string fn(filename);
	if (fn.size() < 4) return false;
	auto ext = fn.substr(fn.size() - 4);
	for (auto& c : ext) c = tolower((unsigned char)c);
	return (ext == ".wav");
}

// Helper function to create an .opus version of the file path
static std::string MakeOpusFilename(const char* wavFile) {
	std::string fn(wavFile);
	return fn.substr(0, fn.size() - 4) + ".opus";
}


static void(__fastcall* pFreeAsyncFileHandle)(std::uint64_t);
static std::uint64_t(__fastcall* pGetOptimalReadSize)(CBaseFileSystem*, void*, std::uint64_t);
static void(__fastcall* pPerformAsyncCallback)(CBaseFileSystem*, FileAsyncRequest_t*, void*, std::uint64_t, std::uint64_t);
static void(__fastcall* pReleaseAsyncOpenedFiles)(CRITICAL_SECTION*);
static void(__fastcall* pLogFileAccess)(CBaseFileSystem*, const char*, const char*, void*);

static CRITICAL_SECTION* pAsyncOpenedFilesCriticalSection;
static std::uint64_t pAsyncOpenedFilesTable;
static void* pFileAccessLoggingPointer;

__int64 HandleOriginalRead(CBaseFileSystem* filesystem, FileAsyncRequest_t* request) {
	// Input validation
	if (request->nBytes < 0 || request->nOffset < 0) {
		if (request->pfnCallback) {
			auto criticalSection = reinterpret_cast<LPCRITICAL_SECTION>(reinterpret_cast<std::uint64_t>(filesystem) + 408);
			EnterCriticalSection_(criticalSection);
			reinterpret_cast<void(__fastcall*)(FileAsyncRequest_t*, std::uint64_t, std::int64_t)>(request->pfnCallback)(request, 0, 0xFFFFFFFF);
			LeaveCriticalSection_(criticalSection);
		}
		return FSASYNC_ERR_FILEOPEN;
	}

	// Get held file if it exists
	AsyncOpenedFile_t* pHeldFile = nullptr;
	if (request->hSpecificAsyncFile != (void*)FS_INVALID_ASYNC_FILE) {
		EnterCriticalSection_(pAsyncOpenedFilesCriticalSection);
		std::uint64_t entryBase = pAsyncOpenedFilesTable + (24ULL * ((__int64)request->hSpecificAsyncFile & 0xFFFF));
		std::uint64_t ptrStruct = *reinterpret_cast<std::uint64_t*>(entryBase + 16ULL);
		if (ptrStruct) {
			_InterlockedIncrement(reinterpret_cast<volatile long*>(ptrStruct + 8ULL));
			pHeldFile = reinterpret_cast<AsyncOpenedFile_t*>(ptrStruct);
		}
		LeaveCriticalSection_(pAsyncOpenedFilesCriticalSection);
	}

	// Get or open file handle
	FileHandle_t hFile = pHeldFile ? *reinterpret_cast<FileHandle_t*>(reinterpret_cast<std::uint64_t>(pHeldFile) + 16) : nullptr;
	if (!pHeldFile || !hFile) {
		hFile = FSOperations::OpenFile(filesystem, request->pszFilename, "rb", 0, request->pszPathID, 0);
		if (pHeldFile) {
			*reinterpret_cast<FileHandle_t*>(reinterpret_cast<std::uint64_t>(pHeldFile) + 16) = hFile;
		}
	}

	__int64 originalResult;
	if (!hFile) {
		if (request->pfnCallback) {
			auto criticalSection = reinterpret_cast<LPCRITICAL_SECTION>(reinterpret_cast<std::uint64_t>(filesystem) + 408);
			EnterCriticalSection_(criticalSection);
			reinterpret_cast<void(__fastcall*)(FileAsyncRequest_t*, std::uint64_t, std::int64_t)>(request->pfnCallback)(request, 0, 0xFFFFFFFF);
			LeaveCriticalSection_(criticalSection);
		}
		originalResult = FSASYNC_ERR_FILEOPEN;
	}
	else {
		// Calculate read size and prepare buffer
		int nBytesToRead = request->nBytes ? static_cast<int>(request->nBytes) :
			static_cast<int>(FSOperations::GetFileSize(filesystem, hFile) - request->nOffset);
		nBytesToRead = (nBytesToRead < 0) ? 0 : nBytesToRead;

		void* pDest;
		int nBytesBuffer;
		if (request->pData) {
			pDest = request->pData;
			nBytesBuffer = nBytesToRead;
		}
		else {
			nBytesBuffer = nBytesToRead + ((request->flags & FSASYNC_FLAGS_NULLTERMINATE) ? 1 : 0);
			std::uint64_t alignment = 0;
			if (FSOperations::GetOptimalIO(filesystem, hFile, &alignment, 0, 0) && (request->nOffset % alignment == 0)) {
				nBytesBuffer = static_cast<int>(pGetOptimalReadSize(filesystem, hFile, nBytesBuffer));
			}
			pDest = request->pfnAlloc ?
				reinterpret_cast<void* (__fastcall*)(const char*, std::uint64_t)>(request->pfnAlloc)(request->pszFilename, nBytesBuffer) :
				FSOperations::AllocOptimalReadBuffer(filesystem, hFile, nBytesBuffer, request->nOffset);
		}

		if (request->nOffset > 0) {
			FSOperations::SeekFile(filesystem, hFile, request->nOffset, 0);
		}

		int oldPriority = ThreadGetPriority(0);
		if (oldPriority < 2) ThreadSetPriority(0, 2);
		__int64 nBytesRead = FSOperations::ReadFile(filesystem, pDest, nBytesToRead, nBytesToRead, hFile);
		if (oldPriority < 2) ThreadSetPriority(0, oldPriority);

		if (!pHeldFile) FSOperations::CloseFile(filesystem, hFile);

		if (request->flags & FSASYNC_FLAGS_NULLTERMINATE) {
			reinterpret_cast<char*>(pDest)[nBytesRead] = '\0';
		}

		if ((nBytesRead == 0) && (nBytesToRead != 0)) {
			Warning("SyncRead failed: file='%s' nBytesRead=0 nBytesToRead=%lld\n",
				request->pszFilename, nBytesToRead);
			originalResult = FSASYNC_ERR_READING;
		}
		else {
			originalResult = FSASYNC_OK;
		}
		int finalBytes = (nBytesRead < nBytesToRead) ? static_cast<int>(nBytesRead) : nBytesToRead;
		pPerformAsyncCallback(filesystem, request, pDest, finalBytes, originalResult);
	}

	if (pHeldFile) {
		pReleaseAsyncOpenedFiles(pAsyncOpenedFilesCriticalSection);
	}

	if (filesystem->fwLevel >= 6) {
		pLogFileAccess(filesystem, "async", request->pszFilename, pFileAccessLoggingPointer);
	}

	const char* statusStr;
	switch (originalResult) {
	case FSASYNC_OK:
		statusStr = "OK";
		break;
	case FSASYNC_ERR_FILEOPEN:
		statusStr = "ERR_FILEOPEN";
		break;
	case FSASYNC_ERR_READING:
		statusStr = "ERR_READING";
		break;
	default:
		statusStr = "UNKNOWN";
	}

	Msg("SyncRead: file='%s' offset=%lld bytes=%lld status=%s\n",
		request->pszFilename,
		request->nOffset,
		request->nBytes,
		statusStr);

	return originalResult;
}

// --------------------------------------------------------------------
// OpenOpusContext: 
//   - If we already have a valid context in the map, just reuse it.
//   - Else open the file, decode entire opus to WAV in memory, store it.
// --------------------------------------------------------------------
static bool OpenOpusContext(
	const std::string& wavName,
	const std::string& opusName,
	CBaseFileSystem* filesystem,
	const char* pathID)
{
	std::lock_guard<std::mutex> lock(g_OpusMapMutex);

	auto it = g_OpusContexts.find(wavName);
	if (it != g_OpusContexts.end() && it->second.isValid) {
		// Already decoded in memory
		it->second.lastAccessed = std::chrono::steady_clock::now();
		return true;
	}

	// Not in map, or invalid. Let's open the .opus
	FileHandle_t fh = FSOperations::OpenFile(filesystem, opusName.c_str(), "rb", 0, pathID, 0);
	if (!fh) {
		return false;
	}

	// Create brand-new context
	OpusDecodeContext ctx;
	if (!DecodeEntireOpus(filesystem, fh, ctx)) {
		return false;
	}
	ctx.lastAccessed = std::chrono::steady_clock::now();

	// Store in map
	g_OpusContexts[wavName] = ctx;
	return true;
}

// --------------------------------------------------------------------
// HandleOpusRead:
//   - Just copy from the in-memory buffer to the engine's read buffer
// --------------------------------------------------------------------
static __int64 HandleOpusRead(CBaseFileSystem* fs, FileAsyncRequest_t* request)
{
	// Grab existing, fully decoded context
	OpusDecodeContext ctx;
	{
		std::lock_guard<std::mutex> lock(g_OpusMapMutex);
		ctx = g_OpusContexts[request->pszFilename];
		ctx.lastAccessed = std::chrono::steady_clock::now();
		// Update back to map
		g_OpusContexts[request->pszFilename] = ctx;
	}

	if (!ctx.isValid) {
		// fallback to old/baseline read
		return HandleOriginalRead(fs, request);
	}

	// The total size of our in-memory WAV
	const size_t totalSize = ctx.decodedBuffer.size();
	__int64 offset = request->nOffset;
	__int64 nBytesToRead = (request->nBytes == 0)
		? (totalSize - offset)
		: request->nBytes;
	if (nBytesToRead < 0)   nBytesToRead = 0;
	if (offset < 0)         offset = 0;
	if (offset > (__int64)totalSize)
		offset = totalSize;

	// Allocate memory if needed
	void* pDest = nullptr;
	if (request->pData) {
		pDest = request->pData;
	}
	else {
		const int extraNullTerm = (request->flags & FSASYNC_FLAGS_NULLTERMINATE) ? 1 : 0;
		int totalAlloc = (int)(nBytesToRead + extraNullTerm);
		if (request->pfnAlloc) {
			pDest = request->pfnAlloc(request->pszFilename, totalAlloc);
		}
		else {
			// or your own FSOperations::AllocOptimalReadBuffer if you want
			pDest = malloc(totalAlloc);
		}
	}
	if (!pDest) {
		// callback with error
		if (request->pfnCallback) {
			request->pfnCallback(request, 0, FSASYNC_ERR_READING);
		}
		return FSASYNC_ERR_READING;
	}

	// Copy from [offset..offset+nBytesToRead) 
	size_t bytesCopied = 0;
	if (nBytesToRead > 0) {
		size_t avail = totalSize - (size_t)offset;
		size_t toCopy = (size_t)nBytesToRead < avail ? (size_t)nBytesToRead : avail;
		memcpy(pDest, &ctx.decodedBuffer[(size_t)offset], toCopy);
		bytesCopied = toCopy;
	}

	// Null-terminate if requested
	if (request->flags & FSASYNC_FLAGS_NULLTERMINATE) {
		if (bytesCopied < (size_t)nBytesToRead) {
			((char*)pDest)[bytesCopied] = '\0';
		}
		else {
			((char*)pDest)[nBytesToRead] = '\0';
		}
	}

	// Fire callback
	__int64 result = (bytesCopied > 0 || nBytesToRead == 0) ? FSASYNC_OK : FSASYNC_ERR_READING;
	pPerformAsyncCallback(fs, request, pDest, (uint32_t)bytesCopied, result);

	if (fs->fwLevel >= 6) {
		pLogFileAccess(fs, "async", request->pszFilename, pFileAccessLoggingPointer);
	}

	Msg("[Opus] SyncRead: file='%s' offset=%lld bytes=%lld => returned=%zu  status=%s\n",
		request->pszFilename, request->nOffset, request->nBytes,
		bytesCopied,
		(result == FSASYNC_OK ? "OK" : "ERR"));
	// Simple debug snippet:
	FILE* f = fopen((std::string("debug_decoded") + std::to_string(rand()) +std::string(".wav")).c_str(), "wb");
	fwrite(ctx.decodedBuffer.data(), 1, ctx.decodedBuffer.size(), f);
	fclose(f);

	return result;
}

// --------------------------------------------------------------------
// Main Hook function
// --------------------------------------------------------------------
__int64 __fastcall Hooked_CBaseFileSystem__SyncRead(
	CBaseFileSystem* filesystem,
	FileAsyncRequest_t* request)
{
	static bool bInited = false;
	if (!bInited) {
		bInited = true;
		pFreeAsyncFileHandle = reinterpret_cast<void(__fastcall*)(std::uint64_t)>(G_filesystem_stdio + 0x38610);
		pGetOptimalReadSize = reinterpret_cast<std::uint64_t(__fastcall*)(CBaseFileSystem*, void*, std::uint64_t)>(G_filesystem_stdio + 0x4B70);
		pPerformAsyncCallback = reinterpret_cast<void(__fastcall*)(CBaseFileSystem*, FileAsyncRequest_t*, void*, std::uint64_t, std::uint64_t)>(G_filesystem_stdio + 0x1F200);
		pReleaseAsyncOpenedFiles = reinterpret_cast<void(__fastcall*)(CRITICAL_SECTION*)>(G_filesystem_stdio + 0x233C0);
		pLogFileAccess = reinterpret_cast<void(__fastcall*)(CBaseFileSystem*, const char*, const char*, void*)>(G_filesystem_stdio + 0x9A40);

		pAsyncOpenedFilesCriticalSection = reinterpret_cast<CRITICAL_SECTION*>(G_filesystem_stdio + 0xF90A0);
		pAsyncOpenedFilesTable = G_filesystem_stdio + 0xF90D0;
		pFileAccessLoggingPointer = reinterpret_cast<void*>(G_filesystem_stdio + 0xCDBC0);
	}

	// Quick validation
	if (request->nBytes < 0 || request->nOffset < 0) {
		if (request->pfnCallback) {
			request->pfnCallback(request, 0, FSASYNC_ERR_FILEOPEN);
		}
		return FSASYNC_ERR_FILEOPEN;
	}


	// Is this a .wav?
	if (EndsWithWav(request->pszFilename)) {
		// See if there's a matching .opus
		std::string opusName = MakeOpusFilename(request->pszFilename);
		if (FSOperations::FileExists(filesystem, opusName.c_str(), request->pszPathID)) {
			// Attempt to decode entire .opus in memory 
			if (OpenOpusContext(request->pszFilename, opusName, filesystem, request->pszPathID)) {
				// Return data from our in-memory WAV
				return HandleOpusRead(filesystem, request);
			}
		}
	}

	// Otherwise normal read
	return HandleOriginalRead(filesystem, request);
}
