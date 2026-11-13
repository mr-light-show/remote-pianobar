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
#include <json-c/json.h>
#include <libwebsockets.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

/* Wire-compat capture buffer */
static char g_compatBuf[8192];
static void compatCapture (const char *msg, size_t len) {
	size_t copy = len < sizeof (g_compatBuf) - 1 ? len : sizeof (g_compatBuf) - 1;
	memcpy (g_compatBuf, msg, copy);
	g_compatBuf[copy] = '\0';
}

/* Route real deliveries (BarWsDeliver) into g_compatBuf: one fake client. */
static char g_compatSocket;
static int compatWriteFrame (void *wsi, unsigned char *buf, size_t len) {
	(void) wsi;
	compatCapture ((const char *) buf, len);
	return (int) len;
}

static BarWsConnId_t compatAttach (BarWsContext_t *ctx) {
	ctx->maxConnections = 1;
	ctx->connections = calloc (1, sizeof (BarWsConnection_t));
	ck_assert_ptr_nonnull (ctx->connections);
	ctx->writeFrame = compatWriteFrame;
	g_compatBuf[0] = '\0';
	BarWsConnId_t id = BarWsConnectionAdd (ctx, &g_compatSocket, "socketio");
	ck_assert_uint_ne (id, BAR_WS_DEST_BROADCAST);
	return id;
}

static void compatDetach (BarWsContext_t *ctx) {
	free (ctx->connections);
	ctx->connections = NULL;
	ctx->maxConnections = 0;
	ctx->numConnections = 0;
	ctx->writeFrame = NULL;
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
	/* The new API: NULL app frees message and returns */
	char *msg = strdup ("2[\"test\"]");
	BarWebsocketBroadcastSocketIoMessage (NULL, BUCKET_STATE, msg);
	/* If we get here without crashing, test passes */
	ck_assert (1);
}
END_TEST

START_TEST(test_websocket_bridge_singleton_lock_null_app) {
	ck_assert(BarWsAcquireSingletonLock(NULL));
}
END_TEST

static void test_ws_context_init_buckets (BarWsContext_t *ctx) {
	for (int i = 0; i < BUCKET_COUNT; i++) {
		pthread_mutex_init (&ctx->buckets[i].mutex, NULL);
	}
}

static void test_ws_context_destroy_buckets (BarWsContext_t *ctx) {
	for (int i = 0; i < BUCKET_COUNT; i++) {
		if (ctx->buckets[i].message != NULL) {
			BarWsMessageFree (ctx->buckets[i].message);
			ctx->buckets[i].message = NULL;
		}
		pthread_mutex_destroy (&ctx->buckets[i].mutex);
	}
}

static const char *test_bucket_payload (BarWsContext_t *ctx, BarWsBucketType_t bucket) {
	ck_assert_ptr_nonnull (ctx->buckets[bucket].message);
	ck_assert_ptr_nonnull (ctx->buckets[bucket].message->data);
	return (const char *)ctx->buckets[bucket].message->data;
}

static void test_setup_web_app (BarApp_t *app, BarWsContext_t *ctx) {
	memset (app, 0, sizeof (*app));
	memset (ctx, 0, sizeof (*ctx));
	BarSettingsInit (&app->settings);
	app->settings.uiMode = BAR_UI_MODE_WEB;
	app->settings.volumeMode = BAR_VOLUME_MODE_PLAYER;
	app->settings.volume = 40;
	app->wsContext = ctx;
	test_ws_context_init_buckets (ctx);
	pthread_mutex_init (&ctx->connMutex, NULL);
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
	test_ws_context_destroy_buckets (ctx);
	pthread_mutex_destroy (&ctx->connMutex);
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

	const char *payload = test_bucket_payload (&ctx, BUCKET_STATE);
	ck_assert (strstr (payload, "\"process\"") != NULL);
	ck_assert (strstr (payload, "\"song\"") != NULL);
	ck_assert (strstr (payload, "songStationName") != NULL);
	ck_assert (strstr (payload, "Display One") != NULL);

	test_teardown_web_app (&app, &ctx);
}
END_TEST

