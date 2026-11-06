# Script-error telemetry: modded games and modded files

R1Delta reports sanitized script errors to the service in `script-error-service/` (`engine/net/script_error_telemetry.cpp`).
A report says whether the game, and each callstack frame's file, was modded, so errors that come from mods can be ranked
apart from errors that are ours. Mods reach scripts three ways, and each has its own reason:

| Reason | Meaning | Detected by |
|---|---|---|
| `addons` | At least one addon under `r1/addons` is enabled. | `FileCache::TrySnapshotEnabledAddons`, read on every report. |
| `core` | A file in the app's own `r1delta` tree differs from, or is absent from, the release's content manifest (Pilot Bots writes into every `app-*\r1delta`). | Manifest comparison, once per process, and again for each frame's file. |
| `native` | A DLL in the game folder that neither R1Delta nor retail Titanfall ships (`pilotbots.dll`), or a shipped DLL whose bytes changed (the patched `tier0.dll`). | Loaded-module scan against the manifest, once per process. |

A frame is `modded` when an enabled addon provides its file (`r1/addons/<name>/scripts/vscripts/<source>`) or its copy in
the `r1delta` tree differs from or is missing from the manifest. That catches mod content under stock paths
(`client/_cl_material_proxies.nut` with a mod's function inside), which a path filter cannot.

## Content manifest

`r1delta/content_manifest.txt` lists the shipped files (format in `engine/net/script_modding.h`):

```
r1delta-content-manifest 1
S <sha256> scripts/vscripts/...        scripts/vscripts/** and resource/**/*.res|*.menu
N <sha256> bin_delta/tier0.dll         *.dll under bin, bin_nexon, bin_delta
```

CI writes it into the payload's `r1delta` folder after the CORE copy and the build outputs are in place
(`ci/new_content_manifest.ps1`), and `ci/verify_release_payload.ps1` checks it: the file set must equal the files on disk
and every hash must match, also on the packaged payload. For a local root, regenerate it after every CORE refresh or
restage (the `tier0.dll` hash changes with each build):

```
pwsh ci/new_content_manifest.ps1 -RootPath <root>\r1delta
```

Without a manifest the game cannot judge `core` and the native modules it ships: the report then carries only `addons`,
and the console line says `content manifest unavailable, core not checked`.

## Process facts

`engine/net/script_mod_state.cpp` hashes the content tree and the loaded DLLs once per process on a low-priority thread
(started from `ScriptErrorTelemetry::Initialize`). Nothing runs on the game thread: reports are classified on the upload
worker, which reuses the scan, re-reads the addon list and hashes only the files of the report's frames (cached by file
time and size). The pure rules live in `engine/net/script_modding.cpp` (unit tests: `tests/script_modding_tests.cpp`).

The classification is always logged: `Script error classified (CLIENT VM): modded: addons,core; 1 of 3 frames modded`.
The scan logs one `Script modding scan:` line naming changed or foreign native modules.

## Wire schemas

`POST /v1/reports` accepts schema 1 (the original report, unchanged) and schema 2, which adds `modded` (a list of
reasons, empty for a stock game) and `modded` on each frame. `GET /health` returns `{"status":"ok","schemas":[1,2]}`.
The client probes `/health` once per service URL and sends schema 2 only when it is advertised; a service that predates
schema 2 (no `schemas` member) or an unreachable one keeps getting schema 1.

## Service

* Fingerprints: a report without modded frames hashes exactly as schema 1 always did, so existing codes are unchanged and
  an unmodded schema 2 report shares the code of the same error from older clients. A modded frame is part of the
  identity (a modded copy of a stock file is a different error). The game-level reasons are not: they are counted.
* Storage (`migrations/0004_modded_counts.sql`): `classified_count` (occurrences from schema 2 clients) and
  `modded_count` (of those, from modded games) per error and per hour bucket. Stored frames carry `"modded": true` where
  it applies. Schema 1 occurrences are unclassified, so the modded share is `modded / classified`, never a share of all
  occurrences.
* Admin ranking (`/admin`, `/v1/admin/top`): a `Modded (window)` column with the share, and `?modded=all|exclude|only`.
  `exclude` drops occurrences known to come from modded games and keeps unclassified ones; `only` counts modded
  occurrences. Cascade counts are not filtered.
* Public page (`/errors/<code>`): modded frames are marked `[modded file]` and the header states the modded share.

Deploy order: run `npm run db:migrate` (0004), then deploy the worker; clients switch to schema 2 on their own once
`/health` advertises it.
