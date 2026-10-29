// Dynamic playlist buffers for the legacy dedicated server's engine (engine_ds.dll).
//
// Stock engine_ds.dll loads playlists.txt into a fixed 160000-byte static buffer and dies with
// Error("playlists.txt is larger than the buffer") once the file reaches 159999 bytes (the read
// is capped at 160000 and the loader compares the byte count against 0x270FF). The compressed
// copy that SV_SendServerinfo hands to clients lives in a second 160000-byte static buffer. The
// retail client engine has no such limit: it sizes the read from the file (size + 2).
//
// This file removes the limit on engine_ds.dll without patching sizes in place. Only the loader
// and the three accessors the server-info path reads are detoured; the raw and compressed copies
// are heap buffers sized from the file that was actually read, so the capacity follows the file
// instead of a constant.
//
// Rules this file keeps, because the previous revision of it broke on both:
//  * It is installed when engine_ds.dll is mapped (before DedicatedMain runs), never later. The
//    VGUI dedicated loads playlists.txt from dedicated.dll's start-up path, which is long before
//    server.dll loads; a detour installed from the server.dll load notification misses that call
//    and the stock 160000-byte loader kills the server.
//  * The file is read through a growable CUtlBuffer with no byte cap, then copied into buffers
//    sized from what was read. The previous revision queried Size() and then read into a
//    pre-sized external buffer; in the VGUI start-up state (observed under CDB) that read
//    returned false, while the stock loader's own call shape worked. There is also no "fall
//    back to the stock loader" path: the stock loader is exactly what fails on a large file.
//  * Everything is verified before anything is hooked (module image size, entry bytes of every
//    detoured function, the stock limit constants). If the layout does not match, nothing is
//    installed and a loud line says so; tests/verify_playlist_loader.py checks the same table
//    against the shipped engine_ds.dll.
//
// The startup line "R1Delta: playlists ..." reports the file size, the buffer sizes and the
// payload that every connecting client receives.

#include "core.h"
#include "logging.h"

#include <cstdint>
#include <cstring>

// ---- engine_ds.dll layout (RVA); keep in sync with tests/verify_playlist_loader.py ------------
static constexpr uint32_t kImageSize      = 0x24EF000;
static constexpr uintptr_t RVA_LOADER     = 0xB9560;  // playlists.txt loader: bool (KeyValues* dst)
static constexpr uintptr_t RVA_GET_RAW_PTR  = 0xB8F30;  // () -> raw buffer pointer
static constexpr uintptr_t RVA_GET_RAW_SIZE = 0xB8F40;  // () -> raw size
static constexpr uintptr_t RVA_GET_COMP     = 0xB89E0;  // (void** ptr, int* size) -> has compressed blob
static constexpr uintptr_t RVA_PARSE      = 0x31D870; // (ctx, "playlists", buffer, 0, 0, 0) -> bool
static constexpr uintptr_t RVA_COMPRESS   = 0x13CA10; // (dst, size_t* dstCap/outSize, src, srcSize) -> bool
static constexpr uintptr_t RVA_BUF_CTOR   = 0x325810; // CUtlBuffer ctor (this, 0, 0, 0): growable
static constexpr uintptr_t RVA_FS_GLOBAL  = 0x203DAF0; // filesystem object base (IFileSystem at +8)
static constexpr uintptr_t RVA_MEMALLOC_GLOBAL = 0x4069D0; // IMemAlloc** cell (g_pMemAllocSingleton)
static constexpr uintptr_t RVA_RAW_SIZE_STATIC  = 0x206B1D8; // stock raw size
static constexpr uintptr_t RVA_COMP_SIZE_STATIC = 0x206B1DC; // stock compressed size
static constexpr uintptr_t RVA_RAW_STATIC       = 0x206B2F0; // the stock 160000-byte buffer

static constexpr size_t STOCK_BUFFER = 160000;  // size of the stock static buffers
static constexpr size_t STOCK_LIMIT  = 159998;  // the biggest file the stock raw buffer can hold

// The server-info message every connecting client receives is built in a 0x40000-byte scratch
// buffer (SV_SendServerinfo->msg); SVC_Playlists carries the compressed (or, if compression
// fails, the raw) file. Past this the serverinfo send overflows and every client is kicked with
// "Server info data overflow", so the server refuses to start instead of looking healthy.
static constexpr size_t SERVERINFO_MESSAGE = 0x40000;
static constexpr size_t PAYLOAD_LIMIT = SERVERINFO_MESSAGE - 0x4000;
// Nothing sane is bigger than this; guards the allocation and the 32-bit size fields.
static constexpr size_t FILE_SANITY_LIMIT = 64u * 1024 * 1024;

struct EntrySite
{
	const char* name;
	uintptr_t rva;
	uint8_t bytes[8];
};

