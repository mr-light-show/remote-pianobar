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
#include "../../src/websocket/protocol/socketio.h"
#include <json-c/json.h>
#include <pthread.h>

/* Wire-compat capture buffer */
static char g_compatBuf[8192];
static void compatCapture (const char *msg, size_t len) {
	size_t copy = len < sizeof (g_compatBuf) - 1 ? len : sizeof (g_compatBuf) - 1;
	memcpy (g_compatBuf, msg, copy);
	g_compatBuf[copy] = '\0';
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
	BarStateInit (app);
	pthread_mutex_init (&app->player.lock, NULL);
	pthread_cond_init (&app->player.cond, NULL);
}

static void test_teardown_web_app (BarApp_t *app, BarWsContext_t *ctx) {
	pthread_cond_destroy (&app->player.cond);
	pthread_mutex_destroy (&app->player.lock);
	BarStateDestroy (app);
	test_ws_context_destroy_buckets (ctx);
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
	app.settings.volume = 41;
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

START_TEST(test_websocket_bridge_unicast_helpers_and_errors) {
	BarApp_t app;
	BarWsContext_t ctx;
	PianoSong_t song;
	PianoStation_t station;
	test_setup_web_app (&app, &ctx);
	test_attach_station_and_song (&app, &station, &song);

	BarSocketIoSetBroadcastCallback (compatCapture);
	g_compatBuf[0] = '\0';

	ck_assert (!BarWsIsUnicastRequest ());

	BarWsBroadcastExplanation (&app, "ignored without unicast target");
	ck_assert_str_eq (g_compatBuf, "");

	int dummy_wsi = 0;
	BarSocketIoSetUnicastTarget (&dummy_wsi);
	ck_assert (BarWsIsUnicastRequest ());
	BarWsBroadcastExplanation (&app, "Because you like rock.");
	ck_assert (strstr (g_compatBuf, "Because you like rock.") != NULL);
	BarSocketIoSetUnicastTarget (NULL);

	g_compatBuf[0] = '\0';
	BarWsEmitError (&app, "station.change", "Station not found");
	ck_assert (strstr (g_compatBuf, "\"error\"") != NULL);
	ck_assert (strstr (g_compatBuf, "Station not found") != NULL);

	g_compatBuf[0] = '\0';
	BarWsEmitErrorEx (&app, "station.change", "Station not found", "abc-123");
	ck_assert (strstr (g_compatBuf, "abc-123") != NULL);

	g_compatBuf[0] = '\0';
	BarWsBroadcastPandoraDisconnected (&app, "session_invalid");
	ck_assert (strstr (g_compatBuf, "session_invalid") != NULL);

	BarSocketIoSetBroadcastCallback (NULL);
	test_teardown_web_app (&app, &ctx);
}
END_TEST

START_TEST(test_websocket_bridge_upcoming_play_state_and_release_lock) {
	BarApp_t app;
	BarWsContext_t ctx;
	PianoSong_t song;
	int dummy_wsi = 0;

	test_setup_web_app (&app, &ctx);
	memset (&song, 0, sizeof (song));
	song.title = "Up Next";

	BarSocketIoSetBroadcastCallback (compatCapture);
	BarSocketIoSetUnicastTarget (&dummy_wsi);
	g_compatBuf[0] = '\0';
	BarWsBroadcastUpcoming (&app, &song, 1);
	ck_assert (strstr (g_compatBuf, "Up Next") != NULL);

	g_compatBuf[0] = '\0';
	BarWsBroadcastPlayState (&app);
	ck_assert (g_compatBuf[0] != '\0');

	app.lockFd = dup (STDERR_FILENO);
	ck_assert_int_ge (app.lockFd, 0);
	BarWsReleaseSingletonLock (&app);
	ck_assert_int_eq (app.lockFd, -1);

	BarSocketIoSetUnicastTarget (NULL);
	BarSocketIoSetBroadcastCallback (NULL);
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

START_TEST(test_websocket_bridge_upcoming_skips_without_unicast_target) {
	BarApp_t app;
	BarWsContext_t ctx;
	PianoSong_t song;
	test_setup_web_app (&app, &ctx);
	memset (&song, 0, sizeof (song));
	song.title = "Hidden Upcoming";

	BarSocketIoSetBroadcastCallback (compatCapture);
	g_compatBuf[0] = '\0';
	BarSocketIoSetUnicastTarget (NULL);

	BarWsBroadcastUpcoming (&app, &song, 1);
	ck_assert_str_eq (g_compatBuf, "");

	BarSocketIoSetBroadcastCallback (NULL);
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
	g_compatBuf[0] = '\0';
	BarSocketIoSetBroadcastCallback (compatCapture);

	struct json_object *data = json_object_new_object ();
	json_object_object_add (data, "elapsed",    json_object_new_int (42));
	json_object_object_add (data, "duration",   json_object_new_int (240));
	json_object_object_add (data, "percentage", json_object_new_int (17));
	BarSocketIoEmit ("progress", data);
	json_object_put (data);

	ck_assert (strncmp (g_compatBuf, "2[", 2) == 0);
	ck_assert (strstr (g_compatBuf, "\"progress\"") != NULL);
	ck_assert (strstr (g_compatBuf, "\"elapsed\"")  != NULL);
	ck_assert (strstr (g_compatBuf, "\"duration\"") != NULL);

	BarSocketIoSetBroadcastCallback (NULL);
}
END_TEST

/* Wire-compat: volume must be a bare integer, not wrapped in an object */
START_TEST (test_wire_compat_volume_is_bare_integer)
{
	g_compatBuf[0] = '\0';
	BarSocketIoSetBroadcastCallback (compatCapture);

	BarSocketIoEmit ("volume", json_object_new_int (50));

	ck_assert (strncmp (g_compatBuf, "2[", 2) == 0);
	ck_assert (strstr (g_compatBuf, "\"volume\"") != NULL);
	ck_assert (strstr (g_compatBuf, "{\"volume\"") == NULL);

	BarSocketIoSetBroadcastCallback (NULL);
}
END_TEST

/* Wire-compat: stop must have no payload element — no comma after "stop" */
START_TEST (test_wire_compat_stop_has_no_payload)
{
	g_compatBuf[0] = '\0';
	BarSocketIoSetBroadcastCallback (compatCapture);

	BarSocketIoEmit ("stop", NULL);

	ck_assert (strncmp (g_compatBuf, "2[", 2) == 0);
	const char *stopPos = strstr (g_compatBuf, "\"stop\"");
	ck_assert_ptr_nonnull (stopPos);
	/* No comma after "stop" means no second (payload) element */
	ck_assert_ptr_null (strchr (stopPos, ','));

	BarSocketIoSetBroadcastCallback (NULL);
}
END_TEST

/* Wire-compat: stations payload must open as an array bracket, not an object */
START_TEST (test_wire_compat_stations_is_array)
{
	g_compatBuf[0] = '\0';
	BarSocketIoSetBroadcastCallback (compatCapture);

	struct json_object *arr = json_object_new_array ();
	struct json_object *s   = json_object_new_object ();
	json_object_object_add (s, "id",         json_object_new_string ("sid"));
	json_object_object_add (s, "name",       json_object_new_string ("My Station"));
	json_object_object_add (s, "isQuickMix", json_object_new_boolean (0));
	json_object_array_add (arr, s);
	BarSocketIoEmit ("stations", arr);
	json_object_put (arr);

	ck_assert (strncmp (g_compatBuf, "2[", 2) == 0);
	ck_assert (strstr (g_compatBuf, "\"stations\"") != NULL);
	const char *after = strstr (g_compatBuf, "\"stations\"");
	ck_assert_ptr_nonnull (after);
	after += strlen ("\"stations\"");
	while (*after == ',' || *after == ' ') after++;
	ck_assert_int_eq (*after, '[');

	BarSocketIoSetBroadcastCallback (NULL);
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
	tcase_add_test(tc_core, test_websocket_destroy_null);
	tcase_add_test(tc_core, test_websocket_elapsed_null);
	tcase_add_test(tc_core, test_websocket_broadcast_null);
	tcase_add_test(tc_core, test_websocket_bridge_singleton_lock_null_app);
	tcase_add_test(tc_core, test_websocket_bridge_broadcast_process_queues_snapshot_payload);
	tcase_add_test(tc_core, test_websocket_bridge_broadcasts_real_player_state_buckets);
	tcase_add_test(tc_core, test_websocket_bridge_start_stop_and_paused_progress);
	tcase_add_test(tc_core, test_websocket_bridge_unicast_helpers_and_errors);
	tcase_add_test(tc_core, test_websocket_bridge_upcoming_play_state_and_release_lock);
	tcase_add_test(tc_core, test_websocket_bridge_upcoming_skips_without_unicast_target);
	tcase_add_test(tc_core, test_websocket_disconnect_all_clients_null_app);
	tcase_add_test(tc_core, test_websocket_bridge_system_volume_mode_broadcast);
	tcase_add_test(tc_core, test_websocket_schedule_volume_broadcast_sets_pending_flag);
	tcase_add_test(tc_core, test_websocket_bridge_both_mode_is_web_active);
	tcase_add_test(tc_core, test_websocket_bridge_print_helpers_and_input_setup);
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

