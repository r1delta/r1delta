#pragma once

#if defined(__WINE__)
  #define DLLEXPORT __attribute__((visibility("default")))
#elif defined(_MSC_VER)
  #define DLLEXPORT
#else
  #define DLLEXPORT __declspec(dllexport)
#endif

#include "com_object.h"
#include "com_pointer.h"

#include "log.h"
#include "util_likely.h"
#include "util_math.h"

#include <dxgi1_6.h>
#include <d3d11_4.h>
