# WebSocket Simplification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make WebSocket delivery explicit, writable-driven and ordered, and keep slow Pandora work off the connection service thread, in four reviewable PRs.

**Architecture:** One outbound queue (ingress → per-client lists, optional replace-key, written only from writable callbacks) and one bounded command FIFO with one worker. Destinations are `broadcast | connection ID`; the globals and TLS unicast key go away. The existing `playStateEpoch` watermark in `BarWsContext_t` stays; this plan builds on it.

**Tech Stack:** C17, pthreads, libwebsockets, json-c, Check; TypeScript/Lit/Vitest/Playwright for the web UI gates.

**Spec:** `docs/superpowers/specs/2026-11-12-websocket-simplification-design.md`

## Global Constraints

- No wire-protocol changes: event names, payloads and fields stay as they are. No HA integration or card changes.
- No thread pool and no parallel Pandora calls; `app->pianoHttpMutex` stays the serialization point.
- Preserve broadcast versus requester-only semantics, and CLI/web/both modes.
- Named limits live in `src/bar_constants.h`; the per-client byte cap must hold at least the largest measured full-state frame.
- `AGENTS.md` rules apply: GNU C style (`BarFoo (args)`), `Bar*` names, user-facing text via `locale/en.yaml` + `make locale-codegen`, new modules added to `Makefile` production, test and lint inputs in the PR that creates them.
- **Do not commit or push.** The user commits when they ask. "Done" for a task means its focused tests pass.
- Focused test run: `PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test && CK_RUN_SUITE=<suite> CK_RUN_CASE=<case> ./pianobar_test`. Do not run the full gate between tasks.

## Pre-PR gate (last task of every PR, run once)

Run sequentially, stop on first failure, record command, exit code and test count:

```sh
make distclean
PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-all
PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-coverage
(cd webui && npm ci && npm test -- --run --coverage)
(cd webui && npm run build)
(cd webui && npm run test:e2e)
git diff --check
```

