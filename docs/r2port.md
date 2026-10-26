# R2 map provider

`r2port_provider` is a path-prefix registry. Providers answer normalized relative paths with existence, size, or shared read-all bytes; the registry keeps a bounded, thread-safe LRU and clears it when another map BSP is requested. R1's filesystem is always consulted before the registry.

For client, listen-server, and legacy dedicated modes, `R1OFactory` receives the native `VFileSystem017` and its `IBaseFileSystem` object. The R2 integration hooks the base-filesystem `FileExists`, size-by-name, and `Open` methods. Each calls the original R1 filesystem first and consults the provider only on a miss. Provider opens return shared memory-file handles; the hooked read, close, seek, tell, and size methods handle those while delegating ordinary handles to R1.

The VPK read hook serves generated `.vmt` and `.vtf` files through the R1O memory-handle layer only after the native R1 VPK and filesystem report a miss. Map BSP/entity requests still use the documented materialization fallback so launch-time map validation sees them.

R1O fake-dedicated mode obtains the TFO `VFileSystem017` through `R1OTFOFileSystemInterface`. Its existing TFO filesystem hooks preserve native lookup and established dedicated loose-file and R1 VPK fallbacks; the R2 provider is consulted last. The same provider is registered for `maps/` files and the `vpk/e*.bsp*` map-list query. Hooks on `VFileSystem017` `FindFirst`/`FindNext` (vtable slots 30/31) append package-name entries for allowlisted R2 BSPs after native results. Point existence and size checks use the provider after existing fallbacks.

The map provider is limited to the 20 allowlisted R2-only maps. It reads only the map BSP and five entity files from the matching `englishclient_<map>.bsp.pak000_dir.vpk`; the BSP is downgraded in memory. AI graphs and BSP lump overrides are optional patch-provided files. The provider does not expose R2 `.bsp_lump` files or names that R1 could treat as `_l_<N>.lmp` overrides.
The converter repacks the cubemaps into the BSP using R1 encoding. The provider uses the library's default `bspdown::Options`: cubemap scale `1/6` and world-light flag bit 2 disabled.

The R2 material provider starts opening/building the RPak index on a background thread when the filesystem provider is registered, before a map request. A cold index logs that its first-use build may take several seconds; the completed index is persisted under `<gamedir>/r2port/cache/<R2-install-fingerprint>`, where the fingerprint includes the R2 root and each RPak name, size, and modification time. After map prefetch identifies material and texture paths, the provider records them in a read-mostly hash table protected by a shared mutex; unknown names return a miss without touching the RPak index or conversion queue. Known-path opens wait at most 20 ms for an asset conversion, then return a miss if the work is not ready.

After the converted BSP is served, the provider queues BSP material-name parsing and VMT/VTF conversion on a worker pool capped at four threads, with a one-worker minimum and `cores - 2` workers when that is between one and four. Its queue is capped at 8192 jobs and generated-file LRU at 16 MiB; the decoded RPak family cache defaults to 256 MiB and is configurable with `r2port_tex_cache_mb`. R1 remains first and the provider does not activate for stock R1 maps. Startup, index, callback, conversion, and signon timings, plus material/texture provider hits and misses, are logged with steady-clock timestamps; callbacks over 250 ms log a warning.
The VPK hook checks the provider's read-mostly name index before issuing its extra native filesystem existence probe, so unrelated misses do not trigger another disk lookup. Set `R1DELTA_R2PORT_TRACE_MISSES=1` to log up to 100 exact unique missed material paths and aggregate request call, unique-name, average-time, and maximum-time counts.

`<gamedir>/r2port/patches` may contain `<map>.r2patch` physics patches and `<map>.ain.r2patch` AI-graph packages. Physics patches replace lump `0x1D` only when their SHA-256 matches the exact source R2 BSP; AI graphs are matched against the converted BSP after any physics replacement. The provider supplies the live map version and a zero CRC only when `developer` is 0, because the developer-mode entity CRC path is not reproduced. Missing, malformed, or mismatched packages fall back to the converter's physics stub and no AI graph.

