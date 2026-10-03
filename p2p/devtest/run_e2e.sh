#!/usr/bin/env bash
# End-to-end test of the p2p transports on Linux (developer tool; the game
# itself is Windows-only). Starts coturn, a fake Cloudflare TURN credential
# API, the real master server (../../../masterserver2), a local DERP relay for
# tailcat, a NAT-emulated fake game server and several fake clients that each
# force a different route.
#
# Usage: run_e2e.sh <masterserver2 dir> [bind ip]
# Needs: g++, go (1.27+ for tailcat), cargo, turnserver (coturn), python3.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
P2P="$(cd "$HERE/.." && pwd)"
MS_DIR="$(cd "$1" && pwd)"
IP="${2:-$(hostname -I | awk '{print $1}')}"
WORK="$(mktemp -d)"
PIDS=()
cleanup() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null || true; done; }
trap cleanup EXIT
echo "work dir: $WORK, ip: $IP"

# --- third-party headers for the harness build ---------------------------
HDR="${R1P_HEADER_CACHE:-$HOME/.cache/r1p_devtest}"
mkdir -p "$HDR/nlohmann"
[ -f "$HDR/nlohmann/json.hpp" ] || curl -sSfL -o "$HDR/nlohmann/json.hpp" https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp
[ -f "$HDR/httplib.h" ] || curl -sSfL -o "$HDR/httplib.h" https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.1/httplib.h

# --- build ---------------------------------------------------------------
echo "building plugins + harness..."
mkdir -p "$WORK/plugins"
export GOTOOLCHAIN="${GOTOOLCHAIN:-auto}"
(cd "$P2P/plugins/iroh" && cargo build --release -q)
(cd "$P2P/plugins/tailcat" && go build -buildmode=c-shared -o "$WORK/plugins/r1delta_tailcat.so" . && go build -o "$WORK/localderp" ./localderp)
cp "$P2P/plugins/iroh/target/release/libr1delta_iroh.so" "$WORK/plugins/"
(cd "$MS_DIR" && go build -o "$WORK/masterserver" .)
g++ -std=c++20 -O1 -g -Wall -Wno-unknown-pragmas -include "$HERE/win_compat.h" -I"$HERE/shim_include" \
    -I"$HERE/stub_include" -I"$HDR" -I"$P2P" "$HERE/fake_engine.cpp" "$P2P"/p2p.cpp "$P2P"/p2p_mux.cpp \
    "$P2P"/p2p_plugin.cpp "$P2P"/p2p_turn.cpp "$P2P"/p2p_server.cpp "$P2P"/p2p_connect.cpp \
    "$P2P"/p2p_protocol.cpp "$P2P"/stun.cpp "$P2P"/upnp_codec.cpp "$P2P"/p2p_identity.cpp "$HERE/crypto_openssl.cpp" \
    -Wno-deprecated-declarations -o "$WORK/fake_engine" -ldl -lpthread -lcrypto

# Identity attestation key (test only) and its public X||Y for the game side.
openssl ecparam -name prime256v1 -genkey -noout -out "$WORK/attest.pem" 2>/dev/null
IDENTITY_PUB="$(openssl ec -in "$WORK/attest.pem" -pubout -outform DER 2>/dev/null | tail -c 64 | od -An -tx1 | tr -d ' \n')"
export P2P_delta_p2p_identity_pubkey="$IDENTITY_PUB"

# --- infrastructure ------------------------------------------------------
turnserver -n --listening-ip="$IP" --listening-port=3478 --relay-ip="$IP" --min-port=49000 --max-port=49200 \
    --lt-cred-mech --user=r1test:r1secret --realm=r1delta.test --no-tls --no-dtls --no-cli \
    --log-file="$WORK/turn.log" --simple-log >/dev/null 2>&1 &
PIDS+=($!)

