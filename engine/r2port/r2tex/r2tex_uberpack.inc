// Compressed Uber carriers (R2_UBER pack): source plane extraction, 2x2 box mip chain on byte planes and block encoders.
// Included by r2tex.cpp inside its anonymous namespace after srgbByteToLinear (uses u16/put32, VtfView, vtfMipBytes, bcdec and
// encodeBc1Block from there). No state; every function is safe to call concurrently.
//
// Layouts (shaderlab/uber/DESIGN.md sec 9):
//   single layer  R = cavity, G = AO, read as .r / .g of $detailColor (t4) by the Uber shader. BC5 (ImageFormat 34), or DXT1 (ImageFormat 13)
//                 with R = B = 255 when only AO is present and the DXT1 fit reaches kUberDxt1MinPsnr: the shader decodes both the same way.
//   two layers    DXT5 (ImageFormat 15), RGB block = (mask, mask, cavity), alpha block = AO. The stock shader reads only R and G (the blend
//                 range, so the mask stays in both), the Uber shader B (cavity) and A (AO) of $blendModulateTexture (t15).
// Every level is encoded from the exact 2x2 box filtered level above it. Diagnostics (environment, off by default): R2TEX_UBERPACK_PSNR prints
// the per channel error of every carrier against its source planes, R2TEX_UBERPACK_TIME prints plane load and encode milliseconds.

enum class UberPackFormat { bc5, dxt1, bc3 };

// An AO-only carrier is stored as DXT1 only when its level 0 reaches this PSNR (dB) against the source plane, otherwise as BC5.
constexpr double kUberDxt1MinPsnr = 40.0;

// ---- source side ---------------------------------------------------------------------------------------------------------

// Linear red (0..255) of an sRGB flagged source: the table replaces a pow per texel.
const std::uint8_t* uberSrgbTable() {
    static const std::array<std::uint8_t, 256> table = [] {
        std::array<std::uint8_t, 256> t{};
        for (int i = 0; i < 256; ++i) t[i] = srgbByteToLinear(static_cast<std::uint8_t>(i));
        return t;
    }();
    return table.data();
}

// Red channel of mip 0 for BC1/BC2/BC3/BC4/BC5 sources straight from the blocks (no RGBA8 intermediate). False for any other format.
bool uberRedFromBlocks(const std::vector<std::uint8_t>& vtf, const VtfView& view, std::vector<std::uint8_t>& out) {
    const std::uint32_t f = view.format;
    if (f != 13 && f != 14 && f != 15 && f != 34 && f != 35) return false;
    std::size_t offset = view.dataOffset;
    for (std::uint32_t m = view.mipCount; m-- > 1;) {
        const std::size_t bytes = vtfMipBytes(f, std::max(1u, view.width >> m), std::max(1u, view.height >> m));
        if (!bytes) return false;
        offset += bytes;
    }
    const std::uint32_t w = view.width, h = view.height;
    const std::size_t bytes = vtfMipBytes(f, w, h);
    if (!bytes || offset > vtf.size() || bytes > vtf.size() - offset) return false;
    const std::size_t blockBytes = (f == 13 || f == 35) ? 8 : 16;
    const std::uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    out.resize(static_cast<std::size_t>(w) * h);
    const std::uint8_t* src = vtf.data() + offset;
    std::uint8_t tmp[64];
    std::uint8_t tmp2[32];
    for (std::uint32_t by = 0; by < bh; ++by)
        for (std::uint32_t bx = 0; bx < bw; ++bx) {
            const std::uint8_t* b = src + (static_cast<std::size_t>(by) * bw + bx) * blockBytes;
            if (w % 4 == 0 && h % 4 == 0 && f == 35) {
                bcdec_bc4(b, out.data() + static_cast<std::size_t>(by) * 4 * w + bx * 4, static_cast<int>(w), 0);
                continue;
            }
            std::uint8_t red[16];
            if (f == 35) bcdec_bc4(b, red, 4, 0);
            else if (f == 34) { bcdec_bc5(b, tmp2, 8, 0); for (int i = 0; i < 16; ++i) red[i] = tmp2[i * 2]; }
            else {
                if (f == 13) bcdec_bc1(b, tmp, 16);
                else if (f == 14) bcdec_bc2(b, tmp, 16);
                else bcdec_bc3(b, tmp, 16);
                for (int i = 0; i < 16; ++i) red[i] = tmp[i * 4];
            }
            for (std::uint32_t py = 0; py < 4 && by * 4 + py < h; ++py)
                for (std::uint32_t px = 0; px < 4 && bx * 4 + px < w; ++px)
                    out[static_cast<std::size_t>(by * 4 + py) * w + bx * 4 + px] = red[py * 4 + px];
        }
    return true;
}