START_TEST(test_websocket_bridge_broadcasts_real_player_state_buckets) {
	BarApp_t app;
	BarWsContext_t ctx;
	PianoSong_t song;
	PianoStation_t station;
	test_setup_web_app (&app, &ctx);
	test_attach_station_and_song (&app, &station, &song);

	BarWsBroadcastVolume (&app);
	ck_assert (strstr (test_bucket_payload (&ctx, BUCKET_VOLUME), "\"volume\"") != NULL);
	ck_assert (strstr (test_bucket_payload (&ctx, BUCKET_VOLUME), "40") != NULL);
	ck_assert (BarPlayerSetVolume (&app.player, 41));
	BarWsBroadcastVolume (&app);
	ck_assert (strstr (test_bucket_payload (&ctx, BUCKET_VOLUME), "41") != NULL);

	BarWsBroadcastStations (&app);
	ck_assert (strstr (test_bucket_payload (&ctx, BUCKET_STATIONS), "\"stations\"") != NULL);
	ck_assert (strstr (test_bucket_payload (&ctx, BUCKET_STATIONS), "Display One") != NULL);

	BarPlayerSetMode (&app.player, PLAYER_PLAYING);
	pthread_mutex_lock (&app.player.lock);
	app.player.songPlayed = 45;
	app.player.songDuration = 180;
	pthread_mutex_unlock (&app.player.lock);
	BarWsBroadcastProgress (&app);
	ck_assert (strstr (test_bucket_payload (&ctx, BUCKET_PROGRESS), "\"progress\"") != NULL);
	ck_assert (strstr (test_bucket_payload (&ctx, BUCKET_PROGRESS), "\"elapsed\"") != NULL);
	ck_assert (strstr (test_bucket_payload (&ctx, BUCKET_PROGRESS), "45") != NULL);
	pthread_mutex_lock (&app.player.lock);
	app.player.songPlayed = 46;
	app.player.songDuration = 0;
	pthread_mutex_unlock (&app.player.lock);
	BarWsBroadcastProgress (&app);
	ck_assert (strstr (test_bucket_payload (&ctx, BUCKET_PROGRESS), "\"percentage\"") != NULL);

	BarWsBroadcastSongStart (&app);
	ck_assert (strstr (test_bucket_payload (&ctx, BUCKET_STATE), "\"start\"") != NULL);
	ck_assert_ptr_null (ctx.buckets[BUCKET_PROGRESS].message);

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
	ck_assert (strstr (test_bucket_payload (&ctx, BUCKET_STATE), "\"start\"") != NULL);
	ck_assert (strstr (test_bucket_payload (&ctx, BUCKET_STATE), "Song One") != NULL);

	BarWsBroadcastSongStop (&app);
	ck_assert (strstr (test_bucket_payload (&ctx, BUCKET_STATE), "\"stop\"") != NULL);

	BarPlayerSetMode (&app.player, PLAYER_PLAYING);
	pthread_mutex_lock (&app.player.lock);
	app.player.doPause = true;
	app.player.songPlayed = 60;
	app.player.songDuration = 180;
	pthread_mutex_unlock (&app.player.lock);
	BarWsBroadcastProgress (&app);
	ck_assert_ptr_null (ctx.buckets[BUCKET_PROGRESS].message);

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
	ck_assert_ptr_nonnull (ctx.buckets[BUCKET_PROGRESS].message);
	BarWsMessageFree (ctx.buckets[BUCKET_PROGRESS].message);
	ctx.buckets[BUCKET_PROGRESS].message = NULL;

	BarWsBroadcastProgress (&app);
	ck_assert_ptr_null (ctx.buckets[BUCKET_PROGRESS].message);

	test_teardown_web_app (&app, &ctx);
}
END_TEST

/* Connection-table fixture: tracks fake clients without real sockets. */
#define TEST_MAX_CLIENTS 4

