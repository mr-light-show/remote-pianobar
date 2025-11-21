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

#ifndef _WEBSOCKET_H
#define _WEBSOCKET_H

#include <stddef.h>
#include <stdbool.h>
#include <time.h>

/* Note: main.h must be included before this header to get BarApp_t definition */

/* WebSocket connection state */
typedef struct {
	void *wsi;                    /* libwebsockets instance */
	bool authenticated;           /* Authentication status */
	char protocol[64];            /* Protocol: "socketio" or "homeassistant" */
} BarWsConnection_t;

/* Song progress tracking */
typedef struct {
	time_t songStartTime;         /* When song started playing */
	unsigned int songDuration;    /* Total duration in seconds */
	bool isPlaying;               /* Is currently playing */
	unsigned int lastBroadcast;   /* Last progress broadcast time */
} BarWsProgress_t;

/* WebSocket server context */
typedef struct {
	void *context;                /* libwebsockets context */
	bool initialized;             /* Initialization status */
	BarWsProgress_t progress;     /* Progress tracking */
	BarWsConnection_t *connections; /* Array of connections */
	size_t numConnections;        /* Number of active connections */
	size_t maxConnections;        /* Maximum connections allowed */
} BarWsContext_t;

/* Initialize WebSocket server */
bool BarWebsocketInit(BarApp_t *app);

/* Destroy WebSocket server */
void BarWebsocketDestroy(BarApp_t *app);

/* Service WebSocket connections (call in main loop) */
void BarWebsocketService(BarApp_t *app, int timeout_ms);

/* Broadcast state to all connected clients */
void BarWebsocketBroadcastState(BarApp_t *app);

/* Broadcast song start event */
void BarWebsocketBroadcastSongStart(BarApp_t *app);

/* Broadcast song stop event */
void BarWebsocketBroadcastSongStop(BarApp_t *app);

/* Broadcast volume change */
void BarWebsocketBroadcastVolume(BarApp_t *app, int volume);

/* Broadcast progress update */
void BarWebsocketBroadcastProgress(BarApp_t *app);

/* Get current elapsed time */
unsigned int BarWebsocketGetElapsed(BarApp_t *app);

/* Handle incoming WebSocket message */
void BarWebsocketHandleMessage(BarApp_t *app, const char *message, 
                               size_t len, const char *protocol);

#endif /* _WEBSOCKET_H */

