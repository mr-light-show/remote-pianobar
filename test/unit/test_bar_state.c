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

#include "../../src/main.h"
#include "../../src/bar_state.h"
#include "../../src/settings.h"

#ifdef WEBSOCKET_ENABLED

/* Minimal station for FindStationById tests (list has one element). */
static PianoStation_t station_a;
static const char *station_a_id = "station-a";

static void bar_state_test_setup_both(BarApp_t *app) {
	memset(app, 0, sizeof(*app));
	app->settings.uiMode = BAR_UI_MODE_BOTH;
	BarStateInit(app);
}

static void bar_state_test_teardown_both(BarApp_t *app) {
	BarStateDestroy(app);
}

/* Init/destroy: stateRwlock is created and destroyed in BOTH mode */
START_TEST(test_bar_state_init_destroy_both) {
	BarApp_t app;
	bar_state_test_setup_both(&app);
	ck_assert(app.settings.uiMode == BAR_UI_MODE_BOTH);
	bar_state_test_teardown_both(&app);
}
END_TEST

/* Station getters/setters under stateRwlock: set then get returns same pointer */
START_TEST(test_bar_state_next_station_get_set) {
	BarApp_t app;
	PianoStation_t st;
	memset(&st, 0, sizeof(st));
	st.id = (char *)"next";
	bar_state_test_setup_both(&app);

	BarStateSetNextStation(&app, &st);
	ck_assert_ptr_eq(BarStateGetNextStation(&app), &st);

	BarStateSetNextStation(&app, NULL);
	ck_assert_ptr_null(BarStateGetNextStation(&app));

	bar_state_test_teardown_both(&app);
}
END_TEST

START_TEST(test_bar_state_current_station_get_set) {
	BarApp_t app;
	PianoStation_t st;
	memset(&st, 0, sizeof(st));
	st.id = (char *)"cur";
	bar_state_test_setup_both(&app);

	BarStateSetCurrentStation(&app, &st);
	ck_assert_ptr_eq(BarStateGetCurrentStation(&app), &st);

	BarStateSetCurrentStation(&app, NULL);
	ck_assert_ptr_null(BarStateGetCurrentStation(&app));

	bar_state_test_teardown_both(&app);
}
END_TEST

/* FindStationById returns NULL when ph.stations == NULL */
START_TEST(test_bar_state_find_station_by_id_null_list) {
	BarApp_t app;
	bar_state_test_setup_both(&app);
	ck_assert(app.ph.stations == NULL);
	ck_assert_ptr_null(BarStateFindStationById(&app, "any"));
	ck_assert_ptr_null(BarStateFindStationById(&app, NULL));
	bar_state_test_teardown_both(&app);
}
END_TEST

/* FindStationById with one station: finds by id, returns NULL for unknown id */
START_TEST(test_bar_state_find_station_by_id_one_station) {
	BarApp_t app;
	memset(&station_a, 0, sizeof(station_a));
	station_a.head.next = NULL;
	station_a.id = (char *)station_a_id;

	bar_state_test_setup_both(&app);
	app.ph.stations = &station_a;

	ck_assert_ptr_eq(BarStateFindStationById(&app, station_a_id), &station_a);
	ck_assert_ptr_null(BarStateFindStationById(&app, "unknown-id"));
	ck_assert_ptr_null(BarStateFindStationById(&app, NULL));

	app.ph.stations = NULL;
	bar_state_test_teardown_both(&app);
}
END_TEST

/* GetStationList returns ph.stations */
START_TEST(test_bar_state_get_station_list) {
	BarApp_t app;
	bar_state_test_setup_both(&app);
	ck_assert_ptr_null(BarStateGetStationList(&app));
	app.ph.stations = &station_a;
	ck_assert_ptr_eq(BarStateGetStationList(&app), &station_a);
	app.ph.stations = NULL;
	bar_state_test_teardown_both(&app);
}
END_TEST

