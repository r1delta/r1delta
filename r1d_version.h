#pragma once

// Connection/master-server compatibility identifier. Release CI replaces this
// with the Git tag (for example, "v2.13.1"). Keep it machine-readable: it is
// sent through delta_version and compared by the connection compatibility code.
#ifndef R1D_VERSION
#define R1D_VERSION "dev"
#endif // !R1D_VERSION

// Human-readable product label. Do not send this through delta_version or the
// master-server compatibility field.
#ifndef R1D_DISPLAY_VERSION
#define R1D_DISPLAY_VERSION "3.0 prerelease 25"
#endif // !R1D_DISPLAY_VERSION

#ifndef R1D_MINIMUM_VERSION
#define R1D_MINIMUM_VERSION "v2.4.7"
#endif // !R1D_MINIMUM_VERSION
