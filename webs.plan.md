# Minimal-Change Mutex Protection for WebSocket Threading

## Strategy: Coarse-Grained Locking

**Key Insight:** Lock once at entry points, unlock at exit. **Zero changes to inner functions.**

**What gets modified:**

- [`src/main.h`](src/main.h) - Add 1 field (mutex)
- [`src/main.c`](src/main.c) - Add 4 lines total (init, destroy, 2 lock/unlock)
- [`src/websocket/protocol/socketio.c`](src/websocket/protocol/socketio.c) - Add lock/unlock to 16 handler functions

**What does NOT get modified:**

- `src/ui_act.c` - **NO CHANGES**
- `src/ui.c` - **NO CHANGES**  
- `src/ui_dispatch.c` - **NO CHANGES**
- Any playlist manipulation code
- Any station iteration code

## Implementation Steps

### Step 1: Add Mutex to BarApp_t (1 line)

**File:** [`src/main.h`](src/main.h) - After line 37 (after `player_t player;`)

```c
typedef struct {
	PianoHandle_t ph;
	CURL *http;
	player_t player;
	pthread_mutex_t stateMutex;  // ADD THIS LINE - protects playlist/stations/curStation/nextStation
	BarSettings_t settings;
	/* ... rest unchanged ... */
} BarApp_t;
```

### Step 2: Initialize Mutex (1 line)

**File:** [`src/main.c`](src/main.c) - After line 516 (after `BarPlayerInit()`)

```c
BarPlayerInit (&app.player, &app.settings);
pthread_mutex_init(&app.stateMutex, NULL);  // ADD THIS LINE
BarSettingsInit (&app.settings);
```

### Step 3: Destroy Mutex (1 line)

**File:** [`src/main.c`](src/main.c) - Before line 631 (before final `return 0`)

```c
BarSettingsDestroy (&app.settings);
pthread_mutex_destroy(&app.stateMutex);  // ADD THIS LINE
/* restore terminal attributes */
```

### Step 4: Lock Main Loop Body (2 lines)

**File:** [`src/main.c`](src/main.c) - Function `BarMainLoop()`

Wrap **entire while loop body** with single lock/unlock:

```c
static void BarMainLoop (BarApp_t *app) {
	pthread_t playerThread;
	
	/* ... initialization code (login, get stations) unchanged ... */
	
	player_t * const player = &app->player;

	while (!app->doQuit) {
		pthread_mutex_lock(&app->stateMutex);  // ADD THIS LINE at ~line 386
		
		/* song finished playing, clean up things/scrobble song */
		if (BarPlayerGetMode (player) == PLAYER_FINISHED) {
			/* ... all existing code unchanged ... */
		}
		
		/* ... rest of main loop ~90 lines unchanged ... */
		
		pthread_mutex_unlock(&app->stateMutex);  // ADD THIS LINE at ~line 475
	}
	
	/* ... cleanup unchanged ... */
}
```

**CRITICAL:** Lock covers lines 386-475 (entire `while (!app->doQuit)` body).

### Step 5: Delete Command Queue Processing

**File:** [`src/main.c`](src/main.c) - Delete lines 417-465

**Remove this block:**

```c
#ifdef WEBSOCKET_ENABLED
/* Process WebSocket commands from web clients (queued by WS thread) */
if (app->settings.uiMode != BAR_UI_MODE_CLI && app->wsContext) {
	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	BarWsMessage_t *msg;
	while ((msg = BarWsQueuePop(&ctx->commandQueue, 0)) != NULL) {
		/* ... ~40 lines ... */
	}
	BarWsBroadcastProgress(app);
}
#endif
```

**Replace with:**

```c
#ifdef WEBSOCKET_ENABLED
if (app->settings.uiMode != BAR_UI_MODE_CLI && app->wsContext) {
	BarWsBroadcastProgress(app);
}
#endif
```

### Step 6: Wrap WebSocket Handlers (16 functions, 2 lines each)

**File:** [`src/websocket/protocol/socketio.c`](src/websocket/protocol/socketio.c)

Add `pthread_mutex_lock()` at START and `pthread_mutex_unlock()` at END of each function:

**Pattern for ALL handlers:**

```c
void BarSocketIoHandleXYZ(BarApp_t *app, ...) {
	/* Early returns BEFORE lock */
	if (!app || !param) return;
	
	pthread_mutex_lock(&app->stateMutex);  // ADD THIS
	
	/* ... entire existing function body unchanged ... */
	
	pthread_mutex_unlock(&app->stateMutex);  // ADD THIS
}
```

**List of 16 functions to wrap:**

