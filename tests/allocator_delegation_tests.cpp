#include "../engine/memory/memory.h"
#include "../engine/core/client_port_override.h"

#include <Windows.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace {

bool Check(bool condition, const char* what)
{
    if (!condition)
        std::printf("FAIL: %s\n", what);
    return condition;
}

using FnMiniDump = void(*)(unsigned int, EXCEPTION_POINTERS*, const char*);
using SetMiniDumpFunctionFn = FnMiniDump(*)(FnMiniDump);

void ProbeMiniDumpCallback(unsigned int, EXCEPTION_POINTERS*, const char*)
{
}
ConVarR1* capturedClientPort = nullptr;
char capturedClientPortValue[16]{};
int capturedClientPortCalls = 0;

void CaptureClientPort(ConVarR1* variable, const char* value)
{
    capturedClientPort = variable;
    ++capturedClientPortCalls;
    strcpy_s(capturedClientPortValue, value);
}

bool TestClientPortUsesEngineSetter()
{
    using r1delta::client_port::ApplyOverride;

    bool passed = true;
    auto* const variable = reinterpret_cast<ConVarR1*>(0x1234);
    passed &= Check(
        ApplyOverride(variable, 27012, &CaptureClientPort),
        "clientport override rejected a valid port");
    passed &= Check(
        capturedClientPort == variable
            && capturedClientPortCalls == 1
            && std::strcmp(capturedClientPortValue, "27012") == 0,
        "clientport override did not delegate the decimal value to the engine setter");

    passed &= Check(
        ApplyOverride(variable, 65535, &CaptureClientPort)
            && capturedClientPortCalls == 2
            && std::strcmp(capturedClientPortValue, "65535") == 0,
        "clientport override rejected the maximum valid port");
    passed &= Check(
        !ApplyOverride(variable, 0, &CaptureClientPort)
            && !ApplyOverride(variable, 65536, &CaptureClientPort)
            && !ApplyOverride(nullptr, 27012, &CaptureClientPort)
            && !ApplyOverride(variable, 27012, nullptr)
            && capturedClientPortCalls == 2,
        "clientport override accepted invalid state or invoked the setter");
    return passed;
}


} // namespace

