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

/* Enable POSIX functions on Linux (not needed on macOS/BSD) */
#if !defined(_XOPEN_SOURCE) && !defined(__APPLE__) && !defined(__FreeBSD__)
#define _XOPEN_SOURCE 600  /* Enable POSIX functions */
#endif

#include "../../main.h"
#include "../../log.h"
#include "../../interrupt.h"
#include "../../system_volume.h"
#include "websocket.h"
#include "websocket_lws_log.h"
#include "../protocol/socketio.h"
#include "../http/http_server.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <libwebsockets.h>
#include <json-c/json.h>

/* Idle/keepalive policy: send PING after WEBSOCKET_PING_TIMEOUT_SEC idle, hang up if no response in WEBSOCKET_HANGUP_TIMEOUT_SEC */
static const lws_retry_bo_t keepalive_policy = {
	.retry_ms_table = NULL,
	.retry_ms_table_count = 0,
	.conceal_count = 0,
	.secs_since_valid_ping = WEBSOCKET_PING_TIMEOUT_SEC,
	.secs_since_valid_hangup = WEBSOCKET_HANGUP_TIMEOUT_SEC,
	.jitter_percent = 0,
};

/* Forward declarations */
static void* BarWebsocketThread(void *arg);

/*	Outbound delivery
 *
 * Producers (any thread) only push into ctx->outbox and wake the service
 * thread with lws_cancel_service(). The service thread alone owns the
 * connection table and sockets:
 *
 *  1. BarWsServiceOutbox() takes the ingress after each lws_service() round,
 *     resolves each destination against the table (unknown or closed IDs
 *     receive nothing; IDs are never reused), appends the shared refcounted
 *     frame to every recipient's pending queue and requests a writable
 *     callback for clients with pending frames.
 *  2. LWS_CALLBACK_SERVER_WRITEABLE writes at most one frame per callback
 *     (BarWsHandleWritable) and asks for another while frames remain, so a
 *     client that stops reading never delays the others.
 *
 * Keyed frames (progress, volume, stations) replace an unsent frame with the
 * same key unless an ordered frame lies between; progress of a previous song
 * is dropped when dequeued.
 */

static int BarWsLwsWriteFrame (void *wsi, unsigned char *buf, size_t len) {
	return lws_write ((struct lws *) wsi, buf, len, LWS_WRITE_TEXT);
}

static void BarWsLwsRequestWritable (void *wsi) {
	lws_callback_on_writable ((struct lws *) wsi);
}

static void BarWsRequestWritable (const BarWsContext_t *ctx, void *wsi) {
	(ctx->requestWritable ? ctx->requestWritable : BarWsLwsRequestWritable) (wsi);
}

/* Wake the service thread so it delivers newly queued frames */
static void BarWsWake (const BarWsContext_t *ctx) {
	if (ctx->context) {
		lws_cancel_service ((struct lws_context *) ctx->context);
	}
}

/* Add a client to the connection table (service thread) and assign its
 * ID. IDs come from a monotonic counter and are never reused. Returns 0
 * if the table is full. */
BarWsConnId_t BarWsConnectionAdd (BarWsContext_t *ctx, void *wsi,
                                  const char *protocol) {
	for (size_t i = 0; i < ctx->maxConnections; i++) {
		BarWsConnection_t *conn = &ctx->connections[i];
		if (conn->wsi == NULL) {
			conn->id = ++ctx->nextConnId;
			conn->wsi = wsi;
			conn->pendingClose = false;
			conn->closeReason = NULL;
			conn->rxDropping = false;
			BarWsClientQueueInit (&conn->pending);
			snprintf (conn->protocol, sizeof (conn->protocol), "%s", protocol);
			ctx->numConnections++;
			return conn->id;
		}
	}
	return BAR_WS_DEST_BROADCAST;
}

/* Tracked connection for `wsi`, or NULL */
static BarWsConnection_t *BarWsConnectionFind (const BarWsContext_t *ctx,
                                               const void *wsi) {
	for (size_t i = 0; i < ctx->maxConnections; i++) {
		if (ctx->connections[i].wsi == wsi) {
			return &ctx->connections[i];
		}
	}
	return NULL;
}