cat > "$WORK/cfapi.py" <<PY
import http.server, json
class H(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        body = json.dumps({"iceServers": [{"urls": ["stun:$IP:3478"]}, {"urls": ["turn:$IP:3478?transport=udp", "turns:$IP:5349?transport=tcp"], "username": "r1test", "credential": "r1secret"}]}).encode()
        self.send_response(201); self.send_header("Content-Type", "application/json"); self.end_headers(); self.wfile.write(body)
    def log_message(self, *a): pass
http.server.HTTPServer(("127.0.0.1", 8099), H).serve_forever()
PY
python3 "$WORK/cfapi.py" & PIDS+=($!)

"$WORK/localderp" > "$WORK/derp.out" 2>"$WORK/derp.err" & PIDS+=($!)
for _ in $(seq 50); do grep -q DERPMAP "$WORK/derp.out" && break; sleep 0.1; done
DERPMAP="$(sed -n 's/^DERPMAP //p' "$WORK/derp.out")"

(cd "$WORK" && exec env MS_TOKEN=x JWT_DISCORD_SECRET=x CLIENT_ID=x CLIENT_SECRET=x REDIRECT_URI=x PORT=8080 \
    RENDEZVOUS_LISTEN="$IP:37999" RENDEZVOUS_PUBLIC_ADDR="$IP:37999" \
    CF_TURN_KEY_ID=key CF_TURN_API_TOKEN=token CF_TURN_API_URL="http://127.0.0.1:8099/%s" ATTEST_KEY_FILE="$WORK/attest.pem" \
    ./masterserver > "$WORK/master.log" 2>&1) & PIDS+=($!)
for _ in $(seq 50); do curl -s "http://$IP:8080/servers" >/dev/null && break; sleep 0.2; done
if ! grep -q "Rendezvous listening" "$WORK/master.log"; then echo "master server failed to start:"; cat "$WORK/master.log"; exit 1; fi

export R1P_PLUGIN_DIR="$WORK/plugins"
export P2P_delta_p2p_tailcat_derpmap_url="$DERPMAP"
# This harness uses only local coturn and its mock credential API, not live
# Cloudflare. Opt in explicitly now that production TURN defaults off.
export P2P_delta_p2p_server_turn=1
MASTER="http://$IP:8080"

FAKE_NAT=1 "$WORK/fake_engine" server "$IP" 37015 "$MASTER" > "$WORK/server.log" 2>&1 & PIDS+=($!)

# --- clients -------------------------------------------------------------
fail=0
port=37100
for prefer in "" direct iroh tailcat turn; do
    port=$((port + 1))
    echo "=== client (prefer='${prefer:-best}')"
    if WAIT_TRANSPORTS="iroh,tailcat,turn" P2P_delta_p2p_prefer="$prefer" P2P_delta_p2p_connect_timeout_ms=8000 \
        "$WORK/fake_engine" client "$IP" "$port" "$MASTER" "$IP:37015" > "$WORK/client_$port.log" 2>&1; then
        :
    else
        fail=1
    fi
    grep -E "Route probe|^\[client\]   [ *]|connecting via|RESULT" "$WORK/client_$port.log" | sed 's/^/    /'
    chosen="$(sed -n 's/.*RESULT ok chosen=\([a-z]*\).*/\1/p' "$WORK/client_$port.log")"
    if [ -n "$prefer" ] && [ "$chosen" != "$prefer" ]; then echo "    expected route $prefer, got '$chosen'"; fail=1; fi
done

echo "=== client -> legacy server (p2p disabled, must connect straight away)"
P2P_delta_p2p_enable=0 "$WORK/fake_engine" server "$IP" 37016 "$MASTER" > "$WORK/legacy_server.log" 2>&1 & PIDS+=($!)
if "$WORK/fake_engine" client "$IP" 37150 "$MASTER" "$IP:37016" > "$WORK/client_legacy.log" 2>&1 &&
    grep -q "does not advertise p2p routes" "$WORK/client_legacy.log"; then
    grep -E "does not advertise|RESULT" "$WORK/client_legacy.log" | sed 's/^/    /'
else
    echo "    legacy fallback failed"; fail=1
fi

run_expect() { # <name> <expect ok|fail> <server port> <client port> [env...]
    local name="$1" expect="$2" sport="$3" cport="$4"; shift 4
    echo "=== $name (expect $expect)"
    if env "$@" "$WORK/fake_engine" client "$IP" "$cport" "$MASTER" "$IP:$sport" > "$WORK/client_$cport.log" 2>&1; then got=ok; else got=fail; fi
    grep -E "RESULT|identity|connecting via" "$WORK/client_$cport.log" | sed 's/^/    /'
    if [ "$got" != "$expect" ]; then echo "    UNEXPECTED: got $got"; fail=1; fi
}
run_expect "manual iroh connect with identity" ok 37015 37160 FAKE_CONNECT_OVERLAY=iroh WAIT_TRANSPORTS=iroh
run_expect "manual iroh connect without identity token" fail 37015 37161 FAKE_CONNECT_OVERLAY=iroh WAIT_TRANSPORTS=iroh FAKE_TOKEN_MS_URL=http://127.0.0.1:1

echo "=== starting a second NAT'd server that bans $IP"
FAKE_NAT=1 FAKE_BANNED_IP="$IP" "$WORK/fake_engine" server "$IP" 37017 "$MASTER" > "$WORK/banning_server.log" 2>&1 & PIDS+=($!)
run_expect "banned player over iroh" fail 37017 37170 WAIT_TRANSPORTS=iroh,turn P2P_delta_p2p_prefer=iroh P2P_delta_p2p_connect_timeout_ms=8000
run_expect "banned player over turn" fail 37017 37171 WAIT_TRANSPORTS=iroh,turn P2P_delta_p2p_prefer=turn P2P_delta_p2p_connect_timeout_ms=8000

echo "=== master server NAT log"
grep -E "\[NAT\]|\[Validation\] (Successfully|Validation failed)" "$WORK/master.log" | head -20 | sed 's/^/    /'
echo "=== server p2p log"
grep -E "P2P" "$WORK/server.log" | head -30 | sed 's/^/    /'
[ "$fail" = 0 ] && echo "E2E PASSED" || { echo "E2E FAILED (logs in $WORK)"; trap - EXIT; cleanup; exit 1; }
