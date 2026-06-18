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
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "../../src/main.h"
#include "../../src/bar_state.h"
#include "../../src/settings.h"
#include "../../src/log.h"

#ifdef WEBSOCKET_ENABLED

/* Minimal station for FindStationById tests (list has one element). */
static PianoStation_t station_a;
static const char *station_a_id = "station-a";

static void bar_state_test_setup(BarApp_t *app, BarUiMode_t mode) {
	memset(app, 0, sizeof(*app));
	app->settings.uiMode = mode;
	BarStateInit(app);
}

static void bar_state_test_teardown(BarApp_t *app) {
	BarStateDestroy(app);
}

/* Init/destroy: stateRwlock is created and destroyed in BOTH mode */
START_TEST(test_bar_state_init_destroy_both) {
	BarApp_t app;
	bar_state_test_setup(&app, BAR_UI_MODE_BOTH);
	ck_assert(app.settings.uiMode == BAR_UI_MODE_BOTH);
	ck_assert(BarStateUsesRwlock(&app));
	bar_state_test_teardown(&app);
}
END_TEST

#ifdef HAVE_DEBUGLOG
/* CLI: BarStateUsesRwlock false while DEBUG_STATE is enabled (no lock logging). */
START_TEST(test_bar_state_debug_state_no_rwlock_cli) {
	BarApp_t app;
	int stderr_dup;
	FILE *null_out;

	stderr_dup = dup(STDERR_FILENO);
	ck_assert(stderr_dup >= 0);
	null_out = fopen("/dev/null", "w");
	ck_assert(null_out != NULL);
	ck_assert(dup2(fileno(null_out), STDERR_FILENO) >= 0);
	fclose(null_out);

	ck_assert_int_eq(setenv("PIANOBAR_DEBUG", "64", 1), 0);
	log_init();
	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_CLI;
	BarStateInit(&app);
	(void)BarStateGetNextStation(&app);
	BarStateSetNextStation(&app, NULL);
	BarStateDestroy(&app);

	dup2(stderr_dup, STDERR_FILENO);
	close(stderr_dup);
	unsetenv("PIANOBAR_DEBUG");
}
END_TEST
#endif

/* BarStateUsesRwlock: false for NULL app and cli mode */
START_TEST(test_bar_state_uses_rwlock_cli_and_null) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_CLI;

	ck_assert(!BarStateUsesRwlock(NULL));
	ck_assert(!BarStateUsesRwlock(&app));

	BarStateInit(&app);
	BarStateSetNextStation(&app, NULL);
	ck_assert_ptr_null(BarStateGetNextStation(&app));
	BarStateDestroy(&app);
}
END_TEST

#ifdef HAVE_DEBUGLOG
/* Exercise DEBUG_STATE log paths in lock helpers (stderr suppressed). */
START_TEST(test_bar_state_debug_state_lock_logging) {
	BarApp_t app;
	PianoSong_t pl;
	PianoStation_t st;
	int stderr_dup;
	FILE *null_out;

	memset(&pl, 0, sizeof(pl));
	pl.head.next = NULL;
	pl.title = (char *)"title";
	memset(&st, 0, sizeof(st));
	st.id = (char *)"station-id";
	st.name = (char *)"station";

	stderr_dup = dup(STDERR_FILENO);
	ck_assert(stderr_dup >= 0);
	null_out = fopen("/dev/null", "w");
	ck_assert(null_out != NULL);
	ck_assert(dup2(fileno(null_out), STDERR_FILENO) >= 0);
	fclose(null_out);

	ck_assert_int_eq(setenv("PIANOBAR_DEBUG", "64", 1), 0);
	log_init();
	bar_state_test_setup(&app, BAR_UI_MODE_BOTH);

	BarStateGetPlaylist(&app);
	BarStateSetPlaylist(&app, &pl);
	BarStateGetPlaylist(&app);
	(void)BarStateGetStationList(&app);
	(void)BarStateGetNextStation(&app);
	BarStateSetNextStation(&app, &st);
	BarStateSetCurrentStation(&app, &st);
	(void)BarStateGetCurrentStation(&app);
	app.ph.stations = &st;
	(void)BarStateFindStationById(&app, st.id);
	app.ph.stations = NULL;
	/* Clear pointer before SwitchStation — it PianoDestroyPlaylist's; pl is stack */
	BarStateSetPlaylist(&app, NULL);
	BarStateSwitchStation(&app, &st);
	BarStateSetNextStation(&app, NULL);
	/* Drain with playlist already NULL: still exercises write lock + unlock */
	BarStateDrainPlaylist(&app);

	bar_state_test_teardown(&app);
	dup2(stderr_dup, STDERR_FILENO);
	close(stderr_dup);
	unsetenv("PIANOBAR_DEBUG");
}
END_TEST

