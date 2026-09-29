//! iroh overlay transport plugin for R1Delta.
//!
//! Implements the C ABI described in `../r1p_plugin.h`: game datagrams are
//! carried as QUIC datagrams over an iroh connection, which hole-punches a
//! direct UDP path when possible and falls back to iroh's relay servers.

use std::collections::{HashMap, VecDeque};
use std::ffi::{c_char, CStr};
use std::net::SocketAddr;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::PathBuf;
use std::str::FromStr;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Mutex, OnceLock};
use std::time::Duration;

use bytes::Bytes;
use iroh::endpoint::{presets, Connection};
use iroh::{Endpoint, EndpointAddr, EndpointId, RelayMap, RelayMode, RelayUrl, SecretKey};
use serde::Deserialize;
use tokio::runtime::Runtime;

pub const ALPN: &[u8] = b"r1delta/game/1";

const ABI_VERSION: i32 = 1;
const OK: i32 = 0;
const GOT: i32 = 1;
const ERR_ARG: i32 = -1;
const ERR_STATE: i32 = -2;
const ERR_NOTFOUND: i32 = -3;
const ERR_AGAIN: i32 = -4;
const ERR_TOOBIG: i32 = -5;
const ERR_BUFFER: i32 = -6;
const ERR_INTERNAL: i32 = -7;

const FLAG_INCOMING: u32 = 0x1;
const FLAG_CLOSED: u32 = 0x2;

const MAX_QUEUED_PACKETS: usize = 8192;
const MAX_LOG_LINES: usize = 256;
const CONNECT_TIMEOUT: Duration = Duration::from_secs(15);
const ONLINE_TIMEOUT: Duration = Duration::from_secs(8);

#[derive(Deserialize, Default)]
struct Config {
    key_file: Option<String>,
    relay_url: Option<String>,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum PeerState {
    Connecting,
    Connected,
    Failed,
    Closed,
}

impl PeerState {
    fn as_str(self) -> &'static str {
        match self {
            PeerState::Connecting => "connecting",
            PeerState::Connected => "connected",
            PeerState::Failed => "failed",
            PeerState::Closed => "closed",
        }
    }
}

struct Peer {
    conn: Option<Connection>,
    state: PeerState,
    error: String,
}

struct Packet {
    peer: u64,
    flags: u32,
    data: Vec<u8>,
}

struct Plugin {
    rt: Runtime,
    endpoint: Endpoint,
    peers: Mutex<HashMap<u64, Peer>>,
    queue: Mutex<VecDeque<Packet>>,
    next_id: AtomicU64,
    listening: AtomicBool,
}

static PLUGIN: OnceLock<Plugin> = OnceLock::new();
static LOG: Mutex<VecDeque<String>> = Mutex::new(VecDeque::new());

fn log(msg: impl Into<String>) {
    if let Ok(mut q) = LOG.lock() {
        if q.len() >= MAX_LOG_LINES {
            q.pop_front();
        }
        q.push_back(msg.into());
    }
}

fn plugin() -> Option<&'static Plugin> {
    PLUGIN.get()
}

/// Runs an FFI body, converting panics into ERR_INTERNAL so they never unwind
/// into the game.
fn guard(f: impl FnOnce() -> i32) -> i32 {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(v) => v,
        Err(_) => {
            log("iroh: internal panic");
            ERR_INTERNAL
        }
    }
}

unsafe fn cstr<'a>(p: *const c_char) -> Option<&'a str> {
    if p.is_null() {
        return None;
    }
    CStr::from_ptr(p).to_str().ok()
}

/// Writes s + NUL into out (cap bytes). Returns the length or ERR_BUFFER.
unsafe fn write_cstr(s: &str, out: *mut c_char, cap: u32) -> i32 {
    if out.is_null() || (s.len() + 1) > cap as usize {
        return ERR_BUFFER;
    }
    std::ptr::copy_nonoverlapping(s.as_ptr(), out as *mut u8, s.len());
    *out.add(s.len()) = 0;
    s.len() as i32
}

fn load_or_create_key(path: &str) -> Option<SecretKey> {
    let path = PathBuf::from(path);
    if let Ok(bytes) = std::fs::read(&path) {
        if bytes.len() == 32 {
            let mut arr = [0u8; 32];
            arr.copy_from_slice(&bytes);
            return Some(SecretKey::from_bytes(&arr));
        }
        log(format!("iroh: ignoring malformed key file {}", path.display()));
    }
    let key = SecretKey::generate();
    if let Some(dir) = path.parent() {
        let _ = std::fs::create_dir_all(dir);
    }
    if let Err(e) = std::fs::write(&path, key.to_bytes()) {
        log(format!("iroh: could not persist key to {}: {e}", path.display()));
    }
    Some(key)
}

