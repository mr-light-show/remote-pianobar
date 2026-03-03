/*
Copyright (c) 2008-2011
	Lars-Dominik Braun <lars@6xq.net>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

/* Thread-safe state wrapper functions */

#include "bar_state.h"
#include "ui.h"
#include "log.h"
#include <assert.h>
#include <string.h>
#include <stdarg.h>

/*	State rwlock: read for getters, write for setters and BarStateCallPandora.
 *	LOCK HIERARCHY: This is Lock #1 in the hierarchy
 *	Must be acquired BEFORE player.lock if both are needed
 *	PROTECTS: Pandora state (stations, playlist, curStation, nextStation, ph)
 *	DURATION: Should be held for microseconds, not milliseconds
 *	NO I/O: Never hold this lock during network calls, disk I/O, or console output
 *	See src/THREAD_SAFETY.md for complete documentation
 */
static void state_rwlock_rdlock_internal(const BarApp_t *app, const char *operation) {
	#ifdef WEBSOCKET_ENABLED
	if (app->settings.uiMode == BAR_UI_MODE_BOTH) {
		pthread_rwlock_rdlock((pthread_rwlock_t *)&app->stateRwlock);
		log_write(DEBUG_UI, "State: Lock acquired (%s) (read)\n", operation);
	}
	#else
	(void)app;
	(void)operation;
	#endif
}

static void state_rwlock_wrlock_internal(const BarApp_t *app, const char *operation) {
	#ifdef WEBSOCKET_ENABLED
	if (app->settings.uiMode == BAR_UI_MODE_BOTH) {
		pthread_rwlock_wrlock((pthread_rwlock_t *)&app->stateRwlock);
		log_write(DEBUG_UI, "State: Lock acquired (%s) (write)\n", operation);
	}
	#else
	(void)app;
	(void)operation;
	#endif
}

__attribute__((format(printf, 3, 4)))
static void state_rwlock_unlock_internal(const BarApp_t *app, const char *operation,
                                         const char *format, ...) {
	#ifdef WEBSOCKET_ENABLED
	if (app->settings.uiMode == BAR_UI_MODE_BOTH) {
		if (format) {
			va_list args;
			va_start(args, format);
			char buffer[256];
			vsnprintf(buffer, sizeof(buffer), format, args);
			va_end(args);
			log_write(DEBUG_UI, "%s", buffer);
		}
		log_write(DEBUG_UI, "State: Lock released\n");
		pthread_rwlock_unlock((pthread_rwlock_t *)&app->stateRwlock);
	}
	#else
	(void)app;
	(void)operation;
	(void)format;
	#endif
}

/*	Macro for executing code with state write lock (void operations with optional debug)
 *  Usage: WITH_STATE_LOCK(app, "OperationName", "Debug: %s", context) { ... }
 *  Or: WITH_STATE_LOCK(app, "OperationName", NULL) { ... }
 */