static void test_setup_clients (BarApp_t *app, BarWsContext_t *ctx) {
	test_setup_web_app (app, ctx);
	ctx->maxConnections = TEST_MAX_CLIENTS;
	ctx->connections = calloc (TEST_MAX_CLIENTS, sizeof (BarWsConnection_t));
	ck_assert_ptr_nonnull (ctx->connections);
}

static void test_teardown_clients (BarApp_t *app, BarWsContext_t *ctx) {
	free (ctx->connections);
	test_teardown_web_app (app, ctx);
}

static char test_fake_sockets[TEST_MAX_CLIENTS];

/* Records which fake sockets a delivery wrote to, and what. */
static void *test_written_wsi[16];
static char test_written_msg[16][64];
static size_t test_written_count;

static int test_write_frame (void *wsi, unsigned char *buf, size_t len) {
	ck_assert_uint_lt (test_written_count, 16);
	test_written_wsi[test_written_count] = wsi;
	snprintf (test_written_msg[test_written_count], 64, "%.*s", (int) len, buf);
	test_written_count++;
	return (int) len;
}

static void test_clients_hook_writer (BarWsContext_t *ctx) {
	test_written_count = 0;
	ctx->writeFrame = test_write_frame;
}

/* Run the service-thread side: drain a bucket and deliver it. */
static void test_drain_bucket (BarWsContext_t *ctx, BarWsBucketType_t bucket) {
	BarWsMessage_t *msg = ctx->buckets[bucket].message;
	ctx->buckets[bucket].message = NULL;
	ck_assert_ptr_nonnull (msg);
	BarWsDeliver (ctx, msg->data, msg->dataLen, msg->dest);
	BarWsMessageFree (msg);
}

