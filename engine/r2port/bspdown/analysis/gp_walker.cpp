// Faithful port of the cell-list construction of the R1 world ray/hull walker
// (engine sub_18010BE50, a2 == 0 path, IDA copy of R1 retail engine.dll), used to test collision grids.
// Floats are IEEE single, same operation order as the machine code. The cell list lives in a persistent
// buffer like the 16384 entry stack array of the original (stale entries survive between calls).
//
// build:  g++ -std=c++17 -O2 -ffp-contract=off -o gp_walker gp_walker.cpp
// usage:  gp_walker <dimX> <dimY> <offX> <offY> <numModels> <scale> <R> <step> <seed> [padCells]
//   Sweeps rays whose start and end lie on a lattice inside [-R, R] (xy), for a set of hull extents,
//   against the grid; then random sequences with persistent stale list. Prints violations.
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

static int ftoi(float x) {  // cvttss2si
    if (!(x > -2147483904.0f && x < 2147483648.0f)) return INT_MIN;
    return (int)x;
}
static int floorI(float x) {  // the walker's floor sequence, returns cvttss2si of the result
    int v = ftoi(x);
    float xf = x;
    if (v != INT_MIN && (float)v != x) xf = (float)(v - (std::signbit(x) ? 1 : 0));
    return ftoi(xf);
}
static int64_t sub32(int a, int b) { return (int64_t)(int32_t)((uint32_t)a - (uint32_t)b); }  // sub eax, r ; movsxd

struct GridInfo {
    float scale, inv;
    int offX, offY, dimX, dimY, numModels;
    int64_t tableSize() const { return (int64_t)dimX * dimY + numModels; }
};
struct Ray {
    float start[3], delta[3], ext[3], end[3];
};

static const int LIST_CAP = 16384;
struct Walker {
    int32_t list[LIST_CAP + 8];  // persistent (stale) list storage
    uint8_t written[LIST_CAP + 8];
    // statistics for the last call
    int64_t count = 0;
    bool overflow = false;
    int staleReads = 0;      // entries below count that this call never wrote
    int oobCounted = 0;      // entries below count (written or stale) outside the table
    int oobCountedWritten = 0;  // ... that this call wrote
    int oobScratch = 0;      // written beyond the final count (scratch), outside the table
    int maxBand = 0;
    int path = 0;
    int wmax = 0, prevMax = LIST_CAP;  // 0 skipped, 1 rect, 2 line, 3 2x2, 4 ymajor, 5 xmajor

    Walker() { memset(list, 0, sizeof(list)); }

    void wr(int64_t k, int64_t v) {
        if (k < 0 || k >= LIST_CAP) { overflow = true; return; }
        list[k] = (int32_t)v;
        written[k] = 1;
        if (k > wmax) wmax = (int)k;
    }

