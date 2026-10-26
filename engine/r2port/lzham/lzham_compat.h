#pragma once

// LZHAM uses C99 limit macro names as constants in its own namespace.
#include <limits.h>
#include <stdint.h>
#if defined(_WIN32) && !defined(WIN32)
#define WIN32 1
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#ifndef LZHAM_64BIT_POINTERS
# if defined(_WIN64)
#  define LZHAM_64BIT_POINTERS 1
# else
#  define LZHAM_64BIT_POINTERS 0
# endif
#endif

#pragma push_macro("INT8_MIN")
#pragma push_macro("INT8_MAX")
#pragma push_macro("UINT8_MAX")
#pragma push_macro("INT16_MIN")
#pragma push_macro("INT16_MAX")
#pragma push_macro("UINT16_MAX")
#pragma push_macro("INT32_MIN")
#pragma push_macro("INT32_MAX")
#pragma push_macro("UINT32_MAX")
#pragma push_macro("INT64_MIN")
#pragma push_macro("INT64_MAX")
#pragma push_macro("UINT64_MAX")
#undef INT8_MIN
#undef INT8_MAX
#undef UINT8_MAX
#undef INT16_MIN
#undef INT16_MAX
#undef UINT16_MAX
#undef INT32_MIN
#undef INT32_MAX
#undef UINT32_MAX
#undef INT64_MIN
#undef INT64_MAX
#undef UINT64_MAX
#include "lzham_types.h"
#pragma pop_macro("UINT64_MAX")
#pragma pop_macro("INT64_MAX")
#pragma pop_macro("INT64_MIN")
#pragma pop_macro("UINT32_MAX")
#pragma pop_macro("INT32_MAX")
#pragma pop_macro("INT32_MIN")
#pragma pop_macro("UINT16_MAX")
#pragma pop_macro("INT16_MAX")
#pragma pop_macro("INT16_MIN")
#pragma pop_macro("UINT8_MAX")
#pragma pop_macro("INT8_MAX")
#pragma pop_macro("INT8_MIN")
