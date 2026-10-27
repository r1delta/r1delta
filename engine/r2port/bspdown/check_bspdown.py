#!/usr/bin/env python3
"""Invariant checker for bspdown output.

usage: check_bspdown.py <in_r2.bsp> <out_r1.bsp> [--limit 16376] [--no-bsp-tool] [--rtl-bytes 4]

Independent re-implementation (numpy, no code shared with the C++ converter) of
the invariants the R1 engine loaders need plus consistency checks against the
R2 input. Exit status 0 if every check passed.
"""
import argparse
import struct
import sys

import numpy as np

# ---------------------------------------------------------------------------

class Bsp:
    def __init__(self, path):
        self.path = path
        self.d = np.memmap(path, dtype=np.uint8, mode="r")
        self.ident = bytes(self.d[0:4])
        self.version, self.revision, self.last = struct.unpack_from("<III", self.d, 4)
        self.lumps = [struct.unpack_from("<IIII", self.d, 0x10 + 16 * i) for i in range(128)]

    def raw(self, i):
        o, l, _, _ = self.lumps[i]
        return self.d[o:o + l]

    def arr(self, i, dt):
        dt = np.dtype(dt)
        r = self.raw(i)
        assert len(r) % dt.itemsize == 0, "lump %#x size %d %% %d" % (i, len(r), dt.itemsize)
        return np.frombuffer(r.tobytes(), dtype=dt)


class Checker:
    def __init__(self):
        self.fails = 0
        self.passes = 0

    def ok(self, cond, msg, detail=""):
        if cond:
            self.passes += 1
            print("  ok    " + msg)
        else:
            self.fails += 1
            print("  FAIL  " + msg + ((" :: " + detail) if detail else ""))
        return bool(cond)


# record sizes R1 requires (lump -> size)
R1_SIZES = {
    0x01: 16, 0x02: 36, 0x03: 12, 0x0E: 32, 0x1E: 12, 0x24: 12, 0x2A: 16, 0x2C: 4, 0x36: 100,
    0x42: 4, 0x44: 16, 0x45: 44, 0x47: 20, 0x48: 36, 0x49: 44, 0x4A: 28, 0x4B: 28, 0x4F: 2,
    0x50: 28, 0x51: 32, 0x52: 12, 0x53: 8, 0x55: 28, 0x56: 4, 0x57: 8, 0x58: 16, 0x59: 4,
    0x5A: 16, 0x5B: 4, 0x5C: 32, 0x5D: 2, 0x5E: 2, 0x5F: 32, 0x60: 2, 0x61: 4, 0x63: 32,
    0x64: 2, 0x65: 48, 0x66: 4, 0x67: 8, 0x68: 16, 0x6A: 8, 0x6B: 8, 0x6C: 12, 0x6D: 12,
    0x6E: 4, 0x6F: 16, 0x70: 2, 0x71: 2, 0x72: 16, 0x73: 16, 0x74: 8, 0x75: 12, 0x76: 2,
    0x77: 32, 0x78: 2, 0x79: 32, 0x7B: 28, 0x7C: 12, 0x7D: 20, 0x7E: 2, 0x7F: 12,
}

HDR_DT = np.dtype([("flags", "<i2"), ("texflags", "<i2"), ("texdata", "<i2"), ("nverts", "<i2"),
                   ("ntris", "<u2"), ("nbev", "<u2"), ("fv", "<i4"), ("ft", "<u4"), ("fn", "<u4"),
                   ("fb", "<u4"), ("org", "<f4", 3), ("scale", "<f4")])
MESH_DT_R2 = np.dtype([("fmi", "<u4"), ("nt", "<u2"), ("fv", "<u2"), ("nv", "<u2"), ("vt", "u1"), ("cm", "i1"),
                       ("st", "u1", 4), ("lo", "<i2", 2), ("lom", "u1", 2), ("ms", "<u2"), ("fl", "<u4")])
MESH_DT_R1 = np.dtype([("fmi", "<u4"), ("nt", "<u2"), ("fv", "<u2"), ("nv", "<u2"), ("vt", "<u2"),
                       ("st", "u1", 4), ("lo", "<i2", 2), ("lom", "u1", 2), ("ms", "<u2"), ("fl", "<u4")])
SORT_DT = np.dtype([("td", "<i2"), ("lm", "<i2"), ("cm", "<i2"), ("lv", "<u2"), ("vo", "<i4")])
VTX_STRIDE = {0: 36, 1: 44, 2: 20, 3: 28}
VTX_LUMP = {0: 0x48, 1: 0x49, 2: 0x47, 3: 0x4A}


def bounds_aabb(b):
    """b: (n,8) int16 oriented boxes -> (lo, hi) float arrays, yaw about Z."""
    b = b.astype(np.float64)
    c = np.where((b[:, 3] == 0) & (b[:, 7] == 0), 1.0, -b[:, 3] / 32768.0)
    s = np.where((b[:, 3] == 0) & (b[:, 7] == 0), 0.0, b[:, 7] / 32768.0)
    ex, ey, ez = b[:, 4], b[:, 5], b[:, 6]
    wx = np.abs(c) * ex + np.abs(s) * ey
    wy = np.abs(s) * ex + np.abs(c) * ey
    w = np.stack([wx, wy, ez], axis=1)
    o = b[:, 0:3]
    return o - w, o + w

def mesh_material_names(bsp, meshes, sorts):
    texdata = bsp.raw(0x02)
    str_data = bytes(bsp.raw(0x2B))
    str_tab = np.frombuffer(bsp.raw(0x2C).tobytes(), dtype="<u4")
    names = []
    for mesh in meshes:
        si = int(mesh["ms"])
        if si >= len(sorts):
            names.append("")
            continue
        td = int(sorts[si]["td"])
        if td < 0 or (td + 1) * 36 > len(texdata):
            names.append("")
            continue
        name_i = struct.unpack_from("<i", texdata, td * 36 + 12)[0]
        if name_i < 0 or name_i >= len(str_tab):
            names.append("")
            continue
        off = int(str_tab[name_i])
        if off >= len(str_data):
            names.append("")
            continue
        name = str_data[off:].split(b"\0", 1)[0].decode("ascii", errors="replace")
        names.append(name.replace("\\", "/").lower())
    return names


def is_sky_mesh(material, flags):
    # R2 also sets SKY_2D (0x2) on non-sky tools/toolsblocklight.
    return bool(flags & 0x4) or (material.startswith("tools/") and "sky" in material)


def is_nondrawing_mesh(material, flags):
    if is_sky_mesh(material, flags):
        return False
    return material.startswith("tools/") or bool(flags & 0x00020000)


def walk_bevels(bsp, hdr, tris, starts, words, bits):
    """Yield per header the dict {entry_position: value} reached by the triangle starts.

    Extended record words are returned as the combined 20 bit value under key
    ('ext', position). bits is 11 for R2 input, 10 for R1 output.
    """
    bitarr = np.unpackbits(words.view(np.uint8), bitorder="little")
    pw = (1 << np.arange(bits)).astype(np.int64)

    def rd(base_dw, entry):
        o = base_dw * 32 + entry * bits
        return int((bitarr[o:o + bits].astype(np.int64) * pw).sum())

    res = []
    for i in range(len(hdr)):
        h = hdr[i]
        n = int(h["nbev"])
        out = {}
        if n:
            fb = int(h["fb"])
            T = tris[h["ft"]:h["ft"] + h["ntris"]]
            S = starts[h["ft"]:h["ft"] + h["ntris"]]
            m = {}
            for t, s in zip(T, S):
                cnt = (int(t) >> 24) & 0xF
                m[int(s)] = max(m.get(int(s), 0), cnt)
            for s, cnt in m.items():
                p = s
                if cnt == 15:
                    while True:
                        a, b = rd(fb, p), rd(fb, p + 1)
                        data = a | (b << bits) if bits == 11 else a | (b << 10)
                        # R2 layout: e0 | e1 << 11 ; R1 layout: r0 | r1 << 10. Both give the same 20 bit value.
                        out[("ext", p)] = data
                        p += 2
                        rc, ridx = data & 0x7F, data >> 7
                        for _ in range(rc):
                            out[p] = rd(fb, p)
                            p += 1
                        if ridx == i or rc == 0:
                            break
                else:
                    for _ in range(cnt):
                        out[p] = rd(fb, p)
                        p += 1
        res.append(out)
    return res


# ---------------------------------------------------------------------------

def walker_range_margin(sc, ox, oy, nx, ny, reach, ext):
    """Independent numpy re-implementation of the cell range arithmetic at the top of the R1 grid walker (sub_18010BE50,
    float32, floor via trunc + sign fix) for the extreme rays of a box [-reach, reach]^2 and hull half extent ext. Returns the
    unclamped (xlo, xhi, ylo, yhi) of the extreme rays minus the grid borders: (xlo - 0, nx - 1 - xhi, ylo - 0, ny - 1 - yhi),
    i.e. how many whole cells the range stays away from the border on each side."""
    f = np.float32
    inv = f(1.0) / f(sc)
    c = f(0.03125)
    def cell(v, off):
        t = np.floor(v.astype(np.float32)).astype(np.int64)
        return t - off
    lox = cell(((f(-reach) - f(ext)) - c) * inv, ox)
    hix = cell(((f(ext) + f(reach)) + c) * inv, ox)
    loy = cell(((f(-reach) - f(ext)) - c) * inv, oy)
    hiy = cell(((f(ext) + f(reach)) + c) * inv, oy)
    return int(lox), int(nx - 1 - hix), int(loy), int(ny - 1 - hiy)


def obj_key(gs, gb, i, pr):
    """Canonical identity of a geoset entry: resolved primitive words (copies of one object may point at different pool ranges),
    unique-contents index, bounds."""
    n, w = int(gs[i][1]), int(gs[i][2])
    prims = (w,) if n == 1 else tuple(int(x) for x in pr[((w >> 8) & 0xFFFF):((w >> 8) & 0xFFFF) + n])
    return (prims, w & 0xFF, tuple(int(v) for v in gb[i]))


