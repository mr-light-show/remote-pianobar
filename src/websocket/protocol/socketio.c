/*
Copyright (c) 2025

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

/* Enable POSIX functions on Linux (not needed on macOS/BSD) */
#if !defined(_XOPEN_SOURCE) && !defined(__APPLE__) && !defined(__FreeBSD__)
#define _XOPEN_SOURCE 600  /* Enable strdup() and POSIX functions */
#endif

#include "../../main.h"
#include "../../log.h"
#include "../../ui.h"
#include "../../ui_dispatch.h"
#include "../../bar_state.h"
#include "../../system_volume.h"
#include "../../station_display.h"
#include "../../websocket_bridge.h"
#include "socketio.h"
#include "error_messages.h"
#include "../core/websocket.h"

#include "../../ui_act.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <json-c/json.h>

/* json-c rejects NULL; Socket.IO payloads treat missing strings as "". */
static struct json_object *BarJsonStringOrEmpty (const char *s)
{
	return json_object_new_string (s != NULL ? s : "");
}

/* Helper: Create JSON object for a song with common fields
 * includeTrackToken: whether to include trackToken field
 * stationFieldName: optional field name for station ("songStationName" or "stationName"), NULL to skip */
static json_object* BarSocketIoCreateSongJson(BarApp_t *app, PianoSong_t *song, 
                                                bool includeTrackToken, 
                                                const char *stationFieldName) {
	json_object *songObj;
	PianoStation_t *songStation;
	
	if (!song) {
		return NULL;
	}
	
	songObj = json_object_new_object();
	
	/* Core song fields - use empty string as fallback for safety */
	json_object_object_add(songObj, "title",    BarJsonStringOrEmpty(song->title));
	json_object_object_add(songObj, "artist",   BarJsonStringOrEmpty(song->artist));
	json_object_object_add(songObj, "album",    BarJsonStringOrEmpty(song->album));
	json_object_object_add(songObj, "coverArt", BarJsonStringOrEmpty(song->coverArt));
	json_object_object_add(songObj, "rating",   json_object_new_int(song->rating));
	json_object_object_add(songObj, "duration", json_object_new_int(song->length));
	
	/* Optional: track token */
	if (includeTrackToken) {
		json_object_object_add(songObj, "trackToken", BarJsonStringOrEmpty(song->trackToken));
	}
	
	/* Optional: station name the song came from */
	if (stationFieldName && song->stationId && app) {
		songStation = BarStateFindStationById(app, song->stationId);
		if (songStation) {
			const char *displayName = songStation->displayName ? songStation->displayName : songStation->name;
			json_object_object_add(songObj, stationFieldName, BarJsonStringOrEmpty(displayName));
		}
	}
	
	return songObj;
}

/* Action capability: whether the action is fully implemented or known-unimplemented */
typedef enum {
	BAR_SOCKETIO_ACTION_DISPATCH,        /* fully implemented — route to dispatch table */
	BAR_SOCKETIO_ACTION_NOT_IMPLEMENTED  /* mapped but no server action: emit error */
} BarSocketIoActionCapability_t;

/* Action mapping: descriptive command name → action ID + capability */
typedef struct {
	const char                   *descriptive;
	BarKeyShortcutId_t            actionId;
	BarSocketIoActionCapability_t capability;
	const char                   *reasonKey; /* l10n key for NOT_IMPLEMENTED error */
} BarActionMapping_t;

static const BarActionMapping_t actionMappings[] = {
	/* Playback */
	{"playback.next",   BAR_KS_SKIP,      BAR_SOCKETIO_ACTION_DISPATCH, NULL},
	{"playback.toggle", BAR_KS_PLAYPAUSE, BAR_SOCKETIO_ACTION_DISPATCH, NULL},
	{"playback.play",   BAR_KS_PLAY,      BAR_SOCKETIO_ACTION_DISPATCH, NULL},
	{"playback.pause",  BAR_KS_PAUSE,     BAR_SOCKETIO_ACTION_DISPATCH, NULL},

	/* Volume */
	{"volume.up",    BAR_KS_VOLUP,    BAR_SOCKETIO_ACTION_DISPATCH, NULL},
	{"volume.down",  BAR_KS_VOLDOWN,  BAR_SOCKETIO_ACTION_DISPATCH, NULL},
	{"volume.reset", BAR_KS_VOLRESET, BAR_SOCKETIO_ACTION_DISPATCH, NULL},
	/* volume.set handled specially in BarSocketIoHandleAction */

	/* Song */
	{"song.love",              BAR_KS_LOVE,                 BAR_SOCKETIO_ACTION_DISPATCH,        NULL},
	{"song.ban",               BAR_KS_BAN,                  BAR_SOCKETIO_ACTION_DISPATCH,        NULL},
	{"song.tired",             BAR_KS_TIRED,                BAR_SOCKETIO_ACTION_DISPATCH,        NULL},
	{"song.bookmark",          BAR_KS_BOOKMARK,             BAR_SOCKETIO_ACTION_NOT_IMPLEMENTED, "web.socket.not_implemented_bookmark"},
	{"song.explain",           BAR_KS_EXPLAIN,              BAR_SOCKETIO_ACTION_DISPATCH,        NULL},
	{"song.info",              BAR_KS_INFO,                 BAR_SOCKETIO_ACTION_DISPATCH,        NULL},
	{"song.createStationFrom", BAR_KS_CREATESTATIONFROMSONG, BAR_SOCKETIO_ACTION_DISPATCH,       NULL},

	/* Station */
	{"station.change",        BAR_KS_SELECTSTATION,  BAR_SOCKETIO_ACTION_DISPATCH, NULL},
	{"station.create",        BAR_KS_CREATESTATION,  BAR_SOCKETIO_ACTION_DISPATCH, NULL},
	{"station.delete",        BAR_KS_DELETESTATION,  BAR_SOCKETIO_ACTION_DISPATCH, NULL},
	{"station.rename",        BAR_KS_RENAMESTATION,  BAR_SOCKETIO_ACTION_DISPATCH, NULL},
	{"station.addMusic",      BAR_KS_ADDMUSIC,       BAR_SOCKETIO_ACTION_DISPATCH, NULL},
	{"station.addGenre",      BAR_KS_GENRESTATION,   BAR_SOCKETIO_ACTION_DISPATCH, NULL},
	{"station.addShared",     BAR_KS_ADDSHARED,      BAR_SOCKETIO_ACTION_DISPATCH, NULL},
	{"station.selectQuickMix",BAR_KS_SELECTQUICKMIX, BAR_SOCKETIO_ACTION_DISPATCH, NULL},
	/* station.mode, station.seeds, music.search, query.stations handled by dedicated events */

	/* Query */
	{"query.history",  BAR_KS_HISTORY,  BAR_SOCKETIO_ACTION_NOT_IMPLEMENTED, "web.socket.not_implemented_history"},
	{"query.upcoming", BAR_KS_UPCOMING, BAR_SOCKETIO_ACTION_DISPATCH,        NULL},

	/* App */
	{"app.quit",               BAR_KS_QUIT,                BAR_SOCKETIO_ACTION_DISPATCH,        NULL},
	{"app.pandora-disconnect", BAR_KS_PANDORA_DISCONNECT,  BAR_SOCKETIO_ACTION_DISPATCH,        NULL},
	{"app.settings",           BAR_KS_SETTINGS,            BAR_SOCKETIO_ACTION_NOT_IMPLEMENTED, "web.socket.not_implemented_settings"},
	{"app.pandora-reconnect",  BAR_KS_PANDORA_RECONNECT,   BAR_SOCKETIO_ACTION_DISPATCH,        NULL},

	{NULL, (BarKeyShortcutId_t)-1, BAR_SOCKETIO_ACTION_DISPATCH, NULL} /* terminator */
};

/* Look up action mapping entry; returns NULL if not found */
static const BarActionMapping_t *BarSocketIoLookupAction (const char *command) {
	if (!command) { return NULL; }
	for (size_t i = 0; actionMappings[i].descriptive != NULL; i++) {
		if (strcmp (command, actionMappings[i].descriptive) == 0) {
			return &actionMappings[i];
		}
	}
	return NULL;
}

/* Translate descriptive command to action ID; kept for callers that only need the ID.
 * Returns (BarKeyShortcutId_t)-1 if command not found. */
static BarKeyShortcutId_t BarSocketIoTranslateAction (const char *command) {
	const BarActionMapping_t *m = BarSocketIoLookupAction (command);
	return m ? m->actionId : (BarKeyShortcutId_t)-1;
}

/* Emit a localized "not implemented" error for a mapped but unsupported action */
static void BarSocketIoEmitNotImplemented (BarApp_t *app, const char *operation,
                                           const char *reasonKey) {
	const char *message = BarL10nGet (app ? &app->l10n : NULL,
	        reasonKey ? reasonKey : "web.socket.not_implemented");
	BarSocketIoEmitError (app, operation, message);
}

/* Parse Socket.IO protocol message format */
static BarSocketIoType_t BarSocketIoParse(const char *message, char **eventName, 
                                          json_object **data) {
	const char *ptr = message;
	BarSocketIoType_t type;
	json_object *arr, *event, *payload;
	
	if (!message || !eventName || !data) {
		return SOCKETIO_ERROR;
	}
	
	*eventName = NULL;
	*data = NULL;
	
	/* Parse packet type (first character) */
	if (*ptr < '0' || *ptr > '6') {
		return SOCKETIO_ERROR;
	}
	type = (BarSocketIoType_t)(*ptr - '0');
	ptr++;
	
	/* For EVENT type (2), parse JSON array: ["eventName", {...}] */
	if (type == SOCKETIO_EVENT) {
		arr = json_tokener_parse(ptr);
		if (!arr || !json_object_is_type(arr, json_type_array)) {
			if (arr) json_object_put(arr);
			return SOCKETIO_ERROR;
		}
		
		/* Get event name (first array element) */
		event = json_object_array_get_idx(arr, 0);
		if (event && json_object_is_type(event, json_type_string)) {
			*eventName = strdup(json_object_get_string(event));
		}
		
		/* Get data payload (second array element, optional) */
		payload = json_object_array_get_idx(arr, 1);
		if (payload) {
			*data = json_object_get(payload); /* Increment reference count */
		}
		
		json_object_put(arr);
	}
	
	return type;
}