#define WITH_STATE_LOCK(app, op_name, fmt, ...) \
	for (int _lock_held = (state_rwlock_wrlock_internal(app, op_name), \
	                        (fmt ? log_write(DEBUG_UI, fmt, ##__VA_ARGS__) : (void)0), 0); \
	     !_lock_held; \
	     state_rwlock_unlock_internal(app, op_name, NULL), _lock_held = 1)

/*	Macro for read-lock + return value (getters)
 */
#define WITH_STATE_LOCK_RETURN(app, op_name, result_var, fmt, ...) \
	for (int _lock_held = (state_rwlock_rdlock_internal(app, op_name), 0); \
	     !_lock_held; \
	     state_rwlock_unlock_internal(app, op_name, fmt, ##__VA_ARGS__), \
	     _lock_held = 1)

/*	Macro for write-lock + return value (e.g. BarStateCallPandora)
 */
#define WITH_STATE_LOCK_WRITE_RETURN(app, op_name, result_var, fmt, ...) \
	for (int _lock_held = (state_rwlock_wrlock_internal(app, op_name), 0); \
	     !_lock_held; \
	     state_rwlock_unlock_internal(app, op_name, fmt, ##__VA_ARGS__), \
	     _lock_held = 1)

/*	Initialize state rwlock (only in BOTH mode)
 */
void BarStateInit(BarApp_t *app) {
	assert(app != NULL);
	
	#ifdef WEBSOCKET_ENABLED
	if (app->settings.uiMode == BAR_UI_MODE_BOTH) {
		pthread_rwlock_init(&app->stateRwlock, NULL);
		log_write(DEBUG_UI, "State: Rwlock initialized\n");
	}
	#endif
}

/*	Destroy state rwlock (only in BOTH mode)
 */
void BarStateDestroy(BarApp_t *app) {
	assert(app != NULL);
	
	#ifdef WEBSOCKET_ENABLED
	if (app->settings.uiMode == BAR_UI_MODE_BOTH) {
		pthread_rwlock_destroy(&app->stateRwlock);
		log_write(DEBUG_UI, "State: Rwlock destroyed\n");
	}
	#endif
}

/*	Get next station (thread-safe)
 */
PianoStation_t *BarStateGetNextStation(const BarApp_t *app) {
	assert(app != NULL);
	
	PianoStation_t *station;
	WITH_STATE_LOCK_RETURN(app, "GetNextStation", station,
	                       "State: GetNextStation -> %s\n", station ? station->name : "null") {
		station = app->nextStation;
	}
	return station;
}

/*	Set next station (thread-safe)
 */
void BarStateSetNextStation(BarApp_t *app, PianoStation_t *station) {
	assert(app != NULL);
	
	WITH_STATE_LOCK(app, "SetNextStation", "State: SetNextStation <- %s\n", station ? station->name : "null") {
		app->nextStation = station;
	}
}

/*	Get current station (thread-safe)
 */
PianoStation_t *BarStateGetCurrentStation(const BarApp_t *app) {
	assert(app != NULL);
	
	PianoStation_t *station;
	WITH_STATE_LOCK_RETURN(app, "GetCurrentStation", station,
	                       "State: GetCurrentStation -> %s\n", station ? station->name : "null") {
		station = app->curStation;
	}
	return station;
}

/*	Set current station (thread-safe)
 */
void BarStateSetCurrentStation(BarApp_t *app, PianoStation_t *station) {
	assert(app != NULL);
	
	WITH_STATE_LOCK(app, "SetCurrentStation", "State: SetCurrentStation <- %s\n", station ? station->name : "null") {
		app->curStation = station;
	}
}

/*	Find station by ID (thread-safe)
 */
PianoStation_t *BarStateFindStationById(const BarApp_t *app, const char *id) {
	assert(app != NULL);
	
	PianoStation_t *station;
	WITH_STATE_LOCK_RETURN(app, "FindStationById", station,
	                       "State: FindStationById id=%s -> %s\n", id ? id : "null", station ? station->name : "null") {
		/* After app.stop, stations may be NULL - handle gracefully */
		if (app->ph.stations == NULL) {
			station = NULL;
		} else {
			station = PianoFindStationById(app->ph.stations, id);
		}
	}
	return station;
}

/*	Get station list (thread-safe)
 */
PianoStation_t *BarStateGetStationList(const BarApp_t *app) {
	assert(app != NULL);
	
	PianoStation_t *stations;
	WITH_STATE_LOCK_RETURN(app, "GetStationList", stations, NULL) {
		stations = app->ph.stations;
	}
	return stations;
}

/*	Get playlist (thread-safe)
 */
PianoSong_t *BarStateGetPlaylist(const BarApp_t *app) {
	assert(app != NULL);
	
	PianoSong_t *playlist;
	WITH_STATE_LOCK_RETURN(app, "GetPlaylist", playlist,
	                       "State: GetPlaylist -> %s\n", playlist ? playlist->title : "null") {
		playlist = app->playlist;
	}
	return playlist;
}

/*	Set playlist (thread-safe)
 */
void BarStateSetPlaylist(BarApp_t *app, PianoSong_t *playlist) {
	assert(app != NULL);
	
	WITH_STATE_LOCK(app, "SetPlaylist", "State: SetPlaylist <- %s\n", playlist ? playlist->title : "null") {
		app->playlist = playlist;
	}
}

/*	Drain playlist (destroy and clear) (thread-safe)
 */
void BarStateDrainPlaylist(BarApp_t *app) {
	assert(app != NULL);
	
	WITH_STATE_LOCK(app, "DrainPlaylist", NULL) {
		if (app->playlist != NULL) {
			PianoDestroyPlaylist(app->playlist);
			app->playlist = NULL;
		}
	}
}

/*	Switch station (drain playlist and set next station) (thread-safe)
 */
void BarStateSwitchStation(BarApp_t *app, PianoStation_t *station) {
	assert(app != NULL);
	
	WITH_STATE_LOCK(app, "SwitchStation", "State: SwitchStation <- %s\n", station ? station->name : "null") {
		/* Drain current playlist */
		if (app->playlist != NULL) {
			PianoDestroyPlaylist(app->playlist);
			app->playlist = NULL;
		}
		/* Set new station */
		app->nextStation = station;
	}
}

/*	Get player mode (thread-safe)
 */
BarPlayerMode BarStateGetPlayerMode(const BarApp_t *app) {
	assert(app != NULL);
	
	/* Player mode uses player->lock, not stateRwlock */
	return BarPlayerGetMode((player_t *)&app->player);
}

/*	Get player time (thread-safe)
 */
void BarStateGetPlayerTime(const BarApp_t *app, unsigned int *played, unsigned int *duration) {
	assert(app != NULL);
	
	/* Player time uses player->lock, not stateRwlock */
	pthread_mutex_lock((pthread_mutex_t *)&app->player.lock);
	if (played != NULL) {
		*played = app->player.songPlayed;
	}
	if (duration != NULL) {
		*duration = app->player.songDuration;
	}
	pthread_mutex_unlock((pthread_mutex_t *)&app->player.lock);
}

/*	Get player paused state (thread-safe)
 */
bool BarStateGetPlayerPaused(const BarApp_t *app) {
	assert(app != NULL);
	
	/* Player pause state uses player->lock, not stateRwlock */
	pthread_mutex_lock((pthread_mutex_t *)&app->player.lock);
	bool paused = app->player.doPause;
	pthread_mutex_unlock((pthread_mutex_t *)&app->player.lock);
	return paused;
}

/*	Make Pandora API call (thread-safe)
 *	
 *	IMPORTANT: This function holds stateRwlock (write) during network I/O.
 *	This is the ONE documented exception to the "no I/O under lock" rule.
 *	
 *	Why this is necessary:
 *	- BarUiPianoCall() performs: prepare → network → parse → modify state
 *	- The parse step (PianoResponse) directly modifies app->ph.stations,
 *	  app->ph.genreStations, and other Pandora state structures
 *	- These modifications must be atomic - we cannot allow other threads
 *	  to observe partially-updated state (e.g., playlist with some songs
 *	  from old request, some from new request)
 *	
 *	Performance impact:
 *	- Lock hold time: 100-500ms (typical Pandora API latency)
 *	- Call frequency: Low (startup, every ~30min, user actions)
 *	- Affected threads: WebSocket and CLI threads briefly blocked
 *	- Tradeoff: We accept brief blocking to guarantee state consistency
 *	
 *	Alternative considered but rejected:
 *	- Unlock → network call → lock → apply response
 *	- Requires deep refactoring of libpiano to support copy-on-write
 *	- Complex merge logic prone to race conditions and bugs
 *	- Cost/benefit analysis: not worth the implementation complexity
 *	
 *	See src/THREAD_SAFETY.md for complete documentation
 */
bool BarStateCallPandora(BarApp_t *app, PianoRequestType_t type,
                         void *data, PianoReturn_t *pRet, CURLcode *wRet) {
	assert(app != NULL);
	
	bool result;
	WITH_STATE_LOCK_WRITE_RETURN(app, "CallPandora", result,
	                             "State: CallPandora type=%d -> %s\n", type, result ? "true" : "false") {
		result = BarUiPianoCall(app, type, data, pRet, wRet);
	}
	return result;
}

/*	Check if logged in to Pandora
 */
bool BarStateIsPandoraConnected(const BarApp_t *app) {
	assert(app != NULL);
	
	/* User is connected if we have a listenerId from login */
	return app->ph.user.listenerId != NULL;
}

