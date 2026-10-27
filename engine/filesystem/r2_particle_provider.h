#pragma once

#include <string>
#include <string_view>

namespace r2port {

// Path of the merged R1 PCF the provider builds from the R2 install's particle files.
inline constexpr const char* kR2ParticlePcfPath = "particles/r2port.pcf";

// Serves particles/r2port.pcf (R2 particle systems converted to R1 PCF, see engine/r2port/particles)
// and the R2-only particle materials (materials/particle/**) from the R2 install's mp_common VPK.
// R1 files always win; nothing is served without an R2 install.
void RegisterR2ParticleProvider();

// True when the R2 install is present and the converted PCF can be built.
bool R2ParticlesAvailable();

// Maps an R2 effect name (info_particle_system / prop_dynamic effect_name) to the converted system
// name. False when the effect is not converted (R1 draws its own definition of it).
bool R2ParticleTargetName(std::string_view r2System, std::string& target);

} // namespace r2port