/*
 * ============================================================================
 * Event dispatch table
 * ============================================================================
 */

/* Canonical event handler type: (app, data, wsi) */
typedef void (*BarSocketIoEventFn)(BarApp_t *, json_object *, void *);

typedef struct {
	const char        *name;
	BarSocketIoEventFn fn;
} BarSocketIoEvent_t;

/* Thin adapters for handlers whose native signatures differ from the canonical type */
static void evtHandleQuery(BarApp_t *a, json_object *d, void *w) {
	(void)d; BarSocketIoHandleQuery(a, w);
}
static void evtHandleStationChange(BarApp_t *a, json_object *d, void *w) {
	(void)w;
	if (!d) { return; }
	if (json_object_is_type(d, json_type_string)) {
		const char *id = json_object_get_string(d);
		if (id) { BarSocketIoHandleChangeStation(a, id); }
	} else if (json_object_is_type(d, json_type_object)) {
		json_object *idObj;
		if (json_object_object_get_ex(d, "id", &idObj)) {
			const char *id = json_object_get_string(idObj);
			if (id) { BarSocketIoHandleChangeStation(a, id); }
		}
	}
}
static void evtEmitStations(BarApp_t *a, json_object *d, void *w) {
	(void)d; (void)w;
	log_write(DEBUG_WEBSOCKET, "Socket.IO: Query stations received\n");
	BarSocketIoEmitStations(a);
}
static void evtGetGenres(BarApp_t *a, json_object *d, void *w) {
	(void)d; (void)w; BarSocketIoHandleGetGenres(a);
}
static void evtPingNoop(BarApp_t *a, json_object *d, void *w) {
	(void)a; (void)d; (void)w; /* client keepalive; no response needed */
}
static void evtSetQuickMix(BarApp_t *a, json_object *d, void *w) {
	(void)w; BarSocketIoHandleSetQuickMix(a, d);
}
static void evtDeleteStation(BarApp_t *a, json_object *d, void *w) {
	(void)w; BarSocketIoHandleDeleteStation(a, d);
}
static void evtCreateStationFrom(BarApp_t *a, json_object *d, void *w) {
	(void)w; BarSocketIoHandleCreateStationFrom(a, d);
}
static void evtAddGenre(BarApp_t *a, json_object *d, void *w) {
	(void)w; BarSocketIoHandleAddGenre(a, d);
}
static void evtSearchMusic(BarApp_t *a, json_object *d, void *w) {
	(void)w; BarSocketIoHandleSearchMusic(a, d);
}
static void evtAddMusic(BarApp_t *a, json_object *d, void *w) {
	(void)w; BarSocketIoHandleAddMusic(a, d);
}
static void evtAddShared(BarApp_t *a, json_object *d, void *w) {
	(void)w; BarSocketIoHandleAddShared(a, d);
}
static void evtRenameStation(BarApp_t *a, json_object *d, void *w) {
	(void)w; BarSocketIoHandleRenameStation(a, d);
}
static void evtGetStationModes(BarApp_t *a, json_object *d, void *w) {
	(void)w; BarSocketIoHandleGetStationModes(a, d);
}
static void evtSetStationMode(BarApp_t *a, json_object *d, void *w) {
	(void)w; BarSocketIoHandleSetStationMode(a, d);
}
static void evtGetStationInfo(BarApp_t *a, json_object *d, void *w) {
	(void)w; BarSocketIoHandleGetStationInfo(a, d);
}
static void evtDeleteSeed(BarApp_t *a, json_object *d, void *w) {
	(void)w; BarSocketIoHandleDeleteSeed(a, d);
}
static void evtDeleteFeedback(BarApp_t *a, json_object *d, void *w) {
	(void)w; BarSocketIoHandleDeleteFeedback(a, d);
}

static const BarSocketIoEvent_t eventHandlers[] = {
	/* station events */
	{"station.change",         evtHandleStationChange},
	{"changeStation",          evtHandleStationChange},   /* legacy alias */
	{"station.setQuickMix",    evtSetQuickMix},
	{"station.delete",         evtDeleteStation},
	{"station.createFrom",     evtCreateStationFrom},
	{"station.getGenres",      evtGetGenres},
	{"station.addGenre",       evtAddGenre},
	{"station.addMusic",       evtAddMusic},
	{"station.addShared",      evtAddShared},
	{"station.rename",         evtRenameStation},
	{"station.getModes",       evtGetStationModes},
	{"station.setMode",        evtSetStationMode},
	{"station.getInfo",        evtGetStationInfo},
	{"station.deleteSeed",     evtDeleteSeed},
	{"station.deleteFeedback", evtDeleteFeedback},
	/* music events */
	{"music.search",           evtSearchMusic},
	/* query events */
	{"query",                  evtHandleQuery},
	{"query.state",            evtHandleQuery},           /* alias */
	{"query.stations",         evtEmitStations},
	/* protocol events */
	{"ping",                   evtPingNoop},
	{NULL, NULL}  /* sentinel */
};

/* Handle incoming Socket.IO message */
void BarSocketIoHandleMessage(BarApp_t *app, const char *message, void *wsi) {
	BarSocketIoType_t type;
	char *eventName = NULL;
	json_object *data = NULL;

	if (!app || !message) {
		log_write(DEBUG_WEBSOCKET_PROGRESS, "Socket.IO: HandleMessage called with null app or message\n");
		return;
	}

	log_write(DEBUG_WEBSOCKET_PROGRESS, "Socket.IO: HandleMessage called with: %.*s%s\n",
	           (int)LOG_MESSAGE_TRUNCATE_LEN, message, strlen(message) > LOG_MESSAGE_TRUNCATE_LEN ? "..." : "");

	type = BarSocketIoParse(message, &eventName, &data);

	log_write(DEBUG_WEBSOCKET_PROGRESS, "Socket.IO: Parsed message - type=%d, eventName=%s\n",
	           type, eventName ? eventName : "(null)");

	if (type == SOCKETIO_CONNECT) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: Client connected\n");
		BarSocketIoHandleQuery(app, wsi);
		goto cleanup;
	}

	if (type != SOCKETIO_EVENT || !eventName) {
		goto cleanup;
	}

	/* "action" has complex sub-routing (string/object data formats); handle before table */
	if (strcmp(eventName, "action") == 0) {
		if (data && json_object_is_type(data, json_type_string)) {
			const char *action = json_object_get_string(data);
			BarSocketIoHandleAction(app, action, NULL, wsi);
		} else if (data && json_object_is_type(data, json_type_object)) {
			json_object *actionObj;
			if (json_object_object_get_ex(data, "action", &actionObj)) {
				const char *action = json_object_get_string(actionObj);
				if (action) { BarSocketIoHandleAction(app, action, data, wsi); }
			} else if (json_object_object_get_ex(data, "command", &actionObj)) {
				const char *action = json_object_get_string(actionObj);
				if (action) { BarSocketIoHandleAction(app, action, data, wsi); }
			}
		}
		goto cleanup;
	}

	/* Table-driven dispatch for all other events */
	for (const BarSocketIoEvent_t *e = eventHandlers; e->name != NULL; e++) {
		if (strcmp(eventName, e->name) == 0) {
			e->fn(app, data, wsi);
			goto cleanup;
		}
	}
	log_write(DEBUG_WEBSOCKET, "Socket.IO: Unknown event: %s\n", eventName);

cleanup:
	free(eventName);
	if (data) {
		json_object_put(data);
	}
}

/* Global broadcast callback (set by WebSocket core) */
static void (*g_broadcastCallback)(const char *message, size_t len) = NULL;

/* Thread-local unicast target - each thread has its own value
 * WebSocket thread: Sets this when handling query actions
 * CLI thread: Always NULL (never set)
 * This eliminates race conditions from shared global variable */
static pthread_key_t g_unicastTargetKey;
static pthread_once_t g_unicastTargetKeyOnce = PTHREAD_ONCE_INIT;

/* Initialize TLS key (called once) */
static void init_unicast_target_key(void) {
	pthread_key_create(&g_unicastTargetKey, NULL);
}

/* Set broadcast callback */
void BarSocketIoSetBroadcastCallback(void (*callback)(const char *, size_t)) {
	g_broadcastCallback = callback;
}

/* Set unicast target (NULL for broadcast mode) */
void BarSocketIoSetUnicastTarget(void *wsi) {
	pthread_once(&g_unicastTargetKeyOnce, init_unicast_target_key);
	pthread_setspecific(g_unicastTargetKey, wsi);
}

/* Get unicast target (for websocket.c to check) */
void *BarSocketIoGetUnicastTarget(void) {
	pthread_once(&g_unicastTargetKeyOnce, init_unicast_target_key);
	return pthread_getspecific(g_unicastTargetKey);
}

