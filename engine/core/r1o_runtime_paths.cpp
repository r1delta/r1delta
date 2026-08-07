#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include "r1o_runtime_paths.h"

#include <array>
#include <cstdint>
#include <cwchar>
#include <mutex>
#include <string>
#include <vector>

namespace r1delta::r1o
{
namespace
{
struct RuntimeModuleSpec
{
    const wchar_t* logicalName;
    const wchar_t* packageName;
    const wchar_t* allInOneName;
    bool useBaseBin;
    uint64_t expectedSize;
    const char* expectedSha256;
};

constexpr std::array<RuntimeModuleSpec, 14> kRuntimeModules = {{
    { L"server_local.dll", L"server.dll", L"server_local.dll", true, 13758792, "50FAAD928E0715A51AD624D0A3EB8F00F7FFAD9E66258DC95C960F9D11FF41E4" },
    { L"GBClient.dll", L"gbclient.dll", L"GBClient.dll", true, 339784, "426E1811FB9B8DFEFAED33B946448D5D8D3DC667401CAF34B30350794FD319DD" },
    { L"datacache.dll", L"datacache.dll", L"datacache.dll", false, 534344, "94E851F8E3BE809907E1F22E974F974527F4F1F571B50CEDC69CBBCA93E38531" },
    { L"studiorender.dll", L"studiorender.dll", L"studiorender.dll", false, 386888, "3E954D6F0EBB026553035EBFE31AD74065C3A2CC21DA7E899647B0C9BC6E9F50" },
    { L"materialsystem_dx11.dll", L"materialsystem_dx11.dll", L"materialsystem_dx11.dll", false, 1946952, "DB3606606534783DF674E02451F9DA9B7C0E3F20DB6850E3AA381DE34D7AA4AB" },
    { L"GFSDK_SSAO.win64.dll", L"GFSDK_SSAO.win64.dll", L"GFSDK_SSAO.win64.dll", false, 979272, "D2D0AF1AEEBF448733A49D41F30814680AD5D1929FE4C9E170CD2BD27C6B37F9" },
    { L"GFSDK_TXAA.win64.dll", L"GFSDK_TXAA.win64.dll", L"GFSDK_TXAA.win64.dll", false, 106824, "5E7CC7694DF4463CC5D0F5402C952D2B0A57D2D21F054F5DFA46647ACF3D6D92" },
    // BugTrap-x64.dll is intentionally absent: the Nexon binary is not
    // redistributable in the Wine dedicated image, and the stub built by the
    // bugtrap_stub project (bin_delta) satisfies the static imports from
    // materialsystem_dx11.dll and launcher.dll. Do not re-add a real-binary
    // size/hash entry here or the layout validation will reject the stub.
    { L"vphysics.dll", L"vphysics.dll", L"vphysics.dll", false, 1670472, "229EB841628A677DFCC653D81F15C67F0D3D405F399BE1AF5024F17D61F6641B" },
    { L"inputsystem.dll", L"inputsystem.dll", L"inputsystem.dll", false, 250184, "B94CFCCC7D8786646D717A56CAA7448813F6CE19CB01CDC2327E9FB1F5AA4911" },
    { L"localize.dll", L"localize.dll", L"localize.dll", false, 250184, "F883A88F0C74719CAB96039E2C73521038135D40B5210FF2F479E326583D3EB4" },
    { L"vguimatsurface.dll", L"vguimatsurface.dll", L"vguimatsurface.dll", false, 1554760, "62E3DDF2F0F885F81A87435AAD4897CCDE789EEEB93B24D5AA32595861897AAB" },
    { L"filesystem_stdio.dll", L"filesystem_stdio.dll", L"filesystem_stdio.dll", false, 1903432, "7EDF98A45B171F7168224A8C989C6F92CB09EBB8DE7DFD6333F64DA68BBA0E79" },
    { L"launcher.dll", L"launcher.dll", L"launcher.dll", false, 1041736, "10B75ABC67EADC02CF0BDCEBAB8BCA1EB76FD241ACA4D5A2A69A125A1B4AA0F6" },
}};

struct RuntimeLayout
{
    std::wstring nexonDirectory;
    std::wstring baseBinDirectory;
    bool allInOne = false;
};

struct RuntimeState
{
    RuntimeLayout layout;
    bool valid = false;
    std::wstring error;
};

std::mutex g_runtimeStateMutex;
std::wstring g_runtimeStateKey;
RuntimeState g_runtimeState;

bool IsFile(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool IsDirectory(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring JoinPath(const std::wstring& directory, const wchar_t* name)
{
    if (directory.empty())
        return name ? name : L"";

    std::wstring path = directory;
    if (path.back() != L'\\' && path.back() != L'/')
        path.push_back(L'\\');
    if (name)
        path.append(name);
    return path;
}

std::wstring ParentDirectory(std::wstring path)
{
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
        path.pop_back();

    const size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return {};
    path.resize(separator);
    return path;
}

const wchar_t* BaseName(const wchar_t* path)
{
    if (!path)
        return L"";

    const wchar_t* base = path;
    for (const wchar_t* it = path; *it; ++it) {
        if (*it == L'\\' || *it == L'/')
            base = it + 1;
    }
    return base;
}

std::wstring ModuleDirectory(HMODULE module)
{
    if (!module)
        return {};

    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size())
        return {};
    return ParentDirectory(std::wstring(path.data(), length));
}

std::wstring ExecutableDirectory()
{
    return ModuleDirectory(GetModuleHandleW(nullptr));
}

std::wstring EnvironmentDirectory(const wchar_t* name)
{
    std::array<wchar_t, 32768> value{};
    const DWORD length = GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
    if (!length || length >= value.size())
        return {};

    std::wstring result(value.data(), length);
    const size_t begin = result.find_first_not_of(L" \t");
    const size_t end = result.find_last_not_of(L" \t");
    if (begin == std::wstring::npos)
        return {};
    result = result.substr(begin, end - begin + 1);
    if (result.size() >= 2 && result.front() == L'"' && result.back() == L'"')
        result = result.substr(1, result.size() - 2);
    while (result.size() > 3 && (result.back() == L'\\' || result.back() == L'/'))
        result.pop_back();
    return result;
}

std::string Narrow(const std::wstring& value)
{
    if (value.empty())
        return {};

    const int length = WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1)
        return {};

    std::string result(static_cast<size_t>(length), '\0');
    if (!WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, result.data(), length, nullptr, nullptr))
        return {};
    result.pop_back();
    return result;
}

std::wstring Widen(const char* value)
{
    if (!value || !*value)
        return {};

    const int length = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
    if (length <= 1)
        return {};

    std::wstring result(static_cast<size_t>(length), L'\0');
    if (!MultiByteToWideChar(CP_ACP, 0, value, -1, result.data(), length))
        return {};
    result.pop_back();
    return result;
}

bool PathsEqual(const std::wstring& left, const std::wstring& right)
{
    if (left.empty() || right.empty())
        return false;

    std::array<wchar_t, 32768> leftFull{};
    std::array<wchar_t, 32768> rightFull{};
    const DWORD leftLength = GetFullPathNameW(left.c_str(), static_cast<DWORD>(leftFull.size()), leftFull.data(), nullptr);
    const DWORD rightLength = GetFullPathNameW(right.c_str(), static_cast<DWORD>(rightFull.size()), rightFull.data(), nullptr);
    if (!leftLength || leftLength >= leftFull.size() || !rightLength || rightLength >= rightFull.size())
        return _wcsicmp(left.c_str(), right.c_str()) == 0;
    return _wcsicmp(leftFull.data(), rightFull.data()) == 0;
}

bool FileSizeMatches(const std::wstring& path, uint64_t expectedSize)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return false;

