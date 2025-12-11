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

#ifndef _SOCKETIO_H
#define _SOCKETIO_H

#include <stddef.h>
#include <stdbool.h>

/* Note: main.h must be included before this header to get BarApp_t definition */

/* Forward declarations */
struct json_object;

/* Socket.IO message types */
typedef enum {
	SOCKETIO_CONNECT = 0,
	SOCKETIO_DISCONNECT = 1,
	SOCKETIO_EVENT = 2,
	SOCKETIO_ACK = 3,
	SOCKETIO_ERROR = 4,
	SOCKETIO_BINARY_EVENT = 5,
	SOCKETIO_BINARY_ACK = 6,
} BarSocketIoType_t;

/* Set broadcast callback (called by WebSocket core) */
void BarSocketIoSetBroadcastCallback(void (*callback)(const char *, size_t));

/* Unicast support - for sending to specific client only */
void BarSocketIoSetUnicastTarget(void *wsi);
void *BarSocketIoGetUnicastTarget(void);

/* Emit 'process' event to specific client only (unicast) */
void BarSocketIoEmitProcessUnicast(BarApp_t *app, void *wsi);

/* Handle incoming Socket.IO message */
void BarSocketIoHandleMessage(BarApp_t *app, const char *message, void *wsi);

/* Emit event to all connected clients */
void BarSocketIoEmit(const char *event, struct json_object *data);

/* Emit 'start' event (song started) */
void BarSocketIoEmitStart(BarApp_t *app);

/* Emit 'stop' event (song stopped) */
void BarSocketIoEmitStop(BarApp_t *app);

/* Emit 'volume' event */
void BarSocketIoEmitVolume(BarApp_t *app, int volume);

/* Emit 'progress' event */
void BarSocketIoEmitProgress(BarApp_t *app, unsigned int elapsed,
                              unsigned int duration);

/* Emit 'stations' event (station list) */
void BarSocketIoEmitStations(BarApp_t *app);

/* Emit 'process' event (full state) */
void BarSocketIoEmitProcess(BarApp_t *app);

/* Emit 'song.explanation' event (explanation text) */
void BarSocketIoEmitExplanation(BarApp_t *app, const char *explanation);

/* Emit 'query.upcoming.result' event (upcoming songs list) */
void BarSocketIoEmitUpcoming(BarApp_t *app, struct PianoSong *firstSong, int maxSongs);

/* Handle 'action' event from client */
void BarSocketIoHandleAction(BarApp_t *app, const char *action, struct json_object *data);

/* Handle 'changeStation' event from client */
void BarSocketIoHandleChangeStation(BarApp_t *app, const char *stationId);

/* Handle 'station.setQuickMix' event from client */
void BarSocketIoHandleSetQuickMix(BarApp_t *app, struct json_object *data);

/* Handle 'station.delete' event from client */
void BarSocketIoHandleDeleteStation(BarApp_t *app, struct json_object *data);

/* Handle 'station.createFrom' event from client */
void BarSocketIoHandleCreateStationFrom(BarApp_t *app, struct json_object *data);

/* Handle 'query' event from client (unicast response to requesting client) */
void BarSocketIoHandleQuery(BarApp_t *app, void *wsi);

/* Fetch genres and emit to client */
void BarSocketIoHandleGetGenres(BarApp_t *app);
void BarSocketIoEmitGenres(BarApp_t *app);

/* Handle 'station.addGenre' with musicId */
void BarSocketIoHandleAddGenre(BarApp_t *app, struct json_object *data);

/* Handle 'station.addShared' with stationId */
void BarSocketIoHandleAddShared(BarApp_t *app, struct json_object *data);

/* Handle music search and emit results */
void BarSocketIoHandleSearchMusic(BarApp_t *app, struct json_object *data);
void BarSocketIoEmitSearchResults(BarApp_t *app, struct PianoSearchResult *searchResult);

/* Handle 'station.addMusic' with musicId and stationId */
void BarSocketIoHandleAddMusic(BarApp_t *app, struct json_object *data);

/* Handle 'station.rename' with stationId and newName */
void BarSocketIoHandleRenameStation(BarApp_t *app, struct json_object *data);

/* Handle 'station.getModes' - fetch available modes for a station */
void BarSocketIoHandleGetStationModes(BarApp_t *app, struct json_object *data);
void BarSocketIoEmitStationModes(BarApp_t *app, PianoRequestDataGetStationModes_t *reqData);

/* Handle 'station.setMode' - set station mode */
void BarSocketIoHandleSetStationMode(BarApp_t *app, struct json_object *data);

/* Handle 'station.getInfo' - fetch station info for seed/feedback management */
void BarSocketIoHandleGetStationInfo(BarApp_t *app, struct json_object *data);
void BarSocketIoEmitStationInfo(BarApp_t *app, PianoRequestDataGetStationInfo_t *reqData);

/* Handle 'station.deleteSeed' and 'station.deleteFeedback' */
void BarSocketIoHandleDeleteSeed(BarApp_t *app, struct json_object *data);
void BarSocketIoHandleDeleteFeedback(BarApp_t *app, struct json_object *data);

#endif /* _SOCKETIO_H */