/* Format Socket.IO event message */
char *BarSocketIoFormatEventMessage(const char *event, json_object *data) {
	json_object *arr;
	const char *jsonStr;
	char *formatted;
	size_t len;
	
	if (!event) {
		return NULL;
	}
	
	/* Create JSON array: ["event", data] */
	arr = json_object_new_array();
	json_object_array_add(arr, BarJsonStringOrEmpty(event));
	
	if (data) {
		json_object_array_add(arr, json_object_get(data)); /* Increment ref */
	}
	
	jsonStr = json_object_to_json_string(arr);
	
	/* Format as Socket.IO message: 2["event",data] */
	len = strlen(jsonStr) + 2; /* "2" + JSON + null */
	formatted = malloc(len);
	if (formatted) {
		snprintf(formatted, len, "2%s", jsonStr);
	}
	
	json_object_put(arr);
	return formatted;
}

/* Emit event to all connected clients */
void BarSocketIoEmit(const char *event, json_object *data) {
	char *message;
	
	if (!event) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: Emit called with null event\n");
		return;
	}
	
	/* Use separate debug flag for progress events */
	bool isProgress = (strcmp(event, "progress") == 0);
	logKind dbgFlag = isProgress ? DEBUG_WEBSOCKET_PROGRESS : DEBUG_WEBSOCKET;

	log_write(dbgFlag, "Socket.IO: Emit event='%s' (data=%p)\n", event, data);

	message = BarSocketIoFormatEventMessage(event, data);
	if (!message) {
		log_write(dbgFlag, "Socket.IO: Failed to format message for event '%s'\n", event);
		return;
	}

	log_write(dbgFlag, "Socket.IO: Formatted message (len=%zu): %.*s%s\n",
	           strlen(message), (int)LOG_MESSAGE_TRUNCATE_LEN, message, strlen(message) > LOG_MESSAGE_TRUNCATE_LEN ? "..." : "");

	/* Broadcast to all clients if callback is set */
	if (g_broadcastCallback) {
		log_write(dbgFlag, "Socket.IO: Calling broadcast callback\n");
		g_broadcastCallback(message, strlen(message));
		log_write(DEBUG_WEBSOCKET_PROGRESS, "Socket.IO: Broadcast callback returned\n");
	} else {
		log_write(dbgFlag, "Socket.IO: Emit '%s' (no broadcast callback set)\n", event);
	}

	free(message);
	log_write(DEBUG_WEBSOCKET_PROGRESS, "Socket.IO: Emit complete for event '%s'\n", event);
}

/* Emit 'start' event (song started) */
void BarSocketIoEmitStart(BarApp_t *app) {
	if (!app) {
		return;
	}

	json_object *data = BarSocketIoBuildStartPayload (app);
	if (data == NULL) {
		return;
	}

	BarSocketIoEmit("start", data);
	json_object_put(data);
}

/* Emit 'stop' event (song stopped) */
void BarSocketIoEmitStop(BarApp_t *app) {
	BarSocketIoEmit("stop", NULL);
}

/* Emit 'volume' event */
void BarSocketIoEmitVolume(BarApp_t *app, int volume) {
	json_object *data = json_object_new_int(volume);
	BarSocketIoEmit("volume", data);
	json_object_put(data);
}

/* Emit 'progress' event */
void BarSocketIoEmitProgress(BarApp_t *app, unsigned int elapsed,
                              unsigned int duration) {
	json_object *data;
	float percentage = 0.0;
	
	if (duration > 0) {
		percentage = (elapsed * 100.0) / duration;
	}
	
	data = json_object_new_object();
	json_object_object_add(data, "elapsed",    json_object_new_int(elapsed));
	json_object_object_add(data, "duration",   json_object_new_int(duration));
	json_object_object_add(data, "percentage", json_object_new_double(percentage));
	
	BarSocketIoEmit("progress", data);
	json_object_put(data);
}

/* Emit 'stations' event (station list) */
void BarSocketIoEmitStations(BarApp_t *app) {
	json_object *stations, *station;
	PianoStation_t **sortedStations;
	size_t stationCount;
	
	if (!app) {
		return;
	}
	
	/* Check if stations are available */
	PianoStation_t *stationList = BarStateGetStationList(app);
	if (!stationList) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: No stations available yet\n");
		/* Send empty stations array */
		stations = json_object_new_array();
		BarSocketIoEmit("stations", stations);
		json_object_put(stations);
		return;
	}
	
	/* Sort stations using configured sort order */
	sortedStations = BarSortedStations(stationList, &stationCount,
	                                   app->settings.sortOrder);
	if (!sortedStations) {
		return;
	}
	
	stations = json_object_new_array();
	
	/* Emit stations in sorted order */
	for (size_t i = 0; i < stationCount; i++) {
		PianoStation_t *curStation = sortedStations[i];
		const char *displayName = curStation->displayName ? curStation->displayName : curStation->name;
		
		station = json_object_new_object();
		json_object_object_add(station, "id",           BarJsonStringOrEmpty(curStation->id));
		json_object_object_add(station, "name",         BarJsonStringOrEmpty(displayName));
		json_object_object_add(station, "isQuickMix",   json_object_new_boolean(curStation->isQuickMix));
		json_object_object_add(station, "isQuickMixed", json_object_new_boolean(curStation->useQuickMix));
		
		json_object_array_add(stations, station);
	}
	
	free(sortedStations);
	
	BarSocketIoEmit("stations", stations);
	json_object_put(stations);
}

/* Emit 'process' event (full state) */
void BarSocketIoEmitProcess(BarApp_t *app) {
	if (!app) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: EmitProcess called with null app\n");
		return;
	}

	json_object *data = BarSocketIoBuildProcessPayload (app);
	if (data == NULL) {
		return;
	}
	log_write(DEBUG_WEBSOCKET, "Socket.IO: EmitProcess calling emit\n");
	BarSocketIoEmit("process", data);
	log_write(DEBUG_WEBSOCKET, "Socket.IO: EmitProcess freeing data object\n");
	json_object_put(data);
	log_write(DEBUG_WEBSOCKET, "Socket.IO: EmitProcess complete\n");
}

/* --- Payload builders (snapshot app state; caller json_object_put()s the result) --- */

/* Comparison for qsort: sort snapshot items by effective display name */
static int stationSnapshotCmp (const void *a, const void *b) {
	const BarStationSnapshot_t *sa = (const BarStationSnapshot_t *) a;
	const BarStationSnapshot_t *sb = (const BarStationSnapshot_t *) b;
	const char *na = sa->displayName ? sa->displayName : (sa->name ? sa->name : "");
	const char *nb = sb->displayName ? sb->displayName : (sb->name ? sb->name : "");
	return strcmp (na, nb);
}

struct json_object *BarSocketIoBuildStartPayload (BarApp_t *app) {
	if (!app) return NULL;

	BarPlaybackSnapshot_t snap;
	BarStateSnapshotPlayback (app, &snap);

	if (!snap.hasSong) {
		BarStateFreePlaybackSnapshot (&snap);
		return NULL;
	}

	struct json_object *data = json_object_new_object ();
	json_object_object_add(data, "title",      BarJsonStringOrEmpty (snap.songTitle));
	json_object_object_add(data, "artist",     BarJsonStringOrEmpty (snap.songArtist));
	json_object_object_add(data, "album",      BarJsonStringOrEmpty (snap.songAlbum));
	json_object_object_add(data, "coverArt",   BarJsonStringOrEmpty (snap.songCoverArt));
	json_object_object_add(data, "trackToken", BarJsonStringOrEmpty (snap.trackToken));
	json_object_object_add(data, "rating",     json_object_new_int    (snap.rating));
	json_object_object_add(data, "duration",   json_object_new_int    ((int) snap.duration));

	if (snap.songStationName) {
		json_object_object_add(data, "songStationName", BarJsonStringOrEmpty(snap.songStationName));
	}

	if (snap.hasStation) {
		json_object_object_add(data, "station",   BarJsonStringOrEmpty (snap.stationName));
		json_object_object_add(data, "stationId", BarJsonStringOrEmpty (snap.stationId));
	}
	BarStateFreePlaybackSnapshot (&snap);
	return data;
}

struct json_object *BarSocketIoBuildStationsPayload (BarApp_t *app) {
	if (!app) return json_object_new_array ();

	BarStationSnapshotList_t snap;
	if (!BarStateSnapshotStations (app, &snap)) return json_object_new_array ();

	if (snap.count > 0) {
		qsort (snap.items, snap.count, sizeof (*snap.items), stationSnapshotCmp);
	}

	struct json_object *stations = json_object_new_array ();
	for (size_t i = 0; i < snap.count; i++) {
		const BarStationSnapshot_t *st = &snap.items[i];
		const char *displayName = st->displayName ? st->displayName : (st->name ? st->name : "");
		struct json_object *station = json_object_new_object ();
		json_object_object_add(station, "id",           BarJsonStringOrEmpty (st->id));
		json_object_object_add(station, "name",         BarJsonStringOrEmpty (displayName));
		json_object_object_add(station, "isQuickMix",   json_object_new_boolean (st->isQuickMix));
		json_object_object_add(station, "isQuickMixed", json_object_new_boolean (st->isQuickMixed));
		json_object_array_add (stations, station);
	}
	BarStateFreeStationSnapshot (&snap);
	return stations;
}

struct json_object *BarSocketIoBuildProcessPayload (BarApp_t *app) {
	if (!app) return NULL;

	/* Snapshot station + song fields atomically under lock */
	BarPlaybackSnapshot_t pbSnap;
	BarStateSnapshotPlayback (app, &pbSnap);

	struct json_object *data = json_object_new_object ();
	json_object_object_add (data, "playing", json_object_new_boolean (pbSnap.hasSong));

	bool paused = BarStateGetPlayerPaused (app);
	json_object_object_add (data, "paused", json_object_new_boolean (paused));

