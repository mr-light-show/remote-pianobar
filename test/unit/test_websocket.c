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

#include <check.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../../src/main.h"
#include "../../src/settings.h"
#include "../../src/bar_state.h"
#include "../../src/system_volume.h"
#include "../../src/websocket_bridge.h"
#include "../../src/websocket/core/websocket.h"
#include "../../src/websocket/core/websocket_lws_log.h"
#include "../../src/websocket/protocol/socketio.h"
#include "../../src/websocket/daemon/daemon.h"
#include "../../src/log.h"
#include "../../src/ui.h"
#include "../../src/interrupt.h"
#include <errno.h>
#include <json-c/json.h>
#include <libwebsockets.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "test_ws_capture.h"

/* Wire-compat capture buffer */
static char g_compatBuf[8192];
static void compatCapture (const char *msg, size_t len) {
	size_t copy = len < sizeof (g_compatBuf) - 1 ? len : sizeof (g_compatBuf) - 1;
	memcpy (g_compatBuf, msg, copy);
	g_compatBuf[copy] = '\0';
}

/* One fake client whose frames land in g_compatBuf once TestWsCaptureFlush
 * runs the service-thread side. Returns its connection ID. */
static BarWsConnId_t compatAttach (BarApp_t *app) {
	g_compatBuf[0] = '\0';
	TestWsCaptureAttach (app, 1, compatCapture);
	return test_ws_cap.ids[0];
}

static void compatDetach (BarApp_t *app) {
	TestWsCaptureDetach (app);
}

/* Test: WebSocket initialization with NULL app should fail */
START_TEST(test_websocket_init_null) {
	bool result = BarWebsocketInit(NULL);
	ck_assert_int_eq(result, false);
}
END_TEST

/* Test: WebSocket cleanup with NULL app should not crash */
START_TEST(test_websocket_destroy_null) {
	BarWebsocketDestroy(NULL);
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Test: Get elapsed time with NULL app should return 0 */
START_TEST(test_websocket_elapsed_null) {
	unsigned int elapsed = BarWebsocketGetElapsed(NULL);
	ck_assert_int_eq(elapsed, 0);
}
END_TEST

/* Test: Broadcast message with NULL app should not crash */
START_TEST(test_websocket_broadcast_null) {
	/* NULL app frees the message and reports failure */
	char *msg = strdup ("2[\"test\"]");
	ck_assert (!BarWebsocketSend (NULL, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, msg));
}
END_TEST

START_TEST(test_websocket_bridge_singleton_lock_null_app) {
	ck_assert(BarWsAcquireSingletonLock(NULL));
}
END_TEST

static const char *test_pending_payload (BarWsContext_t *ctx, BarWsKey_t key) {
	return TestWsPendingLast (ctx, key);
}

static void test_setup_web_app (BarApp_t *app, BarWsContext_t *ctx) {
	memset (app, 0, sizeof (*app));
	memset (ctx, 0, sizeof (*ctx));
	BarSettingsInit (&app->settings);
	app->settings.uiMode = BAR_UI_MODE_WEB;
	app->settings.volumeMode = BAR_VOLUME_MODE_PLAYER;
	app->settings.volume = 40;
	app->wsContext = ctx;
	ctx->outbox = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (ctx->outbox);
	BarStateInit (app);
	pthread_mutex_init (&app->player.lock, NULL);
	pthread_cond_init (&app->player.cond, NULL);
	pthread_cond_init (&app->player.audioCond, NULL);
	app->player.settings = &app->settings;
	app->player.requestedVolume = 40;
}

static void test_teardown_web_app (BarApp_t *app, BarWsContext_t *ctx) {
	pthread_cond_destroy (&app->player.cond);
	pthread_cond_destroy (&app->player.audioCond);
	pthread_mutex_destroy (&app->player.lock);
	BarStateDestroy (app);
	BarWsOutboxFree (ctx->outbox);
	ctx->outbox = NULL;
	TestWsPendingReset ();
	BarSettingsDestroy (&app->settings);
}

static void test_attach_station_and_song (BarApp_t *app,
		PianoStation_t *station, PianoSong_t *song) {
	memset (station, 0, sizeof (*station));
	memset (song, 0, sizeof (*song));

	station->id = "station-1";
	station->name = "Station One";
	station->displayName = "Display One";
	song->title = "Song One";
	song->artist = "Artist One";
	song->stationId = "station-1";
	app->ph.stations = station;
	app->curStation = station;
	app->playlist = song;
}

START_TEST(test_websocket_bridge_broadcast_process_queues_snapshot_payload) {
	BarApp_t app;
	BarWsContext_t ctx;
	PianoSong_t song;
	PianoStation_t station;
	test_setup_web_app (&app, &ctx);
	test_attach_station_and_song (&app, &station, &song);

	BarWsBroadcastProcess (&app);

	const char *payload = test_pending_payload (&ctx, BAR_WS_KEY_NONE);
	ck_assert (strstr (payload, "\"process\"") != NULL);
	ck_assert (strstr (payload, "\"song\"") != NULL);
	ck_assert (strstr (payload, "songStationName") != NULL);
	ck_assert (strstr (payload, "Display One") != NULL);

	test_teardown_web_app (&app, &ctx);
}
END_TEST

START_TEST(test_websocket_bridge_broadcasts_real_player_state_events) {
	BarApp_t app;
	BarWsContext_t ctx;
	PianoSong_t song;
	PianoStation_t station;
	test_setup_web_app (&app, &ctx);
	test_attach_station_and_song (&app, &station, &song);

	BarWsBroadcastVolume (&app);
	ck_assert (strstr (test_pending_payload (&ctx, BAR_WS_KEY_VOLUME), "\"volume\"") != NULL);
	ck_assert (strstr (test_pending_payload (&ctx, BAR_WS_KEY_VOLUME), "40") != NULL);
	ck_assert (BarPlayerSetVolume (&app.player, 41));
	BarWsBroadcastVolume (&app);
	ck_assert (strstr (test_pending_payload (&ctx, BAR_WS_KEY_VOLUME), "41") != NULL);

	BarWsBroadcastStations (&app);
	ck_assert (strstr (test_pending_payload (&ctx, BAR_WS_KEY_STATIONS), "\"stations\"") != NULL);
	ck_assert (strstr (test_pending_payload (&ctx, BAR_WS_KEY_STATIONS), "Display One") != NULL);

	BarPlayerSetMode (&app.player, PLAYER_PLAYING);
	pthread_mutex_lock (&app.player.lock);
	app.player.songPlayed = 45;
	app.player.songDuration = 180;
	pthread_mutex_unlock (&app.player.lock);
	BarWsBroadcastProgress (&app);
	ck_assert (strstr (test_pending_payload (&ctx, BAR_WS_KEY_PROGRESS), "\"progress\"") != NULL);
	ck_assert (strstr (test_pending_payload (&ctx, BAR_WS_KEY_PROGRESS), "\"elapsed\"") != NULL);
	ck_assert (strstr (test_pending_payload (&ctx, BAR_WS_KEY_PROGRESS), "45") != NULL);
	pthread_mutex_lock (&app.player.lock);
	app.player.songPlayed = 46;
	app.player.songDuration = 0;
	pthread_mutex_unlock (&app.player.lock);
	BarWsBroadcastProgress (&app);
	ck_assert (strstr (test_pending_payload (&ctx, BAR_WS_KEY_PROGRESS), "\"percentage\"") != NULL);

	/* A song start discards progress still waiting in ingress */
	pthread_mutex_lock (&app.player.lock);
	app.player.songPlayed = 47;
	pthread_mutex_unlock (&app.player.lock);
	BarWsBroadcastProgress (&app);
	BarWsBroadcastSongStart (&app);
	ck_assert_uint_eq (TestWsPendingTake (&ctx, BAR_WS_KEY_PROGRESS), 0);
	ck_assert (strstr (test_pending_payload (&ctx, BAR_WS_KEY_NONE), "\"start\"") != NULL);

	test_teardown_web_app (&app, &ctx);
}
END_TEST

START_TEST(test_websocket_bridge_start_stop_and_paused_progress) {
	BarApp_t app;
	BarWsContext_t ctx;
	PianoSong_t song;
	PianoStation_t station;
	test_setup_web_app (&app, &ctx);
	test_attach_station_and_song (&app, &station, &song);

	BarWsBroadcastSongStart (&app);
	ck_assert (strstr (test_pending_payload (&ctx, BAR_WS_KEY_NONE), "\"start\"") != NULL);
	ck_assert (strstr (test_pending_payload (&ctx, BAR_WS_KEY_NONE), "Song One") != NULL);

	BarWsBroadcastSongStop (&app);
	ck_assert (strstr (test_pending_payload (&ctx, BAR_WS_KEY_NONE), "\"stop\"") != NULL);

	BarPlayerSetMode (&app.player, PLAYER_PLAYING);
	pthread_mutex_lock (&app.player.lock);
	app.player.doPause = true;
	app.player.songPlayed = 60;
	app.player.songDuration = 180;
	pthread_mutex_unlock (&app.player.lock);
	BarWsBroadcastProgress (&app);
	ck_assert_uint_eq (TestWsPendingTake (&ctx, BAR_WS_KEY_PROGRESS), 0);

	test_teardown_web_app (&app, &ctx);
}
END_TEST

START_TEST (test_websocket_bridge_progress_skips_duplicate_elapsed)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoSong_t song;
	PianoStation_t station;
	test_setup_web_app (&app, &ctx);
	test_attach_station_and_song (&app, &station, &song);

	BarPlayerSetMode (&app.player, PLAYER_PLAYING);
	pthread_mutex_lock (&app.player.lock);
	app.player.songPlayed = 30;
	app.player.songDuration = 180;
	pthread_mutex_unlock (&app.player.lock);

	BarWsBroadcastProgress (&app);
	ck_assert_uint_eq (TestWsPendingTake (&ctx, BAR_WS_KEY_PROGRESS), 1);

	BarWsBroadcastProgress (&app);
	ck_assert_uint_eq (TestWsPendingTake (&ctx, BAR_WS_KEY_PROGRESS), 0);

	/* The same elapsed time in the next song is new progress */
	BarWsBroadcastSongStart (&app);
	BarWsBroadcastProgress (&app);
	ck_assert_uint_eq (TestWsPendingTake (&ctx, BAR_WS_KEY_PROGRESS), 1);

	test_teardown_web_app (&app, &ctx);
}
END_TEST

/* Connection-table fixture: fake clients without real sockets. The test is
 * the event loop: it runs the service side and decides which client gets a
 * writable callback, so blocked and writable clients are explicit. */
#define TEST_MAX_CLIENTS 4

static char test_fake_sockets[TEST_MAX_CLIENTS];

/* Records which fake sockets a delivery wrote to, and what. */
static void *test_written_wsi[80];
static char test_written_msg[80][64];
static size_t test_written_count;
static size_t test_writable_requests[TEST_MAX_CLIENTS];
static bool test_in_writable;     /* set only around BarWsHandleWritable */
static int test_write_result;     /* 0: report the whole frame written */
static size_t test_last_written_len;

static int test_write_frame (void *wsi, unsigned char *buf, size_t len) {
	ck_assert_msg (test_in_writable, "socket write outside the writable callback");
	ck_assert_uint_lt (test_written_count, 80);
	test_written_wsi[test_written_count] = wsi;
	snprintf (test_written_msg[test_written_count], 64, "%.*s", (int) len, buf);
	test_written_count++;
	test_last_written_len = len;
	return test_write_result != 0 ? test_write_result : (int) len;
}

static void test_request_writable (void *wsi) {
	test_writable_requests[(char *) wsi - test_fake_sockets]++;
}

static void test_setup_clients (BarApp_t *app, BarWsContext_t *ctx) {
	test_setup_web_app (app, ctx);
	ctx->maxConnections = TEST_MAX_CLIENTS;
	ctx->connections = calloc (TEST_MAX_CLIENTS, sizeof (BarWsConnection_t));
	ck_assert_ptr_nonnull (ctx->connections);
	test_written_count = 0;
	test_write_result = 0;
	memset (test_writable_requests, 0, sizeof (test_writable_requests));
	ctx->writeFrame = test_write_frame;
	ctx->requestWritable = test_request_writable;
}

static void test_teardown_clients (BarApp_t *app, BarWsContext_t *ctx) {
	for (size_t i = 0; i < ctx->maxConnections; i++) {
		BarWsClientQueueClear (&ctx->connections[i].pending);
	}
	free (ctx->connections);
	test_teardown_web_app (app, ctx);
}

static BarWsConnId_t test_add_client (BarWsContext_t *ctx, int client) {
	BarWsConnId_t id = BarWsConnectionAdd (ctx, &test_fake_sockets[client], "socketio");
	ck_assert_uint_ne (id, BAR_WS_DEST_BROADCAST);
	return id;
}

/* One LWS_CALLBACK_SERVER_WRITEABLE for `client` */
static int test_writable (BarWsContext_t *ctx, int client) {
	test_in_writable = true;
	int r = BarWsHandleWritable (ctx, &test_fake_sockets[client]);
	test_in_writable = false;
	return r;
}

static size_t test_pending_count (BarWsContext_t *ctx, int client) {
	for (size_t i = 0; i < ctx->maxConnections; i++) {
		if (ctx->connections[i].wsi == &test_fake_sockets[client]) {
			return ctx->connections[i].pending.count;
		}
	}
	return 0;
}

static BarWsConnection_t *test_conn (BarWsContext_t *ctx, int client) {
	for (size_t i = 0; i < ctx->maxConnections; i++) {
		if (ctx->connections[i].wsi == &test_fake_sockets[client]) {
			return &ctx->connections[i];
		}
	}
	ck_abort_msg ("client %d not tracked", client);
	return NULL;
}

/* Service side, then writable callbacks for `client` until it is drained */
static void test_drain_client (BarWsContext_t *ctx, int client) {
	BarWsServiceOutbox (ctx);
	while (test_pending_count (ctx, client) > 0) {
		ck_assert_int_eq (test_writable (ctx, client), 0);
	}
}

/* Service side, then drain every tracked client */
static void test_drain_all (BarWsContext_t *ctx) {
	BarWsServiceOutbox (ctx);
	for (int c = 0; c < TEST_MAX_CLIENTS; c++) {
		while (test_pending_count (ctx, c) > 0) {
			ck_assert_int_eq (test_writable (ctx, c), 0);
		}
	}
}

