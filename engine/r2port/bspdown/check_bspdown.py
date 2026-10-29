#!/usr/bin/env python3
"""Invariant checker for bspdown output.

usage: check_bspdown.py <in_r2.bsp> <out_r1.bsp> [--provider] [--no-bsp-tool] [options matching the conversion]

Independent re-implementation (numpy, no code shared with the C++ converter) of
the invariants the R1 engine loaders need plus consistency checks against the
R2 input. Exit status 0 if every check passed.

The checker defaults are the converter CLI defaults; pass the options the conversion used (--cubemaps none|split|keep, --csm-rebase,
--csm-last-env, --rtl-bake none|edge|all, --keep-prop-fade, ...). --provider = the options of the r1delta map provider
(r2_map_provider.cpp R2ConverterOptions): --cubemaps split --csm-rebase --rtl-bake edge --rtl-bake-vsm --rtl-bake-hot clip --rtl-bake-smooth 1 --rtl-bake-bm terrain, --csm-last-env on mp_coliseum / mp_coliseum_column.
"""
import argparse
import os
import re
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


def same_arr(a, b):
    """Equal shape and equal contents (a plain == raises when the shapes differ, e.g. on output made with other options)."""
    return a.shape == b.shape and bool((a == b).all())


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


def texdata_name(bsp, td):
    """Lower case, forward slash material name of texture data record td (None when the record or its name is out of range)."""
    texdata = bsp.raw(0x02)
    str_data = bytes(bsp.raw(0x2B))
    str_tab = np.frombuffer(bsp.raw(0x2C).tobytes(), dtype="<u4")
    if td < 0 or (td + 1) * 36 > len(texdata):
        return None
    name_i = struct.unpack_from("<i", texdata, td * 36 + 12)[0]
    if name_i < 0 or name_i >= len(str_tab) or int(str_tab[name_i]) >= len(str_data):
        return None
    return str_data[int(str_tab[name_i]):].split(b"\0", 1)[0].decode("ascii", errors="replace").replace("\\", "/").lower()


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


def quantize_rtl_visibility(bsp, lights, ids, weights, minimum, linear):
    """Independent atlas-footprint oracle: B is cone*visibility, not visibility alone."""
    minimum = max(0, min(255, int(minimum)))
    ids[(ids >= 0) & (weights == 0)] = -1
    types = np.ascontiguousarray(lights[:, 52:56]).view("<i4").ravel()
    valid = ids >= 0
    safe = np.maximum(ids, 0)
    ids[valid & (types[safe] == 1) & (weights < minimum)] = -1
    weak = valid & (types[safe] == 2) & (weights < minimum)
    active = np.flatnonzero(weak.any(axis=1))
    if not len(active):
        return {"needed": 0, "centres": 0, "borders": 0, "unreachable": 0, "aliases": 0, "disagreements": 0}
    bit = np.array((1, 2, 4, 8), np.uint8)
    masks = (weak[active] * bit).sum(axis=1).astype(np.uint8)
    lookup = np.full(len(ids), -1, np.int32)
    lookup[active] = np.arange(len(active))
    all_keep, any_keep = masks.copy(), np.zeros(len(active), np.uint8)
    coverage = np.zeros(len(active), np.uint8)
    unassigned = np.zeros(len(active), bool)
    aliases = np.zeros(len(active), bool)
    last_mesh = np.full(len(active), -1, np.int64)
    first_mesh = last_mesh.copy()
    best_distance = np.full(len(active), np.inf)
    best_any, best_all = any_keep.copy(), all_keep.copy()
    best_position = np.zeros((len(active), 3), np.float32)
    first_position = best_position.copy()
    best_alias = aliases.copy()
    mesh_dtype = np.dtype({"names": ["first", "triangles", "sort", "flags"],
                          "formats": ["<u4", "<u2", "<u2", "<u4"], "offsets": [0, 4, 22, 24], "itemsize": 28})
    sort_dtype = np.dtype({"names": ["page", "offset"], "formats": ["<i2", "<i4"], "offsets": [2, 8], "itemsize": 12})
    meshes = bsp.arr(0x50, mesh_dtype)
    sorts = bsp.arr(0x52, sort_dtype)
    indices = np.frombuffer(bsp.raw(0x4F).tobytes(), "<u2")
    positions = np.frombuffer(bsp.raw(3).tobytes(), "<f4").reshape(-1, 3)
    vertices = [np.frombuffer(bsp.raw(lump).tobytes(), np.uint8).reshape(-1, stride)
                for lump, stride in ((0x48, 36), (0x49, 44))]
    layout, first = [], 0
    for kind, w, h in bsp.arr(0x53, "<u4,<u2,<u2"):
        if kind == 1:
            w, h = int(w), int(h)
            grid = lookup[first:first + w * h].reshape(h, w)
            layout.append((first, w, h, grid))
            first += w * h
        else:
            layout.append(None)
    f = np.ascontiguousarray(lights).view("<f4").reshape(-1, 25)
    widths = (f[:, 15] - f[:, 16]).astype(np.float64)
    scale = np.full(len(lights), 100000.0)
    smooth = (f[:, 17] > 0.5) & (widths >= 1e-10)
    scale[smooth] = 1.0 / widths[smooth]
    selector = np.clip(f[:, 17].astype(np.float64) - 1, 0, 1)
    threshold = linear[minimum]
    affected_meshes = np.zeros(len(meshes), bool)
    for mi, mesh in enumerate(meshes):
        fmt = (int(mesh["flags"]) >> 9) & 3
        if fmt > 1 or not mesh["triangles"]:
            continue
        sort = sorts[int(mesh["sort"])]
        page = int(sort["page"])
        if page < 0 or layout[page] is None:
            continue
        first, w, h, grid = layout[page]
        if not np.any(grid >= 0):
            continue
        jj = indices[int(mesh["first"]):int(mesh["first"]) + int(mesh["triangles"]) * 3].astype(np.int64) + int(sort["offset"])
        records = vertices[fmt][jj]
        position_ids = records[:, :4].copy().view("<u4").ravel().reshape(-1, 3)
        triangle_uv = records[:, 20:28].copy().view("<f4").reshape(-1, 3, 2).astype(np.float64) * (w, h)
        touched = []
        for uv, pi in zip(triangle_uv, position_ids):
            if not np.isfinite(uv).all():
                raise ValueError("RTL visibility: non-finite lightmap UV")
            low, high = uv.min(axis=0), uv.max(axis=0)
            if high[0] < 0 or high[1] < 0 or low[0] >= w or low[1] >= h:
                continue
            lo = np.floor(np.clip(low, (0, 0), (w - 1, h - 1))).astype(int)
            hi = np.floor(np.clip(high, (0, 0), (w - 1, h - 1))).astype(int)
            yy, xx = np.nonzero(grid[lo[1]:hi[1] + 1, lo[0]:hi[0] + 1] >= 0)
            if not len(xx):
                continue
            xx, yy = xx + lo[0], yy + lo[1]
            si = grid[yy, xx]
            centres = np.column_stack((xx + 0.5, yy + 0.5))
            a, b, c = uv
            determinant = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
            orientation = -1.0 if determinant < 0 else 1.0
            intersects = np.ones(len(si), bool)
            for k in range(3):
                edge = uv[(k + 1) % 3] - uv[k]
                delta = centres - uv[k]
                side = orientation * (edge[0] * delta[:, 1] - edge[1] * delta[:, 0])
                intersects &= side + 0.5 * np.abs(edge).sum() >= -1e-9
            if not intersects.any():
                continue
            si, centres = si[intersects], centres[intersects]
            world = positions[pi].astype(np.float64)
            if not np.isfinite(world).all():
                raise ValueError("RTL visibility: non-finite world position")
            if abs(determinant) < 1e-12:
                cross = np.cross(world[1] - world[0], world[2] - world[0])
                if np.dot(cross, cross) > 1e-12:
                    unassigned[si] = True
                continue
            delta = centres - c
            u = ((b[1] - c[1]) * delta[:, 0] + (c[0] - b[0]) * delta[:, 1]) / determinant
            v = ((c[1] - a[1]) * delta[:, 0] + (a[0] - c[0]) * delta[:, 1]) / determinant
            bary = np.column_stack((u, v, 1 - u - v))
            distance = np.zeros(len(si))
            border = bary.min(axis=1) < -1e-8
            if border.any():
                distance[border] = np.inf
                for k in range(3):
                    nk = (k + 1) % 3
                    edge = uv[nk] - uv[k]
                    length2 = np.dot(edge, edge)
                    t = np.clip(((centres - uv[k]) @ edge) / length2, 0, 1) if length2 else np.zeros(len(si))
                    gap = centres - uv[k] - t[:, None] * edge
                    d2 = (gap * gap).sum(axis=1)
                    better = border & (d2 < distance)
                    bary[better] = 0
                    bary[better, k] = 1 - t[better]
                    bary[better, nk] = t[better]
                    distance[better] = d2[better]
            p = bary @ world
            keep = np.zeros(len(si), np.uint8)
            for s in range(4):
                has = (masks[si] & (1 << s)) != 0
                li = ids[active[si[has]], s]
                delta = p[has] - f[li, :3]
                cosine = (delta * f[li, 6:9]).sum(axis=1) / np.sqrt(np.maximum((delta * delta).sum(axis=1), 1.0))
                cone = np.clip((cosine - f[li, 16]) * scale[li], 0, 1)
                cone += selector[li] * (cone * cone - cone)
                yes = linear[weights[active[si[has]], s]] >= threshold * cone
                keep[np.flatnonzero(has)[yes]] |= np.uint8(1 << s)
            new = last_mesh[si] != mi
            best_distance[si[new]] = np.inf
            last_mesh[si[new]] = mi
            touched.extend(si[new].tolist())
            closer = distance < best_distance[si] - 1e-9
            tie = np.abs(distance - best_distance[si]) <= 1e-9
            ci, ti = si[closer], si[tie]
            best_distance[ci] = distance[closer]
            best_any[ci] = best_all[ci] = keep[closer]
            best_position[ci] = p[closer].astype(np.float32)
            best_alias[ci] = False
            best_any[ti] |= keep[tie]
            best_all[ti] &= keep[tie]
            best_alias[ti] |= ((best_position[ti].astype(np.float64) - p[tie].astype(np.float32)) ** 2).sum(axis=1) > 0.0001
        ti = np.array(touched, np.int64)
        if not len(ti):
            continue
        fresh = first_mesh[ti] < 0
        first_mesh[ti[fresh]] = mi
        first_position[ti[fresh]] = best_position[ti[fresh]]
        aliases[ti] |= ((first_position[ti].astype(np.float64) - best_position[ti]) ** 2).sum(axis=1) > 0.0001
        aliases[ti] |= best_alias[ti]
        affected_meshes[mi] |= bool(aliases[ti].any())
        affected_meshes[first_mesh[ti[aliases[ti]]]] = True
        coverage[ti] |= np.where(best_distance[ti] == 0, 1, 2).astype(np.uint8)
        any_keep[ti] |= best_any[ti]
        all_keep[ti] &= best_all[ti]
    if unassigned.any():
        raise ValueError("RTL visibility: reachable texels have non-invertible UVs, no visibility fallback")
    for s in range(4):
        reject = ((masks & (1 << s)) != 0) & ((coverage == 0) | ((all_keep & (1 << s)) == 0))
        ids[active[reject], s] = -1
    return {"needed": len(active), "centres": int(((coverage & 1) != 0).sum()),
            "borders": int(((coverage != 0) & ((coverage & 1) == 0)).sum()),
            "unreachable": int((coverage == 0).sum()), "aliases": int(aliases.sum()),
            "affected_meshes": int(affected_meshes.sum()),
            "disagreements": sum(int(((((any_keep ^ all_keep) & (1 << s)) != 0) & (coverage != 0)).sum()) for s in range(4))}