static const EntrySite kEntrySites[] = {
	{ "loader",   RVA_LOADER,       { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74 } },
	{ "raw_ptr",  RVA_GET_RAW_PTR,  { 0x48, 0x8D, 0x05, 0xB9, 0x23, 0xFB, 0x01, 0xC3 } },
	{ "raw_size", RVA_GET_RAW_SIZE, { 0x8B, 0x05, 0x92, 0x22, 0xFB, 0x01, 0xC3, 0xCC } },
	{ "comp",     RVA_GET_COMP,     { 0x80, 0x3D, 0xB9, 0x9B, 0xFD, 0x01, 0x00, 0x74 } },
	{ "parse",    RVA_PARSE,        { 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x41, 0x54 } },
	{ "compress", RVA_COMPRESS,     { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C } },
	{ "buf_ctor", RVA_BUF_CTOR,     { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C } },
};

// The stock loader's own size constants, so a rebuilt engine_ds that moved them is noticed.
struct ConstSite
{
	const char* name;
	uintptr_t rva;
	uint8_t bytes[5];
};

static const ConstSite kConstSites[] = {
	{ "stock_cap_compare (cmp eax, 0x270FF)", RVA_LOADER + 0xD0, { 0x3D, 0xFF, 0x70, 0x02, 0x00 } },
};

static uintptr_t g_ds = 0;

template <typename T>
static T Engine(uintptr_t rva)
{
	return reinterpret_cast<T>(g_ds + rva);
}

// ---- IMemAlloc (the allocator the engine's own buffers use) ----------------------------------
// The engine imports tier0's g_pMemAllocSingleton; the import slot holds the address of that
// variable, and the variable holds the IMemAlloc object. vtable[1] = Alloc(size), vtable[5] = Free(ptr).
static void** MemAllocCell()
{
	void** cell = *Engine<void***>(RVA_MEMALLOC_GLOBAL);
	if (cell && !*cell)
	{
		using CreateFn = void* (*)();
		static CreateFn create = reinterpret_cast<CreateFn>(
			GetProcAddress(GetModuleHandleA("tier0.dll"), "CreateGlobalMemAlloc"));
		if (create)
			*cell = create();
	}
	return cell;
}

static void* MemAlloc(size_t n)
{
	void** cell = MemAllocCell();
	if (!cell || !*cell)
		return nullptr;
	void* alloc = *cell;
	void** vtable = *reinterpret_cast<void***>(alloc);
	using AllocFn = void* (__fastcall*)(void*, size_t);
	return reinterpret_cast<AllocFn>(vtable[1])(alloc, n);
}

static void MemFree(void* p)
{
	if (!p)
		return;
	void** cell = MemAllocCell();
	if (!cell || !*cell)
		return;
	void* alloc = *cell;
	void** vtable = *reinterpret_cast<void***>(alloc);
	using FreeFn = void (__fastcall*)(void*, void*);
	reinterpret_cast<FreeFn>(vtable[5])(alloc, p);
}

// ---- the dynamic playlist state ---------------------------------------------------------------
static char* g_rawBuf = nullptr;      // file bytes + terminator
static char* g_compBuf = nullptr;     // compressed copy (or scratch when compression failed)
static size_t g_rawCap = 0;
static size_t g_compCap = 0;
static unsigned g_rawSize = 0;        // file size, what the stock static raw size would hold
static unsigned g_compSize = 0;       // compressed size; 0 when the file did not compress
static bool g_compressed = false;

static char (*oPlaylistLoader)(void* ctx);
static void* (*oPlaylistRawPtr)();
static unsigned (*oPlaylistRawSize)();
static char (*oPlaylistCompressed)(void** outPtr, int* outSize);

// The engine's CUtlBuffer: [0]=memory ptr, +0x10 = grow size (negative: externally owned),
// +0x20 = bytes put. Only the fields the stock loader itself reads are used.
struct BufferView
{
	void* data() const { return *reinterpret_cast<void* const*>(this); }
	int64_t growSize() const { return *reinterpret_cast<const int64_t*>(reinterpret_cast<const uint8_t*>(this) + 0x10); }
	unsigned put() const { return *reinterpret_cast<const unsigned*>(reinterpret_cast<const uint8_t*>(this) + 0x20); }
};

static void ReleaseBuffer(void* buf)
{
	const BufferView* view = static_cast<const BufferView*>(buf);
	if (view->growSize() >= 0 && view->data())
		MemFree(view->data()); // same ownership test the stock loader uses before it frees
}