/* Playlist get/set and drain */
START_TEST(test_bar_state_playlist_get_set) {
	BarApp_t app;
	PianoSong_t pl;
	memset(&pl, 0, sizeof(pl));
	pl.head.next = NULL;
	bar_state_test_setup_both(&app);

	ck_assert_ptr_null(BarStateGetPlaylist(&app));
	BarStateSetPlaylist(&app, &pl);
	ck_assert_ptr_eq(BarStateGetPlaylist(&app), &pl);
	BarStateSetPlaylist(&app, NULL);
	ck_assert_ptr_null(BarStateGetPlaylist(&app));

	bar_state_test_teardown_both(&app);
}
END_TEST

START_TEST(test_bar_state_drain_playlist) {
	BarApp_t app;
	bar_state_test_setup_both(&app);
	app.playlist = NULL;
	BarStateDrainPlaylist(&app);
	ck_assert_ptr_null(BarStateGetPlaylist(&app));
	bar_state_test_teardown_both(&app);
}
END_TEST

/* SwitchStation: drains playlist and sets nextStation (test with playlist NULL) */
START_TEST(test_bar_state_switch_station) {
	BarApp_t app;
	PianoStation_t st;
	memset(&st, 0, sizeof(st));
	st.id = (char *)"switched";
	bar_state_test_setup_both(&app);
	app.playlist = NULL;

	BarStateSwitchStation(&app, &st);
	ck_assert_ptr_null(BarStateGetPlaylist(&app));
	ck_assert_ptr_eq(BarStateGetNextStation(&app), &st);

	bar_state_test_teardown_both(&app);
}
END_TEST

/* Player state getters use player.lock, not stateRwlock */
START_TEST(test_bar_state_player_mode_time_paused) {
	BarApp_t app;
	bar_state_test_setup_both(&app);
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
	bar_state_test_teardown_both(&app);
}
END_TEST

/* Lock order: stateRwlock must be acquired before player.lock when both needed.
 * This test documents the allowed order by acquiring in that order and releasing in reverse. */
START_TEST(test_bar_state_lock_order_state_before_player) {
	BarApp_t app;
	bar_state_test_setup_both(&app);
	pthread_mutex_init(&app.player.lock, NULL);

	pthread_rwlock_rdlock((pthread_rwlock_t *)&app.stateRwlock);
	pthread_mutex_lock(&app.player.lock);
	pthread_mutex_unlock(&app.player.lock);
	pthread_rwlock_unlock((pthread_rwlock_t *)&app.stateRwlock);

	pthread_mutex_destroy(&app.player.lock);
	bar_state_test_teardown_both(&app);
}
END_TEST

/* IsPandoraConnected: simple read of ph.user.listenerId (no lock) */
START_TEST(test_bar_state_is_pandora_connected) {
	BarApp_t app;
	bar_state_test_setup_both(&app);
	ck_assert(BarStateIsPandoraConnected(&app) == false);
	app.ph.user.listenerId = (char *)"lid";
	ck_assert(BarStateIsPandoraConnected(&app) == true);
	app.ph.user.listenerId = NULL;
	bar_state_test_teardown_both(&app);
}
END_TEST

Suite *bar_state_suite(void) {
	Suite *s = suite_create("BarState");
	TCase *tc_core = tcase_create("Init and stations");
	tcase_add_test(tc_core, test_bar_state_init_destroy_both);
	tcase_add_test(tc_core, test_bar_state_next_station_get_set);
	tcase_add_test(tc_core, test_bar_state_current_station_get_set);
	tcase_add_test(tc_core, test_bar_state_find_station_by_id_null_list);
	tcase_add_test(tc_core, test_bar_state_find_station_by_id_one_station);
	tcase_add_test(tc_core, test_bar_state_get_station_list);
	suite_add_tcase(s, tc_core);

	TCase *tc_playlist = tcase_create("Playlist");
	tcase_add_test(tc_playlist, test_bar_state_playlist_get_set);
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
