# Playback Parked Idle + Wake-on-State-Change Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop the playback manager from polling every second while waiting for a web UI station selection, and wake it immediately when queue-related state changes.

**Architecture:** **3A** — add `BarStateSignalPlaybackManager()` called from all `BarStateSet*` / switch / drain paths; it broadcasts `app->player.cond` after releasing `stateRwlock` (never nest `stateRwlock` → `player.lock`). **3B** — when `PLAYER_DEAD` with no playlist and no `nextStation`, block on `pthread_cond_wait` instead of `pthread_cond_timedwait`; keep the 1-second timed wait only when there is playback work to do (active/idle queue, pause timeout, progress broadcast).

**Tech Stack:** C, pthreads, existing `BarApp_t` / `playback_manager.c` / `bar_state.c`, Check unit tests.

**Out of scope:** `autostart_station` wiring, moving state lock traces to `DEBUG_STATE`, web UI auto-select, idle Pandora disconnect.

---

## Problem (current behavior)

`BarPlaybackManagerThread` wakes every `PROGRESS_BROADCAST_INTERVAL_SECS` (1s) via `pthread_cond_timedwait`, even when:

- `player.mode == PLAYER_DEAD`
- `BarStateGetPlaylist()` → null
- `BarStateGetNextStation()` → null

Each wake runs the idle handler and hits state getters twice for playlist + once for `nextStation` → noisy logs and pointless lock traffic until the user sends `station.change`.

`BarUiSwitchStation` / `BarStateSetNextStation` do **not** signal the playback manager; response latency is up to ~1s after UI selection.

---

## File map

| File | Change |
|------|--------|
| `src/bar_state.h` | Declare `BarStateSignalPlaybackManager` |
| `src/bar_state.c` | Implement signal; call from setters |
| `src/playback_manager.c` | Parked wait vs timed wait; one-shot parked log |
| `src/playback_manager.h` | (optional) `BarPlaybackShouldParkIdle` for unit tests |
| `test/unit/test_playback_manager.c` | Park + wake tests |
| `test/unit/test_bar_state.c` | Signal does not deadlock (smoke) |
| `src/THREAD_SAFETY.md` | Document wake points and parked idle |

---

## Design details

### `BarStateSignalPlaybackManager(BarApp_t *app)`

```c
void BarStateSignalPlaybackManager(BarApp_t *app)
{
#ifdef WEBSOCKET_ENABLED
	if (app == NULL || !state_needs_lock(app)) {
		return;
	}
	pthread_mutex_lock(&app->player.lock);
	pthread_cond_broadcast(&app->player.cond);
	pthread_mutex_unlock(&app->player.lock);
#else
	(void)app;
#endif
}
```

**Call sites** (end of function body, **after** `WITH_STATE_LOCK` block releases `stateRwlock`):

- `BarStateSetNextStation`
- `BarStateSetPlaylist`
- `BarStateDrainPlaylist`
- `BarStateSwitchStation`

Do **not** call from getters or snapshot functions.

`BarPlayerSetMode` already broadcasts `player.cond` on mode transitions — no change needed there.

### Parked idle predicate

```c
static bool playback_should_park_idle(const BarApp_t *app, BarPlayerMode mode)
{
	if (mode != PLAYER_DEAD) {
		return false;
	}
	return BarStateGetPlaylist(app) == NULL
	    && BarStateGetNextStation(app) == NULL;
}
```

Extract to `BarPlaybackShouldParkIdle` in `playback_manager.c` and declare in `playback_manager.h` when unit-testing the predicate directly.

### Playback manager wait loop (restructure top of `while`)

