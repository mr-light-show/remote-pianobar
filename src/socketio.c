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

#include "main.h"
#include "socketio.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <json-c/json.h>

/* Handle incoming Socket.IO message */
void BarSocketIoHandleMessage(BarApp_t *app, const char *message) {
	json_object *msg, *event, *data;
	const char *eventName;
	
	if (!app || !message) {
		return;
	}
	
	/* Parse JSON message */
	msg = json_tokener_parse(message);
	if (!msg) {
		fprintf(stderr, "Socket.IO: Failed to parse message\n");
		return;
	}
	
	/* Get event name */
	if (!json_object_object_get_ex(msg, "event", &event)) {
		json_object_put(msg);
		return;
	}
	
	eventName = json_object_get_string(event);
	if (!eventName) {
		json_object_put(msg);
		return;
	}
	
	/* Get event data (optional) */
	json_object_object_get_ex(msg, "data", &data);
	
	/* Route to appropriate handler */
	if (strcmp(eventName, "action") == 0) {
		if (data) {
			const char *action = json_object_get_string(data);
			if (action) {
				BarSocketIoHandleAction(app, action);
			}
		}
	} else if (strcmp(eventName, "changeStation") == 0) {
		if (data) {
			const char *station = json_object_get_string(data);
			if (station) {
				BarSocketIoHandleChangeStation(app, station);
			}
		}
	} else if (strcmp(eventName, "query") == 0) {
		BarSocketIoHandleQuery(app);
	} else {
		fprintf(stderr, "Socket.IO: Unknown event: %s\n", eventName);
	}
	
	json_object_put(msg);
}

/* Emit event to all connected clients */
void BarSocketIoEmit(const char *event, json_object *data) {
	/* TODO: Implement broadcasting to all WebSocket clients */
	/* This will be implemented when integrating with websocket.c */
	fprintf(stderr, "Socket.IO: Emit %s (not yet implemented)\n", event);
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

/* Handle 'action' event from client */
void BarSocketIoHandleAction(BarApp_t *app, const char *action) {
	if (!app || !action) {
		return;
	}
	
	fprintf(stderr, "Socket.IO: Action received: %s\n", action);
	
	/* TODO: Map actions to pianobar commands */
	/* Examples:
	 * 'n' -> next song
	 * 'p' -> play/pause
	 * 'v75' -> set volume to 75%
	 * '+' -> love song
	 * '-' -> ban song
	 */
}

/* Handle 'changeStation' event from client */
void BarSocketIoHandleChangeStation(BarApp_t *app, const char *stationName) {
	if (!app || !stationName) {
		return;
	}
	
	fprintf(stderr, "Socket.IO: Change station: %s\n", stationName);
	
	/* TODO: Find station by name and switch to it */
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

