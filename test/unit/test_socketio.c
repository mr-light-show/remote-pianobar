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
#include <json-c/json.h>
#include "../../src/main.h"
#include "../../src/settings.h"
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
	BarSettings_t settings;
	memset(&app, 0, sizeof(app));
	memset(&settings, 0, sizeof(settings));
	app.settings = settings;
	app.settings.uiMode = BAR_UI_MODE_CLI;  /* Skip mutex in tests */
	#ifdef WEBSOCKET_ENABLED
	pthread_mutex_init(&app.stateMutex, NULL);  /* Initialize mutex for safety */
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
	pthread_mutex_destroy(&app.stateMutex);
	#endif
	
	/* If we reach here without crashing, the test passes */
}
END_TEST

/* Test: Action dispatch - song commands */
START_TEST(test_socketio_translate_song_commands) {
	BarApp_t app;
	BarSettings_t settings;
	memset(&app, 0, sizeof(app));
	memset(&settings, 0, sizeof(settings));
	app.settings = settings;
	app.settings.uiMode = BAR_UI_MODE_CLI;  /* Skip mutex in tests */
	#ifdef WEBSOCKET_ENABLED
	pthread_mutex_init(&app.stateMutex, NULL);  /* Initialize mutex for safety */
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
	pthread_mutex_destroy(&app.stateMutex);
	#endif
	
	/* If we reach here without crashing, the test passes */
}
END_TEST

/* Test: Command translation - volume commands */
START_TEST(test_socketio_translate_volume_commands) {
	BarApp_t app;
	BarSettings_t settings;
	player_t player;
	memset(&app, 0, sizeof(app));
	memset(&settings, 0, sizeof(settings));
	memset(&player, 0, sizeof(player));
	app.settings = settings;
	app.settings.uiMode = BAR_UI_MODE_CLI;  /* Skip mutex in tests */
	app.player = player;
	pthread_mutex_init(&app.player.lock, NULL);
	#ifdef WEBSOCKET_ENABLED
	pthread_mutex_init(&app.stateMutex, NULL);  /* Initialize mutex for safety */
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
	pthread_mutex_destroy(&app.stateMutex);
	#endif
	
	/* If we reach here without crashing, the test passes */
}
END_TEST

/* Test: Action dispatch - reject invalid commands */
START_TEST(test_socketio_reject_invalid_command) {
	BarApp_t app;
	BarSettings_t settings;
	memset(&app, 0, sizeof(app));
	memset(&settings, 0, sizeof(settings));
	app.settings = settings;
	app.settings.uiMode = BAR_UI_MODE_CLI;  /* Skip mutex in tests */
	#ifdef WEBSOCKET_ENABLED
	pthread_mutex_init(&app.stateMutex, NULL);  /* Initialize mutex for safety */
	#endif
	
	BarWsContext_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	app.wsContext = &ctx;
	
	/* Test invalid command - should be silently ignored (not crash) */
	BarSocketIoHandleAction(&app, "invalid.command", NULL, NULL);
	
	#ifdef WEBSOCKET_ENABLED
	pthread_mutex_destroy(&app.stateMutex);
	#endif
	
	/* If we reach here without crashing, the test passes */
}
END_TEST

/* Test: Action dispatch - reject single-letter commands */
START_TEST(test_socketio_reject_single_letter) {
	BarApp_t app;
	BarSettings_t settings;
	memset(&app, 0, sizeof(app));
	memset(&settings, 0, sizeof(settings));
	app.settings = settings;
	app.settings.uiMode = BAR_UI_MODE_CLI;  /* Skip mutex in tests */
	#ifdef WEBSOCKET_ENABLED
	pthread_mutex_init(&app.stateMutex, NULL);  /* Initialize mutex for safety */
	#endif
	
	BarWsContext_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	app.wsContext = &ctx;
	
	/* Test single-letter command - should be silently ignored (not crash) */
	BarSocketIoHandleAction(&app, "n", NULL, NULL);
	
	#ifdef WEBSOCKET_ENABLED
	pthread_mutex_destroy(&app.stateMutex);
	#endif
	
	/* If we reach here without crashing, the test passes */
}
END_TEST

