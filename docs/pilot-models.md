# Pilot models: what ships, what was observed

## What ships (3.0.1 and later)

No `models/humans/**` replacement pilot model and no `models/anim/pilot_slide*.mdl` is staged or shipped (`tools/combo/stage-combo.ps1` deletes the 38 manifest files from a root on every stage; the release composition script outside this repo rejects them, `ci/verify_release_payload.ps1` does not check for them). Every pilot class uses the stock model from the game VPKs, and the native slide pose retarget (`client/slide_model_presentation.cpp`) installs no hook unless `R1DELTA_SLIDE_MODEL_PRESENTATION=1`.

## The replacement set (3.0.0 only, 36 model files + `pilot_slide.mdl`, `pilot_slide_source.mdl`)

Compared with the stock retail models with `tools/combo/mdl_compare.py` (stock = `.build/mp-slide/<kit>/source` of the SP tree, replacement = byte-identical copy of the 3.0.0 payload):

- Studio v52, same internal name, same cdtextures and materials, same body parts, same vertex data. `.vvd`, `.dx11.vtx` and `.phy` differ from stock only in their checksum field (4 bytes each); the new mdl checksum is written into all four files (consistent).
- Skeleton: 105 stock bones (male CQ; 73 male BR/DM; 110 female) become 223 (male) / 228 (female). The first N stock bones keep index, name, parent, bind pose; the rest are appended (`jx_c_origin` with parent -1, finger helper bones, face bones). No vertex references an appended bone, so skinning data stays valid.
- Bone flags of every pre-existing bone change (`BONE_HAS_SAVEFRAME_POS 0x200000`, `BONE_HAS_SAVEFRAME_ROT32 0x800000`, `0x1000000` added); appended bones have flags 0 (or 0x20 IK, 0x1a00000 for the root); `posscale`/`rotscale` of several bones change.
- 2 IK chains (stock 0), 7 includes (stock 6: `models/anim/pilot_slide.mdl` appended last, so no existing sequence index moves), a `r1delta_player_pose` keyvalue block at header offsets 312/316.
- `human_male_anims_workspace.mdl` is NOT a new include: it is already in the stock include list of the male models, so a missing-include theory does not apply to it. `pilot_slide.mdl` is the only new include and is a CORE file.

## Observations (listen server, `S:\game\r2fix-PilotModels`, mp_glitch, cheats on, `thirdperson`, engine `screenshot`)

See `tools/combo/pilot_class_visibility.py` (the harness that produced them).

- Stock models (3.0.2 build): all 6 setfiles x TEAM_IMC/TEAM_MILITIA (12 combinations) draw a full body in 3P, via `SetPlayerSetFile` and via the real spawn path (class data + `kill` + `ClientCommand_RespawnPlayer`). Server and client report the same model names.
- Replacement models staged (the 3.0.0 files, native hook off, as in 3.0.0): the same 12 combinations also draw a full body, standing and running (`SetVelocity` driven).
- Replacement models + `R1DELTA_SLIDE_MODEL_PRESENTATION=1` + `R1DELTA_SLIDE_MODEL_ALL_ROOTS=1`: also drawn.
- Single-field variants of the shipped male CQ mdl (bone flag bits cleared, keyvalue block removed, `pilot_slide` include removed, IK chains removed, `numbones` forced to 105) all draw too.
- So own-player third person is not invisible with the replacement files on a listen server. Not covered: other players seen from a second client, a dedicated server that has different model files than the client.

## Gotchas learned while testing

- `SetModel`/`SetPlayerSetFile` to a pilot model that was never loaded keeps the old model on the first call (lazy load); it works on the next call. This happens with stock models too.
- `thirdperson` is reset by every respawn; `screenshot` writes to `<game cwd>\r1\screenshots` (the game's working directory, shared by every window), TGA.
- `SetPlayerSettings` sets the IMC body model regardless of team; the team model comes from `SetPlayerSetFile` or the spawn path.
