/*
 * Test-only frame capture for code that sends through BarWebsocketSend.
 *
 * Attaches fake clients to app->wsContext (or to a private context when the
 * test has none) and hooks writeFrame, so every frame delivered by
 * BarWsDeliver is recorded with the client that received it. An optional
 * sink sees every frame too (lets older tests keep a "last message" buffer).
 * Header-only: each test file gets its own static capture state.
 */

#pragma once

#include <check.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/main.h"
#include "../../src/websocket/core/websocket.h"

#define TEST_WS_MAX_CLIENTS 2
#define TEST_WS_MAX_FRAMES 32

typedef void (*TestWsSink_t) (const char *message, size_t len);

typedef struct {
	BarWsContext_t ownCtx;
	BarWsConnection_t conns[TEST_WS_MAX_CLIENTS];
	char sockets[TEST_WS_MAX_CLIENTS];
	BarWsConnId_t ids[TEST_WS_MAX_CLIENTS];
	TestWsSink_t sink;
	char *frames[TEST_WS_MAX_FRAMES];
	int frameClient[TEST_WS_MAX_FRAMES];
	size_t frameCount;
} TestWsCapture_t;

static TestWsCapture_t test_ws_cap;

static inline int
test_ws_capture_write (void *wsi, unsigned char *buf, size_t len)
{
	TestWsCapture_t *c = &test_ws_cap;
	ck_assert_uint_lt (c->frameCount, TEST_WS_MAX_FRAMES);
	char *copy = malloc (len + 1);
	ck_assert_ptr_nonnull (copy);
	memcpy (copy, buf, len);
	copy[len] = '\0';
	c->frames[c->frameCount] = copy;
	c->frameClient[c->frameCount] = (int) ((char *) wsi - c->sockets);
	c->frameCount++;
	if (c->sink != NULL) {
		c->sink ((const char *) buf, len);
	}
	return (int) len;
}

static inline void
TestWsCaptureClear (void)
{
	for (size_t i = 0; i < test_ws_cap.frameCount; i++) {
		free (test_ws_cap.frames[i]);
		test_ws_cap.frames[i] = NULL;
	}
	test_ws_cap.frameCount = 0;
}

/* Attach `clients` fake clients (IDs in test_ws_cap.ids) and start capturing.
 * Call after the test has set app->wsContext, if it uses its own context.
 * Initialises connMutex on that context (Detach destroys it), so do not use
 * it on a context whose connMutex the test already initialised; call Detach
 * before the test destroys its context. */
static inline BarWsContext_t *
TestWsCaptureAttach (BarApp_t *app, size_t clients, TestWsSink_t sink)
{
	TestWsCapture_t *c = &test_ws_cap;
	ck_assert_uint_le (clients, TEST_WS_MAX_CLIENTS);
	TestWsCaptureClear ();
	if (app->wsContext == NULL) {
		memset (&c->ownCtx, 0, sizeof (c->ownCtx));
		for (int i = 0; i < BUCKET_COUNT; i++) {
			pthread_mutex_init (&c->ownCtx.buckets[i].mutex, NULL);
		}
		app->wsContext = &c->ownCtx;
	}
	BarWsContext_t *ctx = app->wsContext;
	pthread_mutex_init (&ctx->connMutex, NULL);
	memset (c->conns, 0, sizeof (c->conns));
	ctx->connections = c->conns;
	ctx->maxConnections = TEST_WS_MAX_CLIENTS;
	ctx->numConnections = 0;
	ctx->writeFrame = test_ws_capture_write;
	for (size_t i = 0; i < clients; i++) {
		c->ids[i] = BarWsConnectionAdd (ctx, &c->sockets[i], "socketio");
		ck_assert_uint_ne (c->ids[i], BAR_WS_DEST_BROADCAST);
	}
	c->sink = sink;
	return ctx;
}

/* Stop capturing, free recorded frames, and detach the private context. */
static inline void
TestWsCaptureDetach (BarApp_t *app)
{
	TestWsCapture_t *c = &test_ws_cap;
	BarWsContext_t *ctx = app->wsContext;
	if (ctx != NULL) {
		ctx->connections = NULL;
		ctx->maxConnections = 0;
		ctx->numConnections = 0;
		ctx->writeFrame = NULL;
		pthread_mutex_destroy (&ctx->connMutex);
		if (ctx == &c->ownCtx) {
			for (int i = 0; i < BUCKET_COUNT; i++) {
				if (ctx->buckets[i].message != NULL) {
					BarWsMessageFree (ctx->buckets[i].message);
					ctx->buckets[i].message = NULL;
				}
				pthread_mutex_destroy (&ctx->buckets[i].mutex);
			}
			app->wsContext = NULL;
		}
	}
	c->sink = NULL;
	TestWsCaptureClear ();
}

/* Number of captured frames that went to `client` and contain `needle`. */
static inline size_t
TestWsCaptureCount (int client, const char *needle)
{
	size_t n = 0;
	for (size_t i = 0; i < test_ws_cap.frameCount; i++) {
		if (test_ws_cap.frameClient[i] == client &&
				strstr (test_ws_cap.frames[i], needle) != NULL) {
			n++;
		}
	}
	return n;
}
