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

#include "../core/queue.h"

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

/* Handle incoming Socket.IO message from the client `requester`. The
 * transport only passes connected clients' (nonzero) IDs; replies go to that
 * client alone. BAR_WS_DEST_BROADCAST is a destination for emitters only. */
void BarSocketIoHandleMessage (BarApp_t *app, const char *message, BarWsConnId_t requester);

/* Returns a heap-allocated Socket.IO text frame for event + data.
   Caller owns the result and must free() it.
   Returns NULL on allocation failure.
   Passing data=NULL produces a no-payload frame: 2["eventName"] */
char *BarSocketIoFormatEventMessage (const char *event, struct json_object *data);

/* Emit event now to `dest` (a client, or BAR_WS_DEST_BROADCAST); unbucketed */
void BarSocketIoEmit (BarApp_t *app, BarWsConnId_t dest, const char *event,
                      struct json_object *data);

/* Payload builders — each returns a new json_object reference; caller must json_object_put(). */
struct json_object *BarSocketIoBuildStartPayload    (BarApp_t *app);
struct json_object *BarSocketIoBuildStationsPayload (BarApp_t *app);
struct json_object *BarSocketIoBuildProcessPayload  (BarApp_t *app);

/* Emit 'start' event (song started) */
void BarSocketIoEmitStart(BarApp_t *app);

/* Emit 'stop' event (song stopped) */
void BarSocketIoEmitStop(BarApp_t *app);

/* Emit 'volume' event (volume should be 0-100 percentage) */
void BarSocketIoEmitVolume(BarApp_t *app, int volume);

/* Emit 'progress' event */
void BarSocketIoEmitProgress(BarApp_t *app, unsigned int elapsed,
                              unsigned int duration);

/* Emit 'stations' event (station list) */
void BarSocketIoEmitStations(BarApp_t *app);

/* Emit 'process' event (full state) to all clients, or to `dest` only */
void BarSocketIoEmitProcess(BarApp_t *app);
void BarSocketIoEmitProcessTo (BarApp_t *app, BarWsConnId_t dest);

/* Emit 'song.explanation' event (explanation text) to `dest` */
void BarSocketIoEmitExplanationTo (BarApp_t *app, BarWsConnId_t dest, const char *explanation);

/* Emit 'error' event (error notification). stationId optional (e.g. for station.change "not found"). */
void BarSocketIoEmitError(BarApp_t *app, const char *operation, const char *message);
void BarSocketIoEmitErrorEx(BarApp_t *app, const char *operation, const char *message, const char *stationId);
/* Same as BarSocketIoEmitError, to `dest` only */
void BarSocketIoEmitErrorTo (BarApp_t *app, BarWsConnId_t dest, const char *operation, const char *message);

/* Emit 'pandora.disconnected' event (reason: "user", "idle_timeout", or "connection_lost") */
void BarSocketIoEmitPandoraDisconnected (BarApp_t *app, const char *reason);

/* Emit 'playState' event (paused/resumed state) */
void BarSocketIoEmitPlayState(BarApp_t *app);

/* Emit 'query.upcoming.result' event (upcoming songs list) to `dest` */
void BarSocketIoEmitUpcomingTo (BarApp_t *app, BarWsConnId_t dest, struct PianoSong *first, int max);

/* Handle 'action' event from client `requester` */
void BarSocketIoHandleAction (BarApp_t *app, const char *action, struct json_object *data,
                              BarWsConnId_t requester);

/* Handle 'changeStation' event from client */
void BarSocketIoHandleChangeStation(BarApp_t *app, const char *stationId);

/* Handle 'station.setQuickMix' event from client */
void BarSocketIoHandleSetQuickMix(BarApp_t *app, struct json_object *data);

/* Handle 'station.delete' event from client */
void BarSocketIoHandleDeleteStation(BarApp_t *app, struct json_object *data);

/* Handle 'station.createFrom' event from client */
void BarSocketIoHandleCreateStationFrom(BarApp_t *app, struct json_object *data);

/* Handle 'query' event: full state and stations to `requester` only */
void BarSocketIoHandleQuery (BarApp_t *app, BarWsConnId_t requester);

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