RTL_VISIBILITY_CUTOFF = 187  # fixed sRGB static-visibility cutoff of rule 20 (about half visible), applied after cone normalization
LIGHT_OPT = {"collapse": True, "rtl": True, "pbr": True, "sun_alpha": True, "sky_scale": 0.45, "sun_scale": 1.0, "pbr_scale": 1.0,
             "bake": 0, "bake_edge_min": 0.0,  # bake: --rtl-bake none 0 / edge 1 / all 2 (rule 20f)
             "bake_bm": 2, "bake_smooth": 0, "bake_hot": 0, "bake_vsm": False}  # --rtl-bake-bm skip 2 / terrain 1 / none 0, --rtl-bake-smooth 0..2, --rtl-bake-hot keep 0 / clip 1, --rtl-bake-vsm
CSM_OPT = {"last_env": False}  # --csm-last-env: the converter kept the last environment record instead of the largest
PROP_OPT = {"keep_fade": False}  # --keep-prop-fade: rule 23 off
CUBE_OPT = {"cma_gain": 1.0, "min_scale": 0.25}  # rule 13b: per-cubemap scale = clamp(gain * CMA, min_scale, --cubemap-scale)
PROVIDER_CSM_LAST_ENV_MAPS = ("mp_coliseum", "mp_coliseum_column")  # r2_map_provider.cpp ReadConvertedBsp: no cloudmask, the dummy light_environment stays last


def spot_cone(f, ids, p):
    """Cone term of the R2 lights `ids` (rows of the float32 record view f) at the points p (n,3 float64), the function the real-time light
    B plane is stored with (point lights have no cone: the callers only use it for type 2)."""
    delta = p - f[ids, 0:3].astype(np.float64)
    d2 = (delta * delta).sum(axis=1)
    dot = (delta * f[ids, 6:9].astype(np.float64)).sum(axis=1)
    width = (f[ids, 15] - f[ids, 16]).astype(np.float64)
    exponent = f[ids, 17].astype(np.float64)
    scale = np.where((exponent <= 0.5) | (width < 1e-10), 100000.0, 1.0 / np.where(width < 1e-10, 1.0, width))
    cosine = dot / np.sqrt(np.maximum(d2, 1.0))
    cone = np.clip((cosine - f[ids, 16].astype(np.float64)) * scale, 0.0, 1.0)
    return cone + np.clip(exponent - 1.0, 0.0, 1.0) * (cone * cone - cone)


def terrain_like_name(name):
    """Rule 20f --rtl-bake-bm terrain: some token of the (lower case, forward slash) material path, split at / and _, names ground."""
    for tok in re.split(r"[/_]", name):
        if not tok or tok == "dirty":
            continue
        if tok in ("ter", "sand", "patchy") or any(root in tok for root in ("grass", "mud", "rock", "cliff", "riverbed", "dirt")):
            return True
    return False


