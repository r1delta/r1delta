#pragma once

#include <cstdio>

#include "com_include.h"

#define DXVK_IID_FMT "{%08lx-%04hx-%04hx-%02hhx%02hhx-%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx}"
#define DXVK_IID_ARG(iid) iid.Data1, iid.Data2, iid.Data3, iid.Data4[0], iid.Data4[1], iid.Data4[2], iid.Data4[3], iid.Data4[4], iid.Data4[5], iid.Data4[6], iid.Data4[7]
#define DXVK_LOG(prefix, fmt, ...) std::fprintf(stderr, "d3d11: " prefix ": " fmt "\n", ##__VA_ARGS__)
#define DXVK_LOG_STUB() DXVK_LOG("stub", "%s", __func__)
#define DXVK_LOG_FUNC(prefix, fmt, ...) DXVK_LOG(prefix, "%s: " fmt, __func__, ##__VA_ARGS__)
#define DXVK_LOG_UNK_IFACE(iid) DXVK_LOG_FUNC("warn", "Unknown interface query " DXVK_IID_FMT, DXVK_IID_ARG(iid))
