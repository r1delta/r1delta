"""Frame-time and memory benchmark for an R1Delta client in a private game root (background mode, MCP driven).

One run = one listen-server process that walks a map sequence. On every map it parks a noclip camera at a fixed point, turns
it continuously (`+right`, cl_yawspeed 60: a full turn every 6 s) and records two PresentMon windows: `entry` right after
spawn (first-use work: shader variants, R2 background conversion) and `steady` after a settle delay. Process private bytes
are sampled after every map load and at the end of each map, so R2 -> R1 -> lobby transitions show whether memory is
released. Results are appended as JSON lines.

  python perf_bench.py --root <game root> --label <name> --mcp 9480 --port 29480 --client-port 29481 \
      --maps mp_lobby,mp_thaw,mp_angel_city,mp_lobby --env R2_UBER=off --out results.jsonl
  python perf_bench.py --summarize results.jsonl

Needs: psutil, PresentMon 2.x (--presentmon, run elevated or as a Performance Log Users member). Builds older than v3.0
lack R1DELTA_SAVEDGAMES and background mode; --inject <dll> loads perf_bench_redirect.dll (perf_bench_redirect.cpp next to
this script: SavedGames/Documents redirect + never-activating window) into the suspended process first and runs without
background mode (+not_focus_sleep 0, window off the desktop). Never run such a build against the real profile. Background
mode costs about 10% frame time against that mode, so compare builds in the same mode. Uncapped frame rate:
R1DELTA_BACKGROUND_FPS=0, fps_max 0, mat_vsync 0.
"""
import argparse
import csv
import ctypes
import json
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from ctypes import wintypes as wt
from pathlib import Path

import psutil

# Fixed camera per map: noclip origin and pitch. Maps not listed only record memory.
CAMERAS = {
    "mp_angel_city": (2032, -1778, 400, 10),
    "mp_airbase": (-2339, 2292, 380, 10),
    "mp_eden": (-1619, 9, 346, 10),
}
# Other maps: the spawn point + 280 units (logged as "camera"; pin a good one above).
DEAD_TELEMETRY = "http://127.0.0.1:9/"
DETACHED = 0x00000008 | 0x00000200


class Mcp:
    def __init__(self, port):
        self.base = f"http://127.0.0.1:{port}/"
        self.session = None
        self.next_id = 1

    def rpc(self, method, params=None, timeout=60):
        self.next_id += 1
        body = json.dumps({"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params or {}}).encode()
        headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if self.session:
            headers["Mcp-Session-Id"] = self.session
        with urllib.request.urlopen(urllib.request.Request(self.base, body, headers), timeout=timeout) as r:
            sid = r.headers.get("Mcp-Session-Id")
            if sid:
                self.session = sid
            text = r.read().decode("utf-8", "replace")
        return json.loads(text) if text else None

    def tool(self, name, arguments, timeout=120):
        if not self.session:
            self.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                    "clientInfo": {"name": "perf-bench", "version": "1"}}, timeout=10)
        reply = self.rpc("tools/call", {"name": name, "arguments": arguments}, timeout=timeout)
        if reply and "error" in reply:
            raise RuntimeError(f"{name}: {reply['error']}")
        result = (reply or {}).get("result", {})
        if result.get("structuredContent") is not None:
            return result["structuredContent"]
        return "\n".join(c.get("text", "") for c in result.get("content", []) if c.get("type") == "text")

    def cmd(self, command, capture=False):
        return self.tool("execute_console_command", {"command": command, "capture_output": capture})

    def sq(self, context, code):
        return self.tool("execute_squirrel", {"script": code, "context": context, "capture_output": True})

    def state(self):
        return self.tool("get_game_state", {}, timeout=15)


# --- launch -----------------------------------------------------------------------------------------------------------
class _StartupInfo(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD), ("lpReserved", wt.LPWSTR), ("lpDesktop", wt.LPWSTR), ("lpTitle", wt.LPWSTR),
                ("dwX", wt.DWORD), ("dwY", wt.DWORD), ("dwXSize", wt.DWORD), ("dwYSize", wt.DWORD),
                ("dwXCountChars", wt.DWORD), ("dwYCountChars", wt.DWORD), ("dwFillAttribute", wt.DWORD),
                ("dwFlags", wt.DWORD), ("wShowWindow", wt.WORD), ("cbReserved2", wt.WORD),
                ("lpReserved2", ctypes.c_void_p), ("hStdInput", wt.HANDLE), ("hStdOutput", wt.HANDLE),
                ("hStdError", wt.HANDLE)]


