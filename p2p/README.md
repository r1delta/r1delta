# NAT traversal and alternative transports (`p2p/`)

Servers used to be joinable only when their UDP port was reachable from the
internet (or by manually sharing an EOS fake IP). This module lets a server
behind NAT be joined anyway, and lets clients pick the fastest working route.

## Routes

| Route       | How it works | Needs |
|-------------|--------------|-------|
| `direct`    | Plain UDP to the listed `ip:port` (as before). | Reachable server, or one of the tricks below |
| UPnP / NAT-PMP | Server asks its router to forward `hostport` (IGD `AddPortMapping`, NAT-PMP fallback, lease renewed). Makes `direct` work. | Router with UPnP or NAT-PMP |
| `punch`     | UDP hole punching through the master server rendezvous: the server keeps a NAT mapping open from its game socket, the client registers its own mapping, the master tells the server to punch towards the client (with small port-prediction spray). | Non-symmetric NAT on at least one side |
| `lan`       | Server's private addresses, handed only to clients behind the same public IP. | Same LAN |
| `tailscale` | Server's `100.64.0.0/10` address on a Tailscale adapter; tried when the client also runs Tailscale. | Both on the same tailnet |
| `eos`       | Existing EOS P2P fake-IP transport (EOS does its own NAT punching; relays stay disabled). The server now logs into EOS and advertises its ProductUserId. | EOS |
| `iroh`      | [iroh](https://iroh.computer) QUIC datagrams: hole punching + n0 relays as fallback. `r1delta_iroh.dll` (Rust). | Plugin DLL |
| `tailcat`   | [tailcat](https://github.com/tailscale/tailcat): WireGuard over Tailscale's data plane (magicsock + DERP), no account or tailnet. `r1delta_tailcat.dll` (Go). | Plugin DLL |
| `turn`      | Cloudflare TURN relay. Only servers the master could **not** reach directly get credentials; they allocate a relay address and clients simply send normal UDP to it. | Master configured with Cloudflare TURN keys |

## Choosing a route (`delta_connect`)

The server browser now runs `delta_connect ip:port` instead of `connect`:

1. `POST /nat/connect` to the master: fresh transports, a punch ticket, the
   server's rendezvous mapping and (same public IP only) its LAN addresses.
   Servers that do not run this code (`"p2p": false`), unlisted servers and an
   unreachable master all fall back to a plain `connect` immediately.
2. The client registers its game socket with the rendezvous (`CLI_REGISTER`),
   which makes the master ask the server to hole-punch towards it and to open
   a TURN permission for it.
3. Every candidate route is probed in parallel with `PING` control packets
   sent *through that route*; the server answers `PONG` over the same route
   from the engine's normal socket polling, so all routes pay the same server
   frame latency.
4. After the first answer, slower routes get 300 ms of grace (the whole
   `delta_p2p_connect_timeout_ms` if nothing answered). The winner is the lowest
   `rtt + relay penalty (delta_p2p_relay_penalty_ms, default 20) + tie-break`
   (≤1 ms, favours plain UDP over overlays). `delta_p2p_prefer <route>` forces a
   route when it answers.
5. `connect <address>` is issued with the winner. Overlay routes use fake
   addresses (`[3ffd:BB::id]:port`, see below); everything else is a real IPv4.

`delta_p2p_status` prints the server side services and the last probe table,
e.g.:

```
Route probe for 203.0.113.7:37015 (325 ms):
  * direct     203.0.113.7:37015            41.5 ms
    iroh       15f2dee61d...                41.9 ms
    tailcat    tcpGFwWCBZXE...              no answer
    turn       104.30.1.2:49160             52.3 ms (relayed)
  -> connecting via direct (203.0.113.7:37015)
```

The direct connect dialog also accepts a tailcat address (`tc...`) or an iroh
endpoint id (64 hex chars): `delta_connect_tailcat` / `delta_connect_iroh`.

## How it plugs into the engine

Everything rides on the existing Winsock hooks in `eos/eos_network.cpp`
(`sendto` / `recvfrom` / `closesocket`), which are now installed on every
build:

* `sendto` to `3ffd::/16` → `p2p_mux` → overlay backend (iroh, tailcat, TURN).
  `3ffe::/16` keeps going to EOS unchanged.
* `recvfrom` on the engine's client/server socket first returns datagrams
  queued by the mux (overlay traffic, packets read while probing), then EOS,
  then the real socket. `R1NX` control packets are handled and never reach the
  engine.
* Calls coming from the EOS SDK or a plugin DLL (checked by return address)
  bypass the hooks, so those sockets never receive engine datagrams.

Fake addresses: `3ffd:00BB:0000:0000:<64-bit peer id>` where `BB` is 1 = iroh,
2 = tailcat, 3 = TURN. Overlay datagrams carry a 1-byte frame header and are
fragmented when larger than the transport MTU (tailcat: 1232 bytes).

Plugins implement the small C ABI in `plugins/r1p_plugin.h` and are loaded
from next to `tier0.dll`; if a DLL is missing that route is simply not offered.

## Control packets (`R1NX`)

UDP payloads starting `FF FF FF FF 'R' '1' 'N' 'X' <version=1> <type>`, big
endian. Shared with the master server (`masterserver2/nat.go`).

| Type | Direction | Payload |
|------|-----------|---------|
| 0x01 `SRV_REGISTER` | server → rendezvous | token[16] (from the heartbeat reply) |
| 0x02 `REGISTER_ACK` | rendezvous → peer | observed ip[4] port[2], flags[1], server ip[4] port[2] if flags&1 |
| 0x03 `CLI_REGISTER` | client → rendezvous | ticket[16] (from `/nat/connect`) |
| 0x04 `PUNCH_REQUEST` | rendezvous → server | ticket[16], client ip[4] port[2] (port 0: TURN permission only) |
| 0x05 `PUNCH` | peer ↔ peer | ticket[16], role[1] |
| 0x06 `PING` | client → server, any route | probe id[8], timestamp µs[8] |
| 0x07 `PONG` | server → client, same route | probe id[8], timestamp[8], flags[1] |
| 0x08 `PUNCH_REQ_ACK` | server → rendezvous | ticket[16], client ip[4] port[2] |

The server only acts on `PUNCH_REQUEST` coming from the rendezvous address,
and the rendezvous only accepts a client registration from the same public IP
that requested the ticket, so nobody can aim a server's punches at a third
party.

## Master server

See `masterserver2/NAT.md`. Summary: heartbeats may carry `transports` and
`nat`; the reply is now a JSON object with the rendezvous address, a token,
the reachability result and (for unreachable servers) Cloudflare TURN
credentials. Validation tries direct UDP, then the rendezvous mapping, then
the TURN relay; a server is listed if any path answers.

## Convars

Server: `delta_p2p_server_upnp`, `delta_p2p_server_punch`,
`delta_p2p_server_eos`, `delta_p2p_server_iroh`, `delta_p2p_server_tailcat`,
`delta_p2p_server_turn`, `delta_p2p_punch_spray` (all default on / 2).

Client: `delta_p2p_connect_timeout_ms` (2500), `delta_p2p_relay_penalty_ms`
(20), `delta_p2p_prefer` (empty).

Both: `delta_p2p_enable` (1), `delta_p2p_iroh_relay_url`,
`delta_p2p_tailcat_derpmap_url`, `delta_p2p_debug`.

Server identities for iroh/tailcat are persisted under
`%LOCALAPPDATA%\R1Delta\p2p\` so their addresses survive restarts.

## Testing

* `tests/p2p_codec_tests.cpp` (runs in CI): STUN/TURN codec incl. RFC 5769
  vectors, hashes, control packets, fragmentation, UPnP/NAT-PMP parsing.
* `plugins/test/run_loopback.sh <plugin.so>`: two processes exchanging
  datagrams through a plugin via its C ABI (Linux).
* `devtest/run_e2e.sh <masterserver2 dir>`: the real p2p sources under a
  POSIX shim with a fake engine, against the real master server, coturn, a
  local DERP relay and both plugins. The fake server sits behind an emulated
  port-restricted NAT (so `direct` only works after hole punching) and clients
  force each route in turn while exchanging fragmented game datagrams. A
  legacy (non-p2p) server checks the immediate plain-connect fallback.

Not covered by those: the Windows-only pieces (Winsock hook plumbing inside
the real engine, UPnP/NAT-PMP against a real router, adapter enumeration, EOS
login), which need an in-game test.

## Known limitations

* TURN credentials (12 h TTL) are rotated by the master; the live allocation
  keeps its original username until it fails, then re-allocates with the new
  credentials, which changes the relay address and drops clients currently
  relayed through it.
* Symmetric-NAT-to-symmetric-NAT pairs only work through relays (iroh, tailcat,
  TURN); port prediction is best effort.
* The engine client socket is IPv4, so Tailscale IPv6 and global IPv6 routes
  are not used.
* iroh's and tailcat's public relays are free but rate limited; heavy use
  should run our own (`delta_p2p_iroh_relay_url`,
  `delta_p2p_tailcat_derpmap_url`).