// ---- block encoders --------------------------------------------------------------------------------------------------------

// One BC4 block (8 byte), 8-level mode: endpoints = block max / min, every texel takes its nearest of the 8 evenly spaced levels
// (closed form, no palette search). a0 = max (> a1) selects the 8 level palette; a flat block stores index 0 everywhere.
void uberEncodeBc4(const std::uint8_t (&v)[16], std::uint8_t* out) {
    int lo = 255, hi = 0;
    for (const std::uint8_t x : v) { lo = std::min<int>(lo, x); hi = std::max<int>(hi, x); }
    out[0] = static_cast<std::uint8_t>(hi);
    out[1] = static_cast<std::uint8_t>(lo);
    if (hi == lo) { std::memset(out + 2, 0, 6); return; }
    const int range = hi - lo;
    std::uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) {
        const int rank = ((hi - v[i]) * 7 + range / 2) / range;   // 0 at the max .. 7 at the min
        const std::uint64_t index = rank == 0 ? 0 : (rank == 7 ? 1 : static_cast<std::uint64_t>(rank) + 1);
        bits |= index << (3 * i);
    }
    for (int i = 0; i < 6; ++i) out[2 + i] = static_cast<std::uint8_t>(bits >> (8 * i));
}

// One DXT1 block (8 byte) of a single value ramp: texel = (255, v, 255) with 6 bit green endpoints (R = 31 and B = 31 expand to 255 exactly). The
// palette is E0, E1, (2 E0 + E1) / 3, (E0 + 2 E1) / 3 with E = (g << 2) | (g >> 4), c0 > c1 selects the four colour mode. The endpoint pair is the
// best (squared error over the 16 texels) of the 3 x 3 neighbours of the quantised block max / min; the squared error is returned. A flat block stores
// one colour (c0 = c1, index 0).
int uberEncodeDxt1Ramp(const std::uint8_t (&v)[16], std::uint8_t* out) {
    int lo = 255, hi = 0;
    for (const std::uint8_t x : v) { lo = std::min<int>(lo, x); hi = std::max<int>(hi, x); }
    const int qhi = (hi * 63 + 127) / 255, qlo = (lo * 63 + 127) / 255;
    auto expand = [](int g) { return (g << 2) | (g >> 4); };
    int bestError = std::numeric_limits<int>::max(), bestA = qhi, bestB = qlo;
    std::uint32_t bestIndices = 0;
    for (int da = -1; da <= 1; ++da)
        for (int db = -1; db <= 1; ++db) {
            const int a = std::clamp(qhi + da, 0, 63), b = std::clamp(qlo + db, 0, 63);
            if (a < b) continue;
            int pal[4];
            pal[0] = expand(a); pal[1] = expand(b);
            if (a == b) { pal[2] = pal[3] = pal[0]; pal[1] = pal[0]; }
            else { pal[2] = (2 * pal[0] + pal[1]) / 3; pal[3] = (pal[0] + 2 * pal[1]) / 3; }
            int error = 0;
            std::uint32_t indices = 0;
            for (int i = 0; i < 16; ++i) {
                int best = 0, bestD = std::abs(v[i] - pal[0]);
                for (int k = 1; k < 4; ++k) {
                    const int d = std::abs(v[i] - pal[k]);
                    if (d < bestD) { bestD = d; best = k; }
                }
                error += bestD * bestD;
                indices |= static_cast<std::uint32_t>(best) << (2 * i);
            }
            if (error < bestError) { bestError = error; bestA = a; bestB = b; bestIndices = indices; }
        }
    const std::uint16_t c0 = static_cast<std::uint16_t>(0xF81F | (bestA << 5)), c1 = static_cast<std::uint16_t>(0xF81F | (bestB << 5));
    out[0] = static_cast<std::uint8_t>(c0 & 255); out[1] = static_cast<std::uint8_t>(c0 >> 8);
    out[2] = static_cast<std::uint8_t>(c1 & 255); out[3] = static_cast<std::uint8_t>(c1 >> 8);
    for (int i = 0; i < 4; ++i) out[4 + i] = static_cast<std::uint8_t>(bestIndices >> (8 * i));
    return bestError;
}