	int volumePercent;
	if (app->settings.volumeMode == BAR_VOLUME_MODE_SYSTEM) {
		volumePercent = BarSystemVolumeGet ();
		if (volumePercent < 0) volumePercent = VOLUME_FALLBACK_PERCENT;
	} else {
		volumePercent = app->settings.volume;
	}
	json_object_object_add (data, "volume", json_object_new_int (volumePercent));

	json_object_object_add(data, "station",   BarJsonStringOrEmpty(pbSnap.stationName));
	json_object_object_add(data, "stationId", BarJsonStringOrEmpty(pbSnap.stationId));

	if (pbSnap.hasSong) {
		struct json_object *song = json_object_new_object ();
		json_object_object_add(song, "title",      BarJsonStringOrEmpty (pbSnap.songTitle));
		json_object_object_add(song, "artist",     BarJsonStringOrEmpty (pbSnap.songArtist));
		json_object_object_add(song, "album",      BarJsonStringOrEmpty (pbSnap.songAlbum));
		json_object_object_add(song, "coverArt",   BarJsonStringOrEmpty (pbSnap.songCoverArt));
		json_object_object_add(song, "trackToken", BarJsonStringOrEmpty (pbSnap.trackToken));
		json_object_object_add(song, "rating",     json_object_new_int    (pbSnap.rating));
		json_object_object_add(song, "duration",   json_object_new_int    ((int) pbSnap.duration));
		if (pbSnap.songStationName) {
			json_object_object_add(song, "songStationName", BarJsonStringOrEmpty(pbSnap.songStationName));
		}
		json_object_object_add(data, "song",    song);
		json_object_object_add(data, "elapsed", json_object_new_int((int) BarWebsocketGetElapsed(app)));
	}

	BarStateFreePlaybackSnapshot (&pbSnap);

	if (app->settings.accountCount > 0) {
		const BarAccount_t *active = BarSettingsGetActiveAccount (&app->settings);
		if (active) {
			struct json_object *currentAcct = json_object_new_object ();
			json_object_object_add(currentAcct, "id",    BarJsonStringOrEmpty(active->id));
			json_object_object_add(currentAcct, "label", BarJsonStringOrEmpty(active->label ? active->label : active->id));
			json_object_object_add (data, "current_account", currentAcct);
		}
		struct json_object *accountsArr = json_object_new_array ();
		for (size_t i = 0; i < app->settings.accountCount; i++) {
			struct json_object *acctObj = json_object_new_object ();
			json_object_object_add(acctObj, "id",    BarJsonStringOrEmpty(app->settings.accounts[i].id));
			json_object_object_add(acctObj, "label", BarJsonStringOrEmpty(app->settings.accounts[i].label ? app->settings.accounts[i].label : app->settings.accounts[i].id));
			json_object_array_add (accountsArr, acctObj);
		}
		json_object_object_add (data, "accounts", accountsArr);
	}
	return data;
}

/* Emit 'process' event to specific client only (unicast) */
void BarSocketIoEmitProcessUnicast(BarApp_t *app, void *wsi) {
	/* Set unicast target before emitting */
	BarSocketIoSetUnicastTarget(wsi);
	BarSocketIoEmitProcess(app);
	/* Clear unicast target to return to broadcast mode */
	BarSocketIoSetUnicastTarget(NULL);
}

/* Emit 'song.explanation' event (explanation text) */
void BarSocketIoEmitExplanation(BarApp_t *app, const char *explanation) {
	json_object *data;
	char *explanation_copy;
	
	if (!app || !explanation) {
		return;
	}
	
	/* Make a defensive copy since caller may free the original immediately */
	explanation_copy = strdup(explanation);
	if (!explanation_copy) {
		return;
	}
	
	data = json_object_new_object();
	json_object_object_add(data, "explanation", BarJsonStringOrEmpty(explanation_copy));
	
	BarSocketIoEmit("song.explanation", data);
	json_object_put(data);
	free(explanation_copy);
}

/* Emit 'error' event (error notification) */
void BarSocketIoEmitError(BarApp_t *app, const char *operation, const char *message) {
	BarSocketIoEmitErrorEx(app, operation, message, NULL);
}

void BarSocketIoEmitErrorEx(BarApp_t *app, const char *operation, const char *message, const char *stationId) {
	json_object *data;
	const char *friendlyMessage;
	const BarL10nContext_t *l10n = app ? &app->l10n : NULL;
	
	if (!operation || !message) {
		return;
	}
	
	/* Translate to user-friendly message */
	friendlyMessage = BarWsGetFriendlyErrorMessage(l10n, operation, message);
	
	data = json_object_new_object();
	json_object_object_add(data, "operation", BarJsonStringOrEmpty(operation));
	json_object_object_add(data, "message",   BarJsonStringOrEmpty(friendlyMessage));
	if (stationId && *stationId) {
		json_object_object_add(data, "stationId", BarJsonStringOrEmpty(stationId));
	}
	
	BarSocketIoEmit("error", data);
	json_object_put(data);
}

/* Emit 'pandora.disconnected' event (reason: "user", "idle_timeout", "connection_lost", etc.) */
void BarSocketIoEmitPandoraDisconnected(const char *reason) {
	json_object *data;
	if (!reason) {
		reason = "unknown";
	}
	data = json_object_new_object();
	json_object_object_add(data, "reason", BarJsonStringOrEmpty(reason));
	BarSocketIoEmit("pandora.disconnected", data);
	json_object_put(data);
}

/* If Piano request failed with auth/connection error, notify clients (reactive disconnect) */
static void BarSocketIoOnPandoraRequestFailed(PianoReturn_t pRet) {
	switch (pRet) {
	case PIANO_RET_INVALID_LOGIN:
	case PIANO_RET_P_INVALID_AUTH_TOKEN:
	case PIANO_RET_P_LISTENER_NOT_AUTHORIZED:
	case PIANO_RET_P_USER_NOT_AUTHORIZED:
	case PIANO_RET_P_DEVICE_NOT_FOUND:
	case PIANO_RET_P_INSUFFICIENT_CONNECTIVITY:
		BarSocketIoEmitPandoraDisconnected("connection_lost");
		break;
	default:
		break;
	}
}

/* Wrapper: call Piano API with logging; on failure, log + BarSocketIoOnPandoraRequestFailed + BarSocketIoEmitError. Messages derived from actionName. */
static bool BarSocketIoPianoCallLogged(BarApp_t *app, PianoRequestType_t type,
	void *data, const char *actionName,
	const char *operation,
	PianoReturn_t *pRet, CURLcode *wRet) {
	bool ok = BarUiPianoCallLogged(app, type, data, actionName, pRet, wRet);
	if (ok) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: %s\n", actionName);
		return true;
	}
	log_write(DEBUG_WEBSOCKET, "Socket.IO: Failed: %s\n", actionName);
	BarSocketIoOnPandoraRequestFailed(*pRet);
	char buf[BAR_BUF_SMALL];
	snprintf(buf, sizeof(buf), "Failed: %s", actionName);
	BarSocketIoEmitError(app, operation, buf);
	return false;
}

/* Emit 'playState' event (paused/resumed state) */
void BarSocketIoEmitPlayState(BarApp_t *app) {
	if (!app) {
		return;
	}
	
	json_object *data = json_object_new_object();
	
	pthread_mutex_lock(&app->player.lock);
	json_object_object_add(data, "paused", json_object_new_boolean(app->player.doPause));
	pthread_mutex_unlock(&app->player.lock);
	
	BarSocketIoEmit("playState", data);
	json_object_put(data);
}

/* Emit 'query.upcoming.result' event (upcoming songs list) */
void BarSocketIoEmitUpcoming(BarApp_t *app, PianoSong_t *firstSong, int maxSongs) {
	json_object *songs, *songObj;
	PianoSong_t *song;
	int count = 0;
	
	if (!app) {
		return;
	}
	
	songs = json_object_new_array();
	
	/* Iterate through upcoming songs (limited to maxSongs) */
	song = firstSong;
	while (song && count < maxSongs) {
		/* Create song JSON without trackToken, but with stationName */
		songObj = BarSocketIoCreateSongJson(app, song, false, "stationName");
		if (songObj) {
			json_object_array_add(songs, songObj);
		}
		
		song = (PianoSong_t *)song->head.next;
		count++;
	}
	
	BarSocketIoEmit("query.upcoming.result", songs);
	json_object_put(songs);
}

/* Emit 'genres' event (genre categories and stations) */
void BarSocketIoEmitGenres(BarApp_t *app) {
	json_object *data, *categories, *categoryObj, *genresArray, *genreObj;
	PianoGenreCategory_t *category;
	PianoGenre_t *genre;
	
	if (!app) {
		return;
	}
	
	data = json_object_new_object();
	categories = json_object_new_array();
	
	/* Iterate through genre categories */
	category = app->ph.genreStations;
	while (category != NULL) {
		categoryObj = json_object_new_object();
		
		json_object_object_add(categoryObj, "name", BarJsonStringOrEmpty(category->name));
		
		/* Create genres array for this category */
		genresArray = json_object_new_array();
		genre = category->genres;
		
		while (genre != NULL) {
			genreObj = json_object_new_object();
			
			json_object_object_add(genreObj, "name",    BarJsonStringOrEmpty(genre->name));
			json_object_object_add(genreObj, "musicId", BarJsonStringOrEmpty(genre->musicId));
			
			json_object_array_add(genresArray, genreObj);
			
			genre = (PianoGenre_t *)genre->head.next;
		}
		
		json_object_object_add(categoryObj, "genres", genresArray);
		json_object_array_add(categories, categoryObj);
		
		category = (PianoGenreCategory_t *)category->head.next;
	}
	
	json_object_object_add(data, "categories", categories);
	
	BarSocketIoEmit("genres", data);
	json_object_put(data);
}

