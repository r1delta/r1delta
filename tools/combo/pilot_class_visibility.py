#!/usr/bin/env python3
"""Spawn every pilot class on both teams in a running listen/client window and screenshot the third-person body.

usage: pilot_class_visibility.py MCP_PORT OUT_DIR [--respawn] [--map-prefix mp_glitch] [--shots F:\\content3\\r1\\screenshots]

The window must be in game (use `-usemcp +mcp_start_http <port>`, `+sv_cheats 1`, background flag root). For each team
(TEAM_IMC, TEAM_MILITIA) x setfile (pilot_{male,female}_{br,cq,dm}):
  default : SetTeam + SetPlayerSetFile (the call the team-change autobalance and `setfile` command use)
  --respawn: set playerClassData[level.pilotClass].playerSetFile, `kill`, then ClientCommand_RespawnPlayer(p, "Pilot")
            (the real spawn path: RespawnTitanPilot reads the class data)
It then checks that the client VM reports the same model name as the server, runs `thirdperson`, issues `screenshot`,
and converts the new .tga in the engine screenshot dir (the game's cwd, shared by every window) to OUT_DIR/<team>_<setfile>.jpg.
It prints one line per class. Looking at the images is the visibility check; model-name equality only proves the model was applied.
The first SetModel of a model that was never loaded can silently keep the old model (lazy load): the harness retries until the client agrees.
"""
import json
import os
import re
import sys
import time
import urllib.request

from PIL import Image

CLASSES = ["pilot_male_br", "pilot_male_cq", "pilot_male_dm", "pilot_female_br", "pilot_female_cq", "pilot_female_dm"]
TEAMS = ["TEAM_IMC", "TEAM_MILITIA"]


class Mcp:
    def __init__(self, port):
        self.port, self.sess, self.n = port, None, 1
        self.rpc("initialize", {"protocolVersion": "2024-11-05", "capabilities": {}, "clientInfo": {"name": "pcv", "version": "1"}}, 10)

    def rpc(self, method, params=None, timeout=60):
        self.n += 1
        body = json.dumps({"jsonrpc": "2.0", "id": self.n, "method": method, "params": params or {}}).encode()
        h = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if self.sess:
            h["Mcp-Session-Id"] = self.sess
        r = urllib.request.urlopen(urllib.request.Request(f"http://127.0.0.1:{self.port}/", body, h), timeout=timeout)
        self.sess = r.headers.get("Mcp-Session-Id") or self.sess
        t = r.read().decode()
        return json.loads(t) if t else None

    def call(self, name, args):
        return self.rpc("tools/call", {"name": name, "arguments": args})

    def cmd(self, c, capture=True):
        r = self.call("execute_console_command", {"command": c, "capture_output": capture})
        return r["result"]["content"][0]["text"]

    def script(self, code, ctx="server"):
        r = self.call("execute_squirrel", {"script": code, "context": ctx, "capture_output": True})
        out = []
        for line in r["result"]["content"][0]["text"].splitlines():
            for tag in ("[SERVER SCRIPT] ", "[CLIENT SCRIPT] "):
                i = line.find(tag)
                if i >= 0:
                    out.append(line[i + len(tag):])
            if "SCRIPT ERROR" in line:
                out.append(line)
        return "\n".join(out)


def shoot(m, shots, prefix, dest):
    t0 = time.time()
    m.cmd("screenshot", False)
    for _ in range(20):
        time.sleep(1)
        new = [os.path.join(shots, f) for f in os.listdir(shots) if f.startswith(prefix) and os.path.getmtime(os.path.join(shots, f)) >= t0 - 1]
        if new:
            break
    if not new:
        return None
    time.sleep(1)
    p = max(new, key=os.path.getmtime)
    Image.open(p).convert("RGB").save(dest, quality=90)
    return dest


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    opt = {sys.argv[i]: sys.argv[i + 1] for i in range(len(sys.argv) - 1) if sys.argv[i] in ("--map-prefix", "--shots")}
    if len(a) < 2:
        raise SystemExit(__doc__)
    port, out = int(a[0]), a[1]
    respawn = "--respawn" in sys.argv
    prefix = opt.get("--map-prefix", "mp_glitch")
    shots = opt.get("--shots", r"F:\content3\r1\screenshots")
    os.makedirs(out, exist_ok=True)
    m = Mcp(port)
    m.cmd("mp_timelimit 0", False)
    m.cmd("mp_scorelimit 0", False)
    for team in TEAMS:
        for cls in CLASSES:
            if respawn:
                m.script(f'local p=GetPlayerArray()[0]; p.SetTeam({team}); p.playerClassData[level.pilotClass].playerSetFile="{cls}"; '
                         f'p.playerClassData[level.pilotClass].race <- ("{cls}".find("female")!=null ? RACE_HUMAN_FEMALE : RACE_HUMAN_MALE)')
                m.cmd("kill", False)
                time.sleep(8)
                for _ in range(10):
                    m.script('ClientCommand_RespawnPlayer(GetPlayerArray()[0],"Pilot")')
                    time.sleep(3)
                    if "true" in m.script('printt(IsAlive(GetPlayerArray()[0]))'):
                        break
                srv = m.script('printt(GetPlayerArray()[0].GetModelName())')
            else:
                m.script(f"GetPlayerArray()[0].SetTeam({team})")
                srv = ""
                for _ in range(6):
                    srv = m.script(f'local p=GetPlayerArray()[0]; SetPlayerSetFile(p,"{cls}"); printt(p.GetModelName())')
                    time.sleep(3)
                    cli = m.script("printt(GetLocalClientPlayer().GetModelName())", "client")
                    if cli.lower() == srv.lower():
                        break
            cli = m.script("printt(GetLocalClientPlayer().GetModelName())", "client")
            m.cmd("thirdperson", False)
            time.sleep(2)
            dest = shoot(m, shots, prefix, os.path.join(out, f"{team[5:]}_{cls}.jpg"))
            print(f"{team:13} {cls:16} server={srv.split('/')[-1]:30} client={cli.split('/')[-1]:30} same={srv.lower() == cli.lower()} shot={dest}")


if __name__ == "__main__":
    main()
