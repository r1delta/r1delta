"""Overlays a development build onto an installed R1Delta for testing, and takes it off again.

Usage:
  python tools/dev-overlay.py apply [--dll BUILD/tier0.dll] [--core CORE_CHECKOUT [--base origin/master]] [--app APP_DIR]
  python tools/dev-overlay.py restore [--app APP_DIR]
  python tools/dev-overlay.py status [--app APP_DIR]

`apply` copies the built tier0.dll (and its .pdb when present) into APP_DIR/r1delta/bin_delta and every
file a CORE checkout changes against BASE (committed, modified or untracked) into APP_DIR/r1delta, the
folder the game loads CORE from. It saves each file it replaces under APP_DIR/.dev-overlay first and
records what it added, so `restore` puts the installation back exactly. Applying again restores the
previous overlay first. APP_DIR defaults to the newest %LOCALAPPDATA%/R1Delta/app-* folder.

Unlike an addon, files placed this way are found whatever case the game asks for them in.
The game must not be running: it holds tier0.dll and the files it loaded open.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

STATE_DIR = ".dev-overlay"
MANIFEST = "manifest.json"


def newest_app_dir() -> Path:
    root = Path(os.environ["LOCALAPPDATA"]) / "R1Delta"
    apps = sorted((p for p in root.glob("app-*") if p.is_dir()),
                  key=lambda p: tuple(int(x) for x in p.name[4:].split(".") if x.isdigit()))
    if not apps:
        raise SystemExit(f"no R1Delta installation under {root}")
    return apps[-1]


def game_running() -> bool:
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq Titanfall.exe", "/NH"],
                         capture_output=True, text=True).stdout
    return "Titanfall.exe" in out


def git(repo: Path, *args: str) -> list[str]:
    r = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, check=True)
    return [line for line in r.stdout.splitlines() if line]


def core_files(checkout: Path, base: str) -> list[str]:
    changed = set(git(checkout, "diff", "--name-only", "--diff-filter=d", base))
    changed |= set(git(checkout, "ls-files", "--others", "--exclude-standard"))
    deleted = git(checkout, "diff", "--name-only", "--diff-filter=D", base)
    if deleted:
        raise SystemExit("an overlay cannot delete files; the branch removes: " + ", ".join(deleted))
    return sorted(changed)


def load_manifest(app: Path) -> dict | None:
    path = app / STATE_DIR / MANIFEST
    return json.loads(path.read_text()) if path.exists() else None


def restore(app: Path) -> int:
    manifest = load_manifest(app)
    if manifest is None:
        print("no overlay applied")
        return 0
    state = app / STATE_DIR
    for rel, kind in manifest["files"].items():
        target = app / rel
        if kind == "added":
            target.unlink(missing_ok=True)
        else:
            shutil.copy2(state / "backup" / rel, target)
    shutil.rmtree(state)
    print(f"restored {len(manifest['files'])} file(s) in {app}")
    return 0


def apply(app: Path, dll: Path | None, checkout: Path | None, base: str) -> int:
    restore(app)
    wanted: dict[str, Path] = {}
    if dll:
        wanted["r1delta/bin_delta/tier0.dll"] = dll
        pdb = dll.with_suffix(".pdb")
        if pdb.exists():
            wanted["r1delta/bin_delta/tier0.pdb"] = pdb
    if checkout:
        for rel in core_files(checkout, base):
            wanted["r1delta/" + rel] = checkout / rel
    if not wanted:
        raise SystemExit("nothing to overlay: pass --dll and/or --core")

    state = app / STATE_DIR
    files: dict[str, str] = {}
    for rel, source in wanted.items():
        target = app / rel
        if target.exists():
            backup = state / "backup" / rel
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, backup)
            files[rel] = "replaced"
        else:
            files[rel] = "added"
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        # Record as we go, so an interrupted apply can still be restored.
        (state / MANIFEST).write_text(json.dumps({"files": files}, indent=1))
    print(f"overlaid {len(files)} file(s) onto {app}: "
          f"{sum(k == 'replaced' for k in files.values())} replaced, {sum(k == 'added' for k in files.values())} added")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["apply", "restore", "status"])
    ap.add_argument("--app", type=Path, help="installation folder (default: newest app-* folder)")
    ap.add_argument("--dll", type=Path, help="built tier0.dll")
    ap.add_argument("--core", type=Path, help="CORE checkout whose changed files to overlay")
    ap.add_argument("--base", default="origin/master", help="CORE ref the changes are taken against")
    args = ap.parse_args()

    app = args.app or newest_app_dir()
    if args.command == "status":
        manifest = load_manifest(app)
        if manifest is None:
            print(f"{app}: no overlay applied")
        else:
            for rel, kind in sorted(manifest["files"].items()):
                print(f"{kind:8} {rel}")
        return 0
    if game_running():
        raise SystemExit("Titanfall.exe is running; quit the game first")
    if args.command == "restore":
        return restore(app)
    return apply(app, args.dll, args.core, args.base)


if __name__ == "__main__":
    sys.exit(main())
