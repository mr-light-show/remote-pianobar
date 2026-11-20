# WebSocket Simplification — Design

## Intent

Make WebSocket delivery simple and correct: every outbound message names its destination, frames are written only from libwebsockets writable callbacks, events are never lost to coalescing, and slow Pandora work cannot stall connection servicing.

**Why:** On slow hardware (Raspberry Pi 3) clients get disconnected from time to time. The service thread runs Pandora HTTP inside receive callbacks, so a slow call blocks keepalive and every other client. Separately, the single mixed STATE bucket lets a `playState` update replace a pending song `start`, and `lws_write` is called outside writable callbacks (`websocket.c:728`), which risks unbounded buffering behind slow readers. Ambient globals (`g_wsContext`, `g_broadcastCallback`, a thread-local unicast key) make routing implicit and fragile.

**Code quality goal:** one small queue concept used twice, no ambient routing state, fewer special cases than today. Clean and simple is the priority.

## Goals

1. Explicit recipients: broadcast or a monotonically assigned connection ID. No `struct lws *` held by producers.
2. Writable-driven sends with bounded per-client memory; a slow client cannot delay others.
3. Correct coalescing: only repeated unsent volume/progress updates collapse; start/stop/process/playState and query replies stay ordered.
4. Stale progress from a previous song is never delivered after a new song starts.
5. All application commands run on one worker thread; the service thread only does transport work.
6. Clean, ordered shutdown with no dangling references.

## Non-goals / anti-goals

- No wire-protocol changes, renamed events, or new wire fields. No HA integration or card changes.
- No thread pool or parallel Pandora calls; `pianoHttpMutex` stays the serialization point.
- No file splitting for its own sake; no drive-by refactors.
- Not a success if it passes tests but adds more concepts than it removes.

## Constraints

- Preserve CLI, web and both modes, and existing broadcast versus requester-only semantics.
- Pi 3 class hardware: small, named memory limits; avoid idle polling cost.
- Follow `AGENTS.md`: user-facing strings through `locale/en.yaml`, GNU C style, `Bar*` naming.
- After PR0 there is a single build configuration; `NOWEBSOCKET=1` no longer exists.

## Approaches considered

- **A. Execute the original 12-task plan.** Thorough but adds separate outbox, command and generation-metadata modules, plus scaffolding-only tasks. Rejected as heavier than needed.
- **B. Patch the existing buckets.** Smallest diff, but leaves direct writes, slow-client stalls and blocking commands. Rejected.
- **C. One queue concept used twice (chosen).** Outbound queue with optional replace-key; inbound bounded command FIFO with one worker. Reuses the existing `queue.c` where practical.

PR3 options: **all commands through one worker FIFO (chosen)**; worker only for Pandora-calling commands (two paths, subtle ordering); no worker, only timeouts (does not stop service-thread stalls).

## Design

### Outbound queue