/* Close `conn` on its next writable callback with a bounded reason; its
 * pending frames are dropped (the client resyncs when it reconnects). */
static void BarWsConnectionClose (const BarWsContext_t *ctx, BarWsConnection_t *conn,
                                  int status, const char *reason) {
	if (conn->pendingClose) {
		return;
	}
	log_write (DEBUG_WEBSOCKET, "Closing client %llu: %s\n",
	           (unsigned long long) conn->id, reason);
	conn->pendingClose = true;
	conn->closeStatus = status;
	conn->closeReason = reason;
	BarWsClientQueueClear (&conn->pending);
	BarWsRequestWritable (ctx, conn->wsi);
}

/* Queue a frame received from `wsi` for the command worker (service
 * thread). Nothing runs here, so a slow command never stalls connection
 * servicing. A received message always has a nonzero requester. */
void BarWsHandleReceive (BarApp_t *app, void *wsi, const void *in, size_t len,
                         bool final) {
	if (!app || !app->wsContext) {
		return;
	}
	BarWsContext_t *ctx = app->wsContext;
	BarWsConnection_t *conn = BarWsConnectionFind (ctx, wsi);
	if (conn == NULL) {
		/* Untracked (table was full): its replies would have no recipient */
		log_write (DEBUG_WEBSOCKET, "Dropping message from untracked client (wsi=%p)\n",
		           wsi);
		return;
	}
	/* libwebsockets hands over at most LWS_RX_BUFFER_SIZE bytes per callback,
	 * so a request must arrive whole in one callback. Drop every part of a
	 * longer (or fragmented) message; the connection stays open. */
	if (conn->rxDropping || !final || len > WS_CMD_MAX_REQUEST_BYTES) {
		if (!conn->rxDropping) {
			log_write (LOG_ERROR, "Dropping request from client %llu: not one frame of at most %d bytes\n",
			           (unsigned long long) conn->id, WS_CMD_MAX_REQUEST_BYTES);
		}
		conn->rxDropping = !final;
		return;
	}
	if (!in || len == 0) {
		return;
	}
	switch (BarWsCommandQueuePush (ctx->commands, conn->id, in, len)) {
		case BAR_WS_CMD_OK:
			break;
		case BAR_WS_CMD_FULL:
			log_write (DEBUG_WEBSOCKET, "Command queue full; refusing a request from client %llu\n",
			           (unsigned long long) conn->id);
			BarSocketIoEmitErrorTo (app, conn->id, "command",
					BarL10nGet (&app->l10n, "web.socket.busy"));
			break;
		case BAR_WS_CMD_CLOSED:
			/* Shutting down (or no worker): not an overload, no reply */
			log_write (DEBUG_WEBSOCKET, "Command queue closed; dropping a request\n");
			break;
		case BAR_WS_CMD_NOMEM:
			log_write (LOG_ERROR, "Out of memory queueing a request from client %llu\n",
			           (unsigned long long) conn->id);
			break;
	}
}

void BarWsConnectionRemove (BarWsContext_t *ctx, void *wsi) {
	BarWsConnection_t *conn = BarWsConnectionFind (ctx, wsi);
	if (conn == NULL) {
		return;
	}
	log_write(DEBUG_WEBSOCKET, "Client removed (id %llu, total %zu)\n",
	           (unsigned long long) conn->id, ctx->numConnections - 1);
	BarWsClientQueueClear (&conn->pending);
	conn->id = 0;
	conn->wsi = NULL;
	conn->pendingClose = false;
	conn->closeReason = NULL;
	conn->protocol[0] = '\0';
	ctx->numConnections--;
}

/* Overflow policies. Events are never silently dropped:
 *  - A frame longer than WS_FRAME_MAX_BYTES is refused at push time and
 *    logged at LOG_ERROR; no client is closed, since no reconnect could ever
 *    deliver it.
 *  - Ingress full (a push refused for room or memory): an event is lost and its recipients
 *    are not known cheaply, so every client is closed with a bounded reason;
 *    each reconnects and resyncs from one full-state frame.
 *  - A client's pending queue full (BAR_WS_Q_OVERFLOW): that client is not
 *    reading fast enough; only it is closed, the others are unaffected. */