/* Handle 'station.getGenres' event from client */
void BarSocketIoHandleGetGenres(BarApp_t *app) {
	PianoReturn_t pRet;
	CURLcode wRet;
	
	if (!app) {
		return;
	}
	
	log_write(DEBUG_WEBSOCKET, "Socket.IO: Get genres request\n");
	
	/* Fetch genre stations if not already cached */
	if (app->ph.genreStations == NULL) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: Fetching genre stations from API\n");
		if (!BarSocketIoPianoCallLogged(app, PIANO_REQUEST_GET_GENRE_STATIONS, NULL,
				"Receiving genre stations", "station.getGenres", &pRet, &wRet)) {
			return;
		}
	}
	
	/* Emit genres to client */
	BarSocketIoEmitGenres(app);
}

/* Handle 'station.addGenre' event from client */
void BarSocketIoHandleAddGenre(BarApp_t *app, json_object *data) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataCreateStation_t reqData;
	json_object *musicIdObj;
	const char *musicId;
	
	if (!app || !data) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: addGenre - invalid parameters\n");
		return;
	}
	
	/* Extract musicId from data */
	if (!json_object_object_get_ex(data, "musicId", &musicIdObj)) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: addGenre - missing musicId\n");
		return;
	}
	
	musicId = json_object_get_string(musicIdObj);
	
	if (!musicId) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: addGenre - invalid musicId\n");
		return;
	}
	
	log_write(DEBUG_WEBSOCKET, "Socket.IO: Creating genre station with musicId: %s\n", musicId);
	
	/* Set up request data */
	reqData.token = (char *)musicId;
	reqData.type = PIANO_MUSICTYPE_INVALID;
	
	/* Create the station */
	if (BarSocketIoPianoCallLogged(app, PIANO_REQUEST_CREATE_STATION, &reqData,
			"Adding genre station", "station.addGenre", &pRet, &wRet)) {
		BarUpdateStationDisplayNames(app);
		BarSocketIoEmitStations(app);
	}
}

/* Handle 'station.addShared' event from client */
void BarSocketIoHandleAddShared(BarApp_t *app, json_object *data) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataCreateStation_t reqData;
	json_object *stationIdObj;
	const char *stationId;
	
	if (!app || !data) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: addShared - invalid parameters\n");
		return;
	}
	
	/* Extract stationId from data */
	if (!json_object_object_get_ex(data, "stationId", &stationIdObj)) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: addShared - missing stationId\n");
		return;
	}
	
	stationId = json_object_get_string(stationIdObj);
	
	if (!stationId) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: addShared - invalid stationId\n");
		return;
	}
	
	/* Validate stationId contains only digits */
	for (const char *p = stationId; *p != '\0'; p++) {
		if (*p < '0' || *p > '9') {
			log_write(DEBUG_WEBSOCKET, "Socket.IO: addShared - stationId must contain only digits: %s\n", stationId);
			return;
		}
	}
	
	log_write(DEBUG_WEBSOCKET, "Socket.IO: Adding shared station with ID: %s\n", stationId);
	
	/* Set up request data */
	reqData.token = (char *)stationId;
	reqData.type = PIANO_MUSICTYPE_INVALID;
	
	/* Create the station */
	if (BarSocketIoPianoCallLogged(app, PIANO_REQUEST_CREATE_STATION, &reqData,
			"Adding shared station", "station.addShared", &pRet, &wRet)) {
		BarUpdateStationDisplayNames(app);
		BarSocketIoEmitStations(app);
	}
}

/* Handle 'station.addMusic' event from client */
void BarSocketIoHandleAddMusic(BarApp_t *app, json_object *data) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataAddSeed_t reqData;
	json_object *musicIdObj, *stationIdObj;
	const char *musicId, *stationId;
	PianoStation_t *station;
	
	if (!app || !data) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: addMusic - invalid parameters\n");
		return;
	}
	
	/* Extract musicId and stationId from data */
	if (!json_object_object_get_ex(data, "musicId", &musicIdObj) ||
	    !json_object_object_get_ex(data, "stationId", &stationIdObj)) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: addMusic - missing musicId or stationId\n");
		return;
	}
	
	musicId = json_object_get_string(musicIdObj);
	stationId = json_object_get_string(stationIdObj);
	
	if (!musicId || !stationId) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: addMusic - invalid musicId or stationId\n");
		return;
	}
	
	/* Find the station */
	station = BarStateFindStationById(app, stationId);
	if (!station) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: addMusic - station not found\n");
		return;
	}
	
	log_write(DEBUG_WEBSOCKET, "Socket.IO: Adding music to station: %s\n", station->name);
	
	/* Check if station is shared (QuickMix) and transform if needed */
	if (!BarWsTransformIfShared(app, station)) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: addMusic - failed to transform\n");
		BarSocketIoEmitError(app, "station.addMusic", "Failed to transform station");
		return;
	}
	
	/* Set up request data */
	reqData.musicId = (char *)musicId;
	reqData.station = station;
	
	/* Add music to station */
	BarSocketIoPianoCallLogged(app, PIANO_REQUEST_ADD_SEED, &reqData,
			"Adding music to station", "station.addMusic", &pRet, &wRet);
}

/* Handle 'station.rename' event from client */
void BarSocketIoHandleRenameStation(BarApp_t *app, json_object *data) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataRenameStation_t reqData;
	json_object *stationIdObj, *newNameObj;
	const char *stationId, *newName;
	PianoStation_t *station;
	
	if (!app || !data) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: renameStation - invalid parameters\n");
		return;
	}
	
	/* Extract stationId and newName from data */
	if (!json_object_object_get_ex(data, "stationId", &stationIdObj) ||
	    !json_object_object_get_ex(data, "newName", &newNameObj)) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: renameStation - missing stationId or newName\n");
		return;
	}
	
	stationId = json_object_get_string(stationIdObj);
	newName = json_object_get_string(newNameObj);
	
	if (!stationId || !newName || strlen(newName) == 0) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: renameStation - invalid stationId or newName\n");
		return;
	}
	
	/* Find the station */
	station = BarStateFindStationById(app, stationId);
	if (!station) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: renameStation - station not found\n");
		return;
	}
	
	log_write(DEBUG_WEBSOCKET, "Socket.IO: Renaming station from '%s' to '%s'\n", station->name, newName);
	
	/* Check if station is shared and transform if needed */
	if (!BarWsTransformIfShared(app, station)) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: renameStation - failed to transform\n");
		BarSocketIoEmitError(app, "station.rename", "Failed to transform station");
		return;
	}
	
	/* Set up request data */
	reqData.station = station;
	reqData.newName = (char *)newName;
	
	/* Rename station */
	if (BarSocketIoPianoCallLogged(app, PIANO_REQUEST_RENAME_STATION, &reqData,
			"Renaming station", "station.rename", &pRet, &wRet)) {
		BarUpdateStationDisplayNames(app);
		BarSocketIoEmitStations(app);
	}
}

/* Handle 'station.getModes' event - fetch available modes for a station */
void BarSocketIoHandleGetStationModes(BarApp_t *app, json_object *data) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataGetStationModes_t reqData;
	json_object *stationIdObj;
	const char *stationId;
	PianoStation_t *station;
	
	if (!app || !data) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: getStationModes - invalid parameters\n");
		return;
	}
	
	/* Extract stationId */
	if (!json_object_object_get_ex(data, "stationId", &stationIdObj)) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: getStationModes - missing stationId\n");
		return;
	}
	
	stationId = json_object_get_string(stationIdObj);
	if (!stationId) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: getStationModes - invalid stationId\n");
		return;
	}
	
	/* Find the station */
	station = BarStateFindStationById(app, stationId);
	if (!station) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: getStationModes - station not found\n");
		return;
	}
	
	if (station->isQuickMix) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: getStationModes - QuickMix not supported\n");
		return;
	}
	
	/* Fetch station modes */
	memset(&reqData, 0, sizeof(reqData));
	reqData.station = station;
	if (BarSocketIoPianoCallLogged(app, PIANO_REQUEST_GET_STATION_MODES, &reqData,
			"Fetching station modes", "station.getStationModes", &pRet, &wRet)) {
		BarSocketIoEmitStationModes(app, &reqData);
	}
	
	PianoDestroyStationMode(reqData.retModes);
}

/* Emit station modes to client */
void BarSocketIoEmitStationModes(BarApp_t *app, PianoRequestDataGetStationModes_t *reqData) {
	json_object *data, *modesArray, *modeObj;
	const PianoStationMode_t *mode;
	int i = 0;
	
	if (!app || !reqData || !reqData->retModes) {
		return;
	}
	
	data = json_object_new_object();
	modesArray = json_object_new_array();
	
	mode = reqData->retModes;
	PianoListForeachP(mode) {
		modeObj = json_object_new_object();
		json_object_object_add(modeObj, "id",          json_object_new_int(i));
		json_object_object_add(modeObj, "name",        BarJsonStringOrEmpty (mode->name));
		json_object_object_add(modeObj, "description", BarJsonStringOrEmpty (mode->description));
		json_object_object_add(modeObj, "active",      json_object_new_boolean(mode->active));
		json_object_array_add(modesArray, modeObj);
		i++;
	}
	
	json_object_object_add(data, "modes", modesArray);
	
	BarSocketIoEmit("stationModes", data);
	json_object_put(data);
}

