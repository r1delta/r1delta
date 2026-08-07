#include "../engine/memory/memory.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>

namespace {

bool Check(bool condition, const char* what)
{
    if (!condition)
        std::printf("FAIL: %s\n", what);
    return condition;
}

} // namespace

int main()
{
    bool passed = true;

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
        std::printf("Allocator delegation tests skipped (deployed tier0 pair not staged)\n");
        return 0;
    }

    IMemAlloc* ours = ourCreate();
    IMemAlloc* retail = retailCreate();

    std::printf("A: ours=%p retail=%p\n", static_cast<void*>(ours), static_cast<void*>(retail));
    std::fflush(stdout);

    passed &= Check(ours != nullptr, "our CreateGlobalMemAlloc returned non-null");
    passed &= Check(retail != nullptr, "retail CreateGlobalMemAlloc returned non-null");
    passed &= Check(ours == retail,
        "our tier0 forwards to the same retail allocator singleton");

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