START_TEST (test_send_broadcast_reaches_all_clients)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_clients_hook_writer (&ctx);
	ck_assert_uint_ne (BarWsConnectionAdd (&ctx, &test_fake_sockets[0], "socketio"), 0);
	ck_assert_uint_ne (BarWsConnectionAdd (&ctx, &test_fake_sockets[1], "socketio"), 0);

	ck_assert (BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BUCKET_VOLUME,
			strdup ("2[\"volume\",40]")));
	ck_assert_uint_eq (test_written_count, 0); /* queued, not yet written */
	test_drain_bucket (&ctx, BUCKET_VOLUME);

	ck_assert_uint_eq (test_written_count, 2);
	ck_assert_ptr_eq (test_written_wsi[0], &test_fake_sockets[0]);
	ck_assert_ptr_eq (test_written_wsi[1], &test_fake_sockets[1]);
	ck_assert_str_eq (test_written_msg[0], "2[\"volume\",40]");

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_send_to_id_reaches_only_that_client)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_clients_hook_writer (&ctx);
	BarWsConnId_t a = BarWsConnectionAdd (&ctx, &test_fake_sockets[0], "socketio");
	BarWsConnId_t b = BarWsConnectionAdd (&ctx, &test_fake_sockets[1], "socketio");
	ck_assert_uint_ne (a, 0);
	ck_assert_uint_ne (b, 0);
	ck_assert_uint_ne (a, b);

	ck_assert (BarWebsocketSend (&app, b, BUCKET_STATIONS, strdup ("2[\"stations\"]")));
	ck_assert_uint_eq (test_written_count, 1);
	ck_assert_ptr_eq (test_written_wsi[0], &test_fake_sockets[1]);
	ck_assert_str_eq (test_written_msg[0], "2[\"stations\"]");
	/* Delivered directly: nothing parked in a bucket */
	for (int i = 0; i < BUCKET_COUNT; i++) {
		ck_assert_ptr_null (ctx.buckets[i].message);
	}

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_send_to_reused_slot_does_not_reach_new_client)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_clients_hook_writer (&ctx);
	BarWsConnId_t a = BarWsConnectionAdd (&ctx, &test_fake_sockets[0], "socketio");
	ck_assert_uint_eq (a, 1);
	BarWsConnectionRemove (&ctx, &test_fake_sockets[0]);
	/* Client B reuses slot 0 */
	BarWsConnId_t b = BarWsConnectionAdd (&ctx, &test_fake_sockets[1], "socketio");
	ck_assert_uint_eq (b, 2);
	ck_assert_ptr_eq (ctx.connections[0].wsi, &test_fake_sockets[1]);

	/* Send-time resolution rejects the stale ID */
	ck_assert (!BarWebsocketSend (&app, a, BUCKET_STATE, strdup ("2[\"reply\"]")));
	ck_assert_uint_eq (test_written_count, 0);
	/* Delivery-time filter also skips it (message queued before the swap) */
	BarWsDeliver (&ctx, "2[\"reply\"]", 11, a);
	ck_assert_uint_eq (test_written_count, 0);
	/* B itself is still addressable */
	ck_assert (BarWebsocketSend (&app, b, BUCKET_STATE, strdup ("2[\"reply\"]")));
	ck_assert_uint_eq (test_written_count, 1);
	ck_assert_ptr_eq (test_written_wsi[0], &test_fake_sockets[1]);

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_send_targeted_and_broadcast_do_not_replace_each_other)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_clients_hook_writer (&ctx);
	BarWsConnId_t a = BarWsConnectionAdd (&ctx, &test_fake_sockets[0], "socketio");
	BarWsConnectionAdd (&ctx, &test_fake_sockets[1], "socketio");

	/* Broadcast pending in a bucket, then a targeted reply in the same bucket */
	ck_assert (BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BUCKET_STATIONS,
			strdup ("2[\"bcast\"]")));
	ck_assert (BarWebsocketSend (&app, a, BUCKET_STATIONS, strdup ("2[\"reply\"]")));
	ck_assert_uint_eq (test_written_count, 1);
	ck_assert_str_eq (test_written_msg[0], "2[\"reply\"]");
	ck_assert_ptr_eq (test_written_wsi[0], &test_fake_sockets[0]);
	/* The pending broadcast survived and still reaches both clients */
	test_drain_bucket (&ctx, BUCKET_STATIONS);
	ck_assert_uint_eq (test_written_count, 3);
	ck_assert_str_eq (test_written_msg[1], "2[\"bcast\"]");
	ck_assert_str_eq (test_written_msg[2], "2[\"bcast\"]");

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_send_targeted_state_keeps_pending_progress_broadcast)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_clients_hook_writer (&ctx);
	BarWsConnId_t a = BarWsConnectionAdd (&ctx, &test_fake_sockets[0], "socketio");
	ctx.progress.lastBroadcast = 7;

	ck_assert (BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BUCKET_PROGRESS,
			strdup ("2[\"progress\"]")));
	ck_assert (BarWebsocketSend (&app, a, BUCKET_STATE, strdup ("2[\"start\"]")));
	/* Targeted STATE must not clear everyone's pending PROGRESS */
	ck_assert_ptr_nonnull (ctx.buckets[BUCKET_PROGRESS].message);
	ck_assert_uint_eq (ctx.progress.lastBroadcast, 7);

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_receive_from_untracked_client_is_dropped)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_clients_hook_writer (&ctx);
	BarWsConnectionAdd (&ctx, &test_fake_sockets[0], "socketio");
	BarWsConnectionAdd (&ctx, &test_fake_sockets[1], "socketio");

	/* Live but untracked (e.g. the table was full): its query must not turn
	 * into a broadcast, nor reach anyone else */
	BarWsHandleReceive (&app, &test_fake_sockets[2], "0", 1);
	BarWsHandleReceive (&app, &test_fake_sockets[2], "2[\"query\"]", 11);
	ck_assert_uint_eq (test_written_count, 0);
	/* Empty or missing frames are ignored, even from a tracked client */
	BarWsHandleReceive (&app, &test_fake_sockets[1], "", 0);
	BarWsHandleReceive (&app, &test_fake_sockets[1], NULL, 5);
	ck_assert_uint_eq (test_written_count, 0);
	/* Removing a socket that was never tracked leaves the table alone */
	BarWsConnectionRemove (&ctx, &test_fake_sockets[2]);
	ck_assert_uint_eq (ctx.numConnections, 2);

	/* A tracked client still gets its reply, and only it */
	BarWsHandleReceive (&app, &test_fake_sockets[1], "2[\"query\"]", 11);
	ck_assert_uint_eq (test_written_count, 2);
	ck_assert_ptr_eq (test_written_wsi[0], &test_fake_sockets[1]);
	ck_assert_ptr_eq (test_written_wsi[1], &test_fake_sockets[1]);
	ck_assert (strstr (test_written_msg[0], "process") != NULL);
	ck_assert (strstr (test_written_msg[1], "stations") != NULL);

	test_teardown_clients (&app, &ctx);
}
END_TEST