When the engine's VPK read path reports a miss before an ordinary filesystem open, the provider writes only that requested BSP or entity file to `<gamedir>/r2port_cache/<map>-<package-fingerprint>/maps/` and adds the fingerprinted directory as a tail `GAME` search path, after R1 paths. The fingerprint is SHA-256 over the matching R2 directory VPK and chunk-file names, sizes, and modification times. Before materializing, the hook checks the original R1 filesystem so existing R1 files continue to win. The cache stays under `<gamedir>` because the engine mounts it as a `GAME` search path; it contains only generated BSP and entity files and is removable with `r2port_clear_cache`.

## Test root

`S:\game\r2` is the user's permanent play root. Agents use `S:\game\r2dev` so their staging and test windows never write into the user's tree. Both roots use the pinned CORE release commit and require no per-window stage/unstage or CORE swap.

Create, stage, and verify the agent root with:

```powershell
powershell -ExecutionPolicy Bypass -File .build\r2-root.ps1 -Create -Root r2dev
powershell -ExecutionPolicy Bypass -File .build\r2-root.ps1 -Stage -Root r2dev
powershell -ExecutionPolicy Bypass -File .build\r2-root.ps1 -Verify -Root r2dev
```

Both scripts default to `-Root r2dev`. `-Status -Root r2dev` reports the layout and private disk use; `-Reset -Root r2dev` recreates only the agent root. If an existing agent root still has shared writable CORE directories, use `-Repair -Root r2dev`; it preserves the staged launch files and `bin_delta` while making the writable directories private. `-Stage` keeps a one-time `.pre-r2port-backup` beside each existing file it replaces; reset removes those backups with the selected root. `-Verify` rejects unsafe reparse targets and probes temporary writes in the agent's writable paths.

The launch-root `bin`, `r1`, and `tools` directories are junctions; root files are hardlinks except executables replaced by staging. The CORE checkout keeps pinned scripts, resources, and maps locally, has a private `bin_delta` copy, and keeps writable `bin`, `materials`, `models`, `sound`, `cfg`, `config`, and `admin` directories private. `shaders`, `r2port`, `r2port\cache`, and `r2port_cache` are local write locations. The independent clone keeps `core.autocrlf=false` so checked-out bytes match the pinned blobs. Only unchanged `bin_nexon` and `vgui` content may be shared read-only from the original CORE tree. `crash` and `crashes` are real directories.

To create the user's play root from scratch, the lead may run these commands only after confirming no process is using `S:\game\r2`:

```powershell
powershell -ExecutionPolicy Bypass -File .build\r2-root.ps1 -Create -Root r2
powershell -ExecutionPolicy Bypass -File .build\r2-root.ps1 -Stage -Root r2
powershell -ExecutionPolicy Bypass -File .build\r2-root.ps1 -Verify -Root r2
```

`-Reset -Root r2` removes and recreates that root; use it only when the user's game is closed and the lead explicitly authorizes it. Stage the new build afterward. The user can play from `S:\game\r2` directly by running `r1delta.exe`, `Titanfall.exe`, or `R1Delta_DS.exe` there.

Launch the agent root directly or use the path-owned harness (which defaults to `r2dev`):

```powershell
powershell -ExecutionPolicy Bypass -File .build\run-in-r2.ps1 -Mode Client
powershell -ExecutionPolicy Bypass -File .build\run-in-r2.ps1 -Mode Listen -Map mp_box
powershell -ExecutionPolicy Bypass -File .build\run-in-r2.ps1 -Mode Dedicated -Map mp_box -Port 27050 -Seconds 60 -FatalScriptErrors -Cdb
```

The harness adds `-multiple -allowmultiple` to every Client and Listen launch. The agent executables are `S:\game\r2dev\r1delta.exe`, `S:\game\r2dev\Titanfall.exe`, and `S:\game\r2dev\R1Delta_DS.exe`. Pass `-Root r2` only for lead-authorized tests; agents must leave the user's root alone.

