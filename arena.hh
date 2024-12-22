#pragma once

// NOTE(mrsteyk): Big power of 2 to align, biggest align one would ever wish for.
#define ARENA_HEADER_SIZE 128

// Unreal Engine type bit booleans
typedef uint64_t ArenaFlags;
enum
{
    ArenaFlag_NoChain = (1 << 0),
};

typedef struct Arena Arena;
struct Arena
{
    Arena* current;
    Arena* prev;

    // NOTE(mrsteyk): keep note of parameters for chaining
    ArenaFlags flags;
    uint64_t res_init;
    uint64_t cmt_init;

    // NOTE(mrsteyk): first pos is global start, second is current local pos
    uint64_t pos_init;
    uint64_t pos;

    uint64_t res;
    uint64_t cmt;
};
static_assert(sizeof(Arena) <= ARENA_HEADER_SIZE);

typedef struct ArenaParams ArenaParams;
struct ArenaParams
{
    uint64_t reserve;
    uint64_t commit;
    ArenaFlags flags;
};

typedef struct TempArena TempArena;
struct TempArena
{
    Arena* arena;
    uint64_t start_pos;

    // NOTE(mrsteyk): C++ interface I never use.
    TempArena(Arena* arena);
    ~TempArena();
};

// TODO(mrsteyk): will I ever change those?
//                64kb might become the new page size in the distant future, Apple Silicon does has already iirc.
#define ARENA_DEFAULT_RESERVE    (32ull << 20)
#define ARENA_DEFAULT_COMMIT     (64ull << 10)
#define ARENA_DEFAULT_FLAGS      0

// NOTE(mrsteyk): C++ is just C with classes strikes again
Arena* arena_alloc(uint64_t reserve_ = ARENA_DEFAULT_RESERVE, uint64_t commit_ = ARENA_DEFAULT_COMMIT, uint64_t flags_ = ARENA_DEFAULT_FLAGS);
//#define arena_alloc(...) arena_alloc_(&(ArenaParams){.reserve = ARENA_DEFAULT_RESERVE, .commit = ARENA_DEFAULT_COMMIT, .flags = ARENA_DEFAULT_FLAGS, __VA_ARGS__})
void arena_release(Arena* arena);

void* arena_push_nz(Arena* arena, uint64_t size, uint64_t align = 16);
static void*
arena_push(Arena* arena, uint64_t size, uint64_t align = 16) { return memset(arena_push_nz(arena, size, align), 0, size); }
// Get full arena-chain position.
uint64_t arena_get_pos(Arena* arena);
void     arena_pop_to(Arena* arena, uint64_t pop_pos);
void     arena_pop_by(Arena* arena, uint64_t amount);
void     arena_clear(Arena* arena);

// TODO(mrsteyk): move out

// NOTE(mrsteyk): Returns a null-terminated string to be safe.
static char* arena_strdup(Arena* arena, const char* s, size_t len = -1)
{
    if (len == size_t(-1))
    {
        len = strlen(s);
    }

    auto ret = (char*)arena_push(arena, len + 1);
    memcpy(ret, s, len);
    ret[len] = 0;

    return ret;
}

// NOTE(mrsteyk): Returns a null-terminated string to be safe.
static wchar_t* arena_wstrdup(Arena* arena, const wchar_t* s, size_t len = -1)
{
    if (len == size_t(-1))
    {
        len = wcslen(s);
    }

    auto ret = (wchar_t*)arena_push(arena, (len + 1) * sizeof(wchar_t));
    memcpy(ret, s, len * sizeof(wchar_t));
    ret[len] = 0;

    return ret;
}