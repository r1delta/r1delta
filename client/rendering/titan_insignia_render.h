#pragma once
#include "shared/titan_insignia_image.h"

#include <cstdint>
#include <string>

// Draws custom Titan insignias.
//
// Every stock insignia material (CORE materials/models/titans/custom_decals/decal_pack_01/<name>.vmt)
// carries the material proxy `R1DeltaTitanInsignia`. When it binds for an entity that belongs to a
// player wearing a custom insignia, it points $basetexture at that insignia's texture, and back at the
// stock texture otherwise. The insignia files are served from memory:
//   materials/models/r1delta/insignia/<id>.vtf
//   materials/models/r1delta/insignia/<id>.vmt       VertexLitGeneric, loads the texture for the proxy
//   materials/vgui/r1delta/insignia/<id>.vmt         Basic, for menu image panels
//
// R1 retail client.dll: timestamp 55038DB8, image size 39A4000.
namespace TitanInsigniaRender {

constexpr int kMaxPlayers = 64;

// Registers the proxy with client.dll's material proxy dictionary and the file provider. Run once
// client.dll has initialized, before a map loads.
bool Install(std::uintptr_t clientBase);
bool IsInstalled();

// Main thread. Serves an insignia's files; its material loads on the next Frame(). `local` insignias (the
// player's own and the menu's) are always kept; at most 128 received from servers are, and false means
// this one was not.
bool Add(const std::string& id, const TitanInsignia::TopMip& mip, bool local);
bool Has(const std::string& id);
// Image name for a menu image panel (SetImage) showing the insignia.
std::string MenuImage(const std::string& id);

// Main thread. The insignia the player with entity index `entityIndex` (1-based) wears; empty for none.
void Assign(int entityIndex, const std::string& id);
void ClearAssignments();

// Main thread, every frame: loads added materials and publishes which player wears which texture.
// Without `showOthers` only the local player's own insignia is drawn.
void Frame(bool showOthers);

}
