#!/usr/bin/env python3
"""Compare two Titanfall 1 studio v52 .mdl files (and their .vvd/.dx11.vtx/.phy checksums).

usage: mdl_compare.py STOCK.mdl OTHER.mdl

Prints the header fields that differ, bone count / name-prefix / per-bone flag and bind differences,
includes, cdtextures, the keyvalue block, and whether the mdl/vvd/vtx/phy checksums agree.
Used to diagnose the replacement pilot models (models/humans/{imc_pilot,mcor_pilot,pilot}/*): see docs/pilot-models.md.
Offsets follow the r1 studiohdr_t / mstudiobone_t (216 bytes) in rmdlconv studio.h.
"""
import os
import struct
import sys
from collections import Counter

BONE_SIZE = 216


def cstr(b, o):
    return b[o:b.index(b"\0", o)].decode("latin1")


def parse(path):
    b = open(path, "rb").read()
    if b[:4] != b"IDST" or struct.unpack_from("<i", b, 4)[0] != 52:
        raise SystemExit(f"{path}: not a studio v52 mdl")
    h = {"checksum": struct.unpack_from("<i", b, 8)[0] & 0xFFFFFFFF, "name": cstr(b, 12), "length": struct.unpack_from("<i", b, 76)[0]}
    names = ["numbones", "boneindex", "numbonecontrollers", "bonecontrollerindex", "numhitboxsets", "hitboxsetindex",
             "numlocalanim", "localanimindex", "numlocalseq", "localseqindex", "activitylistversion", "eventsindexed",
             "numtextures", "textureindex", "numcdtextures", "cdtextureindex", "numskinref", "numskinfamilies", "skinindex",
             "numbodyparts", "bodypartindex", "numlocalattachments", "localattachmentindex", "numlocalnodes", "localnodeindex",
             "localnodenameindex"]
    h.update(zip(names, struct.unpack_from("<%di" % len(names), b, 156)))
    (h["numikchains"], h["ikchainindex"], _, _, h["numposeparams"], h["poseparamindex"], h["surfacepropindex"],
     h["keyvalueindex"], h["keyvaluesize"], _, _) = struct.unpack_from("<11i", b, 284)
    h["numincludemodels"], h["includemodelindex"] = struct.unpack_from("<ii", b, 336)
    h["bonetablebynameindex"] = struct.unpack_from("<i", b, 364)[0]
    h["bones"] = []
    for i in range(h["numbones"]):
        o = h["boneindex"] + i * BONE_SIZE
        sz, parent = struct.unpack_from("<ii", b, o)
        flags = struct.unpack_from("<i", b, o + 160)[0]
        h["bones"].append({"name": cstr(b, o + sz), "parent": parent, "flags": flags,
                           "pos": struct.unpack_from("<3f", b, o + 32), "quat": struct.unpack_from("<4f", b, o + 44),
                           "p2b": struct.unpack_from("<12f", b, o + 96)})
    h["includes"] = [cstr(b, h["includemodelindex"] + i * 8 + struct.unpack_from("<i", b, h["includemodelindex"] + i * 8 + 4)[0])
                     for i in range(h["numincludemodels"])]
    h["cdtex"] = [cstr(b, struct.unpack_from("<i", b, h["cdtextureindex"] + i * 4)[0]) for i in range(h["numcdtextures"])]
    h["kv"] = b[h["keyvalueindex"]:h["keyvalueindex"] + h["keyvaluesize"]].decode("latin1") if h["keyvaluesize"] else ""
    return h


def side_checksums(mdl_path):
    base = mdl_path[:-4]
    out = {}
    for ext, fmt, off in ((".vvd", "<I", 8), (".dx11.vtx", "<I", 16), (".phy", "<I", 12)):
        p = base + ext
        out[ext] = struct.unpack_from(fmt, open(p, "rb").read(32), off)[0] if os.path.exists(p) else None
    return out


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    a, b = parse(sys.argv[1]), parse(sys.argv[2])
    print("header fields that differ:")
    for k in a:
        if k not in ("bones", "includes", "cdtex", "kv") and a[k] != b[k]:
            print(f"  {k}: {a[k]} -> {b[k]}")
    print(f"bones: {a['numbones']} -> {b['numbones']}")
    na, nb = [x["name"] for x in a["bones"]], [x["name"] for x in b["bones"]]
    n = min(len(na), len(nb))
    print(f"  same index+name prefix: {sum(1 for i in range(n) if na[i] == nb[i])} of {n}; names of A missing in B: {len([x for x in na if x not in set(nb)])}")
    print(f"  bones only in B: {len([x for x in nb if x not in set(na)])}")
    diff = Counter()
    for i in range(n):
        x, y = a["bones"][i], b["bones"][i]
        if x["name"] != y["name"]:
            continue
        for f in ("parent", "flags", "pos", "quat", "p2b"):
            if f in ("pos", "quat", "p2b"):
                if any(abs(u - v) > 1e-5 for u, v in zip(x[f], y[f])):
                    diff[f] += 1
            elif x[f] != y[f]:
                diff[f] += 1
    print("  per-bone differences over the shared prefix:", dict(diff))
    print("  flag values A:", {hex(k): v for k, v in Counter(x["flags"] for x in a["bones"]).items()})
    print("  flag values B:", {hex(k): v for k, v in Counter(x["flags"] for x in b["bones"]).items()})
    print("includes A:", a["includes"])
    print("includes B:", b["includes"])
    print("cdtextures equal:", a["cdtex"] == b["cdtex"])
    print("keyvalues A:", a["kv"][:200])
    print("keyvalues B:", b["kv"][:200])
    for tag, path, h in (("A", sys.argv[1], a), ("B", sys.argv[2], b)):
        s = side_checksums(path)
        print(f"checksums {tag}: mdl {h['checksum']:#x} " + " ".join(f"{k} {('%#x' % v) if v is not None else 'missing'}" for k, v in s.items())
              + ("  CONSISTENT" if all(v == h["checksum"] for v in s.values() if v is not None) else "  MISMATCH"))


if __name__ == "__main__":
    main()
