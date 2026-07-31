#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../engine/core/r1o_runtime_paths.h"

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

using CreateInterfaceFn = void*(__cdecl*)(const char*, int*);

namespace
{
HMODULE g_proxyModule = nullptr;
HMODULE g_headlessCoherent = nullptr;
HMODULE g_headlessD3D11 = nullptr;
HMODULE g_headlessSSAO = nullptr;
HMODULE g_headlessTXAA = nullptr;
HMODULE g_realMaterialSystem = nullptr;
CreateInterfaceFn g_realCreateInterface = nullptr;
std::once_flag g_loadOnce;
bool g_loadOk = false;
using MaterialSystemSetShaderAPIFn = void(__fastcall*)(void* thisptr, const char* shaderApi);
MaterialSystemSetShaderAPIFn g_materialSystemSetShaderAPIOriginal = nullptr;
using MaterialSystemInitFn = int(__fastcall*)(void* thisptr);
MaterialSystemInitFn g_materialSystemInitOriginal = nullptr;
using MaterialSystemThreadModeFn = void(__fastcall*)(void* thisptr, int mode);
MaterialSystemThreadModeFn g_materialSystemThreadModeOriginal = nullptr;
using MaterialSystemModInitFn = void(__fastcall*)(void* thisptr);
MaterialSystemModInitFn g_materialSystemR1OModInitOriginal = nullptr;
using MaterialSystemGetRenderContextFn = void*(__fastcall*)(void* thisptr);
MaterialSystemGetRenderContextFn g_materialSystemGetRenderContextOriginal = nullptr;
void** g_materialSystemVTable = nullptr;
std::mutex g_materialSystemPatchMutex;
uintptr_t g_fakeRenderContextVTable[256]{};
void* g_fakeRenderContextVTablePtr = g_fakeRenderContextVTable;

void WriteLog(const char* fmt, va_list args)
{
    char buffer[1024];
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, args);

    OutputDebugStringA("[r1delta_materialsystem_nodx] ");
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
}

void Log(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteLog(fmt, args);
    va_end(args);
}

bool VerboseLoggingEnabled()
{
    static const bool enabled = [] {
        const char* commandLine = GetCommandLineA();
        return commandLine
            && (strstr(commandLine, "-r1o_fake_dedi_logs")
                || strstr(commandLine, "-r1o_verbose_logs"));
    }();
    return enabled;
}

void VerboseLog(const char* fmt, ...)
{
    if (!VerboseLoggingEnabled())
        return;

    va_list args;
    va_start(args, fmt);
    WriteLog(fmt, args);
    va_end(args);
}

std::wstring JoinPath(const std::wstring& left, const wchar_t* right)
{
    if (left.empty())
        return right;

    std::wstring result = left;
    const wchar_t tail = result[result.size() - 1];
    if (tail != L'\\' && tail != L'/')
        result += L'\\';
    result += right;
    return result;
}

bool GetProxyDirectory(std::wstring* directory)
{
    wchar_t proxyPath[MAX_PATH * 4]{};
    const DWORD length = GetModuleFileNameW(g_proxyModule, proxyPath, static_cast<DWORD>(std::size(proxyPath)));
    if (length == 0 || length >= std::size(proxyPath))
    {
        Log("could not resolve the nodx proxy directory, gle=%lu", GetLastError());
        return false;
    }

    wchar_t* slash = wcsrchr(proxyPath, L'\\');
    if (!slash)
    {
        Log("nodx proxy path has no directory separator");
        return false;
    }
    *slash = L'\0';
    *directory = proxyPath;
    return true;
}

bool ModuleExports(HMODULE module, const char* const* exports, size_t exportCount)
{
    for (size_t i = 0; i < exportCount; ++i)
    {
        if (!GetProcAddress(module, exports[i]))
            return false;
    }
    return true;
}

