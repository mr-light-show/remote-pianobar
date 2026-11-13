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

#ifndef _WEBSOCKET_H
#define _WEBSOCKET_H

#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>

#include "queue.h"
#include "outbox.h"

/* Note: main.h must be included before this header to get BarApp_t definition */

/* WebSocket connection state. The connection table, including each client's
 * pending queue, is owned by the service thread; producers never touch it. */
typedef struct {
	BarWsConnId_t id;             /* Connection ID (service thread assigns) */
	void *wsi;                    /* libwebsockets instance */
	bool authenticated;           /* Authentication status */
	bool pendingClose;            /* Close on the next writable callback */
	int closeStatus;              /* WebSocket close status for pendingClose */
	const char *closeReason;      /* Static, bounded close reason */
	char protocol[64];            /* Protocol: "socketio" or "homeassistant" */
	BarWsClientQueue_t pending;   /* Frames not yet written to this client */
} BarWsConnection_t;

/* Progress de-duplication. Producer-side only: touched solely by the
 * progress producer (playback manager thread). */
typedef struct {
	unsigned int lastBroadcast;   /* Last elapsed time queued */
	uint32_t lastGeneration;      /* Song generation lastBroadcast belonged to */
} BarWsProgress_t;

/* WebSocket server context */
typedef struct {
	void *context;                /* libwebsockets context */
	bool initialized;             /* Initialization status */
	
	/* Threading */
	pthread_t thread;             /* WebSocket service thread */
	_Atomic bool threadRunning;   /* Thread lifecycle flag */
	/* stateRwlock is in BarApp_t (web/both via BarStateUsesRwlock); player->lock for transport here */
	
	/* Producer ingress (any thread) -> service thread. Holds the song
	 * generation and the completed play-state watermark under its lock. */
	BarWsOutbox_t *outbox;
	
	BarWsProgress_t progress;
	
	/* Delayed volume broadcast (for debouncing) */
	struct {
		time_t scheduleTime;    /* When to broadcast (epoch ms) */
		bool pending;           /* Whether broadcast is pending */
	} delayedVolumeBroadcast;
	pthread_mutex_t volumeBroadcastMutex;
	
	/* System volume polling (for detecting external changes) */
	int lastPolledVolume;         /* Last known system volume (-1 = unknown) */
	time_t lastVolumePollTime;    /* Last poll timestamp (seconds) */
	
	/* Connection table: service thread only */
	BarWsConnection_t *connections;
	size_t numConnections;
	size_t maxConnections;
	BarWsConnId_t nextConnId;     /* Monotonic; last ID handed out */
	/* Socket adapters (NULL = libwebsockets); seams so tests can drive
	 * delivery with fake clients. writeFrame is the only socket write and is
	 * called only from the writable handler. */
	int (*writeFrame) (void *wsi, unsigned char *buf, size_t len);
	void (*requestWritable) (void *wsi);
} BarWsContext_t;

/* Initialize WebSocket server */
bool BarWebsocketInit(BarApp_t *app);

/* Destroy WebSocket server */
void BarWebsocketDestroy(BarApp_t *app);

/*
 * Queue a pre-formatted Socket.IO event for `dest` (a connection ID or
 * BAR_WS_DEST_BROADCAST). Thread-safe; never touches sockets. Always takes
 * ownership of `message`. Keyed events (progress, volume, stations) may be
 * replaced by a newer event with the same key that a client has not received
 * yet; BAR_WS_KEY_NONE events are delivered in order. `dest` is resolved on
 * the service thread: an ID that is unknown or closed by then (including a
 * reused slot, whose new client has a new ID) receives nothing.
 * Returns false for a NULL app/context/message, for a frame longer than
 * WS_FRAME_MAX_BYTES (logged, nobody is closed), or when the ingress is full;
 * a full ingress makes the service thread close every client so it resyncs
 * (see BarWsServiceOutbox).
 */
bool BarWebsocketSend (BarApp_t *app, BarWsConnId_t dest, BarWsKey_t key,
                       char *message);
/* Broadcast a song start, admitted atomically with a new song generation
 * (stale progress is discarded and can never be ordered ahead of it). */
bool BarWebsocketBroadcastSongStart (BarApp_t *app, char *message);
/* Song generation; progress producers read it before sampling elapsed time */
uint32_t BarWebsocketSongGeneration (BarApp_t *app);
/* Broadcast progress sampled under `observedGeneration`; dropped at delivery
 * if a newer song started meanwhile. */
bool BarWebsocketBroadcastProgress (BarApp_t *app, uint32_t observedGeneration,
                                    char *message);

/* Service thread: take everything producers queued, append it to each
 * recipient's pending queue and request writable callbacks. Applies the
 * overflow policies documented in websocket.c. */
void BarWsServiceOutbox (BarWsContext_t *ctx);
/* Service thread, LWS_CALLBACK_SERVER_WRITEABLE body: write at most one
 * pending frame to `wsi` and request another callback if more are pending.
 * Returns -1 when libwebsockets must close the connection. */
int BarWsHandleWritable (BarWsContext_t *ctx, void *wsi);

/* Service-thread connection table updates (exposed for tests).
 * Add returns the new connection's ID, or 0 if the table is full. Remove
 * frees the connection's pending frames. */
BarWsConnId_t BarWsConnectionAdd (BarWsContext_t *ctx, void *wsi,
                                  const char *protocol);
void BarWsConnectionRemove (BarWsContext_t *ctx, void *wsi);
/* Receive-callback body: dispatch a frame from `wsi` as its connection's
 * request. Frames from a connection missing from the table (e.g. it was
 * full) are dropped, never handled as requester 0. Service thread only. */
void BarWsHandleReceive (BarApp_t *app, void *wsi, const void *in, size_t len);

/* Same ownership contract; broadcasts a completed play state unless an
 * older epoch than the newest accepted one (checked under the ingress lock;
 * the watermark survives delivery and generic messages). Callers snapshot
 * stable player state, then release every application lock. Epoch is
 * internal metadata, never wire content. */
void BarWebsocketBroadcastPlayStateMessage (BarApp_t *app, char *message,
                                             uint64_t epoch);

/* Get current elapsed time */
unsigned int BarWebsocketGetElapsed(BarApp_t *app);

/* Handle incoming WebSocket message from the client `requester` */
void BarWebsocketHandleMessage (BarApp_t *app, const char *message,
                                size_t len, const char *protocol,
                                BarWsConnId_t requester);

/* Schedule delayed volume broadcast (for debouncing) */
void BarWsScheduleVolumeBroadcast(BarWsContext_t *ctx, int delayMs);
/* Execute a due debounced volume event using the current canonical value. */
void BarWsProcessVolumeBroadcast(BarWsContext_t *ctx, BarApp_t *app);

/* Close every WebSocket client (used by app.stop). Service thread only. */
void BarWebsocketDisconnectAllClients(BarApp_t *app);

#endif /* _WEBSOCKET_H */