fn push_packet(p: &Plugin, peer: u64, flags: u32, data: Vec<u8>) {
    let mut q = p.queue.lock().unwrap();
    if q.len() >= MAX_QUEUED_PACKETS {
        // The game is not draining us (e.g. it is loading a map); drop the
        // oldest datagram like a full socket buffer would.
        q.pop_front();
    }
    q.push_back(Packet { peer, flags, data });
}

fn set_state(p: &Plugin, id: u64, state: PeerState, error: Option<String>) {
    if let Some(peer) = p.peers.lock().unwrap().get_mut(&id) {
        peer.state = state;
        if let Some(e) = error {
            peer.error = e;
        }
    }
}

async fn read_loop(id: u64, conn: Connection, incoming: bool) {
    let p = plugin().expect("plugin initialised");
    let base = if incoming { FLAG_INCOMING } else { 0 };
    loop {
        match conn.read_datagram().await {
            Ok(bytes) => push_packet(p, id, base, bytes.to_vec()),
            Err(e) => {
                log(format!("iroh: peer {id} closed: {e}"));
                set_state(p, id, PeerState::Closed, Some(e.to_string()));
                push_packet(p, id, base | FLAG_CLOSED, Vec::new());
                return;
            }
        }
    }
}

async fn accept_loop() {
    let p = plugin().expect("plugin initialised");
    while let Some(incoming) = p.endpoint.accept().await {
        tokio::spawn(async move {
            let conn = match incoming.await {
                Ok(conn) => conn,
                Err(e) => {
                    log(format!("iroh: incoming connection failed: {e}"));
                    return;
                }
            };
            if conn.alpn() != ALPN {
                conn.close(0u32.into(), b"bad alpn");
                return;
            }
            let id = p.next_id.fetch_add(1, Ordering::Relaxed);
            log(format!("iroh: accepted {} as peer {id}", conn.remote_id().fmt_short()));
            p.peers.lock().unwrap().insert(
                id,
                Peer { conn: Some(conn.clone()), state: PeerState::Connected, error: String::new() },
            );
            read_loop(id, conn, true).await;
        });
    }
}

/// Parses "<endpoint id>[|<relay url>][|<ip:port>,<ip:port>...]".
fn parse_remote(s: &str) -> Option<EndpointAddr> {
    let mut parts = s.split('|');
    let id = EndpointId::from_str(parts.next()?.trim()).ok()?;
    let mut addr = EndpointAddr::new(id);
    if let Some(relay) = parts.next() {
        let relay = relay.trim();
        if !relay.is_empty() {
            if let Ok(url) = RelayUrl::from_str(relay) {
                addr = addr.with_relay_url(url);
            }
        }
    }
    if let Some(ips) = parts.next() {
        for ip in ips.split(',') {
            if let Ok(sa) = SocketAddr::from_str(ip.trim()) {
                addr = addr.with_ip_addr(sa);
            }
        }
    }
    Some(addr)
}

fn format_local_addr(ep: &Endpoint) -> String {
    let addr = ep.addr();
    let relay = addr.relay_urls().next().map(|u| u.to_string()).unwrap_or_default();
    let ips: Vec<String> = addr.ip_addrs().map(|a| a.to_string()).collect();
    format!("{}|{}|{}", ep.id(), relay, ips.join(","))
}

#[no_mangle]
pub extern "C" fn r1p_abi_version() -> i32 {
    ABI_VERSION
}

/// # Safety
/// `config_json` must be NULL or a valid NUL-terminated string.
#[no_mangle]
pub unsafe extern "C" fn r1p_init(config_json: *const c_char) -> i32 {
    guard(|| {
        if PLUGIN.get().is_some() {
            return OK;
        }
        let cfg: Config = cstr(config_json)
            .and_then(|s| serde_json::from_str(s).ok())
            .unwrap_or_default();

        let rt = match tokio::runtime::Builder::new_multi_thread()
            .worker_threads(2)
            .thread_name("r1delta-iroh")
            .enable_all()
            .build()
        {
            Ok(rt) => rt,
            Err(e) => {
                log(format!("iroh: runtime failed: {e}"));
                return ERR_INTERNAL;
            }
        };

        let mut builder = Endpoint::builder(presets::N0).alpns(vec![ALPN.to_vec()]);
        if let Some(key) = cfg.key_file.as_deref().and_then(load_or_create_key) {
            builder = builder.secret_key(key);
        }
        if let Some(url) = cfg.relay_url.as_deref().filter(|s| !s.is_empty()) {
            match RelayUrl::from_str(url) {
                Ok(url) => builder = builder.relay_mode(RelayMode::Custom(RelayMap::from(url))),
                Err(e) => log(format!("iroh: ignoring bad relay url {url}: {e}")),
            }
        }
        let endpoint = match rt.block_on(builder.bind()) {
            Ok(ep) => ep,
            Err(e) => {
                log(format!("iroh: bind failed: {e}"));
                return ERR_INTERNAL;
            }
        };
        log(format!("iroh: endpoint {} bound", endpoint.id().fmt_short()));

        let plugin = Plugin {
            rt,
            endpoint,
            peers: Mutex::new(HashMap::new()),
            queue: Mutex::new(VecDeque::new()),
            next_id: AtomicU64::new(1),
            listening: AtomicBool::new(false),
        };
        if PLUGIN.set(plugin).is_err() {
            return OK; // raced with another init; theirs wins
        }
        OK
    })
}