#define BAR_WS_CLOSE_INGRESS_FULL "Server event queue full"
#define BAR_WS_CLOSE_CLIENT_FULL "Client event queue full"

void BarWsServiceOutbox (BarWsContext_t *ctx) {
	if (!ctx || !ctx->outbox) {
		return;
	}
	bool overflowed = BarWsOutboxTakeOverflow (ctx->outbox);
	BarWsPending_t *list = BarWsOutboxTake (ctx->outbox);

	if (overflowed) {
		log_write (LOG_ERROR, "Outbound event queue overflowed; closing all clients so they resync\n");
		for (size_t i = 0; i < ctx->maxConnections; i++) {
			if (ctx->connections[i].wsi != NULL) {
				BarWsConnectionClose (ctx, &ctx->connections[i],
				                      LWS_CLOSE_STATUS_UNEXPECTED_CONDITION,
				                      BAR_WS_CLOSE_INGRESS_FULL);
			}
		}
		BarWsPendingFree (list);
		return;
	}

	for (const BarWsPending_t *p = list; p; p = p->next) {
		for (size_t i = 0; i < ctx->maxConnections; i++) {
			BarWsConnection_t *conn = &ctx->connections[i];
			if (conn->wsi == NULL || conn->pendingClose ||
					(p->dest != BAR_WS_DEST_BROADCAST && conn->id != p->dest)) {
				continue;
			}
			if (BarWsClientQueueAppend (&conn->pending, p->frame, p->key,
					p->generation) != BAR_WS_Q_OK) {
				BarWsConnectionClose (ctx, conn, LWS_CLOSE_STATUS_POLICY_VIOLATION,
				                      BAR_WS_CLOSE_CLIENT_FULL);
			}
		}
	}
	BarWsPendingFree (list);

	for (size_t i = 0; i < ctx->maxConnections; i++) {
		const BarWsConnection_t *conn = &ctx->connections[i];
		if (conn->wsi != NULL && !conn->pendingClose && conn->pending.count > 0) {
			BarWsRequestWritable (ctx, conn->wsi);
		}
	}
}

int BarWsHandleWritable (BarWsContext_t *ctx, void *wsi) {
	BarWsConnection_t *conn = BarWsConnectionFind (ctx, wsi);
	if (conn == NULL) {
		return 0;
	}
	if (conn->pendingClose) {
		return -1;
	}
	BarWsFrame_t *frame = BarWsClientQueuePop (&conn->pending,
			BarWsOutboxGeneration (ctx->outbox));
	if (frame == NULL) {
		return 0;
	}

	size_t len = BarWsFrameLen (frame);
	const char *text = (const char *) BarWsFrameData (frame);
	bool isProgress = (len > 13 && strncmp (text, "2[ \"progress\"", 13) == 0);
	log_write (isProgress ? DEBUG_WEBSOCKET_PROGRESS : DEBUG_WEBSOCKET,
	           "Sending to client %llu (%zu bytes): %.*s%s\n",
	           (unsigned long long) conn->id, len, (int) LOG_MESSAGE_TRUNCATE_LEN,
	           text, len > LOG_MESSAGE_TRUNCATE_LEN ? "..." : "");

	/* The frame keeps LWS_PRE bytes in front of its payload for lws_write */
	int written = (ctx->writeFrame ? ctx->writeFrame : BarWsLwsWriteFrame)
			(wsi, BarWsFrameData (frame), len);
	BarWsFrameUnref (frame);
	if (written < 0) {
		log_write (DEBUG_WEBSOCKET, "lws_write failed for client %llu; closing\n",
		           (unsigned long long) conn->id);
		BarWsClientQueueClear (&conn->pending);
		return -1;
	}
	/* A short count is not a partial send to retry: lws buffers the rest of
	 * the frame itself and holds back the next writable callback until it is
	 * flushed, so the frame counts as sent. */
	if (conn->pending.count > 0) {
		BarWsRequestWritable (ctx, wsi);
	}
	return 0;
}

