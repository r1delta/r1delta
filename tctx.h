#pragma once

#include "arena.h"

constexpr size_t TCTX_ARENA_COUNT = 2;
typedef struct TCtx TCtx;
struct TCtx {
	Arena* arena[TCTX_ARENA_COUNT];

	TCtx();
	~TCtx();

	Arena* get_arena_for_scratch(Arena** conflict = nullptr, size_t conflict_size = 0);
};

// TODO(mrsteyk): remove constructor from TLS callbacks to not hinder OS stuff.
extern thread_local TCtx tctx;