bool LoadHeadlessStub(
    const std::wstring& stubDirectory,
    const wchar_t* fileName,
    const char* const* requiredExports,
    size_t requiredExportCount,
    HMODULE* result)
{
    if (HMODULE existing = GetModuleHandleW(fileName))
    {
        wchar_t loadedPath[MAX_PATH * 4]{};
        GetModuleFileNameW(existing, loadedPath, static_cast<DWORD>(std::size(loadedPath)));
        Log(
            "cannot install the R1O headless %ls stub because a module with that name is already loaded from %ls",
            fileName,
            loadedPath[0] ? loadedPath : L"<unknown>");
        return false;
    }

    const std::wstring path = JoinPath(stubDirectory, fileName);
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module)
    {
        Log("required R1O headless stub is unavailable, path=%ls gle=%lu", path.c_str(), GetLastError());
        return false;
    }

    if (!ModuleExports(module, requiredExports, requiredExportCount))
    {
        Log("R1O headless stub is missing required exports, path=%ls", path.c_str());
        FreeLibrary(module);
        return false;
    }

    *result = module;
    VerboseLog("preloaded R1O headless stub from %ls", path.c_str());
    return true;
}

bool LoadHeadlessGraphicsStubs()
{
    std::wstring proxyDirectory;
    if (!GetProxyDirectory(&proxyDirectory))
        return false;

    const std::wstring stubDirectory = JoinPath(proxyDirectory, L"r1o_stubs");
    const char* const d3d11Exports[] = {
        "D3D11CoreCreateDevice",
        "D3D11CreateDevice",
        "D3D11CreateDeviceAndSwapChain",
    };
    const char* const ssaoExports[] = {
        "GFSDK_SSAO_CreateContext_D3D11",
        "GFSDK_SSAO_CreateContext_GL",
    };
    const char* const txaaExports[] = {
        "TxaaCloseDX",
        "TxaaCloseGL",
        "TxaaOpenDX",
        "TxaaOpenGL",
        "TxaaResolveDX",
        "TxaaResolveGL",
    };

    return LoadHeadlessStub(
               stubDirectory,
               L"d3d11.dll",
               d3d11Exports,
               std::size(d3d11Exports),
               &g_headlessD3D11)
        && LoadHeadlessStub(
               stubDirectory,
               L"GFSDK_SSAO.win64.dll",
               ssaoExports,
               std::size(ssaoExports),
               &g_headlessSSAO)
        && LoadHeadlessStub(
               stubDirectory,
               L"GFSDK_TXAA.win64.dll",
               txaaExports,
               std::size(txaaExports),
               &g_headlessTXAA);
}

struct SectionRange
{
    uint8_t* begin;
    uint8_t* end;
};

bool GetSectionRange(HMODULE module, const char* name, SectionRange* range)
{
    auto* const base = reinterpret_cast<uint8_t*>(module);
    auto* const dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    auto* const nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        char sectionName[9]{};
        memcpy(sectionName, section->Name, 8);
        if (_stricmp(sectionName, name) != 0)
            continue;

        const DWORD size = section->Misc.VirtualSize ? section->Misc.VirtualSize : section->SizeOfRawData;
        range->begin = base + section->VirtualAddress;
        range->end = range->begin + size;
        return true;
    }

    return false;
}

uint8_t* FindUniquePattern(const SectionRange& range, const std::vector<int>& pattern)
{
    uint8_t* match = nullptr;
    size_t matchCount = 0;

    if (pattern.empty() || range.end <= range.begin || static_cast<size_t>(range.end - range.begin) < pattern.size())
        return nullptr;

    for (uint8_t* cursor = range.begin; cursor <= range.end - pattern.size(); ++cursor)
    {
        bool ok = true;
        for (size_t i = 0; i < pattern.size(); ++i)
        {
            if (pattern[i] >= 0 && cursor[i] != static_cast<uint8_t>(pattern[i]))
            {
                ok = false;
                break;
            }
        }

        if (!ok)
            continue;

        match = cursor;
        ++matchCount;
        if (matchCount > 1)
            return nullptr;
    }

    return matchCount == 1 ? match : nullptr;
}