static bool test_send (BarApp_t *app, BarWsConnId_t dest, BarWsKey_t key,
		const char *text) {
	char *copy = strdup (text);
	ck_assert_ptr_nonnull (copy);
	return BarWebsocketSend (app, dest, key, copy);
}

/* Heap text of exactly `len` bytes: 2["big","xxx...x"] */
static char *test_frame_text (size_t len) {
	static const char head[] = "2[\"big\",\"";
	static const char tail[] = "\"]";
	ck_assert_uint_gt (len, sizeof (head) + sizeof (tail));
	char *t = malloc (len + 1);
	ck_assert_ptr_nonnull (t);
	memset (t, 'x', len);
	memcpy (t, head, sizeof (head) - 1);
	memcpy (t + len - (sizeof (tail) - 1), tail, sizeof (tail) - 1);
	t[len] = '\0';
	return t;
}

START_TEST (test_send_broadcast_reaches_all_clients)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_add_client (&ctx, 0);
	test_add_client (&ctx, 1);

	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_VOLUME, "2[\"volume\",40]"));
	ck_assert_uint_eq (test_written_count, 0); /* queued, not yet written */
	test_drain_all (&ctx);

	ck_assert_uint_eq (test_written_count, 2);
	ck_assert_ptr_eq (test_written_wsi[0], &test_fake_sockets[0]);
	ck_assert_ptr_eq (test_written_wsi[1], &test_fake_sockets[1]);
	ck_assert_str_eq (test_written_msg[0], "2[\"volume\",40]");
	ck_assert_str_eq (test_written_msg[1], "2[\"volume\",40]");

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_send_to_id_reaches_only_that_client)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	BarWsConnId_t a = test_add_client (&ctx, 0);
	BarWsConnId_t b = test_add_client (&ctx, 1);
	ck_assert_uint_ne (a, b);

	ck_assert (test_send (&app, b, BAR_WS_KEY_NONE, "2[\"stations\"]"));
	BarWsServiceOutbox (&ctx);
	ck_assert_uint_eq (test_pending_count (&ctx, 0), 0);
	ck_assert_uint_eq (test_pending_count (&ctx, 1), 1);
	ck_assert_uint_eq (test_writable_requests[0], 0);
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 1);
	ck_assert_ptr_eq (test_written_wsi[0], &test_fake_sockets[1]);
	ck_assert_str_eq (test_written_msg[0], "2[\"stations\"]");

	test_teardown_clients (&app, &ctx);
}
END_TEST

/* Destination IDs are resolved on the service thread, so a send to an ID
 * that is gone can only be accepted and then dropped there. */
START_TEST (test_send_to_reused_slot_does_not_reach_new_client)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	BarWsConnId_t a = test_add_client (&ctx, 0);
	ck_assert_uint_eq (a, 1);
	BarWsConnectionRemove (&ctx, &test_fake_sockets[0]);
	/* Client B reuses slot 0 */
	BarWsConnId_t b = test_add_client (&ctx, 1);
	ck_assert_uint_eq (b, 2);
	ck_assert_ptr_eq (ctx.connections[0].wsi, &test_fake_sockets[1]);

	ck_assert (test_send (&app, a, BAR_WS_KEY_NONE, "2[\"reply\"]"));
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 0);
	/* B itself is still addressable */
	ck_assert (test_send (&app, b, BAR_WS_KEY_NONE, "2[\"reply\"]"));
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 1);
	ck_assert_ptr_eq (test_written_wsi[0], &test_fake_sockets[1]);

	test_teardown_clients (&app, &ctx);
}
END_TEST

/* A reply queued while its requester was connected must not reach the
 * client that takes over the slot before the service thread runs. */
START_TEST (test_reused_slot_does_not_receive_old_reply)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	BarWsConnId_t a = test_add_client (&ctx, 0);
	ck_assert (test_send (&app, a, BAR_WS_KEY_NONE, "2[\"old-reply\"]"));
	BarWsConnectionRemove (&ctx, &test_fake_sockets[0]);
	test_add_client (&ctx, 1);
	ck_assert_ptr_eq (ctx.connections[0].wsi, &test_fake_sockets[1]);

	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 0);
	ck_assert_uint_eq (test_pending_count (&ctx, 1), 0);

	test_teardown_clients (&app, &ctx);
}
END_TEST

/* Replies are ordered events: a reply and a keyed broadcast for the same
 * client never replace each other, and the other client gets the broadcast. */
START_TEST (test_send_targeted_and_broadcast_do_not_replace_each_other)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	BarWsConnId_t a = test_add_client (&ctx, 0);
	test_add_client (&ctx, 1);

	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_STATIONS, "2[\"bcast\"]"));
	ck_assert (test_send (&app, a, BAR_WS_KEY_NONE, "2[\"reply\"]"));
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 3);
	ck_assert_ptr_eq (test_written_wsi[0], &test_fake_sockets[0]);
	ck_assert_str_eq (test_written_msg[0], "2[\"bcast\"]");
	ck_assert_ptr_eq (test_written_wsi[1], &test_fake_sockets[0]);
	ck_assert_str_eq (test_written_msg[1], "2[\"reply\"]");
	ck_assert_ptr_eq (test_written_wsi[2], &test_fake_sockets[1]);
	ck_assert_str_eq (test_written_msg[2], "2[\"bcast\"]");

	test_teardown_clients (&app, &ctx);
}
END_TEST

/* A targeted send has no global side effects: it neither starts a new song
 * generation nor discards anyone's pending progress. */
START_TEST (test_send_targeted_state_keeps_pending_progress_broadcast)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	BarWsConnId_t a = test_add_client (&ctx, 0);
	test_add_client (&ctx, 1);

	uint32_t generation = BarWebsocketSongGeneration (&app);
	ck_assert (BarWebsocketBroadcastProgress (&app, generation, strdup ("2[\"progress\"]")));
	ck_assert (test_send (&app, a, BAR_WS_KEY_NONE, "2[\"start\"]"));
	ck_assert_uint_eq (BarWebsocketSongGeneration (&app), generation);
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 3);
	ck_assert_str_eq (test_written_msg[0], "2[\"progress\"]");
	ck_assert_str_eq (test_written_msg[1], "2[\"start\"]");
	ck_assert_ptr_eq (test_written_wsi[2], &test_fake_sockets[1]);
	ck_assert_str_eq (test_written_msg[2], "2[\"progress\"]");

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_receive_from_untracked_client_is_dropped)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_add_client (&ctx, 0);
	BarWsConnId_t b = test_add_client (&ctx, 1);
	ctx.commands = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (ctx.commands);

	/* Live but untracked (e.g. the table was full): its query must not turn
	 * into a broadcast, nor reach anyone else, so it is never queued */
	BarWsHandleReceive (&app, &test_fake_sockets[2], "0", 1, true);
	BarWsHandleReceive (&app, &test_fake_sockets[2], "2[\"query\"]", 11, true);
	/* Empty or missing frames are ignored, even from a tracked client */
	BarWsHandleReceive (&app, &test_fake_sockets[1], "", 0, true);
	BarWsHandleReceive (&app, &test_fake_sockets[1], NULL, 5, true);
	/* Removing a socket that was never tracked leaves the table alone */
	BarWsConnectionRemove (&ctx, &test_fake_sockets[2]);
	ck_assert_uint_eq (ctx.numConnections, 2);

	/* A tracked client's request is queued with its ID; the receive path
	 * runs nothing itself */
	BarWsHandleReceive (&app, &test_fake_sockets[1], "2[\"query\"]", 11, true);
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 0);
	BarWsConnId_t id = 0;
	char *text = NULL;
	ck_assert (BarWsCommandQueuePop (ctx.commands, &id, &text, NULL));
	ck_assert_uint_eq (id, b);
	ck_assert_str_eq (text, "2[\"query\"]");
	/* Nothing else was queued: a sentinel pushed now is next */
	ck_assert_int_eq (BarWsCommandQueuePush (ctx.commands, 9, "s", 1), BAR_WS_CMD_OK);
	BarWsConnId_t sentinel = 0;
	ck_assert (BarWsCommandQueuePop (ctx.commands, &sentinel, NULL, NULL));
	ck_assert_uint_eq (sentinel, 9);

	/* The worker's step: its reply reaches the requester, and only it */
	BarSocketIoHandleMessage (&app, text, id);
	free (text);
	ck_assert_uint_eq (test_written_count, 0); /* queued, not yet written */
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 2);
	ck_assert_ptr_eq (test_written_wsi[0], &test_fake_sockets[1]);
	ck_assert_ptr_eq (test_written_wsi[1], &test_fake_sockets[1]);
	ck_assert (strstr (test_written_msg[0], "process") != NULL);
	ck_assert (strstr (test_written_msg[1], "stations") != NULL);

	BarWsCommandQueueFree (ctx.commands);
	test_teardown_clients (&app, &ctx);
}
END_TEST

/* Formerly test_send_unknown_id_frees_message_and_returns_false: IDs are now
 * resolved on the service thread, so an unknown ID is accepted and dropped
 * there (nothing written, nothing leaked); invalid arguments still fail. */
START_TEST (test_send_unknown_id_is_dropped_and_bad_args_fail)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_add_client (&ctx, 0);

	/* Run under ASan/valgrind to catch a leak; NULL args must not crash. */
	ck_assert (test_send (&app, 999, BAR_WS_KEY_NONE, "x"));
	ck_assert (!BarWebsocketSend (NULL, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, strdup ("x")));
	ck_assert (!BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, NULL));
	ck_assert (!BarWebsocketBroadcastSongStart (NULL, strdup ("x")));
	ck_assert (!BarWebsocketBroadcastProgress (NULL, 0, strdup ("x")));
	BarWebsocketBroadcastPlayStateMessage (NULL, strdup ("x"), 1);
	ck_assert_uint_eq (BarWebsocketSongGeneration (NULL), 0);
	app.wsContext = NULL;
	ck_assert (!BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, strdup ("x")));
	app.wsContext = &ctx;
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 0);

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_enqueue_does_not_write)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	BarWsConnId_t a = test_add_client (&ctx, 0);
	test_add_client (&ctx, 1);

	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, "2[\"e1\"]"));
	ck_assert (test_send (&app, a, BAR_WS_KEY_NONE, "2[\"reply\"]"));
	ck_assert (BarWebsocketBroadcastSongStart (&app, strdup ("2[\"start\"]")));
	ck_assert_uint_eq (test_written_count, 0);
	/* The service side only routes and asks for writable callbacks */
	BarWsServiceOutbox (&ctx);
	ck_assert_uint_eq (test_written_count, 0);
	ck_assert_uint_eq (test_pending_count (&ctx, 0), 3);
	ck_assert_uint_eq (test_pending_count (&ctx, 1), 2);
	ck_assert_uint_eq (test_writable_requests[0], 1);
	ck_assert_uint_eq (test_writable_requests[1], 1);

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_writable_callback_writes_one_frame)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_add_client (&ctx, 0);

	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, "2[\"a\"]"));
	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_VOLUME, "2[\"b\"]"));
	BarWsServiceOutbox (&ctx);
	ck_assert_uint_eq (test_writable_requests[0], 1);

	ck_assert_int_eq (test_writable (&ctx, 0), 0);
	ck_assert_uint_eq (test_written_count, 1);
	ck_assert_str_eq (test_written_msg[0], "2[\"a\"]");
	/* more pending: another writable callback is requested */
	ck_assert_uint_eq (test_writable_requests[0], 2);

	ck_assert_int_eq (test_writable (&ctx, 0), 0);
	ck_assert_uint_eq (test_written_count, 2);
	ck_assert_str_eq (test_written_msg[1], "2[\"b\"]");
	ck_assert_uint_eq (test_writable_requests[0], 2); /* drained: no request */

	/* a spurious callback with nothing pending writes nothing */
	ck_assert_int_eq (test_writable (&ctx, 0), 0);
	ck_assert_uint_eq (test_written_count, 2);
	/* nor does one for a socket that is not tracked */
	ck_assert_int_eq (test_writable (&ctx, 3), 0);
	ck_assert_uint_eq (test_written_count, 2);

	test_teardown_clients (&app, &ctx);
}
END_TEST

/* lws_write's contract: a short positive count means lws buffered the rest
 * itself, so the frame is sent and the next callback writes the next one. */
START_TEST (test_short_write_counts_frame_as_sent)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_add_client (&ctx, 0);

	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, "2[\"first\"]"));
	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, "2[\"second\"]"));
	BarWsServiceOutbox (&ctx);
	test_write_result = 1;
	ck_assert_int_eq (test_writable (&ctx, 0), 0);
	ck_assert_int_eq (test_writable (&ctx, 0), 0);
	ck_assert_uint_eq (test_written_count, 2);
	ck_assert_str_eq (test_written_msg[0], "2[\"first\"]");
	ck_assert_str_eq (test_written_msg[1], "2[\"second\"]");

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_write_failure_closes_connection)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_add_client (&ctx, 0);

	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, "2[\"a\"]"));
	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, "2[\"b\"]"));
	BarWsServiceOutbox (&ctx);
	test_write_result = -1;
	ck_assert_int_eq (test_writable (&ctx, 0), -1);
	ck_assert_uint_eq (test_written_count, 1);
	ck_assert_uint_eq (test_pending_count (&ctx, 0), 0);

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_nonwritable_client_keeps_pending_frame)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_add_client (&ctx, 0);

	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, "2[\"kept\"]"));
	/* Service passes run, but the socket never becomes writable */
	BarWsServiceOutbox (&ctx);
	BarWsServiceOutbox (&ctx);
	ck_assert_uint_eq (test_written_count, 0);
	ck_assert_uint_eq (test_pending_count (&ctx, 0), 1);
	/* Once writable, the frame goes out */
	ck_assert_int_eq (test_writable (&ctx, 0), 0);
	ck_assert_uint_eq (test_written_count, 1);
	ck_assert_str_eq (test_written_msg[0], "2[\"kept\"]");

	test_teardown_clients (&app, &ctx);
}
END_TEST

/* A closed connection's pending frames are freed (leaks show under
 * make test-asan) and the shared frames still reach the other client. */