    // returns list count (including the model cell)
    void walk(const GridInfo& g, const Ray& r, int a2 = 0) {
        memset(written, 0, (size_t)(prevMax + 8 < LIST_CAP ? prevMax + 8 : LIST_CAP));
        wmax = 0;
        count = 0; overflow = false; staleReads = oobCounted = oobCountedWritten = oobScratch = 0; maxBand = 0; path = 0;
        int64_t r8 = 0;
        if (a2 == 0) {
            float xs = r.start[0], xe = r.end[0];
            float xmin, xmax; int64_t xsign;
            if (xs >= xe) { xmin = xe; xmax = xs; xsign = -1; } else { xmin = xs; xmax = xe; xsign = 1; }
            float ys = r.start[1], ye = r.end[1];
            float ymin, ymax; int64_t ysign = -1;
            if (ys >= ye) { ymin = ye; ymax = ys; } else { ymin = ys; ymax = ye; ysign = 1; }
            float ex = r.ext[0], ey = r.ext[1];
            const float c1 = 0.03125f;
            float t = (xmin - ex) - c1; t = t * g.inv;
            int64_t xlo = sub32(floorI(t), g.offX);
            float u = (ex + xmax) + c1; u = u * g.inv;
            int64_t xhi = sub32(floorI(u), g.offX);
            int64_t dimX = g.dimX, dimY = g.dimY;
            if (xlo < 0) xlo = 0;
            if (xhi >= dimX) xhi = dimX - 1;
            int64_t xrange = xhi - xlo;
            float t2 = (ymin - ey) - c1; t2 = t2 * g.inv;
            int64_t ylo = sub32(floorI(t2), g.offY);
            float u2 = (ey + ymax) + c1; u2 = u2 * g.inv;
            int64_t yhi = sub32(floorI(u2), g.offY);
            if (ylo < 0) ylo = 0;
            if (yhi >= dimY) yhi = dimY - 1;
            int64_t yrange = yhi - ylo;
            if (xrange >= 0 && yrange >= 0) {
                float dy = r.delta[1], dx = r.delta[0];
                float ady = std::fabs(dy), adx = std::fabs(dx);
                bool rect = false;
                if (ady + adx == 0.0f) rect = true;
                else {
                    float thr = g.scale * 0.70710677f - 0.0625f;
                    if (!(ex < thr)) rect = true;  // comiss xmm8, xmm0 ; jnb
                }
                if (rect) {
                    path = 1;
                    for (int64_t row = ylo; row <= yhi; row++)
                        for (int64_t c = xlo; c <= xhi; c++) wr(r8++, (int32_t)((int32_t)row * (int32_t)dimX + (int32_t)c));
                } else {
                    int64_t xstart = xsign >= 0 ? xlo : xhi, xend = xsign >= 0 ? xhi : xlo;
                    int64_t ystart = ysign >= 0 ? ylo : yhi, yend = ysign >= 0 ? yhi : ylo;
                    int64_t cell0 = dimX * ystart + xstart;
                    int64_t prod = yrange * xrange;
                    if (prod == 0) {
                        path = 2;
                        wr(0, cell0); r8 = 1;
                        if (yrange != 0) {
                            int64_t rcx = cell0, stride = dimX * ysign, rr = ystart;
                            while (rr != yend) { rcx += stride; rr += ysign; r8++; wr(r8 - 1, (int32_t)rcx); }
                        } else {
                            int64_t rcx = cell0, xx = xstart;
                            while (xx != xend) { rcx += xsign; xx += xsign; r8++; wr(r8 - 1, (int32_t)rcx); }
                        }
                    } else if (prod == 1) {
                        path = 3;
                        int32_t edi = (int32_t)ysign * (int32_t)dimX;
                        wr(0, cell0);
                        wr(1, (int32_t)(xsign + cell0));
                        wr(2, (int32_t)(edi + cell0));
                        wr(3, (int32_t)(xsign + edi + (int32_t)cell0));
                        r8 = 4;
                    } else {
                        float ex2 = ex + 0.046875f;
                        float sx = g.inv * r.start[0]; sx = sx - (float)g.offX;     // xmm9
                        float sy = g.inv * r.start[1]; sy = sy - (float)g.offY;     // xmm6
                        float v46 = std::sqrt(ady * ady + adx * adx);               // sqrt(dy^2 + dx^2) (addss order dy*dy + dx*dx)
                        v46 = v46 * ex2; v46 = v46 * g.inv;
                        if (!(adx > ady)) {
                            // y major: loop rows
                            path = 4;
                            int64_t stride = dimX * ysign;
                            float half = adx * 0.5f; half = half + v46;
                            float rcp = 1.0f / ady;
                            float hw = half * rcp;
                            float xc = (float)ystart; xc = xc + 0.5f; xc = xc - sy; xc = xc * dx;
                            float slope = dx * rcp;
                            xc = xc / dy; xc = xc + sx;
                            float lo = xc - hw, hi = hw + xc;
                            int64_t row = ystart, rowBase = dimX * ystart;
                            for (;;) {
                                int64_t a = floorI(lo), b = floorI(hi);
                                if (a < xlo) a = xlo;
                                if (b > xhi) b = xhi;
                                int64_t n1 = b - a;
                                if (n1 >= 0) {
                                    int64_t first = xsign >= 0 ? a : b;
                                    int64_t cell = first + rowBase;
                                    wr(r8, (int32_t)cell); wr(r8 + 1, (int32_t)(xsign + cell)); wr(r8 + 2, (int32_t)(cell + 2 * xsign));
                                    if (n1 + 1 > maxBand) maxBand = (int)(n1 + 1);
                                    r8 += n1 + 1;
                                }
                                if (row == yend) break;
                                row += ysign; lo += slope; hi += slope; rowBase += stride;
                            }
                        } else {
                            // x major: loop columns
                            path = 5;
                            int64_t stride = dimX * ysign;
                            float half = ady * 0.5f; half = half + v46;
                            float rcp = 1.0f / adx;
                            float yc = (float)xstart; yc = yc + 0.5f; yc = yc - sx; yc = yc * dy; yc = yc / dx; yc = yc + sy;
                            float hw = half * rcp;
                            float slope = rcp * dy;
                            float lo = yc - hw, hi = hw + yc;
                            int64_t col = xstart;
                            for (;;) {
                                int64_t a = floorI(lo), b = floorI(hi);
                                if (a < ylo) a = ylo;
                                if (b > yhi) b = yhi;
                                int64_t n1 = b - a;
                                if (n1 >= 0) {
                                    int64_t first = ysign >= 0 ? a : b;
                                    int64_t cell = first * dimX + col;
                                    wr(r8, (int32_t)cell); wr(r8 + 1, (int32_t)(cell + stride)); wr(r8 + 2, (int32_t)(cell + 2 * stride));
                                    if (n1 + 1 > maxBand) maxBand = (int)(n1 + 1);
                                    r8 += n1 + 1;
                                }
                                if (col == xend) break;
                                col += xsign; lo += slope; hi += slope;
                            }
                        }
                    }
                }
            }
        }
        // model cell
        wr(r8, (int64_t)((int32_t)g.dimY * (int32_t)g.dimX + a2));
        r8++;
        count = r8;
        int64_t T = g.tableSize();
        for (int64_t k = 0; k < r8 && k < LIST_CAP; k++) {
            bool bad = list[k] < 0 || list[k] >= T;
            if (!written[k]) { staleReads++; }
            if (bad) { oobCounted++; if (written[k]) oobCountedWritten++; }
        }
        // scratch: entries written beyond count (the 2 slots after each band)
        for (int64_t k = r8; k <= wmax && k < LIST_CAP; k++) {
            if (!written[k]) continue;
            if (list[k] < 0 || list[k] >= T) oobScratch++;
        }
        prevMax = wmax;
    }
};