// ---- mip chain + level encode --------------------------------------------------------------------------------------------

// One mip level as interleaved byte channels: single layer (AO, cavity), layered (mask, AO, cavity).
struct UberLevel {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> texel;
};

UberLevel uberBoxDown(const UberLevel& prev, std::size_t channels) {
    UberLevel next;
    next.width = std::max(1u, prev.width / 2);
    next.height = std::max(1u, prev.height / 2);
    next.texel.resize(static_cast<std::size_t>(next.width) * next.height * channels);
    const std::uint32_t pw = prev.width, ph = prev.height;
    for (std::uint32_t y = 0; y < next.height; ++y) {
        const std::uint32_t y0 = std::min(ph - 1, y * 2), y1 = std::min(ph - 1, y * 2 + 1);
        const std::uint8_t* row0 = prev.texel.data() + static_cast<std::size_t>(y0) * pw * channels;
        const std::uint8_t* row1 = prev.texel.data() + static_cast<std::size_t>(y1) * pw * channels;
        std::uint8_t* dst = next.texel.data() + static_cast<std::size_t>(y) * next.width * channels;
        for (std::uint32_t x = 0; x < next.width; ++x) {
            const std::uint32_t x0 = std::min(pw - 1, x * 2), x1 = std::min(pw - 1, x * 2 + 1);
            for (std::size_t c = 0; c < channels; ++c) {
                const unsigned sum = row0[x0 * channels + c] + row0[x1 * channels + c] + row1[x0 * channels + c] + row1[x1 * channels + c];
                dst[x * channels + c] = static_cast<std::uint8_t>((sum + 2) / 4);
            }
        }
    }
    return next;
}

std::uint32_t uberPackVtfFormat(UberPackFormat format) {
    return format == UberPackFormat::bc5 ? 34 : (format == UberPackFormat::dxt1 ? 13 : 15);
}

std::size_t uberBlockBytes(UberPackFormat format) { return format == UberPackFormat::dxt1 ? 8 : 16; }

std::size_t uberLevelBytes(UberPackFormat format, std::uint32_t w, std::uint32_t h) {
    return static_cast<std::size_t>((w + 3) / 4) * ((h + 3) / 4) * uberBlockBytes(format);
}

// Encodes one level; returns the squared error of the DXT1 blocks (0 for the other formats).
std::uint64_t uberEncodeLevel(const UberLevel& level, UberPackFormat format, bool layered, std::uint8_t* out) {
    const std::size_t nc = layered ? 3 : 2;
    const std::uint32_t w = level.width, h = level.height;
    std::uint64_t sse = 0;
    const std::uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (std::uint32_t by = 0; by < bh; ++by)
        for (std::uint32_t bx = 0; bx < bw; ++bx) {
            std::uint8_t v[3][16];
            for (std::uint32_t py = 0; py < 4; ++py) {
                const std::uint32_t y = std::min(h - 1, by * 4 + py);
                for (std::uint32_t px = 0; px < 4; ++px) {
                    const std::uint32_t x = std::min(w - 1, bx * 4 + px);
                    const std::uint8_t* t = level.texel.data() + (static_cast<std::size_t>(y) * w + x) * nc;
                    for (std::size_t c = 0; c < nc; ++c) v[c][py * 4 + px] = t[c];
                }
            }
            std::uint8_t* block = out + (static_cast<std::size_t>(by) * bw + bx) * uberBlockBytes(format);
            if (format == UberPackFormat::bc5) {
                uberEncodeBc4(v[1], block);       // R = cavity
                uberEncodeBc4(v[0], block + 8);   // G = AO
            } else if (format == UberPackFormat::bc3) {
                std::uint8_t color[16][3];
                for (int i = 0; i < 16; ++i) { color[i][0] = v[0][i]; color[i][1] = v[0][i]; color[i][2] = v[2][i]; }
                uberEncodeBc4(v[1], block);       // alpha = AO
                encodeBc1Block(color, block + 8); // RGB = (mask, mask, cavity)
            } else {   // dxt1 ramp: R = B = 255, G = AO
                sse += static_cast<std::uint64_t>(uberEncodeDxt1Ramp(v[0], block));
            }
        }
    return sse;
}

