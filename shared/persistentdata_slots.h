#pragma once

namespace PersistentDataSlots {

constexpr int kMaximumSupportedClients = 64;
constexpr int kReplayPlayerSlot = 18;

constexpr bool IsValidPlayerSlot(int playerSlot, int maxClients)
{
	return maxClients > 0
		&& maxClients <= kMaximumSupportedClients
		&& playerSlot >= 0
		&& playerSlot < maxClients;
}

constexpr bool IsReplayPlayerSlot(int playerSlot)
{
	return playerSlot == kReplayPlayerSlot;
}

}