START_TEST (test_send_unknown_id_frees_message_and_returns_false)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_clients (&app, &ctx);
	test_clients_hook_writer (&ctx);
	BarWsConnectionAdd (&ctx, &test_fake_sockets[0], "socketio");

	/* Run under ASan/valgrind to catch a leak; NULL args must not crash. */
	ck_assert (!BarWebsocketSend (&app, 999, BUCKET_STATE, strdup ("x")));
	ck_assert (!BarWebsocketSend (NULL, BAR_WS_DEST_BROADCAST, BUCKET_STATE, strdup ("x")));
	ck_assert (!BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BUCKET_STATE, NULL));
	app.wsContext = NULL;
	ck_assert (!BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BUCKET_STATE, strdup ("x")));
	app.wsContext = &ctx;
	ck_assert_uint_eq (test_written_count, 0);
	for (int i = 0; i < BUCKET_COUNT; i++) {
		ck_assert_ptr_null (ctx.buckets[i].message);
	}

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

	BarWsConnId_t requester = compatAttach (&ctx);

	BarWsEmitExplanationTo (&app, requester + 1, "ignored for an unknown client");
	ck_assert_str_eq (g_compatBuf, "");

	BarWsEmitExplanationTo (&app, requester, "Because you like rock.");
	ck_assert (strstr (g_compatBuf, "Because you like rock.") != NULL);

	g_compatBuf[0] = '\0';
	BarWsEmitErrorTo (&app, requester, "song.explain", "Failed to receive explanation");
	ck_assert (strstr (g_compatBuf, "\"error\"") != NULL);
	ck_assert (strstr (g_compatBuf, "song.explain") != NULL);

	g_compatBuf[0] = '\0';
	BarWsEmitError (&app, "station.change", "Station not found");
	ck_assert (strstr (g_compatBuf, "\"error\"") != NULL);
	ck_assert (strstr (g_compatBuf, "Station not found") != NULL);

	g_compatBuf[0] = '\0';
	BarWsEmitErrorEx (&app, "station.change", "Station not found", "abc-123");
	ck_assert (strstr (g_compatBuf, "abc-123") != NULL);

	g_compatBuf[0] = '\0';
	BarWsBroadcastPandoraDisconnected (&app, "session_invalid");
	{
		const char *payload = test_bucket_payload (&ctx, BUCKET_STATE);
		ck_assert (strstr (payload, "pandora.disconnected") != NULL);
		ck_assert (strstr (payload, "session_invalid") != NULL);
	}
	BarWsBroadcastPandoraDisconnected (&app, NULL);
	{
		const char *payload = test_bucket_payload (&ctx, BUCKET_STATE);
		ck_assert (strstr (payload, "pandora.disconnected") != NULL);
		ck_assert (strstr (payload, "unknown") != NULL);
	}

	compatDetach (&ctx);
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

	const BarPlayerPlayStateSnapshot snapshot = {true, 1};
	BarWsBroadcastPlayState (NULL);
	BarWsBroadcastPlayState (&app);
	BarWsBroadcastPlayStateSnapshot (NULL, &snapshot);
	BarWsBroadcastPlayStateSnapshot (&app, &snapshot);
	BarWsBroadcastPandoraDisconnected (NULL, "ignored");
	BarWsBroadcastPandoraDisconnected (&app, "ignored");
	ck_assert_ptr_null (ctx.buckets[BUCKET_STATE].message);

	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarWsBroadcastPlayStateSnapshot (&app, NULL);
	ck_assert_ptr_null (ctx.buckets[BUCKET_STATE].message);
	app.wsContext = NULL;
	BarWsBroadcastPlayState (&app);
	BarWsBroadcastPlayStateSnapshot (&app, &snapshot);
	BarWsBroadcastPandoraDisconnected (&app, "ignored");
	ck_assert_ptr_null (ctx.buckets[BUCKET_STATE].message);

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

	BarWsConnId_t requester = compatAttach (&ctx);
	BarWsEmitUpcomingTo (&app, requester, &song, 1);
	ck_assert (strstr (g_compatBuf, "Up Next") != NULL);

	g_compatBuf[0] = '\0';
	BarWsBroadcastPlayState (&app);
	{
		const char *payload = test_bucket_payload (&ctx, BUCKET_STATE);
		ck_assert (strstr (payload, "playState") != NULL);
	}

	app.lockFd = dup (STDERR_FILENO);
	ck_assert_int_ge (app.lockFd, 0);
	BarWsReleaseSingletonLock (&app);
	ck_assert_int_eq (app.lockFd, -1);

	compatDetach (&ctx);
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
	ck_assert_ptr_null (ctx.buckets[BUCKET_STATE].message);
	app.player.audioControlWaiters = 0;
	BarWsBroadcastPlayState (&app);
	ck_assert_ptr_nonnull (ctx.buckets[BUCKET_STATE].message);
	test_teardown_web_app (&app, &ctx);
}
END_TEST

