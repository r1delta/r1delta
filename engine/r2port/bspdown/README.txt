bspdown: Titanfall 2 (rBSP v37) multiplayer BSP -> Titanfall 1 (rBSP v29) BSP
==========================================================================

Rewrites an R2 BSP image into one the R1 engine's map loaders accept. No R2 data is
changed in meaning except where the R1 loader forces it (rules below). Textures,
models, entities, shaders, game logic and engine patches are out of scope.

Files
-----
  bspdown.hpp        public API (DowngradeBsp, Options, Report, Error)
  bspdown.cpp        driver, header/lump layout, rules 1, 2, 6, 8 (small lumps)
  tricoll.cpp        rule 3  (bevel stream 11 bit -> 10 bit)
  collision.cpp      rules 4, 7 (collision part), 9 (contents), 18 (grid padding)
  sprp.cpp           rule 5  (static props v13 -> v12)
  meshclip.cpp       rules 7 (render mesh clipping, bounds), 10 (cubemap per sort), 15 (no-draw meshes)
  physics.cpp       rules 12 (world-collision placeholder) and 14 (all-absent 0x46 table)
  shadow.cpp         rule 17 (shadow environments / CSM tree / shadow meshes, options only)
  lighting.cpp       rule 20 (world lights, probe light indices, real-time lights)
  main.cpp           CLI
  internal.hpp       shared structs
  check_bspdown.py   invariant checker (independent numpy implementation)
  roundtrip_diff.py  lump-by-lump diff for the R1 -> bsp_regen -> bspdown round trip
  run_all.py         convert + check the six required maps (extracts missing ones with corvpk)
  run_all23.py       same for all 23 R2 maps, one at a time (deletes each extraction)
  build_msvc.bat     MSVC build
  analysis/          one-off analysis scripts used while working out the formats
  dec/loaders.c      Hex-Rays output of the R1 loaders that were read (analysis only, do not commit)
  out/               reports, check logs, converted BSPs of the six required maps

Build
-----
  g++ (MSYS2 ucrt64, C++17):
    g++ -std=c++17 -O2 -Wall -Wextra -o bspdown.exe main.cpp bspdown.cpp tricoll.cpp collision.cpp sprp.cpp meshclip.cpp tangents.cpp physics.cpp cubemaps.cpp shadow.cpp lighting.cpp
  MSVC (VS 2022 or later, x64): run build_msvc.bat  (cl /std:c++17 /O2 /EHsc ...)
  Both were built and produce byte identical output on mp_lf_deck.

Use
---
  bspdown.exe <in_r2.bsp> <out_r1.bsp> [--limit 16376] [--rtl-bytes 1..4] [--no-remap-contents]
              [--keep-prop-flag10] [--props keep|hide-scatter|hide-all] [--scatter-min N]
              [--csm-rebase] [--empty-shadow-meshes] [--worldlight-flag2]
              [--cubemaps none|split|keep] [--keep-r2-lumps] [--grid-size N] [--no-grid-pad] [--quiet]
  The per-rule report is printed to stdout. Exit status 1 and a message on stderr when the
  input cannot be converted.

  Library (for the r1delta file hook):
    #include "bspdown.hpp"
    std::vector<uint8_t> r1 = bspdown::DowngradeBsp(r2Data, r2Size);            // throws bspdown::Error
    std::vector<uint8_t> r1 = bspdown::DowngradeBsp(r2Data, r2Size, opts, &report);
  The input must be the complete R2 file with all lumps embedded (the unpacked
  maps/<map>.bsp contains them; see "external lump files" below). Eden (211 MB in,
  182 MB out) converts in 0.25 s on this machine, memory use is about twice the input.

