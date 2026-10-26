# R2 map provider

`r2port_provider` is a path-prefix registry. Providers answer normalized relative paths with existence, size, or shared read-all bytes; the registry keeps a bounded, thread-safe LRU and clears it when another map BSP is requested. R1's filesystem is always consulted before the registry.

For client, listen-server, and legacy dedicated modes, `R1OFactory` receives the native `VFileSystem017` and its `IBaseFileSystem` object. The R2 integration hooks the base-filesystem `FileExists`, size-by-name, and `Open` methods. Each calls the original R1 filesystem first and consults the provider only on a miss. Provider opens return shared memory-file handles; the hooked read, close, seek, tell, and size methods handle those while delegating ordinary handles to R1.

Provider materials and textures are answered by the Open hooks, not by the VPK read hook. The R1 VPK read function fills an out-struct that its callers inspect (`*b == -1` means "not in a VPK, try the file system") and ignores the returned pointer, so a memory handle returned from that hook is never used. In client, listen-server, and legacy dedicated modes the provider answers `IBaseFileSystem::Open` (slot 2) and `IFileSystem::Open` (the `pathId` overload, slot 74 of the stock R1 `filesystem_stdio.dll`); R1 loads VTFs through the latter. Because native code must never dereference a provider handle, `InstallR1FileSystemHandleHooks` also wraps every native `FileHandle_t` consumer of `IFileSystem`: `SetBufferSize` (23), `IsOk` (24), `EndOfFile` (25), `ReadLine` (26), `ReadEx` (75), and `IBaseFileSystem::Flush` (8). It verifies the module's build stamp (`0x54874230`, image size `0x2116000`) first; the R1O TFO layout has two more slots before these and is hooked separately. Map BSP/entity requests still use the documented materialization fallback, now the only provider work left in the VPK read hook, so launch-time map validation sees them.

R1O fake-dedicated mode obtains the TFO `VFileSystem017` through `R1OTFOFileSystemInterface`. Its existing TFO filesystem hooks preserve native lookup and established dedicated loose-file and R1 VPK fallbacks; the R2 provider is consulted last. The same provider is registered for `maps/` files and the `vpk/e*.bsp*` map-list query. Hooks on `VFileSystem017` `FindFirst`/`FindNext` (vtable slots 30/31) append package-name entries for allowlisted R2 BSPs after native results. Point existence and size checks use the provider after existing fallbacks.

The map provider is limited to the 20 allowlisted R2-only maps. It reads only the map BSP and five entity files from the matching `englishclient_<map>.bsp.pak000_dir.vpk`; the BSP is downgraded in memory. AI graphs and BSP lump overrides are optional patch-provided files. The provider does not expose R2 `.bsp_lump` files or names that R1 could treat as `_l_<N>.lmp` overrides.
The converter repacks the cubemaps into the BSP using R1 encoding. The provider uses the library's default `bspdown::Options`: cubemap scale `1/6` and world-light flag bit 2 disabled.

The R2 material provider starts opening/building the RPak index on a background thread when the filesystem provider is registered, before a map request. A cold index logs that its first-use build may take several seconds; the completed index is persisted under `<gamedir>/r2port/cache/<R2-install-fingerprint>`, where the fingerprint includes the R2 root and each RPak name, size, and modification time. After map prefetch identifies material and texture paths, the provider records them in a read-mostly hash table protected by a shared mutex; unknown names return a miss without touching the RPak index or conversion queue. Known-path opens wait for the asset conversion (up to 60 s): a missed open leaves R1 with its error texture for the rest of the map, so a miss is never returned for a name the prefetch queued. A VMT's textures are registered as known before its reader resumes. Converted files are persisted under `<cache>/converted/<revision>/materials/...` (VMTs with a `.deps` sidecar listing their textures); the worker loads from there before converting and writes new output through a temporary name plus rename, so a warm map load reads files instead of running r2tex. Bump `kConvertedCacheRevision` in `r2_material_provider.cpp` when the r2tex library or its output changes; deleting the `converted` directory is always safe.

After the converted BSP is served, the provider queues BSP material-name parsing and VMT/VTF conversion on a worker pool capped at four threads, with a one-worker minimum and `cores - 2` workers when that is between one and four. Its queue is capped at 8192 jobs and generated-file LRU at 16 MiB; the decoded RPak family cache defaults to 256 MiB and is configurable with `r2port_tex_cache_mb`. R1 remains first and the provider does not activate for stock R1 maps. Startup, index, callback, conversion, and signon timings, plus material/texture provider hits and misses, are logged with steady-clock timestamps; callbacks over 250 ms log a warning.
The VPK hook checks the provider's read-mostly name index before issuing its extra native filesystem existence probe, so unrelated misses do not trigger another disk lookup. `R1DELTA_R2PORT_TRACE_MISSES` accepts one or more comma-, semicolon-, or pipe-separated path substrings; it records aggregate counts for all material requests and tracks paths only when they match. `R1DELTA_R2PORT_TRACE_FS` uses the same path-substring filter and records up to 4096 matching filesystem calls in a bounded ring without per-call logging. Values such as `0`, `1`, `false`, `true`, `all`, and `*` do not enable tracing. Run `r2port_dump_stats` to print provider counters and write the captured trace to `<gamedir>/r2port/fs-trace.txt` (the ring holds more entries than the console keeps); normal runs leave both traces disabled. The VPK hook no longer handles `materials/` paths at all; the Open hooks trace as `IFileSystem::Open[...]` and `CBaseFileSystem::Open[...]`.