bool PatchByte(uint8_t* address, uint8_t value, const char* reason)
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(address, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        Log("VirtualProtect failed for %s at %p, gle=%lu", reason, address, GetLastError());
        return false;
    }

    const uint8_t oldValue = *address;
    *address = value;
    FlushInstructionCache(GetCurrentProcess(), address, 1);

    DWORD ignored = 0;
    VirtualProtect(address, 1, oldProtect, &ignored);

    VerboseLog("patched %s at %p: %02X -> %02X", reason, address, oldValue, value);
    return true;
}

bool PatchBytes(uint8_t* address, const uint8_t* bytes, size_t size, const char* reason)
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        Log("VirtualProtect failed for %s at %p size=%zu, gle=%lu", reason, address, size, GetLastError());
        return false;
    }

    memcpy(address, bytes, size);
    FlushInstructionCache(GetCurrentProcess(), address, size);

    DWORD ignored = 0;
    VirtualProtect(address, size, oldProtect, &ignored);

    VerboseLog("patched %s at %p size=%zu", reason, address, size);
    return true;
}

bool PatchIfBytesMatch(HMODULE module, uintptr_t rva, const uint8_t* expected, const uint8_t* replacement, size_t size, const char* reason)
{
    auto* address = reinterpret_cast<uint8_t*>(module) + rva;
    if (memcmp(address, expected, size) != 0)
    {
        Log("did not patch %s at rva=%p because bytes did not match", reason, reinterpret_cast<void*>(rva));
        return false;
    }

    return PatchBytes(address, replacement, size, reason);
}