1. **Line 1405** - `BarSocketIoHandleAction()`
2. **Line 1457** - `BarSocketIoHandleChangeStation()`  
3. **Line 1480** - `BarSocketIoHandleSetQuickMix()`
4. **Line 1538** - `BarSocketIoHandleDeleteStation()`
5. **Line 1621** - `BarSocketIoHandleCreateStationFrom()`
6. **Line 1675** - `BarSocketIoHandleQuery()`
7. **Line 705** - `BarSocketIoHandleGetGenres()`
8. **Line 729** - `BarSocketIoHandleAddGenre()`
9. **Line 772** - `BarSocketIoHandleAddMusic()`
10. **Line 828** - `BarSocketIoHandleRenameStation()`
11. **Line 886** - `BarSocketIoHandleGetStationModes()`
12. **Line 968** - `BarSocketIoHandleSetStationMode()`
13. **Line 1014** - `BarSocketIoHandleGetStationInfo()`
14. **Line 1123** - `BarSocketIoHandleDeleteSeed()`
15. **Line 1210** - `BarSocketIoHandleDeleteFeedback()`
16. **Line 1339** - `BarSocketIoHandleSearchMusic()`

### Step 7: Update BarSocketIoHandleAction

**File:** [`src/websocket/protocol/socketio.c`](src/websocket/protocol/socketio.c) - Line 1405

**Replace entire function** (remove queue push, add direct dispatch):

```c
void BarSocketIoHandleAction(BarApp_t *app, const char *action, json_object *data) {
	const char *translated;
	
	if (!app || !action || !app->wsContext) return;
	
	translated = BarSocketIoTranslateCommand(action);
	if (!translated) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Unknown action: %s\n", action);
		return;
	}
	
	pthread_mutex_lock(&app->stateMutex);  // LOCK
	
	/* Special handling for volume.set */
	if (strcmp(action, "volume.set") == 0 && data) {
		json_object *volumeObj;
		if (json_object_object_get_ex(data, "volume", &volumeObj)) {
			int volumePercent = json_object_get_int(volumeObj);
			int volumeDb = sliderToDb(volumePercent, app->settings.maxGain);
			
			app->settings.volume = volumeDb;
			BarPlayerSetVolume(&app->player);
			BarSocketIoEmitVolume(app, volumeDb);
			
			pthread_mutex_unlock(&app->stateMutex);
			return;
		}
	}
	
	/* Direct dispatch - NO QUEUE */
	BarUiDispatch(app, translated[0], app->curStation, app->playlist,
	              false, BAR_DC_GLOBAL | BAR_DC_STATION | BAR_DC_SONG);
	
	/* Handle state updates for specific commands */
	if (translated[0] == '+' || translated[0] == '-') {
		if (app->playlist) {
			BarSocketIoEmitStart(app);
		}
	}
	
	pthread_mutex_unlock(&app->stateMutex);  // UNLOCK
}
```

### Step 8: Remove Command Queue Infrastructure

**File:** [`src/websocket/core/websocket.h`](src/websocket/core/websocket.h) - Line ~67

**Remove from BarWsContext_t:**

```c
typedef struct {
	/* ... other fields ... */
	// DELETE THIS LINE:
	// BarWsQueue_t commandQueue;
	/* ... keep broadcast buckets ... */
} BarWsContext_t;
```

**File:** [`src/websocket/core/websocket.c`](src/websocket/core/websocket.c)

**Line ~439** - Remove initialization:

```c
// DELETE THIS LINE:
// BarWsQueueInit(&ctx->commandQueue, 50, ctx->context);
```

**Line ~507** - Remove cleanup:

```c
// DELETE THIS LINE:
// BarWsQueueDestroy(&ctx->commandQueue);
```

## Testing Checklist

- [ ] CLI-only mode still works
- [ ] Both mode: CLI and web work concurrently
- [ ] Web-only mode works
- [ ] Rapid station changes don't crash
- [ ] Love/ban during playlist update works
- [ ] Multiple web clients work
- [ ] No audio stuttering
- [ ] No deadlocks after 10 minutes

## Summary

**Total Lines Changed:**

- `main.h`: +1 line
- `main.c`: +4 lines (init/destroy/lock/unlock), -48 lines (queue removal)
- `socketio.c`: +32 lines (16 functions × 2 lines), ~50 lines modified (HandleAction rewrite)
- `websocket.h`: -1 line
- `websocket.c`: -2 lines

**Files NOT Modified:**

- All of `ui_act.c` (27 action functions)
- All of `ui.c` (selection/helper functions)
- `ui_dispatch.c`
- Any internal helper functions

**Why This Works:**

- Main loop holds lock for entire iteration → CLI commands safe
- WebSocket handlers hold lock for entire operation → Web commands safe  
- All inner functions called with lock already held → No changes needed
- Coarse-grained = simple, no deadlock risk

## Estimated Effort

**4-6 hours** implementation + **2-4 hours** testing = **1 day total**

