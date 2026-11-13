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
#include "commands.h"

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
	char *rxBuf;                  /* Parts of the message being received */
	size_t rxLen;                 /* Bytes in rxBuf */
	bool rxDropping;              /* Dropping the rest of an oversize message */
	bool busyNotified;            /* Told "busy" since its last accepted request */
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
	
	/* Command worker: receive callbacks (service thread) push requests, one
	 * worker thread runs them in FIFO order. commands outlives the worker
	 * (closed by WorkerStop, freed by Destroy after the service thread). */
	BarWsCommandQueue_t *commands;
	pthread_t worker;
	bool workerStarted;           /* Lifecycle owner only */
	
	/* Delayed volume broadcast (for debouncing) */
	struct {
		time_t scheduleTime;    /* When to broadcast (epoch ms) */
		bool pending;           /* Whether broadcast is pending */
	} delayedVolumeBroadcast;
	pthread_mutex_t volumeBroadcastMutex;
	/* Service-loop timer (lws_sorted_usec_list_t) for the volume debounce
	 * and the system-volume poll; service thread only */
	void *timer;
	
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

/* Destroy WebSocket server: stops the command worker, then the service
 * thread, then frees the transport. Lifecycle owner only; every other
 * producer (playback manager, CLI) must have stopped first. */
void BarWebsocketDestroy(BarApp_t *app);

/* Command worker lifecycle. Production code only reaches these through
 * BarWebsocketInit/BarWebsocketDestroy; they are public as test seams
 * (worker tests on a fake-client context).
 *
 * Start the command worker and its queue (Init does this before the service
 * thread starts). A closed queue left by an earlier Stop is freed first (a
 * restart exists only for tests), so no service thread may be running.
 * Returns false, with nothing started or allocated, on failure. */
bool BarWebsocketWorkerStart (BarApp_t *app);
/* Stop admission (queued commands are dropped and freed), interrupt an
 * in-flight Pandora call through the interrupt target and join the worker.
 * Replies the worker has not delivered yet are abandoned. Lifecycle owner
 * only: from the worker itself (e.g. inside app.quit) it logs and returns. */
void BarWebsocketWorkerStop (BarApp_t *app);
/* Test seam: replaces pthread_create for the worker and service threads
 * (NULL restores it). */
typedef int (*BarWsThreadCreate_fn) (pthread_t *thread, void *(*fn) (void *),
                                     void *arg);
void BarWebsocketSetTestThreadCreateHook (BarWsThreadCreate_fn hook);

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
 * Add returns the new connection's ID, or BAR_WS_CONN_NONE if the table is
 * full. Remove frees the connection's pending frames and partial message. */
BarWsConnId_t BarWsConnectionAdd (BarWsContext_t *ctx, void *wsi,
                                  const char *protocol);
void BarWsConnectionRemove (BarWsContext_t *ctx, void *wsi);
/* Receive-callback body: queue a message from `wsi` for the command worker
 * as its connection's request; nothing is executed here. `final` is false
 * for every part of a message but the last; parts are collected per
 * connection. A message longer than WS_CMD_MAX_REQUEST_BYTES is dropped
 * whole, tail included (the connection stays open). A full command queue
 * answers the requester with a localized error, once until one of its
 * requests is accepted again; a closed one (shutdown) drops silently.
 * Frames from a connection missing from the table (e.g. it was full) are
 * dropped, never handled as requester 0. Service thread only. */
void BarWsHandleReceive (BarApp_t *app, void *wsi, const void *in, size_t len,
                         bool final);

/* Same ownership contract; broadcasts a completed play state unless an
 * older epoch than the newest accepted one (checked under the ingress lock;
 * the watermark survives delivery and generic messages). Callers snapshot
 * stable player state, then release every application lock. Epoch is
 * internal metadata, never wire content. */
void BarWebsocketBroadcastPlayStateMessage (BarApp_t *app, char *message,
                                             uint64_t epoch);

/* Get current elapsed time */
unsigned int BarWebsocketGetElapsed(BarApp_t *app);

/* Schedule delayed volume broadcast (for debouncing) */
void BarWsScheduleVolumeBroadcast(BarWsContext_t *ctx, int delayMs);
/* Execute a due debounced volume event using the current canonical value. */
void BarWsProcessVolumeBroadcast(BarWsContext_t *ctx, BarApp_t *app);

/* Close every WebSocket client (used by app.stop). Service thread only. */
void BarWebsocketDisconnectAllClients(BarApp_t *app);

#endif /* _WEBSOCKET_H */