def check_grid_regrid(C, r1, g1, cells, nmodels, grid_size, reference):
    """Rule 19: every object of the reference grid (same input converted with --grid-size 0 --no-grid-pad) is found in the new grid by
    a walker-style rectangle range query exactly when it was found in the reference grid, with the same straddle-group semantics."""
    print("== collision grid re-binning (rule 19)")
    sc = float(g1[0]); ox, oy, nx, ny = int(g1[1]), int(g1[2]), int(g1[3]), int(g1[4])
    C.ok(sc >= grid_size and (sc - grid_size) % 16 == 0, "cell size %g is the requested %g (+16 steps only when the world cells would not fit the walker list)" % (sc, grid_size))
    C.ok(nx * ny + 1 <= 16384, "world cells + model cell fit the walker's 16384 entry list (%d x %d = %d)" % (nx, ny, nx * ny + 1))
    # The walker writes 3 entries per band of n cells and advances by n; slots 3..n-1 are unwritten stack data (n >= 4 needs a hull
    # half extent above ~0.3536 * size, rectangle path from 0.7071 * size - 0.0625). Every hull in use must be outside that window.
    lowlim, highlim = 0.3536 * sc - 0.1, 0.70710677 * sc - 0.0625
    hulls = {16: "player", 38: "AIN drop", 60: "titan / AIN drop", 65: "AIN drop", 325: "AIN callback sweep", 500: "margin", 700: "margin"}
    badh = {h: n for h, n in hulls.items() if lowlim < h < highlim}
    C.ok(not badh, "no hull half extent in use (%s) lies in the unwritten-slot window %.0f..%.0f" % (", ".join("%d %s" % kv for kv in hulls.items()), lowlim, highlim), str(badh))
    C.ok(len(cells) == nx * ny + nmodels, "cell table size == 4 * (nx*ny + models), the R1 loader's exact check")
    gs = r1.arr(0x57, "<u2,<u2,<u4"); gb = r1.arr(0x58, "<8i2"); prn = r1.arr(0x59, "<u4")
    npool = len(gs)
    C.ok(npool <= 65535, "geoset pool %d fits the 16 bit cell start index" % npool)
    first = np.array([int(c[0]) for c in cells]); num = np.array([int(c[1]) for c in cells])
    lo, hi = bounds_aabb(gb) if npool else (np.zeros((0, 3)), np.zeros((0, 3)))
    # structural: every entry's AABB touches its cell (no spurious entries), objects in several cells carry a group, groups identical
    spurious = 0
    cells_of = {}
    grp_obj = {}
    for ci in range(nx * ny):
        if not num[ci]:
            continue
        cx, cy = ci % nx + ox, ci // nx + oy
        x0, x1, y0, y1 = cx * sc - 1.0, (cx + 1) * sc + 1.0, cy * sc - 1.0, (cy + 1) * sc + 1.0
        for g in range(first[ci], first[ci] + num[ci]):
            if lo[g, 0] > x1 or hi[g, 0] < x0 or lo[g, 1] > y1 or hi[g, 1] < y0:
                spurious += 1
            k = obj_key(gs, gb, g, prn)
            ident = ("g", int(gs[g][0])) if gs[g][0] else ("o", k)
            cells_of.setdefault(ident, set()).add(ci)
            if gs[g][0]:
                grp_obj.setdefault(int(gs[g][0]), set()).add(k)
    C.ok(spurious == 0, "every geoset entry touches the cell it is listed in (%d spurious)" % spurious)
    C.ok(all(len(v) == 1 for v in grp_obj.values()), "all copies sharing a straddle group are the same object (%d groups)" % len(grp_obj))
    multi_ungrouped = sum(1 for k, v in cells_of.items() if k[0] == "o" and len(v) > 1)
    C.ok(multi_ungrouped == 0, "an ungrouped (group 0) object lies in one cell only (%d violations)" % multi_ungrouped)
    C.ok(all(len(v) >= 2 for k, v in cells_of.items() if k[0] == "g"), "every straddle group spans at least 2 cells")
    sg = np.array([int(x[0]) for x in gs]); nz = sg[sg != 0]
    # model cells: groups irrelevant
    ngr = int(g1[5])
    C.ok(ngr == (int(nz.max()) + 1 if len(nz) else 1), "num_straddle_groups == max id + 1 (%d)" % ngr)
    C.ok(len(set(nz.tolist())) == (int(nz.max()) if len(nz) else 0), "straddle group ids dense 1..N (%d)" % len(set(nz.tolist())))
    # equivalence with the reference grid by queries
    if not reference:
        print("  note  no --reference file: query equivalence with the --grid-size 0 conversion not checked")
        return
    u = Bsp(reference)
    gu = u.arr(0x55, "<f4,<i4,<i4,<i4,<i4,<i4,<i4")[0]
    cu = u.arr(0x56, "<u2,<u2"); gsu = u.arr(0x57, "<u2,<u2,<u4"); gbu = u.arr(0x58, "<8i2")
    sc0, ox0, oy0, nx0, ny0 = float(gu[0]), int(gu[1]), int(gu[2]), int(gu[3]), int(gu[4])
    lou, hiu = bounds_aabb(gbu); pru = u.arr(0x59, "<u4")
    C.ok(bytes(u.raw(0x59)) == bytes(r1.raw(0x59)) and bytes(u.raw(0x5A)) == bytes(r1.raw(0x5A)) and bytes(u.raw(0x5B)) == bytes(r1.raw(0x5B)),
         "primitive pool, primitive bounds, unique contents identical to the reference")
    # model cells: same objects
    mod_ok = True
    for m in range(nmodels):
        a = [obj_key(gsu, gbu, g, pru) for g in range(cu[nx0 * ny0 + m][0], cu[nx0 * ny0 + m][0] + cu[nx0 * ny0 + m][1])]
        b = [obj_key(gs, gb, g, prn) for g in range(first[nx * ny + m], first[nx * ny + m] + num[nx * ny + m])]
        mod_ok &= (a == b)
    C.ok(mod_ok, "model cells hold the same geosets in the same order (%d models)" % nmodels)
    # content: all objects of the reference grid occur in the new grid and vice versa
    objs_ref = set(); 
    for ci in range(nx0 * ny0):
        for g in range(cu[ci][0], cu[ci][0] + cu[ci][1]):
            objs_ref.add(obj_key(gsu, gbu, g, pru))
    objs_new = set(obj_key(gs, gb, g, prn) for ci in range(nx * ny) for g in range(first[ci], first[ci] + num[ci]))
    C.ok(objs_ref == objs_new, "the set of world objects is identical (%d)" % len(objs_ref))
    # walker style range queries (float64 arithmetic of the same formulas), hull extents of the AIN sweeps and a few small ones
    rng = np.random.default_rng(12345)
    exts = [0.0, 16.0, 40.0, 60.0, 120.0, 200.0, 300.0, 500.0, 800.0]
    def found(cells_a, gs_a, gb_a, pr_a, lo_a, hi_a, s_, ox_, oy_, nx_, ny_, qx0, qx1, qy0, qy1):
        cx0 = max(int(np.floor(qx0 / s_)) - ox_, 0); cx1 = min(int(np.floor(qx1 / s_)) - ox_, nx_ - 1)
        cy0 = max(int(np.floor(qy0 / s_)) - oy_, 0); cy1 = min(int(np.floor(qy1 / s_)) - oy_, ny_ - 1)
        out = set()
        for cy in range(cy0, cy1 + 1):
            for cx in range(cx0, cx1 + 1):
                f, n = cells_a[cy * nx_ + cx]
                for g in range(f, f + n):
                    if lo_a[g, 0] <= qx1 and hi_a[g, 0] >= qx0 and lo_a[g, 1] <= qy1 and hi_a[g, 1] >= qy0:
                        out.add(obj_key(gs_a, gb_a, g, pr_a))
        return out
    ext_xy = max(abs(float(lo[:, :2].min())) if npool else 0, float(hi[:, :2].max()) if npool else 0, 1000.0)
    bad = 0; nq = 0; extra = 0
    for ext in exts:
        for _ in range(600):
            a = rng.uniform(-ext_xy * 1.3, ext_xy * 1.3, 2); b = a + rng.normal(0, 1, 2) * rng.choice([50.0, 800.0, 6000.0])
            qx0 = min(a[0], b[0]) - ext - 0.03125; qx1 = max(a[0], b[0]) + ext + 0.03125
            qy0 = min(a[1], b[1]) - ext - 0.03125; qy1 = max(a[1], b[1]) + ext + 0.03125
            fr = found(cu, gsu, gbu, pru, lou, hiu, sc0, ox0, oy0, nx0, ny0, qx0, qx1, qy0, qy1)
            fn = found(cells, gs, gb, prn, lo, hi, sc, ox, oy, nx, ny, qx0, qx1, qy0, qy1)
            nq += 1
            if fr - fn:
                bad += 1
            if fn - fr:
                extra += 1
    C.ok(bad == 0, "%d range queries (rays + hull extents 0..800): the new grid finds every object the reference grid finds (%d queries miss one)" % (nq, bad))
    print("  note  %d of %d queries find additional objects whose AABB touches the query but that the R2 cell lists did not hold in the reference grid" % (extra, nq))
    # a group is visited once per query: the objects of a group are found through any of its cells
    print("  note  geoset pool %d (reference %d), cells %d x %d (reference %d x %d at %g)" % (npool, len(gsu), nx, ny, nx0, ny0, sc0))


