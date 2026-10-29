// AVX2 DXT1 (BC1) encoder for r2tex: bit-exact with encodeBc1Block (the scalar reference in r2tex.cpp), eight blocks per vector.
//
// Included inside namespace r2tex::(anonymous) by r2tex.cpp. Uses only intrinsics (MSVC accepts _mm256_* without /arch:AVX2) and
// is entered only after bc1Avx2Available() returned true, so a CPU without AVX2 never executes a VEX instruction and falls back to
// the scalar encoder.
//
// Exactness: every lane performs the same float operations, in the same order, as encodeBc1Block (no FMA, no reassociation;
// mul and add are separate intrinsics, and fp_contract is off). Distances and errors are integers below 2^24, so they are exact
// in float. The scalar `break` statements become per-lane masks (a lane that broke stops updating its state).
#pragma fp_contract(off)
#ifndef BC1_MAXPASS
#define BC1_MAXPASS 3
#endif

namespace bc1avx {

inline __m256 F(float x) { return _mm256_set1_ps(x); }

// Correctly rounded a / b from y = RN(1 / b) (Markstein: with y the rounded reciprocal, q0 = RN(a y) is within one ulp, the residual
// r = a - b q0 is exact under fma, and RN(q0 + r y) is RN(a / b)). Bit-identical to _mm256_div_ps(a, b) for the normal-range operands
// used here; replaces many 5+ cycle divisions by one shared reciprocal. Exhaustively spot-checked in the test harness.
inline __m256 divWith(__m256 a, __m256 y, __m256 b) {
    const __m256 q0 = _mm256_mul_ps(a, y);
    const __m256 r = _mm256_fnmadd_ps(b, q0, a);
    return _mm256_fmadd_ps(r, y, q0);
}

// px[i][c] is texel i (row-major 4x4), channel c, one block per lane, values 0..255.
// Outputs: per lane the two 565 endpoints (c0 >= c1 as in the scalar encoder) and the 32 bit index word.
inline void encode8(const __m256 (&px)[16][3], __m256i& outC0, __m256i& outC1, __m256i& outIdx) {
    __m256 mean[3];
    for (int c = 0; c < 3; ++c) {
        __m256 s = _mm256_setzero_ps();
        for (int i = 0; i < 16; ++i) s = _mm256_add_ps(s, px[i][c]);
        mean[c] = _mm256_mul_ps(s, F(0.0625f));   // exact: a power of two
    }
    __m256 cov[3][3];
    for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) cov[a][b] = _mm256_setzero_ps();
    for (int i = 0; i < 16; ++i) {
        const __m256 d0 = _mm256_sub_ps(px[i][0], mean[0]);
        const __m256 d1 = _mm256_sub_ps(px[i][1], mean[1]);
        const __m256 d2 = _mm256_sub_ps(px[i][2], mean[2]);
        cov[0][0] = _mm256_add_ps(cov[0][0], _mm256_mul_ps(d0, d0));
        cov[0][1] = _mm256_add_ps(cov[0][1], _mm256_mul_ps(d0, d1));
        cov[0][2] = _mm256_add_ps(cov[0][2], _mm256_mul_ps(d0, d2));
        cov[1][1] = _mm256_add_ps(cov[1][1], _mm256_mul_ps(d1, d1));
        cov[1][2] = _mm256_add_ps(cov[1][2], _mm256_mul_ps(d1, d2));
        cov[2][2] = _mm256_add_ps(cov[2][2], _mm256_mul_ps(d2, d2));
    }
    cov[1][0] = cov[0][1]; cov[2][0] = cov[0][2]; cov[2][1] = cov[1][2];   // d[a]*d[b] == d[b]*d[a] bitwise
    __m256 axis[3];
    for (int c = 0; c < 3; ++c) axis[c] = _mm256_add_ps(cov[c][c], F(1e-3f));
    __m256 live = _mm256_castsi256_ps(_mm256_set1_epi32(-1));
    for (int it = 0; it < 6; ++it) {
        __m256 next[3];
        for (int a = 0; a < 3; ++a) {
            __m256 s = _mm256_setzero_ps();
            for (int b = 0; b < 3; ++b) s = _mm256_add_ps(s, _mm256_mul_ps(cov[a][b], axis[b]));
            next[a] = s;
        }
        const __m256 len = _mm256_sqrt_ps(_mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(next[0], next[0]), _mm256_mul_ps(next[1], next[1])),
                                                         _mm256_mul_ps(next[2], next[2])));
        live = _mm256_andnot_ps(_mm256_cmp_ps(len, F(1e-6f), _CMP_LT_OQ), live);
        if (_mm256_movemask_ps(live) == 0) break;
        const __m256 yl = _mm256_div_ps(F(1.0f), len);
        for (int a = 0; a < 3; ++a) axis[a] = _mm256_blendv_ps(axis[a], divWith(next[a], yl, len), live);
    }
    __m256 tmin = _mm256_setzero_ps(), tmax = _mm256_setzero_ps();
    for (int i = 0; i < 16; ++i) {
        const __m256 t = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(_mm256_sub_ps(px[i][0], mean[0]), axis[0]),
                                                      _mm256_mul_ps(_mm256_sub_ps(px[i][1], mean[1]), axis[1])),
                                       _mm256_mul_ps(_mm256_sub_ps(px[i][2], mean[2]), axis[2]));
        tmin = _mm256_min_ps(t, tmin);   // std::min(tmin, t) == (t < tmin) ? t : tmin
        tmax = _mm256_max_ps(t, tmax);   // std::max(tmax, t) == (tmax < t) ? t : tmax
    }
    __m256 hi[3], lo[3];
    for (int c = 0; c < 3; ++c) {
        hi[c] = _mm256_add_ps(mean[c], _mm256_mul_ps(axis[c], tmax));
        lo[c] = _mm256_add_ps(mean[c], _mm256_mul_ps(axis[c], tmin));
    }

    auto q565 = [](const __m256* c) {
        const __m256 half = F(0.5f);
        const __m256 k255 = F(255.0f), y255 = _mm256_castsi256_ps(_mm256_set1_epi32(0x3B808081));   // RN(1 / 255)
        __m256i r = _mm256_cvttps_epi32(_mm256_add_ps(divWith(_mm256_mul_ps(c[0], F(31.0f)), y255, k255), half));
        __m256i g = _mm256_cvttps_epi32(_mm256_add_ps(divWith(_mm256_mul_ps(c[1], F(63.0f)), y255, k255), half));
        __m256i b = _mm256_cvttps_epi32(_mm256_add_ps(divWith(_mm256_mul_ps(c[2], F(31.0f)), y255, k255), half));
        const __m256i zero = _mm256_setzero_si256();
        r = _mm256_min_epi32(_mm256_max_epi32(r, zero), _mm256_set1_epi32(31));
        g = _mm256_min_epi32(_mm256_max_epi32(g, zero), _mm256_set1_epi32(63));
        b = _mm256_min_epi32(_mm256_max_epi32(b, zero), _mm256_set1_epi32(31));
        return _mm256_or_si256(_mm256_or_si256(_mm256_slli_epi32(r, 11), _mm256_slli_epi32(g, 5)), b);
    };
    const __m256i mask31 = _mm256_set1_epi32(31), mask63 = _mm256_set1_epi32(63);
    auto expandF = [&](__m256i c, __m256 (&o)[3]) {
        const __m256i r = _mm256_and_si256(_mm256_srli_epi32(c, 11), mask31);
        const __m256i g = _mm256_and_si256(_mm256_srli_epi32(c, 5), mask63);
        const __m256i b = _mm256_and_si256(c, mask31);
        o[0] = _mm256_cvtepi32_ps(_mm256_or_si256(_mm256_slli_epi32(r, 3), _mm256_srli_epi32(r, 2)));
        o[1] = _mm256_cvtepi32_ps(_mm256_or_si256(_mm256_slli_epi32(g, 2), _mm256_srli_epi32(g, 4)));
        o[2] = _mm256_cvtepi32_ps(_mm256_or_si256(_mm256_slli_epi32(b, 3), _mm256_srli_epi32(b, 2)));
    };
    // floor(n / 3) for an integer n (0..765) held in a float: (n * 21846) >> 16 (checked for every n in the test harness).
    auto div3 = [](__m256 n) {
        return _mm256_cvtepi32_ps(_mm256_srli_epi32(_mm256_mullo_epi32(_mm256_cvttps_epi32(n), _mm256_set1_epi32(21846)), 16));
    };

    __m256 pxSq = _mm256_setzero_ps();   // sum of |px|^2 over the block (exact integers)
    for (int i = 0; i < 16; ++i)
        pxSq = _mm256_add_ps(pxSq, _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(px[i][0], px[i][0]), _mm256_mul_ps(px[i][1], px[i][1])),
                                                _mm256_mul_ps(px[i][2], px[i][2])));
    const __m256 weightTable = _mm256_setr_ps(1.0f, 0.0f, 2.0f / 3.0f, 1.0f / 3.0f, 0, 0, 0, 0);
    __m256 bestError = F(3.0e9f);
    __m256i bestC0 = _mm256_setzero_si256(), bestC1 = _mm256_setzero_si256(), bestIdx = _mm256_setzero_si256();
    __m256 alive = _mm256_castsi256_ps(_mm256_set1_epi32(-1));
    for (int pass = 0; pass < BC1_MAXPASS; ++pass) {
        __m256i c0 = q565(hi), c1 = q565(lo);
        {
            const __m256i mx = _mm256_max_epi32(c0, c1), mn = _mm256_min_epi32(c0, c1);
            c0 = mx; c1 = mn;
        }
        const __m256 same = _mm256_castsi256_ps(_mm256_cmpeq_epi32(c0, c1));
        __m256 p0[3], p1[3], p2[3], p3[3];
        expandF(c0, p0); expandF(c1, p1);
        for (int c = 0; c < 3; ++c) {
            p2[c] = div3(_mm256_add_ps(_mm256_mul_ps(F(2.0f), p0[c]), p1[c]));
            p3[c] = div3(_mm256_add_ps(p0[c], _mm256_mul_ps(F(2.0f), p1[c])));
        }
        // Palette entry k scores s_k = |pal_k|^2 - 2 px.pal_k (= dist - |px|^2) as the key 4 s_k + k, all small integers held exactly in
        // float: the minimum key is the smallest distance and, among equal distances, the smallest k, which is the scalar "first
        // strict minimum". A lane with c0 == c1 only keeps entry 0 (the others get a huge key).
        const __m256 pal8c[4][3] = {{p0[0], p0[1], p0[2]}, {p1[0], p1[1], p1[2]}, {p2[0], p2[1], p2[2]}, {p3[0], p3[1], p3[2]}};
        __m256 palSq[4], palDot[4][3];
        for (int k = 0; k < 4; ++k) {
            const __m256 sq = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(pal8c[k][0], pal8c[k][0]), _mm256_mul_ps(pal8c[k][1], pal8c[k][1])),
                                            _mm256_mul_ps(pal8c[k][2], pal8c[k][2]));
            palSq[k] = _mm256_add_ps(_mm256_mul_ps(sq, F(4.0f)), F(static_cast<float>(k)));
            if (k) palSq[k] = _mm256_blendv_ps(palSq[k], F(1.0e9f), same);
            for (int c = 0; c < 3; ++c) palDot[k][c] = _mm256_mul_ps(pal8c[k][c], F(8.0f));
        }
        __m256i errorInt = _mm256_setzero_si256();
        __m256i indices = _mm256_setzero_si256();
        __m256i index[16];
        for (int i = 0; i < 16; ++i) {
            __m256 best = _mm256_sub_ps(palSq[0], _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(px[i][0], palDot[0][0]), _mm256_mul_ps(px[i][1], palDot[0][1])),
                                                                _mm256_mul_ps(px[i][2], palDot[0][2])));
            for (int k = 1; k < 4; ++k) {
                const __m256 s = _mm256_sub_ps(palSq[k], _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(px[i][0], palDot[k][0]), _mm256_mul_ps(px[i][1], palDot[k][1])),
                                                                       _mm256_mul_ps(px[i][2], palDot[k][2])));
                best = _mm256_min_ps(best, s);
            }
            const __m256i keyInt = _mm256_cvttps_epi32(best);
            const __m256i idx = _mm256_and_si256(keyInt, _mm256_set1_epi32(3));
            index[i] = idx;
            errorInt = _mm256_add_epi32(errorInt, _mm256_srai_epi32(keyInt, 2));
            indices = _mm256_or_si256(indices, _mm256_slli_epi32(idx, 2 * i));
        }
        const __m256 error = _mm256_add_ps(_mm256_cvtepi32_ps(errorInt), pxSq);
        const __m256 better = _mm256_and_ps(_mm256_cmp_ps(error, bestError, _CMP_LT_OQ), alive);
        bestError = _mm256_blendv_ps(bestError, error, better);
        bestIdx = _mm256_blendv_epi8(bestIdx, indices, _mm256_castps_si256(better));
        bestC0 = _mm256_blendv_epi8(bestC0, c0, _mm256_castps_si256(better));
        bestC1 = _mm256_blendv_epi8(bestC1, c1, _mm256_castps_si256(better));
        if (pass == 2) break;
        alive = _mm256_andnot_ps(_mm256_cmp_ps(error, _mm256_setzero_ps(), _CMP_EQ_OQ), alive);
        if (_mm256_movemask_ps(alive) == 0) break;
        // Least squares: texel ~ a * E0 + (1 - a) * E1 with a = 1, 0, 2/3, 1/3 for indices 0, 1, 2, 3.
        __m256 aa = _mm256_setzero_ps(), bb = _mm256_setzero_ps(), ab = _mm256_setzero_ps();
        __m256 ya[3] = {_mm256_setzero_ps(), _mm256_setzero_ps(), _mm256_setzero_ps()};
        __m256 yb[3] = {_mm256_setzero_ps(), _mm256_setzero_ps(), _mm256_setzero_ps()};
        for (int i = 0; i < 16; ++i) {
            const __m256 a = _mm256_permutevar8x32_ps(weightTable, index[i]);
            const __m256 b = _mm256_sub_ps(F(1.0f), a);
            aa = _mm256_add_ps(aa, _mm256_mul_ps(a, a));
            bb = _mm256_add_ps(bb, _mm256_mul_ps(b, b));
            ab = _mm256_add_ps(ab, _mm256_mul_ps(a, b));
            for (int c = 0; c < 3; ++c) {
                ya[c] = _mm256_add_ps(ya[c], _mm256_mul_ps(a, px[i][c]));
                yb[c] = _mm256_add_ps(yb[c], _mm256_mul_ps(b, px[i][c]));
            }
        }
        const __m256 det = _mm256_sub_ps(_mm256_mul_ps(aa, bb), _mm256_mul_ps(ab, ab));
        const __m256 absDet = _mm256_andnot_ps(F(-0.0f), det);
        alive = _mm256_andnot_ps(_mm256_cmp_ps(absDet, F(1e-4f), _CMP_LT_OQ), alive);
        if (_mm256_movemask_ps(alive) == 0) break;
        const __m256 zero = _mm256_setzero_ps(), top = F(255.0f);
        const __m256 yd = _mm256_div_ps(F(1.0f), det);
        for (int c = 0; c < 3; ++c) {
            const __m256 h = divWith(_mm256_sub_ps(_mm256_mul_ps(ya[c], bb), _mm256_mul_ps(yb[c], ab)), yd, det);
            const __m256 l = divWith(_mm256_sub_ps(_mm256_mul_ps(yb[c], aa), _mm256_mul_ps(ya[c], ab)), yd, det);
            hi[c] = _mm256_min_ps(_mm256_max_ps(h, zero), top);
            lo[c] = _mm256_min_ps(_mm256_max_ps(l, zero), top);
        }
    }
    outC0 = bestC0; outC1 = bestC1; outIdx = bestIdx;
}

