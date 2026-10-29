# DLC map ownership

Five groups, bit n of every mask (bit 0 = base game, always owned):

| group | maps | owned when |
|---|---|---|
| 1-3 | the Titanfall DLC packs (`mp_runoff`/`swampland`/`wargames`, `mp_haven`/`switchback`/`harmony_mines`, `mp_zone_18`/`backwater`/`sandtrap`) | `vpk/client_mp_delta_common.bsp.pak000_000.vpk` is non-empty and every map of the group has its `englishclient_<map>.bsp.pak000_dir.vpk`, or every map has its own non-empty content pack plus directory VPK |
| 4 | Titanfall Online maps `mp_box`, `mp_mia`, `mp_nest2` | delta_common is non-empty and the three directory VPKs exist |
| 5 | the 20 converted Titanfall 2 maps (`kR2MapAllowlist`) | a valid Titanfall 2 install (same search as the map provider, `R1DELTA_TITANFALL2_PATH` first) contains every file of `engine/filesystem/r2_install_manifest.inc` at its recorded size or larger. Starpaks are not listed (players delete the HD ones). Regenerate with `tools/r2port/gen_install_manifest.py <Titanfall2 root>` |

A dedicated server uses the `server_` packs and falls back to the `client_` ones. A zero-byte file is a launcher placeholder and counts as missing. Sizes are read from an open handle: `GetFileAttributesEx` reports 0 for the 12.5 GB delta_common pack.

## Convars

* `dlc1_enabled` .. `dlc5_enabled` (userinfo). Recomputed from disk at startup and before every connect and map load; the stock startup check is patched out in `r1delta.wpatch`, which used to leave them at their default `1`. The dedicated server registers all five (it drops userinfo it has no convar for). On an R1O fake-dedicated (TFO) server the engine ships dlc1-3, `dlc4_enabled`/`dlc5_enabled` are registered as R1O userinfo convars.
* `dlc1_override` .. `dlc5_override` (server hoster). Defaults `1 1 1 0 0`. The override makes the rotation pretend every player owns the group; it never helps a host that does not own the group itself, and it does not stop the kick below.
* `serverDLCMapGroups` (replicated) is now computed by the server: bit n is set when the host owns group n and (every connected human owns it or `dlcN_override`). It is what `ServerHasDLCMapGroupEnabled` and the playlist availability check read. Remove the old `serverDLCMapGroups 15` line from `cfg/autoexec_delta.cfg`.

## Enforcement

* Server, at connect (`HookedCBaseClientConnect`; on TFO `CGameClient::Connect`, engine_r1o `+0x142B80`, which receives the packet's userinfo): a client without the group of the map the server is on is rejected with the text below. TFO clients send `dlc1_enabled` .. `dlc5_enabled` in the connect packet (verified: `vars=35 dlc5=0`).
* Server, on `map`/`changelevel` (engine.dll `0x1CD40`, engine_ds `0xA3610`; on TFO `changelevel`/`changelevel2`, engine_r1o `+0x186B60`/`+0x186C70`): connected clients without the new map's group are kicked.
* TFO only: the engine's map to group lookup (engine_r1o `+0x58910`, behind `GetDLCMapGroupForMap` and the playlist availability mask) is extended to five groups, except for the call inside its own `CGameClient::CheckDLC`, which only knows dlc1-3 and would otherwise disconnect every client on a group 4/5 map. The TFO host owns groups 1-4 (its engine loads those maps itself) and group 5 when its Titanfall 2 install is valid. `serverDLCMapGroups` is republished from the host frame, so `ServerHasDLCMapGroupEnabled` and the playlist mask follow the connected players.
* Client, signon NEW (`0x765B0`) and the `map` command (`0x136FC0`): the map is not loaded when the group is not owned; the client disconnects with the message instead of reading a missing pack.
* Messages: groups 1-4 "Redownload the game from the Delta launcher."; group 5 "Verify that Titanfall 2 is installed correctly, and purchase it if you do not own it."

Playtest host: launch with `+dlc5_override 1` (players without Titanfall 2 are still kicked when a dlc5 map starts).

Not covered: engine_ds refusing a `map` the host itself lacks; on TFO the `map` command (host state NewGame) is not hooked, only `changelevel`/`changelevel2` (what rotation uses) kick.
