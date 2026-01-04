# Thread Safety in Pianobar

This document describes the threading model, lock hierarchy, and safe coding patterns for pianobar's concurrent architecture.

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Lock Hierarchy](#lock-hierarchy)
3. [Safe Patterns](#safe-patterns)
4. [Unsafe Patterns](#unsafe-patterns)
5. [Guidelines for Contributors](#guidelines-for-contributors)
6. [Debugging Tools](#debugging-tools)

---

## Architecture Overview

Pianobar supports two UI modes:

- **CLI-only mode** (`BAR_UI_MODE_CLI`): Single-threaded, no locking needed
- **Both mode** (`BAR_UI_MODE_BOTH`): Multi-threaded with CLI + WebSocket threads

### Threading Model (BOTH Mode)

```
┌─────────────────────┐      ┌──────────────────────┐      ┌─────────────────────┐
│   Main/CLI Thread   │      │   Player Thread      │      │   WebSocket Thread  │
├─────────────────────┤      ├──────────────────────┤      ├─────────────────────┤
│ - User input (CLI)  │      │ - Audio decoding     │      │ - libwebsockets     │
│ - Station selection │      │ - Playback timing    │      │ - Client connections│
│ - Volume control    │      │ - Buffer management  │      │ - WebUI commands    │
│ - Pandora API calls │      │ - Song transitions   │      │ - State broadcasts  │
└─────────────────────┘      └──────────────────────┘      └─────────────────────┘
         │                            │                              │
         └────────────────────────────┴──────────────────────────────┘
                                      │
                         ┌────────────▼───────────┐
                         │   Shared State (app)   │
                         ├────────────────────────┤
                         │ - stations (Pandora)   │
                         │ - playlist             │
                         │ - curStation           │
                         │ - nextStation          │
                         │ - player.doPause       │
                         │ - player.songPlayed    │
                         │ - player.songDuration  │
                         └────────────────────────┘
```

### Three Independent Locks

Pianobar uses **three independent locks** that protect different data:

| Lock | Purpose | Scope | File |
|------|---------|-------|------|
| `app->stateMutex` | Protects Pandora state (stations, playlist, curStation, nextStation) | Only in `BAR_UI_MODE_BOTH` | [`bar_state.c`](bar_state.c) |
| `app->player.lock` | Protects player state (doPause, songPlayed, songDuration, mode) | Always active | [`player.c`](player.c) |
| libwebsockets internal | Protects WebSocket connection state | Managed by libwebsockets | N/A |

**Key Design Principle:** These locks are **independent** and protect **non-overlapping data**. This minimizes lock contention and simplifies reasoning about deadlocks.

---

## Lock Hierarchy

### Lock Ordering Rules

When multiple locks must be acquired, **always follow this order**:

```
1. app->stateMutex     (Pandora state)
   ↓
2. app->player.lock    (Player state)
```

**Never reverse this order.** Violating lock ordering is the #1 cause of deadlocks.

### Lock Duration Guidelines

- **Minimize lock duration**: Hold locks for the shortest time possible
- **No I/O under lock**: Never make network calls, read files, or print to console while holding a lock
- **No allocations under lock**: Avoid `malloc`/`free` while holding locks (use stack buffers or pre-allocate)
- **No nested locks**: Prefer releasing one lock before acquiring another

### Conditional Locking

`app->stateMutex` is **conditionally active**:

```c
#ifdef WEBSOCKET_ENABLED
if (app->settings.uiMode == BAR_UI_MODE_BOTH) {
    pthread_mutex_lock(&app->stateMutex);
    // ... critical section ...
    pthread_mutex_unlock(&app->stateMutex);
}
#endif
```

In CLI-only mode, the lock operations are no-ops (optimized away). This is implemented via the `WITH_STATE_LOCK` macros in [`bar_state.c`](bar_state.c).

---

## Safe Patterns

### Pattern 1: Reading Shared State

**Use the state getter functions** - they handle locking automatically:

```c
// ✓ SAFE: Thread-safe getter
PianoStation_t *station = BarStateGetCurrentStation(app);
if (station) {
    printf("Current station: %s\n", station->name);
}
```

**Implementation** (from [`bar_state.c`](bar_state.c)):

```c
PianoStation_t *BarStateGetCurrentStation(const BarApp_t *app) {
    assert(app != NULL);
    
    PianoStation_t *station;
    WITH_STATE_LOCK_RETURN(app, "GetCurrentStation", station,
                           "-> %s", station ? station->name : "null") {
        station = app->curStation;
    }
    return station;
}
```

**Key points:**
- Lock is acquired, value is copied to local variable, lock is released
- The returned pointer is safe to use after the function returns (stations are reference-counted by libpiano)

### Pattern 2: Modifying Shared State

**Use the state setter functions**:

```c
// ✓ SAFE: Thread-safe setter
BarStateSetNextStation(app, newStation);
```

### Pattern 3: Accessing Player State

**Direct lock/unlock for player state**:

```c
// ✓ SAFE: Direct player lock usage
pthread_mutex_lock(&app->player.lock);
bool paused = app->player.doPause;
unsigned int played = app->player.songPlayed;
pthread_mutex_unlock(&app->player.lock);
```

**Alternative: Use helper functions**:

```c
// ✓ SAFE: Helper function
bool paused = BarStateGetPlayerPaused(app);

// ✓ SAFE: Get both values atomically
unsigned int played, duration;
BarStateGetPlayerTime(app, &played, &duration);
```

### Pattern 4: Making Pandora API Calls

**Always use the wrapper function**:

```c
// ✓ SAFE: Wrapper handles locking
PianoReturn_t pRet;
CURLcode wRet;
bool success = BarStateCallPandora(app, PIANO_REQUEST_GET_PLAYLIST, 
                                    &reqData, &pRet, &wRet);
```

**Why:** The wrapper (`BarStateCallPandora` in [`bar_state.c`](bar_state.c)) ensures that:
1. The `stateMutex` is held during the API call
2. State modifications from the response are atomic
3. Other threads see consistent state (e.g., not partial playlist updates)

### Pattern 5: Switching Stations

**Use the atomic switch function**:

```c
// ✓ SAFE: Atomic station switch with playlist drain
BarStateSwitchStation(app, newStation);
```

**Why:** This function (in [`bar_state.c`](bar_state.c)) atomically:
1. Locks `stateMutex`
2. Drains the current playlist
3. Sets `nextStation`
4. Unlocks

Without this atomicity, the player thread could see an inconsistent state (e.g., playlist from old station but `nextStation` pointing to new station).

### Pattern 6: Broadcasting State to WebSocket Clients

**Always call broadcast functions AFTER releasing locks**:

```c
// ✓ SAFE: Lock, modify, unlock, THEN broadcast
pthread_mutex_lock(&app->player.lock);
app->player.doPause = true;
pthread_cond_broadcast(&app->player.cond);
pthread_mutex_unlock(&app->player.lock);

// Broadcast happens AFTER lock is released
BarWsBroadcastPlayState(app);
```

**Why:** Broadcast functions may acquire locks internally. Calling them while holding a lock risks nested locking and contention.

---

## Unsafe Patterns

### Anti-Pattern 1: Direct State Access Without Lock

```c
// ✗ UNSAFE: Race condition
if (app->playlist != NULL) {
    printf("Song: %s\n", app->playlist->title);  // May crash!
}
```

**Problem:** Between the NULL check and the dereference, another thread could modify `app->playlist`.

**Fix:** Use the getter:

```c
// ✓ SAFE
PianoSong_t *song = BarStateGetPlaylist(app);
if (song) {
    printf("Song: %s\n", song->title);
}
```

### Anti-Pattern 2: Reversed Lock Order

```c
// ✗ UNSAFE: Wrong lock order (DEADLOCK RISK)
pthread_mutex_lock(&app->player.lock);        // Lock 2 first
// ... some work ...
pthread_mutex_lock(&app->stateMutex);         // Then Lock 1
// ... critical section ...
pthread_mutex_unlock(&app->stateMutex);
pthread_mutex_unlock(&app->player.lock);
```

**Problem:** If another thread locks in the correct order (`stateMutex` → `player.lock`), both threads will deadlock.

**Fix:** Always lock in hierarchy order:

```c
// ✓ SAFE: Correct lock order
pthread_mutex_lock(&app->stateMutex);          // Lock 1 first
pthread_mutex_lock(&app->player.lock);         // Then Lock 2
// ... critical section ...
pthread_mutex_unlock(&app->player.lock);
pthread_mutex_unlock(&app->stateMutex);
```

**Even better:** Avoid nested locks entirely by using state getters/setters.

### Anti-Pattern 3: Holding Lock During I/O

```c
// ✗ UNSAFE: Network I/O under lock
pthread_mutex_lock(&app->stateMutex);
BarUiPianoCall(app, PIANO_REQUEST_GET_PLAYLIST, ...);  // Blocks for seconds!
pthread_mutex_unlock(&app->stateMutex);
```

**Problem:** Network calls can take seconds. Holding the lock blocks all other threads from accessing state.

**Fix:** Use the wrapper (which is designed for this):

```c
// ✓ SAFE: Wrapper handles locking efficiently
BarStateCallPandora(app, PIANO_REQUEST_GET_PLAYLIST, ...);
```

### Anti-Pattern 4: Returning with Lock Held

```c
// ✗ UNSAFE: Function returns with lock held
PianoStation_t *getStation(BarApp_t *app) {
    pthread_mutex_lock(&app->stateMutex);
    return app->curStation;  // LOCK STILL HELD!
}
```

**Problem:** Caller has no way to unlock. This causes deadlock on next lock attempt.

**Fix:** Always unlock before returning:

```c
// ✓ SAFE: Unlock before return
PianoStation_t *getStation(BarApp_t *app) {
    pthread_mutex_lock(&app->stateMutex);
    PianoStation_t *station = app->curStation;
    pthread_mutex_unlock(&app->stateMutex);
    return station;
}
```

**Even better:** Use the `WITH_STATE_LOCK_RETURN` macro which handles this automatically.

### Anti-Pattern 5: Redundant Lock Acquisition

```c
// ✗ UNSAFE: Lock acquired twice in same call chain
void outerFunction(BarApp_t *app) {
    pthread_mutex_lock(&app->player.lock);
    innerFunction(app);  // Also locks player.lock!
    pthread_mutex_unlock(&app->player.lock);
}

void innerFunction(BarApp_t *app) {
    pthread_mutex_lock(&app->player.lock);  // DEADLOCK!
    // ...
    pthread_mutex_unlock(&app->player.lock);
}
```

**Problem:** pthreads mutexes are **not recursive** by default. The second lock attempt will deadlock.

**Fix 1:** Don't call inner functions that acquire the same lock:

```c
// ✓ SAFE: Inner function doesn't lock
void outerFunction(BarApp_t *app) {
    pthread_mutex_lock(&app->player.lock);
    innerFunctionNoLock(app);
    pthread_mutex_unlock(&app->player.lock);
}
```

**Fix 2:** Release the lock before calling the inner function:

```c
// ✓ SAFE: Release lock before call
void outerFunction(BarApp_t *app) {
    pthread_mutex_lock(&app->player.lock);
    bool paused = app->player.doPause;
    pthread_mutex_unlock(&app->player.lock);
    
    innerFunction(app);  // Can safely acquire lock
}
```

---

## Guidelines for Contributors

### When Adding New Features

1. **Identify shared data**: What state will be accessed by multiple threads?
2. **Use existing abstractions**: Can you use `BarStateGet*()` or `BarStateSet*()` functions?
3. **Minimize critical sections**: Keep lock-held time as short as possible
4. **Test with BOTH mode**: Always test with `ui_mode = both` in config
5. **Enable debug logging**: Use `PIANOBAR_DEBUG=8` to see lock acquisition traces

### Checklist for New Code

- [ ] Uses state getters/setters instead of direct access
- [ ] Locks are acquired in hierarchy order (stateMutex → player.lock)
- [ ] No I/O operations under lock
- [ ] No memory allocation/deallocation under lock
- [ ] Locks are released on all code paths (including error paths)
- [ ] No recursive lock acquisition
- [ ] Broadcast functions called AFTER locks are released

### Common Scenarios

#### Scenario 1: Adding a New UI Action

```c
// Template for thread-safe UI action
BarUiActCallback(BarUiActMyNewAction) {
    // 1. Get required state (locks/unlocks internally)
    PianoStation_t *station = BarStateGetCurrentStation(app);
    if (!station) {
        BarUiMsg(&app->settings, MSG_ERR, "No station selected.\n");
        return;
    }
    
    // 2. Perform non-locked work (API calls, user input, etc.)
    PianoReturn_t pRet;
    CURLcode wRet;
    bool success = BarUiPianoCall(app, PIANO_REQUEST_XYZ, ...);
    
    // 3. Update state atomically (if needed)
    if (success) {
        BarStateSetNextStation(app, newStation);
    }
    
    // 4. Broadcast changes to WebSocket clients
    BarWsBroadcastStations(app);
}
```

#### Scenario 2: Adding a New WebSocket Handler

```c
// Template for thread-safe WebSocket handler
void BarSocketIoHandleMyEvent(BarApp_t *app, json_object *data) {
    if (!app || !data) {
        return;
    }
    
    // 1. Parse input (no locks needed)
    const char *param = json_object_get_string(
        json_object_object_get(data, "param"));
    
    // 2. Get state using thread-safe getters
    PianoStation_t *station = BarStateFindStationById(app, param);
    
    // 3. Call UI action (which handles locking)
    if (station) {
        BarUiSwitchStation(app, station);
    }
    
    // 4. Broadcast updated state
    BarSocketIoEmitStations(app);
}
```

#### Scenario 3: Reading Multiple State Values Atomically

If you need multiple values to be consistent (read at the same instant):

```c
// ✓ SAFE: Atomic read of multiple values
pthread_mutex_lock(&app->player.lock);
bool paused = app->player.doPause;
unsigned int played = app->player.songPlayed;
unsigned int duration = app->player.songDuration;
pthread_mutex_unlock(&app->player.lock);

// Now all three values are from the same instant in time
```

**Don't** do multiple getter calls for atomicity:

```c
// ✗ UNSAFE: Values may be inconsistent
bool paused = BarStateGetPlayerPaused(app);        // Locks/unlocks
unsigned int played, duration;
BarStateGetPlayerTime(app, &played, &duration);    // Locks/unlocks again

// Between the two calls, player state may have changed!
```

---

## Debugging Tools

### 1. Debug Logging

Set `PIANOBAR_DEBUG=8` to see lock acquisition traces:

```bash
$ PIANOBAR_DEBUG=8 ./pianobar
...
State: Lock acquired (GetCurrentStation)
State: GetCurrentStation -> Radio Paradise
State: Lock released (GetCurrentStation)
```

This helps identify:
- Lock ordering violations
- Long-held locks
- Unexpected nested locking

### 2. Lock Visualization

With `DEBUG_UI` enabled, you can trace the call stack:

```
State: Lock acquired (SwitchStation)
State: SwitchStation <- Radio Paradise
State: Lock released (SwitchStation)
```

### 3. ThreadSanitizer (Advanced)

For serious deadlock debugging, compile with ThreadSanitizer:

```bash
$ make clean
$ CFLAGS="-fsanitize=thread -g" make
$ ./pianobar
```

ThreadSanitizer will detect:
- Data races
- Deadlocks
- Lock order inversions

**Note:** ThreadSanitizer has significant performance overhead. Only use for debugging.

### 4. Stress Testing

Test concurrent access by:

1. **Rapid CLI commands**: Hold down a key (e.g., 'n' for next song)
2. **Multiple WebSocket clients**: Open 5+ browser tabs
3. **Concurrent actions**: Press CLI keys while clicking WebUI buttons

Monitor for:
- Crashes
- Hangs (deadlocks)
- Inconsistent state (e.g., wrong song info displayed)

### 5. Lock Assertions (Optional)

For paranoid debugging, add assertions to verify lock state. These are enabled by compiling with `-DDEBUG`:

```bash
$ make clean
$ make DEBUG=1
$ ./pianobar
```

**Available assertions** (defined in [`bar_state.h`](bar_state.h)):

```c
ASSERT_STATE_LOCK_HELD(app);        // Verify stateMutex is held
ASSERT_STATE_LOCK_NOT_HELD(app);    // Verify stateMutex is free
ASSERT_PLAYER_LOCK_HELD(player);    // Verify player.lock is held
ASSERT_PLAYER_LOCK_NOT_HELD(player); // Verify player.lock is free
```

**Example usage:**

```c
// Function that expects stateMutex to be held by caller
static void modifyStateNoLock(BarApp_t *app, PianoStation_t *station) {
    ASSERT_STATE_LOCK_HELD(app);  // Verify contract
    app->curStation = station;
}

// Function that must NOT hold any locks
static void doNetworkCall(BarApp_t *app) {
    ASSERT_STATE_LOCK_NOT_HELD(app);     // Verify no locks held
    ASSERT_PLAYER_LOCK_NOT_HELD(&app->player);
    
    // Safe to do blocking I/O
    BarUiPianoCall(app, PIANO_REQUEST_XYZ, ...);
}
```

**How they work:**
- Use `pthread_mutex_trylock()` to test lock state
- If trylock succeeds, lock is free (assertion fails if we expected it held)
- If trylock returns `EBUSY`, lock is held (assertion succeeds)
- Zero runtime overhead in release builds (compiled out)

---

## Summary

### The Golden Rules

1. **Use state abstractions**: Always use `BarState*()` functions
2. **Lock ordering**: `stateMutex` before `player.lock` (if both needed)
3. **Minimize lock duration**: Hold locks for microseconds, not milliseconds
4. **No I/O under lock**: Release locks before network/disk/console operations
5. **Broadcast after unlock**: Call `BarWsBroadcast*()` functions after releasing locks
6. **Test with BOTH mode**: Always test multi-threaded execution

### Quick Reference

| Operation | Function | Lock Used |
|-----------|----------|-----------|
| Get current station | `BarStateGetCurrentStation()` | `stateMutex` |
| Set next station | `BarStateSetNextStation()` | `stateMutex` |
| Find station by ID | `BarStateFindStationById()` | `stateMutex` |
| Switch station | `BarStateSwitchStation()` | `stateMutex` |
| Get playlist | `BarStateGetPlaylist()` | `stateMutex` |
| Drain playlist | `BarStateDrainPlaylist()` | `stateMutex` |
| Get player paused | `BarStateGetPlayerPaused()` | `player.lock` |
| Get player time | `BarStateGetPlayerTime()` | `player.lock` |
| Make Pandora API call | `BarStateCallPandora()` | `stateMutex` |

### Resources

- [`bar_state.c`](bar_state.c) - Thread-safe state access functions
- [`bar_state.h`](bar_state.h) - State API declarations
- [`player.c`](player.c) - Player thread implementation
- [`websocket/protocol/socketio.c`](websocket/protocol/socketio.c) - WebSocket handlers
- [`ui_act.c`](ui_act.c) - UI action callbacks

---

**Last Updated:** January 2026  
**Pianobar Version:** 2.0.0+

