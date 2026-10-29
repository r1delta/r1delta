#!/usr/bin/env python3
"""Static check of the legacy dedicated server's playlist buffer patch sites.

Checks, against a real engine_ds.dll:
  * the module identity and every hooked entry / stock-limit constant that
    engine/playlist_loader.cpp verifies at runtime (the table is parsed out of that source file,
    so the source and this test can never drift apart silently);
  * that the playlist detours are installed from the engine_ds load notification and not from the
    server.dll one (a late install misses the VGUI dedicated's first load: the original bug);
  * that no shipped wpatch entry rewrites a hooked function;
  * that the CORE playlists file fits the server-info message with the margin the loader enforces.

usage: verify_playlist_loader.py [--engine-ds PATH] [--core DIR]
  defaults: PATH = <core>/bin/engine_ds.dll, DIR = $R1DELTA_CORE or S:\\game\\r1delta
exit status 0 = all checks passed.
"""
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
LOADER_CPP = os.path.join(REPO, 'engine', 'playlist_loader.cpp')
LOAD_CPP = os.path.join(REPO, 'engine', 'core', 'load.cpp')

failures = []


def check(ok, text):
    print(('PASS  ' if ok else 'FAIL  ') + text)
    if not ok:
        failures.append(text)


class Pe:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        d = self.data
        pe = struct.unpack_from('<I', d, 0x3C)[0]
        assert d[pe:pe + 4] == b'PE\0\0', 'not a PE file'
        nsec, = struct.unpack_from('<H', d, pe + 6)
        optsz, = struct.unpack_from('<H', d, pe + 20)
        self.timestamp, = struct.unpack_from('<I', d, pe + 8)
        self.size_of_image, = struct.unpack_from('<I', d, pe + 24 + 56)
        self.sections = []
        off = pe + 24 + optsz
        for i in range(nsec):
            name = d[off:off + 8].rstrip(b'\0').decode('latin1')
            vsize, vaddr, rawsize, rawptr = struct.unpack_from('<IIII', d, off + 8)
            self.sections.append((name, vaddr, max(vsize, rawsize), rawptr, rawsize))
            off += 40

    def bytes_at(self, rva, n):
        for name, vaddr, vsize, rawptr, rawsize in self.sections:
            if vaddr <= rva < vaddr + vsize:
                o = rawptr + rva - vaddr
                return self.data[o:o + n]
        return None