START_TEST (test_close_frees_pending)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_add_client (&ctx, 0);
	test_add_client (&ctx, 1);

	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, "2[\"a\"]"));
	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_STATIONS, "2[\"b\"]"));
	BarWsServiceOutbox (&ctx);
	ck_assert_uint_eq (test_pending_count (&ctx, 0), 2);
	BarWsConnectionRemove (&ctx, &test_fake_sockets[0]);
	ck_assert_ptr_null (ctx.connections[0].wsi);
	ck_assert_ptr_null (ctx.connections[0].pending.head);
	ck_assert_uint_eq (ctx.connections[0].pending.count, 0);
	ck_assert_int_eq (test_writable (&ctx, 0), 0);
	ck_assert_uint_eq (test_written_count, 0);

	test_drain_client (&ctx, 1);
	ck_assert_uint_eq (test_written_count, 2);
	ck_assert_str_eq (test_written_msg[1], "2[\"b\"]");

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_blocked_client_does_not_delay_writable_client)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_add_client (&ctx, 0); /* blocked: never gets a writable callback */
	test_add_client (&ctx, 1);

	char text[32];
	for (int i = 0; i < 20; i++) {
		snprintf (text, sizeof (text), "2[\"e%d\"]", i);
		ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, text));
		test_drain_client (&ctx, 1);
		ck_assert_uint_eq (test_written_count, (size_t) i + 1);
		ck_assert_ptr_eq (test_written_wsi[i], &test_fake_sockets[1]);
		ck_assert_str_eq (test_written_msg[i], text);
	}
	ck_assert_uint_eq (test_pending_count (&ctx, 0), 20);
	ck_assert (!test_conn (&ctx, 0)->pendingClose);

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_overflow_closes_only_that_client)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_add_client (&ctx, 0); /* stops reading */
	test_add_client (&ctx, 1);

	for (int i = 0; i < WS_CLIENT_MAX_MSGS; i++) {
		ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, "2[\"m\"]"));
	}
	test_drain_client (&ctx, 1);
	ck_assert_uint_eq (test_pending_count (&ctx, 0), WS_CLIENT_MAX_MSGS);
	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, "2[\"last\"]"));
	test_drain_client (&ctx, 1);

	BarWsConnection_t *slow = test_conn (&ctx, 0);
	ck_assert (slow->pendingClose);
	ck_assert_str_eq (slow->closeReason, "Client event queue full");
	ck_assert_int_eq (slow->closeStatus, LWS_CLOSE_STATUS_POLICY_VIOLATION);
	ck_assert_uint_eq (slow->pending.count, 0);
	size_t before = test_written_count;
	ck_assert_int_eq (test_writable (&ctx, 0), -1); /* lws closes it */
	ck_assert_uint_eq (test_written_count, before);

	BarWsConnection_t *fast = test_conn (&ctx, 1);
	ck_assert (!fast->pendingClose);
	ck_assert_uint_eq (test_written_count, WS_CLIENT_MAX_MSGS + 1);
	ck_assert_str_eq (test_written_msg[WS_CLIENT_MAX_MSGS], "2[\"last\"]");
	/* the closed client gets nothing more; the other still does */
	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, "2[\"after\"]"));
	BarWsServiceOutbox (&ctx);
	ck_assert_uint_eq (slow->pending.count, 0);
	ck_assert_uint_eq (fast->pending.count, 1);

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_ingress_full_closes_all_clients)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_add_client (&ctx, 0);
	BarWsConnId_t b = test_add_client (&ctx, 1);

	for (int i = 0; i < WS_INGRESS_MAX_MSGS; i++) {
		ck_assert (test_send (&app, b, BAR_WS_KEY_NONE, "2[\"m\"]"));
	}
	/* even a reply to one client loses an event: everyone resyncs */
	ck_assert (!test_send (&app, b, BAR_WS_KEY_NONE, "2[\"lost\"]"));
	BarWsServiceOutbox (&ctx);
	for (int c = 0; c < 2; c++) {
		BarWsConnection_t *conn = test_conn (&ctx, c);
		ck_assert (conn->pendingClose);
		ck_assert_str_eq (conn->closeReason, "Server event queue full");
		ck_assert_int_eq (conn->closeStatus, LWS_CLOSE_STATUS_UNEXPECTED_CONDITION);
		ck_assert_uint_eq (conn->pending.count, 0);
		ck_assert_uint_eq (test_writable_requests[c], 1);
		ck_assert_int_eq (test_writable (&ctx, c), -1);
	}
	ck_assert_uint_eq (test_written_count, 0);
	/* the flag was consumed: the next pass delivers normally */
	ck_assert (test_send (&app, b, BAR_WS_KEY_NONE, "2[\"x\"]"));
	BarWsServiceOutbox (&ctx);
	ck_assert_uint_eq (test_conn (&ctx, 1)->pending.count, 0); /* closing */

	test_teardown_clients (&app, &ctx);
}
END_TEST

/* An oversize frame can never be delivered, so reconnecting would not help:
 * it is rejected on its own and nobody is closed. */
START_TEST (test_oversize_frame_is_rejected_without_closing_clients)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	BarWsConnId_t a = test_add_client (&ctx, 0);
	test_add_client (&ctx, 1);

	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, "2[\"before\"]"));
	BarWsServiceOutbox (&ctx);
	ck_assert (!BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
			test_frame_text (WS_FRAME_MAX_BYTES + 1)));
	ck_assert (!BarWebsocketSend (&app, a, BAR_WS_KEY_NONE,
			test_frame_text (WS_FRAME_MAX_BYTES + 1)));
	uint32_t generation = BarWebsocketSongGeneration (&app);
	ck_assert (!BarWebsocketBroadcastSongStart (&app, test_frame_text (WS_FRAME_MAX_BYTES + 1)));
	ck_assert_uint_eq (BarWebsocketSongGeneration (&app), generation + 1);
	BarWsServiceOutbox (&ctx);
	for (int c = 0; c < 2; c++) {
		ck_assert (!test_conn (&ctx, c)->pendingClose);
		ck_assert_uint_eq (test_pending_count (&ctx, c), 1); /* queue intact */
	}

	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, "2[\"next\"]"));
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 4);
	ck_assert_str_eq (test_written_msg[0], "2[\"before\"]");
	ck_assert_str_eq (test_written_msg[1], "2[\"next\"]");
	ck_assert_str_eq (test_written_msg[2], "2[\"before\"]");
	ck_assert_str_eq (test_written_msg[3], "2[\"next\"]");

	test_teardown_clients (&app, &ctx);
}
END_TEST

/* A large requester-only stations reply (~100 KiB, a big library) reaches
 * only the requester, intact, and closes nobody. */
START_TEST (test_large_stations_reply_does_not_close_clients)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	BarWsConnId_t a = test_add_client (&ctx, 0);
	test_add_client (&ctx, 1);

	char *reply = test_frame_text (100 * 1024);
	ck_assert (BarWebsocketSend (&app, a, BAR_WS_KEY_NONE, reply));
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 1);
	ck_assert_ptr_eq (test_written_wsi[0], &test_fake_sockets[0]);
	ck_assert_uint_eq (test_last_written_len, 100 * 1024);
	ck_assert (strncmp (test_written_msg[0], "2[\"big\",\"xxx", 12) == 0);
	for (int c = 0; c < 2; c++) {
		ck_assert (!test_conn (&ctx, c)->pendingClose);
	}

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_disconnect_all_clients_closes_and_frees_pending)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_add_client (&ctx, 0);
	test_add_client (&ctx, 1);
	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, "2[\"a\"]"));
	BarWsServiceOutbox (&ctx);

	BarWebsocketDisconnectAllClients (&app);
	BarWebsocketDisconnectAllClients (&app); /* idempotent */
	for (int c = 0; c < 2; c++) {
		BarWsConnection_t *conn = test_conn (&ctx, c);
		ck_assert (conn->pendingClose);
		ck_assert_str_eq (conn->closeReason, "Server stopping");
		ck_assert_int_eq (conn->closeStatus, LWS_CLOSE_STATUS_GOINGAWAY);
		ck_assert_uint_eq (conn->pending.count, 0);
		ck_assert_int_eq (test_writable (&ctx, c), -1);
	}
	ck_assert_uint_eq (test_written_count, 0);

	test_teardown_clients (&app, &ctx);
}
END_TEST

/* Review focus: a pause right after a queued song start keeps both */
START_TEST (test_start_then_playstate_both_delivered)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_add_client (&ctx, 0);

	ck_assert (BarWebsocketBroadcastSongStart (&app, strdup ("2[\"start\"]")));
	BarWebsocketBroadcastPlayStateMessage (&app, strdup ("2[\"playState\"]"), 1);
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 2);
	ck_assert_str_eq (test_written_msg[0], "2[\"start\"]");
	ck_assert_str_eq (test_written_msg[1], "2[\"playState\"]");

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_stop_then_start_both_delivered)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_add_client (&ctx, 0);

	ck_assert (test_send (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, "2[\"stop\"]"));
	ck_assert (BarWebsocketBroadcastSongStart (&app, strdup ("2[\"start\"]")));
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 2);
	ck_assert_str_eq (test_written_msg[0], "2[\"stop\"]");
	ck_assert_str_eq (test_written_msg[1], "2[\"start\"]");

	test_teardown_clients (&app, &ctx);
}
END_TEST

/* Progress already routed to a client queue is dropped there once the next
 * song starts; progress of the new song goes through. */
START_TEST (test_progress_for_old_song_dropped_after_start)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_add_client (&ctx, 0);

	uint32_t oldSong = BarWebsocketSongGeneration (&app);
	ck_assert (BarWebsocketBroadcastProgress (&app, oldSong, strdup ("2[\"progress\",\"old\"]")));
	BarWsServiceOutbox (&ctx);
	ck_assert_uint_eq (test_pending_count (&ctx, 0), 1);
	ck_assert (BarWebsocketBroadcastSongStart (&app, strdup ("2[\"start\"]")));
	uint32_t newSong = BarWebsocketSongGeneration (&app);
	ck_assert_uint_ne (newSong, oldSong);
	ck_assert (BarWebsocketBroadcastProgress (&app, newSong, strdup ("2[\"progress\",\"new\"]")));
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 2);
	ck_assert_str_eq (test_written_msg[0], "2[\"start\"]");
	ck_assert_str_eq (test_written_msg[1], "2[\"progress\",\"new\"]");

	test_teardown_clients (&app, &ctx);
}
END_TEST

/* R2: a sample taken for the old song but pushed after the start is stale */
START_TEST (test_progress_sampled_before_start_but_pushed_after_is_dropped)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_add_client (&ctx, 0);

	uint32_t observed = BarWebsocketSongGeneration (&app); /* then sample */
	ck_assert (BarWebsocketBroadcastSongStart (&app, strdup ("2[\"start\"]")));
	ck_assert (BarWebsocketBroadcastProgress (&app, observed, strdup ("2[\"progress\",\"late\"]")));
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 1);
	ck_assert_str_eq (test_written_msg[0], "2[\"start\"]");

	test_teardown_clients (&app, &ctx);
}
END_TEST

/* Through the bridge: progress queued before a start never reaches clients,
 * and the same elapsed time is published again for the new song. */
START_TEST (test_bridge_progress_resumes_after_song_start)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoSong_t song;
	PianoStation_t station;
	test_setup_clients (&app, &ctx);
	test_attach_station_and_song (&app, &station, &song);
	test_add_client (&ctx, 0);

	BarPlayerSetMode (&app.player, PLAYER_PLAYING);
	pthread_mutex_lock (&app.player.lock);
	app.player.songPlayed = 12;
	app.player.songDuration = 100;
	pthread_mutex_unlock (&app.player.lock);
	BarWsBroadcastProgress (&app);
	BarWsBroadcastSongStart (&app);
	BarWsBroadcastProgress (&app);
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 2);
	ck_assert (strstr (test_written_msg[0], "\"start\"") != NULL);
	ck_assert (strstr (test_written_msg[1], "\"progress\"") != NULL);

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_websocket_bridge_disconnect_all_clients_with_context)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_web_app (&app, &ctx);
	BarWsDisconnectAllClients (&app);
	ck_assert (1);
	test_teardown_web_app (&app, &ctx);
}
END_TEST

START_TEST(test_websocket_bridge_unicast_helpers_and_errors) {
	BarApp_t app;
	BarWsContext_t ctx;
	PianoSong_t song;
	PianoStation_t station;
	test_setup_web_app (&app, &ctx);
	test_attach_station_and_song (&app, &station, &song);

	BarWsConnId_t requester = compatAttach (&app);

	BarWsEmitExplanationTo (&app, requester + 1, "ignored for an unknown client");
	TestWsCaptureFlush ();
	ck_assert_str_eq (g_compatBuf, "");

	BarWsEmitExplanationTo (&app, requester, "Because you like rock.");
	TestWsCaptureFlush ();
	ck_assert (strstr (g_compatBuf, "Because you like rock.") != NULL);

	g_compatBuf[0] = '\0';
	BarWsEmitErrorTo (&app, requester, "song.explain", "Failed to receive explanation");
	TestWsCaptureFlush ();
	ck_assert (strstr (g_compatBuf, "\"error\"") != NULL);
	ck_assert (strstr (g_compatBuf, "song.explain") != NULL);

	g_compatBuf[0] = '\0';
	BarWsEmitError (&app, "station.change", "Station not found");
	TestWsCaptureFlush ();
	ck_assert (strstr (g_compatBuf, "\"error\"") != NULL);
	ck_assert (strstr (g_compatBuf, "Station not found") != NULL);

	g_compatBuf[0] = '\0';
	BarWsEmitErrorEx (&app, "station.change", "Station not found", "abc-123");
	TestWsCaptureFlush ();
	ck_assert (strstr (g_compatBuf, "abc-123") != NULL);

	g_compatBuf[0] = '\0';
	BarWsBroadcastPandoraDisconnected (&app, "session_invalid");
	{
		const char *payload = test_pending_payload (&ctx, BAR_WS_KEY_NONE);
		ck_assert (strstr (payload, "pandora.disconnected") != NULL);
		ck_assert (strstr (payload, "session_invalid") != NULL);
	}
	BarWsBroadcastPandoraDisconnected (&app, NULL);
	{
		const char *payload = test_pending_payload (&ctx, BAR_WS_KEY_NONE);
		ck_assert (strstr (payload, "pandora.disconnected") != NULL);
		ck_assert (strstr (payload, "unknown") != NULL);
	}

	compatDetach (&app);
	test_teardown_web_app (&app, &ctx);
}
END_TEST