START_TEST(test_websocket_bridge_system_volume_mode_broadcast) {
	BarApp_t app;
	BarWsContext_t ctx;

	test_setup_web_app (&app, &ctx);
	app.settings.volumeMode = BAR_VOLUME_MODE_SYSTEM;

	BarWsBroadcastVolume (&app);
	ck_assert_ptr_nonnull (test_bucket_payload (&ctx, BUCKET_VOLUME));
	ck_assert (strstr (test_bucket_payload (&ctx, BUCKET_VOLUME), "volume") != NULL);

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

/* Init must fail cleanly (no context left behind) when it cannot listen */
START_TEST (test_websocket_init_fails_cleanly_when_bind_fails)
{
	BarApp_t app;

	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	/* TEST-NET-3: never a local address, so bind() fails */
	app.settings.websocketHost = strdup ("203.0.113.1");
	app.settings.websocketPort = 18083;

	ck_assert (!BarWebsocketInit (&app));
	ck_assert_ptr_null (app.wsContext);
	BarSettingsDestroy (&app.settings);
}
END_TEST

/* Service-thread side of a real server: a queued broadcast is drained from
 * its bucket and written to every client by the running service thread. */
static pthread_mutex_t g_threadWriteLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_threadWriteCond = PTHREAD_COND_INITIALIZER;
static char g_threadWritten[64];
static size_t g_threadWriteCount;

static int test_thread_write_frame (void *wsi, unsigned char *buf, size_t len) {
	(void) wsi;
	pthread_mutex_lock (&g_threadWriteLock);
	snprintf (g_threadWritten, sizeof (g_threadWritten), "%.*s", (int) len, buf);
	g_threadWriteCount++;
	pthread_cond_signal (&g_threadWriteCond);
	pthread_mutex_unlock (&g_threadWriteLock);
	return (int) len;
}

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

START_TEST (test_websocket_service_thread_drains_bucket_to_clients)
{
	BarApp_t app;
	char fakeSocket;

	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.websocketHost = strdup ("127.0.0.1");
	app.settings.websocketPort = test_free_loopback_port ();
	ck_assert (BarWebsocketInit (&app));
	BarWsContext_t *ctx = app.wsContext;
	g_threadWriteCount = 0;
	ctx->writeFrame = test_thread_write_frame;
	ck_assert_uint_ne (BarWsConnectionAdd (ctx, &fakeSocket, "socketio"), 0);

	/* The bucket mutex orders the hook and table setup before the drain */
	ck_assert (BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BUCKET_VOLUME,
			strdup ("2[\"volume\",40]")));

	struct timespec deadline;
	clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 5;
	pthread_mutex_lock (&g_threadWriteLock);
	while (g_threadWriteCount == 0) {
		if (pthread_cond_timedwait (&g_threadWriteCond, &g_threadWriteLock,
				&deadline) != 0) {
			break;
		}
	}
	size_t written = g_threadWriteCount;
	pthread_mutex_unlock (&g_threadWriteLock);
	ck_assert_uint_eq (written, 1);
	ck_assert_str_eq (g_threadWritten, "2[\"volume\",40]");

	/* Stop the service thread before the fake client is dropped */
	atomic_store_explicit (&app.doQuit, true, memory_order_relaxed);
	BarWebsocketDestroy (&app);
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
	json_object *event = json_tokener_parse (test_bucket_payload (&ctx, BUCKET_VOLUME) + 1);
	ck_assert_ptr_nonnull (event);
	ck_assert_int_eq (json_object_get_int (json_object_array_get_idx (event, 1)), 72);
	json_object_put (event);
	BarWsScheduleVolumeBroadcast (&ctx, 0);
	ck_assert (BarPlayerSetVolume (&app.player, 84));
	app.settings.volume = 3;
	compatAttach (&ctx);
	BarWsProcessVolumeBroadcast (&ctx, &app);
	event = json_tokener_parse (g_compatBuf + 1);
	ck_assert_ptr_nonnull (event);
	ck_assert_str_eq (json_object_get_string (json_object_array_get_idx (event, 0)), "volume");
	ck_assert_int_eq (json_object_get_int (json_object_array_get_idx (event, 1)), 84);
	ck_assert (!ctx.delayedVolumeBroadcast.pending);
	json_object_put (event);
	compatDetach (&ctx);
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

	BarWsConnId_t requester = compatAttach (&ctx);
	BarWsEmitUpcomingTo (&app, requester + 1, &song, 1);
	ck_assert_str_eq (g_compatBuf, "");

	compatDetach (&ctx);
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
	compatAttach (&ctx);

	struct json_object *data = json_object_new_object ();
	json_object_object_add (data, "elapsed",    json_object_new_int (42));
	json_object_object_add (data, "duration",   json_object_new_int (240));
	json_object_object_add (data, "percentage", json_object_new_int (17));
	BarSocketIoEmit (&app, BAR_WS_DEST_BROADCAST, "progress", data);
	json_object_put (data);

	ck_assert (strncmp (g_compatBuf, "2[", 2) == 0);
	ck_assert (strstr (g_compatBuf, "\"progress\"") != NULL);
	ck_assert (strstr (g_compatBuf, "\"elapsed\"")  != NULL);
	ck_assert (strstr (g_compatBuf, "\"duration\"") != NULL);

	compatDetach (&ctx);
	test_teardown_web_app (&app, &ctx);
}
END_TEST