    const uint64_t size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return size == expectedSize;
}

bool ComputeSha256(const std::wstring& path, std::array<unsigned char, 32>& digest)
{
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD bytesReturned = 0;
    std::vector<unsigned char> object;
    bool success = false;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        goto cleanup;
    if (BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength),
            sizeof(objectLength),
            &bytesReturned,
            0) < 0)
        goto cleanup;

    object.resize(objectLength);
    if (BCryptCreateHash(
            algorithm,
            &hash,
            object.empty() ? nullptr : object.data(),
            static_cast<ULONG>(object.size()),
            nullptr,
            0,
            0) < 0)
        goto cleanup;

    {
        std::array<unsigned char, 64 * 1024> buffer{};
        for (;;) {
            DWORD bytesRead = 0;
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr))
                goto cleanup;
            if (!bytesRead)
                break;
            if (BCryptHashData(hash, buffer.data(), bytesRead, 0) < 0)
                goto cleanup;
        }
    }

    success = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;

cleanup:
    if (hash)
        BCryptDestroyHash(hash);
    if (algorithm)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return success;
}

std::string HexDigest(const std::array<unsigned char, 32>& digest)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string value;
    value.resize(digest.size() * 2);
    for (size_t index = 0; index < digest.size(); ++index) {
        value[index * 2] = kHex[digest[index] >> 4];
        value[index * 2 + 1] = kHex[digest[index] & 0xF];
    }
    return value;
}

