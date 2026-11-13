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
#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
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
#include "test_ws_capture.h"
#include <math.h>

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

/* Latest frame a client received, after running the service-thread side
 * when a capture is attached (frames are written only from writable
 * callbacks) */
static const char *test_last_broadcast (void) {
	if (test_ws_cap.ctx != NULL) {
		TestWsCaptureFlush ();
	}
	return lastBroadcastMessage;
}

static void clearBroadcastMock() {
	free(lastBroadcastMessage);
	lastBroadcastMessage = NULL;
	lastBroadcastLen = 0;
}

/* Test: Socket.IO emit with event name only */
START_TEST(test_socketio_emit_event_only) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	
	BarSocketIoEmit(&app, BAR_WS_DEST_BROADCAST, "test_event", NULL);
	
	ck_assert_ptr_nonnull(test_last_broadcast ());
	/* json-c adds spaces: "2[ "test_event" ]" */
	ck_assert(strstr(test_last_broadcast (), "2[") != NULL);
	ck_assert(strstr(test_last_broadcast (), "test_event") != NULL);
	
	clearBroadcastMock();
	TestWsCaptureDetach (&app);
}
END_TEST

/* Test: Socket.IO emit with event and data */
START_TEST(test_socketio_emit_with_data) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	
	json_object *data = json_object_new_object();
	json_object_object_add(data, "key", json_object_new_string("value"));
	
	BarSocketIoEmit(&app, BAR_WS_DEST_BROADCAST, "test_event", data);
	json_object_put(data);
	
	ck_assert_ptr_nonnull(test_last_broadcast ());
	ck_assert(strstr(test_last_broadcast (), "2[") != NULL);
	ck_assert(strstr(test_last_broadcast (), "test_event") != NULL);
	ck_assert(strstr(test_last_broadcast (), "key") != NULL);
	ck_assert(strstr(test_last_broadcast (), "value") != NULL);
	
	clearBroadcastMock();
	TestWsCaptureDetach (&app);
}
END_TEST

/* Test: Socket.IO stop event (no data) */
START_TEST(test_socketio_emit_stop) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	
	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	
	BarSocketIoEmitStop(&app);
	
	ck_assert_ptr_nonnull(test_last_broadcast ());
	ck_assert(strstr(test_last_broadcast (), "2[") != NULL);
	ck_assert(strstr(test_last_broadcast (), "stop") != NULL);
	
	clearBroadcastMock();
	TestWsCaptureDetach (&app);
}
END_TEST

/* Test: Socket.IO progress event format */
START_TEST(test_socketio_emit_progress) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	
	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	
	BarSocketIoEmitProgress(&app, 60, 180);
	
	ck_assert_ptr_nonnull(test_last_broadcast ());
	ck_assert(strstr(test_last_broadcast (), "2[") != NULL);
	ck_assert(strstr(test_last_broadcast (), "progress") != NULL);
	ck_assert(strstr(test_last_broadcast (), "elapsed") != NULL);
	ck_assert(strstr(test_last_broadcast (), "60") != NULL);
	ck_assert(strstr(test_last_broadcast (), "duration") != NULL);
	ck_assert(strstr(test_last_broadcast (), "180") != NULL);
	ck_assert(strstr(test_last_broadcast (), "percentage") != NULL);
	ck_assert(strstr(test_last_broadcast (), "33.") != NULL);
	
	clearBroadcastMock();
	TestWsCaptureDetach (&app);
}
END_TEST

/* Test: Socket.IO volume event */
START_TEST(test_socketio_emit_volume) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	
	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	
	BarSocketIoEmitVolume(&app, 75);
	
	ck_assert_ptr_nonnull(test_last_broadcast ());
	ck_assert(strstr(test_last_broadcast (), "2[") != NULL);
	ck_assert(strstr(test_last_broadcast (), "volume") != NULL);
	ck_assert(strstr(test_last_broadcast (), "75") != NULL);
	
	clearBroadcastMock();
	TestWsCaptureDetach (&app);
}
END_TEST

/* Test: Action dispatch - playback commands */
START_TEST(test_socketio_translate_playback_commands) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	BarSettingsInit(&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;  /* Skip mutex in tests */
	BarUiPianoHttpMutexInit(&app);
	pthread_rwlock_init(&app.stateRwlock, NULL);  /* Initialize mutex for safety */
	
	/* Create mock WebSocket context (no queue needed - commands execute directly) */
	BarWsContext_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	app.wsContext = &ctx;
	
	/* Test playback.next - commands now execute directly via BarUiDispatchById */
	/* Just verify it doesn't crash */
	BarSocketIoHandleAction(&app, "playback.next", NULL, BAR_WS_DEST_BROADCAST);
	
	/* Test playback.toggle */
	BarSocketIoHandleAction(&app, "playback.toggle", NULL, BAR_WS_DEST_BROADCAST);
	
	pthread_rwlock_destroy(&app.stateRwlock);
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
	pthread_rwlock_init(&app.stateRwlock, NULL);  /* Initialize mutex for safety */
	
	BarWsContext_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	app.wsContext = &ctx;
	
	/* Test song.love - commands now execute directly via BarUiDispatchById */
	BarSocketIoHandleAction(&app, "song.love", NULL, BAR_WS_DEST_BROADCAST);
	
	/* Test song.ban */
	BarSocketIoHandleAction(&app, "song.ban", NULL, BAR_WS_DEST_BROADCAST);
	
	/* Test song.tired */
	BarSocketIoHandleAction(&app, "song.tired", NULL, BAR_WS_DEST_BROADCAST);
	
	pthread_rwlock_destroy(&app.stateRwlock);
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
	pthread_cond_init (&app.player.audioCond, NULL);
	app.player.settings = &app.settings;
	app.player.requestedVolume = app.settings.volume;
	pthread_rwlock_init(&app.stateRwlock, NULL);  /* Initialize mutex for safety */
	
	BarWsContext_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	app.wsContext = &ctx;
	
	/* Test volume.up - commands now execute directly via BarUiDispatchById */
	BarSocketIoHandleAction(&app, "volume.up", NULL, BAR_WS_DEST_BROADCAST);
	
	/* Test volume.down */
	BarSocketIoHandleAction(&app, "volume.down", NULL, BAR_WS_DEST_BROADCAST);
	
	pthread_cond_destroy (&app.player.audioCond);
	pthread_mutex_destroy(&app.player.lock);
	pthread_rwlock_destroy(&app.stateRwlock);
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
	pthread_rwlock_init(&app.stateRwlock, NULL);  /* Initialize mutex for safety */
	
	BarWsContext_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	app.wsContext = &ctx;
	
	/* Test invalid command - should be silently ignored (not crash) */
	BarSocketIoHandleAction(&app, "invalid.command", NULL, BAR_WS_DEST_BROADCAST);
	
	pthread_rwlock_destroy(&app.stateRwlock);
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
	pthread_rwlock_init(&app.stateRwlock, NULL);  /* Initialize mutex for safety */
	
	BarWsContext_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	app.wsContext = &ctx;
	
	/* Test single-letter command - should be silently ignored (not crash) */
	BarSocketIoHandleAction(&app, "n", NULL, BAR_WS_DEST_BROADCAST);
	
	pthread_rwlock_destroy(&app.stateRwlock);
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
	pthread_rwlock_init(&app.stateRwlock, NULL);  /* Initialize mutex for safety */
	
	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock();
	
	/* Query should emit 'process' and 'stations' events */
	BarSocketIoHandleQuery(&app, BAR_WS_DEST_BROADCAST);
	
	/* At least one message should be broadcast */
	ck_assert_ptr_nonnull(test_last_broadcast ());
	ck_assert(strstr(test_last_broadcast (), "process") != NULL ||
	          strstr(lastBroadcastMessage, "stations") != NULL);
	
	pthread_rwlock_destroy(&app.stateRwlock);
	BarSettingsDestroy(&app.settings);
	
	clearBroadcastMock();
	TestWsCaptureDetach (&app);
}
END_TEST

