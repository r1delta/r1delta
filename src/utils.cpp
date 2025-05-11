#include "core.h"
#include "utils.h"

constexpr size_t EXEC_MEM_SIZE = 32 * 1152;
static_assert(EXEC_MEM_SIZE % 4096 == 0, "EXEC_MEM_SIZE is not page granular");
void* execMem = 0;
uint8_t* execCursor;

void UpdateRWXFunction(void* rwxfunc, void* real) {
	if (rwxfunc < execMem || rwxfunc >= execCursor)
		return;

	*(void**)((uint8_t*)rwxfunc + 2) = real;
}

uintptr_t CreateFunction(void* func, void* real) {
	if (!execMem) {
		// allocate executable memory.
		// NOTE(mrsteyk): this only allocates in page granularity.
		execMem = VirtualAlloc(NULL, EXEC_MEM_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		execCursor = (uint8_t*)execMem;
	}
	if (!execMem) return 0;

	uintptr_t ret = (uintptr_t)InterlockedAdd64((volatile LONG64*)&execCursor, 32);
	uint8_t* bytes = (uint8_t*)ret;

	if (bytes >= ((uint8_t*)execMem + EXEC_MEM_SIZE)) {
		R1DAssert(!"Unreachable");
		return 0;
	}

	// mov rcx, real
	*bytes++ = 0x48;
	*bytes++ = 0xB9;
	*reinterpret_cast<uintptr_t*>(bytes) = (uintptr_t)real;
	bytes += sizeof(uintptr_t);

	// jmp func (RIP-relative indirect)
	*bytes++ = 0xFF;
	*bytes++ = 0x25;
	*reinterpret_cast<int32_t*>(bytes) = 0; // offset = 0 (next instruction)
	bytes += sizeof(int32_t);

	*reinterpret_cast<uintptr_t*>(bytes) = (uintptr_t)func;
	bytes += sizeof(uintptr_t);

	// return the function pointer.
	return ret;
}
