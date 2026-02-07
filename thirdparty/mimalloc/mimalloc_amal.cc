#if !BUILD_DEBUG
# define NDEBUG
# include "..\thirdparty\mimalloc\include\mimalloc.h"
# include "..\thirdparty\mimalloc\include\mimalloc\types.h"
# undef NDEBUG
#endif

#include "..\thirdparty\mimalloc\src\alloc-aligned.c"
#include "..\thirdparty\mimalloc\src\alloc-posix.c"
#include "..\thirdparty\mimalloc\src\alloc.c"
#include "..\thirdparty\mimalloc\src\arena.c"
#include "..\thirdparty\mimalloc\src\bitmap.c"
#include "..\thirdparty\mimalloc\src\init.c"
#include "..\thirdparty\mimalloc\src\libc.c"
#include "..\thirdparty\mimalloc\src\prim\prim.c"
#include "..\thirdparty\mimalloc\src\options.c"
#include "..\thirdparty\mimalloc\src\page.c"
#include "..\thirdparty\mimalloc\src\random.c"
#include "..\thirdparty\mimalloc\src\segment-map.c"
#include "..\thirdparty\mimalloc\src\segment.c"
#include "..\thirdparty\mimalloc\src\os.c"
#include "..\thirdparty\mimalloc\src\stats.c"

#if !BUILD_DEBUG
# define NDEBUG
#endif
#include "..\thirdparty\mimalloc\src\heap.c"
#undef NDEBUG