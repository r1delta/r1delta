#pragma once
void InstallExceptionHandler();
void InstallRetailMinidumpCallbacks();

// Writes the crash report for a fatal condition that raises no exception and terminates the process. The retail
// tier0 Error() ends the process directly, so without this the engine's own fatal errors (and the R1Delta invariant
// checks that end in Error()) exit with no report at all.
[[noreturn]] void ReportFatalConditionAndTerminate(const char* detail);
