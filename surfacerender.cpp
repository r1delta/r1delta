#include "surfacerender.h"
#include "vguisurface.h"
#include "localize.h"
#include "load.h"
#include "squirrel.h"
#include "cvar.h"
#include "logging.h"
#include <cwctype>
#include "r1d_version.h"
#include <vector>
#include <algorithm> // for std::max
#include <ctime>
#include <cmath> // for M_PI and math functions

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Forward declare or include Vector type
#include "public/vector.h"

// Externs for watermark data collection
extern char g_cl_MapDisplayName[128];
extern uintptr_t G_engine;
extern uintptr_t G_client;

// This struct holds the information for one floating damage number
struct DamageNumber_t
{
    int damage;
    Vector worldPos;
    float spawnTime;
    bool isCritical;
    float batchWindow;
    int sourceID;
};

// Global list to hold all active damage numbers
std::vector<DamageNumber_t> g_DamageNumbers;

// Font handles for drawing
vgui::HFont DamageNumberFont = 0;
vgui::HFont DamageNumberCritFont = 0;

vgui::IPanel* panel = nullptr;
vgui::ISurface* surface = nullptr;

char fpsStringData[4096] = { 0 };
bool g_bIsDrawingFPSPanel = false;

// Constants derived from dumps
#define OFF_PR_CONTAINER 0xBECA50   // qword_180BECA50
#define OFF_PR_INTERFACE 0x1B18     // 6936 decimal
#define OFF_PR_PING      7080
#define OFF_PR_KILLS     7216
#define OFF_PR_DEATHS    7284
#define OFF_GEN          13848
#define OFF_XP           13840
#define OFF_VELOCITY     0x1F0
#define XP_AT_MAX_LEVEL  900000.0f



// Helper to get the PlayerResource pointer
void* GetPlayerResource() {
	// Read the container pointer
	uintptr_t container = *(uintptr_t*)(G_client + OFF_PR_CONTAINER);
	if (!container) return nullptr;

	// The interface is embedded at this offset, so we just add, not dereference
	return (void*)(container + OFF_PR_INTERFACE);
}

void UpdateWatermarkData()
{
	// Clear the buffer
	memset(fpsStringData, 0, sizeof(fpsStringData));

	// 1. Get Local Player
	static auto getlocalplayer = reinterpret_cast<void* (*)(int)>((uintptr_t)G_client + 0x7B120);
	void* pLocalPlayer = getlocalplayer(-1);

	if (!pLocalPlayer || !G_engine) {
		strncpy(fpsStringData, "No local player", sizeof(fpsStringData) - 1);
		return;
	}

	// Get engine client interface pointer
	void* pEngineClient = (void*)(G_engine + 0x795650);
	if (!pEngineClient) {
		strncpy(fpsStringData, "No engine client", sizeof(fpsStringData) - 1);
		return;
	}

	// 2. FPS - calculated from frame time using gpClientGlobals
	// gpClientGlobals is at G_client + 0xAEA640 (same struct layout as CGlobalVarsServer2015)
	void* gpClientGlobals = *(void**)(G_client + 0xAEA640);
	float absoluteframetime = *(float*)((uintptr_t)gpClientGlobals + 0x0C);
	float fps = (absoluteframetime > 0) ? (1.0f / absoluteframetime) : 0.0f;

	// 3. Velocity
	float* vel = (float*)((uintptr_t)pLocalPlayer + OFF_VELOCITY);
	float velocity = sqrtf(vel[0]*vel[0] + vel[1]*vel[1]);

	// 4. Network Stats
	int pingMs = 0;
	int lossIn = 0;
	int lossOut = 0;
	float serverVar = 0.0f;

	// Engine->GetNetChannelInfo (Index 160 derived from offset 1280)
	void* nci = CallVFunc<void*>(160, pEngineClient);

	if (nci) {
		// GetAvgLatency (Flow Outgoing = 1)
		float latency = CallVFunc<float>(10, nci, 1);
		pingMs = (int)(latency * 1000.0f);

		// GetAvgLoss
		float fLossIn = CallVFunc<float>(11, nci, 0);
		float fLossOut = CallVFunc<float>(11, nci, 1);
		lossIn = (int)(fLossIn * 100.0f);
		lossOut = (int)(fLossOut * 100.0f);

		// GetRemoteFramerate (Index 25)
		float frameTime = 0, frameTimeStdDev = 0, startTimeStdDev = 0;
		CallVFunc<void>(25, nci, &frameTime, &frameTimeStdDev, &startTimeStdDev);
		serverVar = startTimeStdDev * 1000.0f;
	}

	// 5. Player Stats (K/D)
	float kdRatio = 0.0f;
	int localIndex = CallVFunc<int>(22, pEngineClient, 0); // GetLocalPlayer

	void* g_PR = GetPlayerResource();
	if (g_PR) {
		int* pKills = (int*)((uintptr_t)g_PR + OFF_PR_KILLS);
		int* pDeaths = (int*)((uintptr_t)g_PR + OFF_PR_DEATHS);
		int* pPings = (int*)((uintptr_t)g_PR + OFF_PR_PING);

		if (localIndex >= 0 && localIndex < 64) {
			int k = pKills[localIndex];
			int d = pDeaths[localIndex];

			if (!nci) pingMs = pPings[localIndex]; // Fallback ping

			kdRatio = (d > 0) ? ((float)k / (float)d) : (float)k;
		}
	}

	// 6. Generation Calculation (Decimal)
	// In R1: Internal Gen 0 = Gen 1. Internal Gen 9 = Gen 10.
	int rawGen = *(int*)((uintptr_t)pLocalPlayer + OFF_GEN);
	int currentXP = *(int*)((uintptr_t)pLocalPlayer + OFF_XP);

	// Calculate progress through current gen (0.0 to 1.0)
	float progress = (float)currentXP / XP_AT_MAX_LEVEL;
	if (progress > 0.99f) progress = 0.99f; // Clamp so we don't show Gen 11.00 visually

	// Format: (InternalGen + 1) + progress
	float genDisplay = (float)(rawGen + 1) + progress;

	// 7. Player Name (Fallback if Discord RPC hasn't fired or for safety)
	const char* playerName = "Pilot";
	static auto nameVar = OriginalCCVar_FindVar(cvarinterface, "name");
	if (nameVar && nameVar->m_Value.m_pszString) {
		playerName = nameVar->m_Value.m_pszString;
	}

	// 8. Format into buffer
	snprintf(fpsStringData, sizeof(fpsStringData),
		"%d frames / sec | %s | %.0f u/s speed\n"
		"%dms ping time | %d%% / %d%% loss | %.1fms server var\n"
		"%s | %.1f kills/deaths | generation %.2f",
		(int)fps,
		g_cl_MapDisplayName,
		velocity,
		pingMs,
		lossIn,
		lossOut,
		serverVar,
		playerName,
		kdRatio,
		genDisplay
	);
}
__int64 (*osub_1800165C0)(
    __int64 a1,
    __int64 a2,
    __int64 a3,
    __int64 a4,
    int a5,
    unsigned int a6,
    unsigned int a7,
    int a8,
    const char* fmt,
    ...);
__int64 (*sub_180016490)(
    __int64 a1,
    __int64 a2,
    __int64 a3,
    __int64 a4,
    int a5,
    unsigned int a6,
    unsigned int a7,
    int a8,
    const char* fmt,
    va_list va);

__int64 sub_1800165C0(
    __int64 a1,
    __int64 a2,
    __int64 a3,
    __int64 a4,
    int a5,
    unsigned int a6,
    unsigned int a7,
    int a8,
    const char* fmt,
    ...)
{
    va_list args;
    va_start(args, fmt);

    if (fmt[0] == 'n') {
        // name: %s
        g_bIsDrawingFPSPanel = false;
    }

    if (!g_bIsDrawingFPSPanel) {
        // if we're not drawing the FPS panel, just call the real function
        auto result = sub_180016490(a1, a2, a3, a4, a5, a6, a7, a8, fmt, args);
        va_end(args);
        return result;
    }

    // otherwise, format into a temp buffer and append to fpsStringData
    char temp[4096];
    int len = vsnprintf(temp, sizeof(temp), (std::string(fmt) + std::string("\n")).c_str(), args);
    va_end(args);

    if (len > 2) {
        size_t cur = strnlen_s(fpsStringData, sizeof(fpsStringData));
        // how many bytes we can still copy (leave room for NUL)
        size_t space = sizeof(fpsStringData) - cur - 1;
        if (space > 0) {
            // append as much as will fit
            strncat_s(fpsStringData, sizeof(fpsStringData), temp, space);
        }
    }

    // we didn’t draw anything, so return 0
    return 0;
}
ConVarR1* cvar_delta_watermark = nullptr;
ConVarR1* cvar_delta_damage_numbers = nullptr;
ConVarR1* cvar_delta_damage_numbers_lifetime = nullptr;
ConVarR1* cvar_delta_damage_numbers_size = nullptr;
ConVarR1* cvar_delta_damage_numbers_crit_size = nullptr;
ConVarR1* cvar_delta_damage_numbers_batching = nullptr;
ConVarR1* cvar_delta_damage_numbers_batching_window = nullptr;

