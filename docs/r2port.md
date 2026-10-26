# R2 map provider

`r2port_provider` is a path-prefix registry. Providers answer normalized relative paths with existence, size, or shared read-all bytes; the registry keeps a bounded, thread-safe LRU and clears it when another map BSP is requested. R1's filesystem is always consulted before the registry.

For client, listen-server, and legacy dedicated modes, `R1OFactory` receives the native `VFileSystem017` and its `IBaseFileSystem` object. The R2 integration hooks the base-filesystem `FileExists`, size-by-name, and `Open` methods. Each calls the original R1 filesystem first and consults the provider only on a miss. Provider opens return shared memory-file handles; the hooked read, close, seek, tell, and size methods handle those while delegating ordinary handles to R1.

The `VFileSystem017` `FindFirst`/`FindNext` wrappers (vtable slots 30/31) append allowlisted R2 BSP names after native R1 results. The base-filesystem hooks cover direct existence, size, and open checks, while map startup can first try its VPK reader and only then reach the normal search path.

R1O fake-dedicated mode obtains the TFO `VFileSystem017` through `R1OTFOFileSystemInterface`. Its existing TFO filesystem hooks preserve native lookup and established dedicated loose-file and R1 VPK fallbacks; the R2 provider is consulted last. The same provider is registered for `maps/` files and the `vpk/e*.bsp*` map-list query. Hooks on `VFileSystem017` `FindFirst`/`FindNext` (vtable slots 30/31) append package-name entries for allowlisted R2 BSPs after native results. Point existence and size checks use the provider after existing fallbacks.

The map provider is limited to the 20 allowlisted R2-only maps. It reads only the map BSP and five entity files from the matching `englishclient_<map>.bsp.pak000_dir.vpk`; the BSP is downgraded in memory. AI graphs and BSP lump overrides are optional patch-provided files. The provider does not expose R2 `.bsp_lump` files or names that R1 could treat as `_l_<N>.lmp` overrides.
The converter repacks the cubemaps into the BSP using R1 encoding. The provider uses the library's default `bspdown::Options`: cubemap scale `1/6` and world-light flag bit 2 disabled.

The R2 material provider starts opening/building the RPak index on a background thread when the filesystem provider is registered, before a map request. A cold index logs that its first-use build may take several seconds; the completed index is persisted under `<gamedir>/r2port/cache/<R2-install-fingerprint>`, where the fingerprint includes the R2 root and each RPak name, size, and modification time. Material-provider callbacks wait at most 10 ms for the index and 20 ms for an asset conversion, then return a miss if the work is not ready.

After the converted BSP is served, the provider queues BSP material-name parsing and VMT/VTF conversion on a worker pool capped at four threads, with a one-worker minimum and `cores - 2` workers when that is between one and four. Its queue is capped at 8192 jobs and generated-file LRU at 16 MiB; the decoded RPak family cache defaults to 256 MiB and is configurable with `r2port_tex_cache_mb`. R1 remains first and the provider does not activate for stock R1 maps. Startup, index, callback, conversion, and signon timings, plus material/texture provider hits and misses, are logged with steady-clock timestamps; callbacks over 250 ms log a warning.

`<gamedir>/r2port/patches` may contain `<map>.r2patch` physics patches and `<map>.ain.r2patch` AI-graph packages. Physics patches replace lump `0x1D` only when their SHA-256 matches the exact source R2 BSP; AI graphs are matched against the converted BSP after any physics replacement. The provider supplies the live map version and a zero CRC only when `developer` is 0, because the developer-mode entity CRC path is not reproduced. Missing, malformed, or mismatched packages fall back to the converter's physics stub and no AI graph.

When the engine's VPK read path reports a miss before an ordinary filesystem open, the provider writes only that requested BSP or entity file to `<gamedir>/r2port_cache/<map>-<package-fingerprint>/maps/` and adds the fingerprinted directory as a tail `GAME` search path, after R1 paths. The fingerprint is SHA-256 over the matching R2 directory VPK and chunk-file names, sizes, and modification times. Before materializing, the hook checks the original R1 filesystem so existing R1 files continue to win. The cache stays under `<gamedir>` because the engine mounts it as a `GAME` search path; it contains only generated BSP and entity files and is removable with `r2port_clear_cache`.

## Test root

Use the permanent `S:\game\r2` installation for R2-port work. It has a pinned, independent CORE checkout; it does not stage into or swap files in the shared `S:\game` runtime. Create and stage it with:

```powershell
powershell -ExecutionPolicy Bypass -File .build\r2-root.ps1 -Create
powershell -ExecutionPolicy Bypass -File .build\r2-root.ps1 -Stage
powershell -ExecutionPolicy Bypass -File .build\r2-root.ps1 -Verify
```

`-Status` reports the layout and private disk use. `-Reset` recreates only `S:\game\r2`. The root `bin`, `r1`, and `tools` directories and unchanged large CORE directories are junctions; CORE directories that differ from the source are private checkouts. Launch-root files are hardlinks except executables replaced by staging. The pinned scripts, resources, maps, and `bin_delta` copy are local; `crash` and `crashes` are real directories.

Launch from the permanent root directly, or use the bounded/path-owned harness:

```powershell
powershell -ExecutionPolicy Bypass -File .build\run-in-r2.ps1 -Mode Client
powershell -ExecutionPolicy Bypass -File .build\run-in-r2.ps1 -Mode Listen -Map mp_box
powershell -ExecutionPolicy Bypass -File .build\run-in-r2.ps1 -Mode Dedicated -Map mp_box -Port 27050 -Seconds 60 -FatalScriptErrors -Cdb
```

The launcher is `S:\game\r2\r1delta.exe`; client/listen and dedicated executables are `S:\game\r2\Titanfall.exe` and `S:\game\r2\R1Delta_DS.exe`. No stage/unstage or CORE swap is needed per window. You can play from `S:\game\r2` directly.
