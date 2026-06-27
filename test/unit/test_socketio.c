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
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <json-c/json.h>
#include "../../src/main.h"
#include "../../src/l10n.h"
#include "../../src/bar_state.h"
#include "../../src/settings.h"
#include "../../src/ui.h"
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
	BarUiPianoHttpMutexInit(&app);
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
	BarUiPianoHttpMutexDestroy(&app);
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
	BarUiPianoHttpMutexInit(&app);
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
	BarUiPianoHttpMutexDestroy(&app);
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

START_TEST(test_socketio_handle_query_stations_event_emits_station_list) {
	BarApp_t app;
	PianoStation_t station;
	memset(&app, 0, sizeof(app));
	memset(&station, 0, sizeof(station));
	BarSettingsInit(&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;
	app.settings.sortOrder = BAR_SORT_NAME_AZ;
	station.id = "station-1";
	station.name = "Station One";
	app.ph.stations = &station;

	BarSocketIoSetBroadcastCallback(mockBroadcastCallback);
	clearBroadcastMock();

	BarSocketIoHandleMessage(&app, "2[\"query.stations\"]", NULL);

	ck_assert_ptr_nonnull(lastBroadcastMessage);
	ck_assert(strstr(lastBroadcastMessage, "\"stations\"") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "Station One") != NULL);

	BarSettingsDestroy(&app.settings);
	clearBroadcastMock();
}
END_TEST

START_TEST(test_socketio_handle_action_object_command_emits_not_implemented_error) {
	BarApp_t app;
	BarWsContext_t ctx;
	memset(&app, 0, sizeof(app));
	memset(&ctx, 0, sizeof(ctx));
	BarSettingsInit(&app.settings);
	ck_assert(BarL10nInit(&app.l10n, &app.settings));
	app.wsContext = &ctx;

	BarSocketIoSetBroadcastCallback(mockBroadcastCallback);
	clearBroadcastMock();

	BarSocketIoHandleMessage(&app, "2[\"action\",{\"command\":\"app.settings\"}]", NULL);

	ck_assert_ptr_nonnull(lastBroadcastMessage);
	ck_assert(strstr(lastBroadcastMessage, "\"error\"") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "app.settings") != NULL);

	BarL10nDestroy(&app.l10n);
	BarSettingsDestroy(&app.settings);
	clearBroadcastMock();
}
END_TEST

START_TEST(test_socketio_handle_station_change_missing_station_reports_error) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	BarSettingsInit(&app.settings);
	ck_assert(BarL10nInit(&app.l10n, &app.settings));

	BarSocketIoSetBroadcastCallback(mockBroadcastCallback);
	clearBroadcastMock();

	BarSocketIoHandleMessage(&app, "2[\"station.change\",{\"id\":\"missing-station\"}]", NULL);

	ck_assert_ptr_nonnull(lastBroadcastMessage);
	ck_assert(strstr(lastBroadcastMessage, "\"error\"") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "missing-station") != NULL);

	BarL10nDestroy(&app.l10n);
	BarSettingsDestroy(&app.settings);
	clearBroadcastMock();
}
END_TEST

START_TEST(test_socketio_dispatches_legacy_query_and_station_change_string) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	BarSettingsInit(&app.settings);
	ck_assert(BarL10nInit(&app.l10n, &app.settings));

	BarSocketIoSetBroadcastCallback(mockBroadcastCallback);
	clearBroadcastMock();

	BarSocketIoHandleMessage(&app, "2[\"query.state\",{}]", NULL);
	ck_assert_ptr_nonnull(lastBroadcastMessage);
	ck_assert(strstr(lastBroadcastMessage, "[") != NULL);

	clearBroadcastMock();
	BarSocketIoHandleMessage(&app, "2[\"changeStation\",\"missing-station\"]", NULL);
	ck_assert_ptr_nonnull(lastBroadcastMessage);
	ck_assert(strstr(lastBroadcastMessage, "missing-station") != NULL);

	BarL10nDestroy(&app.l10n);
	BarSettingsDestroy(&app.settings);
	clearBroadcastMock();
}
END_TEST

START_TEST(test_socketio_table_dispatch_rejects_missing_payload_for_mutating_events) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	BarSettingsInit(&app.settings);

	const char *events[] = {
		"station.delete",
		"station.createFrom",
		"station.addGenre",
		"station.addMusic",
		"station.addShared",
		"station.rename",
		"station.setMode",
		"station.getInfo",
		"station.deleteSeed",
		"station.deleteFeedback",
		"music.search",
	};

	for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
		char frame[128];
		snprintf(frame, sizeof(frame), "2[\"%s\"]", events[i]);
		BarSocketIoHandleMessage(&app, frame, NULL);
	}

	BarSettingsDestroy(&app.settings);
}
END_TEST

