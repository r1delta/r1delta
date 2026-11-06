#pragma once

#include <cstddef>
#include <cstdint>

// Retail R1 client.dll (TimeDateStamp 0x55038DB8).
//
// CHudDamageIndicator (the panel inside the "vgui_damage_3d" client-side VGUI screen that
// cl_player.nut Create_DamageIndicatorHUD builds in Player_AddClient) registers itself in a
// per-split-screen-slot global, g_pDamageIndicator[slot] at client.dll+0x16FF0C0: the
// constructor (client.dll+0x41A340) stores `this` there and the slot index at this+0x2F0. The
// script natives AddGrenadeIndicator (client.dll+0x41ABF0) and the damage-indicator adders
// (client.dll+0x41A860, +0x41AB90) append to the panel found in that global.
//
// The destructor (client.dll+0x41A550) clears the global only when
// engine->IsLocalPlayerResolvable() is true. At level shutdown the screen is destroyed while the
// local player is not resolvable, so the global keeps pointing at the freed panel until the next
// map's Player_AddClient builds a new one, which happens after SIGNONSTATE_FULL and after the
// first snapshot has created entities. A frag grenade created in that window (or a client whose
// player is added late) appends through the freed panel: crash client.dll+0x41A966 with a
// garbage element count and pointer.
namespace r1delta::hud_damage_indicator
{
inline constexpr std::uintptr_t kDestructorRva = 0x41A550;
inline constexpr std::uintptr_t kSlotTableRva = 0x16FF0C0;
inline constexpr std::size_t kSlotOffset = 0x2F0;

inline constexpr std::uint8_t kExpectedDestructorPrologue[] = {
	0x40, 0x53,                               // push rbx
	0x48, 0x83, 0xEC, 0x20,                   // sub rsp, 20h
	0x48, 0x8D, 0x05, 0x4B, 0x9C, 0x4F, 0x00, // lea rax, CHudDamageIndicator::vftable
	0x48, 0x8B, 0xD9,                         // mov rbx, rcx
	0x48, 0x89, 0x01,                         // mov [rcx], rax
};

// Releases the slot a destroyed indicator registered itself in, whatever the engine's local
// player state is. The slot index is the one the constructor stored and used to index the table,
// so it is in range for any indicator the constructor finished. A slot that already holds a newer
// indicator is left alone.
inline void ReleaseSlot(void** slotTable, const void* indicator)
{
	const int slot = *reinterpret_cast<const int*>(static_cast<const std::uint8_t*>(indicator) + kSlotOffset);
	if (slot >= 0 && slotTable[slot] == indicator)
		slotTable[slot] = nullptr;
}
}
