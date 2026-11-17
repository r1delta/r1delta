#pragma once
#include "titan_insignia_image.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Wire format of custom Titan insignias.
//
// 1. Client: the userinfo convar `_r1dins` holds "<instance>" or "<instance>:<id>". <instance> is
//    16 hex digits drawn once per game process; <id> names the client's own insignia
//    (TitanInsignia::ComputeId). The engine sends the convar in the signon NET_SetConVar and again
//    whenever it changes; the NET_SetConVar writer appends the packed insignia to such a message as
//    `_r1dinsd` entries.
// 2. Server: validates the packed insignia against <id> and relays it to every R1Delta client as
//    svc_StringCmd `r1d_insignia data <id> <part> <parts> <chunk>`, then names who wears it with
//    `r1d_insignia set <entity index> <id or ->` (shared/titan_insignia_server.cpp paces and limits
//    this). A client receives each insignia once per <instance>; a map change resends only what may
//    have been lost when the engine cleared the reliable stream.
namespace TitanInsignia::Wire {

constexpr char kMarkerConVar[] = "_r1dins";
constexpr char kUploadChunkConVar[] = "_r1dinsd";
constexpr char kCommand[] = "r1d_insignia";
constexpr char kNoInsignia[] = "-";

// NET_SetConVar values hold 259 characters; a command line holds 512.
constexpr std::size_t kUploadChunkChars = 252;
constexpr std::size_t kCommandChunkChars = 400;
// Base64 of the largest zstd frame a 16 KB top mip can produce.
constexpr std::size_t kMaxPackedChars = 23 * 1024;
constexpr std::size_t kMaxParts = (kMaxPackedChars + kCommandChunkChars - 1) / kCommandChunkChars;

struct Marker {
	std::uint64_t instance = 0;
	std::string id; // empty: no insignia
};

std::string FormatMarker(const Marker& marker);
bool ParseMarker(std::string_view text, Marker& marker);

// zstd-compressed top mip as unpadded base64url, which needs no quoting in a console command.
bool Pack(const TopMip& mip, std::string& packed);
// False unless `packed` decodes to exactly kTopMipBytes.
bool Unpack(std::string_view packed, TopMip& mip);

}