/* Test: Handle query event */
START_TEST(test_socketio_handle_query) {
	BarApp_t app;
	BarSettings_t settings;
	PianoStation_t station;
	
	memset(&app, 0, sizeof(app));
	memset(&settings, 0, sizeof(settings));
	memset(&station, 0, sizeof(station));
	
	/* Setup minimal station */
	station.name = "Test Station";
	station.id = "S123456";
	station.head.next = NULL;
	
	/* Setup app with station and settings */
	app.ph.stations = &station;
	app.settings = settings;
	app.settings.sortOrder = 0;  /* BAR_SORT_NAME_AZ */
	app.settings.uiMode = BAR_UI_MODE_CLI;  /* Skip mutex in tests */
	#ifdef WEBSOCKET_ENABLED
	pthread_mutex_init(&app.stateMutex, NULL);  /* Initialize mutex for safety */
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
	pthread_mutex_destroy(&app.stateMutex);
	#endif
	
	clearBroadcastMock();
}
END_TEST

/* Test: Client keepalive "ping" event is a no-op (no crash, no broadcast) */
START_TEST(test_socketio_ping_keepalive_noop) {
	BarApp_t app;
	BarSettings_t settings;
	memset(&app, 0, sizeof(app));
	memset(&settings, 0, sizeof(settings));
	app.settings = settings;
	app.settings.uiMode = BAR_UI_MODE_CLI;
	#ifdef WEBSOCKET_ENABLED
	pthread_mutex_init(&app.stateMutex, NULL);
	#endif

	BarSocketIoSetBroadcastCallback(mockBroadcastCallback);
	clearBroadcastMock();

	/* Socket.IO EVENT packet: 2["ping"] - client keepalive; server must not respond */
	BarSocketIoHandleMessage(&app, "2[\"ping\"]", NULL);

	/* Ping is explicitly a no-op: no broadcast should be sent */
	ck_assert_ptr_null(lastBroadcastMessage);

	#ifdef WEBSOCKET_ENABLED
	pthread_mutex_destroy(&app.stateMutex);
	#endif
	clearBroadcastMock();
}
END_TEST

/* Test: Rating commands emit state update */
START_TEST(test_socketio_rating_emits_state) {
	BarApp_t app;
	BarSettings_t settings;
	PianoSong_t song;
	PianoStation_t station;
	
	memset(&app, 0, sizeof(app));
	memset(&settings, 0, sizeof(settings));
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
	app.settings = settings;
	app.settings.uiMode = BAR_UI_MODE_CLI;  /* Skip mutex in tests */
	#ifdef WEBSOCKET_ENABLED
	pthread_mutex_init(&app.stateMutex, NULL);  /* Initialize mutex for safety */
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
	pthread_mutex_destroy(&app.stateMutex);
	#endif
	
	clearBroadcastMock();
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
	suite_add_tcase(s, tc_emit);
	
	/* Action dispatch tests */
	tc_translate = tcase_create("Action Dispatch");
	tcase_add_test(tc_translate, test_socketio_translate_playback_commands);
	tcase_add_test(tc_translate, test_socketio_translate_song_commands);
	tcase_add_test(tc_translate, test_socketio_translate_volume_commands);
	tcase_add_test(tc_translate, test_socketio_reject_invalid_command);
	tcase_add_test(tc_translate, test_socketio_reject_single_letter);
	suite_add_tcase(s, tc_translate);
	
	/* Event handler tests */
	tc_handle = tcase_create("Event Handlers");
	tcase_add_test(tc_handle, test_socketio_handle_query);
	tcase_add_test(tc_handle, test_socketio_ping_keepalive_noop);
	tcase_add_test(tc_handle, test_socketio_rating_emits_state);
	suite_add_tcase(s, tc_handle);
	
	return s;
}
