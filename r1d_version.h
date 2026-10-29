#pragma once

// Connection/master-server compatibility identifier. Release CI replaces this
// with the Git tag (for example, "v2.13.1"). Keep it machine-readable: it is
// sent through delta_version and compared by the connection compatibility code.
#ifndef R1D_VERSION
#define R1D_VERSION "dev"
#endif // !R1D_VERSION

// Human-readable product label. Release CI replaces this with the workflow
// DISPLAY_VERSION pin (for example, "v3.11 for Workgroups"). Do not send this
// through delta_version or the master-server compatibility field.
#ifndef R1D_DISPLAY_VERSION
#define R1D_DISPLAY_VERSION "dev"
#endif // !R1D_DISPLAY_VERSION

// Oldest server version the server browser lists. R1Delta 3.0 is not network
// compatible with 2.x (the connect handshake requires matching major.minor).
#ifndef R1D_MINIMUM_VERSION
#define R1D_MINIMUM_VERSION "v3.0.0"
#endif // !R1D_MINIMUM_VERSION

// Source revision the binary was built from. Release and CI builds stamp the GitHub SHA so a
// crash report identifies its exact source tree; local builds leave it empty.
#ifndef R1D_COMMIT
#define R1D_COMMIT ""
#endif // !R1D_COMMIT
