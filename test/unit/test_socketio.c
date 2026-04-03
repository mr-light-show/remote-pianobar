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
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <json-c/json.h>
#include "../../src/main.h"
#include "../../src/l10n.h"
#include "../../src/settings.h"
#include "../../src/system_volume.h"
#include "../../src/websocket/core/websocket.h"
#include "../../src/websocket/core/queue.h"
#include "../../src/websocket/protocol/socketio.h"

/* Mock broadcast callback for testing emissions */
static char *lastBroadcastMessage = NULL;
static size_t lastBroadcastLen = 0;

static void mockBroadcastCallback(const char *message, size_t len) {
	free(lastBroadcastMessage);
	lastBroadcastMessage = malloc(len + 1);
	memcpy(lastBroadcastMessage, message, len);
	lastBroadcastMessage[len] = '\0';
	lastBroadcastLen = len;
}

static void clearBroadcastMock() {
	free(lastBroadcastMessage);
	lastBroadcastMessage = NULL;
	lastBroadcastLen = 0;
}

/* Test: Socket.IO emit with event name only */
START_TEST(test_socketio_emit_event_only) {
	BarSocketIoSetBroadcastCallback(mockBroadcastCallback);
	
	BarSocketIoEmit("test_event", NULL);
	
	ck_assert_ptr_nonnull(lastBroadcastMessage);
	/* json-c adds spaces: "2[ "test_event" ]" */
	ck_assert(strstr(lastBroadcastMessage, "2[") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "test_event") != NULL);
	
	clearBroadcastMock();
}
END_TEST

/* Test: Socket.IO emit with event and data */
START_TEST(test_socketio_emit_with_data) {
	BarSocketIoSetBroadcastCallback(mockBroadcastCallback);
	
	json_object *data = json_object_new_object();
	json_object_object_add(data, "key", json_object_new_string("value"));
	
	BarSocketIoEmit("test_event", data);
	json_object_put(data);
	
	ck_assert_ptr_nonnull(lastBroadcastMessage);
	ck_assert(strstr(lastBroadcastMessage, "2[") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "test_event") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "key") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "value") != NULL);
	
	clearBroadcastMock();
}
END_TEST

/* Test: Socket.IO stop event (no data) */
START_TEST(test_socketio_emit_stop) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	
	BarSocketIoSetBroadcastCallback(mockBroadcastCallback);
	
	BarSocketIoEmitStop(&app);
	
	ck_assert_ptr_nonnull(lastBroadcastMessage);
	ck_assert(strstr(lastBroadcastMessage, "2[") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "stop") != NULL);
	
	clearBroadcastMock();
}
END_TEST

/* Test: Socket.IO progress event format */
START_TEST(test_socketio_emit_progress) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	
	BarSocketIoSetBroadcastCallback(mockBroadcastCallback);
	
	BarSocketIoEmitProgress(&app, 60, 180);
	
	ck_assert_ptr_nonnull(lastBroadcastMessage);
	ck_assert(strstr(lastBroadcastMessage, "2[") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "progress") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "elapsed") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "60") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "duration") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "180") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "percentage") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "33.") != NULL);
	
	clearBroadcastMock();
}
END_TEST

/* Test: Socket.IO volume event */
START_TEST(test_socketio_emit_volume) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	
	BarSocketIoSetBroadcastCallback(mockBroadcastCallback);
	
	BarSocketIoEmitVolume(&app, 75);
	
	ck_assert_ptr_nonnull(lastBroadcastMessage);
	ck_assert(strstr(lastBroadcastMessage, "2[") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "volume") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "75") != NULL);
	
	clearBroadcastMock();
}
END_TEST

/* Test: Action dispatch - playback commands */
START_TEST(test_socketio_translate_playback_commands) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	BarSettingsInit(&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;  /* Skip mutex in tests */
	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_init(&app.stateRwlock, NULL);  /* Initialize mutex for safety */
	#endif
	
	/* Create mock WebSocket context (no queue needed - commands execute directly) */
	BarWsContext_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	app.wsContext = &ctx;
	
	/* Test playback.next - commands now execute directly via BarUiDispatchById */
	/* Just verify it doesn't crash */
	BarSocketIoHandleAction(&app, "playback.next", NULL, NULL);
	
	/* Test playback.toggle */
	BarSocketIoHandleAction(&app, "playback.toggle", NULL, NULL);
	
	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_destroy(&app.stateRwlock);
	#endif
	BarSettingsDestroy(&app.settings);
	
	/* If we reach here without crashing, the test passes */
}
END_TEST