/* WebSocket protocol callback */
static int callback_websocket(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
	BarApp_t *app = (BarApp_t *)lws_context_user(lws_get_context(wsi));
	char filepath[WEBSOCKET_FILEPATH_MAX];
	const char *webui_path;
	char *url;
	
	switch (reason) {
		case LWS_CALLBACK_HTTP:
			/* HTTP request received */
			url = (char *)in;
			
			/* Get webui path from settings, default to ./dist/webui */
			webui_path = app->settings.webuiPath;
			if (!webui_path || strlen(webui_path) == 0) {
				webui_path = "./dist/webui";
			}
			
			log_write(DEBUG_WEBSOCKET, "HTTP: Request for %s\n", url);
			
			/* Handle root path */
			if (strcmp(url, "/") == 0 || strlen(url) == 0) {
				return BarHttpServeIndex(wsi, webui_path);
			}
			
			/* Serve requested file */
			snprintf(filepath, sizeof(filepath), "%s%s", webui_path, url);
			return BarHttpServeFile(wsi, filepath);
			
	case LWS_CALLBACK_ESTABLISHED: {
		/* New client connected - track it */
		log_write(DEBUG_WEBSOCKET, "Client connected\n");
		
		if (app && app->wsContext) {
			BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
			
			BarWsConnId_t id = BarWsConnectionAdd (ctx, wsi,
					lws_get_protocol (wsi)->name);
			if (id != 0) {
				log_write(DEBUG_WEBSOCKET, "Client tracked (id %llu, total %zu)\n",
				           (unsigned long long) id, ctx->numConnections);

				/* Send current state to the new client only */
				BarSocketIoEmitProcessTo (app, id);
			}
		}
		break;
	}
		
	case LWS_CALLBACK_CLOSED: {
		/* Client disconnected - remove from tracking */
		log_write(DEBUG_WEBSOCKET, "Client disconnected\n");
		
		if (app && app->wsContext) {
			BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
			
			BarWsConnectionRemove (ctx, wsi);
		}
		break;
	}
			
		case LWS_CALLBACK_RECEIVE:
			/* Received message from client: queue it for the worker */
			BarWsHandleReceive (app, wsi, in, len, lws_is_final_fragment (wsi) != 0);
			break;
			
		case LWS_CALLBACK_SERVER_WRITEABLE:
			/* Ready to send: one pending frame, or the pending close */
			if (app && app->wsContext) {
				BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
				const BarWsConnection_t *conn = BarWsConnectionFind (ctx, wsi);
				if (conn && conn->pendingClose && conn->closeReason) {
					lws_close_reason (wsi, (enum lws_close_status) conn->closeStatus,
					                  (unsigned char *) conn->closeReason,
					                  strlen (conn->closeReason));
				}
				return BarWsHandleWritable (ctx, wsi);
			}
			break;
			
		default:
			break;
	}
	
	return 0;
}

/* WebSocket protocols */
static struct lws_protocols protocols[] = {
	{
		"socketio",
		callback_websocket,
		0,
		LWS_RX_BUFFER_SIZE,
		0, NULL, 0
	},
	{
		"homeassistant",
		callback_websocket,
		0,
		LWS_RX_BUFFER_SIZE,
		0, NULL, 0
	},
	{ NULL, NULL, 0, 0, 0, NULL, 0 } /* terminator */
};

/* Schedule/reschedule delayed volume broadcast */
void BarWsScheduleVolumeBroadcast(BarWsContext_t *ctx, int delayMs) {
	if (!ctx) {
		return;
	}
	
	pthread_mutex_lock(&ctx->volumeBroadcastMutex);
	
	/* Get current time in milliseconds */
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	time_t nowMs = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
	
	ctx->delayedVolumeBroadcast.scheduleTime = nowMs + delayMs;
	ctx->delayedVolumeBroadcast.pending = true;
	
	log_write(DEBUG_WEBSOCKET, "Scheduled volume broadcast in %dms (will use current volume at broadcast time)\n", 
	           delayMs);
	
	pthread_mutex_unlock(&ctx->volumeBroadcastMutex);
	
	/* Wake websocket thread to update sleep timeout */
	if (ctx->context) {
		lws_cancel_service((struct lws_context *)ctx->context);
	}
}

