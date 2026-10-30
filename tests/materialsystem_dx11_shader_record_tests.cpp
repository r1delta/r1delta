#include "../engine/core/materialsystem_dx11_shader_record.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace
{
using namespace r1delta::materialsystem_dx11;

bool Check(bool condition, const char* message)
{
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}

using Record = std::array<std::uint8_t, 24>;

Record MakeRecord(std::uint32_t flags, std::uint64_t tableSlot)
{
	Record record{};
	const std::uint32_t key = 0x41000007u;
	std::memcpy(record.data(), &key, sizeof(key));
	std::memcpy(record.data() + kShaderRecordFlagsOffset, &flags, sizeof(flags));
	std::memcpy(record.data() + kShaderRecordTableOffset, &tableSlot, sizeof(tableSlot));
	return record;
}

std::uint64_t TableSlot(const Record& record)
{
	std::uint64_t value = 0;
	std::memcpy(&value, record.data() + kShaderRecordTableOffset, sizeof(value));
	return value;
}

std::uint32_t Flags(const Record& record)
{
	std::uint32_t value = 0;
	std::memcpy(&value, record.data() + kShaderRecordFlagsOffset, sizeof(value));
	return value;
}

std::size_t g_lastAllocationBytes = 0;
int g_allocations = 0;

void* AllocateForTest(std::size_t bytes)
{
	++g_allocations;
	g_lastAllocationBytes = bytes;
	void* const block = std::malloc(bytes);
	if (block)
		std::memset(block, 0xCD, bytes);   // slot 11 zeroes the table, the allocator does not
	return block;
}

void* AllocateFailsForTest(std::size_t)
{
	++g_allocations;
	return nullptr;
}

bool TestRetailConstants()
{
	return Check(kRetailTimeDateStamp == 0x54874238u, "unexpected retail timestamp")
		&& Check(kRetailImageSize == 0x3E0000u, "unexpected retail image size")
		&& Check(kMaterialShaderRecordOffset == 104, "record is CMaterial+104")
		&& Check(kShaderRecordFlagsOffset == 4, "flags are record+4")
		&& Check(kShaderRecordTableOffset == 16, "table slot is record+16")
		&& Check(kShaderRecordOwnsTableFlag == 0x80000000u, "ownership flag is bit 31")
		&& Check(kMaterialShaderReleaseRva == 0x383E0, "unexpected release RVA")
		&& Check(kUnlitGenericStateRva == 0x2F4F0, "unexpected UnlitGeneric state RVA")
		&& Check(kVertexLitGenericStateRva == 0x31190, "unexpected VertexLitGeneric state RVA")
		&& Check(kLightmappedGenericStateRva == 0x28FE0, "unexpected LightmappedGeneric state RVA")
		&& Check(kShaderRecordAllocateRva == 0x72B80, "unexpected allocator RVA")
		&& Check(kMaterialVtableRva == 0x184DC8, "unexpected CMaterial vtable RVA")
		&& Check(kUnlitGenericTableBytes == 32, "UnlitGeneric slot 9 allocates 32 bytes")
		&& Check(kVertexLitGenericTableBytes == 32, "VertexLitGeneric slot 9 allocates 32 bytes")
		&& Check(kLightmappedGenericTableBytes == 34, "LightmappedGeneric slot 9 allocates 34 bytes")
		&& Check(
			kExpectedUnlitGenericStatePrologue[18] == 0x49 && kExpectedUnlitGenericStatePrologue[21] == 0x10,
			"UnlitGeneric prologue must end with mov rbx,[r8+10h]");
}

bool TestInlineHandleBoundaries()
{
	return Check(!IsInlineTextureHandle(0), "null is not a handle (slot 9 allocates)")
		&& Check(IsInlineTextureHandle(1), "1 is a handle")
		&& Check(IsInlineTextureHandle(0xF87), "0xF87 from the mp_glitch crash is a handle")
		&& Check(IsInlineTextureHandle(0xFC5), "0xFC5 from the mp_glitch crash is a handle")
		&& Check(IsInlineTextureHandle(0xFFFF), "0xFFFF is the last handle value")
		&& Check(!IsInlineTextureHandle(0x10000), "0x10000 is the first pointer-sized value")
		&& Check(!IsInlineTextureHandle(0x000001A48DADEE18ull), "heap pointer is not a handle");
}

bool TestRepairReplacesInlineHandle()
{
	g_allocations = 0;
	Record record = MakeRecord(0x90000000u, 0xF87);
	std::uint64_t stale = 0;
	const ShaderRecordRepairResult result =
		RepairShaderRecordTable(record.data(), kUnlitGenericTableBytes, AllocateForTest, &stale);
	const std::uint64_t slot = TableSlot(record);
	const bool ok = Check(result == ShaderRecordRepairResult::repaired, "inline handle must be repaired")
		&& Check(stale == 0xF87, "stale handle must be reported")
		&& Check(g_allocations == 1 && g_lastAllocationBytes == 32, "one 32-byte allocation")
		&& Check(slot >= kMinimumUserPointer, "slot must now hold a pointer")
		&& Check(Flags(record) == 0x90000000u, "owned flag stays set")
		&& Check(*reinterpret_cast<const std::uint64_t*>(slot) == 0
			&& *reinterpret_cast<const std::uint64_t*>(slot + 24) == 0, "table must be zeroed");
	std::free(reinterpret_cast<void*>(slot));
	return ok;
}

bool TestRepairSetsOwnershipWhenFlagWasClear()
{
	Record record = MakeRecord(0x10000000u, 0xFC5);
	const ShaderRecordRepairResult result =
		RepairShaderRecordTable(record.data(), kLightmappedGenericTableBytes, AllocateForTest);
	const std::uint64_t slot = TableSlot(record);
	const bool ok = Check(result == ShaderRecordRepairResult::repaired, "repair expected")
		&& Check(g_lastAllocationBytes == 34, "34-byte allocation for LightmappedGeneric")
		&& Check(Flags(record) == 0x90000000u, "repair must mark the table as owned");
	std::free(reinterpret_cast<void*>(slot));
	return ok;
}

bool TestRepairLeavesValidSlotsAlone()
{
	g_allocations = 0;
	Record empty = MakeRecord(0x90000000u, 0);
	Record real = MakeRecord(0x90000000u, 0x000001A48DADEE18ull);
	const Record emptyBefore = empty;
	const Record realBefore = real;
	return Check(
			RepairShaderRecordTable(empty.data(), 32, AllocateForTest) == ShaderRecordRepairResult::intact,
			"null slot is slot 9's business")
		&& Check(
			RepairShaderRecordTable(real.data(), 32, AllocateForTest) == ShaderRecordRepairResult::intact,
			"a real pointer must not be replaced")
		&& Check(g_allocations == 0, "no allocation for valid slots")
		&& Check(empty == emptyBefore && real == realBefore, "valid records must not change")
		&& Check(
			RepairShaderRecordTable(nullptr, 32, AllocateForTest) == ShaderRecordRepairResult::intact,
			"null record");
}

bool TestRepairAllocationFailure()
{
	Record record = MakeRecord(0x90000000u, 0xF87);
	const Record before = record;
	std::uint64_t stale = 0;
	return Check(
			RepairShaderRecordTable(record.data(), 32, AllocateFailsForTest, &stale)
				== ShaderRecordRepairResult::allocationFailed,
			"allocation failure must be reported")
		&& Check(stale == 0xF87, "stale handle reported even when the allocation fails")
		&& Check(record == before, "record untouched on allocation failure");
}

bool TestClearUnownedSlot()
{
	Record inlineHandle = MakeRecord(0x10000000u, 0xF87);
	Record corruptedPointer = MakeRecord(0x00000001u, 0x000001A48DAD0F87ull);   // low word replaced by an inline handle
	Record owned = MakeRecord(0x90000000u, 0x000001A48DADEE18ull);
	Record empty = MakeRecord(0, 0);
	const Record ownedBefore = owned;
	const Record emptyBefore = empty;
	return Check(ClearUnownedShaderRecordTable(inlineHandle.data()), "inline handle must be cleared")
		&& Check(TableSlot(inlineHandle) == 0, "slot zero after clear")
		&& Check(ClearUnownedShaderRecordTable(corruptedPointer.data()), "unowned pointer-looking value must be cleared")
		&& Check(TableSlot(corruptedPointer) == 0, "corrupted slot zero after clear")
		&& Check(!ClearUnownedShaderRecordTable(owned.data()), "owned block must stay for the next table shader")
		&& Check(owned == ownedBefore, "owned record unchanged")
		&& Check(!ClearUnownedShaderRecordTable(empty.data()), "empty slot needs no write")
		&& Check(empty == emptyBefore, "empty record unchanged")
		&& Check(!ClearUnownedShaderRecordTable(nullptr), "null record");
}
}

int main()
{
	const bool ok = TestRetailConstants()
		&& TestInlineHandleBoundaries()
		&& TestRepairReplacesInlineHandle()
		&& TestRepairSetsOwnershipWhenFlagWasClear()
		&& TestRepairLeavesValidSlotsAlone()
		&& TestRepairAllocationFailure()
		&& TestClearUnownedSlot();
	if (!ok)
		return 1;
	std::cout << "materialsystem_dx11 shader record tests passed\n";
	return 0;
}