// Define the function pointer type for GetVectorInScreenSpace
typedef bool(*GetVectorInScreenSpace_t)(Vector, int&, int&, Vector*);
GetVectorInScreenSpace_t GetVectorInScreenSpace_ptr = nullptr;

__int64(*osub_18028BEA0)(__int64 a1, __int64 a2, double a3);
__int64 __fastcall sub_18028BEA0(__int64 a1, __int64 a2, double a3) {
    static auto cl_showfps = OriginalCCVar_FindVar(cvarinterface, "cl_showfps");
    bool isDrawing = cl_showfps->m_Value.m_nValue == 1 && cvar_delta_watermark->m_Value.m_nValue == 1;
    g_bIsDrawingFPSPanel = isDrawing;

    // Update watermark data if enabled
    if (isDrawing) {
        UpdateWatermarkData();
    } else {
        // Clear your buffer if not drawing
        memset(fpsStringData, 0, sizeof(fpsStringData));
    }

    // This static remembers what the last state was
    static bool wasDrawing = false;
    static auto vguimatsurface = GetModuleHandleA("vguimatsurface.dll");
    auto hookAddr = (LPVOID)(((uintptr_t)vguimatsurface) + 0x165C0);

    // Only change the hook when the state flips
    if (isDrawing != wasDrawing) {
        if (isDrawing) {
            MH_EnableHook(hookAddr);
        }
        else {
            MH_DisableHook(hookAddr);
        }
        wasDrawing = isDrawing;
    }

    // call the original
    auto ret = osub_18028BEA0(a1, a2, a3);

    // reset your flag if you're doing one-shot draws, etc.
    g_bIsDrawingFPSPanel = false;
    return ret;
}
// at top of file (near other globals/fonts)
static vgui::HFont WatermarkTitleFont = 0;
static vgui::HFont WatermarkInfoFont = 0;
static vgui::HTexture WatermarkLogoTex = 0;  // optional