const RuntimeModuleSpec* FindModuleSpec(const wchar_t* moduleName)
{
    const wchar_t* base = BaseName(moduleName);
    for (const RuntimeModuleSpec& spec : kRuntimeModules) {
        if (_wcsicmp(base, spec.logicalName) == 0)
            return &spec;
        if (_wcsicmp(base, spec.packageName) == 0 && spec.useBaseBin)
            return &spec;
    }
    return nullptr;
}

std::wstring ModulePath(const RuntimeLayout& layout, const RuntimeModuleSpec& spec)
{
    if (layout.allInOne)
        return JoinPath(layout.nexonDirectory, spec.allInOneName);
    return JoinPath(spec.useBaseBin ? layout.baseBinDirectory : layout.nexonDirectory, spec.packageName);
}

RuntimeLayout PackageLayoutFromRoot(const std::wstring& root)
{
    RuntimeLayout layout;
    layout.nexonDirectory = JoinPath(root, L"bin_nexon");
    layout.baseBinDirectory = JoinPath(root, L"bin");
    return layout;
}

RuntimeLayout RequestedLayout()
{
    const std::wstring overrideDirectory = EnvironmentDirectory(L"R1DELTA_TFO_BIN");
    if (!overrideDirectory.empty()) {
        const std::wstring nestedNexon = JoinPath(overrideDirectory, L"bin_nexon");
        const std::wstring nestedBin = JoinPath(overrideDirectory, L"bin");
        if (IsDirectory(nestedNexon) && IsDirectory(nestedBin))
            return PackageLayoutFromRoot(overrideDirectory);

        if (_wcsicmp(BaseName(overrideDirectory.c_str()), L"bin_nexon") == 0) {
            RuntimeLayout layout;
            layout.nexonDirectory = overrideDirectory;
            layout.baseBinDirectory = JoinPath(ParentDirectory(overrideDirectory), L"bin");
            return layout;
        }

        RuntimeLayout layout;
        layout.nexonDirectory = overrideDirectory;
        layout.baseBinDirectory = overrideDirectory;
        layout.allInOne = true;
        return layout;
    }

    const std::wstring tier0Directory = ModuleDirectory(GetModuleHandleW(L"tier0.dll"));
    if (!tier0Directory.empty()) {
        const RuntimeLayout tier0Layout = PackageLayoutFromRoot(ParentDirectory(tier0Directory));
        if (IsDirectory(tier0Layout.nexonDirectory) && IsDirectory(tier0Layout.baseBinDirectory))
            return tier0Layout;
    }

    return PackageLayoutFromRoot(JoinPath(ExecutableDirectory(), L"r1delta"));
}