def rtl_bake_receivers(r2, pages, need):
    """Rule 20f receivers. For every lightmap texel t with need[t]: the surface point and normal the texel stands for. A texel takes the
    nearest point (barycentric, clamped to the triangle edge for gutter texels, at most 1.5 texels away) of the nearest triangle of any
    lit mesh whose lightmap chart (UV extent + 2 texels) reaches it; the earlier triangle wins ties. Meshes whose material name ends in _bm
    (layered terrain, RTL-blind in R1) are not receivers unless LIGHT_OPT["bake_bm"] says so: 2 = every _bm material is skipped (default),
    1 = only terrain-like names, 0 = none. Returns (reached bool (T,), position float32 (T,3), normal float32 (T,3),
    number of _bm meshes skipped)."""
    T = len(need)
    best = np.full(T, np.inf, np.float32)
    rpos = np.zeros((T, 3), np.float32)
    rnrm = np.zeros((T, 3), np.float32)
    m2 = r2.arr(0x50, MESH_DT_R2)
    so2 = r2.arr(0x52, SORT_DT)
    names = mesh_material_names(r2, m2, so2)
    idx = r2.arr(0x4F, "<u2")
    pos = r2.arr(0x03, "<f4").reshape(-1, 3)
    nrm = r2.arr(0x1E, "<f4").reshape(-1, 3)
    vtx = {f_: np.frombuffer(r2.raw(VTX_LUMP[f_]).tobytes(), np.uint8).reshape(-1, VTX_STRIDE[f_]) for f_ in (0, 1)}
    sat = {}
    for h, pg in enumerate(pages):
        if pg is None:
            continue
        first, w, hh = pg
        a = need[first:first + w * hh].reshape(hh, w).astype(np.int64)
        s = np.zeros((hh + 1, w + 1), np.int64)
        s[1:, 1:] = a.cumsum(0).cumsum(1)
        sat[h] = s
    skipped = 0
    for mi in range(len(m2)):
        mesh = m2[mi]
        fmt = (int(mesh["fl"]) >> 9) & 3
        nt = int(mesh["nt"])
        if fmt > 1 or not nt:
            continue
        sort = so2[int(mesh["ms"])]
        lm = int(sort["lm"])
        if lm < 0 or lm >= len(pages) or pages[lm] is None:
            continue
        if names[mi].endswith("_bm") and (LIGHT_OPT["bake_bm"] == 2 or (LIGHT_OPT["bake_bm"] == 1 and terrain_like_name(names[mi]))):
            skipped += 1
            continue
        first, w, hh = pages[lm]
        s = sat[lm]
        if not s[-1, -1]:
            continue
        jj = idx[int(mesh["fmi"]):int(mesh["fmi"]) + 3 * nt].astype(np.int64) + int(sort["vo"])
        rec = vtx[fmt][jj]
        uv = rec[:, 20:28].copy().view("<f4").reshape(nt, 3, 2).astype(np.float64) * (float(w), float(hh))
        if not np.isfinite(uv).all():
            raise ValueError("RTL bake: non-finite lightmap UV")
        pi = rec[:, 0:4].copy().view("<u4").ravel().reshape(nt, 3)
        ni = rec[:, 4:8].copy().view("<u4").ravel().reshape(nt, 3)
        lo, hi = uv.min(axis=1), uv.max(axis=1)
        det = (uv[:, 1, 1] - uv[:, 2, 1]) * (uv[:, 0, 0] - uv[:, 2, 0]) + (uv[:, 2, 0] - uv[:, 1, 0]) * (uv[:, 0, 1] - uv[:, 2, 1])
        x0 = np.floor(np.clip(lo[:, 0] - 2, 0.0, w - 1)).astype(np.int64)
        x1 = np.floor(np.clip(hi[:, 0] + 2, 0.0, w - 1)).astype(np.int64)
        y0 = np.floor(np.clip(lo[:, 1] - 2, 0.0, hh - 1)).astype(np.int64)
        y1 = np.floor(np.clip(hi[:, 1] + 2, 0.0, hh - 1)).astype(np.int64)
        count = s[y1 + 1, x1 + 1] - s[y0, x1 + 1] - s[y1 + 1, x0] + s[y0, x0]
        live = ~((hi[:, 0] < 0) | (hi[:, 1] < 0) | (lo[:, 0] >= w) | (lo[:, 1] >= hh)) & (np.abs(det) >= 1e-12) & (count > 0)
        needp = need[first:first + w * hh].reshape(hh, w)
        for ti in np.flatnonzero(live):
            u = uv[ti]
            yy, xx = np.nonzero(needp[y0[ti]:y1[ti] + 1, x0[ti]:x1[ti] + 1])
            yy = yy + y0[ti]
            xx = xx + x0[ti]
            px, py = xx + 0.5, yy + 0.5
            d = det[ti]
            b0 = ((u[1, 1] - u[2, 1]) * (px - u[2, 0]) + (u[2, 0] - u[1, 0]) * (py - u[2, 1])) / d
            b1 = ((u[2, 1] - u[0, 1]) * (px - u[2, 0]) + (u[0, 0] - u[2, 0]) * (py - u[2, 1])) / d
            bary = np.stack([b0, b1, 1.0 - b0 - b1], axis=1)
            dist = np.zeros(len(px))
            outside = bary.min(axis=1) < -1e-8
            if outside.any():
                cand = np.empty((3, len(px)))
                ts = np.empty((3, len(px)))
                for k in range(3):
                    nk = (k + 1) % 3
                    ex, ey = u[nk, 0] - u[k, 0], u[nk, 1] - u[k, 1]
                    l2 = ex * ex + ey * ey
                    t = np.clip(((px - u[k, 0]) * ex + (py - u[k, 1]) * ey) / l2, 0.0, 1.0) if l2 else np.zeros(len(px))
                    gx, gy = px - u[k, 0] - t * ex, py - u[k, 1] - t * ey
                    cand[k] = gx * gx + gy * gy
                    ts[k] = t
                kb = cand.argmin(axis=0)
                col = np.arange(len(px))
                dist = np.where(outside, cand[kb, col], 0.0)
                nb = np.zeros_like(bary)
                nb[col, kb] = 1.0 - ts[kb, col]
                nb[col, (kb + 1) % 3] = ts[kb, col]
                bary = np.where(outside[:, None], nb, bary)
            tex = first + yy * w + xx
            take = (dist <= 2.25) & ~(dist >= best[tex].astype(np.float64) - 1e-9)
            if not take.any():
                continue
            tex, bary = tex[take], bary[take]
            best[tex] = dist[take].astype(np.float32)
            wp = pos[pi[ti]].astype(np.float64)
            wn = nrm[ni[ti]].astype(np.float64)
            nvec = np.zeros((len(tex), 3))
            for k in range(3):
                acc = np.zeros(len(tex), np.float32)  # float32 accumulation, like the converter's float receiver array
                for m in range(3):
                    acc = acc + (bary[:, m] * wp[m, k]).astype(np.float32)
                    nvec[:, k] = nvec[:, k] + bary[:, m] * wn[m, k]
                rpos[tex, k] = acc
            nl = np.sqrt((nvec * nvec).sum(axis=1))
            rnrm[tex] = np.where((nl > 1e-9)[:, None], nvec / np.where(nl > 1e-9, nl, 1.0)[:, None], 0.0).astype(np.float32)
    return np.isfinite(best), rpos, rnrm, skipped


