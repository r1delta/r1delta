// Dynamic playlist buffers for the dedicated server's engine.
//
// engine_ds.dll loads playlists.txt into a fixed 160000-byte static buffer and dies with
// Error("playlists.txt is larger than the buffer") at 159999 bytes. The retail engine's
// loader instead queries the file size through the filesystem interface and allocates
// size + 2 for the read. This ports that behaviour onto engine_ds.dll: the loader is
// detoured with the retail flow (filesystem Size -> IMemAlloc -> bind -> ReadFile ->
// parse -> compress) and the buffers are served through the engine's own accessors, so
// playlists of any realistic size load. Small files are also copied into the stock
// static buffer so the engine's direct readers (the remote-playlist paths) keep working.

#include "core.h"

#include <cstdint>
#include <cstring>

// ---- engine_ds.dll layout (RVA) ---------------------------------------------
static constexpr uintptr_t RVA_LOADER           = 0xB9560;  // playlists.txt loader
static constexpr uintptr_t RVA_GET_RAW_PTR      = 0xB8F30;  // () -> raw buffer pointer
static constexpr uintptr_t RVA_GET_RAW_SIZE     = 0xB8F40;  // () -> raw size
static constexpr uintptr_t RVA_GET_COMP         = 0xB89E0;  // (void** ptr, int* size) -> has compressed blob
static constexpr uintptr_t RVA_PARSE            = 0x31D870; // (ctx, "playlists", buffer, 0, 0, 0) -> bool
static constexpr uintptr_t RVA_COMPRESS         = 0x13CA10; // (dst, unsigned* dstSize, src, unsigned srcSize) -> bool
static constexpr uintptr_t RVA_BUF_CTOR         = 0x325810; // CUtlBuffer ctor (0, 0, 0)
static constexpr uintptr_t RVA_BUF_BIND         = 0x3259C0; // CUtlBuffer external bind (ptr, size, 0, 0)
static constexpr uintptr_t RVA_FS_GLOBAL        = 0x203DAF0; // filesystem object base
static constexpr uintptr_t RVA_MEMALLOC_GLOBAL  = 0x4069D0;  // allocator cell (IMemAlloc**)
static constexpr uintptr_t RVA_RAW_SIZE_STATIC  = 0x206B1D8;
static constexpr uintptr_t RVA_COMP_SIZE_STATIC = 0x206B1DC;
static constexpr uintptr_t RVA_RAW_STATIC       = 0x206B2F0; // the stock 160000-byte buffer

static constexpr size_t RAW_FLOOR   = 160000; // keeps the stock remote-download cap (159999) safe
static constexpr size_t STOCK_LIMIT = 159998; // the biggest file the stock buffer can hold

template <typename T>
static T Engine(uintptr_t rva)
{
	return reinterpret_cast<T>(G_engine_ds + rva);
}

// IMemAlloc: vtable[1] = Alloc(size) (tier0!CMimMemAlloc::Alloc), vtable[5] = Free(ptr)
static void* MemAlloc(size_t n)
{
	void** cell = *Engine<void***>(RVA_MEMALLOC_GLOBAL);
	if (!cell)
		return nullptr;
	if (!*cell)
	{
		using CreateFn = void* (*)();
		static CreateFn create = reinterpret_cast<CreateFn>(
			GetProcAddress(GetModuleHandleA("tier0.dll"), "CreateGlobalMemAlloc"));
		if (!create)
			return nullptr;
		*cell = create();
	}
	void* alloc = *cell;
	void** vtable = *reinterpret_cast<void***>(alloc);
	using AllocFn = void* (__fastcall*)(void*, size_t);
	return reinterpret_cast<AllocFn>(vtable[1])(alloc, n);
}

static void MemFree(void* p)
{
	if (!p)
		return;
	void** cell = *Engine<void***>(RVA_MEMALLOC_GLOBAL);
	if (!cell || !*cell)
		return;
	void* alloc = *cell;
	void** vtable = *reinterpret_cast<void***>(alloc);
	using FreeFn = void (__fastcall*)(void*, void*);
	reinterpret_cast<FreeFn>(vtable[5])(alloc, p);
}

// the dynamic playlist state
static char* g_rawBuf = nullptr;
static char* g_compBuf = nullptr;
static unsigned g_rawSize = 0;
static unsigned g_compSize = 0;

static char (*oPlaylistLoader)(void* ctx);
static void* (*oPlaylistRawPtr)();
static unsigned (*oPlaylistRawSize)();
static char (*oPlaylistCompressed)(void** outPtr, int* outSize);

