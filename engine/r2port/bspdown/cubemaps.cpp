// Origin: C:\Users\r3muxd\tmp\r2port\bspdown. Bundled in r1delta under GPL-3.0.
// Rule 13: per-cubemap textures in the pakfile (lump 0x28).
//
// R2 keeps all cubemaps of a map in one BC6H array texture, materials/maps/<map>/cubemaps.hdr.vtf
// (VTF 7.5, 256x256, one frame per lump 0x2A entry, 6 faces). R1 (Mod_LoadCubemapSamples) asks for
// <map>/c<x>_<y>_<z>[.hdr] per 0x2A entry (x,y,z = the three int32 of the entry), then
// <map>/cubemapdefault, then engine/defaultcubemap. This stage decodes the BC6H array and writes
// the files R1 looks for into a rebuilt store-only zip:
//   c<x>_<y>_<z>.hdr.vtf  RGBA8888 64x64, 6 faces, 7 mips, stock R1 header (flags 0x620c), alpha = scale
//   c<x>_<y>_<z>.vtf      DXT5 32x32, 6 faces, 6 mips (linear value/4, alpha 255)
//   cubemapdefault(.hdr).vtf  the average of all cubemaps
// Face slots need no remapping: seam matching of the R2 faces and of stock R1 cubemaps
// (mp_rise) produces the identical face/edge adjacency graph (see README.txt).
//
// BC6H decoding: bcdec.h by Sergii "iOrange" Kudlai (MIT or Unlicense, notice inside bcdec.h).
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"

#include "internal.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <map>