/* Test: In-place rating uses process payload (BarWsBroadcastProcess -> BarSocketIoEmitProcess) */
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
	song.rating = PIANO_RATE_LOVE;  /* After successful rate-song, same as ui_act */
	song.length = 180;
	song.stationId = "S123456";
	
	station.name = "Test Station";
	station.displayName = "Display Station";
	station.id = "S123456";
	
	app.playlist = &song;
	app.curStation = &station;
	app.ph.stations = &station;
	BarSettingsInit(&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;  /* Skip mutex in tests */
	app.settings.volumeMode = BAR_VOLUME_MODE_PLAYER;
	app.settings.volume = 50;
	ck_assert_int_eq (pthread_mutex_init (&app.player.lock, NULL), 0);
	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_init(&app.stateRwlock, NULL);  /* Initialize mutex for safety */
	#endif
	
	BarSocketIoSetBroadcastCallback(mockBroadcastCallback);
	clearBroadcastMock();
	
	BarSocketIoEmitProcess(&app);
	
	ck_assert_ptr_nonnull(lastBroadcastMessage);
	ck_assert(strstr(lastBroadcastMessage, "process") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "\"song\"") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "rating") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "1") != NULL);  /* PIANO_RATE_LOVE = 1 */
	ck_assert(strstr(lastBroadcastMessage, "songStationName") != NULL);
	ck_assert(strstr(lastBroadcastMessage, "Display Station") != NULL);
	
	pthread_mutex_destroy(&app.player.lock);
	#ifdef WEBSOCKET_ENABLED
	pthread_rwlock_destroy(&app.stateRwlock);
	#endif
	BarSettingsDestroy(&app.settings);
	clearBroadcastMock();
}
END_TEST

START_TEST(test_socketio_build_start_payload_uses_snapshot_song_station_name) {
	BarApp_t app;
	PianoSong_t song;
	PianoStation_t station;

	memset(&app, 0, sizeof(app));
	memset(&song, 0, sizeof(song));
	memset(&station, 0, sizeof(station));

	BarSettingsInit(&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;
	station.id = "song-station";
	station.name = "Raw Station";
	station.displayName = "Snapshot Station";
	song.title = "Snapshot Song";
	song.artist = "Snapshot Artist";
	song.stationId = "song-station";
	app.playlist = &song;
	app.ph.stations = &station;

	json_object *payload = BarSocketIoBuildStartPayload(&app);
	ck_assert_ptr_nonnull(payload);
	const char *text = json_object_to_json_string(payload);
	ck_assert(strstr(text, "songStationName") != NULL);
	ck_assert(strstr(text, "Snapshot Station") != NULL);

	json_object_put(payload);
	BarSettingsDestroy(&app.settings);
}
END_TEST

START_TEST (test_socketio_build_process_payload_includes_song_fields) {
	BarApp_t app;
	PianoSong_t song;
	PianoStation_t station;

	memset (&app, 0, sizeof (app));
	memset (&song, 0, sizeof (song));
	memset (&station, 0, sizeof (station));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;
	station.id = "proc-station";
	station.name = "Process Station";
	song.title = "Process Song";
	song.artist = "Process Artist";
	song.stationId = "proc-station";
	app.curStation = &station;
	app.playlist = &song;
	app.ph.stations = &station;

	json_object *payload = BarSocketIoBuildProcessPayload (&app);
	ck_assert_ptr_nonnull (payload);
	const char *text = json_object_to_json_string (payload);
	ck_assert (strstr (text, "Process Song") != NULL);
	ck_assert (strstr (text, "Process Artist") != NULL);

	json_object_put (payload);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_socketio_emit_stations_empty_when_list_missing) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;
	app.ph.stations = NULL;

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitStations (&app);

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "stations") != NULL);
	ck_assert (strncmp (lastBroadcastMessage, "2[", 2) == 0);

	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
}
END_TEST

START_TEST (test_socketio_emit_play_state_reports_paused_flag) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	ck_assert_int_eq (pthread_mutex_init (&app.player.lock, NULL), 0);
	app.player.doPause = true;

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitPlayState (&app);

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "playState") != NULL);
	ck_assert (strstr (lastBroadcastMessage, "true") != NULL);

	pthread_mutex_destroy (&app.player.lock);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
}
END_TEST

START_TEST (test_socketio_emit_pandora_disconnected_includes_reason) {
	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitPandoraDisconnected ("idle_timeout");

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "idle_timeout") != NULL);

	clearBroadcastMock ();
}
END_TEST

