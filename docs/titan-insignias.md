# Custom Titan insignias

A player puts images in `<game>/r1/insignias` (PNG, JPEG, TGA, BMP, GIF, PSD; up to 16 MB and 512x512, larger images are refused with their size in the menu). The Titan insignia menu lists them before the stock insignias. Picking one draws it on all of that player's Titans for every R1Delta client in the match; picking a stock insignia turns it off. The stock insignia the loadout names is still what servers and clients without the feature draw.

## Convars

| Convar | Side | Default | Meaning |
|---|---|---|---|
| `delta_titan_insignia` | client, archived | `""` | File name in `r1/insignias`; empty for none |
| `delta_titan_insignias_show` | client, archived | `1` | `0` draws only your own custom insignia; other players' Titans keep their stock insignia. Settings > R1Delta > "Others' Custom Insignias" |
| `delta_titan_insignias` | server | `1` | `0`: every client is told each player wears none, and nothing more is relayed; `1` again relays what players wear now |
| `_r1dins` | client userinfo | | Marker, see below |

UI natives: `GetTitanInsigniaFiles()` (`[{ file, image, error }]`), `GetTitanInsignia()` (`{ file, image, error }`), `SetTitanInsignia( file )` (returns why the file cannot be used, or `""`), `OpenTitanInsigniaFolder()`.

## Image

Every stock insignia is slot 1 of the `atlas_titan`, `ogre_titan` and `stryder_titan` skin families: its own material `models/titans/custom_decals/decal_pack_01/<name>.vmt` (VertexLitGeneric, `$decal`, `$translucent`) with a 128x128 DXT5 VTF, 8 mips, flags `0x2200`. A custom insignia is fitted into 128x128 keeping its aspect ratio (the rest transparent) and DXT5-encoded (stb_image, stb_image_resize2, stb_dxt). Only the 16 KB top mip travels; every client rebuilds the lower mips with the same integer alpha-weighted box filter (bcdec decodes the top mip), so all clients draw identical textures. The id is the first 16 bytes of the SHA-256 of the top mip, in hex. `shared/titan_insignia_image.*`, tests in `tests/titan_insignia_tests.cpp`.

## Drawing

client.dll keeps Source's material proxy registry: `CMaterialProxyDict` (getter `+0x1AE4F0`, vtable slot 2 `Add(name, factory)`), with 84 proxies including the dead `CPlayerLogoProxy`. The 112 stock insignia VMTs in CORE name the proxy `R1DeltaTitanInsignia`, registered on the first host frame (the DLL load notification comes before client.dll's static initializers build the dictionary). On bind it resolves the renderable to its entity (`IClientRenderable[0]` then `IClientUnknown[7]`, as `CEntityMaterialProxy::OnBind` at `+0x22A1D0` does), the entity to its player (`ffa_targeting::ResolveClientPlayerOwner`: the player itself, or the boss player of an auto-Titan), and sets `$basetexture` to that player's texture or back to the stock texture (`IMaterial` slots 54 FindVar, 61 GetTexture, 73 SetTexture). Decal binds pass a player index instead of a renderable and get the stock texture.

TF1's `IMaterialSystem::FindTexture` (slot 93) only finds textures that are already loaded, so each insignia is loaded as a material: an in-memory file provider serves `materials/models/r1delta/insignia/<id>.vtf` and `.vmt`, and `materials/vgui/r1delta/insignia/<id>.vmt` for menu image panels (the provider registry refuses paths with `..`, so menu images cannot use `../models/...`). The main thread loads the material (`FindMaterial`, slot 85) and references it for the rest of the process. `client/rendering/titan_insignia_render.*`.

## Wire

1. Client: `_r1dins` (userinfo, `FCVAR_PERSIST` so the signon message carries it) holds `<instance>` or `<instance>:<id>`; `<instance>` is 16 random hex digits per game process. The engine sends it at signon and whenever it changes; the NET_SetConVar writer appends the zstd-compressed top mip as unpadded base64url in `_r1dinsd` entries of 252 characters.
2. Server (`shared/titan_insignia_server.cpp`): the NET_SetConVar reader consumes both, checks the data against the id, and relays it to every client that sent a marker as svc_StringCmd `r1d_insignia data <id> <part> <parts> <chunk>` (400 characters per part, 1 to 59 parts), then `r1d_insignia set <entity index> <id or ->`. Limits:
   * A player's insignia changes at most once every 5 s; a newer upload waits for the cooldown, and the newest one wins.
   * Commands to each client leave a queue, 8 every 250 ms (about 15 KB/s), so a join does not put every insignia on the reliable stream at once.
   * A client gets each id once per `<instance>` (the server remembers at most 256 ids per client). A signon (connect or map change) drops the client's queue and sends again what was queued or left the queue in the last 10 s, which the engine's reliable-stream clear may have lost; everything else only gets its `set` line again.
   * A new connection in a slot, or a slot whose connection ends (checked four times a second from the host frame; `Dlc_ServerFrame`'s hook on the legacy dedicated server), broadcasts `set <slot> -`.

   Clients without the feature never send a marker and never receive these commands. R1O fake dedicated servers do not relay insignias.
3. Client: `r1d_insignia` is server-executable (whitelisted in `IsValidServerCommand`); parts are assembled, unpacked and checked against the id before the files are served. A client keeps at most 128 insignias received from servers (its own and the menu's do not count); further ones show the stock insignia. `shared/titan_insignia.cpp`.

Measured: a simple image packs into one part; random noise, the worst case, into 27.

## Testing a build

`python tools/dev-overlay.py apply --dll .build/dll/tier0.dll --core <CORE checkout>` copies the DLL and the CORE branch's changed files into the installed `app-*/r1delta` (backing up what it replaces); `restore` undoes it. An addon that ships its own `scripts/vscripts/ui/_loadout_shared.nut` (Frontier Defense 2, WeaponPortDev) hides the menu part: the insignia folder tiles need CORE's copy.
