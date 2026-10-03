#!/usr/bin/env bash
# Usage: run_loopback.sh <plugin.so> [port]
# Starts a server and a client instance of the plugin and checks datagrams
# round-trip between them.
set -euo pipefail
PLUGIN="$1"
PORT="${2:-37015}"
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${TMPDIR:-/tmp}/plugin_loopback"
g++ -std=c++17 -O1 -o "$BIN" "$HERE/plugin_loopback.cpp" -ldl -lpthread
OUT="$(mktemp)"
"$BIN" "$PLUGIN" server "$PORT" > "$OUT" &
SERVER=$!
trap 'kill $SERVER 2>/dev/null || true; rm -f "$OUT"' EXIT
for _ in $(seq 1 300); do
  if grep -q '^ADDR ' "$OUT"; then break; fi
  sleep 0.1
done
ADDR="$(sed -n 's/^ADDR //p' "$OUT")"
if [ -z "$ADDR" ]; then echo "server never printed an address"; exit 1; fi
echo "server address: $ADDR"
"$BIN" "$PLUGIN" client "$ADDR" "$PORT"
wait $SERVER
