/*
 * R1Delta overlay transport plugin ABI (version 1).
 *
 * Overlay transports (iroh, tailcat) are shipped as optional DLLs next to
 * tier0.dll and loaded at runtime by p2p/p2p_plugin.cpp. Each DLL carries
 * unreliable datagrams between two game processes and exports exactly the
 * functions below. The game adds its own framing/fragmentation on top, so a
 * plugin only needs to move opaque datagrams of up to r1p_max_datagram()
 * bytes.
 *
 * All functions are thread safe and non-blocking unless stated otherwise.
 * Strings are UTF-8, NUL terminated. Return values >= 0 mean success.
 */
#ifndef R1P_PLUGIN_H
#define R1P_PLUGIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define R1P_ABI_VERSION 1

#define R1P_OK 0
#define R1P_GOT 1            /* r1p_recv / r1p_poll_log returned an item */
#define R1P_ERR_ARG (-1)     /* bad argument */
#define R1P_ERR_STATE (-2)   /* not initialised / already running */
#define R1P_ERR_NOTFOUND (-3)/* unknown peer */
#define R1P_ERR_AGAIN (-4)   /* peer not connected yet; datagram dropped */
#define R1P_ERR_TOOBIG (-5)  /* datagram larger than r1p_max_datagram */
#define R1P_ERR_BUFFER (-6)  /* output buffer too small */
#define R1P_ERR_INTERNAL (-7)

/* r1p_recv flags */
#define R1P_PEER_INCOMING 0x1u /* peer connected to us (server role) */
#define R1P_PEER_CLOSED 0x2u   /* peer went away; len is 0 */

/* Returns R1P_ABI_VERSION. */
int32_t r1p_abi_version(void);

/*
 * Initialises the plugin. config_json is an object with optional keys:
 *   "key_file":  path of a file holding the persistent node identity
 *   "relay_url": custom relay / DERP map URL
 * Blocks while the transport binds its sockets (usually < 1s).
 */
int32_t r1p_init(const char* config_json);

/*
 * Starts accepting incoming peers for the game port and writes the address
 * clients must pass to r1p_connect into out_addr. May block up to ~10s while
 * the transport connects to its relay.
 */
int32_t r1p_listen(uint16_t port, char* out_addr, uint32_t cap);

/*
 * Starts connecting to a remote address previously returned by r1p_listen on
 * the other side. Returns immediately with a peer handle; poll
 * r1p_peer_status to learn when it is usable.
 */
int32_t r1p_connect(const char* remote_addr, uint16_t port, uint64_t* out_peer);

/* Sends one datagram. */
int32_t r1p_send(uint64_t peer, const uint8_t* data, uint32_t len);

/* Pops one received datagram. Returns R1P_GOT, or R1P_OK when empty. */
int32_t r1p_recv(uint64_t* out_peer, uint32_t* out_flags, uint8_t* buf, uint32_t cap, uint32_t* out_len);

/*
 * Writes a JSON object describing the peer:
 *   {"state":"connecting|connected|failed|closed","path":"direct|relay|unknown",
 *    "rtt_ms":12.5,"error":"..."}
 */
int32_t r1p_peer_status(uint64_t peer, char* out_json, uint32_t cap);

/* Largest datagram r1p_send accepts for this peer right now. */
int32_t r1p_max_datagram(uint64_t peer);

int32_t r1p_close_peer(uint64_t peer);

/* Pops one log line for the game console. Returns R1P_GOT or R1P_OK. */
int32_t r1p_poll_log(char* out, uint32_t cap);

void r1p_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* R1P_PLUGIN_H */