// RGBA8 rows (4 bytes per texel, alpha ignored) -> DXT1. Edge blocks clamp the texel coordinate like the scalar path.
inline void encodeImageRgba(const std::uint8_t* rgba, std::uint32_t w, std::uint32_t h, std::uint8_t* out, std::size_t blockStride = 8) {
    const std::uint32_t bx = (w + 3) / 4, by = (h + 3) / 4;
    for (std::uint32_t y = 0; y < by; ++y) {
        for (std::uint32_t x = 0; x < bx; x += 8) {
            const std::uint32_t lanes = std::min<std::uint32_t>(8, bx - x);
            alignas(32) std::uint32_t texel[16][8];
            const bool interior = (x + 8 <= bx) && (w % 4 == 0) && (y * 4 + 4 <= h);
            if (interior) {
                for (std::uint32_t py = 0; py < 4; ++py) {
                    const std::uint8_t* row = rgba + (static_cast<std::size_t>(y * 4 + py) * w + x * 4) * 4;
                    // 8 blocks x 4 texels (dwords): transpose to 4 vectors (one per px column), one block per lane.
                    __m256i col[4];
                    for (int half = 0; half < 2; ++half) {
                        const __m128i r0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + (half * 4 + 0) * 16));
                        const __m128i r1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + (half * 4 + 1) * 16));
                        const __m128i r2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + (half * 4 + 2) * 16));
                        const __m128i r3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + (half * 4 + 3) * 16));
                        const __m128i t0 = _mm_unpacklo_epi32(r0, r1), t1 = _mm_unpacklo_epi32(r2, r3);
                        const __m128i t2 = _mm_unpackhi_epi32(r0, r1), t3 = _mm_unpackhi_epi32(r2, r3);
                        const __m128i c0 = _mm_unpacklo_epi64(t0, t1), c1 = _mm_unpackhi_epi64(t0, t1);
                        const __m128i c2 = _mm_unpacklo_epi64(t2, t3), c3 = _mm_unpackhi_epi64(t2, t3);
                        if (half == 0) { col[0] = _mm256_castsi128_si256(c0); col[1] = _mm256_castsi128_si256(c1); col[2] = _mm256_castsi128_si256(c2); col[3] = _mm256_castsi128_si256(c3); }
                        else { col[0] = _mm256_inserti128_si256(col[0], c0, 1); col[1] = _mm256_inserti128_si256(col[1], c1, 1); col[2] = _mm256_inserti128_si256(col[2], c2, 1); col[3] = _mm256_inserti128_si256(col[3], c3, 1); }
                    }
                    for (int pxi = 0; pxi < 4; ++pxi) _mm256_store_si256(reinterpret_cast<__m256i*>(texel[py * 4 + pxi]), col[pxi]);
                }
            } else {
                for (std::uint32_t j = 0; j < 8; ++j) {
                    const std::uint32_t bj = x + std::min(j, lanes - 1);
                    for (std::uint32_t py = 0; py < 4; ++py)
                        for (std::uint32_t pxi = 0; pxi < 4; ++pxi) {
                            const std::uint32_t sx = std::min(w - 1, bj * 4 + pxi), sy = std::min(h - 1, y * 4 + py);
                            std::memcpy(&texel[py * 4 + pxi][j], rgba + (static_cast<std::size_t>(sy) * w + sx) * 4, 4);
                        }
                }
            }
            __m256 px[16][3];
            const __m256i m255 = _mm256_set1_epi32(255);
            for (int i = 0; i < 16; ++i) {
                const __m256i v = _mm256_load_si256(reinterpret_cast<const __m256i*>(texel[i]));
                px[i][0] = _mm256_cvtepi32_ps(_mm256_and_si256(v, m255));
                px[i][1] = _mm256_cvtepi32_ps(_mm256_and_si256(_mm256_srli_epi32(v, 8), m255));
                px[i][2] = _mm256_cvtepi32_ps(_mm256_and_si256(_mm256_srli_epi32(v, 16), m255));
            }
            __m256i c0, c1, idx;
            encode8(px, c0, c1, idx);
            alignas(32) std::uint32_t a0[8], a1[8], ai[8];
            _mm256_store_si256(reinterpret_cast<__m256i*>(a0), c0);
            _mm256_store_si256(reinterpret_cast<__m256i*>(a1), c1);
            _mm256_store_si256(reinterpret_cast<__m256i*>(ai), idx);
            for (std::uint32_t j = 0; j < lanes; ++j) {
                std::uint8_t* o = out + (static_cast<std::size_t>(y) * bx + x + j) * blockStride;
                o[0] = static_cast<std::uint8_t>(a0[j] & 255); o[1] = static_cast<std::uint8_t>(a0[j] >> 8);
                o[2] = static_cast<std::uint8_t>(a1[j] & 255); o[3] = static_cast<std::uint8_t>(a1[j] >> 8);
                std::memcpy(o + 4, &ai[j], 4);
            }
        }
    }
}