class _ProcessInformation(ctypes.Structure):
    _fields_ = [("hProcess", wt.HANDLE), ("hThread", wt.HANDLE), ("dwProcessId", wt.DWORD), ("dwThreadId", wt.DWORD)]


def _kernel32():
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.VirtualAllocEx.restype = ctypes.c_void_p
    k32.VirtualAllocEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wt.DWORD, wt.DWORD]
    k32.WriteProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
    k32.CreateRemoteThread.restype = wt.HANDLE
    k32.CreateRemoteThread.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p,
                                       wt.DWORD, ctypes.c_void_p]
    k32.GetProcAddress.restype = ctypes.c_void_p
    k32.GetProcAddress.argtypes = [wt.HMODULE, ctypes.c_char_p]
    k32.GetModuleHandleW.restype = wt.HMODULE
    k32.GetExitCodeThread.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD)]
    return k32


def _remote_load(k32, process, dll):
    data = (dll + "\0").encode("utf-16-le")
    memory = k32.VirtualAllocEx(process, None, len(data), 0x3000, 0x04)
    k32.WriteProcessMemory(process, memory, data, len(data), None)
    load_library = k32.GetProcAddress(k32.GetModuleHandleW("kernel32.dll"), b"LoadLibraryW")
    thread = k32.CreateRemoteThread(process, None, 0, load_library, memory, 0, None)
    if not thread:
        raise OSError(ctypes.get_last_error(), "CreateRemoteThread")
    k32.WaitForSingleObject(thread, 60000)
    code = wt.DWORD()
    k32.GetExitCodeThread(thread, ctypes.byref(code))
    k32.CloseHandle(thread)
    if not code.value:
        raise RuntimeError(f"LoadLibrary({dll}) failed in the target")


def launch_injected(args, cwd, env, dlls):
    """CreateProcess suspended, load `dlls` (shell32 first so a DLL can hook it from DllMain), resume."""
    k32 = _kernel32()
    startup = _StartupInfo()
    startup.cb = ctypes.sizeof(startup)
    info = _ProcessInformation()
    block = "".join(f"{k}={v}\0" for k, v in env.items()) + "\0"
    flags = 0x00000004 | 0x00000400 | DETACHED   # CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT
    if not k32.CreateProcessW(None, ctypes.create_unicode_buffer(subprocess.list2cmdline(args)), None, None, False, flags,
                              ctypes.create_unicode_buffer(block, len(block)), str(cwd), ctypes.byref(startup),
                              ctypes.byref(info)):
        raise OSError(ctypes.get_last_error(), "CreateProcessW")
    try:
        for dll in ["shell32.dll", *dlls]:
            _remote_load(k32, info.hProcess, dll)
    except Exception:
        k32.TerminateProcess(info.hProcess, 1)
        raise
    k32.ResumeThread(info.hThread)
    k32.CloseHandle(info.hThread)
    k32.CloseHandle(info.hProcess)
    return info.dwProcessId


def wait_for_launch_slot(rig: str):
    """Shared machines: the e6 rig's game.py gates client launches on free commit charge and the client count."""
    if rig and Path(rig, "game.py").is_file():
        sys.path.insert(0, rig)
        import game   # noqa: E402 - optional rig dependency
        game.wait_for_launch_slot("client")