START_TEST(test_bar_state_debug_state_lock_logging_web) {
	BarApp_t app;
	PianoSong_t pl;
	PianoStation_t st;
	int stderr_dup;
	FILE *null_out;

	memset(&pl, 0, sizeof(pl));
	pl.head.next = NULL;
	pl.title = (char *)"title";
	memset(&st, 0, sizeof(st));
	st.id = (char *)"web-station-id";
	st.name = (char *)"web-station";

	stderr_dup = dup(STDERR_FILENO);
	ck_assert(stderr_dup >= 0);
	null_out = fopen("/dev/null", "w");
	ck_assert(null_out != NULL);
	ck_assert(dup2(fileno(null_out), STDERR_FILENO) >= 0);
	fclose(null_out);

	ck_assert_int_eq(setenv("PIANOBAR_DEBUG", "64", 1), 0);
	log_init();
	bar_state_test_setup(&app, BAR_UI_MODE_WEB);

	BarStateGetPlaylist(&app);
	BarStateSetPlaylist(&app, &pl);
	(void)BarStateGetPlaylist(&app);
	(void)BarStateGetNextStation(&app);
	BarStateSetNextStation(&app, &st);
	BarStateSetCurrentStation(&app, &st);
	(void)BarStateGetCurrentStation(&app);
	BarStateSetPlaylist(&app, NULL);
	BarStateSwitchStation(&app, &st);
	BarStateSetNextStation(&app, NULL);
	BarStateDrainPlaylist(&app);

	bar_state_test_teardown(&app);
	dup2(stderr_dup, STDERR_FILENO);
	close(stderr_dup);
	unsetenv("PIANOBAR_DEBUG");
}
END_TEST
#endif

/* Init/destroy: stateRwlock is created and destroyed in WEB mode */
START_TEST(test_bar_state_init_destroy_web) {
	BarApp_t app;
	bar_state_test_setup(&app, BAR_UI_MODE_WEB);
	ck_assert(app.settings.uiMode == BAR_UI_MODE_WEB);
	ck_assert(BarStateUsesRwlock(&app));
	bar_state_test_teardown(&app);
}
END_TEST

/* Playlist get/set under stateRwlock in WEB mode */
START_TEST(test_bar_state_playlist_get_set_web) {
	BarApp_t app;
	PianoSong_t pl;
	memset(&pl, 0, sizeof(pl));
	pl.head.next = NULL;
	bar_state_test_setup(&app, BAR_UI_MODE_WEB);

	ck_assert_ptr_null(BarStateGetPlaylist(&app));
	BarStateSetPlaylist(&app, &pl);
	ck_assert_ptr_eq(BarStateGetPlaylist(&app), &pl);

	bar_state_test_teardown(&app);
}
END_TEST

/* Station getters/setters under stateRwlock: set then get returns same pointer */
START_TEST(test_bar_state_next_station_get_set) {
	BarApp_t app;
	PianoStation_t st;
	memset(&st, 0, sizeof(st));
	st.id = (char *)"next";
	bar_state_test_setup(&app, BAR_UI_MODE_BOTH);

	BarStateSetNextStation(&app, &st);
	ck_assert_ptr_eq(BarStateGetNextStation(&app), &st);

	BarStateSetNextStation(&app, NULL);
	ck_assert_ptr_null(BarStateGetNextStation(&app));

	bar_state_test_teardown(&app);
}
END_TEST

START_TEST(test_bar_state_current_station_get_set) {
	BarApp_t app;
	PianoStation_t st;
	memset(&st, 0, sizeof(st));
	st.id = (char *)"cur";
	bar_state_test_setup(&app, BAR_UI_MODE_BOTH);

	BarStateSetCurrentStation(&app, &st);
	ck_assert_ptr_eq(BarStateGetCurrentStation(&app), &st);

	BarStateSetCurrentStation(&app, NULL);
	ck_assert_ptr_null(BarStateGetCurrentStation(&app));

	bar_state_test_teardown(&app);
}
END_TEST

