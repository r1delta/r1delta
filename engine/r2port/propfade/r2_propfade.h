#pragma once

#include <cstdint>

// R2 "Model Draw Distance" for static props on converted R2 maps (docs/r2port.md, "Static prop draw distance").
//
// Retail R2 multiplies the fade distance of every static prop by the video setting `fadeDistScale` (menu: Normal 1.0, Far 1.5,
// Very Far 2.25; engine.dll gathers the visible props with (max(1, fadeDistScale) * prop fade)^2). Retail R1 has no such
// factor, so the per-prop R2 fade distances that bspdown copies into the sprp lump cull the props at 1/2.25 of the distance
// R2 shows them at on its top setting. The R1 loader (engine.dll sub_18018B710) bakes the prop's squared fade distance into the
// prop record once per level; this module scales that value after the loader returns. Nothing runs per frame.
namespace r2port::propfade {

// engine.dll was loaded at engineBase: checks the retail R1 build stamp and the hook target bytes, registers the
// r2port_prop_fade_scale cvar and the r2port_prop_fade_status command and detours the prop loader. Scaling is active only
// while the loaded level is a converted R2 map (r2port::IsR2PortMap of the last opened maps/*.bsp).
void InstallEngine(std::uintptr_t engineBase);

} // namespace r2port::propfade