```c
BarPlayerMode mode = BarPlayerGetMode(&app->player);
const bool park_idle = playback_should_park_idle(app, mode);

pthread_mutex_lock(&app->player.lock);
if (park_idle) {
	if (!atomic_load(&g_parkedLogged)) {
		atomic_store(&g_parkedLogged, true);
		log_write(DEBUG_UI, "PlaybackMgr: Parked (waiting for station)\n");
	}
	pthread_cond_wait(&app->player.cond, &app->player.lock);
} else {
	atomic_store(&g_parkedLogged, false);
	struct timespec timeout;
	clock_gettime(CLOCK_REALTIME, &timeout);
	timeout.tv_sec += PROGRESS_BROADCAST_INTERVAL_SECS;
	pthread_cond_timedwait(&app->player.cond, &app->player.lock, &timeout);
}
mode = app->player.mode;
bool isPaused = app->player.doPause;
time_t pauseStart = app->player.pauseStartTime;
pthread_mutex_unlock(&app->player.lock);
```

**Wake sources while parked:** `BarStateSignalPlaybackManager`, `BarPlayerSetMode`, play/pause/skip (`ui_act.c`), quit (`BarUiActQuit`), `BarPlaybackManagerStop`.

**After `doQuit`:** existing `while (!app->doQuit && g_running)` exits on next iteration; quit path already broadcasts `player.cond`.

### Idle handler cleanup (same PR, small)

In the `PLAYER_DEAD` block, read playlist once:

```c
PianoSong_t *playlist = BarStateGetPlaylist(app);
if (playlist != NULL) {
	/* advance */
	...
}
playlist = BarStateGetPlaylist(app);  /* may have advanced; or reuse local if advance sets null */
```

Keep behavior identical; optional second get only if advance occurred.

---

## Expected log behavior (after)

**Before station selected:**

```
PlaybackMgr: Player idle
PlaybackMgr: Parked (waiting for station)
(silence — blocked on cond_wait)
```

**User selects station (`station.change`):**

```
State: SetNextStation <- QuickMix
PlaybackMgr: Starting next song   (immediate, no 1s gap)
```

---

### Task 1: Playback wake signal from state setters

**Files:**
- Modify: `src/bar_state.h`
- Modify: `src/bar_state.c`
- Test: `test/unit/test_bar_state.c`

- [ ] **Step 1: Add declaration to `bar_state.h`**

```c
/* Wake playback manager after queue/station mutations (web/both only). */
void BarStateSignalPlaybackManager(BarApp_t *app);
```

- [ ] **Step 2: Implement in `bar_state.c`**

Use implementation above. Place after `state_needs_lock` helper.

- [ ] **Step 3: Call from four setters**

At the end of each function, after the `WITH_STATE_LOCK` / `WITH_STATE_LOCK_RETURN` scope:

```c
BarStateSignalPlaybackManager(app);
```

Functions: `BarStateSetNextStation`, `BarStateSetPlaylist`, `BarStateDrainPlaylist`, `BarStateSwitchStation`.

- [ ] **Step 4: Add smoke test `test_signal_playback_manager_no_deadlock`**

```c
START_TEST(test_signal_playback_manager_no_deadlock) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	pthread_cond_init(&app.player.cond, NULL);
	BarStateSignalPlaybackManager(&app);
	pthread_mutex_destroy(&app.player.lock);
	pthread_cond_destroy(&app.player.cond);
	BarStateDestroy(&app);
}
END_TEST
```

Register under existing `BarState` suite (`#ifdef WEBSOCKET_ENABLED`).

- [ ] **Step 5: Run tests**

```bash
cd /Users/khawes/stash/pianobar/remote-pianobar
make test-all
```

Expected: exit 0.

- [ ] **Step 6: Commit**

```bash
git add src/bar_state.h src/bar_state.c test/unit/test_bar_state.c
git commit -m "feat: signal playback manager on queue state changes"
```

---

### Task 2: Parked idle wait in playback manager

**Files:**
- Modify: `src/playback_manager.c`
- Modify: `src/playback_manager.h` (if exporting predicate)
- Test: `test/unit/test_playback_manager.c`

- [ ] **Step 1: Add `g_parkedLogged` atomic** next to `g_idleLogged` in `playback_manager.c`.

