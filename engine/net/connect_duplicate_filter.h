#pragma once

// A game client can put the same C2S_CONNECT datagram on the wire twice in one frame (observed on a plain LAN join: two datagrams from the
// same address, byte for byte equal, delivered by one NET_ProcessSocket loop with the same `received` stamp). The engine's connect path is
// not idempotent: the second copy finds the client the first one just created (same address), disconnects it ("Connection closing",
// OnClientDisconnected fires for a player that never spawned) and allocates a new user id. On the R1O server that doubled every
// connection's user id; a copy whose address does not match the new client's would instead be refused as "Player's account is already
// on the server".
//
// Only an exact repeat of the immediately preceding connect datagram (same source, same stamp, same bytes) is dropped. A different client
// connecting in the same frame, or the same client with a fresh challenge, is a different datagram and is processed.

#include <cstddef>
#include <cstdint>
#include <cstring>

class ConnectDuplicateFilter {
public:
	// `from` is the packet's netadr_t (kAddressBytes are compared), `received` its frame stamp, `data`/`length` the datagram as received.
	bool IsDuplicate(const void* from, double received, const void* data, size_t length)
	{
		const bool duplicate = m_valid
			&& received == m_received
			&& length == m_length
			&& std::memcmp(from, m_from, kAddressBytes) == 0
			&& std::memcmp(data, m_data, length) == 0;
		if (duplicate)
			return true;

		m_valid = length <= kMaxDatagramBytes;
		if (m_valid) {
			std::memcpy(m_from, from, kAddressBytes);
			m_received = received;
			m_length = length;
			std::memcpy(m_data, data, length);
		}
		return false;
	}

private:
	static constexpr size_t kAddressBytes = 24;     // sizeof(netadr_t): type, 16 byte address, port, two flags
	static constexpr size_t kMaxDatagramBytes = 1240;   // the connect packet buffer size the converters use

	bool m_valid = false;
	double m_received = 0.0;
	size_t m_length = 0;
	unsigned char m_from[kAddressBytes] = {};
	unsigned char m_data[kMaxDatagramBytes] = {};
};
