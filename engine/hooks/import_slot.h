// Rewrites a loaded module's import address table slots by the address they are bound to.
#pragma once

#include <cstddef>
#include <cstdint>

// Points every import address table slot of the module at `moduleBase` that is bound to `boundTarget` at `replacement`
// and returns how many slots were rewritten. Matching on the bound address covers imports by name and by ordinal and
// forwarded exports alike. Call it only after the module's imports are bound (not from its load notification).
size_t ReplaceBoundImport(std::uintptr_t moduleBase, const void* boundTarget, const void* replacement);
