#if defined(__WINE__)
#define DLLEXPORT __attribute__((visibility("default")))
#elif defined(_MSC_VER)
#define DLLEXPORT
#else
#define DLLEXPORT __declspec(dllexport)
#endif