int main()
{
    bool passed = true;
    passed &= TestClientPortUsesEngineSetter();


    // Load the deployed binaries exactly as the game does: our tier0.dll
    // (bin_delta) plus the pristine retail tier0_orig.dll (bin). The engine
    // imports CreateGlobalMemAlloc from our tier0, which must forward to the
    // retail singleton in tier0_orig.
    HMODULE ourTier0 = LoadLibraryW(L"tier0.dll");
    HMODULE retailTier0 = GetModuleHandleW(L"tier0_orig.dll");
    if (!retailTier0)
        retailTier0 = LoadLibraryW(L"tier0_orig.dll");

    // This test verifies the forwarding relationship between the two deployed
    // binaries. CI runs before the payload is staged, so tier0.dll (our build)
    // is not on the test search path and tier0_orig.dll may be an unrelated
    // checkout artifact. Only run the assertions when both resolve their
    // CreateGlobalMemAlloc export; otherwise skip.
    using CreateGlobalMemAllocFn = IMemAlloc* (WINAPI*)();
    CreateGlobalMemAllocFn ourCreate = nullptr;
    CreateGlobalMemAllocFn retailCreate = nullptr;
    if (ourTier0)
        ourCreate = reinterpret_cast<CreateGlobalMemAllocFn>(
            GetProcAddress(ourTier0, "CreateGlobalMemAlloc"));
    if (retailTier0)
        retailCreate = reinterpret_cast<CreateGlobalMemAllocFn>(
            GetProcAddress(retailTier0, "CreateGlobalMemAlloc"));

    if (!ourCreate || !retailCreate) {
        std::printf("Allocator delegation integration tests skipped (deployed tier0 pair not staged)\n");
        std::printf("%s\n", passed ? "All allocator boundary tests passed" : "Allocator boundary tests FAILED");
        return passed ? 0 : 1;
    }

    IMemAlloc* ours = ourCreate();
    IMemAlloc* retail = retailCreate();

    std::printf("A: ours=%p retail=%p\n", static_cast<void*>(ours), static_cast<void*>(retail));
    std::fflush(stdout);

    passed &= Check(ours != nullptr, "our CreateGlobalMemAlloc returned non-null");
    passed &= Check(retail != nullptr, "retail CreateGlobalMemAlloc returned non-null");
    passed &= Check(ours == retail,
        "our tier0 forwards to the same retail allocator singleton");

    auto setMiniDumpFunction = reinterpret_cast<SetMiniDumpFunctionFn>(
        GetProcAddress(retailTier0, "SetMiniDumpFunction"));
    passed &= Check(setMiniDumpFunction != nullptr,
        "retail tier0 exports SetMiniDumpFunction");
    if (setMiniDumpFunction) {
        FnMiniDump previous = setMiniDumpFunction(&ProbeMiniDumpCallback);
        FnMiniDump installed = setMiniDumpFunction(previous);
        passed &= Check(installed == &ProbeMiniDumpCallback,
            "retail minidump callback setter round-trips the installed callback");
    }

    // Dump the retail allocator's vtable slots so we can compare its layout to
    // our IMemAlloc declaration. If the layouts differ, virtual dispatch on the
    // retail object through our interface calls the wrong slot.
    if (retail) {
        std::printf("B: dumping vtable\n");
        std::fflush(stdout);
        uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(retail);
        std::printf("C: retail allocator=%p vtable=%p\n", static_cast<void*>(retail), static_cast<void*>(vtable));
        std::fflush(stdout);
        for (int i = 0; i < 48; ++i)
            std::printf("  slot[%02d] = 0x%llx\n", i, static_cast<unsigned long long>(vtable[i]));
        std::fflush(stdout);
    }

    IMemAlloc* const singleton = ours ? ours : retail;
    if (singleton) {
        std::printf("D: Alloc(4096)\n");
        std::fflush(stdout);
        void* p = singleton->Alloc(4096);
        std::printf("E: Alloc done p=%p\n", p);
        std::fflush(stdout);
        passed &= Check(p != nullptr, "Alloc(4096) returned non-null");
        if (p) {
            std::memset(p, 0xAB, 4096);
            std::printf("F: GetSize\n");
            std::fflush(stdout);
            size_t sz = singleton->GetSize(p);
            std::printf("G: GetSize done=%zu\n", sz);
            std::fflush(stdout);
            passed &= Check(sz >= 4096, "GetSize covers the request");
            std::printf("H: Realloc(8192)\n");
            std::fflush(stdout);
            void* r = singleton->Realloc(p, 8192);
            std::printf("I: Realloc done r=%p\n", r);
            std::fflush(stdout);
            passed &= Check(r != nullptr, "Realloc(8192) returned non-null");
            if (r) {
                std::memset(r, 0xCD, 8192);
                std::printf("J: Free(r)\n");
                std::fflush(stdout);
                singleton->Free(r);
                std::printf("K: Free done\n");
                std::fflush(stdout);
            } else {
                singleton->Free(p);
            }
        }

        std::printf("L: Alloc_Aligned(512,16)\n");
        std::fflush(stdout);
        void* aligned = singleton->Alloc_Aligned(512, 16);
        std::printf("M: Alloc_Aligned done=%p\n", aligned);
        std::fflush(stdout);
        passed &= Check(aligned != nullptr, "Alloc_Aligned(512,16) returned non-null");
        if (aligned) {
            passed &= Check((reinterpret_cast<uintptr_t>(aligned) % 16) == 0,
                "Alloc_Aligned is 16-byte aligned");
            singleton->Free_Aligned(aligned, 16);
        }

        constexpr size_t requestedMsvcAlignment = alignof(std::max_align_t) * 2;
        const char initialValue[] = "";
        char* conVarValue = static_cast<char*>(
            singleton->Alloc_Aligned(sizeof(initialValue), requestedMsvcAlignment));
        passed &= Check(conVarValue != nullptr,
            "R1O ConVar initial string allocation returned non-null");
        passed &= Check(
            !conVarValue || (reinterpret_cast<uintptr_t>(conVarValue) % alignof(std::max_align_t)) == 0,
            "R1O ConVar string allocation satisfies the allocator's guaranteed alignment");
        if (conVarValue) {
            std::memcpy(conVarValue, initialValue, sizeof(initialValue));
            const char replacementValue[] = "replacement value longer than the initial buffer";
            char* replacement = static_cast<char*>(
                singleton->Alloc_Aligned(sizeof(replacementValue), requestedMsvcAlignment));
            passed &= Check(replacement != nullptr,
                "R1O ConVar replacement string allocation returned non-null");
            if (replacement) {
                singleton->Free_Aligned(conVarValue, requestedMsvcAlignment);
                conVarValue = nullptr;
                std::memcpy(replacement, replacementValue, sizeof(replacementValue));
                passed &= Check(std::strcmp(replacement, replacementValue) == 0,
                    "R1O ConVar replacement string preserved its value");
                singleton->Free_Aligned(replacement, requestedMsvcAlignment);
            }
            if (conVarValue)
                singleton->Free_Aligned(conVarValue, requestedMsvcAlignment);
        }

        // mi_* forwarding helpers must route through the interface.
        std::printf("N: mi_malloc(256)\n");
        std::fflush(stdout);
        void* viaHelper = singleton->mi_malloc(256);
        std::printf("O: mi_malloc done=%p\n", viaHelper);
        std::fflush(stdout);
        passed &= Check(viaHelper != nullptr, "mi_malloc helper returned non-null");
        if (viaHelper)
            singleton->mi_free(viaHelper);
        void* viaHelperAligned = singleton->mi_malloc_aligned(256, 8);
        passed &= Check(viaHelperAligned != nullptr, "mi_malloc_aligned helper returned non-null");
        if (viaHelperAligned)
            singleton->mi_free_aligned(viaHelperAligned, 8);
    }

    std::printf("%s\n", passed ? "All allocator delegation tests passed" : "Allocator delegation tests FAILED");
    return passed ? 0 : 1;
}