/* Check and execute pending volume broadcast if timer expired */
void BarWsProcessVolumeBroadcast(BarWsContext_t *ctx, BarApp_t *app) {
	if (!ctx || !app) {
		return;
	}
	
	pthread_mutex_lock(&ctx->volumeBroadcastMutex);
	
	if (!ctx->delayedVolumeBroadcast.pending) {
		pthread_mutex_unlock(&ctx->volumeBroadcastMutex);
		return;
	}
	
	/* Check if timer has expired */
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	time_t nowMs = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
	
	if (nowMs >= ctx->delayedVolumeBroadcast.scheduleTime) {
		ctx->delayedVolumeBroadcast.pending = false;
		pthread_mutex_unlock(&ctx->volumeBroadcastMutex);
		
		/* Read CURRENT volume as percentage - frontend always expects 0-100 */
		int volumePercent;
		if (app->settings.volumeMode == BAR_VOLUME_MODE_SYSTEM) {
			volumePercent = BarSystemVolumeGet();
			if (volumePercent < 0) volumePercent = VOLUME_FALLBACK_PERCENT;
			log_write(DEBUG_WEBSOCKET, "Executing delayed volume broadcast - %d%% (system volume)\n", 
			           volumePercent);
		} else {
			/* Player mode: volume is already 0-100 linear */
			volumePercent = BarPlayerGetVolume (&app->player);
			log_write(DEBUG_WEBSOCKET, "Executing delayed volume broadcast - %d%% (player volume)\n", 
			           volumePercent);
		}
		
		/* Broadcast to all clients */
		BarSocketIoEmitVolume(app, volumePercent);
	} else {
		pthread_mutex_unlock(&ctx->volumeBroadcastMutex);
	}
}

/* Poll system volume for external changes (keyboard keys, other apps)
 * Called from WebSocket thread, checks every 1 second */
static void BarWsPollSystemVolume(BarWsContext_t *ctx, BarApp_t *app) {
	if (!ctx || !app) {
		return;
	}
	
	/* Only poll in system volume mode */
	if (app->settings.volumeMode != BAR_VOLUME_MODE_SYSTEM) {
		return;
	}
	
	/* Check if 1 second has elapsed since last poll */
	time_t now = time(NULL);
	if (now - ctx->lastVolumePollTime < 1) {
		return;  /* Too soon, skip this poll */
	}
	ctx->lastVolumePollTime = now;
	
	/* Check if default audio device changed and refresh if needed */
	if (BarSystemVolumeRefreshDevice()) {
		log_write(DEBUG_WEBSOCKET, "Default audio output device changed\n");
	}
	
	/* Read current system volume */
	int currentVolume = BarSystemVolumeGet();
	if (currentVolume < 0) {
		return;  /* Error reading volume */
	}
	
	/* Check if volume changed from last known value */
	if (currentVolume != ctx->lastPolledVolume) {
		log_write(DEBUG_WEBSOCKET, "System volume changed externally: %d%% → %d%%\n",
		           ctx->lastPolledVolume, currentVolume);
		ctx->lastPolledVolume = currentVolume;
		
		/* Broadcast to all clients */
		BarSocketIoEmitVolume(app, currentVolume);
	}
}

/* WebSocket service thread - runs lws_service() loop */
static void* BarWebsocketThread(void *arg) {
	BarApp_t *app = (BarApp_t *)arg;
	if (!app || !app->wsContext) {
		return NULL;
	}
	
	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	
	log_write(DEBUG_WEBSOCKET, "Thread started\n");
	
	while (atomic_load_explicit (&ctx->threadRunning, memory_order_relaxed) && !atomic_load_explicit (&app->doQuit, memory_order_relaxed)) {
		/* Returns after socket activity, a producer's lws_cancel_service()
		 * wakeup, or WEBSOCKET_POLL_MS (keeps the volume timers below ticking) */
		if (ctx->context) {
			lws_service(ctx->context, WEBSOCKET_POLL_MS);
		}
		
		/* Process delayed volume broadcast (debouncing) */
		BarWsProcessVolumeBroadcast(ctx, app);
		
		/* Poll system volume for external changes (every 1s) */
		BarWsPollSystemVolume(ctx, app);
		
		/* Deliver what producers (and the callbacks above) queued */
		BarWsServiceOutbox (ctx);
		
		/* NOTE: Progress broadcasting now handled by playback_manager thread
		 * This ensures timing is independent of WebSocket servicing delays
		 */
	}
	
	log_write(DEBUG_WEBSOCKET, "Thread stopped\n");
	return NULL;
}