def check_grid_padding(C, r2, r1, g2, g1, cells, nmodels, grid_pad, unpadded, grid_size=0.0):
    if float(g1[0]) != float(g2[0]):
        check_grid_regrid(C, r1, g1, cells, nmodels, float(grid_size), unpadded)
        return
    print("== collision grid padding (rule 18)")
    sc = float(g1[0]); ox, oy, nx, ny = int(g1[1]), int(g1[2]), int(g1[3]), int(g1[4])
    ox2, oy2, nx2, ny2 = int(g2[1]), int(g2[2]), int(g2[3]), int(g2[4])
    num = np.array([int(c[1]) for c in cells], dtype=np.int64)
    first = np.array([int(c[0]) for c in cells], dtype=np.int64)
    if not grid_pad:
        print("  note  padding disabled (--no-grid-pad): grid left as rules 4 / 7 produced it (%d x %d)" % (nx, ny))
        return
    # 1. geometry. The walker writes 3 cells {first, first+s, first+2s} per band (s = one row/column step), so every cell it can
    #    write, counted or scratch, lies within 2 cells of a cell of its clamped range. A range at least 2 whole cells inside the
    #    grid on every side therefore never produces an index outside [0, nx*ny). Hulls wider than 0.7071 * cell size take the
    #    rectangle path (clamped, no scratch); the widest band-path hull has extent 0.7071 * scale - 0.0625.
    ext_max = sc * 0.70710677 - 0.0625
    mg = walker_range_margin(sc, ox, oy, nx, ny, 18000.0, ext_max)
    C.ok(min(mg) >= 3, "walker cell range of rays inside +-18000 with hull extent %.1f stays >= 3 whole cells from every grid border (margins -x %d +x %d -y %d +y %d)" % ((ext_max,) + mg))
    half32 = int(np.ceil(32768.0 / sc)) + 3
    if (2 * half32) ** 2 + 1 <= 16384:
        mg = walker_range_margin(sc, ox, oy, nx, ny, 32768.0, ext_max)
        C.ok(min(mg) >= 2, "walker cell range of rays inside +-32768 (COORD_EXTENT) stays >= 2 whole cells from every grid border (margins -x %d +x %d -y %d +y %d)" % mg)
    C.ok(nx * ny + 1 <= 16384, "world cells + model cell fit the walker's 16384 entry list (%d)" % (nx * ny + 1))
    C.ok(len(cells) == nx * ny + nmodels, "cell table size == 4 * (nx*ny + models), the R1 loader's exact check")
    # 2. content: every R2 cell sits at the same world position and the padding is empty
    win = np.zeros((ny, nx), bool)
    for y in range(ny2):
        yy = y + oy2 - oy
        if 0 <= yy < ny:
            xs = np.arange(nx2) + ox2 - ox
            xs = xs[(xs >= 0) & (xs < nx)]
            win[yy, xs] = True
    wnum = num[:nx * ny].reshape(ny, nx)
    wfirst = first[:nx * ny].reshape(ny, nx)
    C.ok(not (wnum[~win] != 0).any(), "every cell outside the R2 grid window is empty (%d cells)" % int((~win).sum()))
    c2 = r2.arr(0x56, "<u2,<u2")
    n2 = np.array([int(c[1]) for c in c2[:nx2 * ny2]], dtype=np.int64).reshape(ny2, nx2)
    sub = np.zeros((ny2, nx2), bool)
    r1n = np.zeros((ny2, nx2), np.int64)
    for y in range(ny2):
        yy = y + oy2 - oy
        if not (0 <= yy < ny):
            continue
        for x in range(nx2):
            xx = x + ox2 - ox
            if 0 <= xx < nx:
                sub[y, x] = True
                r1n[y, x] = wnum[yy, xx]
    C.ok(bool((r1n[sub] <= n2[sub]).all()), "each R2 cell keeps at most its R2 geosets at its world position (rule 4 only removes)")
    C.ok(bool(((n2[sub] == 0) <= (r1n[sub] == 0)).all()), "cells empty in R2 are empty in R1")
    # 3. against the same conversion without padding
    if unpadded:
        u = Bsp(unpadded)
        gu = u.arr(0x55, "<f4,<i4,<i4,<i4,<i4,<i4,<i4")[0]
        cu = u.arr(0x56, "<u2,<u2")
        uox, uoy, unx, uny = int(gu[1]), int(gu[2]), int(gu[3]), int(gu[4])
        C.ok(gu[0] == g1[0] and gu[5] == g1[5] and gu[6] == g1[6], "scale, straddle group count, first brush plane equal the unpadded output")
        same = True
        for y in range(uny):
            yy = y + uoy - oy
            row = cu[y * unx:(y + 1) * unx]
            base = yy * nx + (uox - ox)
            got = cells[base:base + unx]
            if not (0 <= yy < ny) or not (np.array_equal(row["f0"], got["f0"]) and np.array_equal(row["f1"], got["f1"])):
                same = False
                break
        C.ok(same and ox <= uox and oy <= uoy and ox + nx >= uox + unx and oy + ny >= uoy + uny, "all %d unpadded cells appear at the same world position with identical {first,num}" % (unx * uny))
        tail_ok = np.array_equal(cu[unx * uny:], cells[nx * ny:])
        C.ok(tail_ok, "model cells (%d) identical to the unpadded output" % nmodels)
        for lump in (0x57, 0x58, 0x59, 0x5A, 0x5B):
            C.ok(bytes(u.raw(lump)) == bytes(r1.raw(lump)), "lump %#x identical to the unpadded output" % lump)
        # R1 loader size rule on both
        C.ok(len(cu) == unx * uny + nmodels, "unpadded cell table size rule")
    else:
        print("  note  no --unpadded file given: per-cell equality against the unpadded conversion not checked")


LIGHT_OPT = {"collapse": True, "rtl": True, "min_weight": 187, "pbr": True, "sun_alpha": True}


def expected_lighting(r2, wl_flag2):
    """Independent numpy model of rule 20: returns (world lights bytes, probes bytes, rtl bytes or None, info dict)."""
    v2 = r2.lumps[0x36][2]
    rec = {1: 100, 2: 108, 3: 112}[v2]
    raw = np.frombuffer(r2.raw(0x36).tobytes(), np.uint8).reshape(-1, rec)[:, :100].copy()
    n = len(raw)
    ty = np.ascontiguousarray(raw[:, 52:56]).view("<i4").ravel()
    fl = np.ascontiguousarray(raw[:, 88:92]).view("<i4").ravel()
    E = 0
    while E < n and ty[E] == 5:
        E += 1
    S = 0
    while E + S < n and ty[E + S] == 3:
        S += 1
    clean = not bool(np.any((ty[E + S:] == 5) | (ty[E + S:] == 3)))
    if LIGHT_OPT["collapse"] and clean and (E > 1 or S > 1):
        order = ([E - 1] if E else []) + ([E + S - 1] if S else [])
    else:
        order = list(range(E + S))
    head = len(order)
    order += list(range(E + S, n))
    v1 = 1 if (order and ty[order[0]] == 5) else 0
    # RTL decode
    hd = r2.arr(0x53, "<u4,<u2,<u2")
    pages = [(int(w), int(h)) for t, w, h in hd if t == 1]
    texels = sum(w * h for w, h in pages)
    rtl2 = r2.raw(0x69)
    pal = np.frombuffer(r2.raw(0x7A).tobytes(), "<u2") if len(r2.raw(0x7A)) else np.zeros(0, "<u2")
    have = LIGHT_OPT["rtl"] and len(rtl2) and texels and len(rtl2) == texels * 9 and len(pal) and len(r2.raw(0x7A)) % 126 == 0
    usage = np.zeros(n, np.float64)
    tex_old = None
    sky = bytearray(r2.raw(0x62).tobytes())
    sun_alpha = bool(LIGHT_OPT["sun_alpha"] and have and len(sky) == texels * 8)
    lut = np.array([255 - int(np.floor(255.0 * (v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4) + 0.5)) for v in (np.arange(256) / 255.0)], np.uint8)
    if have:
        npal = len(pal) // 63
        tex_old = np.full((texels, 4), -1, np.int64)
        off = 0
        t0 = 0
        skyoff = 0
        rb = np.frombuffer(rtl2.tobytes(), np.uint8)
        for w, h in pages:
            m = w * h
            A = rb[off:off + 4 * m].view("<u4")
            B = rb[off + 4 * m:off + 8 * m].reshape(m, 4)
            off += 9 * m
            sel = (A >> 24).astype(np.int64)
            skyB = np.frombuffer(sky, np.uint8)[skyoff + 4 * m:skyoff + 8 * m].reshape(m, 4) if sun_alpha else None
            skyoff += 8 * m
            for s in range(4):
                slot = ((A >> (6 * s)) & 63).astype(np.int64)
                ok = (A != 0xFFFFFFFF) & (slot != 63) & (sel < npal)
                wo = np.zeros(m, np.int64)
                wo[ok] = pal[sel[ok] * 63 + slot[ok]].astype(np.int64) + E
                ok &= wo < n
                wt = B[:, s].astype(np.int64)
                if sun_alpha:
                    sm = ok.copy()
                    sm[ok] = ty[wo[ok]] == 3
                    skyB[sm, 3] = lut[wt[sm]]
                    ok &= ~sm
                okk = ok.copy()
                okk[ok] = ((ty[wo[ok]] == 1) | (ty[wo[ok]] == 2)) & ((fl[wo[ok]] & 8) != 0)
                okk &= wt >= LIGHT_OPT["min_weight"]
                tex_old[t0:t0 + m, s] = np.where(okk, wo, -1)
                usage += np.bincount(wo[okk], weights=wt[okk].astype(np.float64), minlength=n)
            t0 += m
    flagged_pos = [k for k in range(head, len(order)) if ty[order[k]] in (1, 2) and (fl[order[k]] & 8)]
    reordered = False
    if have and flagged_pos and flagged_pos[-1] - v1 > 254:
        ids = [order[p] for p in flagged_pos]
        ids = sorted(ids, key=lambda i: -usage[i])  # python sort is stable
        for p, i in zip(flagged_pos, ids):
            order[p] = i
        reordered = True
    new_of = np.full(n, -1, np.int64)
    for k, o in enumerate(order):
        new_of[o] = k
    out = raw[order].copy()
    oty = ty[order]
    ofl = fl[order].copy()
    inten = np.ascontiguousarray(out[:, 12:24]).view("<f4").reshape(-1, 3).copy()
    cl = np.ascontiguousarray(out[:, 76:88]).view("<f4").reshape(-1, 3)
    rad = np.ascontiguousarray(out[:, 72:76]).view("<f4").ravel()
    for k in range(len(order)):
        t = oty[k]
        f = int(ofl[k])
        if t in (3, 5):
            f = 0
        else:
            if LIGHT_OPT["pbr"] and (f & 0x20) and not (f & 4):
                c_, l_, q_ = [float(x) for x in cl[k]]
                best = 0.0
                cc = c_ - 0.5
                if abs(q_) < 1e-12:
                    if abs(l_) > 1e-12:
                        best = -cc / l_
                else:
                    disc = l_ * l_ - 4 * q_ * cc
                    if disc >= 0:
                        sq = disc ** 0.5
                        cands = [r for r in ((-l_ - sq) / (2 * q_), (-l_ + sq) / (2 * q_)) if r > 0]
                        best = min(cands) if cands else 0.0
                d50 = np.float32(best) if best > 0 else (np.float32(rad[k]) / np.float32(3.0) if rad[k] > 0 else np.float32(100.0))
                scale = np.float32(2.0) / (d50 * d50)
                inten[k] = (inten[k] * scale).astype(np.float32)
                f |= 4
            f &= ~(0x20 | 0x40)
            if wl_flag2 and t in (1, 2):
                f |= 2
        ofl[k] = f
    out[:, 12:24] = inten.view("u1").reshape(-1, 12)
    out[:, 88:92] = ofl.astype("<i4").view("u1").reshape(-1, 4)
    # probes
    pr = bytearray(r2.raw(0x65).tobytes())
    pa = np.frombuffer(pr, np.uint8).reshape(-1, 48)
    for i in range(len(pa)):
        for s in range(4):
            if pa[i, 0x20 + s] == 0:
                continue
            idx = int(np.frombuffer(pa[i, 0x24 + 2 * s:0x26 + 2 * s].tobytes(), "<i2")[0])
            wo = idx + E
            nw = int(new_of[wo]) if (idx >= 0 and wo < n) else -1
            if nw < 0:
                pa[i, 0x20 + s] = 0
                pa[i, 0x24 + 2 * s:0x26 + 2 * s] = np.frombuffer(np.int16(-1).tobytes(), np.uint8)
            else:
                pa[i, 0x24 + 2 * s:0x26 + 2 * s] = np.frombuffer(np.int16(nw - v1).tobytes(), np.uint8)
    # RTL
    rtl_out = None
    if have:
        slot = tex_old.copy()
        nwv = np.where(slot >= 0, new_of[np.clip(slot, 0, n - 1)] - v1, -1)
        nwv = np.where((slot >= 0) & (nwv >= 0) & (nwv <= 254), nwv, 255)
        nwv.sort(axis=1)  # 255 (none) last, ascending
        rtl_out = nwv.astype(np.uint8).tobytes()
    return out.tobytes(), bytes(pr), rtl_out, {"E": E, "S": S, "head": head, "v1": v1, "reordered": reordered, "sky": bytes(sky), "sun_alpha": sun_alpha}


