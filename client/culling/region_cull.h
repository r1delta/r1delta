#pragma once

#include <string>

// Hooks the client.dll region-cull task (retail sub_1802319F0) with an optimized,
// numerically identical reimplementation of the same traversal. See
// region_cull_core.h for the recovered model and the optimization notes.
//
// Runtime controls:
//   r1delta_cull_optimize  1  use the reimplementation (0 = call the original)
//   r1delta_cull_verify    0  run the original for real, run the reimplementation into a
//                             scratch output without scheduler side effects, and compare
//   r1delta_cull_status       console command: counters, verify mismatches, first mismatch
void InitRegionCullHooks();

// One-line cull state for crash reports and the console command.
std::string GetRegionCullStatusLine();