/* Wire-compat: volume must be a bare integer, not wrapped in an object */
START_TEST (test_wire_compat_volume_is_bare_integer)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_web_app (&app, &ctx);
	compatAttach (&ctx);

	BarSocketIoEmit (&app, BAR_WS_DEST_BROADCAST, "volume", json_object_new_int (50));

	ck_assert (strncmp (g_compatBuf, "2[", 2) == 0);
	ck_assert (strstr (g_compatBuf, "\"volume\"") != NULL);
	ck_assert (strstr (g_compatBuf, "{\"volume\"") == NULL);

	compatDetach (&ctx);
	test_teardown_web_app (&app, &ctx);
}
END_TEST

/* Wire-compat: stop must have no payload element — no comma after "stop" */
START_TEST (test_wire_compat_stop_has_no_payload)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_web_app (&app, &ctx);
	compatAttach (&ctx);

	BarSocketIoEmit (&app, BAR_WS_DEST_BROADCAST, "stop", NULL);

	ck_assert (strncmp (g_compatBuf, "2[", 2) == 0);
	const char *stopPos = strstr (g_compatBuf, "\"stop\"");
	ck_assert_ptr_nonnull (stopPos);
	/* No comma after "stop" means no second (payload) element */
	ck_assert_ptr_null (strchr (stopPos, ','));

	compatDetach (&ctx);
	test_teardown_web_app (&app, &ctx);
}
END_TEST

