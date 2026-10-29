"""Unattended run harness for the movement/slide work (listen server on r2-lead, background window, MCP).
Own PIDs only. Never kills by name."""
import json, os, subprocess, sys, time, urllib.request, ctypes, psutil

ROOT = os.environ.get('R1D_MV_ROOT', r'S:\game\r2-combo')
BASE = r'C:\Users\r3muxd\tmp\r1delta-combined\.build'
CWD = BASE + r'\cwd'
RUNS = BASE + r'\runs'
MCP_PORT = int(os.environ.get('R1D_MV_MCP', '8861'))
GAME_PORT = int(os.environ.get('R1D_MV_GAME', '27140'))
CLIENT_PORT = GAME_PORT + 1
os.makedirs(CWD, exist_ok=True)
os.makedirs(RUNS, exist_ok=True)


class Mcp:
    def __init__(self, port):
        self.port = port; self.sess = None; self.id = 1

    def rpc(self, method, params=None, timeout=30):
        self.id += 1
        body = json.dumps({"jsonrpc": "2.0", "id": self.id, "method": method, "params": params or {}}).encode()
        h = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if self.sess: h["Mcp-Session-Id"] = self.sess
        r = urllib.request.urlopen(urllib.request.Request(f"http://127.0.0.1:{self.port}/", body, h), timeout=timeout)
        s = r.headers.get("Mcp-Session-Id")
        if s: self.sess = s
        return json.loads(r.read().decode() or "null")

    def init(self, tmo=240):
        t0 = time.time()
        while time.time() - t0 < tmo:
            try:
                self.sess = None
                self.rpc("initialize", {"protocolVersion": "2024-11-05", "capabilities": {}, "clientInfo": {"name": "mv", "version": "1"}}, timeout=5)
                return True
            except Exception:
                time.sleep(2)
        return False

    def call(self, name, args, timeout=60):
        return self.rpc("tools/call", {"name": name, "arguments": args}, timeout)

    def cmd(self, c):
        r = self.call("execute_console_command", {"command": c, "capture_output": False})
        return r["result"]["content"][0]["text"] if "result" in r else str(r)

    def sq(self, s, ctx="server"):
        r = self.call("execute_squirrel", {"script": s, "context": ctx, "capture_output": True})
        res = r.get("result", r)
        try:
            return res["content"][0]["text"]
        except Exception:
            return json.dumps(res)

    def state(self):
        try:
            return self.call("get_game_state", {})["result"]["structuredContent"]
        except Exception as e:
            return {"err": str(e)}

    def wait_map(self, mp, tmo=600):
        t0 = time.time(); s = None
        while time.time() - t0 < tmo:
            s = self.state()
            if s.get('in_game') and mp in s.get('current_map', ''):
                return s
            time.sleep(2)
        return s


mcp = Mcp(MCP_PORT)
PROC = [None]
OWN = set()


def commit_free_gb():
    class MS(ctypes.Structure):
        _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong), ("ullTotalPhys", ctypes.c_ulonglong), ("ullAvailPhys", ctypes.c_ulonglong), ("ullTotalPageFile", ctypes.c_ulonglong), ("ullAvailPageFile", ctypes.c_ulonglong), ("ullTotalVirtual", ctypes.c_ulonglong), ("ullAvailVirtual", ctypes.c_ulonglong), ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]
    m = MS(); m.dwLength = ctypes.sizeof(MS)
    ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m))
    return m.ullAvailPageFile / 2**30


def record():
    if PROC[0] is None: return
    try:
        for c in psutil.Process(PROC[0].pid).children(recursive=True): OWN.add(c.pid)
        OWN.add(PROC[0].pid)
    except Exception:
        pass

    # The launcher exits after spawning Titanfall.exe: own processes are the ones carrying my unique MCP port.
    for pr in psutil.process_iter(['pid', 'cmdline']):
        try:
            cl = pr.info['cmdline'] or []
            if '+mcp_start_http' in cl and str(MCP_PORT) in cl and cl[cl.index('+mcp_start_http') + 1] == str(MCP_PORT):
                OWN.add(pr.info['pid'])
        except Exception:
            pass

def kill():
    record()
    for pid in list(OWN):
        try: psutil.Process(pid).kill()
        except Exception: pass
    OWN.clear(); PROC[0] = None
    time.sleep(3)