- A message carries `{destination, optional replace-key, owned frame}`.
- Producers enqueue under one lock and call `lws_cancel_service()`. Only the service thread touches sockets.
- The service thread resolves destinations into per-client pending lists, requests `lws_callback_on_writable()`, and writes one frame per writable callback (preserving `LWS_PRE`, honoring libwebsockets' return-value contract).
- Replace-keys exist for the three latest-value-wins updates that coalesce today: progress, volume and the station list (a full snapshot). A keyed message replaces an earlier unsent message with the same key and no ordered message in between. Evaluated per client, so clients with different backlogs stay correct. *(Builder to confirm from the current bucket set that no other bucket exists.)*
- Per-client pending lists live in a table owned by the service thread, keyed by connection ID. Broadcast frames are shared and refcounted; replacing a keyed entry swaps that client's pointer, so memory per client stays small on a Pi 3. *(Author's call.)*
- Stale progress: the websocket layer owns a song generation counter, incremented when a song `start` is admitted, under the outbound queue's lock. Progress is tagged with the generation current at enqueue and dropped at dequeue if it no longer matches. This touches only the websocket/bridge layer, not `player.c`. Generation never appears on the wire. *(Author's call.)*
- Per-client and ingress limits are named constants in `bar_constants.h`, sized from measured full-state/station payloads with headroom. The per-client byte cap must be at least the largest measured full-state frame, so one resync frame always fits into an empty queue; the cap is checked before appending. A client that exceeds its limit is closed with a bounded reason and recovers by reconnecting and resyncing, which is one frame, so a reconnect loop is not expected. Events are never silently dropped. *(Author's call.)*
- Ingress full: the producer gets an explicit failure, logs it, and the service thread closes the affected recipients (all clients for a broadcast, the requester for a reply). Callers do not retry. *(Author's call.)*
- Recipients are connection IDs from a `uint64` counter; a reconnect into a reused slot never receives the previous client's replies, and replies to a disconnected requester are dropped silently. Table size follows the existing max-client setting.

### Command worker (PR3)

- Receive callbacks validate the frame, copy request bytes, and enqueue `{requester ID, bytes}` to a bounded FIFO; admission failure is distinct from shutdown rejection.
- One worker executes commands in FIFO order and replies through the outbound queue with an explicit destination. Query replies (full state, explain, upcoming) go only to the requester; state-change broadcasts are unchanged.
- Invariant for borrowed data: the worker resolves station/song IDs to pointers only inside the same critical section the handlers already use (`pianoHttpMutex`, plus `stateRwlock` where applicable), and never holds a pointer across a queue wait. Before PR3, audit every handler that dereferences `PianoStation_t`/`PianoSong_t` without that mutex, including CLI mutations in Both mode.
- Cheap controls (volume, pause) also queue behind a slow call. That already happens today, and the service thread no longer blocks.
- Shutdown order: stop admission, cancel queued commands and free payloads, interrupt in-flight curl through the existing mechanism, lifecycle owner joins the worker, then destroys the transport. `app.quit` originating on the worker signals the owner instead of joining itself. Pending replies are abandoned at shutdown (tested).
- Caveat: this stops WebSocket-client drops caused by a stalled service thread. It does not change Pandora-side session expiry or network failures.

## PR breakdown

0. **Cleanup:**
   - Remove the `NOWEBSOCKET=1` build option. This fork's upstream (pianobar) has no WebSocket support and doesn't want it, so users who don't want WebSockets can use upstream. libwebsockets and json-c become hard dependencies. `BAR_UI_MODE_CLI` stays as a runtime mode that doesn't start the server or playback manager.
   - Delete the Makefile branches and the `build.sh` references. Remove every `WEBSOCKET_ENABLED` guard (`main.c`, `ui_act.c`, `settings.c/h`, `bar_state.c/h`, `main.h`, `websocket_bridge.c`, `test_main.c`). Delete the bridge stubs, the cppcheck suppression, and the README and `README_TESTING.md` mentions.
   - Fix the playback-fixture flaky test. The cause is already diagnosed: during an intentional player stop the fixture's `send()` fails with `EPIPE` while its cleanup flag is still false, and the test joins the player before setting it. Separate cancel-expected from the cleanup flag and add deterministic ordering tests.
   - The NOWEBSOCKET removal is mechanical with no behavior change. This PR must leave a stable passing baseline for the rest.
1. **Foundations:** explicit recipients and connection IDs; remove the globals and TLS key; query replies pass destinations explicitly.
2. **Outbound queue:** writable-driven sends, per-client limits, replace-keys, ordered events, stale-progress rejection.
3. **Command worker:** bounded FIFO, one worker, clean shutdown, updated `THREAD_SAFETY.md` and `WEBSOCKET_API.md`. Starts with the handler-pointer audit. This is the fix for the Pi 3 disconnects, so it is a committed deliverable. **Stop-and-ask trigger:** if the audit finds that making handlers safe needs new locking in CLI-side code, stop and bring the findings to the user before proceeding.

PR2 is acceptable to ship on its own, with commands still on the service thread, and must not assume a worker exists. PR1 provides the explicit-destination reply API that PR3 plugs into.

Both mode: the worker is not the only mutator. The CLI thread keeps mutating under the existing locks. The audit fixes only real violations in handlers this work touches; everything else is documented in `THREAD_SAFETY.md`. *(Author's call.)*

## Testing and verification

- Write a focused failing test before each change; keep focused tests fast while developing.
- No per-task full gates. Run the full gate (`make distclean`, `PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-all`, web tests with coverage, web build, e2e, `git diff --check`) **once per PR, just before creating it**.
- At that same point: patch coverage of at least 90% on changed production lines, plus the `pre-push-ci-parity` and `pre-push-coverage` skill steps. Real-server Playwright run against a disposable local player for PRs 2 and 3.
- New modules are added to `Makefile` production, test and lint inputs in the PR that creates them.

## Left to the builder

Exact struct layouts, limit values (from measurement), the specific replace-key representation, how connection IDs are stored, test file names, and whether `queue.c` is extended or a small new module is clearer.