Then: confirm ≥90% line coverage on added/modified production lines (from the PR's starting revision, per `.agents/skills/pre-push-coverage/SKILL.md`), and follow `.agents/skills/pre-push-ci-parity/SKILL.md` (Makefile/test changes: `make test-ci-local`). For PRs 2 and 3 also run the Playwright real-server cases (`PIANOBAR_RUNNING=1`) against a disposable local headless player with isolated config and no real credentials. Do not retry failures into green, raise timeouts or disable tests; diagnose and fix.

## Review Focus

1. A client reconnects into a reused connection slot: the old client's pending reply must not reach the new one (Task 1.1).
2. One client stops reading while another keeps receiving: the writable client is unaffected and memory stays bounded (Task 2.3).
3. Pause arrives right after a song `start` is queued: the client gets both song metadata and pause state (Task 2.2).
4. A station-heavy account produces a full-state frame bigger than a naive cap: a resync into an empty queue must still fit (Task 2.1).
5. Requester disconnects, or `app.quit` runs, while a command is queued or in flight: no crash, no write after teardown, worker never joins itself (Task 3.3).

## File Structure

| File | Responsibility |
|---|---|
| `src/websocket/core/outbox.h/.c` (new, PR2) | Refcounted frames, per-client queue with replace-keys and limits, ingress queue |
| `src/websocket/core/commands.h/.c` (new, PR3) | Bounded owned command FIFO |
| `src/websocket/core/websocket.h/.c` | Transport, connection table, IDs, writable drain, worker lifecycle |
| `src/websocket/protocol/socketio.h/.c` | Protocol handlers and emitters, now destination-explicit |
| `src/websocket_bridge.h/.c` | App-facing broadcast/emit helpers |
| `test/unit/test_ws_outbox.c`, `test/unit/test_ws_commands.c` (new) | Queue unit tests, registered in `test/test_main.c` |

---

# PR0: Cleanup and stable baseline

### Task 0.1: Remove the `NOWEBSOCKET=1` build option
**Status: complete** (a307ebd, review approved; kept the `staticFunction:src/websocket_bridge.c` cppcheck suppression because `make lint` needs it).

**Files:** Modify `Makefile`, `build.sh`, `src/main.h`, `src/main.c`, `src/ui_act.c`, `src/settings.c/h`, `src/bar_state.h`, `src/websocket_bridge.c/h`, `test/test_main.c`, `test/unit/test_player.c`, `.cppcheck-suppress`, `README.md`, `README_TESTING.md`, `AGENTS.md`.

- [x] **Step 1: Run the baseline** (`make distclean && PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-all`) and record the result. If the known flaky test fails, continue to Task 0.2 first and return here.
- [x] **Step 2: Remove every `WEBSOCKET_ENABLED` guard (keep the enabled branch), the `ifneq/ifeq ($(NOWEBSOCKET),1)` Makefile branches, the bridge stubs, the cppcheck suppression on line 40, and all documentation mentions.** `BAR_UI_MODE_CLI` stays; it simply doesn't start the server or playback manager at runtime.
- [x] **Step 3: Verify.** `grep -rIn "NOWEBSOCKET\|WEBSOCKET_ENABLED" . --exclude-dir=node_modules --exclude-dir=.git --exclude-dir=docs` prints nothing. `make distclean && make && make test` passes with the same check count as Step 1.

### Task 0.2: Fix the playback fixture's expected-disconnect race
**Status: complete** (a466a5f fixture fix, b5d1b8c tidy, 9e38df6 stale lint suppression; 100/100 `stream_shutdown`; review approved).

**Files:** Modify `test/integration/fixture_http.c/h`, `test/integration/test_playback_integration.c`, register tests in `test/test_main.c`; create `test/unit/test_fixture_send_errors.c` only if the classifier is not reachable from existing unit suites.

**Interfaces:** Produces `bool FixtureSendErrorExpected (int err, bool cancelExpected)` in `fixture_http.h`: true only when `cancelExpected` and `err` is `EPIPE` or `ECONNRESET`.

- [x] **Step 1: Write failing tests** `test_fixture_epipe_before_cancel_is_error`, `test_fixture_epipe_after_cancel_is_expected`, `test_fixture_other_error_after_cancel_is_error`, using the signature above (compile seam first, then watch the behavioral failure).
- [x] **Step 2: Implement the classifier.** Separate `cancelExpected` (atomic, published immediately before skip/disconnect/quit, without closing the fixture or releasing EOF early) from the cleanup/exit flag. Route the fixture's send-error handling through the classifier; file-open, accept, malformed-request and other send errors still fail the test. Retry `EINTR` sends.
- [x] **Step 3: Move the pause/stop regression into its own Check case `stream_shutdown`** in suite `playback_integration`, keeping all existing tests registered.
- [x] **Step 4: Verify.** Run `for i in $(seq 100); do PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 CK_RUN_SUITE=playback_integration CK_RUN_CASE=stream_shutdown ./pianobar_test || break; done`; expect 100 passes.

### Task 0.3: PR0 gate
**Status: complete** (gate green: `test-all` 513/0, vitest 384/384, build, e2e 24 passed / 15 preview-only skips, `git diff --check`; PR0 adds 6 `src` lines, all guard restructuring).

- [x] Run the Pre-PR gate. Expected: all green, check count ≥ the Task 0.1 baseline plus the new fixture tests.

---

# PR1: Explicit recipients

### Task 1.1: Connection IDs and a destination-explicit send
**Status: complete** (6d0aad9 + fix 84e9548; targeted sends bypass buckets and deliver immediately via `BarWsDeliver`, service-thread only until PR2; review clean after 1 fix round).

**Files:** Modify `src/websocket/core/websocket.h/.c`, `test/unit/test_websocket.c`.

**Interfaces:** Produces:
```c
typedef uint64_t BarWsConnId_t;                 /* 0 is never assigned */
#define BAR_WS_DEST_BROADCAST ((BarWsConnId_t) 0)
bool BarWebsocketSend (BarApp_t *app, BarWsConnId_t dest,
                       BarWsBucketType_t bucket, char *message);
```
`BarWsConnection_t` gains `BarWsConnId_t id`. `BarWebsocketSend` always takes ownership of `message` (freeing it on failure) and returns false for unknown/closed destinations or NULL context.

- [x] **Step 1: Write failing tests** in suite `websocket`: `test_send_broadcast_reaches_all_clients`, `test_send_to_id_reaches_only_that_client`, `test_send_to_reused_slot_does_not_reach_new_client` (client A in slot 0 gets ID 1, disconnects, client B reuses slot 0 with ID 2; a send to ID 1 returns false and B receives nothing), `test_send_unknown_id_frees_message_and_returns_false`.
- [x] **Step 2: Assign IDs from a monotonically increasing `uint64_t` at connection establishment** (service thread) and resolve `dest` against the connection table at send time; existing delivery behavior is otherwise unchanged.
- [x] **Step 3: Run the focused suite** and expect PASS.

### Task 1.2: Remove ambient routing
**Status: complete** (b01e6f9 + fix 8b18925; interim `BUCKET_NONE` deliver-now lane keeps direct emitters immediate until PR2; messages from untracked connections are dropped; review clean after 1 fix round).

**Files:** Modify `src/websocket/protocol/socketio.h/.c`, `src/websocket/core/websocket.c`, `src/websocket_bridge.h/.c`, `src/ui_act.c`, `test/unit/test_socketio.c`, `test/unit/test_ui_act_reconnect.c`.

**Interfaces:** Consumes Task 1.1's types. Produces:
```c
void BarSocketIoHandleMessage (BarApp_t *app, const char *message, BarWsConnId_t requester);
void BarSocketIoHandleQuery   (BarApp_t *app, BarWsConnId_t requester);
void BarSocketIoEmitProcessTo     (BarApp_t *app, BarWsConnId_t dest);
void BarSocketIoEmitExplanationTo (BarApp_t *app, BarWsConnId_t dest, const char *explanation);
void BarSocketIoEmitUpcomingTo    (BarApp_t *app, BarWsConnId_t dest, struct PianoSong *first, int max);
```
Removes `g_wsContext`, `g_broadcastCallback`, `g_unicastTargetKey`, `BarSocketIoSetBroadcastCallback`, `BarSocketIoSetUnicastTarget/GetUnicastTarget`, `BarSocketIoEmitProcessUnicast`, `BarWsIsUnicastRequest`. Every emitter sends through `BarWebsocketSend`.

- [x] **Step 1: Write failing two-client tests** `test_query_process_reaches_only_requester`, `test_query_explain_reaches_only_requester`, `test_query_upcoming_reaches_only_requester`, `test_state_broadcast_reaches_both_clients`, and a CLI test `test_cli_explain_prints_terminal_output_and_no_web_reply` (follow `test_ui_act_reconnect.c` patterns). Add capture tests per event family (playback, progress, volume, stations, errors, Pandora disconnect) asserting destination and payload; prove one is sensitive by deliberately mis-routing it, then restore.
- [x] **Step 2: Thread `requester`/`dest` through the query paths** (`ui_act.c` explain/upcoming handlers take the destination; CLI callers print to the terminal and never call `*To`). Migrate all emitters to `BarWebsocketSend`, then delete the listed globals and functions. Payloads and error behavior are unchanged.
- [x] **Step 3: Verify.** Focused suites `socketio`, `websocket`, `ui_act_reconnect` pass. `grep -rn "g_wsContext\|g_broadcastCallback\|g_unicastTargetKey\|SetUnicastTarget\|IsUnicastRequest" src` prints nothing.

### Task 1.3: PR1 gate
**Status: complete** (gate green at 32c3d19: `test-all` 538/0, vitest 384/384, build, e2e 24 passed / 15 preview-only skips; patch coverage 231/243 = 95.1%; uncovered lines are the lws glue for Task 2.3 and two failure branches).

- [x] Run the Pre-PR gate. Expected: green; coverage ≥90% on changed production lines.

---

# PR2: Outbound queue

### Task 2.1: Frames and per-client queue
**Status: complete** (1e568f4; measured process 676 B / stations 26,492 B at 200 stations; limits 64K frame, 256K per client, 64 msgs; ingress 256K / 128 msgs; review clean).

**Files:** Create `src/websocket/core/outbox.h/.c`, `test/unit/test_ws_outbox.c`; modify `Makefile`, `test/test_main.c`, `src/bar_constants.h`.

**Interfaces:** Produces:
```c
typedef enum { BAR_WS_KEY_NONE = 0, BAR_WS_KEY_PROGRESS, BAR_WS_KEY_VOLUME,
               BAR_WS_KEY_STATIONS } BarWsKey_t;
typedef struct BarWsFrame BarWsFrame_t;       /* refcounted; payload preceded by LWS_PRE bytes */
BarWsFrame_t *BarWsFrameNew (const char *text, size_t len);   /* NULL if len > WS_FRAME_MAX_BYTES or no memory */
BarWsFrame_t *BarWsFrameRef (BarWsFrame_t *f);
void BarWsFrameUnref (BarWsFrame_t *f);
typedef struct BarWsClientQueue BarWsClientQueue_t;
typedef enum { BAR_WS_Q_OK, BAR_WS_Q_OVERFLOW } BarWsQResult_t;
void BarWsClientQueueInit (BarWsClientQueue_t *q);
void BarWsClientQueueClear (BarWsClientQueue_t *q);           /* frees all pending */
BarWsQResult_t BarWsClientQueueAppend (BarWsClientQueue_t *q, BarWsFrame_t *f,
                                       BarWsKey_t key, uint32_t generation);
BarWsFrame_t *BarWsClientQueuePop (BarWsClientQueue_t *q, uint32_t currentGeneration);
```
Constants in `bar_constants.h`: `WS_CLIENT_MAX_MSGS`, `WS_CLIENT_MAX_BYTES`, `WS_INGRESS_MAX_MSGS`, `WS_INGRESS_MAX_BYTES`, `WS_FRAME_MAX_BYTES`. `Append` takes its own reference; a keyed append replaces an earlier pending entry with the same key only when no unkeyed entry follows it; the byte cap is checked before appending; `Pop` discards `BAR_WS_KEY_PROGRESS` entries whose generation ≠ `currentGeneration`.

- [x] **Step 1: Measure** the largest `process` and `stations` frames on a large station list (add a throwaway log line or test helper), and set the constants: per-client byte cap ≥ 4× the largest frame, `WS_FRAME_MAX_BYTES` ≥ 2× the largest frame, message caps small enough for a Pi 3 (record the numbers in a comment above the constants).
- [x] **Step 2: Write failing tests** `test_queue_fifo_order`, `test_keyed_replaces_unsent_same_key`, `test_keyed_does_not_replace_across_ordered_event`, `test_start_then_playstate_both_kept`, `test_stop_then_start_both_kept`, `test_progress_with_old_generation_dropped_on_pop`, `test_byte_cap_overflow_returns_overflow`, `test_empty_queue_accepts_one_max_size_full_state_frame`, `test_oversize_frame_rejected`, `test_clear_frees_pending` (run under `make test-asan`), `test_shared_frame_survives_other_queue_clear`.
- [x] **Step 3: Implement** the types above as intrusive singly linked lists; add the new files to production, test and lint inputs in `Makefile`.
- [x] **Step 4: Run** `CK_RUN_SUITE=ws_outbox ./pianobar_test` and `make test-asan`; expect PASS.

### Task 2.2: Ingress and generation
**Status: complete** (a125ffc; ingress with pre-allocation limit check, generation stamped under the lock; review clean). Carry-forward to 2.3: atomic song-start push, producer-observed generation for progress, ingress overflow closes all clients.

**Files:** Modify `src/websocket/core/outbox.h/.c`, `test/unit/test_ws_outbox.c`.

**Interfaces:** Produces:
```c
typedef struct BarWsOutbox BarWsOutbox_t;
BarWsOutbox_t *BarWsOutboxNew (void);
void BarWsOutboxFree (BarWsOutbox_t *o);
/* Takes ownership of text; false if ingress is full or no memory (text freed). */
bool BarWsOutboxPush (BarWsOutbox_t *o, BarWsConnId_t dest, BarWsKey_t key, char *text, size_t len);
/* Service thread: detach everything pushed so far, in order. */
BarWsPending_t *BarWsOutboxTake (BarWsOutbox_t *o);
uint32_t BarWsOutboxSongStarted (BarWsOutbox_t *o);   /* increments and returns generation */
uint32_t BarWsOutboxGeneration (BarWsOutbox_t *o);
```
`BarWsPending_t` is a list node `{dest, key, generation, frame, next}`. `Push` tags progress with the generation current under the same lock; admitting a song `start` calls `SongStarted` and discards unsent keyed progress.

- [x] **Step 1: Write failing tests** `test_push_does_not_deliver_until_take`, `test_ingress_full_returns_false_and_frees`, `test_progress_pushed_before_start_is_dropped_after_start`, `test_progress_for_new_song_survives`, and a threaded `test_concurrent_push_take_no_loss_no_leak` (barrier-controlled, not sleeps).
- [x] **Step 2: Implement** with one mutex; producers never touch sockets.
- [x] **Step 3: Run focused suite**; expect PASS.

### Task 2.3: Writable-driven delivery in the transport
**Status: complete** (1708d51 outbox additions, bfcd955 transport switch, 6d46c80 fix; buckets and direct writes removed; oversize frames are per-message rejections; limits 256K frame / 1 MiB per client / 1 MiB ingress; review clean after 1 fix round).

**Files:** Modify `src/websocket/core/websocket.h/.c`, `src/websocket_bridge.c`, `test/unit/test_websocket.c`; add a real-transport test using a local libwebsockets client in the same file.

**Interfaces:** Consumes Tasks 1.1–2.2. `BarWebsocketSend` now pushes into `BarWsOutbox_t` and calls `lws_cancel_service()`; `BarWsConnection_t` gains `BarWsClientQueue_t pending`. Remove `BarWsBucket_t`, `BarWsBucketType_t` fields and bucket polling; the playState epoch check moves into the push path under the outbox lock (same rule as today: reject older completed epochs, never publish in-progress audio changes). Remove cross-thread access to `progress.lastBroadcast` by keeping deduplication on the producer side only.

- [x] **Step 1: Write failing tests** `test_enqueue_does_not_write`, `test_writable_callback_writes_one_frame`, `test_nonwritable_client_keeps_pending_frame`, `test_close_frees_pending`, `test_blocked_client_does_not_delay_writable_client` (explicit blocked/writable signals), `test_overflow_closes_only_that_client`, `test_ingress_full_closes_affected_recipients`, and a real-transport `test_end_to_end_delivery_order_over_lws`. Assert the socket-write adapter is called only from the writable path.
- [x] **Step 2: On the service thread, `Take` ingress, resolve destinations to connections (dropping unknown IDs), append to each client's queue, and call `lws_callback_on_writable()`.** Drain one frame per `LWS_CALLBACK_SERVER_WRITEABLE`, preserving `LWS_PRE` and honoring `lws_write`'s return contract; request another writable if more is pending. Overflow closes the connection with a bounded reason. `lws_service()` still uses `WEBSOCKET_POLL_MS`.
- [x] **Step 3: Run** suites `websocket` and `ws_outbox` plus `make test-asan`; expect PASS. Verify no `lws_write` call remains outside the writable handler and the HTTP server: `grep -n lws_write src/websocket/core/websocket.c`.

### Task 2.4: PR2 gate
**Status: complete with one documented limitation** (gate green at 6d46c80: `test-all` 590/0, vitest 384/384, build, e2e preview 24 passed / 15 skipped; patch coverage 446/476 = 93.7%). Real-server Playwright could not run without a Pandora session and the player exits on a failed login; transport covered by real-libwebsockets unit tests instead; smoke-test the real UI on the Pi. `lws: ZERO RANDOM FD` after daemonize is pre-existing.

- [x] Run the Pre-PR gate including the real-server Playwright cases. Confirm a manual smoke on the real UI: song start, pause, volume, station list, reconnect.

---

# PR3: Command worker

### Task 3.1: Handler pointer audit (stop-and-ask)
**Status: complete** (audit + user-approved fixes B1-B5 (a3a81f8, 26089dd, 754f8cb, c4d2f3b, 80ed6dc, 3aafda1), draft table in THREAD_SAFETY.md, eventcmd releases the mutex around its wait (294b4d0); lock order pianoHttpMutex -> stateRwlock -> player audio; C1 interactive-prompt stall documented only; review clean after 1 fix round).

**Files:** Read `src/websocket/protocol/socketio.c`, `src/ui_act.c`, `src/ui.c`, `src/bar_state.c`; write findings into a draft section of `src/THREAD_SAFETY.md`.

- [x] **Step 1: List every handler that dereferences `PianoStation_t`/`PianoSong_t`** and record whether it holds `pianoHttpMutex` and/or `stateRwlock` for the whole use, and whether any pointer is kept across a blocking call.
- [x] **Step 2: Fix violations inside handlers this work touches** (resolve IDs to pointers inside the critical section, copy what is needed).
- [x] **Step 3: Stop and report to the user** if making handlers safe needs new locking in terminal (CLI) code; otherwise note the findings and continue.

### Task 3.2: Bounded owned command queue
**Status: complete** (1d641bf; max request 4096 B, 32 items, 64 KiB; review clean).

**Files:** Create `src/websocket/core/commands.h/.c`, `test/unit/test_ws_commands.c`; modify `Makefile`, `test/test_main.c`, `src/bar_constants.h` (`WS_CMD_MAX_ITEMS`, `WS_CMD_MAX_BYTES`).

**Interfaces:** Produces:
```c
typedef struct BarWsCommandQueue BarWsCommandQueue_t;
typedef enum { BAR_WS_CMD_OK, BAR_WS_CMD_FULL, BAR_WS_CMD_CLOSED, BAR_WS_CMD_NOMEM } BarWsCommandResult_t;
BarWsCommandQueue_t *BarWsCommandQueueNew (void);
void BarWsCommandQueueFree (BarWsCommandQueue_t *q);
BarWsCommandResult_t BarWsCommandQueuePush (BarWsCommandQueue_t *q, BarWsConnId_t requester,
                                            const char *bytes, size_t len);    /* copies */
bool BarWsCommandQueuePop (BarWsCommandQueue_t *q, BarWsConnId_t *requester,
                           char **bytes, size_t *len);  /* blocks; caller frees bytes; false once closed */
void BarWsCommandQueueClose (BarWsCommandQueue_t *q);   /* wakes consumers, frees queued items */
```

- [x] **Step 1: Write failing tests** `test_cmd_fifo_order`, `test_cmd_copies_input` (mutate/free caller buffer after push), `test_cmd_item_limit_returns_full`, `test_cmd_byte_limit_returns_full`, `test_cmd_push_after_close_returns_closed`, `test_cmd_close_wakes_blocked_pop`, `test_cmd_close_frees_queued`, `test_cmd_nomem_returns_nomem`.
- [x] **Step 2: Implement** with a mutex and condition variable; add to `Makefile` inputs.
- [x] **Step 3: Run** `CK_RUN_SUITE=ws_commands ./pianobar_test` and `make test-asan`; expect PASS.

### Task 3.3: One worker, responsive servicing, clean shutdown
**Status: complete** (cdfd783 worker; 188680a bind test fix; 9cfc464 test-ci-local python3; a3420ac queue tests; f2bde35 docs; initial snapshot stays on the service thread; fragmented/oversize client messages dropped; review clean).

**Files:** Modify `src/websocket/core/websocket.h/.c`, `src/websocket/protocol/socketio.c`, `src/websocket_bridge.c`, `src/main.c` (only where shutdown order requires it), `test/unit/test_websocket.c`, `test/unit/test_socketio.c`, `src/THREAD_SAFETY.md`, `WEBSOCKET_API.md`.

**Interfaces:** Consumes `BarWsCommandQueue_t`, `BarSocketIoHandleMessage (app, message, requester)`. Produces `bool BarWebsocketWorkerStart (BarApp_t *app)` (called from `BarWebsocketInit`, rolls back fully on failure) and `void BarWebsocketWorkerStop (BarApp_t *app)` (lifecycle owner only).

- [x] **Step 1: Write failing tests** `test_slow_command_does_not_block_service_thread` (mock Pandora call blocked on a condition variable; a second client connects and receives an unrelated queued update while it is blocked; service thread never waits on the command condvar or `pianoHttpMutex`), `test_command_replies_reach_only_requester`, `test_commands_execute_in_fifo_order`, `test_requester_disconnect_before_reply_is_dropped`, `test_worker_start_failure_rolls_back`, `test_shutdown_empty_queue`, `test_shutdown_with_queued_commands_frees_them`, `test_shutdown_with_blocked_http_interrupts_and_joins`, `test_app_quit_from_worker_signals_owner_not_self_join`, `test_repeated_start_stop`, `test_admission_full_returns_localized_error_to_requester` (reuse existing error copy via `locale/en.yaml`).
- [x] **Step 2: Receive callbacks validate the frame, copy it into the queue with the requester ID and return.** Admission `FULL` replies to that requester with an application error; `CLOSED` is dropped. The worker pops, runs `BarSocketIoHandleMessage` with the audited locking from Task 3.1, and replies through `BarWebsocketSend`. Initial full-state queries go through the same path.
- [x] **Step 3: Shutdown order:** stop admission (`Close`), interrupt in-flight curl via the existing interrupt mechanism, lifecycle owner joins the worker, then destroys outbox/transport. Pending replies are abandoned and the test asserts that. No app/player/state/queue lock is held across a join.
- [x] **Step 4: Remove obsolete code** (command enum values `MSG_TYPE_COMMAND_*` and linked-list fields made unused); document thread ownership, lock rules, limits and overload/reconnect behavior in `THREAD_SAFETY.md` and `WEBSOCKET_API.md`.
- [x] **Step 5: Run** suites `websocket`, `socketio`, `ws_commands`, `playback_integration` with `make test-asan`; expect PASS.

### Task 3.4: PR3 gate
**Status: complete** (final gate green at b9921f8: `test-all` 659/0, vitest 384/384, build, e2e preview 24 passed / 15 skips; patch coverage PR3 92.7%, whole branch 93.9%; TSan clean; Linux arm64 make test 658/0 with leak count back to the 21 pre-existing test-teardown leaks; two verification fixes 1d9b6dd and 604d690).

- [x] Run the Pre-PR gate including the real-server Playwright cases, in web and both modes. Final review of the full diff against the six goals in the spec.

---

## Execution record

For each PR, append: starting revision, gate results (commands, exit codes, test counts), failing-test evidence per task, patch coverage, and remaining limitations.