/* Handle 'station.setMode' event - set station mode */
void BarSocketIoHandleSetStationMode(BarApp_t *app, json_object *data) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataSetStationMode_t reqData;
	json_object *stationIdObj, *modeIdObj;
	const char *stationId;
	int modeId;
	PianoStation_t *station;
	
	if (!app || !data) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: setStationMode - invalid parameters\n");
		return;
	}
	
	/* Extract stationId and modeId */
	if (!json_object_object_get_ex(data, "stationId", &stationIdObj) ||
	    !json_object_object_get_ex(data, "modeId", &modeIdObj)) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: setStationMode - missing parameters\n");
		return;
	}
	
	stationId = json_object_get_string(stationIdObj);
	modeId = json_object_get_int(modeIdObj);
	
	/* Find the station */
	station = BarStateFindStationById(app, stationId);
	if (!station) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: setStationMode - station not found\n");
		return;
	}
	
	log_write(DEBUG_WEBSOCKET, "Socket.IO: Setting station mode to %d\n", modeId);
	
	/* Set station mode */
	reqData.station = station;
	reqData.id = modeId;
	
	BarSocketIoPianoCallLogged(app, PIANO_REQUEST_SET_STATION_MODE, &reqData,
			"Setting station mode", "station.setStationMode", &pRet, &wRet);
}

/* Handle 'station.getInfo' event - fetch station info for seed/feedback management */
void BarSocketIoHandleGetStationInfo(BarApp_t *app, json_object *data) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataGetStationInfo_t reqData;
	json_object *stationIdObj;
	const char *stationId;
	PianoStation_t *station;
	
	if (!app || !data) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: getStationInfo - invalid parameters\n");
		return;
	}
	
	/* Extract stationId */
	if (!json_object_object_get_ex(data, "stationId", &stationIdObj)) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: getStationInfo - missing stationId\n");
		return;
	}
	
	stationId = json_object_get_string(stationIdObj);
	
	/* Find the station */
	station = BarStateFindStationById(app, stationId);
	if (!station) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: getStationInfo - station not found\n");
		return;
	}
	
	/* Fetch station info */
	memset(&reqData, 0, sizeof(reqData));
	reqData.station = station;
	
	if (BarSocketIoPianoCallLogged(app, PIANO_REQUEST_GET_STATION_INFO, &reqData,
			"Fetching station info", "station.getStationInfo", &pRet, &wRet)) {
		BarSocketIoEmitStationInfo(app, &reqData);
	}
	
	PianoDestroyStationInfo(&reqData.info);
}

/* Emit station info (seeds and feedback) to client */
void BarSocketIoEmitStationInfo(BarApp_t *app, PianoRequestDataGetStationInfo_t *reqData) {
	json_object *data, *artistsArray, *songsArray, *stationsArray, *feedbackArray;
	json_object *itemObj;
	PianoArtist_t *artist;
	PianoSong_t *song;
	PianoStation_t *station;
	
	if (!app || !reqData) {
		return;
	}
	
	data = json_object_new_object();
	
	/* Artist seeds */
	artistsArray = json_object_new_array();
	artist = reqData->info.artistSeeds;
	PianoListForeachP(artist) {
		itemObj = json_object_new_object();
		json_object_object_add(itemObj, "seedId", BarJsonStringOrEmpty (artist->seedId));
		json_object_object_add(itemObj, "name",   BarJsonStringOrEmpty (artist->name));
		json_object_array_add(artistsArray, itemObj);
	}
	json_object_object_add(data, "artistSeeds", artistsArray);
	
	/* Song seeds */
	songsArray = json_object_new_array();
	song = reqData->info.songSeeds;
	PianoListForeachP(song) {
		itemObj = json_object_new_object();
		json_object_object_add(itemObj, "seedId", BarJsonStringOrEmpty (song->seedId));
		json_object_object_add(itemObj, "title",  BarJsonStringOrEmpty (song->title));
		json_object_object_add(itemObj, "artist", BarJsonStringOrEmpty (song->artist));
		json_object_array_add(songsArray, itemObj);
	}
	json_object_object_add(data, "songSeeds", songsArray);
	
	/* Station seeds */
	stationsArray = json_object_new_array();
	station = reqData->info.stationSeeds;
	PianoListForeachP(station) {
		itemObj = json_object_new_object();
		json_object_object_add(itemObj, "seedId", BarJsonStringOrEmpty (station->seedId));
		json_object_object_add(itemObj, "name",   BarJsonStringOrEmpty (station->name));
		json_object_array_add(stationsArray, itemObj);
	}
	json_object_object_add(data, "stationSeeds", stationsArray);
	
	/* Feedback */
	feedbackArray = json_object_new_array();
	song = reqData->info.feedback;
	PianoListForeachP(song) {
		itemObj = json_object_new_object();
		json_object_object_add(itemObj, "feedbackId", BarJsonStringOrEmpty (song->feedbackId));
		json_object_object_add(itemObj, "title",      BarJsonStringOrEmpty (song->title));
		json_object_object_add(itemObj, "artist",     BarJsonStringOrEmpty (song->artist));
		json_object_object_add(itemObj, "rating",     json_object_new_int(song->rating));
		json_object_array_add(feedbackArray, itemObj);
	}
	json_object_object_add(data, "feedback", feedbackArray);
	
	BarSocketIoEmit("stationInfo", data);
	json_object_put(data);
}

/* Handle 'station.deleteSeed' event */
void BarSocketIoHandleDeleteSeed(BarApp_t *app, json_object *data) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataDeleteSeed_t reqData;
	PianoRequestDataGetStationInfo_t infoReqData;
	json_object *seedIdObj, *seedTypeObj, *stationIdObj;
	const char *seedId, *seedType, *stationId;
	PianoStation_t *station;
	
	if (!app || !data) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: deleteSeed - invalid parameters\n");
		return;
	}
	
	/* Extract parameters */
	if (!json_object_object_get_ex(data, "seedId", &seedIdObj) ||
	    !json_object_object_get_ex(data, "seedType", &seedTypeObj) ||
	    !json_object_object_get_ex(data, "stationId", &stationIdObj)) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: deleteSeed - missing parameters\n");
		return;
	}
	
	seedId = json_object_get_string(seedIdObj);
	seedType = json_object_get_string(seedTypeObj);
	stationId = json_object_get_string(stationIdObj);
	
	/* Find the station */
	station = BarStateFindStationById(app, stationId);
	if (!station) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: deleteSeed - station not found\n");
		return;
	}
	
	/* Fetch station info to find the seed object */
	memset(&infoReqData, 0, sizeof(infoReqData));
	infoReqData.station = station;
	
	if (!BarSocketIoPianoCallLogged(app, PIANO_REQUEST_GET_STATION_INFO, &infoReqData,
			"Fetching station info", "station.deleteSeed", &pRet, &wRet)) {
		return;
	}
	
	/* Find and delete the seed based on type */
	memset(&reqData, 0, sizeof(reqData));
	
	if (strcmp(seedType, "artist") == 0) {
		PianoArtist_t *artist = infoReqData.info.artistSeeds;
		PianoListForeachP(artist) {
			if (strcmp(artist->seedId, seedId) == 0) {
				reqData.artist = artist;
				break;
			}
		}
		if (reqData.artist != NULL) {
			log_write(DEBUG_WEBSOCKET, "Socket.IO: Deleting artist seed\n");
			BarSocketIoPianoCallLogged(app, PIANO_REQUEST_DELETE_SEED, &reqData,
					"Deleting artist seed", "station.deleteSeed", &pRet, &wRet);
		}
	} else if (strcmp(seedType, "song") == 0) {
		PianoSong_t *song = infoReqData.info.songSeeds;
		PianoListForeachP(song) {
			if (strcmp(song->seedId, seedId) == 0) {
				reqData.song = song;
				break;
			}
		}
		if (reqData.song != NULL) {
			log_write(DEBUG_WEBSOCKET, "Socket.IO: Deleting song seed\n");
			BarSocketIoPianoCallLogged(app, PIANO_REQUEST_DELETE_SEED, &reqData,
					"Deleting song seed", "station.deleteSeed", &pRet, &wRet);
		}
	} else if (strcmp(seedType, "station") == 0) {
		PianoStation_t *seedStation = infoReqData.info.stationSeeds;
		PianoListForeachP(seedStation) {
			if (strcmp(seedStation->seedId, seedId) == 0) {
				reqData.station = seedStation;
				break;
			}
		}
		if (reqData.station != NULL) {
			log_write(DEBUG_WEBSOCKET, "Socket.IO: Deleting station seed\n");
			BarSocketIoPianoCallLogged(app, PIANO_REQUEST_DELETE_SEED, &reqData,
					"Deleting station seed", "station.deleteSeed", &pRet, &wRet);
		}
	}
	
	PianoDestroyStationInfo(&infoReqData.info);
}

/* Handle 'station.deleteFeedback' event */
void BarSocketIoHandleDeleteFeedback(BarApp_t *app, json_object *data) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataGetStationInfo_t infoReqData;
	json_object *feedbackIdObj, *stationIdObj;
	const char *feedbackId, *stationId;
	PianoStation_t *station;
	PianoSong_t *feedbackSong = NULL;
	
	if (!app || !data) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: deleteFeedback - invalid parameters\n");
		return;
	}
	
	/* Extract parameters */
	if (!json_object_object_get_ex(data, "feedbackId", &feedbackIdObj) ||
	    !json_object_object_get_ex(data, "stationId", &stationIdObj)) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: deleteFeedback - missing parameters\n");
		return;
	}
	
	feedbackId = json_object_get_string(feedbackIdObj);
	stationId = json_object_get_string(stationIdObj);
	
	/* Find the station */
	station = BarStateFindStationById(app, stationId);
	if (!station) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: deleteFeedback - station not found\n");
		return;
	}
	
	/* Fetch station info to find the feedback object */
	memset(&infoReqData, 0, sizeof(infoReqData));
	infoReqData.station = station;
	
	if (!BarSocketIoPianoCallLogged(app, PIANO_REQUEST_GET_STATION_INFO, &infoReqData,
			"Fetching station info", "station.deleteFeedback", &pRet, &wRet)) {
		return;
	}
	
	/* Find the feedback entry */
	PianoSong_t *song = infoReqData.info.feedback;
	PianoListForeachP(song) {
		if (strcmp(song->feedbackId, feedbackId) == 0) {
			feedbackSong = song;
			break;
		}
	}
	
	if (feedbackSong != NULL) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: Deleting feedback\n");
		BarSocketIoPianoCallLogged(app, PIANO_REQUEST_DELETE_FEEDBACK, feedbackSong,
				"Deleting feedback", "station.deleteFeedback", &pRet, &wRet);
	}
	
	PianoDestroyStationInfo(&infoReqData.info);
}