static BarWsThreadCreate_fn g_threadCreateHook = NULL;

void BarWebsocketSetTestThreadCreateHook (BarWsThreadCreate_fn hook) {
	g_threadCreateHook = hook;
}

static int BarWsThreadCreate (pthread_t *thread, void *(*fn) (void *), void *arg) {
	if (g_threadCreateHook != NULL) {
		return g_threadCreateHook (thread, fn, arg);
	}
	return pthread_create (thread, NULL, fn, arg);
}

/*	Command worker
 *
 * The one thread that runs application commands, in the order they were
 * received. Handlers keep their own locking (pianoHttpMutex for Pandora and
 * borrowed stations/songs, stateRwlock, player.lock; see THREAD_SAFETY.md)
 * and reply through BarWebsocketSend, which never blocks on a client. Only
 * the request bytes and the requester ID cross from the service thread.
 */
static void *BarWsWorkerThread (void *arg) {
	BarApp_t *app = arg;
	BarWsCommandQueue_t *commands = ((BarWsContext_t *) app->wsContext)->commands;
	BarWsConnId_t requester;
	char *text;

	log_write (DEBUG_WEBSOCKET, "Command worker started\n");
	while (BarWsCommandQueuePop (commands, &requester, &text, NULL)) {
		BarSocketIoHandleMessage (app, text, requester);
		free (text);
	}
	log_write (DEBUG_WEBSOCKET, "Command worker stopped\n");
	return NULL;
}

bool BarWebsocketWorkerStart (BarApp_t *app) {
	if (!app || !app->wsContext) {
		return false;
	}
	BarWsContext_t *ctx = app->wsContext;
	if (ctx->workerStarted) {
		return false;
	}
	/* Closed by an earlier Stop; no service thread pushes to it now */
	BarWsCommandQueueFree (ctx->commands);
	ctx->commands = BarWsCommandQueueNew ();
	if (!ctx->commands) {
		log_write (LOG_ERROR, "Failed to allocate the command queue\n");
		return false;
	}
	if (BarWsThreadCreate (&ctx->worker, BarWsWorkerThread, app) != 0) {
		log_write (LOG_ERROR, "Failed to create the command worker\n");
		BarWsCommandQueueFree (ctx->commands);
		ctx->commands = NULL;
		return false;
	}
	ctx->workerStarted = true;
	return true;
}

void BarWebsocketWorkerStop (BarApp_t *app) {
	if (!app || !app->wsContext) {
		return;
	}
	BarWsContext_t *ctx = app->wsContext;
	if (!ctx->workerStarted) {
		return;
	}
	if (pthread_equal (pthread_self (), ctx->worker)) {
		/* e.g. a command asking to quit: the lifecycle owner joins later */
		log_write (LOG_ERROR, "Command worker cannot stop itself\n");
		return;
	}
	/* Refuse new commands and drop the queued ones, so the worker exits
	 * after the command in flight. Interrupt that command's Pandora call
	 * the way SIGINT does: during a request the interrupt target is the
	 * request's own flag, which curl's progress callback polls. */
	BarWsCommandQueueClose (ctx->commands);
	BarInterruptIncrement ();
	log_write (DEBUG_WEBSOCKET, "Waiting for the command worker...\n");
	pthread_join (ctx->worker, NULL);
	ctx->workerStarted = false;
}

