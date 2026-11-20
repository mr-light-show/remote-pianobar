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

/* WebSocket service edges: full connection table, missing context, and the
 * close reason a real client receives when the server drops it. */

#include <check.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <libwebsockets.h>
#include "../../src/main.h"
#include "../../src/settings.h"
#include "../../src/bar_state.h"
#include "../../src/websocket/core/websocket.h"
#include "../test_helpers.h"

START_TEST (test_connection_add_returns_broadcast_id_when_table_full) {
	char sockets[3];
	BarWsContext_t ctx;
	memset (&ctx, 0, sizeof (ctx));
	ctx.maxConnections = 2;
	ctx.connections = calloc (ctx.maxConnections, sizeof (BarWsConnection_t));
	ck_assert_ptr_nonnull (ctx.connections);

	BarWsConnId_t a = BarWsConnectionAdd (&ctx, &sockets[0], "socketio");
	BarWsConnId_t b = BarWsConnectionAdd (&ctx, &sockets[1], "socketio");
	ck_assert_uint_ne (a, BAR_WS_DEST_BROADCAST);
	ck_assert_uint_ne (b, BAR_WS_DEST_BROADCAST);
	ck_assert_uint_ne (a, b);
	ck_assert_uint_eq (ctx.numConnections, 2);

	/* Full: the extra client is not tracked and gets no ID */
	ck_assert_uint_eq (BarWsConnectionAdd (&ctx, &sockets[2], "socketio"),
			BAR_WS_DEST_BROADCAST);
	ck_assert_uint_eq (ctx.numConnections, 2);

	/* A freed slot is reusable, with a fresh ID */
	BarWsConnectionRemove (&ctx, &sockets[0]);
	BarWsConnId_t c = BarWsConnectionAdd (&ctx, &sockets[2], "socketio");
	ck_assert_uint_ne (c, BAR_WS_DEST_BROADCAST);
	ck_assert_uint_ne (c, a);
	ck_assert_uint_ne (c, b);
	free (ctx.connections);
}
END_TEST

START_TEST (test_service_outbox_ignores_missing_context_and_outbox) {
	BarWsContext_t ctx;
	memset (&ctx, 0, sizeof (ctx));
	BarWsServiceOutbox (NULL);
	BarWsServiceOutbox (&ctx); /* no outbox: nothing to service */
	ck_assert_ptr_null (ctx.outbox);
}
END_TEST

START_TEST (test_song_generation_is_zero_without_websocket_context) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	ck_assert_ptr_null (app.wsContext);
	ck_assert_uint_eq (BarWebsocketSongGeneration (&app), 0);
	ck_assert_uint_eq (BarWebsocketSongGeneration (NULL), 0);
}
END_TEST

typedef struct {
	struct lws *wsi;
	bool established, failed, closed;
	unsigned closeStatus;
	char closeReason[64];
} CloseClient_t;

static int close_client_cb (struct lws *wsi, enum lws_callback_reasons reason,
		void *user, void *in, size_t len) {
	(void) user;
	CloseClient_t *c = lws_get_opaque_user_data (wsi);
	if (c == NULL) {
		return 0;
	}
	switch (reason) {
		case LWS_CALLBACK_CLIENT_ESTABLISHED:
			c->established = true;
			break;
		case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
			c->failed = true;
			break;
		case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
			if (len >= 2) {
				const unsigned char *b = in;
				c->closeStatus = (unsigned) (b[0] << 8 | b[1]);
				size_t n = len - 2 < sizeof (c->closeReason) - 1
						? len - 2 : sizeof (c->closeReason) - 1;
				memcpy (c->closeReason, b + 2, n);
				c->closeReason[n] = '\0';
			}
			break;
		case LWS_CALLBACK_CLIENT_CLOSED:
			c->closed = true;
			break;
		default:
			break;
	}
	return 0;
}

static const struct lws_protocols close_client_protocols[] = {
	{ "socketio", close_client_cb, 0, 4096, 0, NULL, 0 },
	{ NULL, NULL, 0, 0, 0, NULL, 0 }
};

/* A client that does not read while the server floods it is dropped, and the
 * server's close frame carries the reason (either the ingress or the per-client
 * queue overflowed first, depending on how fast the service thread drains).
 * The test waits for the close frame itself, not for the client socket
 * teardown (LWS_CALLBACK_CLIENT_CLOSED), which can lag the frame by ~30 s. */
