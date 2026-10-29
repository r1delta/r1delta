#pragma once

#include <cstdint>

// Opt-in post pass for the R1 client: an R2-style display curve and RCAS sharpening, both default OFF (docs/r2port.md,
// "Post effects"). One full-screen draw (`r1d/post`, screenspace_general with the runtime shader `r1d_post`) runs right
// after the stock engine_post draw and before the HUD. With every cvar at 0 no hook is installed and nothing runs.
namespace r2port::postfx {

// Registers the file provider that answers materials/r1d/post.vmt (no cost until asked).
void RegisterMaterialProvider();

// client.dll was loaded at clientBase: checks the retail build stamp and the hook target bytes, registers the cvars
// (r2port_display_curve, r2port_sharpen_amount, r2port_sharpen_denoise, r2port_sharpen_width, r2port_postfx_stats,
// r2port_postfx_srgb) and the r2port_postfx_status command. Installs nothing until a cvar asks for it.
void InstallClient(std::uintptr_t clientBase);

} // namespace r2port::postfx
