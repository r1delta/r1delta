"""Rule 23 check: far-origin props get a raised fade distance and nothing else changes.
usage: python check_far_origin_props.py [bspdown.exe] [r2.bsp] [expected far prop indices, comma separated]
Default: mp_crashsite3 (props 11205..11208: cave water and three waterfalls, origin 0 0 0, geometry thousands of units away).
Runs bspdown twice (default and --keep-prop-fade) and compares the two R1 files: only the fade fields (+0x24, +0x28) of the expected props
may differ, the new value must be 65536 (R2 fade -1), and every other byte of the file must be identical."""
import struct, subprocess, sys, os, tempfile
exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bspdown.exe')
r2 = sys.argv[2] if len(sys.argv) > 2 else r'C:\Users\r3muxd\tmp\r2port\maps\csl\r2\depot\r2dlc10\game\r2\maps\mp_crashsite3.bsp'
expect = [int(x) for x in (sys.argv[3] if len(sys.argv) > 3 else '11205,11206,11207,11208').split(',')]
tmp = tempfile.mkdtemp()
a, b = os.path.join(tmp, 'fixed.bsp'), os.path.join(tmp, 'keep.bsp')
subprocess.run([exe, r2, a], check=True, capture_output=True)
subprocess.run([exe, r2, b, '--keep-prop-fade'], check=True, capture_output=True)
da, db = open(a, 'rb').read(), open(b, 'rb').read()
fails = []
if len(da) != len(db): fails.append('file sizes differ %d vs %d' % (len(da), len(db)))
o, l, v, u = struct.unpack_from('<IIII', da, 16 + 16 * 0x23)
n = struct.unpack_from('<i', da, o)[0]
q = npr = None
for i in range(n):
    gid, fl, ver, off, ln = struct.unpack_from('<4sHHII', da, o + 4 + 16 * i)
    if gid == b'prps':
        p = off; nm = struct.unpack_from('<I', da, p)[0]; qq = p + 4 + nm * 128
        nl = struct.unpack_from('<I', da, qq)[0]; qq += 4 + nl * 2
        npr = struct.unpack_from('<I', da, qq)[0]; q = qq + 12
assert q is not None, 'no prps sub-lump'
allowed = set()
for k in expect:
    for f in range(8): allowed.add(q + k * 84 + 0x24 + f)
diff = [i for i in range(min(len(da), len(db))) if da[i] != db[i]]
stray = [i for i in diff if i not in allowed]
if stray: fails.append('%d bytes differ outside the expected prop fade fields, first at 0x%x' % (len(stray), stray[0]))
for k in expect:
    fa = struct.unpack_from('<ff', da, q + k * 84 + 0x24); fb = struct.unpack_from('<ff', db, q + k * 84 + 0x24)
    if fa[0] != fa[1]: fails.append('prop %d: fade min %g != max %g' % (k, fa[0], fa[1]))
    if fb[0] > 0: fails.append('prop %d: R2 fade is %g, this check expects -1 props' % (k, fb[0]))
    elif fa[0] != 65536.0: fails.append('prop %d: fade %g, expected 65536' % (k, fa[0]))
changed = sorted(set((i - q) // 84 for i in diff if q <= i < q + npr * 84))
if changed != sorted(expect): fails.append('changed props %s, expected %s' % (changed, sorted(expect)))
print('props', npr, 'changed', changed)
if fails:
    print('FAIL'); [print(' ', f) for f in fails]; sys.exit(1)
print('PASS: only', len(expect), 'far-origin props changed, fade 65536, rest of the file identical')