START_TEST (test_socketio_handle_change_station_switches_when_found) {
	BarApp_t app;
	PianoStation_t station;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	BarSettingsInit (&app.settings);
	BarStateInit (&app);
	station.id = "live-station";
	station.name = "Live Station";
	app.ph.stations = &station;

	BarSocketIoHandleChangeStation (&app, "live-station");

	ck_assert_ptr_eq (BarStateGetNextStation (&app), &station);
	ck_assert_ptr_null (BarStateGetPlaylist (&app));

	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_socketio_volume_set_action_updates_player_volume) {
	BarApp_t app;
	BarWsContext_t ctx;
	json_object *data;
	memset (&app, 0, sizeof (app));
	memset (&ctx, 0, sizeof (ctx));
	BarSettingsInit (&app.settings);
	app.settings.volumeMode = BAR_VOLUME_MODE_PLAYER;
	app.settings.volume = 50;
	ck_assert_int_eq (pthread_mutex_init (&app.player.lock, NULL), 0);
	ck_assert_int_eq (pthread_mutex_init (&ctx.volumeBroadcastMutex, NULL), 0);
	app.player.settings = &app.settings;
	app.wsContext = &ctx;

	data = json_object_new_object ();
	json_object_object_add (data, "volume", json_object_new_int (33));

	BarSocketIoHandleAction (&app, "volume.set", data, NULL);

	ck_assert_int_eq (app.settings.volume, 33);
	pthread_mutex_lock (&ctx.volumeBroadcastMutex);
	ck_assert (ctx.delayedVolumeBroadcast.pending);
	pthread_mutex_unlock (&ctx.volumeBroadcastMutex);

	json_object_put (data);
	pthread_mutex_destroy (&ctx.volumeBroadcastMutex);
	pthread_mutex_destroy (&app.player.lock);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_socketio_volume_set_clamps_out_of_range) {
	BarApp_t app;
	BarWsContext_t ctx;
	json_object *data;
	memset (&app, 0, sizeof (app));
	memset (&ctx, 0, sizeof (ctx));
	BarSettingsInit (&app.settings);
	ck_assert_int_eq (pthread_mutex_init (&app.player.lock, NULL), 0);
	ck_assert_int_eq (pthread_mutex_init (&ctx.volumeBroadcastMutex, NULL), 0);
	app.player.settings = &app.settings;
	app.wsContext = &ctx;

	data = json_object_new_object ();
	json_object_object_add (data, "volume", json_object_new_int (150));
	BarSocketIoHandleAction (&app, "volume.set", data, NULL);
	ck_assert_int_eq (app.settings.volume, 100);

	json_object_put (data);
	pthread_mutex_destroy (&ctx.volumeBroadcastMutex);
	pthread_mutex_destroy (&app.player.lock);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_socketio_build_stations_payload_sorts_snapshot) {
	BarApp_t app;
	PianoStation_t beta, alpha;
	memset (&app, 0, sizeof (app));
	memset (&beta, 0, sizeof (beta));
	memset (&alpha, 0, sizeof (alpha));
	BarSettingsInit (&app.settings);
	app.settings.sortOrder = BAR_SORT_NAME_AZ;
	BarStateInit (&app);
	beta.id = "b"; beta.name = "Beta";
	alpha.id = "a"; alpha.name = "Alpha";
	beta.head.next = (PianoListHead_t *) &alpha;
	app.ph.stations = &beta;

	json_object *payload = BarSocketIoBuildStationsPayload (&app);
	ck_assert_ptr_nonnull (payload);
	ck_assert_int_eq (json_object_array_length (payload), 2);
	struct json_object *first = json_object_array_get_idx (payload, 0);
	struct json_object *nameObj = NULL;
	ck_assert (json_object_object_get_ex (first, "name", &nameObj));
	ck_assert_str_eq (json_object_get_string (nameObj), "Alpha");

	json_object_put (payload);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_socketio_build_stations_payload_quickmix_10_name_az) {
	BarApp_t app;
	PianoStation_t alpha, quickmix;
	memset (&app, 0, sizeof (app));
	memset (&alpha, 0, sizeof (alpha));
	memset (&quickmix, 0, sizeof (quickmix));
	BarSettingsInit (&app.settings);
	app.settings.sortOrder = BAR_SORT_QUICKMIX_10_NAME_AZ;
	BarStateInit (&app);
	alpha.id = "a"; alpha.name = "Alpha";
	quickmix.id = "q"; quickmix.name = "Zzz QuickMix"; quickmix.isQuickMix = 1;
	alpha.head.next = (PianoListHead_t *) &quickmix;
	app.ph.stations = &alpha;

	json_object *payload = BarSocketIoBuildStationsPayload (&app);
	ck_assert_ptr_nonnull (payload);
	ck_assert_int_eq (json_object_array_length (payload), 2);
	struct json_object *first = json_object_array_get_idx (payload, 0);
	struct json_object *nameObj = NULL;
	ck_assert (json_object_object_get_ex (first, "name", &nameObj));
	ck_assert_str_eq (json_object_get_string (nameObj), "Zzz QuickMix");

	json_object_put (payload);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_socketio_unknown_action_emits_nothing) {
	BarApp_t app;
	BarWsContext_t ctx;
	memset (&app, 0, sizeof (app));
	memset (&ctx, 0, sizeof (ctx));
	BarSettingsInit (&app.settings);
	app.wsContext = &ctx;

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoHandleAction (&app, "not.a.real.action", NULL, NULL);
	ck_assert_ptr_null (lastBroadcastMessage);

	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
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

	BarUiPianoHttpMutexInit (&app);

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
	BarUiPianoHttpMutexDestroy (&app);
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

/* Mapped-but-unimplemented actions must emit an error event, not silently no-op */
START_TEST (test_socketio_unimplemented_actions_emit_error)
{
	static const char *unimplemented[] = {
		"query.history", "song.bookmark", "app.settings", NULL
	};

	BarApp_t app;
	BarWsContext_t ctx;
	memset (&app, 0, sizeof (app));
	memset (&ctx,  0, sizeof (ctx));
	BarSettingsInit (&app.settings);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	app.wsContext = &ctx;

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);

	for (size_t i = 0; unimplemented[i] != NULL; i++) {
		clearBroadcastMock ();
		BarSocketIoHandleAction (&app, unimplemented[i], NULL, NULL);
		ck_assert_msg (lastBroadcastMessage != NULL,
		               "Expected error broadcast for action '%s'", unimplemented[i]);
		ck_assert_msg (strstr (lastBroadcastMessage, "\"error\"") != NULL,
		               "Expected 'error' event for action '%s'", unimplemented[i]);
		ck_assert_msg (strstr (lastBroadcastMessage, unimplemented[i]) != NULL,
		               "Expected operation name in error payload for '%s'", unimplemented[i]);
	}

	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
}
END_TEST