// Small helper to pick an accent color based on version/build
static inline void GetAccentRGBA(int& r, int& g, int& b)
{
    // dev → purple-ish, release → teal-ish
    if (!strcmp_static(R1D_VERSION, "dev"))
        r = 72, g = 34, b = 34;        // purple
    else
        r = 64, g = 180, b = 255;       // teal
}
// Truncate a wide string so its rendered width <= maxPx, appending an ellipsis.
// Uses the current ISurface + font metrics.
static std::wstring EllipsizeToWidth(const std::wstring& s, vgui::HFont font, int maxPx)
{
    if (maxPx <= 0) return L"";

    int w = 0, h = 0;
    surface->GetTextSize(font, s.c_str(), w, h);
    if (w <= maxPx) return s;

    // Use the single-character Unicode ellipsis. If you prefer three dots, use L"...".
    const wchar_t* ELL = L"\u2026";
    int ellW = 0, ellH = 0;
    surface->GetTextSize(font, ELL, ellW, ellH);
    if (ellW > maxPx) return L""; // no room for even the ellipsis

    // Trim from the end until "<prefix>… " fits. O(n) is fine here given tiny strings.
    std::wstring prefix = s;
    while (!prefix.empty())
    {
        std::wstring cand = prefix + ELL;
        surface->GetTextSize(font, cand.c_str(), w, h);
        if (w <= maxPx) return cand;
        prefix.pop_back();
    }
    return L"";
}
static inline void FormatClockANSI(char* out, size_t outSize)
{
    std::time_t t = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &t); // Windows-safe; uses local timezone
    std::strftime(out, outSize, "%I:%M %p %m/%d/%y", &lt); // "12:04 AM 11/05/25"
}
// Convert HSV to RGB for rainbow colors
static inline void HSVtoRGB(float h, float s, float v, int& r, int& g, int& b)
{
    float c = v * s;
    float x = c * (1.0f - fabs(fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float r1, g1, b1;
    if (h < 60) { r1 = c; g1 = x; b1 = 0; }
    else if (h < 120) { r1 = x; g1 = c; b1 = 0; }
    else if (h < 180) { r1 = 0; g1 = c; b1 = x; }
    else if (h < 240) { r1 = 0; g1 = x; b1 = c; }
    else if (h < 300) { r1 = x; g1 = 0; b1 = c; }
    else { r1 = c; g1 = 0; b1 = x; }

    r = (int)((r1 + m) * 255);
    g = (int)((g1 + m) * 255);
    b = (int)((b1 + m) * 255);
}

// sRGB helpers for generation color
static inline float srgb_to_linear(float c) {
    c /= 255.0f;
    return (c <= 0.04045f) ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static inline uint8_t linear_to_srgb(float l) {
    float c = (l <= 0.0031308f) ? 12.92f * l : 1.055f * powf(l, 1.0f / 2.4f) - 0.055f;
    if (c < 0) c = 0;
    if (c > 1) c = 1;
    return (uint8_t)lroundf(c * 255.0f);
}

struct RGB_t { uint8_t r, g, b; };

// --- Card background and contrast constants ----------------------------------
static const RGB_t kCardBg = {16, 16, 20};
static constexpr float kMinContrast = 6.5f;

// --- OKLab / OKLCH helpers ---------------------------------------------------
struct OKLab { float L, a, b; };
struct OKLCh { float L, C, h; }; // h in degrees [0..360)

static inline float cbrtf_safe(float x){ return copysignf(cbrtf(fabsf(x)), x); }

static OKLab oklab_from_linear_rgb(float r, float g, float b) {
    float l = 0.4122214708f*r + 0.5363325363f*g + 0.0514459929f*b;
    float m = 0.2119034982f*r + 0.6806995451f*g + 0.1073969566f*b;
    float s = 0.0883024619f*r + 0.2817188376f*g + 0.6299787005f*b;

    float l_ = cbrtf_safe(l), m_ = cbrtf_safe(m), s_ = cbrtf_safe(s);

    return {
        0.2104542553f*l_ + 0.7936177850f*m_ - 0.0040720468f*s_,
        1.9779984951f*l_ - 2.4285922050f*m_ + 0.4505937099f*s_,
        0.0259040371f*l_ + 0.7827717662f*m_ - 0.8086757660f*s_
    };
}

static void linear_rgb_from_oklab(const OKLab& L, float& r, float& g, float& b) {
    float l_ = L.L + 0.3963377774f*L.a + 0.2158037573f*L.b;
    float m_ = L.L - 0.1055613458f*L.a - 0.0638541728f*L.b;
    float s_ = L.L - 0.0894841775f*L.a - 1.2914855480f*L.b;

    float l = l_*l_*l_, m = m_*m_*m_, s = s_*s_*s_;

    r =  4.0767416621f*l - 3.3077115913f*m + 0.2309699292f*s;
    g = -1.2684380046f*l + 2.6097574011f*m - 0.3413193965f*s;
    b =  0.0045164051f*l - 0.0050040034f*m + 1.0000000000f*s;
}

static inline OKLCh oklch_from_oklab(const OKLab& L) {
    float C = sqrtf(L.a*L.a + L.b*L.b);
    float h = atan2f(L.b, L.a) * 180.0f / (float)M_PI;
    if (h < 0) h += 360.0f;
    return { L.L, C, h };
}
static inline OKLab oklab_from_oklch(const OKLCh& c) {
    float hr = c.h * (float)M_PI / 180.0f;
    return { c.L, c.C * cosf(hr), c.C * sinf(hr) };
}

static inline RGB_t srgb_from_oklch(const OKLCh& c) {
    OKLab L = oklab_from_oklch(c);
    float rl, gl, bl; linear_rgb_from_oklab(L, rl, gl, bl);

    // Clamp linear then convert to sRGB bytes
    rl = fminf(fmaxf(rl, 0.0f), 1.0f);
    gl = fminf(fmaxf(gl, 0.0f), 1.0f);
    bl = fminf(fmaxf(bl, 0.0f), 1.0f);

    return RGB_t{ linear_to_srgb(rl), linear_to_srgb(gl), linear_to_srgb(bl) };
}

// Shortest-arc OKLCH interpolation
static inline OKLCh oklch_lerp(const OKLCh& a, const OKLCh& b, float t) {
    float L = a.L + (b.L - a.L)*t;
    float C = a.C + (b.C - a.C)*t;
    float dh = b.h - a.h;
    if (fabsf(dh) > 180.0f) dh -= copysignf(360.0f, dh);
    float h = a.h + dh * t;
    if (h < 0) h += 360.0f; else if (h >= 360.0f) h -= 360.0f;
    return { L, C, h };
}

struct GenAnchor { float gen; float L, C, h; }; // OKLCH

// Lore-preserving, visually separated anchors
static const GenAnchor GEN_ANCHORS[] = {
    //  gen ,    L ,   C ,   h°
    { 1.0f, 0.92f, 0.02f,   0.0f },  // 1: white
    { 2.0f, 0.89f, 0.08f,  18.0f },  // 2: muted white-red (warm tilt)
    { 3.0f, 0.87f, 0.10f, 195.0f },  // 3: white/gray-teal (cool teal)
    { 4.0f, 0.78f, 0.00f,   0.0f },  // 4: grey (C≈0)
    { 5.0f, 0.83f, 0.16f,  40.0f },  // 5: vibrant orange-grey
    { 6.0f, 0.85f, 0.12f, 220.0f },  // 6: muted baby blue
    { 7.0f, 0.72f, 0.22f,  12.0f },  // 7: crimson red
    { 8.0f, 0.74f, 0.20f, 200.0f },  // 8: sea blue
    { 9.0f, 0.76f, 0.16f, 115.0f },  // 9: greenish-red → olive/green cast
    {10.0f, 0.90f, 0.08f, 150.0f },  // 10: green/white
    {11.0f, 0.76f, 0.22f, 285.0f },  // 11: vibrant purple
};
static const size_t N_GEN_ANCHORS = sizeof(GEN_ANCHORS)/sizeof(GEN_ANCHORS[0]);

static inline float rel_luminance(RGB_t c) {
    float r = srgb_to_linear(c.r), g = srgb_to_linear(c.g), b = srgb_to_linear(c.b);
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

static inline float contrast_ratio(RGB_t a, RGB_t b) {
    float L1 = rel_luminance(a), L2 = rel_luminance(b);
    if (L1 < L2) { float t = L1; L1 = L2; L2 = t; }
    return (L1 + 0.05f) / (L2 + 0.05f);
}

static RGB_t ensure_contrast_on_dark(RGB_t c, RGB_t bg, float min_ratio) {
    if (contrast_ratio(c, bg) >= min_ratio) return c;

    // Brighten until we meet contrast ratio
    float r = srgb_to_linear(c.r);
    float g = srgb_to_linear(c.g);
    float b = srgb_to_linear(c.b);

    const RGB_t white = { 255, 255, 255 };
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 22; i++) {
        float mid = (lo + hi) * 0.5f;
        RGB_t m = {
            linear_to_srgb(r + mid * (1.0f - r)),
            linear_to_srgb(g + mid * (1.0f - g)),
            linear_to_srgb(b + mid * (1.0f - b))
        };
        if (contrast_ratio(m, bg) >= min_ratio) hi = mid; else lo = mid;
    }
    return RGB_t{
        linear_to_srgb(r + hi * (1.0f - r)),
        linear_to_srgb(g + hi * (1.0f - g)),
        linear_to_srgb(b + hi * (1.0f - b))
    };
}

// Smooth transition (Hermite)
static inline float smoothstepf(float a, float b, float x) {
    float t = (x - a) / (b - a);
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Progress toward next gen: returns p in [0..1]
// If your decimal is 1..50, force level 1 -> 0.0 so "level 1" shows 0% next-gen.
static inline float NextGenProgress(float generation) {
    int g = (int)generation;
    int dec = (int)((generation - g) * 100.0f + 0.5f); // 0..50 (or 1..50 in your current feed)
    float p = (dec <= 1) ? 0.0f : (dec / 50.0f);       // level 40 => 0.8
    return std::clamp(p, 0.0f, 1.0f);
}

// Per-character rainbow as "next style" for gen 10
static inline OKLCh RainbowOKLChAt(int i, int len) {
    float t = (len > 1) ? (i / (float)(len - 1)) : 0.5f;
    float hue = 210.0f * t; // same rainbow span you used before
    int rr, gg, bb; HSVtoRGB(hue, 1.0f, 1.0f, rr, gg, bb);

    // sRGB 8-bit -> linear -> OKLab -> OKLCH
    OKLab lab = oklab_from_linear_rgb(
        srgb_to_linear((float)rr),
        srgb_to_linear((float)gg),
        srgb_to_linear((float)bb)
    );
    return oklch_from_oklab(lab);
}

static RGB_t gen_text_color(float gen) {
    // clamp to [1, 11]
    if (gen <= GEN_ANCHORS[0].gen) {
        return ensure_contrast_on_dark(srgb_from_oklch({GEN_ANCHORS[0].L, GEN_ANCHORS[0].C, GEN_ANCHORS[0].h}),
                                       kCardBg, kMinContrast);
    }
    if (gen >= GEN_ANCHORS[N_GEN_ANCHORS-1].gen) {
        return ensure_contrast_on_dark(srgb_from_oklch({GEN_ANCHORS[N_GEN_ANCHORS-1].L, GEN_ANCHORS[N_GEN_ANCHORS-1].C, GEN_ANCHORS[N_GEN_ANCHORS-1].h}),
                                       kCardBg, kMinContrast);
    }

    // find neighbor anchors
    size_t i = 0;
    while (i + 1 < N_GEN_ANCHORS && !(gen >= GEN_ANCHORS[i].gen && gen <= GEN_ANCHORS[i+1].gen)) ++i;

    const GenAnchor a = GEN_ANCHORS[i];
    const GenAnchor b = GEN_ANCHORS[i+1];
    float t = (gen - a.gen) / (b.gen - a.gen);

    OKLCh ca{a.L, a.C, a.h};
    OKLCh cb{b.L, b.C, b.h};
    OKLCh cmix = oklch_lerp(ca, cb, t);

    RGB_t out = srgb_from_oklch(cmix);

    // Keep your contrast guarantee
    return ensure_contrast_on_dark(out, kCardBg, kMinContrast);
}

void DrawWatermark()
{
    if (!cvar_delta_watermark || !cvar_delta_watermark->m_Value.m_nValue) return;

    // Lazy init fonts / tiny logo
    if (!WatermarkTitleFont) {
        WatermarkTitleFont = surface->CreateFont();
        surface->SetFontGlyphSet(
            WatermarkTitleFont, "Verdana", 15, 750, 0, 0,
            vgui::FONTFLAG_ANTIALIAS | vgui::FONTFLAG_DROPSHADOW);
    }
    if (!WatermarkInfoFont) {
        WatermarkInfoFont = surface->CreateFont();
        surface->SetFontGlyphSet(
            WatermarkInfoFont, "Verdana", 12, 450, 0, 0,
            vgui::FONTFLAG_ANTIALIAS | vgui::FONTFLAG_DROPSHADOW);
    }
    if (!WatermarkLogoTex) {
        WatermarkLogoTex = surface->CreateNewTextureID();
        surface->DrawSetTextureFile(WatermarkLogoTex, "ui/menu/r1delta/icon", 0, false); // optional
    }

    int screenW = 0, screenH = 0;
    surface->GetScreenSize(screenW, screenH);

    // --- Title ---------------------------------------------------------------
    char ansiTitle[256];
    if (!strcmp_static(R1D_VERSION, "dev"))
        snprintf(ansiTitle, sizeof(ansiTitle), "R1Delta [dev]");
    else
        snprintf(ansiTitle, sizeof(ansiTitle), "R1Delta %s", R1D_VERSION);

    // Build the live clock string every frame
    char clockAnsi[64];
    FormatClockANSI(clockAnsi, sizeof(clockAnsi));

    wchar_t wTitle[256];
    wchar_t wClock[64];
    G_localizeIface->ConvertANSIToUnicode(ansiTitle, wTitle, sizeof(wTitle));
    G_localizeIface->ConvertANSIToUnicode(clockAnsi, wClock, sizeof(wClock));

    int titleW = 0, titleH = 0;
    int clockW = 0, clockH = 0;
    surface->GetTextSize(WatermarkTitleFont, wTitle, titleW, titleH);
    surface->GetTextSize(WatermarkTitleFont, wClock, clockW, clockH);

    // --- Info as 3×3 table ---------------------------------------------------
    const char* infoAnsi = (fpsStringData[0] != '\0')
        ? fpsStringData
        : "560 frames / sec | smuggler's cove | 420 u/s velocity\n585ms ping time | 100% / 100% loss | 16ms server var\nwaywardVagabond | 10.1 kills/deaths | generation 10.04";

    wchar_t wInfoBlock[4096];
    G_localizeIface->ConvertANSIToUnicode(infoAnsi, wInfoBlock, sizeof(wInfoBlock));

    auto trim_ws = [](std::wstring s) {
        size_t a = 0, b = s.size();
        while (a < b && iswspace(s[a])) ++a;
        while (b > a && iswspace(s[b - 1])) --b;
        return s.substr(a, b - a);
        };

    std::vector<std::vector<std::wstring>> tableRows;
    {
        wchar_t tmp[4096];
        wcsncpy_s(tmp, wInfoBlock, _TRUNCATE);
        wchar_t* lineCtx = nullptr;
        for (wchar_t* line = wcstok_s(tmp, L"\n", &lineCtx);
            line;
            line = wcstok_s(nullptr, L"\n", &lineCtx))
        {
            std::vector<std::wstring> cols;
            wchar_t* cellCtx = nullptr;
            for (wchar_t* cell = wcstok_s(line, L"|", &cellCtx);
                cell;
                cell = wcstok_s(nullptr, L"|", &cellCtx))
            {
                cols.emplace_back(trim_ws(cell));
            }
            if (!cols.empty()) tableRows.emplace_back(std::move(cols));
        }
    }

    const int rows = (int)tableRows.size();
    const int cols = rows ? (int)tableRows[0].size() : 0;

    // --- Layout constants ----------------------------------------------------
    const int padX = 0;
    const int padY = 8;
    const int gapTitleInfo = 6; // space between title and table (hairline sits here visually)
    const int accentW = 2;
    const int logoSize = 16;
    const int logoGap = 6;
    const int TITLE_Y_OFFSET = -2; // lift the title/clock up ~2px

    const int cellPadX = 8;
    const int cellPadY = 2;
    const int gridT = 1; // inner grid thickness
    const int MAX_COL_CHARS = 12;
    int maxColWidthPx = 0;
    {
        int wW = 0, hW = 0;
        std::wstring widest(MAX_COL_CHARS, L'W');           // "WWWWWWWW"
        surface->GetTextSize(WatermarkInfoFont, widest.c_str(), wW, hW);
        maxColWidthPx = wW + cellPadX * 2;                  // include cell padding
    }

    // Measure table (per-column widths only). Row height will be uniform.
    int tableW = 0, tableH = 0;
    std::vector<int> colW(cols, 0);
    std::vector<int> rowH(rows, 0);
    const int infoTall = surface->GetFontTall(WatermarkInfoFont); // constant per font

    if (rows && cols) {
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                int cw = 0, dummy = 0;
                surface->GetTextSize(WatermarkInfoFont, tableRows[r][c].c_str(), cw, dummy);

                // desired pixel width for this cell's content (with padding)
                int desired = cw + cellPadX * 2;

                // hard cap to 8×'W' width
                desired = std::min(desired, maxColWidthPx);

                colW[c] = std::max(colW[c], desired);
                rowH[r] = 0; // placeholder; we'll set a uniform value below
            }
        }

        for (int c = 0; c < cols; ++c) tableW += colW[c];
        tableW += std::max(0, cols - 1) * gridT;

        // Uniform row height. You said "there's no reason they can't all be 17 px."
        const int uniformRowH = 17;
        for (int r = 0; r < rows; ++r) rowH[r] = uniformRowH;
        tableH = rows * uniformRowH + std::max(0, rows - 1) * gridT;
    }


    // Card/content size
    const bool hasLogo = surface->IsTextureIDValid(WatermarkLogoTex);
    const int  logoLeftW = (hasLogo ? (logoSize + logoGap) : 0);

    // Left side (logo + title text) width:
    const int  titleRowLeftW = logoLeftW + titleW;

    // Space between title and clock on the same row:
    const int  titleClockGap = 16;

    // Make sure content width fits table and the entire title+gap+clock row
    const int  titleRowCombinedW = titleRowLeftW + titleClockGap + clockW;
    const int  contentW = std::max(titleRowCombinedW, tableW);

    const int cardW = accentW + padX * 2 + contentW;
    // Compute actual gap from title to table (will be 2 px with the topEdge math)
    const int actualGap = 2; // hairline is +1, topEdge is +1 more


    // --- Positioning (flush with top-right) ---------------------------------
    const int marginTop = 0, marginRight = 0;
    const int cardX0 = screenW - cardW - marginRight;
    const int cardY0 = marginTop;
    const int cardX1 = cardX0 + cardW;

    // Hairline and table origin (use hairline as table top, 1px below it)
    const int hairY = cardY0 + padY + titleH + TITLE_Y_OFFSET + 1;
    const int tX    = cardX0 + accentW + padX;
    const int tY    = hairY + 1;                  // <-- actual table top

    // Content edges (for title/clock, hairline, horizontal grid alignment)
    const int contentLeft  = tX;
    const int contentRight = cardX1 - padX;      // right edge of content

    // Table visual edges (for vertical grid lines)
    const int leftEdge  = tX;
    const int rightEdge = tX + tableW;

    // Bottom padding: add 2px below table
    const int bottomPad = 2;
    const int contentBottom = rows ? (tY + tableH) : (cardY0 + padY + titleH + TITLE_Y_OFFSET);
    const int cardH = (contentBottom - cardY0) + bottomPad;
    const int cardY1 = cardY0 + cardH;

    // Bubble rect with slight top/right bleed - left edge aligns with hairline/table
    const int bubbleBorderT = 1;
    const int bubbleX0 = leftEdge - accentW;      // align with accent bar, which is to the left of table
    const int bubbleY0 = cardY0 - bubbleBorderT;  // bleed up 1px
    const int bubbleX1 = cardX1 + bubbleBorderT;  // bleed right 1px
    const int bubbleY1 = cardY1;                  // ends exactly at content bottom (+bottomPad)



    // --- Background card -----------------------------------------------------
    surface->DrawWordBubble(
        bubbleX0, bubbleY0, bubbleX1, bubbleY1,
        /*borderThickness=*/bubbleBorderT,
        /*bg*/   Color(16, 16, 20, 220),
        /*edge*/ Color(255, 255, 255, 20),
        /*bPointer=*/false);

    // From here on, work inside the content rect
    const int x0 = cardX0, y0 = cardY0, x1 = cardX1, y1 = cardY1;

    // Accent bar (aligned with bubble left edge)
    int ar = 0, ag = 0, ab = 0; GetAccentRGBA(ar, ag, ab);
    surface->DrawSetColor(ar, ag, ab, 255);
    surface->DrawFilledRect(leftEdge - accentW, y0, leftEdge, y1);

    // Header highlight & hairline (aligned with content right edge)
    surface->DrawFilledRectFade(leftEdge, y0, contentRight, y0 + 6, /*a0*/60, /*a1*/0, /*bHorizontal=*/false);
    surface->DrawSetColor(255, 255, 255, 10);
    surface->DrawFilledRect(leftEdge, hairY, contentRight, hairY + 1);


    // --- Title row -----------------------------------------------------------
    int curX = tX + cellPadX;
    int curY = y0 + padY;

    if (hasLogo) {
        surface->DrawSetTexture(WatermarkLogoTex);
        surface->DrawSetColor(255, 255, 255, 220);
        surface->DrawTexturedRect(
            curX,
            (curY + (titleH - logoSize) / 2) - 3,
            curX + logoSize,
            ((curY + (titleH - logoSize) / 2) - 3) + logoSize);
        curX += logoSize + logoGap;
    }

    surface->DrawSetTextFont(WatermarkTitleFont);
    surface->DrawSetTextColor(255, 255, 255, 255);

    // Left: title (lifted)
    surface->DrawSetTextPos(curX, curY + TITLE_Y_OFFSET);
    surface->DrawPrintText(wTitle, (int)wcslen(wTitle));

    // Right: live clock (same row/font, right-aligned to content edge)
    const int clockX = contentRight - cellPadX - clockW;
    surface->DrawSetTextPos(clockX, curY + TITLE_Y_OFFSET);
    surface->DrawPrintText(wClock, (int)wcslen(wClock));


    // --- Table (inner lines that run to card edges) --------------------------
    if (rows && cols) {
        // Grid edges match the table bounds (leftEdge/rightEdge already defined above)
        const int topEdge = tY;
        const int bottomEdge = tY + tableH + bottomPad;  // extend to match bottom padding

        // Inner grid lines
        surface->DrawSetColor(255, 255, 255, 20);

        // Vertical lines: at column boundaries, from topEdge to bottomEdge
        int gx = tX;
        for (int c = 0; c < cols - 1; ++c) {
            gx += colW[c];
            surface->DrawFilledRect(gx, topEdge, gx + gridT, bottomEdge);
            gx += gridT;
        }

        // Horizontal lines: extend to content right edge
        int gy = tY;
        for (int r = 0; r < rows - 1; ++r) {
            gy += rowH[r];
            surface->DrawFilledRect(leftEdge, gy, contentRight, gy + gridT);
            gy += gridT;
        }
        // Cell text
        surface->DrawSetTextFont(WatermarkInfoFont);

        int yCursor = tY;
        for (int r = 0; r < rows; ++r) {
            int xCursor = tX;
            for (int c = 0; c < cols; ++c) {
                const std::wstring& cell = tableRows[r][c];
                const int availW = std::max(0, colW[c] - cellPadX * 2);

                // Get a display string that fits the cell (prefix + … if needed)
                std::wstring display = EllipsizeToWidth(cell, WatermarkInfoFont, availW);

                // Determine coloring based on content
                bool shouldBeRed = false;
                bool shouldBeRainbow = false;
                bool shouldBeKDGradient = false;
                bool shouldBeVelocityGradient = false;
                bool shouldBeGenerationGradient = false;
                float kdValue = 0.0f;
                float velocityValue = 0.0f;
                float generationValue = 0.0f;

                // Convert to narrow string for easier parsing
                char narrowCell[256] = { 0 };
                WideCharToMultiByte(CP_ACP, 0, cell.c_str(), -1, narrowCell, sizeof(narrowCell), NULL, NULL);

                // Check for FPS (looking for "frames per sec")
                if (strstr(narrowCell, "frames / sec")) {
                    int fps = atoi(narrowCell);
                    if (fps < 60) shouldBeRed = true;
                }
                // Check for ping (looking for "ms ping")
                else if (strstr(narrowCell, "ms ping")) {
                    int ping = atoi(narrowCell);
                    if (ping > 200) shouldBeRed = true;
                }
                // Check for packet loss (looking for "% / % loss")
                else if (strstr(narrowCell, "loss")) {
                    int loss1 = 0, loss2 = 0;
                    if (sscanf(narrowCell, "%d%% / %d%% loss", &loss1, &loss2) == 2) {
                        if (loss1 > 1 || loss2 > 1) shouldBeRed = true;
                    }
                }
                // Check for server variance (looking for "server var")
                else if (strstr(narrowCell, "server var")) {
                    int variance = atoi(narrowCell);
                    if (variance > 1) shouldBeRed = true;
                }
                // Check for velocity (looking for "u/s velocity")
                else if (strstr(narrowCell, "u/s speed")) {
                    velocityValue = (float)atof(narrowCell);
                    if (velocityValue >= 500.0f) shouldBeVelocityGradient = true;
                }
                // Check for K/D ratio (looking for "kills/deaths")
                else if (strstr(narrowCell, "kills/deaths")) {
                    kdValue = (float)atof(narrowCell);
                    shouldBeKDGradient = true;
                }
                // Check for generation
                else if (strstr(narrowCell, "generation")) {
                    const char* numStart = strstr(narrowCell, "generation");
                    if (numStart) {
                        numStart += strlen("generation");
                        while (*numStart == ' ') numStart++;
                        generationValue = (float)atof(numStart);

                        // Check for max generation (10.50) for rainbow
                        if (fabs(generationValue - 10.50f) < 0.01f) {
                            shouldBeRainbow = true;
                        } else {
                            shouldBeGenerationGradient = true;
                        }
                    }
                }

                // Measure the (possibly truncated) text for vertical centering
                int tw = 0, dummy = 0;
                surface->GetTextSize(WatermarkInfoFont, display.c_str(), tw, dummy);

                const int tx = xCursor + cellPadX;
                const int ty = yCursor + (rowH[r] - infoTall) / 2;

                // Generation gradient with soft blending
                if (shouldBeRainbow || shouldBeGenerationGradient) {
                    int genFloor = (int)generationValue;
                    // If you display only 1..11, guard the index:
                    genFloor = std::clamp(genFloor, 1, (int)N_GEN_ANCHORS);

                    // Progress toward "next style"
                    float p = NextGenProgress(generationValue); // e.g., gen 10.25 -> 0.5

                    // Soft blend width ~3 characters around the breakpoint
                    int len = (int)display.size();
                    int charX = tx;
                    float band = (len > 0) ? (0.5f * (3.0f / (float)len)) : 0.0f; // total width ≈ 3 chars

                    // Current-gen anchor (OKLCH)
                    OKLCh cur = { GEN_ANCHORS[genFloor - 1].L,
                                  GEN_ANCHORS[genFloor - 1].C,
                                  GEN_ANCHORS[genFloor - 1].h };

                    // Default "next" is the next gen anchor…
                    OKLCh nxtAnchor = cur;
                    if (genFloor < (int)N_GEN_ANCHORS) {
                        nxtAnchor = { GEN_ANCHORS[genFloor].L,
                                      GEN_ANCHORS[genFloor].C,
                                      GEN_ANCHORS[genFloor].h };
                    }

                    for (int i = 0; i < len; ++i) {
                        // Character's normalized position across the string
                        float pos = (i + 0.5f) / std::max(1, len);

                        // Weight of "next style" at this char with a soft band:
                        // left of (p - band) => 1, right of (p + band) => 0, cubic ramp in between.
                        float wNext = 1.0f - smoothstepf(p - band, p + band, pos);

                        // For gen 10, the "next style" is rainbow *per character*, not gen 11 purple.
                        OKLCh nxt = (genFloor == 10) ? RainbowOKLChAt(i, len) : nxtAnchor;

                        // Perceptual blend (shortest-arc hue) between current and per-char next
                        OKLCh mix = oklch_lerp(cur, nxt, wNext);

                        // Convert to sRGB and enforce contrast on your dark card
                        RGB_t charColor = ensure_contrast_on_dark(srgb_from_oklch(mix), kCardBg, kMinContrast);

                        surface->DrawSetTextColor(charColor.r, charColor.g, charColor.b, 235);
                        surface->DrawSetTextPos(charX, ty);

                        wchar_t singleChar[2] = { display[i], L'\0' };
                        surface->DrawPrintText(singleChar, 1);

                        int cw = 0, ch = 0;
                        surface->GetTextSize(WatermarkInfoFont, singleChar, cw, ch);
                        charX += cw;
                    }
                }
                // Velocity gradient (solid color based on velocity)
                else if (shouldBeVelocityGradient) {
                    int rr, gg, bb;

                    if (velocityValue < 500.0f) {
                        // Below threshold - white
                        rr = 255; gg = 255; bb = 255;
                    } else if (velocityValue >= 1000.0f) {
                        // Max blue
                        rr = 100; gg = 150; bb = 255;
                    } else {
                        // Gradient from white to blue (500-1000)
                        float t = (velocityValue - 500.0f) / 500.0f;
                        rr = (int)(255 * (1.0f - t) + 100 * t);
                        gg = (int)(255 * (1.0f - t) + 150 * t);
                        bb = (int)(255 * (1.0f - t) + 255 * t);
                    }

                    surface->DrawSetTextColor(rr, gg, bb, 235);
                    surface->DrawSetTextPos(tx, ty);
                    surface->DrawPrintText(display.c_str(), (int)display.size());
                }
                // K/D gradient color
                else if (shouldBeKDGradient) {
                    int rr, gg, bb;

                    // Check for perfect 10.0 K/D (rainbow)
                    if (fabs(kdValue - 10.0f) < 0.01f) {
                        int len = (int)display.size();
                        int charX = tx;

                        for (int i = 0; i < len; ++i) {
                            float hue = (i / (float)(len - 1)) * 210.0f;
                            HSVtoRGB(hue, 1.0f, 1.0f, rr, gg, bb);

                            surface->DrawSetTextColor(rr, gg, bb, 235);
                            surface->DrawSetTextPos(charX, ty);

                            wchar_t singleChar[2] = { display[i], L'\0' };
                            surface->DrawPrintText(singleChar, 1);

                            int charW = 0, charH = 0;
                            surface->GetTextSize(WatermarkInfoFont, singleChar, charW, charH);
                            charX += charW;
                        }

                        xCursor += colW[c] + (c < cols - 1 ? gridT : 0);
                        continue;
                    }

                    if (kdValue < 0.3f) {
                        // Poor K/D - solid red
                        surface->DrawSetTextColor(255, 96, 96, 255);
                        surface->DrawSetTextPos(tx, ty);
                        surface->DrawPrintText(display.c_str(), (int)display.size());
                    }
                    else if (kdValue < 1.5f) {
                        // Average K/D - white text
                        surface->DrawSetTextColor(255, 255, 255, 255);
                        surface->DrawSetTextPos(tx, ty);
                        surface->DrawPrintText(display.c_str(), (int)display.size());
                    }
                    else {
                        // Good K/D (1.5+) - apply color gradient
                        float adjustedValue = kdValue - 1.5f;

                        float hue;
                        if (adjustedValue < 1.0f) {
                            // 1.5-2.5: Red (0°) to Yellow (60°)
                            hue = adjustedValue * 60.0f;
                        }
                        else {
                            // 2.5+: Yellow (60°) to Cyan (180°)
                            float t = std::min((adjustedValue - 1.0f) / 1.0f, 1.0f);
                            hue = 60.0f + t * 120.0f;
                        }

                        HSVtoRGB(hue, 1.0f, 1.0f, rr, gg, bb);
                        surface->DrawSetTextColor(rr, gg, bb, 235);
                        surface->DrawSetTextPos(tx, ty);
                        surface->DrawPrintText(display.c_str(), (int)display.size());
                    }
                }
                else {
                    // Normal text rendering
                    if (shouldBeRed) {
                        surface->DrawSetTextColor(255, 96, 96, 255);  // Red
                    }
                    else {
                        surface->DrawSetTextColor(255, 255, 255, 255);  // White
                    }

                    surface->DrawSetTextPos(tx, ty);
                    surface->DrawPrintText(display.c_str(), (int)display.size());
                }

                xCursor += colW[c] + (c < cols - 1 ? gridT : 0);
            }
            yCursor += rowH[r] + (r < rows - 1 ? gridT : 0);
        }
    }
}

void DrawDamageNumbers()
{
    if (!cvar_delta_damage_numbers || !cvar_delta_damage_numbers->m_Value.m_nValue)
        return;

    if (!GetVectorInScreenSpace_ptr) // Make sure the pointer is valid
        return;

    // Initialize fonts if they haven't been already
    if (!DamageNumberFont)
    {
        DamageNumberFont = surface->CreateFont();
        surface->SetFontGlyphSet(DamageNumberFont, "ConduitITCPro-Medium", cvar_delta_damage_numbers_size->m_Value.m_nValue, 800, 0, 0, vgui::FONTFLAG_ANTIALIAS);
    }
    if (!DamageNumberCritFont)
    {
        DamageNumberCritFont = surface->CreateFont();
        surface->SetFontGlyphSet(DamageNumberCritFont, "ConduitITCPro-Medium", cvar_delta_damage_numbers_crit_size->m_Value.m_nValue, 900, 0, 0, vgui::FONTFLAG_ANTIALIAS);
    }

    float currentTime = Plat_FloatTime();
    float lifeTime = cvar_delta_damage_numbers_lifetime->m_Value.m_fValue;

    // Iterate backwards so we can safely erase elements
    for (int i = g_DamageNumbers.size() - 1; i >= 0; --i)
    {
        auto& item = g_DamageNumbers[i];

        // Remove expired numbers
        if (currentTime > item.spawnTime + lifeTime)
        {
            g_DamageNumbers.erase(g_DamageNumbers.begin() + i);
            continue;
        }

        // Batching logic (similar to TF2)
        if (cvar_delta_damage_numbers_batching && cvar_delta_damage_numbers_batching->m_Value.m_nValue &&
            item.batchWindow > 0.f && item.sourceID != -1 && i > 0)
        {
            // Check if previous item is from same source and within batch window
            auto& prevItem = g_DamageNumbers[i - 1];
            float timeDiff = item.spawnTime - prevItem.spawnTime;

            if (timeDiff <= item.batchWindow && prevItem.sourceID == item.sourceID)
            {
                // Merge this damage into the previous one
                prevItem.damage += item.damage;
                // Reset the spawn time so the batched number appears fresh and starts animating from the bottom
                prevItem.spawnTime = item.spawnTime;
                // Also update the position to the new hit location
                prevItem.worldPos = item.worldPos;

                prevItem.isCritical = item.isCritical;

                g_DamageNumbers.erase(g_DamageNumbers.begin() + i);
                continue;
            }
        }

        float lifeFrac = (currentTime - item.spawnTime) / lifeTime;

        // Apply exponential ease-in curve: slow start, fast end
        float easedLifeFrac = powf(lifeFrac, 2.5f);

        // Animate position upwards with exponential easing
        Vector animatedWorldPos = item.worldPos;
        animatedWorldPos.z += easedLifeFrac * 40.0f; // Moves 40 units up over its lifetime

        int x, y;
        if (!GetVectorInScreenSpace_ptr(animatedWorldPos, x, y, nullptr))
        {
            continue; // Not on screen
        }

        // Calculate fade-out alpha with exponential curve (starts fading after 50% lifetime)
        int alpha = 255;
        if (lifeFrac > 0.5f)
        {
            // Exponential fade: slow fade at first, then fast fade at the end
            float fadeLifeFrac = (lifeFrac - 0.5f) * 2.0f; // 0 to 1 over the fade period
            float easedFade = powf(fadeLifeFrac, 3.0f); // Ease-in: slow then fast (adjust power for steepness)
            alpha = static_cast<int>(255.0f * (1.0f - easedFade));
            alpha = std::max(0, alpha); // Clamp
        }

        // Prepare text and font
        wchar_t wBuf[16];
        swprintf(wBuf, 16, L"%d", item.damage);

        vgui::HFont currentFont = item.isCritical ? DamageNumberCritFont : DamageNumberFont;

        // Get text size first
        int textWide, textTall;
        surface->GetTextSize(currentFont, wBuf, textWide, textTall);

        // Center the text
        x -= textWide / 2;
        y -= textTall / 2;

        // Set font
        surface->DrawSetTextFont(currentFont);

        // Draw red outline by rendering text multiple times with offset
        // Outline is always red (255, 0, 0)
        int outlineOffsets[][2] = {
            {-1, -1}, {0, -1}, {1, -1},
            {-1,  0},          {1,  0},
            {-1,  1}, {0,  1}, {1,  1}
        };

        // Draw outline in red
        surface->DrawSetTextColor(255, 0, 0, alpha);
        for (int j = 0; j < 8; j++)
        {
            surface->DrawSetTextPos(x + outlineOffsets[j][0], y + outlineOffsets[j][1]);
            surface->DrawPrintText(wBuf, wcslen(wBuf));
        }

        // Draw main text
        // Crits: black text, Non-crits: white text
        if (item.isCritical)
            surface->DrawSetTextColor(0, 0, 0, alpha);  // Black for crits
        else
            surface->DrawSetTextColor(255, 255, 255, alpha);  // White for non-crits

        surface->DrawSetTextPos(x, y);
        surface->DrawPrintText(wBuf, wcslen(wBuf));
    }
}

#define NUM_STATES 3
ConVarR1* cvar_delta_script_errors_notification = nullptr;
vgui::HFont ScriptErrorNotificationFont = 0;
vgui::HTexture ScriptErrorWarningTexture = 0;
float LastScriptError[NUM_STATES] = { 0.f, 0.f, 0.f };
void DrawScriptErrors() {
    if (!ScriptErrorNotificationFont) {
        ScriptErrorNotificationFont = surface->CreateFont();
        surface->SetFontGlyphSet(ScriptErrorNotificationFont, "Tahoma", 13, 800, 0, 0, vgui::FONTFLAG_ANTIALIAS);
    }
    if (!ScriptErrorWarningTexture) {
        ScriptErrorWarningTexture = surface->CreateNewTextureID();
        surface->DrawSetTextureFile(ScriptErrorWarningTexture, "ui/menu/r1delta/error", 0, false);
    }

    if (!cvar_delta_script_errors_notification->m_Value.m_nValue) return;

    int idealy = 32;
    int height = 30;
    float endTime = Plat_FloatTime() - 10;
    float recent = Plat_FloatTime() - 0.5f;

    const wchar_t* ScriptErrorStates[NUM_STATES] = { L"Server", L"Client", L"UI" };

    for (size_t i = 0; i < NUM_STATES; i++) {
        if (!LastScriptError[i]) continue;

        int x = 32;
        int y = idealy;

        wchar_t text[64] = { 0,0 };
        wsprintf(text, L"Something is creating %ws script errors", ScriptErrorStates[i]);
        int textWidth, textHeight;
        surface->GetTextSize(ScriptErrorNotificationFont, text, textWidth, textHeight);
        int width = textWidth + 48;

        surface->DrawSetColor(40, 40, 40, 255);
        surface->DrawFilledRect(x + 2, y + 2, x + 2 + width, y + 2 + height);
        surface->DrawSetColor(240, 240, 240, 255);
        surface->DrawFilledRect(x, y, x + width, y + height);

        if (LastScriptError[i] > recent) {
            surface->DrawSetColor(255, 200, 0, (LastScriptError[i] - recent) * 510);
            surface->DrawFilledRect(x, y, x + width, y + height);
        }

        surface->DrawSetTextFont(ScriptErrorNotificationFont);
        surface->DrawSetTextColor(90, 90, 90, 255);
        surface->DrawSetTextPos(x + 34, y + 8);
        surface->DrawPrintText(text, wcslen(text));

        surface->DrawSetColor(255, 255, 255, 150 + sinf(y + Plat_FloatTime() * 30) * 100);
        surface->DrawSetTexture(ScriptErrorWarningTexture);
        surface->DrawTexturedRect(x + 6, y + 6, x + 6 + 16, y + 6 + 16);

        idealy += 40;

        if (LastScriptError[i] < endTime) LastScriptError[i] = 0;
    }
}
void OnScriptError(ScriptContext state) {
    LastScriptError[state] = Plat_FloatTime();
}

void (*oPaintTraverse)(uintptr_t thisptr, vgui::VPANEL panel, bool forceRepaint, bool allowForce);
void PaintTraverse(uintptr_t thisptr, vgui::VPANEL paintPanel, bool forceRepaint, bool allowForce) {
    static vgui::VPANEL inGameRenderPanel = 0, menuRenderPanel = 0;

    oPaintTraverse(thisptr, paintPanel, forceRepaint, allowForce);

    if (!inGameRenderPanel) {
        if (!strcmp_static(panel->GetName(paintPanel), "MatSystemTopPanel")) {
            inGameRenderPanel = paintPanel;
        }
    }
    if (!menuRenderPanel) {
        if (!strcmp_static(panel->GetName(paintPanel), "CBaseModPanel")) {
            menuRenderPanel = paintPanel;
        }
    }

    if (paintPanel == inGameRenderPanel)
    {
        UpdateWatermarkData();
        DrawWatermark();
        DrawDamageNumbers();
    }
    if (paintPanel == inGameRenderPanel || paintPanel == menuRenderPanel) DrawScriptErrors();
}

uint64_t(*oOnClientScriptErrorHook)(uintptr_t sqstate);
uint64_t OnClientScriptErrorHook(uintptr_t sqstate) {
    uintptr_t intobj = *(uintptr_t*)(sqstate + 0xE8);
    const char* stateName = (const char*)(intobj + 0x4174);

    ScriptContext state = SCRIPT_CONTEXT_SERVER;
    if (stateName[0] == 'C') state = SCRIPT_CONTEXT_CLIENT;
    if (stateName[0] == 'U') state = SCRIPT_CONTEXT_UI;
    OnScriptError(state);

    return oOnClientScriptErrorHook(sqstate);
}


void(*oOnScreenSizeChanged)(uintptr_t thisptr, int w, int h);
void OnScreenSizeChanged(uintptr_t thisptr, int w, int h) {
    oOnScreenSizeChanged(thisptr, w, h);
    WatermarkTitleFont = WatermarkInfoFont = WatermarkLogoTex = ScriptErrorNotificationFont = DamageNumberFont = DamageNumberCritFont = 0;
}

__int64(*oCPluginHudMessage_ctor)(uintptr_t thisptr, uintptr_t panel);
__int64 CPluginHudMessage_ctor(uintptr_t thisptr, uintptr_t ppanel) {
    auto engineVgui = ((void* (__fastcall*)())(G_engine + 0x21E670))();
    return oCPluginHudMessage_ctor(thisptr, (*(__int64(__fastcall**)(void*, int))(*(_QWORD*)engineVgui + 8LL))(engineVgui, 5));
}

extern ConVarR1* RegisterConVar(const char* name, const char* value, int flags, const char* helpString);

// Squirrel script function to add a damage number
SQInteger Script_AddDamageNumber(HSQUIRRELVM v) {
    if (g_DamageNumbers.size() > 50) // Prevent spam
        return 0;

    auto r1_vm = GetClientVMPtr();

    SQFloat damage;
    Vector pos;
    SQBool isCritical;
    SQInteger sourceID = -1;

    // Arg 2: damage (float)
    if (SQ_FAILED(sq_getfloat(r1_vm, v, 2, &damage)))
        return sq_throwerror(v, "Invalid argument 1: expected float damage");

    // Arg 3, 4, 5: position (vector as 3 floats)
    if (SQ_FAILED(sq_getfloat(r1_vm, v, 3, &pos.x)) ||
        SQ_FAILED(sq_getfloat(r1_vm, v, 4, &pos.y)) ||
        SQ_FAILED(sq_getfloat(r1_vm, v, 5, &pos.z)))
        return sq_throwerror(v, "Invalid argument 2,3,4: expected vector position");

    // Arg 6: isCritical (bool)
    if (SQ_FAILED(sq_getbool(r1_vm, v, 6, &isCritical)))
        return sq_throwerror(v, "Invalid argument 5: expected bool isCritical");

    // Arg 7: sourceID (optional int) - for batching
    sq_getinteger(r1_vm, v, 7, &sourceID);

    // Block damage numbers at or very close to the origin (invalid position)
    const float EPSILON = 0.1f;
    if (fabs(pos.x) < EPSILON && fabs(pos.y) < EPSILON && fabs(pos.z) < EPSILON)
        return 0; // Reject this damage number

    // Add the damage number to the global list
    DamageNumber_t dmgNum;
    dmgNum.damage = (int)damage;
    dmgNum.worldPos = pos;
    dmgNum.spawnTime = Plat_FloatTime();
    dmgNum.isCritical = isCritical != 0;

    // Set batching parameters
    dmgNum.batchWindow = cvar_delta_damage_numbers_batching && cvar_delta_damage_numbers_batching->m_Value.m_nValue
        ? cvar_delta_damage_numbers_batching_window->m_Value.m_fValue
        : 0.f;
    dmgNum.sourceID = sourceID;

    g_DamageNumbers.push_back(dmgNum);

    return 0;
}
void FontSizeChangeCallback(IConVar* var, const char* pOldValue, float flOldValue) {
    DamageNumberFont = 0;
    DamageNumberCritFont = 0;

}

void DeltaWatermarkDebugCommand(const CCommand& args) {
    // Get Local Player
    static auto getlocalplayer = reinterpret_cast<void* (*)(int)>((uintptr_t)G_client + 0x7B120);
    void* pLocalPlayer = getlocalplayer(-1);

    if (!pLocalPlayer || !G_engine) {
        Msg("No local player or engine not available\n");
        return;
    }

    // Get engine client interface pointer
    void* pEngineClient = (void*)(G_engine + 0x795650);
    if (!pEngineClient) {
        Msg("No engine client\n");
        return;
    }

    // FPS - calculated from frame time using gpClientGlobals
    void* gpClientGlobals = *(void**)(G_client + 0xAEA640);
    float absoluteframetime = *(float*)((uintptr_t)gpClientGlobals + 0x0C);
    float fps = (absoluteframetime > 0) ? (1.0f / absoluteframetime) : 0.0f;

    // Velocity
    float* vel = (float*)((uintptr_t)pLocalPlayer + OFF_VELOCITY);
    float velocity_x = vel[0];
    float velocity_y = vel[1];
    float velocity = sqrtf(vel[0]*vel[0] + vel[1]*vel[1]);

    // Network Stats
    int pingMs = 0;
    int lossIn = 0;
    int lossOut = 0;
    float serverVar = 0.0f;

    void* nci = CallVFunc<void*>(160, pEngineClient);

    if (nci) {
        float latency = CallVFunc<float>(10, nci, 1);
        pingMs = (int)(latency * 1000.0f);

        float fLossIn = CallVFunc<float>(11, nci, 0);
        float fLossOut = CallVFunc<float>(11, nci, 1);
        lossIn = (int)(fLossIn * 100.0f);
        lossOut = (int)(fLossOut * 100.0f);

        float frameTime = 0, frameTimeStdDev = 0, startTimeStdDev = 0;
        CallVFunc<void>(25, nci, &frameTime, &frameTimeStdDev, &startTimeStdDev);
        serverVar = startTimeStdDev * 1000.0f;
    }

    // Player Stats (K/D)
    int kills = 0;
    int deaths = 0;
    float kdRatio = 0.0f;
    int localIndex = CallVFunc<int>(22, pEngineClient, 0);

    void* g_PR = GetPlayerResource();
    if (g_PR) {
        int* pKills = (int*)((uintptr_t)g_PR + OFF_PR_KILLS);
        int* pDeaths = (int*)((uintptr_t)g_PR + OFF_PR_DEATHS);
        int* pPings = (int*)((uintptr_t)g_PR + OFF_PR_PING);

        if (localIndex >= 0 && localIndex < 64) {
            kills = pKills[localIndex];
            deaths = pDeaths[localIndex];

            if (!nci) pingMs = pPings[localIndex];

            kdRatio = (deaths > 0) ? ((float)kills / (float)deaths) : (float)kills;
        }
    }

    // Generation & XP
    int rawGen = *(int*)((uintptr_t)pLocalPlayer + OFF_GEN);
    int currentXP = *(int*)((uintptr_t)pLocalPlayer + OFF_XP);
    float progress = (float)currentXP / XP_AT_MAX_LEVEL;
    if (progress > 0.99f) progress = 0.99f;
    float genDisplay = (float)(rawGen + 1) + progress;

    // Player Name
    const char* playerName = "Pilot";
    static auto nameVar = OriginalCCVar_FindVar(cvarinterface, "name");
    if (nameVar && nameVar->m_Value.m_pszString) {
        playerName = nameVar->m_Value.m_pszString;
    }

    // Map name
    const char* mapName = g_cl_MapDisplayName;

    // Print all raw values
    Msg("=== Delta Watermark Debug ===\n");
    Msg("absoluteframetime: %f\n", absoluteframetime);
    Msg("fps: %f\n", fps);
    Msg("velocity_x: %f\n", velocity_x);
    Msg("velocity_y: %f\n", velocity_y);
    Msg("velocity: %f\n", velocity);
    Msg("pingMs: %d\n", pingMs);
    Msg("lossIn: %d%%\n", lossIn);
    Msg("lossOut: %d%%\n", lossOut);
    Msg("serverVar: %f ms\n", serverVar);
    Msg("kills: %d\n", kills);
    Msg("deaths: %d\n", deaths);
    Msg("kdRatio: %f\n", kdRatio);
    Msg("rawGen: %d\n", rawGen);
    Msg("currentXP: %d\n", currentXP);
    Msg("progress: %f\n", progress);
    Msg("genDisplay: %f\n", genDisplay);
    Msg("playerName: %s\n", playerName);
    Msg("mapName: %s\n", mapName);
    Msg("localIndex: %d\n", localIndex);
}
ConCommandR1* RegisterConCommand(const char* commandName, void (*callback)(const ::CCommand&), const char* helpString, int flags);

void SetupSurfaceRenderHooks() {
    cvar_delta_watermark = RegisterConVar("delta_watermark", "1", FCVAR_GAMEDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show R1Delta watermark with version information");
    cvar_delta_damage_numbers = RegisterConVar("delta_damage_numbers", "0", FCVAR_GAMEDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show TF2-style floating damage numbers on hit.");
    cvar_delta_damage_numbers_lifetime = RegisterConVar("delta_damage_numbers_lifetime", "1.5", FCVAR_GAMEDLL, "How long damage numbers stay on screen.");
    cvar_delta_damage_numbers_size = RegisterConVar("delta_damage_numbers_size", "32", FCVAR_GAMEDLL, "Font size for normal damage numbers.");
    cvar_delta_damage_numbers_crit_size = RegisterConVar("delta_damage_numbers_crit_size", "36", FCVAR_GAMEDLL, "Font size for critical damage numbers.");
    cvar_delta_damage_numbers_batching = RegisterConVar("delta_damage_numbers_batching", "1", FCVAR_GAMEDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Batch damage numbers from the same source within a time window.");
    cvar_delta_damage_numbers_batching_window = RegisterConVar("delta_damage_numbers_batching_window", "3", FCVAR_GAMEDLL, "Time window for batching damage numbers (seconds).");
    cvar_delta_damage_numbers_size->m_fnChangeCallbacks.AddToTail((FnChangeCallback_t)FontSizeChangeCallback);
    cvar_delta_damage_numbers_crit_size->m_fnChangeCallbacks.AddToTail((FnChangeCallback_t)FontSizeChangeCallback);

    RegisterConCommand("delta_watermark_debug", DeltaWatermarkDebugCommand, "Print raw watermark debug information", FCVAR_NONE);

    auto vguimatsurface = GetModuleHandleA("vguimatsurface.dll");
    auto vguimatsurface_CreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(vguimatsurface, "CreateInterface"));
    surface = (vgui::ISurface*)vguimatsurface_CreateInterface("VGUI_Surface031", 0);

    auto vgui = GetModuleHandleA("vgui2.dll");
    auto vgui_CreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(vgui, "CreateInterface"));
    panel = (vgui::IPanel*)vgui_CreateInterface("VGUI_Panel009", 0);

    sub_180016490 = (decltype(sub_180016490))(((uintptr_t)(vguimatsurface)) + 0x16490);
    MH_CreateHook(GetVFunc<LPVOID>(panel, 46), &PaintTraverse, reinterpret_cast<LPVOID*>(&oPaintTraverse));
    MH_CreateHook((LPVOID)(((uintptr_t)(vguimatsurface)) + 0x165C0), &sub_1800165C0, reinterpret_cast<LPVOID*>(&osub_1800165C0));
    MH_CreateHook((LPVOID)(((uintptr_t)(G_client)) + 0x28BEA0), &sub_18028BEA0, reinterpret_cast<LPVOID*>(&osub_18028BEA0));
    MH_CreateHook((LPVOID)(((uintptr_t)(vguimatsurface)) + 0x119E0), &OnScreenSizeChanged, reinterpret_cast<LPVOID*>(&oOnScreenSizeChanged));
    MH_CreateHook((LPVOID)(((uintptr_t)(G_engine)) + 0x5E860), &CPluginHudMessage_ctor, reinterpret_cast<LPVOID*>(&oCPluginHudMessage_ctor));

    // Initialize GetVectorInScreenSpace function pointer
    GetVectorInScreenSpace_ptr = reinterpret_cast<GetVectorInScreenSpace_t>(G_client + 0x0105170);
}

void SetupSquirrelErrorNotificationHooks() {
    cvar_delta_script_errors_notification = RegisterConVar("delta_script_errors_notification", "1", FCVAR_GAMEDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show a notification whenever a script error occurs");

    auto launcher = (uintptr_t)GetModuleHandleA("launcher.dll");
    MH_CreateHook((LPVOID)(launcher + 0x3A5E0), &OnClientScriptErrorHook, reinterpret_cast<LPVOID*>(&oOnClientScriptErrorHook));
}

void SetupLocalizeIface() {
    auto mlocalize = GetModuleHandleA("localize.dll");
    auto localize_CreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(mlocalize, "CreateInterface"));
    G_localizeIface = (ILocalize*)localize_CreateInterface("Localize_001", 0);
    G_localize = (uintptr_t)mlocalize;
}