`<gamedir>/r2port/patches` may contain `<map>.r2patch` physics patches and `<map>.ain.r2patch` AI-graph packages. Physics patches replace lump `0x1D` only when their SHA-256 matches the exact source R2 BSP; AI graphs are matched against the converted BSP after any physics replacement. The provider supplies the live map version and a zero CRC only when `developer` is 0, because the developer-mode entity CRC path is not reproduced. Missing, malformed, or mismatched packages fall back to the converter's physics stub and no AI graph.

When the engine's VPK read path reports a miss before an ordinary filesystem open, the provider writes only that requested BSP or entity file to `<gamedir>/r2port_cache/<map>-<package-fingerprint>/maps/` and adds the fingerprinted directory as a tail `GAME` search path, after R1 paths. The fingerprint is SHA-256 over the matching R2 directory VPK and chunk-file names, sizes, and modification times. Before materializing, the hook checks the original R1 filesystem so existing R1 files continue to win. The cache stays under `<gamedir>` because the engine mounts it as a `GAME` search path; it contains only generated BSP and entity files and is removable with `r2port_clear_cache`.

The R1 map-pack material provider (`r1_map_pack_material_provider.cpp`) is registered after the R2 provider for `materials/`. A background thread indexes every `*client_mp_*.bsp.pak000_dir.vpk` in `<cwd>/vpk` (not `client_mp_common`). A request that R1's own lookup missed and the R2 provider could not answer is read from the pack that holds it, for example `materials/cable/zipline.vmt` (R1 forces it for ropes; served from `englishclient_mp_airbase`) and `materials/tools/toolsskybox.vmt`. R1 content always wins, and the first served zipline or skybox file is logged once.

The R2 model provider (`r2_model_provider.cpp`, converter vendored under `engine/r2port/modelconv` by `tools/sync-r2port-modelconv.ps1`) answers `models/**.mdl`, `.vvd`, `.dx11.vtx`, `.vtx`, `.phy` and `.vvc` for the 20 allowlisted maps. R2 models are MDL v53 files with embedded companions inside `englishclient_<map>.bsp.pak000_dir.vpk` and `englishclient_mp_common.bsp.pak000_dir.vpk`; the converter turns them into R1 MDL v52 plus companions. A model whose `.mdl` R1 already has (checked with the native existence probe) is never served from R2, including its companions, because a converted R2 VVC or PHY next to an R1 MDL crashes the client. The converted R1 MDL keeps the one-frame `@ref` animation and the `ref` sequence: client-side animating entities (`prop_dynamic`) index sequence 0 unconditionally and crashed in `client.dll+0x104DC` when the converter dropped them. Conversions are cached per root in `<gamedir>/r2port/cache/models/<revision>/<package signature>/` (bump `kModelCacheRevision` when the converter changes). When the BSP is served, a background thread converts every model named in the converted BSP's `sprp` lump and in the five entity files, and registers each model's materials with the material provider as known paths (`PrefetchR2ModelMaterials`); the MDL read registers them again, so a material is known before R1 resolves it. Known paths whose VMT or textures were cached from an earlier map are re-registered after a map change. `r2port_dump_stats` prints the model counters.

`R2MapPackFileProvider` (in `r2_map_provider.cpp`, registered for `materials/` after the other providers) serves `.vtf`, `.vmt` and `.raw` files that the active R2 map's own directory VPK carries and nothing else answers: the `cloudmask` textures and `color_correction` `.raw` files that only entity keys name (present in all 20 maps' VPKs), plus a few map-specific VMT and VTF files.

## Master server policy

R2 map servers are not advertised in this branch. The heartbeat POST, delist DELETE, and heartbeat sender are disabled. The client server-list GET remains for browsing, and the `/server-token` request remains for client authentication; it only obtains a token and does not publish server metadata. Revert the broadcast-disable commit before shipping if master publication is approved.

## Test roots

The user's `S:\game\r2` is their separate play tree; they can launch it directly through `S:\game\r2\r1delta.exe`, with `S:\game\r2\r1delta` beside it. Agents MUST NOT stage, reset, or run tests from it. Agents also do not modify `S:\game\mp` or `S:\game\r1delta`. `S:\game\r2dev` is the golden template and staging reference; agents do not launch tests from it.