START_TEST (test_socketio_format_event_message_returns_valid_packet)
{
	struct json_object *data = json_object_new_object ();
	json_object_object_add (data, "elapsed", json_object_new_int (7));

	char *message = BarSocketIoFormatEventMessage ("progress", data);

	ck_assert_ptr_nonnull (message);
	ck_assert (strncmp (message, "2[", 2) == 0);
	ck_assert (strstr (message, "\"progress\"") != NULL);
	ck_assert (strstr (message, "\"elapsed\"")  != NULL);

	free (message);
	json_object_put (data);
}
END_TEST

START_TEST (test_socketio_emit_explanation_includes_text)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitExplanation (&app, "Because you liked jazz.");

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "song.explanation") != NULL);
	ck_assert (strstr (lastBroadcastMessage, "Because you liked jazz.") != NULL);

	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
}
END_TEST

START_TEST (test_socketio_emit_explanation_null_args_noop)
{
	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitExplanation (NULL, "ignored");
	BarSocketIoEmitExplanation ((BarApp_t *) 1, NULL);

	ck_assert_ptr_null (lastBroadcastMessage);
	clearBroadcastMock ();
}
END_TEST

START_TEST (test_socketio_emit_upcoming_lists_songs)
{
	BarApp_t app;
	PianoSong_t first, second;
	PianoStation_t station;
	memset (&app, 0, sizeof (app));
	memset (&first, 0, sizeof (first));
	memset (&second, 0, sizeof (second));
	memset (&station, 0, sizeof (station));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;
	first.title = "First";
	first.artist = "Artist One";
	first.stationId = "s1";
	first.head.next = &second.head;
	second.title = "Second";
	second.artist = "Artist Two";
	second.stationId = "s1";
	station.id = "s1";
	station.name = "Station One";
	app.ph.stations = &station;

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitUpcoming (&app, &first, 5);

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "query.upcoming.result") != NULL);
	ck_assert (strstr (lastBroadcastMessage, "First") != NULL);
	ck_assert (strstr (lastBroadcastMessage, "Second") != NULL);

	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
}
END_TEST

START_TEST (test_socketio_emit_genres_from_cached_categories)
{
	BarApp_t app;
	PianoGenreCategory_t category;
	PianoGenre_t genre;
	memset (&app, 0, sizeof (app));
	memset (&category, 0, sizeof (category));
	memset (&genre, 0, sizeof (genre));
	BarSettingsInit (&app.settings);
	category.name = "Rock";
	genre.name = "Classic Rock";
	genre.musicId = "G123";
	category.genres = &genre;
	app.ph.genreStations = &category;

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitGenres (&app);

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "genres") != NULL);
	ck_assert (strstr (lastBroadcastMessage, "Classic Rock") != NULL);

	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
}
END_TEST


