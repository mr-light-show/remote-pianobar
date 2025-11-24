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

#include "../../main.h"
#include "../../debug.h"
#include "socketio.h"
#include "../core/websocket.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <json-c/json.h>

/* Convert slider percentage (0-100) to decibels (-40 to maxGain)
 * Uses perceptual curve: squared for bottom half, linear for top half */
static int sliderToDb(int sliderPercent, int maxGain) {
	if (sliderPercent <= 50) {
		/* Bottom half: -40 to 0 dB */
		double normalized = sliderPercent / 50.0;
		return (int)(-40.0 * pow(1.0 - normalized, 2.0));
	} else {
		/* Top half: 0 to maxGain dB */
		double normalized = (sliderPercent - 50) / 50.0;
		return (int)(maxGain * normalized);
	}
}

/* Command mapping: descriptive → single-letter */
typedef struct {
	const char *descriptive;
	const char *singleLetter;
} BarCommandMapping_t;

static const BarCommandMapping_t commandMappings[] = {
	/* Playback */
	{"playback.next", "n"},
	{"playback.toggle", "p"},
	{"playback.play", "P"},
	{"playback.pause", "S"},
	
	/* Volume */
	{"volume.up", ")"},
	{"volume.down", "("},
	{"volume.reset", "^"},
	{"volume.set", "%"},
	
	/* Song */
	{"song.love", "+"},
	{"song.ban", "-"},
	{"song.tired", "t"},
	{"song.bookmark", "b"},
	{"song.explain", "e"},
	{"song.info", "i"},
	{"song.createStationFrom", "v"},
	
	/* Station */
	{"station.change", "s"},
	{"station.create", "c"},
	{"station.delete", "d"},
	{"station.rename", "r"},
	{"station.addMusic", "a"},
	{"station.addGenre", "g"},
	{"station.addShared", "j"},
	{"station.selectQuickMix", "x"},
	{"station.manage", "="},
	
	/* Query */
	{"query.history", "h"},
	{"query.upcoming", "u"},
	{"query.stations", "s"}, /* Special: handled differently */
	
	/* App */
	{"app.quit", "q"},
	{"app.settings", "!"},
	
	{NULL, NULL} /* terminator */
};

/* Translate descriptive command to single-letter command 
 * Returns NULL if command not found (no backward compatibility) */
static const char *BarSocketIoTranslateCommand(const char *command) {
	if (!command) {
		return NULL;
	}
	
	/* Look up descriptive command in mapping table */
	for (size_t i = 0; commandMappings[i].descriptive != NULL; i++) {
		if (strcmp(command, commandMappings[i].descriptive) == 0) {
			return commandMappings[i].singleLetter;
		}
	}
	
	/* Not found - reject single-letter and unknown commands */
	return NULL;
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

/* Handle incoming Socket.IO message */
void BarSocketIoHandleMessage(BarApp_t *app, const char *message) {
	BarSocketIoType_t type;
	char *eventName = NULL;
	json_object *data = NULL;
	
	if (!app || !message) {
		return;
	}
	
	/* Parse Socket.IO message */
	type = BarSocketIoParse(message, &eventName, &data);
	
	if (type == SOCKETIO_CONNECT) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Client connected\n");
		/* Send initial state on connect */
		BarSocketIoHandleQuery(app);
		goto cleanup;
	}
	
	if (type != SOCKETIO_EVENT || !eventName) {
		goto cleanup;
	}
	
	/* Route to appropriate handler */
	if (strcmp(eventName, "action") == 0) {
		if (data && json_object_is_type(data, json_type_string)) {
			const char *action = json_object_get_string(data);
			BarSocketIoHandleAction(app, action, NULL);
		} else if (data && json_object_is_type(data, json_type_object)) {
			json_object *actionObj;
			if (json_object_object_get_ex(data, "action", &actionObj)) {
				const char *action = json_object_get_string(actionObj);
				if (action) {
					BarSocketIoHandleAction(app, action, data);
				}
			} else if (json_object_object_get_ex(data, "command", &actionObj)) {
				const char *action = json_object_get_string(actionObj);
				if (action) {
					BarSocketIoHandleAction(app, action, data);
				}
			}
		}
	} else if (strcmp(eventName, "station.change") == 0 || 
	           strcmp(eventName, "changeStation") == 0) {
		/* Support both new (station.change) and old (changeStation) names */
		if (data && json_object_is_type(data, json_type_string)) {
			const char *station = json_object_get_string(data);
			BarSocketIoHandleChangeStation(app, station);
		} else if (data && json_object_is_type(data, json_type_object)) {
			json_object *stationObj;
			if (json_object_object_get_ex(data, "station", &stationObj)) {
				const char *station = json_object_get_string(stationObj);
				if (station) {
					BarSocketIoHandleChangeStation(app, station);
				}
			}
		}
	} else if (strcmp(eventName, "query") == 0 || 
	           strcmp(eventName, "query.state") == 0) {
		/* query or query.state = full state */
		BarSocketIoHandleQuery(app);
	} else if (strcmp(eventName, "query.stations") == 0) {
		/* query.stations = just stations list */
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Query stations received\n");
		BarSocketIoEmitStations(app);
	} else {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Unknown event: %s\n", eventName);
	}
	
cleanup:
	free(eventName);
	if (data) {
		json_object_put(data);
	}
}