// Unit-normal texel (RGBA8 word, A = 0) for eight (r, g) slope pairs given as int32 lanes 0..255: the arithmetic of the scalar loop in convertNormalVtf.
inline __m256i normalWord(__m256i r, __m256i g) {
    const __m256 scale = F(2.867661f / 255.0f), bias = F(1.439453f), one = F(1.0f), half = F(0.5f), k255 = F(255.0f), zero = _mm256_setzero_ps();
    const __m256 sx = _mm256_sub_ps(_mm256_mul_ps(_mm256_cvtepi32_ps(r), scale), bias);
    const __m256 sy = _mm256_sub_ps(_mm256_mul_ps(_mm256_cvtepi32_ps(g), scale), bias);
    const __m256 inv = _mm256_div_ps(one, _mm256_sqrt_ps(_mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(sx, sx), _mm256_mul_ps(sy, sy)), one)));
    auto enc = [&](__m256 s) {
        const __m256 t = _mm256_add_ps(_mm256_mul_ps(_mm256_add_ps(_mm256_mul_ps(s, half), half), k255), half);
        return _mm256_cvttps_epi32(_mm256_min_ps(_mm256_max_ps(t, zero), k255));
    };
    const __m256i x = enc(_mm256_mul_ps(sx, inv)), y = enc(_mm256_mul_ps(sy, inv)), z = enc(inv);
    return _mm256_or_si256(_mm256_or_si256(x, _mm256_slli_epi32(y, 8)), _mm256_slli_epi32(z, 16));
}