def apply_rtl_bake(r2, raw_f, ty, fl, out_f, ofl, new_of, v1, tex_orig, tex_old, weights, sky, mode, edge_min, linear):
    """Independent model of rule 20f (--rtl-bake edge|all). Mutates tex_old (baked slots removed) and sky (plane A of the lightmap,
    lump 0x62), returns statistics.
    A real-time point/spot light is a candidate when its FINAL record is still flag 0x8 at an RTL byte (array index - v1 <= 254). Only
    lights WITH R2 flag 0x10 are baked (R1 makes dynamic shadow maps for flag 0x8 lights without 0x10, those keep the RTL path), and not
    when more than a quarter of their lit slots would exceed the lightmap's 4.0 energy cap; edge mode needs >= edge_min (8) 'edge' texels.
    A baked light adds colour * attenuation(final polynomial) * facing * linear(B) to plane A (energy encoded as rgb * (alpha + 1) / 64)."""
    n = len(ty)
    hdr = r2.arr(0x53, "<u4,<u2,<u2")
    pages, first = [], 0
    for t, w, h in hdr:
        if t == 1 and int(w) * int(h):
            pages.append((first, int(w), int(h)))
            first += int(w) * int(h)
        else:
            pages.append(None)
    texels = first
    nw = new_of
    final_ok = ((ty == 1) | (ty == 2)) & ((fl & 8) != 0) & (nw >= 0) & (nw - v1 >= 0) & (nw - v1 <= 254)
    final_ok[final_ok] = (ofl[nw[final_ok]] & 8) != 0
    ok = np.flatnonzero(final_ok)
    F = np.zeros((n, 25), np.float32)
    F[ok] = out_f[nw[ok]]
    org = F[:, 0:3].astype(np.float64)
    inten = F[:, 3:6].astype(np.float64)
    radius, c0, c1, c2 = (F[:, 18].astype(np.float64), F[:, 19].astype(np.float64), F[:, 20].astype(np.float64), F[:, 21].astype(np.float64))
    den = radius * c1 + radius * radius * c2
    rational = (radius > 0) & (np.abs(den) > 1e-12)
    safe = np.where(rational, den, 1.0)
    A = np.where(rational, -1.0000005 * (radius * c1) / safe, 0.0)
    Bq = np.where(rational, -1.0000005 * (radius * radius * c2) / safe, 0.0)

    def attenuation(i, d2, d):
        r2_ = radius[i] * radius[i]
        sn = np.minimum(1.0, np.maximum(1.0 / np.where(rational[i], r2_, 1.0), d2 / np.where(rational[i], r2_, 1.0)))
        return np.clip(np.where(rational[i], 1.0 + A[i] * np.sqrt(sn) + Bq[i] * sn, c0[i] + c1[i] * d + c2[i] * d2), 0.0, 1.0)

    cand = (tex_orig >= 0) & final_ok[np.clip(tex_orig, 0, n - 1)]
    need = cand.any(axis=1)
    reached, rpos, rnrm, skipped = rtl_bake_receivers(r2, pages, need)
    tx = np.zeros(texels, np.int64); tyc = np.zeros(texels, np.int64); tw = np.zeros(texels, np.int64); th = np.zeros(texels, np.int64)
    tbase = np.zeros(texels, np.int64)
    for pg in pages:
        if pg is None:
            continue
        f0, w, h = pg
        tx[f0:f0 + w * h] = np.tile(np.arange(w), h)
        tyc[f0:f0 + w * h] = np.repeat(np.arange(h), w)
        tw[f0:f0 + w * h] = w
        th[f0:f0 + w * h] = h
        tbase[f0:f0 + w * h] = 8 * f0 + 4 * (np.arange(w * h))
    lw = linear[weights]
    total = np.zeros(n, np.int64); hot = np.zeros(n, np.int64); edge = np.zeros(n, np.int64)
    p64 = rpos.astype(np.float64)
    nrm64 = rnrm.astype(np.float64)
    for s in range(4):
        t = np.flatnonzero(cand[:, s] & reached & (lw[:, s] > 0))
        if not len(t):
            continue
        i = tex_orig[t, s]
        w_ = lw[t, s]
        np.add.at(total, i, 1)
        h = org[i] - p64[t]
        hd2 = (h * h).sum(axis=1)
        hd = np.sqrt(np.maximum(hd2, 1e-12))
        hf = np.clip((h * nrm64[t]).sum(axis=1) / hd, 0.0, 1.0)
        ha = attenuation(i, hd2, hd)
        np.add.at(hot, i, (inten[i].max(axis=1) * ha * hf * w_ > 3.5).astype(np.int64))
        cn = np.ones(len(t))
        spot = ty[i] == 2
        if spot.any():
            cn[spot] = spot_cone(raw_f, i[spot], p64[t[spot]])
        live = cn >= 0.25
        vis = np.minimum(1.0, w_ / np.where(live, cn, 1.0))
        partial = live & (vis < 0.8)
        np.add.at(edge, i[partial], 1)
        hard = live & ~partial
        ti_, ii_ = t[hard], i[hard]
        found = np.zeros(len(ti_), bool)
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = tx[ti_] + dx, tyc[ti_] + dy
            inside = (nx >= 0) & (ny >= 0) & (nx < tw[ti_]) & (ny < th[ti_])
            nt = np.where(inside, ti_ + dx + dy * tw[ti_], 0)
            inside &= reached[nt]
            q = p64[nt]
            dd = np.sqrt(((q - org[ii_]) ** 2).sum(axis=1))
            inside &= ~((radius[ii_] > 0) & (dd > radius[ii_]))
            ncn = np.ones(len(ti_))
            sp = inside & (ty[ii_] == 2)
            if sp.any():
                ncn[sp] = spot_cone(raw_f, ii_[sp], q[sp])
            inside &= ncn >= 0.25
            dn = (rnrm[ti_, 0] * rnrm[nt, 0] + rnrm[ti_, 1] * rnrm[nt, 1]) + rnrm[ti_, 2] * rnrm[nt, 2]  # float32 products and sums, like the converter
            inside &= dn.astype(np.float64) >= 0.9
            nwt = np.zeros(len(ti_))
            for ss in range(4):
                nwt = np.where(tex_orig[nt, ss] == ii_, lw[nt, ss], nwt)
            found |= inside & (nwt < 0.05 * ncn)
        np.add.at(edge, ii_[found], 1)
    emin = int(edge_min) if edge_min > 0 else 8
    vsm = ((fl & 0x10) == 0) & (not LIGHT_OPT["bake_vsm"])
    too_hot = (total > 0) & (hot * 4 > total) & (not LIGHT_OPT["bake_hot"])
    bake = final_ok & ~vsm & ~too_hot & (np.ones(n, bool) if mode == 2 else edge >= emin)
    smooth_k = min(max(int(LIGHT_OPT["bake_smooth"]), 0), 2)
    kern = [2.0, 1.0] if smooth_k == 1 else [6.0, 4.0, 1.0]

    def smooth_weights(t, i, own):
        """Binomial low-pass of the linear visibility of light i over the same-surface neighbour texels of t (reached, normal dot >= 0.9,
        within 4 units of the texel's plane; a neighbour without the light counts as 0 inside the light radius and is left out beyond it);
        float32 products and sums for the geometry tests and double accumulation in the converter's (dy, dx) order."""
        acc = np.zeros(len(t))
        wacc = np.zeros(len(t))
        for dy in range(-smooth_k, smooth_k + 1):
            for dx in range(-smooth_k, smooth_k + 1):
                kw = kern[abs(dx)] * kern[abs(dy)]
                if dx == 0 and dy == 0:
                    acc = acc + kw * own
                    wacc = wacc + kw
                    continue
                inside = (tx[t] + dx >= 0) & (tyc[t] + dy >= 0) & (tx[t] + dx < tw[t]) & (tyc[t] + dy < th[t])
                nt = np.where(inside, t + dx + dy * tw[t], 0)
                ok = inside & reached[nt]
                dn = (rnrm[t, 0] * rnrm[nt, 0] + rnrm[t, 1] * rnrm[nt, 1]) + rnrm[t, 2] * rnrm[nt, 2]
                ok &= dn.astype(np.float64) >= 0.9
                off = (rnrm[t, 0] * (rpos[nt, 0] - rpos[t, 0]) + rnrm[t, 1] * (rpos[nt, 1] - rpos[t, 1])) + rnrm[t, 2] * (rpos[nt, 2] - rpos[t, 2])
                ok &= np.abs(off).astype(np.float64) <= 4.0
                wn = np.zeros(len(t))
                found = np.zeros(len(t), bool)
                for ss in range(4):
                    hit = ~found & (tex_orig[nt, ss] == i)
                    wn = np.where(hit, lw[nt, ss], wn)
                    found |= hit
                ex = rpos[nt, 0].astype(np.float64) - org[i, 0]
                ey = rpos[nt, 1].astype(np.float64) - org[i, 1]
                ez = rpos[nt, 2].astype(np.float64) - org[i, 2]
                beyond = (radius[i] > 0) & (ex * ex + ey * ey + ez * ez > radius[i] * radius[i])
                ok &= found | ~beyond
                acc = acc + np.where(ok, kw * wn, 0.0)
                wacc = wacc + np.where(ok, kw, 0.0)
        return np.where(wacc > 0, acc / np.where(wacc > 0, wacc, 1.0), own)

    # accumulate in slot order 0..3, encode plane A
    add = np.zeros((texels, 3))
    anyb = np.zeros(texels, bool)
    baked_slots = unreached = 0
    for s in range(4):
        i = tex_orig[:, s]
        sel = cand[:, s] & bake[np.clip(i, 0, n - 1)]
        t = np.flatnonzero(sel)
        wv = lw[t, s].copy()
        if smooth_k > 0 and len(t):
            rr = reached[t]
            if rr.any():
                wv[rr] = smooth_weights(t[rr], i[t[rr]], wv[rr])
        zero = t[wv <= 0]
        tex_old[zero, s] = -1
        keep = wv > 0
        t, wv = t[keep], wv[keep]
        r_ = reached[t]
        unreached += int((~r_).sum())
        t, wv = t[r_], wv[r_]
        i = i[t]
        h = org[i] - p64[t]
        d2 = (h * h).sum(axis=1)
        d = np.sqrt(d2)
        far = d >= 1e-6
        t, i, h, d2, d, wv = t[far], i[far], h[far], d2[far], d[far], wv[far]
        facing = np.clip((h * nrm64[t]).sum(axis=1) / d, 0.0, 1.0)
        k = attenuation(i, d2, d) * facing * wv
        add[t] += inten[i] * k[:, None]
        tex_old[t, s] = -1
        anyb[t] = True
        baked_slots += len(t)
    t = np.flatnonzero(anyb)
    skyarr = np.frombuffer(sky, np.uint8)
    px = skyarr[tbase[t][:, None] + np.arange(4)].astype(np.float64)
    lin = np.where(px[:, :3] / 255.0 <= 0.04045, px[:, :3] / 255.0 / 12.92, ((px[:, :3] / 255.0 + 0.055) / 1.055) ** 2.4)
    e = lin * ((px[:, 3:4] + 1) / 64.0) + add[t]
    peak = np.maximum(e.max(axis=1), 1.0 / 64)
    clipped = peak > 4.0
    e = np.where(clipped[:, None], e * (4.0 / peak)[:, None], e)
    alpha = np.clip(np.ceil(np.minimum(peak, 4.0) * 64.0) - 1, 0, 255)
    v = np.clip(np.minimum(e, 4.0) / ((alpha + 1) / 64.0)[:, None], 0.0, 1.0)
    srgb = np.where(v <= 0.0031308, v * 12.92, 1.055 * v ** (1 / 2.4) - 0.055)
    skyarr[tbase[t][:, None] + np.arange(3)] = np.floor(255.0 * srgb + 0.5).astype(np.uint8)
    skyarr[tbase[t] + 3] = alpha.astype(np.uint8)
    return {"lights_baked": int(bake.sum()), "lights_kept": int((final_ok & ~bake).sum()), "edge_min": emin, "candidate_slots": int(cand.sum()),
            "baked_slots": baked_slots, "texels_changed": int(len(t)), "clipped": int(clipped.sum()), "unreached": unreached,
            "bm_meshes": skipped, "baked_ids": np.flatnonzero(bake)}


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
        ambient_slot, sun_slot = max(E - 1, 0), max(S - 1, 0)
        if E == S:
            for slot in reversed(range(E)):
                sun = raw[E + slot].view("<f4")
                template = (np.all(np.abs(sun[3:6] - 3.0) < 1e-3)
                            and np.all(np.abs(sun[6:9] - (-0.33544, 0.52557, -0.78183)) < 2e-3))
                if not template:
                    ambient_slot = sun_slot = slot
                    break
        order = ([ambient_slot] if E else []) + ([E + sun_slot] if S else [])
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
    have = len(rtl2) and texels and len(rtl2) == texels * 9 and len(pal) and len(r2.raw(0x7A)) % 126 == 0
    usage = np.zeros(n, np.float64)
    tex_old = None
    sky = bytearray(r2.raw(0x62).tobytes())
    sun_alpha = bool(LIGHT_OPT["sun_alpha"] and have and len(sky) == texels * 8)
    linear_visibility = np.array([v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4 for v in (np.arange(256) / 255.0)])
    lut = (255 - np.floor(255.0 * linear_visibility + 0.5)).astype(np.uint8)
    if have:
        npal = len(pal) // 63
        tex_old = np.full((texels, 4), -1, np.int64)
        source_weights = np.empty((texels, 4), np.uint8)
        off = 0
        t0 = 0
        skyoff = 0
        rb = np.frombuffer(rtl2.tobytes(), np.uint8)
        for w, h in pages:
            m = w * h
            A = rb[off:off + 4 * m].view("<u4")
            B = rb[off + 4 * m:off + 8 * m].reshape(m, 4)
            source_weights[t0:t0 + m] = B
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
                tex_old[t0:t0 + m, s] = np.where(okk, wo, -1)
            t0 += m
    flagged_pos = [k for k in range(head, len(order)) if ty[order[k]] in (1, 2) and (fl[order[k]] & 8)]
    source_intensity = np.ascontiguousarray(raw[:, 12:24]).view("<f4").reshape(-1, 3)
    source_attenuation = np.ascontiguousarray(raw[:, 76:88]).view("<f4").reshape(-1, 3)
    source_radius = np.ascontiguousarray(raw[:, 72:76]).view("<f4").ravel()

    def pbr_intensity_scale(i):
        c_, l_, q_ = [float(x) for x in source_attenuation[i]]
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
        d50 = np.float32(best) if best > 0 else (source_radius[i] / np.float32(3.0) if source_radius[i] > 0 else np.float32(100.0))
        return np.float32(LIGHT_OPT["pbr_scale"]) * np.float32(2.0) / (d50 * d50)

    reordered = False
    do_rtl = bool(LIGHT_OPT["rtl"] and have)
    visibility_info = None
    tex_orig = tex_old.copy() if tex_old is not None else None  # slots before the visibility quantizer (rule 20f bakes from these)
    if do_rtl:
        visibility_info = quantize_rtl_visibility(r2, raw, tex_old, source_weights,
                                                   RTL_VISIBILITY_CUTOFF, linear_visibility)
        for s in range(4):
            retained = tex_old[:, s] >= 0
            usage += np.bincount(tex_old[retained, s], weights=linear_visibility[source_weights[retained, s]], minlength=n)
    if do_rtl and flagged_pos and flagged_pos[-1] - v1 > 254:
        ids = [order[p] for p in flagged_pos]
        for i in ids:
            scale = pbr_intensity_scale(i) if LIGHT_OPT["pbr"] and (fl[i] & 0x20) and not (fl[i] & 4) else 1.0
            usage[i] *= float(scale) * sum(float(c) * w for c, w in zip(source_intensity[i], (0.2126, 0.7152, 0.0722)))
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
    for k in range(len(order)):
        t = oty[k]
        f = int(ofl[k])
        if t in (3, 5):
            f = 0
            g = np.float32(LIGHT_OPT["sky_scale"] if t == 5 else LIGHT_OPT["sun_scale"])
            if g != np.float32(1.0):
                inten[k] = (inten[k] * g).astype(np.float32)
        else:
            if LIGHT_OPT["pbr"] and (f & 0x20) and not (f & 4):
                scale = pbr_intensity_scale(order[k])
                inten[k] = (inten[k] * scale).astype(np.float32)
                f |= 4
            f &= ~(0x20 | 0x40)
            if t in (1, 2) and (f & 8) and k >= 255:
                f &= ~8
            if wl_flag2 and t in (1, 2):
                f |= 2
        ofl[k] = f
    out[:, 12:24] = inten.view("u1").reshape(-1, 12)
    out[:, 88:92] = ofl.astype("<i4").view("u1").reshape(-1, 4)
    bake_info = None
    if do_rtl and LIGHT_OPT["bake"] and len(sky) == texels * 8:
        bake_info = apply_rtl_bake(r2, np.ascontiguousarray(raw).view("<f4").reshape(n, 25), ty, fl,
                                   np.ascontiguousarray(out).view("<f4").reshape(len(out), 25), ofl, new_of, v1, tex_orig, tex_old,
                                   source_weights, sky, LIGHT_OPT["bake"], LIGHT_OPT["bake_edge_min"], linear_visibility)
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
    if do_rtl:
        slot = tex_old.copy()
        world = np.where(slot >= 0, new_of[np.clip(slot, 0, n - 1)], -1)
        nwv = world - v1
        final_realtime = (ofl[np.clip(world, 0, len(order) - 1)] & 8) != 0
        nwv = np.where((slot >= 0) & (nwv >= 0) & (nwv <= 254) & final_realtime, nwv, 255)
        nwv.sort(axis=1)  # 255 (none) last, ascending
        rtl_out = nwv.astype(np.uint8).tobytes()
    return out.tobytes(), bytes(pr), rtl_out, {"E": E, "S": S, "head": head, "v1": v1, "reordered": reordered, "sky": bytes(sky), "sun_alpha": sun_alpha, "visibility": visibility_info, "bake": bake_info}