def launch(root: Path, a, first_map):
    env = dict(os.environ)
    env.update({"R1DELTA_BACKGROUND": "1", "FROM_DELTA_LAUNCHER": "1", "R1DELTA_BACKGROUND_FPS": "0",
                "R1DELTA_SAVEDGAMES": str(root / "savedgames")})
    for item in a.env:
        key, _, value = item.partition("=")
        if value:
            env[key] = value
        else:
            env.pop(key, None)
    delta = root / "r1delta"
    # game.py's PATH prefix: launcher.dll resolves its retail dependencies through it.
    env["PATH"] = ";".join([str(delta / "bin_delta"), str(delta / "bin"), str(root / "bin" / "x64_retail"),
                            str(root / "r1" / "bin" / "x64_retail"), env.get("PATH", "")])
    args = [str(root / "Titanfall.exe"), "-multiple", "-allowmultiple", "-windowed", "-noborder", "-w", "1280", "-h", "720",
            "-nomessagebox", "-novid", "-dev", "-usemcp", "+mcp_start_http", str(a.mcp), "+developer", "1",
            "+delta_script_error_reporting", "0", "+delta_script_error_service_url", DEAD_TELEMETRY,
            "-port", str(a.port), "+hostport", str(a.port), "+clientport", str(a.client_port),
            "+hide_server", "1", "+sv_cheats", "1", "+fps_max", "0", "+mat_vsync", "0", "+map", first_map]
    if a.inject:
        # No background mode (pre-v3.0 builds, or a comparison run with the root's r1delta_background.flag renamed): the
        # injected DLL keeps the window from activating, so the engine's unfocused sleep must be off; window off the desktop.
        env.pop("R1DELTA_BACKGROUND", None)
        args += ["-x", "-4000", "-y", "0", "+not_focus_sleep", "0"]
    if a.inject:
        env["SGREDIR_PATH"] = str(root / "savedgames")
        return launch_injected(args, root, env, [a.inject])
    return subprocess.Popen(args, cwd=str(root), env=env, creationflags=DETACHED, stdin=subprocess.DEVNULL,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).pid


# --- measurement --------------------------------------------------------------------------------------------------------
def private_mb(pid):
    info = psutil.Process(pid).memory_info()
    return round(info.private / 2**20), round(info.rss / 2**20)