/// # Safety
/// `out_addr` must point to `cap` writable bytes.
#[no_mangle]
pub unsafe extern "C" fn r1p_listen(_port: u16, out_addr: *mut c_char, cap: u32) -> i32 {
    guard(|| {
        let Some(p) = plugin() else { return ERR_STATE };
        if !p.listening.swap(true, Ordering::AcqRel) {
            p.rt.spawn(accept_loop());
        }
        let online = p.rt.block_on(async { tokio::time::timeout(ONLINE_TIMEOUT, p.endpoint.online()).await });
        if online.is_err() {
            log("iroh: relay not reachable yet; advertising direct addresses only");
        }
        let addr = format_local_addr(&p.endpoint);
        let written = write_cstr(&addr, out_addr, cap);
        if written < 0 {
            written
        } else {
            OK
        }
    })
}

/// # Safety
/// `remote` must be a valid string and `out_peer` a valid pointer.
#[no_mangle]
pub unsafe extern "C" fn r1p_connect(remote: *const c_char, _port: u16, out_peer: *mut u64) -> i32 {
    guard(|| {
        let Some(p) = plugin() else { return ERR_STATE };
        let Some(remote) = cstr(remote) else { return ERR_ARG };
        if out_peer.is_null() {
            return ERR_ARG;
        }
        let Some(addr) = parse_remote(remote) else { return ERR_ARG };
        let id = p.next_id.fetch_add(1, Ordering::Relaxed);
        p.peers
            .lock()
            .unwrap()
            .insert(id, Peer { conn: None, state: PeerState::Connecting, error: String::new() });
        *out_peer = id;

        let ep = p.endpoint.clone();
        p.rt.spawn(async move {
            let p = plugin().expect("plugin initialised");
            let target = addr.id.fmt_short().to_string();
            match tokio::time::timeout(CONNECT_TIMEOUT, ep.connect(addr, ALPN)).await {
                Ok(Ok(conn)) => {
                    log(format!("iroh: connected to {target} as peer {id}"));
                    {
                        let mut peers = p.peers.lock().unwrap();
                        match peers.get_mut(&id) {
                            Some(peer) => {
                                peer.conn = Some(conn.clone());
                                peer.state = PeerState::Connected;
                            }
                            None => {
                                // Closed by the game while we were connecting.
                                conn.close(0u32.into(), b"closed");
                                return;
                            }
                        }
                    }
                    read_loop(id, conn, false).await;
                }
                Ok(Err(e)) => {
                    log(format!("iroh: connect to {target} failed: {e}"));
                    set_state(p, id, PeerState::Failed, Some(e.to_string()));
                }
                Err(_) => {
                    log(format!("iroh: connect to {target} timed out"));
                    set_state(p, id, PeerState::Failed, Some("timeout".into()));
                }
            }
        });
        OK
    })
}

/// # Safety
/// `data` must point to `len` readable bytes.
#[no_mangle]
pub unsafe extern "C" fn r1p_send(peer: u64, data: *const u8, len: u32) -> i32 {
    guard(|| {
        let Some(p) = plugin() else { return ERR_STATE };
        if data.is_null() && len > 0 {
            return ERR_ARG;
        }
        let conn = {
            let peers = p.peers.lock().unwrap();
            match peers.get(&peer) {
                None => return ERR_NOTFOUND,
                Some(peer) => match &peer.conn {
                    Some(c) if peer.state == PeerState::Connected => c.clone(),
                    _ => return ERR_AGAIN,
                },
            }
        };
        let payload = Bytes::copy_from_slice(std::slice::from_raw_parts(data, len as usize));
        match conn.send_datagram(payload) {
            Ok(()) => OK,
            Err(iroh::endpoint::SendDatagramError::TooLarge) => ERR_TOOBIG,
            Err(_) => ERR_AGAIN,
        }
    })
}

