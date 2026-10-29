#pragma once

#include <cstdint>

// Makes the R1 client load particles/r2port.pcf (the converted R2 particle systems) right after it
// reads particles_manifest.txt. Does nothing when the R2 install is absent or client.dll is not the
// retail R1 build the RVAs were taken from.
void InstallClientParticleHook(std::uintptr_t clientBase);
