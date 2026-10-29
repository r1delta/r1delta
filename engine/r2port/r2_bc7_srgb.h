#pragma once
// BC7 passthrough support for the retail materialsystem_dx11.dll (sha256 0d2337844afb0e6e3c2079638c798101bf25910d1360dd0f64bf4e1d9c6b78c8,
// 2,801,664 bytes). The engine maps the VTF flag 0x80000 (sRGB) to DXGI 29/72/75/78 for RGBA8/BC1/BC2/BC3 only: BC7 is created as
// DXGI_FORMAT_BC7_UNORM (98), so an sRGB BC7 texture would be sampled linear. The 54 byte switch inside the texture creator
// (RVA 0x16663, image format in r10d) is rewritten so that 98 maps to 99 (BC7_UNORM_SRGB); every sRGB variant is base + 1, one
// `inc r10d` serves all five formats. Plain code bytes, no relocations; original bytes are checked by the patcher before writing.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <Windows.h>

namespace r2port {

constexpr long long kMatsysSrgbSwitchRva = 0x16663;
constexpr std::size_t kMatsysSrgbSwitchSize = 54;

// 41 83 fa 1c / 74 2a / 41 83 fa 47 / 74 1c / 41 83 fa 4a / 74 0e / 41 83 fa 4d / 75 1e / mov r10d,4eh / jmp / mov r10d,4bh / jmp /
// mov r10d,48h / jmp / mov r10d,1dh
inline const unsigned char kMatsysSrgbSwitchOriginal[kMatsysSrgbSwitchSize] = {
    0x41, 0x83, 0xfa, 0x1c, 0x74, 0x2a, 0x41, 0x83, 0xfa, 0x47, 0x74, 0x1c, 0x41, 0x83, 0xfa, 0x4a,
    0x74, 0x0e, 0x41, 0x83, 0xfa, 0x4d, 0x75, 0x1e, 0x41, 0xba, 0x4e, 0x00, 0x00, 0x00, 0xeb, 0x16,
    0x41, 0xba, 0x4b, 0x00, 0x00, 0x00, 0xeb, 0x0e, 0x41, 0xba, 0x48, 0x00, 0x00, 0x00, 0xeb, 0x06,
    0x41, 0xba, 0x1d, 0x00, 0x00, 0x00};

// cmp r10d,1Ch / je +18 / cmp r10d,47h / je +12 / cmp r10d,4Ah / je +0C / cmp r10d,4Dh / je +06 / cmp r10d,62h / jne +18 / inc r10d / 21 nops
inline const unsigned char kMatsysSrgbSwitchPatched[kMatsysSrgbSwitchSize] = {
    0x41, 0x83, 0xfa, 0x1c, 0x74, 0x18, 0x41, 0x83, 0xfa, 0x47, 0x74, 0x12, 0x41, 0x83, 0xfa, 0x4a,
    0x74, 0x0c, 0x41, 0x83, 0xfa, 0x4d, 0x74, 0x06, 0x41, 0x83, 0xfa, 0x62, 0x75, 0x18, 0x41, 0xff,
    0xc2, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90};

// True when the loaded materialsystem_dx11.dll carries the patched switch (BC7 sRGB textures sample as sRGB).
inline bool MatsysBc7SrgbPatched() noexcept
{
    const HMODULE module = GetModuleHandleA("materialsystem_dx11.dll");
    if (!module)
        return false;
    const auto* code = reinterpret_cast<const unsigned char*>(module) + kMatsysSrgbSwitchRva;
    return std::memcmp(code, kMatsysSrgbSwitchPatched, kMatsysSrgbSwitchSize) == 0;
}



// True when the default adapter supports D3D feature level 11.0 (BC7 needs it). The probe creates no device (the engine creates its own with
// the same default adapter and no feature level list), runs once and is cached; false when it fails.
inline bool GpuHasFeatureLevel11() noexcept
{
    static const bool supported = []() {
        using CreateDeviceFn = HRESULT(WINAPI*)(void*, UINT, HMODULE, UINT, const UINT*, UINT, UINT, void**, UINT*, void**);
        const HMODULE d3d11 = LoadLibraryExA("d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!d3d11)
            return false;
        const auto createDevice = reinterpret_cast<CreateDeviceFn>(GetProcAddress(d3d11, "D3D11CreateDevice"));
        if (!createDevice)
            return false;
        UINT featureLevel = 0;
        // D3D_DRIVER_TYPE_HARDWARE = 1, D3D11_SDK_VERSION = 7, D3D_FEATURE_LEVEL_11_0 = 0xB000.
        const HRESULT hr = createDevice(nullptr, 1, nullptr, 0, nullptr, 0, 7, nullptr, &featureLevel, nullptr);
        return SUCCEEDED(hr) && featureLevel >= 0xB000;
    }();
    return supported;
}

} // namespace r2port