START_TEST (test_socketio_handle_get_genres_emits_cached_payload)
{
	BarApp_t app;
	PianoGenreCategory_t category;
	PianoGenre_t genre;
	memset (&app, 0, sizeof (app));
	memset (&category, 0, sizeof (category));
	memset (&genre, 0, sizeof (genre));
	BarSettingsInit (&app.settings);
	BarUiPianoHttpMutexInit (&app);
	category.name = "Pop";
	genre.name = "Top Pop";
	genre.musicId = "G999";
	category.genres = &genre;
	app.ph.genreStations = &category;

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoHandleGetGenres (&app);

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "genres") != NULL);
	ck_assert (strstr (lastBroadcastMessage, "Top Pop") != NULL);

	BarUiPianoHttpMutexDestroy (&app);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
}
END_TEST

static bool
mock_genre_fetch_auth_fail (BarApp_t *app, const PianoRequestType_t type, void *data,
                            PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	(void) data;
	if (type == PIANO_REQUEST_GET_GENRE_STATIONS) {
		*pRet = PIANO_RET_INVALID_LOGIN;
		*wRet = CURLE_OK;
		return false;
	}
	return false;
}

START_TEST (test_socketio_handle_get_genres_auth_failure_notifies_disconnect)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	BarUiPianoHttpMutexInit (&app);
	app.ph.genreStations = NULL;

	BarUiPianoCallSetTestHook (mock_genre_fetch_auth_fail);
	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoHandleGetGenres (&app);

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "error") != NULL);

	BarUiPianoCallClearTestHook ();
	BarUiPianoHttpMutexDestroy (&app);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
}
END_TEST

START_TEST (test_socketio_handle_message_station_get_genres_event)
{
	BarApp_t app;
	PianoGenreCategory_t category;
	PianoGenre_t genre;
	memset (&app, 0, sizeof (app));
	memset (&category, 0, sizeof (category));
	memset (&genre, 0, sizeof (genre));
	BarSettingsInit (&app.settings);
	BarUiPianoHttpMutexInit (&app);
	category.name = "Jazz";
	genre.name = "Smooth Jazz";
	genre.musicId = "G555";
	category.genres = &genre;
	app.ph.genreStations = &category;

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoHandleMessage (&app, "2[\"station.getGenres\"]", NULL);

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "genres") != NULL);

	BarUiPianoHttpMutexDestroy (&app);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
}
END_TEST

START_TEST (test_socketio_emit_process_unicast_targets_client)
{
	BarApp_t app;
	PianoSong_t song;
	PianoStation_t station;
	void *fake_wsi = (void *) 0xdeadbeef;
	memset (&app, 0, sizeof (app));
	memset (&song, 0, sizeof (song));
	memset (&station, 0, sizeof (station));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;
	song.title = "Unicast Song";
	song.artist = "Artist";
	song.stationId = "s-unicast";
	station.id = "s-unicast";
	station.name = "Unicast Station";
	app.playlist = &song;
	app.curStation = &station;
	app.ph.stations = &station;
	ck_assert_int_eq (pthread_mutex_init (&app.player.lock, NULL), 0);

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitProcessUnicast (&app, fake_wsi);
	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "process") != NULL);
	ck_assert_ptr_null (BarSocketIoGetUnicastTarget ());

	pthread_mutex_destroy (&app.player.lock);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
}
END_TEST

START_TEST (test_socketio_handle_add_genre_missing_music_id)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	BarUiPianoHttpMutexInit (&app);

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoHandleAddGenre (&app, json_object_new_object ());

	ck_assert_ptr_null (lastBroadcastMessage);

	BarUiPianoHttpMutexDestroy (&app);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
}
END_TEST

