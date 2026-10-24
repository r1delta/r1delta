#pragma once

#include "vpk_async_precache_entry.h"

namespace r1delta::shared_particle_materials
{
// Exact full-path lookup for the two canonical shared particle materials.
// Accepts ASCII case differences and either slash; leaves result unchanged on
// a miss. Successful results borrow process-lifetime storage, never ownership.
// Only the verified Client2015 raw-text cache consumer may receive these nodes;
// it locks the node and uses a read-only external CUtlBuffer without freeing it.
bool LookupResource(
	const char* path,
	vpk_async_precache::ResourceLookup& result) noexcept;
}