// Decodes one encoded level back to the UberLevel channel layout (R2TEX_UBERPACK_PSNR only).
void uberDecodeLevel(const std::uint8_t* data, UberPackFormat format, bool layered, std::uint32_t w, std::uint32_t h, std::vector<std::uint8_t>& texel) {
    const std::size_t nc = layered ? 3 : 2;
    texel.assign(static_cast<std::size_t>(w) * h * nc, 0);
    const std::uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (std::uint32_t by = 0; by < bh; ++by)
        for (std::uint32_t bx = 0; bx < bw; ++bx) {
            std::uint8_t v[3][16] = {};
            const std::uint8_t* b = data + (static_cast<std::size_t>(by) * bw + bx) * uberBlockBytes(format);
            std::uint8_t rgba[64];
            if (format == UberPackFormat::bc5) {
                bcdec_bc4(b, v[1], 4, 0);       // R = cavity
                bcdec_bc4(b + 8, v[0], 4, 0);   // G = AO
            } else if (format == UberPackFormat::bc3) {
                bcdec_bc3(b, rgba, 16);
                for (int i = 0; i < 16; ++i) { v[0][i] = rgba[i * 4]; v[1][i] = rgba[i * 4 + 3]; v[2][i] = rgba[i * 4 + 2]; }
            } else {
                bcdec_bc1(b, rgba, 16);
                for (int i = 0; i < 16; ++i) { v[0][i] = rgba[i * 4 + 1]; v[1][i] = rgba[i * 4]; }
            }
            for (std::uint32_t py = 0; py < 4 && by * 4 + py < h; ++py)
                for (std::uint32_t px = 0; px < 4 && bx * 4 + px < w; ++px)
                    for (std::size_t c = 0; c < nc; ++c)
                        texel[(static_cast<std::size_t>(by * 4 + py) * w + bx * 4 + px) * nc + c] = v[c][py * 4 + px];
        }
}