bool ApplyHeadlessPatches(HMODULE module)
{
    SectionRange text{};
    if (!GetSectionRange(module, ".text", &text))
    {
        Log("could not locate .text in real materialsystem_dx11.dll");
        return false;
    }

    // The dedicated no-DX path must not execute TFO's queued renderer work.
    // sub_1800BE7D0 drains the material worker queue; the queued callback from
    // sub_180097DA0 creates the D3D11 device and publishes renderer state.
    // Making D3D11CreateDevice fail while still draining the queue leaves the
    // material system half-initialized: forcing the callback result to success
    // then lets studiorender call the absent shader-device interface. Preserve
    // the original fake-nodx behavior and leave the renderer job unexecuted.
    const uint8_t materialWorkerDrainProlog[] = { 0x48, 0x83, 0xEC, 0x38 };
    const uint8_t materialWorkerDrainDisabled[] = { 0xC3, 0x90, 0x90, 0x90 };
    const bool materialWorkerDrainPatched = PatchIfBytesMatch(
        module,
        0xBE7D0,
        materialWorkerDrainProlog,
        materialWorkerDrainDisabled,
        sizeof(materialWorkerDrainDisabled),
        "TFO material worker drain in fake nodx path");

    const std::vector<int> skipErrorShaderPattern = {
        0x48, 0x89, 0x59, 0x20, 0x48, 0x89, 0x71, 0x38, 0x48, 0x83, 0xC1, 0x40,
        0xE8, -1, -1, -1, -1, 0xE8, -1, -1, -1, -1, 0x80, 0x7C, 0x24, 0x40,
        0x00, 0x48, 0x8B, 0x74, 0x24, 0x48, 0x75, 0x0D, 0x32, 0xC0
    };

    bool errorShaderPatched = false;
    if (uint8_t* match = FindUniquePattern(text, skipErrorShaderPattern))
        errorShaderPatched = PatchByte(match + 32, 0xEB, "TFO DX11 error shader branch for nodx proxy");
    else
        Log("did not uniquely find TFO DX11 error shader branch patch pattern");

    // The queued device result is intentionally false in fake-nodx mode.
    // Preserve that internal result so no renderer state is published, but do
    // not report it as a real under-spec GPU.
    const uint8_t gpuErrorBranch[] = { 0x75, 0x24 };
    const uint8_t gpuErrorBranchDisabled[] = { 0xEB, 0x24 };
    const bool gpuErrorBranchPatched = PatchIfBytesMatch(
        module,
        0x97EB6,
        gpuErrorBranch,
        gpuErrorBranchDisabled,
        sizeof(gpuErrorBranchDisabled),
        "TFO fake-nodx under-spec logger branch");

    // sub_180097DA0 is also the material app system's Connect method. Its early
    // validation failures must remain failures, but once the headless setup has
    // completed it must report Connect success to the owning app-system group.
    // Otherwise that group stops connecting at VMaterialSystem083 and never
    // connects later systems such as vgui2 (leaving their filesystem globals
    // null). Change only the final return of the queued device-result byte;
    // the internal result above remains false and no D3D state is fabricated.
    const uint8_t materialConnectResult[] = { 0x40, 0x0F, 0xB6, 0xC7 };
    const uint8_t materialConnectHeadlessSuccess[] = { 0xB0, 0x01, 0x90, 0x90 };
    const bool materialConnectResultPatched = PatchIfBytesMatch(
        module,
        0x97EDC,
        materialConnectResult,
        materialConnectHeadlessSuccess,
        sizeof(materialConnectHeadlessSuccess),
        "TFO fake-nodx completed material Connect result");

    const uint8_t shaderDeviceFinalizeCall[] = { 0xFF, 0x90, 0x48, 0x01, 0x00, 0x00 };
    const uint8_t nopCall[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    const bool shaderFinalizePatched = PatchIfBytesMatch(
        module,
        0x9BCFF,
        shaderDeviceFinalizeCall,
        nopCall,
        sizeof(nopCall),
        "TFO DX11 shader-device finalize call in fake nodx path");
    const bool queuedShaderFinalizePatched = PatchIfBytesMatch(
        module,
        0xFC3A6,
        shaderDeviceFinalizeCall,
        nopCall,
        sizeof(nopCall),
        "TFO DX11 shader-device queued-material finalize call in fake nodx path");

    const uint8_t materialFileLoadProlog[] = { 0x48, 0x8B, 0xC4, 0x48 };
    const uint8_t materialFileLoadDisabled[] = { 0x32, 0xC0, 0xC3, 0x90 };
    const bool materialFileLoadPatched = PatchIfBytesMatch(
        module,
        0x8CFC0,
        materialFileLoadProlog,
        materialFileLoadDisabled,
        sizeof(materialFileLoadDisabled),
        "TFO DX11 VMT file load in fake nodx path");

    return materialWorkerDrainPatched
        && errorShaderPatched
        && gpuErrorBranchPatched
        && materialConnectResultPatched
        && shaderFinalizePatched
        && queuedShaderFinalizePatched
        && materialFileLoadPatched;
}

bool LoadHeadlessCoherentShim()
{
    if (GetModuleHandleW(L"CoherentUIGT.dll"))
    {
        Log("CoherentUIGT.dll was already loaded before the headless shim");
        return false;
    }

    std::wstring proxyDirectory;
    if (!GetProxyDirectory(&proxyDirectory))
        return false;

    const std::wstring shimPath = JoinPath(proxyDirectory, L"CoherentUIGT.dll");
    g_headlessCoherent = LoadLibraryExW(shimPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_headlessCoherent)
    {
        Log("required headless CoherentUIGT shim is unavailable, path=%ls gle=%lu", shimPath.c_str(), GetLastError());
        return false;
    }

    VerboseLog("preloaded headless CoherentUIGT shim from %ls", shimPath.c_str());
    return true;
}

void LoadRealMaterialSystemOnce()
{
    if (!LoadHeadlessCoherentShim())
        return;
    if (!LoadHeadlessGraphicsStubs())
        return;

    const std::wstring path = r1delta::r1o::ResolveTFOModulePathW(L"materialsystem_dx11.dll");
    if (!path.empty())
        g_realMaterialSystem = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_realMaterialSystem)
    {
        const std::wstring validationError = r1delta::r1o::TFORuntimeValidationErrorW();
        Log(
            "failed to load real materialsystem_dx11.dll, path=%ls, gle=%lu, validation=%ls",
            path.empty() ? L"<unresolved>" : path.c_str(),
            GetLastError(),
            validationError.empty() ? L"<ok>" : validationError.c_str());
        return;
    }

    g_realCreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(g_realMaterialSystem, "CreateInterface"));
    if (!g_realCreateInterface)
    {
        Log("real materialsystem_dx11.dll has no CreateInterface export, gle=%lu", GetLastError());
        return;
    }

    VerboseLog("loaded real TFO materialsystem_dx11.dll from %ls", path.c_str());
    if (!ApplyHeadlessPatches(g_realMaterialSystem))
    {
        Log("required headless material patches were not all applied; refusing to expose the material system");
        return;
    }
    g_loadOk = true;
}