static char PlaylistLoader_Detour(void* ctx)
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
	using ReadFn = bool (__fastcall*)(void*, const char*, const char*, void*, int, int, int);

	// Read the whole file into a growable engine buffer (nMaxBytes 0 = no cap).
	alignas(16) unsigned char bufObj[0x80] = {};
	using BufCtorFn = void (__fastcall*)(void*, int, int, int);
	Engine<BufCtorFn>(RVA_BUF_CTOR)(bufObj, 0, 0, 0);
	if (!reinterpret_cast<ReadFn>(fsvt[14])(fs, path, "GAME", bufObj, 0, 0, 0)) // vtable +112 = ReadFile
	{
		ReleaseBuffer(bufObj);
		return 0; // missing file: the caller reports "Failed to load playlists" like stock
	}
	const BufferView* view = reinterpret_cast<const BufferView*>(bufObj);
	const size_t fileSize = view->put();
	const void* fileData = view->data();
	if (fileSize == 0 || !fileData)
	{
		ReleaseBuffer(bufObj);
		return 0;
	}
	if (fileSize > FILE_SANITY_LIMIT)
	{
		const unsigned reported = static_cast<unsigned>(fileSize);
		ReleaseBuffer(bufObj);
		Error("playlists.txt is %u bytes; R1Delta refuses playlist files over %u bytes.\n", reported, static_cast<unsigned>(FILE_SANITY_LIMIT));
		return 0;
	}

	// new buffers, sized from this file; the previous ones stay in place until this load succeeds
	const size_t rawCap = fileSize + 2;
	// compress() copies the whole input into its output before it compresses, so the output
	// buffer must hold the raw file. A result that does not shrink counts as "not compressed".
	const size_t compCap = fileSize + 4096;
	char* rawBuf = static_cast<char*>(MemAlloc(rawCap));
	char* compBuf = static_cast<char*>(MemAlloc(compCap));
	if (!rawBuf || !compBuf)
	{
		ReleaseBuffer(bufObj);
		MemFree(rawBuf);
		MemFree(compBuf);
		Error("playlists.txt is %u bytes and R1Delta could not allocate %u bytes for it.\n", static_cast<unsigned>(fileSize), static_cast<unsigned>(rawCap + compCap));
		return 0;
	}
	std::memcpy(rawBuf, fileData, fileSize);
	std::memset(rawBuf + fileSize, 0, rawCap - fileSize); // the parser reads to the terminator
	ReleaseBuffer(bufObj);

	// parse and compress, as the stock loader does
	using ParseFn = bool (__fastcall*)(void*, const char*, void*, int, int, int);
	if (!Engine<ParseFn>(RVA_PARSE)(ctx, "playlists", rawBuf, 0, 0, 0))
	{
		MemFree(rawBuf);
		MemFree(compBuf);
		return 0;
	}
	using CompFn = bool (__fastcall*)(void*, size_t*, void*, size_t);
	size_t outSize = compCap;
	const bool compressed = Engine<CompFn>(RVA_COMPRESS)(compBuf, &outSize, rawBuf, fileSize);

	const size_t payload = compressed ? outSize : fileSize;
	if (payload > PAYLOAD_LIMIT)
	{
		const unsigned reported = static_cast<unsigned>(fileSize);
		MemFree(rawBuf);
		MemFree(compBuf);
		// tier0's printf has no %zu: every count is passed as a 32-bit value
		Error(
			"playlists.txt is %u bytes and sends %u bytes (%s) to every connecting client; the server-info "
			"message holds %u. Shrink the file (the stock engine limit was %u bytes).\n",
			reported, static_cast<unsigned>(payload), compressed ? "compressed" : "uncompressed, compression failed",
			static_cast<unsigned>(PAYLOAD_LIMIT), static_cast<unsigned>(STOCK_LIMIT));
		return 0;
	}

	// publish
	MemFree(g_rawBuf);
	MemFree(g_compBuf);
	g_rawBuf = rawBuf;
	g_compBuf = compBuf;
	g_rawCap = rawCap;
	g_compCap = compCap;
	g_rawSize = static_cast<unsigned>(fileSize);
	g_compressed = compressed;
	g_compSize = compressed ? static_cast<unsigned>(outSize) : 0;

	// The engine's own statics: the direct readers (-downloadPlaylists paths) keep the stock
	// buffer. A file that fits is mirrored there; one that does not leaves it empty.
	const bool fitsStock = fileSize <= STOCK_LIMIT;
	if (fitsStock)
		std::memcpy(Engine<char*>(RVA_RAW_STATIC), rawBuf, fileSize);
	*Engine<unsigned*>(RVA_RAW_SIZE_STATIC) = fitsStock ? g_rawSize : 0;
	*Engine<unsigned*>(RVA_COMP_SIZE_STATIC) = 0;

	// tier0's printf has no %zu: every count is passed as a 32-bit value
	Msg("R1Delta: playlists \"%s\": %u bytes read, raw buffer %u bytes, compressed buffer %u bytes; "
		"each client receives %u bytes (%s) of the %u-byte server-info message. Stock engine buffer: %u bytes (%s).\n",
		path, g_rawSize, static_cast<unsigned>(rawCap), static_cast<unsigned>(compCap), static_cast<unsigned>(payload),
		compressed ? "compressed" : "uncompressed", static_cast<unsigned>(SERVERINFO_MESSAGE),
		static_cast<unsigned>(STOCK_BUFFER), fitsStock ? "file would also have fit" : "file exceeds it, dynamic buffers in use");
	return 1;
}