/* Test: Client keepalive "ping" event is a no-op (no crash, no broadcast) */
START_TEST(test_socketio_ping_keepalive_noop) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	BarSettingsInit(&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;
	pthread_rwlock_init(&app.stateRwlock, NULL);

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock();

	/* Socket.IO EVENT packet: 2["ping"] - client keepalive; server must not respond */
	BarSocketIoHandleMessage(&app, "2[\"ping\"]", BAR_WS_DEST_BROADCAST);

	/* Ping is explicitly a no-op: no broadcast should be sent */
	ck_assert_ptr_null(test_last_broadcast ());

	pthread_rwlock_destroy(&app.stateRwlock);
	BarSettingsDestroy(&app.settings);
	clearBroadcastMock();
	TestWsCaptureDetach (&app);
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

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock();

	BarSocketIoHandleMessage(&app, "2[\"query.stations\"]", BAR_WS_DEST_BROADCAST);

	ck_assert_ptr_nonnull(test_last_broadcast ());
	ck_assert(strstr(test_last_broadcast (), "\"stations\"") != NULL);
	ck_assert(strstr(test_last_broadcast (), "Station One") != NULL);

	BarSettingsDestroy(&app.settings);
	clearBroadcastMock();
	TestWsCaptureDetach (&app);
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

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock();

	BarSocketIoHandleMessage(&app, "2[\"action\",{\"command\":\"app.settings\"}]", BAR_WS_DEST_BROADCAST);

	ck_assert_ptr_nonnull(test_last_broadcast ());
	ck_assert(strstr(test_last_broadcast (), "\"error\"") != NULL);
	ck_assert(strstr(test_last_broadcast (), "app.settings") != NULL);

	BarL10nDestroy(&app.l10n);
	BarSettingsDestroy(&app.settings);
	clearBroadcastMock();
	TestWsCaptureDetach (&app);
}
END_TEST

START_TEST(test_socketio_handle_station_change_missing_station_reports_error) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	BarSettingsInit(&app.settings);
	ck_assert(BarL10nInit(&app.l10n, &app.settings));

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock();

	BarSocketIoHandleMessage(&app, "2[\"station.change\",{\"id\":\"missing-station\"}]", BAR_WS_DEST_BROADCAST);

	ck_assert_ptr_nonnull(test_last_broadcast ());
	ck_assert(strstr(test_last_broadcast (), "\"error\"") != NULL);
	ck_assert(strstr(test_last_broadcast (), "missing-station") != NULL);

	BarL10nDestroy(&app.l10n);
	BarSettingsDestroy(&app.settings);
	clearBroadcastMock();
	TestWsCaptureDetach (&app);
}
END_TEST

START_TEST(test_socketio_dispatches_legacy_query_and_station_change_string) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	BarSettingsInit(&app.settings);
	ck_assert(BarL10nInit(&app.l10n, &app.settings));

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock();

	BarSocketIoHandleMessage(&app, "2[\"query.state\",{}]", BAR_WS_DEST_BROADCAST);
	ck_assert_ptr_nonnull(test_last_broadcast ());
	ck_assert(strstr(test_last_broadcast (), "[") != NULL);

	clearBroadcastMock();
	BarSocketIoHandleMessage(&app, "2[\"changeStation\",\"missing-station\"]", BAR_WS_DEST_BROADCAST);
	ck_assert_ptr_nonnull(test_last_broadcast ());
	ck_assert(strstr(test_last_broadcast (), "missing-station") != NULL);

	BarL10nDestroy(&app.l10n);
	BarSettingsDestroy(&app.settings);
	clearBroadcastMock();
	TestWsCaptureDetach (&app);
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
		BarSocketIoHandleMessage(&app, frame, BAR_WS_DEST_BROADCAST);
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
	pthread_rwlock_init(&app.stateRwlock, NULL);  /* Initialize mutex for safety */
	
	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock();
	
	BarSocketIoEmitProcess(&app);
	
	ck_assert_ptr_nonnull(test_last_broadcast ());
	ck_assert(strstr(test_last_broadcast (), "process") != NULL);
	ck_assert(strstr(test_last_broadcast (), "\"song\"") != NULL);
	ck_assert(strstr(test_last_broadcast (), "rating") != NULL);
	ck_assert(strstr(test_last_broadcast (), "1") != NULL);  /* PIANO_RATE_LOVE = 1 */
	ck_assert(strstr(test_last_broadcast (), "songStationName") != NULL);
	ck_assert(strstr(test_last_broadcast (), "Display Station") != NULL);
	
	pthread_mutex_destroy(&app.player.lock);
	pthread_rwlock_destroy(&app.stateRwlock);
	BarSettingsDestroy(&app.settings);
	clearBroadcastMock();
	TestWsCaptureDetach (&app);
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

/* Break caught: process snapshots expose a stale settings volume. */
START_TEST (test_socketio_process_payload_reads_canonical_runtime_volume)
{
	BarApp_t app = {0};
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;
	pthread_mutex_init (&app.player.lock, NULL);
	pthread_cond_init (&app.player.audioCond, NULL);
	app.player.settings = &app.settings;
	ck_assert (BarPlayerSetVolume (&app.player, 72));
	app.settings.volume = 3;
	json_object *payload = BarSocketIoBuildProcessPayload (&app), *volume;
	ck_assert_ptr_nonnull (payload);
	ck_assert (json_object_object_get_ex (payload, "volume", &volume));
	ck_assert_int_eq (json_object_get_int (volume), 72);
	json_object_put (payload);
	pthread_cond_destroy (&app.player.audioCond);
	pthread_mutex_destroy (&app.player.lock);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_socketio_emit_stations_empty_when_list_missing) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;
	app.ph.stations = NULL;

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitStations (&app);

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "stations") != NULL);
	ck_assert (strncmp (test_last_broadcast (), "2[", 2) == 0);

	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
}
END_TEST

START_TEST (test_socketio_emit_play_state_reports_paused_flag) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	ck_assert_int_eq (pthread_mutex_init (&app.player.lock, NULL), 0);
	app.player.doPause = true;

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitPlayState (&app);

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "playState") != NULL);
	ck_assert (strstr (test_last_broadcast (), "true") != NULL);

	pthread_mutex_destroy (&app.player.lock);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
}
END_TEST

START_TEST (test_socketio_emit_pandora_disconnected_includes_reason) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitPandoraDisconnected (&app, "idle_timeout");

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "idle_timeout") != NULL);

	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
}
END_TEST