namespace bspdown {
namespace detail {

namespace {

constexpr uint8_t kHdr8VtfHeader[96] = {
    0x56, 0x54, 0x46, 0x00, 0x07, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x40, 0x00, 0x0C, 0x62, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xEC, 0xD1, 0xB9, 0x3E, 0xD4, 0x60, 0xA8, 0x3E, 0x07, 0x8B, 0x86, 0x3E, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x07, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x30, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x43, 0x52, 0x43, 0x02, 0x9A, 0x6E, 0x24, 0x3B,
};
constexpr uint8_t kLdrVtfHeader[88] = {
    0x56, 0x54, 0x46, 0x00, 0x07, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x20, 0x00, 0x00, 0x40, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x3F, 0x0F, 0x00, 0x00, 0x00, 0x06, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x30, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00,
};

// ---------------------------------------------------------------------------- zip (store only)

uint32_t crc32(const uint8_t* p, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

struct ZipFile {
    std::string name;
    Bytes data;
};

uint16_t r16(const Bytes& b, size_t o) {
    uint16_t v;
    std::memcpy(&v, &b[o], 2);
    return v;
}
uint32_t r32(const Bytes& b, size_t o) {
    uint32_t v;
    std::memcpy(&v, &b[o], 4);
    return v;
}
void w16(Bytes& b, uint16_t v) { appendRaw(b, v); }
void w32(Bytes& b, uint32_t v) { appendRaw(b, v); }

std::vector<ZipFile> readZip(const Bytes& z) {
    std::vector<ZipFile> out;
    if (z.size() < 22) return out;
    size_t e = SIZE_MAX;
    size_t lo = z.size() > 22 + 65535 ? z.size() - 22 - 65535 : 0;
    for (size_t i = z.size() - 22 + 1; i-- > lo;)
        if (r32(z, i) == 0x06054B50u) {
            e = i;
            break;
        }
    if (e == SIZE_MAX) throw Error("pakfile (0x28): no zip end-of-central-directory record");
    uint16_t n = r16(z, e + 10);
    uint32_t cdOff = r32(z, e + 16);
    size_t o = cdOff;
    for (uint16_t i = 0; i < n; i++) {
        if (o + 46 > z.size() || r32(z, o) != 0x02014B50u) throw Error("pakfile (0x28): bad central directory entry");
        uint16_t method = r16(z, o + 10);
        uint32_t csize = r32(z, o + 20), usize = r32(z, o + 24);
        uint16_t nl = r16(z, o + 28), xl = r16(z, o + 30), cl = r16(z, o + 32);
        uint32_t lho = r32(z, o + 42);
        if (o + 46 + nl > z.size()) throw Error("pakfile (0x28): truncated central directory");
        std::string name((const char*)&z[o + 46], nl);
        if (method != 0 || csize != usize) throw Error("pakfile (0x28): entry '" + name + "' is compressed (method " + std::to_string(method) + "), only stored entries are supported");
        if ((uint64_t)lho + 30 > z.size() || r32(z, lho) != 0x04034B50u) throw Error("pakfile (0x28): bad local header for '" + name + "'");
        size_t ds = (size_t)lho + 30 + r16(z, lho + 26) + r16(z, lho + 28);
        if ((uint64_t)ds + usize > z.size()) throw Error("pakfile (0x28): entry '" + name + "' exceeds the lump");
        out.push_back({name, Bytes(z.begin() + ds, z.begin() + ds + usize)});
        o += 46 + nl + xl + cl;
    }
    return out;
}

// Same layout as the stock R1 pakfile: version 10 / made by 20, DOS date and time 0, no extra
// fields, end record with the 32 byte "XZP1 0" comment written by bspzip.
Bytes writeZip(const std::vector<ZipFile>& files) {
    Bytes z, cd;
    for (const ZipFile& f : files) {
        uint32_t off = (uint32_t)z.size(), crc = crc32(f.data.data(), f.data.size());
        w32(z, 0x04034B50u); w16(z, 10); w16(z, 0); w16(z, 0); w16(z, 0); w16(z, 0);
        w32(z, crc); w32(z, (uint32_t)f.data.size()); w32(z, (uint32_t)f.data.size());
        w16(z, (uint16_t)f.name.size()); w16(z, 0);
        z.insert(z.end(), f.name.begin(), f.name.end());
        z.insert(z.end(), f.data.begin(), f.data.end());
        w32(cd, 0x02014B50u); w16(cd, 20); w16(cd, 10); w16(cd, 0); w16(cd, 0); w16(cd, 0); w16(cd, 0);
        w32(cd, crc); w32(cd, (uint32_t)f.data.size()); w32(cd, (uint32_t)f.data.size());
        w16(cd, (uint16_t)f.name.size()); w16(cd, 0); w16(cd, 0); w16(cd, 0); w16(cd, 0); w32(cd, 0); w32(cd, off);
        cd.insert(cd.end(), f.name.begin(), f.name.end());
    }
    uint32_t cdOff = (uint32_t)z.size();
    z.insert(z.end(), cd.begin(), cd.end());
    w32(z, 0x06054B50u); w16(z, 0); w16(z, 0); w16(z, (uint16_t)files.size()); w16(z, (uint16_t)files.size());
    w32(z, (uint32_t)cd.size()); w32(z, cdOff); w16(z, 32);
    const char comment[32] = {'X', 'Z', 'P', '1', ' ', '0'};
    z.insert(z.end(), comment, comment + 32);
    return z;
}

// ---------------------------------------------------------------------------- image helpers

struct Face {
    std::vector<float> rgb;  // n*n*3 linear
    int n = 0;
};

Face boxDown(const Face& f, int n2) {
    Face o;
    o.n = n2;
    o.rgb.assign((size_t)n2 * n2 * 3, 0.0f);
    int k = f.n / n2;
    for (int y = 0; y < n2; y++)
        for (int x = 0; x < n2; x++)
            for (int c = 0; c < 3; c++) {
                double s = 0;
                for (int j = 0; j < k; j++)
                    for (int i = 0; i < k; i++) s += f.rgb[(((size_t)(y * k + j)) * f.n + (x * k + i)) * 3 + c];
                o.rgb[((size_t)y * n2 + x) * 3 + c] = (float)(s / (k * k));
            }
    return o;
}

// BC3 block: alpha 255 (a0 = a1 = 255, all indices 0). Colour: endpoints start at the bounding box
// and are refined with a few least squares iterations over the nearest-palette assignment.
void encodeBC3(const uint8_t px[16][3], uint8_t* out) {
    float e0[3], e1[3];
    {
        int mn[3] = {255, 255, 255}, mx[3] = {0, 0, 0};
        for (int i = 0; i < 16; i++)
            for (int c = 0; c < 3; c++) {
                mn[c] = std::min(mn[c], (int)px[i][c]);
                mx[c] = std::max(mx[c], (int)px[i][c]);
            }
        for (int c = 0; c < 3; c++) {
            e0[c] = (float)mx[c];
            e1[c] = (float)mn[c];
        }
    }
    auto to565 = [](const float* c) {
        auto q = [](float v, int m) { return (int)std::min((float)m, std::max(0.0f, std::floor(v * m / 255.0f + 0.5f))); };
        return (uint16_t)(q(c[0], 31) << 11 | q(c[1], 63) << 5 | q(c[2], 31));
    };
    auto from565 = [](uint16_t c, int* o) {
        int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
        o[0] = (r << 3) | (r >> 2);
        o[1] = (g << 2) | (g >> 4);
        o[2] = (b << 3) | (b >> 2);
    };
    uint16_t c0 = 0, c1 = 0, bc0 = 0, bc1 = 0;
    uint32_t idx = 0, bidx = 0;
    long bestErr = LONG_MAX;
    for (int iter = 0; iter < 4; iter++) {
        c0 = to565(e0);
        c1 = to565(e1);
        if (c0 < c1) std::swap(c0, c1);
        int pal[4][3];
        from565(c0, pal[0]);
        from565(c1, pal[1]);
        for (int c = 0; c < 3; c++) {
            pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3;
            pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
        }
        idx = 0;
        long err = 0;
        int sel[16];
        for (int i = 0; i < 16; i++) {
            int best = 0;
            long bd = LONG_MAX;
            for (int k = 0; k < (c0 != c1 ? 4 : 1); k++) {
                long d = 0;
                for (int c = 0; c < 3; c++) d += (long)(px[i][c] - pal[k][c]) * (px[i][c] - pal[k][c]);
                if (d < bd) {
                    bd = d;
                    best = k;
                }
            }
            sel[i] = best;
            idx |= (uint32_t)best << (2 * i);
            err += bd;
        }
        if (err < bestErr) {
            bestErr = err;
            bc0 = c0;
            bc1 = c1;
            bidx = idx;
        }
        if (c0 == c1) break;
        // least squares: pixel ~ a * E0 + b * E1 with weights from the index (0 -> c0, 2 -> 2/3, 3 -> 1/3, 1 -> c1)
        static const float wt[4] = {1.0f, 0.0f, 2.0f / 3.0f, 1.0f / 3.0f};
        double aa = 0, bb = 0, ab = 0, ax[3] = {0, 0, 0}, bx[3] = {0, 0, 0};
        for (int i = 0; i < 16; i++) {
            double a = wt[sel[i]], b = 1.0 - a;
            aa += a * a; bb += b * b; ab += a * b;
            for (int c = 0; c < 3; c++) {
                ax[c] += a * px[i][c];
                bx[c] += b * px[i][c];
            }
        }
        double det = aa * bb - ab * ab;
        if (std::fabs(det) < 1e-6) break;
        for (int c = 0; c < 3; c++) {
            e0[c] = (float)((ax[c] * bb - bx[c] * ab) / det);
            e1[c] = (float)((bx[c] * aa - ax[c] * ab) / det);
        }
    }
    out[0] = 255; out[1] = 255;
    std::memset(out + 2, 0, 6);
    std::memcpy(out + 8, &bc0, 2);
    std::memcpy(out + 10, &bc1, 2);
    std::memcpy(out + 12, &bidx, 4);
}

struct CubeSet {
    Face face[6];  // 32x32 linear
};

// Stock R1 HDR cubemap encoding (RGBA8888, 64x64, 6 faces, 7 mips, flags 0x620c): the R1 pixel
// shaders (vertexlit_and_unlit_generic_ps50, lightmappedgeneric_ps50) sample the cube and compute
//   env = rgb * (alpha * 3.984375 + 0.015625) = rgb * (alphaByte + 1) / 64
// (rgb unorm, no sRGB read: the engine only reads sRGB for VTF flag 0x80000, see README rule 13).
// Per pixel: alpha = smallest byte with (alpha + 1) / 64 >= max channel, at least 4 like the stock
// files; rgb = value / scale. Values above 4.0 (alpha 255) are clamped.
struct EncodeStats {
    size_t clamped = 0, pixels = 0;
};

void encodePixel(const float* v, uint8_t* out, EncodeStats* st) {
    float mx = std::max(v[0], std::max(v[1], v[2]));
    if (!(mx > 0)) mx = 0;
    int a = (int)std::ceil(64.0 * mx - 1e-9) - 1;
    a = std::max(4, std::min(255, a));
    float s = (float)(a + 1) / 64.0f;
    if (st) {
        st->pixels++;
        if (mx > s * 1.0001f) st->clamped++;
    }
    for (int c = 0; c < 3; c++) {
        float t = v[c] / s;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        out[c] = (uint8_t)std::floor(t * 255.0f + 0.5f);
    }
    out[3] = (uint8_t)a;
}

std::vector<std::vector<Face>> mipChain(const CubeSet& cs) {
    std::vector<Face> cur(6);
    for (int f = 0; f < 6; f++) cur[f] = cs.face[f];
    std::vector<std::vector<Face>> mips;
    mips.push_back(cur);
    while (cur[0].n > 1) {
        std::vector<Face> nx(6);
        for (int f = 0; f < 6; f++) nx[f] = boxDown(cur[f], cur[f].n / 2);
        mips.push_back(nx);
        cur = nx;
    }
    return mips;
}

Bytes buildHdrVtf(const CubeSet& cs, EncodeStats* st) {
    Bytes out(kHdr8VtfHeader, kHdr8VtfHeader + sizeof(kHdr8VtfHeader));
    std::vector<std::vector<Face>> mips = mipChain(cs);
    // reflectivity = mean of (rgb/255)^2.2 over mip 0 (matches the stock files exactly), data CRC resource
    double refl[3] = {0, 0, 0};
    Bytes data;
    for (size_t m = mips.size(); m-- > 0;)
        for (int f = 0; f < 6; f++) {
            const Face& fc = mips[m][f];
            for (int i = 0; i < fc.n * fc.n; i++) {
                uint8_t px[4];
                encodePixel(&fc.rgb[(size_t)i * 3], px, m == 0 ? st : nullptr);
                data.insert(data.end(), px, px + 4);
                if (m == 0)
                    for (int c = 0; c < 3; c++) refl[c] += std::pow(px[c] / 255.0, 2.2);
            }
        }
    float rf[3];
    for (int c = 0; c < 3; c++) rf[c] = (float)(refl[c] / (6.0 * mips[0][0].n * mips[0][0].n));
    std::memcpy(&out[0x20], rf, 12);
    uint32_t crc = crc32(data.data(), data.size());
    std::memcpy(&out[0x5C], &crc, 4);
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

// LDR DXT5 variant (stock R1 ships black placeholders here): linear rgb = value / 4 with alpha 255,
// which the shader decode (alpha 255 -> x4) turns back into the value; values above 4 clamp.
Bytes buildLdrVtf(const CubeSet& cs) {
    Bytes out(kLdrVtfHeader, kLdrVtfHeader + sizeof(kLdrVtfHeader));
    CubeSet c32;
    for (int f = 0; f < 6; f++) c32.face[f] = boxDown(cs.face[f], 32);
    std::vector<std::vector<Face>> mips = mipChain(c32);
    for (size_t m = mips.size(); m-- > 0;)
        for (int f = 0; f < 6; f++) {
            const Face& fc = mips[m][f];
            int bw = std::max(fc.n / 4, 1);
            for (int by = 0; by < bw; by++)
                for (int bx = 0; bx < bw; bx++) {
                    uint8_t px[16][3];
                    for (int j = 0; j < 4; j++)
                        for (int i = 0; i < 4; i++) {
                            int x = std::min(bx * 4 + i, fc.n - 1), y = std::min(by * 4 + j, fc.n - 1);
                            for (int c = 0; c < 3; c++) {
                                float t = fc.rgb[((size_t)y * fc.n + x) * 3 + c] / 4.0f;
                                px[j * 4 + i][c] = (uint8_t)std::floor(std::min(1.0f, std::max(0.0f, t)) * 255.0f + 0.5f);
                            }
                        }
                    uint8_t blk[16];
                    encodeBC3(px, blk);
                    out.insert(out.end(), blk, blk + 16);
                }
        }
    return out;
}

}  // namespace

void convertCubemaps(Ctx& c) {
    Bytes& zl = c.lump[L_PAKFILE];
    const Bytes& cml = c.lump[L_CUBEMAPS];
    if (c.opt.cubemapFiles == false) {
        c.say("[rule 13] cubemap textures: disabled (--no-cubemap-files), pakfile kept as is");
        return;
    }
    if (zl.empty()) {
        c.say("[rule 13] cubemap textures: pakfile (0x28) is empty, nothing to convert");
        return;
    }
    if (cml.size() % 16) throw Error("cubemaps (0x2A): size is not a multiple of 16");
    const size_t nCube = cml.size() / 16;
    std::vector<ZipFile> files = readZip(zl);
    std::string dir, srcName;
    const ZipFile* src = nullptr;
    for (const ZipFile& f : files) {
        const std::string suffix = "/cubemaps.hdr.vtf";
        if (f.name.size() > suffix.size() && f.name.compare(f.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            src = &f;
            dir = f.name.substr(0, f.name.size() - suffix.size() + 1);
            break;
        }
    }
    if (!src) {
        c.say("[rule 13] cubemap textures: pakfile has no cubemaps.hdr.vtf (%zu entries), left unchanged", files.size());
        return;
    }
    if (nCube == 0) throw Error("pakfile has cubemaps.hdr.vtf but cubemaps (0x2A) is empty");
    const Bytes& d = src->data;
    if (d.size() < 0x58 || std::memcmp(d.data(), "VTF\0", 4)) throw Error("cubemaps.hdr.vtf: not a VTF");
    uint32_t verMaj = r32(d, 4), verMin = r32(d, 8), hdrSize = r32(d, 12);
    uint16_t w = r16(d, 16), h = r16(d, 18), frames = r16(d, 24);
    uint32_t fmt = r32(d, 0x34);
    uint8_t mips = d[0x38];
    uint32_t nres = r32(d, 0x44);
    if (verMaj != 7 || verMin != 5) throw Error("cubemaps.hdr.vtf: VTF version " + std::to_string(verMaj) + "." + std::to_string(verMin) + ", expected 7.5");
    if (fmt != 66) throw Error("cubemaps.hdr.vtf: image format " + std::to_string(fmt) + ", expected 66 (BC6H)");
    if (w != h || w < 64 || (w & (w - 1)) || mips == 0) throw Error("cubemaps.hdr.vtf: unsupported size " + std::to_string(w) + "x" + std::to_string(h));
    if (frames != nCube)
        throw Error("cubemaps.hdr.vtf has " + std::to_string(frames) + " frames but cubemaps (0x2A) has " + std::to_string(nCube) + " entries");
    size_t start = hdrSize;
    for (uint32_t i = 0; i < nres && 0x50 + 8 * (size_t)i + 8 <= d.size(); i++)
        if (d[0x50 + 8 * i] == '0' && d[0x50 + 8 * i + 1] == 0 && d[0x50 + 8 * i + 2] == 0) start = r32(d, 0x50 + 8 * i + 4);
    // mips are stored smallest first; inside a mip: frame, face
    std::vector<size_t> msize(mips);
    for (int m = 0; m < mips; m++) {
        int mw = std::max(w >> m, 1);
        msize[m] = (size_t)std::max(mw / 4, 1) * std::max(mw / 4, 1) * 16;
    }
    size_t off0 = start;
    for (int m = mips - 1; m > 0; m--) off0 += msize[m] * frames * 6;
    if (off0 + msize[0] * frames * 6 > d.size()) throw Error("cubemaps.hdr.vtf: image data is smaller than the header says");
    size_t trailing = d.size() - (off0 + msize[0] * frames * 6);

    std::vector<CubeSet> cubes(frames);
    std::vector<std::array<double, 6>> lumTable(frames), relErr(frames);
    size_t neg = 0, nonfinite = 0;
    const float scale = c.opt.cubemapScale;
    if (!(scale > 0)) throw Error("cubemapScale must be > 0");
    for (uint32_t f = 0; f < frames; f++)
        for (int fc = 0; fc < 6; fc++) {
            const uint8_t* blk = &d[off0 + ((size_t)f * 6 + fc) * msize[0]];
            Face big;
            big.n = w;
            big.rgb.assign((size_t)w * w * 3, 0.0f);
            for (int by = 0; by < w / 4; by++)
                for (int bx = 0; bx < w / 4; bx++)
                    bcdec_bc6h_float(blk + ((size_t)by * (w / 4) + bx) * 16, &big.rgb[((size_t)by * 4 * w + bx * 4) * 3], w * 3, 0);
            double lsum = 0;
            for (size_t i = 0; i < big.rgb.size(); i += 3) {
                for (int k = 0; k < 3; k++) {
                    float& v = big.rgb[i + k];
                    if (!std::isfinite(v)) {
                        nonfinite++;
                        v = 0;
                    }
                    if (v < 0) {
                        neg++;
                        v = 0;
                    }
                }
                lsum += 0.2126 * big.rgb[i] + 0.7152 * big.rgb[i + 1] + 0.0722 * big.rgb[i + 2];
            }
            lumTable[f][fc] = lsum / ((double)w * w);
            cubes[f].face[fc] = boxDown(big, 64);
            for (float& v : cubes[f].face[fc].rgb) v *= scale;
        }
    // average cube for cubemapdefault
    CubeSet avg;
    for (int fc = 0; fc < 6; fc++) {
        avg.face[fc].n = 64;
        avg.face[fc].rgb.assign(64 * 64 * 3, 0.0f);
        for (uint32_t f = 0; f < frames; f++)
            for (size_t i = 0; i < avg.face[fc].rgb.size(); i++) avg.face[fc].rgb[i] += cubes[f].face[fc].rgb[i] / frames;
    }

    std::vector<ZipFile> out;
    std::map<std::string, int> seen;
    auto add = [&](const std::string& n, Bytes data) {
        if (seen.count(n)) return false;
        seen[n] = 1;
        out.push_back({n, std::move(data)});
        return true;
    };
    add(dir + "cubemapdefault.vtf", buildLdrVtf(avg));
    EncodeStats est;
    add(dir + "cubemapdefault.hdr.vtf", buildHdrVtf(avg, nullptr));
    size_t dup = 0;
    for (uint32_t f = 0; f < frames; f++) {
        int32_t o[3];
        std::memcpy(o, &cml[(size_t)f * 16], 12);
        char nm[96];
        std::snprintf(nm, sizeof(nm), "c%d_%d_%d", o[0], o[1], o[2]);
        bool a = add(dir + nm + ".vtf", buildLdrVtf(cubes[f]));
        Bytes hv = buildHdrVtf(cubes[f], &est);
        // validation: decode mip 0 with the R1 shader formula and compare with the (scaled) R2 value
        {
            size_t mip0 = sizeof(kHdr8VtfHeader);
            for (int n = 32; n >= 1; n /= 2) mip0 += (size_t)6 * n * n * 4;
            for (int fc = 0; fc < 6; fc++) {
                double err = 0, ref = 0;
                const Face& fa = cubes[f].face[fc];
                for (int i = 0; i < 64 * 64; i++) {
                    const uint8_t* px = &hv[mip0 + ((size_t)fc * 64 * 64 + i) * 4];
                    double sc = (px[3] + 1) / 64.0;
                    for (int k = 0; k < 3; k++) {
                        double dec = px[k] / 255.0 * sc;
                        double r = std::min((double)fa.rgb[(size_t)i * 3 + k], 4.0);
                        err += std::fabs(dec - r);
                        ref += r;
                    }
                }
                relErr[f][fc] = ref > 0 ? err / ref : 0;
            }
        }
        bool b = add(dir + nm + ".hdr.vtf", std::move(hv));
        if (!a || !b) dup++;
    }
    // keep other stored entries of the R2 pakfile (none in any map seen)
    std::string kept;
    for (const ZipFile& f : files)
        if (&f != src) {
            add(f.name, f.data);
            kept += " " + f.name;
        }
    zl = writeZip(out);
    c.present[L_PAKFILE] = true;

    c.say("[rule 13] cubemap textures: R2 cubemaps.hdr.vtf (%ux%u BC6H, %u frames x 6 faces, %u mips, data at %zu, %zu trailing bytes) -> %zu files in the pakfile for %zu cubemaps under %s; pakfile %zu -> %zu bytes",
          w, h, frames, (unsigned)mips, start, trailing, out.size(), nCube, dir.c_str(), src->data.size(), zl.size());
    if (dup) c.say("[rule 13] warning: %zu cubemaps share an origin with an earlier entry, their files were not written twice", dup);
    if (!kept.empty()) c.say("[rule 13] other R2 pakfile entries kept:%s", kept.c_str());
    c.say("[rule 13] per file: c<x>_<y>_<z>.hdr.vtf = RGBA8888 64x64 x6 faces x7 mips, header flags 0x620c, stock R1 encoding rgb * (alpha + 1) / 64 (alpha >= 4), values = R2 BC6H * %g (4x4 box of the 256 mip, 2x2 box mips); c<x>_<y>_<z>.vtf = DXT5 32x32 (linear value/4, alpha 255); cubemapdefault(.hdr).vtf = average of all %zu cubemaps; %zu negative and %zu non-finite BC6H values clamped to 0; %zu of %zu mip 0 pixels above the encodable 4.0 clamped",
          (double)scale, nCube, neg, nonfinite, est.clamped, est.pixels);
    for (uint32_t f = 0; f < frames && f < 12; f++) {
        int32_t o[3];
        std::memcpy(o, &cml[(size_t)f * 16], 12);
        c.say("[rule 13]   c%d_%d_%d R2 face mean luminance (slot 0..5, no face remap): %.3f %.3f %.3f %.3f %.3f %.3f | mean rel. error of the R1 decode vs scaled R2 (%%): %.2f %.2f %.2f %.2f %.2f %.2f", o[0], o[1], o[2],
              lumTable[f][0], lumTable[f][1], lumTable[f][2], lumTable[f][3], lumTable[f][4], lumTable[f][5],
              100 * relErr[f][0], 100 * relErr[f][1], 100 * relErr[f][2], 100 * relErr[f][3], 100 * relErr[f][4], 100 * relErr[f][5]);
    }
    if (frames > 12) c.say("[rule 13]   ... %u more cubemaps not listed", frames - 12);
}

}  // namespace detail
}  // namespace bspdown