inline void normalsToRgba(const std::uint8_t* src, std::size_t count, std::uint8_t* dst) {
    const __m256i m255 = _mm256_set1_epi32(255);
    std::size_t i = 0;
    for (; i + 8 <= count; i += 8) {
        const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i * 4));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i * 4),
                            normalWord(_mm256_and_si256(v, m255), _mm256_and_si256(_mm256_srli_epi32(v, 8), m255)));
    }
    for (; i < count; ++i) {
        const float sx = src[i * 4] * (2.867661f / 255.0f) - 1.439453f;
        const float sy = src[i * 4 + 1] * (2.867661f / 255.0f) - 1.439453f;
        const float inv = 1.0f / std::sqrt(sx * sx + sy * sy + 1.0f);
        dst[i * 4] = static_cast<std::uint8_t>(std::clamp((sx * inv * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f, 255.0f));
        dst[i * 4 + 1] = static_cast<std::uint8_t>(std::clamp((sy * inv * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f, 255.0f));
        dst[i * 4 + 2] = static_cast<std::uint8_t>(std::clamp((inv * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f, 255.0f));
        dst[i * 4 + 3] = 0;
    }
}

// Writes eight texels (two block rows of four) of one 4x4 block: lanes 0..3 -> row py, lanes 4..7 -> row py + 1.
inline void storeNormalHalf(std::uint8_t* blockOrigin, std::size_t rowBytes, int firstRow, __m256i word) {
    _mm_storeu_si128(reinterpret_cast<__m128i*>(blockOrigin + firstRow * rowBytes), _mm256_castsi256_si128(word));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(blockOrigin + (firstRow + 1) * rowBytes), _mm256_extracti128_si256(word, 1));
}

// BC5 (R, G slopes; bcdec precise BC4 decode, unsigned) straight to unit-normal RGBA8. w and h must be multiples of 4.
inline void normalsFromBc5(const std::uint8_t* src, std::uint32_t w, std::uint32_t h, std::uint8_t* dst) {
    static const int wa6[8] = {65536, 0, 56173, 46812, 37450, 28086, 18724, 9363};
    static const int wb6[8] = {0, 65536, 9363, 18724, 28086, 37450, 46812, 56173};
    static const int wa4[8] = {65536, 0, 52429, 39321, 26215, 13107, 0, 0};
    static const int wb4[8] = {0, 65536, 13107, 26215, 39321, 52429, 0, 0};
    const __m256i vwa6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(wa6)), vwb6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(wb6));
    const __m256i vwa4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(wa4)), vwb4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(wb4));
    const __m256i bias6 = _mm256_set1_epi32(32768);
    const __m256i bias4 = _mm256_setr_epi32(32768, 32768, 32768, 32768, 32768, 32768, 32768, 32768 + 255 * 65536);
    const __m256i shift3 = _mm256_setr_epi32(0, 3, 6, 9, 12, 15, 18, 21);
    const __m256i m7 = _mm256_set1_epi32(7);
    auto palette = [&](std::uint64_t block) {
        const int a0 = static_cast<int>(block & 0xFF), a1 = static_cast<int>((block >> 8) & 0xFF);
        const bool six = a0 > a1;
        const __m256i wa = six ? vwa6 : vwa4, wb = six ? vwb6 : vwb4, bias = six ? bias6 : bias4;
        return _mm256_srli_epi32(_mm256_add_epi32(_mm256_add_epi32(_mm256_mullo_epi32(wa, _mm256_set1_epi32(a0)),
                                                                     _mm256_mullo_epi32(wb, _mm256_set1_epi32(a1))), bias), 16);
    };
    const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;
    const std::uint32_t bx = w / 4, by = h / 4;
    for (std::uint32_t y = 0; y < by; ++y) {
        std::uint8_t* origin = dst + static_cast<std::size_t>(y) * 4 * rowBytes;
        for (std::uint32_t x = 0; x < bx; ++x, origin += 16, src += 16) {
            std::uint64_t r64, g64;
            std::memcpy(&r64, src, 8); std::memcpy(&g64, src + 8, 8);
            const __m256i palR = palette(r64), palG = palette(g64);
            const std::uint64_t ri = r64 >> 16, gi = g64 >> 16;
            for (int half = 0; half < 2; ++half) {
                const int sh = half * 24;
                const __m256i ir = _mm256_and_si256(_mm256_srlv_epi32(_mm256_set1_epi32(static_cast<int>((ri >> sh) & 0xFFFFFF)), shift3), m7);
                const __m256i ig = _mm256_and_si256(_mm256_srlv_epi32(_mm256_set1_epi32(static_cast<int>((gi >> sh) & 0xFFFFFF)), shift3), m7);
                storeNormalHalf(origin, rowBytes, half * 2, normalWord(_mm256_permutevar8x32_epi32(palR, ir), _mm256_permutevar8x32_epi32(palG, ig)));
            }
        }
    }
}