def wait_map(mcp: Mcp, pid, map_name, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not psutil.pid_exists(pid):
            raise RuntimeError(f"process exited while loading {map_name}")
        try:
            state = mcp.state()
            if state.get("in_game") and state.get("current_map") == map_name + ".bsp":
                return
        except (urllib.error.URLError, ConnectionError, TimeoutError, OSError, RuntimeError, json.JSONDecodeError):
            pass
        time.sleep(1)
    raise RuntimeError(f"{map_name} not loaded after {timeout}s")


def player_origin(mcp: Mcp, timeout=90):
    deadline = time.time() + timeout
    code = 'local p = GetPlayerArray(); if (p.len() && IsAlive(p[0])) { local o = p[0].GetOrigin(); printl("ORIGIN " + o.x + " " + o.y + " " + o.z) }'
    while time.time() < deadline:
        match = re.search(r"ORIGIN (\S+) (\S+) (\S+)", mcp.sq("server", code) or "")
        if match:
            return tuple(float(v) for v in match.groups())
        time.sleep(2)
    raise RuntimeError("player never spawned")


def presentmon(exe, pid, seconds, out: Path):
    subprocess.run([exe, "--process_id", str(pid), "--output_file", str(out), "--timed", str(seconds),
                    "--terminate_after_timed", "--no_console_stats", "--v1_metrics", "--session_name", f"perfbench{pid}",
                    "--stop_existing_session"], capture_output=True, timeout=seconds + 90, check=False)


def frame_stats(path: Path):
    with open(path, encoding="utf-8-sig") as f:
        times = [float(row["msBetweenPresents"]) for row in csv.DictReader(f)][1:]
    if len(times) < 20:
        return {"frames": len(times)}
    ordered = sorted(times)
    pct = lambda p: ordered[min(len(ordered) - 1, int(p / 100 * len(ordered)))]
    median = pct(50)
    return {"frames": len(times), "fps": round(1000 * len(times) / sum(times), 1), "p50": round(median, 2),
            "p95": round(pct(95), 2), "p99": round(pct(99), 2), "p999": round(pct(99.9), 2), "max": round(ordered[-1], 1),
            "hitch_2_5x": sum(t > 2.5 * median for t in times), "hitch_50ms": sum(t > 50 for t in times),
            "hitch_100ms": sum(t > 100 for t in times)}


def run(a):
    root = Path(a.root)
    maps = a.maps.split(",")
    out = Path(a.out)
    wait_for_launch_slot(a.rig)
    pid = launch(root, a, maps[0])
    mcp = Mcp(a.mcp)
    record = {"label": a.label, "root": str(root), "env": a.env, "started": time.strftime("%Y-%m-%d %H:%M:%S"), "maps": []}
    scratch = Path(tempfile.mkdtemp(prefix="perfbench"))
    try:
        for index, map_name in enumerate(maps):
            began = time.time()
            if index:
                mcp.cmd(f"map {map_name}")
                time.sleep(3)
            wait_map(mcp, pid, map_name, a.load_timeout)
            entry = {"map": map_name, "load_s": round(time.time() - began, 1), "mem_loaded": private_mb(pid)}
            if map_name != "mp_lobby":
                spawn = player_origin(mcp)
                camera = CAMERAS.get(map_name) or (spawn[0], spawn[1], spawn[2] + 280, 10)
                entry["camera"] = camera
                mcp.cmd(f"noclip; setpos {camera[0]} {camera[1]} {camera[2]}; setang {camera[3]} 0 0; cl_yawspeed 60; +right")
                for window, delay in (("entry", 0), ("steady", a.settle)):
                    time.sleep(delay)
                    csv_path = scratch / f"{map_name}_{window}.csv"
                    presentmon(a.presentmon, pid, a.capture, csv_path)
                    entry[window] = frame_stats(csv_path)
                    csv_path.unlink(missing_ok=True)
                mcp.cmd("-right")
            else:
                time.sleep(a.capture)
            entry["mem_end"] = private_mb(pid)
            record["maps"].append(entry)
            print(json.dumps(entry), flush=True)
    except Exception as error:   # the record keeps what was measured; the failure is part of the result
        record["error"] = str(error)
        print("ERROR", error, flush=True)
    finally:
        for proc in psutil.process_iter(["pid", "exe"]):
            try:
                if (proc.info["exe"] or "").lower().startswith(str(root).lower()):
                    proc.kill()
            except psutil.Error:
                pass
        scratch.rmdir()
    with open(out, "a", encoding="utf-8") as f:
        f.write(json.dumps(record) + "\n")
    return 0 if "error" not in record else 1


def summarize(path):
    rows = [json.loads(line) for line in open(path, encoding="utf-8") if line.strip()]
    cells = {}
    for row in rows:
        for entry in row["maps"]:
            for window in ("entry", "steady"):
                if window in entry and "p50" in entry[window]:
                    cells.setdefault((row["label"], entry["map"], window), []).append(entry[window])
            cells.setdefault((row["label"], entry["map"], "mem"), []).append(entry["mem_end"][0])
    print("| build | map | window | runs | p50 ms | p95 ms | p99 ms | max ms | hitches >2.5x med | >50 ms |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for (label, map_name, window), values in sorted(cells.items()):
        if window == "mem":
            continue
        fmt = lambda key: " / ".join(str(v[key]) for v in values)
        print(f"| {label} | {map_name} | {window} | {len(values)} | {fmt('p50')} | {fmt('p95')} | {fmt('p99')} | "
              f"{fmt('max')} | {fmt('hitch_2_5x')} | {fmt('hitch_50ms')} |")
    print()
    print("| build | map | private MB at map end (per run) |")
    print("|---|---|---|")
    for (label, map_name, window), values in sorted(cells.items()):
        if window == "mem":
            print(f"| {label} | {map_name} | {' / '.join(map(str, values))} |")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--summarize")
    ap.add_argument("--root")
    ap.add_argument("--label")
    ap.add_argument("--mcp", type=int)
    ap.add_argument("--port", type=int)
    ap.add_argument("--client-port", type=int)
    ap.add_argument("--maps", default="mp_lobby,mp_thaw,mp_angel_city,mp_eden,mp_airbase,mp_lobby")
    ap.add_argument("--env", action="append", default=[], help="KEY=VALUE (KEY= removes it)")
    ap.add_argument("--inject", default="", help="DLL loaded before the game starts (SavedGames redirect for old builds)")
    ap.add_argument("--presentmon", default="PresentMon.exe")
    ap.add_argument("--capture", type=float, default=20)
    ap.add_argument("--settle", type=float, default=25)
    ap.add_argument("--load-timeout", type=float, default=600)
    ap.add_argument("--out", default="perf_bench.jsonl")
    ap.add_argument("--rig", default=r"C:\Users\r3muxd\tmp\e6\rig", help="e6 rig directory (launch gate); empty = no gate")
    a = ap.parse_args()
    if a.summarize:
        summarize(a.summarize)
        return 0
    return run(a)


if __name__ == "__main__":
    sys.exit(main())