START_TEST (test_socketio_handle_change_station_switches_when_found) {
	BarApp_t app;
	PianoStation_t station;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit (&app);
	ck_assert_int_eq (pthread_mutex_init (&app.player.lock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app.player.cond, NULL), 0);
	ck_assert_int_eq (pthread_mutex_init (&app.player.decoderLock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app.player.decoderCond, NULL), 0);
	app.player.settings = &app.settings;
	app.lastStationId = strdup ("old-station");
	station.id = "live-station";
	station.name = "Live Station";
	app.ph.stations = &station;

	BarSocketIoHandleChangeStation (&app, "live-station");

	ck_assert_ptr_eq (BarStateGetNextStation (&app), &station);
	ck_assert_ptr_null (BarStateGetPlaylist (&app));
	ck_assert_ptr_null (app.lastStationId);

	pthread_cond_destroy (&app.player.decoderCond);
	pthread_mutex_destroy (&app.player.decoderLock);
	pthread_cond_destroy (&app.player.cond);
	pthread_mutex_destroy (&app.player.lock);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static bool
mock_set_quickmix_ok (BarApp_t *app, const PianoRequestType_t type, void *data,
                      PianoReturn_t *pRet, CURLcode *wRet)
{
	(void)data;
	ck_assert_int_eq (type, PIANO_REQUEST_SET_QUICKMIX);
	ck_assert_ptr_nonnull (app->ph.stations);
	PianoStation_t *first = app->ph.stations;
	PianoStation_t *second = (PianoStation_t *)first->head.next;
	ck_assert_ptr_nonnull (second);
	ck_assert (!first->useQuickMix);
	ck_assert (second->useQuickMix);
	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;
	return true;
}

static bool
mock_set_quickmix_unexpected (BarApp_t *app, const PianoRequestType_t type,
                              void *data, PianoReturn_t *pRet, CURLcode *wRet)
{
	(void)app;
	(void)type;
	(void)data;
	(void)pRet;
	(void)wRet;
	ck_abort_msg ("oversized QuickMix payload must not reach Pandora save");
	return false;
}

static bool
mock_set_quickmix_empty_ok (BarApp_t *app, const PianoRequestType_t type,
                            void *data, PianoReturn_t *pRet, CURLcode *wRet)
{
	(void)data;
	ck_assert_int_eq (type, PIANO_REQUEST_SET_QUICKMIX);
	ck_assert_ptr_nonnull (app->ph.stations);
	PianoStation_t *first = app->ph.stations;
	PianoStation_t *second = (PianoStation_t *)first->head.next;
	ck_assert_ptr_nonnull (second);
	ck_assert (!first->useQuickMix);
	ck_assert (!second->useQuickMix);
	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;
	return true;
}

START_TEST (test_socketio_handle_set_quickmix_applies_before_save) {
	BarApp_t app;
	PianoStation_t first, second;
	json_object *data;
	memset (&app, 0, sizeof (app));
	memset (&first, 0, sizeof (first));
	memset (&second, 0, sizeof (second));
	BarSettingsInit (&app.settings);
	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);
	first.id = "S1";
	first.name = "Station One";
	first.useQuickMix = true;
	first.head.next = &second.head;
	second.id = "S2";
	second.name = "Station Two";
	second.useQuickMix = false;
	app.ph.stations = &first;

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_set_quickmix_ok);
	data = json_object_new_array ();
	json_object_array_add (data, json_object_new_string ("S2"));
	BarSocketIoHandleSetQuickMix (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	ck_assert (!first.useQuickMix);
	ck_assert (second.useQuickMix);
	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "stations") != NULL);

	clearBroadcastMock ();
	app.ph.stations = NULL;
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
	TestWsCaptureDetach (&app);
}
END_TEST

START_TEST (test_socketio_handle_set_quickmix_empty_clears_all) {
	BarApp_t app;
	PianoStation_t first, second;
	json_object *data;
	memset (&app, 0, sizeof (app));
	memset (&first, 0, sizeof (first));
	memset (&second, 0, sizeof (second));
	BarSettingsInit (&app.settings);
	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);
	first.id = "S1";
	first.name = "Station One";
	first.useQuickMix = true;
	first.head.next = &second.head;
	second.id = "S2";
	second.name = "Station Two";
	second.useQuickMix = true;
	app.ph.stations = &first;

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_set_quickmix_empty_ok);
	data = json_object_new_array ();
	ck_assert_ptr_nonnull (data);
	BarSocketIoHandleSetQuickMix (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	ck_assert (!first.useQuickMix);
	ck_assert (!second.useQuickMix);
	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "stations") != NULL);

	clearBroadcastMock ();
	app.ph.stations = NULL;
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
	TestWsCaptureDetach (&app);
}
END_TEST

START_TEST (test_socketio_handle_set_quickmix_rejects_too_many_ids) {
	BarApp_t app;
	PianoStation_t first, second;
	json_object *data;
	memset (&app, 0, sizeof (app));
	memset (&first, 0, sizeof (first));
	memset (&second, 0, sizeof (second));
	BarSettingsInit (&app.settings);
	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);
	first.id = "S1";
	first.name = "Station One";
	first.useQuickMix = true;
	first.head.next = &second.head;
	second.id = "S2";
	second.name = "Station Two";
	second.useQuickMix = false;
	app.ph.stations = &first;

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_set_quickmix_unexpected);
	data = json_object_new_array ();
	ck_assert_ptr_nonnull (data);
	for (size_t i = 0; i < 1025; i++) {
		char stationId[32];
		snprintf (stationId, sizeof (stationId), "S%zu", i);
		json_object_array_add (data, json_object_new_string (stationId));
	}
	BarSocketIoHandleSetQuickMix (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	ck_assert (first.useQuickMix);
	ck_assert (!second.useQuickMix);
	ck_assert_ptr_null (test_last_broadcast ());

	clearBroadcastMock ();
	app.ph.stations = NULL;
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
	TestWsCaptureDetach (&app);
}
END_TEST

START_TEST (test_socketio_handle_set_quickmix_skips_non_string_ids) {
	BarApp_t app;
	PianoStation_t first, second;
	json_object *data;
	memset (&app, 0, sizeof (app));
	memset (&first, 0, sizeof (first));
	memset (&second, 0, sizeof (second));
	BarSettingsInit (&app.settings);
	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);
	first.id = "S1";
	first.name = "Station One";
	first.useQuickMix = true;
	first.head.next = &second.head;
	second.id = "S2";
	second.name = "Station Two";
	second.useQuickMix = false;
	app.ph.stations = &first;

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_set_quickmix_ok);
	data = json_object_new_array ();
	json_object_array_add (data, json_object_new_int (1));
	json_object_array_add (data, json_object_new_string ("S2"));
	BarSocketIoHandleSetQuickMix (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	ck_assert (!first.useQuickMix);
	ck_assert (second.useQuickMix);

	clearBroadcastMock ();
	app.ph.stations = NULL;
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
	TestWsCaptureDetach (&app);
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
	ck_assert_int_eq (pthread_cond_init (&app.player.audioCond, NULL), 0);
	app.player.requestedVolume = 50;
	ck_assert_int_eq (pthread_mutex_init (&ctx.volumeBroadcastMutex, NULL), 0);
	app.player.settings = &app.settings;
	app.wsContext = &ctx;

	data = json_object_new_object ();
	json_object_object_add (data, "volume", json_object_new_int (33));

	BarSocketIoHandleAction (&app, "volume.set", data, BAR_WS_DEST_BROADCAST);

	ck_assert_int_eq (app.settings.volume, 33);
	pthread_mutex_lock (&ctx.volumeBroadcastMutex);
	ck_assert (ctx.delayedVolumeBroadcast.pending);
	pthread_mutex_unlock (&ctx.volumeBroadcastMutex);

	json_object_put (data);
	pthread_mutex_destroy (&ctx.volumeBroadcastMutex);
	pthread_cond_destroy (&app.player.audioCond);
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
	ck_assert_int_eq (pthread_cond_init (&app.player.audioCond, NULL), 0);
	ck_assert_int_eq (pthread_mutex_init (&ctx.volumeBroadcastMutex, NULL), 0);
	app.player.settings = &app.settings;
	app.wsContext = &ctx;

	data = json_object_new_object ();
	json_object_object_add (data, "volume", json_object_new_int (150));
	BarSocketIoHandleAction (&app, "volume.set", data, BAR_WS_DEST_BROADCAST);
	ck_assert_int_eq (app.settings.volume, 100);

	json_object_put (data);
	pthread_mutex_destroy (&ctx.volumeBroadcastMutex);
	pthread_cond_destroy (&app.player.audioCond);
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

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoHandleAction (&app, "not.a.real.action", NULL, BAR_WS_DEST_BROADCAST);
	ck_assert_ptr_null (test_last_broadcast ());

	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
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

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitProcess (&app);

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "process") != NULL);
	ck_assert (strstr (test_last_broadcast (), "accounts") != NULL);
	ck_assert (strstr (test_last_broadcast (), "alpha") != NULL);
	ck_assert (strstr (test_last_broadcast (), "beta") != NULL);
	ck_assert (strstr (test_last_broadcast (), "current_account") != NULL);
	ck_assert (strstr (test_last_broadcast (), "Beta") != NULL);

	free_settings_accounts (&app);
	BarSettingsDestroy (&app.settings);
	pthread_mutex_destroy (&app.player.lock);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
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

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	json_object *data = json_object_new_object ();
	json_object_object_add (data, "account_id", json_object_new_string ("ghost"));
	BarSocketIoHandleAction (&app, "app.pandora-reconnect", data, BAR_WS_DEST_BROADCAST);
	json_object_put (data);

	ck_assert_uint_eq (app.settings.activeAccountIndex, 0);
	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "error") != NULL);
	ck_assert (strstr (test_last_broadcast (), "ghost") != NULL);

	free_settings_accounts (&app);
	BarUiPianoHttpMutexDestroy (&app);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
}
END_TEST