START_TEST(test_websocket_bridge_guarded_broadcasts_noop_without_web_context) {
	BarApp_t app;
	BarWsContext_t ctx;
	memset (&app, 0, sizeof (app));
	memset (&ctx, 0, sizeof (ctx));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;
	app.wsContext = &ctx;
	ctx.outbox = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (ctx.outbox);

	const BarPlayerPlayStateSnapshot snapshot = {true, 1};
	BarWsBroadcastPlayState (NULL);
	BarWsBroadcastPlayState (&app);
	BarWsBroadcastPlayStateSnapshot (NULL, &snapshot);
	BarWsBroadcastPlayStateSnapshot (&app, &snapshot);
	BarWsBroadcastPandoraDisconnected (NULL, "ignored");
	BarWsBroadcastPandoraDisconnected (&app, "ignored");
	ck_assert_ptr_null (BarWsOutboxTake (ctx.outbox));

	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarWsBroadcastPlayStateSnapshot (&app, NULL);
	ck_assert_ptr_null (BarWsOutboxTake (ctx.outbox));
	app.wsContext = NULL;
	BarWsBroadcastPlayState (&app);
	BarWsBroadcastPlayStateSnapshot (&app, &snapshot);
	BarWsBroadcastPandoraDisconnected (&app, "ignored");
	ck_assert_ptr_null (BarWsOutboxTake (ctx.outbox));

	BarWsOutboxFree (ctx.outbox);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST(test_websocket_bridge_upcoming_play_state_and_release_lock) {
	BarApp_t app;
	BarWsContext_t ctx;
	PianoSong_t song;

	test_setup_web_app (&app, &ctx);
	memset (&song, 0, sizeof (song));
	song.title = "Up Next";
	PianoSong_t playing;
	memset (&playing, 0, sizeof (playing));
	playing.head.next = &song.head;
	app.playlist = &playing;

	BarWsConnId_t requester = compatAttach (&app);
	ck_assert (BarWsEmitUpcomingTo (&app, requester, 1));
	TestWsCaptureFlush ();
	ck_assert (strstr (g_compatBuf, "Up Next") != NULL);

	g_compatBuf[0] = '\0';
	BarWsBroadcastPlayState (&app);
	{
		const char *payload = test_pending_payload (&ctx, BAR_WS_KEY_NONE);
		ck_assert (strstr (payload, "playState") != NULL);
	}

	app.lockFd = dup (STDERR_FILENO);
	ck_assert_int_ge (app.lockFd, 0);
	BarWsReleaseSingletonLock (&app);
	ck_assert_int_eq (app.lockFd, -1);

	compatDetach (&app);
	test_teardown_web_app (&app, &ctx);
}
END_TEST

START_TEST (test_websocket_play_state_waits_for_queued_control)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_web_app (&app, &ctx);
	app.player.audioControlWaiters = 1;
	BarWsBroadcastPlayState (&app);
	ck_assert_uint_eq (TestWsPendingTake (&ctx, BAR_WS_KEY_NONE), 0);
	app.player.audioControlWaiters = 0;
	BarWsBroadcastPlayState (&app);
	ck_assert_uint_eq (TestWsPendingTake (&ctx, BAR_WS_KEY_NONE), 1);
	test_teardown_web_app (&app, &ctx);
}
END_TEST

START_TEST(test_websocket_bridge_system_volume_mode_broadcast) {
	BarApp_t app;
	BarWsContext_t ctx;

	test_setup_web_app (&app, &ctx);
	app.settings.volumeMode = BAR_VOLUME_MODE_SYSTEM;

	BarWsBroadcastVolume (&app);
	ck_assert_ptr_nonnull (test_pending_payload (&ctx, BAR_WS_KEY_VOLUME));
	ck_assert (strstr (test_pending_payload (&ctx, BAR_WS_KEY_VOLUME), "volume") != NULL);

	test_teardown_web_app (&app, &ctx);
}
END_TEST

START_TEST(test_websocket_bridge_print_helpers_and_input_setup) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);

	app.input.fds[0] = 99;
	app.input.fds[1] = 100;
	app.input.maxfd = 200;
	BarWsConfigureWebOnlyInput (&app);
	ck_assert_int_eq (app.input.fds[0], -1);
	ck_assert_int_eq (app.input.fds[1], -1);
	ck_assert_int_eq (app.input.maxfd, 0);

	FILE *out = tmpfile ();
	ck_assert_ptr_nonnull (out);

	/* CLI mode: no web output expected. */
	app.settings.uiMode = BAR_UI_MODE_CLI;
	BarWsPrintWebInfo (&app, out);
	BarWsPrintPidFileInfo (&app, true, out);
	rewind (out);
	char buf[256] = {0};
	size_t n = fread (buf, 1, sizeof (buf) - 1, out);
	(void)n;
	ck_assert_str_eq (buf, "");

	rewind (out);
	ck_assert_int_eq (ftruncate (fileno (out), 0), 0);

	/* WEB mode prints URL using configured host/port. */
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.websocketHost = strdup ("192.0.2.7");
	app.settings.websocketPort = 9123;
	app.settings.pidFile = strdup ("/tmp/test_pianobar.pid");
	BarWsPrintWebInfo (&app, out);
	BarWsPrintPidFileInfo (&app, true, out);
	rewind (out);
	memset (buf, 0, sizeof (buf));
	n = fread (buf, 1, sizeof (buf) - 1, out);
	(void)n;
	ck_assert (strstr (buf, "192.0.2.7") != NULL);
	ck_assert (strstr (buf, "9123") != NULL);
	ck_assert (strstr (buf, "/tmp/test_pianobar.pid") != NULL);

	fclose (out);
	BarSettingsDestroy (&app.settings);
}
END_TEST

#ifdef HAVE_DEBUGLOG

static void capture_stderr_to_pipe (int pipefd[2], int *saved_stderr)
{
	ck_assert_int_eq (pipe (pipefd), 0);
	*saved_stderr = dup (STDERR_FILENO);
	ck_assert (*saved_stderr >= 0);
	ck_assert (dup2 (pipefd[1], STDERR_FILENO) >= 0);
	close (pipefd[1]);
}

static void restore_stderr_from_pipe (int pipefd_read, int saved_stderr, char *buf,
		size_t buf_size, ssize_t *out_len)
{
	fflush (stderr);
	dup2 (saved_stderr, STDERR_FILENO);
	close (saved_stderr);
	*out_len = read (pipefd_read, buf, buf_size - 1);
	close (pipefd_read);
	if (*out_len < 0) {
		*out_len = 0;
	}
	buf[*out_len] = '\0';
}

START_TEST (test_lws_log_message_strips_prefix_and_handles_null)
{
	ck_assert_str_eq (BarWsLwsLogMessage (NULL), "");
	ck_assert_str_eq (BarWsLwsLogMessage ("plain message\n"), "plain message\n");
	ck_assert_str_eq (BarWsLwsLogMessage (
		"[2026/07/27 15:48:52:2679] N: lws_create_context: ok\n"),
		"lws_create_context: ok\n");
}
END_TEST

START_TEST (test_lws_log_emit_routes_by_level)
{
	int pipefd[2];
	int saved_stderr;
	char buf[4096];
	ssize_t n;

	capture_stderr_to_pipe (pipefd, &saved_stderr);
	log_set_debug_mask (DEBUG_WEBSOCKET);

	BarWsLwsLogEmit (LLL_NOTICE,
		"[2026/07/27 15:48:52:2679] N: lws_socket_bind: source ads 0.0.0.0\n");
	BarWsLwsLogEmit (LLL_ERR, "[2026/07/27 15:48:52:2688] E: bind failed\n");
	BarWsLwsLogEmit (LLL_WARN, "");
	BarWsLwsConfigureLogging ();

	restore_stderr_from_pipe (pipefd[0], saved_stderr, buf, sizeof (buf), &n);
	log_set_debug_mask (0);

	ck_assert (n > 0);
	ck_assert (strstr (buf, "WebSocket") != NULL);
	ck_assert (strstr (buf, "lws_socket_bind") != NULL);
	ck_assert (strstr (buf, "Error") != NULL);
	ck_assert (strstr (buf, "bind failed") != NULL);
}
END_TEST

START_TEST (test_websocket_init_destroy_emits_debug_lifecycle_logs)
{
	BarApp_t app;
	int pipefd[2];
	int saved_stderr;
	char buf[8192];
	ssize_t n;

	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.websocketHost = strdup ("127.0.0.1");
	app.settings.websocketPort = 18081;

	capture_stderr_to_pipe (pipefd, &saved_stderr);
	log_set_debug_mask (DEBUG_WEBSOCKET);

	if (BarWebsocketInit (&app)) {
		BarWebsocketDestroy (&app);
	}

	restore_stderr_from_pipe (pipefd[0], saved_stderr, buf, sizeof (buf), &n);
	log_set_debug_mask (0);
	BarSettingsDestroy (&app.settings);

	ck_assert (n > 0);
	ck_assert (strstr (buf, "Server started on port 18081") != NULL);
	ck_assert (strstr (buf, "Server stopped") != NULL);
}
END_TEST

#endif /* HAVE_DEBUGLOG */