/* Emit 'searchResults' event (music search results) */
void BarSocketIoEmitSearchResults(BarApp_t *app, PianoSearchResult_t *searchResult) {
	json_object *data, *categories, *categoryObj, *resultsArray, *resultObj;
	PianoArtist_t *artist;
	PianoSong_t *song;
	
	if (!app || !searchResult) {
		return;
	}
	
	data = json_object_new_object();
	categories = json_object_new_array();
	
	/* Create Artists category */
	if (searchResult->artists != NULL) {
		categoryObj = json_object_new_object();
		json_object_object_add(categoryObj, "name", BarJsonStringOrEmpty("Artists"));
		
		resultsArray = json_object_new_array();
		artist = searchResult->artists;
		
		while (artist != NULL) {
			resultObj = json_object_new_object();
			
			json_object_object_add(resultObj, "name",    BarJsonStringOrEmpty(artist->name));
			json_object_object_add(resultObj, "musicId", BarJsonStringOrEmpty(artist->musicId));
			
			json_object_array_add(resultsArray, resultObj);
			
			artist = (PianoArtist_t *)artist->head.next;
		}
		
		json_object_object_add(categoryObj, "results", resultsArray);
		json_object_array_add(categories, categoryObj);
	}
	
	/* Create Songs category */
	if (searchResult->songs != NULL) {
		categoryObj = json_object_new_object();
		json_object_object_add(categoryObj, "name", BarJsonStringOrEmpty("Songs"));
		
		resultsArray = json_object_new_array();
		song = searchResult->songs;
		
		while (song != NULL) {
			resultObj = json_object_new_object();
			
			json_object_object_add(resultObj, "title",   BarJsonStringOrEmpty(song->title));
			json_object_object_add(resultObj, "artist",  BarJsonStringOrEmpty(song->artist));
			json_object_object_add(resultObj, "musicId", BarJsonStringOrEmpty(song->musicId));
			
			json_object_array_add(resultsArray, resultObj);
			
			song = (PianoSong_t *)song->head.next;
		}
		
		json_object_object_add(categoryObj, "results", resultsArray);
		json_object_array_add(categories, categoryObj);
	}
	
	json_object_object_add(data, "categories", categories);
	
	BarSocketIoEmit("searchResults", data);
	json_object_put(data);
}

/* Handle 'music.search' event from client */
void BarSocketIoHandleSearchMusic(BarApp_t *app, json_object *data) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataSearch_t reqData;
	json_object *queryObj;
	const char *query;
	
	if (!app || !data) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: music.search - invalid parameters\n");
		return;
	}
	
	/* Extract query from data */
	if (!json_object_object_get_ex(data, "query", &queryObj)) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: music.search - missing query\n");
		return;
	}
	
	query = json_object_get_string(queryObj);
	
	if (!query) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: music.search - invalid query\n");
		return;
	}
	
	log_write(DEBUG_WEBSOCKET, "Socket.IO: Searching for: %s\n", query);
	
	/* Set up request data */
	reqData.searchStr = (char *)query;
	memset(&reqData.searchResult, 0, sizeof(reqData.searchResult));
	
	/* Perform the search */
	if (BarSocketIoPianoCallLogged(app, PIANO_REQUEST_SEARCH, &reqData,
			"Searching", "music.search", &pRet, &wRet)) {
		BarSocketIoEmitSearchResults(app, &reqData.searchResult);
		PianoDestroySearchResult(&reqData.searchResult);
	}
}

/* Handle 'action' event from client */
void BarSocketIoHandleAction(BarApp_t *app, const char *action, json_object *data, void *wsi) {
	BarKeyShortcutId_t actionId;
	
	if (!app || !action || !app->wsContext) {
		return;
	}
	
	/* Special handling for volume.set with percentage value */
	if (strcmp(action, "volume.set") == 0 && data) {
		json_object *volumeObj;
		if (json_object_object_get_ex(data, "volume", &volumeObj)) {
			int volumePercent = json_object_get_int(volumeObj);
			/* Clamp to 0-100 range */
			if (volumePercent < 0) volumePercent = 0;
			if (volumePercent > VOLUME_MAX_PERCENT) volumePercent = VOLUME_MAX_PERCENT;
			
			if (app->settings.volumeMode == BAR_VOLUME_MODE_SYSTEM) {
				/* System volume mode - set OS volume directly */
				log_write(DEBUG_WEBSOCKET, "Socket.IO: Action '%s' → system volume=%d%%\n", 
				           action, volumePercent);
				BarSystemVolumeSet(volumePercent);
			} else {
				/* Player volume mode - use percentage directly (linear 0-100) */
				log_write(DEBUG_WEBSOCKET, "Socket.IO: Action '%s' → volume=%d%%\n", 
				           action, volumePercent);
				app->settings.volume = volumePercent;
				BarPlayerSetVolume(&app->player);
			}
			
			/* Schedule debounced broadcast (will read current volume at broadcast time) */
			BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
			BarWsScheduleVolumeBroadcast(ctx, 500);  /* 500ms debounce */
			
			return;
		}
	}
	
	/* Special handling for app.pandora-reconnect with account_id:
	 * Account switching must work regardless of Pandora connection state,
	 * so call the reconnect callback directly, bypassing the dispatch
	 * table's context check (which requires BAR_DC_PANDORA_DISCONNECTED). */
	if (strcmp(action, "app.pandora-reconnect") == 0 && data) {
		json_object *accountIdObj;
		if (json_object_object_get_ex(data, "account_id", &accountIdObj)) {
			const char *accountId = json_object_get_string(accountIdObj);
			if (accountId) {
				if (!BarSettingsSetActiveAccountById(&app->settings, accountId)) {
					log_write(DEBUG_WEBSOCKET, "Socket.IO: Unknown account_id: %s\n", accountId);
					BarSocketIoEmitErrorEx(app, "app.pandora-reconnect", "Unknown account", accountId);
					return;
				}
				log_write(DEBUG_WEBSOCKET, "Socket.IO: Switching to account '%s', calling reconnect directly\n", accountId);
				PianoStation_t *curStation = BarStateGetCurrentStation(app);
				PianoSong_t *curSong = BarStateGetPlaylist(app);
				BarUiActPandoraReconnect(app, curStation, curSong, BAR_DC_GLOBAL);
				BarSocketIoEmitProcess(app);
				return;
			}
		}
	}

	/* Look up action — check capability before dispatch */
	const BarActionMapping_t *mapping = BarSocketIoLookupAction (action);

	if (mapping == NULL) {
		log_write (DEBUG_WEBSOCKET, "Socket.IO: Unknown or unsupported action: %s\n", action);
		log_write (DEBUG_WEBSOCKET, "Socket.IO: Use descriptive commands (e.g., playback.next, song.love)\n");
		return;
	}

	if (mapping->capability == BAR_SOCKETIO_ACTION_NOT_IMPLEMENTED) {
		log_write (DEBUG_WEBSOCKET, "Socket.IO: Action '%s' is not implemented\n", action);
		BarSocketIoEmitNotImplemented (app, action, mapping->reasonKey);
		return;
	}

	actionId = mapping->actionId;
	
	log_write(DEBUG_WEBSOCKET, "Socket.IO: Action '%s' → ID %d (executing directly)\n", 
	           action, actionId);
	
	/* Set context based on Pandora connection status */
	BarUiDispatchContext_t context = BAR_DC_GLOBAL;
	if (BarStateIsPandoraConnected(app)) {
		context |= BAR_DC_PANDORA_CONNECTED;
	} else {
		context |= BAR_DC_PANDORA_DISCONNECTED;
	}
	
	/* Add station context if station is selected */
	PianoStation_t *currentStation = BarStateGetCurrentStation(app);
	if (currentStation) {
		context |= BAR_DC_STATION;
	}
	
	/* Add song context if song is playing */
	PianoSong_t *currentSong = BarStateGetPlaylist(app);
	if (currentSong) {
		context |= BAR_DC_SONG;
	}
	
	/* When Pandora is disconnected, treat playback.play as reconnect so clients can always send "play" */
	if (actionId == BAR_KS_PLAY && (context & BAR_DC_PANDORA_DISCONNECTED)) {
		actionId = BAR_KS_PANDORA_RECONNECT;
		log_write(DEBUG_WEBSOCKET, "Socket.IO: playback.play → pandora-reconnect (Pandora disconnected)\n");
	}
	
	/* Check if action can be executed in current context */
	const BarUiDispatchAction_t *dispatcher = BarUiDispatchLookupById(actionId);
	if (dispatcher && (dispatcher->context & context) != dispatcher->context) {
		/* Action requirements not met - send error to client */
		const char *errorMsg = NULL;
		if ((dispatcher->context & BAR_DC_SONG) && !currentSong) {
			errorMsg = "No song is currently playing";
		} else if ((dispatcher->context & BAR_DC_STATION) && !currentStation) {
			errorMsg = "No station is selected";
		} else if ((dispatcher->context & BAR_DC_PANDORA_CONNECTED) && 
		           !(context & BAR_DC_PANDORA_CONNECTED)) {
			errorMsg = "Not connected to Pandora";
		} else {
			errorMsg = "Action cannot be performed in current context";
		}
		
		log_write(DEBUG_WEBSOCKET, "Socket.IO: Action '%s' failed: %s\n", action, errorMsg);
		BarSocketIoEmitError(app, action, errorMsg);
		return;
	}

	/* For query actions (explain, upcoming), set unicast target so response
	 * only goes to the requesting client, not all clients */
	bool useUnicast = (actionId == BAR_KS_EXPLAIN || actionId == BAR_KS_UPCOMING);
	if (useUnicast && wsi) {
		BarSocketIoSetUnicastTarget(wsi);
	}
	
	/* Execute action directly by ID in WebSocket thread
	 * 
	 * THREAD SAFETY: This call may trigger state lock acquisition via:
	 * - BarStateGet* / BarStateSet* use stateRwlock in web and both (BarStateUsesRwlock)
	 * - Action callbacks may acquire player.lock (e.g., BarUiActPlay)
	 * - BarUiPianoCall (Pandora HTTP) serializes on app->pianoHttpMutex
	 * 
	 * Lock ordering is safe: stateRwlock → player.lock (if both needed)
	 * See src/THREAD_SAFETY.md for details */
	BarUiDispatchById(app, actionId, currentStation, currentSong, false, context);
	
	/* Clear unicast target after query actions */
	if (useUnicast) {
		BarSocketIoSetUnicastTarget(NULL);
	}
	
	/* Note: Play/pause/toggle broadcast state via BarWsBroadcastPlayState() in ui_act.c.
	 * song.love / song.ban emit a full `process` (not `start`) via BarWsBroadcastProcess
	 * in BarUiActLoveSong/BarUiActBanSong after a successful PIANO_REQUEST_RATE_SONG —
	 * do not add BarSocketIoEmitStart here (duplicate). */
}

