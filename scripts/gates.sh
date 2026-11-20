#!/usr/bin/env bash
# Run the pre-push gates concurrently from one tree (`make gates`).
#
# Each gate builds in its own directory (build/<variant>, see the Makefile),
# so they never share objects and no step needs `make clean` first. Docker
# (`make test-ci-local`) builds inside its container and never writes to the
# host tree.
#
# Steps (GATES_STEPS, space-separated, run in this order):
#   docker       make test-ci-local                 (Linux coverage, = CI c-tests)
#   coverage     make test-coverage                 (build/coverage)
#   test-all     make test-all: -O2 tests + lint    (build/default)
#   docker-asan  PIANOBAR_CI_TARGET=test-asan make test-ci-local
#                                                   (Linux ASan + LSan, = CI c-asan)
#   asan         make test-asan                     (build/asan)
#   tsan         make test-tsan                     (build/tsan)
# Every test step runs with PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1.
#
# GATES_JOBS   steps at once (default 3). The playback suites use real-time
#              waits and flake on an oversubscribed host; keep this at 2 or 3.
# GATES_MAKE_JOBS  make -j per step (default ncpu / GATES_JOBS, at least 2)
# GATES_TEST_JOBS  TEST_JOBS (test-parallel.sh workers) per step (default 3)
#
# A failed step is rerun once, alone, after the others finish (the tests are
# load-sensitive). It is reported as "passed on rerun"; a second failure fails
# the gate. Logs: build/gates/<step>.log.
#
# For a patch-coverage number run `make test-coverage` on its own: under gate
# load a few timing-dependent lines (e.g. which side of a race a test lands
# on) can differ from a standalone run.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
unset MAKEFLAGS MFLAGS MAKELEVEL

# Skip what the change cannot affect (scripts/changed-scope.sh; GATES_SCOPE=full
# forces every step). A full pass on a clean tree leaves build/gates/passed
# (the tested commit; not written for a GATES_STEPS subset), which
# .githooks/pre-push trusts for that commit.
STAMP="${ROOT}/build/gates/passed"
rm -f "$STAMP"
SCOPE="$(./scripts/changed-scope.sh)"
case "$SCOPE" in
docs)
	echo "gates: docs-only change, nothing to run (GATES_SCOPE=full to force)"
	exit 0
	;;
web)
	echo "gates: webui-only change, running the web unit tests"
	(cd webui && npm test -- --run)
	exit $?
	;;
esac
clean_tree () { [ -z "$(git status --porcelain -uall)" ]; }
start_head=""
if clean_tree; then
	start_head="$(git rev-parse HEAD)"
fi

ncpu="$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"
STEPS="${GATES_STEPS:-docker coverage test-all docker-asan asan tsan}"
CAP="${GATES_JOBS:-3}"
MJOBS="${GATES_MAKE_JOBS:-$(( ncpu / CAP ))}"
[ "$MJOBS" -lt 2 ] && MJOBS=2
TJOBS="${GATES_TEST_JOBS:-3}"
LOGS="${ROOT}/build/gates"
mkdir -p "$LOGS"
rm -f "$LOGS"/*.log "$LOGS"/*.rc

# PIANOBAR_CI_JOBS: make -j inside the Docker steps (test-ci-local passes
# TEST_JOBS into the container too).
export PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 TEST_JOBS="$TJOBS" PIANOBAR_CI_JOBS="$MJOBS"

step_cmd () {
	case "$1" in
	docker) echo "make test-ci-local" ;;
	docker-asan) echo "env PIANOBAR_CI_TARGET=test-asan make test-ci-local" ;;
	coverage) echo "make -j$MJOBS test-coverage" ;;
	test-all) echo "make -j$MJOBS test-all" ;;
	asan) echo "make -j$MJOBS test-asan" ;;
	tsan) echo "make -j$MJOBS test-tsan" ;;
	*) return 1 ;;
	esac
}

for s in $STEPS; do
	step_cmd "$s" >/dev/null || { echo "gates: unknown step: $s" >&2; exit 2; }
done

now () { date +%s; }

# Shared generated sources first, so concurrent makes never race on them.
make locale-codegen >/dev/null || exit 1

run_step () { # step logsuffix
	local s="$1" log="$LOGS/$1$2.log" start rc
	start=$(now)
	# shellcheck disable=SC2046
	$(step_cmd "$s") >"$log" 2>&1
	rc=$?
	echo "$rc $(( $(now) - start ))" >"$LOGS/$1$2.rc"
	return "$rc"
}

checks_of () {
	grep -aoE '(test-parallel: Checks: [0-9]+|[0-9]+%: Checks: [0-9]+, Failures: [0-9]+, Errors: [0-9]+)' "$1" |
		tail -n 1 | sed 's/^test-parallel: //'
}

echo "gates: steps [$STEPS], $CAP at once, make -j$MJOBS, TEST_JOBS=$TJOBS; logs in build/gates/"
gate_start=$(now)
for s in $STEPS; do
	# bash 3.2 (macOS) has no `wait -n`: poll the running jobs.
	while [ "$(jobs -rp | wc -l | tr -d ' ')" -ge "$CAP" ]; do
		sleep 1
	done
	echo "gates: start $s"
	run_step "$s" "" &
done
wait

failed=""
for s in $STEPS; do
	read -r rc secs <"$LOGS/$s.rc"
	printf 'gates: %-12s exit %-3s %4ss  %s\n' "$s" "$rc" "$secs" "$(checks_of "$LOGS/$s.log")"
	[ "$rc" = 0 ] || failed="$failed $s"
done

final=0
for s in $failed; do
	echo "gates: rerunning $s alone (see build/gates/$s.log for the first failure)"
	run_step "$s" ".rerun"
	read -r rc secs <"$LOGS/$s.rerun.rc"
	if [ "$rc" = 0 ]; then
		printf 'gates: %-12s passed on rerun %4ss  %s\n' "$s" "$secs" "$(checks_of "$LOGS/$s.rerun.log")"
	else
		printf 'gates: %-12s FAILED again (exit %s), log: build/gates/%s.rerun.log\n' "$s" "$rc" "$s"
		tail -n 30 "$LOGS/$s.rerun.log"
		final=1
	fi
done

echo "gates: total $(( $(now) - gate_start ))s"
if [ "$final" -eq 0 ] && [ -z "${GATES_STEPS:-}" ] && [ -n "$start_head" ] && clean_tree &&
		[ "$(git rev-parse HEAD)" = "$start_head" ]; then
	echo "$start_head" >"$STAMP"
fi
[ "$final" -eq 0 ] && echo "gates: all passed" || echo "gates: FAILED"
exit "$final"
