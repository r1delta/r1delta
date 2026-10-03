// Command r1delta_tailcat builds (with -buildmode=c-shared) the tailcat
// overlay transport plugin for R1Delta. It implements the C ABI described in
// ../r1p_plugin.h on top of github.com/tailscale/tailcat: WireGuard tunnels
// over Tailscale's data plane (magicsock NAT traversal + DERP relays) with no
// Tailscale account or control plane.
//
// The game server listens with a tailcat Server that only admits UDP to the
// game port; clients dial it with a tailcat Client and exchange datagrams over
// a connected UDP flow inside the tunnel.
package main

/*
#include <stdint.h>
*/
import "C"

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
	"unsafe"

	"github.com/tailscale/tailcat"
	"tailscale.com/types/key"
	"tailscale.com/wgengine/filter"
)

const (
	abiVersion = 1

	rOK       = 0
	rGot      = 1
	rErrArg   = -1
	rErrState = -2
	rErrNF    = -3
	rErrAgain = -4
	rErrBig   = -5
	rErrBuf   = -6
	rErrInt   = -7

	flagIncoming = 0x1
	flagClosed   = 0x2

	maxQueued      = 8192
	maxLogLines    = 256
	connectTimeout = 20 * time.Second
)

type config struct {
	KeyFile  string `json:"key_file"`
	RelayURL string `json:"relay_url"` // alternate DERP map URL
	Verbose  bool   `json:"verbose"`
}

type peer struct {
	incoming bool
	conn     tailcat.ConnPacketConn // nil until connected
	client   *tailcat.Client        // outgoing peers only
	state    string
	err      string
	path     string
	rttMs    float64
}

type packet struct {
	peer  uint64
	flags uint32
	data  []byte
}

var (
	mu       sync.Mutex
	inited   bool
	cfg      config
	peers    = map[uint64]*peer{}
	nextID   uint64 = 1
	queue    []packet
	server   *tailcat.Server
	logLines []string
)

func logf(format string, args ...any) {
	line := fmt.Sprintf(format, args...)
	mu.Lock()
	if len(logLines) >= maxLogLines {
		logLines = logLines[1:]
	}
	logLines = append(logLines, strings.TrimRight(line, "\n"))
	mu.Unlock()
}

// libLogf receives tailcat/magicsock logs, which are very chatty; they are
// only forwarded when the game asked for verbose logging.
func libLogf(format string, args ...any) {
	mu.Lock()
	verbose := cfg.Verbose
	mu.Unlock()
	if verbose {
		logf("tailcat: "+format, args...)
	}
}

func pushPacket(id uint64, flags uint32, data []byte) {
	mu.Lock()
	if len(queue) >= maxQueued {
		queue = queue[1:]
	}
	queue = append(queue, packet{peer: id, flags: flags, data: data})
	mu.Unlock()
}

func addPeer(p *peer) uint64 {
	mu.Lock()
	defer mu.Unlock()
	id := nextID
	nextID++
	peers[id] = p
	return id
}

func readLoop(id uint64, c tailcat.ConnPacketConn, base uint32) {
	buf := make([]byte, 65536)
	for {
		n, err := c.Read(buf)
		if err != nil {
			mu.Lock()
			if p, ok := peers[id]; ok {
				p.state = "closed"
				p.err = err.Error()
			}
			mu.Unlock()
			pushPacket(id, base|flagClosed, nil)
			return
		}
		pushPacket(id, base, append([]byte(nil), buf[:n]...))
	}
}

func goString(p *C.char) string {
	if p == nil {
		return ""
	}
	return C.GoString(p)
}

func writeCString(s string, out *C.char, capacity C.uint32_t) C.int32_t {
	if out == nil || uint32(len(s))+1 > uint32(capacity) {
		return rErrBuf
	}
	dst := unsafe.Slice((*byte)(unsafe.Pointer(out)), int(capacity))
	copy(dst, s)
	dst[len(s)] = 0
	return rOK
}

// persistedIdentity is the on-disk form of a server identity, so that a
// server keeps the same tailcat address across restarts.
type persistedIdentity struct {
	Key          key.NodePrivate      `json:"key"`
	PresharedKey tailcat.PresharedKey `json:"psk"`
}