int main(int argc, char** argv) {
    if (argc < 10) { fprintf(stderr, "usage: dimX dimY offX offY numModels scale R step seed [padCells]\n"); return 2; }
    GridInfo g;
    g.dimX = atoi(argv[1]); g.dimY = atoi(argv[2]); g.offX = atoi(argv[3]); g.offY = atoi(argv[4]);
    g.numModels = atoi(argv[5]); g.scale = (float)atof(argv[6]); g.inv = 1.0f / g.scale;
    float R = (float)atof(argv[7]); float step = (float)atof(argv[8]); unsigned seed = (unsigned)atoi(argv[9]);
    int pad = argc > 10 ? atoi(argv[10]) : 0;
    if (pad) { g.offX -= pad; g.offY -= pad; g.dimX += 2 * pad; g.dimY += 2 * pad; }
    printf("grid scale %g off (%d,%d) dims (%d,%d) models %d cells %lld extent x [%g,%g) y [%g,%g)\n", g.scale, g.offX, g.offY, g.dimX, g.dimY, g.numModels, (long long)g.tableSize(),
           g.offX * g.scale, (g.offX + g.dimX) * g.scale, g.offY * g.scale, (g.offY + g.dimY) * g.scale);
    static Walker W;
    const float exts[] = {0.f, 16.f, 32.f, 40.f, 60.f, 64.f, 80.f, 88.f, 90.f, 91.f, 100.f, 120.f, 150.f, 180.f, 181.f, 182.f, 200.f, 250.f, 300.f, 350.f, 400.f, 450.f, 480.f, 497.f, 498.f, 600.f};
    if (argc > 12 && std::string(argv[11]) == "file") {
        FILE* f = fopen(argv[12], "r");
        double a[7]; long long i = 0, bad = 0;
        memset(W.list, 0, sizeof(W.list));
        while (fscanf(f, "%lf %lf %lf %lf %lf %lf %lf", a, a + 1, a + 2, a + 3, a + 4, a + 5, a + 6) == 7) {
            Ray r{};
            r.start[0] = (float)a[0]; r.start[1] = (float)a[1]; r.start[2] = (float)a[2];
            r.end[0] = (float)a[3]; r.end[1] = (float)a[4]; r.end[2] = (float)a[5];
            for (int k = 0; k < 3; k++) r.delta[k] = (float)(a[3 + k] - a[k]);
            r.ext[0] = r.ext[1] = (float)a[6]; r.ext[2] = 36;
            W.walk(g, r);
            if (getenv("DUMPCELL")) { int want = atoi(getenv("DUMPCELL")); bool has = false; for (int64_t k = 0; k < W.count; k++) if (W.list[k] == want) has = true;
                if (has) { printf("ray %lld start (%g,%g) end (%g,%g) ext %g path %d band %d list:", i, a[0], a[1], a[3], a[4], a[6], W.path, W.maxBand); for (int64_t k = 0; k < W.count && k < 60; k++) printf(" %d%s", W.list[k], W.written[k] ? "" : "?"); printf("\n"); } }
            if (W.oobCounted || W.staleReads) { bad++; printf("ray %lld: start (%g,%g) delta (%g,%g) ext %g path %d count %lld band %d stale %d oob %d\n", i, a[0], a[1], r.delta[0], r.delta[1], a[6], W.path, (long long)W.count, W.maxBand, W.staleReads, W.oobCounted); }
            i++;
        }
        printf("%lld rays, %lld with stale/out-of-table reads\n", i, bad);
        return 0;
    }
    if (argc > 11 && std::string(argv[11]) == "local") {
        std::mt19937 rng(seed);
        float x0 = g.offX * g.scale - 2000, x1 = (g.offX + g.dimX) * g.scale + 2000, y0 = g.offY * g.scale - 2000, y1 = (g.offY + g.dimY) * g.scale + 2000;
        std::uniform_real_distribution<float> UX(x0, x1), UY(y0, y1), UL(0.f, 1.f);
        const float lens[] = {1.f, 30.f, 200.f, 1000.f, 4000.f, 20000.f};
        for (float ext : {0.f, 16.f, 24.f, 32.f, 40.f, 60.f, 90.f}) {
            for (float len : lens) {
                long long bad = 0, n = 300000; int shown = 0;
                memset(W.list, 0, sizeof(W.list));
                for (long long i = 0; i < n; i++) {
                    Ray r{};
                    r.start[0] = UX(rng); r.start[1] = UY(rng);
                    float a = UL(rng) * 6.2831853f, l = UL(rng) * len;
                    r.delta[0] = std::cos(a) * l; r.delta[1] = std::sin(a) * l;
                    if (i % 7 == 0) r.delta[0] = 0; if (i % 11 == 0) r.delta[1] = 0;
                    r.end[0] = r.start[0] + r.delta[0]; r.end[1] = r.start[1] + r.delta[1];
                    r.ext[0] = r.ext[1] = ext; r.ext[2] = 36;
                    W.walk(g, r);
                    if (W.oobCounted) { bad++; if (shown++ < 2) printf("   sample ext %g len %g: start (%g,%g) delta (%g,%g) path %d count %lld band %d stale %d\n", ext, len, r.start[0], r.start[1], r.delta[0], r.delta[1], W.path, (long long)W.count, W.maxBand, W.staleReads); }
                }
                printf("local ext %5.0f len %6.0f: %lld rays, %lld read out-of-table cells\n", ext, len, n, bad);
            }
        }
        return 0;
    }
    // ---- 1. lattice sweep: all (start, end) pairs on a coarse lattice, fresh list (zero stale), per extent
    {
        std::vector<float> pts;
        for (float v = -R; v <= R + 0.001f; v += step) pts.push_back(v);
        printf("lattice: %zu values per axis, R=%g step=%g\n", pts.size(), R, step);
        for (float ext : exts) {
            long long rays = 0, counted = 0, scratch = 0, stale = 0, overflow = 0, band4 = 0;
            for (float sx : pts) for (float sy : pts) for (float ex_ : pts) for (float ey_ : pts) {
                Ray r{};
                r.start[0] = sx; r.start[1] = sy; r.end[0] = ex_; r.end[1] = ey_;
                r.delta[0] = ex_ - sx; r.delta[1] = ey_ - sy;
                r.ext[0] = r.ext[1] = ext; r.ext[2] = ext;
                W.walk(g, r);
                rays++;
                counted += W.oobCounted > 0; scratch += W.oobScratch > 0; stale += W.staleReads > 0; overflow += W.overflow; band4 += W.maxBand >= 4;
            }
            printf("  ext %5.0f: rays %lld  counted-OOB %lld  scratch-OOB %lld  stale-reads %lld  band>=4 %lld  list-overflow %lld\n", ext, rays, counted, scratch, stale, band4, overflow);
        }
    }
    // ---- 2. random sequence with persistent stale list: does any read hit an out-of-table index?
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> U(-R, R);
        for (float ext : exts) {
            memset(W.list, 0, sizeof(W.list));
            long long rays = 0, crash = 0, firstAt = -1;
            int shown = 0;
            for (int i = 0; i < 2000000; i++) {
                Ray r{};
                r.start[0] = U(rng); r.start[1] = U(rng); r.end[0] = U(rng); r.end[1] = U(rng);
                r.delta[0] = r.end[0] - r.start[0]; r.delta[1] = r.end[1] - r.start[1];
                r.ext[0] = r.ext[1] = r.ext[2] = ext;
                W.walk(g, r);
                rays++;
                if (W.oobCounted) {
                    crash++;
                    if (firstAt < 0) firstAt = i;
                    if (shown < 3 && ext == 400.f) {
                        for (int64_t k = 0; k < W.count && k < LIST_CAP; k++)
                            if (W.list[k] < 0 || W.list[k] >= g.tableSize()) {
                                printf("    sample (ext 400, ray %d): list entry %lld of %lld = %d (row %d, col %d of %dx%d, table %lld), %s\n", i, (long long)k, (long long)W.count, W.list[k],
                                       W.list[k] / g.dimX, W.list[k] % g.dimX, g.dimX, g.dimY, (long long)g.tableSize(), W.written[k] ? "written this call" : "stale (not written this call)");
                                shown++;
                                break;
                            }
                    }
                }
            }
            printf("  random sequence ext %5.0f: rays %lld  reads of out-of-table cell (stale or counted) %lld (first at %lld)\n", ext, rays, crash, firstAt);
        }
    }
    return 0;
}