- [ ] **Step 2: Add `playback_should_park_idle` (or exported `BarPlaybackShouldParkIdle`).**

- [ ] **Step 3: Replace uniform `pthread_cond_timedwait` with park vs timed branch** per design above.

- [ ] **Step 4: Add unit test `test_should_park_idle_when_dead_and_empty`**

```c
START_TEST(test_should_park_idle_when_dead_and_empty) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	app.player.mode = PLAYER_DEAD;
	ck_assert(BarPlaybackShouldParkIdle(&app));
	pthread_mutex_destroy(&app.player.lock);
	BarStateDestroy(&app);
}
END_TEST
```

Add `test_should_not_park_when_next_station_set` with `BarStateSetNextStation(&app, &st)`.

- [ ] **Step 5: Add integration-style test `test_manager_wakes_on_set_next_station`**

```c
START_TEST(test_manager_wakes_on_set_next_station) {
	BarApp_t app;
	PianoStation_t st = { .id = "1", .name = "Test" };
	/* init web mode, state, player lock+cond, PLAYER_DEAD */
	ck_assert(BarPlaybackManagerStart(&app));
	usleep(50000); /* let thread park */
	BarStateSetNextStation(&app, &st); /* signals wake */
	usleep(100000);
	/* assert manager did not hang: g_running still true, thread joinable via Stop */
	BarPlaybackManagerStop(&app);
	...
}
END_TEST
```

This test verifies no deadlock and that `SetNextStation` unblocks the manager; full playlist fetch is not required.

- [ ] **Step 6: Run `make test-all`** — exit 0.

- [ ] **Step 7: Commit**

```bash
git add src/playback_manager.c src/playback_manager.h test/unit/test_playback_manager.c
git commit -m "feat: park playback manager until station or playlist is ready"
```

---

### Task 3: Documentation

**Files:**
- Modify: `src/THREAD_SAFETY.md`

- [ ] **Step 1: Add subsection "Playback manager wake"**

Document:

- Parked when `PLAYER_DEAD` + empty queue
- `BarStateSignalPlaybackManager` call sites
- Lock rule: signal **after** releasing `stateRwlock`; only acquire `player.lock` inside signal
- `BarPlayerSetMode` still wakes on mode changes

- [ ] **Step 2: Commit**

```bash
git add src/THREAD_SAFETY.md
git commit -m "docs: playback parked idle and wake protocol"
```

---

## Self-review (spec coverage)

| Requirement | Task |
|-------------|------|
| 3A: wake on `SetNextStation` / playlist changes | Task 1 |
| 3A: wake on `SwitchStation` / drain | Task 1 |
| 3B: no 1 Hz poll when parked | Task 2 |
| 3B: keep 1s timed wait when playing / queue work | Task 2 |
| Shutdown / quit still works | Task 2 (existing broadcasts) |
| Tests | Tasks 1–2 |
| Thread-safety docs | Task 3 |

## Risks

| Risk | Mitigation |
|------|------------|
| Missed wake if state mutated without setter | Grep `app->playlist` / `app->nextStation` — all paths go through `bar_state.c` today |
| `pthread_cond_wait` without timeout during shutdown | `BarPlaybackManagerStop` and quit already broadcast `player.cond` |
| Spurious wakeup | Re-evaluate `park_idle` each loop iteration |
| Fetch playlist on wake with `nextStation` set but no network | Existing behavior; unchanged |

## Manual verification

```bash
PIANOBAR_DEBUG=4 ./pianobar   # UI only, not STATE
```

1. Start web mode, log in, do **not** select station → expect one `Parked (waiting for station)` then silence.
2. Open web UI, select station → playback starts without ~1s delay; `Network` playlist fetch follows.
3. Disconnect / quit → process exits cleanly.

---

## Execution handoff

**Plan complete and saved to `docs/superpowers/plans/2026-06-17-playback-parked-idle.md`.**

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
2. **Inline Execution** — implement all tasks in one session with `executing-plans`

**Which approach?**