START_TEST (test_websocket_init_destroy_stops_when_app_quits)
{
	BarApp_t app;

	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.websocketHost = strdup ("127.0.0.1");
	app.settings.websocketPort = 18082;

	if (!BarWebsocketInit (&app)) {
		BarSettingsDestroy (&app.settings);
		return;
	}

	usleep (80000);
	atomic_store_explicit (&app.doQuit, true, memory_order_relaxed);
	usleep (80000);
	BarWebsocketDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

/* Init must fail cleanly (no context left behind) when it cannot listen.
 * The port is already held by a listening socket, so bind() fails with
 * EADDRINUSE on every platform. (A non-local address does not work: on Linux
 * libwebsockets retries EADDRNOTAVAIL in a busy loop and never returns.) */
START_TEST (test_websocket_init_fails_cleanly_when_bind_fails)
{
	BarApp_t app;
	struct sockaddr_in addr;
	socklen_t len = sizeof (addr);

	int holder = socket (AF_INET, SOCK_STREAM, 0);
	ck_assert_int_ge (holder, 0);
	memset (&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	addr.sin_port = 0;
	ck_assert_int_eq (bind (holder, (struct sockaddr *) &addr, sizeof (addr)), 0);
	ck_assert_int_eq (listen (holder, 1), 0);
	ck_assert_int_eq (getsockname (holder, (struct sockaddr *) &addr, &len), 0);

	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.websocketHost = strdup ("127.0.0.1");
	app.settings.websocketPort = ntohs (addr.sin_port);

	ck_assert (!BarWebsocketInit (&app));
	ck_assert_ptr_null (app.wsContext);
	BarSettingsDestroy (&app.settings);
	close (holder);
}
END_TEST

/* A loopback port that was free a moment ago (the kernel picks it) */
static int test_free_loopback_port (void) {
	int fd = socket (AF_INET, SOCK_STREAM, 0);
	ck_assert_int_ge (fd, 0);
	struct sockaddr_in addr;
	memset (&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	addr.sin_port = 0;
	ck_assert_int_eq (bind (fd, (struct sockaddr *) &addr, sizeof (addr)), 0);
	socklen_t len = sizeof (addr);
	ck_assert_int_eq (getsockname (fd, (struct sockaddr *) &addr, &len), 0);
	close (fd);
	return ntohs (addr.sin_port);
}

/* Real transport: libwebsockets clients talk to the real server (service
 * thread, writable callbacks, lws_write) over loopback. The test thread is a
 * producer for the server and the event loop of the client context. */
#define TEST_E2E_MAX_MSGS 16

#define TEST_E2E_MAX_OUT 8

typedef struct {
	struct lws *wsi;
	bool established, failed, closed;
	bool closeNow;                /* hang up on the next writable callback */
	char *partial;                /* frame being reassembled from fragments */
	size_t partialLen;
	char *msgs[TEST_E2E_MAX_MSGS];
	size_t count;
	char *out[TEST_E2E_MAX_OUT];  /* frames to send, in order (owned) */
	int outFlags[TEST_E2E_MAX_OUT]; /* lws_write protocol flags */
	size_t outCount, outSent;
} TestE2eClient_t;

static int test_e2e_client_cb (struct lws *wsi, enum lws_callback_reasons reason,
		void *user, void *in, size_t len) {
	(void) user;
	TestE2eClient_t *c = lws_get_opaque_user_data (wsi);
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
		case LWS_CALLBACK_CLIENT_CLOSED:
			c->closed = true;
			break;
		case LWS_CALLBACK_CLIENT_RECEIVE:
			c->partial = realloc (c->partial, c->partialLen + len + 1);
			ck_assert_ptr_nonnull (c->partial);
			memcpy (c->partial + c->partialLen, in, len);
			c->partialLen += len;
			if (lws_is_final_fragment (wsi)) {
				ck_assert_uint_lt (c->count, TEST_E2E_MAX_MSGS);
				c->msgs[c->count] = strndup (c->partial, c->partialLen);
				ck_assert_ptr_nonnull (c->msgs[c->count]);
				c->count++;
				c->partialLen = 0;
			}
			break;
		case LWS_CALLBACK_CLIENT_WRITEABLE:
			if (c->closeNow) {
				return -1;
			}
			if (c->outSent < c->outCount) {
				const int flags = c->outFlags[c->outSent];
				const char *text = c->out[c->outSent++];
				size_t n = strlen (text);
				unsigned char *buf = malloc (LWS_PRE + n);
				ck_assert_ptr_nonnull (buf);
				memcpy (&buf[LWS_PRE], text, n);
				ck_assert_int_eq (lws_write (wsi, &buf[LWS_PRE], n,
						(enum lws_write_protocol) flags), (int) n);
				free (buf);
				if (c->outSent < c->outCount) {
					lws_callback_on_writable (wsi);
				}
			}
			break;
		default:
			break;
	}
	return 0;
}

static const struct lws_protocols test_e2e_protocols[] = {
	{ "socketio", test_e2e_client_cb, 0, 4096, 0, NULL, 0 },
	{ NULL, NULL, 0, 0, 0, NULL, 0 }
};

static void test_e2e_connect (struct lws_context *cctx, int port, TestE2eClient_t *c) {
	struct lws_client_connect_info info;
	memset (&info, 0, sizeof (info));
	info.context = cctx;
	info.address = "127.0.0.1";
	info.port = port;
	info.path = "/";
	info.host = info.address;
	info.origin = info.address;
	info.protocol = "socketio";
	info.opaque_user_data = c;
	info.pwsi = &c->wsi;
	ck_assert_ptr_nonnull (lws_client_connect_via_info (&info));
}

/* Service the client context until both clients have at least `a`/`b`
 * messages (and are established); fails on timeout or connection error. */
static void test_e2e_pump_until (struct lws_context *cctx, TestE2eClient_t *ca,
		size_t a, TestE2eClient_t *cb, size_t b) {
	struct timespec start, now;
	clock_gettime (CLOCK_MONOTONIC, &start);
	for (;;) {
		ck_assert (!ca->failed && !cb->failed);
		bool aDone = a == 0 || (ca->established && ca->count >= a);
		bool bDone = b == 0 || (cb->established && cb->count >= b);
		if (aDone && bDone) {
			return;
		}
		clock_gettime (CLOCK_MONOTONIC, &now);
		ck_assert_msg (now.tv_sec - start.tv_sec < 3,
				"timed out: A has %zu/%zu, B has %zu/%zu messages",
				ca->count, a, cb->count, b);
		lws_service (cctx, 20);
	}
}

static void test_e2e_free (TestE2eClient_t *c) {
	for (size_t i = 0; i < c->count; i++) {
		free (c->msgs[i]);
	}
	for (size_t i = 0; i < c->outCount; i++) {
		free (c->out[i]);
	}
	free (c->partial);
}

/* Queue a frame (or, with NO_FIN/CONTINUATION flags, a fragment) for the
 * client to send, in order, on its writable callbacks */
static void test_e2e_send_flags (TestE2eClient_t *c, const char *text, int flags) {
	ck_assert_uint_lt (c->outCount, TEST_E2E_MAX_OUT);
	c->out[c->outCount] = strdup (text);
	ck_assert_ptr_nonnull (c->out[c->outCount]);
	c->outFlags[c->outCount] = flags;
	c->outCount++;
	lws_callback_on_writable (c->wsi);
}

static void test_e2e_send (TestE2eClient_t *c, const char *text) {
	test_e2e_send_flags (c, text, LWS_WRITE_TEXT);
}

static void test_e2e_expect (const TestE2eClient_t *c, size_t i, const char *needle) {
	ck_assert_uint_lt (i, c->count);
	ck_assert_msg (strstr (c->msgs[i], needle) != NULL,
			"message %zu is '%s', expected '%s'", i, c->msgs[i], needle);
}

START_TEST (test_end_to_end_delivery_order_over_lws)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.volumeMode = BAR_VOLUME_MODE_PLAYER;
	app.settings.websocketHost = strdup ("127.0.0.1");
	app.settings.websocketPort = test_free_loopback_port ();
	BarStateInit (&app);
	pthread_mutex_init (&app.player.lock, NULL);
	pthread_cond_init (&app.player.cond, NULL);
	pthread_cond_init (&app.player.audioCond, NULL);
	app.player.settings = &app.settings;
	ck_assert (BarWebsocketInit (&app));

	struct lws_context_creation_info info;
	memset (&info, 0, sizeof (info));
	info.port = CONTEXT_PORT_NO_LISTEN;
	info.protocols = test_e2e_protocols;
	struct lws_context *cctx = lws_create_context (&info);
	ck_assert_ptr_nonnull (cctx);

	/* Connect one at a time; each gets its full state on connect */
	TestE2eClient_t a, b;
	memset (&a, 0, sizeof (a));
	memset (&b, 0, sizeof (b));
	test_e2e_connect (cctx, app.settings.websocketPort, &a);
	test_e2e_pump_until (cctx, &a, 1, &b, 0);
	test_e2e_connect (cctx, app.settings.websocketPort, &b);
	test_e2e_pump_until (cctx, &a, 1, &b, 1);
	test_e2e_expect (&a, 0, "\"process\"");
	test_e2e_expect (&b, 0, "\"process\"");

	/* Producer side (this thread): ordered and keyed events interleaved */
	ck_assert (BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
			strdup ("2[\"e1\"]")));
	ck_assert (BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_VOLUME,
			strdup ("2[\"volume\",7]")));
	ck_assert (BarWebsocketBroadcastSongStart (&app, strdup ("2[\"start\"]")));
	ck_assert (BarWebsocketBroadcastProgress (&app, BarWebsocketSongGeneration (&app),
			strdup ("2[\"progress\",1]")));
	ck_assert (BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_STATIONS,
			strdup ("2[\"stations\",[]]")));
	ck_assert (BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
			strdup ("2[\"e2\"]")));
	test_e2e_pump_until (cctx, &a, 7, &b, 7);
	static const char *const order[] = {"\"e1\"", "\"volume\",7", "\"start\"",
			"\"progress\",1", "\"stations\",[]", "\"e2\""};
	for (size_t i = 0; i < 6; i++) {
		test_e2e_expect (&a, i + 1, order[i]);
		test_e2e_expect (&b, i + 1, order[i]);
	}

	/* A query from A (receive path) is answered to A only */
	test_e2e_send (&a, "2[\"query\"]");
	test_e2e_pump_until (cctx, &a, 9, &b, 7);
	test_e2e_expect (&a, 7, "\"process\"");
	test_e2e_expect (&a, 8, "\"stations\"");
	/* Anything misrouted to B would arrive before this later broadcast */
	ck_assert (BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
			strdup ("2[\"end\"]")));
	test_e2e_pump_until (cctx, &a, 10, &b, 8);
	test_e2e_expect (&a, 9, "\"end\"");
	test_e2e_expect (&b, 7, "\"end\"");
	ck_assert_uint_eq (a.count, 10);
	ck_assert_uint_eq (b.count, 8);

	lws_context_destroy (cctx);
	test_e2e_free (&a);
	test_e2e_free (&b);
	BarWebsocketDestroy (&app);
	pthread_cond_destroy (&app.player.audioCond);
	pthread_cond_destroy (&app.player.cond);
	pthread_mutex_destroy (&app.player.lock);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

/* A frame of exactly WS_FRAME_MAX_BYTES fits an empty client queue and goes
 * out intact through the real writable path and lws_write (which truncates
 * and buffers a frame this large internally). */
START_TEST (test_frame_at_limit_is_delivered_to_empty_client)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.volumeMode = BAR_VOLUME_MODE_PLAYER;
	app.settings.websocketHost = strdup ("127.0.0.1");
	app.settings.websocketPort = test_free_loopback_port ();
	BarStateInit (&app);
	pthread_mutex_init (&app.player.lock, NULL);
	pthread_cond_init (&app.player.cond, NULL);
	pthread_cond_init (&app.player.audioCond, NULL);
	app.player.settings = &app.settings;
	ck_assert (BarWebsocketInit (&app));

	struct lws_context_creation_info info;
	memset (&info, 0, sizeof (info));
	info.port = CONTEXT_PORT_NO_LISTEN;
	info.protocols = test_e2e_protocols;
	struct lws_context *cctx = lws_create_context (&info);
	ck_assert_ptr_nonnull (cctx);
	TestE2eClient_t a, none;
	memset (&a, 0, sizeof (a));
	memset (&none, 0, sizeof (none));
	test_e2e_connect (cctx, app.settings.websocketPort, &a);
	test_e2e_pump_until (cctx, &a, 1, &none, 0);

	char *big = test_frame_text (WS_FRAME_MAX_BYTES);
	char *expected = strdup (big);
	ck_assert_ptr_nonnull (expected);
	ck_assert (BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, big));
	ck_assert (BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
			strdup ("2[\"after\"]")));
	test_e2e_pump_until (cctx, &a, 3, &none, 0);
	ck_assert_uint_eq (strlen (a.msgs[1]), WS_FRAME_MAX_BYTES);
	ck_assert_int_eq (memcmp (a.msgs[1], expected, WS_FRAME_MAX_BYTES), 0);
	test_e2e_expect (&a, 2, "\"after\"");
	free (expected);

	lws_context_destroy (cctx);
	test_e2e_free (&a);
	BarWebsocketDestroy (&app);
	pthread_cond_destroy (&app.player.audioCond);
	pthread_cond_destroy (&app.player.cond);
	pthread_mutex_destroy (&app.player.lock);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_websocket_bridge_print_bind_all_and_startup_info)
{
	BarApp_t app;
	FILE *out;
	char buf[512];
	size_t n;

	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	out = tmpfile ();
	ck_assert_ptr_nonnull (out);

	app.settings.uiMode = BAR_UI_MODE_BOTH;
	app.settings.websocketHost = strdup ("0.0.0.0");
	app.settings.websocketPort = 8080;
	BarWsPrintWebInfo (&app, out);
	rewind (out);
	memset (buf, 0, sizeof (buf));
	n = fread (buf, 1, sizeof (buf) - 1, out);
	(void) n;
	ck_assert (strstr (buf, "http://0.0.0.0:8080/") != NULL);
	{
		char lan[INET_ADDRSTRLEN];

		BarDaemonGetIPv4Address (lan, sizeof (lan));
		if (lan[0] != '\0' && strcmp (lan, "127.0.0.1") != 0) {
			char expect[128];

			snprintf (expect, sizeof (expect), "http://%s:8080/", lan);
			ck_assert (strstr (buf, expect) != NULL);
		}
	}

	free (app.settings.websocketHost);
	app.settings.websocketHost = NULL;
	app.settings.websocketPort = 9999;
	rewind (out);
	ck_assert_int_eq (ftruncate (fileno (out), 0), 0);
	BarWsPrintWebInfo (&app, out);
	rewind (out);
	memset (buf, 0, sizeof (buf));
	n = fread (buf, 1, sizeof (buf) - 1, out);
	(void) n;
	ck_assert (strstr (buf, "http://127.0.0.1:9999/") != NULL);

	app.settings.webuiPath = strdup ("/custom/webui");
	rewind (out);
	ck_assert_int_eq (ftruncate (fileno (out), 0), 0);
	BarWsPrintWebInfo (&app, out);
	rewind (out);
	memset (buf, 0, sizeof (buf));
	n = fread (buf, 1, sizeof (buf) - 1, out);
	(void) n;
	ck_assert (strstr (buf, "Web UI files: /custom/webui") != NULL);
	free (app.settings.webuiPath);
	app.settings.webuiPath = NULL;

	rewind (out);
	ck_assert_int_eq (ftruncate (fileno (out), 0), 0);
	free (app.settings.websocketHost);
	app.settings.websocketHost = strdup ("::");
	app.settings.websocketPort = 8080;
	BarWsPrintWebInfo (&app, out);
	rewind (out);
	memset (buf, 0, sizeof (buf));
	n = fread (buf, 1, sizeof (buf) - 1, out);
	(void) n;
	ck_assert (strstr (buf, "http://:::8080/") != NULL);

	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.webuiPath = NULL;
	BarWsPrintWebInfo (&app, out);
	rewind (out);
	memset (buf, 0, sizeof (buf));
	n = fread (buf, 1, sizeof (buf) - 1, out);
	(void) n;
	ck_assert (strstr (buf, "Web UI files: (using built-in)") != NULL);

#ifdef HAVE_DEBUGLOG
	{
		int pipefd[2];
		int saved_stderr;
		char errbuf[2048];
		ssize_t errlen;

		capture_stderr_to_pipe (pipefd, &saved_stderr);
		BarWsPrintStartupInfo (&app);
		restore_stderr_from_pipe (pipefd[0], saved_stderr, errbuf,
		                         sizeof (errbuf), &errlen);
		ck_assert (strstr (errbuf, "Welcome") != NULL);
		ck_assert (strstr (errbuf, "http://:::") != NULL);

		capture_stderr_to_pipe (pipefd, &saved_stderr);
		app.settings.uiMode = BAR_UI_MODE_BOTH;
		BarWsPrintStartupInfo (&app);
		restore_stderr_from_pipe (pipefd[0], saved_stderr, errbuf,
		                         sizeof (errbuf), &errlen);
		ck_assert_int_eq (errlen, 0);
	}
#endif

	fclose (out);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST(test_websocket_schedule_volume_broadcast_sets_pending_flag) {
	BarWsContext_t ctx;
	memset (&ctx, 0, sizeof (ctx));
	ck_assert_int_eq (pthread_mutex_init (&ctx.volumeBroadcastMutex, NULL), 0);

	BarWsScheduleVolumeBroadcast (&ctx, 250);

	pthread_mutex_lock (&ctx.volumeBroadcastMutex);
	ck_assert (ctx.delayedVolumeBroadcast.pending);
	ck_assert_int_gt (ctx.delayedVolumeBroadcast.scheduleTime, 0);
	pthread_mutex_unlock (&ctx.volumeBroadcastMutex);

	pthread_mutex_destroy (&ctx.volumeBroadcastMutex);
}
END_TEST

/* Break caught: immediate/delayed messages use a stale settings mirror or
 * sample at schedule time rather than at event dispatch. */
START_TEST (test_websocket_immediate_and_delayed_volume_read_current_player_value)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_web_app (&app, &ctx);
	pthread_mutex_init (&ctx.volumeBroadcastMutex, NULL);
	ck_assert (BarPlayerSetVolume (&app.player, 72));
	app.settings.volume = 3; /* Deliberately stale mirror distinguishes the reader. */
	BarWsBroadcastVolume (&app);
	json_object *event = json_tokener_parse (test_pending_payload (&ctx, BAR_WS_KEY_VOLUME) + 1);
	ck_assert_ptr_nonnull (event);
	ck_assert_int_eq (json_object_get_int (json_object_array_get_idx (event, 1)), 72);
	json_object_put (event);
	BarWsScheduleVolumeBroadcast (&ctx, 0);
	ck_assert (BarPlayerSetVolume (&app.player, 84));
	app.settings.volume = 3;
	compatAttach (&app);
	BarWsProcessVolumeBroadcast (&ctx, &app);
	TestWsCaptureFlush ();
	event = json_tokener_parse (g_compatBuf + 1);
	ck_assert_ptr_nonnull (event);
	ck_assert_str_eq (json_object_get_string (json_object_array_get_idx (event, 0)), "volume");
	ck_assert_int_eq (json_object_get_int (json_object_array_get_idx (event, 1)), 84);
	ck_assert (!ctx.delayedVolumeBroadcast.pending);
	json_object_put (event);
	compatDetach (&app);
	pthread_mutex_destroy (&ctx.volumeBroadcastMutex);
	test_teardown_web_app (&app, &ctx);
}
END_TEST