// BC1 (R, G of the decoded colour; bcdec colour expansion) straight to unit-normal RGBA8. w and h must be multiples of 4.
inline void normalsFromBc1(const std::uint8_t* src, std::uint32_t w, std::uint32_t h, std::uint8_t* dst) {
    const __m256i shift2 = _mm256_setr_epi32(0, 2, 4, 6, 8, 10, 12, 14);
    const __m256i m3 = _mm256_set1_epi32(3);
    const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;
    const std::uint32_t bx = w / 4, by = h / 4;
    for (std::uint32_t y = 0; y < by; ++y) {
        std::uint8_t* origin = dst + static_cast<std::size_t>(y) * 4 * rowBytes;
        for (std::uint32_t x = 0; x < bx; ++x, origin += 16, src += 8) {
            std::uint16_t c0, c1; std::uint32_t indices;
            std::memcpy(&c0, src, 2); std::memcpy(&c1, src + 2, 2); std::memcpy(&indices, src + 4, 4);
            const int r0 = (c0 >> 11) & 0x1F, g0 = (c0 >> 5) & 0x3F, r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F;
            alignas(32) int pr[8] = {}, pg[8] = {};
            pr[0] = (r0 * 527 + 23) >> 6; pg[0] = (g0 * 259 + 33) >> 6;
            pr[1] = (r1 * 527 + 23) >> 6; pg[1] = (g1 * 259 + 33) >> 6;
            if (c0 > c1) {
                pr[2] = ((2 * r0 + r1) * 351 + 61) >> 7; pg[2] = ((2 * g0 + g1) * 2763 + 1039) >> 11;
                pr[3] = ((r0 + r1 * 2) * 351 + 61) >> 7; pg[3] = ((g0 + g1 * 2) * 2763 + 1039) >> 11;
            } else {
                pr[2] = ((r0 + r1) * 1053 + 125) >> 8; pg[2] = ((g0 + g1) * 4145 + 1019) >> 11;
                pr[3] = 0; pg[3] = 0;
            }
            const __m256i palR = _mm256_load_si256(reinterpret_cast<const __m256i*>(pr)), palG = _mm256_load_si256(reinterpret_cast<const __m256i*>(pg));
            for (int half = 0; half < 2; ++half) {
                const __m256i idx = _mm256_and_si256(_mm256_srlv_epi32(_mm256_set1_epi32(static_cast<int>((indices >> (half * 16)) & 0xFFFF)), shift2), m3);
                storeNormalHalf(origin, rowBytes, half * 2, normalWord(_mm256_permutevar8x32_epi32(palR, idx), _mm256_permutevar8x32_epi32(palG, idx)));
            }
        }
    }
}


