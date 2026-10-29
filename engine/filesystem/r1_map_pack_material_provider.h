#pragma once

namespace r2port {

// Registers a last-resort `materials/` provider that serves files packed in the stock R1
// per-map VPKs (for example tools/toolsskybox.vmt). R1's own lookup and the R2 provider
// are consulted first.
void RegisterR1MapPackMaterialProvider();

} // namespace r2port
