#!/usr/bin/env bash
# Run the Check test binary one suite per process, several at a time.
#
# usage: scripts/test-parallel.sh [path/to/pianobar_test]
#
# Every suite_create("...") name under test/ becomes one shard, run as
# CK_RUN_SUITE="<name>" <binary>. Suites listed in TEST_SPLIT_SUITES are split
# further into one shard per tcase_create("...") name (CK_RUN_CASE). The
# environment (PIANOBAR_INTEGRATION, ASAN_OPTIONS, ...) is passed through.
#
# Completeness guards, so a shard can never silently drop tests:
#   - every suite_create/tcase_create argument must be a string literal;
#   - a shard fails if it exits non-zero, prints no "Checks:" line or
#     reports "Checks: 0" (pianobar_test also exits non-zero when
#     CK_RUN_SUITE/CK_RUN_CASE match nothing);
#   - the summed Checks count is printed for comparison with a serial run.
#
# TEST_JOBS     worker count; default min(4, ncpu/2), at least 1.
#               TEST_JOBS=1 runs the binary once, unsharded: exactly the
#               serial `./pianobar_test` run.
# TEST_SPLIT_SUITES  suites to split by tcase (default: playback_integration)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SELF="${ROOT}/scripts/$(basename "$0")"

# Worker mode: run one shard, record its result, print its log in one piece.
if [ "${1:-}" = "--shard" ]; then
	bin="$2"
	out="$3"
	shard="$4"
	suite="${shard%%	*}"
	tcase=""
	case "$shard" in
	*"	"*) tcase="${shard#*	}" ;;
	esac
	id="$(printf '%s_%s' "$suite" "$tcase" | tr -c 'A-Za-z0-9_\n' '_')"
	log="${out}/${id}.log"
	start=$(date +%s)
	rc=0
	if [ -n "$tcase" ]; then
		CK_RUN_SUITE="$suite" CK_RUN_CASE="$tcase" "$bin" >"$log" 2>&1 || rc=$?
	else
		(unset CK_RUN_CASE; CK_RUN_SUITE="$suite" "$bin") >"$log" 2>&1 || rc=$?
	fi
	secs=$(( $(date +%s) - start ))
	checks="$(grep -aoE 'Checks: [0-9]+' "$log" | tail -n 1 | sed 's/Checks: //' || true)"
	label="$suite${tcase:+ / $tcase}"
	printf '%s\t%s\t%s\t%s\n' "$rc" "${checks:-none}" "$secs" "$label" >"${out}/${id}.result"
	# Serialise log output between workers (mkdir is atomic).
	while ! mkdir "${out}/.lock" 2>/dev/null; do
		sleep 0.05
	done
	printf '=== %s (exit %s, %ss)\n' "$label" "$rc" "$secs"
	cat "$log"
	rmdir "${out}/.lock"
	exit 0
fi