def launch(mapname='mp_box', extra=None, env_extra=None, stdout=None):
    f = commit_free_gb()
    if f < 20: raise RuntimeError("free commit %.1f GB" % f)
    free_c = psutil.disk_usage('C:\\').free / 2**30
    if free_c < 4: raise RuntimeError("C: free %.1f GB" % free_c)
    env = dict(os.environ)
    env['R1DELTA_BACKGROUND'] = '1'; env['FROM_DELTA_LAUNCHER'] = '1'
    core = ROOT + r'\r1delta'
    env['PATH'] = f"{core}\\bin_delta;{core}\\bin;{CWD}\\bin\\x64_retail;{CWD}\\r1\\bin\\x64_retail;" + env['PATH']
    if env_extra: env.update(env_extra)
    a = [ROOT + r'\r1delta.exe', '-allowmultiple', '-multiple', '-nomessagebox', '-dev', '-novid', '-windowed', '-noborder', '-condebug',
         '-port', str(GAME_PORT), '+hostport', str(GAME_PORT), '+clientport', str(CLIENT_PORT), '+launchplaylist', 'ffa', '+map', mapname,
         '-usemcp', '+mcp_start_http', str(MCP_PORT), '+sv_cheats', '1', '+mp_enablematchending', '0', '+mp_enabletimelimit', '0',
         '-w', '1280', '-h', '720', '+fps_max', '60', '+cl_localnetworkbackdoor', '0']
    if extra: a += extra
    mcp.sess = None
    fl = 0x00000008 | 0x00000200  # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP: survive a kernel restart
    try:
        PROC[0] = subprocess.Popen(a, cwd=CWD, env=env, stdout=stdout, stderr=subprocess.STDOUT if stdout else None, creationflags=fl | 0x01000000)
    except OSError:
        PROC[0] = subprocess.Popen(a, cwd=CWD, env=env, stdout=stdout, stderr=subprocess.STDOUT if stdout else None, creationflags=fl)
    return PROC[0]


def console_log():
    p = ROOT + r'\r1delta\console.log'
    try:
        return open(p, 'r', encoding='utf-8', errors='replace').read()
    except Exception:
        return ''


CDB = r'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'


def launch_cdb(mapname='mp_box', log=None, extra=None, cmds=None, env_extra=None, children=True):
    """Titanfall.exe directly under cdb (the r1delta.exe launcher exits after spawning). Debug output goes to log."""
    log = log or (RUNS + r'\cdb-%d.log' % int(time.time()))
    env = dict(os.environ)
    env['R1DELTA_BACKGROUND'] = '1'; env['FROM_DELTA_LAUNCHER'] = '1'
    core = ROOT + r'\r1delta'
    env['PATH'] = f"{core}\\bin_delta;{core}\\bin;{CWD}\\bin\\x64_retail;{CWD}\\r1\\bin\\x64_retail;" + env['PATH']
    if env_extra: env.update(env_extra)
    a = ['-allowmultiple', '-multiple', '-nomessagebox', '-dev', '-novid', '-windowed', '-noborder', '-condebug',
         '-port', str(GAME_PORT), '+hostport', str(GAME_PORT), '+clientport', str(CLIENT_PORT), '+launchplaylist', 'ffa', '+map', mapname,
         '-usemcp', '+mcp_start_http', str(MCP_PORT), '+sv_cheats', '1', '+mp_enablematchending', '0', '+mp_enabletimelimit', '0',
         '-w', '1280', '-h', '720', '+fps_max', '60', '+cl_localnetworkbackdoor', '0']
    if extra: a += extra
    cf = RUNS + r'\cdb-init.txt'
    open(cf, 'w').write('sxd -c2 ".echo MV_SECOND_CHANCE; .lastevent; kv 40; q" av\n' + (cmds or '') + 'g\n')
    mcp.sess = None
    PROC[0] = subprocess.Popen([CDB, '-G'] + (['-o'] if children else []) + ['-logo', log, '-cf', cf, ROOT + r'\r1delta.exe'] + a, cwd=CWD, env=env,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
    return PROC[0], log


def attach_cdb(cmds, mapname='mp_box', log=None, extra=None, env_extra=None):
    """Launch normally through r1delta.exe, then attach cdb to my Titanfall.exe (found by my unique MCP port)."""
    log = log or (RUNS + r'\cdbattach-%d.log' % int(time.time()))
    launch(mapname, extra=extra, env_extra=env_extra)
    pid = None
    for _ in range(300):
        time.sleep(0.1)
        record()
        for q in list(OWN):
            try:
                if psutil.Process(q).name().lower() == 'titanfall.exe': pid = q
            except Exception: pass
        if pid: break
    if not pid: raise RuntimeError('no Titanfall.exe appeared')
    cf = RUNS + r'\cdb-init.txt'
    open(cf, 'w').write('sxd -c2 ".echo MV_SECOND_CHANCE; .lastevent; kv 40; q" av\n' + cmds + 'g\n')
    c = subprocess.Popen([CDB, '-G', '-p', str(pid), '-logo', log, '-cf', cf], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
    OWN.add(c.pid)
    return pid, c, log
