#pragma once

namespace r2port {

// Serves the generated R1 sound scripts for the converted R2 maps (see engine/r2port/audio/r2_sound_scripts.h):
//   scripts/r2port_sounds.txt         sound aliases (listed in the CORE game_sounds_manifest.txt)
//   scripts/soundscapes_<map>.txt     per-map soundscapes R1 loads on level start
// Nothing is served without an R2 install; the files exist in memory only.
void RegisterR2SoundProvider();

} // namespace r2port