Rules and what was verified
---------------------------
Evidence key: [R1] read in the R1 engine loaders (IDA, copy of engine.dll database);
[data] measured on R1/R2 files; [regen] bsp_regen/src/*.hpp; [bt] bsp_tool.

1. Header [R1: CMapLoadHelper::Init sub_1800ACB10/ACE40]. Ident rBSP, version 29 (the loader
   accepts 27..29), revision kept, lastLump field kept at 127 (the loader reads it as the
   highest lump index). Lumps are re-laid out in index order, 4-byte aligned, fourCC 0. R1
   never reads lump versions except lump 0x36 (must be 1, see rule 8) and ignores 0x42's.

2. Real-time lights, lump 0x69 [R1 sub_1800B0710]. R1 needs length = k * texels with k in 1..4,
   texels summed over lightmap headers of type 1 (all headers in the R2 maps are type 1), and
   treats 0xFF as "no light". R2 stores 9 bytes per texel in the BSP (the .bsp_lump copy from
   the VPK has 8) in a layout that is not understood, so no faithful mapping exists. Output:
   0xFF fill at --rtl-bytes (default 4, the R1 stock width). Consequence: no real-time light
   assignments on lightmapped surfaces. Sky lightmap 0x62 is 8 bytes/texel in R1 and R2 and is
   copied; the converter checks R1's size rule (8 * type-1 texels).
   UPDATE (SHIMMER-ANALYSIS.md): R1's RTL texel IS decoded: 4 bytes = up to 4 light slots, byte = world light index - 1 (light 0 is
   the sun/ambient entry), 0xFF = none; only lights with flag bit 0x8 are listed, ascending, those within their radius (93.7 % of
   rasterized luxels of stock mp_rise agree). Stock mp_box ships all 0xFF, so the fill is a valid state. R2's lump is two RGBA8
   planes per lightmap (R2 matsys sub_18006C2B0: realTimeLights0/1) plus a 1 byte plane in the BSP copy; its fields were not decoded.

3. Tricoll, lumps 0x42/0x44/0x45/0x60/0x61 [R1 sub_180109480, regen tricoll.hpp, data].
   Header 44 bytes, triangles 4 bytes, nodes 16 bytes, bevel starts 2 bytes: identical in R1
   and R2 (node counts match R1's formula on every map). Only the bevel stream differs: 11 bit
   entries (R2) vs 10 bit (R1). The converter walks every header exactly like the R1->R2 code in
   bsp_regen does (distinct (start, count) pairs, count 15 = extended runs of
   {20 bit word: count (7), header (13)} + entries) and rewrites the entries at the same entry
   positions in 10 bit packing, header by header; first_bevel_index is recomputed
   (sum of (10n+31)/32 words). Fails with a specific message when an entry is > 1023, a header
   has > 1024 triangles, an extended record's header index is > 8191, or a run/stream/node range
   leaves its lump. Measured: 5,027,147 entries on eden, all reachable from triangle starts;
   max entry values 733..1022 over 22 maps.
   Round trip: R1 mp_angel_city -> bsp_regen -> bspdown reproduces lump 0x61 and 0x45 byte for byte.
   LIMIT: mp_angel_city (R2) has 9133 tricoll headers. 20954 extended bevel segments there point at
   header indexes >= 8192 (10018 of them close runs of the 941 headers whose own index is >= 8192),
   which R1's 13 bit header field cannot hold, so the converter reports it and stops. The other
   22 maps have at most 6545 headers.

4. Collision grid, lumps 0x55..0x5B [regen main.cpp, bt, data]. R2 maps carry type 0x60
   (static prop) primitives both as single-primitive geosets and as children of multi-primitive
   geosets (738 mixed geosets on eden), so geosets cannot just be deleted. The converter
   compacts the primitive pool and the geoset pool (order preserved, so every cell and every
   multi-geoset range stays contiguous):
     - prop primitives and prop-only geosets are removed;
     - a multi geoset that loses children gets a new range; with one child left it becomes a
       single-primitive geoset (word and bounds of the child); otherwise its unique-contents
       index is recomputed as the OR of its children (this invariant holds in all R1 and R2 files
       checked) and its bounds become the union of the children's;
     - straddle group ids are renumbered 1..N in pool order; Grid.num_straddle_groups = N + 1
       (R1 stock files use max id + 1);
     - cell ranges are remapped, cell count stays nx*ny + models (R1 checks this exactly).
   Grid scale is left at 704 (R1 stock maps use 256). The R1 cell lookup routines I read
   (sub_18010BE50, sub_1801243B0) compute indices from the loaded inverse scale and offsets with
   no hard coded 256; not exercised at runtime.
   R1 consumer found: sub_180109C10 builds a per-brush contents byte table from the geoset and
   primitive words (type bits 0xE0000000 == 0 means brush), consistent with the word layout used.
   Round trip on mp_angel_city (60588 geoset slots) and mp_box: every cell decodes to the same
   geosets/primitives/bounds/contents as the original; lump byte sizes differ because bsp_regen
   duplicates shared geoset ranges per cell and keeps its prop contents values in the table.

5. Static props, game lump 0x23 [R1 sub_18018FD90/B710, regen, bt]. sprp v13 -> v12, header
   (id 'prps', flags 0, version 12, absolute offset, length) rebuilt, model name table copied,
   leaf list written empty, the three header ints kept, 64 -> 84 byte props, trailing R2 count
   and records dropped (count was 0 in all maps checked). Field mapping (R2 offset -> R1 offset):
   origin 0x00->0x00, angles 0x0C->0x0C, scale 0x18->0x48, model 0x1C->0x18, solid/flags/skin/
   cubemap 0x1E..0x23->same, fade distance 0x24 -> fade_min 0x24 AND fade_max 0x28 (the R2 value is
   -1 or a distance, R1 stock props have min == max), lighting origin 0x28->0x2C, diffuse
   0x34->0x40, collision add/remove 0x38/0x3C->0x4C/0x50. Defaults: first_leaf/num_leaves 0
   (R1 never reads them), forced_fade_scale 1.0 (all R1 props have 1.0), cpu/gpu 0,0,0,0
   (all R1 props), disable_x360 0 (R1 does not read it). bsp_regen's R2 layout was confirmed
   against R2 data (diffuse 0xFFFFFFFF at 0x34; bsp_tool's layout with 16 bit collision flags is
   not right). Round trip: all mapped fields identical; first_leaf/num_leaves/disable_x360 differ
   because R1 stock props hold unread garbage there (hex 'ffff', '0900', ...).

6. Light probe refs 0x68: 20 -> 16 bytes (trailing int32 dropped; non-zero in about 99.9% of R2
   records, always 0 in R1, unknown meaning). Probes 0x65 (48), tree 0x67 (8), static prop
   indices 0x66 (4): same sizes in R1/R2, copied. R1 adds 32 to a probe's light index when its
   weight byte is non-zero (sub_1800B0B30); whether R2's indices mean the same is unknown.

7. Bounds [R1: Init-time fatal check in sub_1801B0F90, +-1 padding in sub_1800BB700].
   Render meshes of model 0 with any vertex beyond +-limit: triangles clipped against the box
   (Sutherland-Hodgman), albedo/lightmap uv and step, colour interpolated linearly, normals and
   tangent vectors interpolated and renormalised into new vertex-normal entries, new positions
   appended to 0x03. The clipped mesh gets a fresh vertex block at the end of its vertex lump
   and a fresh material sort (indices are 16 bit relative to the sort's offset), new indices
   at a 6-aligned position with the degenerate padding triangle R1 data has, first_vertex /
   num_vertices / last_vertex recomputed, mesh bounds recomputed (axis aligned, tan_yaw 0).
   Meshes with no triangle left, and TOOLS/TOOLSSKYBOX meshes with any vertex beyond the limit,
   keep their mesh slot (object reference, AABB-tree and level-info lumps index meshes) with 0
   triangles and a zero-size bounds box. World mesh bounds that still exceed are recomputed.
   Model mins/maxs are clamped to the limit. Collision: primitives entirely outside are removed,
   bounds of the others clamped, grid rows/columns lying completely outside are removed (done on
   mp_crashsite3), cells that straddle the limit are kept (cell size 704 cannot be cut). Rule 18 then
   pads the grid with empty cells, so the final grid does extend beyond the limit (those cells are empty).
   NOT clipped: tricoll geometry and brushes (BVH nodes and bevels would need rebuilding),
   float AABB records 0x77/0x63/0x79 (stock R1 maps contain +-inf and beyond-limit records
   there), portal/occluder/shadow vertices. The full 20-map audit found no out-of-limit brush
   in 19 maps; mp_relic02 has two, indices 171 and 172, each referenced once, with max Z
   16639.3516. They are preserved. The fatal extent check tests world-model mins/maxs, not
   brush bounds; these world bounds are clamped. Brush collision beyond the engine trace
   clamp is not runtime-tested.
   Measured: eden: 4 meshes clipped (147 triangles clipped, 553 dropped, 295 new vertices),
   kodai: 1 mesh (beacon_clouds, 4 clipped), crashsite3: 4 TOOLS/TOOLSSKYBOX meshes emptied,
   lf_deck: 1 sky mesh emptied; the other 18 maps need nothing. Per-map lines are in
   out/<map>.report.txt.

8. Other R1 loader requirements found by reading every loader called from sub_18010A590 and
   sub_1800B2630 (Mod_Load* and CollisionBSPData_Load*):
   - World lights 0x36: R1 requires lump version 1, divides by 100, aborts above 4064 lights
     and prints "Invalid worldlight lump version" otherwise, leaving the light array unset. R2 uses
     version 3 (112 bytes) or 2 (108 bytes): the first 100 bytes are the R1 record, version set to 1.
     Maximum over the 22 maps is 458 lights.
   - Cell/portal/PVS lumps 0x6A..0x79, 0x7B, 0x7C..0x7F, 0x63/0x64, planes, texdata, vertices,
     normals, models (32 bytes), cubemaps (16), brushes (32), lightmap headers (8), sky lightmap,
     level info (28): record sizes identical in R2 (59 checks, 'funny lump size' conditions
     verified, all pass on every converted map). Mesh (28 bytes) and mesh bounds (32) sizes match.
   - Lumps R1 never reads that R2 adds are dropped to save space: 0x04..0x07 (about 2.2 MB on
     eden), 0x37, 0x7A. --keep-r2-lumps keeps them.
   - R1-only lumps 0x3E, 0x46 are absent in R2 and left absent; 0x1D has only the R2 terminator
     record, which R1's loader (sub_1801099F0) handles.
   - Mesh vertex type: R2 has vertex_type(u8) + cubemap(i8) where R1 has a uint16; the cubemap
     byte is cleared (rule 10).

9. Contents (not requested; added because the R1 trace masks need it) [data, regen]. R2 world
   collision has no SOLID bit: unique-contents values are patterns like 0xEB0280. bsp_regen's
   R1 -> R2 prop mapping is SOLID -> 0xEB0280, WINDOW -> 0xE30240, GRATE -> 0xA30240; this
   rule inverts it: values holding a whole signature get the R1 flag back (0xEB0280 -> 0x1,
   0xFB0280 -> 0x100001, 0xE30240 -> 0x2, 0xA30240 -> 0x8). These results coincide with values in
   stock R1 maps (0x1, 0x100001 are the first entries of R1 tables). Other R2 values are left
   alone (partial clip combinations such as 0xEA0280, and 0x800000 variants that match R1 values
   0x230000, 0x2B0000, 0x310000 when 0x800000 is removed). Disable with --no-remap-contents.
   Not verified in the engine.

10. Cubemaps per material sort [R1 sub_1800B4590, data]. R2 gives every sort cubemap 0 and stores
    the cubemap per mesh; R1 stores it per sort and, for every sort whose cubemap is not -1, asks
    the material system for "<MAP>/<material>_<x>_<y>_<z>" (the patch materials R1 packs into the
    BSP's pakfile as materials/maps/<map>/<material>_<x>_<y>_<z>.vmt; R2 maps carry only
    cubemaps.hdr.vtf there). Stock R1 mp_angel_city has sort cubemap -1 on 3077 of its 12344 lit-bump meshes,
    so -1 on lit sorts is a normal state. Modes (--cubemaps):
      none  (default): every sort gets -1, plain material names, no patch materials required;
      split: sorts are duplicated so each mesh's R2 cubemap lands on its sort (needs patch materials);
      keep : sorts keep the R2 value 0 (would look for patch materials for cubemap 0).

11. Vertex tangent frames, lumps 0x49 and 0x4A (tangents.cpp). R1 stores tangentS and tangentT as plain
    indices into 0x1E and its loader (R1O sub_1800E1E80) does not bounds check them. R2 stores
    (S index | 0x80000000 if the UV mapping is mirrored) and 0. Per vertex: s = field & 0x7FFFFFFF,
    h = -1 if the flag is set else +1, t = normalize(h * cross(normal, s)); t is appended to 0x1E
    (deduplicated on exact float bits, old entries unchanged), field0 = s, field1 = index of t.
    Same encoding verified in the R2 unlit-TS lump (0x4A). Position, normal and tangent indices of
    every record in 0x47..0x4B are validated (also unreferenced records) and the conversion fails if
    one is out of range. S is copied from R2 and is not exactly perpendicular to the normal
    (|dot| up to 0.5, stock R1 up to 0.29); T is exactly perpendicular. No degenerate case occurred
    (fallback: arbitrary perpendicular). Checker: handedness rule vs UV-derived handedness on >= 200k
    vertices: 100% on stock R1 mp_box, 99.9% on converted R2 (the residual is identical in the R2 input).
    Normals grow by about 0.5 * lit-bump count (eden 898115 -> 1528292); nothing else indexes 0x1E.

12. Stub world physics, lump 0x1D (physics.cpp). R2 BSPs hold only the 16 byte terminator
    {-1,-1,0,0}; the R1 server dereferences solids[0] of the world collide (CPhysicsHook "world_0")
    and crashes. When the input has no record for model 0 with solids >= 1, the lump is replaced by the
    2315 byte stub from MRVN-Radiant titanfall.cpp EmitStubs (GPL-2.0-or-later, origin in the file
    header): {model 0, dataSize 2148, keySize 135, solids 1} + VPHY blob + key values
    "staticsolid {index 0, contents 33570819} virtualterrain {} virtualtrimeshes {} materialtable {...}"
    + terminator. It is a placeholder object, not collision geometry; world traces still use
    tricoll/brushes. Runtime result (lead): with this stub and rule 11 the converted mp_lf_stacks loads
    and is playable on R1. --physics none keeps the input lump. check_bspdown.py asserts that the output
    lump parses as {model,dataSize,keySize,solids} records ending with {-1,-1,0,0} and that model 0 has
    >= 1 solid (--physics stub), or is unchanged (--physics none).

13. Cubemap textures in the pakfile, lump 0x28 (cubemaps.cpp). R1 (Mod_LoadCubemapSamples,
    sub_1800B5A80) loads "<map>/c<x>_<y>_<z>" (".hdr" first when HDR is on; x,y,z = the three
    int32 of each lump 0x2A entry), then "<map>/cubemapdefault", then "engine/defaultcubemap". R2's
    pakfile holds one file, materials/maps/<map>/cubemaps.hdr.vtf. The pakfile is rebuilt (store
    only, DOS date/time 0, version 10, the 32 byte "XZP1 0" end comment like the stock R1 pakfile;
    deterministic) with, for every 0x2A entry, c<x>_<y>_<z>.hdr.vtf and c<x>_<y>_<z>.vtf, plus
    cubemapdefault.vtf / .hdr.vtf (average of the clamped cubemaps). Names use the int32 origins verbatim
    (c-12919_12458_13006). R2's cubemaps.hdr.vtf is dropped; other stored entries would be kept (none
    exist). Lump 0x2A is untouched, every sort cubemap stays -1 (no patch VMTs).
    R2 source parse (confirmed on all maps run): VTF 7.5, header 104 bytes + resource table (0x30 = image
    data, CMA, CRC), format 66 BC6H (unsigned float; no negative or non-finite values occurred), mips
    smallest first, inside a mip frame then face; 256 mip 0, 9 mips, exactly header + 24 faces x 87,408
    bytes (65536+16384+4096+1024+256+64+16+16+16), frames == number of 0x2A entries (checked). Decoder:
    bcdec.h (bcdec_bc6h_float), MIT / Unlicense by Sergii "iOrange" Kudlai, vendored unchanged.
    R1 DECODE (reverse engineered from the shipped R1 pixel shaders, DXBC disassembled with
    D3DDisassemble; evidence in out\evidence\*.asm):
      vertexlit_and_unlit_generic_ps50 and lightmappedgeneric_ps50 sample the cube with
        sample_l texturecube r4 = t5(reflect vector), lod
        r1.x = r4.w * 3.984375 + 0.015625          i.e. (alphaByte + 1) / 64 for an 8 bit alpha
        env  = r4.xyz * r1.x * fresnel * c_envMapLightScale (cb2[12].x)
      The lightmaps use the same factor ((alpha + 1) / 64), the albedo is multiplied by 4.59479.
      Flags: sub_180063DD0 (materialsystem_dx11) maps VTF flags to engine flags: 0x4000 = cubemap,
      0x80000 (PRE_SRGB) = sRGB view for non-float formats; the stock cubemaps (0x620c =
      0x2000 8bit alpha | 0x4000 envmap | 0x200 nolod | 0xC clamp s,t) and ours have no 0x80000, so rgb is
      read as linear unorm. RGBA16F cubemaps have alpha 1.0 -> factor 4.0 (the previous output: R2 values
      x4, about 36x brighter than the stock R1 average, which is the likely cause of the bright
      tinted reflections on the viewmodel; the stock mp_box / cubemapdefault F16 files are black).
    So the stock encoding is value = rgb/255 * (alpha + 1)/64, and the stock files (mp_rise,
    mp_angel_city c*.hdr.vtf) are 64x64 RGBA8888, 7 mips, flags 0x620c, header 96 bytes with a CRC resource,
    alpha 4..38 (never below 4), reflectivity = mean((rgb/255)^2.2) of mip 0 (reproduced exactly).
    Output (this version): c<x>_<y>_<z>.hdr.vtf = exactly that format (header copied from stock, reflectivity
    computed like stock, 131160 bytes): per pixel alpha = smallest byte with (alpha+1)/64 >= max channel,
    at least 4, rgb = value/scale; values above 4.0 clamp (none occurred); mip 0 = 4x4 box of the 256 mip,
    further mips 2x2 box of the values, each mip encoded on its own. c<x>_<y>_<z>.vtf = DXT5 32x32 flags
    0x4000 (stock ships black placeholders here): linear value/4 with alpha 255. cubemapdefault.hdr.vtf uses the
    same encoding of the average cube (stock: black F16 placeholder; the default is only used when a
    cubemap file is missing).
    EXPOSURE (--cubemap-scale, default 4): R1 decodes with c_envMapLightScale and the engine's own units, R2's BC6H values are in R2's
    units. The earlier default 1/6 was an empirical fit on mp_rise / mp_angel_city (R2/R1 cube luminance 8.5, 2.4, 10.6). The mp_wargames
    rosetta test (rosetta\ROSETTA.md) shows the ratio is map dependent (R2/R1 = 0.57 at the same origin on mp_wargames, face ratios
    R1/R2 3.7 2.4 2.3 2.8 1.24 0.43), so no exact constant exists. In game on mp_wargames at fixed exposure
    (mat_force_tonemap_scale 1) the converted map reached 69 % of the stock R1 brightness with 1/6 and 86 % with 4
    (4 is also the largest factor that keeps R2 values up to 1.0 encodable: stock alpha 255 -> 4.0; about 1 % of the
    brightest mip 0 pixels of the main cube clamp). Use --cubemap-scale 1/6 to get the old output.
    CLIPPING at scale 4 (measured with analysis\cubeclip.py: convert at --cubemap-scale 0.125, rescale; max R2 value 2.6 to 9.0,
    most maps below 5.2; mip 0 pixels with any channel above 4.0 / energy kept by the clamp, scale 2 | 3 | 4):
    glitch 0.03 | 3.8 | 12.3 % (95.6 % kept at 4; sky slot 4: 58 % of its texels clip, 90 % of its luminance kept),
    colony02 0.4 | 0.8 | 1.6 % (94 % kept), eden 1.7 | 2.8 | 5.1 % (95 %; ground slot 5: 16 %, 79 % kept), thaw 0.2 | 0.6 | 1.8 % (98 %),
    lf_meadow 0.7 | 4.5 | 9.9 % (88 %), lf_stacks 2.5 | 10.5 | 19.2 % (90.5 %), lf_township 0.7 | 4.5 | 11.5 % (88 %),
    lf_traffic 1.0 | 7.7 | 16.7 % (89 %), lf_uma 0.9 | 3.7 | 6.9 % (91 %). Clipping is concentrated on the sky face (slot 4)
    and the values only overshoot to about 5.2, so the hard per-channel clamp at 4.0 keeps 88 to 98 % of the luminance
    energy; scaling a cubemap down so its maximum fits (what a per cube scale would do) keeps less (about 80 % for
    max 5.0), and a soft knee only lowers the values between the knee and 4.0, so the clamp is kept.
    The default cube is the mean of the CLAMPED cubemaps (what R1 decodes for them); the mean of the unclamped
    values made it 4.8 to 13 % brighter than that mean on seven maps (eden, colony02, meadow, stacks, township, traffic,
    uma) and failed the checker's 3 % default-cube test.
    VALIDATION (cubemaps.cpp prints it, check_bspdown.py recomputes it from the output with the R1 formula against
    an independent BC6H decode * scale): mean relative error of the decoded RGBA8888 vs the scaled R2 box
    filtered values, per face, mp_glitch, slots 0..5 (%): cube 0: 0.16 0.15 0.15 0.16 0.11 0.26; cube 1: 0.19 0.18
    0.22 0.23 0.12 0.38; cube 2: 0.15 0.16 0.14 0.15 0.11 0.21; cube 3: 0.17 0.18 0.15 0.17 0.11 0.21.
    FACE ORDER: no remap needed. Seam matching (best matching edge for each of the 24 face edges by edge
    pixel difference) yields the same face/edge/reversal graph on R2 mp_glitch (24 of 24) and on the stock R1
    cubemaps of mp_rise and mp_angel_city (24 of 24 each); mp_lf_stacks cube 0 17 of 24 (dark faces). Slot 4 is
    the sky (+Z), 5 the ground, 0..3 = +X, -X, +Y, -Y (D3D layout, Z up). Per face mean luminance of the
    R2 files: mp_glitch cube 0 0.320 0.343 0.356 0.338 0.888 0.191, cube 2 0.336 0.349 0.397 0.360 1.055 0.209;
    the full table is in out\evidence\cubemap_face_evidence.txt. R2 cubemap colours are neutral (mean rgb
    0.405 0.399 0.417 on mp_glitch), so the cubemaps alone do not explain a magenta cast.
    check_bspdown.py asserts: valid zip, stored entries only, exact file set, all VTF header fields (hdr:
    v7.5 RGBA8888 64x64 flags 0x620c 7 mips header 96, 131160 bytes; ldr: DXT5 32x32 flags 0x4000 6 mips, 8440 bytes),
    alpha >= 4, default = average, DXT5 vs value/4, and the R1 decode vs the independent R2 decode * scale.
    Lighting data checked for the cast (rule 13b): see "Map wide lighting comparison" below.

14. Physics triangles, lump 0x46 [R1 sub_180109980 -> sub_18010D5B0 -> sub_18010CD00].
    R2 has no 0x46 data. The R1 framing is a u16 entry count followed by one u16 size per
    tricoll header; 0 and 0xFFFF mean absent, with no blob bytes. The converter writes the
    final 0x45 header count and an all-0xFFFF size table for every output, including maps whose
    0x1D remains the placeholder stub. This makes every absent entry explicit and keeps the
    table length equal to 2 + 2 * header_count. The checker verifies count, exact length, and
    absence of blob data. It does not synthesize the topology blobs from R2 tricoll data.
    R1 stores null pointers for absent entries and the consumers null-check them. This is a
    valid framing path, not a verified substitute for real VPhysics world solids.

15. Non-drawing render geometry [data, regen]. Active meshes in lump 0x50 are removed from rendering
    when their resolved material name begins `tools/`, except sky-named materials or meshes with the
    SKY flag (0x4); the R2 SKIP flag (0x20000) is also removed unless the mesh is sky. SKY_2D (0x2)
    alone is not a sky exemption: R2 sets it on non-sky `tools/toolsblocklight` meshes.
    The material name is resolved through 0x52, 0x02, 0x2C and 0x2B. Mesh slots stay in place because
    object-reference, cell-AABB and level-info data index them. Filtered slots get 0 triangles, zero
    vertex range and zero bounds; material-sort IDs and sort records stay intact. Lump 0x4F is rebuilt
    from active mesh ranges, preserving 6-index alignment and the degenerate pad for odd triangle
    counts. Collision data (tricoll, brushes, geosets) is not changed by this rule. The checker verifies
    every source non-sky tools/SKIP mesh is empty, no output non-drawing mesh remains active, filtered
    slots and bounds are zero, and all active index ranges/padding exactly cover the compact output.
    Sky meshes remain because stock R1 maps use the SKY-flagged `tools/toolsskybox` material.

16. Static prop flag bit 0x10 [R1 sub_18018B710, sub_18018D7A0, sub_18018F1D0; data]. The R1 prop loader stores prop+58 =
    ((flags & 0x10) == 0). The two prop drawers (IStaticPropMgr::DrawStaticProps variants) add a prop to the batch when
    (a3 & 8) != 0 or prop+58 != 0, i.e. a prop with bit 0x10 is skipped in the pass without a3 bit 8 and drawn in the pass with it
    (the depth/shadow variant selector, a3 bits 8/16 index the material shader table exactly like the CSM world mesh drawer
    sub_1800A24D0). Stock R1 maps never set 0x10 except on the vista props (flags 0x54 = 4|0x10|0x40). R2 mp_glitch sets 0x10 on
    14,138 of 16,183 props (all models/glitch/glitch_grass_01_amber.mdl, flags 0x14/0x16) with the Source meaning NO_SHADOW; mp_lf_stacks
    has 18, mp_lf_deck 12. Copying the bit made them "shadow/depth only" props in R1. Default now: clear 0x10 unless 0x40 is set;
    --keep-prop-flag10 restores the copy. Diagnostic options (default keep): --props hide-scatter (scale 0.0001 for every model
    with >= --scatter-min (500) instances: grass 14,138 and moss 922 on mp_glitch) and --props hide-all; prop count/order is
    unchanged because the cell and CSM object references index props.

17. Shadow data [data, R1 sub_1800B2540 / sub_1800BC060 loaders]. R2 lump 0x05 = N * 36 bytes {first (node, ref, mesh), last (node, ref,
    mesh), sun dir}; every R2 map has two records, an empty dummy environment (node 0 with +-FLT_MAX bounds, 1 shadow mesh of 6
    triangles) and the real one. R1 has no such lump and reads 0x63 as a tree rooted at node 0 (stock mp_box/mp_rise: node 0 is the
    root with all refs), 0x7C..0x7F as one set. The converter copies them, so on R2 conversions R1's CSM root is the dummy.
    Options (default off): --csm-rebase drops everything before the last environment (nodes, refs, shadow meshes + their 18 indices)
    and lowers child indices; --empty-shadow-meshes writes 0x7C..0x7F empty. Shadow mesh vertex/index/mesh record formats are the same in R1 and R2
    (12 byte vertices, 20 byte alpha vertices, u16 indices relative to the mesh's vertex offset, 12 byte meshes); they have their own
    vertex arrays, so rules 7/11/15 do not affect them. Checker: recomputes the rebase independently.

18. Collision grid padding [R1 sub_18010BE50 / sub_180108710 / sub_180109B10 (IDA copy), data, simulation analysis\gp_walker.cpp]. Crash fixed:
    6 converted maps (mp_colony02, mp_lf_meadow, mp_lf_township, mp_lf_traffic, mp_lf_uma, mp_thaw) access-violated in the world grid
    walker (engine+0x10c5d0 / +0x10c7b0) during the AIN builder's script sweeps; captured on colony02: cell index 0x881 = 2177 =
    47*46+15 read from the walker's list on a 46 x 46 grid whose 0x56 table has 2122 entries.
    Lump layout. 0x55 = 7 dwords: cell size (float, 704.0 in every R2 map, 256.0 in stock R1), cell offset x, y (in cells; world
    cell = floor(coordinate / size) - offset), cell count x, y, straddle-group (marker) count, first brush plane. The loader
    stores the size at +0x60 and 1/size at +0x64 of the collision data, offsets +0x68/+0x6C, counts +0x70/+0x74, marker count
    +0x80 (sizes the per-ray visited array, 4 bytes per group; the geoset's first u16 is its group, 0 = none), first brush plane
    +0xD0. 0x56 = {u16 first geoset, u16 count} per cell; the loader accepts only a size of exactly 4 * (numModels + nx * ny) and
    leaves the table unset otherwise (no limit on nx, ny). World cell (ix, iy) is entry iy * nx + ix, model m is entry nx * ny + m.
    An empty cell has count 0; the walker skips it by count and uses the start only for pointer arithmetic (stock mp_rise: all
    7619 empty cells carry start 45244, mp_box varies). Stock numbers: mp_rise size 256, offset (-37,-51), count (97,113), 2262 marker
    groups, first brush plane 627, 114 models, 11075 entries; mp_box 256, (-62,-62), (76,80), 102, 36, 2 models, 6082 entries.
    Stock R1 grids cover the content, not the whole +-16384 world (mp_rise x [-9472, 15360) y [-13056, 15872)); R2 grids are
    sized the same way (colony02 46 x 46 from -16192, glitch 42 x 43, eden 42 x 44, lf_stacks/township/traffic/uma 25 x 10,
    meadow 20 x 21, thaw 46 x 46).
    Walker. The walker (a2 == 0) computes the cell range of the ray box widened by the hull extent, clamps it to
    [0, nx) x [0, ny), and fills a 16384 entry stack list: rectangle fill when the hull extent is >= 0.7071 * size - 0.0625
    (or the ray has no xy length), else for every row/column a "band" of n cells around the ray, written as 3 entries {first,
    first + s, first + 2 s} (s = one step in travel direction) and advanced by n. So (a) the 2 slots after a band are scratch
    entries that sit up to 2 rows/columns beyond the clamped range (index up to nx * (ny + 2) - 1 at the +y border, negative at
    -y), and (b) for n >= 4 the slots from the 4th on are never written and keep the entries of earlier traces (the list is
    not cleared). The consuming loop reads cells[list[k]] for every k < count. n >= 4 needs a hull half extent above about 0.354 * the
    cell size (diagonal ray, 2 * (0.5 + 1.414 * extent / size) > 2) and below the rectangle threshold 0.7071 * size, i.e. about
    250..497 with the R2 size 704 (a 256 grid would send those hulls down the rectangle path). The AIN hull sizes were not
    captured [INFERENCE: a hull in that range, e.g. dropship/droppod, is swept]; an earlier trace that touched the border rows
    leaves entries like 47 * 46 + x behind. Cell 2177 of a table of 2122 entries returned garbage {start, count} -> geoset
    -> straddle group 10530 >> 620 markers -> access violation. Reproduced with analysis\gp_walker.cpp (a port of the
    list construction; 2,000,000 random rays per hull extent with a persistent stale list, colony02 grid, rays in +-18000):
    0 reads outside the table for hull extents <= 250 and >= 498, 311..5273 for 300..497 (first one after 94..2214 rays); the
    offending entries are stale slots of the form row -1 or row 47 (e.g. 2167 = 47 * 46 + 5, "not written this call"), the shape of the capture.
    Rule. The grid is padded with empty {0,0} cells on all four sides to offset min(old, -N), count max(old end, N) - offset,
    N = ceil(32768 / size) + 3 = 50 (extent +-35200 for size 704): the walker's range of any ray whose endpoints (and hull box)
    lie in +-32768 stays at least 2 whole cells inside the grid, so no counted or scratch index leaves [0, nx * ny) (which is
    also what the clamp needs: stale entries only ever come from earlier traces). 0x57..0x5B are unchanged, the model cells keep their
    order and move to the end of the larger table, the 0x56 size rule above holds (nx * ny + models). The cell count is limited by the
    walker's 16384 entry list (rectangle fill of a huge hull covers the whole grid): reach 32768, else 24576, else 18000 is
    chosen, the converter fails when even that does not fit (never for 704 cells: 100 x 100). Option: --no-grid-pad.
    Result: colony02 table 2122 -> 10006 entries, +31,536 bytes per map (+4 * 7884). Simulation with the padded grid: 0 out-of-table
    reads and 0 out-of-table scratch entries for every hull extent 0..600 over all (start, end) lattice rays in +-18000 and in +-32768
    and over 2M random persistent-list rays per extent in +-18000 / +-32768 (dense lattice +-32768 step 2048: 1,185,921 rays per extent,
    17 extents, all clean); rays reaching beyond +-35200 can still leave scratch entries at the border (40000: 1414..1804 per 38416
    lattice rays) and bring the old failure back for hulls 300..497 (logs: out\gp\gp_sim_*.txt).
    Limits (found on mp_lf_traffic, which still crashed with this padding: AV at cell 101 * 100 + 25, a stale slot): the unwritten
    band slots hold arbitrary stack data, including scratch entries of earlier traces whose rays reached beyond +-35200 (the AIN
    builder's rays do), so padding cannot make every stale read safe. Rule 19 removes the cause for the hulls the walker is
    used with. Rule 18 stays as an extra margin when the grid is not re-binned (--grid-size 0) and is skipped when the cell list limit forbids it.
    Checker: sizes, the exact 0x56 rule, the walker's range arithmetic (numpy, float32) for the extreme rays of +-18000 and +-32768
    with hull extent 0.7071 * size - 0.0625 (>= 3 and >= 2 whole cells from every border), 16384 list fit, padding cells empty,
    each R2 cell at its world position with count <= R2's, and with --unpadded <bsp of the same input converted with --no-grid-pad>:
    every unpadded cell at the same world position with identical {first, count}, identical model cells and lumps 0x57..0x5B.
    Other position-indexed data: none. The R1 loaders/walkers that take a position use trees (CSM / cell AABB trees 0x63, 0x77, light probe
    tree 0x67, tricoll BVH 0x44) walked by plane/box tests, which resolve any position; grid fields (+0x70/0x74/0x78 of the collision data)
    are read only by the walker and by the model-cell lookups sub_180106DC0 / sub_180090DD0, which use nx * ny from the loaded header.

19. Collision grid re-binning to cell size 2048 [R1 sub_18010BE50, simulation analysis\gp_walker.cpp, check_bspdown.py]. The walker's
    band path (non-rectangle path, hull half extent below 0.7071 * size - 0.0625, ray not axis-degenerate) writes 3 cell entries per band
    of n cells ({first, first + s, first + 2 s}) and advances by n, so for n >= 4 the slots 3..n-1 hold stale stack data, which the
    consumer reads as cell indices (cells[list[k]]). Earlier traces leave out-of-table values there (scratch slots past a band, rays far
    outside the grid), so such a read gives a garbage {first, count} cell and the geoset/bounds reads fault. n >= 4 needs a hull half
    extent above about 0.3536 * size (width of the band 1 + 2.83 * ext / size > 2). Padding (rule 18) cannot make this safe (rays out to
    +-23000 occur), the only safe state is: no hull in use inside the window (0.3536 * size, 0.7071 * size - 0.0625).
    Window per cell size: 704 -> 249..497 (AIN callback sweep hull 325: crashed colony02, mp_lf_*, thaw); 256 -> 91..181 (crashed
    mp_lf_stacks and mp_thaw on player-hull wall rays in game [INFERENCE: some hull of 91..181 is traced there; the crash log's
    candidate array holds geoset indices 0xc04.. > pool size 2796, i.e. a cell read beyond the 0x56 table]); 1024 -> 362..724; 2048 -> 724..1448. Hulls known: player 16,
    AIN per-node drops 38/60/65, titan 60, AIN callback sweeps 325. Default 2048 (no R1 hull is that large; the rectangle path above
    1448 lists every cell, <= 16383 of them). Simulation with the port of the list construction (analysis\gp_sim_s1024_stacks.txt, grid
    17 x 7): 0 stale/out-of-table reads for hull extents 0..350 on 194,481 lattice rays +-60000 and 2M random rays per extent; 400..600
    (window of 1024) do read them, as predicted. Local rays (short, near the content, hulls 0..90, 300,000 per length) and the exact
    PhysicsTest ray sequence were clean for 256 too, so the failing hull there is not the player hull of the script.
    The converter re-bins lumps 0x55/0x56/0x57/0x58 after rule 4: every geoset goes into each new cell its AABB touches (candidates come
    from the old cells overlapping the new cell, so R2's assignment is only refined), the geoset pool is re-emitted per cell (16 bit
    start index; the converter stops above 65535); the grid covers only the content (like stock R1 grids), at most 16383 world cells
    (the cell size grows in steps of 16 until that holds). Pools shrink (stacks 797 -> 433, max 81 geosets per cell; stock mp_rise has
    cells with 484). Straddle groups:
    in R2 data one group id is shared by a bundle of different geosets duplicated together in several cells (mp_lf_meadow group 121 has
    two objects), which is only correct while all members share their cells; after re-binning every distinct object (resolved
    primitive words, contents, bounds) that lies in >= 2 cells gets its own group, others 0, ids dense 1..N, Grid.num_straddle_groups = N + 1.
    Model cells copy their geosets (group 0). Primitive lumps 0x59/0x5A/0x5B are unchanged. Rule 18 padding is not applied to a
    re-binned grid. Options: --grid-size <units> (default 2048, 0 = keep 704 and pad with rule 18).
    Checker (with --reference <same input converted with --grid-size 0 --no-grid-pad>): size rule, 16383 cell limit, pool < 65536, no hull
    of the known set (16..700) inside the unwritten-slot window, no entry outside its cell, groups = one object each spanning >= 2 cells,
    dense ids, model cells identical, set of world objects identical, and 5400 walker-style range queries (rays and hull extents 0..800)
    in which the new grid finds every object the reference grid finds.

20. Lighting: world lights, probe light indices, real-time lights (lighting.cpp; replaces the 0xFF RTL fill of rule 2 and the plain 100 byte
    truncation of rule 8a) [R1 engine IDA sub_1800AD260, sub_1800A32B0, sub_18008F550, sub_1800A6D80, sub_1800B0B30, sub_1800A4FD0;
    lightbaker scene.cpp / baker.cpp / probes.cpp (MIT) for the R2 side; data on R2 mp_glitch / mp_rise / mp_wargames / mp_lf_stacks and
    R1 mp_rise; full evidence: lightref\FINDINGS.md].
    a) Array layout. R2: [sky ambient x E][sun x E][realtime spots][realtime points][baked spots][baked points], E = number of
       light_environment entities (2 on every R2 map: index 0 = dummy skybox environment, last = the real one). R1 expects
       [sky ambient][sun][...]: the CPU light path (sub_18008F550, props/models) uses light[0] as the ambient only if its type is 5 and light[1]
       as the sun (must be type 3); the GPU constants (sub_1800A32B0) take the LAST type 3 / type 5 entry; the GPU light upload (sub_1800A6D80)
       sends lights[v1..n) with v1 = (light[0].type == 5), slot = world index - v1, trailing type-0 lights trimmed.
       Rule: keep only the last ambient and the last sun (the real environment) at indices 0/1, drop the dummy pairs (--no-collapse-envs keeps the R2 order).
       Sun/ambient flags are set to 0 (R1 stock; R2 uses 0x18 on the sun).
    b) Probe light indices (lump 0x65, 4 x int16 at +0x24, weights 4 x u8 at +0x20). R2 stores world index - E, R1 resolves
       world index = stored + v1 (the loader adds 32 when the weight is non-zero, consumers subtract 32 - v1). Without the fix every
       probe-lit prop gets the PREVIOUS array entry (numeric check, mp_glitch: R2 rule +2 puts 97.5 % of the referenced lights within their
       radius of the probe, the R1 rule +1 only 30.3 %; stock R1 rise with +1: 95.3 %). Output: stored' = new world index - v1; entries pointing
       at a dropped (dummy) light get weight 0 / index -1.
    c) Real-time lights (lump 0x69, 9 bytes per type-1 lightmap texel; lightbaker docs/TF2_LIGHTMAP_AND_SHADOW_NOTES.md "Recovered RTL packing",
       verified here: per mesh, light at palette index + E is within radius of the mesh box for 88.7 % of pairs, <= 42 % at any other offset).
       Layout per type-1 page in header order: A plane (w*h uint32 LE: bits 0..23 four 6 bit slots, 63 = unused, bits 24..31 palette id;
       0xFFFFFFFF = none), B plane (w*h x 4 bytes: visibility weight per slot), C plane (w*h bytes: candidate count). Palette = lump 0x7A,
       126 byte records of 63 uint16, entry = world index - E. Slot 0 of the first palette column is reserved for the sun.
       R1 layout: 4 bytes per texel, byte = world index - v1, 0xFF = none, ascending, at most 4 (R1 stock mp_rise: byte 0 = the sun never occurs, 68 % of
       texels empty, so the sun is NOT in the R1 list). Conversion: per texel keep the slots that are point/spot lights with flag 0x8 and weight >=
       --rtl-min-weight (default 187: B is stored sRGB, 187 = 0.5 visibility, the R2 baker writes quarter steps 137/187/224); the sun slot and baked lights are dropped; bytes above 254 do not exist: if the real-time group needs more than
       255 slots (glitch 338, wargames 260 flagged lights) the group is reordered by total visibility weight and the least used lights are dropped
       (glitch: 11,079 of 885,554 slots). R1 has no per-texel visibility weight; the R2 shadowing is only kept as the omitted entries.
       --no-rtl restores the 0xFF fill.
    d) PBR lights. R2 entity _pbr_falloff 1 = world light flag 0x20 (no 0x4); the compiled intensity is brightness/255 * 1e5 (the two blue
       glitch point lights: 1e5, radius 500, d50 150; probe references to them: 937). R1 evaluates lights without flag 0x4 as 1 / (c + l d + q d^2)
       (sub_1800A4FD0), which is unbounded near the radius. Rewrite: set flag 0x4 and multiply the intensity by 2 / d50^2 where d50 is the
       distance at which c + l d + q d^2 = 0.5 (the inverse-square irradiance I/d50^2 equals I' * 0.5 there). Provisional: the shape inside d50 is
       not calibrated against R2 renders. Flags 0x20 and 0x40 are cleared. --pbr-lights keep leaves the records alone.
    Checker: independent numpy model of all three lumps, byte exact (check_bspdown.py, options --no-collapse-envs --no-rtl --rtl-min-weight
    --pbr-lights).

Results
-------
Required maps (all checks of check_bspdown.py pass, including bsp_tool.load_bsp as an R1 branch
BSP, 116-117 checks each); outputs are in out\:
  map                    in bytes     out bytes   tricoll hdrs  geosets      prims         meshes clipped/emptied(+sky)
  mp_lf_deck             31,377,600   26,089,040  1787          1461->1121   4270->2212    0 / 0 (+1 sky)
  mp_eden               211,188,772  181,889,168  6545          5959->4455   14219->7926   4 / 0
  mp_crashsite3         207,546,420  177,386,880  4651          7972->5980   14163->10069  0 / 0 (+4 sky), 90 grid cells trimmed
  mp_forwardbase_kodai  191,741,916  160,401,700  5625          6065->4665   12819->6892   1 / 0
  mp_lf_stacks           24,440,244   20,311,836   884          1069->797    3372->1178    0 / 0
  mp_lf_uma              37,343,284   31,582,144  1399          1594->1187   4518->1719    0 / 0
Extracted from the R2 VPKs with corvpk: mp_lf_stacks, mp_lf_uma (and, for the 23-map run, all others).
All 23 maps: 22 convert and pass the checker (97 checks without bsp_tool); mp_angel_city stops in
rule 3 (see above). Table: out\all23_summary.txt.

Round trip (original R1 -> bsp_regen -> bspdown, --cubemaps keep --no-remap-contents --limit 40000):
  mp_box (8.8 MB) and mp_angel_city (176 MB, 5806 tricoll headers, 18364 props), extracted from the
  R1 vpks. 62 of 69 lumps byte identical for angel_city (all geometry, tricoll including the bevel
  stream, planes, vertices, meshes, sorts, light probes, world lights, cells/portals, pakfile).
  Differing: 0x55-0x5B (identical after resolving pools, see rule 4), 0x23 (props: three unread
  fields), 0x69 (rule 2, expected). With the default limit the only additional differences are
  geoset bounds of the stock R1 file beyond 16376 that the converter clamps (1728 slots, stock
  R1 mp_angel_city has 1020 such bounds, up to 18613, and loads fine) and, with default cubemap mode,
  material sorts.

Map wide lighting comparison, R1 mp_rise vs R2 mp_rise / mp_glitch (nothing was changed on these except where stated)
---------------------------------------------------------------------------
 * World lights 0x36. R2 v3 is 112 bytes = the 100 byte R1 record + 3 floats (always 0, 0.03..0.1 / 80, 0 / 1);
   v2 = +2 floats. bsp_tool lists the first 100 bytes with identical field order for R1 and R2 (12 floats origin,
   intensity, normal, shadow_offset; viscluster, type, style; stop_dot, stop_dot2, exponent, radius, constant,
   linear, quadratic; flags, texture_data, parent). Checked in data: the R1 loader (sub_1800AD260) reads type at
   +52, stop_dot2/exponent/radius at +64/+68/+72 and constant/linear/quadratic at +76/+80/+84, which matches.
   Statistics agree field by field: type histogram {1: 469 point, 2: 244 spot, 5: 1, 3: 1} (R1 rise) vs {1: 700, 2: 320, 5: 2,
   3: 2} (R2 rise), unit normals, intensity max component p5/50/95 0.5/3/8.6 vs 0.5/2/8, radius median 300 vs 250,
   constant 1, linear -0.0065 vs -0.0078, texture_data and parent 0, style 0. Truncating to 100 bytes is correct.
   One difference: R1 flags are 6, 30, 14 (and 0 for the two sun/sky entries) on every light, R2 4, 28, 12, 76, 56
   (same without bit 0x2). Meaning of 0x2 unknown; option --worldlight-flag2 sets it like stock (default off).
 * Light probes 0x65: same 48 byte layout, ambient cube RGBE bytes (rgb byte mean 131/160/114 R1 rise, 135/171/107 R2 rise,
   127/168/131 R2 glitch; signed exponent byte mean about 0) so no unit or channel difference. sky_dir_sun_vis: R1 stores
   a sky direction scaled to 8192 times the sky openness (|xyz|/8192 median 0.44, p90 0.78 on rise) plus a sun visibility short that is
   0 or 8192; R2 has the same direction/openness distribution (median 0.44, p90 0.79) and the sun visibility short is 1 or
   8192 (R1 0). (My earlier note that R2's 4th short and vector length differ was wrong.) Static light indices stay
   < world light count (nothing is renumbered, the 112 -> 100 byte truncation does not move any light);
   weights: R1 rise 12106 probes with 0 lights, 4 lights 11494; R2 rise 10420 and 18099 (R2 data is just denser).
 * Lightmaps 0x62/0x53: sky lightmap RGBA8888 pairs; mp_rise R1 vs R2 rgb byte mean 114/109/111 vs 110/106/109, alpha 255 for
   35-45% of texels in both, decoded mean luminance 0.14 vs 0.11 (linear rgb * (alpha+1)/64): same encoding, no change.
 * Level info 0x7B and the R1 loader's checks were verified earlier (record sizes, mesh ordering).
 * Not found by data comparison: a magenta/purple cast source. Candidates that data cannot resolve: model materials that
   reference textures R1 does not have, the sky (R2 has a 4096x4096 BC6H 2D panorama), per-prop cubemap indices (R2 1..N,
   R1 0; stacks has the same and works).

Research: real R1 world physics from R2 data (nothing below is implemented)
---------------------------------------------------------------------------
(a) Authoring with the R1 vphysics.dll (MRVN-Radiant r1.patch, titanfall_physics.cpp).
    Local C:\whatever\bin\x64_retail\vphysics.dll matches every identity check in the patch:
    size 2,084,352, SHA-256 4cbf051a20e5b50973f5e9acb016dc006cd839895231be2f9ea63c568d94abd8
    (equals the expectedHash bytes), PE TimeDateStamp 0x5487422E (2014-12-09), machine AMD64,
    SizeOfImage 0x221000, export CreateInterface at RVA 0x14B440, vtable RVA 0x18B470 (the singleton
    object at RVA 0x1D01C8 holds it in the file image), and the prologue bytes of vtable slots
    10, 11, 12, 13, 17, 18, 19 at RVAs 0x18D80, 0x19680, 0x15F10, 0x1A2B0, 0x19040, 0x15EF0, 0x15F00
    (checked from the file with a small PE parser; not loaded or called). Imports: KERNEL32, tier0.dll,
    vstdlib.dll. Calls: CreateSoup (slot 10), DestroySoup (11), AddTriangle(a,b,c, 0) (12), Compile
    (13, bool false), Destroy (17), Size (18), Write (19) = serialized "VPHY" blob; one soup per
    contents value, keyvalues "staticsolid {index, contents}" per soup. This reproduces the layout of
    the stock R1 lump (mp_box: model 0 = 45,344 byte blob + 2 staticsolids with contents 33570819 and
    65536, model 1 = a "solid" with mass/volume, then the terminator).
    In-process use: the r1delta process already has this vphysics.dll loaded (engine/server need it),
    so the hook could call CreateInterface("VPhysicsCollision007") on the loaded module after the same
    hash/RVA checks and compile at downgrade time on Windows x64. Unknowns: compile time for maps with
    1.2 million tricoll triangles (eden), memory use, whether the soup compile is thread safe inside the
    file hook, and whether the singleton needs the engine's tier0 allocator (it does import tier0).
(b) Input data. Per contents value, MRVN feeds (1) brush faces (windings of non-bevel sides, fan
    triangulated) and (2) tessellated patch meshes. R2 equivalents: tricoll headers (0x45) with
    triangles (0x42: 4 bytes {A:10, B:7 rel., C:7 rel., flags:8}, vertices 0x03 starting at first_vertex;
    when header.scale != 0 the vertices are relative to -(origin/scale), i.e. model-like instances, for
    patches scale is 0) and brushes (0x5C: origin + extents AABB, side planes from 0x01 starting at
    Grid.first_brush_plane through the offsets lump 0x5D, 6 axial sides + non-axial extras, side
    properties 0x5E) which need convex winding generation from the planes (not trivial, no code yet).
    Contents come from the unique-contents table through the primitive (type 0x40 tricoll header index,
    type 0 brush index) referenced by the geoset lumps, after the rule 9 remap to R1 meaning. Static props
    would not be included (props carry their own model physics in R1).
(c) Lump 0x46 PHYSICS_TRIANGLES (R1 only; absent in R2). Parsed in the engine by sub_180109980 ->
    sub_18010D5B0 -> sub_18010CD00: u16 N, N u16 sizes (0 or 0xFFFF = none), then the blobs; N must be
    the tricoll header count. Each entry is attached (offset +24 of the triangle object) to a
    CVirtualTriColl (vftable at 0x180627F30; methods read triangles from the tricoll header and its
    vertices) that is registered with the physics system, i.e. per tricoll header extra physics data.
    In stock mp_box: N = 37 = header count, size per header = 51 + 7 * (triangles - vertices) bytes
    exactly (37 for 2 triangles / 4 vertices, 65 for 20 triangles / 6 vertices; fit residual 0), content
    starts 01000000 04 04 06 06 ... (looks like per-triangle adjacency/ledge data, format not decoded).
    The converter now writes a header-count-matched all-absent table (rule 14): N equals the
    0x45 count and each size is 0xFFFF, with no blobs. The loader stores null pointers for
    those entries and consumers null-check them. The virtual-tricoll fallback's complete
    collision behavior and runtime cost remain unverified; the opaque topology blobs are not
    generated.
Conclusion: real R1 physics is feasible by running the local vphysics.dll polysoup authoring on
tricoll triangles (easy) plus brush windings (needs plane intersection code), with unknown compile cost;
the stub in rule 12 is the cheap default until then.

Open questions and unverified items
-----------------------------------
 * Nothing here was loaded by the R1 engine. Everything above is checked against the loader code
   read in IDA, against stock R1 files, and with bsp_tool.
 * External lump files: R1's CMapLoadHelper::Init (sub_1800ACB10) probes <map>_l_<N>.lmp (Source style,
   N = 0, 1, ... until the first missing file; every file found replaces lump N). The R2 VPKs unpack to
   maps/<map>.bsp.<hex>.bsp_lump files next to the BSP; their names do not match R1's pattern, so R1 should
   ignore them (read from the code, not tried). The converted BSP must be served as the only source of
   the lumps. R1 also reads the <map>_<name>.ent partition files named in lump 0x18 (the R2 ones are
   used as is, not converted).
 * Tricoll bevel records can address 8191 headers; mp_angel_city has 9133 headers. A lenient
   conversion (drop foreign segments, terminate the own run with an empty record) needs R1's
   runtime reader of the extended records, which I did not find (the tricoll trace code is not
   in the engine functions I searched), so it is not implemented.
 * Emptied meshes (0 triangles) are my choice to keep object-reference lumps valid. Whether R1's
   renderer is fine with a 0 triangle mesh is not known.
 * Real-time lights are absent (rule 2). Probe static-light indices (rule 6) may not mean the
   same in R2.
 * Contents remap (rule 9): derived from bsp_regen's forward table, not from R1 runtime behaviour.
   Many other R2 contents patterns (0xC80280, 0xCA0280, ...) are not mapped.
 * Physics: R2 has no 0x46 and the converter's 0x1D may still be the placeholder stub. Rule 14
   writes a valid all-absent 0x46 table matched to the final 0x45 header count. R1 takes the
   null-pointer path for each virtual-tricoll entry; the resulting collision behavior and
   runtime cost have not been tested in game.
 * Grid scale 704 and cells reaching -16896 (mp_angel_city, mp_crashsite3: 45 cells) are kept.
 * Prop collision flags add/remove are copied with R2 meanings.
 * Texture data flags, brush side flags and surface properties are copied; their R2 meaning was not compared.
