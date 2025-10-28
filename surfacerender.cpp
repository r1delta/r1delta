#include "surfacerender.h"
#include "vguisurface.h"
#include "localize.h"
#include "load.h"
#include "squirrel.h"

#include "r1d_version.h"
#include <vector>
#include <algorithm> // for std::max

// Forward declare or include Vector type
#include "public/vector.h"

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
    // Clear your buffer and read the new state
    memset(fpsStringData, 0, sizeof(fpsStringData));
    static auto cl_showfps = OriginalCCVar_FindVar(cvarinterface, "cl_showfps");
    bool isDrawing = cl_showfps->m_Value.m_nValue == 1 && cvar_delta_watermark->m_Value.m_nValue == 1;
    g_bIsDrawingFPSPanel = isDrawing;
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

    // reset your flag if you’re doing one-shot draws, etc.
    g_bIsDrawingFPSPanel = false;
    return ret;
}

vgui::HFont WatermarkFont = 0, WatermarkSmallFont = 0;
void DrawWatermark() {
    if (!WatermarkFont) {
        WatermarkFont = surface->CreateFont();
        surface->SetFontGlyphSet(WatermarkFont, "Verdana", 14, 650, 0, 0, vgui::FONTFLAG_DROPSHADOW | vgui::FONTFLAG_ANTIALIAS);
    }
    if (!WatermarkSmallFont) {
        WatermarkSmallFont = surface->CreateFont();
        surface->SetFontGlyphSet(WatermarkSmallFont, "Verdana", 12, 100, 0, 0, vgui::FONTFLAG_DROPSHADOW | vgui::FONTFLAG_ANTIALIAS);
    }

    if (!cvar_delta_watermark->m_Value.m_nValue) return;

    int screenWidth, screenHeight;
    surface->GetScreenSize(screenWidth, screenHeight);

    // --- Text Preparation ---
    // Line 1: Version Info
    char ansiBuffer1[512];
    if (!strcmp_static(R1D_VERSION, "dev"))
        snprintf(ansiBuffer1, sizeof(ansiBuffer1), "R1Delta [dev]");
    else
        snprintf(ansiBuffer1, sizeof(ansiBuffer1), "R1Delta %s", R1D_VERSION);
    wchar_t watermarkText1[512];
    G_localizeIface->ConvertANSIToUnicode(ansiBuffer1, watermarkText1, sizeof(watermarkText1));

    // Line 2+: FPS Info or Default URL
    const char* urlString = "https://r1delta.net/";
    const char* secondData = urlString;
    if (fpsStringData[0] != '\x00') {
        secondData = fpsStringData;
    }
    wchar_t watermarkText2[4096];
    G_localizeIface->ConvertANSIToUnicode(secondData, watermarkText2, sizeof(watermarkText2));

    // --- Calculate Text Dimensions and Total Height/Max Width ---
    int text1Wide, text1Tall;
    surface->GetTextSize(WatermarkFont, watermarkText1, text1Wide, text1Tall);

    int maxLineWidth = text1Wide;       // Start with the width of the first line
    int totalTextHeight = text1Tall;    // Start with the height of the first line
    int smallFontLineSpacing = 1;       // Pixels between lines using the small font

    // Need a mutable copy for wcstok_s
    wchar_t watermarkText2Copy[4096];
    wcsncpy_s(watermarkText2Copy, sizeof(watermarkText2Copy) / sizeof(wchar_t), watermarkText2, _TRUNCATE);

    wchar_t* ctx = nullptr;
    wchar_t* line = wcstok_s(watermarkText2Copy, L"\n", &ctx);
    int numSmallFontLines = 0;

    while (line) {
        // Skip empty lines potentially generated by consecutive newlines
        if (wcslen(line) == 0) {
            line = wcstok_s(nullptr, L"\n", &ctx);
            continue;
        }

        int lineWide, lineTall;
        surface->GetTextSize(WatermarkSmallFont, line, /*out*/ lineWide, /*out*/ lineTall);

        maxLineWidth = std::max(maxLineWidth, lineWide); // Update max width if this line is wider
        if (numSmallFontLines == 0) {
            totalTextHeight += 2; // Initial padding before the first small line
        }
        else {
            totalTextHeight += smallFontLineSpacing; // Padding between small lines
        }
        totalTextHeight += lineTall; // Add height of the current line

        numSmallFontLines++;
        line = wcstok_s(nullptr, L"\n", &ctx);
    }
    maxLineWidth /= 2;
    // --- Draw Background ---
    int textPadding = 5; // Padding between text edge and background edge
    int bgRectX0 = screenWidth - maxLineWidth - textPadding * 2; // Left edge based on widest line + padding on both sides
    int bgRectY0 = 0;                                      // Top edge
    int bgRectX1 = screenWidth;                            // Right edge
    int bgRectY1 = totalTextHeight + textPadding; // Bottom edge based on total text height + bottom padding
    // Ensure bgRectX0 is not negative
    bgRectX0 = std::max(0, bgRectX0);


    surface->DrawSetColor(0, 0, 0, 120);
    surface->DrawFilledRect(bgRectX0, bgRectY0, bgRectX1, bgRectY1);

    // --- Draw Fade Effect (Optional, adjust height) ---
    int effectCounter = 1;
    int fadeWidth = text1Wide - 10; // Original width calculation - maybe adjust based on maxLineWidth?
    fadeWidth = std::max(50, fadeWidth); // Ensure minimum width
    int fadeStartX = screenWidth - maxLineWidth - textPadding * 2 - fadeWidth;
    fadeStartX = std::max(0, fadeStartX); // Don't go off screen left

    for (int i = fadeWidth; i > -50; --i) // Loop adjusted for potentially wider start
    {
        int alpha = 120 - (effectCounter * 2);
        alpha = std::max(0, alpha); // Clamp alpha >= 0
        surface->DrawSetColor(0, 0, 0, alpha);

        int lineX0 = fadeStartX + i;
        // Ensure line stays within screen bounds if background is very wide
        if (lineX0 < 0) continue;
        if (lineX0 >= bgRectX0) continue; // Don't draw over main background

        int lineY0 = 0;
        int lineX1 = lineX0 + 1;
        int lineY1 = bgRectY1; // Use the calculated total height for the fade lines
        surface->DrawFilledRect(lineX0, lineY0, lineX1, lineY1);
        effectCounter++;
    }


    // --- Draw Text ---
    // Draw Line 1 (Version) - Right Aligned
    surface->DrawSetTextFont(WatermarkFont);
    surface->DrawSetTextColor(255, 255, 255, 255);
    int text1X = screenWidth - text1Wide - textPadding; // Use its own width for alignment
    int text1Y = textPadding / 2; // Position slightly down from top
    surface->DrawSetTextPos(text1X, text1Y);
    surface->DrawPrintText(watermarkText1, wcslen(watermarkText1));

    // Draw Line 2+ (FPS/URL) - Right Aligned
    surface->DrawSetTextFont(WatermarkSmallFont);
    surface->DrawSetTextColor(255, 255, 255, 220);

    int currentY = text1Tall + 2; // Start Y position for the second block of text

    // Reset strtok state by using the original string again
    ctx = nullptr;
    line = wcstok_s(watermarkText2, L"\n", &ctx); // Use original watermarkText2 now for drawing

    while (line) {
        // Skip empty lines
        if (wcslen(line) == 0) {
            line = wcstok_s(nullptr, L"\n", &ctx);
            continue;
        }

        int lineWide, lineTall;
        surface->GetTextSize(WatermarkSmallFont, line, /*out*/ lineWide, /*out*/ lineTall);

        // Compute X so that text is right-aligned relative to screen edge
        int lineX = screenWidth - lineWide - textPadding;

        surface->DrawSetTextPos(lineX, currentY);
        surface->DrawPrintText(line, wcslen(line));

        // Advance Y by this line's height plus spacing
        currentY += lineTall + smallFontLineSpacing;

        // Next segment
        line = wcstok_s(nullptr, L"\n", &ctx);
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
    WatermarkFont = WatermarkSmallFont = ScriptErrorNotificationFont = DamageNumberFont = DamageNumberCritFont = 0;
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
