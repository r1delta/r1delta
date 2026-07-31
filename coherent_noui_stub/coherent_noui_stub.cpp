#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <mutex>

extern "C" void* __fastcall CoherentILogHandlerCtor(void* self)
{
    return self;
}

extern "C" void __fastcall CoherentILogHandlerDtor(void*)
{
}

extern "C" void* __fastcall CoherentResourceHandlerCtor(void* self)
{
    return self;
}

extern "C" void __fastcall CoherentResourceHandlerDtor(void*)
{
}

extern "C" void __fastcall CoherentResourceHandlerOnModifyHeaders(void*, void*)
{
}

extern "C" void __fastcall CoherentResourceHandlerOnShouldLoadResourceRequest(void*, const void*, void*)
{
}

extern "C" bool __fastcall CoherentURLParse(const char*, void*, void*, void*)
{
    return false;
}

extern "C" const char* __fastcall CoherentGetHeaderField(int)
{
    return nullptr;
}

extern "C" void __fastcall CoherentDecodeURLString(const char*, char* output, unsigned int* outputLength)
{
    if (!outputLength)
        return;

    if (output && *outputLength)
        output[0] = '\0';
    *outputLength = 0;
}

extern "C" std::uintptr_t __fastcall CoherentSystemNoOp()
{
    return 0;
}

extern "C" void* __fastcall CoherentInitializeUIGTSystem()
{
    static std::uintptr_t vtable[256]{};
    static void* vtablePointer = vtable;
    static std::once_flag initOnce;
    std::call_once(initOnce, [] {
        for (std::uintptr_t& slot : vtable)
            slot = reinterpret_cast<std::uintptr_t>(&CoherentSystemNoOp);
    });
    return &vtablePointer;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}
