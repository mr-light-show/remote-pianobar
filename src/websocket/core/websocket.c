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
#include "../../debug.h"
#include "websocket.h"
#include "../protocol/socketio.h"
#include "../http/http_server.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <libwebsockets.h>
#include <json-c/json.h>

/* Global context for broadcast callback */
static BarWsContext_t *g_wsContext = NULL;

/* Forward declarations */
static void BarWebsocketBroadcast(const char *message, size_t len);
static void BarWebsocketProcessBroadcast(BarWsContext_t *ctx, BarWsMessage_t *msg);
static void* BarWebsocketThread(void *arg);

/* Bucket helper functions */

/* Map message type to bucket */
static BarWsBucketType_t BarWsGetBucket(BarWsMsgType_t type) {
	switch (type) {
		case MSG_TYPE_BROADCAST_START:
		case MSG_TYPE_BROADCAST_STOP:
			return BUCKET_STATE;
		case MSG_TYPE_BROADCAST_PROGRESS:
			return BUCKET_PROGRESS;
		case MSG_TYPE_BROADCAST_VOLUME:
			return BUCKET_VOLUME;
		case MSG_TYPE_BROADCAST_STATIONS:
			return BUCKET_STATIONS;
		default:
			debugPrint(DEBUG_WEBSOCKET, "Unknown message type: %d\n", type);
			return BUCKET_STATE; // Fallback
	}
}

/* Initialize buckets */
static void BarWsBucketsInit(BarWsContext_t *ctx) {
	if (!ctx) return;
	
	for (int i = 0; i < BUCKET_COUNT; i++) {
		ctx->buckets[i].message = NULL;
		pthread_mutex_init(&ctx->buckets[i].mutex, NULL);
	}
}

/* Destroy buckets */
static void BarWsBucketsDestroy(BarWsContext_t *ctx) {
	if (!ctx) return;
	
	for (int i = 0; i < BUCKET_COUNT; i++) {
		pthread_mutex_lock(&ctx->buckets[i].mutex);
		if (ctx->buckets[i].message) {
			BarWsMessageFree(ctx->buckets[i].message);
			ctx->buckets[i].message = NULL;
		}
		pthread_mutex_unlock(&ctx->buckets[i].mutex);
		pthread_mutex_destroy(&ctx->buckets[i].mutex);
	}
}

/* Put message in bucket (replaces BarWsQueuePush for broadcasts) */
static void BarWsBucketPut(BarWsContext_t *ctx, BarWsMsgType_t type,
                           const void *data, size_t dataLen) {
	if (!ctx) return;
	
	BarWsBucketType_t bucket = BarWsGetBucket(type);
	
	/* STATE bucket clears PROGRESS bucket */
	if (bucket == BUCKET_STATE) {
		pthread_mutex_lock(&ctx->buckets[BUCKET_PROGRESS].mutex);
		if (ctx->buckets[BUCKET_PROGRESS].message) {
			BarWsMessageFree(ctx->buckets[BUCKET_PROGRESS].message);
			ctx->buckets[BUCKET_PROGRESS].message = NULL;
		}
		pthread_mutex_unlock(&ctx->buckets[BUCKET_PROGRESS].mutex);
	}
	
	/* Create new message */
	BarWsMessage_t *msg = calloc(1, sizeof(BarWsMessage_t));
	if (!msg) return;
	
	msg->type = type;
	msg->next = NULL;
	
	/* Copy data if provided */
	if (data && dataLen > 0) {
		msg->data = malloc(dataLen);
		if (msg->data) {
			memcpy(msg->data, data, dataLen);
			msg->dataLen = dataLen;
		} else {
			free(msg);
			return;
		}
	}
	
	/* Replace bucket contents */
	pthread_mutex_lock(&ctx->buckets[bucket].mutex);
	if (ctx->buckets[bucket].message) {
		BarWsMessageFree(ctx->buckets[bucket].message);
	}
	ctx->buckets[bucket].message = msg;
	pthread_mutex_unlock(&ctx->buckets[bucket].mutex);
	
	/* Wake WebSocket thread immediately */
	if (ctx->context) {
		lws_cancel_service((struct lws_context *)ctx->context);
	}
}