/* Handle 'changeStation' event from client */
void BarSocketIoHandleChangeStation(BarApp_t *app, const char *stationId) {
	PianoStation_t *station;
	
	if (!app || !stationId) {
		return;
	}
	
	log_write(DEBUG_WEBSOCKET, "Socket.IO: Change station request: %s\n", stationId);
	
	/* Find station by ID */
	station = BarStateFindStationById(app, stationId);
	
	if (station) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: Switching to station: %s\n", station->name);
		/* Clear saved station - user made explicit choice */
		free(app->lastStationId);
		app->lastStationId = NULL;
		/* Switch to the new station and drain current playlist */
		BarUiSwitchStation(app, station);
		log_write(DEBUG_WEBSOCKET, "Socket.IO: Station switch initiated\n");
	} else {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: Station not found: %s\n", stationId);
		BarSocketIoEmitErrorEx(app, "station.change", "Station not found", stationId);
	}
}

/* Handle 'station.setQuickMix' event from client */
void BarSocketIoHandleSetQuickMix(BarApp_t *app, json_object *data) {
	PianoStation_t *station;
	json_object *stationIdsArray;
	int arrayLen, i;
	
	if (!app || !data) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: setQuickMix - invalid parameters\n");
		return;
	}
	
	/* Expect data to be an array of station IDs */
	if (!json_object_is_type(data, json_type_array)) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: setQuickMix - data is not an array\n");
		return;
	}
	
	log_write(DEBUG_WEBSOCKET, "Socket.IO: Setting QuickMix stations...\n");
	
	/* First, set all stations to NOT be in QuickMix */
	station = BarStateGetStationList(app);
	PianoListForeachP (station) {
		station->useQuickMix = false;
	}
	
	/* Now set the selected stations to be in QuickMix */
	arrayLen = json_object_array_length(data);
	for (i = 0; i < arrayLen; i++) {
		json_object *stationIdObj = json_object_array_get_idx(data, i);
		if (stationIdObj && json_object_is_type(stationIdObj, json_type_string)) {
			const char *stationId = json_object_get_string(stationIdObj);
			
			/* Find station by ID */
			station = BarStateGetStationList(app);
			PianoListForeachP (station) {
				if (strcmp(station->id, stationId) == 0) {
					station->useQuickMix = true;
					log_write(DEBUG_WEBSOCKET, "Socket.IO: Added '%s' to QuickMix\n", 
					           station->name);
					break;
				}
			}
		}
	}
	
	/* Call Pandora API to save QuickMix settings */
	PianoReturn_t pRet;
	CURLcode wRet;
	if (BarSocketIoPianoCallLogged(app, PIANO_REQUEST_SET_QUICKMIX, NULL,
			"Setting QuickMix stations", "station.setQuickMix", &pRet, &wRet)) {
		BarSocketIoEmitStations(app);
	}
}

/* Handle 'station.delete' event from client */
void BarSocketIoHandleDeleteStation(BarApp_t *app, json_object *data) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoStation_t *station;
	const char *stationId;
	bool wasCurrentStation;
	
	if (!app || !data) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: deleteStation - invalid parameters\n");
		return;
	}
	
	/* Extract station ID from data (can be string or in object) */
	if (json_object_is_type(data, json_type_string)) {
		stationId = json_object_get_string(data);
	} else if (json_object_is_type(data, json_type_object)) {
		json_object *stationIdObj;
		if (json_object_object_get_ex(data, "stationId", &stationIdObj)) {
			stationId = json_object_get_string(stationIdObj);
		} else {
			log_write(DEBUG_WEBSOCKET, "Socket.IO: deleteStation - missing stationId\n");
			return;
		}
	} else {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: deleteStation - invalid data type\n");
		return;
	}
	
	if (!stationId) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: deleteStation - null stationId\n");
		return;
	}
	
	log_write(DEBUG_WEBSOCKET, "Socket.IO: Delete station request: %s\n", stationId);
	
	/* Find station by ID */
	station = BarStateFindStationById(app, stationId);
	
	if (!station) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: Station not found: %s\n", stationId);
		return;
	}
	
	log_write(DEBUG_WEBSOCKET, "Socket.IO: Deleting station: %s\n", station->name);
	
	/* Check if this is the currently playing station */
	wasCurrentStation = (station == BarStateGetCurrentStation(app));
	
	/* Delete the station */
	if (BarSocketIoPianoCallLogged(app, PIANO_REQUEST_DELETE_STATION, station,
			"Deleting station", "station.delete", &pRet, &wRet)) {
		if (wasCurrentStation) {
			PianoStation_t *quickMixStation = NULL;
			PianoStation_t *curStation = BarStateGetStationList(app);
			
			while (curStation != NULL) {
				if (curStation->isQuickMix) {
					quickMixStation = curStation;
					break;
				}
				curStation = (PianoStation_t *)curStation->head.next;
			}
			
			if (quickMixStation) {
				log_write(DEBUG_WEBSOCKET, "Socket.IO: Switching to QuickMix after deletion\n");
				BarUiSwitchStation(app, quickMixStation);
			}
			
			BarStateSetCurrentStation(app, NULL);
		}
		
		BarSocketIoEmitStations(app);
	}
}

/* Handle 'station.createFrom' event from client */
void BarSocketIoHandleCreateStationFrom(BarApp_t *app, json_object *data) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataCreateStation_t reqData;
	json_object *trackTokenObj, *typeObj;
	const char *trackToken, *type;
	
	if (!app || !data) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: createFrom - invalid parameters\n");
		return;
	}
	
	/* Extract trackToken and type from data */
	if (!json_object_object_get_ex(data, "trackToken", &trackTokenObj) ||
	    !json_object_object_get_ex(data, "type", &typeObj)) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: createFrom - missing trackToken or type\n");
		return;
	}
	
	trackToken = json_object_get_string(trackTokenObj);
	type = json_object_get_string(typeObj);
	
	if (!trackToken || !type) {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: createFrom - invalid trackToken or type\n");
		return;
	}
	
	log_write(DEBUG_WEBSOCKET, "Socket.IO: Creating station from %s...\n", type);
	
	/* Set up request data */
	reqData.token = (char *)trackToken;
	reqData.type = PIANO_MUSICTYPE_INVALID;
	
	if (strcmp(type, "song") == 0) {
		reqData.type = PIANO_MUSICTYPE_SONG;
	} else if (strcmp(type, "artist") == 0) {
		reqData.type = PIANO_MUSICTYPE_ARTIST;
	} else {
		log_write(DEBUG_WEBSOCKET, "Socket.IO: createFrom - invalid type: %s\n", type);
		return;
	}
	
	/* Create the station */
	if (BarSocketIoPianoCallLogged(app, PIANO_REQUEST_CREATE_STATION, &reqData,
			"Creating station", "station.createFrom", &pRet, &wRet)) {
		BarUpdateStationDisplayNames(app);
		BarSocketIoEmitStations(app);
	}
}

/* Handle 'query' event from client (unicast response to requesting client) */
void BarSocketIoHandleQuery(BarApp_t *app, void *wsi) {
	if (!app) {
		return;
	}
	
	log_write(DEBUG_WEBSOCKET, "Socket.IO: Query received\n");
	
	/* Send full state to requesting client only (unicast) */
	BarSocketIoSetUnicastTarget(wsi);
	BarSocketIoEmitProcess(app);
	BarSocketIoEmitStations(app);
	BarSocketIoSetUnicastTarget(NULL);
}