std::wstring LayoutKey(const RuntimeLayout& layout)
{
    return layout.nexonDirectory + L"|" + layout.baseBinDirectory + (layout.allInOne ? L"|1" : L"|0");
}

RuntimeState ValidateLayout(const RuntimeLayout& layout)
{
    RuntimeState state;
    state.layout = layout;

    if (!IsDirectory(layout.nexonDirectory)) {
        state.error = L"R1O runtime directory is missing: " + layout.nexonDirectory;
        return state;
    }
    if (!layout.allInOne && !IsDirectory(layout.baseBinDirectory)) {
        state.error = L"R1 base binary directory is missing: " + layout.baseBinDirectory;
        return state;
    }

    for (const RuntimeModuleSpec& spec : kRuntimeModules) {
        const std::wstring path = ModulePath(layout, spec);
        if (!IsFile(path)) {
            state.error = L"R1O runtime file is missing: " + path;
            return state;
        }
        if (!FileSizeMatches(path, spec.expectedSize)) {
            state.error = L"R1O runtime file has the wrong size: " + path;
            return state;
        }

        std::array<unsigned char, 32> digest{};
        if (!ComputeSha256(path, digest)) {
            state.error = L"R1O runtime file could not be hashed: " + path;
            return state;
        }
        if (_stricmp(HexDigest(digest).c_str(), spec.expectedSha256) != 0) {
            state.error = L"R1O runtime file has the wrong SHA-256: " + path;
            return state;
        }
    }

    state.valid = true;
    return state;
}

RuntimeState CurrentRuntimeState()
{
    const RuntimeLayout requested = RequestedLayout();
    const std::wstring key = LayoutKey(requested);

    std::lock_guard<std::mutex> lock(g_runtimeStateMutex);
    if (key != g_runtimeStateKey) {
        g_runtimeState = ValidateLayout(requested);
        g_runtimeStateKey = key;
        if (!g_runtimeState.valid) {
            const std::wstring message = L"[r1delta_r1o_runtime] " + g_runtimeState.error + L"\n";
            OutputDebugStringW(message.c_str());
        }
    }
    return g_runtimeState;
}
}

std::wstring ResolveTFOBinDirectoryW()
{
    const RuntimeState state = CurrentRuntimeState();
    return state.valid ? state.layout.nexonDirectory : std::wstring{};
}

std::string ResolveTFOBinDirectoryA()
{
    return Narrow(ResolveTFOBinDirectoryW());
}

std::wstring ResolveTFOModulePathW(const wchar_t* moduleName)
{
    if (!moduleName || !*moduleName)
        return {};

    const RuntimeModuleSpec* spec = FindModuleSpec(moduleName);
    if (!spec)
        return moduleName;

    const RuntimeState state = CurrentRuntimeState();
    return state.valid ? ModulePath(state.layout, *spec) : std::wstring{};
}

std::string ResolveTFOModulePathA(const char* moduleName)
{
    const std::wstring wideName = Widen(moduleName);
    if (wideName.empty())
        return moduleName ? moduleName : "";
    return Narrow(ResolveTFOModulePathW(wideName.c_str()));
}

std::wstring TFORuntimeValidationErrorW()
{
    const RuntimeState state = CurrentRuntimeState();
    return state.valid ? std::wstring{} : state.error;
}

std::string TFORuntimeValidationErrorA()
{
    return Narrow(TFORuntimeValidationErrorW());
}

bool IsTFORuntimeModulePathW(const wchar_t* modulePath)
{
    if (!modulePath || !*modulePath)
        return false;

    const RuntimeState state = CurrentRuntimeState();
    if (!state.valid)
        return false;

    for (const RuntimeModuleSpec& spec : kRuntimeModules) {
        if (PathsEqual(modulePath, ModulePath(state.layout, spec)))
            return true;
    }
    return false;
}

bool IsTFORuntimeModulePathA(const char* modulePath)
{
    const std::wstring widePath = Widen(modulePath);
    return !widePath.empty() && IsTFORuntimeModulePathW(widePath.c_str());
}
}