START_TEST(test_websocket_bridge_upcoming_skips_unknown_requester) {
	BarApp_t app;
	BarWsContext_t ctx;
	PianoSong_t song;
	test_setup_web_app (&app, &ctx);
	memset (&song, 0, sizeof (song));
	song.title = "Hidden Upcoming";
	PianoSong_t playing;
	memset (&playing, 0, sizeof (playing));
	playing.head.next = &song.head;
	app.playlist = &playing;

	BarWsConnId_t requester = compatAttach (&app);
	ck_assert (BarWsEmitUpcomingTo (&app, requester + 1, 1));
	TestWsCaptureFlush ();
	ck_assert_str_eq (g_compatBuf, "");

	compatDetach (&app);
	test_teardown_web_app (&app, &ctx);
}
END_TEST

START_TEST(test_websocket_disconnect_all_clients_null_app) {
	BarWebsocketDisconnectAllClients (NULL);
	ck_assert (1);
}
END_TEST

START_TEST(test_websocket_bridge_both_mode_is_web_active) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_BOTH;

	ck_assert (BarWsIsWebActive (&app));
	ck_assert (BarWsSettingsIsWebActive (&app.settings));
	ck_assert (!BarIsWebOnlyMode (&app));
	ck_assert (!BarShouldSkipCliOutput (&app));

	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST(test_websocket_bridge_predicates_and_cli_noops) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;
	app.lockFd = 42;

	ck_assert (!BarIsWebOnlyMode (&app));
	ck_assert (!BarShouldSkipCliOutput (&app));
	ck_assert (!BarWsIsWebActive (&app));
	ck_assert (!BarWsSettingsIsWebActive (&app.settings));
	ck_assert (BarWsInit (&app));
	BarWsDestroy (&app);
	ck_assert (BarWsDaemonize (&app));
	BarWsRemovePidFile (&app);
	ck_assert (BarWsStartPlaybackManager (&app));
	BarWsStopPlaybackManager (&app);
	ck_assert (BarWsAcquireSingletonLock (&app));
	ck_assert_int_eq (app.lockFd, -1);

	app.settings.uiMode = BAR_UI_MODE_WEB;
	ck_assert (BarIsWebOnlyMode (&app));
	ck_assert (BarShouldSkipCliOutput (&app));
	ck_assert (BarWsIsWebActive (&app));
	ck_assert (BarWsSettingsIsWebActive (&app.settings));

	BarSettingsDestroy (&app.settings);
}
END_TEST

/* Wire-compat: progress must be an object, not bare values */
START_TEST (test_wire_compat_progress_is_object)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_web_app (&app, &ctx);
	compatAttach (&app);

	struct json_object *data = json_object_new_object ();
	json_object_object_add (data, "elapsed",    json_object_new_int (42));
	json_object_object_add (data, "duration",   json_object_new_int (240));
	json_object_object_add (data, "percentage", json_object_new_int (17));
	BarSocketIoEmit (&app, BAR_WS_DEST_BROADCAST, "progress", data);
	json_object_put (data);

	TestWsCaptureFlush ();
	ck_assert (strncmp (g_compatBuf, "2[", 2) == 0);
	ck_assert (strstr (g_compatBuf, "\"progress\"") != NULL);
	ck_assert (strstr (g_compatBuf, "\"elapsed\"")  != NULL);
	ck_assert (strstr (g_compatBuf, "\"duration\"") != NULL);

	compatDetach (&app);
	test_teardown_web_app (&app, &ctx);
}
END_TEST

/* Wire-compat: volume must be a bare integer, not wrapped in an object */
START_TEST (test_wire_compat_volume_is_bare_integer)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_web_app (&app, &ctx);
	compatAttach (&app);

	BarSocketIoEmit (&app, BAR_WS_DEST_BROADCAST, "volume", json_object_new_int (50));

	TestWsCaptureFlush ();
	ck_assert (strncmp (g_compatBuf, "2[", 2) == 0);
	ck_assert (strstr (g_compatBuf, "\"volume\"") != NULL);
	ck_assert (strstr (g_compatBuf, "{\"volume\"") == NULL);

	compatDetach (&app);
	test_teardown_web_app (&app, &ctx);
}
END_TEST

/* Wire-compat: stop must have no payload element — no comma after "stop" */
START_TEST (test_wire_compat_stop_has_no_payload)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_web_app (&app, &ctx);
	compatAttach (&app);

	BarSocketIoEmit (&app, BAR_WS_DEST_BROADCAST, "stop", NULL);

	TestWsCaptureFlush ();
	ck_assert (strncmp (g_compatBuf, "2[", 2) == 0);
	const char *stopPos = strstr (g_compatBuf, "\"stop\"");
	ck_assert_ptr_nonnull (stopPos);
	/* No comma after "stop" means no second (payload) element */
	ck_assert_ptr_null (strchr (stopPos, ','));

	compatDetach (&app);
	test_teardown_web_app (&app, &ctx);
}
END_TEST

/* Wire-compat: stations payload must open as an array bracket, not an object */
START_TEST (test_wire_compat_stations_is_array)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_web_app (&app, &ctx);
	compatAttach (&app);

	struct json_object *arr = json_object_new_array ();
	struct json_object *s   = json_object_new_object ();
	json_object_object_add (s, "id",         json_object_new_string ("sid"));
	json_object_object_add (s, "name",       json_object_new_string ("My Station"));
	json_object_object_add (s, "isQuickMix", json_object_new_boolean (0));
	json_object_array_add (arr, s);
	BarSocketIoEmit (&app, BAR_WS_DEST_BROADCAST, "stations", arr);
	json_object_put (arr);

	TestWsCaptureFlush ();
	ck_assert (strncmp (g_compatBuf, "2[", 2) == 0);
	ck_assert (strstr (g_compatBuf, "\"stations\"") != NULL);
	const char *after = strstr (g_compatBuf, "\"stations\"");
	ck_assert_ptr_nonnull (after);
	after += strlen ("\"stations\"");
	while (*after == ',' || *after == ' ') after++;
	ck_assert_int_eq (*after, '[');

	compatDetach (&app);
	test_teardown_web_app (&app, &ctx);
}
END_TEST

/* Create test suite */
/* ------------------------------------------------------------------------
 * Command worker
 * ------------------------------------------------------------------------ */

#define TEST_EXPLAIN "2[\"action\",\"song.explain\"]"
#define TEST_QUERY   "2[\"query\"]"
#define TEST_QUIT    "2[\"action\",\"app.quit\"]"
#define TEST_PING    "2[\"ping\"]"

/* Pandora mock for the worker tests. Every call records its thread and
 * answers EXPLAIN with "why-N" (N = call number). With `block`, the first
 * call waits until the test opens the gate or the call is interrupted: like
 * curl's progress callback it watches its own flag, registered as the
 * interrupt target for the duration of the call (as BarPianoHttpRequest
 * does). The handler holds pianoHttpMutex meanwhile (song actions do). */
typedef struct {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool block, open, interrupted;
	bool stopFromCall;            /* call BarWebsocketWorkerStop inside */
	int calls;
	pthread_t thread;
	_Atomic sig_atomic_t flag;
} TestGate_t;

static TestGate_t test_gate;

static bool test_gate_call (BarApp_t * const app, const PianoRequestType_t type,
		void * const data, PianoReturn_t * const pRet, CURLcode * const wRet) {
	pthread_mutex_lock (&test_gate.lock);
	const int n = ++test_gate.calls;
	test_gate.thread = pthread_self ();
	pthread_cond_broadcast (&test_gate.cond);
	bool aborted = false;
	if (test_gate.block && n == 1) {
		_Atomic sig_atomic_t *prev = BarInterruptGetTarget ();
		BarInterruptSetTarget (&test_gate.flag);
		while (!test_gate.open && atomic_load (&test_gate.flag) == 0) {
			struct timespec ts;
			clock_gettime (CLOCK_REALTIME, &ts);
			ts.tv_nsec += 1000000;
			if (ts.tv_nsec >= 1000000000) {
				ts.tv_sec++;
				ts.tv_nsec -= 1000000000;
			}
			pthread_cond_timedwait (&test_gate.cond, &test_gate.lock, &ts);
		}
		BarInterruptSetTarget (prev);
		aborted = !test_gate.open;
		test_gate.interrupted = aborted;
	}
	const bool stop = test_gate.stopFromCall;
	pthread_mutex_unlock (&test_gate.lock);
	if (stop) {
		BarWebsocketWorkerStop (app); /* on the worker: must not join itself */
	}
	if (aborted) {
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_ABORTED_BY_CALLBACK;
		return false;
	}
	if (type == PIANO_REQUEST_EXPLAIN) {
		char why[32];
		snprintf (why, sizeof (why), "why-%d", n);
		((PianoRequestDataExplain_t *) data)->retExplain = strdup (why);
	}
	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;
	return true;
}

static int test_gate_calls (void) {
	pthread_mutex_lock (&test_gate.lock);
	const int n = test_gate.calls;
	pthread_mutex_unlock (&test_gate.lock);
	return n;
}

/* Wait (bounded) until the mock has been called `calls` times. With FIFO
 * execution, every command queued before that call has finished. */
static void test_gate_wait_calls (int calls) {
	struct timespec deadline;
	clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 5;
	pthread_mutex_lock (&test_gate.lock);
	while (test_gate.calls < calls) {
		const int r = pthread_cond_timedwait (&test_gate.cond, &test_gate.lock,
				&deadline);
		ck_assert_msg (r != ETIMEDOUT, "timed out waiting for call %d (saw %d)",
				calls, test_gate.calls);
	}
	pthread_mutex_unlock (&test_gate.lock);
}

static void test_gate_open (void) {
	pthread_mutex_lock (&test_gate.lock);
	test_gate.open = true;
	pthread_cond_broadcast (&test_gate.cond);
	pthread_mutex_unlock (&test_gate.lock);
}

static PianoStation_t test_worker_station;
static PianoSong_t test_worker_song;

/* What the handlers need besides the transport: the recursive Pandora
 * mutex, a playing song while "connected", player locks for app.quit, and
 * the mock. No interrupt target, as at startup before main sets one. */
static void test_worker_app_begin (BarApp_t *app, bool block) {
	BarUiPianoHttpMutexInit (app);
	pthread_mutex_init (&app->player.decoderLock, NULL);
	pthread_cond_init (&app->player.decoderCond, NULL);
	test_attach_station_and_song (app, &test_worker_station, &test_worker_song);
	app->ph.user.listenerId = (char *) "listener";
	BarInterruptSetTarget (NULL);
	memset (&test_gate, 0, sizeof (test_gate));
	pthread_mutex_init (&test_gate.lock, NULL);
	pthread_cond_init (&test_gate.cond, NULL);
	test_gate.block = block;
	BarUiPianoCallSetTestHook (test_gate_call);
}

static void test_worker_app_end (BarApp_t *app) {
	BarUiPianoCallClearTestHook ();
	pthread_cond_destroy (&test_gate.cond);
	pthread_mutex_destroy (&test_gate.lock);
	app->ph.user.listenerId = NULL;
	app->ph.stations = NULL;
	app->curStation = NULL;
	app->playlist = NULL;
	pthread_cond_destroy (&app->player.decoderCond);
	pthread_mutex_destroy (&app->player.decoderLock);
	BarUiPianoHttpMutexDestroy (app);
}

/* Fake-client context (the test is the service thread) with a real worker */
static void test_setup_worker (BarApp_t *app, BarWsContext_t *ctx, bool block) {
	test_setup_clients (app, ctx);
	test_worker_app_begin (app, block);
}

static void test_teardown_worker (BarApp_t *app, BarWsContext_t *ctx) {
	BarWebsocketWorkerStop (app);
	BarWsCommandQueueFree (ctx->commands);
	ctx->commands = NULL;
	test_worker_app_end (app);
	test_teardown_clients (app, ctx);
}

/* One receive callback with a whole frame from fake client `client` */
static void test_receive (BarApp_t *app, int client, const char *text) {
	BarWsHandleReceive (app, &test_fake_sockets[client], text, strlen (text), true);
}

static int test_fail_create_at;   /* fail the Nth thread creation; 0: never */
static int test_create_calls;

static int test_thread_create (pthread_t *thread, void *(*fn) (void *), void *arg) {
	if (++test_create_calls == test_fail_create_at) {
		return EAGAIN;
	}
	return pthread_create (thread, NULL, fn, arg);
}

START_TEST (test_commands_execute_in_fifo_order)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_worker (&app, &ctx, true);
	test_add_client (&ctx, 0);
	ck_assert (BarWebsocketWorkerStart (&app));

	/* The first command blocks in Pandora; the rest queue up behind it */
	test_receive (&app, 0, TEST_EXPLAIN);
	test_gate_wait_calls (1);
	test_receive (&app, 0, TEST_QUERY);
	test_receive (&app, 0, TEST_EXPLAIN);
	test_receive (&app, 0, TEST_QUERY);
	test_receive (&app, 0, TEST_EXPLAIN);
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 0); /* nothing overtook it */

	test_gate_open ();
	test_gate_wait_calls (3); /* the last command runs: all before it replied */
	BarWebsocketWorkerStop (&app); /* joins after the in-flight command */
	test_drain_all (&ctx);
	static const char *const order[] = {"why-1", "\"process\"", "\"stations\"",
			"why-2", "\"process\"", "\"stations\"", "why-3"};
	ck_assert_uint_eq (test_written_count, 7);
	for (size_t i = 0; i < 7; i++) {
		ck_assert_msg (strstr (test_written_msg[i], order[i]) != NULL,
				"frame %zu is '%s', expected '%s'", i, test_written_msg[i], order[i]);
	}
	/* They ran on the worker, not on this (service) thread */
	ck_assert (!pthread_equal (test_gate.thread, pthread_self ()));

	test_teardown_worker (&app, &ctx);
}
END_TEST

START_TEST (test_requester_disconnect_before_reply_is_dropped)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_worker (&app, &ctx, true);
	test_add_client (&ctx, 0);
	test_add_client (&ctx, 1);
	ck_assert (BarWebsocketWorkerStart (&app));

	test_receive (&app, 0, TEST_EXPLAIN);
	test_gate_wait_calls (1);
	/* A hangs up while its command runs; C takes over A's slot */
	BarWsConnectionRemove (&ctx, &test_fake_sockets[0]);
	test_add_client (&ctx, 2);
	ck_assert_ptr_eq (ctx.connections[0].wsi, &test_fake_sockets[2]);

	test_gate_open ();
	BarWebsocketWorkerStop (&app); /* the reply to A is queued by now */
	ck_assert (!test_gate.interrupted);
	ck_assert_int_eq (test_gate_calls (), 1);
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 0); /* not B, not C */

	test_teardown_worker (&app, &ctx);
}
END_TEST