def expected_prop_fade(r2, origin, fade, keep):
    """Independent model of rule 23: returns (fade distance float32 per prop, number of far-origin props, props without usable bounds).
    World bounds of a prop = union of the bounds (lump 0x79, 32 byte records parallel to the cell object references 0x78) of every
    reference whose id is propBase + prop index, propBase = (levelinfo word 2 + 63) & ~63. A prop whose origin is farther from the centre of
    those bounds than their half-diagonal + 256 gets R2 fade + origin-to-centre distance (65536 when the R2 value is <= 0)."""
    n = len(origin)
    li = bytes(r2.raw(0x7B))
    refs = r2.arr(0x78, "<u2")
    rb = r2.raw(0x79)
    if len(li) < 12 or len(rb) != len(refs) * 32:
        return fade.copy(), 0, n
    base = (struct.unpack_from("<I", li, 8)[0] + 63) & ~63
    ids = refs.astype(np.int64) - base
    sel = (refs >= base) & (ids < n)
    bb = np.frombuffer(rb.tobytes(), "<f4").reshape(-1, 8)[sel]
    mn = np.full((n, 3), np.inf, np.float32)
    mx = np.full((n, 3), -np.inf, np.float32)
    np.minimum.at(mn, ids[sel], bb[:, 0:3])
    np.maximum.at(mx, ids[sel], bb[:, 4:7])
    has = np.zeros(n, bool)
    has[ids[sel]] = True
    has &= np.isfinite(mn).all(axis=1) & np.isfinite(mx).all(axis=1) & ~(np.abs(mn) > 1e6).any(axis=1) & ~(np.abs(mx) > 1e6).any(axis=1)
    with np.errstate(invalid="ignore", over="ignore"):  # props without finite bounds (+-inf records) are excluded through `has`
        centre = 0.5 * (mn.astype(np.float64) + mx.astype(np.float64))
        off = origin.astype(np.float64) - centre
        half = np.sqrt((0.25 * (mx.astype(np.float64) - mn.astype(np.float64)) ** 2).sum(axis=1))
        dist = np.sqrt((off * off).sum(axis=1))
        far = has & (dist > half + 256.0)
        raised = np.where(fade > 0, fade + dist.astype(np.float32), np.float32(65536.0)).astype(np.float32)
    out = fade.copy() if keep else np.where(far, raised, fade).astype(np.float32)
    return out, int(far.sum()), int((~has).sum())


# Rule 22 / rule 24 target materials (vcalpha.cpp kOpaqueVertexAlphaMaterials / kEmitterGlowMaterials; keep in sync with r2tex).
OPAQUE_VERTEX_ALPHA_MATERIALS = frozenset((
    "world/beacon/beacon_metal_panel_grey_02/beacon_metal_panel_grey_02_vc", "world/concrete/concrete_broken_surface",
    "world/concrete/concrete_dam_01", "world/concrete/dam_concrete_01", "world/metal/ext_building_panel_imc_02",
    "world/metal/ext_building_panel_imc_03", "world/metal/metal_wall_panels_clean_vc", "world/ola/sewer_lab_trim_02",
    "world/ola/sewer_lab_trim_04", "world/ola/sewer_lab_trim_05", "world/ola/sewer_padded_ceiling_panels",
    "world/ola/sewer_rubber_floor_panel_01", "world/signs/beacon_tileable_decal_01", "world/timeshift/concrete/concrete_gap_wall_01",
    "world/timeshift/rock/ts_rockwall01_vc"))
