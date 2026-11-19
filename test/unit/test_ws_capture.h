/*
 * Test-only helpers for code that sends through BarWebsocketSend.
 *
 * Frame capture: attaches fake clients to app->wsContext (or to a private
 * context when the test has none) and hooks the socket adapters.
 * TestWsCaptureFlush runs the service-thread side (BarWsServiceOutbox, then
 * writable callbacks until every client queue is empty), recording every
 * frame with the client that received it. The write hook fails the test if
 * it runs outside a writable callback. An optional sink sees every frame too
 * (lets older tests keep a "last message" buffer).
 *
 * Ingress inspection: for tests without clients, TestWsPendingTake takes
 * everything queued so far and remembers the newest payload per key.
 *
 * Header-only: each test file gets its own static state.
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
#define TEST_WS_KEYS (BAR_WS_KEY_STATIONS + 1)

typedef void (*TestWsSink_t) (const char *message, size_t len);

typedef struct {
	BarWsContext_t ownCtx;
	BarWsContext_t *ctx;          /* attached context */
	BarWsOutbox_t *ownOutbox;     /* created by Attach, freed by Detach */
	BarWsConnection_t conns[TEST_WS_MAX_CLIENTS];
	char sockets[TEST_WS_MAX_CLIENTS];
	BarWsConnId_t ids[TEST_WS_MAX_CLIENTS];
	TestWsSink_t sink;
	bool inWritable;              /* set only around BarWsHandleWritable */
	char *frames[TEST_WS_MAX_FRAMES];
	int frameClient[TEST_WS_MAX_FRAMES];
	size_t frameCount;
	char *lastPending[TEST_WS_KEYS];
} TestWsCapture_t;

static TestWsCapture_t test_ws_cap;

static inline int
test_ws_capture_write (void *wsi, unsigned char *buf, size_t len)
{
	TestWsCapture_t *c = &test_ws_cap;
	ck_assert_msg (c->inWritable, "socket write outside the writable callback");
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
test_ws_capture_request_writable (void *wsi)
{
	(void) wsi; /* TestWsCaptureFlush drives every client with pending frames */
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
 * Call after the test has set app->wsContext, if it uses its own context;
 * an outbox is created if that context has none. Call Detach before the
 * test destroys its context. */
static inline BarWsContext_t *
TestWsCaptureAttach (BarApp_t *app, size_t clients, TestWsSink_t sink)
{
	TestWsCapture_t *c = &test_ws_cap;
	ck_assert_uint_le (clients, TEST_WS_MAX_CLIENTS);
	TestWsCaptureClear ();
	if (app->wsContext == NULL) {
		memset (&c->ownCtx, 0, sizeof (c->ownCtx));
		app->wsContext = &c->ownCtx;
	}
	BarWsContext_t *ctx = app->wsContext;
	c->ctx = ctx;
	c->ownOutbox = NULL;
	if (ctx->outbox == NULL) {
		c->ownOutbox = ctx->outbox = BarWsOutboxNew ();
		ck_assert_ptr_nonnull (ctx->outbox);
	}
	memset (c->conns, 0, sizeof (c->conns));
	ctx->connections = c->conns;
	ctx->maxConnections = TEST_WS_MAX_CLIENTS;
	ctx->numConnections = 0;
	ctx->writeFrame = test_ws_capture_write;
	ctx->requestWritable = test_ws_capture_request_writable;
	for (size_t i = 0; i < clients; i++) {
		c->ids[i] = BarWsConnectionAdd (ctx, &c->sockets[i], "socketio");
		ck_assert_uint_ne (c->ids[i], BAR_WS_DEST_BROADCAST);
	}
	c->sink = sink;
	return ctx;
}

/* Run the service-thread side: route queued frames, then give every client
 * with pending frames writable callbacks until all queues are empty. */
static inline void
TestWsCaptureFlush (void)
{
	TestWsCapture_t *c = &test_ws_cap;
	BarWsContext_t *ctx = c->ctx;
	ck_assert_ptr_nonnull (ctx);
	BarWsServiceOutbox (ctx);
	bool more = true;
	while (more) {
		more = false;
		for (size_t i = 0; i < ctx->maxConnections; i++) {
			BarWsConnection_t *conn = &ctx->connections[i];
			if (conn->wsi != NULL && conn->pending.count > 0) {
				c->inWritable = true;
				BarWsHandleWritable (ctx, conn->wsi);
				c->inWritable = false;
				more = true;
			}
		}
	}
}

/* Stop capturing, free recorded and pending frames, and detach the private
 * context and outbox. */
static inline void
TestWsCaptureDetach (BarApp_t *app)
{
	TestWsCapture_t *c = &test_ws_cap;
	BarWsContext_t *ctx = app->wsContext;
	if (ctx != NULL) {
		for (size_t i = 0; i < ctx->maxConnections; i++) {
			BarWsClientQueueClear (&ctx->connections[i].pending);
		}
		ctx->connections = NULL;
		ctx->maxConnections = 0;
		ctx->numConnections = 0;
		ctx->writeFrame = NULL;
		ctx->requestWritable = NULL;
		if (c->ownOutbox != NULL) {
			BarWsOutboxFree (c->ownOutbox);
			c->ownOutbox = NULL;
			ctx->outbox = NULL;
		}
		if (ctx == &c->ownCtx) {
			app->wsContext = NULL;
		}
	}
	c->ctx = NULL;
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

/* Take everything queued on ctx's outbox so far, remembering the newest
 * payload per key (see TestWsPendingLast). Returns how many taken entries
 * had `key`. */
static inline size_t
TestWsPendingTake (BarWsContext_t *ctx, BarWsKey_t key)
{
	size_t n = 0;
	BarWsPending_t *list = BarWsOutboxTake (ctx->outbox);
	for (const BarWsPending_t *p = list; p; p = p->next) {
		size_t len = BarWsFrameLen (p->frame);
		char *copy = malloc (len + 1);
		ck_assert_ptr_nonnull (copy);
		memcpy (copy, BarWsFrameData (p->frame), len);
		copy[len] = '\0';
		free (test_ws_cap.lastPending[p->key]);
		test_ws_cap.lastPending[p->key] = copy;
		if (p->key == key) {
			n++;
		}
	}
	BarWsPendingFree (list);
	return n;
}

/* Newest payload ever queued with `key` (taking pending entries first) */
static inline const char *
TestWsPendingLast (BarWsContext_t *ctx, BarWsKey_t key)
{
	TestWsPendingTake (ctx, key);
	ck_assert_ptr_nonnull (test_ws_cap.lastPending[key]);
	return test_ws_cap.lastPending[key];
}

/* Forget remembered payloads (call in teardown) */
static inline void
TestWsPendingReset (void)
{
	for (int k = 0; k < TEST_WS_KEYS; k++) {
		free (test_ws_cap.lastPending[k]);
		test_ws_cap.lastPending[k] = NULL;
	}
}
