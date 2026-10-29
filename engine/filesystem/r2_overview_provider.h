#pragma once

namespace r2port {

// Serves the minimap overview data R1's client reads for an allowlisted R2-only map:
//   resource/overviews/<map>.txt                   built from the R2 resource/overviews/<map>.txt in the map's directory VPK
//   materials/vgui/minimaps/<map>.vmt              HUD minimap material (MiniMap proxy + circular mask)
//   materials/vgui/minimaps/fullscreen/<map>.vmt   fullscreen map material (MiniMap proxy + square mask)
// The image itself, materials/overviews/<map>.vtf, is converted from the R2 rpak texture "overviews\<map>" by the
// R2 material provider (it queues the path with the rest of the map's materials). R1 files always win; nothing is
// served without an R2 install.
void RegisterR2OverviewProvider();

} // namespace r2port