/* Get and remove message from bucket (returns NULL if empty) */
static BarWsMessage_t* BarWsBucketTake(BarWsContext_t *ctx, BarWsBucketType_t bucket) {
	if (!ctx || bucket >= BUCKET_COUNT) return NULL;
	
	pthread_mutex_lock(&ctx->buckets[bucket].mutex);
	BarWsMessage_t *msg = ctx->buckets[bucket].message;
	ctx->buckets[bucket].message = NULL;
	pthread_mutex_unlock(&ctx->buckets[bucket].mutex);
	
	return msg;
}

/* WebSocket protocol callback */
static int callback_websocket(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
	BarApp_t *app = (BarApp_t *)lws_context_user(lws_get_context(wsi));
	char filepath[512];
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
			
			debugPrint(DEBUG_WEBSOCKET, "HTTP: Request for %s\n", url);
			
			/* Handle root path */
			if (strcmp(url, "/") == 0 || strlen(url) == 0) {
				return BarHttpServeIndex(wsi, webui_path);
			}
			
			/* Serve requested file */
			snprintf(filepath, sizeof(filepath), "%s%s", webui_path, url);
			return BarHttpServeFile(wsi, filepath);
			
	case LWS_CALLBACK_ESTABLISHED: {
		/* New client connected - track it */
		debugPrint(DEBUG_WEBSOCKET, "WebSocket: Client connected\n");
		
		if (app && app->wsContext) {
			BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
			
			/* Find empty slot in connections array */
			for (size_t i = 0; i < ctx->maxConnections; i++) {
				if (ctx->connections[i].wsi == NULL) {
					ctx->connections[i].wsi = wsi;
					strncpy(ctx->connections[i].protocol, 
					        lws_get_protocol(wsi)->name, 
					        sizeof(ctx->connections[i].protocol) - 1);
					ctx->numConnections++;
					
					debugPrint(DEBUG_WEBSOCKET, "WebSocket: Client tracked (slot %zu, total %zu)\n", 
					           i, ctx->numConnections);
					
					/* Send current state to new client */
					BarSocketIoEmitProcess(app);
					break;
				}
			}
		}
		break;
	}
		
	case LWS_CALLBACK_CLOSED: {
		/* Client disconnected - remove from tracking */
		debugPrint(DEBUG_WEBSOCKET, "WebSocket: Client disconnected\n");
		
		if (app && app->wsContext) {
			BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
			
			for (size_t i = 0; i < ctx->maxConnections; i++) {
				if (ctx->connections[i].wsi == wsi) {
					ctx->connections[i].wsi = NULL;
					ctx->connections[i].protocol[0] = '\0';
					ctx->numConnections--;
					
					debugPrint(DEBUG_WEBSOCKET, "WebSocket: Client removed (slot %zu, total %zu)\n", 
					           i, ctx->numConnections);
					break;
				}
			}
		}
		break;
	}
			
		case LWS_CALLBACK_RECEIVE:
			/* Received message from client */
			if (app && in && len > 0) {
				char *message = malloc(len + 1);
				if (message) {
					memcpy(message, in, len);
					message[len] = '\0';
					BarWebsocketHandleMessage(app, message, len, "socketio");
					free(message);
				}
			}
			break;
			
		case LWS_CALLBACK_SERVER_WRITEABLE:
			/* Ready to send data to client */
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
		4096,
		0, NULL, 0
	},
	{
		"homeassistant",
		callback_websocket,
		0,
		4096,
		0, NULL, 0
	},
	{ NULL, NULL, 0, 0, 0, NULL, 0 } /* terminator */
};

