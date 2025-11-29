// Localization system hooks for R1Delta
// Handles loading localization files and mod localization

#include "localize.h"
#include "core.h"
#include "load.h"
#include "logging.h"
#include "filesystem.h"

#include <vector>
#include <string>

// Global localization interface
ILocalize* G_localizeIface = nullptr;

// List of mod localization files to load
std::vector<std::string> modLocalization_files;

// Original function pointers
bool (*o_pCLocalise__AddFile)(void* pVguiLocalize, const char* path, const char* pathId, bool bIncludeFallbackSearchPaths) = nullptr;
void (*o_pCLocalize__ReloadLocalizationFiles)(void* pVguiLocalize) = nullptr;

bool __fastcall pCLocalise__AddFile(void* pVguiLocalize, const char* path, const char* pathId, bool bIncludeFallbackSearchPaths) {
    auto ret = o_pCLocalise__AddFile(pVguiLocalize, path, pathId, bIncludeFallbackSearchPaths);
    return ret;
}

void __fastcall h_CLocalize__ReloadLocalizationFiles(void* pVguiLocalize)
{
    // Load mod localization files
    for (const auto& file : modLocalization_files) {
        o_pCLocalise__AddFile(G_localizeIface, file.c_str(), "GAME", false);
    }
    // Load R1Delta localization
    o_pCLocalise__AddFile(G_localizeIface, "resource/delta_%language%.txt", "GAME", false);
    o_pCLocalize__ReloadLocalizationFiles(pVguiLocalize);
}

void SetupLocalizeIface()
{
    auto mlocalize = GetModuleHandleA("localize.dll");
    auto localize_CreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(mlocalize, "CreateInterface"));
    G_localizeIface = (ILocalize*)localize_CreateInterface("Localize_001", 0);
    G_localize = (uintptr_t)mlocalize;
}
