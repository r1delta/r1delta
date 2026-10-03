#pragma once

namespace p2p
{

// Console logging with a "[P2P] " prefix. Safe from any thread.
void Log(const char* fmt, ...);
// Only printed when delta_p2p_debug is non-zero.
void Debug(const char* fmt, ...);

} // namespace p2p
