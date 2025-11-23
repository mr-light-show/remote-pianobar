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

/* Forward declaration for json_object (from json-c) */
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

/* Handle incoming Socket.IO message */
void BarSocketIoHandleMessage(BarApp_t *app, const char *message);

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

/* Handle 'action' event from client */
void BarSocketIoHandleAction(BarApp_t *app, const char *action, struct json_object *data);

/* Handle 'changeStation' event from client */
void BarSocketIoHandleChangeStation(BarApp_t *app, const char *stationName);

/* Handle 'query' event from client */
void BarSocketIoHandleQuery(BarApp_t *app);

#endif /* _SOCKETIO_H */

