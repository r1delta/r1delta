# R2 map provider

`r2port_provider` is a path-prefix registry. Providers answer normalized relative paths with existence, size, or shared read-all bytes; the registry keeps a bounded, thread-safe LRU and flushes it when another provider map is opened. R1's filesystem is always consulted before the registry.

For client, listen-server, and legacy dedicated modes, `R1OFactory` receives the native `VFileSystem017` and its `IBaseFileSystem` object. The R2 integration hooks the base-filesystem `FileExists`, size-by-name, and `Open` methods. Each calls the original R1 filesystem first and consults the provider only on a miss. Provider opens return shared memory-file handles; the hooked read, close, seek, tell, and size methods handle those while delegating ordinary handles to R1.

The native `maps/*.bsp` directory listing continues through R1 `FindFirst`/`FindNext`; the existing wrappers append allowlisted R2 BSP names only after R1 has returned its first result. Map validation, playlist checks, and direct size/existence queries use the original-first base-filesystem hooks above.

R1O fake-dedicated mode obtains the TFO `VFileSystem017` through `R1OTFOFileSystemInterface`. Its existing TFO filesystem hooks preserve the native lookup and the established dedicated loose-file and R1 VPK fallbacks; the R2 provider is consulted last. The same provider is registered for both filesystem paths. This covers point existence and size checks used by map validation and playlist lookups as well as file opens.

The map provider is limited to the 20 allowlisted R2-only maps. It reads only the map BSP and five entity files from the matching `englishclient_<map>.bsp.pak000_dir.vpk`; the BSP is downgraded in memory. AI graphs and BSP lump overrides are optional patch-provided files. The provider does not expose R2 `.bsp_lump` files or names that R1 could treat as `_l_<N>.lmp` overrides.

`GetR2MapPatchDirectory()` identifies `<gamedir>/r2port`, where a later patch adapter can read `<map>.patch` files. The `MapPatchSource` interface supplies optional lump and virtual-file overrides only when the patch's SHA-256 matches the exact source BSP; no patch serialization or patch data is registered yet.