/* BarSocketIoEmitErrorEx includes stationId in JSON when set */
START_TEST (test_socketio_emit_error_ex_with_station_id) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitErrorEx (&app, "station.rename", "Network error", "station-42");

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "error") != NULL);
	ck_assert (strstr (test_last_broadcast (), "stationId") != NULL);
	ck_assert (strstr (test_last_broadcast (), "station-42") != NULL);

	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
}
END_TEST

/* Null message: no broadcast */
START_TEST (test_socketio_emit_error_ex_null_message) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitErrorEx (&app, "op", NULL, "sid");

	ck_assert_ptr_null (test_last_broadcast ());

	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
}
END_TEST

/* app NULL: no server to send through; must not crash or leak */
START_TEST (test_socketio_emit_error_ex_null_app) {
	clearBroadcastMock ();

	BarSocketIoEmitErrorEx (NULL, "music.search", "Network error", NULL);

	ck_assert_ptr_null (test_last_broadcast ());
}
END_TEST

/* Empty stationId: omit stationId field */
START_TEST (test_socketio_emit_error_ex_empty_station_id) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitErrorEx (&app, "station.change", "Station not found", "");

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "error") != NULL);
	ck_assert (strstr (test_last_broadcast (), "stationId") == NULL);

	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
}
END_TEST

/* BarSocketIoEmitError delegates to EmitErrorEx with NULL stationId */
START_TEST (test_socketio_emit_error_wrapper) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitError (&app, "playback.play", "Not connected to Pandora");

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "error") != NULL);
	ck_assert (strstr (test_last_broadcast (), "stationId") == NULL);

	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
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

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);

	for (size_t i = 0; unimplemented[i] != NULL; i++) {
		clearBroadcastMock ();
		BarSocketIoHandleAction (&app, unimplemented[i], NULL, BAR_WS_DEST_BROADCAST);
		ck_assert_msg (test_last_broadcast () != NULL,
		               "Expected error broadcast for action '%s'", unimplemented[i]);
		ck_assert_msg (strstr (test_last_broadcast (), "\"error\"") != NULL,
		               "Expected 'error' event for action '%s'", unimplemented[i]);
		ck_assert_msg (strstr (test_last_broadcast (), unimplemented[i]) != NULL,
		               "Expected operation name in error payload for '%s'", unimplemented[i]);
	}

	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
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

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitExplanationTo (&app, test_ws_cap.ids[0], "Because you liked jazz.");

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "song.explanation") != NULL);
	ck_assert (strstr (test_last_broadcast (), "Because you liked jazz.") != NULL);

	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
}
END_TEST

START_TEST (test_socketio_emit_explanation_null_args_noop)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitExplanationTo (NULL, test_ws_cap.ids[0], "ignored");
	BarSocketIoEmitExplanationTo (&app, test_ws_cap.ids[0], NULL);

	ck_assert_ptr_null (test_last_broadcast ());
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
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
	/* The queue after the playing song is read from state */
	PianoSong_t playing;
	memset (&playing, 0, sizeof (playing));
	playing.head.next = &first.head;
	app.playlist = &playing;

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	ck_assert (BarSocketIoEmitUpcomingTo (&app, test_ws_cap.ids[0], 5));

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "query.upcoming.result") != NULL);
	ck_assert (strstr (test_last_broadcast (), "First") != NULL);
	ck_assert (strstr (test_last_broadcast (), "Second") != NULL);

	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
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

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitGenres (&app);

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "genres") != NULL);
	ck_assert (strstr (test_last_broadcast (), "Classic Rock") != NULL);

	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
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

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoHandleGetGenres (&app);

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "genres") != NULL);
	ck_assert (strstr (test_last_broadcast (), "Top Pop") != NULL);

	BarUiPianoHttpMutexDestroy (&app);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
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
	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoHandleGetGenres (&app);

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "error") != NULL);

	BarUiPianoCallClearTestHook ();
	BarUiPianoHttpMutexDestroy (&app);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
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

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoHandleMessage (&app, "2[\"station.getGenres\"]", BAR_WS_DEST_BROADCAST);

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "genres") != NULL);

	BarUiPianoHttpMutexDestroy (&app);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
}
END_TEST

START_TEST (test_socketio_emit_process_to_targets_client)
{
	BarApp_t app;
	PianoSong_t song;
	PianoStation_t station;
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

	TestWsCaptureAttach (&app, 2, mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoEmitProcessTo (&app, test_ws_cap.ids[1]);
	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "process") != NULL);
	ck_assert (strstr (test_last_broadcast (), "Unicast Song") != NULL);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 1);
	ck_assert_int_eq (test_ws_cap.frameClient[0], 1);

	pthread_mutex_destroy (&app.player.lock);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
}
END_TEST

START_TEST (test_socketio_handle_add_genre_missing_music_id)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	BarUiPianoHttpMutexInit (&app);

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();

	BarSocketIoHandleAddGenre (&app, json_object_new_object ());

	ck_assert_ptr_null (test_last_broadcast ());

	BarUiPianoHttpMutexDestroy (&app);
	BarSettingsDestroy (&app.settings);
	clearBroadcastMock ();
	TestWsCaptureDetach (&app);
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
	BarSocketIoHandleAction (&app, "volume.set", data, BAR_WS_DEST_BROADCAST);

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

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_rename_station);
	data = json_object_new_object ();
	json_object_object_add (data, "stationId", json_object_new_string ("S1"));
	json_object_object_add (data, "newName", json_object_new_string ("Renamed"));
	BarSocketIoHandleRenameStation (&app, data);
	BarUiPianoCallClearTestHook ();

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "stations") != NULL);

	json_object_put (data);
	clearBroadcastMock ();
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
	TestWsCaptureDetach (&app);
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

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_delete_station);
	data = json_object_new_object ();
	json_object_object_add (data, "stationId", json_object_new_string ("S1"));
	BarSocketIoHandleDeleteStation (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "stations") != NULL);
	clearBroadcastMock ();
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
	TestWsCaptureDetach (&app);
}
END_TEST