/* Test: Action dispatch - song commands */
START_TEST(test_socketio_translate_song_commands) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	BarSettingsInit(&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;  /* Skip mutex in tests */
	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_init(&app.stateRwlock, NULL);  /* Initialize mutex for safety */
	#endif
	
	BarWsContext_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	app.wsContext = &ctx;
	
	/* Test song.love - commands now execute directly via BarUiDispatchById */
	BarSocketIoHandleAction(&app, "song.love", NULL, NULL);
	
	/* Test song.ban */
	BarSocketIoHandleAction(&app, "song.ban", NULL, NULL);
	
	/* Test song.tired */
	BarSocketIoHandleAction(&app, "song.tired", NULL, NULL);
	
	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_destroy(&app.stateRwlock);
	#endif
	BarSettingsDestroy(&app.settings);
	
	/* If we reach here without crashing, the test passes */
}
END_TEST

/* Test: Command translation - volume commands */
START_TEST(test_socketio_translate_volume_commands) {
	BarApp_t app;
	player_t player;
	memset(&app, 0, sizeof(app));
	memset(&player, 0, sizeof(player));
	BarSettingsInit(&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;  /* Skip mutex in tests */
	app.player = player;
	pthread_mutex_init(&app.player.lock, NULL);
	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_init(&app.stateRwlock, NULL);  /* Initialize mutex for safety */
	#endif
	
	BarWsContext_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	app.wsContext = &ctx;
	
	/* Test volume.up - commands now execute directly via BarUiDispatchById */
	BarSocketIoHandleAction(&app, "volume.up", NULL, NULL);
	
	/* Test volume.down */
	BarSocketIoHandleAction(&app, "volume.down", NULL, NULL);
	
	pthread_mutex_destroy(&app.player.lock);
	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_destroy(&app.stateRwlock);
	#endif
	BarSettingsDestroy(&app.settings);
	
	/* If we reach here without crashing, the test passes */
}
END_TEST

/* Test: Action dispatch - reject invalid commands */
START_TEST(test_socketio_reject_invalid_command) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	BarSettingsInit(&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;  /* Skip mutex in tests */
	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_init(&app.stateRwlock, NULL);  /* Initialize mutex for safety */
	#endif
	
	BarWsContext_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	app.wsContext = &ctx;
	
	/* Test invalid command - should be silently ignored (not crash) */
	BarSocketIoHandleAction(&app, "invalid.command", NULL, NULL);
	
	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_destroy(&app.stateRwlock);
	#endif
	BarSettingsDestroy(&app.settings);
	
	/* If we reach here without crashing, the test passes */
}
END_TEST

/* Test: Action dispatch - reject single-letter commands */
START_TEST(test_socketio_reject_single_letter) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	BarSettingsInit(&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;  /* Skip mutex in tests */
	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_init(&app.stateRwlock, NULL);  /* Initialize mutex for safety */
	#endif
	
	BarWsContext_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	app.wsContext = &ctx;
	
	/* Test single-letter command - should be silently ignored (not crash) */
	BarSocketIoHandleAction(&app, "n", NULL, NULL);
	
	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_destroy(&app.stateRwlock);
	#endif
	BarSettingsDestroy(&app.settings);
	
	/* If we reach here without crashing, the test passes */
}
END_TEST

/* Test: Handle query event */
START_TEST(test_socketio_handle_query) {
	BarApp_t app;
	PianoStation_t station;
	
	memset(&app, 0, sizeof(app));
	memset(&station, 0, sizeof(station));
	
	/* Setup minimal station */
	station.name = "Test Station";
	station.id = "S123456";
	station.head.next = NULL;
	
	/* Setup app with station and settings */
	app.ph.stations = &station;
	BarSettingsInit(&app.settings);
	app.settings.sortOrder = 0;  /* BAR_SORT_NAME_AZ */
	app.settings.uiMode = BAR_UI_MODE_CLI;  /* Skip mutex in tests */
	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_init(&app.stateRwlock, NULL);  /* Initialize mutex for safety */
	#endif
	
	BarSocketIoSetBroadcastCallback(mockBroadcastCallback);
	clearBroadcastMock();
	
	/* Query should emit 'process' and 'stations' events */
	BarSocketIoHandleQuery(&app, NULL);
	
	/* At least one message should be broadcast */
	ck_assert_ptr_nonnull(lastBroadcastMessage);
	ck_assert(strstr(lastBroadcastMessage, "process") != NULL ||
	          strstr(lastBroadcastMessage, "stations") != NULL);
	
	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_destroy(&app.stateRwlock);
	#endif
	BarSettingsDestroy(&app.settings);
	
	clearBroadcastMock();
}
END_TEST

/* Test: Client keepalive "ping" event is a no-op (no crash, no broadcast) */
START_TEST(test_socketio_ping_keepalive_noop) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	BarSettingsInit(&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;
	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_init(&app.stateRwlock, NULL);
	#endif

	BarSocketIoSetBroadcastCallback(mockBroadcastCallback);
	clearBroadcastMock();

	/* Socket.IO EVENT packet: 2["ping"] - client keepalive; server must not respond */
	BarSocketIoHandleMessage(&app, "2[\"ping\"]", NULL);

	/* Ping is explicitly a no-op: no broadcast should be sent */
	ck_assert_ptr_null(lastBroadcastMessage);

	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_destroy(&app.stateRwlock);
	#endif
	BarSettingsDestroy(&app.settings);
	clearBroadcastMock();
}
END_TEST

/* Test: Rating commands emit state update */
START_TEST(test_socketio_rating_emits_state) {
	BarApp_t app;
	PianoSong_t song;
	PianoStation_t station;
	
	memset(&app, 0, sizeof(app));
	memset(&song, 0, sizeof(song));
	memset(&station, 0, sizeof(station));
	
	/* Setup minimal song and station */
	song.artist = "Test Artist";
	song.title = "Test Song";
	song.album = "Test Album";
	song.coverArt = "http://example.com/art.jpg";
	song.trackToken = "test_token";
	song.rating = PIANO_RATE_NONE;
	song.length = 180;
	
	station.name = "Test Station";
	station.id = "S123456";
	
	app.playlist = &song;
	app.curStation = &station;
	BarSettingsInit(&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;  /* Skip mutex in tests */
	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_init(&app.stateRwlock, NULL);  /* Initialize mutex for safety */
	#endif
	
	BarSocketIoSetBroadcastCallback(mockBroadcastCallback);
	clearBroadcastMock();
	
	/* Simulate love command - should emit start event */
	song.rating = PIANO_RATE_LOVE;  /* Simulate what BarUiDispatchById does */
	BarSocketIoEmitStart(&app);
	
	/* Verify "start" event was emitted with rating */
	ck_assert_ptr_nonnull(lastBroadcastMessage);
	ck_assert(strstr(lastBroadcastMessage, "start") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "rating") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "1") != NULL);  /* PIANO_RATE_LOVE = 1 */
	
	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_destroy(&app.stateRwlock);
	#endif
	BarSettingsDestroy(&app.settings);
	clearBroadcastMock();
}
END_TEST

