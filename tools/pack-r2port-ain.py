#!/usr/bin/env python3
"""Pack an R1 v54 AIN graph into the provider's <map>.ain.r2patch package.

Format (engine/filesystem/r2_map_patch_source.cpp, ReadAinPatch), little-endian:
  0  "R2AP"
  4  u16 package version (1)
  6  u16 header length (52 + map name length + zero padding)
  8  u16 map name length
  10 u16 compression (1 = zstd)
  12 u32 uncompressed body length
  16 u32 compressed payload length
  20 32 bytes SHA-256 of the exact CONVERTED bsp the provider serves (after any physics patch)
  52 map name (ASCII, no NUL)
  header_length: zstd frame of the AIN body (the .ain file from offset 12; the 12-byte
     version/mapVersion/crc header is rebuilt by the provider at load time)

The provider only serves a package at developer 0 and only when the BSP hash matches, so a
package is tied to one converter output. Rebuild it (ain-map.ps1) when the BSP bytes change.

  pack_ain.py pack  --map M --ain M.ain (--bsp converted.bsp | --root S:\\game\\r2-shader) --out M.ain.r2patch
  pack_ain.py check --map M --pkg M.ain.r2patch [--bsp converted.bsp | --root ROOT]
"""
import argparse
import glob
import hashlib
import os
import struct
import sys

import zstandard

MAX_BODY = 64 * 1024 * 1024


class Bad(Exception):
    pass


def validate_body(b):
    """Mirror of ValidateAinV54Body in the engine."""
    size = len(b)
    off = 0

    def need(n):
        if off + n > size:
            raise Bad("truncated at %d" % off)

    def count(elem):
        nonlocal off
        need(4)
        (c,) = struct.unpack_from("<i", b, off)
        off += 4
        if c < 0 or c * elem > size - off:
            raise Bad("implausible count %d at %d" % (c, off - 4))
        return c

    def skip(n, rec):
        nonlocal off
        need(n * rec)
        off += n * rec

    nodes = count(68)
    skip(nodes, 68)
    links = count(10)
    for _ in range(links):
        need(4)
        s, d = struct.unpack_from("<HH", b, off)
        if s >= nodes or d >= nodes:
            raise Bad("link endpoint out of range")
        off += 10
    skip(nodes, 4)
    need(2)
    (trav,) = struct.unpack_from("<H", b, off)
    off += 2
    if trav > 0x7FFF:
        raise Bad("traverse count")
    skip(trav, 20)
    for _ in range(5):
        skip(1, 4)
        need(4)
        bits, words = struct.unpack_from("<HH", b, off)
        off += 4
        if words != (bits + 31) // 32:
            raise Bad("hull zone word count")
        skip(words, 4)
    n = count(26)
    for _ in range(n):
        skip(1, 17)
        m = count(2)
        skip(m, 2)
        lm = count(2)
        skip(lm, 2)
        skip(1, 1)
    n = count(11)
    skip(n, 11)
    need(4)
    off += 4
    if off != size:
        raise Bad("trailing bytes: parsed %d of %d" % (off, size))
    return nodes, links


def find_converted_bsp(root, map_name):
    pattern = os.path.join(root, "r1delta", "r2port_cache", map_name + "-*", "maps", map_name + ".bsp")
    hits = sorted(glob.glob(pattern), key=os.path.getmtime)
    if not hits:
        raise Bad("no converted BSP in provider cache: " + pattern)
    return hits[-1]


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.digest()


def pack(map_name, ain_path, bsp_path, out_path):
    with open(ain_path, "rb") as f:
        ain = f.read()
    if len(ain) < 16:
        raise Bad("ain too short")
    version, map_version, crc = struct.unpack_from("<iii", ain, 0)
    if version != 54:
        raise Bad("not a v54 graph: version %d" % version)
    body = ain[12:]
    nodes, links = validate_body(body)
    digest = sha256_file(bsp_path)
    name = map_name.encode("ascii")
    payload = zstandard.ZstdCompressor(level=19).compress(body)
    header_len = 52 + len(name)
    header = b"R2AP" + struct.pack("<HHHH II", 1, header_len, len(name), 1, len(body), len(payload))
    header += digest + name
    assert len(header) == header_len
    with open(out_path, "wb") as f:
        f.write(header + payload)
    return nodes, links, digest.hex(), len(header) + len(payload), map_version


def check(map_name, pkg_path, bsp_path):
    with open(pkg_path, "rb") as f:
        p = f.read()
    if len(p) < 52 or p[:4] != b"R2AP":
        raise Bad("bad magic")
    ver, hlen, nlen, comp, ulen, clen = struct.unpack_from("<HHHHII", p, 4)
    if ver != 1 or comp != 1 or nlen != len(map_name) or hlen < 52 + nlen or hlen > len(p):
        raise Bad("bad header")
    if clen != len(p) - hlen or not (0 < ulen <= MAX_BODY):
        raise Bad("bad lengths")
    if p[52:52 + nlen] != map_name.encode("ascii") or any(p[52 + nlen:hlen]):
        raise Bad("bad map name / reserved")
    body = zstandard.ZstdDecompressor().decompress(p[hlen:], max_output_size=ulen)
    if len(body) != ulen:
        raise Bad("decompressed length mismatch")
    nodes, links = validate_body(body)
    want = p[20:52]
    status = "bsp hash not compared"
    if bsp_path:
        status = "bsp hash match" if sha256_file(bsp_path) == want else "BSP HASH MISMATCH"
    return nodes, links, want.hex(), status


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("pack", "check"):
        s = sub.add_parser(name)
        s.add_argument("--map", required=True)
        s.add_argument("--bsp")
        s.add_argument("--root")
        if name == "pack":
            s.add_argument("--ain", required=True)
            s.add_argument("--out", required=True)
        else:
            s.add_argument("--pkg", required=True)
    a = ap.parse_args()
    try:
        bsp = a.bsp
        if not bsp and a.root:
            bsp = find_converted_bsp(a.root, a.map)
        if a.cmd == "pack":
            if not bsp:
                raise Bad("need --bsp or --root")
            nodes, links, digest, size, mv = pack(a.map, a.ain, bsp, a.out)
            print("packed %s nodes=%d links=%d bsp=%s bspsha256=%s package_bytes=%d" % (a.map, nodes, links, os.path.basename(os.path.dirname(os.path.dirname(bsp))), digest, size))
        else:
            nodes, links, digest, status = check(a.map, a.pkg, bsp)
            print("package ok %s nodes=%d links=%d bspsha256=%s (%s)" % (a.map, nodes, links, digest, status))
    except Bad as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