/* Free the transport; neither the worker nor the service thread may run */
static void BarWsContextFree (BarApp_t *app) {
	BarWsContext_t *ctx = app->wsContext;

	/* Destroying the lws context closes every connection (CLOSED frees its
	 * pending frames); no callback runs after this point. */
	if (ctx->context) {
		lws_context_destroy (ctx->context);
		ctx->context = NULL;
	}
	if (ctx->connections) {
		for (size_t i = 0; i < ctx->maxConnections; i++) {
			BarWsClientQueueClear (&ctx->connections[i].pending);
		}
		free (ctx->connections);
		ctx->connections = NULL;
	}
	pthread_mutex_destroy (&ctx->volumeBroadcastMutex);
	/* Replies not delivered by now are abandoned with the queues */
	BarWsCommandQueueFree (ctx->commands);
	BarWsOutboxFree (ctx->outbox);
	free (ctx);
	app->wsContext = NULL;
}

/* Initialize WebSocket server */
bool BarWebsocketInit(BarApp_t *app) {
	struct lws_context_creation_info info;
	
	if (!app) {
		return false;
	}
	
	/* Allocate WebSocket context */
	app->wsContext = calloc(1, sizeof(BarWsContext_t));
	if (!app->wsContext) {
		log_write(LOG_ERROR, "Failed to allocate context\n");
		return false;
	}
	
	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	pthread_mutex_init(&ctx->volumeBroadcastMutex, NULL);
	ctx->maxConnections = 32;
	ctx->connections = calloc(ctx->maxConnections, sizeof(BarWsConnection_t));
	ctx->outbox = BarWsOutboxNew ();
	if (!ctx->connections || !ctx->outbox) {
		log_write(LOG_ERROR, "Failed to allocate connection table or outbound queue\n");
		BarWsContextFree (app);
		return false;
	}
	
	/* Initialize libwebsockets */
	memset(&info, 0, sizeof(info));

	BarWsLwsConfigureLogging();
	
	info.port = app->settings.websocketPort;
	info.iface = app->settings.websocketHost ? app->settings.websocketHost : "0.0.0.0";
	info.protocols = protocols;
	info.user = app;
	info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
	info.retry_and_idle_policy = &keepalive_policy;
	/* Removed LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE
	 * so we can use custom CSP for Google Fonts */
	
	ctx->context = lws_create_context(&info);
	if (!ctx->context) {
		log_write(LOG_ERROR, "Failed to create context on port %d\n",
		        app->settings.websocketPort);
		BarWsContextFree (app);
		return false;
	}
	
	ctx->initialized = true;
	
	/* Initialize delayed volume broadcast */
	ctx->delayedVolumeBroadcast.pending = false;
	
	/* Initialize system volume polling state */
	ctx->lastPolledVolume = -1;  /* Unknown until first poll */
	ctx->lastVolumePollTime = 0; /* Force immediate first poll */
	
	log_write(DEBUG_WEBSOCKET, "Server started on port %d\n",
	        app->settings.websocketPort);
	
	/* The worker and its queue exist before the first receive callback */
	if (!BarWebsocketWorkerStart (app)) {
		BarWsContextFree (app);
		return false;
	}
	
	/* Start WebSocket thread */
	ctx->threadRunning = true;
	if (BarWsThreadCreate (&ctx->thread, BarWebsocketThread, app) != 0) {
		log_write(LOG_ERROR, "Failed to create thread\n");
		BarWebsocketWorkerStop (app);
		BarWsContextFree (app);
		return false;
	}
	
	log_write(DEBUG_WEBSOCKET, "Thread created successfully\n");
	
	return true;
}

/* Destroy WebSocket server */
void BarWebsocketDestroy(BarApp_t *app) {
	if (!app || !app->wsContext) {
		return;
	}
	
	log_write(DEBUG_WEBSOCKET, "Stopping server...\n");
	
	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	
	/* No command runs after this; frames received meanwhile are dropped */
	BarWebsocketWorkerStop (app);
	
	/* Signal thread to stop */
	ctx->threadRunning = false;
	
	/* Cancel any ongoing lws_service() calls - safe in multi-threaded mode */
	if (ctx->context) {
		lws_cancel_service(ctx->context);
	}
	
	/* Wait for thread to finish */
	log_write(DEBUG_WEBSOCKET, "Waiting for thread to stop...\n");
	pthread_join(ctx->thread, NULL);
	log_write(DEBUG_WEBSOCKET, "Thread stopped\n");
	
	/* Both threads are gone: free the transport */
	BarWsContextFree (app);
	
	log_write(DEBUG_WEBSOCKET, "Server stopped\n");
}

