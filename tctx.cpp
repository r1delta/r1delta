#include "core.h"

#include "tctx.hh"

thread_local TCtx tctx = {};

TCtx::TCtx()
{
	for (size_t i = 0; i < TCTX_ARENA_COUNT; ++i)
	{
		this->arena[i] = arena_alloc();
	}
}

TCtx::~TCtx()
{
	for (size_t i = 0; i < TCTX_ARENA_COUNT; ++i)
	{
		arena_release(this->arena[i]);
	}
}

Arena* TCtx::get_arena_for_scratch(Arena** conflict, size_t conflict_size)
{
	auto arenas = this->arena;

	for (size_t i = 0; i < TCTX_ARENA_COUNT; ++i)
	{
		bool flag = false;
		for (size_t j = 0; j < conflict_size; ++j)
		{
			if (arenas[i] == conflict[j])
			{
				flag = true;
				break;
			}
		}
		if (!flag)
		{
			return arenas[i];
		}
	}

	R1DAssert(!"Unreachable");
	return nullptr;
}