// Builds the carrier VTF from level 0 planes. `mask` is null for the single layer form. `ao` / `cavity` hold the planes already resized to
// width x height (empty = absent = 255).
bool encodeUberPackVtf(std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>* mask, const std::vector<std::uint8_t>& ao,
                       const std::vector<std::uint8_t>& cavity, std::vector<std::uint8_t>& vtf, const char* debugName) {
    const auto t0 = std::chrono::steady_clock::now();
    const bool layered = mask != nullptr;
    const std::size_t nc = layered ? 3 : 2;
    const std::size_t texels = static_cast<std::size_t>(width) * height;
    UberLevel level;
    level.width = width; level.height = height;
    level.texel.resize(texels * nc);
    for (std::size_t p = 0; p < texels; ++p) {
        std::uint8_t* t = level.texel.data() + p * nc;
        std::size_t c = 0;
        if (layered) t[c++] = (*mask)[p];
        t[c++] = ao.empty() ? 255 : ao[p];
        t[c++] = cavity.empty() ? 255 : cavity[p];
    }
    UberPackFormat format = layered ? UberPackFormat::bc3 : (cavity.empty() ? UberPackFormat::dxt1 : UberPackFormat::bc5);
    // AO-only carriers are DXT1 unless the block fit is poor (level 0 PSNR below kUberDxt1MinPsnr): the shader decodes both layouts the same way
    // (R = cavity = 255, G = AO), so the texture just becomes BC5. The level 0 blocks are kept to avoid encoding them twice.
    std::vector<std::uint8_t> top;
    if (format == UberPackFormat::dxt1) {
        top.resize(uberLevelBytes(format, width, height));
        const std::uint64_t sse = uberEncodeLevel(level, format, layered, top.data());
        if (static_cast<double>(sse) > static_cast<double>(texels) * 255.0 * 255.0 / std::pow(10.0, kUberDxt1MinPsnr / 10.0)) {
            format = UberPackFormat::bc5;
            std::vector<std::uint8_t>().swap(top);
        }
    }
    std::size_t levels = 1;
    for (std::uint32_t w = width, h = height; w > 1 || h > 1; w = std::max(1u, w / 2), h = std::max(1u, h / 2)) ++levels;
    // Mips are stored smallest first.
    std::vector<std::size_t> offsets(levels);
    std::size_t total = 0x58;
    {
        std::vector<std::size_t> sizes(levels);
        std::uint32_t w = width, h = height;
        for (std::size_t m = 0; m < levels; ++m, w = std::max(1u, w / 2), h = std::max(1u, h / 2)) sizes[m] = uberLevelBytes(format, w, h);
        for (std::size_t m = levels; m-- > 0;) { offsets[m] = total; total += sizes[m]; }
    }
    vtf.assign(total, 0);
    const bool eval = std::getenv("R2TEX_UBERPACK_PSNR") != nullptr;
    double sq[3] = {0, 0, 0}, sq0[3] = {0, 0, 0}, ae0[3] = {0, 0, 0};
    int maxErr0[3] = {0, 0, 0};
    std::size_t count[3] = {0, 0, 0}, count0 = 0;
    std::vector<std::uint8_t> decoded;
    for (std::size_t m = 0;; ++m) {
        if (m == 0 && !top.empty()) std::memcpy(vtf.data() + offsets[0], top.data(), top.size());
        else uberEncodeLevel(level, format, layered, vtf.data() + offsets[m]);
        if (eval) {
            uberDecodeLevel(vtf.data() + offsets[m], format, layered, level.width, level.height, decoded);
            const std::size_t n = static_cast<std::size_t>(level.width) * level.height;
            for (std::size_t p = 0; p < n; ++p)
                for (std::size_t c = 0; c < nc; ++c) {
                    const int d = static_cast<int>(decoded[p * nc + c]) - level.texel[p * nc + c];
                    sq[c] += static_cast<double>(d) * d; ++count[c];
                    if (m == 0) { sq0[c] += static_cast<double>(d) * d; ae0[c] += std::abs(d); maxErr0[c] = std::max(maxErr0[c], std::abs(d)); }
                }
            if (m == 0) count0 = n;
        }
        if (m + 1 == levels) break;
        level = uberBoxDown(level, nc);
    }
    if (eval) {
        auto psnr = [](double s, std::size_t n) { return s <= 0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 * n / s); };
        const char* names2[3] = {"ao", "cav", "-"};
        const char* names3[3] = {"mask", "ao", "cav"};
        std::string line = std::string("UBERPSNR ") + (debugName ? debugName : "?") + " " + std::to_string(width) + "x" + std::to_string(height) +
                           " fmt=" + std::to_string(uberPackVtfFormat(format)) + " bytes=" + std::to_string(total);
        for (std::size_t c = 0; c < nc; ++c) {
            char buf[160];
            std::snprintf(buf, sizeof buf, " %s[mip0 psnr %.2f mae %.3f max %d | all psnr %.2f]", (layered ? names3 : names2)[c],
                          psnr(sq0[c], count0), ae0[c] / count0, maxErr0[c], psnr(sq[c], count[c]));
            line += buf;
        }
        std::fprintf(stderr, "%s\n", line.c_str());
    }
    // Same VTF 7.5 header as convertTextureWithDb; flags 0 (no sRGB, no clamp: the AO/cavity UVs are the base UVs).
    std::memcpy(vtf.data(), "VTF\0", 4);
    put32(vtf.data() + 4, 7); put32(vtf.data() + 8, 5); put32(vtf.data() + 12, 0x58);
    put16(vtf.data() + 0x10, static_cast<std::uint16_t>(width)); put16(vtf.data() + 0x12, static_cast<std::uint16_t>(height));
    put32(vtf.data() + 0x14, 0);
    put16(vtf.data() + 0x18, 1); put16(vtf.data() + 0x1a, 0);
    putFloat(vtf.data() + 0x20, 0.5f); putFloat(vtf.data() + 0x24, 0.5f); putFloat(vtf.data() + 0x28, 0.5f);
    putFloat(vtf.data() + 0x30, 1.0f);
    putI32(vtf.data() + 0x34, static_cast<std::int32_t>(uberPackVtfFormat(format)));
    vtf[0x38] = static_cast<std::uint8_t>(levels);
    putI32(vtf.data() + 0x39, -1); vtf[0x3d] = 0; vtf[0x3e] = 0; put16(vtf.data() + 0x3f, 1);
    put32(vtf.data() + 0x44, 1);
    vtf[0x50] = 0x30; vtf[0x51] = 0; vtf[0x52] = 0; vtf[0x53] = 0; put32(vtf.data() + 0x54, 0x58);
    if (std::getenv("R2TEX_UBERPACK_TIME"))
        std::fprintf(stderr, "UBERTIME %s %ux%u fmt=%u encode_ms=%.1f\n", debugName ? debugName : "?", static_cast<unsigned>(width), static_cast<unsigned>(height),
                     uberPackVtfFormat(format), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    return true;
}