static void free_settings_accounts (BarApp_t *app) {
	if (!app->settings.accounts || app->settings.accountCount == 0) {
		return;
	}
	for (size_t i = 0; i < app->settings.accountCount; i++) {
		free (app->settings.accounts[i].id);
		free (app->settings.accounts[i].label);
		free (app->settings.accounts[i].username);
		free (app->settings.accounts[i].password);
		free (app->settings.accounts[i].passwordCmd);
		free (app->settings.accounts[i].autostartStation);
	}
	free (app->settings.accounts);
	app->settings.accounts = NULL;
	app->settings.accountCount = 0;
}

/* EmitProcess JSON includes accounts and current_account when accountCount > 0 */
START_TEST (test_socketio_emit_process_includes_accounts) {
	BarApp_t app;

	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;
	app.settings.volumeMode = BAR_VOLUME_MODE_PLAYER;
	app.settings.volume = 55;
	ck_assert_int_eq (pthread_mutex_init (&app.player.lock, NULL), 0);

	app.settings.accountCount = 2;
	app.settings.accounts = calloc (2, sizeof (BarAccount_t));
	ck_assert_ptr_nonnull (app.settings.accounts);
	app.settings.accounts[0].id = strdup ("alpha");
	app.settings.accounts[0].label = strdup ("Alpha");
	app.settings.accounts[1].id = strdup ("beta");
	app.settings.accounts[1].label = strdup ("Beta");
	app.settings.activeAccountIndex = 1;

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitProcess (&app);

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "process") != NULL);
	ck_assert (strstr (lastBroadcastMessage, "accounts") != NULL);
	ck_assert (strstr (lastBroadcastMessage, "alpha") != NULL);
	ck_assert (strstr (lastBroadcastMessage, "beta") != NULL);
	ck_assert (strstr (lastBroadcastMessage, "current_account") != NULL);
	ck_assert (strstr (lastBroadcastMessage, "Beta") != NULL);

	free_settings_accounts (&app);
	BarSettingsDestroy (&app.settings);
	pthread_mutex_destroy (&app.player.lock);
	clearBroadcastMock ();
}
END_TEST

