#include "surfacerender.h"
#include "vguisurface.h"
#include "localize.h"
#include "load.h"
#include "squirrel.h"

#include "r1d_version.h"

vgui::IPanel* panel = nullptr;
vgui::ISurface* surface = nullptr;

ILocalize* localize = nullptr;

ConVarR1* cvar_delta_watermark = nullptr;
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
    char ansiBuffer[64];
    snprintf(ansiBuffer, sizeof(ansiBuffer), "R1Delta %s", R1D_VERSION);
    wchar_t watermarkText1[512];
    localize->ConvertANSIToUnicode(ansiBuffer, watermarkText1, sizeof(watermarkText1));
    int text1Wide, text1Tall;
    surface->GetTextSize(WatermarkFont, watermarkText1, text1Wide, text1Tall);
    surface->DrawSetColor(0, 0, 0, 120);
    int bgRectX0 = screenWidth - text1Wide + 10;
    int bgRectY0 = 0;
    int bgRectX1 = screenWidth;
    int bgRectY1 = text1Tall + 18;
    surface->DrawFilledRect(bgRectX0, bgRectY0, bgRectX1, bgRectY1);
    int effectCounter = 1;
    for (int i = 9; i > -50; --i)
    {
        int alpha = 120 - (effectCounter * 2);
        alpha = std::max(0, alpha);
        surface->DrawSetColor(0, 0, 0, alpha);
        int lineX0 = screenWidth - text1Wide + i;
        int lineY0 = 0;
        int lineX1 = lineX0 + 1;
        int lineY1 = text1Tall + 18;
        surface->DrawFilledRect(lineX0, lineY0, lineX1, lineY1);
        effectCounter++;
    }
    surface->DrawSetTextFont(WatermarkFont);
    surface->DrawSetTextColor(255, 255, 255, 255);
    int text1X = screenWidth - text1Wide - 5;
    int text1Y = 2;
    surface->DrawSetTextPos(text1X, text1Y);
    surface->DrawPrintText(watermarkText1, wcslen(watermarkText1));
    sprintf(ansiBuffer, "r1delta.net");
    wchar_t watermarkText2[512];
    localize->ConvertANSIToUnicode(ansiBuffer, watermarkText2, sizeof(watermarkText2));
    int text2Wide, text2Tall;
    surface->GetTextSize(WatermarkSmallFont, watermarkText2, text2Wide, text2Tall);
    surface->DrawSetTextFont(WatermarkSmallFont);
    surface->DrawSetTextColor(255, 255, 255, 220);
    int text2X = screenWidth - text2Wide - 5;
    int text2Y = text1Tall + 2;
    text2Y = text2Tall + 2;
    surface->DrawSetTextPos(text2X, text2Y);
    surface->DrawPrintText(watermarkText2, wcslen(watermarkText2));
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

    if (paintPanel == inGameRenderPanel) DrawWatermark();
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

extern ConVarR1* RegisterConVar(const char* name, const char* value, int flags, const char* helpString);

void SetupSurfaceRenderHooks() {
    cvar_delta_watermark = RegisterConVar("delta_watermark", "1", FCVAR_GAMEDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show R1Delta watermark with version information");

	auto vguimatsurface = GetModuleHandleA("vguimatsurface.dll");
	auto vguimatsurface_CreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(vguimatsurface, "CreateInterface"));
    surface = (vgui::ISurface*)vguimatsurface_CreateInterface("VGUI_Surface031", 0);

	auto vgui = GetModuleHandleA("vgui2.dll");
	auto vgui_CreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(vgui, "CreateInterface"));
	panel = (vgui::IPanel*)vgui_CreateInterface("VGUI_Panel009", 0);

    auto mlocalize = GetModuleHandleA("localize.dll");
    auto localize_CreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(mlocalize, "CreateInterface"));
    localize = (ILocalize*)localize_CreateInterface("Localize_001", 0);

	MH_CreateHook(GetVFunc<LPVOID>(panel, 46), &PaintTraverse, reinterpret_cast<LPVOID*>(&oPaintTraverse));
}

void SetupSquirrelErrorNotificationHooks() {
    cvar_delta_script_errors_notification = RegisterConVar("delta_script_errors_notification", "1", FCVAR_GAMEDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show a notification whenever a script error occurs");

    auto launcher = (uintptr_t)GetModuleHandleA("launcher.dll");
    MH_CreateHook((LPVOID)(launcher + 0x3A5E0), &OnClientScriptErrorHook, reinterpret_cast<LPVOID*>(&oOnClientScriptErrorHook));
}