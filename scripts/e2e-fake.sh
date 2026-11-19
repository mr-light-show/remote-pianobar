#!/usr/bin/env bash
# Run the web UI Playwright E2E suite against the REAL pianobar binary, talking
# to a local fake Pandora API (test/e2e-fake-pandora/fake_pandora.py). No
# Pandora account, network access or sound card is needed.
#
# Usage: scripts/e2e-fake.sh [extra playwright args...]
#   PIANOBAR_BIN   pianobar binary (default: ./pianobar, built with make if absent)
#   E2E_PROJECT    Playwright project (default: chromium)
#   E2E_KEEP=1     keep the temp directory (logs, config) after the run
#
# Safety: pianobar runs with a throw-away HOME and an explicit config, on free
# loopback ports. Nothing outside the temp directory is read or written, and
# only processes started here are killed.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PIANOBAR_BIN=${PIANOBAR_BIN:-$ROOT/pianobar}
case $PIANOBAR_BIN in /*) ;; *) PIANOBAR_BIN=$PWD/$PIANOBAR_BIN ;; esac
E2E_PROJECT=${E2E_PROJECT:-chromium}

TMP=$(mktemp -d "${TMPDIR:-/tmp}/pianobar-e2e.XXXXXX")
FAKE_PID=
PIANOBAR_PID=
PIDFILE=$TMP/pianobar.pid

cleanup() {
	status=$?
	# Web mode daemonizes, so the daemon's PID comes from its pid_file (or the
	# lock file inside our private HOME), not from the process started below.
	[ -s "$PIDFILE" ] && PIANOBAR_PID=$(cat "$PIDFILE")
	[ -n "$PIANOBAR_PID" ] || PIANOBAR_PID=$(head -n 1 "$TMP/home/.config/pianobar/pianobar.lock" 2>/dev/null)
	case "$PIANOBAR_PID" in ''|*[!0-9]*) PIANOBAR_PID= ;; esac
	for pid in "$PIANOBAR_PID" "$FAKE_PID"; do
		if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
			kill "$pid" 2>/dev/null
			for _ in 1 2 3 4 5 6 7 8 9 10; do
				kill -0 "$pid" 2>/dev/null || break
				sleep 0.3
			done
			kill -9 "$pid" 2>/dev/null
		fi
	done
	if [ -n "${E2E_KEEP:-}" ] || { [ "$status" -ne 0 ] && [ -n "${E2E_KEEP_ON_FAIL:-}" ]; }; then
		echo "e2e-fake: kept $TMP" >&2
	else
		rm -rf "$TMP"
	fi
	exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

die() { echo "e2e-fake: $*" >&2; exit 2; }

for tool in python3 openssl curl npx; do
	command -v "$tool" >/dev/null 2>&1 || die "missing required tool: $tool"
done

# Build prerequisites on demand.
[ -x "$PIANOBAR_BIN" ] || { make -C "$ROOT" >&2 || die "make failed"; }
[ -x "$PIANOBAR_BIN" ] || die "pianobar binary not found: $PIANOBAR_BIN"
if [ ! -d "$ROOT/webui/node_modules" ]; then
	(cd "$ROOT/webui" && npm ci) >&2 || die "npm ci failed"
fi
if [ ! -f "$ROOT/dist/webui/index.html" ]; then
	(cd "$ROOT/webui" && npx vite build) >&2 || die "web UI build failed"
fi

# No ambient proxy may redirect pianobar's or the decoder's traffic.
unset http_proxy https_proxy all_proxy HTTP_PROXY HTTPS_PROXY ALL_PROXY
export no_proxy= NO_PROXY=

free_port() {
	python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'
}
WS_PORT=$(free_port)

# Throwaway self-signed certificate for the fake's HTTPS tunnel (never committed).
cat >"$TMP/openssl.cnf" <<'EOF'
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = 127.0.0.1
[ext]
subjectAltName = IP:127.0.0.1
basicConstraints = critical,CA:TRUE
keyUsage = digitalSignature,keyEncipherment,keyCertSign
extendedKeyUsage = serverAuth
EOF
openssl req -x509 -newkey rsa:2048 -nodes -days 2 -config "$TMP/openssl.cnf" \
	-keyout "$TMP/key.pem" -out "$TMP/cert.pem" >/dev/null 2>&1 \
	|| die "could not generate the throwaway certificate"

# Made-up Blowfish keys and partner credentials shared by fake and pianobar.
OUTKEY=e2e-out-key-1
INKEY=e2e-in-key-2
PARTNER_USER=e2e-partner
PARTNER_PASSWORD=e2e-partner-password

python3 -I "$ROOT/test/e2e-fake-pandora/fake_pandora.py" \
	--port 0 --port-file "$TMP/fake.port" --cert "$TMP/cert.pem" --key "$TMP/key.pem" \
	--audio "$ROOT/test/fixtures/tone.mp3" --outkey "$OUTKEY" --inkey "$INKEY" \
	--partner-user "$PARTNER_USER" --partner-password "$PARTNER_PASSWORD" \
	>"$TMP/fake.log" 2>&1 &
FAKE_PID=$!

for _ in $(seq 1 100); do
	[ -s "$TMP/fake.port" ] && break
	kill -0 "$FAKE_PID" 2>/dev/null || { cat "$TMP/fake.log" >&2; die "fake Pandora exited early"; }
	sleep 0.1
done
[ -s "$TMP/fake.port" ] || die "fake Pandora did not start"
FAKE_PORT=$(cat "$TMP/fake.port")

# Isolated HOME with an explicit config (XDG_CONFIG_HOME pinned inside it).
mkdir -p "$TMP/home/.config/pianobar"
cat >"$TMP/home/.config/pianobar/config" <<EOF
user = e2e@example.invalid
password = e2e-password
rpc_host = 127.0.0.1
rpc_tls_port = $FAKE_PORT
control_proxy = http://127.0.0.1:$FAKE_PORT
ca_bundle = $TMP/cert.pem
partner_user = $PARTNER_USER
partner_password = $PARTNER_PASSWORD
device = e2e-device
encrypt_password = $OUTKEY
decrypt_password = $INKEY
autostart_station = st-001
ui_mode = web
websocket_host = 127.0.0.1
websocket_port = $WS_PORT
webui_path = $ROOT/dist/webui
pid_file = $PIDFILE
log_file = $TMP/pianobar-daemon.log
EOF

# ui_mode=web daemonizes: this foreground process prints its startup banner and
# exits once the daemon is up (PID in $PIDFILE).
(
	cd "$TMP/home" || exit 1
	HOME="$TMP/home" XDG_CONFIG_HOME="$TMP/home/.config" \
		PIANOBAR_TEST_NULL_AUDIO=1 \
		exec "$PIANOBAR_BIN"
) </dev/null >"$TMP/pianobar.log" 2>&1 &
LAUNCHER_PID=$!

BASE_URL="http://127.0.0.1:$WS_PORT"
ready=
for _ in $(seq 1 150); do
	if [ -s "$PIDFILE" ] && ! kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
		echo "--- pianobar log ---" >&2; cat "$TMP/pianobar.log" "$TMP/pianobar-daemon.log" >&2 2>/dev/null
		echo "--- fake log ---" >&2; cat "$TMP/fake.log" >&2
		die "pianobar exited early"
	fi
	if ! kill -0 "$LAUNCHER_PID" 2>/dev/null && [ ! -s "$PIDFILE" ]; then
		echo "--- pianobar log ---" >&2; cat "$TMP/pianobar.log" >&2
		die "pianobar launcher exited without starting the daemon"
	fi
	if curl -sf -o /dev/null "$BASE_URL/" && grep -q 'user.getStationList -> ok' "$TMP/fake.log"; then
		ready=1
		break
	fi
	sleep 0.2
done
if [ -z "$ready" ]; then
	echo "--- pianobar log ---" >&2; cat "$TMP/pianobar.log" "$TMP/pianobar-daemon.log" >&2 2>/dev/null
	echo "--- fake log ---" >&2; cat "$TMP/fake.log" >&2
	die "pianobar did not become ready (no login/station list within 30s)"
fi
echo "e2e-fake: pianobar at $BASE_URL, fake Pandora on 127.0.0.1:$FAKE_PORT" >&2

# Web mode waits for a station to be chosen, and the playback specs need a
# playing track. Choose one the way a client would: a Socket.IO "station.change"
# over a raw WebSocket (stdlib only; protocol in WEBSOCKET_API.md).
python3 -I - "$WS_PORT" st-001 <<'PY' || die "could not select the station"
import base64, os, socket, struct, sys
port, station = int(sys.argv[1]), sys.argv[2]
sock = socket.create_connection(("127.0.0.1", port), timeout=10)
key = base64.b64encode(os.urandom(16)).decode()
sock.sendall(("GET /socket.io HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\n"
              "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n"
              "Sec-WebSocket-Protocol: socketio\r\n\r\n" % (port, key)).encode())
reply = b""
while b"\r\n\r\n" not in reply:
    chunk = sock.recv(4096)
    if not chunk:
        sys.exit("websocket handshake failed")
    reply += chunk
if b" 101 " not in reply.split(b"\r\n")[0] + b" ":
    sys.exit("websocket handshake refused: %r" % reply[:80])
payload = ('2["station.change",{"id":"%s"}]' % station).encode()
mask = os.urandom(4)
frame = bytes([0x81, 0x80 | len(payload)]) + mask + bytes(
    b ^ mask[i % 4] for i, b in enumerate(payload))
assert len(payload) < 126
sock.sendall(frame)
sock.settimeout(1)
try:   # let the server read the command before the socket goes away
    sock.recv(65536)
except OSError:
    pass
sock.close()
PY
playing=
for _ in $(seq 1 100); do
	if grep -q 'station.getPlaylist -> ok' "$TMP/fake.log" && grep -q '/audio/.*200\|/audio/.*206' "$TMP/fake.log"; then
		playing=1
		break
	fi
	sleep 0.2
done
if [ -z "$playing" ]; then
	echo "--- pianobar log ---" >&2; cat "$TMP/pianobar-daemon.log" >&2 2>/dev/null
	echo "--- fake log ---" >&2; cat "$TMP/fake.log" >&2
	die "no playlist/audio request after selecting the station"
fi

cd "$ROOT/webui" || exit 1
PIANOBAR_RUNNING=1 PIANOBAR_URL="$BASE_URL" \
	npx playwright test --project="$E2E_PROJECT" --reporter=list --workers=1 "$@"
rc=$?

if [ "$rc" -ne 0 ]; then
	echo "--- fake log (tail) ---" >&2; tail -n 40 "$TMP/fake.log" >&2
	echo "--- pianobar log (tail) ---" >&2; tail -n 40 "$TMP/pianobar-daemon.log" >&2 2>/dev/null
fi
exit "$rc"