Agent roots:

- `r2-provider` for ProviderHook
- `r2-texlib` for TexLib
- `r2-shader` shared by ShaderLab and RootBuilder
- `r2-lead` for the lead

Create, stage, and verify an agent root from the pinned template:

```powershell
powershell -ExecutionPolicy Bypass -File .build\r2-root.ps1 -Create -Root r2-provider -From r2dev
powershell -ExecutionPolicy Bypass -File .build\r2-root.ps1 -Stage -Root r2-provider
powershell -ExecutionPolicy Bypass -File .build\r2-root.ps1 -Verify -Root r2-provider
powershell -ExecutionPolicy Bypass -File .build\r2-root.ps1 -Status -Root r2-provider
```

Use `-Reset -Root <agent-root>` to remove and recreate only that agent root, then run `-Stage` again before launching. Use `-Remove -Root <agent-root>` to delete one agent root safely, `-List` to report all roots, and `-Status -Root <agent-root>` to show its creation time, private payload MiB, shared hardlink MiB, and running process count. Creation is refused when `C:` has less than 3 GiB free. `r2dev` is template-only; `-Create`, `-Reset`, and `-Remove` do not operate on it.

Each launch root has root files hardlinked from `S:\game\mp`, `bin`/`r1`/`tools` junctions like `mp`, and private real `crash`/`crashes` directories. Each CORE is an independent clone pinned to `316993bd0142e706ea5386943530f2b8d2c711dd` with `core.autocrlf=false`; `scripts`, `resource`, and `maps` are private checkouts, while `bin_delta` and staged executables are private copies. Large existing CORE content under `bin`, `materials`, `models`, `sound`, `cfg`, `config`, `admin`, `bin_nexon`, and `vgui` is in real directories whose existing files are hardlinks to the template. Hardlinks share disk blocks and file metadata, not directory entries: agents may add files or unlink their own link, but MUST NOT open an existing hardlinked file for writing. Fixture installers unlink the agent root's file before replacing it and restore the original from `r2dev` on removal. Runtime additions and fixture directories are locally excluded from Git status; tracked files are still checked against pinned Git blobs. `-Verify` checks layout, hardlink identities, sample blob hashes, private write probes, and absence of IDA databases.

`-Stage -Root <agent-root>` updates only that root and refuses while a process is running from the same root; other agent roots may stay active. There is no shared `GAME.lock` or stage/unstage/core-swap flow for agent runs. `-Remove` unlinks hardlinks and removes junctions without following them.

Run clients or servers with the assigned owner and root. Host/client UDP ports are allocated from the owner's ranges; MCP TCP is allocated and reserved when `-McpPort` is supplied. Explicit port parameters must be in the owner's ranges. The registry at `C:\Users\r3muxd\tmp\r2port\runs\` detects port collisions across concurrent runs. Roots are isolated from each other, but loose fixtures affect every run from the same root.

| Owner | Root | MCP TCP | Host/server UDP | Client UDP |
|---|---|---:|---:|---:|
| ProviderHook | `r2-provider` | 8760-8769 | 27060-27069 | 27090-27099 |
| TexLib | `r2-texlib` | 8780-8789 | 27080-27089 | 27080-27089 |
| ShaderLab | `r2-shader` | 8770-8779 | 27070-27079 | 27070-27079 |
| RootBuilder | `r2-shader` | 8790-8799 | 27110-27119 | 27110-27119 |
| Lead | `r2-lead` | — | 27050-27059 | 27050-27059 |

Headless dedicated map/script check (default hidden window, at most 180 seconds):

```powershell
powershell -ExecutionPolicy Bypass -File .build\run-in-r2.ps1 -Owner RootBuilder -Root r2-shader -Mode Dedicated -Map mp_box -Seconds 15 -FatalScriptErrors
```

For a client that starts at the menu and receives its first map command over MCP, use `-Mode Client` without `+map`; MCP clients omit `-console` by default and start minimized. Example:

```powershell
powershell -ExecutionPolicy Bypass -File .build\run-in-r2.ps1 -Owner ProviderHook -Root r2-provider -Mode Client -McpPort 8760 -WindowStyle Minimized -Seconds 180 -FatalScriptErrors -GameArguments @('-w','1280','-h','720','+r_blurmenubg','0','+r_drawvgui','0')
```

`run-in-r2.ps1` identifies and stops only processes whose executable path is under the selected root (accepting its `S:` and physical `C:` aliases), adds `-multiple -allowmultiple` for client/listen launches, and caps a run at 180 seconds. `-NoConsole` overrides the mode default; `-WindowStyle` accepts `Normal`, `Minimized`, or `Hidden` (client/listen default minimized, dedicated default hidden). Use the legacy dedicated server for short map/script checks; the stock `mp_box` baseline is a 15-20 second check, once per staged build. Use client UI only when rendered frames are required.