/* FindStationById returns NULL when ph.stations == NULL */
START_TEST(test_bar_state_find_station_by_id_null_list) {
	BarApp_t app;
	bar_state_test_setup(&app, BAR_UI_MODE_BOTH);
	ck_assert(app.ph.stations == NULL);
	ck_assert_ptr_null(BarStateFindStationById(&app, "any"));
	ck_assert_ptr_null(BarStateFindStationById(&app, NULL));
	bar_state_test_teardown(&app);
}
END_TEST

/* FindStationById with one station: finds by id, returns NULL for unknown id */
START_TEST(test_bar_state_find_station_by_id_one_station) {
	BarApp_t app;
	memset(&station_a, 0, sizeof(station_a));
	station_a.head.next = NULL;
	station_a.id = (char *)station_a_id;

	bar_state_test_setup(&app, BAR_UI_MODE_BOTH);
	app.ph.stations = &station_a;

	ck_assert_ptr_eq(BarStateFindStationById(&app, station_a_id), &station_a);
	ck_assert_ptr_null(BarStateFindStationById(&app, "unknown-id"));
	ck_assert_ptr_null(BarStateFindStationById(&app, NULL));

	app.ph.stations = NULL;
	bar_state_test_teardown(&app);
}
END_TEST

/* GetStationList returns ph.stations */
START_TEST(test_bar_state_get_station_list) {
	BarApp_t app;
	bar_state_test_setup(&app, BAR_UI_MODE_BOTH);
	ck_assert_ptr_null(BarStateGetStationList(&app));
	app.ph.stations = &station_a;
	ck_assert_ptr_eq(BarStateGetStationList(&app), &station_a);
	app.ph.stations = NULL;
	bar_state_test_teardown(&app);
}
END_TEST

/* Playlist get/set and drain */
START_TEST(test_bar_state_playlist_get_set) {
	BarApp_t app;
	PianoSong_t pl;
	memset(&pl, 0, sizeof(pl));
	pl.head.next = NULL;
	bar_state_test_setup(&app, BAR_UI_MODE_BOTH);

	ck_assert_ptr_null(BarStateGetPlaylist(&app));
	BarStateSetPlaylist(&app, &pl);
	ck_assert_ptr_eq(BarStateGetPlaylist(&app), &pl);
	BarStateSetPlaylist(&app, NULL);
	ck_assert_ptr_null(BarStateGetPlaylist(&app));

	bar_state_test_teardown(&app);
}
END_TEST

START_TEST(test_bar_state_drain_playlist) {
	BarApp_t app;
	bar_state_test_setup(&app, BAR_UI_MODE_BOTH);
	app.playlist = NULL;
	BarStateDrainPlaylist(&app);
	ck_assert_ptr_null(BarStateGetPlaylist(&app));
	bar_state_test_teardown(&app);
}
END_TEST

/* SwitchStation: drains playlist and sets nextStation (test with playlist NULL) */
START_TEST(test_bar_state_switch_station) {
	BarApp_t app;
	PianoStation_t st;
	memset(&st, 0, sizeof(st));
	st.id = (char *)"switched";
	bar_state_test_setup(&app, BAR_UI_MODE_BOTH);
	app.playlist = NULL;

	BarStateSwitchStation(&app, &st);
	ck_assert_ptr_null(BarStateGetPlaylist(&app));
	ck_assert_ptr_eq(BarStateGetNextStation(&app), &st);

	bar_state_test_teardown(&app);
}
END_TEST

/* Player state getters use player.lock, not stateRwlock */
START_TEST(test_bar_state_player_mode_time_paused) {
	BarApp_t app;
	bar_state_test_setup(&app, BAR_UI_MODE_BOTH);
	pthread_mutex_init(&app.player.lock, NULL);
	app.player.mode = PLAYER_DEAD;
	app.player.songPlayed = 10;
	app.player.songDuration = 100;
	app.player.doPause = false;

	ck_assert(BarStateGetPlayerMode(&app) == PLAYER_DEAD);
	app.player.mode = PLAYER_PLAYING;
	ck_assert(BarStateGetPlayerMode(&app) == PLAYER_PLAYING);

	unsigned int played = 0, duration = 0;
	BarStateGetPlayerTime(&app, &played, &duration);
	ck_assert_uint_eq(played, 10);
	ck_assert_uint_eq(duration, 100);

	ck_assert(BarStateGetPlayerPaused(&app) == false);
	app.player.doPause = true;
	ck_assert(BarStateGetPlayerPaused(&app) == true);

	pthread_mutex_destroy(&app.player.lock);
	bar_state_test_teardown(&app);
}
END_TEST

