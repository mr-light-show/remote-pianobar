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
#include "../../ui.h"
#include "../../ui_dispatch.h"
#include "socketio.h"
#include "../core/websocket.h"

/* Forward declare ui_act function to avoid full header include */
extern void BarUiSwitchStation(BarApp_t *app, PianoStation_t *station);

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
	
	/* Music Search */
	{"music.search", "C"},
	
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
	} else if (strcmp(eventName, "station.setQuickMix") == 0) {
		/* Set QuickMix stations from array of station IDs */
		BarSocketIoHandleSetQuickMix(app, data);
	} else if (strcmp(eventName, "station.delete") == 0) {
		/* Delete a station */
		BarSocketIoHandleDeleteStation(app, data);
	} else if (strcmp(eventName, "station.createFrom") == 0) {
		/* Create station from current song or artist */
		BarSocketIoHandleCreateStationFrom(app, data);
	} else if (strcmp(eventName, "station.getGenres") == 0) {
		/* Get genre categories and stations */
		BarSocketIoHandleGetGenres(app);
	} else if (strcmp(eventName, "station.addGenre") == 0) {
		/* Add genre station */
		BarSocketIoHandleAddGenre(app, data);
	} else if (strcmp(eventName, "music.search") == 0) {
		/* Search for music (artists/songs) */
		BarSocketIoHandleSearchMusic(app, data);
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
	json_object_object_add(data, "trackToken", 
	                       json_object_new_string(song->trackToken ? song->trackToken : ""));
	
	if (app->curStation) {
		json_object_object_add(data, "station", 
		                       json_object_new_string(app->curStation->name));
		json_object_object_add(data, "stationId",
		                       json_object_new_string(app->curStation->id));
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
	PianoStation_t **sortedStations;
	size_t stationCount;
	
	if (!app) {
		return;
	}
	
	/* Sort stations using configured sort order */
	sortedStations = BarSortedStations(app->ph.stations, &stationCount,
	                                   app->settings.sortOrder);
	if (!sortedStations) {
		return;
	}
	
	stations = json_object_new_array();
	
	/* Emit stations in sorted order */
	for (size_t i = 0; i < stationCount; i++) {
		PianoStation_t *curStation = sortedStations[i];
		
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
	}
	
	free(sortedStations);
	
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
		json_object_object_add(data, "stationId",
		                       json_object_new_string(app->curStation->id));
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
		json_object_object_add(song, "trackToken", 
		                       json_object_new_string(app->playlist->trackToken ? app->playlist->trackToken : ""));
		
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

/* Emit 'song.explanation' event (explanation text) */
void BarSocketIoEmitExplanation(BarApp_t *app, const char *explanation) {
	json_object *data;
	
	if (!app || !explanation) {
		return;
	}
	
	data = json_object_new_object();
	json_object_object_add(data, "explanation", 
	                       json_object_new_string(explanation));
	
	BarSocketIoEmit("song.explanation", data);
	json_object_put(data);
}

/* Emit 'query.upcoming.result' event (upcoming songs list) */
void BarSocketIoEmitUpcoming(BarApp_t *app, PianoSong_t *firstSong, int maxSongs) {
	json_object *songs, *songObj;
	PianoSong_t *song;
	PianoStation_t *songStation;
	int count = 0;
	
	if (!app) {
		return;
	}
	
	songs = json_object_new_array();
	
	/* Iterate through upcoming songs (limited to maxSongs) */
	song = firstSong;
	while (song && count < maxSongs) {
		songObj = json_object_new_object();
		
		json_object_object_add(songObj, "title", 
		                       json_object_new_string(song->title ? song->title : ""));
		json_object_object_add(songObj, "artist", 
		                       json_object_new_string(song->artist ? song->artist : ""));
		json_object_object_add(songObj, "album", 
		                       json_object_new_string(song->album ? song->album : ""));
		json_object_object_add(songObj, "coverArt", 
		                       json_object_new_string(song->coverArt ? song->coverArt : ""));
		json_object_object_add(songObj, "duration", 
		                       json_object_new_int(song->length));
		json_object_object_add(songObj, "rating", 
		                       json_object_new_int(song->rating));
		
		/* Add station name the song came from */
		if (song->stationId) {
			songStation = PianoFindStationById(app->ph.stations, song->stationId);
			if (songStation) {
				json_object_object_add(songObj, "stationName",
				                       json_object_new_string(songStation->name));
			}
		}
		
		json_object_array_add(songs, songObj);
		
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
		
		json_object_object_add(categoryObj, "name",
		                       json_object_new_string(category->name ? category->name : ""));
		
		/* Create genres array for this category */
		genresArray = json_object_new_array();
		genre = category->genres;
		
		while (genre != NULL) {
			genreObj = json_object_new_object();
			
			json_object_object_add(genreObj, "name",
			                       json_object_new_string(genre->name ? genre->name : ""));
			json_object_object_add(genreObj, "musicId",
			                       json_object_new_string(genre->musicId ? genre->musicId : ""));
			
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
	
	debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Get genres request\n");
	
	/* Fetch genre stations if not already cached */
	if (app->ph.genreStations == NULL) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Fetching genre stations from API\n");
		if (!BarUiPianoCall(app, PIANO_REQUEST_GET_GENRE_STATIONS, NULL, &pRet, &wRet)) {
			debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Failed to fetch genre stations\n");
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
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: addGenre - invalid parameters\n");
		return;
	}
	
	/* Extract musicId from data */
	if (!json_object_object_get_ex(data, "musicId", &musicIdObj)) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: addGenre - missing musicId\n");
		return;
	}
	
	musicId = json_object_get_string(musicIdObj);
	
	if (!musicId) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: addGenre - invalid musicId\n");
		return;
	}
	
	debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Creating genre station with musicId: %s\n", musicId);
	
	/* Set up request data */
	reqData.token = (char *)musicId;
	reqData.type = PIANO_MUSICTYPE_INVALID;
	
	/* Create the station */
	if (BarUiPianoCall(app, PIANO_REQUEST_CREATE_STATION, &reqData, &pRet, &wRet)) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Genre station created successfully\n");
		
		/* Emit updated station list to all clients */
		BarSocketIoEmitStations(app);
	} else {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Failed to create genre station\n");
	}
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
		json_object_object_add(categoryObj, "name", json_object_new_string("Artists"));
		
		resultsArray = json_object_new_array();
		artist = searchResult->artists;
		
		while (artist != NULL) {
			resultObj = json_object_new_object();
			
			json_object_object_add(resultObj, "name",
			                       json_object_new_string(artist->name ? artist->name : ""));
			json_object_object_add(resultObj, "musicId",
			                       json_object_new_string(artist->musicId ? artist->musicId : ""));
			
			json_object_array_add(resultsArray, resultObj);
			
			artist = (PianoArtist_t *)artist->head.next;
		}
		
		json_object_object_add(categoryObj, "results", resultsArray);
		json_object_array_add(categories, categoryObj);
	}
	
	/* Create Songs category */
	if (searchResult->songs != NULL) {
		categoryObj = json_object_new_object();
		json_object_object_add(categoryObj, "name", json_object_new_string("Songs"));
		
		resultsArray = json_object_new_array();
		song = searchResult->songs;
		
		while (song != NULL) {
			resultObj = json_object_new_object();
			
			json_object_object_add(resultObj, "title",
			                       json_object_new_string(song->title ? song->title : ""));
			json_object_object_add(resultObj, "artist",
			                       json_object_new_string(song->artist ? song->artist : ""));
			json_object_object_add(resultObj, "musicId",
			                       json_object_new_string(song->musicId ? song->musicId : ""));
			
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
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: music.search - invalid parameters\n");
		return;
	}
	
	/* Extract query from data */
	if (!json_object_object_get_ex(data, "query", &queryObj)) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: music.search - missing query\n");
		return;
	}
	
	query = json_object_get_string(queryObj);
	
	if (!query) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: music.search - invalid query\n");
		return;
	}
	
	debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Searching for: %s\n", query);
	
	/* Set up request data */
	reqData.searchStr = (char *)query;
	memset(&reqData.searchResult, 0, sizeof(reqData.searchResult));
	
	/* Perform the search */
	if (BarUiPianoCall(app, PIANO_REQUEST_SEARCH, &reqData, &pRet, &wRet)) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Search completed successfully\n");
		
		/* Emit search results to client */
		BarSocketIoEmitSearchResults(app, &reqData.searchResult);
		
		/* Clean up search results */
		PianoDestroySearchResult(&reqData.searchResult);
	} else {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Search failed\n");
	}
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
		/* Switch to the new station and drain current playlist */
		BarUiSwitchStation(app, station);
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Station switch initiated\n");
	} else {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Station not found: %s\n", stationName);
	}
}