/* Process broadcast message from main thread (runs in WS thread) */
static void BarWebsocketProcessBroadcast(BarWsContext_t *ctx, BarWsMessage_t *msg) {
	if (!ctx || !msg) {
		return;
	}
	
	/* Note: We can't access BarApp_t here safely, so we'll need to redesign
	 * broadcast messages to be self-contained or use a different approach */
	
	switch (msg->type) {
		case MSG_TYPE_BROADCAST_START:
			/* Song started */
			ctx->progress.isPlaying = true;
			/* TODO: Extract song data from msg->data and emit */
			break;
			
		case MSG_TYPE_BROADCAST_STOP:
			/* Song stopped */
			ctx->progress.isPlaying = false;
			/* TODO: Emit stop event */
			break;
			
	case MSG_TYPE_BROADCAST_PROGRESS: {
		/* Progress update - data contains elapsed time (unsigned int) */
		if (msg->data && msg->dataLen >= sizeof(unsigned int) * 2) {
		/* ARM64 FIX: Use memcpy to avoid unaligned access */
		unsigned int times[2];
		memcpy(times, msg->data, sizeof(times));
		unsigned int elapsed = times[0];
		unsigned int duration = times[1];
		
		debugPrint(DEBUG_WEBSOCKET_PROGRESS, "WebSocket: Progress broadcast - elapsed=%u, duration=%u\n", 
		           elapsed, duration);
			
			/* We can't access BarApp_t here, but we can call SocketIO directly
			 * since it uses the global broadcast callback */
			json_object *data = json_object_new_object();
			json_object_object_add(data, "elapsed", json_object_new_int(elapsed));
			json_object_object_add(data, "duration", json_object_new_int(duration));
			
			float percentage = 0.0;
			if (duration > 0) {
				percentage = (elapsed * 100.0) / duration;
			}
			json_object_object_add(data, "percentage", json_object_new_double(percentage));
			
			BarSocketIoEmit("progress", data);
			json_object_put(data);
		}
		break;
	}
			
		case MSG_TYPE_BROADCAST_VOLUME: {
			/* Volume changed - data contains volume (int) */
			if (msg->data && msg->dataLen >= sizeof(int)) {
				/* ARM64 FIX: Use memcpy to avoid unaligned access */
				int volume;
				memcpy(&volume, msg->data, sizeof(volume));
				debugPrint(DEBUG_WEBSOCKET, "WebSocket: Volume broadcast - volume=%d\n", volume);
				/* TODO: Emit volume event */
			}
			break;
		}
			
		case MSG_TYPE_BROADCAST_STATIONS:
			/* Station list changed */
			/* TODO: Emit stations event */
			break;
			
		default:
			break;
	}
}

/* WebSocket service thread - runs lws_service() loop */
static void* BarWebsocketThread(void *arg) {
	BarApp_t *app = (BarApp_t *)arg;
	if (!app || !app->wsContext) {
		return NULL;
	}
	
	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	
	debugPrint(DEBUG_WEBSOCKET, "WebSocket: Thread started\n");
	
	while (ctx->threadRunning) {
		/* Service WebSocket */
		if (ctx->context) {
			lws_service(ctx->context, 50);
		}
		
		/* Process all buckets in priority order */
		for (int i = 0; i < BUCKET_COUNT; i++) {
			BarWsMessage_t *msg = BarWsBucketTake(ctx, i);
			if (msg) {
				BarWebsocketProcessBroadcast(ctx, msg);
				BarWsMessageFree(msg);
			}
		}
		
		/* NOTE: Command queue processing removed - main thread handles this */
	}
	
	debugPrint(DEBUG_WEBSOCKET, "WebSocket: Thread stopped\n");
	return NULL;
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
		fprintf(stderr, "WebSocket: Failed to allocate context\n");
		return false;
	}
	
	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	
	/* Set global context for broadcast callback */
	g_wsContext = ctx;
	
	/* Initialize libwebsockets */
	memset(&info, 0, sizeof(info));
	
#ifndef HAVE_DEBUGLOG
	/* Suppress libwebsockets startup messages - only show errors and warnings */
	lws_set_log_level(LLL_ERR | LLL_WARN, NULL);