START_TEST (test_socketio_handle_add_music_missing_fields_noop)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();
	BarSocketIoHandleAddMusic (&app, json_object_new_object ());
	ck_assert_ptr_null (test_last_broadcast ());
	clearBroadcastMock ();
	BarSettingsDestroy (&app.settings);
	TestWsCaptureDetach (&app);
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
	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_create_station_ok);
	data = json_object_new_object ();
	json_object_object_add (data, "stationId", json_object_new_string ("12345"));
	BarSocketIoHandleAddShared (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "stations") != NULL);
	clearBroadcastMock ();
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
	TestWsCaptureDetach (&app);
}
END_TEST

START_TEST (test_socketio_handle_add_shared_rejects_non_digit_station_id)
{
	BarApp_t app;
	json_object *data;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();
	data = json_object_new_object ();
	json_object_object_add (data, "stationId", json_object_new_string ("abc"));
	BarSocketIoHandleAddShared (&app, data);
	json_object_put (data);
	ck_assert_ptr_null (test_last_broadcast ());
	clearBroadcastMock ();
	BarSettingsDestroy (&app.settings);
	TestWsCaptureDetach (&app);
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
	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_create_station_ok);
	data = json_object_new_object ();
	json_object_object_add (data, "trackToken", json_object_new_string ("TT:1"));
	json_object_object_add (data, "type", json_object_new_string ("song"));
	BarSocketIoHandleCreateStationFrom (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	ck_assert_ptr_nonnull (test_last_broadcast ());
	clearBroadcastMock ();
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
	TestWsCaptureDetach (&app);
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

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_get_station_modes_ok);
	data = json_object_new_object ();
	json_object_object_add (data, "stationId", json_object_new_string ("S1"));
	BarSocketIoHandleGetStationModes (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "stationModes") != NULL);
	clearBroadcastMock ();
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
	TestWsCaptureDetach (&app);
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

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();
	data = json_object_new_object ();
	json_object_object_add (data, "stationId", json_object_new_string ("QM"));
	BarSocketIoHandleGetStationModes (&app, data);
	json_object_put (data);
	ck_assert_ptr_null (test_last_broadcast ());
	clearBroadcastMock ();
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
	TestWsCaptureDetach (&app);
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

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_get_station_info_ok);
	data = json_object_new_object ();
	json_object_object_add (data, "stationId", json_object_new_string ("S1"));
	BarSocketIoHandleGetStationInfo (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "stationInfo") != NULL);
	clearBroadcastMock ();
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
	TestWsCaptureDetach (&app);
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

	TestWsCaptureAttach (&app, 1, mockBroadcastCallback);
	clearBroadcastMock ();
	BarUiPianoCallSetTestHook (mock_search_music_ok);
	data = json_object_new_object ();
	json_object_object_add (data, "query", json_object_new_string ("beatles"));
	BarSocketIoHandleSearchMusic (&app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);

	ck_assert_ptr_nonnull (test_last_broadcast ());
	ck_assert (strstr (test_last_broadcast (), "searchResults") != NULL);
	clearBroadcastMock ();
	BarUiPianoHttpMutexDestroy (&app);
	BarSettingsDestroy (&app.settings);
	TestWsCaptureDetach (&app);
}
END_TEST

/* --- Destination capture: two fake clients, every delivered frame recorded --- */

/* Captured frame `i` went to `client` and carries `event`. */
static void assert_frame (size_t i, int client, const char *event) {
	TestWsCaptureFlush ();
	ck_assert_uint_lt (i, test_ws_cap.frameCount);
	ck_assert_int_eq (test_ws_cap.frameClient[i], client);
	ck_assert_int_eq (test_ws_cap.frames[i][0], '2');
	json_object *arr = json_tokener_parse (test_ws_cap.frames[i] + 1);
	ck_assert_ptr_nonnull (arr);
	ck_assert_str_eq (json_object_get_string (json_object_array_get_idx (arr, 0)), event);
	json_object_put (arr);
}

/* Payload (second array element) of captured frame `i`; caller puts it. */
static json_object *frame_payload (size_t i) {
	json_object *arr = json_tokener_parse (test_ws_cap.frames[i] + 1);
	ck_assert_ptr_nonnull (arr);
	json_object *payload = json_object_get (json_object_array_get_idx (arr, 1));
	json_object_put (arr);
	return payload;
}

/* Broadcast frame `event` reached exactly clients 0 and 1, once each. */
static void assert_broadcast_frame (const char *event) {
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 2);
	assert_frame (0, 0, event);
	assert_frame (1, 1, event);
	ck_assert_str_eq (test_ws_cap.frames[0], test_ws_cap.frames[1]);
}

static void capture_app_init (BarApp_t *app, PianoStation_t *station, PianoSong_t *song) {
	memset (app, 0, sizeof (*app));
	memset (station, 0, sizeof (*station));
	memset (song, 0, sizeof (*song));
	BarSettingsInit (&app->settings);
	app->settings.uiMode = BAR_UI_MODE_WEB;
	app->settings.volumeMode = BAR_VOLUME_MODE_PLAYER;
	BarStateInit (app);
	ck_assert_int_eq (pthread_mutex_init (&app->player.lock, NULL), 0);
	station->id = "S1";
	station->name = "Station One";
	song->title = "Song One";
	song->artist = "Artist One";
	song->stationId = station->id;
	app->ph.stations = station;
	BarStateSetCurrentStation (app, station);
	TestWsCaptureAttach (app, 2, NULL);
}

static void capture_app_destroy (BarApp_t *app) {
	TestWsCaptureDetach (app);
	pthread_mutex_destroy (&app->player.lock);
	BarStateDestroy (app);
	BarSettingsDestroy (&app->settings);
}

START_TEST (test_query_process_reaches_only_requester) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);
	BarStateSetPlaylist (&app, &song);

	BarSocketIoHandleQuery (&app, test_ws_cap.ids[1]);

	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 2);
	assert_frame (0, 1, "process");
	assert_frame (1, 1, "stations");
	ck_assert_uint_eq (TestWsCaptureCount (0, ""), 0);
	json_object *process = frame_payload (0);
	json_object *songObj, *title;
	ck_assert (json_object_object_get_ex (process, "song", &songObj));
	ck_assert (json_object_object_get_ex (songObj, "title", &title));
	ck_assert_str_eq (json_object_get_string (title), "Song One");
	json_object_put (process);

	capture_app_destroy (&app);
}
END_TEST

START_TEST (test_query_message_routes_reply_to_sender) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);

	/* Socket.IO CONNECT and the query event both reply to the sender only */
	BarSocketIoHandleMessage (&app, "0", test_ws_cap.ids[0]);
	BarSocketIoHandleMessage (&app, "2[\"query\"]", test_ws_cap.ids[0]);

	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 4);
	ck_assert_uint_eq (TestWsCaptureCount (0, "process"), 2);
	ck_assert_uint_eq (TestWsCaptureCount (0, "stations"), 2);
	ck_assert_uint_eq (TestWsCaptureCount (1, ""), 0);

	capture_app_destroy (&app);
}
END_TEST