bool EnsureLoaded()
{
    std::call_once(g_loadOnce, LoadRealMaterialSystemOnce);
    return g_loadOk;
}

void __fastcall MaterialSystemSetShaderAPI(void* thisptr, const char* shaderApi)
{
    VerboseLog("SetShaderAPI(%s) suppressed for fake nodx object=%p", shaderApi ? shaderApi : "<null>", thisptr);
}

int __fastcall MaterialSystemInitNoDx(void* thisptr)
{
    // IAppSystem::Init must succeed so later app systems are initialized, but
    // the TFO implementation is the GPU-dependent path that loads
    // resource/tfodata/gpu_blacklist.csv and creates shader-device state.
    VerboseLog("IAppSystem::Init suppressed with INIT_OK for fake nodx object=%p", thisptr);
    return 1;
}

void __fastcall MaterialSystemSetThreadMode2015(void* thisptr, int mode)
{
    VerboseLog("2015 dedicated material thread-mode slot suppressed for fake nodx object=%p mode=%d", thisptr, mode);
}

void __fastcall MaterialSystemR1OModInitNoDx(void* thisptr)
{
    VerboseLog("R1O material ModInit slot suppressed for fake nodx object=%p", thisptr);
}

__int64 __fastcall FakeMatRenderContextNoOp()
{
    return 0;
}

void* __fastcall MaterialSystemGetRenderContextNoDx(void* thisptr)
{
    static std::once_flag initOnce;
    std::call_once(initOnce, [] {
        for (uintptr_t& slot : g_fakeRenderContextVTable)
            slot = reinterpret_cast<uintptr_t>(&FakeMatRenderContextNoOp);
    });

    static std::once_flag logOnce;
    std::call_once(logOnce, [thisptr] {
        VerboseLog("GetRenderContext returning fake no-op context for fake nodx object=%p context=%p", thisptr, &g_fakeRenderContextVTablePtr);
    });

    return &g_fakeRenderContextVTablePtr;
}

bool PatchVTableSlot(void** slot, void* hook, void** original, const char* reason)
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        Log("failed to make vtable writable for %s patch, slot=%p gle=%lu", reason, slot, GetLastError());
        return false;
    }

    *original = *slot;
    *slot = hook;
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));

    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), oldProtect, &ignored);
    return true;
}

