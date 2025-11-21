/*
Copyright (c) 2025
    Kyle Hawes <khawes@netflix.com>

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
#include "socketio.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <json-c/json.h>

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
		fprintf(stderr, "Socket.IO: Client connected\n");
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
			BarSocketIoHandleAction(app, action);
		} else if (data && json_object_is_type(data, json_type_object)) {
			json_object *actionObj;
			if (json_object_object_get_ex(data, "action", &actionObj)) {
				const char *action = json_object_get_string(actionObj);
				if (action) {
					BarSocketIoHandleAction(app, action);
				}
			}
		}
	} else if (strcmp(eventName, "changeStation") == 0) {
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
	} else if (strcmp(eventName, "query") == 0) {
		BarSocketIoHandleQuery(app);
	} else {
		fprintf(stderr, "Socket.IO: Unknown event: %s\n", eventName);
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
		fprintf(stderr, "Socket.IO: Failed to format message\n");
		return;
	}
	
	/* Broadcast to all clients if callback is set */
	if (g_broadcastCallback) {
		g_broadcastCallback(message, strlen(message));
	} else {
		fprintf(stderr, "Socket.IO: Emit '%s' (no broadcast callback set)\n", event);
	}
	
	free(message);
}

/* Emit 'start' event (song started) */
void BarSocketIoEmitStart(BarApp_t *app) {
	json_object *data;
	
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
	
	if (app->curStation) {
		json_object_object_add(data, "station", 
		                       json_object_new_string(app->curStation->name));
	}
	
	if (app->playlist) {
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
void BarSocketIoHandleAction(BarApp_t *app, const char *action) {
	if (!app || !action) {
		return;
	}
	
	fprintf(stderr, "Socket.IO: Action received: %s\n", action);
	
	/* Map common actions to pianobar commands
	 * For Phase 2.1, we log the action. Full integration with
	 * pianobar's command system will be in Phase 4.
	 * 
	 * Common actions:
	 * 'n' -> next song
	 * 'p' -> play/pause
	 * 'P' -> pause
	 * 'S' -> play
	 * '+' -> love song
	 * '-' -> ban song
	 * 't' -> tired of song
	 * 'v75' -> set volume to 75%
	 * 's' -> select station
	 * 'q' -> quit
	 */
	
	/* For now, handle volume changes as a demonstration */
	if (action[0] == 'v' && strlen(action) > 1) {
		int volume = atoi(action + 1);
		if (volume >= 0 && volume <= 100) {
			fprintf(stderr, "Socket.IO: Volume change to %d%% (would set player volume)\n", volume);
			/* In Phase 4, we'll actually set: app->player.volume = volume */
		}
	}
	
	/* Queue action for main loop processing
	 * In Phase 4, actions will be queued for processing by the main event loop */
}

/* Handle 'changeStation' event from client */
void BarSocketIoHandleChangeStation(BarApp_t *app, const char *stationName) {
	PianoStation_t *station;
	
	if (!app || !stationName) {
		return;
	}
	
	fprintf(stderr, "Socket.IO: Change station request: %s\n", stationName);
	
	/* Find station by name or ID */
	station = BarSocketIoFindStation(app, stationName);
	
	if (station) {
		fprintf(stderr, "Socket.IO: Switching to station: %s\n", station->name);
		/* Set as next station to play */
		app->nextStation = station;
		
		/* In Phase 4, we'll trigger station change in main loop:
		 * - Clear current playlist
		 * - Fetch new songs from nextStation
		 * - Start playback
		 * For now, we just set nextStation which will be picked up
		 * when current playlist runs out or user skips
		 */
	} else {
		fprintf(stderr, "Socket.IO: Station not found: %s\n", stationName);
	}
}

/* Handle 'query' event from client */
void BarSocketIoHandleQuery(BarApp_t *app) {
	if (!app) {
		return;
	}
	
	fprintf(stderr, "Socket.IO: Query received\n");
	
	/* Send full state to client */
	BarSocketIoEmitProcess(app);
	BarSocketIoEmitStations(app);
}