START_TEST (test_state_broadcast_reaches_both_clients) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);

	BarSocketIoEmitProcess (&app);
	assert_broadcast_frame ("process");

	capture_app_destroy (&app);
}
END_TEST

START_TEST (test_capture_playback_start_stop_broadcast) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);
	BarStateSetPlaylist (&app, &song);

	BarSocketIoEmitStart (&app);
	assert_broadcast_frame ("start");
	json_object *start = frame_payload (0);
	json_object *field;
	ck_assert (json_object_object_get_ex (start, "title", &field));
	ck_assert_str_eq (json_object_get_string (field), "Song One");
	ck_assert (json_object_object_get_ex (start, "stationId", &field));
	ck_assert_str_eq (json_object_get_string (field), "S1");
	json_object_put (start);

	TestWsCaptureClear ();
	BarSocketIoEmitStop (&app);
	assert_broadcast_frame ("stop");
	ck_assert_ptr_null (frame_payload (0));

	capture_app_destroy (&app);
}
END_TEST

START_TEST (test_capture_progress_broadcast) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);

	BarSocketIoEmitProgress (&app, 30, 120);
	assert_broadcast_frame ("progress");
	json_object *p = frame_payload (0);
	json_object *field;
	ck_assert (json_object_object_get_ex (p, "elapsed", &field));
	ck_assert_int_eq (json_object_get_int (field), 30);
	ck_assert (json_object_object_get_ex (p, "duration", &field));
	ck_assert_int_eq (json_object_get_int (field), 120);
	ck_assert (json_object_object_get_ex (p, "percentage", &field));
	ck_assert (fabs (json_object_get_double (field) - 25.0) < 0.001);
	json_object_put (p);

	capture_app_destroy (&app);
}
END_TEST

START_TEST (test_capture_volume_broadcast) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);

	BarSocketIoEmitVolume (&app, 64);
	assert_broadcast_frame ("volume");
	json_object *v = frame_payload (0);
	ck_assert (json_object_is_type (v, json_type_int));
	ck_assert_int_eq (json_object_get_int (v), 64);
	json_object_put (v);

	capture_app_destroy (&app);
}
END_TEST

START_TEST (test_capture_stations_broadcast) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);

	BarSocketIoEmitStations (&app);
	assert_broadcast_frame ("stations");
	json_object *list = frame_payload (0);
	ck_assert_int_eq (json_object_array_length (list), 1);
	json_object *id;
	ck_assert (json_object_object_get_ex (json_object_array_get_idx (list, 0), "id", &id));
	ck_assert_str_eq (json_object_get_string (id), "S1");
	json_object_put (list);

	capture_app_destroy (&app);
}
END_TEST

START_TEST (test_capture_error_broadcast_and_to_requester) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);

	BarSocketIoEmitErrorEx (&app, "station.change", "Station not found", "S9");
	assert_broadcast_frame ("error");
	json_object *e = frame_payload (0);
	json_object *field;
	ck_assert (json_object_object_get_ex (e, "operation", &field));
	ck_assert_str_eq (json_object_get_string (field), "station.change");
	ck_assert (json_object_object_get_ex (e, "stationId", &field));
	ck_assert_str_eq (json_object_get_string (field), "S9");
	json_object_put (e);

	TestWsCaptureClear ();
	BarSocketIoEmitErrorTo (&app, test_ws_cap.ids[1], "query.upcoming", "No songs in queue");
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 1);
	assert_frame (0, 1, "error");
	e = frame_payload (0);
	ck_assert (json_object_object_get_ex (e, "operation", &field));
	ck_assert_str_eq (json_object_get_string (field), "query.upcoming");
	json_object_put (e);

	capture_app_destroy (&app);
}
END_TEST

START_TEST (test_capture_pandora_disconnected_broadcast) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);

	BarSocketIoEmitPandoraDisconnected (&app, "connection_lost");
	assert_broadcast_frame ("pandora.disconnected");
	json_object *d = frame_payload (0);
	json_object *reason;
	ck_assert (json_object_object_get_ex (d, "reason", &reason));
	ck_assert_str_eq (json_object_get_string (reason), "connection_lost");
	json_object_put (d);

	capture_app_destroy (&app);
}
END_TEST

START_TEST (test_capture_explanation_and_upcoming_to_requester) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);

	BarSocketIoEmitExplanationTo (&app, test_ws_cap.ids[0], "Because jazz.");
	PianoSong_t playing;
	memset (&playing, 0, sizeof (playing));
	playing.head.next = &song.head;
	app.playlist = &playing;
	ck_assert (BarSocketIoEmitUpcomingTo (&app, test_ws_cap.ids[0], 5));
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 2);
	assert_frame (0, 0, "song.explanation");
	assert_frame (1, 0, "query.upcoming.result");
	ck_assert (strstr (test_ws_cap.frames[0], "Because jazz.") != NULL);
	ck_assert (strstr (test_ws_cap.frames[1], "Song One") != NULL);

	/* A requester that has gone away gets nothing, and nobody else does */
	TestWsCaptureClear ();
	BarSocketIoEmitProcessTo (&app, test_ws_cap.ids[1] + 100);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 0);

	capture_app_destroy (&app);
}
END_TEST

static bool mockExplainText (BarApp_t *app, const PianoRequestType_t type, void *data,
		PianoReturn_t *pRet, CURLcode *wRet) {
	(void) app;
	if (type == PIANO_REQUEST_EXPLAIN) {
		((PianoRequestDataExplain_t *) data)->retExplain = strdup ("Because routing.");
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_action_query_replies_reach_only_requester) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);
	BarUiPianoHttpMutexInit (&app);
	BarStateSetPlaylist (&app, &song);
	app.ph.user.listenerId = "listener";

	BarUiPianoCallSetTestHook (mockExplainText);
	BarSocketIoHandleAction (&app, "song.explain", NULL, test_ws_cap.ids[1]);
	BarUiPianoCallClearTestHook ();
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 1);
	assert_frame (0, 1, "song.explanation");
	ck_assert (strstr (test_ws_cap.frames[0], "Because routing.") != NULL);

	/* Only one song queued: the "no songs" error goes to the requester only */
	TestWsCaptureClear ();
	BarSocketIoHandleAction (&app, "query.upcoming", NULL, test_ws_cap.ids[0]);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 1);
	assert_frame (0, 0, "error");
	ck_assert (strstr (test_ws_cap.frames[0], "query.upcoming") != NULL);

	app.ph.user.listenerId = NULL;
	BarUiPianoHttpMutexDestroy (&app);
	capture_app_destroy (&app);
}
END_TEST

static bool mockSetQuickMixAccept (BarApp_t *app, const PianoRequestType_t type,
		void *data, PianoReturn_t *pRet, CURLcode *wRet) {
	(void) app;
	(void) data;
	ck_assert_int_eq (type, PIANO_REQUEST_SET_QUICKMIX);
	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;
	return true;
}

/* Frames parsed from the wire reach their handlers with the sender's ID;
 * these replies are broadcasts today and must stay broadcasts. */
