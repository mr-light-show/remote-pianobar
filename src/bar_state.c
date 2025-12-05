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
#include "debug.h"
#include <assert.h>
#include <string.h>
#include <stdarg.h>

/*	Lock state mutex with logging (only in BOTH mode when WebSocket enabled)
 */
static void state_mutex_lock_internal(const BarApp_t *app, const char *operation) {
	#ifdef WEBSOCKET_ENABLED
	/* Only lock in BOTH mode - CLI and Web threads both accessing state */
	if (app->settings.uiMode == BAR_UI_MODE_BOTH) {
		pthread_mutex_lock((pthread_mutex_t *)&app->stateMutex);
		debugPrint(DEBUG_UI, "State: Lock acquired (%s)\n", operation);
	}
	#else
	(void)app;
	(void)operation;
	#endif
}

/*	Unlock state mutex with logging (only in BOTH mode when WebSocket enabled)
 */
__attribute__((format(printf, 3, 4)))
static void state_mutex_unlock_internal(const BarApp_t *app, const char *operation, 
                                        const char *format, ...) {
	#ifdef WEBSOCKET_ENABLED
	/* Only unlock in BOTH mode */
	if (app->settings.uiMode == BAR_UI_MODE_BOTH) {
		if (format) {
			va_list args;
			va_start(args, format);
			char buffer[256];
			vsnprintf(buffer, sizeof(buffer), format, args);
			va_end(args);
			debugPrint(DEBUG_UI, "State: %s\n", buffer);
		}
		debugPrint(DEBUG_UI, "State: Lock released (%s)\n", operation);
		pthread_mutex_unlock((pthread_mutex_t *)&app->stateMutex);
	}
	#else
	(void)app;
	(void)operation;
	(void)format;
	#endif
}

/*	Macro for executing code with state lock (void operations with optional debug)
 *  Usage: WITH_STATE_LOCK(app, "OperationName", "Debug: %s", context) {
 *             // your code here
 *         }
 *  Or: WITH_STATE_LOCK(app, "OperationName", NULL) { ... }
 */
#define WITH_STATE_LOCK(app, op_name, fmt, ...) \
	for (int _lock_held = (state_mutex_lock_internal(app, op_name), \
	                        (fmt ? debugPrint(DEBUG_UI, fmt, ##__VA_ARGS__) : (void)0), 0); \
	     !_lock_held; \
	     state_mutex_unlock_internal(app, op_name, NULL), _lock_held = 1)

/*	Macro for executing code with state lock (operations with return value)
 *  Usage: PianoStation_t *result;
 *         WITH_STATE_LOCK_RETURN(app, "OperationName", result, "-> %s", result_description) {
 *             result = app->nextStation;
 *         }
 *         return result;
 */
#define WITH_STATE_LOCK_RETURN(app, op_name, result_var, fmt, ...) \
	for (int _lock_held = (state_mutex_lock_internal(app, op_name), 0); \
	     !_lock_held; \
	     state_mutex_unlock_internal(app, op_name, fmt, ##__VA_ARGS__), \
	     _lock_held = 1)

/*	Initialize state mutex (only in BOTH mode)
 */
void BarStateInit(BarApp_t *app) {
	assert(app != NULL);
	
	#ifdef WEBSOCKET_ENABLED
	/* Only initialize mutex in BOTH mode */
	if (app->settings.uiMode == BAR_UI_MODE_BOTH) {
		pthread_mutex_init(&app->stateMutex, NULL);
		debugPrint(DEBUG_UI, "State: Mutex initialized\n");
	}
	#endif
}

/*	Destroy state mutex (only in BOTH mode)
 */
void BarStateDestroy(BarApp_t *app) {
	assert(app != NULL);
	
	#ifdef WEBSOCKET_ENABLED
	/* Only destroy mutex in BOTH mode */
	if (app->settings.uiMode == BAR_UI_MODE_BOTH) {
		pthread_mutex_destroy(&app->stateMutex);
		debugPrint(DEBUG_UI, "State: Mutex destroyed\n");
	}
	#endif
}

/*	Get next station (thread-safe)
 */
PianoStation_t *BarStateGetNextStation(const BarApp_t *app) {
	assert(app != NULL);
	
	PianoStation_t *station;
	WITH_STATE_LOCK_RETURN(app, "GetNextStation", station,
	                       "-> %s", station ? station->name : "null") {
		station = app->nextStation;
	}
	return station;
}

/*	Set next station (thread-safe)
 */
void BarStateSetNextStation(BarApp_t *app, PianoStation_t *station) {
	assert(app != NULL);
	
	WITH_STATE_LOCK(app, "SetNextStation", "<- %s", station ? station->name : "null") {
		app->nextStation = station;
	}
}

/*	Get current station (thread-safe)
 */
PianoStation_t *BarStateGetCurrentStation(const BarApp_t *app) {
	assert(app != NULL);
	
	PianoStation_t *station;
	WITH_STATE_LOCK_RETURN(app, "GetCurrentStation", station,
	                       "-> %s", station ? station->name : "null") {
		station = app->curStation;
	}
	return station;
}

/*	Set current station (thread-safe)
 */
void BarStateSetCurrentStation(BarApp_t *app, PianoStation_t *station) {
	assert(app != NULL);
	
	WITH_STATE_LOCK(app, "SetCurrentStation", "<- %s", station ? station->name : "null") {
		app->curStation = station;
	}
}

/*	Find station by ID (thread-safe)
 */
PianoStation_t *BarStateFindStationById(const BarApp_t *app, const char *id) {
	assert(app != NULL);
	
	PianoStation_t *station;
	WITH_STATE_LOCK_RETURN(app, "FindStationById", station,
	                       "id=%s -> %s", id ? id : "null", station ? station->name : "null") {
		station = PianoFindStationById(app->ph.stations, id);
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
	                       "-> %s", playlist ? playlist->title : "null") {
		playlist = app->playlist;
	}
	return playlist;
}

/*	Set playlist (thread-safe)
 */
void BarStateSetPlaylist(BarApp_t *app, PianoSong_t *playlist) {
	assert(app != NULL);
	
	WITH_STATE_LOCK(app, "SetPlaylist", "<- %s", playlist ? playlist->title : "null") {
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
	
	WITH_STATE_LOCK(app, "SwitchStation", "<- %s", station ? station->name : "null") {
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
	
	/* Player mode uses player->lock, not stateMutex */
	return BarPlayerGetMode((player_t *)&app->player);
}

/*	Get player time (thread-safe)
 */
void BarStateGetPlayerTime(const BarApp_t *app, unsigned int *played, unsigned int *duration) {
	assert(app != NULL);
	
	/* Player time uses player->lock, not stateMutex */
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
	
	/* Player pause state uses player->lock, not stateMutex */
	pthread_mutex_lock((pthread_mutex_t *)&app->player.lock);
	bool paused = app->player.doPause;
	pthread_mutex_unlock((pthread_mutex_t *)&app->player.lock);
	return paused;
}

/*	Make Pandora API call (thread-safe)
 */
bool BarStateCallPandora(BarApp_t *app, PianoRequestType_t type,
                         void *data, PianoReturn_t *pRet, CURLcode *wRet) {
	assert(app != NULL);
	
	bool result;
	WITH_STATE_LOCK_RETURN(app, "CallPandora", result,
	                       "type=%d -> %s", type, result ? "true" : "false") {
		result = BarUiPianoCall(app, type, data, pRet, wRet);
	}
	return result;
}