#endif
	
	info.port = app->settings.websocketPort;
	info.iface = app->settings.websocketHost ? app->settings.websocketHost : "0.0.0.0";
	info.protocols = protocols;
	info.user = app;
	info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
	/* Removed LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE
	 * so we can use custom CSP for Google Fonts */
	
	ctx->context = lws_create_context(&info);
	if (!ctx->context) {
		fprintf(stderr, "WebSocket: Failed to create context on port %d\n",
		        app->settings.websocketPort);
		free(ctx);
		app->wsContext = NULL;
		return false;
	}
	
	ctx->initialized = true;
	ctx->maxConnections = 32;
	ctx->connections = calloc(ctx->maxConnections, sizeof(BarWsConnection_t));
	
	/* Initialize buckets */
	BarWsBucketsInit(ctx);
	
	/* Initialize mutex */
	pthread_mutex_init(&ctx->stateMutex, NULL);
	
	/* Set up Socket.IO broadcast callback */
	BarSocketIoSetBroadcastCallback(BarWebsocketBroadcast);
	
	fprintf(stderr, "WebSocket: Server started on port %d\n",
	        app->settings.websocketPort);
	
	/* Start WebSocket thread */
	ctx->threadRunning = true;
	if (pthread_create(&ctx->thread, NULL, BarWebsocketThread, app) != 0) {
		fprintf(stderr, "WebSocket: Failed to create thread\n");
		
		/* Cleanup on failure */
		BarWsBucketsDestroy(ctx);
		pthread_mutex_destroy(&ctx->stateMutex);
		lws_context_destroy(ctx->context);
		free(ctx->connections);
		free(ctx);
		app->wsContext = NULL;
		return false;
	}
	
	fprintf(stderr, "WebSocket: Thread created successfully\n");
	
	return true;
}

/* Destroy WebSocket server */
void BarWebsocketDestroy(BarApp_t *app) {
	if (!app || !app->wsContext) {
		return;
	}
	
	fprintf(stderr, "WebSocket: Stopping server...\n");
	
	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	
	/* Signal thread to stop */
	ctx->threadRunning = false;
	
	/* Cancel any ongoing lws_service() calls - safe in multi-threaded mode */
	if (ctx->context) {
		lws_cancel_service(ctx->context);
	}
	
	/* Wait for thread to finish */
	fprintf(stderr, "WebSocket: Waiting for thread to stop...\n");
	pthread_join(ctx->thread, NULL);
	fprintf(stderr, "WebSocket: Thread stopped\n");
	
	/* Now safe to cleanup (thread is dead) */
	if (ctx->context) {
		lws_context_destroy(ctx->context);
		ctx->context = NULL;
	}
	
	/* Cleanup buckets */
	BarWsBucketsDestroy(ctx);
	
	pthread_mutex_destroy(&ctx->stateMutex);
	
	if (ctx->connections) {
		free(ctx->connections);
		ctx->connections = NULL;
	}
	
	/* Clear global context */
	g_wsContext = NULL;
	
	free(ctx);
	app->wsContext = NULL;
	
	fprintf(stderr, "WebSocket: Server stopped\n");
}

/* Service WebSocket connections */
void BarWebsocketService(BarApp_t *app, int timeout_ms) {
	if (!app || !app->wsContext) {
		return;
	}
	
	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	
	if (ctx->context) {
		lws_service(ctx->context, timeout_ms);
	}
}

/* Get current elapsed time */
unsigned int BarWebsocketGetElapsed(BarApp_t *app) {
	if (!app || !app->wsContext) {
		return 0;
	}
	
	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	
	if (!ctx->progress.isPlaying) {
		return 0;
	}
	
	time_t now = time(NULL);
	unsigned int elapsed = (unsigned int)(now - ctx->progress.songStartTime);
	
	/* Cap at duration */
	if (elapsed > ctx->progress.songDuration) {
		elapsed = ctx->progress.songDuration;
	}
	
	return elapsed;
}