func loadOrCreateIdentity(path string) (persistedIdentity, error) {
	var id persistedIdentity
	if b, err := os.ReadFile(path); err == nil {
		if err := json.Unmarshal(b, &id); err == nil && !id.Key.IsZero() && !id.PresharedKey.IsZero() {
			return id, nil
		}
		logf("tailcat: ignoring malformed identity file %s", path)
	}
	id.Key = key.NewNode()
	id.PresharedKey = tailcat.NewPresharedKey()
	b, err := json.Marshal(id)
	if err != nil {
		return id, err
	}
	os.MkdirAll(filepath.Dir(path), 0o700)
	if err := os.WriteFile(path, b, 0o600); err != nil {
		logf("tailcat: could not persist identity to %s: %v", path, err)
	}
	return id, nil
}

//export r1p_abi_version
func r1p_abi_version() C.int32_t { return abiVersion }

//export r1p_init
func r1p_init(configJSON *C.char) C.int32_t {
	mu.Lock()
	defer mu.Unlock()
	if inited {
		return rOK
	}
	if s := goString(configJSON); s != "" {
		if err := json.Unmarshal([]byte(s), &cfg); err != nil {
			return rErrArg
		}
	}
	inited = true
	return rOK
}

//export r1p_listen
func r1p_listen(port C.uint16_t, outAddr *C.char, capacity C.uint32_t) C.int32_t {
	mu.Lock()
	if !inited {
		mu.Unlock()
		return rErrState
	}
	if server != nil {
		addr := string(server.TailcatAddr())
		mu.Unlock()
		return writeCString(addr, outAddr, capacity)
	}
	c := cfg
	mu.Unlock()

	gamePort := uint16(port)
	s := &tailcat.Server{
		Logf:       libLogf,
		DERPMapURL: c.RelayURL,
		// Only the game port is reachable through the tunnel.
		ServedTCPPorts: []filter.PortRange{},
		ServedUDPPorts: []filter.PortRange{{First: gamePort, Last: gamePort}},
	}
	if c.KeyFile != "" {
		if id, err := loadOrCreateIdentity(c.KeyFile); err == nil {
			s.Key = id.Key
			s.PresharedKey = id.PresharedKey
		}
	}
	s.OnUDP = func(p uint16) func(tailcat.ConnPacketConn) {
		if p != gamePort {
			return nil
		}
		return func(conn tailcat.ConnPacketConn) {
			id := addPeer(&peer{incoming: true, conn: conn, state: "connected", path: "unknown", rttMs: -1})
			logf("tailcat: accepted flow from %v as peer %d", conn.RemoteAddr(), id)
			readLoop(id, conn, flagIncoming)
		}
	}
	if err := s.Start(); err != nil {
		logf("tailcat: server start failed: %v", err)
		return rErrInt
	}
	mu.Lock()
	server = s
	mu.Unlock()
	addr := string(s.TailcatAddr())
	logf("tailcat: listening on game port %d", gamePort)
	return writeCString(addr, outAddr, capacity)
}

// discoLoop nudges magicsock towards a direct path and records how the
// tunnel is currently routed.
func discoLoop(id uint64, c *tailcat.Client) {
	for i := 0; i < 12; i++ {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		res, err := c.DiscoPing(ctx)
		cancel()
		mu.Lock()
		p, ok := peers[id]
		if !ok || p.state != "connected" {
			mu.Unlock()
			return
		}
		if err == nil && res != nil {
			if res.Endpoint != "" {
				p.path = "direct"
			} else if res.DERPRegionID != 0 {
				p.path = "relay"
			}
			p.rttMs = res.LatencySeconds * 1000
		}
		direct := p.path == "direct"
		mu.Unlock()
		if direct && i >= 2 {
			return
		}
		time.Sleep(2 * time.Second)
	}
}

//export r1p_connect
func r1p_connect(remote *C.char, port C.uint16_t, outPeer *C.uint64_t) C.int32_t {
	addr := strings.TrimSpace(goString(remote))
	if addr == "" || outPeer == nil {
		return rErrArg
	}
	mu.Lock()
	if !inited {
		mu.Unlock()
		return rErrState
	}
	c := cfg
	mu.Unlock()

	client := &tailcat.Client{Server: tailcat.Addr(addr), Logf: libLogf, DERPMapURL: c.RelayURL}
	id := addPeer(&peer{client: client, state: "connecting", path: "unknown", rttMs: -1})
	*outPeer = C.uint64_t(id)

	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), connectTimeout)
		defer cancel()
		conn, err := client.DialUDPPort(ctx, uint16(port))
		mu.Lock()
		p, ok := peers[id]
		if !ok {
			mu.Unlock()
			if conn != nil {
				conn.Close()
			}
			client.Close()
			return
		}
		if err != nil {
			p.state = "failed"
			p.err = err.Error()
			mu.Unlock()
			logf("tailcat: connect failed: %v", err)
			client.Close()
			return
		}
		p.conn = conn
		p.state = "connected"
		mu.Unlock()
		logf("tailcat: connected as peer %d", id)
		go discoLoop(id, client)
		readLoop(id, conn, 0)
	}()
	return rOK
}