static void* __fastcall PlaylistRawPtr_Detour()
{
	return g_rawBuf ? static_cast<void*>(g_rawBuf) : oPlaylistRawPtr();
}

static unsigned __fastcall PlaylistRawSize_Detour()
{
	return g_rawBuf ? g_rawSize : oPlaylistRawSize();
}

static char __fastcall PlaylistCompressed_Detour(void** outPtr, int* outSize)
{
	if (!g_rawBuf)
		return oPlaylistCompressed(outPtr, outSize);
	if (!g_compressed)
		return 0; // the caller then sends the raw file ("Sending uncompressed playlists to client")
	*outPtr = g_compBuf;
	*outSize = static_cast<int>(g_compSize);
	return 1;
}

static bool VerifyEngineLayout(uintptr_t base)
{
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
	{
		Warning("R1Delta: dynamic playlist buffers NOT installed: engine_ds.dll has no PE header.\n");
		return false;
	}
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
	if (nt->OptionalHeader.SizeOfImage != kImageSize)
	{
		Warning("R1Delta: dynamic playlist buffers NOT installed: engine_ds.dll image size 0x%X is not 0x%X; "
			"the stock 160000-byte playlists.txt limit applies.\n", nt->OptionalHeader.SizeOfImage, kImageSize);
		return false;
	}
	for (const EntrySite& site : kEntrySites)
	{
		if (std::memcmp(reinterpret_cast<const void*>(base + site.rva), site.bytes, sizeof(site.bytes)) != 0)
		{
			Warning("R1Delta: dynamic playlist buffers NOT installed: %s at engine_ds+0x%X does not match; "
				"the stock 160000-byte playlists.txt limit applies.\n", site.name, static_cast<unsigned>(site.rva));
			return false;
		}
	}
	for (const ConstSite& site : kConstSites)
	{
		if (std::memcmp(reinterpret_cast<const void*>(base + site.rva), site.bytes, sizeof(site.bytes)) != 0)
		{
			Warning("R1Delta: dynamic playlist buffers NOT installed: %s at engine_ds+0x%X does not match; "
				"the stock 160000-byte playlists.txt limit applies.\n", site.name, static_cast<unsigned>(site.rva));
			return false;
		}
	}
	return true;
}

// Installed from the engine_ds load notification in engine/core/load.cpp (before MH_EnableHook),
// so the first load, which the VGUI dedicated performs during start-up, is already covered.
void PlaylistLoader_Init(uintptr_t engineDsBase)
{
	if (!engineDsBase || !VerifyEngineLayout(engineDsBase))
		return;
	g_ds = engineDsBase;

	struct Hook
	{
		uintptr_t rva;
		LPVOID detour;
		LPVOID* original;
	};
	const Hook hooks[] = {
		{ RVA_LOADER,       reinterpret_cast<LPVOID>(&PlaylistLoader_Detour),     reinterpret_cast<LPVOID*>(&oPlaylistLoader) },
		{ RVA_GET_RAW_PTR,  reinterpret_cast<LPVOID>(&PlaylistRawPtr_Detour),     reinterpret_cast<LPVOID*>(&oPlaylistRawPtr) },
		{ RVA_GET_RAW_SIZE, reinterpret_cast<LPVOID>(&PlaylistRawSize_Detour),    reinterpret_cast<LPVOID*>(&oPlaylistRawSize) },
		{ RVA_GET_COMP,     reinterpret_cast<LPVOID>(&PlaylistCompressed_Detour), reinterpret_cast<LPVOID*>(&oPlaylistCompressed) },
	};
	// all or nothing: the loader detour without the accessor detours would serve clients a stale
	// copy of the stock buffer
	size_t created = 0;
	for (const Hook& hook : hooks)
	{
		const MH_STATUS status = MH_CreateHook(reinterpret_cast<LPVOID>(g_ds + hook.rva), hook.detour, hook.original);
		if (status != MH_OK)
		{
			Warning("R1Delta: dynamic playlist buffers NOT installed: MH_CreateHook engine_ds+0x%X failed (%d); "
				"the stock 160000-byte playlists.txt limit applies.\n", static_cast<unsigned>(hook.rva), static_cast<int>(status));
			for (size_t i = 0; i < created; ++i)
				MH_RemoveHook(reinterpret_cast<LPVOID>(g_ds + hooks[i].rva));
			return;
		}
		++created;
	}
}
