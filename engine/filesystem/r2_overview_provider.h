#pragma once

namespace r2port {

// Serves the minimap overview data R1's client reads for an allowlisted R2-only map:
//   resource/overviews/<map>.txt                   built from the R2 resource/overviews/<map>.txt in the map's directory VPK
//   materials/vgui/minimaps/<map>.vmt              HUD minimap material (MiniMap proxy + circular mask)
//   materials/vgui/minimaps/fullscreen/<map>.vmt   fullscreen map material (MiniMap proxy + square mask)
//   materials/vgui/r2/loadscreens/<map>.vmt        map picker / lobby / server browser / end of game picture (script image "r2/loadscreens/<map>"): the loading screen below
//   materials/loadscreens/<map>_widescreen.vmt     loading screen (the same material, the name R1 uses)
// The images themselves, materials/overviews/<map>.vtf and materials/loadscreens/<map>_widescreen.vtf, are converted from the R2 rpak textures "overviews\<map>" and "loadscreens\<map>_widescreen" by the
// R2 material provider (it queues the path with the rest of the map's materials). R1 files always win; nothing is
// served without an R2 install.
void RegisterR2OverviewProvider();

} // namespace r2port
