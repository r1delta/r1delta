// Test stub: r1o_vpk.cpp reports to the R2 provider's filesystem trace. The test never enables tracing.
#include "../engine/filesystem/r2port_provider.h"

namespace r2port {
bool FilesystemTraceEnabled() { return false; }
bool FilesystemTracePathMatches(const char*) { return false; }
void TraceFilesystemCall(const char*, const char*, const char*, int64_t, const char*, const char*) {}
} // namespace r2port
