#include "core.h"

#include "arena.hh"

Arena*
arena_alloc(uint64_t reserve_, uint64_t commit_, uint64_t flags_)
{
    Assert(IsPow2(reserve_));
    Assert(IsPow2(commit_));

    uint64_t reserve = AlignPow2(reserve_, 4096);
    uint64_t commit = AlignPow2(commit_, 4096);

    Arena* arena = (Arena*)VirtualAlloc(0, reserve, MEM_RESERVE, PAGE_READWRITE);
    if (!arena)
    {
#if BUILD_DEBUG
        __debugbreak();
#else
        MessageBoxW(0, L"Arena allocation failed!\nBlame mrsteyk or something.", L"Arena allocation fail!", MB_ICONERROR);
        ExitProcess(ERROR_OUTOFMEMORY | (0b11ul << 30));
#endif
    }

    Assert(VirtualAlloc(arena, commit, MEM_COMMIT, PAGE_READWRITE) != 0);

    arena->current = arena;
    arena->prev = 0;

    arena->flags = flags_;
    arena->res_init = reserve_;
    arena->cmt_init = commit_;

    arena->pos_init = 0;
    arena->pos = ARENA_HEADER_SIZE;

    arena->res = reserve;
    arena->cmt = commit;

    return arena;
}

void
arena_release(Arena* arena)
{
    Arena* p = 0;
    for (Arena* c = arena->current; !!c; c = p)
    {
        p = arena->prev;
        VirtualFree(c, 0, MEM_RELEASE);
    }
}

//- mrsteyk: memory allocation and deallocation

void*
arena_push(Arena* arena, uint64_t size, uint64_t align)
{
    Arena* current = arena->current;
    uint64_t pos_align = AlignPow2(current->pos, align);
    uint64_t pos_post = pos_align + size;

    if (current->res < pos_post && !(current->flags & ArenaFlag_NoChain))
    {
        Arena* n;

        uint64_t res = current->res_init;
        uint64_t cmt = current->cmt_init;
        if (size + ARENA_HEADER_SIZE > res)
        {
            // TODO(mrsteyk): actual page size.
            res = cmt = AlignPow2(size + ARENA_HEADER_SIZE, 4096);
        }
        n = arena_alloc(res, cmt, current->flags);

        n->pos_init = current->pos_init + current->res;
        n->prev = current;
        arena->current = n;
        current = n;

        pos_align = AlignPow2(current->pos, align);
        pos_post = pos_align + size;
    }

    if (current->cmt < pos_post)
    {
        uint64_t cmt_new = AlignPow2(pos_post, current->cmt_init);
        if (cmt_new > current->res)
        {
            cmt_new = current->res;
        }

        uint64_t c_cmt = current->cmt;
        Assert(VirtualAlloc((uint8_t*)current + c_cmt, cmt_new - c_cmt, MEM_COMMIT, PAGE_READWRITE));
        current->cmt = cmt_new;
    }

    void* ret = 0;
    if (current->cmt >= pos_post)
    {
        ret = (uint8_t*)current + pos_align;
    }

    Assert(ret);

    return ret;
}

uint64_t
arena_get_pos(Arena* arena)
{
    Arena* c = arena->current;
    return c->pos_init + c->pos;
}

void
arena_pop_to(Arena* arena, uint64_t pop_pos)
{
    Arena* c = arena->current;
    uint64_t pop = pop_pos;
    if (pop_pos < ARENA_HEADER_SIZE)
    {
        pop = ARENA_HEADER_SIZE;
    }

    for (Arena* p = 0; c->pos_init >= pop; c = p)
    {
        p = c->prev;
        VirtualFree(c, 0, MEM_RELEASE);
    }

    arena->current = c;
    uint64_t pos = pop - c->pos_init;
    c->pos = pos;
}

void
arena_pop_by(Arena* arena, uint64_t amount)
{
    uint64_t pos = arena_get_pos(arena);
    // NOTE(mrsteyk): can't be equals because ARENA_HEADER_SIZE?
    if (pos > amount) {
        arena_pop_to(arena, pos - amount);
    }
}

//- mrsteyk: TempArena

TempArena::TempArena(Arena* arena)
{
    this->arena = arena;
    this->start_pos = arena_get_pos(arena);
}

TempArena::~TempArena()
{
    arena_pop_to(this->arena, this->start_pos);
}