/* Broadcast state to all connected clients */
void BarWebsocketBroadcastState(BarApp_t *app) {
	if (!app || !app->wsContext) {
		return;
	}
	
	/* TODO: Implement state broadcasting */
	/* This will be implemented in socketio.c and ha_bridge.c */
}

/* Broadcast song start event */
void BarWebsocketBroadcastSongStart(BarApp_t *app) {
	if (!app || !app->wsContext) {
		return;
	}
	
	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	
	/* Update progress tracking */
	pthread_mutex_lock(&ctx->stateMutex);
	ctx->progress.songStartTime = time(NULL);
	ctx->progress.isPlaying = true;
	ctx->progress.isPaused = false;
	ctx->progress.pausedAt = 0;
	ctx->progress.pausedElapsed = 0;
	ctx->progress.lastBroadcast = 0;
	
	/* Get song duration from player */
	if (app->player.songDuration > 0) {
		ctx->progress.songDuration = app->player.songDuration;
	}
	pthread_mutex_unlock(&ctx->stateMutex);
	
	/* Queue start event to bucket for WebSocket thread */
	BarWsBucketPut(ctx, MSG_TYPE_BROADCAST_START, NULL, 0);
	
	/* TEMPORARY: Also call Socket.IO directly until thread processing is complete */
	BarSocketIoEmitStart(app);
}

/* Broadcast song stop event */
void BarWebsocketBroadcastSongStop(BarApp_t *app) {
	if (!app || !app->wsContext) {
		return;
	}
	
	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	
	pthread_mutex_lock(&ctx->stateMutex);
	ctx->progress.isPlaying = false;
	pthread_mutex_unlock(&ctx->stateMutex);
	
	/* Queue stop event to bucket for WebSocket thread */
	BarWsBucketPut(ctx, MSG_TYPE_BROADCAST_STOP, NULL, 0);
	
	/* TEMPORARY: Also call Socket.IO directly until thread processing is complete */
	BarSocketIoEmitStop(app);
}

/* Broadcast volume change */
void BarWebsocketBroadcastVolume(BarApp_t *app, int volume) {
	if (!app || !app->wsContext) {
		return;
	}
	
	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	
	/* Queue volume event to bucket for WebSocket thread */
	BarWsBucketPut(ctx, MSG_TYPE_BROADCAST_VOLUME, &volume, sizeof(volume));
	
	/* TEMPORARY: Also call Socket.IO directly until thread processing is complete */
	BarSocketIoEmitVolume(app, volume);
}

/* Broadcast progress update */
void BarWebsocketBroadcastProgress(BarApp_t *app) {
	if (!app || !app->wsContext) {
		return;
	}
	
	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	player_t *player = &app->player;
	
	/* Lock to safely access progress state */
	pthread_mutex_lock(&ctx->stateMutex);
	
	if (!ctx->progress.isPlaying) {
		pthread_mutex_unlock(&ctx->stateMutex);
		return;
	}
	
	unsigned int elapsed;
	
	/* Check if player is paused */
	pthread_mutex_lock(&player->lock);
	bool paused = player->doPause;
	pthread_mutex_unlock(&player->lock);
	
	if (paused) {
		/* Paused - use frozen elapsed time */
		if (!ctx->progress.isPaused) {
			/* Just paused - save current elapsed */
			ctx->progress.isPaused = true;
			ctx->progress.pausedAt = time(NULL);
			elapsed = (unsigned int)(ctx->progress.pausedAt - ctx->progress.songStartTime);
			ctx->progress.pausedElapsed = elapsed;
		} else {
			/* Still paused - use saved elapsed */
			elapsed = ctx->progress.pausedElapsed;
		}
	} else {
		/* Playing - calculate elapsed */
		if (ctx->progress.isPaused) {
			/* Just resumed - adjust start time to account for pause duration */
			ctx->progress.isPaused = false;
			time_t pauseDuration = time(NULL) - ctx->progress.pausedAt;
			ctx->progress.songStartTime += pauseDuration;
		}
		
		time_t now = time(NULL);
		elapsed = (unsigned int)(now - ctx->progress.songStartTime);
	}
	
	/* Cap at duration */
	if (elapsed > ctx->progress.songDuration) {
		elapsed = ctx->progress.songDuration;
	}
	
	/* Only broadcast if changed */
	if (elapsed == ctx->progress.lastBroadcast) {
		pthread_mutex_unlock(&ctx->stateMutex);
		return;
	}
	
	ctx->progress.lastBroadcast = elapsed;
	unsigned int duration = ctx->progress.songDuration;
	
	pthread_mutex_unlock(&ctx->stateMutex);
	
	/* Queue progress update to bucket for WebSocket thread */
	unsigned int times[2] = {elapsed, duration};
	BarWsBucketPut(ctx, MSG_TYPE_BROADCAST_PROGRESS, times, sizeof(times));
}

