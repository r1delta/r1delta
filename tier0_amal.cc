#define _CRT_SECURE_NO_WARNINGS
#define DLL1_EXPORTS
#define _WINDOWS
//#define _USRDLL
#define UNICODE
#define _UNICODE

#if 0
#if BUILD_DEBUG
# define _DEBUG
#else
# define NDEBUG
#endif
#endif

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "vstdlib")
#pragma comment(lib, "tier0_orig")

//~ mrsteyk: H files

#include <iostream>
#include <vector>
#include <array>
#include <string>
#include <string_view>
#include <filesystem>

#include <winsock2.h>

#include "core.h"
#include "arena.hh"
#include "tctx.hh"

/*
#include "beardbgon.h"
#include "bitbuf.h"
#include "client.h"
#include "color.h"
#include "compression.h"
#include "coordsize.h"
#include "core.h"
#include "cvar.h"
#include "dedicated.h"
#include "engine.h"
#include "factory.h"
#include "filecache.h"
#include "filesystem.h"
#include "framework.h"
#include "keyvalues.h"
#include "load.h"
#include "logging.h"
#include "memory.h"
#include "model_info.h"
#include "navmesh.h"
#include "netadr.h"
#include "netchanwarnings.h"
#include "patcher.h"
#include "persistentdata.h"
#include "physics.h"
//#include "platform.h"
#include "predictionerror.h"
#include "sendmoveclampfix.h"
#include "squirrel.h"
#include "TableDestroyer.h"
#include "thirdparty\nlohmann\json.hpp"
//#include "thirdparty\nlohmann\json_fwd.hpp"
#include "thirdparty\zstd-1.5.6\zstd.h"
#include "utils.h"
#include "vector.h"
#include "weaponxdebug.h"
*/

//~ mrsteyk: C++ files
#include "arena.cc"
#include "tctx.cpp"

#include "bitbuf.cpp"
#include "compression.cpp"
#include "dedicated.cpp"
#include "engine.cpp"
#include "factory.cpp"
#include "eos_logging_manager.cpp"
#include "filecache.cpp"
#include "filesystem.cpp"
#include "fake_ip_layer.cpp"
#include "eos_layer.cpp"
#include "eos_network.cpp"
#include "net_hooks.cpp"
#include "load.cpp"
#include "logging.cpp"
#include "memory.cpp"
#include "masterserver.cpp"
#include "model_info.cpp"
#include "navmesh.cpp"
#include "netchanwarnings.cpp"
#include "newbitbuf.cpp"
#include "patcher.cpp"
#include "predictionerror.cpp"
#include "sendmoveclampfix.cpp"
#include "squirrel.cpp"
#include "TableDestroyer.cpp"
#include "utils.cpp"

//#include "security_fixes.cc"

#include "cvar.cpp"
#include "client.cpp"
#include "persistentdata.cpp"
#include "dllmain.cpp"

#define ARRAYSIZE(A) RTL_NUMBER_OF_V2(A)
#include "keyvalues.cpp" // Includes vsdk
#undef ARRAYSIZE

// Add the following includes from the first version
#include "vsdk\tier0\dbg.cpp"
#include "vsdk\tier0\platformtime.cpp"
#include "vsdk\tier0\valve_tracelogging.cpp"
#include "vsdk\tier1\ipv6text.c"
#include "vsdk\tier1\netadr.cpp"
#include "vsdk\tier1\utlbuffer.cpp"
#include "vsdk\vstdlib\strtools.cpp"
#undef ARRAYSIZE

//- mrsteyk: third party
#if BUILD_DEBUG
# define _DEBUG
#else
# define NDEBUG
#endif
#include "thirdparty\zstd\zstd.c"
#undef swap
#undef NDEBUG
#undef _DEBUG
