# Stalled-Stream Stop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make skip, Pandora disconnect and quit take effect at once while the player thread is blocked in a network read of a stalled audio stream, instead of waiting for the playback manager's 10 s join timeout (or the 30 s network timeout).

**When:** First PR after the `refactor/ws-simplification-*` stack (#68–#71) merges. Branch from `main` then: `fix/stalled-stream-stop`.

**Locking (decided):** `stopRequested` is an `_Atomic bool`. Every write happens under `player->lock` (set in `BarPlayerRequestStop`, cleared in the new-song reset), so writes are ordered with `doQuit` and `controlEpoch`. The only reader, the FFmpeg interrupt callback, uses a lock-free atomic load: it runs every polling slice and per packet, and taking `player->lock` there would contend with every control action and could invert the `player->lock` → `decoderLock` order if an FFmpeg read is ever reached while `decoderLock` is held.

**Architecture:** FFmpeg already polls an interrupt callback during blocking I/O (`BarPlayerFfmpegInterruptCb`, `src/player.c:1008`), but it only looks at `player->interrupted`, which the stop path never sets. Publish a lock-free "stop requested" flag from `BarPlayerRequestStop` and have the callback return 1 while it is set. Clear it when the next song starts. No new locks, no new threads.

**Tech Stack:** C17, FFmpeg libavformat (HTTP via `avio`), pthreads, Check.

## Global Constraints

- Read `AGENTS.md` and `src/THREAD_SAFETY.md` first. The callback runs on FFmpeg's read path on the player thread with no application lock held (comment at `player.c:1013`); it must stay lock-free (atomics only).
- Do not change the meaning of `player->interrupted` (SIGINT-style one-shot, `1`, cleared by the callback; `2` = force stop from the playback manager, `playback_manager.c:142`).
- An open aborted by a stop must not be treated as a stream failure: no retry, no stale-CDN (403) handling, no error event to clients.
- ≥90% patch coverage on changed `src/` lines; `make test-all`, `make test-asan`, `make test-tsan`, `make test-ci-local` all pass.
- No user-visible strings expected. If any are added, they go in `locale/en.yaml` + `make locale-codegen`.

## Verified findings (evidence, stack tip 308c9e7)

1. **Stop does not reach the blocked read.** Skip (`BarUiDoSkipSong`, `ui_act.c:65`), disconnect and quit call `BarPlayerRequestStop` (`player.c:1537`), which sets `doQuit` under `player->lock` and broadcasts `cond`/`audioCond`. That wakes paused or waiting code but not a thread blocked in `av_read_frame` (`player.c:1326`) or `avformat_open_input` (`player.c:1050`).
2. **The interrupt callback ignores `doQuit`.** `BarPlayerFfmpegInterruptCb` returns 1 only when `player->interrupted != 0`. It is reset to 0 at song start (`player.c:926`) and set to 2 only by the playback manager's force path after `PLAYER_JOIN_TIMEOUT_SECS` = 10 s (`playback_manager.c:45,136-143`).
3. **User impact:** in production a skip or quit during a stalled stream takes about 10 s (join timeout, then the force interrupt). If the stall happens in a path that does not go through `force_join_player_thread`, it waits for the HTTP `timeout` option, `settings->timeout` = 30 s by default (`player.c:1042-1046`, `settings.c:702`).
4. **Test evidence:** under full CPU load, `test_player_paused_active_and_decoded_stop_paths_join_promptly` (`test/integration/test_playback_integration.c`) failed `join.joined` 4 times in 153 runs; a stack sample at the timeout showed the player thread in libavformat `poll()` on the HTTP read. The test's join helper uses `BarPlayerJoinThreadWithTimeout(..., 10)` with no force interrupt (`:835-839`), so it sees the raw delay.
5. **Shared callback:** the same callback is installed on every format context (`player.c:1039-1040`), so it also covers `avformat_find_stream_info` and HTTP reconnects inside libavformat.

## Review Focus

- **Stop during `avformat_open_input`:** the open returns `AVERROR_EXIT`. The song must end quietly (mode back to waiting, no retry loop, no 403 refresh, no error toast). Test: `test_stop_during_stalled_open_aborts_quietly`.
- **Flag cleared too early or too late:** a skip must not carry over and abort the next song's open; a stop requested just before the next song starts must still win. Tests: `test_stop_flag_does_not_abort_next_song`, `test_stop_requested_before_open_aborts_open`.
- **Pause is not a stop:** pausing while the network stalls must not abort the read. Test: `test_pause_during_stalled_read_does_not_abort`.
- **TSan:** the new flag is `_Atomic`; no plain read of `doQuit` from the callback.

---

### Task 1: Stalling HTTP fixture

**Files:**
- Modify: `test/integration/test_playback_integration.c` (next to the unfinished-stream fixture, ~`:842-910`)

- [ ] **Step 1:** Add a fixture mode that sends the HTTP headers and a few KiB of `tone.mp3`, then stops sending without closing (stall), and one that accepts the connection but never answers (stall during open). Reuse the existing `UnfinishedAudioStream` struct and `cancelExpected` logic.
- [ ] **Step 2:** Run the existing suite to confirm nothing changed: `PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 CK_RUN_SUITE=playback_integration ./pianobar_test`.

### Task 2: Failing tests

**Files:**
- Modify: `test/integration/test_playback_integration.c`, `test/unit/test_player.c` (callback unit tests)

- [ ] **Step 1: Write the failing tests**
  - `test_skip_during_stalled_read_joins_promptly`: stalled-read fixture; skip; join with `BarPlayerJoinThreadWithTimeout(..., 2)` succeeds and latency < 1 s. Repeat for disconnect and quit.
  - `test_stop_during_stalled_open_aborts_quietly`: never-answering fixture; stop; join < 1 s; no retry attempted (count fixture accepts = 1); no error event emitted.
  - `test_stop_requested_before_open_aborts_open`: request stop, then start the player thread; open aborts at once.
  - `test_stop_flag_does_not_abort_next_song`: skip song 1, start song 2 on a normal fixture; song 2 plays.
  - `test_pause_during_stalled_read_does_not_abort`: stalled read, pause for 1 s, resume, fixture resumes sending; playback continues.
  - Unit (`test_player.c`): `test_ffmpeg_interrupt_cb_returns_1_when_stop_requested`, `..._returns_0_after_song_start_reset`, existing `interrupted` 1/2 behavior unchanged.
- [ ] **Step 2: Run and confirm they fail**: `PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 CK_RUN_SUITE=playback_integration ./pianobar_test`. Expected: the stalled-read/open tests time out (≈10 s+), the unit test returns 0.

### Task 3: Implement

**Files:**
- Modify: `src/player.h` (add `_Atomic bool stopRequested;` next to `interrupted`, `:138`)
- Modify: `src/player.c` (`BarPlayerRequestStop` `:1537`, song-start reset `:926`, `BarPlayerFfmpegInterruptCb` `:1008`, `openStream` failure handling `:1050-1070`, read-error branch `:1334`)

- [ ] **Step 1:** In `BarPlayerRequestStop`, inside the existing `player->lock` section (next to `doQuit = true` / `++controlEpoch`), `atomic_store_explicit(&player->stopRequested, true, memory_order_release)`.
- [ ] **Step 2:** In the callback, return 1 if `atomic_load_explicit(&player->stopRequested, memory_order_acquire)`; keep the existing `interrupted` branches unchanged.
- [ ] **Step 3:** Reset `stopRequested` to false in the new-song reset block (`:902-926`), which already holds `player->lock` and checks `controlEpoch` before clearing `doQuit`. Because set and reset both happen under `player->lock`, a stop can never interleave with a reset: it is either before it (and the epoch check refuses the reset) or after it (and aborts the new song, as intended).
- [ ] **Step 4:** In `openStream` and the `av_read_frame` error branch, treat `AVERROR_EXIT` with `stopRequested` set as a requested stop: log at debug level, skip the stale-CDN/403 and retry paths, and end the song the same way a normal skip does.
- [ ] **Step 5:** Update the comment on `BarPlayerFfmpegInterruptCb` and the player section of `src/THREAD_SAFETY.md` (new atomic, who writes it, who reads it, why no lock).
- [ ] **Step 6:** Run the tests from Task 2; expected PASS.

### Task 4: Flaky test follow-through

- [ ] With the fix in place, run `test_player_paused_active_and_decoded_stop_paths_join_promptly` 50 times with the CPU saturated (one `yes > /dev/null` per core) on both backends; expect 0 failures. Record counts in the PR.
- [ ] Consider lowering its 10 s join helper to 2 s now that stop is prompt; keep the `latencyMs < 2000` assertion.

### Task 5: Full verification

- [ ] `PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-coverage`; ≥90% patch coverage on changed `src/` lines.
- [ ] `make test-all`, `make test-asan`, `make test-tsan` (0 warnings), `make test-ci-local`.
- [ ] `make test-e2e-fake` still passes (skip button spec exercises the stop path).
- [ ] Manual: run pianobar (isolated config, free port), start a song, block the stream mid-song (e.g. `pfctl` rule or a proxy that stops forwarding), press skip in the web UI; the next song starts within about 1 s.

**Acceptance:** Skip, disconnect and quit return control within 1 s during a stalled read or open. No retries or error events after a requested stop. Pause during a stall still resumes. Sanitizers clean.

## Open questions

- Q1: Should a stop during a stalled **open** also cancel the playlist prefetch for the next song, or only the current open? Default in this plan: only the current open.
- Q2: Is 1 s the right bound for the tests, or should they use 2 s for slow CI runners? Default: assert < 1 s locally, < 2 s when `CI` is set.