/* Broadcast message to all connected WebSocket clients */
static void BarWebsocketBroadcast(const char *message, size_t len) {
	if (!g_wsContext || !message || len == 0) {
		debugPrint(DEBUG_WEBSOCKET, "WebSocket: Broadcast called with null/empty message\n");
		return;
	}
	
	BarWsContext_t *ctx = g_wsContext;
	
	/* Detect progress messages for separate debug flag */
	bool isProgress = (len > 13 && strncmp(message, "2[ \"progress\"", 13) == 0);
#ifdef HAVE_DEBUGLOG
	debugKind dbgFlag = isProgress ? DEBUG_WEBSOCKET_PROGRESS : DEBUG_WEBSOCKET;
#else
	int dbgFlag = 0;  /* Unused when debugPrint is a no-op */
#endif
	
	debugPrint(dbgFlag, "WebSocket: Broadcasting to %zu clients (%zu bytes): %.100s%s\n", 
	           ctx->numConnections, len, message, len > 100 ? "..." : "");
	
	/* Iterate through all connected clients */
	for (size_t i = 0; i < ctx->maxConnections; i++) {
		if (ctx->connections[i].wsi != NULL) {
			struct lws *wsi = (struct lws *)ctx->connections[i].wsi;
			
			debugPrint(dbgFlag, "WebSocket: Sending to client %zu (wsi=%p)\n", i, wsi);
			
			/* Allocate buffer with LWS_PRE padding */
			unsigned char *buf = malloc(LWS_PRE + len);
			if (!buf) {
				debugPrint(dbgFlag, "WebSocket: Failed to allocate buffer for client %zu\n", i);
				continue;
			}
			
			/* Copy message after LWS_PRE padding */
			memcpy(&buf[LWS_PRE], message, len);
			
			debugPrint(dbgFlag, "WebSocket: About to call lws_write (len=%zu)\n", len);
			
			/* Send to this client */
			int written = lws_write(wsi, &buf[LWS_PRE], len, LWS_WRITE_TEXT);
			
			if (written < 0) {
				debugPrint(dbgFlag, "WebSocket: lws_write failed for client %zu (error=%d)\n", i, written);
			} else {
				debugPrint(dbgFlag, "WebSocket: lws_write succeeded for client %zu (%d bytes)\n", i, written);
			}
			
			free(buf);
		}
	}
	
	debugPrint(dbgFlag, "WebSocket: Broadcast complete\n");
}

/* Handle incoming WebSocket message */
void BarWebsocketHandleMessage(BarApp_t *app, const char *message,
                               size_t len, const char *protocol) {
	if (!app || !message || len == 0) {
		return;
	}
	
	/* Route to appropriate protocol handler */
	if (strcmp(protocol, "homeassistant") == 0) {
		/* TODO: Call BarHaBridgeHandleMessage(app, message); */
		debugPrint(DEBUG_WEBSOCKET, "WebSocket: HA message received (not yet implemented)\n");
	} else {
		/* Default to Socket.IO */
		BarSocketIoHandleMessage(app, message);
	}
}

