# Build and Test Wall-Clock Reduction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut wall-clock time of `make test`, `make test-all`, `make test-coverage`, `make test-asan`, `make test-tsan`, `make test-ci-local`, the former local gate script (now `make gates`) and the GitHub `c-tests` job, **without losing any test, assertion, coverage line or CI parity**. The measured standard gate (coverage, then `test-all`, then Docker) takes about 7.5 to 11 minutes. The target is about 2 to 2.5 minutes, bound by the Docker step.

**Architecture:** Almost all the time is in three places: one 30 s test that waits on an lws internal timer, a test binary that runs 30 suites strictly serially, and a Docker step that reinstalls apt packages and rebuilds in a bind mount on every run. The C compile is cheap (7 s at `-j10`). So the plan is test-side and script-side only: no `src/**` change, no change to what is asserted. Each task states how to prove the Check counts and lcov line totals are unchanged.

**Tech Stack:** GNU make 3.81 (macOS) and 4.x (Linux), Check 0.15.2, lcov/gcov, cppcheck 2.18, Docker, GitHub Actions.

**Spec:** This document. Measurements were taken in a scratch worktree (since removed) at stack tip `308c9e7`; the raw logs were not kept. The implemented results are in #86 and #87. No source file in the repository was changed.

---

## Measured baseline

Host: Apple Silicon, `hw.ncpu=10`, Apple clang 21 (the Makefile maps `gcc` to it on Darwin), Check 0.15.2, cppcheck 2.18.0, no ccache. The host was **not idle**: other gates, a Docker VM, and at the end ~10 CPU-burning `yes` processes were running (load 4 to 16 for most numbers, 43 and 89 for the ones flagged). Treat values as +/-15%. Environment for test runs: `PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1`, `HOME=$(mktemp -d)`. Check count at this tip: **816** (`100%: Checks: 816, Failures: 0, Errors: 0`), 30 suites.

| Step | Wall | Notes |
|---|---|---|
| Clean `make pianobar_test`, `-j1` | 20.1 s | user 11.8 s, load 6.8 |
| Clean `make pianobar_test`, `-j10` | 7.4 s | floor is `src/miniaudio_impl.o` (5.3 s) |
| Clean `pianobar_test` at `-O0 -g`, `-j1` | 14.3 s | |
| Clean `make pianobar` (binary tests never use), `-j1` | 12.6 s | `main.o` is not linked into `pianobar_test` |
| Sum of per-object compiles (66 objects, `-O2`) | 38.6 s | src 21.6 s, test 17.0 s |
| `./pianobar_test` with env (3 runs) | 64.5, 65.4, 64.7 s | |
| `./pianobar_test` without env (integration skipped) | 51.7 s | what the pre-push hook runs for non-test diffs |
| Sum of Check durations (CK XML) | 61.3 s | **one test is 30.0 s (49%)** |
| `make lint` | 21.0 s | single-threaded cppcheck |
| `make lint-test` | >627 s, aborted | not in any gate; do not add one |
| `make test-all` (`-j1`) | 114.0 s | build + test + lint, serial |
| `make -j10 test-all` | 76.6 s | lint overlaps the tests (load 19, so conservative) |
| `make test-coverage` end to end | ~83 s | build 4.7 + run 72.9 + lcov 5.7 (phased run); the whole-target run read 94.8 s at load 43 |
| `make test-asan` equivalent (`-j10` build) | build 20.4 s, run 72.5 s | |
| `make test-tsan` equivalent (`-j10` build) | build 9.2 s, run 101.6 s | run was partly under load 89 |
| `locale-codegen` | 0.2 s | negligible |
| Web UI `npm test -- --run --coverage` | 11.8 s | 26 files, 384 tests, node v25 here; CI uses Node 18 |
| Docker apt phase of `test-ci-local` | 161 s | `gate-docker-quiet-wt.log` 13:14:55 to 13:17:36; that run stopped right after apt |
| Docker whole step | 268 s and 478 s | from log mtimes of the ws-split gate and the later quiet-wt gate |
| Local gate, standard (cov + test-all + docker) | ~11 min | quiet-wt: 13:36:34 start, coverage 83 s, test-all 103 s, docker 478 s |
| Local gate, "full" (adds asan + tsan) | +~100 s each | ws-split gate, log mtimes |

### Per-suite wall (serial, with env)

`ws_service_edges` 30.3 s, `playback_integration` 14.9 s, `Player` 7.5 s, `ui_act` 3.0 s, `PlaybackManager` 2.5 s, `ui` 1.6 s, `WebSocketEdgeCases` 1.5 s, `WebSocket` 0.9 s, `CliInputPaths` 0.8 s, everything else 0.6 s or less (about 5 s combined for the remaining 21 suites).

### Slowest single tests (Check XML durations)