// DXT5 alpha half (BC4 style, see encodeDxt5AlphaBlock in r2tex.cpp) of eight neighbouring blocks of a w-wide alpha plane
// (4 rows from `src`, 32 texels per row), written at out + j * stride. Same decisions as the scalar encoder: palette
// hi, lo, ((7 - k) hi + k lo) / 7, first minimum |a - pal| wins. Needs w % 4 == 0 and 8 whole blocks inside the row.
inline void encodeAlpha8(const std::uint8_t* src, std::uint32_t w, std::uint8_t* out, std::size_t stride) {
    const __m256i offsets = _mm256_setr_epi32(0, 4, 8, 12, 16, 20, 24, 28);
    __m256i P[16];
    for (int r = 0; r < 4; ++r) {
        const __m256i d = _mm256_i32gather_epi32(reinterpret_cast<const int*>(src + static_cast<std::size_t>(r) * w), offsets, 1);
        for (int c = 0; c < 4; ++c) P[r * 4 + c] = _mm256_and_si256(_mm256_srli_epi32(d, 8 * c), _mm256_set1_epi32(255));
    }
    __m256i hi = P[0], lo = P[0];
    for (int i = 1; i < 16; ++i) { hi = _mm256_max_epi32(hi, P[i]); lo = _mm256_min_epi32(lo, P[i]); }
    __m256i pal[8];
    pal[0] = hi; pal[1] = lo;
    for (int k = 1; k <= 6; ++k) {
        const __m256i n = _mm256_add_epi32(_mm256_mullo_epi32(hi, _mm256_set1_epi32(7 - k)), _mm256_mullo_epi32(lo, _mm256_set1_epi32(k)));
        pal[k + 1] = _mm256_srli_epi32(_mm256_mullo_epi32(n, _mm256_set1_epi32(9363)), 16);   // n / 7 for n <= 1785 (checked exhaustively in the test harness)
    }
    __m256i w0 = _mm256_setzero_si256(), w1 = _mm256_setzero_si256();
    for (int i = 0; i < 16; ++i) {
        __m256i best = _mm256_or_si256(_mm256_slli_epi32(_mm256_abs_epi32(_mm256_sub_epi32(P[i], pal[0])), 3), _mm256_setzero_si256());
        for (int k = 1; k < 8; ++k)
            best = _mm256_min_epi32(best, _mm256_or_si256(_mm256_slli_epi32(_mm256_abs_epi32(_mm256_sub_epi32(P[i], pal[k])), 3), _mm256_set1_epi32(k)));
        const __m256i idx = _mm256_and_si256(best, _mm256_set1_epi32(7));
        if (i < 10) w0 = _mm256_or_si256(w0, _mm256_slli_epi32(idx, 3 * i));
        else if (i == 10) { w0 = _mm256_or_si256(w0, _mm256_slli_epi32(idx, 30)); w1 = _mm256_srli_epi32(idx, 2); }
        else w1 = _mm256_or_si256(w1, _mm256_slli_epi32(idx, 3 * (i - 11) + 1));
    }
    alignas(32) std::uint32_t aHi[8], aLo[8], aW0[8], aW1[8];
    _mm256_store_si256(reinterpret_cast<__m256i*>(aHi), hi); _mm256_store_si256(reinterpret_cast<__m256i*>(aLo), lo);
    _mm256_store_si256(reinterpret_cast<__m256i*>(aW0), w0); _mm256_store_si256(reinterpret_cast<__m256i*>(aW1), w1);
    for (int j = 0; j < 8; ++j) {
        std::uint8_t* o = out + static_cast<std::size_t>(j) * stride;
        o[0] = static_cast<std::uint8_t>(aHi[j]); o[1] = static_cast<std::uint8_t>(aLo[j]);
        std::memcpy(o + 2, &aW0[j], 4);
        o[6] = static_cast<std::uint8_t>(aW1[j]); o[7] = static_cast<std::uint8_t>(aW1[j] >> 8);
    }
}