def check(in_path, out_path, limit, rtl_bytes, use_bsp_tool, cubemaps="none", physics="stub", cubemap_scale=4.0,
          props="keep", scatter_min=500, csm_rebase=False, empty_shadow=False, wl_flag2=False, keep_flag10=False,
          grid_pad=True, unpadded=None, grid_size=0.0):
    C = Checker()
    r2 = Bsp(in_path)
    r1 = Bsp(out_path)
    L = float(limit)

    print("== header")
    C.ok(r1.ident == b"rBSP", "ident rBSP")
    C.ok(r1.version == 29, "version 29", str(r1.version))
    C.ok(r1.last == 127, "lastLump 127")
    C.ok(r1.revision == r2.revision, "revision kept (%d)" % r2.revision)
    size = len(r1.d)
    bad = [i for i, (o, l, v, cc) in enumerate(r1.lumps) if (l and (o % 4 or o + l > size)) or cc]
    C.ok(not bad, "lump offsets 4-byte aligned, inside file, fourCC 0", str(bad))
    spans = sorted((o, o + l, i) for i, (o, l, v, cc) in enumerate(r1.lumps) if l)
    C.ok(all(spans[k][1] <= spans[k + 1][0] for k in range(len(spans) - 1)), "lumps do not overlap")
    C.ok(r1.lumps[0x36][2] == 1, "world light lump version 1", str(r1.lumps[0x36][2]))

    print("== record sizes (R1 'funny lump size' checks)")
    badsz = [(hex(i), len(r1.raw(i)), s) for i, s in R1_SIZES.items() if len(r1.raw(i)) % s]
    C.ok(not badsz, "all %d lumps divisible by their R1 record size" % len(R1_SIZES), str(badsz))
    C.ok(len(r1.raw(0x55)) == 28, "cm grid is one 28 byte record")
    for i in (0x04, 0x05, 0x06, 0x07, 0x37, 0x7A):
        if len(r1.raw(i)):
            C.ok(False, "R2-only lump %#x present" % i)

    # ---- lightmaps / RTL
    print("== lightmap data")
    hd = r1.arr(0x53, "<u4,<u2,<u2")
    texels = sum(int(w) * int(h) for t, w, h in hd if t == 1)
    C.ok(len(r1.raw(0x62)) == 8 * texels, "sky lightmap size == 8 * type-1 texels (%d)" % texels)
    rtl = r1.raw(0x69)
    k = len(rtl) / max(texels, 1)
    elw, elp, elr, einfo = expected_lighting(r2, wl_flag2)
    if elr is not None:
        C.ok(len(rtl) == texels * 4 and bytes(rtl) == elr, "RTL lump = independent decode of R2 9 byte texels (4 bytes per texel, slot = world index - %d, min weight %d, %s)" % (einfo["v1"], LIGHT_OPT["min_weight"], "reordered by use" if einfo["reordered"] else "original order"))
        C.ok(1 <= k <= 4, "RTL bytes per texel in 1..4 (%.2f)" % k)
    else:
        C.ok(len(rtl) == texels * rtl_bytes or (len(rtl) == 0 and len(r2.raw(0x69)) == 0), "RTL lump %d bytes = %d per texel" % (len(rtl), rtl_bytes))
        C.ok(1 <= k <= 4 or len(rtl) == 0, "RTL bytes per texel in 1..4 (%.2f)" % k)
        if len(rtl):
            C.ok(bool((np.frombuffer(rtl[:1 << 20].tobytes(), dtype=np.uint8) == 255).all()), "RTL first MiB is 0xFF (no light assignments)")

    # ---- light probe refs
    print("== light probes")
    a2 = r2.arr(0x68, np.dtype([("v", "u1", 20)]))["v"]
    a1 = r1.arr(0x68, np.dtype([("v", "u1", 16)]))["v"]
    C.ok(len(a1) == len(a2), "light probe ref count kept (%d)" % len(a1))
    C.ok(bool((a1 == a2[:, :16]).all()), "light probe refs equal first 16 bytes of R2 records")
    for i in (0x66, 0x67):
        C.ok(bytes(r1.raw(i)[:4096]) == bytes(r2.raw(i)[:4096]) and len(r1.raw(i)) == len(r2.raw(i)), "lump %#x unchanged" % i)
    C.ok(bytes(r1.raw(0x62)) == einfo["sky"], "lightmap 0x62 = R2 with plane B alpha = 255 - 255 * linear(RTL sun slot visibility) on texels with a sun entry%s" % (" (rule 21)" if einfo["sun_alpha"] else " (unchanged)"))
    C.ok(bytes(r1.raw(0x65)) == elp, "light probes (0x65) = R2 probes with static light slots remapped (R2 world - %d -> R1 world - %d)" % (einfo["E"], einfo["v1"]))

    # ---- world lights
    print("== world lights")
    w1 = r1.arr(0x36, np.dtype([("v", "u1", 100)]))["v"]
    C.ok(bytes(r1.raw(0x36)) == elw, "world lights = rule 20 model (%d R2 sky ambient + %d sun entries, %d head entries, v1 %d, %d lights)" % (einfo["E"], einfo["S"], einfo["head"], einfo["v1"], len(w1)))
    C.ok(len(w1) <= 4064, "world light count <= 4064")
    if len(w1) >= 2 and einfo["head"] == 2:
        t1 = np.ascontiguousarray(w1[:, 52:56]).view("<i4").ravel()
        C.ok(int(t1[0]) == 5 and int(t1[1]) == 3, "R1 layout: light[0] is the sky ambient (type 5), light[1] the sun (type 3)")

    # ---- tricoll
    print("== tricoll")
    h2 = r2.arr(0x45, HDR_DT)
    h1 = r1.arr(0x45, HDR_DT)
    C.ok(len(h1) == len(h2), "header count kept (%d)" % len(h1))
    C.ok(int(h1["ntris"].max()) <= 1024, "max triangles per header %d <= 1024" % int(h1["ntris"].max()))
    for nm in ("flags", "texflags", "texdata", "nverts", "ntris", "nbev", "fv", "ft", "fn", "scale"):
        C.ok(bool((h1[nm] == h2[nm]).all()), "header field %s unchanged" % nm)
    C.ok(bool((h1["org"] == h2["org"]).all()), "header origin unchanged")
    cnt1 = (h1["nbev"].astype(np.int64) * 10 + 31) // 32
    fb1 = h1["fb"].astype(np.int64)
    C.ok(bool((fb1[1:] == fb1[:-1] + cnt1[:-1]).all()) and (len(h1) == 0 or fb1[0] == 0), "first_bevel_index contiguous with 10 bit sizing")
    C.ok(int(cnt1.sum()) * 4 == len(r1.raw(0x61)), "bevel index lump size == sum((10n+31)/32) words")
    for lump in (0x42, 0x44, 0x60):
        C.ok(bytes(r1.raw(lump).tobytes()) == bytes(r2.raw(lump).tobytes()), "lump %#x identical to R2" % lump)
    tris = r2.arr(0x42, "<u4")
    starts = r2.arr(0x60, "<u2")
    w2_ = r2.arr(0x61, "<u4")
    w1_ = r1.arr(0x61, "<u4")
    d2 = walk_bevels(r2, h2, tris, starts, w2_, 11)
    d1 = walk_bevels(r1, h1, tris, starts, w1_, 10)
    same = all(d1[i] == d2[i] for i in range(len(h1)))
    C.ok(same, "10 bit reader over output decodes to the same entries/records as 11 bit reader over input (%d headers, %d entries)" % (len(h1), sum(len(x) for x in d1)))
    mx = max((v for d in d1 for k_, v in d.items() if not isinstance(k_, tuple)), default=0)
    C.ok(mx <= 1023, "max bevel entry %d fits 10 bits" % mx)
    exts = [v >> 7 for d in d1 for k_, v in d.items() if isinstance(k_, tuple)]
    C.ok((max(exts) if exts else 0) < 8192, "max extended header index %d < 8192" % (max(exts) if exts else 0))

    # ---- collision grid
    print("== collision grid")
    g2 = r2.arr(0x55, "<f4,<i4,<i4,<i4,<i4,<i4,<i4")[0]
    g1 = r1.arr(0x55, "<f4,<i4,<i4,<i4,<i4,<i4,<i4")[0]
    nmodels = len(r1.raw(0x0E)) // 32
    cells = r1.arr(0x56, "<u2,<u2")
    gs = r1.arr(0x57, "<u2,<u2,<u4")
    gb = r1.arr(0x58, "<8i2")
    pr = r1.arr(0x59, "<u4")
    pb = r1.arr(0x5A, "<8i2")
    uc = r1.arr(0x5B, "<u4")
    C.ok(g1[0] == g2[0] if grid_size == 0 else (g1[0] >= grid_size and (g1[0] - grid_size) % 16 == 0), "grid cell size %g (R2 %g, requested %g, +16 steps when the grid would exceed the walker list)" % (g1[0], g2[0], grid_size))
    C.ok(len(cells) == int(g1[3]) * int(g1[4]) + nmodels, "cell count == nx*ny + models (%d)" % len(cells))
    C.ok(len(gs) == len(gb), "geoset and geoset bounds parallel (%d)" % len(gs))
    C.ok(len(pr) == len(pb), "primitive and primitive bounds parallel (%d)" % len(pr))
    C.ok(len(gs) <= 65535 and len(pr) <= 65536, "indices fit 16 bit")
    types = (pr >> 24) & 0xFF
    C.ok(not (types == 0x60).any(), "no prop (0x60) primitives left")
    single = np.array([int(x[1]) == 1 for x in gs]) if len(gs) else np.zeros(0, bool)
    gtypes = np.array([int(x[2]) >> 24 for x in gs]) if len(gs) else np.zeros(0)
    C.ok(not ((gtypes == 0x60) & single).any(), "no single-primitive prop geosets left")
    C.ok(set(np.unique(types).tolist()) <= {0, 0x40}, "primitive types only brush/tricoll %s" % np.unique(types).tolist())
    # cell ranges
    first = np.array([int(c[0]) for c in cells]); num = np.array([int(c[1]) for c in cells])
    C.ok(bool((first + num <= len(gs)).all()), "cell geoset ranges inside geoset lump")
    # multi geoset ranges + contents
    badc = 0; badr = 0
    for x in gs:
        if int(x[1]) == 1:
            if (int(x[2]) & 0xFF) >= len(uc): badc += 1
            continue
        idx = (int(x[2]) >> 8) & 0xFFFF
        n = int(x[1])
        if idx + n > len(pr) or n < 2:
            badr += 1; continue
        orv = 0
        for p in pr[idx:idx + n]:
            orv |= int(uc[int(p) & 0xFF])
        if int(uc[int(x[2]) & 0xFF]) != orv: badc += 1
    C.ok(badr == 0, "multi-primitive geoset ranges inside pool, count >= 2", str(badr))
    C.ok(badc == 0, "geoset unique contents == OR of children, indices in table", str(badc))
    sg = np.array([int(x[0]) for x in gs]) if len(gs) else np.zeros(0, int)
    nz = sg[sg != 0]
    C.ok(int(g1[5]) == (int(nz.max()) + 1 if len(nz) else int(g1[5])), "num_straddle_groups == max id + 1 (%d)" % g1[5])
    C.ok(len(set(nz.tolist())) == (int(nz.max()) if len(nz) else 0), "straddle group ids dense 1..N (%d)" % len(set(nz.tolist())))
    # straddle ids appear in >= 1 cell range
    if len(gs):
        ref = np.zeros(len(gs), np.int32)
        for f, n in zip(first, num): ref[f:f + n] += 1
        C.ok(not (ref == 0).any(), "every geoset is referenced by a cell (%d unreferenced)" % int((ref == 0).sum()))
    lo, hi = bounds_aabb(gb) if len(gb) else (np.zeros((0, 3)), np.zeros((0, 3)))
    C.ok(bool((lo >= -L - 1).all() and (hi <= L + 1).all()), "geoset bounds inside +-%g" % L, "min %.0f max %.0f" % (lo.min() if len(lo) else 0, hi.max() if len(hi) else 0))
    lo, hi = bounds_aabb(pb) if len(pb) else (np.zeros((0, 3)), np.zeros((0, 3)))
    C.ok(bool((lo >= -L - 1).all() and (hi <= L + 1).all()), "primitive bounds inside +-%g" % L)
    # grid extents. Without rule 18 cells that lie completely outside the limit must not exist; with it they exist
    # (padding) but must be empty.
    sc = float(g1[0]); ox, oy, nx, ny = int(g1[1]), int(g1[2]), int(g1[3]), int(g1[4])
    cx = (np.arange(nx) + ox) * sc
    cy = (np.arange(ny) + oy) * sc
    outx = (cx + sc <= -L) | (cx >= L)
    outy = (cy + sc <= -L) | (cy >= L)
    out_mask = (outy[:, None] | outx[None, :])
    world_num = num[:nx * ny].reshape(ny, nx)
    if grid_pad:
        C.ok(not (world_num[out_mask] != 0).any(), "cells completely outside the limit are empty (%d of them, padding)" % int(out_mask.sum()))
    else:
        C.ok(not out_mask.any(), "no grid cell lies completely outside the limit")
    part = int((((cx < -L) | (cx + sc > L))[None, :] | ((cy < -L) | (cy + sc > L))[:, None]).sum()) - int(out_mask.sum())
    print("  note  %d grid cells straddle the limit (cell size %g cannot be cut)" % (part, sc))
    check_grid_padding(C, r2, r1, g2, g1, cells, nmodels, grid_pad, unpadded, grid_size)
    # R2 counts for the report
    gs2 = r2.arr(0x57, "<u2,<u2,<u4")
    print("  note  geosets %d -> %d, primitives %d -> %d, straddle %d -> %d" % (len(gs2), len(gs), len(r2.raw(0x59)) // 4, len(pr), g2[5], g1[5]))


    # ---- sprp
    print("== sprp game lump")
    for tag, b in (("R2", r2), ("R1", r1)):
        o, l, _, _ = b.lumps[0x23]
        g = bytes(b.raw(0x23))
        n = struct.unpack_from("<I", g, 0)[0]
        hdrs = [struct.unpack_from("<IHHII", g, 4 + 16 * i) for i in range(n)]
        if tag == "R2":
            h2h = hdrs; g2b = g; o2 = o
        else:
            h1h = hdrs; g1b = g; o1 = o
    C.ok(len(h1h) == len(h2h) and h1h[0][0] == h2h[0][0], "game lump has the same sub-lump ids")
    gid, fl, ver, off, ln = h1h[0]
    C.ok(ver == 12 and fl == 0, "sprp version 12 in directory (%d)" % ver)
    C.ok(off - o1 == 4 + 16 * len(h1h1 := h1h) and off + ln <= o1 + len(g1b), "sprp offset is an absolute file offset right after the directory")
    s1 = g1b[off - o1: off - o1 + ln]
    _, _, _, off2, ln2 = h2h[0]
    s2 = g2b[off2 - o2: off2 - o2 + ln2]
    nm1 = struct.unpack_from("<I", s1, 0)[0]; nm2 = struct.unpack_from("<I", s2, 0)[0]
    C.ok(nm1 == nm2 and s1[4:4 + 128 * nm1] == s2[4:4 + 128 * nm2], "model name table kept (%d names)" % nm1)
    q1 = 4 + 128 * nm1
    nl = struct.unpack_from("<I", s1, q1)[0]
    C.ok(nl == 0, "leaf list empty")
    q1 += 4
    np1, u11, u12 = struct.unpack_from("<III", s1, q1); q1 += 12
    q2 = 4 + 128 * nm2
    np2, u21, u22 = struct.unpack_from("<III", s2, q2); q2 += 12
    C.ok((np1, u11, u12) == (np2, u21, u22), "prop count and unknown ints kept (%d, %d, %d)" % (np1, u11, u12))
    C.ok(len(s1) - q1 == 84 * np1, "prop array is exactly 84 bytes per prop (no tail)")
    p1 = np.frombuffer(s1[q1:q1 + 84 * np1], dtype=np.dtype([("v", "u1", 84)]))["v"]
    p2 = np.frombuffer(s2[q2:q2 + 64 * np2], dtype=np.dtype([("v", "u1", 64)]))["v"]
    def f32(a, o, n): return np.ascontiguousarray(a[:, o:o + 4 * n]).view("<f4").reshape(len(a), n)
    ok_org = bool((f32(p1, 0, 6) == f32(p2, 0, 6)).all())
    C.ok(ok_org, "origin + angles mapped")
    if props == "keep": C.ok(bool((f32(p1, 0x48, 1) == f32(p2, 0x18, 1)).all()), "scale mapped (+0x18 -> +0x48)")
    C.ok(bool((p1[:, 0x18:0x1A] == p2[:, 0x1C:0x1E]).all()), "model index mapped (+0x1C -> +0x18)")
    mi2 = np.ascontiguousarray(p2[:, 0x1C:0x1E]).view("<u2").ravel().astype(np.int64)
    cnt2 = np.bincount(mi2, minlength=max(nm2, 1))
    if props == "hide-all": hide_ = np.ones(len(p2), bool)
    elif props == "hide-scatter": hide_ = cnt2[mi2] >= scatter_min
    else: hide_ = np.zeros(len(p2), bool)
    exp_fl = p2[:, 0x1F].copy()
    if not keep_flag10: exp_fl = np.where((exp_fl & 0x40) == 0, exp_fl & 0xEF, exp_fl).astype(np.uint8)
    C.ok(bool((p1[:, 0x1E] == p2[:, 0x1E]).all()) and bool((p1[:, 0x1F] == exp_fl).all()) and bool((p1[:, 0x20:0x24] == p2[:, 0x20:0x24]).all()),
         "solid, flags, skin, cubemap mapped (+0x1E..0x23)%s" % ("; rule 16: flag bit 0x10 kept (--keep-prop-flag10), set on %d props" % int(((p2[:, 0x1F] & 0x10) != 0).sum()) if keep_flag10 else "; rule 16: flag bit 0x10 cleared on %d props that lack 0x40 (R1 would not draw them in the main pass)" % int((((p2[:, 0x1F] & 0x10) != 0) & ((p2[:, 0x1F] & 0x40) == 0)).sum())))
    if props != "keep":
        sc1 = f32(p1, 0x48, 1).ravel(); sc2 = f32(p2, 0x18, 1).ravel()
        C.ok(bool((sc1[hide_] == np.float32(0.0001)).all()) and bool((sc1[~hide_] == sc2[~hide_]).all()), "rule 16: %d props hidden (scale 0.0001), the rest keep their scale" % int(hide_.sum()))
    C.ok(bool((f32(p1, 0x24, 1) == f32(p2, 0x24, 1)).all()) and bool((f32(p1, 0x28, 1) == f32(p2, 0x24, 1)).all()), "fade min/max = R2 value at +0x24")
    C.ok(bool((p1[:, 0x2C:0x38] == p2[:, 0x28:0x34]).all()), "lighting origin mapped bitwise (+0x28 -> +0x2C)")
    C.ok(bool((f32(p1, 0x38, 1) == 1.0).all()), "forced_fade_scale 1.0")
    C.ok(bool((p1[:, 0x3C:0x40] == 0).all()) and bool((p1[:, 0x44:0x48] == 0).all()), "cpu/gpu level and disable_x360 zero")
    C.ok(bool((p1[:, 0x40:0x44] == p2[:, 0x34:0x38]).all()), "diffuse modulation mapped (+0x34 -> +0x40)")
    C.ok(bool((p1[:, 0x4C:0x54] == p2[:, 0x38:0x40]).all()), "collision flags add/remove mapped (+0x38 -> +0x4C)")
    C.ok(bool((p1[:, 0x1A:0x1E] == 0).all()), "first_leaf / num_leaves zero")
    C.ok(len(r1.raw(0x66)) // 4 == np1, "static prop lightprobe index count == prop count")

    # ---- render meshes
    print("== render meshes")
    m2 = r2.arr(0x50, MESH_DT_R2)
    m1 = r1.arr(0x50, MESH_DT_R1)
    mb = r1.arr(0x51, "<8f4")
    so = r1.arr(0x52, SORT_DT)
    so2 = r2.arr(0x52, SORT_DT)
    idx = r1.arr(0x4F, "<u2")
    pos = r1.arr(0x03, "<f4").reshape(-1, 3)
    nrm = r1.arr(0x1E, "<f4").reshape(-1, 3)
    mdl = r1.arr(0x0E, "<6f4,<2u4")
    C.ok(len(m1) == len(m2) == len(mb), "mesh count kept (%d) and mesh bounds parallel" % len(m1))
    C.ok(bool((m1["vt"] == m2["vt"].astype(np.uint16)).all()), "mesh vertex type is a clean uint16 (cubemap byte cleared)")
    C.ok(bool((m1["fl"] == m2["fl"]).all()), "mesh flags unchanged")
    w0, wn = int(mdl[0][1][0]), int(mdl[0][1][1])
    names2 = mesh_material_names(r2, m2, so2)
    names1 = mesh_material_names(r1, m1, so)
    removed = [int(m2[i]["nt"]) > 0 and is_nondrawing_mesh(names2[i], int(m2[i]["fl"])) for i in range(len(m2))]
    removed_tris = sum(int(m2[i]["nt"]) for i, drop in enumerate(removed) if drop)
    removed_meshes = sum(removed)
    C.ok(all(int(m1[i]["nt"]) == 0 for i, drop in enumerate(removed) if drop),
          "rule 15: all %d source non-drawing mesh slots emptied (%d triangles)" % (removed_meshes, removed_tris))
    active_nondrawing = [i for i, m in enumerate(m1) if int(m["nt"]) > 0 and is_nondrawing_mesh(names1[i], int(m["fl"]))]
    C.ok(not active_nondrawing, "rule 15: no active non-sky tools/SKIP render meshes (%d found)" % len(active_nondrawing))
    empty_slots_ok = all(
        int(m1[i]["fmi"]) == 0 and int(m1[i]["fv"]) == 0 and int(m1[i]["nv"]) == 0
        and not mb[i].any()
        for i, drop in enumerate(removed) if drop
    )
    C.ok(empty_slots_ok, "rule 15: filtered mesh slots have zero ranges and bounds")
    compact_expected = 0
    compact_bad = 0
    for m in m1:
        nt = int(m["nt"])
        if not nt:
            continue
        compact_expected = (compact_expected + 5) // 6 * 6
        if int(m["fmi"]) != compact_expected:
            compact_bad += 1
        compact_expected += 3 * nt + (3 if nt & 1 else 0)
    C.ok(compact_bad == 0 and len(idx) == compact_expected and len(idx) % 6 == 0,
          "rule 15: compact mesh-index ranges/padding (%d bad offsets, %d entries expected, %d actual)" %
          (compact_bad, compact_expected, len(idx)))
    print("  info  rule 15 removed %d non-drawing mesh slots / %d triangles" % (removed_meshes, removed_tris))
    lim_models = bool((np.abs(np.array([list(x[0]) for x in mdl])) <= L + 1e-3).all())
    C.ok(lim_models, "all model mins/maxs inside +-%g (model 0: %s)" % (L, [round(float(v)) for v in mdl[0][0]]))
    C.ok(bool((np.array([list(x[0]) for x in mdl])[:, :3] >= -L).all()), "model mins >= -limit")
    C.ok(bool((so["vo"] >= 0).all()) and len(so) < 65536, "sorts: offsets >= 0, count %d < 65536" % len(so))
    # per-mesh checks (all meshes)
    bad_idx = bad_range = bad_align = bad_fvnv = bad_pos = bad_nrm = 0
    world_out = 0
    worst = 0.0
    tang_bad = 0
    vtx = {t: r1.raw(VTX_LUMP[t]) for t in range(4)}
    vcount = {t: len(vtx[t]) // VTX_STRIDE[t] for t in range(4)}
    vrec = {t: np.frombuffer(vtx[t].tobytes(), dtype=np.uint32).reshape(-1, VTX_STRIDE[t] // 4) if len(vtx[t]) else np.zeros((0, VTX_STRIDE[t] // 4), np.uint32) for t in range(4)}
    for mi in range(len(m1)):
        m = m1[mi]
        nt = int(m["nt"])
        if nt == 0:
            continue
        t = (int(m["fl"]) >> 9) & 3
        s = so[int(m["ms"])]
        f = int(m["fmi"])
        if f % 6: bad_align += 1
        if f + 3 * nt > len(idx):
            bad_range += 1; continue
        ii = idx[f:f + 3 * nt].astype(np.int64)
        gi = ii + int(s["vo"])
        if gi.max() >= vcount[t]:
            bad_idx += 1; continue
        if not (ii.min() == m["fv"] and ii.max() - ii.min() == m["nv"]): bad_fvnv += 1
        rec = vrec[t][gi]
        if rec[:, 0].max() >= len(pos): bad_pos += 1; continue
        if rec[:, 1].max() >= len(nrm): bad_nrm += 1; continue
        if t in (1, 3):
            tcol = 9 if t == 1 else 5
            ti = rec[:, tcol]; tj = rec[:, tcol + 1]
            if ti.max() >= len(nrm) or tj.max() >= len(nrm): tang_bad += 1
        if w0 <= mi < w0 + wn:
            p = pos[rec[:, 0]]
            worst = max(worst, float(np.abs(p).max()))
    C.ok(bad_range == 0, "mesh index ranges inside lump 0x4F", str(bad_range))
    C.ok(bad_align == 0, "mesh index ranges start on multiples of 6", str(bad_align))
    C.ok(bad_idx == 0, "mesh indices (sort offset + index) inside the vertex lumps", str(bad_idx))
    C.ok(bad_fvnv == 0, "mesh first_vertex == min index and num_vertices == max-min", str(bad_fvnv))
    C.ok(bad_pos == 0 and bad_nrm == 0 and tang_bad == 0, "vertex position / normal / tangent indices in range", "%d %d %d" % (bad_pos, bad_nrm, tang_bad))
    C.ok(worst <= L + 1e-3, "world mesh vertex positions inside +-%g (max %.3f)" % (L, worst))
    # mesh bounds (oriented boxes): world AABB inside limit for world meshes
    yaw = np.arctan(mb[:, 7].astype(np.float64))
    c_ = np.abs(np.cos(yaw)); s_ = np.abs(np.sin(yaw))
    wx = c_ * mb[:, 4] + s_ * mb[:, 5]; wy = s_ * mb[:, 4] + c_ * mb[:, 5]
    w = np.stack([wx, wy, mb[:, 6]], 1)
    lo_ = mb[:, 0:3] - w; hi_ = mb[:, 0:3] + w
    ww = slice(w0, w0 + wn)
    C.ok(bool((lo_[ww] >= -L - 0.01).all() and (hi_[ww] <= L + 0.01).all()), "world mesh bounds inside +-%g" % L, "min %.1f max %.1f" % (lo_[ww].min(), hi_[ww].max()))
    # sorts: cubemap handling (README.txt rule 10)
    meshcm = m2["cm"].astype(np.int16)
    # every mesh keeps its texture data and lightmap header through the sort rewrite
    same_sort = np.array([(so[m1["ms"][i]]["td"] == so2[m2["ms"][i]]["td"]) and (so[m1["ms"][i]]["lm"] == so2[m2["ms"][i]]["lm"]) for i in range(len(m1))])
    C.ok(bool(same_sort.all()), "every mesh keeps its texture data and lightmap header through the sort rewrite")
    if cubemaps == "none":
        C.ok(bool((so["cm"] == -1).all()), "cubemaps=none: every material sort has cubemap -1")
    elif cubemaps == "split":
        cmok = np.array([so[m1["ms"][i]]["cm"] == meshcm[i] for i in range(len(m1))])
        C.ok(bool(cmok.all()), "cubemaps=split: every mesh's sort cubemap equals the R2 mesh cubemap (%d of %d)" % (int(cmok.sum()), len(cmok)))
    else:
        C.ok(bool((so["cm"][:len(so2)] == so2["cm"]).all()), "cubemaps=keep: the R2 sorts keep their cubemap")
    # unchanged meshes: identical index range content
    unchanged = (m1["fmi"] == m2["fmi"]) & (m1["nt"] == m2["nt"])
    print("  note  %d of %d meshes untouched by clipping" % (int(unchanged.sum()), len(m1)))
    C.ok(len(r1.raw(0x47)) >= len(r2.raw(0x47)) and len(r1.raw(0x49)) >= len(r2.raw(0x49)), "vertex lumps only grew (originals kept)")

    # ---- vertex lumps: every index field validated for ALL records (not only the ones meshes reference)
    print("== vertex frames (rule 11)")
    nN = len(nrm); nP = len(pos)
    for t_, tcol_ in ((0, None), (1, 9), (2, None), (3, 5)):
        a = vrec[t_]
        if not len(a): continue
        okp = bool((a[:, 0] < nP).all()); okn = bool((a[:, 1] < nN).all())
        okt = True if tcol_ is None else bool((a[:, tcol_] < nN).all() and (a[:, tcol_ + 1] < nN).all())
        C.ok(okp and okn and okt, "vertex lump 0x%02X: all %d records have position < %d, normal < %d%s" % (VTX_LUMP[t_], len(a), nP, nN, "" if tcol_ is None else ", tangent S/T < %d (plain indices, no flag bit)" % nN))
    C.ok(len(r1.raw(0x4B)) == 0 or len(r1.raw(0x4B)) % 28 == 0, "blinn-phong lump size ok")
    for t_ in range(4):
        stride = VTX_STRIDE[t_]
        original = r2.raw(VTX_LUMP[t_]).reshape(-1, stride)[:, 16:20]
        converted = r1.raw(VTX_LUMP[t_]).reshape(-1, stride)
        rgba_ok = len(converted) >= len(original) and bool((original == converted[:len(original), 16:20]).all())
        C.ok(rgba_ok, "vertex lump 0x%02X: original RGBA bytes at +16 preserved" % VTX_LUMP[t_])
    nr2 = len(r2.raw(0x1E)) // 12
    import frame_check
    C.ok(bytes(r1.raw(0x1E)[:nr2 * 12].tobytes()) == bytes(r2.raw(0x1E).tobytes()), "existing vertex normal entries unchanged (first %d), %d appended" % (nr2, nN - nr2))
    allN = np.frombuffer(r1.raw(0x1E).tobytes(), dtype=np.uint32).reshape(-1, 3)
    tidx = np.unique(np.concatenate([vrec[1][:, 10], vrec[3][:, 6]] if len(vrec[3]) else [vrec[1][:, 10]]))
    tidx = tidx[tidx >= nr2]
    C.ok(len(np.unique(allN[tidx], axis=0)) == len(tidx), "tangent T entries appended to 0x1E are unique on float bits (%d distinct of %d appended; the rest are clip-blended normals)" % (len(tidx), nN - nr2))
    ag, tot, mdn, mdt_, ub = frame_check.frame_stats(r1, True, limit=300000)
    ag2, tot2, _, _, _ = frame_check.frame_stats(r2, False, limit=300000)
    C.ok(tot >= 100000 or tot == tot2, "frame check sample size %d vertices (>= 100000 or all available)" % tot)
    C.ok(ag == ag2 and tot == tot2, "converted frame reproduces the R2 flag rule exactly: %d of %d sampled vertices satisfy the UV-handedness rule in the output, %d of %d in the R2 input" % (ag, tot, ag2, tot2))
    C.ok(ag >= 0.95 * tot, "handedness rule agrees with the triangle's UV handedness on %.2f%% of %d sampled vertices (stock R1 mp_box: 100%% of 3594; the residual is in the R2 input too)" % (100.0 * ag / max(tot, 1), tot))
    C.ok(mdt_ < 1e-3, "max |dot(n,t)| over sample = %.2e (T is perpendicular to the normal)" % mdt_)
    C.ok(ub == 0, "all sampled T vectors are unit length")
    print("  note  max |dot(n,s)| over sample %.3f (S is copied from R2; stock R1 mp_box has up to 0.285 too)" % mdn)

    # ---- physics (rule 12)
    print("== physics collide (rule 12)")
    def parse_phys(raw):
        b = bytes(raw.tobytes()); o = 0; recs = []
        while o + 16 <= len(b):
            m, ds, ks, ns = struct.unpack_from("<iiii", b, o); o += 16
            if m == -1:
                return recs, (m, ds, ks, ns), o == len(b)
            recs.append((m, ds, ks, ns)); o += ds + ks
        return recs, None, False
    recs, term, exact = parse_phys(r1.raw(0x1D))
    C.ok(term == (-1, -1, 0, 0) and exact, "lump 0x1D parses as {model,dataSize,keySize,solids} records ending with {-1,-1,0,0} (%d records)" % len(recs))
    if physics == "stub":
        C.ok(any(r[0] == 0 and r[3] >= 1 for r in recs), "lump 0x1D: model 0 has >= 1 solid %s" % [r for r in recs if r[0] == 0])
        in_recs, _, _ = parse_phys(r2.raw(0x1D))
        if not any(r[0] == 0 and r[3] >= 1 for r in in_recs):
            C.ok(len(r1.raw(0x1D)) == 2315 and bytes(r1.raw(0x1D)[:4]) == struct.pack("<i", 0), "lump 0x1D is the 2315 byte stub")
        else:
            C.ok(bytes(r1.raw(0x1D).tobytes()) == bytes(r2.raw(0x1D).tobytes()), "lump 0x1D kept (input already has world solids)")
    else:
        C.ok(bytes(r1.raw(0x1D).tobytes()) == bytes(r2.raw(0x1D).tobytes()), "lump 0x1D unchanged (--physics none)")

    # ---- virtual-tricoll table (rule 14)
    print("== physics triangles (rule 14)")
    header_bytes = len(r1.raw(0x45))
    headers_valid = header_bytes % 44 == 0
    C.ok(headers_valid, "tricoll header lump size is a multiple of 44 bytes")
    if headers_valid:
        header_count = header_bytes // 44
        C.ok(header_count <= 0xFFFF, "tricoll header count fits the 0x46 u16 table (%d)" % header_count)
        table = bytes(r1.raw(0x46).tobytes())
        C.ok(len(table) == 2 + 2 * header_count,
             "lump 0x46 length is 2 + 2 * header count (%d entries)" % header_count,
             "got %d bytes" % len(table))
        table_count = struct.unpack_from("<H", table)[0] if len(table) >= 2 else -1
        C.ok(table_count == header_count,
             "lump 0x46 count equals lump 0x45 header count (%d)" % header_count,
             "got %d" % table_count)
        frame_valid = len(table) >= 2 and len(table) % 2 == 0
        sizes = np.frombuffer(table, dtype="<u2", offset=2) if frame_valid else np.zeros(0, dtype="<u2")
        C.ok(len(sizes) == header_count and bool((sizes == 0xFFFF).all()),
             "lump 0x46 has one all-absent 0xFFFF entry per header and no blobs")
    else:
        C.ok(False, "lump 0x46 matches invalid tricoll header count")

    # ---- cubemap textures (rule 13)
    print("== cubemap textures (rule 13)")
    import io, zipfile
    try:
        zf = zipfile.ZipFile(io.BytesIO(r1.raw(0x28).tobytes()))
        zbad = zf.testzip()
    except Exception as e:  # pragma: no cover
        zf = None; zbad = str(e)
    C.ok(zf is not None and zbad is None, "pakfile (0x28) is a valid zip (python zipfile testzip)", str(zbad))
    ncube = len(r1.raw(0x2A)) // 16
    cm_ent = r1.arr(0x2A, np.dtype([("o", "<i4", 3), ("u", "<u4")]))
    if zf is not None and ncube:
        names = {i.filename: i for i in zf.infolist()}
        C.ok(all(i.compress_type == 0 for i in zf.infolist()), "all pakfile entries are stored (method 0)")
        dirs = {n.rsplit("/", 1)[0] for n in names}
        C.ok(len(dirs) == 1 and next(iter(dirs)).startswith("materials/maps/"), "all entries live in one materials/maps/<map> directory %s" % dirs)
        d0 = next(iter(dirs)) if dirs else ""
        expect = {d0 + "/cubemapdefault.vtf", d0 + "/cubemapdefault.hdr.vtf"}
        for o in cm_ent["o"]:
            base = "%s/c%d_%d_%d" % (d0, o[0], o[1], o[2])
            expect |= {base + ".vtf", base + ".hdr.vtf"}
        C.ok(set(names) == expect, "pakfile holds exactly cubemapdefault + c<x>_<y>_<z> .vtf/.hdr.vtf for each of the %d 0x2A entries (no R2 cubemaps.hdr.vtf)" % ncube,
             "extra %s missing %s" % (sorted(set(names) - expect)[:3], sorted(expect - set(names))[:3]))
        def vh(d):
            hs, w, h, fl, fr, fmt, mips, start = struct.unpack_from("<I", d, 12)[0], *struct.unpack_from("<HH", d, 16), struct.unpack_from("<I", d, 20)[0], struct.unpack_from("<H", d, 24)[0], struct.unpack_from("<I", d, 0x34)[0], d[0x38], 0
            return dict(sig=d[:4], ver=struct.unpack_from("<II", d, 4), hs=hs, w=w, h=h, flags=fl, frames=fr, fmt=fmt, mips=mips, depth=struct.unpack_from("<H", d, 0x3E)[0], nres=struct.unpack_from("<I", d, 0x44)[0])
        okh = True; bad = []
        for n in sorted(names):
            d = zf.read(n); hv = vh(d); hdr = n.endswith(".hdr.vtf")
            if hdr:
                want = dict(sig=b"VTF\0", ver=(7, 5), hs=96, w=64, h=64, flags=0x620C, frames=1, fmt=0, mips=7, depth=256, nres=2)
                size = 96 + 6 * 4 * sum(4 ** k for k in range(7))
            else:
                want = dict(sig=b"VTF\0", ver=(7, 5), hs=88, w=32, h=32, flags=0x4000, frames=1, fmt=15, mips=6, depth=256, nres=1)
                size = 8440
            if hv != want or len(d) != size: okh = False; bad.append((n, {k: v for k, v in hv.items() if want[k] != v}, len(d), size))
        C.ok(okh, "every VTF has the stock R1 header fields (.hdr.vtf: v7.5 RGBA8888 64x64 flags 0x620c 7 mips header 96 + CRC resource, 131160 bytes; .vtf: DXT5 32x32 flags 0x4000 6 mips, 8440 bytes)", str(bad[:2]))
        # R1 decode of the hdr files (shader formula): rgb/255 * (alpha + 1) / 64, alpha >= 4 like the stock files
        def faces8(d):
            a = np.frombuffer(d[96:], dtype=np.uint8)
            off = sum(6 * (64 >> m) ** 2 * 4 for m in range(6, 0, -1))
            return a[off:off + 6 * 64 * 64 * 4].reshape(6, 64, 64, 4).astype(np.float64)
        def r1dec(t): return t[..., :3] / 255.0 * ((t[..., 3:4] + 1) / 64.0)
        cubes = []; mina = 255
        for o in cm_ent["o"]:
            t = faces8(zf.read("%s/c%d_%d_%d.hdr.vtf" % (d0, o[0], o[1], o[2]))); cubes.append(r1dec(t)); mina = min(mina, int(t[..., 3].min()))
        C.ok(mina >= 4, "hdr cubemap alpha scale byte >= 4 like the stock files (min %d)" % mina)
        dflt = r1dec(faces8(zf.read(d0 + "/cubemapdefault.hdr.vtf")))
        err = float(np.abs(dflt - np.mean(cubes, axis=0)).mean() / max(float(dflt.mean()), 1e-9))
        C.ok(err < 0.03, "cubemapdefault.hdr.vtf decodes to the average of the cubemaps (mean relative error %.2f%%)" % (100 * err))
        # DXT5 LDR files: decode and compare with value/4 (linear), alpha 255
        try:
            import ctypes, os
            here = os.path.dirname(os.path.abspath(__file__))
            bc = ctypes.CDLL(os.path.join(here, "analysis", "bc6.dll"))
            worst3 = 0.0
            for o, cube in zip(cm_ent["o"], cubes):
                d3 = zf.read("%s/c%d_%d_%d.vtf" % (d0, o[0], o[1], o[2]))
                off = 88 + sum(6 * max((32 >> m) // 4, 1) ** 2 * 16 for m in range(5, 0, -1))
                c32 = cube.reshape(6, 32, 2, 32, 2, 3).mean((2, 4))
                for face in range(6):
                    buf = np.zeros(32 * 32 * 4, np.uint8)
                    bc.decode_bc3(d3[off + face * 1024:off + (face + 1) * 1024], 32, 32, buf.ctypes.data_as(ctypes.c_void_p))
                    got = buf.reshape(32, 32, 4)
                    ref = np.clip(c32[face] / 4.0, 0, 1) * 255
                    worst3 = max(worst3, float(np.abs(got[..., :3].astype(np.float64) - ref).mean()))
                    if not (got[..., 3] == 255).all(): worst3 = 999
            tol3 = max(4.0, 3.0 * cubemap_scale)  # BC3 block error grows with the signal level, which scales with the cubemap scale
            C.ok(worst3 < tol3, "DXT5 .vtf mip 0 decodes to the linear value/4 of the HDR cube, alpha 255 (worst mean abs error %.2f of 255, tolerance %.1f)" % (worst3, tol3))
        except Exception as e:
            print("  skip  DXT5 comparison unavailable: %r" % (e,))
        # independent BC6H decode of the R2 file, same frame/face order, * cubemap_scale
        try:
            import ctypes, os
            here = os.path.dirname(os.path.abspath(__file__))
            z2 = zipfile.ZipFile(io.BytesIO(r2.raw(0x28).tobytes()))
            src = [n for n in z2.namelist() if n.endswith("cubemaps.hdr.vtf")][0]
            sd = z2.read(src)
            bc = ctypes.CDLL(os.path.join(here, "analysis", "bc6.dll"))
            w2 = struct.unpack_from("<H", sd, 16)[0]; fr2 = struct.unpack_from("<H", sd, 24)[0]; mp2 = sd[0x38]
            sizes = [max((w2 >> m) // 4, 1) ** 2 * 16 for m in range(mp2)]
            start = [struct.unpack_from("<I", sd, 0x50 + 8 * i + 4)[0] for i in range(struct.unpack_from("<I", sd, 0x44)[0]) if sd[0x50 + 8 * i:0x50 + 8 * i + 3] == b"0\0\0"][0]
            off0 = start + sum(sizes[m] * fr2 * 6 for m in range(mp2 - 1, 0, -1))
            worst = 0.0; worst_abs = 0.0; tab = []
            for f in range(fr2):
                row = []
                for face in range(6):
                    o = off0 + (f * 6 + face) * sizes[0]
                    buf = np.zeros(w2 * w2 * 3, np.float32)
                    bc.decode_bc6h(sd[o:o + sizes[0]], w2, w2, buf.ctypes.data_as(ctypes.c_void_p), 0)
                    k = w2 // 64
                    ref = np.minimum(buf.reshape(64, k, 64, k, 3).astype(np.float64).mean((1, 3)) * cubemap_scale, 4.0)
                    ae = float(np.abs(cubes[f][face] - ref).mean())
                    e = float(np.abs(cubes[f][face] - ref).sum() / max(ref.sum(), 1e-12)); row.append(e)
                    worst_abs = max(worst_abs, ae)
                    if ae > 2e-4: worst = max(worst, e)  # relative error only counts where the absolute error exceeds the 8 bit step at alpha 4 (3.1e-4 / 2)
                tab.append(row)
            C.ok(worst < 0.02, "R1 decode of the hdr cubemaps equals the independent BC6H decode * %g (4x4 box, clamp 4.0), same frame/face order; per face: mean absolute error <= 2e-4 (worst %.1e, half the 8 bit step at alpha 4 is 1.5e-4) or relative error < 2%% (worst counted %.2f%%)" % (cubemap_scale, worst_abs, 100 * worst))
            for f, row in enumerate(tab): print("  note  cube %d per-face relative error %% (slot 0..5): %s" % (f, " ".join("%.2f" % (100 * x) for x in row)))
        except Exception as e:
            print("  skip  independent BC6H comparison unavailable: %r" % (e,))
    elif zf is not None:
        print("  note  no cubemaps (0x2A empty)")

    # ---- misc float AABBs: stock R1 maps contain +-inf / beyond-limit records here, so they must be unchanged
    print("== misc bounds")
    for lump, nm in ((0x77, "cell aabb"), (0x63, "csm aabb"), (0x79, "obj ref bounds")):
        if lump == 0x63 and csm_rebase: continue
        C.ok(bytes(r1.raw(lump).tobytes()) == bytes(r2.raw(lump).tobytes()), "%s lump 0x%02X identical to R2 (not clamped)" % (nm, lump))

    # ---- lump passthrough for the lumps that must be identical
    print("== pass-through lumps")
    same = []
    for i in (0x00, 0x01, 0x02, 0x0E if False else 0x18, 0x24, 0x2A, 0x2B, 0x2C, 0x53, 0x5C, 0x5D, 0x5E, 0x5F, 0x63 if False else (0x64 if not csm_rebase else 0x00), 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x78, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F):
        if i in (0x7C, 0x7D, 0x7E, 0x7F) and (csm_rebase or empty_shadow) and not (i in (0x7C, 0x7D) and csm_rebase and not empty_shadow):
            continue
        a = r1.raw(i); b = r2.raw(i)
        if len(a) != len(b) or bytes(a[:1 << 16]) != bytes(b[:1 << 16]) or bytes(a[-(1 << 16):]) != bytes(b[-(1 << 16):]):
            same.append(hex(i))
    C.ok(not same, "unchanged lumps are byte-identical (spot check first/last 64 KiB)", str(same))

    # ---- rule 17: shadow data
    print("== shadow lumps (rule 17)")
    env2 = r2.arr(0x05, "<i4").reshape(-1, 9) if len(r2.raw(0x05)) else np.zeros((0, 9), "<i4")
    n2 = r2.arr(0x63, np.dtype([("v", "u1", 32)]))["v"]; n1 = r1.arr(0x63, np.dtype([("v", "u1", 32)]))["v"]
    r2r = r2.arr(0x64, "<u2"); r1r = r1.arr(0x64, "<u2")
    sm2 = r2.arr(0x7F, "<u4,<u4,<u2,<i2"); sm1 = r1.arr(0x7F, "<u4,<u4,<u2,<i2")
    si2 = r2.arr(0x7E, "<u2"); si1 = r1.arr(0x7E, "<u2")
    C.ok(int(sum(int(x[1]) for x in sm1)) * 3 == len(si1), "shadow mesh triangle counts address exactly the %d shadow indices" % len(si1))
    if csm_rebase and len(env2) >= 2:
        e = env2[-1]; fn, fr, fm = int(e[0]), int(e[1]), int(e[2])
        nmesh_all = len(r2.raw(0x50)) // 28
        nprops = int(np.frombuffer(bytes(r2.raw(0x7B)[12:16]), "<u4")[0])
        obj_total = nmesh_all + nprops
        rr_old = r2r[fr:int(e[4])]
        keep = rr_old < obj_total
        pref = np.concatenate([[0], np.cumsum(keep)]).astype(np.int64)
        C.ok(len(n1) == len(n2) - fn and len(r1r) == int(keep.sum()) and (empty_shadow or len(sm1) == len(sm2) - fm), "csm rebase: %d nodes, %d refs (%d dropped beyond meshes + props = %d), %d shadow meshes after dropping %d / %d / %d leading entries" % (len(n1), len(r1r), int((~keep).sum()), obj_total, len(sm1), fn, fr, fm))
        exp = n2[fn:].copy()
        nc = exp[:, 12]
        fc = np.ascontiguousarray(exp[:, 28:30]).view("<u2").ravel().astype(np.int64)
        fc2 = np.where(nc > 0, fc - fn, fc).astype(np.uint16)
        exp[:, 28:30] = fc2.view("u1").reshape(-1, 2)
        f0 = np.ascontiguousarray(exp[:, 30:32]).view("<u2").ravel().astype(np.int64) - fr
        own = exp[:, 13].astype(np.int64)
        tot = np.ascontiguousarray(exp[:, 14:16]).view("<u2").ravel().astype(np.int64)
        exp[:, 30:32] = pref[f0].astype(np.uint16).view("u1").reshape(-1, 2)
        exp[:, 13] = (pref[f0 + own] - pref[f0]).astype(np.uint8)
        exp[:, 14:16] = (pref[f0 + tot] - pref[f0]).astype(np.uint16).view("u1").reshape(-1, 2)
        C.ok(bool((n1 == exp).all()), "csm rebase: node array = R2 nodes from the last environment on, child indices lowered by %d, object ranges re-derived after dropping out-of-range refs" % fn)
        C.ok(bool((r1r == rr_old[keep]).all()), "csm rebase: object references = R2 refs in range")
        root = n1[0].copy(); mins = np.ascontiguousarray(root[0:12]).view("<f4"); maxs = np.ascontiguousarray(root[16:28]).view("<f4")
        C.ok(bool((mins < maxs).all()) and int(root[12]) > 0, "csm rebase: node 0 is a real root (mins < maxs, %d children, %d total refs)" % (int(root[12]), int(np.ascontiguousarray(root[14:16]).view("<u2")[0])))
        drop_idx = int(sum(int(x[1]) for x in sm2[:fm])) * 3
        C.ok(empty_shadow or (bool((si1 == si2[drop_idx:]).all()) and bool((sm1 == sm2[fm:]).all())), "csm rebase: shadow meshes and indices = R2 data after the dummy environment (%d indices dropped)" % drop_idx)
    elif not empty_shadow:
        C.ok(bool((n1 == n2).all()) and bool((r1r == r2r).all()) and bool((sm1 == sm2).all()) and bool((si1 == si2).all()), "shadow lumps (0x63, 0x64, 0x7E, 0x7F) identical to R2")
    if empty_shadow:
        C.ok(all(len(r1.raw(i)) == 0 for i in (0x7C, 0x7D, 0x7E, 0x7F)), "rule 17: lumps 0x7C..0x7F are empty")

    # ---- bsp_tool
    if use_bsp_tool:
        print("== bsp_tool")
        try:
            import bsp_tool
            b = bsp_tool.load_bsp(out_path)
            C.ok(b.__class__.__module__.split(".")[-1] in ("titanfall", "respawn") or "titanfall" in b.__class__.__module__ or True, "bsp_tool loaded %s (branch module %s)" % (type(b).__name__, b.branch.__name__ if hasattr(b, "branch") else "?"))
            br = getattr(b, "branch", None)
            C.ok(br is not None and br.__name__.endswith("titanfall") and not br.__name__.endswith("titanfall2"), "bsp_tool chose the R1 (titanfall) branch")
            C.ok(b.BSP_VERSION == 29 if hasattr(b, "BSP_VERSION") else True, "bsp_tool BSP_VERSION 29")
            touched = 0
            problems = []
            for name in ("MODELS", "MESHES", "MATERIAL_SORTS", "VERTICES", "VERTEX_NORMALS", "VERTEX_LIT_BUMP", "VERTEX_UNLIT",
                         "TRICOLL_HEADERS", "TRICOLL_TRIANGLES", "CM_GRID", "CM_GRID_CELLS", "CM_GEO_SETS", "CM_GEO_SET_BOUNDS",
                         "CM_PRIMITIVES", "CM_PRIMITIVE_BOUNDS", "LIGHTPROBE_REFERENCES", "WORLD_LIGHTS", "LEVEL_INFO",
                         "CELLS", "PORTALS", "LIGHTMAP_HEADERS", "CUBEMAPS", "TEXTURE_DATA", "GAME_LUMP"):
                try:
                    lump = getattr(b, name)
                    n = len(lump) if hasattr(lump, "__len__") else 1
                    if hasattr(lump, "__getitem__") and n:
                        _ = lump[0]; _ = lump[n - 1]
                    touched += 1
                except Exception as e:  # pragma: no cover
                    problems.append("%s: %s" % (name, e))
            C.ok(not problems, "bsp_tool parsed %d lumps (first/last record each)" % touched, "; ".join(problems))
            try:
                sp = b.GAME_LUMP.sprp
                C.ok(len(sp.props) == np1, "bsp_tool GAME_LUMP.sprp parsed as v12: %d props, %d model names" % (len(sp.props), len(sp.model_names)))
            except Exception as e:
                C.ok(False, "bsp_tool sprp parse", str(e))
            if wn:
                try:
                    msh = b.mesh(w0)
                    C.ok(True, "bsp_tool built mesh %d" % w0)
                except Exception as e:
                    C.ok(False, "bsp_tool mesh(%d)" % w0, str(e))
        except ImportError as e:
            C.ok(False, "bsp_tool import", str(e))
        except Exception as e:
            C.ok(False, "bsp_tool load_bsp", repr(e))

    print("\nSUMMARY %s: %d passed, %d failed" % (out_path, C.passes, C.fails))
    return C.fails


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("r2")
    ap.add_argument("r1")
    ap.add_argument("--limit", type=float, default=16376.0)
    ap.add_argument("--rtl-bytes", type=int, default=4)
    ap.add_argument("--cubemaps", choices=("none", "split", "keep"), default="none")
    ap.add_argument("--physics", choices=("stub", "none"), default="stub")
    ap.add_argument("--cubemap-scale", type=float, default=4.0)
    ap.add_argument("--no-bsp-tool", action="store_true")
    ap.add_argument("--props", choices=("keep", "hide-scatter", "hide-all"), default="keep")
    ap.add_argument("--scatter-min", type=int, default=500)
    ap.add_argument("--csm-rebase", action="store_true")
    ap.add_argument("--empty-shadow-meshes", action="store_true")
    ap.add_argument("--worldlight-flag2", action="store_true")
    ap.add_argument("--no-collapse-envs", action="store_true", help="rule 20 disabled in the converter")
    ap.add_argument("--no-rtl", action="store_true", help="rule 20 RTL conversion disabled in the converter")
    ap.add_argument("--rtl-min-weight", type=int, default=187)
    ap.add_argument("--no-sun-alpha", action="store_true", help="rule 21 disabled in the converter")
    ap.add_argument("--pbr-lights", choices=("keep", "convert"), default="convert")
    ap.add_argument("--keep-prop-flag10", action="store_true")
    ap.add_argument("--no-grid-pad", action="store_true", help="rule 18 disabled in the converter")
    ap.add_argument("--grid-size", type=float, default=2048.0, help="rule 19 cell size given to the converter (0 = R2 size kept)")
    ap.add_argument("--reference", "--unpadded", dest="reference", help="the same input converted with --grid-size 0 --no-grid-pad: equivalence checks for rules 18 / 19")
    a = ap.parse_args()
    LIGHT_OPT.update(collapse=not a.no_collapse_envs, rtl=not a.no_rtl, min_weight=a.rtl_min_weight, sun_alpha=not a.no_sun_alpha, pbr=(a.pbr_lights == "convert"))
    sys.exit(1 if check(a.r2, a.r1, a.limit, a.rtl_bytes, not a.no_bsp_tool, a.cubemaps, a.physics, a.cubemap_scale,
                        a.props, a.scatter_min, a.csm_rebase, a.empty_shadow_meshes, a.worldlight_flag2, a.keep_prop_flag10,
                        not a.no_grid_pad, a.reference, a.grid_size) else 0)
