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
#include "websocket.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libwebsockets.h>
#include <json-c/json.h>

/* WebSocket protocol callback */
static int callback_websocket(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
	BarApp_t *app = (BarApp_t *)lws_context_user(lws_get_context(wsi));
	
	switch (reason) {
		case LWS_CALLBACK_ESTABLISHED:
			/* New client connected */
			fprintf(stderr, "WebSocket: Client connected\n");
			break;
			
		case LWS_CALLBACK_CLOSED:
			/* Client disconnected */
			fprintf(stderr, "WebSocket: Client disconnected\n");
			break;
			
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
	
	/* Initialize libwebsockets */
	memset(&info, 0, sizeof(info));
	info.port = app->settings.websocketPort;
	info.protocols = protocols;
	info.user = app;
	info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT |
	               LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;
	
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
	
	fprintf(stderr, "WebSocket: Server started on port %d\n",
	        app->settings.websocketPort);
	
	return true;
}

/* Destroy WebSocket server */
void BarWebsocketDestroy(BarApp_t *app) {
	if (!app || !app->wsContext) {
		return;
	}
	
	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	
	if (ctx->context) {
		lws_context_destroy(ctx->context);
		ctx->context = NULL;
	}
	
	if (ctx->connections) {
		free(ctx->connections);
		ctx->connections = NULL;
	}
	
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
	ctx->progress.songStartTime = time(NULL);
	ctx->progress.isPlaying = true;
	ctx->progress.lastBroadcast = 0;
	
	/* Get song duration from player */
	if (app->player.songDuration > 0) {
		ctx->progress.songDuration = app->player.songDuration;
	}
	
	/* Broadcast to all clients */
	BarWebsocketBroadcastState(app);
}

/* Broadcast song stop event */
void BarWebsocketBroadcastSongStop(BarApp_t *app) {
	if (!app || !app->wsContext) {
		return;
	}
	
	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	
	ctx->progress.isPlaying = false;
	
	/* Broadcast to all clients */
	BarWebsocketBroadcastState(app);
}

/* Broadcast volume change */
void BarWebsocketBroadcastVolume(BarApp_t *app, int volume) {
	if (!app || !app->wsContext) {
		return;
	}
	
	/* Broadcast to all clients */
	BarWebsocketBroadcastState(app);
}

/* Broadcast progress update */
void BarWebsocketBroadcastProgress(BarApp_t *app) {
	if (!app || !app->wsContext) {
		return;
	}
	
	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	
	if (!ctx->progress.isPlaying) {
		return;
	}
	
	time_t now = time(NULL);
	
	/* Only broadcast every 1-2 seconds */
	if (now - ctx->progress.lastBroadcast < 1) {
		return;
	}
	
	ctx->progress.lastBroadcast = now;
	
	/* Broadcast to all clients */
	BarWebsocketBroadcastState(app);
}

/* Handle incoming WebSocket message */
void BarWebsocketHandleMessage(BarApp_t *app, const char *message,
                               size_t len, const char *protocol) {
	if (!app || !message || len == 0) {
		return;
	}
	
	/* Parse JSON message */
	json_object *msg = json_tokener_parse(message);
	if (!msg) {
		fprintf(stderr, "WebSocket: Failed to parse message\n");
		return;
	}
	
	/* Route to appropriate protocol handler */
	if (strcmp(protocol, "homeassistant") == 0) {
		/* TODO: Call BarHaBridgeHandleMessage(app, message); */
		fprintf(stderr, "WebSocket: HA message received (not yet implemented)\n");
	} else {
		/* Default to Socket.IO */
		/* TODO: Call BarSocketIoHandleMessage(app, message); */
		fprintf(stderr, "WebSocket: Socket.IO message received (not yet implemented)\n");
	}
	
	json_object_put(msg);
}