EMITTER_GLOW_MATERIALS = frozenset(("world/vr_training/vr_light_trim_01_totrans",))


def expected_vertex_edits(r2):
    """Rules 22 and 24 on the R2 data: for every vertex lump type, the sorted unique vertex indices (sort vertex offset + mesh index) used by
    R2 meshes whose material is a rule 22 target (alpha forced to 255) and a rule 24 target (alpha stored as the albedo coordinate)."""
    m2 = r2.arr(0x50, MESH_DT_R2)
    so2 = r2.arr(0x52, SORT_DT)
    names = mesh_material_names(r2, m2, so2)
    idx = r2.arr(0x4F, "<u2")
    found = {t: ([], []) for t in range(4)}
    for mi, m in enumerate(m2):
        nt = int(m["nt"])
        which = 0 if names[mi] in OPAQUE_VERTEX_ALPHA_MATERIALS else 1 if names[mi] in EMITTER_GLOW_MATERIALS else -1
        if not nt or which < 0:
            continue
        fmi = int(m["fmi"])
        if fmi + 3 * nt > len(idx):
            continue
        found[(int(m["fl"]) >> 9) & 3][which].append(idx[fmi:fmi + 3 * nt].astype(np.int64) + int(so2[int(m["ms"])]["vo"]))
    res = {}
    for t in range(4):
        n0 = len(r2.raw(VTX_LUMP[t])) // VTX_STRIDE[t]
        pair = []
        for lst in found[t]:
            v = np.unique(np.concatenate(lst)) if lst else np.zeros(0, np.int64)
            pair.append(v[v < n0])
        res[t] = tuple(pair)
    return res


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
    bk = einfo["bake"]
    if bk is not None:
        print("  note  rule 20f model (%s): %d lights baked, %d kept as RTL (edge texel minimum %d); %d candidate slots, %d slots baked into plane A over %d texels (%d above the encodable 4.0), %d baked slots without a receiver kept as RTL; %d _bm meshes skipped as receivers" %
              ("all" if LIGHT_OPT["bake"] == 2 else "edge", bk["lights_baked"], bk["lights_kept"], bk["edge_min"], bk["candidate_slots"], bk["baked_slots"], bk["texels_changed"], bk["clipped"], bk["unreached"], bk["bm_meshes"]))
    if elr is not None:
        C.ok(len(rtl) == texels * 4 and bytes(rtl) == elr, "RTL lump = independent decode of R2 9 byte texels (4 bytes per texel, slot = world index - %d, min weight %d, %s%s)" % (einfo["v1"], RTL_VISIBILITY_CUTOFF, "reordered by contribution potential" if einfo["reordered"] else "original order", "; rule 20f: %d lights baked into lightmap plane A leave the lists" % bk["lights_baked"] if bk is not None else ""))
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
    C.ok(bytes(r1.raw(0x62)) == einfo["sky"], "lightmap 0x62 = R2 with plane B alpha = 255 - 255 * linear(RTL sun slot visibility) on texels with a sun entry%s%s" % (" (rule 21)" if einfo["sun_alpha"] else " (unchanged)", "; plane A = R2 + the %d baked lights' colour * attenuation * facing * visibility on %d texels, energy re-encoded (rule 20f)" % (bk["lights_baked"], bk["texels_changed"]) if bk is not None else ""))
    C.ok(bytes(r1.raw(0x65)) == elp, "light probes (0x65) = R2 probes with static light slots remapped (R2 world - %d -> R1 world - %d)" % (einfo["E"], einfo["v1"]))

    # ---- world lights
    print("== world lights")
    w1 = r1.arr(0x36, np.dtype([("v", "u1", 100)]))["v"]
    C.ok(bytes(r1.raw(0x36)) == elw, "world lights = rule 20 model (%d R2 sky ambient + %d sun entries, %d head entries, v1 %d, %d lights)" % (einfo["E"], einfo["S"], einfo["head"], einfo["v1"], len(w1)))
    C.ok(len(w1) <= 4064, "world light count <= 4064")
    final_types = np.ascontiguousarray(w1[:, 52:56]).view("<i4").ravel()
    final_flags = np.ascontiguousarray(w1[:, 88:92]).view("<i4").ravel()
    local = (final_types == 1) | (final_types == 2)
    C.ok(not bool(np.any((final_flags[255:] & 8) & local[255:])),
         "final local lights at indices 255/256 and beyond have flag 0x8 cleared")
    if len(rtl) == texels * 4:
        packed = np.frombuffer(rtl, np.uint8).reshape(-1, 4)
        refs = np.unique(packed)
        refs = refs[refs != 255].astype(np.int64) + einfo["v1"]
        in_range = bool(np.all(refs < len(w1)))
        enabled = in_range and bool(np.all(local[refs] & ((final_flags[refs] & 8) != 0)))
        C.ok(enabled and bool(np.all(refs < 255)),
             "every emitted RTL byte resolves to a FINAL flag-0x8 point/spot below conservative index 255")
        holes = (np.cumsum(packed == 255, axis=1) != 0) & (packed != 255)
        C.ok(not bool(np.any(holes)), "RTL lists terminate at the first 0xFF without later references")
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
    exp_fade, far_n, noref_n = expected_prop_fade(r2, f32(p2, 0, 3), f32(p2, 0x24, 1).ravel(), PROP_OPT["keep_fade"])
    C.ok(bool((f32(p1, 0x24, 1).ravel().view("<u4") == exp_fade.view("<u4")).all()) and bool((f32(p1, 0x28, 1).ravel().view("<u4") == exp_fade.view("<u4")).all()),
         "fade min/max = R2 value at +0x24" + ("" if PROP_OPT["keep_fade"] else
         "; rule 23: raised for the %d props whose origin is farther than half-diagonal + 256 from the union of their cell object-reference bounds (R2 <= 0 -> 65536, else R2 + origin-to-centre distance); %d props have no usable cell references" % (far_n, noref_n)))
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
    edits = expected_vertex_edits(r2)
    for t_ in range(4):
        stride = VTX_STRIDE[t_]
        o2 = r2.raw(VTX_LUMP[t_]).reshape(-1, stride)
        converted = r1.raw(VTX_LUMP[t_]).reshape(-1, stride)
        n0 = len(o2)
        exp_rgba = o2[:, 16:20].copy()
        exp_uv = o2[:, 8:16].copy()
        v22, v24 = edits[t_]
        exp_rgba[v22, 3] = 255
        if len(v24):
            u = ((o2[v24, 19].astype(np.float32) + np.float32(0.5)) / np.float32(256.0)).astype("<f4")
            exp_uv[v24, 0:4] = u.view(np.uint8).reshape(-1, 4)
            exp_uv[v24, 4:8] = np.frombuffer(np.float32(0.5).tobytes(), np.uint8)
            exp_rgba[v24, 3] = 255
        rgba_ok = len(converted) >= n0 and bool((converted[:n0, 16:20] == exp_rgba).all()) and bool((converted[:n0, 8:16] == exp_uv).all())
        C.ok(rgba_ok, "vertex lump 0x%02X: original RGBA (+16) and albedo UV (+8) preserved except rule 22 (alpha 255 on %d vertices of the vertex-tinted materials) and rule 24 (glow ramp coordinate (a + 0.5) / 256, 0.5 and alpha 255 on %d vertices)" % (VTX_LUMP[t_], len(v22), len(v24)))
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
    cm_dt = np.dtype([("o", "<i4", 3), ("u", "<u4")])
    cm_ent = r1.arr(0x2A, cm_dt)
    cm2 = r2.arr(0x2A, cm_dt)
    frames = len(cm2)
    try:
        r2_names = zipfile.ZipFile(io.BytesIO(r2.raw(0x28).tobytes())).namelist()
    except Exception:  # pragma: no cover
        r2_names = []
    src_names = [n for n in r2_names if n.endswith("/cubemaps.hdr.vtf")]
    if zf is not None and ncube:
        names = {i.filename: i for i in zf.infolist()}
        C.ok(all(i.compress_type == 0 for i in zf.infolist()), "all pakfile entries are stored (method 0)")
        d0 = src_names[0].rsplit("/", 1)[0] if src_names else ""
        C.ok(len(src_names) == 1 and d0.startswith("materials/maps/"), "the R2 pakfile has exactly one cubemaps.hdr.vtf, in a materials/maps/<map> directory (%s)" % d0)
        extra_cm = cm_ent[frames:]
        C.ok(len(cm_ent) >= frames and bytes(cm_ent[:frames].tobytes()) == bytes(cm2.tobytes()), "rule 13c: the first %d cubemap records (0x2A) are the R2 records" % frames)
        known_u = {int(u) for u in cm2["u"]}
        origins = [tuple(int(v) for v in o) for o in cm_ent["o"]]
        C.ok(all(int(u) in known_u for u in extra_cm["u"]) and len(set(origins)) == len(origins),
             "rule 13c: the %d extra cubemap samples copy the trailing word of an R2 record and every sample origin is unique (the c<x>_<y>_<z> names)" % len(extra_cm))
        kept = {n for n in r2_names if n not in src_names}
        expect = {d0 + "/cubemapdefault.vtf", d0 + "/cubemapdefault.hdr.vtf"}
        for o in cm_ent["o"]:
            base = "%s/c%d_%d_%d" % (d0, o[0], o[1], o[2])
            expect |= {base + ".vtf", base + ".hdr.vtf"}
        cube_files = set(expect)
        # rule 13d: every material sort that carries a cubemap needs the patch material <map dir>/<material>_<x>_<y>_<z>.vmt
        patch = {}
        if cubemaps == "split":
            env_dir = d0[len("materials/"):] + "/"
            for s in r1.arr(0x52, SORT_DT):
                ci, td = int(s["cm"]), int(s["td"])
                mat = texdata_name(r1, td)
                if ci < 0 or ci >= frames or mat is None:
                    continue
                o = cm2[ci]["o"]
                path = "%s/%s_%d_%d_%d.vmt" % (d0, mat, o[0], o[1], o[2])
                patch.setdefault(path, ("\"patch\"\r\n{\r\n\t\"include\"\t\t\"materials/%s.vmt\"\r\n\t\"replace\"\r\n\t{\r\n\t\t\"$envmap\"\t\t\"%sc%d_%d_%d\"\r\n\t}\r\n}\r\n" % (mat, env_dir, o[0], o[1], o[2])).encode("ascii"))
        expect |= set(patch) | kept
        if cubemaps == "split":
            C.ok(set(names) == expect and all(zf.read(p) == c for p, c in patch.items()),
                 "rule 13d: pakfile holds exactly cubemapdefault + c<x>_<y>_<z> .vtf/.hdr.vtf for each of the %d 0x2A entries + %d patch materials <material>_<x>_<y>_<z>.vmt (include the base material, replace $envmap) for the (material, cubemap) pairs of the sorts%s (no R2 cubemaps.hdr.vtf)" % (ncube, len(patch), " + %d other R2 entries" % len(kept) if kept else ""),
                 "extra %s missing %s" % (sorted(set(names) - expect)[:3], sorted(expect - set(names))[:3]))
            C.ok(all(n.startswith(d0 + "/") for n in names if n not in kept), "rule 13d: every generated entry lives under %s (patch materials in the sub-directory of their material name)" % d0)
        else:
            C.ok(set(names) == expect and all(n.rsplit("/", 1)[0] == d0 for n in names if n not in kept),
                 "all generated entries live in the one directory %s: cubemapdefault + c<x>_<y>_<z> .vtf/.hdr.vtf for each of the %d 0x2A entries%s (no R2 cubemaps.hdr.vtf, no patch materials)" % (d0, ncube, " + %d other R2 entries" % len(kept) if kept else ""),
                 "extra %s missing %s" % (sorted(set(names) - expect)[:3], sorted(expect - set(names))[:3]))
        def vh(d):
            hs, w, h, fl, fr, fmt, mips, start = struct.unpack_from("<I", d, 12)[0], *struct.unpack_from("<HH", d, 16), struct.unpack_from("<I", d, 20)[0], struct.unpack_from("<H", d, 24)[0], struct.unpack_from("<I", d, 0x34)[0], d[0x38], 0
            return dict(sig=d[:4], ver=struct.unpack_from("<II", d, 4), hs=hs, w=w, h=h, flags=fl, frames=fr, fmt=fmt, mips=mips, depth=struct.unpack_from("<H", d, 0x3E)[0], nres=struct.unpack_from("<I", d, 0x44)[0])
        okh = True; bad = []
        for n in sorted(cube_files):
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
        err = float(np.abs(dflt - np.mean(cubes[:frames], axis=0)).mean() / max(float(dflt.mean()), 1e-9))
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
            # rule 13b: per-cubemap scale = clamp(gain * CMA[f], min_scale, cubemap_scale) from the CMA resource (one float per frame)
            cma = np.zeros(0, "<f4")
            for i in range(struct.unpack_from("<I", sd, 0x44)[0]):
                if sd[0x50 + 8 * i:0x50 + 8 * i + 3] == b"CMA":
                    co = struct.unpack_from("<I", sd, 0x50 + 8 * i + 4)[0]
                    cn = min(struct.unpack_from("<I", sd, co)[0] // 4, (len(sd) - co - 4) // 4)
                    cma = np.frombuffer(sd[co + 4:co + 4 + 4 * cn], "<f4")
            f32_ = np.float32
            scales = [float(min(f32_(cubemap_scale), max(f32_(CUBE_OPT["min_scale"]), f32_(CUBE_OPT["cma_gain"]) * cma[f])))
                      if CUBE_OPT["cma_gain"] > 0 and f < len(cma) and np.isfinite(cma[f]) and cma[f] > 0 else float(f32_(cubemap_scale)) for f in range(fr2)]
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
                    ref = np.minimum(buf.reshape(64, k, 64, k, 3).astype(np.float64).mean((1, 3)) * scales[f], 4.0)
                    ae = float(np.abs(cubes[f][face] - ref).mean())
                    e = float(np.abs(cubes[f][face] - ref).sum() / max(ref.sum(), 1e-12)); row.append(e)
                    worst_abs = max(worst_abs, ae)
                    if ae > 2e-4: worst = max(worst, e)  # relative error only counts where the absolute error exceeds the 8 bit step at alpha 4 (3.1e-4 / 2)
                tab.append(row)
            C.ok(worst < 0.02, "R1 decode of the hdr cubemaps equals the independent BC6H decode * the per-cubemap scale clamp(%g * CMA, %g, %g) = %s (4x4 box, clamp 4.0), same frame/face order; per face: mean absolute error <= 2e-4 (worst %.1e, half the 8 bit step at alpha 4 is 1.5e-4) or relative error < 2%% (worst counted %.2f%%)" % (CUBE_OPT["cma_gain"], CUBE_OPT["min_scale"], cubemap_scale, " ".join("%.3g" % s_ for s_ in scales[:12]), worst_abs, 100 * worst))
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
    for i in (0x00, 0x01, 0x02, 0x0E if False else 0x18, 0x24, 0x2B, 0x2C, 0x53, 0x5C, 0x5D, 0x5E, 0x5F, 0x63 if False else (0x64 if not csm_rebase else 0x00), 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x78, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F):  # 0x2A: rule 13c appends samples, checked with the pakfile
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
        # independent re-derivation of the chosen environment: largest node + ref + mesh content, ties to the later record
        score = [int(x[3] - x[0]) + int(x[4] - x[1]) + int(x[5] - x[2]) for x in env2]
        pick = len(env2) - 1 if CSM_OPT["last_env"] else max(range(len(env2)), key=lambda i: (score[i], i))
        e = env2[pick]; fn, fr, fm = int(e[0]), int(e[1]), int(e[2]); ln, lr, lm = int(e[3]), int(e[4]), int(e[5])
        mesh_word = int(np.frombuffer(bytes(r2.raw(0x7B)[8:12]), "<u4")[0])
        nprops = int(np.frombuffer(bytes(r2.raw(0x7B)[12:16]), "<u4")[0])
        obj_total = ((mesh_word + 63) & ~63) + nprops
        rr_old = r2r[fr:lr]
        keep = rr_old < obj_total
        pref = np.concatenate([[0], np.cumsum(keep)]).astype(np.int64)
        C.ok(len(n1) == ln - fn and len(r1r) == int(keep.sum()) and (empty_shadow or len(sm1) == lm - fm), "csm rebase: environment %d of %d chosen %s; %d nodes, %d refs (%d dropped beyond meshes + props = %d), %d shadow meshes after dropping %d / %d / %d leading entries" % (pick, len(env2), "as the last record (--csm-last-env)" if CSM_OPT["last_env"] else "by content", len(n1), len(r1r), int((~keep).sum()), obj_total, len(sm1), fn, fr, fm))
        exp = n2[fn:ln].copy()
        nc = exp[:, 12]
        fc = np.ascontiguousarray(exp[:, 28:30]).view("<u2").ravel().astype(np.int64)
        fc2 = np.where(nc > 0, fc - fn, fc).astype(np.uint16)
        exp[:, 28:30] = fc2.view("u1").reshape(-1, 2)
        # firstObjRef is only meaningful for nodes whose subtree has references (total > 0); a ref-less node (the dummy environment's single
        # node, kept with --csm-last-env on mp_coliseum / mp_coliseum_column, whose first ref index lies outside the kept refs) is compared
        # on everything except that field.
        own = exp[:, 13].astype(np.int64)
        tot = np.ascontiguousarray(exp[:, 14:16]).view("<u2").ravel().astype(np.int64)
        has_refs = tot > 0
        f0 = np.where(has_refs, np.ascontiguousarray(exp[:, 30:32]).view("<u2").ravel().astype(np.int64) - fr, 0)
        C.ok(bool((f0 >= 0).all()) and bool((f0 + tot <= len(rr_old)).all()), "csm rebase: every node's object reference range lies inside the chosen environment's refs")
        f0 = np.clip(f0, 0, len(rr_old))
        n1_ok = n1.shape == exp.shape
        exp[:, 30:32] = np.where(has_refs, pref[f0], np.ascontiguousarray(n1[:, 30:32]).view("<u2").ravel() if n1_ok else 0).astype(np.uint16).view("u1").reshape(-1, 2)
        exp[:, 13] = np.where(has_refs, pref[np.minimum(f0 + own, len(rr_old))] - pref[f0], own).astype(np.uint8)
        exp[:, 14:16] = np.where(has_refs, pref[np.minimum(f0 + tot, len(rr_old))] - pref[f0], tot).astype(np.uint16).view("u1").reshape(-1, 2)
        C.ok(same_arr(n1, exp), "csm rebase: node array = the chosen environment's R2 nodes, child indices lowered by %d, object ranges re-derived after dropping out-of-range refs" % fn)
        C.ok(same_arr(r1r, rr_old[keep]), "csm rebase: object references = R2 refs in range")
        root = n1[0].copy(); mins = np.ascontiguousarray(root[0:12]).view("<f4"); maxs = np.ascontiguousarray(root[16:28]).view("<f4")
        empty_root = bool((mins > maxs).all()) and len(n1) == 1 and int(root[12]) == 0 and int(root[13]) == 0  # mp_coliseum: the real record is a single empty (inverted +-FLT_MAX) node
        C.ok(empty_root or (bool((mins <= maxs).all()) and (len(n1) == 1 or int(root[12]) > 0)), "csm rebase: node 0 is the chosen environment's root (mins <= maxs, %d children, %d total refs%s)" % (int(root[12]), int(np.ascontiguousarray(root[14:16]).view("<u2")[0]), ", empty single-node tree" if empty_root else ""))
        drop_idx = int(sum(int(x[1]) for x in sm2[:fm])) * 3
        keep_idx = int(sum(int(x[1]) for x in sm2[fm:lm])) * 3
        C.ok(empty_shadow or (same_arr(si1, si2[drop_idx:drop_idx + keep_idx]) and same_arr(sm1, sm2[fm:lm])), "csm rebase: shadow meshes and indices = the chosen environment's R2 data (%d leading, %d trailing indices dropped)" % (drop_idx, len(si2) - drop_idx - keep_idx))
    elif not empty_shadow:
        C.ok(same_arr(n1, n2) and same_arr(r1r, r2r) and same_arr(sm1, sm2) and same_arr(si1, si2), "shadow lumps (0x63, 0x64, 0x7E, 0x7F) identical to R2")
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
    ap.add_argument("--csm-last-env", action="store_true", help="rule 17: converter kept the last environment record")
    ap.add_argument("--empty-shadow-meshes", action="store_true")
    ap.add_argument("--worldlight-flag2", action="store_true")
    ap.add_argument("--no-collapse-envs", action="store_true", help="rule 20 disabled in the converter")
    ap.add_argument("--no-rtl", action="store_true", help="rule 20 RTL conversion disabled in the converter")
    ap.add_argument("--no-sun-alpha", action="store_true", help="rule 21 disabled in the converter")
    ap.add_argument("--pbr-lights", choices=("keep", "convert"), default="convert")
    ap.add_argument("--sky-ambient-scale", type=float, default=0.45, help="rule 20e")
    ap.add_argument("--sun-scale", type=float, default=1.0, help="rule 20e")
    ap.add_argument("--pbr-scale", type=float, default=1.0, help="rule 20e")
    ap.add_argument("--keep-prop-flag10", action="store_true")
    ap.add_argument("--keep-prop-fade", action="store_true", help="rule 23 disabled in the converter (R2 prop fade distance copied for every prop)")
    ap.add_argument("--rtl-bake", choices=("none", "edge", "all"), default="none", help="rule 20f: the converter's --rtl-bake mode (provider default: edge)")
    ap.add_argument("--rtl-bake-edge-min", type=float, default=0.0, help="rule 20f: the converter's --rtl-bake-edge-min (0 = its default 8)")
    ap.add_argument("--rtl-bake-bm", choices=("skip", "terrain", "none"), default="skip", help="rule 20f: the converter's --rtl-bake-bm (_bm receivers: skip all / only terrain-like names / bake all)")
    ap.add_argument("--rtl-bake-smooth", type=int, choices=(0, 1, 2), default=0, help="rule 20f: the converter's --rtl-bake-smooth window (0 off, 1 = 3x3, 2 = 5x5)")
    ap.add_argument("--rtl-bake-hot", choices=("keep", "clip"), default="keep", help="rule 20f: the converter's --rtl-bake-hot")
    ap.add_argument("--rtl-bake-vsm", action="store_true", help="rule 20f: the converter's --rtl-bake-vsm (lights without R2 flag 0x10 are baked too)")
    ap.add_argument("--cubemap-cma-gain", type=float, default=1.0, help="rule 13b: the converter's --cubemap-cma-gain")
    ap.add_argument("--cubemap-min-scale", type=float, default=0.25, help="rule 13b: the converter's --cubemap-min-scale")
    ap.add_argument("--provider", action="store_true",
                    help="validate under the r1delta map provider's default options (r2_map_provider.cpp R2ConverterOptions): --cubemaps split --csm-rebase "
                         "--rtl-bake edge --rtl-bake-vsm --rtl-bake-hot clip --rtl-bake-smooth 1 --rtl-bake-bm terrain (DrydockSteps preset), plus --csm-last-env for mp_coliseum / mp_coliseum_column (taken from the input file name); every other option at the converter default")
    ap.add_argument("--no-grid-pad", action="store_true", help="rule 18 disabled in the converter")
    ap.add_argument("--grid-size", type=float, default=2048.0, help="rule 19 cell size given to the converter (0 = R2 size kept)")
    ap.add_argument("--reference", "--unpadded", dest="reference", help="the same input converted with --grid-size 0 --no-grid-pad: equivalence checks for rules 18 / 19")
    a = ap.parse_args()
    if a.provider:
        a.cubemaps, a.csm_rebase, a.rtl_bake = "split", True, "edge"
        a.rtl_bake_vsm, a.rtl_bake_hot, a.rtl_bake_smooth, a.rtl_bake_bm = True, "clip", 1, "terrain"
        if os.path.splitext(os.path.basename(a.r2))[0].lower() in PROVIDER_CSM_LAST_ENV_MAPS:
            a.csm_last_env = True
    CSM_OPT["last_env"] = a.csm_last_env
    PROP_OPT["keep_fade"] = a.keep_prop_fade
    CUBE_OPT.update(cma_gain=a.cubemap_cma_gain, min_scale=a.cubemap_min_scale)
    LIGHT_OPT.update(collapse=not a.no_collapse_envs, rtl=not a.no_rtl, sun_alpha=not a.no_sun_alpha, pbr=(a.pbr_lights == "convert"),
                     sky_scale=a.sky_ambient_scale, sun_scale=a.sun_scale, pbr_scale=a.pbr_scale,
                     bake={"none": 0, "edge": 1, "all": 2}[a.rtl_bake], bake_edge_min=a.rtl_bake_edge_min,
                     bake_bm={"skip": 2, "terrain": 1, "none": 0}[a.rtl_bake_bm], bake_smooth=a.rtl_bake_smooth,
                     bake_hot=1 if a.rtl_bake_hot == "clip" else 0, bake_vsm=a.rtl_bake_vsm)
    sys.exit(1 if check(a.r2, a.r1, a.limit, a.rtl_bytes, not a.no_bsp_tool, a.cubemaps, a.physics, a.cubemap_scale,
                        a.props, a.scatter_min, a.csm_rebase, a.empty_shadow_meshes, a.worldlight_flag2, a.keep_prop_flag10,
                        not a.no_grid_pad, a.reference, a.grid_size) else 0)