/* Lock order: stateRwlock must be acquired before player.lock when both needed.
 * This test documents the allowed order by acquiring in that order and releasing in reverse. */
START_TEST(test_bar_state_lock_order_state_before_player) {
	BarApp_t app;
	bar_state_test_setup(&app, BAR_UI_MODE_BOTH);
	pthread_mutex_init(&app.player.lock, NULL);

	pthread_rwlock_rdlock((pthread_rwlock_t *)&app.stateRwlock);
	pthread_mutex_lock(&app.player.lock);
	pthread_mutex_unlock(&app.player.lock);
	pthread_rwlock_unlock((pthread_rwlock_t *)&app.stateRwlock);

	pthread_mutex_destroy(&app.player.lock);
	bar_state_test_teardown(&app);
}
END_TEST

/* IsPandoraConnected: simple read of ph.user.listenerId (no lock) */
START_TEST(test_bar_state_is_pandora_connected) {
	BarApp_t app;
	bar_state_test_setup(&app, BAR_UI_MODE_BOTH);
	ck_assert(BarStateIsPandoraConnected(&app) == false);
	app.ph.user.listenerId = (char *)"lid";
	ck_assert(BarStateIsPandoraConnected(&app) == true);
	app.ph.user.listenerId = NULL;
	bar_state_test_teardown(&app);
}
END_TEST

/* stateRwlock must be acquired and released in BAR_UI_MODE_WEB, not only BOTH.
   After BarStateSetCurrentStation returns, the lock must be acquirable again. */
START_TEST(test_bar_state_lock_taken_in_web_mode) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit(&app);

	PianoStation_t st;
	memset(&st, 0, sizeof(st));
	st.id = (char *)"web-station";

	BarStateSetCurrentStation(&app, &st);

	/* Lock must have been released — trywrlock must succeed */
	int rc = pthread_rwlock_trywrlock(&app.stateRwlock);
	ck_assert_int_eq(rc, 0);
	pthread_rwlock_unlock(&app.stateRwlock);

	BarStateDestroy(&app);
}
END_TEST

#ifdef WEBSOCKET_ENABLED
START_TEST(test_signal_playback_manager_no_deadlock) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	pthread_cond_init(&app.player.cond, NULL);
	BarStateSignalPlaybackManager(&app);
	pthread_mutex_destroy(&app.player.lock);
	pthread_cond_destroy(&app.player.cond);
	BarStateDestroy(&app);
}
END_TEST

START_TEST(test_signal_playback_manager_noop_null_and_cli) {
	BarStateSignalPlaybackManager(NULL);

	BarApp_t app;
	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_CLI;
	BarStateInit(&app);
	BarStateSignalPlaybackManager(&app);
	BarStateDestroy(&app);
}
END_TEST
#endif

/* --- Snapshot tests --- */

START_TEST(test_bar_state_snapshot_stations_copies_fields)
{
	BarApp_t app;
	PianoStation_t s1, s2;
	memset (&app, 0, sizeof (app));
	memset (&s1,  0, sizeof (s1));
	memset (&s2,  0, sizeof (s2));
	BarStateInit (&app);

	s1.id = "aaa"; s1.name = "Station A"; s1.isQuickMix  = 0;
	s2.id = "bbb"; s2.name = "Station B"; s2.isQuickMix  = 1;
	s1.head.next = (PianoListHead_t *)&s2;
	app.ph.stations = &s1;

	BarStationSnapshotList_t snap;
	ck_assert (BarStateSnapshotStations (&app, &snap));
	ck_assert_uint_eq (snap.count, 2);
	ck_assert_str_eq (snap.items[0].id,   "aaa");
	ck_assert_str_eq (snap.items[0].name, "Station A");
	ck_assert_str_eq (snap.items[1].id,   "bbb");
	ck_assert_str_eq (snap.items[1].name, "Station B");
	ck_assert (snap.items[1].isQuickMix);

	BarStateFreeStationSnapshot (&snap);
	ck_assert_ptr_null (snap.items);
	ck_assert_uint_eq  (snap.count, 0);
	BarStateDestroy (&app);
}
END_TEST