//export r1p_send
func r1p_send(id C.uint64_t, data *C.uint8_t, length C.uint32_t) C.int32_t {
	if data == nil && length > 0 {
		return rErrArg
	}
	if int(length) > tailcat.MaxUDPPayload {
		return rErrBig
	}
	mu.Lock()
	p, ok := peers[uint64(id)]
	var conn tailcat.ConnPacketConn
	if ok && p.state == "connected" {
		conn = p.conn
	}
	mu.Unlock()
	if !ok {
		return rErrNF
	}
	if conn == nil {
		return rErrAgain
	}
	buf := C.GoBytes(unsafe.Pointer(data), C.int(length))
	if _, err := conn.Write(buf); err != nil {
		return rErrAgain
	}
	return rOK
}

//export r1p_recv
func r1p_recv(outPeer *C.uint64_t, outFlags *C.uint32_t, buf *C.uint8_t, capacity C.uint32_t, outLen *C.uint32_t) C.int32_t {
	if outPeer == nil || outFlags == nil || outLen == nil || (buf == nil && capacity > 0) {
		return rErrArg
	}
	mu.Lock()
	if len(queue) == 0 {
		mu.Unlock()
		return rOK
	}
	pkt := queue[0]
	if uint32(len(pkt.data)) > uint32(capacity) {
		mu.Unlock()
		*outLen = C.uint32_t(len(pkt.data))
		return rErrBuf
	}
	queue[0] = packet{}
	queue = queue[1:]
	if pkt.flags&flagClosed != 0 {
		delete(peers, pkt.peer)
	}
	mu.Unlock()

	*outPeer = C.uint64_t(pkt.peer)
	*outFlags = C.uint32_t(pkt.flags)
	*outLen = C.uint32_t(len(pkt.data))
	if len(pkt.data) > 0 {
		copy(unsafe.Slice((*byte)(unsafe.Pointer(buf)), len(pkt.data)), pkt.data)
	}
	return rGot
}

//export r1p_peer_status
func r1p_peer_status(id C.uint64_t, out *C.char, capacity C.uint32_t) C.int32_t {
	mu.Lock()
	p, ok := peers[uint64(id)]
	var status map[string]any
	if ok {
		status = map[string]any{"state": p.state, "path": p.path, "rtt_ms": p.rttMs, "error": p.err}
	}
	mu.Unlock()
	if !ok {
		return rErrNF
	}
	b, _ := json.Marshal(status)
	return writeCString(string(b), out, capacity)
}

//export r1p_max_datagram
func r1p_max_datagram(id C.uint64_t) C.int32_t {
	mu.Lock()
	_, ok := peers[uint64(id)]
	mu.Unlock()
	if !ok {
		return rErrNF
	}
	return C.int32_t(tailcat.MaxUDPPayload)
}

//export r1p_close_peer
func r1p_close_peer(id C.uint64_t) C.int32_t {
	mu.Lock()
	p, ok := peers[uint64(id)]
	delete(peers, uint64(id))
	mu.Unlock()
	if !ok {
		return rErrNF
	}
	closePeer(p)
	return rOK
}

func closePeer(p *peer) {
	if p.conn != nil {
		p.conn.Close()
	}
	if p.client != nil {
		p.client.Close()
	}
}

//export r1p_poll_log
func r1p_poll_log(out *C.char, capacity C.uint32_t) C.int32_t {
	mu.Lock()
	if len(logLines) == 0 {
		mu.Unlock()
		return rOK
	}
	line := logLines[0]
	logLines = logLines[1:]
	mu.Unlock()
	if capacity == 0 {
		return rErrBuf
	}
	if len(line)+1 > int(capacity) {
		line = line[:int(capacity)-1]
	}
	if writeCString(line, out, capacity) != rOK {
		return rErrBuf
	}
	return rGot
}

//export r1p_shutdown
func r1p_shutdown() {
	mu.Lock()
	all := make([]*peer, 0, len(peers))
	for id, p := range peers {
		all = append(all, p)
		delete(peers, id)
	}
	s := server
	server = nil
	mu.Unlock()
	for _, p := range all {
		closePeer(p)
	}
	if s != nil {
		s.Close()
	}
}


func main() {}
