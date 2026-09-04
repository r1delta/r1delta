#pragma once

#include "core.h"

namespace r1delta::gpu_latency
{
inline constexpr char kDisableLaunchArgument[] = "-r1delta_disable_gpu_low_latency";

inline bool IsDisabled()
{
    return HasEngineCommandLineFlag(kDisableLaunchArgument);
}
} // namespace r1delta::gpu_latency