/* Get current elapsed time from player */
unsigned int BarWebsocketGetElapsed(BarApp_t *app) {
	if (!app) {
		return 0;
	}
	
	/* Use player's tracked time - same source as CLI */
	pthread_mutex_lock(&app->player.lock);
	unsigned int elapsed = app->player.songPlayed;
	pthread_mutex_unlock(&app->player.lock);
	
	return elapsed;
}

/* Outbox of a usable context; frees message and returns NULL otherwise */
static BarWsContext_t *BarWsSendContext (const BarApp_t *app, char *message) {
	if (!app || !app->wsContext || !message ||
			!((BarWsContext_t *) app->wsContext)->outbox) {
		free (message);
		return NULL;
	}
	return app->wsContext;
}

/* Common tail of every send: log a rejection and wake the service thread
 * (also on rejection, so it handles the overflow promptly) */
static bool BarWsSent (const BarWsContext_t *ctx, bool queued) {
	if (!queued) {
		log_write (DEBUG_WEBSOCKET, "Outbound event rejected (ingress full or no memory)\n");
	}
	BarWsWake (ctx);
	return queued;
}

bool BarWebsocketSend (BarApp_t *app, BarWsConnId_t dest, BarWsKey_t key,
                       char *message) {
	BarWsContext_t *ctx = BarWsSendContext (app, message);
	if (!ctx) {
		return false;
	}
	return BarWsSent (ctx, BarWsOutboxPush (ctx->outbox, dest, key, message,
			strlen (message)));
}

bool BarWebsocketBroadcastSongStart (BarApp_t *app, char *message) {
	BarWsContext_t *ctx = BarWsSendContext (app, message);
	if (!ctx) {
		return false;
	}
	return BarWsSent (ctx, BarWsOutboxPushSongStart (ctx->outbox, message,
			strlen (message)));
}

uint32_t BarWebsocketSongGeneration (BarApp_t *app) {
	if (!app || !app->wsContext) {
		return 0;
	}
	return BarWsOutboxGeneration (((BarWsContext_t *) app->wsContext)->outbox);
}

bool BarWebsocketBroadcastProgress (BarApp_t *app, uint32_t observedGeneration,
                                    char *message) {
	BarWsContext_t *ctx = BarWsSendContext (app, message);
	if (!ctx) {
		return false;
	}
	return BarWsSent (ctx, BarWsOutboxPushProgress (ctx->outbox,
			observedGeneration, message, strlen (message)));
}

void BarWebsocketBroadcastPlayStateMessage (BarApp_t *app, char *message,
                                             uint64_t epoch) {
	BarWsContext_t *ctx = BarWsSendContext (app, message);
	if (ctx) {
		/* Not queued also when an older epoch is refused, which is not an
		 * error; a real rejection raises the overflow flag the service
		 * thread acts on, so wake it either way. */
		BarWsOutboxPushPlayState (ctx->outbox, epoch, message, strlen (message));
		BarWsWake (ctx);
	}
}

/* Disconnect all WebSocket clients (used by app.stop) */
void BarWebsocketDisconnectAllClients(BarApp_t *app) {
	if (!app || !app->wsContext) {
		return;
	}
	
	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	
	log_write(DEBUG_WEBSOCKET, "Disconnecting all clients (%zu connected)\n", 
	           ctx->numConnections);
	
	/* Close each connected client on its next writable callback */
	for (size_t i = 0; i < ctx->maxConnections; i++) {
		if (ctx->connections[i].wsi != NULL) {
			BarWsConnectionClose (ctx, &ctx->connections[i],
			                      LWS_CLOSE_STATUS_GOINGAWAY, "Server stopping");
		}
	}
	
	log_write(DEBUG_WEBSOCKET, "All clients marked for disconnect\n");
}
