import io, os, re, shutil, struct, subprocess, sys, zipfile
import numpy as np

HERE = r'C:\Users\r3muxd\tmp\r2port\integrator'
CLI = HERE + r'\cli\bspdown_vendored.exe'
CORVPK = r'C:\NOTWINDOWS\Users\r3muxd\Downloads\corvpk.exe'
W = HERE + r'\wcc'


def lump(b, i):
    off, ln, _, _ = struct.unpack_from('<IIII', b, 16 + 16 * i)
    return b[off:off + ln]


def decode_hdr(d):
    a = np.frombuffer(d[96:], dtype=np.uint8)
    off = sum(6 * (64 >> m) ** 2 * 4 for m in range(6, 0, -1))
    t = a[off:off + 6 * 64 * 64 * 4].reshape(6, 64, 64, 4).astype(np.float64)
    return t[..., :3] / 255.0 * ((t[..., 3:4] + 1) / 64.0)


def analyse(name):
    shutil.rmtree(W, ignore_errors=True)
    os.makedirs(W)
    subprocess.run([CORVPK, 'unpack', r'D:\Titanfall2\vpk\englishclient_%s.bsp.pak000_dir.vpk' % name, W, '1'], capture_output=True)
    src = W + r'\maps\%s.bsp' % name
    out = W + r'\out.bsp'
    r = subprocess.run([CLI, src, out, '--cubemap-scale', '0.125'], capture_output=True, text=True)
    line = [l for l in (r.stdout + r.stderr).splitlines() if 'mip 0 pixels above' in l]
    b = open(out, 'rb').read()
    z = zipfile.ZipFile(io.BytesIO(lump(b, 0x28)))
    cubes = []
    default = None
    for n in z.namelist():
        if n.endswith('.hdr.vtf'):
            v = decode_hdr(z.read(n))
            if n.endswith('cubemapdefault.hdr.vtf'):
                default = v
            else:
                cubes.append(v)
    T = np.array(cubes) * 8.0  # R2 values: converted at scale 0.125, so up to 32 survive the 4.0 encoder clamp
    res = {'map': name, 'ncube': len(cubes), 'scale1_clamped_line': line[0][line[0].find('%') - 0:][-90:] if line else ''}
    lum = lambda x: 0.2126 * x[..., 0] + 0.7152 * x[..., 1] + 0.0722 * x[..., 2]
    rows = []
    for s in (1, 2, 3, 4, 6):
        S = T * s
        clip = (S.max(axis=-1) > 4.0)
        C = np.minimum(S, 4.0)
        energy = lum(C).mean() / max(lum(S).mean(), 1e-12)
        perface = clip.mean(axis=(0, 2, 3)) * 100
        faceE = [lum(C[:, f]).mean() / max(lum(S[:, f]).mean(), 1e-12) for f in range(6)]
        # default cube: mean of unclamped scaled then clamp (current) vs mean of clamped cubes
        cur = np.minimum(S.mean(axis=0), 4.0)
        alt = C.mean(axis=0)
        rel = float(np.abs(cur - alt).mean() / max(alt.mean(), 1e-9))
        rows.append((s, round(clip.mean() * 100, 3), [round(x, 2) for x in perface], round(energy, 4), [round(x, 3) for x in faceE], round(rel * 100, 2)))
    res['rows'] = rows
    res['face_mean_lum_R2'] = [round(float(lum(T[:, f]).mean()), 3) for f in range(6)]
    res['max_value'] = float(T.max())
    shutil.rmtree(W, ignore_errors=True)
    return res


if __name__ == '__main__':
    for m in sys.argv[1:]:
        r = analyse(m)
        print(r['map'], 'cubemaps', r['ncube'], 'max', r['max_value'], 'face mean lum', r['face_mean_lum_R2'])
        for row in r['rows']:
            print('   scale %d: clipped %.3f%% | per face %% %s | energy kept %.4f | per face kept %s | default avg-of-unclamped vs avg-of-clamped %.2f%%' % (row[0], row[1], row[2], row[3], row[4], row[5]))
        sys.stdout.flush()