START_TEST (test_socketio_volume_set_system_mode)
{
	BarApp_t app;
	BarWsContext_t ctx;
	json_object *data;
	memset (&app, 0, sizeof (app));
	memset (&ctx, 0, sizeof (ctx));
	BarSettingsInit (&app.settings);
	app.settings.volumeMode = BAR_VOLUME_MODE_SYSTEM;
	ck_assert_int_eq (pthread_mutex_init (&app.player.lock, NULL), 0);
	ck_assert_int_eq (pthread_mutex_init (&ctx.volumeBroadcastMutex, NULL), 0);
	app.player.settings = &app.settings;
	app.wsContext = &ctx;

	data = json_object_new_object ();
	json_object_object_add (data, "volume", json_object_new_int (42));
	BarSocketIoHandleAction (&app, "volume.set", data, NULL);

	pthread_mutex_lock (&ctx.volumeBroadcastMutex);
	ck_assert (ctx.delayedVolumeBroadcast.pending);
	pthread_mutex_unlock (&ctx.volumeBroadcastMutex);

	json_object_put (data);
	pthread_mutex_destroy (&ctx.volumeBroadcastMutex);
	pthread_mutex_destroy (&app.player.lock);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static bool
mock_rename_station (BarApp_t *app, const PianoRequestType_t type, void *data,
                     PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	if (type == PIANO_REQUEST_RENAME_STATION) {
		PianoRequestDataRenameStation_t *req = data;
		ck_assert_ptr_nonnull (req->newName);
		ck_assert_str_eq (req->newName, "Renamed");
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_socketio_handle_rename_station_success)
{
	BarApp_t app;
	BarWsContext_t ctx;
	json_object *data;
	PianoStation_t station;
	memset (&app, 0, sizeof (app));
	memset (&ctx, 0, sizeof (ctx));
	memset (&station, 0, sizeof (station));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);
	station.id = "S1";
	station.name = "Old";
	station.isCreator = true;
	app.ph.stations = &station;
	app.wsContext = &ctx;

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_rename_station);
	data = json_object_new_object ();
	json_object_object_add (data, "stationId", json_object_new_string ("S1"));
	json_object_object_add (data, "newName", json_object_new_string ("Renamed"));
	BarSocketIoHandleRenameStation (&app, data);
	BarUiPianoCallClearTestHook ();

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "stations") != NULL);

	json_object_put (data);
	clearBroadcastMock ();
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static bool
mock_add_seed (BarApp_t *app, const PianoRequestType_t type, void *data,
               PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	if (type == PIANO_REQUEST_ADD_SEED) {
		PianoRequestDataAddSeed_t *req = data;
		ck_assert_str_eq (req->musicId, "AM:123");
		ck_assert_ptr_nonnull (req->station);
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_socketio_handle_add_music_success)
{
	BarApp_t app;
	PianoStation_t station;
	json_object *data;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);
	station.id = "S1";
	station.name = "Station";
	station.isCreator = true;
	app.ph.stations = &station;

	BarUiPianoCallSetTestHook (mock_add_seed);
	data = json_object_new_object ();
	json_object_object_add (data, "musicId", json_object_new_string ("AM:123"));
	json_object_object_add (data, "stationId", json_object_new_string ("S1"));
	BarSocketIoHandleAddMusic (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static bool
mock_delete_station (BarApp_t *app, const PianoRequestType_t type, void *data,
                     PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	if (type == PIANO_REQUEST_DELETE_STATION) {
		ck_assert_ptr_nonnull (data);
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_socketio_handle_delete_station_success)
{
	BarApp_t app;
	PianoStation_t station;
	json_object *data;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);
	station.id = "S1";
	station.name = "Delete Me";
	station.isCreator = true;
	app.ph.stations = &station;
	BarStateSetCurrentStation (&app, &station);

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_delete_station);
	data = json_object_new_object ();
	json_object_object_add (data, "stationId", json_object_new_string ("S1"));
	BarSocketIoHandleDeleteStation (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "stations") != NULL);
	clearBroadcastMock ();
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_socketio_handle_add_music_missing_fields_noop)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();
	BarSocketIoHandleAddMusic (&app, json_object_new_object ());
	ck_assert_ptr_null (lastBroadcastMessage);
	clearBroadcastMock ();
	BarSettingsDestroy (&app.settings);
}
END_TEST