/* Handle 'station.setQuickMix' event from client */
void BarSocketIoHandleSetQuickMix(BarApp_t *app, json_object *data) {
	PianoStation_t *station;
	json_object *stationIdsArray;
	int arrayLen, i;
	
	if (!app || !data) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: setQuickMix - invalid parameters\n");
		return;
	}
	
	/* Expect data to be an array of station IDs */
	if (!json_object_is_type(data, json_type_array)) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: setQuickMix - data is not an array\n");
		return;
	}
	
	debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Setting QuickMix stations...\n");
	
	/* First, set all stations to NOT be in QuickMix */
	station = app->ph.stations;
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
			station = app->ph.stations;
			PianoListForeachP (station) {
				if (strcmp(station->id, stationId) == 0) {
					station->useQuickMix = true;
					debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Added '%s' to QuickMix\n", 
					           station->name);
					break;
				}
			}
		}
	}
	
	/* Call Pandora API to save QuickMix settings */
	PianoReturn_t pRet;
	CURLcode wRet;
	if (BarUiPianoCall(app, PIANO_REQUEST_SET_QUICKMIX, NULL, &pRet, &wRet)) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: QuickMix settings saved successfully\n");
		
		/* Emit updated station list to all clients */
		BarSocketIoEmitStations(app);
	} else {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Failed to save QuickMix settings\n");
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
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: deleteStation - invalid parameters\n");
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
			debugPrint(DEBUG_WEBSOCKET, "Socket.IO: deleteStation - missing stationId\n");
			return;
		}
	} else {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: deleteStation - invalid data type\n");
		return;
	}
	
	if (!stationId) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: deleteStation - null stationId\n");
		return;
	}
	
	debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Delete station request: %s\n", stationId);
	
	/* Find station by ID or name */
	station = BarSocketIoFindStation(app, stationId);
	
	if (!station) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Station not found: %s\n", stationId);
		return;
	}
	
	debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Deleting station: %s\n", station->name);
	
	/* Check if this is the currently playing station */
	wasCurrentStation = (station == app->curStation);
	
	/* Delete the station */
	if (BarUiPianoCall(app, PIANO_REQUEST_DELETE_STATION, station, &pRet, &wRet)) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Station deleted successfully\n");
		
		/* If we deleted the current station, switch to QuickMix */
		if (wasCurrentStation) {
			PianoStation_t *quickMixStation = NULL;
			PianoStation_t *curStation = app->ph.stations;
			
			/* Find QuickMix station */
			while (curStation != NULL) {
				if (curStation->isQuickMix) {
					quickMixStation = curStation;
					break;
				}
				curStation = (PianoStation_t *)curStation->head.next;
			}
			
			if (quickMixStation) {
				debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Switching to QuickMix after deletion\n");
				BarUiSwitchStation(app, quickMixStation);
			}
			
			/* Clear curStation as the struct was destroyed */
			app->curStation = NULL;
		}
		
		/* Emit updated station list to all clients */
		BarSocketIoEmitStations(app);
	} else {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Failed to delete station\n");
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
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: createFrom - invalid parameters\n");
		return;
	}
	
	/* Extract trackToken and type from data */
	if (!json_object_object_get_ex(data, "trackToken", &trackTokenObj) ||
	    !json_object_object_get_ex(data, "type", &typeObj)) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: createFrom - missing trackToken or type\n");
		return;
	}
	
	trackToken = json_object_get_string(trackTokenObj);
	type = json_object_get_string(typeObj);
	
	if (!trackToken || !type) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: createFrom - invalid trackToken or type\n");
		return;
	}
	
	debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Creating station from %s...\n", type);
	
	/* Set up request data */
	reqData.token = (char *)trackToken;
	reqData.type = PIANO_MUSICTYPE_INVALID;
	
	if (strcmp(type, "song") == 0) {
		reqData.type = PIANO_MUSICTYPE_SONG;
	} else if (strcmp(type, "artist") == 0) {
		reqData.type = PIANO_MUSICTYPE_ARTIST;
	} else {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: createFrom - invalid type: %s\n", type);
		return;
	}
	
	/* Create the station */
	if (BarUiPianoCall(app, PIANO_REQUEST_CREATE_STATION, &reqData, &pRet, &wRet)) {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Station created successfully\n");
		
		/* Emit updated station list to all clients */
		BarSocketIoEmitStations(app);
	} else {
		debugPrint(DEBUG_WEBSOCKET, "Socket.IO: Failed to create station\n");
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

