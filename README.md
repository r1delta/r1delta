# R1Delta
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/r1delta/r1delta)

Dedicated server framework for Titanfall 1 that allows hosting custom servers and loading mods.

### Disclaimer
**This is a development preview (a.k.a beta) where things may and WILL break over time and features that you might see in-game might not be available in the future.**

By reading beyond this disclaimer you hereby accept this and lose any right of complaining negatively about it.

# Installation
To get started, go to the [releases page](https://github.com/r1delta/r1delta/releases) and download the latest `Setup.exe`

If you're having issues please open an issue or join the [discord](https://discord.gg/zbFCcSM5t7)

For developers and contributors, keep reading below.

## Hosting a server
You can run "R1Delta Dedicated Server" on Windows in the start menu to run a dedicated server, or if you want to host a listen server you can just click "Private Match" from the lobby menu in R1Delta.

Instructions on how to run the dedicated server on Docker are [here](https://gist.github.com/quad-damage/3f4ad9c524b638510340cc31da86d3ff). This image is managed by one of our contributors ([@quadruple](https://github.com/quad-damage)) and not officially maintained by R1Delta, but we still endorse it.

The master server will not show your server if it can't be connected to publicly, so make sure to port-forward 27015 (and onwards if you're running multiple servers, 27016 etc) if you want it to be listed. 

If you want to make a friends-only server and hide it from the server browser you should put `hide_server 1` into the console, you can then give out your ip and port for a direct connect (in the style of ip:port) or use Discord game invites, just note that you'll still need to port-forward if your friends are going to connect over the internet. You can also set `sv_password` if you'd like to password protect your server but keep it on the server browser, just keep in mind this *will* leak your server's IP.

# Building From Source
1. [Install vcpkg and run vcpkg integrate install](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started-msbuild)
2. Make your dev root directory somewhere (on the disk you want to build R1Delta on) like `C:\depot\r1delta`
3. Run the following commands in a command prompt (**not as Administrator**):
4. `subst S: C:\depot\r1delta` 
5. `git clone --recursive http://github.com/r1delta/r1delta S:\src`
6. `mkdir S:\game`
7. `git clone http://github.com/r1delta/CORE S:\game\r1delta`
8. Build S:\src\r1delta.sln (open Dll1.sln, restore the NuGet packages if it's not done automatically, and build the solution)
9. If you did this right, you should now have `S:\game\r1delta.exe` and it should boot properly.
10. It is recommended, though not required, that you put your original R1 install (or delta'd R1 content) at S:\content.
   
If you want to make the S: subst persistent across restarts, you need to import a registry key.
For example, this is for `C:\depot\r1delta`:
```
REGEDIT4 

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\DOS Devices] 
"S:"="\\??\\C:\\depot\\r1delta"
```

## Predicted script movement (API 3)

Movement is opt-in per player. Register the same program name, source text, and slot count on CLIENT and SERVER with `Movement_Register(name, source, slots, presentationName)`, then call `Movement_Enable(player, name)` on SERVER. Registration does not change movement or require the native hooks to be ready yet. `Movement_Disable(player)` restores ordinary movement. Identical registration/enabling is harmless; replacement starts a new session and clears state. There is one active program per player.

Keep shared declarations under `scripts/vscripts/movement` and include them from the base client/server mapspawn paths, not autorun. A VM supports 16 programs, names of 1–32 ASCII letters/digits or `_./-`, source up to 65,536 bytes, and 0–64 native state slots. `Movement_GetApiVersion()` returns `3`; `Movement_IsAvailable()` and `Movement_GetLastError()` expose native readiness/errors. SERVER can query `Movement_IsEnabled(player, name)`; an empty name matches any enabled program.

### Simulation and state

The synchronous source executes for each simulated command, including prediction replays. `MOVE_SETUP` edits command inputs before native preparation, `MOVE_SIMULATE` runs before the native dispatcher, and `MOVE_FINISH` observes the result before publication. Native collision and locomotion still execute once. Command/tick/time accessors describe that simulation, not a render frame; integrate forces using `Move_GetFrameTime()`.

Programs run in the owning script VM and may call ordinary APIs, including replicated convar getters. This does **not** make ordinary globals, entity script fields, arbitrary native setters, `SetServerVar`, sounds, or spawned entities rollback-safe. Deterministic simulation state belongs in `Move_Get/SetIntState(index)` or `Move_Get/SetFloatState(index)`. These are two views of the same 32-bit word; use one consistent type per slot. Do not yield, wait, start threads, or produce external effects during simulation. Shared helper code and any convars it reads must also agree between peers.

`Move_GetPlayer()` returns the evaluated native player entity and requires an existing native script instance. It is not a persistent-state container. `Move_GetPlayerHandle()` instead returns the serial-qualified network identity used by movement entity queries. Check validity before querying other entities. `Move_GetTraceSurface()` adds the native surface index to the existing hull-trace result, or `-1` when absent.

Custom slots, native jump resources, and authoritative native frames are restored together for replay. Cached frames retain their matching frozen custom state. Missing/incompatible programs or unavailable history disconnect explicitly rather than silently running different movement.

### Command-captured custom input

CLIENT may register a synchronous named callback with `Movement_SetInputCallback(name, callbackName)`. It runs once while building a new native usercmd, not on replay or retransmission. Only inside it, use `Move_InputGetButtons()`, `Move_InputSetButtons(bits)`, and `Move_InputGetCommandNumber()`. Simulation reads the captured 32-bit payload through `Move_GetCustomButtons()`.

This uses the six unused camera-data words gated by `hasCameraData`: one button word and five reserved zero words. The native command/verified-command sizes stay `0x134`/`0x138`; normal copy, seed, checksum, backup, and delta decoding remain in use. The writer preserves raw integer bits, including float NaN-shaped patterns. Do not also use those fields for camera/headtracking data in a v3 session.

### Optional native checkpoints

In `MOVE_SETUP`, call `Move_SetCheckpointMask(mask)` with the desired `MOVE_CHECKPOINT_*` bits; the default is zero. `MOVE_CHECKPOINT_ALL` enables all eight. A checkpoint executes at its real native decision point, not as another movement pass. The mask is reset each command.

| Phase / matching checkpoint bit | Native context or control |
| --- | --- |
| `MOVE_FRICTION` / `FRICTION` | Wish direction/speed, native deceleration, surface friction, wall normal, and output wish velocity. `Move_ReplaceFriction(true)` replaces this friction operation; otherwise native friction runs, including the existing friction scale. |
| `MOVE_GROUND_CONTACT` / `GROUND_CONTACT` | Actual ground publication. `Move_HasGroundNormal()` gates `Move_GetGroundNormal(axis)`; a player's gravity-up vector is not a contact plane. Persist a needed contact normal in slots. |
| `MOVE_JUMP` / `JUMP` | Read or change the selected legal native jump height with `Move_Get/SetSelectedJumpHeight()`. This does not manufacture a jump request. |
| `MOVE_JUMP_FINISH` / `JUMP_FINISH` | `Move_DidJump()` reports whether the legal native jump actually happened. |
| `MOVE_START_GRAVITY` / `START_GRAVITY` | `Move_GetGravityContribution()` reports the pending native half-gravity contribution. |
| `MOVE_FINISH_GRAVITY` / `FINISH_GRAVITY` | The second native half-gravity contribution, for coherent slope/rope forces. |
| `MOVE_DUCK` / `DUCK` | Native duck request/state and coherent transition timing. `Move_SetDuckDuration(milliseconds)` accepts 1–1023, matching the native network field. Native unduck timing remains intact. |
| `MOVE_WALK` / `WALK` | Before native ground walking and its scalar getters. |

The bit names after `/` in the first column are prefixed with `MOVE_CHECKPOINT_`. Checkpoint-only getters/setters reject the wrong phase. Other v3 controls include acceleration/deceleration overrides and their clear functions, a multiplier at the native move-scale getter, duck retention, crouch-jump permission, and sprint permission. Acceleration/deceleration overrides precede native player modifiers; sprint permission cannot override a native refusal. Context getters expose world-up eligibility, water jump/level, stance, duck state/request, view/input direction, and the native raw move scale. Controls are command-local, not persistent player patches.

### Committed presentation

Replay must simulate again; presentation must not. `Move_EmitEvent()` still delivers IDs 0–31 once per session/command/event to the registered client callback after prediction unwinds. `MOVE_EVENT_RESET` requests cleanup on disable, replacement, disconnect, VM destruction, or shutdown.

For ongoing effects, ordinary CLIENT code can use `Movement_HasPresentation(name)` and `Movement_GetPresentationInt/Float(name, slot)`, plus the `Command`, `Session`, and `Time` getters. These read the final committed snapshot, never replay scratch. Ordinary SERVER code has corresponding `Movement_HasPlayerState(player, name)` and `Movement_GetPlayerStateInt/Float/Command/Session/Time` accessors for published authoritative state. Simulation time is not ordinary render `Time()`; anchor cosmetic interpolation to the snapshot clock.

`Movement_SetView(name, fovScale, forward, right, up, pitch, yaw, roll)` composes a session-owned FOV multiplier and camera-relative viewmodel contribution with native view calculation/bob/lag. `Movement_ClearView(name)` removes it. CLIENT `Movement_SetActivity(player, session, activity)` requests a model-validated activity; unsupported activities return false. Native exclusive death/traversal selection and bookkeeping remain in control. Clear it with `Movement_ClearActivity(player, session)`. These are presentation sinks, not authoritative animation state or substitutes for missing model sequences/assets.

### Slide/grapple reference

The base multiplayer mapspawn includes and registers `movement/pilot`; it does not automatically enable it. On a loaded multiplayer map:

```text
script printt(Movement_IsAvailable())
script PilotMovement_Enable(GetPlayerArray()[0])
bind mouse4 +scriptCommand3
```

Crouch while moving to slide. The grapple samples `+scriptCommand3` into custom input; press again to cancel, rather than releasing the key. Disable with:

```text
script PilotMovement_Disable(GetPlayerArray()[0])
```

`pilot.slide_grapple` combines the independently authored slide and grapple sources in 64 slots. Sliding uses native friction, landing, jump, gravity, duck, and movement-scalar checkpoints. Grapple flight, static world anchoring, launch, winching, four-corner routing, and detach state are predicted; ordinary consumers own procedural local cable/claw geometry, remote native ropes, and weapon holster/deploy cleanup. It is not a transfer of private SP content or Powder assets/source. Exact authored slide body/viewmodel sequences, surface-specific sound/particles, and grapple model/audio require separately licensed assets and their mappings.

Use `cl_localnetworkbackdoor 0` for multiplayer prediction. Both peers need matching CODE/CORE and supported native binaries; v3 is not wire-compatible with older movement sessions. No-program players retain the native movement path.

# Credits
- [@r3muxd](https://github.com/r3muxd)
- [@sonny-tel](https://github.com/sonny-tel)
- [@mrsteyk](https://github.com/mrsteyk)
- [@PANCHO7532](https://github.com/PANCHO7532)
- [@quadruple](https://github.com/quad-damage)
- [@mv](https://github.com/mvoolt)
- [@NachosChipeados](https://github.com/NachosChipeados)
- [@dogecore](https://github.com/HappyDOGE)
- [@Allusive](https://github.com/AllusiveWheat)
- [@koutsie](https://github.com/koutsie)
- [@dyslexi](https://github.com/Dys-lexi/)
- [@Akabenko](https://github.com/Akabenko): hints on the Titanfall 2 map port implementation and on the sharpening filter.

## Third-Party Code Attribution

R1Delta incorporates code from several open-source projects. We are grateful to the developers of these projects for their contributions to the open-source community.

- **[R1Spectre/Spectre](https://github.com/R1Spectre/Spectre)**: GPLv3 License
- **[ValveSoftware/GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets)**: BSD 3-Clause License
- **[R2Northstar/NorthstarLauncher](https://github.com/R2Northstar/NorthstarLauncher)**: MIT License
- **[ValveSoftware/source-sdk-2013](https://github.com/ValveSoftware/source-sdk-2013)**: Source 1 SDK License
- **[p0358/black_market_edition](https://github.com/p0358/black_market_edition)**: LGPL v2.1 License
- **[facebook/zstd](https://github.com/facebook/zstd)**: Dual-licensed under BSD and GPLv2
- **[TsudaKageyu/minhook](https://github.com/TsudaKageyu/minhook)**: BSD 2-Clause License
- **[Mauler125/r5sdk](https://github.com/Mauler125/r5sdk)**: Source 1 SDK License
- **[IcePixelx/silver-bun](https://github.com/IcePixelx/silver-bun)**: TODO
- **[snake-biscuits/bsp_regen](https://github.com/snake-biscuits/bsp_regen)**: GPLv3 License
- **[nlohmann/json](https://github.com/nlohmann/json)**: MIT License

R1Delta itself is licensed under the GNU General Public License v3.0 (GPLv3). For full license texts and additional legal notices, please refer to the LICENSE file and thirdpartylegalnotices.txt in this repository.

# Bear
![bear](https://github.com/r1delta/r1delta/assets/37985788/41548f20-0878-4e1e-8538-e9be808fc363)
![bear](https://github.com/r1delta/r1delta/assets/37985788/41548f20-0878-4e1e-8538-e9be808fc363)
![bear](https://github.com/r1delta/r1delta/assets/37985788/41548f20-0878-4e1e-8538-e9be808fc363)

# FAQ

## Q: What if I have hiccups
Remind yourself you are not a fish