/* app.pandora-reconnect with unknown account_id emits error and leaves active index */
START_TEST (test_socketio_pandora_reconnect_unknown_account) {
	BarApp_t app;
	BarWsContext_t ctx;

	memset (&app, 0, sizeof (app));
	memset (&ctx, 0, sizeof (ctx));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;
	app.wsContext = &ctx;

	app.settings.accountCount = 2;
	app.settings.accounts = calloc (2, sizeof (BarAccount_t));
	ck_assert_ptr_nonnull (app.settings.accounts);
	app.settings.accounts[0].id = strdup ("a");
	app.settings.accounts[0].label = strdup ("A");
	app.settings.accounts[1].id = strdup ("b");
	app.settings.accounts[1].label = strdup ("B");
	app.settings.activeAccountIndex = 0;

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	json_object *data = json_object_new_object ();
	json_object_object_add (data, "account_id", json_object_new_string ("ghost"));
	BarSocketIoHandleAction (&app, "app.pandora-reconnect", data, NULL);
	json_object_put (data);

	ck_assert_uint_eq (app.settings.activeAccountIndex, 0);
	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "error") != NULL);
	ck_assert (strstr (lastBroadcastMessage, "ghost") != NULL);

	free_settings_accounts (&app);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
}
END_TEST

/* BarSocketIoEmitErrorEx includes stationId in JSON when set */
START_TEST (test_socketio_emit_error_ex_with_station_id) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitErrorEx (&app, "station.rename", "Network error", "station-42");

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "error") != NULL);
	ck_assert (strstr (lastBroadcastMessage, "stationId") != NULL);
	ck_assert (strstr (lastBroadcastMessage, "station-42") != NULL);

	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
}
END_TEST

/* Null message: no broadcast */
START_TEST (test_socketio_emit_error_ex_null_message) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitErrorEx (&app, "op", NULL, "sid");

	ck_assert_ptr_null (lastBroadcastMessage);

	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
}
END_TEST

/* app NULL: l10n NULL, still emits friendly error JSON */
START_TEST (test_socketio_emit_error_ex_null_app) {
	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitErrorEx (NULL, "music.search", "Network error", NULL);

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "error") != NULL);
	ck_assert (strstr (lastBroadcastMessage, "operation") != NULL);

	clearBroadcastMock ();
}
END_TEST

/* Empty stationId: omit stationId field */
START_TEST (test_socketio_emit_error_ex_empty_station_id) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitErrorEx (&app, "station.change", "Station not found", "");

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "error") != NULL);
	ck_assert (strstr (lastBroadcastMessage, "stationId") == NULL);

	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
}
END_TEST

/* BarSocketIoEmitError delegates to EmitErrorEx with NULL stationId */
START_TEST (test_socketio_emit_error_wrapper) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitError (&app, "playback.play", "Not connected to Pandora");

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "error") != NULL);
	ck_assert (strstr (lastBroadcastMessage, "stationId") == NULL);

	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
}
END_TEST

Suite *socketio_suite(void) {
	Suite *s;
	TCase *tc_emit;
	TCase *tc_translate;
	TCase *tc_handle;
	
	s = suite_create("Socket.IO Protocol");
	
	/* Event emission tests */
	tc_emit = tcase_create("Event Emission");
	tcase_add_test(tc_emit, test_socketio_emit_event_only);
	tcase_add_test(tc_emit, test_socketio_emit_with_data);
	tcase_add_test(tc_emit, test_socketio_emit_stop);
	tcase_add_test(tc_emit, test_socketio_emit_progress);
	tcase_add_test(tc_emit, test_socketio_emit_volume);
	tcase_add_test(tc_emit, test_socketio_emit_process_includes_accounts);
	suite_add_tcase(s, tc_emit);
	
	/* Action dispatch tests */
	tc_translate = tcase_create("Action Dispatch");
	tcase_add_test(tc_translate, test_socketio_translate_playback_commands);
	tcase_add_test(tc_translate, test_socketio_translate_song_commands);
	tcase_add_test(tc_translate, test_socketio_translate_volume_commands);
	tcase_add_test(tc_translate, test_socketio_reject_invalid_command);
	tcase_add_test(tc_translate, test_socketio_reject_single_letter);
	tcase_add_test(tc_translate, test_socketio_pandora_reconnect_unknown_account);
	tcase_add_test(tc_translate, test_socketio_emit_error_ex_with_station_id);
	tcase_add_test(tc_translate, test_socketio_emit_error_ex_null_message);
	tcase_add_test(tc_translate, test_socketio_emit_error_ex_null_app);
	tcase_add_test(tc_translate, test_socketio_emit_error_ex_empty_station_id);
	tcase_add_test(tc_translate, test_socketio_emit_error_wrapper);
	suite_add_tcase(s, tc_translate);
	
	/* Event handler tests */
	tc_handle = tcase_create("Event Handlers");
	tcase_add_test(tc_handle, test_socketio_handle_query);
	tcase_add_test(tc_handle, test_socketio_ping_keepalive_noop);
	tcase_add_test(tc_handle, test_socketio_rating_emits_state);
	suite_add_tcase(s, tc_handle);
	
	return s;
}
