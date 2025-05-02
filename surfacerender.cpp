#include "surfacerender.h"
#include "vguisurface.h"
#include "localize.h"
#include "load.h"

#include "r1d_version.h"

vgui::IPanel* panel = nullptr;
vgui::ISurface* surface = nullptr;

ILocalize* localize = nullptr;

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

void (*oPaintTraverse)(uintptr_t thisptr, vgui::VPANEL panel, bool forceRepaint, bool allowForce);
void PaintTraverse(uintptr_t thisptr, vgui::VPANEL paintPanel, bool forceRepaint, bool allowForce) {
	static vgui::VPANEL renderPanel = 0;

	oPaintTraverse(thisptr, paintPanel, forceRepaint, allowForce);

	if (!renderPanel) {
		if (!strcmp_static(panel->GetName(paintPanel), "MatSystemTopPanel")) {
			renderPanel = paintPanel;
		}
	} else if (paintPanel != renderPanel) return;

	DrawWatermark();
}

void SetupSurfaceRenderHooks()
{
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