START_TEST(test_bar_state_snapshot_stations_empty)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarStateInit (&app);
	app.ph.stations = NULL;

	BarStationSnapshotList_t snap;
	ck_assert (BarStateSnapshotStations (&app, &snap));
	ck_assert_uint_eq (snap.count, 0);
	ck_assert_ptr_null (snap.items);

	BarStateFreeStationSnapshot (&snap);
	BarStateDestroy (&app);
}
END_TEST

START_TEST(test_bar_state_snapshot_playback_no_song)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarStateInit (&app);

	BarPlaybackSnapshot_t snap;
	BarStateSnapshotPlayback (&app, &snap);
	ck_assert (!snap.hasSong);
	ck_assert (!snap.hasStation);

	BarStateFreePlaybackSnapshot (&snap);
	BarStateDestroy (&app);
}
END_TEST

START_TEST(test_bar_state_snapshot_playback_with_song)
{
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t    song;
	memset (&app,     0, sizeof (app));
	memset (&station, 0, sizeof (station));
	memset (&song,    0, sizeof (song));
	BarStateInit (&app);

	station.id   = "sid";
	station.name = "My Station";
	station.displayName = "Display Station";
	app.ph.stations = &station;
	song.title   = "Cool Track";
	song.artist  = "Cool Artist";
	song.stationId = "sid";

	app.curStation = &station;
	app.playlist   = &song;

	BarPlaybackSnapshot_t snap;
	BarStateSnapshotPlayback (&app, &snap);
	ck_assert (snap.hasStation);
	ck_assert (snap.hasSong);
	ck_assert_str_eq (snap.stationId,   "sid");
	ck_assert_str_eq (snap.stationName, "Display Station");
	ck_assert_str_eq (snap.songTitle,   "Cool Track");
	ck_assert_str_eq (snap.songArtist,  "Cool Artist");
	ck_assert_str_eq (snap.songStationName, "Display Station");

	BarStateFreePlaybackSnapshot (&snap);
	ck_assert_ptr_null (snap.stationId);
	ck_assert_ptr_null (snap.songTitle);
	ck_assert_ptr_null (snap.songStationName);
	BarStateDestroy (&app);
}
END_TEST

START_TEST (test_bar_state_snapshot_playback_unknown_song_station_id)
{
	BarApp_t app;
	PianoSong_t song;
	memset (&app, 0, sizeof (app));
	memset (&song, 0, sizeof (song));
	BarStateInit (&app);

	song.title = "Orphan Track";
	song.stationId = "missing-station";
	app.playlist = &song;

	BarPlaybackSnapshot_t snap;
	BarStateSnapshotPlayback (&app, &snap);
	ck_assert (snap.hasSong);
	ck_assert_str_eq (snap.songTitle, "Orphan Track");
	ck_assert_ptr_null (snap.songStationName);

	BarStateFreePlaybackSnapshot (&snap);
	BarStateDestroy (&app);
}
END_TEST

START_TEST (test_bar_state_snapshot_stations_uses_display_name)
{
	BarApp_t app;
	PianoStation_t station;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	BarStateInit (&app);

	station.id = "sid";
	station.name = "Raw Name";
	station.displayName = "Pretty Name";
	app.ph.stations = &station;

	BarStationSnapshotList_t snap;
	ck_assert (BarStateSnapshotStations (&app, &snap));
	ck_assert_uint_eq (snap.count, 1);
	ck_assert_str_eq (snap.items[0].displayName, "Pretty Name");

	BarStateFreeStationSnapshot (&snap);
	BarStateDestroy (&app);
}
END_TEST