def parse_source():
    src = open(LOADER_CPP, encoding='utf-8').read()
    consts = {}
    for m in re.finditer(r'static constexpr\s+\w+\s+(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)u?(?:\s*\*\s*(\d+)\s*\*\s*(\d+))?\s*;', src):
        v = int(m.group(2), 0)
        if m.group(3):
            v *= int(m.group(3)) * int(m.group(4))
        consts[m.group(1)] = v

    def table(name):
        body = re.search(name + r'\[\]\s*=\s*\{(.*?)\n\};', src, re.S).group(1)
        rows = []
        for line in body.splitlines():
            m = re.match(r'\s*\{\s*"([^"]+)",\s*([^,]+?),\s*\{([^}]*)\}\s*\}', line)
            if not m:
                continue
            expr = m.group(2).strip()
            if '+' in expr:
                a, b = [x.strip() for x in expr.split('+')]
                rva = consts[a] + int(b, 0)
            else:
                rva = consts[expr] if expr in consts else int(expr, 0)
            rows.append((m.group(1), rva, bytes(int(x, 16) for x in m.group(3).split(','))))
        return rows

    return consts, table('kEntrySites'), table('kConstSites'), src


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--core', default=os.environ.get('R1DELTA_CORE', r'S:\game\r1delta'))
    ap.add_argument('--engine-ds')
    args = ap.parse_args()
    engine_ds = args.engine_ds or os.path.join(args.core, 'bin', 'engine_ds.dll')

    consts, entries, cons, src = parse_source()
    check(len(entries) >= 7 and len(cons) >= 1, 'parsed %d entry sites and %d constant sites from playlist_loader.cpp' % (len(entries), len(cons)))

    pe = Pe(engine_ds)
    check(pe.size_of_image == consts['kImageSize'],
          '%s image size 0x%X == kImageSize 0x%X (timestamp 0x%X)' % (os.path.basename(engine_ds), pe.size_of_image, consts['kImageSize'], pe.timestamp))
    for name, rva, expect in entries + cons:
        got = pe.bytes_at(rva, len(expect))
        check(got == expect, 'site %-40s engine_ds+0x%X: %s' % (name, rva, got.hex(' ') if got else 'unmapped'))

    # the stock limit the whole patch exists for must still be what the source claims
    cap = pe.bytes_at(consts['RVA_LOADER'] + 0xD0, 5)
    check(cap is not None and struct.unpack_from('<I', cap, 1)[0] == consts['STOCK_LIMIT'] + 1,
          'stock loader compare constant == STOCK_LIMIT + 1 (%d)' % (consts['STOCK_LIMIT'] + 1))

    # install point: engine_ds load notification, before its MH_EnableHook
    load = open(LOAD_CPP, encoding='utf-8').read()
    calls = [m.start() for m in re.finditer(r'PlaylistLoader_Init\s*\(', load)]
    check(len(calls) == 1, 'exactly one PlaylistLoader_Init call in load.cpp (found %d)' % len(calls))
    if len(calls) == 1:
        block_start = load.rfind('L"engine_ds.dll"', 0, calls[0])
        next_branch = load.find('else if (string_equal_size', calls[0])
        in_block = block_start != -1 and next_branch != -1 and block_start < calls[0] < next_branch
        enable = load.find('MH_EnableHook(MH_ALL_HOOKS)', calls[0])
        check(in_block and 0 < enable - calls[0] < 800,
              'PlaylistLoader_Init runs in the engine_ds.dll load branch, before that branch enables its hooks')
        do_server = load.find('do_server(const LDR_DLL_NOTIFICATION_DATA')
        check(do_server == -1 or not (do_server < calls[0] < load.find('\n}\n', do_server)),
              'PlaylistLoader_Init is not called from do_server (server.dll loads after the first playlist load)')

    # no wpatch entry may rewrite a hooked function or the stock constants
    hooked = [(rva, rva + 16) for name, rva, _ in entries if name in ('loader', 'raw_ptr', 'raw_size', 'comp')]
    hooked.append((consts['RVA_LOADER'], consts['RVA_LOADER'] + 0x1F2))
    for fname in ('r1delta.wpatch', 'r1delta_ds.wpatch', 'r1delta_noorigin.wpatch'):
        p = os.path.join(args.core, fname)
        if not os.path.exists(p):
            continue
        hits = []
        for line in open(p, encoding='utf-8', errors='replace'):
            line = line.strip()
            if line.startswith(';') or not line.lower().startswith('engine_ds.dll'):
                continue
            parts = line.split(',')
            try:
                # wpatch offsets are relative to the named section
                section = parts[1].strip().lstrip('.')
                base = next(va for (n, va, _, _, _) in pe.sections if n.lstrip('.') == section)
                off = base + int(parts[2].strip(), 16)
            except (IndexError, ValueError, StopIteration):
                continue
            hits.extend((off, line) for lo, hi in hooked if lo <= off < hi)
        check(not hits, '%s: no engine_ds patch inside a hooked playlist function' % fname)

    # the CORE playlists file against the message the loader enforces
    pl = os.path.join(args.core, 'playlists.txt')
    if os.path.exists(pl):
        size = os.path.getsize(pl)
        limit = consts['SERVERINFO_MESSAGE'] - 0x4000
        est = size / 3.5  # measured ratio on the shipped file is 3.76; 3.5 keeps a margin
        print('INFO  playlists.txt %d bytes (stock engine limit %d); estimated payload %d of %d' % (size, consts['STOCK_LIMIT'], est, limit))
        check(est < limit, 'playlists.txt estimated payload fits the server-info message with margin')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