START_TEST (test_flooded_client_receives_close_reason_over_lws) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.volumeMode = BAR_VOLUME_MODE_PLAYER;
	app.settings.websocketHost = strdup ("127.0.0.1");
	app.settings.websocketPort = free_loopback_port ();
	BarStateInit (&app);
	pthread_mutex_init (&app.player.lock, NULL);
	pthread_cond_init (&app.player.cond, NULL);
	pthread_cond_init (&app.player.audioCond, NULL);
	app.player.settings = &app.settings;
	ck_assert (BarWebsocketInit (&app));

	struct lws_context_creation_info info;
	memset (&info, 0, sizeof (info));
	info.port = CONTEXT_PORT_NO_LISTEN;
	info.protocols = close_client_protocols;
	struct lws_context *cctx = lws_create_context (&info);
	ck_assert_ptr_nonnull (cctx);

	CloseClient_t c;
	memset (&c, 0, sizeof (c));
	struct lws_client_connect_info ci;
	memset (&ci, 0, sizeof (ci));
	ci.context = cctx;
	ci.address = "127.0.0.1";
	ci.port = app.settings.websocketPort;
	ci.path = "/";
	ci.host = ci.address;
	ci.origin = ci.address;
	ci.protocol = "socketio";
	ci.opaque_user_data = &c;
	ci.pwsi = &c.wsi;
	ck_assert_ptr_nonnull (lws_client_connect_via_info (&ci));

	struct timespec start, now;
	clock_gettime (CLOCK_MONOTONIC, &start);
	while (!c.established) {
		ck_assert (!c.failed);
		clock_gettime (CLOCK_MONOTONIC, &now);
		ck_assert_msg (now.tv_sec - start.tv_sec < 5, "client never connected");
		lws_service (cctx, 20);
	}

	/* Flood without servicing the client: far more than the kernel socket
	 * buffers, ingress and per-client queue can hold */
	char *payload = malloc (WS_FRAME_MAX_BYTES + 1);
	ck_assert_ptr_nonnull (payload);
	memset (payload, 'a', WS_FRAME_MAX_BYTES);
	payload[WS_FRAME_MAX_BYTES] = '\0';
	for (int i = 0; i < 160; i++) {
		BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
				strdup (payload));
		if (i % 8 == 7) {
			usleep (1000); /* let the service thread run between bursts */
		}
	}
	free (payload);

	/* Now read: the close frame follows the data already on the wire */
	clock_gettime (CLOCK_MONOTONIC, &start);
	while (!c.closed && c.closeStatus == 0) {
		clock_gettime (CLOCK_MONOTONIC, &now);
		ck_assert_msg (now.tv_sec - start.tv_sec < 20,
				"client never received a close frame");
		lws_service (cctx, 20);
	}
	ck_assert_msg (strcmp (c.closeReason, "Server event queue full") == 0 ||
			strcmp (c.closeReason, "Client event queue full") == 0,
			"unexpected close reason '%s'", c.closeReason);
	ck_assert_msg (c.closeStatus == LWS_CLOSE_STATUS_UNEXPECTED_CONDITION ||
			c.closeStatus == LWS_CLOSE_STATUS_POLICY_VIOLATION,
			"unexpected close status %u", c.closeStatus);

	lws_context_destroy (cctx);
	BarWebsocketDestroy (&app);
	pthread_cond_destroy (&app.player.audioCond);
	pthread_cond_destroy (&app.player.cond);
	pthread_mutex_destroy (&app.player.lock);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

Suite *ws_service_edges_suite (void);
Suite *ws_service_edges_suite (void) {
	Suite *s = suite_create ("ws_service_edges");
	TCase *tc = tcase_create ("Core");
	tcase_add_test (tc, test_connection_add_returns_broadcast_id_when_table_full);
	tcase_add_test (tc, test_service_outbox_ignores_missing_context_and_outbox);
	tcase_add_test (tc, test_song_generation_is_zero_without_websocket_context);
	tcase_add_test (tc, test_flooded_client_receives_close_reason_over_lws);
	tcase_set_timeout (tc, 60);
	suite_add_tcase (s, tc);
	return s;
}
