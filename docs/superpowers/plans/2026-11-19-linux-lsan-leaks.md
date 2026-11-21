# Linux LeakSanitizer Leaks in `make test-asan` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `make test-asan` exits 0 on Linux with LeakSanitizer enabled (`ASAN_OPTIONS=detect_leaks=1`, `PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1`), then runs in CI so new leaks fail the PR.

**Architecture:** Every leak found is in test code: fixtures that never free what they own, and calls that drop a returned or borrowed reference. No `src/**` change is needed. One new regression test guards the production path that was suspected (Piano handle reset on every disconnect/reconnect).

**Tech Stack:** C (GNU style), Check, GCC ASan/UBSan/LSan on Ubuntu 24.04, json-c, libgcrypt.

**Spec:** This document; the investigation findings are below.

---

## Investigation findings (read first)

### Reproduction

- Code: branch tip `a596303` (PR #71 of the stacked series), copied to a scratch directory and built in a container. Fixed in #85.
- Container: `ubuntu:24.04` with the same apt package list as `scripts/test-ci-local.sh` (GCC 13, libgcrypt 1.10, json-c 0.17, lws 4.3.3). Isolated `HOME=/tmp/h`.
- Command: `make distclean && make test-asan` with `PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 ASAN_OPTIONS=detect_leaks=1`.
- Fork mode (the default): `96%: Checks: 815, Failures: 0, Errors: 28`, then `make: *** [Makefile:436: test-asan] Error 1`. Each leaking test's forked child prints an LSan report and exits 1, which Check records as `(after this point) Early exit with return value 1`. So fork mode already ties each leak to one test. Total: 28 `SUMMARY` lines, **437,322 bytes**.
- `CK_FORK=no` cross-check: `100%: Checks: 815, Failures: 0, Errors: 0`, then a single report at process exit, `435,082 byte(s) leaked in 194 allocation(s)`, exit 1. It shows the same allocation sites. The difference in bytes comes only from the racy playlist test (group B).
- To check one area: `CK_RUN_SUITE=ui_act`, `CK_RUN_SUITE=CliInputPaths`, `CK_RUN_SUITE=playback_integration`, `CK_RUN_SUITE=BarState CK_RUN_CASE=Playlist`, `CK_RUN_SUITE="Socket.IO Protocol"`, `CK_RUN_SUITE=WebSocket CK_RUN_CASE=wire_compat`.
- `make test-asan` sets no `ASAN_OPTIONS`. On Linux x86_64, GCC's ASan turns LSan on by default, so **`make test-asan` already fails on Linux today**. On macOS LSan is inactive, which is why the leaks were never seen.

### The suspected production leak is not real

The earlier guess was that the `PianoInit` call inside `BarSessionTeardown` loses memory on every reconnect. That is wrong:

- `src/bar_state.c:575-586` `BarStateResetPianoHandle` calls `PianoDestroy(&app->ph)` (line 580) **before** `PianoInit` (line 581), under the state write lock.
- `PianoDestroy` (`src/libpiano/piano.c:203-217`) → `PianoDestroyPartner` (`piano.c:177-185`) frees `user`/`password`/`device`/`authToken` and runs `gcry_cipher_close` on both ciphers. These are the exact allocations `PianoInit` makes (`piano.c:43-45` strdup, `piano.c:47`/`:56` `gcry_cipher_open`).
- The handle that exists when the program ends is freed at `src/main.c:899` `PianoDestroy (&app.ph)`.
- The leaked bytes are the *fresh* handle that the reset creates (stack: `PianoInit@piano.c:47/56` ← `BarStateResetPianoHandle@bar_state.c:581` ← `BarSessionTeardown@ui_act.c:923`). It leaks only because the tests never call `PianoDestroy` afterwards. `test/unit/test_ui_act_reconnect.c` calls `PianoInit (&app.ph` 17 times and `PianoDestroy (&app.ph` 0 times. The handle the test itself created is not reported, because the reset frees it.

**Production impact: none per reconnect, none per song.** Two leaks exist only on exit paths. Tests do not reach them and LSan does not report them (see Open Questions):
- `src/main.c:796-806`: if `PianoInit` fails at startup, `main` returns without calling `PianoDestroy`. A partially built handle (up to 3 strdups and 1 cipher) leaks once as the process exits.
- `src/main.c:883`: `if (BarMainFinalizationStatus (&app) != EXIT_SUCCESS) { return EXIT_FAILURE; }` skips all cleanup when the program exits with a failure. Again only once, at exit.

### Leak groups (all test-only)

| Group | Root cause | Tests (runs) | Bytes |
|---|---|---|---|
| **A** | Fixture never calls `PianoDestroy(&app.ph)` after a disconnect/reconnect reset the handle, or after its own `PianoInit` | 24 | 426,730 |
| **B** | Racy playlist test drops the song returned by `BarStateAdvancePlaylist` | 1 | 8,960 (varies, 6,720 in the no-fork run) |
| **C** | json-c object passed to a function that only borrows it, never `json_object_put` | 3 | 1,544 |
| **D** | Test overwrites `settings.username`/`password` that were already read from config | 1 (×4 loop iterations) | 72 |
| **E** | Integration test uses the teardown helper that does not free `lastStationId` (and `ph`) | 1 (same test as A) | 16 |
| | **Total** | **28** | **437,322** |

#### Group A: Piano handle not destroyed by test cleanup (24 runs, 426,730 B)

A full handle is 18,551 B: 2 × 9,247 B Blowfish cipher handles (`piano.c:47`, `:56`), plus 33 + 16 + 8 B strdups (`piano.c:44`, `:45`, `:43`).

- `test/unit/test_ui_act_reconnect.c` (suite `ui_act`, tcase `actions`), 20 runs:
  - Leak through `BarSessionTeardown`, at 18,551 B each: `test_ui_do_pandora_disconnect_when_player_already_dead` (876), `_idle_timeout_message` (922), `_clears_playback_state_before_destroy` (961), `test_ui_act_pandora_reconnect_clears_playback_state` (1033), `test_session_auto_recover_resumes_without_disconnect_event` (1138), `test_session_auto_recover_failure_is_sticky` (1206), `test_ui_do_pandora_disconnect_logs_when_manager_not_parked` (1257), `test_ui_act_pandora_reconnect_logs_when_manager_not_parked` (1310), `test_ui_do_pandora_disconnect_waits_for_running_manager` (1370), `test_ui_act_pandora_disconnect_delegates_to_helper` (1662), `test_ui_do_pandora_disconnect_waits_for_player_stop` (1783), `test_ui_do_pandora_disconnect_playlist_session_error_message` (2273), `test_ui_do_pandora_reconnect_get_stations_failure` (~2380).
  - `test_ui_do_pandora_disconnect_waits_for_parked_manager` (1408): calls `PianoInit` at :1424 and never resets or destroys the handle. The leak comes from the test's own `PianoInit`.
  - `test_ui_do_pandora_disconnect_piano_reinit_failure` (2310) and `test_ui_do_pandora_reconnect_piano_reinit_failure` (2343), 9,304 B each. Here the reset's `PianoInit` fails in `gcry_cipher_setkey` (empty key), after strdups and cipher `in` were allocated. The partial handle is left in `app->ph`. Production frees it on the next reset or at `main.c:899`, so this is test-only too.
  - `test_pandora_disconnect_reconnect_wait_without_holds` (~4171, loop `_i` 0..3), 4 runs, 18,551 B each, plus group D.
- `test/unit/test_cli_input_paths.c` (suite `CliInputPaths`, tcase `Core`), 3 runs: `test_disconnect_discards_song_history` (~811), `test_reconnect_without_saved_station_resumes_nothing` (~844), `test_reconnect_keeps_queued_station_over_saved_one` (~862). Shared helper `session_app_destroy` (~796-808) has no `PianoDestroy`, though `session_app_init` (~791) calls `PianoInit`.
- `test/integration/test_playback_integration.c` (suite `playback_integration`), 1 run: `test_playback_fetch_generic_failure_clears_next_station` (~408-426). The failure path disconnects, which resets `ph` and sets `lastStationId`. The test then calls `teardown_integration_app` (:118-129, no `PianoDestroy`, no `free(lastStationId)`) instead of `teardown_integration_app_with_piano` (:131-144), which does both. That covers group E as well.

#### Group B: dropped `BarStateAdvancePlaylist` result (1 test, about 9 KB, nondeterministic)

- `test/unit/test_bar_state.c` `test_bar_state_advance_playlist_race_with_drain` (~403-430), allocations at :408/:409. `advance_playlist_worker` (~384-390) does `PianoSong_t *finished = BarStateAdvancePlaylist(args->app); (void) finished;`. `BarStateAdvancePlaylist` (`src/bar_state.c:295-308`) detaches the head and passes ownership to the caller. In production the playback manager takes ownership. Songs the drain thread frees first do not leak, so the amount depends on how the threads interleave.

#### Group C: borrowed json-c objects never released (3 tests, 1,544 B)

- `test/unit/test_socketio.c` :1614 `BarSocketIoHandleAddGenre (&app, json_object_new_object ())` and :1813 `BarSocketIoHandleAddMusic (&app, json_object_new_object ())`, 744 B each (48 B direct + 696 B indirect, json-c's internal table). These handlers borrow their argument. The other tests show the contract: :1746 then `json_object_put (data)` at :1748. The dispatcher (`src/websocket/protocol/socketio.c:244`/`:250`) also only borrows.
- `test/unit/test_websocket.c` :2104 `BarSocketIoEmit (&app, BAR_WS_DEST_BROADCAST, "volume", json_object_new_int (50))`, 56 B. `BarSocketIoEmit` borrows (`socketio.c:426-429` → `BarSocketIoEmitKeyed`). `BarSocketIoEmitVolume` at `socketio.c:432-436` shows the caller-puts pattern.

#### Group D: settings strings overwritten without free (72 B)

- `test_pandora_disconnect_reconnect_wait_without_holds` (`test_ui_act_reconnect.c` ~4181-4184): `read_default_settings` writes `user = testuser\npassword = testpass\n` (:866) and `BarSettingsRead` strdups both (`src/settings.c:214`). The test then sets `app.settings.username = strdup ("user"); app.settings.password = strdup ("pass");` and loses the 9 + 9 B strings. 18 B × 4 runs.

## Global Constraints

- Change test code only (`test/**`). Do not change `src/**` in Tasks 1-4. If a reviewer wants the optional `main.c` exit-path hardening (Open Questions), it is a separate task with ≥90% patch coverage.
- Keep test behavior and assertions the same. Only add cleanup and fix ownership.
- Free resources in the order the code already uses: stop the playback manager first, and run `PianoDestroy` under `pianoHttpMutex` when the mutex still exists (as `teardown_integration_app_with_piano` does). See `src/THREAD_SAFETY.md`.
- Because `test/**` and `Makefile`/`.github/workflows/test.yml` change: before any push, run `make test-ci-local` or `make distclean && PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-all` (see `.agents/skills/pre-push-ci-parity/SKILL.md`). Commit or push only when the user asks.
- Linux leak check (used as the "failing test" in every task): `PIANOBAR_CI_TARGET=test-asan make test-ci-local`, with `ASAN_OPTIONS=detect_leaks=1` passed into the container (add `-e ASAN_OPTIONS` to `scripts/test-ci-local.sh`, see Task 5). Or build the image once and run `CK_RUN_SUITE=<suite> ./pianobar_test` inside it for quick checks. Use an isolated `HOME`, and work on a copy of the tree so container-built objects never mix with the host build.

## Review Focus

1. Cleanup that runs after a *failed* `PianoInit` (reinit-failure tests): `PianoDestroy` must handle a half-built handle. `gcry_cipher_close(NULL)` and `free(NULL)` are no-ops, and `PianoInit` `memset`s first, so this is safe. Confirm under ASan.
2. `test_ui_do_pandora_disconnect_waits_for_parked_manager`: destroy `ph` only **after** `BarPlaybackManagerStop`, so the manager thread cannot touch the handle.
3. The racy playlist test must still exercise the race. Freeing `finished` inside the worker must not hold the state lock, because `BarStateAdvancePlaylist` already released it.
4. `test_reconnect_keeps_queued_station_over_saved_one` leaves `lastStationId = "saved-station"`. `session_app_destroy` already frees it, so do not free it twice.

---

### Task 1: Destroy the Piano handle in reconnect/disconnect fixtures (group A, 23 runs)

**Files:**
- Modify: `test/unit/test_ui_act_reconnect.c` (the 17 tests that call `PianoInit (&app.ph`, plus the 4-way loop test)
- Modify: `test/unit/test_cli_input_paths.c` (`session_app_destroy`)

- [ ] **Step 1: Reproduce the failure.** In the Linux container: `CK_RUN_SUITE=ui_act ./pianobar_test` and `CK_RUN_SUITE=CliInputPaths ./pianobar_test` (ASan build, `ASAN_OPTIONS=detect_leaks=1`). Expected: 20 + 3 errors, `Early exit with return value 1`, LSan stacks through `PianoInit@piano.c:47/56`.
- [ ] **Step 2: Fix `test_ui_act_reconnect.c`.** In each test that calls `PianoInit (&app.ph, …)`, add `PianoDestroy (&app.ph);` to the cleanup, before `pthread_mutex_destroy (&app.pianoHttpMutex)` / `BarUiPianoHttpMutexDestroy (&app)` and after any `BarPlaybackManagerStop`. A small static helper (e.g. `destroy_piano_for_test (BarApp_t *)` that locks `pianoHttpMutex`, calls `PianoDestroy`, then unlocks) is fine. Do not refactor the rest of each cleanup.
- [ ] **Step 3: Fix `session_app_destroy`** in `test_cli_input_paths.c`: add `PianoDestroy (&app->ph);` before `pthread_mutex_destroy (&app->pianoHttpMutex)`.
- [ ] **Step 4: Verify.** Rerun both suites in the container: 0 errors, no LSan report (group D's 72 B in the loop test stay until Task 3). `make test` on macOS still passes.
- [ ] **Step 5: Commit** `test: destroy the Piano handle after disconnect/reconnect tests`.

### Task 2: Integration teardown (groups A + E, 1 run)

**Files:**
- Modify: `test/integration/test_playback_integration.c` (`test_playback_fetch_generic_failure_clears_next_station`)

- [ ] **Step 1: Reproduce.** `CK_RUN_SUITE=playback_integration ./pianobar_test` under ASan in the container. Expected: 1 error, 18,567 B (includes `BarStateSetLastStationId@bar_state.c:234`).
- [ ] **Step 2: Fix.** Switch the test's teardown to `teardown_integration_app_with_piano (&barApp);`. First check that `setup_integration_app` initialises `pianoHttpMutex` and `ph` the way the `_with_piano` helper expects. If it does not (e.g. there is a `setup_integration_app_with_piano`), use the matching setup/teardown pair. Also check the other tests in this suite that reach disconnect, in case the leak is hidden because they don't reach `BarSessionTeardown` today.
- [ ] **Step 3: Verify** that the suite is LSan-clean, and that `PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test` passes on macOS.
- [ ] **Step 4: Commit** `test: free piano handle and resume id in generic-failure playback test`.

### Task 3: Small ownership fixes (groups B, C, D)

**Files:**
- Modify: `test/unit/test_bar_state.c` (`advance_playlist_worker`)
- Modify: `test/unit/test_socketio.c` (:1614, :1813)
- Modify: `test/unit/test_websocket.c` (:2104)
- Modify: `test/unit/test_ui_act_reconnect.c` (~4183-4184)

- [ ] **Step 1: Reproduce** each in the container: `CK_RUN_SUITE=BarState CK_RUN_CASE=Playlist`, `CK_RUN_SUITE="Socket.IO Protocol"`, `CK_RUN_SUITE=WebSocket CK_RUN_CASE=wire_compat`, `CK_RUN_SUITE=ui_act`. Expected: LSan errors in the four tests named above.
- [ ] **Step 2: Group B.** In `advance_playlist_worker`, free the detached song: `PianoDestroyPlaylist (finished);`. The `calloc`'d test songs have NULL string fields, and the function accepts NULL. Remove the `(void) finished;`.
- [ ] **Step 3: Group C.** Keep the object in a local variable, pass it, then `json_object_put` it, matching :1745-1748:
  `json_object *data = json_object_new_object (); BarSocketIoHandleAddGenre (&app, data); json_object_put (data);` (same for `AddMusic` and the `volume` emit).
- [ ] **Step 4: Group D.** Before the two `strdup`s, `free (app.settings.username); free (app.settings.password);`. Or drop the overrides and use the `testuser`/`testpass` values that are already loaded, if the hooks don't depend on the exact strings (`teardown_login_fails` should be checked).
- [ ] **Step 5: Verify** that each suite is LSan-clean. Run the `BarState Playlist` case 5 times: group B is timing-dependent, so it must be clean on every run.
- [ ] **Step 6: Commit** `test: release borrowed json objects, dropped songs and overwritten settings`.

### Task 4: Regression guard for the production reset path

The production code is correct today. This test makes sure it stays that way, and it is the test that would have caught the suspected per-reconnect leak.

**Files:**
- Test: `test/unit/test_bar_state.c` (or wherever `BarStateResetPianoHandle` is already covered: `grep -rn BarStateResetPianoHandle test/`)

- [ ] **Step 1: Write the test** `test_bar_state_reset_piano_handle_does_not_accumulate`: set up the app with partner settings from `BarSettingsInit`/defaults. Call `PianoInit`, then `BarStateResetPianoHandle` 10 times, asserting `PIANO_RET_OK` each time. Then `PianoDestroy (&app.ph)` and tear down. Optionally give `app.ph.stations` / `app.ph.user` a small allocated value before a reset and check the reset clears them (they must be NULL afterwards). That proves the reset frees state and does not just overwrite it.
- [ ] **Step 2: Run** it under ASan on Linux. Expected: **PASS with no LSan report** (the production path does not leak). Sanity check: temporarily comment out `PianoDestroy(&app->ph)` at `src/bar_state.c:580` in the scratch copy, and the test must fail with a 10× `PianoInit` leak. Revert that change.
- [ ] **Step 3: Commit** `test: guard piano handle reset against per-reconnect leaks`.

### Task 5: Make `make test-asan` leak-clean on Linux and enforce it in CI

**Files:**
- Modify: `scripts/test-ci-local.sh` (pass `-e ASAN_OPTIONS` / `-e LSAN_OPTIONS` through to `docker run`)
- Modify (optional): `.github/workflows/test.yml` (new job)
- Modify (optional): `Makefile` `test-asan` (~line 430)

- [ ] **Step 1: Full Linux run.** `ASAN_OPTIONS=detect_leaks=1 PIANOBAR_CI_TARGET=test-asan make test-ci-local`. Expected: `100%: Checks: 816, Failures: 0, Errors: 0` (815 + the Task 4 test) and exit 0. Also run once with `CK_FORK=no` to catch leaks that only show at process exit.
- [ ] **Step 2: Makefile (optional).** Do **not** hard-code `detect_leaks=1` in `test-asan`. On macOS, Apple's ASan does not support it and may abort with "detect_leaks is not supported on this platform" (check this on the dev Mac before relying on it). On Linux, GCC already enables it by default. If an explicit setting is wanted, use `ASAN_OPTIONS="$${ASAN_OPTIONS:-detect_leaks=1}"` only when `uname -s` is `Linux`. Changing the `Makefile` means the CI-parity rule applies.
- [ ] **Step 3: CI job (optional, recommended).** Add a `c-asan` job to `.github/workflows/test.yml`. Use the same `runs-on`, checkout and apt install as `c-tests`, then:
  ```yaml
      - name: Build and test with ASan/UBSan/LSan
        env:
          PIANOBAR_INTEGRATION: "1"
          PIANOBAR_TEST_NO_DEVICE: "1"
          ASAN_OPTIONS: "detect_leaks=1"
        run: make test-asan
  ```
  Run it as its own job, in parallel with `c-tests`. It must not share `coverage.info`. Before pushing, run `PIANOBAR_CI_TARGET=test-asan make test-ci-local`, because `test.yml` changed (pre-push CI parity).
- [ ] **Step 4: Commit** `ci: run test-asan with leak detection on Linux`.

**Interim `detect_leaks=0`?** Not recommended. All five groups are small test-only fixes (Tasks 1-3), with no production code to wait on. Land them before or together with the CI job, so leak detection is on from the first CI run. Use `detect_leaks=0` only if the CI job has to merge before Tasks 1-3. If so, set it in the CI step `env` (never in the `Makefile`), with a comment linking this plan, and remove it in the PR that lands Task 3. Do not use an LSan suppressions file for these leaks: they are real ownership bugs in tests, and a `PianoInit` suppression would also hide a future production regression.

## Acceptance

- In the Linux container, `ASAN_OPTIONS=detect_leaks=1 PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-asan` exits 0 with 0 errors, in both fork mode and `CK_FORK=no`.
- The racy `BarState`/`Playlist` case stays LSan-clean over 5 repeated runs.
- The Task 4 regression test exists, passes, and fails when `PianoDestroy` is removed from `BarStateResetPianoHandle`.
- `make test-all` passes on macOS. `make distclean && PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-all` (or `make test-ci-local`) passes.
- No `src/**` diff (unless the optional `main.c` hardening is accepted separately).
- If Task 5 Step 3 is done: the `c-asan` job is green on the PR.

## Open Questions

1. **`main.c` exit-path cleanup** (`main.c:796-806` PianoInit-failure return, `main.c:883` early `return EXIT_FAILURE`). These leak once at process exit, and no user would notice. Fix them (add `PianoDestroy (&app.ph)` on the init-failure path) or leave them? Fixing needs a `main` finalization test to meet the 90% patch coverage target. Recommendation: leave them unless LSan ever runs the real binary (e.g. a future web-mode smoke test under ASan).
2. Should `PianoInit` itself clean up the partial handle when it fails? It would be cleaner for libpiano, but today the callers (`BarStateResetPianoHandle`, `main`) rely on a later `PianoDestroy`, and a failed reset leaves `app->ph` partly built until then. Changing this is a libpiano behavior change, not needed for this plan.
3. Should the stack below PR #71 (or `main`) get the same fixes? These leaks reproduce at `a596303`. Check with `git log -S"PianoInit (&app.ph"` which PR introduced each leaking test, and land each fix in the PR that introduced the test, or in one follow-up PR on top of the stack.
4. How long the ASan CI job runs was not measured. If it is much slower than `c-tests`, consider running only on `pull_request`.