START_TEST (test_bar_state_snapshot_playback_uses_station_name_when_no_display_name)
{
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	memset (&song, 0, sizeof (song));
	BarStateInit (&app);

	station.id = "sid";
	station.name = "Plain Name";
	station.displayName = NULL;
	app.ph.stations = &station;
	song.title = "Track";
	song.stationId = "sid";
	app.curStation = &station;
	app.playlist = &song;

	BarPlaybackSnapshot_t snap;
	BarStateSnapshotPlayback (&app, &snap);
	ck_assert_str_eq (snap.stationName, "Plain Name");
	ck_assert_str_eq (snap.songStationName, "Plain Name");

	BarStateFreePlaybackSnapshot (&snap);
	BarStateDestroy (&app);
}
END_TEST

Suite *bar_state_suite(void) {
	Suite *s = suite_create("BarState");
	TCase *tc_core = tcase_create("Init and stations");
	tcase_add_test(tc_core, test_bar_state_init_destroy_both);
	tcase_add_test(tc_core, test_bar_state_init_destroy_web);
	tcase_add_test(tc_core, test_bar_state_uses_rwlock_cli_and_null);
#ifdef HAVE_DEBUGLOG
	tcase_add_test(tc_core, test_bar_state_debug_state_no_rwlock_cli);
	tcase_add_test(tc_core, test_bar_state_debug_state_lock_logging);
	tcase_add_test(tc_core, test_bar_state_debug_state_lock_logging_web);
#endif
	tcase_add_test(tc_core, test_bar_state_next_station_get_set);
	tcase_add_test(tc_core, test_bar_state_current_station_get_set);
	tcase_add_test(tc_core, test_bar_state_find_station_by_id_null_list);
	tcase_add_test(tc_core, test_bar_state_find_station_by_id_one_station);
	tcase_add_test(tc_core, test_bar_state_get_station_list);
	suite_add_tcase(s, tc_core);

	TCase *tc_playlist = tcase_create("Playlist");
	tcase_add_test(tc_playlist, test_bar_state_playlist_get_set);
	tcase_add_test(tc_playlist, test_bar_state_playlist_get_set_web);
	tcase_add_test(tc_playlist, test_bar_state_drain_playlist);
	tcase_add_test(tc_playlist, test_bar_state_switch_station);
	suite_add_tcase(s, tc_playlist);

	TCase *tc_player = tcase_create("Player state (player.lock)");
	tcase_add_test(tc_player, test_bar_state_player_mode_time_paused);
	suite_add_tcase(s, tc_player);

	TCase *tc_lock = tcase_create("Lock order");
	tcase_add_test(tc_lock, test_bar_state_lock_order_state_before_player);
	suite_add_tcase(s, tc_lock);

	TCase *tc_misc = tcase_create("Misc");
	tcase_add_test(tc_misc, test_bar_state_is_pandora_connected);
	suite_add_tcase(s, tc_misc);

	TCase *tc_web = tcase_create("WEB mode locking");
	tcase_add_test(tc_web, test_bar_state_lock_taken_in_web_mode);
#ifdef WEBSOCKET_ENABLED
	tcase_add_test(tc_web, test_signal_playback_manager_no_deadlock);
	tcase_add_test(tc_web, test_signal_playback_manager_noop_null_and_cli);
#endif
	suite_add_tcase(s, tc_web);

	TCase *tc_snap = tcase_create("Snapshot");
	tcase_add_test(tc_snap, test_bar_state_snapshot_stations_copies_fields);
	tcase_add_test(tc_snap, test_bar_state_snapshot_stations_empty);
	tcase_add_test(tc_snap, test_bar_state_snapshot_playback_no_song);
	tcase_add_test(tc_snap, test_bar_state_snapshot_playback_with_song);
	tcase_add_test(tc_snap, test_bar_state_snapshot_playback_unknown_song_station_id);
	tcase_add_test(tc_snap, test_bar_state_snapshot_stations_uses_display_name);
	tcase_add_test(tc_snap, test_bar_state_snapshot_playback_uses_station_name_when_no_display_name);
	suite_add_tcase(s, tc_snap);

	return s;
}

#else

/* Stub when WEBSOCKET_ENABLED is not set: bar_state uses no lock in that case. */
START_TEST(test_bar_state_stub_no_websocket) {
	ck_assert(1);
}
END_TEST

Suite *bar_state_suite(void) {
	Suite *s = suite_create("BarState");
	TCase *tc = tcase_create("No-op");
	tcase_add_test(tc, test_bar_state_stub_no_websocket);
	suite_add_tcase(s, tc);
	return s;
}

#endif