START_TEST (test_shutdown_empty_queue)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_worker (&app, &ctx, false);
	test_add_client (&ctx, 0);

	/* An idle worker is blocked in Pop; Stop wakes and joins it */
	ck_assert (BarWebsocketWorkerStart (&app));
	ck_assert (ctx.workerStarted);
	BarWebsocketWorkerStop (&app);
	ck_assert (!ctx.workerStarted);
	BarWebsocketWorkerStop (&app); /* idempotent */
	BarWebsocketWorkerStop (NULL);

	/* Admission is closed: a late frame is dropped without a reply */
	test_receive (&app, 0, TEST_QUERY);
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 0);
	ck_assert_int_eq (test_gate_calls (), 0);

	test_teardown_worker (&app, &ctx);
}
END_TEST

START_TEST (test_shutdown_with_queued_commands_frees_them)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_worker (&app, &ctx, true);
	test_add_client (&ctx, 0);
	ck_assert (BarWebsocketWorkerStart (&app));

	test_receive (&app, 0, TEST_EXPLAIN);
	test_gate_wait_calls (1);
	test_receive (&app, 0, TEST_EXPLAIN);
	test_receive (&app, 0, TEST_QUERY);
	test_receive (&app, 0, TEST_EXPLAIN);
	test_receive (&app, 0, TEST_QUERY);

	/* Stop drops the four queued commands (ASan checks they are freed),
	 * interrupts the blocked call and joins */
	BarWebsocketWorkerStop (&app);
	ck_assert (test_gate.interrupted);
	ck_assert_int_eq (test_gate_calls (), 1); /* the queued ones never ran */

	/* Only the interrupted command replied (its error); Stop delivered
	 * nothing: pending replies are abandoned to the transport teardown */
	ck_assert_uint_eq (test_written_count, 0);
	ck_assert_uint_eq (TestWsPendingTake (&ctx, BAR_WS_KEY_NONE), 1);
	const char *reply = TestWsPendingLast (&ctx, BAR_WS_KEY_NONE);
	ck_assert (strstr (reply, "\"error\"") != NULL);
	ck_assert (strstr (reply, "song.explain") != NULL);

	test_teardown_worker (&app, &ctx);
}
END_TEST

START_TEST (test_app_quit_from_worker_signals_owner_not_self_join)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_worker (&app, &ctx, false);
	test_add_client (&ctx, 0);
	ck_assert (BarWebsocketWorkerStart (&app));
	test_gate.stopFromCall = true; /* a Stop from the worker must be refused */

	test_receive (&app, 0, TEST_QUIT);
	test_receive (&app, 0, TEST_EXPLAIN);
	test_receive (&app, 0, TEST_QUERY);
	test_receive (&app, 0, TEST_EXPLAIN);
	test_gate_wait_calls (1);
	/* app.quit ran first and only signalled the lifecycle owner */
	ck_assert (atomic_load (&app.doQuit));
	pthread_mutex_lock (&app.player.lock);
	ck_assert (app.player.doQuit);
	pthread_mutex_unlock (&app.player.lock);

	/* Neither app.quit nor a Stop called on the worker stopped it: the
	 * commands behind them still run */
	test_gate_wait_calls (2);
	ck_assert (ctx.workerStarted);
	test_drain_all (&ctx);
	ck_assert_uint_ge (test_written_count, 3);
	ck_assert (strstr (test_written_msg[0], "why-1") != NULL);
	ck_assert (strstr (test_written_msg[1], "\"process\"") != NULL);
	ck_assert (strstr (test_written_msg[2], "\"stations\"") != NULL);

	/* The owner stops (and joins) it */
	BarWebsocketWorkerStop (&app);
	ck_assert (!ctx.workerStarted);

	test_teardown_worker (&app, &ctx);
}
END_TEST

START_TEST (test_admission_full_returns_localized_error_to_requester)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_web_app (&app, &ctx);
	TestWsCaptureAttach (&app, 2, NULL);
	ctx.commands = BarWsCommandQueueNew (); /* no worker: nothing drains it */
	ck_assert_ptr_nonnull (ctx.commands);
	void *a = &test_ws_cap.sockets[0];
	void *b = &test_ws_cap.sockets[1];

	for (int i = 0; i < WS_CMD_MAX_ITEMS; i++) {
		BarWsHandleReceive (&app, a, TEST_PING, strlen (TEST_PING), true);
	}
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 0);

	/* Full: the requester (only) is told, in the localized copy */
	BarWsHandleReceive (&app, a, TEST_PING, strlen (TEST_PING), true);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 1);
	ck_assert_int_eq (test_ws_cap.frameClient[0], 0);
	json_object *event = json_tokener_parse (test_ws_cap.frames[0] + 1);
	ck_assert_ptr_nonnull (event);
	ck_assert_str_eq (json_object_get_string (json_object_array_get_idx (event, 0)),
			"error");
	json_object *data = json_object_array_get_idx (event, 1);
	json_object *field;
	ck_assert (json_object_object_get_ex (data, "operation", &field));
	ck_assert_str_eq (json_object_get_string (field), "command");
	ck_assert (json_object_object_get_ex (data, "message", &field));
	ck_assert_str_eq (json_object_get_string (field),
			BarL10nGet (&app.l10n, "web.socket.busy"));
	ck_assert_str_eq (json_object_get_string (field),
			"The player is busy. Try again in a moment.");
	json_object_put (event);

	/* The queue is shared: another client is refused too, and told */
	BarWsHandleReceive (&app, b, TEST_PING, strlen (TEST_PING), true);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 2);
	ck_assert_int_eq (test_ws_cap.frameClient[1], 1);
	ck_assert (strstr (test_ws_cap.frames[1], "busy") != NULL);

	/* Room again after one is taken */
	ck_assert (BarWsCommandQueuePop (ctx.commands, NULL, NULL, NULL));
	BarWsHandleReceive (&app, b, TEST_PING, strlen (TEST_PING), true);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 2);

	/* Closed (shutdown) is not overload: dropped without a reply */
	BarWsCommandQueueClose (ctx.commands);
	BarWsHandleReceive (&app, a, TEST_PING, strlen (TEST_PING), true);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 2);

	BarWsCommandQueueFree (ctx.commands);
	ctx.commands = NULL;
	TestWsCaptureDetach (&app);
	test_teardown_web_app (&app, &ctx);
}
END_TEST

START_TEST (test_oversize_request_frame_is_dropped_connection_stays_open)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_add_client (&ctx, 0);
	ctx.commands = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (ctx.commands);
	void *a = &test_fake_sockets[0];
	char *big = malloc (WS_CMD_MAX_REQUEST_BYTES + 1);
	ck_assert_ptr_nonnull (big);
	memset (big, 'x', WS_CMD_MAX_REQUEST_BYTES + 1);

	/* Longer than one request */
	BarWsHandleReceive (&app, a, big, WS_CMD_MAX_REQUEST_BYTES + 1, true);
	/* A message that arrives in parts (as libwebsockets delivers a frame
	 * larger than its receive buffer): every part is dropped, even a tail
	 * that would parse as a command on its own */
	BarWsHandleReceive (&app, a, big, WS_CMD_MAX_REQUEST_BYTES, false);
	BarWsHandleReceive (&app, a, big, 100, false);
	BarWsHandleReceive (&app, a, TEST_QUERY, strlen (TEST_QUERY), true);
	/* The connection stays open and usable: a maximal request still fits */
	BarWsHandleReceive (&app, a, big, WS_CMD_MAX_REQUEST_BYTES, true);
	BarWsHandleReceive (&app, a, TEST_PING, strlen (TEST_PING), true);
	free (big);

	size_t len = 0;
	char *text = NULL;
	ck_assert (BarWsCommandQueuePop (ctx.commands, NULL, &text, &len));
	ck_assert_uint_eq (len, WS_CMD_MAX_REQUEST_BYTES);
	free (text);
	ck_assert (BarWsCommandQueuePop (ctx.commands, NULL, &text, NULL));
	ck_assert_str_eq (text, TEST_PING);
	free (text);
	ck_assert_int_eq (BarWsCommandQueuePush (ctx.commands, 9, "s", 1), BAR_WS_CMD_OK);
	BarWsConnId_t sentinel = 0;
	ck_assert (BarWsCommandQueuePop (ctx.commands, &sentinel, NULL, NULL));
	ck_assert_uint_eq (sentinel, 9);

	ck_assert (!test_conn (&ctx, 0)->pendingClose);
	ck_assert_uint_eq (ctx.numConnections, 1);
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 0);

	BarWsCommandQueueFree (ctx.commands);
	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_worker_start_failure_rolls_back)
{
	/* Directly: nothing is left started or allocated */
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_worker (&app, &ctx, false);
	test_add_client (&ctx, 0);
	test_create_calls = 0;
	test_fail_create_at = 1;
	BarWebsocketSetTestThreadCreateHook (test_thread_create);
	ck_assert (!BarWebsocketWorkerStart (&app));
	ck_assert_ptr_null (ctx.commands);
	ck_assert (!ctx.workerStarted);
	BarWebsocketWorkerStop (&app); /* nothing to stop */
	test_receive (&app, 0, TEST_QUERY); /* no queue: dropped */
	test_drain_all (&ctx);
	ck_assert_uint_eq (test_written_count, 0);
	BarWebsocketSetTestThreadCreateHook (NULL);
	ck_assert (!BarWebsocketWorkerStart (NULL));
	test_teardown_worker (&app, &ctx);

	/* Through Init: the worker (1st thread) or the service thread (2nd,
	 * after the worker started) failing leaves no context, listener, queue
	 * or thread behind; the port can be bound again right away */
	BarApp_t srv;
	memset (&srv, 0, sizeof (srv));
	BarSettingsInit (&srv.settings);
	srv.settings.uiMode = BAR_UI_MODE_WEB;
	srv.settings.websocketHost = strdup ("127.0.0.1");
	srv.settings.websocketPort = test_free_loopback_port ();
	BarWebsocketSetTestThreadCreateHook (test_thread_create);
	for (int failAt = 1; failAt <= 2; failAt++) {
		test_create_calls = 0;
		test_fail_create_at = failAt;
		ck_assert (!BarWebsocketInit (&srv));
		ck_assert_ptr_null (srv.wsContext);
		ck_assert_int_eq (test_create_calls, failAt);
	}
	BarWebsocketSetTestThreadCreateHook (NULL);
	ck_assert (BarWebsocketInit (&srv));
	ck_assert (((BarWsContext_t *) srv.wsContext)->workerStarted);
	BarWebsocketDestroy (&srv);
	ck_assert_ptr_null (srv.wsContext);
	BarSettingsDestroy (&srv.settings);
}
END_TEST

/* Real server for the command tests: the E2E setup plus the worker needs */
static void test_e2e_app_init (BarApp_t *app, bool block) {
	memset (app, 0, sizeof (*app));
	BarSettingsInit (&app->settings);
	app->settings.uiMode = BAR_UI_MODE_WEB;
	app->settings.volumeMode = BAR_VOLUME_MODE_PLAYER;
	app->settings.websocketHost = strdup ("127.0.0.1");
	app->settings.websocketPort = test_free_loopback_port ();
	BarStateInit (app);
	pthread_mutex_init (&app->player.lock, NULL);
	pthread_cond_init (&app->player.cond, NULL);
	pthread_cond_init (&app->player.audioCond, NULL);
	app->player.settings = &app->settings;
	test_worker_app_begin (app, block);
	ck_assert (BarWebsocketInit (app));
}

static void test_e2e_app_destroy (BarApp_t *app) {
	BarWebsocketDestroy (app);
	test_worker_app_end (app);
	pthread_cond_destroy (&app->player.audioCond);
	pthread_cond_destroy (&app->player.cond);
	pthread_mutex_destroy (&app->player.lock);
	BarStateDestroy (app);
	BarSettingsDestroy (&app->settings);
}

static struct lws_context *test_e2e_client_context (void) {
	struct lws_context_creation_info info;
	memset (&info, 0, sizeof (info));
	info.port = CONTEXT_PORT_NO_LISTEN;
	info.protocols = test_e2e_protocols;
	struct lws_context *cctx = lws_create_context (&info);
	ck_assert_ptr_nonnull (cctx);
	return cctx;
}

/* Service the client context until `c` has written every queued frame, then
 * wait for the mock to be called `calls` times. (lws_service ignores its
 * timeout in lws 4.x, so it is only called while client work is pending.) */
static void test_e2e_pump_until_calls (struct lws_context *cctx,
		const TestE2eClient_t *c, int calls) {
	struct timespec start, now;
	clock_gettime (CLOCK_MONOTONIC, &start);
	while (c->outSent < c->outCount) {
		ck_assert (!c->failed && !c->closed);
		clock_gettime (CLOCK_MONOTONIC, &now);
		ck_assert_msg (now.tv_sec - start.tv_sec < 3, "timed out sending");
		lws_service (cctx, 20);
	}
	test_gate_wait_calls (calls);
}

/* The Pi 3 disconnect fix: while a command is stuck in a slow Pandora call
 * (holding pianoHttpMutex), the service thread still accepts connections,
 * sends their initial state and delivers unrelated updates. */
