"""Tangent frame check helpers (used by check_bspdown.py)."""
import numpy as np

def uv_handedness(bsp, mesh_dt_fl_shift=9, limit=None):
    """Return arrays (n, s, t, hand_uv) for lit-bump vertices that belong to non-degenerate triangles of world meshes."""
    mdt = np.dtype([("fmi", "<u4"), ("nt", "<u2"), ("fv", "<u2"), ("nv", "<u2"), ("vt", "<u2"), ("st", "u1", 4), ("lo", "<i2", 2), ("lom", "u1", 2), ("ms", "<u2"), ("fl", "<u4")])
    sdt = np.dtype([("td", "<i2"), ("lm", "<i2"), ("cm", "<i2"), ("lv", "<u2"), ("vo", "<i4")])
    m = bsp.arr(0x50, mdt); so = bsp.arr(0x52, sdt); idx = bsp.arr(0x4F, "<u2")
    pos = bsp.arr(3, "<f4").reshape(-1, 3); nrm = bsp.arr(0x1E, "<f4").reshape(-1, 3)
    v = np.frombuffer(bsp.raw(0x49).tobytes(), dtype=np.uint32).reshape(-1, 11)
    out = []
    count = 0
    for i in np.nonzero(((m["fl"] >> 9) & 3) == 1)[0]:
        nt = int(m["nt"][i])
        if not nt: continue
        ii = idx[int(m["fmi"][i]):int(m["fmi"][i]) + 3 * nt].astype(np.int64).reshape(-1, 3) + int(so["vo"][m["ms"][i]])
        # skip padding triangle (degenerate repeats) automatically via area test
        r = v[ii]  # (nt,3,11)
        p = pos[r[:, :, 0]].astype(np.float64)
        uv = r[:, :, 2:4].view("<f4").astype(np.float64)
        e1 = p[:, 1] - p[:, 0]; e2 = p[:, 2] - p[:, 0]
        d1 = uv[:, 1] - uv[:, 0]; d2 = uv[:, 2] - uv[:, 0]
        det = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
        gn = np.cross(e1, e2)
        ok = (np.linalg.norm(gn, axis=1) > 1e-3) & (np.abs(det) > 1e-9)
        for k in range(3):
            n = nrm[r[ok, k, 1]].astype(np.float64)
            h = np.sign((gn[ok] * n).sum(1)) * np.sign(det[ok])
            out.append((n, r[ok, k, :], h))
        count += int(ok.sum()) * 3
        if limit and count >= limit: break
    return out, nrm

def frame_stats(bsp, converted, limit=200000):
    out, nrm = uv_handedness(bsp, limit=limit)
    agree = total = 0; maxdn = maxdt = 0.0; unit_bad = 0
    for n, r, h in out:
        s = nrm[r[:, 9] & 0x7FFFFFFF].astype(np.float64) if not converted else nrm[r[:, 9]].astype(np.float64)
        t = nrm[r[:, 10]].astype(np.float64) if converted else None
        if converted:
            rule = np.sign((np.cross(n, s) * t).sum(1))
            maxdt = max(maxdt, float(np.abs((n * t).sum(1)).max()))
            unit_bad += int((np.abs(np.linalg.norm(t, axis=1) - 1) > 1e-3).sum())
        else:
            rule = np.where((r[:, 9] >> 31) == 1, -1.0, 1.0)
        maxdn = max(maxdn, float(np.abs((n * s).sum(1)).max()))
        agree += int((rule == h).sum()); total += len(h)
    return agree, total, maxdn, maxdt, unit_bad
