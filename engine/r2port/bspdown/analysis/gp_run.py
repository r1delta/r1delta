"""Rule 18 verification: for each map extract (corvpk), convert padded and --no-grid-pad, run check_bspdown.py with --unpadded,
print sizes and SHA-256, delete the extraction and the outputs. Logs: out/gp/<map>.report.txt, out/gp/<map>.check.txt."""
import hashlib, os, shutil, subprocess, sys
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORVPK = r"C:\NOTWINDOWS\Users\r3muxd\Downloads\corvpk.exe"
EXE = os.path.join(HERE, "bspdown.exe")
TMP = os.path.join(HERE, "work", "gp_tmp")
OUT = os.path.join(HERE, "out", "gp")
os.makedirs(OUT, exist_ok=True)
def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 22), b""):
            h.update(b)
    return h.hexdigest()
rows = []
for m in sys.argv[1:]:
    shutil.rmtree(TMP, ignore_errors=True); os.makedirs(TMP)
    subprocess.run([CORVPK, "unpack", r"D:\Titanfall2\vpk\englishclient_%s.bsp.pak000_dir.vpk" % m, TMP, "1"], capture_output=True)
    src = os.path.join(TMP, "maps", m + ".bsp")
    if not os.path.exists(src):
        print(m, "extract failed"); continue
    for fn in os.listdir(os.path.join(TMP, "maps")):  # keep only the BSP itself (it embeds all lumps); saves disk
        if fn != m + ".bsp":
            os.remove(os.path.join(TMP, "maps", fn))
    dst = os.path.join(TMP, m + "_r1.bsp"); dstu = os.path.join(TMP, m + "_r1_unpadded.bsp")
    r = subprocess.run([EXE, src, dst], capture_output=True, text=True)
    open(os.path.join(OUT, m + ".report.txt"), "w").write(r.stdout + r.stderr)
    ru = subprocess.run([EXE, src, dstu, "--grid-size", "0", "--no-grid-pad", "--quiet"], capture_output=True, text=True)
    unp_size = os.path.getsize(dstu) if ru.returncode == 0 else 0
    unp_sha = sha(dstu) if ru.returncode == 0 else ""
    if r.returncode or ru.returncode:
        print(m, "CONVERT FAILED", r.stderr[:300], ru.stderr[:300]); continue
    mini = os.path.join(TMP, m + "_grid_unpadded.bsp")  # header + lumps 0x55..0x5B and 0x0E of the unpadded output only
    d = open(dstu, "rb").read()
    import struct
    L = [struct.unpack_from("<IIII", d, 0x10 + 16 * i) for i in range(128)]
    keep = (0x0E, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B)
    body = bytearray(d[:0x10 + 128 * 16]); off = len(body)
    chunks = []
    for i in range(128):
        o, l, v, cc = L[i]
        if i in keep:
            struct.pack_into("<IIII", body, 0x10 + 16 * i, off, l, v, 0); chunks.append(d[o:o + l]); off += l
        else:
            struct.pack_into("<IIII", body, 0x10 + 16 * i, 0, 0, v, 0)
    open(mini, "wb").write(bytes(body) + b"".join(chunks))
    del d
    os.remove(dstu)
    c = subprocess.run([sys.executable, os.path.join(HERE, "check_bspdown.py"), src, dst, "--no-bsp-tool", "--reference", mini], capture_output=True, text=True)
    open(os.path.join(OUT, m + ".check.txt"), "w").write(c.stdout + c.stderr)
    summ = [l for l in c.stdout.splitlines() if l.startswith("SUMMARY")]
    fails = [l.strip() for l in c.stdout.splitlines() if "FAIL" in l]
    row = (m, os.path.getsize(src), unp_size, os.path.getsize(dst), unp_sha, sha(dst), summ[0].split(": ", 1)[1] if summ else "no summary", fails)
    rows.append(row)
    print(*row, flush=True)
    os.remove(dst)
    shutil.rmtree(TMP, ignore_errors=True)
with open(os.path.join(OUT, "summary.txt"), "a") as f:
    for r in rows:
        f.write(" | ".join(str(x) for x in r) + "\n")