/// # Safety
/// All pointers must be valid; `buf` must have `cap` writable bytes.
#[no_mangle]
pub unsafe extern "C" fn r1p_recv(
    out_peer: *mut u64,
    out_flags: *mut u32,
    buf: *mut u8,
    cap: u32,
    out_len: *mut u32,
) -> i32 {
    guard(|| {
        let Some(p) = plugin() else { return OK };
        if out_peer.is_null() || out_flags.is_null() || out_len.is_null() || (buf.is_null() && cap > 0) {
            return ERR_ARG;
        }
        let pkt = {
            let mut q = p.queue.lock().unwrap();
            match q.front() {
                None => return OK,
                Some(front) if front.data.len() > cap as usize => {
                    *out_len = front.data.len() as u32;
                    return ERR_BUFFER;
                }
                Some(_) => q.pop_front().unwrap(),
            }
        };
        *out_peer = pkt.peer;
        *out_flags = pkt.flags;
        *out_len = pkt.data.len() as u32;
        if !pkt.data.is_empty() {
            std::ptr::copy_nonoverlapping(pkt.data.as_ptr(), buf, pkt.data.len());
        }
        if pkt.flags & FLAG_CLOSED != 0 {
            p.peers.lock().unwrap().remove(&pkt.peer);
        }
        GOT
    })
}

/// # Safety
/// `out_json` must point to `cap` writable bytes.
#[no_mangle]
pub unsafe extern "C" fn r1p_peer_status(peer: u64, out_json: *mut c_char, cap: u32) -> i32 {
    guard(|| {
        let Some(p) = plugin() else { return ERR_STATE };
        let (state, error, conn) = {
            let peers = p.peers.lock().unwrap();
            let Some(peer) = peers.get(&peer) else { return ERR_NOTFOUND };
            (peer.state, peer.error.clone(), peer.conn.clone())
        };
        let mut path = "unknown";
        let mut rtt_ms = -1.0f64;
        if let Some(conn) = &conn {
            let paths = conn.paths();
            for pth in paths.iter() {
                if pth.is_selected() {
                    path = if pth.is_relay() { "relay" } else { "direct" };
                    rtt_ms = pth.rtt().as_secs_f64() * 1000.0;
                }
            }
        }
        let json = serde_json::json!({
            "state": state.as_str(),
            "path": path,
            "rtt_ms": rtt_ms,
            "error": error,
        })
        .to_string();
        let written = write_cstr(&json, out_json, cap);
        if written < 0 {
            written
        } else {
            OK
        }
    })
}

#[no_mangle]
pub extern "C" fn r1p_max_datagram(peer: u64) -> i32 {
    guard(|| {
        let Some(p) = plugin() else { return ERR_STATE };
        let peers = p.peers.lock().unwrap();
        let Some(peer) = peers.get(&peer) else { return ERR_NOTFOUND };
        match peer.conn.as_ref().and_then(|c| c.max_datagram_size()) {
            Some(n) => n.min(i32::MAX as usize) as i32,
            None => 1024,
        }
    })
}

#[no_mangle]
pub extern "C" fn r1p_close_peer(peer: u64) -> i32 {
    guard(|| {
        let Some(p) = plugin() else { return ERR_STATE };
        match p.peers.lock().unwrap().remove(&peer) {
            Some(peer) => {
                if let Some(conn) = peer.conn {
                    conn.close(0u32.into(), b"closed");
                }
                OK
            }
            None => ERR_NOTFOUND,
        }
    })
}

/// # Safety
/// `out` must point to `cap` writable bytes.
#[no_mangle]
pub unsafe extern "C" fn r1p_poll_log(out: *mut c_char, cap: u32) -> i32 {
    guard(|| {
        let line = match LOG.lock() {
            Ok(mut q) => match q.pop_front() {
                Some(l) => l,
                None => return OK,
            },
            Err(_) => return OK,
        };
        let mut line = line;
        if line.len() + 1 > cap as usize {
            let mut cut = (cap as usize).saturating_sub(1);
            while cut > 0 && !line.is_char_boundary(cut) {
                cut -= 1;
            }
            line.truncate(cut);
        }
        if write_cstr(&line, out, cap) < 0 {
            return ERR_BUFFER;
        }
        GOT
    })
}

#[no_mangle]
pub extern "C" fn r1p_shutdown() {
    let _ = guard(|| {
        if let Some(p) = plugin() {
            let conns: Vec<Connection> = p
                .peers
                .lock()
                .unwrap()
                .drain()
                .filter_map(|(_, peer)| peer.conn)
                .collect();
            for c in conns {
                c.close(0u32.into(), b"shutdown");
            }
            let _ = p.rt.block_on(async {
                tokio::time::timeout(Duration::from_secs(2), p.endpoint.close()).await
            });
        }
        OK
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_remote_formats() {
        let id = SecretKey::generate().public().to_string();
        assert!(parse_remote(&id).is_some());
        let full = format!("{id}|https://relay.example.org./|1.2.3.4:5,[::1]:6");
        let addr = parse_remote(&full).unwrap();
        assert_eq!(addr.ip_addrs().count(), 2);
        assert_eq!(addr.relay_urls().count(), 1);
        assert!(parse_remote("not-a-key").is_none());
    }
}
