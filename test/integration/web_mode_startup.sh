#!/usr/bin/env bash
# Smoke test: the web-only daemon (ui_mode = web) must start and shut down
# without leaving a standard fd closed, and must show why it stopped on stderr
# when no log_file is configured.
#
# Pandora is pointed at a closed loopback port, so the login fails at once
# (no network traffic, no account) and the daemon exits. Before the stdio fix
# that exit printed "lws: ZERO RANDOM FD" (closed fd 0 reused by lws) and the
# real reason went to /dev/null.
#
# Usage: test/integration/web_mode_startup.sh
#   PIANOBAR_BIN   pianobar binary (default: ./pianobar)
#   SMOKE_KEEP=1   keep the temp directory
#
# Safety: runs with a throw-away HOME and an explicit config on loopback ports.
# Only the process started here is signalled.
set -u

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PIANOBAR_BIN=${PIANOBAR_BIN:-$ROOT/pianobar}
case $PIANOBAR_BIN in /*) ;; *) PIANOBAR_BIN=$PWD/$PIANOBAR_BIN ;; esac
[ -x "$PIANOBAR_BIN" ] || { echo "web_mode_startup: no binary: $PIANOBAR_BIN" >&2; exit 2; }
command -v python3 >/dev/null 2>&1 || { echo "web_mode_startup: python3 required" >&2; exit 2; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/pianobar-startup.XXXXXX")
DAEMON_PID=
cleanup() {
	status=$?
	if [ -n "$DAEMON_PID" ] && kill -0 "$DAEMON_PID" 2>/dev/null; then
		kill "$DAEMON_PID" 2>/dev/null
		sleep 1
		kill -9 "$DAEMON_PID" 2>/dev/null
	fi
	if [ -n "${SMOKE_KEEP:-}" ] || { [ "$status" -ne 0 ] && [ -n "${SMOKE_KEEP_ON_FAIL:-}" ]; }; then
		echo "web_mode_startup: kept $TMP" >&2
	else
		rm -rf "$TMP"
	fi
	exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

fail() {
	echo "web_mode_startup: FAIL: $*" >&2
	echo "--- stderr ---" >&2; cat "$TMP/err" >&2
	exit 1
}

free_port() {
	python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'
}
WS_PORT=$(free_port)
DEAD_PORT=$(free_port)

mkdir -p "$TMP/home/.config/pianobar"
cat >"$TMP/home/.config/pianobar/config" <<CFG
user = smoke@example.invalid
password = smoke-password
rpc_host = 127.0.0.1
rpc_tls_port = $DEAD_PORT
control_proxy = http://127.0.0.1:$DEAD_PORT
ui_mode = web
websocket_host = 127.0.0.1
websocket_port = $WS_PORT
pid_file = $TMP/pianobar.pid
CFG

unset http_proxy https_proxy all_proxy HTTP_PROXY HTTPS_PROXY ALL_PROXY
export no_proxy= NO_PROXY=

# No log_file on purpose: stdout is /dev/null, stderr stays the caller's.
(
	cd "$TMP/home" || exit 1
	HOME="$TMP/home" XDG_CONFIG_HOME="$TMP/home/.config" \
		PIANOBAR_TEST_NULL_AUDIO=1 \
		exec "$PIANOBAR_BIN"
) </dev/null >"$TMP/out" 2>"$TMP/err" &
LAUNCHER_PID=$!

# The banner names the daemon's PID; wait for it, then for the daemon to exit.
for _ in $(seq 1 100); do
	DAEMON_PID=$(sed -n 's/^Pianobar daemon started (PID: \([0-9][0-9]*\)).*/\1/p' "$TMP/err" | head -n 1)
	[ -n "$DAEMON_PID" ] && break
	kill -0 "$LAUNCHER_PID" 2>/dev/null || break
	sleep 0.1
done
[ -n "$DAEMON_PID" ] || fail "daemon never reported its PID"

for _ in $(seq 1 150); do
	kill -0 "$DAEMON_PID" 2>/dev/null || break
	sleep 0.1
done
if kill -0 "$DAEMON_PID" 2>/dev/null; then
	fail "daemon still running 15s after a failed login"
fi
wait "$LAUNCHER_PID" 2>/dev/null

grep -q 'ZERO RANDOM FD' "$TMP/err" && fail "lws reported fd 0 as its random fd (stdin was closed)"
grep -q 'Network error:' "$TMP/err" || fail "the startup failure was not shown on stderr"

echo "web_mode_startup: ok"
