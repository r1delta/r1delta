#include "vpk_async_precache_fix.h"

#include "engine/core/core.h"
#include "engine/logging/logging.h"

#include <Windows.h>

#include <cstdint>

namespace {

constexpr DWORD kExpectedTimeDateStamp = 0x54874230;
constexpr DWORD kExpectedSizeOfImage = 0x2116000;

bool IsExpectedR1ClientFileSystem(uintptr_t filesystemBase)
{
	if (!filesystemBase)
		return false;

	const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(filesystemBase);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
		return false;

	const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
		filesystemBase + static_cast<uintptr_t>(dos->e_lfanew));
	return nt->Signature == IMAGE_NT_SIGNATURE
		&& nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64
		&& nt->FileHeader.TimeDateStamp == kExpectedTimeDateStamp
		&& nt->OptionalHeader.SizeOfImage == kExpectedSizeOfImage;
}

bool s_AsyncPrecacheFixInstalled;

}

bool InstallR1ClientVPKAsyncPrecacheFix(uintptr_t filesystemBase)
{
	if (s_AsyncPrecacheFixInstalled)
		return true;
	if (!filesystemBase || IsDedicatedServer())
		return false;
	if (!IsExpectedR1ClientFileSystem(filesystemBase)) {
		Warning(
			"R1Delta: VPK async-precache fix skipped; filesystem_stdio.dll is not the expected R1 client image\n");
		return false;
	}

	// Do not replace the retail worker. The replacement's stale entrySlot path
	// can retain a datacache entry after the pack store changes ownership; the
	// resulting virtual call jumps into datacache RTTI (.rdata), producing the
	// observed DEP crash. The retail worker is the owner of its stack fallback
	// and task-lifetime invariants, so preserving it is safer than reconstructing
	// the function out of process-global tables.
	s_AsyncPrecacheFixInstalled = true;
	OutputDebugStringA(
		"R1Delta: R1 client VPK async-precache replacement disabled; using retail worker\n");
	return true;
}