BIN="${1:-./pianobar_test}"
case "$BIN" in
/*) ;;
*) BIN="$(pwd)/${BIN#./}" ;;
esac
if [ ! -x "$BIN" ]; then
	echo "test-parallel: test binary not found: $BIN" >&2
	exit 2
fi

ncpu="$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"
default_jobs=$(( ncpu / 2 ))
[ "$default_jobs" -gt 4 ] && default_jobs=4
[ "$default_jobs" -lt 1 ] && default_jobs=1
JOBS="${TEST_JOBS:-$default_jobs}"
case "$JOBS" in
''|*[!0-9]*|0) echo "test-parallel: TEST_JOBS must be a positive integer" >&2; exit 2 ;;
esac

if [ "$JOBS" -eq 1 ]; then
	echo "test-parallel: TEST_JOBS=1, running $BIN serially"
	exec "$BIN"
fi

cd "$ROOT"
SPLIT="${TEST_SPLIT_SUITES:-playback_integration}"

# Every suite_create/tcase_create must take a string literal, or the shard
# list below could miss a suite.
nonliteral="$(grep -rnE '(suite|tcase)_create *\(' test --include='*.c' |
	grep -vE '(suite|tcase)_create *\( *"[^"]+" *\)' || true)"
if [ -n "$nonliteral" ]; then
	echo "test-parallel: suite_create/tcase_create without a string literal:" >&2
	echo "$nonliteral" >&2
	exit 2
fi

suites="$(grep -rhoE 'suite_create *\( *"[^"]+"' test --include='*.c' |
	sed -E 's/^suite_create *\( *"//; s/"$//' | sort -u)"
if [ -z "$suites" ]; then
	echo "test-parallel: no suite_create names found under test/" >&2
	exit 2
fi

OUT="$(mktemp -d "${TMPDIR:-/tmp}/pianobar-test-par.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
shards="${OUT}/shards"
: >"$shards"

# Longest suites first so the slowest shard starts at once; the rest follow
# in name order.
SLOW_FIRST="playback_integration Player PlaybackManager ui_act ui WebSocket WebSocketEdgeCases CliInputPaths ws_outbox"
ordered="$( (for s in $SLOW_FIRST; do
		printf '%s\n' "$suites" | grep -qxF "$s" && printf '%s\n' "$s"
	done
	printf '%s\n' "$suites" | grep -vxF -f <(printf '%s\n' $SLOW_FIRST)) || true)"

while IFS= read -r suite; do
	[ -n "$suite" ] || continue
	split=0
	for s in $SPLIT; do
		[ "$s" = "$suite" ] && split=1
	done
	if [ "$split" -eq 1 ]; then
		files="$(grep -rlE "suite_create *\\( *\"${suite}\" *\\)" test --include='*.c')"
		if [ "$(printf '%s\n' "$files" | wc -l | tr -d ' ')" -ne 1 ]; then
			echo "test-parallel: suite \"$suite\" must be created in exactly one file to split it" >&2
			exit 2
		fi
		cases="$(grep -hoE 'tcase_create *\( *"[^"]+"' $files |
			sed -E 's/^tcase_create *\( *"//; s/"$//')"
		if [ -z "$cases" ] || [ "$(printf '%s\n' "$cases" | sort | uniq -d)" != "" ]; then
			echo "test-parallel: suite \"$suite\" needs unique tcase_create names to split" >&2
			exit 2
		fi
		while IFS= read -r c; do
			printf '%s\t%s\n' "$suite" "$c" >>"$shards"
		done <<<"$cases"
	else
		printf '%s\n' "$suite" >>"$shards"
	fi
done <<<"$ordered"

nshards="$(wc -l <"$shards" | tr -d ' ')"
echo "test-parallel: ${nshards} shards, ${JOBS} workers, binary ${BIN}"
start=$(date +%s)
tr '\n' '\0' <"$shards" |
	xargs -0 -n 1 -P "$JOBS" "$SELF" --shard "$BIN" "$OUT" || true

total=0
failed=0
nresults=0
for r in "$OUT"/*.result; do
	[ -e "$r" ] || continue
	nresults=$((nresults + 1))
	IFS='	' read -r rc checks secs label <"$r"
	if [ "$rc" != 0 ]; then
		echo "test-parallel: FAIL ${label}: exit ${rc}" >&2
		failed=1
	elif [ "$checks" = none ] || [ "$checks" = 0 ]; then
		echo "test-parallel: FAIL ${label}: no checks ran (Checks: ${checks})" >&2
		failed=1
	fi
	case "$checks" in
	''|*[!0-9]*) ;;
	*) total=$((total + checks)) ;;
	esac
done
if [ "$nresults" -ne "$nshards" ]; then
	echo "test-parallel: FAIL only ${nresults} of ${nshards} shards reported a result" >&2
	failed=1
fi

echo "test-parallel: Checks: ${total} in ${nshards} shards, $(( $(date +%s) - start ))s"
if [ "$failed" -ne 0 ]; then
	echo "test-parallel: FAILED" >&2
	exit 1
fi
echo "test-parallel: all shards passed"