// Vertical pass of resizeUberPlane for one output row: out[x] = clamp(sum_k weights[k] * rows[k][x] + 0.5) with the scalar order of
// additions per texel (no FMA), eight texels per step.
inline void resizeRow(const float* const* rows, const float* weights, std::size_t count, std::uint32_t width, std::uint8_t* out) {
    std::uint32_t x = 0;
    const __m256 half = F(0.5f), zero = _mm256_setzero_ps(), top = F(255.0f);
    for (; x + 8 <= width; x += 8) {
        __m256 sum = _mm256_setzero_ps();
        for (std::size_t k = 0; k < count; ++k) sum = _mm256_add_ps(sum, _mm256_mul_ps(F(weights[k]), _mm256_loadu_ps(rows[k] + x)));
        const __m256i v = _mm256_cvttps_epi32(_mm256_min_ps(_mm256_max_ps(_mm256_add_ps(sum, half), zero), top));
        alignas(32) std::int32_t tmp[8];
        _mm256_store_si256(reinterpret_cast<__m256i*>(tmp), v);
        for (int j = 0; j < 8; ++j) out[x + j] = static_cast<std::uint8_t>(tmp[j]);
    }
    for (; x < width; ++x) {
        float sum = 0.0f;
        for (std::size_t k = 0; k < count; ++k) sum += weights[k] * rows[k][x];
        out[x] = static_cast<std::uint8_t>(std::clamp(sum + 0.5f, 0.0f, 255.0f));
    }
}

}  // namespace bc1avx

// AVX2 usable: CPUID.7.EBX bit 5, and the OS saves YMM state (XCR0 bits 1 and 2). Evaluated once.
inline bool bc1Avx2Available() {
    static const bool available = [] {
        const char* off = std::getenv("R2TEX_DXT_SIMD");
        if (off && off[0] == '0') return false;
        int regs[4];
        __cpuid(regs, 0);
        if (regs[0] < 7) return false;
        __cpuid(regs, 1);
        const bool osxsave = (regs[2] & (1 << 27)) != 0, avx = (regs[2] & (1 << 28)) != 0;
        if (!osxsave || !avx) return false;
        if ((_xgetbv(0) & 6) != 6) return false;
        const bool fma = (regs[2] & (1 << 12)) != 0;   // CPUID.1:ECX FMA3, used by divWith
        __cpuidex(regs, 7, 0);
        return fma && (regs[1] & (1 << 5)) != 0;
    }();
    return available;
}
