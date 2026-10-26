# R2 map provider

`r2port_provider` is a path-prefix registry. Providers answer normalized relative paths with existence, size, or shared read-all bytes; the registry keeps a bounded, thread-safe LRU and clears it when another map BSP is requested. R1's filesystem is always consulted before the registry.

For client, listen-server, and legacy dedicated modes, `R1OFactory` receives the native `VFileSystem017` and its `IBaseFileSystem` object. The R2 integration hooks the base-filesystem `FileExists`, size-by-name, and `Open` methods. Each calls the original R1 filesystem first and consults the provider only on a miss. Provider opens return shared memory-file handles; the hooked read, close, seek, tell, and size methods handle those while delegating ordinary handles to R1.

The `VFileSystem017` `FindFirst`/`FindNext` wrappers (vtable slots 30/31) append allowlisted R2 BSP names after native R1 results. The base-filesystem hooks cover direct existence, size, and open checks, while map startup can first try its VPK reader and only then reach the normal search path.

R1O fake-dedicated mode obtains the TFO `VFileSystem017` through `R1OTFOFileSystemInterface`. Its existing TFO filesystem hooks preserve native lookup and established dedicated loose-file and R1 VPK fallbacks; the R2 provider is consulted last. The same provider is registered for `maps/` files and the `vpk/e*.bsp*` map-list query. Hooks on `VFileSystem017` `FindFirst`/`FindNext` (vtable slots 30/31) append package-name entries for allowlisted R2 BSPs after native results. Point existence and size checks use the provider after existing fallbacks.

The map provider is limited to the 20 allowlisted R2-only maps. It reads only the map BSP and five entity files from the matching `englishclient_<map>.bsp.pak000_dir.vpk`; the BSP is downgraded in memory. AI graphs and BSP lump overrides are optional patch-provided files. The provider does not expose R2 `.bsp_lump` files or names that R1 could treat as `_l_<N>.lmp` overrides.

`GetR2MapPatchDirectory()` identifies `<gamedir>/r2port`, where a later patch adapter can read `<map>.patch` files. The `MapPatchSource` interface supplies optional lump and virtual-file overrides only when the patch's SHA-256 matches the exact source BSP; no patch serialization or patch data is registered yet.

When the engine's VPK read path reports a miss before an ordinary filesystem open, the provider writes only that requested BSP or entity file to `<gamedir>/r2port_cache/<map>-<package-fingerprint>/maps/` and adds the fingerprinted directory as a `GAME` search path. The fingerprint is SHA-256 over the matching R2 directory VPK and chunk-file names, sizes, and modification times. The cache is removable with `r2port_clear_cache`; the command removes `<gamedir>/r2port_cache`. Before materializing, the hook checks the original R1 filesystem so existing R1 files continue to win.