static bool
mock_create_station_ok (BarApp_t *app, const PianoRequestType_t type, void *data,
                        PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	if (type == PIANO_REQUEST_CREATE_STATION) {
		ck_assert_ptr_nonnull (data);
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_socketio_handle_add_shared_station_success)
{
	BarApp_t app;
	BarWsContext_t ctx;
	json_object *data;
	memset (&app, 0, sizeof (app));
	memset (&ctx, 0, sizeof (ctx));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.wsContext = &ctx;
	for (int i = 0; i < BUCKET_COUNT; i++) {
		pthread_mutex_init (&ctx.buckets[i].mutex, NULL);
	}
	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_create_station_ok);
	data = json_object_new_object ();
	json_object_object_add (data, "stationId", json_object_new_string ("12345"));
	BarSocketIoHandleAddShared (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "stations") != NULL);
	clearBroadcastMock ();
	for (int i = 0; i < BUCKET_COUNT; i++) {
		pthread_mutex_destroy (&ctx.buckets[i].mutex);
	}
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_socketio_handle_add_shared_rejects_non_digit_station_id)
{
	BarApp_t app;
	json_object *data;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();
	data = json_object_new_object ();
	json_object_object_add (data, "stationId", json_object_new_string ("abc"));
	BarSocketIoHandleAddShared (&app, data);
	json_object_put (data);
	ck_assert_ptr_null (lastBroadcastMessage);
	clearBroadcastMock ();
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_socketio_handle_create_station_from_song)
{
	BarApp_t app;
	BarWsContext_t ctx;
	json_object *data;
	memset (&app, 0, sizeof (app));
	memset (&ctx, 0, sizeof (ctx));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.wsContext = &ctx;
	for (int i = 0; i < BUCKET_COUNT; i++) {
		pthread_mutex_init (&ctx.buckets[i].mutex, NULL);
	}
	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_create_station_ok);
	data = json_object_new_object ();
	json_object_object_add (data, "trackToken", json_object_new_string ("TT:1"));
	json_object_object_add (data, "type", json_object_new_string ("song"));
	BarSocketIoHandleCreateStationFrom (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	clearBroadcastMock ();
	for (int i = 0; i < BUCKET_COUNT; i++) {
		pthread_mutex_destroy (&ctx.buckets[i].mutex);
	}
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static PianoStationMode_t *g_test_station_mode_heap = NULL;

static bool
mock_get_station_modes_ok (BarApp_t *app, const PianoRequestType_t type, void *data,
                           PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	if (type == PIANO_REQUEST_GET_STATION_MODES) {
		PianoRequestDataGetStationModes_t *req = data;
		g_test_station_mode_heap = calloc (1, sizeof (*g_test_station_mode_heap));
		ck_assert_ptr_nonnull (g_test_station_mode_heap);
		g_test_station_mode_heap->name = strdup ("My Mode");
		g_test_station_mode_heap->description = strdup ("Desc");
		g_test_station_mode_heap->active = true;
		req->retModes = g_test_station_mode_heap;
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_socketio_handle_get_station_modes_success)
{
	BarApp_t app;
	PianoStation_t station;
	json_object *data;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	BarSettingsInit (&app.settings);
	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);
	station.id = "S1";
	station.name = "Station";
	app.ph.stations = &station;

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_get_station_modes_ok);
	data = json_object_new_object ();
	json_object_object_add (data, "stationId", json_object_new_string ("S1"));
	BarSocketIoHandleGetStationModes (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "stationModes") != NULL);
	clearBroadcastMock ();
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_socketio_handle_get_station_modes_skips_quickmix)
{
	BarApp_t app;
	PianoStation_t station;
	json_object *data;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	BarSettingsInit (&app.settings);
	BarStateInit (&app);
	station.id = "QM";
	station.isQuickMix = true;
	app.ph.stations = &station;

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();
	data = json_object_new_object ();
	json_object_object_add (data, "stationId", json_object_new_string ("QM"));
	BarSocketIoHandleGetStationModes (&app, data);
	json_object_put (data);
	ck_assert_ptr_null (lastBroadcastMessage);
	clearBroadcastMock ();
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static bool
mock_set_station_mode_ok (BarApp_t *app, const PianoRequestType_t type, void *data,
                          PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	(void) data;
	if (type == PIANO_REQUEST_SET_STATION_MODE) {
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_socketio_handle_set_station_mode_success)
{
	BarApp_t app;
	PianoStation_t station;
	json_object *data;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	BarSettingsInit (&app.settings);
	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);
	station.id = "S1";
	app.ph.stations = &station;

	BarUiPianoCallSetTestHook (mock_set_station_mode_ok);
	data = json_object_new_object ();
	json_object_object_add (data, "stationId", json_object_new_string ("S1"));
	json_object_object_add (data, "modeId", json_object_new_int (2));
	BarSocketIoHandleSetStationMode (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static bool
mock_get_station_info_ok (BarApp_t *app, const PianoRequestType_t type, void *data,
                          PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	if (type == PIANO_REQUEST_GET_STATION_INFO) {
		PianoRequestDataGetStationInfo_t *req = data;
		PianoArtist_t *artist = calloc (1, sizeof (*artist));
		ck_assert_ptr_nonnull (artist);
		artist->name = strdup ("Seed Artist");
		artist->musicId = strdup ("AM:seed");
		req->info.artistSeeds = artist;
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_socketio_handle_get_station_info_success)
{
	BarApp_t app;
	PianoStation_t station;
	json_object *data;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	BarSettingsInit (&app.settings);
	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);
	station.id = "S1";
	app.ph.stations = &station;

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_get_station_info_ok);
	data = json_object_new_object ();
	json_object_object_add (data, "stationId", json_object_new_string ("S1"));
	BarSocketIoHandleGetStationInfo (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "stationInfo") != NULL);
	clearBroadcastMock ();
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static bool
mock_search_music_ok (BarApp_t *app, const PianoRequestType_t type, void *data,
                      PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	if (type == PIANO_REQUEST_SEARCH) {
		PianoRequestDataSearch_t *req = data;
		PianoSong_t *song = calloc (1, sizeof (*song));
		ck_assert_ptr_nonnull (song);
		song->title = strdup ("Found Song");
		song->musicId = strdup ("AM:1");
		req->searchResult.songs = song;
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_socketio_handle_search_music_success)
{
	BarApp_t app;
	json_object *data;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	BarUiPianoHttpMutexInit (&app);

	BarSocketIoSetBroadcastCallback (mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_search_music_ok);
	data = json_object_new_object ();
	json_object_object_add (data, "query", json_object_new_string ("beatles"));
	BarSocketIoHandleSearchMusic (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	ck_assert_ptr_nonnull (lastBroadcastMessage);
	ck_assert (strstr (lastBroadcastMessage, "searchResults") != NULL);
	clearBroadcastMock ();
	BarUiPianoHttpMutexDestroy (&app);
	BarSettingsDestroy (&app.settings);
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
	tcase_add_test(tc_translate, test_socketio_unimplemented_actions_emit_error);
	suite_add_tcase(s, tc_translate);
	
	/* Event handler tests */
	tc_handle = tcase_create("Event Handlers");
	tcase_add_test(tc_handle, test_socketio_handle_query);
	tcase_add_test(tc_handle, test_socketio_ping_keepalive_noop);
	tcase_add_test(tc_handle, test_socketio_handle_query_stations_event_emits_station_list);
	tcase_add_test(tc_handle, test_socketio_handle_action_object_command_emits_not_implemented_error);
	tcase_add_test(tc_handle, test_socketio_handle_station_change_missing_station_reports_error);
	tcase_add_test(tc_handle, test_socketio_dispatches_legacy_query_and_station_change_string);
	tcase_add_test(tc_handle, test_socketio_table_dispatch_rejects_missing_payload_for_mutating_events);
	tcase_add_test(tc_handle, test_socketio_rating_emits_state);
	tcase_add_test(tc_handle, test_socketio_build_start_payload_uses_snapshot_song_station_name);
	tcase_add_test(tc_handle, test_socketio_build_process_payload_includes_song_fields);
	tcase_add_test(tc_handle, test_socketio_emit_stations_empty_when_list_missing);
	tcase_add_test(tc_handle, test_socketio_emit_play_state_reports_paused_flag);
	tcase_add_test(tc_handle, test_socketio_emit_pandora_disconnected_includes_reason);
	tcase_add_test(tc_handle, test_socketio_handle_change_station_switches_when_found);
	tcase_add_test(tc_handle, test_socketio_volume_set_action_updates_player_volume);
	tcase_add_test(tc_handle, test_socketio_volume_set_clamps_out_of_range);
	tcase_add_test(tc_handle, test_socketio_volume_set_system_mode);
	tcase_add_test(tc_handle, test_socketio_handle_rename_station_success);
	tcase_add_test(tc_handle, test_socketio_handle_add_music_success);
	tcase_add_test(tc_handle, test_socketio_handle_delete_station_success);
	tcase_add_test(tc_handle, test_socketio_handle_add_music_missing_fields_noop);
	tcase_add_test(tc_handle, test_socketio_handle_add_shared_station_success);
	tcase_add_test(tc_handle, test_socketio_handle_add_shared_rejects_non_digit_station_id);
	tcase_add_test(tc_handle, test_socketio_handle_create_station_from_song);
	tcase_add_test(tc_handle, test_socketio_handle_get_station_modes_success);
	tcase_add_test(tc_handle, test_socketio_handle_get_station_modes_skips_quickmix);
	tcase_add_test(tc_handle, test_socketio_handle_set_station_mode_success);
	tcase_add_test(tc_handle, test_socketio_handle_get_station_info_success);
	tcase_add_test(tc_handle, test_socketio_handle_search_music_success);
	tcase_add_test(tc_handle, test_socketio_build_stations_payload_sorts_snapshot);
	tcase_add_test(tc_handle, test_socketio_build_stations_payload_quickmix_10_name_az);
	tcase_add_test(tc_handle, test_socketio_unknown_action_emits_nothing);
	suite_add_tcase(s, tc_handle);

	/* Formatter tests */
	TCase *tc_format = tcase_create ("Format");
	tcase_add_test (tc_format, test_socketio_format_event_message_returns_valid_packet);
	suite_add_tcase (s, tc_format);

	TCase *tc_extra = tcase_create ("Extended coverage");
	tcase_add_test (tc_extra, test_socketio_emit_explanation_includes_text);
	tcase_add_test (tc_extra, test_socketio_emit_explanation_null_args_noop);
	tcase_add_test (tc_extra, test_socketio_emit_upcoming_lists_songs);
	tcase_add_test (tc_extra, test_socketio_emit_genres_from_cached_categories);
	tcase_add_test (tc_extra, test_socketio_handle_get_genres_emits_cached_payload);
	tcase_add_test (tc_extra, test_socketio_handle_get_genres_auth_failure_notifies_disconnect);
	tcase_add_test (tc_extra, test_socketio_handle_message_station_get_genres_event);
	tcase_add_test (tc_extra, test_socketio_emit_process_unicast_targets_client);
	tcase_add_test (tc_extra, test_socketio_handle_add_genre_missing_music_id);
	suite_add_tcase (s, tc_extra);
	
	return s;
}