static char __fastcall PlaylistLoader_Detour(void* ctx)
{
	// the stock path handling: playlists.txt, or -playlistFile <path>
	const char* path = "playlists.txt";
	const char* override = nullptr;
	using CommandLineFn = void* (*)();
	static CommandLineFn commandLine = reinterpret_cast<CommandLineFn>(
		GetProcAddress(GetModuleHandleA("tier0.dll"), "CommandLine"));
	if (commandLine)
	{
		void* cmd = commandLine();
		void** vt = *reinterpret_cast<void***>(cmd);
		using GetParmFn = bool (__fastcall*)(void*, const char*, const char**);
		if (reinterpret_cast<GetParmFn>(vt[4])(cmd, "-playlistFile", &override) && override)
			path = override;
	}

	// the filesystem interface (the object at the global + 8, as in the stock loader)
	void* fsObj = *Engine<void**>(RVA_FS_GLOBAL);
	if (!fsObj)
		return 0;
	void* fs = static_cast<char*>(fsObj) + 8;
	void** fsvt = *reinterpret_cast<void***>(fs);
	using SizeFn = int (__fastcall*)(void*, const char*, const char*);
	using ReadFn = bool (__fastcall*)(void*, const char*, const char*, void*, int, int, int);

	int size = reinterpret_cast<SizeFn>(fsvt[6])(fs, path, "GAME"); // vtable +48 = size
	if (size <= 0)
		return 0;

	// buffers sized from the file, like the retail loader
	MemFree(g_rawBuf);
	MemFree(g_compBuf);
	g_rawBuf = nullptr;
	g_compBuf = nullptr;
	size_t rawCap = size + 2 > RAW_FLOOR ? size + 2 : RAW_FLOOR;
	size_t compCap = 2 * (size_t)size + 65536 > RAW_FLOOR ? 2 * (size_t)size + 65536 : RAW_FLOOR;
	g_rawBuf = static_cast<char*>(MemAlloc(rawCap));
	g_compBuf = static_cast<char*>(MemAlloc(compCap));
	if (!g_rawBuf || !g_compBuf)
		return 0;
	// the stock buffer was zeroed memory and the parser reads to the terminator
	std::memset(g_rawBuf, 0, rawCap);

	// bind the buffer and read the file through a CUtlBuffer, exactly like stock
	unsigned char bufObj[0x80] = {};
	using BufCtorFn = void (__fastcall*)(void*, int, int, int);
	using BufBindFn = void (__fastcall*)(void*, void*, int, int, int);
	Engine<BufCtorFn>(RVA_BUF_CTOR)(bufObj, 0, 0, 0);
	Engine<BufBindFn>(RVA_BUF_BIND)(bufObj, g_rawBuf, (int)rawCap, 0, 0);
	if (!reinterpret_cast<ReadFn>(fsvt[14])(fs, path, "GAME", bufObj, (int)rawCap, 0, 0)) // vtable +112 = ReadFile
		return 0;
	g_rawSize = *reinterpret_cast<unsigned*>(bufObj + 0x20); // the buffer's content count

	// parse and compress, as the stock loader does
	using ParseFn = bool (__fastcall*)(void*, const char*, void*, int, int, int);
	if (!Engine<ParseFn>(RVA_PARSE)(ctx, "playlists", g_rawBuf, 0, 0, 0))
		return 0;
	using CompFn = bool (__fastcall*)(void*, unsigned*, void*, unsigned);
	unsigned outSize = (unsigned)compCap;
	if (Engine<CompFn>(RVA_COMPRESS)(g_compBuf, &outSize, g_rawBuf, g_rawSize))
		g_compSize = outSize;

	// the engine's own statics: keep the direct readers (remote-playlist paths) working
	if (g_rawSize && g_rawSize <= STOCK_LIMIT)
		std::memcpy(Engine<char*>(RVA_RAW_STATIC), g_rawBuf, g_rawSize);
	*Engine<unsigned*>(RVA_RAW_SIZE_STATIC) = g_rawSize;
	*Engine<unsigned*>(RVA_COMP_SIZE_STATIC) = g_compSize;
	return 1;
}

static void* __fastcall PlaylistRawPtr_Detour()
{
	return g_rawBuf ? static_cast<void*>(g_rawBuf) : oPlaylistRawPtr();
}

static unsigned __fastcall PlaylistRawSize_Detour()
{
	return g_rawSize ? g_rawSize : oPlaylistRawSize();
}

static char __fastcall PlaylistCompressed_Detour(void** outPtr, int* outSize)
{
	if (!g_compBuf)
		return oPlaylistCompressed(outPtr, outSize);
	*outPtr = g_compBuf;
	*outSize = (int)g_compSize;
	return 1;
}

// Installed from the engine_ds hook block in engine/core/load.cpp (before MH_EnableHook).
void PlaylistLoader_Init()
{
	MH_CreateHook((LPVOID)(G_engine_ds + RVA_LOADER), &PlaylistLoader_Detour, reinterpret_cast<LPVOID*>(&oPlaylistLoader));
	MH_CreateHook((LPVOID)(G_engine_ds + RVA_GET_RAW_PTR), &PlaylistRawPtr_Detour, reinterpret_cast<LPVOID*>(&oPlaylistRawPtr));
	MH_CreateHook((LPVOID)(G_engine_ds + RVA_GET_RAW_SIZE), &PlaylistRawSize_Detour, reinterpret_cast<LPVOID*>(&oPlaylistRawSize));
	MH_CreateHook((LPVOID)(G_engine_ds + RVA_GET_COMP), &PlaylistCompressed_Detour, reinterpret_cast<LPVOID*>(&oPlaylistCompressed));
}
