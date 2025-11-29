// %*++***###*##**##++**+++*++*%%%%%%%+*%+#*+%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#=%%%#**#+#%
// ==----------------------------------------------------------------------=================+
// =------------------------------------::----------------------------------===---==========+
// ---------------------------------:-:--::::-::::-------------------=======================+
// =-------------------------------::::::::-::::-:::----------==============+===+++=========+
// ----------------------------::::::--:---=====----------===========++==++++++++++++++++++++
// ----------------------------:-----:---==++++++====-==========++++++++++++++++++++++++++++*
// -------------------------------------=+++++++=============++++++++++++++++++++++++++++++**
// -------------------------------------=++++*+========++++++++++++++++++++++++++++++++++++**
// ----------------------------:::::::--=+++++=======+++++++++++++++++++++++************++++*
// ---------------------::::::::::::::::-==+++===++++++++++++++++++++++++********###%%%##*++*
// -------:::::::::::::::::::::::::::::::-=====+####**+++++++++++++++++*********#%%%@@@@%%#**
// ------:-:::::::::::::::::::::::::::::::-====*%%%%#*++++++++++++++++++********##%@@@@@%%#**
// ----------::::::::::::::::::::::::::-=--====+#%%%*++++++++++++++++++++*********##%%%%%#***
// -------------=*=-:::::::::::::::::-=++======++***+++++++++++++++++++**************###*****
// -------------=*#=-------======++++*###*+=+=++=++++++++++*+++******************************
// =-----=======+*#*+++++++*****##########+=++++++++++***************************************
// +++++++++++****#################*****#*+=+++++++++****************************************
// ++**+++++++++++++======+++++++++++++****+=+++***################**************************
// *****+=--------::-::::::::::::::::::------=*#%%%%%%%%%%%%%%%%%%%#####*********************
// ******=-----------:::::::::::---:::::::::-=#%%%%@@@@@@@@@@@@@@%%%%###********************#
// ******=---------------:::::::::::-:::::::-*%%%@@@@@@@@@@@@@@@@@%%%%##********************#
// ****#*=-----------------:::::::::::::::::-=*%%@@@@@@@@@@@@@@@@@@%%##*********************#
// ******+===-------------::::::::::::---:::--=*#%%%@@@@@@@@@@@@@%%######**#**************###
// ==++==------------------:::::::::::::-------=+**##%%%@%%%%%%%%##########*****************#
// ==--------------------------::-:::::::::::---=++**##%%%%%%%%%%%##########*************####
// =--------------------------------:---::::--:--==+**###%#%%%%%%%%%%%#####**************####
// ====--------------------------:-------::-------==+++****###########******************#####
// ===--==------------------------------------::---==+++++******************************#####
// ===-------------------------------------:::-:----=+++********************************####%
// =====---------------------------------------------=++++******************************####%
// ======------------------==------------------------==+++***************************######%%
// =========-----===--------==------------------------==++********#*#####**#######*########%%

#include "core.h"
#include "vtable_tools.h"

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

// Creates a callgate trampoline that checks the caller's return address to determine
// which vtable index to use. If the caller is within [dll_start, dll_end], uses orig_idx.
// Otherwise uses new_idx. This handles vtable mismatches where the object needs to be
// passed to multiple DLLs with different vtable layouts.
//
// Generated assembly:
//   mov rax, [rsp]          ; load return address
//   mov r10, dll_start
//   cmp rax, r10
//   jb originalFunction
//   mov r10, dll_end
//   cmp rax, r10
//   ja originalFunction
//   mov rax, newFunctionAddress
//   jmp rax
// originalFunction:
//   mov rax, originalFunctionAddress
//   jmp rax
uintptr_t CreateCallgate(void* vftable, uintptr_t dll_start, uintptr_t dll_end,
                         int orig_idx, int new_idx) {
	void* execMem_local = VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!execMem_local) return 0;

	uint8_t* bytes = static_cast<uint8_t*>(execMem_local);

	// Load the return address into rax
	*bytes++ = 0x48; *bytes++ = 0x8B; *bytes++ = 0x04; *bytes++ = 0x24; // mov rax, [rsp]

	// Move dll_start to r10 for comparison
	*bytes++ = 0x49; *bytes++ = 0xBA; // mov r10, dll_start
	*(uintptr_t*)bytes = dll_start;
	bytes += sizeof(uintptr_t);

	// cmp rax, r10
	*bytes++ = 0x4C; *bytes++ = 0x39; *bytes++ = 0xD0;

	// jb originalFunction (Offset will be filled later)
	*bytes++ = 0x0F; *bytes++ = 0x82;
	int32_t* jbOffsetLoc = reinterpret_cast<int32_t*>(bytes);
	bytes += sizeof(int32_t);

	// Move dll_end to r10 for comparison
	*bytes++ = 0x49; *bytes++ = 0xBA; // mov r10, dll_end
	*(uintptr_t*)bytes = dll_end;
	bytes += sizeof(uintptr_t);

	// cmp rax, r10
	*bytes++ = 0x4C; *bytes++ = 0x39; *bytes++ = 0xD0;

	// ja originalFunction (Offset will be filled later)
	*bytes++ = 0x0F; *bytes++ = 0x87;
	int32_t* jaOffsetLoc = reinterpret_cast<int32_t*>(bytes);
	bytes += sizeof(int32_t);

	// Redirect to new function (caller is within the specified DLL range)
	uintptr_t newFunctionAddress = ((uintptr_t*)vftable)[new_idx];
	*bytes++ = 0x48; *bytes++ = 0xB8; // mov rax, newFunctionAddress
	*(uintptr_t*)bytes = newFunctionAddress;
	bytes += sizeof(uintptr_t);
	*bytes++ = 0xFF; *bytes++ = 0xE0; // jmp rax

	uintptr_t originalFunctionLoc = reinterpret_cast<uintptr_t>(bytes);

	// Fill in the offsets for jb and ja
	*jbOffsetLoc = static_cast<int32_t>(originalFunctionLoc - (reinterpret_cast<uintptr_t>(jbOffsetLoc) + sizeof(int32_t)));
	*jaOffsetLoc = static_cast<int32_t>(originalFunctionLoc - (reinterpret_cast<uintptr_t>(jaOffsetLoc) + sizeof(int32_t)));

	// Original function pointer (caller is outside the specified DLL range)
	uintptr_t originalFunctionAddress = ((uintptr_t*)vftable)[orig_idx];

	// mov rax, originalFunctionAddress
	*bytes++ = 0x48; *bytes++ = 0xB8;
	*(uintptr_t*)bytes = originalFunctionAddress;
	bytes += sizeof(uintptr_t);

	// jmp rax
	*bytes++ = 0xFF; *bytes++ = 0xE0;

	return reinterpret_cast<uintptr_t>(execMem_local);
}