START_TEST (test_slow_command_does_not_block_service_thread)
{
	BarApp_t app;
	test_e2e_app_init (&app, true);
	BarWsContext_t *ctx = app.wsContext;
	struct lws_context *cctx = test_e2e_client_context ();
	TestE2eClient_t a, b;
	memset (&a, 0, sizeof (a));
	memset (&b, 0, sizeof (b));
	test_e2e_connect (cctx, app.settings.websocketPort, &a);
	test_e2e_pump_until (cctx, &a, 1, &b, 0);

	test_e2e_send (&a, TEST_EXPLAIN);
	test_e2e_pump_until_calls (cctx, &a, 1);
	/* Blocked on the worker, which holds pianoHttpMutex */
	ck_assert (pthread_equal (test_gate.thread, ctx->worker));
	ck_assert (!pthread_equal (test_gate.thread, ctx->thread));
	ck_assert_int_eq (pthread_mutex_trylock (&app.pianoHttpMutex), EBUSY);

	/* A second client connects and gets its state; an update reaches both */
	test_e2e_connect (cctx, app.settings.websocketPort, &b);
	test_e2e_pump_until (cctx, &a, 1, &b, 1);
	test_e2e_expect (&b, 0, "\"process\"");
	ck_assert (BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
			strdup ("2[\"unrelated\"]")));
	test_e2e_pump_until (cctx, &a, 2, &b, 2);
	test_e2e_expect (&a, 1, "\"unrelated\"");
	test_e2e_expect (&b, 1, "\"unrelated\"");
	ck_assert_int_eq (test_gate_calls (), 1); /* still blocked throughout */

	/* Released: the reply reaches its requester only */
	test_gate_open ();
	test_e2e_pump_until (cctx, &a, 3, &b, 2);
	test_e2e_expect (&a, 2, "why-1");
	ck_assert (BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
			strdup ("2[\"end\"]")));
	test_e2e_pump_until (cctx, &a, 4, &b, 3);
	test_e2e_expect (&b, 2, "\"end\"");
	ck_assert_uint_eq (a.count, 4);
	ck_assert_uint_eq (b.count, 3);

	lws_context_destroy (cctx);
	test_e2e_free (&a);
	test_e2e_free (&b);
	test_e2e_app_destroy (&app);
}
END_TEST

START_TEST (test_command_replies_reach_only_requester)
{
	BarApp_t app;
	test_e2e_app_init (&app, false);
	struct lws_context *cctx = test_e2e_client_context ();
	TestE2eClient_t a, b;
	memset (&a, 0, sizeof (a));
	memset (&b, 0, sizeof (b));
	test_e2e_connect (cctx, app.settings.websocketPort, &a);
	test_e2e_connect (cctx, app.settings.websocketPort, &b);
	test_e2e_pump_until (cctx, &a, 1, &b, 1);

	test_e2e_send (&a, TEST_QUERY);
	test_e2e_send (&b, TEST_EXPLAIN);
	test_e2e_pump_until (cctx, &a, 3, &b, 2);
	test_e2e_expect (&a, 1, "\"process\"");
	test_e2e_expect (&a, 2, "\"stations\"");
	test_e2e_expect (&b, 1, "why-1");
	/* Anything misrouted would arrive before this later broadcast */
	ck_assert (BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
			strdup ("2[\"end\"]")));
	test_e2e_pump_until (cctx, &a, 4, &b, 3);
	test_e2e_expect (&a, 3, "\"end\"");
	test_e2e_expect (&b, 2, "\"end\"");
	ck_assert_uint_eq (a.count, 4);
	ck_assert_uint_eq (b.count, 3);

	lws_context_destroy (cctx);
	test_e2e_free (&a);
	test_e2e_free (&b);
	test_e2e_app_destroy (&app);
}
END_TEST

START_TEST (test_shutdown_with_blocked_http_interrupts_and_joins)
{
	BarApp_t app;
	test_e2e_app_init (&app, true);
	struct lws_context *cctx = test_e2e_client_context ();
	TestE2eClient_t a, none;
	memset (&a, 0, sizeof (a));
	memset (&none, 0, sizeof (none));
	test_e2e_connect (cctx, app.settings.websocketPort, &a);
	test_e2e_pump_until (cctx, &a, 1, &none, 0);
	test_e2e_send (&a, TEST_EXPLAIN);
	test_e2e_pump_until_calls (cctx, &a, 1);

	/* Destroy interrupts the stuck call through the interrupt target, joins
	 * the worker, then the service thread, and frees the transport */
	BarWebsocketDestroy (&app);
	ck_assert (test_gate.interrupted);
	ck_assert_ptr_null (app.wsContext);

	lws_context_destroy (cctx);
	test_e2e_free (&a);
	test_e2e_app_destroy (&app); /* Destroy again is a no-op */
}
END_TEST

START_TEST (test_repeated_start_stop)
{
	/* The worker alone, on one context */
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_worker (&app, &ctx, false);
	test_add_client (&ctx, 0);
	for (int cycle = 1; cycle <= 3; cycle++) {
		ck_assert (BarWebsocketWorkerStart (&app));
		ck_assert (!BarWebsocketWorkerStart (&app)); /* already running */
		test_receive (&app, 0, TEST_EXPLAIN);
		test_gate_wait_calls (cycle);
		BarWebsocketWorkerStop (&app);
		test_drain_all (&ctx);
		ck_assert_uint_eq (test_written_count, (size_t) cycle);
		char why[16];
		snprintf (why, sizeof (why), "why-%d", cycle);
		ck_assert (strstr (test_written_msg[cycle - 1], why) != NULL);
	}
	test_teardown_worker (&app, &ctx);

	/* The whole server, each time with a command round trip */
	for (int cycle = 0; cycle < 3; cycle++) {
		BarApp_t srv;
		test_e2e_app_init (&srv, false);
		struct lws_context *cctx = test_e2e_client_context ();
		TestE2eClient_t a, none;
		memset (&a, 0, sizeof (a));
		memset (&none, 0, sizeof (none));
		test_e2e_connect (cctx, srv.settings.websocketPort, &a);
		test_e2e_pump_until (cctx, &a, 1, &none, 0);
		test_e2e_send (&a, TEST_QUERY);
		test_e2e_pump_until (cctx, &a, 3, &none, 0);
		test_e2e_expect (&a, 1, "\"process\"");
		lws_context_destroy (cctx);
		test_e2e_free (&a);
		test_e2e_app_destroy (&srv);
	}
}
END_TEST

/* Real commands from a real client. An oversize frame (a valid query padded
 * past the limit, which libwebsockets delivers in parts) and a message sent
 * in two fragments (the second a valid query on its own) are dropped
 * without closing the connection; later commands are answered in order. */
START_TEST (test_end_to_end_commands_over_lws)
{
	BarApp_t app;
	test_e2e_app_init (&app, false);
	struct lws_context *cctx = test_e2e_client_context ();
	TestE2eClient_t a, none;
	memset (&a, 0, sizeof (a));
	memset (&none, 0, sizeof (none));
	test_e2e_connect (cctx, app.settings.websocketPort, &a);
	test_e2e_pump_until (cctx, &a, 1, &none, 0);

	char oversize[WS_CMD_MAX_REQUEST_BYTES + 16];
	memset (oversize, ' ', sizeof (oversize));
	memcpy (oversize, "2[\"query\"", 9);
	memcpy (oversize + sizeof (oversize) - 2, "]", 2);
	test_e2e_send (&a, oversize);
	test_e2e_send_flags (&a, "2[\"pad\",", LWS_WRITE_TEXT | LWS_WRITE_NO_FIN);
	test_e2e_send_flags (&a, TEST_QUERY, LWS_WRITE_CONTINUATION);
	test_e2e_send (&a, TEST_EXPLAIN);
	test_e2e_send (&a, TEST_QUERY);
	test_e2e_pump_until (cctx, &a, 4, &none, 0);
	test_e2e_expect (&a, 1, "why-1");
	test_e2e_expect (&a, 2, "\"process\"");
	test_e2e_expect (&a, 3, "\"stations\"");
	ck_assert (BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
			strdup ("2[\"end\"]")));
	test_e2e_pump_until (cctx, &a, 5, &none, 0);
	test_e2e_expect (&a, 4, "\"end\"");
	ck_assert_uint_eq (a.count, 5);
	ck_assert (!a.closed);

	lws_context_destroy (cctx);
	test_e2e_free (&a);
	test_e2e_app_destroy (&app);
}
END_TEST

Suite *websocket_suite(void) {
	Suite *s;
	TCase *tc_core;
	TCase *tc_compat;
	
	s = suite_create("WebSocket");
	tc_core = tcase_create("Core");
	
	tcase_add_test(tc_core, test_websocket_init_null);
	tcase_add_test(tc_core, test_websocket_init_destroy_stops_when_app_quits);
	tcase_add_test(tc_core, test_websocket_init_fails_cleanly_when_bind_fails);
	tcase_add_test(tc_core, test_end_to_end_delivery_order_over_lws);
	tcase_add_test(tc_core, test_websocket_destroy_null);
	tcase_add_test(tc_core, test_websocket_elapsed_null);
	tcase_add_test(tc_core, test_websocket_broadcast_null);
	tcase_add_test(tc_core, test_websocket_bridge_singleton_lock_null_app);
	tcase_add_test(tc_core, test_websocket_bridge_broadcast_process_queues_snapshot_payload);
	tcase_add_test(tc_core, test_websocket_bridge_broadcasts_real_player_state_events);
	tcase_add_test(tc_core, test_websocket_bridge_start_stop_and_paused_progress);
	tcase_add_test(tc_core, test_websocket_bridge_progress_skips_duplicate_elapsed);
	tcase_add_test(tc_core, test_websocket_bridge_disconnect_all_clients_with_context);
	tcase_add_test (tc_core, test_send_broadcast_reaches_all_clients);
	tcase_add_test (tc_core, test_send_to_id_reaches_only_that_client);
	tcase_add_test (tc_core, test_send_to_reused_slot_does_not_reach_new_client);
	tcase_add_test (tc_core, test_send_targeted_and_broadcast_do_not_replace_each_other);
	tcase_add_test (tc_core, test_send_targeted_state_keeps_pending_progress_broadcast);
	tcase_add_test (tc_core, test_send_unknown_id_is_dropped_and_bad_args_fail);
	tcase_add_test (tc_core, test_reused_slot_does_not_receive_old_reply);
	tcase_add_test (tc_core, test_enqueue_does_not_write);
	tcase_add_test (tc_core, test_writable_callback_writes_one_frame);
	tcase_add_test (tc_core, test_short_write_counts_frame_as_sent);
	tcase_add_test (tc_core, test_write_failure_closes_connection);
	tcase_add_test (tc_core, test_nonwritable_client_keeps_pending_frame);
	tcase_add_test (tc_core, test_close_frees_pending);
	tcase_add_test (tc_core, test_blocked_client_does_not_delay_writable_client);
	tcase_add_test (tc_core, test_overflow_closes_only_that_client);
	tcase_add_test (tc_core, test_ingress_full_closes_all_clients);
	tcase_add_test (tc_core, test_oversize_frame_is_rejected_without_closing_clients);
	tcase_add_test (tc_core, test_large_stations_reply_does_not_close_clients);
	tcase_add_test (tc_core, test_frame_at_limit_is_delivered_to_empty_client);
	tcase_add_test (tc_core, test_disconnect_all_clients_closes_and_frees_pending);
	tcase_add_test (tc_core, test_start_then_playstate_both_delivered);
	tcase_add_test (tc_core, test_stop_then_start_both_delivered);
	tcase_add_test (tc_core, test_progress_for_old_song_dropped_after_start);
	tcase_add_test (tc_core, test_progress_sampled_before_start_but_pushed_after_is_dropped);
	tcase_add_test (tc_core, test_bridge_progress_resumes_after_song_start);
	tcase_add_test (tc_core, test_receive_from_untracked_client_is_dropped);
	tcase_add_test(tc_core, test_websocket_bridge_unicast_helpers_and_errors);
	tcase_add_test(tc_core, test_websocket_bridge_guarded_broadcasts_noop_without_web_context);
	tcase_add_test(tc_core, test_websocket_bridge_upcoming_play_state_and_release_lock);
	tcase_add_test (tc_core, test_websocket_play_state_waits_for_queued_control);
	tcase_add_test(tc_core, test_websocket_bridge_upcoming_skips_unknown_requester);
	tcase_add_test(tc_core, test_websocket_disconnect_all_clients_null_app);
	tcase_add_test(tc_core, test_websocket_bridge_system_volume_mode_broadcast);
	tcase_add_test(tc_core, test_websocket_schedule_volume_broadcast_sets_pending_flag);
	tcase_add_test(tc_core, test_websocket_immediate_and_delayed_volume_read_current_player_value);
	tcase_add_test(tc_core, test_websocket_bridge_both_mode_is_web_active);
	tcase_add_test(tc_core, test_websocket_bridge_print_helpers_and_input_setup);
	tcase_add_test(tc_core, test_websocket_bridge_print_bind_all_and_startup_info);
#ifdef HAVE_DEBUGLOG
	tcase_add_test(tc_core, test_lws_log_message_strips_prefix_and_handles_null);
	tcase_add_test(tc_core, test_lws_log_emit_routes_by_level);
	tcase_add_test(tc_core, test_websocket_init_destroy_emits_debug_lifecycle_logs);
#endif
	tcase_add_test(tc_core, test_websocket_bridge_predicates_and_cli_noops);
	
	suite_add_tcase(s, tc_core);

	TCase *tc_worker = tcase_create ("worker");
	tcase_add_test (tc_worker, test_slow_command_does_not_block_service_thread);
	tcase_add_test (tc_worker, test_command_replies_reach_only_requester);
	tcase_add_test (tc_worker, test_commands_execute_in_fifo_order);
	tcase_add_test (tc_worker, test_requester_disconnect_before_reply_is_dropped);
	tcase_add_test (tc_worker, test_worker_start_failure_rolls_back);
	tcase_add_test (tc_worker, test_shutdown_empty_queue);
	tcase_add_test (tc_worker, test_shutdown_with_queued_commands_frees_them);
	tcase_add_test (tc_worker, test_shutdown_with_blocked_http_interrupts_and_joins);
	tcase_add_test (tc_worker, test_app_quit_from_worker_signals_owner_not_self_join);
	tcase_add_test (tc_worker, test_repeated_start_stop);
	tcase_add_test (tc_worker, test_admission_full_returns_localized_error_to_requester);
	tcase_add_test (tc_worker, test_oversize_request_frame_is_dropped_connection_stays_open);
	tcase_add_test (tc_worker, test_end_to_end_commands_over_lws);
	tcase_set_timeout (tc_worker, 30);
	suite_add_tcase (s, tc_worker);

	tc_compat = tcase_create ("wire_compat");
	tcase_add_test (tc_compat, test_wire_compat_progress_is_object);
	tcase_add_test (tc_compat, test_wire_compat_volume_is_bare_integer);
	tcase_add_test (tc_compat, test_wire_compat_stop_has_no_payload);
	tcase_add_test (tc_compat, test_wire_compat_stations_is_array);
	suite_add_tcase (s, tc_compat);
	
	return s;
}
