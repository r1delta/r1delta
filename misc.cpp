// Miscellaneous hooks for R1Delta
// Contains various small hooks that don't fit into other categories

#include "misc.h"
#include "core.h"
#include "load.h"
#include "logging.h"
#include "cvar.h"

#include <iostream>
#include <cstdarg>
#include <ctime>

// VTable manipulation
BOOL RemoveItemsFromVTable(uintptr_t vTableAddr, size_t indexStart, size_t itemCount) {
    if (vTableAddr == 0) return FALSE;

    uintptr_t* vTable = reinterpret_cast<uintptr_t*>(vTableAddr);
    size_t vTableSize = 77;

    if (indexStart + itemCount > vTableSize) return FALSE;

    size_t elementsToShift = vTableSize - (indexStart + itemCount);

    DWORD oldProtect;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(vTableAddr), vTableSize * sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        std::cerr << "Failed to change memory protection." << std::endl;
        return FALSE;
    }

    for (size_t i = 0; i < elementsToShift; ++i) {
        vTable[indexStart + i] = vTable[indexStart + itemCount + i];
    }

    memset(&vTable[vTableSize - itemCount], 0, itemCount * sizeof(uintptr_t));

    if (!VirtualProtect(reinterpret_cast<LPVOID>(vTableAddr), vTableSize * sizeof(uintptr_t), oldProtect, &oldProtect)) {
        std::cerr << "Failed to restore memory protection." << std::endl;
        return FALSE;
    }

    return TRUE;
}

// Get build number based on days since epoch
const char* GetBuildNo() {
    static char buffer[64] = {};

    if (buffer[0] == '\0') {
        std::tm epoch_tm = {};
        epoch_tm.tm_year = 2023 - 1900;
        epoch_tm.tm_mon = 11;  // December
        epoch_tm.tm_mday = 1;
        epoch_tm.tm_hour = 0;
        epoch_tm.tm_min = 0;
        epoch_tm.tm_sec = 0;
        std::time_t epoch_time = _mkgmtime(&epoch_tm);
        std::time_t current_time = std::time(nullptr);
        double seconds_since = std::difftime(current_time, epoch_time);
        int beat = static_cast<int>(seconds_since / 86400);
#ifdef _DEBUG
        std::snprintf(buffer, sizeof(buffer), "R1DMP_PC_BUILD_%d (DEBUG)", beat);
#else
        std::snprintf(buffer, sizeof(buffer), "R1DMP_PC_BUILD_%d", beat);
#endif
    }

    return buffer;
}

// Shutdown hook
__int64 __fastcall CServerGameDLL_DLLShutdown(__int64 a1, void*** a2, __int64 a3)
{
    Msg("Shutting down\n");
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