START_TEST (test_message_events_dispatch_from_requester) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);
	BarUiPianoHttpMutexInit (&app);
	BarWsConnId_t sender = test_ws_cap.ids[0];

	/* "action" with a string payload */
	BarSocketIoHandleMessage (&app, "2[\"action\",\"app.settings\"]", sender);
	assert_broadcast_frame ("error");
	ck_assert (strstr (test_ws_cap.frames[0], "app.settings") != NULL);

	/* "action" with an object payload naming the action */
	TestWsCaptureClear ();
	BarSocketIoHandleMessage (&app, "2[\"action\",{\"action\":\"app.settings\"}]", sender);
	assert_broadcast_frame ("error");
	ck_assert (strstr (test_ws_cap.frames[0], "app.settings") != NULL);

	/* Table-dispatched events */
	TestWsCaptureClear ();
	BarUiPianoCallSetTestHook (mock_get_station_modes_ok);
	BarSocketIoHandleMessage (&app, "2[\"station.getModes\",{\"stationId\":\"S1\"}]", sender);
	BarUiPianoCallClearTestHook ();
	assert_broadcast_frame ("stationModes");
	ck_assert (strstr (test_ws_cap.frames[0], "My Mode") != NULL);

	TestWsCaptureClear ();
	station.useQuickMix = false;
	BarUiPianoCallSetTestHook (mockSetQuickMixAccept);
	BarSocketIoHandleMessage (&app, "2[\"station.setQuickMix\",[\"S1\"]]", sender);
	BarUiPianoCallClearTestHook ();
	ck_assert (station.useQuickMix);
	assert_broadcast_frame ("stations");

	BarUiPianoHttpMutexDestroy (&app);
	capture_app_destroy (&app);
}
END_TEST

/* --- Borrowed libpiano pointers: handlers resolve them inside the
 * pianoHttpMutex critical section (THREAD_SAFETY.md, handler pointer audit) */

static void *piano_mutex_probe (void *arg) {
	BarApp_t *app = arg;
	const int rc = pthread_mutex_trylock (&app->pianoHttpMutex);
	if (rc == 0) {
		pthread_mutex_unlock (&app->pianoHttpMutex);
	}
	return (void *) (intptr_t) rc;
}

/* pianoHttpMutex is recursive, so the owner cannot test it; ask another thread */
static bool piano_mutex_held_by_caller (BarApp_t *app) {
	pthread_t probe;
	void *rc = NULL;
	ck_assert_int_eq (pthread_create (&probe, NULL, piano_mutex_probe, app), 0);
	ck_assert_int_eq (pthread_join (probe, &rc), 0);
	return (intptr_t) rc == EBUSY;
}

static unsigned g_pianoCallsLocked;
static unsigned g_pianoCallsUnlocked;

/* The hook replaces BarUiPianoCall, so the mutex is held only when the handler
 * took it itself around the station lookup and the call. */
static bool mock_piano_call_records_mutex (BarApp_t *app, const PianoRequestType_t type,
		void *data, PianoReturn_t *pRet, CURLcode *wRet) {
	(void) type;
	(void) data;
	if (piano_mutex_held_by_caller (app)) {
		g_pianoCallsLocked++;
	} else {
		g_pianoCallsUnlocked++;
	}
	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;
	return true;
}

typedef void (*BarTestStationHandler_fn) (BarApp_t *, json_object *);

static void run_station_handler_under_probe (BarApp_t *app,
		BarTestStationHandler_fn handler, const char *json, const char *name) {
	json_object *data = json_tokener_parse (json);
	ck_assert_ptr_nonnull (data);
	g_pianoCallsLocked = 0;
	g_pianoCallsUnlocked = 0;
	BarUiPianoCallSetTestHook (mock_piano_call_records_mutex);
	handler (app, data);
	BarUiPianoCallClearTestHook ();
	json_object_put (data);
	ck_assert_msg (g_pianoCallsUnlocked == 0,
			"%s used a station pointer outside pianoHttpMutex", name);
	ck_assert_msg (g_pianoCallsLocked > 0, "%s made no Pandora call", name);
}

START_TEST (test_station_handlers_hold_piano_mutex_across_station_use) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);
	BarUiPianoHttpMutexInit (&app);
	station.isCreator = true;
	/* Not the current station, so delete does not switch stations */
	BarStateSetCurrentStation (&app, NULL);

	run_station_handler_under_probe (&app, BarSocketIoHandleAddMusic,
			"{\"musicId\":\"M1\",\"stationId\":\"S1\"}", "addMusic");
	run_station_handler_under_probe (&app, BarSocketIoHandleRenameStation,
			"{\"stationId\":\"S1\",\"newName\":\"New\"}", "rename");
	run_station_handler_under_probe (&app, BarSocketIoHandleGetStationModes,
			"{\"stationId\":\"S1\"}", "getModes");
	run_station_handler_under_probe (&app, BarSocketIoHandleSetStationMode,
			"{\"stationId\":\"S1\",\"modeId\":1}", "setMode");
	run_station_handler_under_probe (&app, BarSocketIoHandleGetStationInfo,
			"{\"stationId\":\"S1\"}", "getInfo");
	run_station_handler_under_probe (&app, BarSocketIoHandleDeleteSeed,
			"{\"seedId\":\"X\",\"seedType\":\"artist\",\"stationId\":\"S1\"}", "deleteSeed");
	run_station_handler_under_probe (&app, BarSocketIoHandleDeleteFeedback,
			"{\"feedbackId\":\"F\",\"stationId\":\"S1\"}", "deleteFeedback");
	run_station_handler_under_probe (&app, BarSocketIoHandleDeleteStation,
			"{\"stationId\":\"S1\"}", "delete");

	BarUiPianoHttpMutexDestroy (&app);
	capture_app_destroy (&app);
}
END_TEST

/* Shared stations are transformed first; both calls stay in one critical section */
START_TEST (test_add_music_transform_and_seed_share_piano_mutex) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);
	BarUiPianoHttpMutexInit (&app);
	station.isCreator = false;

	run_station_handler_under_probe (&app, BarSocketIoHandleAddMusic,
			"{\"musicId\":\"M1\",\"stationId\":\"S1\"}", "addMusic");
	ck_assert_uint_eq (g_pianoCallsLocked, 2);

	BarUiPianoHttpMutexDestroy (&app);
	capture_app_destroy (&app);
}
END_TEST

/* Deleting the playing station switches to QuickMix by ID after the delete */
START_TEST (test_delete_current_station_switches_to_quickmix) {
	BarApp_t app;
	PianoStation_t station, quickMix;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);
	BarUiPianoHttpMutexInit (&app);
	ck_assert_int_eq (pthread_cond_init (&app.player.cond, NULL), 0);
	ck_assert_int_eq (pthread_mutex_init (&app.player.decoderLock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app.player.decoderCond, NULL), 0);
	app.player.settings = &app.settings;
	memset (&quickMix, 0, sizeof (quickMix));
	quickMix.id = "QM";
	quickMix.name = "QuickMix";
	quickMix.isQuickMix = true;
	station.head.next = &quickMix.head;

	run_station_handler_under_probe (&app, BarSocketIoHandleDeleteStation,
			"{\"stationId\":\"S1\"}", "delete");
	/* Clearing current/next is the delete response's job (BarStatePianoResponse),
	 * which the hook bypasses */
	ck_assert_ptr_eq (BarStateGetNextStation (&app), &quickMix);

	pthread_cond_destroy (&app.player.decoderCond);
	pthread_mutex_destroy (&app.player.decoderLock);
	pthread_cond_destroy (&app.player.cond);
	BarUiPianoHttpMutexDestroy (&app);
	capture_app_destroy (&app);
}
END_TEST