bool PatchMaterialSystemForNoDx(void* materialSystem)
{
    std::lock_guard<std::mutex> lock(g_materialSystemPatchMutex);
    static bool patchOk = false;
    if (!materialSystem)
        return false;

    auto vtable = *reinterpret_cast<void***>(materialSystem);
    if (!vtable)
        return false;

    if (g_materialSystemVTable == vtable)
        return patchOk;

    const bool initPatched = PatchVTableSlot(
        &vtable[3],
        reinterpret_cast<void*>(&MaterialSystemInitNoDx),
        reinterpret_cast<void**>(&g_materialSystemInitOriginal),
        "VMaterialSystem083 IAppSystem::Init");
    const bool setShaderApiPatched = PatchVTableSlot(
        &vtable[6],
        reinterpret_cast<void*>(&MaterialSystemSetShaderAPI),
        reinterpret_cast<void**>(&g_materialSystemSetShaderAPIOriginal),
        "VMaterialSystem083 SetShaderAPI");
    const bool threadModePatched = PatchVTableSlot(
        &vtable[9],
        reinterpret_cast<void*>(&MaterialSystemSetThreadMode2015),
        reinterpret_cast<void**>(&g_materialSystemThreadModeOriginal),
        "VMaterialSystem083 2015 dedicated thread-mode");
    const bool r1oModInitPatched = PatchVTableSlot(
        &vtable[21],
        reinterpret_cast<void*>(&MaterialSystemR1OModInitNoDx),
        reinterpret_cast<void**>(&g_materialSystemR1OModInitOriginal),
        "VMaterialSystem083 R1O fake nodx ModInit");
    const bool renderContextPatched = PatchVTableSlot(
        &vtable[145],
        reinterpret_cast<void*>(&MaterialSystemGetRenderContextNoDx),
        reinterpret_cast<void**>(&g_materialSystemGetRenderContextOriginal),
        "VMaterialSystem083 fake nodx render context");

    g_materialSystemVTable = vtable;
    patchOk = initPatched
        && setShaderApiPatched
        && threadModePatched
        && r1oModInitPatched
        && renderContextPatched;
    VerboseLog(
        "patched VMaterialSystem083 fake nodx slots object=%p vt=%p init=%d original=%p hook=%p setShaderAPI=%d original=%p hook=%p threadMode=%d original=%p hook=%p r1oModInit=%d original=%p hook=%p renderContext=%d original=%p hook=%p",
        materialSystem,
        vtable,
        initPatched ? 1 : 0,
        reinterpret_cast<void*>(g_materialSystemInitOriginal),
        reinterpret_cast<void*>(&MaterialSystemInitNoDx),
        setShaderApiPatched ? 1 : 0,
        reinterpret_cast<void*>(g_materialSystemSetShaderAPIOriginal),
        reinterpret_cast<void*>(&MaterialSystemSetShaderAPI),
        threadModePatched ? 1 : 0,
        reinterpret_cast<void*>(g_materialSystemThreadModeOriginal),
        reinterpret_cast<void*>(&MaterialSystemSetThreadMode2015),
        r1oModInitPatched ? 1 : 0,
        reinterpret_cast<void*>(g_materialSystemR1OModInitOriginal),
        reinterpret_cast<void*>(&MaterialSystemR1OModInitNoDx),
        renderContextPatched ? 1 : 0,
        reinterpret_cast<void*>(g_materialSystemGetRenderContextOriginal),
        reinterpret_cast<void*>(&MaterialSystemGetRenderContextNoDx));
    return patchOk;
}
}

extern "C" __declspec(dllexport) void* __cdecl CreateInterface(const char* name, int* returnCode)
{
    if (!EnsureLoaded())
    {
        if (returnCode)
            *returnCode = 1;
        return nullptr;
    }

    void* result = g_realCreateInterface(name, returnCode);
    VerboseLog("CreateInterface(%s) -> %p rc=%d", name ? name : "<null>", result, returnCode ? *returnCode : 0);
    if (result && name && _stricmp(name, "VMaterialSystem083") == 0
        && !PatchMaterialSystemForNoDx(result))
    {
        Log("required VMaterialSystem083 headless hooks were not all applied; refusing the interface");
        if (returnCode)
            *returnCode = 1;
        return nullptr;
    }
    return result;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_proxyModule = instance;
        DisableThreadLibraryCalls(instance);
        VerboseLog("proxy attached");
    }
    return TRUE;
}