/* Wire-compat: stations payload must open as an array bracket, not an object */
START_TEST (test_wire_compat_stations_is_array)
{
	BarApp_t app;
	BarWsContext_t ctx;
	test_setup_web_app (&app, &ctx);
	compatAttach (&ctx);

	struct json_object *arr = json_object_new_array ();
	struct json_object *s   = json_object_new_object ();
	json_object_object_add (s, "id",         json_object_new_string ("sid"));
	json_object_object_add (s, "name",       json_object_new_string ("My Station"));
	json_object_object_add (s, "isQuickMix", json_object_new_boolean (0));
	json_object_array_add (arr, s);
	BarSocketIoEmit (&app, BAR_WS_DEST_BROADCAST, "stations", arr);
	json_object_put (arr);

	ck_assert (strncmp (g_compatBuf, "2[", 2) == 0);
	ck_assert (strstr (g_compatBuf, "\"stations\"") != NULL);
	const char *after = strstr (g_compatBuf, "\"stations\"");
	ck_assert_ptr_nonnull (after);
	after += strlen ("\"stations\"");
	while (*after == ',' || *after == ' ') after++;
	ck_assert_int_eq (*after, '[');

	compatDetach (&ctx);
	test_teardown_web_app (&app, &ctx);
}
END_TEST

/* Create test suite */
Suite *websocket_suite(void) {
	Suite *s;
	TCase *tc_core;
	TCase *tc_compat;
	
	s = suite_create("WebSocket");
	tc_core = tcase_create("Core");
	
	tcase_add_test(tc_core, test_websocket_init_null);
	tcase_add_test(tc_core, test_websocket_init_destroy_stops_when_app_quits);
	tcase_add_test(tc_core, test_websocket_init_fails_cleanly_when_bind_fails);
	tcase_add_test(tc_core, test_websocket_service_thread_drains_bucket_to_clients);
	tcase_add_test(tc_core, test_websocket_destroy_null);
	tcase_add_test(tc_core, test_websocket_elapsed_null);
	tcase_add_test(tc_core, test_websocket_broadcast_null);
	tcase_add_test(tc_core, test_websocket_bridge_singleton_lock_null_app);
	tcase_add_test(tc_core, test_websocket_bridge_broadcast_process_queues_snapshot_payload);
	tcase_add_test(tc_core, test_websocket_bridge_broadcasts_real_player_state_buckets);
	tcase_add_test(tc_core, test_websocket_bridge_start_stop_and_paused_progress);
	tcase_add_test(tc_core, test_websocket_bridge_progress_skips_duplicate_elapsed);
	tcase_add_test(tc_core, test_websocket_bridge_disconnect_all_clients_with_context);
	tcase_add_test (tc_core, test_send_broadcast_reaches_all_clients);
	tcase_add_test (tc_core, test_send_to_id_reaches_only_that_client);
	tcase_add_test (tc_core, test_send_to_reused_slot_does_not_reach_new_client);
	tcase_add_test (tc_core, test_send_targeted_and_broadcast_do_not_replace_each_other);
	tcase_add_test (tc_core, test_send_targeted_state_keeps_pending_progress_broadcast);
	tcase_add_test (tc_core, test_send_unknown_id_frees_message_and_returns_false);
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

	tc_compat = tcase_create ("wire_compat");
	tcase_add_test (tc_compat, test_wire_compat_progress_is_object);
	tcase_add_test (tc_compat, test_wire_compat_volume_is_bare_integer);
	tcase_add_test (tc_compat, test_wire_compat_stop_has_no_payload);
	tcase_add_test (tc_compat, test_wire_compat_stations_is_array);
	suite_add_tcase (s, tc_compat);
	
	return s;
}