| s | Suite | Test |
|---|---|---|
| 30.02 | ws_service_edges | `test_flooded_client_receives_close_reason_over_lws` |
| 2.81 | playback_integration | `test_null_audio_repeated_start_stop` |
| 2.36 | PlaybackManager | `test_manager_progress_only_while_active_after_pause_wakes` |
| 2.00 | Player | `test_player_snapshot_reserves_lifetime_and_timeout_does_not_steal` |
| 1.57, 1.51 | playback_integration | `test_player_pause_resume_stops_device_and_preserves_cursor` (plain and null-audio) |
| 1.26 | playback_integration | `test_playback_two_song_manual_advance` |
| 1.23 | WebSocketEdgeCases | `test_ws_oversize_single_frame_is_dropped_not_sliced` |

### Experiments run in the scratch tree

| Experiment | Result |
|---|---|
| Flood test: loop exits on `closeStatus != 0` instead of waiting for `closed` | 30.0 s to 0.30 s, 4/4 checks still pass, reason and status assertions untouched |
| Coverage build, suites in parallel via `CK_RUN_SUITE` + `xargs -P4`, with the flood fix | run 72.9 s to 15.6 s; Checks sum 816; all shards exit 0 |
| Same, `-P10`, original flood test | run 30.9 s (bound by the 30 s test) |
| ASan, `-P4`, original flood test | 72.5 s to 36.5 s, Checks sum 816 |
| TSan, `-P4`, original flood test | 43.4 s, but `playback_integration` failed once: `test_null_audio_concurrent_controls_until_song_end` early exit 1 while host load was 89 from unrelated processes (see Risks) |
| `cppcheck -j4` / `-j10` | 24.4 s to 7.1 / 6.3 s **but exit 1**: inline `staticFunction` suppressions and `--suppress=unusedFunction` become "Unmatched suppression" (strict mode). **Not equivalent. Do not use.** |
| `cppcheck --cppcheck-build-dir` | exit 1, same unmatched-suppression failure. **Not equivalent.** |
| lcov line totals | serial run 1: 10991 of 38491; serial run 2: 10991; parallel original: 10991; parallel with fix: 10991 (3 of 4 runs), 10989 (1 of 4, six `src/ui.c` lines flipped, see Risks) |
| lcov function/branch totals | functions 966 of 2853 in every run; branches 4275 to 4278 of 19014 in all runs, **including two identical serial runs** (4278, 4277) |

---

## Findings

### F1. One test accounts for half of all test time, and it is waiting on an lws timer, not working

`test/unit/test_ws_service_edges.c:139` `test_flooded_client_receives_close_reason_over_lws`. The flood itself takes 0.03 s (`:191-197`). Instrumented timeline (CK_FORK=no, scratch copy only):

- flood ends at t0; the client's `LWS_CALLBACK_WS_PEER_INITIATED_CLOSE` fires within 0.2 s of t0 (0 to 200 ms across runs), with reason `Client event queue full` or `Server event queue full` and status 1008/1011 already captured (`:112-120`);
- `LWS_CALLBACK_CLIENT_CLOSED` (the only thing that sets `c.closed`, `:121-123`) fires 3.0 s later;
- the `lws_service` call that delivers it does not return until t0+30.0 s.

The wait loop is `while (!c.closed)` at `:202-206`. The 20 s guard at `:203-204` cannot fire because the whole delay is inside one `lws_service` call. The tcase timeout is 60 s (`:232`). I did not read lws source, so the reason `lws_service` blocks for the remaining ~27 s is unknown, but the effect is reproducible (3/3 runs, and with `lws_service (cctx, 0)` as well).

Changing the loop condition to `while (!c.closed && c.closeStatus == 0)` makes the test 0.3 s. The assertions that the close frame carries one of the two reasons (`:207-209`) and one of the two statuses (`:210-212`) are unchanged. What is no longer observed is that the client's own `CLIENT_CLOSED` callback fired; `lws_context_destroy (cctx)` at `:215` still tears the client down. The "server really closes the client" behavior is still proven by the received close frame.

### F2. `make` is never run with `-j` anywhere

`grep` for `-j`, `MAKEFLAGS`, `nproc` and `ncpu` over `Makefile`, `build.sh`, `scripts/`, `.github/`, `.githooks/`, `DEVELOPMENT.md`, `README_TESTING.md`, `TESTING.md` and `AGENTS.md` finds nothing. The Makefile is `-j` safe: `l10n_defaults_gen.o` depends on the codegen stamp (`Makefile:238-239`), depfiles use `$(@:.o=.d)` (`:221-231`), and `make -j10 test-coverage` and `make -j10 test-all` both passed. Call sites that run serial: `.githooks/pre-push:21-27` (`make test-all`), `scripts/test-ci-local.sh:34`, `.github/workflows/test.yml:31` (`make test-coverage`) and the former local gate script.

### F3. The 30 suites run one after another in one process, though suites are independent

`test/test_main.c:79-110` builds one `SRunner` and `:112` calls `srunner_run_all (sr, CK_NORMAL)`. Check forks each test (fork mode is the default), but forks run serially. Check already supports `CK_RUN_SUITE` and `CK_RUN_CASE` (used in `README_TESTING.md:142`), so a runner can split by suite with **no C change**. Verified: running every suite as its own process and summing "Checks:" gives 816, identical to the single run. `TESTING.md:68-70` documents `./pianobar_test --suite socketio`, but `main (void)` at `test_main.c:73` ignores arguments, so that doc line is stale.

Suites that must not share a moment with another copy of the same run:

- `Daemon` writes fixed pid files: `test/unit/test_daemon.c:82`, `:104`, `:144` (`/tmp/test_pianobar_daemon_*.pid`). Two simultaneous full runs (two worktrees in the gate, or CI plus a local run on the same host) can collide. Everything else uses `TestTmpMkdtemp` (`test/test_tmp.h`) and `free_loopback_port` (`test/test_helpers.h:33`). The fixed `8080`/`9999` values in `test_websocket.c:1861,1882,1908` are format-string checks of `BarWsPrintWebInfo`, not binds. `test_ui.c:112,138,163` and the `test_ui.c:164` / `test_websocket.c:1396` pid strings appear to be format-only too; I read the first group only.
- `playback_integration` and `Player` use a real-time null audio backend and wall-clock waits (`test_playback_integration.c:740`, `:1390-1399`, `test_player.c:2898`). They are the suites most sensitive to CPU oversubscription (TSan run above).

### F4. The gate runs the same 816 checks three to five times and rebuilds from scratch each time

The former ad-hoc gate script (outside the repo; replaced by `scripts/gates.sh`, `make gates`, #87): coverage run (`:6`), `distclean` + `test-all` (`:9-10`, same 816 checks, different `-O` level and plus lint), optional `distclean` + asan (`:13-14`) and tsan (`:16-18`), then `distclean` + Docker (`:23-24`, the same 816 checks on Linux). Every step is `distclean` first, so every step pays a clean serial build (20 s at `-O2`, `-j1`). Objects are built in-tree (`Makefile:221-249`), so a different flag set cannot coexist with another; `test-coverage: clean` (`:402`), `test-asan: clean clean-test-asan` (`:432`) and `test-tsan: clean clean-test-asan` (`:443`) all wipe the tree.

Coverage and `test-all` run identical tests at different optimization levels (coverage appends `-O0`, `Makefile:404`; `test-all` uses the default `-O2`). CI only ever builds the tests at `-O0`: PRs pass `CFLAGS=-g -O0` (`test.yml:25`), and pushes to `main` get `-O2 ... -O0` where the last flag wins. So the `-O2` pass in `test-all` is local-only safety, not CI parity.

### F5. `make test-ci-local` repeats ~160 s of apt on every run, builds in the host tree, and builds serially

`scripts/test-ci-local.sh:15-27`: `apt-get update` and a 19-package `apt-get install` inside a fresh `ubuntu:24.04` container each run (161 s measured). `:16` bind-mounts the host worktree at `/work`, so the Linux build writes objects, gcda/gcno, `pianobar_test` and `coverage.info` into the host worktree (as they were in the stack worktree at the time; fixed in #86). That is why the gate must `distclean` after Docker, and why Docker cannot run beside a host build in the same tree. The in-container `make test-coverage` (`:34`) is serial though the Docker VM has 10 CPUs. The apt output is redirected to `/dev/null` (`:27`), so an install failure only shows up later as a confusing missing-tool error (the quiet-wt Docker run died on `python3: No such file` after apt).

### F6. CI reinstalls dependencies per job, builds serially, and shares nothing between jobs

- `test.yml:13-19` `c-tests`: apt update + install on every run; `:31` `make test-coverage` serial (4 vCPU runner). No lint, no asan, no tsan in any workflow (`grep` for `cppcheck`/`lint` in `.github` is empty).
- `build.yml:19-22`: a second runner repeats apt for a build that compiles roughly the same objects (plus `main.o` and the link).
- `release.yml:101-107`: apt install, then a second `apt-get update && apt-get install libblas-dev liblapack-dev` at `:108-109`, which the first command already installs (`:105`). `make CFLAGS=... clean && make` at `:128-129`, `:341-342`, `:531-532` are serial.
- `builder-images.yml` already publishes dependency-baked images, but only `linux/arm64` (`:33-40`) and from a dependency list (`.github/docker/arm64-build-deps.txt`) that lacks `lcov` and `cppcheck`, so `c-tests` cannot use it.
- In-flight, not at this tip: branch `test/fake-pandora-e2e` (`6836ad2`) adds an `e2e-fake` job (`test.yml` +30 lines). It does its own apt, `npx playwright install --with-deps chromium`, and `make test-e2e-fake: pianobar`, i.e. a third full build of `pianobar`. `c-tests` and `web-tests` already run in parallel as separate jobs; only `e2e-fake` could reuse an artifact.

### F7. Smaller costs

- `src/miniaudio_impl.o` is 5.3 s of the 7.4 s `-j10` clean build (`Makefile:234-236`; vendored, never changes). `src/bar_state.o` is 1.9 s. No ccache installed locally or in CI.
- `test-coverage` runs three `lcov` invocations (`Makefile:408-413`): capture 4.2 s, then two `--remove` passes 1.5 s together. One `--remove` with all three patterns is equivalent. No HTML is generated, so nothing to cut there.
- `test-coverage: clean` (`Makefile:402`) does not delete `*.gcda`/`*.gcno` (only `distclean` does, `:263-265`). A repeat run without `distclean` merges stale counters, which is why the gate calls `distclean`. A coverage-only target that clears gcda would remove that hazard.
- `clean-test-asan` (`Makefile:438-439`) is redundant: `clean` already removes `TEST_OBJ` and `TEST_BIN` (`:253-259`).
- `make test-all` is `test lint` (`:398`), run strictly in that order. Lint is 21 s and independent of the tests.
- Residual real-time waits in tests: `test_playback_manager.c:543` `usleep (1200000)` (a negative window that proves no event is sent), the null-audio start/stop delay table (`test_playback_integration.c:1390`, about 1 s per pass, two passes), `test_player.c:2898` (5 x 100 ms), `test_playback_integration.c:740` (500 ms hold). Together under 10 s. These encode production timeouts; shortening them needs injectable timeouts and is deferred.
- Flake seen once in 6 runs (not caused by this work): `test_state_edge_paths.c:154-170` `test_daemon_lock_path_falls_back_to_passwd_home` failed with `want == "(null)/..."`. Likely cause (not confirmed): `pw` from `getpwuid` points at static storage that `BarDaemonGetLockFilePath` (which presumably calls `getpwuid` again) can overwrite. Copy `pw->pw_dir` before the call. Worth fixing before parallel runs, since it adds noise to the gate.

---

## Ranked tasks

Estimates are for the standard gate unless stated. "Saves" is wall-clock. Risk is to test fidelity, not to the schedule.

### Quick wins (about half a day, all test-side or script-side)

#### Task 1: Stop waiting 30 s in the flood test

**Saves:** 30 s per test run. The standard gate runs the suite 3 times (coverage, `test-all`, Docker): about 90 s. The full gate adds ASan and TSan: about 150 s. The pre-push hook (no env) goes from 52 s to 22 s of test time.
**Risk:** Low. Test-only, one condition. **Changes CI parity/coverage:** No (lcov totals unchanged in 3 of 4 runs, see Risks).

**Files:** `test/unit/test_ws_service_edges.c:202`

- [ ] Change `while (!c.closed) {` to `while (!c.closed && c.closeStatus == 0) {`, and reword the "client was never closed" message (`:204`) to "client never received a close frame". Keep the 20 s guard and every `ck_assert` after the loop. Reword the comment above the test (`:136-138`) to say the close frame, not the socket teardown, is what is awaited.
- [ ] Optionally lower the tcase timeout at `:232` from 60 to 30. Do not do this in the same change as the loop edit if the CI runners are slow.
- [ ] Verify: `CK_RUN_SUITE=ws_service_edges ./pianobar_test` reports `Checks: 4` and runs in under 1 s, five times in a row. `make test-all` (with env) still reports `Checks: 816`.
- [ ] Verify coverage: compare lcov line totals before and after (see Verification).

#### Task 2: Use `-j` for builds and overlap lint with tests

**Saves:** clean `-O2` build 20.1 s to 7.4 s per rebuild; `make test-all` 114 s to 77 s (measured at load 19). With Task 3, lint (21 s) hides entirely behind the tests.
**Risk:** Low. **Changes CI parity/coverage:** No.

**Files:** `.githooks/pre-push`, `scripts/test-ci-local.sh`, `.github/workflows/test.yml`, `.github/workflows/build.yml`, `.github/workflows/release.yml`, `AGENTS.md`/`DEVELOPMENT.md` command tables.

- [ ] Add a portable job count to the hook and scripts, e.g. `JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu)}"`, and run `make -j"$JOBS" test-all` in `.githooks/pre-push:21` and `:27`.
- [ ] In CI use `make -j"$(nproc)" test-coverage` (`test.yml:31`), `make -j"$(nproc)"` (`build.yml:27,30`). `test-coverage` already recurses with `${MAKE}`, so the flag is inherited.
- [ ] Do not change the Makefile default; `-j` stays opt-in so `make` on a laptop stays predictable.
- [ ] Note in `DEVELOPMENT.md` that `make -jN test-all` interleaves test and lint output.
- [ ] Remove the redundant second `apt-get update` step in `release.yml:108-109` (the packages are already in the list at `:105`).
- [ ] Verify: `make distclean && make -j10 pianobar_test` and `make -j10 test-all` exit 0 with `Checks: 816`.

#### Task 3: Parallel suite runner (new `scripts/test-parallel.sh`, `make test-par`)

**Saves:** test phase 61 to 65 s down to 15.6 s with 4 workers and Task 1 (measured, coverage build). ASan: 72.5 s to about 36 s measured with the flood test unfixed; about 12 to 15 s with Task 1. With the integration suite split by tcase (below) the floor is about 8 s (estimated from per-test durations, not measured).
**Risk:** Medium. Timing-sensitive suites can flake when the host is oversubscribed. **Changes CI parity/coverage:** Mechanically no (same binary, same tests, same env), but it changes how CI executes tests, so see Risks and the verification gate.

**Files:** `scripts/test-parallel.sh` (new), `Makefile` (new `test-par`; `test-coverage`, `test-asan`, `test-tsan` call it), `.github/workflows/test.yml`, `scripts/test-ci-local.sh`.

- [ ] Script behavior: build suite list by `grep -ho 'suite_create *("[^"]*")' test/ -r`, longest-first order (`playback_integration`, `Player`, `PlaybackManager`, `ui_act`, `ui`, ...), run each as `CK_RUN_SUITE="<name>" ./pianobar_test` under `xargs -P "${TEST_JOBS:-4}"` writing to per-suite temp files, print each log when it finishes, and exit non-zero if any shard exits non-zero.
- [ ] Completeness guards (so a shard can never silently drop tests): fail if any shard prints `Checks: 0` or no `Checks:` line; print the summed `Checks`; fail if a `suite_create` name in `test/` matches no shard. Make `CK_RUN_SUITE` runs fail on unknown names rather than run zero tests.
- [ ] Split `playback_integration` further with `CK_RUN_CASE`: tcases `core`, `stream_shutdown`, `null_audio_device` (`test_playback_integration.c:1485-1521`). That suite is 14.9 s alone and is the critical path once the 30 s test is gone.
- [ ] Put the `Daemon` suite's pid paths under `TestTmpMkdtemp` (`test_daemon.c:82,104,144`) so two full runs cannot collide (needed by Task 5).
- [ ] Cap workers at `min(4, ncpu/2)` by default; allow `TEST_JOBS=1` to reproduce the serial run exactly. Keep plain `./pianobar_test` and `make test` serial and unchanged.
- [ ] Coverage under parallel: gcov already merges counters at process exit (builds use `-fprofile-update=atomic`, `Makefile:404`). Measured: lcov line totals equal to the serial run.
- [ ] Update `TESTING.md:68-70` (the stale `--suite` flag) to the real `CK_RUN_SUITE` usage and document `make test-par`.
- [ ] Verify: summed `Checks` = 816, every shard exit 0, lcov totals match (see Verification), repeated 5 times on an otherwise idle host.

#### Task 4: Docker: prebuilt image, copy-in workdir, parallel build

**Saves:** about 160 s of apt per run, plus the serial in-container build and the host `distclean`. Docker step 270 to 480 s becomes about 60 to 90 s (estimate: build ~8 s + tests ~20 s + lcov + container start; not measured because Docker must not be run for this plan).
**Risk:** Medium-low. **Changes CI parity/coverage:** Keeps the same base image and package list, so toolchain parity is preserved if the image is built from the same list. A different cached lcov/gcc version would move coverage numbers, so rebuild the image on a schedule (Task 7).

**Files:** `scripts/test-ci-local.sh`, new `.github/docker/Dockerfile.ci-test` (or reuse the `Dockerfile.arm64-build` pattern with a `deps` file), `Makefile` (optional `test-ci-local-image` target).

- [ ] Add a `docker build` step with a stable tag, e.g. `pianobar-ci:ubuntu24.04`, that installs the package list once (the same list `scripts/test-ci-local.sh:23-27` uses, plus `cppcheck` if lint is wanted in the container). Docker's layer cache makes later runs near-instant; rebuild when the dependency file changes.
- [ ] Stop printing apt output to `/dev/null` in that build so failures are visible.
- [ ] Mount the repo read-only (`-v "$ROOT:/src:ro"`) and copy it to a container-local directory before building (`git ls-files -z | tar --null -T - -cf - | tar -x -C /build`, or `cp -a`), so the host tree is never written and no objects built for Linux land in the macOS worktree. Copy `coverage.info` out with a second mount only if it is needed.
- [ ] Run `make -j"$(nproc)" test-coverage` (or `make test-par`) in the container.
- [ ] Verify: Docker exit 0, `Checks: 815` or `816` as before, and `git status` plus `ls coverage.info *.o` on the host are untouched after the run. Then drop the `distclean` after Docker in the gate.

#### Task 5: Restructure the local gate to run variants in parallel copies

**Saves:** wall time becomes the longest step instead of the sum. With Tasks 1 to 4: coverage ~27 s (build 4.7 + run 15.6 + lcov 5.7), `test-all` ~30 s (build 7 + lint overlapped), ASan ~35 s, TSan ~35 s, Docker ~60 to 90 s. Run concurrently, the gate is bound by Docker, about 2 to 2.5 minutes. Run with a cap of 2 or 3 heavy steps at once (see Risks).
**Risk:** Medium (host CPU contention against timing-sensitive tests). **Changes CI parity/coverage:** No; the same targets run, each in its own tree.

**Files:** `scripts/gates.sh` (`make gates`; done in #87).

- [ ] Give each variant its own tree (`git worktree add`, or `rsync -a --exclude '*.o' --exclude '*.d' --exclude '*.gc*'` of the tree) so no step needs `make distclean` first. That removes 4 to 6 clean rebuilds from the gate.
- [ ] Order: start Docker first in the background (it dominates and, after Task 4, never touches the host tree), run coverage and `test-all` next, and ASan/TSan after, capped at 2 to 3 concurrent heavy steps.
- [ ] Keep `test-all` (`-O2` + lint) in the gate; it is the only place `-O2` tests and lint run. Add an opt-in `GATE_FAST=1` that runs lint only for the second pass if a push does not touch `src/**`.
- [ ] Verify: the gate's recorded `Checks:` lines match the baseline for each step (816 host, 815 Docker), exit codes zero, `patchcov` output identical.

### Larger changes

#### Task 6: CI: dependencies from a prebuilt image or cache, `-j`, and shared artifacts

**Saves:** an estimated 60 to 120 s per CI job on apt (not measured; GitHub runners were not available), plus 10 to 20 s of build from `-j$(nproc)`, plus the test-phase cut from Tasks 1 and 3. `build.yml` and `e2e-fake` can stop installing dependencies they have no use for.
**Risk:** Medium. **Changes CI parity/coverage: YES.** Moving `c-tests` into a container, or caching apt packages, changes where gcc, lcov and libraries come from. Take the Codecov patch number from before and after on the same PR before accepting.

**Files:** `.github/workflows/test.yml`, `.github/workflows/build.yml`, `.github/workflows/builder-images.yml`, `.github/docker/*`.

- [ ] Option A (preferred): extend `builder-images.yml` to also build a `linux/amd64` test image (native, no QEMU, fast) from a deps file that adds `lcov`; run `c-tests` with `container:` set to it. This reuses the single-source-of-truth pattern already in `.github/docker/arm64-build-deps.txt`.
- [ ] Option B: keep `ubuntu-latest` and cache the apt `.deb` set with an apt-cache action keyed on the package list. Smaller change, smaller win.
- [ ] Have `build.yml` upload the `pianobar` binary as an artifact and have the in-flight `e2e-fake` job download it instead of running `make test-e2e-fake: pianobar` again. (Do this when `test/fake-pandora-e2e` lands.)
- [ ] `e2e-fake`: cache `~/.cache/ms-playwright`, and install only the Chromium browser and its deps (`playwright install --with-deps chromium` already does this). The `playwright.config.ts` has firefox and webkit projects; if the CI job passes `--project=chromium` it avoids unused work.
- [ ] Release workflow: the `make clean && make` pairs at `release.yml:128-129`, `:341-342`, `:531-532` already start from a fresh checkout; drop `clean`, add `-j`.
- [ ] Verify: Codecov `c-tests` patch number on a reference PR unchanged to within the known flake (see Verification); `make test-coverage` green; job duration recorded before and after.

#### Task 7: Compiler cache and build-variant directories

**Saves:** ccache removes most of the 5.3 s `miniaudio_impl.o` from every clean non-coverage build (about 4 to 5 s per build across `test-all`, ASan, TSan and `pianobar`). A `BUILD_DIR` per variant (Makefile pattern rules `${BUILD_DIR}/%.o: %.c`) removes the need for `distclean` between variants and lets a developer keep coverage, ASan and plain builds side by side.
**Risk:** Medium for the Makefile restructure; ccache with `--coverage` is not safe to assume (gcno/gcda path handling), so use ccache only for non-coverage builds. **Changes CI parity/coverage:** Variant directories do not change what is built. Anything touching coverage objects can change lcov paths; check `coverage.info` `SF:` paths and the `*/test/*` and `*l10n_defaults_gen.c` removals before accepting.

**Files:** `Makefile` (all `%.o`, `-include` and `clean` rules), `DEVELOPMENT.md`.

- [ ] Install ccache locally (not installed now) and run non-coverage builds through `CC="ccache gcc"`; leave coverage builds uncached.
- [ ] The `BUILD_DIR` restructure is now its own task (Task 8). Do ccache separately from it.

#### Task 8: Out-of-tree, per-variant build directories (added 2026-11-19)

**Why:** every test variant builds its `.o`, `.gcno` and `.gcda` files in the source tree, and `test-coverage`, `test-asan` and `test-tsan` each start with `make clean`. So the gates cannot run at the same time in one checkout, every switch between variants is a full rebuild, and `make test-ci-local` (bind mount) leaves Linux objects and `.gcda` files in the macOS tree. That last one broke `lcov` in two agent runs on 2026-11-19 until `make coverage-clean` was run. Per-variant object directories let the gates run in parallel from a single tree without worktrees or copies, and make Task 5 a plain `make -j` target instead of a script that manages trees.

**Saves:** every `make clean`/`distclean` between variants (one full rebuild per variant switch, about 7 s at `-j10` and more for coverage and sanitizer builds). Incremental rebuilds when switching back to a variant. It also makes the parallel gate possible from one tree, bound by its slowest step, the same as Task 5.
**Risk:** Medium. It touches every object, dependency and clean rule, plus everything that expects `./pianobar` or `*.o` at the top level. **Changes CI parity/coverage:** it doesn't change what is compiled, but coverage paths can move. Check `coverage.info` `SF:` paths and the lcov `--remove` patterns before accepting.

**Files:** `Makefile` (object, dependency, link, `clean`/`distclean`/`coverage-clean`, `test-*` rules), `.gitignore`, `scripts/test-ci-local.sh`, `.githooks/pre-push`, `build.sh`, `.github/workflows/*.yml` (anything that runs `./pianobar` or `./pianobar_test`, or globs `*.o`/`*.gc*`), `debian/` and macOS packaging scripts if they reference build outputs, `DEVELOPMENT.md`, `README_TESTING.md`, `AGENTS.md`.

- [ ] **Layout:** `BUILD_DIR ?= build/default`, and each variant target sets its own directory:

  | Target | Directory |
  |---|---|
  | `test-coverage` | `build/coverage` |
  | `test-asan` | `build/asan` |
  | `test-tsan` | `build/tsan` |
  | `minimal` | `build/minimal` |
  | Docker CI | `build/linux-ci` |

  Use pattern rules `$(BUILD_DIR)/%.o: %.c` with `@mkdir -p $(@D)`, and put `.d` dependency files beside the objects. Must work on GNU make 3.81 (macOS) and 4.x.
- [ ] **Outputs:** binaries go in `$(BUILD_DIR)/`. Keep a top-level `./pianobar`, copied or symlinked from `build/default`, so `build.sh`, `make install`, packaging and the docs keep working. Test binaries and `coverage.info` stay per variant: `build/coverage/coverage.info`, copied to the top level only where CI and Codecov expect it.
- [ ] **Drop the `clean` prerequisite** from `test-coverage`, `test-asan` and `test-tsan`. `clean` removes `build/` plus any stray top-level objects left over from the old layout.
- [ ] **Generated sources** (`src/l10n_defaults_gen.c`, `locale/<lang>`, `webui/src/locales/*.json`) are shared across variants. Generate them once per tree, from one `locale-codegen` prerequisite. Concurrent `make` invocations must not race on them: either a single `make -j gates` parent target, or codegen first and then the variants.
- [ ] **Coverage:**
  - run lcov with `--directory build/coverage --base-directory .` so `SF:` paths still read `src/...`;
  - keep `-fprofile-update=atomic`;
  - check that the `*/test/*` and `*l10n_defaults_gen.c` removals still match;
  - check that Codecov maps the paths: same patch % on a reference PR, before and after.
- [ ] **Docker:** `scripts/test-ci-local.sh` builds into `build/linux-ci`, so host and container objects never mix. With Task 4's copy-in workdir it doesn't write to the host tree at all.
- [ ] **Tests that write into the tree:** find any test or fixture that writes to a fixed path in the tree or in `/tmp`. The `Daemon` pid files are already fixed via `TestTmpMkdtemp`. Two variants running at once must not share such a path.
- [ ] **Parallel gate:** add `make gates` (or rework Task 5) to run `test-all`, `test-asan`, `test-tsan`, `test-coverage` and Docker at the same time from one tree. Follow Task 5's caps: 2 to 3 heavy steps at once, and rerun a failed load-sensitive step alone once before reporting it as failed. Then point AGENTS.md's pre-push section at it.
- [ ] **Verify:**
  - each variant's `Checks:` count matches the baseline;
  - lcov line and function totals are identical to the baseline (Verification §3);
  - `git status` is clean after every target;
  - running `make test-coverage` and then `make test-asan` keeps both directories intact;
  - `build.sh`, `make install`, `release.yml` (dispatch a dev tag) and `build.yml` still produce the same packages;
  - time `make gates` against the sequential gate (median of 3, idle host).

### Cheap cleanups (bundle with Task 2 or 3)

- [ ] Merge the two `lcov --remove` calls (`Makefile:410-413`) into one with all three patterns. Saves about 1.5 s; verify `coverage.info` is byte-identical.
- [ ] Delete `clean-test-asan` from the prerequisites of `test-asan`/`test-tsan` (`Makefile:432,443`), or leave the target and drop the dependency. Pure cleanup.
- [ ] Fix `test_state_edge_paths.c:154-170` (`pw->pw_dir` copy) before enabling Task 3 by default.
- [ ] Update `README_TESTING.md:36-41`, which says `test-all` also runs ASan; it runs `test lint` (`Makefile:398`).

### Deferred

- **Injectable audio-command and progress timeouts** in `test_player.c:487` (2.0 s), `test_playback_manager.c:543` (1.2 s), and similar: roughly 5 to 8 s total, but touches production time constants (`src/**`, 90% patch-coverage gate) and the real timeout behavior is what those tests protect.
- **Parallel cppcheck / cppcheck cache:** measured not equivalent (F-table above). Only overlap lint with tests (Task 2).

---

## Expected totals (estimates built from the measured pieces)

| | Today | After Tasks 1 to 5 |
|---|---|---|
| `make test` (no env, pre-push for non-test diffs) | 52 s test + 21 s lint + 20 s build = ~95 s | ~12 s test + build 7 s, lint hidden = ~25 s |
| Host `make test-coverage` | ~83 s | ~27 s |
| Host `make test-all` | ~103 to 114 s | ~30 s |
| `make test-asan` + `make test-tsan` | ~100 s each | ~35 s each |
| `make test-ci-local` | 270 to 480 s | ~60 to 90 s (not measured) |
| Standard gate | ~11 min sequential | ~2 to 2.5 min with Docker started first |
| `c-tests` CI job | apt + serial build + 73 s tests | apt cut (Task 6) + `-j` + ~16 s tests |

---

## Verification (use for every task)

Take the **baseline on the same environment as the check** (Linux numbers differ from the macOS numbers below because gcc and clang write different gcov data). Record before the first change and after each task.

1. **Check counts.** `PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 ./pianobar_test | grep -a 'Checks:'` must still read `Checks: 816, Failures: 0, Errors: 0` on the host (815 in the Docker gate step; the reason for the one-check difference was not investigated). With the parallel runner, the sum of every shard's `Checks:` must equal that number (per-suite counts at this tip: `ui_act` 116, `WebSocket` 94, `Socket.IO Protocol` 85, `Player` 84, `BarState` 52, `ws_outbox` 38, `Settings` 38, `PlaybackManager` 38, `playback_integration` 29, `WebSocket error messages` 19, `playback_lifecycle` 19, `station_sort` 18, `CliInputPaths` 17, `ui` 16, `l10n` 16, `ws_outbox_faults` 15, `ws_commands` 15, `ui_readline` 14, `Daemon` 14, `WebSocketEdgeCases` 12, `Log` 12, `WsFailurePaths` 11, `HTTP Server` 9, `SocketIoStationFailures` 8, `StateEdgePaths` 6, `ui_act_station` 5, `system_volume` 5, `libpiano_response` 5, `ws_service_edges` 4, `interrupt` 2).
2. **Same tests ran, not only the same count.** Compare `CK_XML_LOG_FILE_NAME` test ids before and after (816 ids, 815 success + the known flake). Task 1 changes durations only.
3. **lcov totals** (macOS, this tip, measured): `lcov --summary coverage.info --rc lcov_branch_coverage=1 --ignore-errors inconsistent,unused` gives `lines 10991 of 38491`, `functions 966 of 2853`, `branches 4275-4278 of 19014`. Lines and functions must match exactly. Branches vary by up to 3 even between two identical serial runs, so accept a 5-branch band. For a per-line check, diff the set of `DA:<line>,<hits>` records with hit>0 per file against the baseline `coverage.info`.
4. **Known coverage flake:** one of four parallel-with-fix runs flipped six `src/ui.c` lines (224, 307, 308, 319, 320, 322, the libcurl retry-on-error path in `BarUiPianoCall`). Before judging a regression, rerun twice; flag only lines that stay different. Do not treat a one-off `ui.c` difference as a lost test.
5. **Patch coverage tooling** (the local branch-aware patch-coverage script, not in the repo) output unchanged for the same diff.
6. **Idle-host rule for timing claims.** Re-measure on an idle machine and with `nice` for other work; compare medians of 3 runs.
7. **CI-parity items** (Tasks 3 in CI, 6, 7): run `make test-ci-local` before and after; for CI, one reference PR before and after with Codecov `c-tests` patch % and job duration.

## Risks and things that could change numbers

- **Timing-sensitive suites under CPU oversubscription.** In the TSan parallel run, `playback_integration` failed (`test_null_audio_concurrent_controls_until_song_end`, early exit 1) while the host load was 89 from unrelated `yes` processes; the serial TSan run passed. Parallel runs raise the load the tests see. Keep default workers at `min(4, ncpu/2)`, cap concurrent gate steps at 2 to 3, and never run `playback_integration` next to a full second copy of the suite. If it flakes, run it first and alone.
- **Fixed `/tmp` pid paths in the `Daemon` suite** (`test_daemon.c:82,104,144`) make two simultaneous full runs on one host unsafe until Task 3 moves them under `TestTmpMkdtemp`.
- **Lint semantics.** `cppcheck -j` and `--cppcheck-build-dir` both turned a passing lint (exit 0) into exit 1. Do not use them.
- **CI parity:** Task 6 (container or apt cache) and anything that changes `test-coverage` (Task 3 inside it, Task 7 coverage objects) can shift Codecov numbers. Task 4 keeps `ubuntu:24.04` but changes how it is provisioned.
- **`-O2` safety net:** CI never runs tests at `-O2`; local `test-all` does. Task 5 keeps it. Removing it would weaken only the local gate.
- **Coverage merge across processes** relies on gcov's exit-time merge and `-fprofile-update=atomic`; measured equal on clang/macOS. Re-measure on Linux gcc in Docker before enabling it in CI.

## Open questions

- Why does `lws_service` block ~27 s after the client's `CLIENT_CLOSED` callback in the flood test? It may indicate a real 30 s linger or poll timeout in the server's close path (`src/websocket/core/websocket.c`). Task 1 does not depend on the answer, but it is worth a separate look because it could be a production-visible delay on slow clients.
- Why does the Docker gate step report `Checks: 815` where the host reports 816? Not investigated (it may be an intentional platform skip).
- Should lint also run in CI? It is not run in any workflow today; adding it would add 21 s to a parallel job and would be a coverage-of-checks change, not a time reduction.
- Web UI unit tests (12 s) and the in-flight Playwright job were not profiled beyond a single local Vitest run.

## Execution order

1. Tasks 1 and 2 (minutes of work, about 120 s off the standard gate), plus the `getpwuid` and `README_TESTING.md` cleanups.
2. Task 3 (the largest test-time win after Task 1) with Task 3's `Daemon` fix.
3. Task 4 (largest gate-time win), then Task 5.
4. Tasks 6 and 7 only after the above are measured and merged.
5. Task 8 (per-variant build directories). If it lands before Task 5, Task 5 becomes `make gates` from one tree instead of a script that manages worktrees.
