#pragma once

#include <cstdint>

namespace r2port::shaders {

// Replaces only the PIXEL shader of selected R1 `UnlitTwoTexture` materials (client materialsystem_dx11.dll, retail
// timestamp 0x54874238) with one of our own, so props and brushes keep the stock vertex shader, vertex format and draw
// path. A material opts in through a parameter the class otherwise ignores while `$godray` is 0: set
// `$godrayMinFadeDist` to kOverrideIdBase + id (see r2_shader_override.cpp for the id table).
//
// The hook wraps the class' draw-state function (vtable slot 9, RVA 0x2FA10), calls the original, and then rewrites the
// pixel shader name (`a4+24`) and static pixel combo (`a4+72`). The replacement file comes from the shader cache
// (`<name>_ps40`) and must have a single static combo 0 and a single dynamic variant, therefore the material must also
// set `$nofog 1` (the fog dynamic combo would index past our one-entry variant table).
//
// Unsupported builds install nothing and log why; a missing shader leaves the stock shader in place and logs loudly once.
void InstallShaderOverrides(std::uintptr_t materialSystemBase);

} // namespace r2port::shaders
