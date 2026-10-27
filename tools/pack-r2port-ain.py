#!/usr/bin/env python3
"""Pack an R1 v54 AIN graph into the provider's <map>.ain.r2patch package.

Format (engine/filesystem/r2_map_patch_source.cpp, ReadAinPatch), little-endian:
  0  "R2AP"
  4  u16 package format version (2)
  6  u16 header length (52 + map name length + zero padding)
  8  u16 map name length
  10 u16 compression (1 = zstd)
  12 u32 uncompressed body length
  16 u32 compressed payload length
  20 32 bytes SHA-256 of the SOURCE Titanfall 2 BSP (maps/<map>.bsp exactly as read from the map's
     englishclient_<map>.bsp.pak000_dir.vpk, the same key the .r2patch physics patches use)
  52 map name (ASCII, no NUL)
  header_length: zstd frame of the AIN body (the .ain file from offset 12; the 12-byte
     version/mapVersion/crc header is rebuilt by the provider at load time from the live map version)

The key is the source BSP, so a package survives any change to the converter or entity rewrite. The
provider still only serves a package at developer 0 and when the body parses as a v54 graph. Format
version 1 packages (keyed to the converted BSP) are rejected; repack them with this tool.

  pack_ain.py pack  --map M --ain M.ain --source-bsp M.bsp --out M.ain.r2patch
  pack_ain.py check --map M --pkg M.ain.r2patch [--source-bsp M.bsp]
"""
import argparse
import hashlib
import os
import struct
import sys

import zstandard

MAX_BODY = 64 * 1024 * 1024
FORMAT_VERSION = 2


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


def check_source_bsp(path):
    """The key is the R2 BSP, not the converted one; refuse to hash anything else."""
    with open(path, "rb") as f:
        head = f.read(16)
    if len(head) < 16 or head[:4] != b"rBSP":
        raise Bad("%s is not an rBSP file" % path)
    version = struct.unpack_from("<i", head, 4)[0]
    if version != 37:
        raise Bad("%s is rBSP version %d; the key is the Titanfall 2 (version 37) source BSP, not a converted R1 BSP" % (path, version))


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.digest()


def pack(map_name, ain_path, source_bsp_path, out_path):
    with open(ain_path, "rb") as f:
        ain = f.read()
    if len(ain) < 16:
        raise Bad("ain too short")
    version, map_version, crc = struct.unpack_from("<iii", ain, 0)
    if version != 54:
        raise Bad("not a v54 graph: version %d" % version)
    body = ain[12:]
    nodes, links = validate_body(body)
    check_source_bsp(source_bsp_path)
    digest = sha256_file(source_bsp_path)
    name = map_name.encode("ascii")
    payload = zstandard.ZstdCompressor(level=19).compress(body)
    header_len = 52 + len(name)
    header = b"R2AP" + struct.pack("<HHHH II", FORMAT_VERSION, header_len, len(name), 1, len(body), len(payload))
    header += digest + name
    assert len(header) == header_len
    with open(out_path, "wb") as f:
        f.write(header + payload)
    return nodes, links, digest.hex(), len(header) + len(payload), map_version


def check(map_name, pkg_path, source_bsp_path):
    with open(pkg_path, "rb") as f:
        p = f.read()
    if len(p) < 52 or p[:4] != b"R2AP":
        raise Bad("bad magic")
    ver, hlen, nlen, comp, ulen, clen = struct.unpack_from("<HHHHII", p, 4)
    if ver != FORMAT_VERSION or comp != 1 or nlen != len(map_name) or hlen < 52 + nlen or hlen > len(p):
        raise Bad("bad header (format version %d, expected %d)" % (ver, FORMAT_VERSION))
    if clen != len(p) - hlen or not (0 < ulen <= MAX_BODY):
        raise Bad("bad lengths")
    if p[52:52 + nlen] != map_name.encode("ascii") or any(p[52 + nlen:hlen]):
        raise Bad("bad map name / reserved")
    body = zstandard.ZstdDecompressor().decompress(p[hlen:], max_output_size=ulen)
    if len(body) != ulen:
        raise Bad("decompressed length mismatch")
    nodes, links = validate_body(body)
    want = p[20:52]
    status = "source bsp hash not compared"
    if source_bsp_path:
        check_source_bsp(source_bsp_path)
        status = "source bsp hash match" if sha256_file(source_bsp_path) == want else "SOURCE BSP HASH MISMATCH"
    return nodes, links, want.hex(), status


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("pack", "check"):
        s = sub.add_parser(name)
        s.add_argument("--map", required=True)
        s.add_argument("--source-bsp", help="the R2 maps/<map>.bsp exactly as read from the map's directory VPK")
        if name == "pack":
            s.add_argument("--ain", required=True)
            s.add_argument("--out", required=True)
        else:
            s.add_argument("--pkg", required=True)
    a = ap.parse_args()
    try:
        if a.cmd == "pack":
            if not a.source_bsp:
                raise Bad("need --source-bsp")
            nodes, links, digest, size, mv = pack(a.map, a.ain, a.source_bsp, a.out)
            print("packed %s nodes=%d links=%d mapversion=%d source_bsp_sha256=%s package_bytes=%d" % (a.map, nodes, links, mv, digest, size))
        else:
            nodes, links, digest, status = check(a.map, a.pkg, a.source_bsp)
            print("package ok %s nodes=%d links=%d source_bsp_sha256=%s (%s)" % (a.map, nodes, links, digest, status))
    except Bad as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