START_TEST (test_get_genres_fetch_holds_piano_mutex) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);
	BarUiPianoHttpMutexInit (&app);

	g_pianoCallsLocked = 0;
	g_pianoCallsUnlocked = 0;
	BarUiPianoCallSetTestHook (mock_piano_call_records_mutex);
	BarSocketIoHandleGetGenres (&app);
	BarUiPianoCallClearTestHook ();
	ck_assert_uint_eq (g_pianoCallsLocked, 1);
	ck_assert_uint_eq (g_pianoCallsUnlocked, 0);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (TestWsCaptureCount (0, "genres"), 1);

	BarUiPianoHttpMutexDestroy (&app);
	capture_app_destroy (&app);
}
END_TEST

/* B1: song actions resolve the current song and station inside
 * pianoHttpMutex and keep it for the whole Pandora call */
START_TEST (test_song_actions_hold_piano_mutex_across_song_use) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);
	BarUiPianoHttpMutexInit (&app);
	ck_assert_int_eq (pthread_cond_init (&app.player.cond, NULL), 0);
	ck_assert_int_eq (pthread_mutex_init (&app.player.decoderLock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app.player.decoderCond, NULL), 0);
	app.player.settings = &app.settings;
	station.isCreator = true;
	song.trackToken = "token";
	app.playlist = &song;
	app.ph.user.listenerId = "listener";

	static const char * const actions[] = {
		"song.love", "song.ban", "song.tired", "song.explain",
	};
	for (size_t i = 0; i < sizeof (actions) / sizeof (actions[0]); i++) {
		g_pianoCallsLocked = 0;
		g_pianoCallsUnlocked = 0;
		BarUiPianoCallSetTestHook (mock_piano_call_records_mutex);
		BarSocketIoHandleAction (&app, actions[i], NULL, test_ws_cap.ids[0]);
		BarUiPianoCallClearTestHook ();
		ck_assert_msg (g_pianoCallsUnlocked == 0,
				"%s used the song outside pianoHttpMutex", actions[i]);
		ck_assert_msg (g_pianoCallsLocked == 1, "%s made no Pandora call", actions[i]);
	}
	/* The hold is released when the action returns */
	ck_assert (!piano_mutex_held_by_caller (&app));

	app.ph.user.listenerId = NULL;
	pthread_cond_destroy (&app.player.decoderCond);
	pthread_mutex_destroy (&app.player.decoderLock);
	pthread_cond_destroy (&app.player.cond);
	BarUiPianoHttpMutexDestroy (&app);
	capture_app_destroy (&app);
}
END_TEST

/* A failed context check releases the hold too */
START_TEST (test_song_action_context_error_releases_piano_mutex) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	capture_app_init (&app, &station, &song);
	BarUiPianoHttpMutexInit (&app);
	app.ph.user.listenerId = "listener";

	BarSocketIoHandleAction (&app, "song.love", NULL, test_ws_cap.ids[0]);
	ck_assert (!piano_mutex_held_by_caller (&app));
	TestWsCaptureFlush ();
	ck_assert_uint_eq (TestWsCaptureCount (0, "error"), 1);

	app.ph.user.listenerId = NULL;
	BarUiPianoHttpMutexDestroy (&app);
	capture_app_destroy (&app);
}
END_TEST

typedef struct {
	BarApp_t *app;
	BarWsConnId_t requester;
	atomic_bool done;
} BarTestUpcomingThread_t;

static void *upcoming_action_thread (void *arg) {
	BarTestUpcomingThread_t *t = arg;
	BarSocketIoHandleAction (t->app, "query.upcoming", NULL, t->requester);
	atomic_store (&t->done, true);
	return NULL;
}

/* The upcoming list is read under the state lock: a writer (e.g. the CLI
 * truncating the queue on a station switch) holds it off */
START_TEST (test_upcoming_reads_queue_under_state_lock) {
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t cur, next;
	capture_app_init (&app, &station, &cur);
	memset (&next, 0, sizeof (next));
	cur.head.next = &next.head;
	next.title = "Queued Song";
	app.playlist = &cur;
	app.ph.user.listenerId = "listener";

	BarTestUpcomingThread_t t = { .app = &app, .requester = test_ws_cap.ids[0] };
	atomic_init (&t.done, false);
	pthread_t thread;
	pthread_rwlock_wrlock (&app.stateRwlock);
	ck_assert_int_eq (pthread_create (&thread, NULL, upcoming_action_thread, &t), 0);
	usleep (100 * 1000);
	const bool finishedWhileLocked = atomic_load (&t.done);
	pthread_rwlock_unlock (&app.stateRwlock);
	ck_assert_int_eq (pthread_join (thread, NULL), 0);
	ck_assert_msg (!finishedWhileLocked, "upcoming walked the queue without the state lock");

	TestWsCaptureFlush ();
	ck_assert_uint_eq (TestWsCaptureCount (0, "query.upcoming.result"), 1);
	ck_assert_uint_eq (TestWsCaptureCount (0, "Queued Song"), 1);

	app.ph.user.listenerId = NULL;
	capture_app_destroy (&app);
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
	tcase_add_test(tc_emit, test_socketio_process_payload_reads_canonical_runtime_volume);
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
	tcase_add_test(tc_handle, test_socketio_handle_set_quickmix_applies_before_save);
	tcase_add_test(tc_handle, test_socketio_handle_set_quickmix_empty_clears_all);
	tcase_add_test(tc_handle, test_socketio_handle_set_quickmix_rejects_too_many_ids);
	tcase_add_test(tc_handle, test_socketio_handle_set_quickmix_skips_non_string_ids);
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
	tcase_add_test (tc_extra, test_socketio_emit_process_to_targets_client);
	tcase_add_test (tc_extra, test_socketio_handle_add_genre_missing_music_id);
	suite_add_tcase (s, tc_extra);

	TCase *tc_dest = tcase_create ("Destinations");
	tcase_add_test (tc_dest, test_query_process_reaches_only_requester);
	tcase_add_test (tc_dest, test_query_message_routes_reply_to_sender);
	tcase_add_test (tc_dest, test_state_broadcast_reaches_both_clients);
	tcase_add_test (tc_dest, test_capture_playback_start_stop_broadcast);
	tcase_add_test (tc_dest, test_capture_progress_broadcast);
	tcase_add_test (tc_dest, test_capture_volume_broadcast);
	tcase_add_test (tc_dest, test_capture_stations_broadcast);
	tcase_add_test (tc_dest, test_capture_error_broadcast_and_to_requester);
	tcase_add_test (tc_dest, test_capture_pandora_disconnected_broadcast);
	tcase_add_test (tc_dest, test_capture_explanation_and_upcoming_to_requester);
	tcase_add_test (tc_dest, test_action_query_replies_reach_only_requester);
	tcase_add_test (tc_dest, test_message_events_dispatch_from_requester);
	suite_add_tcase (s, tc_dest);

	TCase *tc_ptr = tcase_create ("Borrowed pointers");
	tcase_add_test (tc_ptr, test_station_handlers_hold_piano_mutex_across_station_use);
	tcase_add_test (tc_ptr, test_add_music_transform_and_seed_share_piano_mutex);
	tcase_add_test (tc_ptr, test_delete_current_station_switches_to_quickmix);
	tcase_add_test (tc_ptr, test_get_genres_fetch_holds_piano_mutex);
	tcase_add_test (tc_ptr, test_upcoming_reads_queue_under_state_lock);
	tcase_add_test (tc_ptr, test_song_actions_hold_piano_mutex_across_song_use);
	tcase_add_test (tc_ptr, test_song_action_context_error_releases_piano_mutex);
	suite_add_tcase (s, tc_ptr);
	
	return s;
}