/* Global broadcast callback (set by WebSocket core) */
static void (*g_broadcastCallback)(const char *message, size_t len) = NULL;

/* Set broadcast callback */
void BarSocketIoSetBroadcastCallback(void (*callback)(const char *, size_t)) {
	g_broadcastCallback = callback;
}

/* Format Socket.IO event message */
static char *BarSocketIoFormat(const char *event, json_object *data) {
	json_object *arr;
	const char *jsonStr;
	char *formatted;
	size_t len;
	
	if (!event) {
		return NULL;
	}
	
	/* Create JSON array: ["event", data] */
	arr = json_object_new_array();
	json_object_array_add(arr, json_object_new_string(event));
	
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
		return;
	}
	
	message = BarSocketIoFormat(event, data);
	if (!message) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Failed to format message\n");
		return;
	}
	
	/* Broadcast to all clients if callback is set */
	if (g_broadcastCallback) {
		g_broadcastCallback(message, strlen(message));
	} else {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Emit '%s' (no broadcast callback set)\n", event);
	}
	
	free(message);
}

/* Emit 'start' event (song started) */
void BarSocketIoEmitStart(BarApp_t *app) {
	json_object *data;
	PianoStation_t *songStation;
	
	if (!app || !app->playlist) {
		return;
	}
	
	PianoSong_t *song = app->playlist;
	
	data = json_object_new_object();
	json_object_object_add(data, "artist", 
	                       json_object_new_string(song->artist));
	json_object_object_add(data, "title", 
	                       json_object_new_string(song->title));
	json_object_object_add(data, "album", 
	                       json_object_new_string(song->album));
	json_object_object_add(data, "coverArt", 
	                       json_object_new_string(song->coverArt));
	json_object_object_add(data, "rating", 
	                       json_object_new_int(song->rating));
	json_object_object_add(data, "duration", 
	                       json_object_new_int(song->length));
	
	if (app->curStation) {
		json_object_object_add(data, "station", 
		                       json_object_new_string(app->curStation->name));
	}
	
	/* Station the song came from (important in QuickMix) */
	if (song->stationId) {
		songStation = PianoFindStationById(app->ph.stations, song->stationId);
		if (songStation) {
			json_object_object_add(data, "songStationName",
			                       json_object_new_string(songStation->name));
		}
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
	json_object_object_add(data, "elapsed", json_object_new_int(elapsed));
	json_object_object_add(data, "duration", json_object_new_int(duration));
	json_object_object_add(data, "percentage", json_object_new_double(percentage));
	
	BarSocketIoEmit("progress", data);
	json_object_put(data);
}

/* Emit 'stations' event (station list) */
void BarSocketIoEmitStations(BarApp_t *app) {
	json_object *stations, *station;
	PianoStation_t *curStation;
	
	if (!app) {
		return;
	}
	
	stations = json_object_new_array();
	
	curStation = app->ph.stations;
	while (curStation != NULL) {
		station = json_object_new_object();
		json_object_object_add(station, "id", 
		                       json_object_new_string(curStation->id));
		json_object_object_add(station, "name", 
		                       json_object_new_string(curStation->name));
		json_object_object_add(station, "isQuickMix", 
		                       json_object_new_boolean(curStation->isQuickMix));
		json_object_object_add(station, "isQuickMixed", 
		                       json_object_new_boolean(curStation->useQuickMix));
		
		json_object_array_add(stations, station);
		curStation = (PianoStation_t *) curStation->head.next;
	}
	
	BarSocketIoEmit("stations", stations);
	json_object_put(stations);
}

/* Emit 'process' event (full state) */
void BarSocketIoEmitProcess(BarApp_t *app) {
	json_object *data, *song;
	
	if (!app) {
		return;
	}
	
	data = json_object_new_object();
	json_object_object_add(data, "playing", 
	                       json_object_new_boolean(app->playlist != NULL));
	
	/* Include current volume */
	json_object_object_add(data, "volume", 
	                       json_object_new_int(app->settings.volume));
	
	/* Include max gain configuration */
	json_object_object_add(data, "maxGain", 
	                       json_object_new_int(app->settings.maxGain));
	
	if (app->curStation) {
		json_object_object_add(data, "station", 
		                       json_object_new_string(app->curStation->name));
	}
	
	if (app->playlist) {
		PianoStation_t *songStation;
		
		song = json_object_new_object();
		json_object_object_add(song, "title", 
		                       json_object_new_string(app->playlist->title));
		json_object_object_add(song, "artist", 
		                       json_object_new_string(app->playlist->artist));
		json_object_object_add(song, "album", 
		                       json_object_new_string(app->playlist->album));
		json_object_object_add(song, "coverArt", 
		                       json_object_new_string(app->playlist->coverArt));
		json_object_object_add(song, "rating", 
		                       json_object_new_int(app->playlist->rating));
		json_object_object_add(song, "duration", 
		                       json_object_new_int(app->playlist->length));
		
		/* Station the song came from (important in QuickMix) */
		if (app->playlist->stationId) {
			songStation = PianoFindStationById(app->ph.stations, app->playlist->stationId);
			if (songStation) {
				json_object_object_add(song, "songStationName",
				                       json_object_new_string(songStation->name));
			}
		}
		
		json_object_object_add(data, "song", song);
	}
	
	BarSocketIoEmit("process", data);
	json_object_put(data);
}

/* Helper: Find station by name or ID */
static PianoStation_t *BarSocketIoFindStation(BarApp_t *app, const char *nameOrId) {
	PianoStation_t *station;
	
	if (!app || !nameOrId) {
		return NULL;
	}
	
	station = app->ph.stations;
	while (station != NULL) {
		if (strcmp(station->name, nameOrId) == 0 ||
		    strcmp(station->id, nameOrId) == 0) {
			return station;
		}
		station = (PianoStation_t *)station->head.next;
	}
	
	return NULL;
}

/* Handle 'action' event from client */
void BarSocketIoHandleAction(BarApp_t *app, const char *action, json_object *data) {
	const char *translated;
	BarWsContext_t *ctx;
	char commandBuffer[16];
	
	if (!app || !action || !app->wsContext) {
		return;
	}
	
	/* Translate descriptive command to single-letter */
	translated = BarSocketIoTranslateCommand(action);
	
	if (!translated) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Unknown or unsupported action: %s\n", action);
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Use descriptive commands (e.g., playback.next, song.love)\n");
		return;
	}
	
	/* Special handling for volume.set with percentage value */
	if (strcmp(action, "volume.set") == 0 && data) {
		json_object *volumeObj;
		if (json_object_object_get_ex(data, "volume", &volumeObj)) {
			int volumePercent = json_object_get_int(volumeObj);
			/* Clamp to 0-100 range */
			if (volumePercent < 0) volumePercent = 0;
			if (volumePercent > 100) volumePercent = 100;
			
			/* Convert percentage to dB using perceptual curve */
			int volumeDb = sliderToDb(volumePercent, app->settings.maxGain);
			
			/* Format as "%<db>" (command + value) */
			snprintf(commandBuffer, sizeof(commandBuffer), "%%%d", volumeDb);
			debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Action '%s' → '%s' (%d%% = %ddB)\n", 
			           action, translated, volumePercent, volumeDb);
			
			/* Queue command for main loop to process */
			ctx = (BarWsContext_t *)app->wsContext;
			BarWsQueuePush(&ctx->commandQueue, MSG_TYPE_COMMAND_ACTION, 
			               commandBuffer, strlen(commandBuffer) + 1);
			return;
		}
	}
	
	debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Action '%s' → '%s'\n", action, translated);
	
	/* Queue command for main loop to process */
	ctx = (BarWsContext_t *)app->wsContext;
	BarWsQueuePush(&ctx->commandQueue, MSG_TYPE_COMMAND_ACTION, 
	               translated, strlen(translated) + 1);
}

/* Handle 'changeStation' event from client */
void BarSocketIoHandleChangeStation(BarApp_t *app, const char *stationName) {
	PianoStation_t *station;
	
	if (!app || !stationName) {
		return;
	}
	
	debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Change station request: %s\n", stationName);
	
	/* Find station by name or ID */
	station = BarSocketIoFindStation(app, stationName);
	
	if (station) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Switching to station: %s\n", station->name);
		/* Set as next station to play */
		app->nextStation = station;
		
		/* Queue the station change command to trigger immediate change */
		BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
		if (ctx) {
			BarWsQueuePush(&ctx->commandQueue, MSG_TYPE_COMMAND_ACTION, 
			               "s", 2);
			debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Queued station change command\n");
		}
	} else {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Station not found: %s\n", stationName);
	}
}

/* Handle 'query' event from client */
void BarSocketIoHandleQuery(BarApp_t *app) {
	if (!app) {
		return;
	}
	
	debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Query received\n");
	
	/* Send full state to client */
	BarSocketIoEmitProcess(app);
	BarSocketIoEmitStations(app);
}

