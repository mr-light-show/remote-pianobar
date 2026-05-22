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
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "../../src/bar_state.h"
#include "../../src/main.h"
#include "../../src/player.h"
#include "../../src/playback_manager.h"

#ifdef WEBSOCKET_ENABLED

static void *test_player_thread_ok(void *arg) {
	(void)arg;
	return (void *)PLAYER_RET_OK;
}

/* Covers post-cleanup cache refresh (was stale PLAYER_FINISHED in the loop). */
START_TEST(test_refresh_cached_mode_after_cleanup) {
	BarApp_t app;
	BarPlayerMode cached = PLAYER_FINISHED;

	memset(&app, 0, sizeof(app));
	pthread_mutex_init(&app.player.lock, NULL);
	app.player.mode = PLAYER_DEAD;

	cached = BarPlaybackManagerRefreshCachedModeAfterCleanup(&app, cached);
	ck_assert_int_eq(cached, PLAYER_DEAD);

	pthread_mutex_destroy(&app.player.lock);
}
END_TEST

/* Exercises FINISHED cleanup + mode refresh without starting the manager thread. */
START_TEST(test_complete_song_cleanup_refreshes_mode) {
	BarApp_t app;
	pthread_t playerThread = 0;
	bool playerStarted = true;
	BarPlayerMode mode = PLAYER_FINISHED;

	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.maxRetry = 3;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);

	ck_assert(pthread_create(&playerThread, NULL, test_player_thread_ok, NULL) == 0);

	mode = BarPlaybackManagerCompleteSongCleanup(&app, &playerThread,
	                                             &playerStarted, mode);
	ck_assert_int_eq(mode, PLAYER_DEAD);
	ck_assert(!playerStarted);

	pthread_mutex_destroy(&app.player.lock);
	BarStateDestroy(&app);
}
END_TEST

START_TEST(test_handle_finished_mode_passthrough) {
	BarApp_t app;
	pthread_t playerThread = 0;
	bool playerStarted = false;

	memset(&app, 0, sizeof(app));
	pthread_mutex_init(&app.player.lock, NULL);
	app.player.mode = PLAYER_DEAD;

	ck_assert_int_eq(
		BarPlaybackManagerHandleFinishedMode(&app, &playerThread,
		                                     &playerStarted, PLAYER_DEAD),
		PLAYER_DEAD);
	ck_assert(!playerStarted);

	pthread_mutex_destroy(&app.player.lock);
}
END_TEST

START_TEST(test_handle_finished_mode_runs_cleanup) {
	BarApp_t app;
	pthread_t playerThread = 0;
	bool playerStarted = true;

	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.maxRetry = 3;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	ck_assert(pthread_create(&playerThread, NULL, test_player_thread_ok, NULL) == 0);

	ck_assert_int_eq(
		BarPlaybackManagerHandleFinishedMode(&app, &playerThread,
		                                     &playerStarted, PLAYER_FINISHED),
		PLAYER_DEAD);
	ck_assert(!playerStarted);

	pthread_mutex_destroy(&app.player.lock);
	BarStateDestroy(&app);
}
END_TEST

START_TEST(test_complete_song_cleanup_interrupt_on_quit) {
	BarApp_t app;
	pthread_t playerThread = 0;
	bool playerStarted = true;
	BarPlayerMode mode = PLAYER_FINISHED;

	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.maxRetry = 3;
	app.doQuit = true;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	app.player.interrupted = 1;
	ck_assert(pthread_create(&playerThread, NULL, test_player_thread_ok, NULL) == 0);

	mode = BarPlaybackManagerCompleteSongCleanup(&app, &playerThread,
	                                             &playerStarted, mode);
	ck_assert_int_eq(mode, PLAYER_DEAD);

	pthread_mutex_destroy(&app.player.lock);
	BarStateDestroy(&app);
}
END_TEST

START_TEST(test_complete_song_cleanup_no_interrupt_log_when_not_quitting) {
	BarApp_t app;
	pthread_t playerThread = 0;
	bool playerStarted = true;
	BarPlayerMode mode = PLAYER_FINISHED;

	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.maxRetry = 3;
	app.doQuit = false;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	app.player.interrupted = 1;
	ck_assert(pthread_create(&playerThread, NULL, test_player_thread_ok, NULL) == 0);

	mode = BarPlaybackManagerCompleteSongCleanup(&app, &playerThread,
	                                             &playerStarted, mode);
	ck_assert_int_eq(mode, PLAYER_DEAD);

	pthread_mutex_destroy(&app.player.lock);
	BarStateDestroy(&app);
}
END_TEST

/* Covers manager-thread call to HandleFinishedMode (one loop, no playback). */
START_TEST(test_manager_thread_one_loop_iteration) {
	BarApp_t app;

	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	pthread_cond_init(&app.player.cond, NULL);
	app.player.mode = PLAYER_DEAD;

	ck_assert(BarPlaybackManagerStart(&app));
	pthread_mutex_lock(&app.player.lock);
	pthread_cond_broadcast(&app.player.cond);
	pthread_mutex_unlock(&app.player.lock);
	usleep(100000);

	BarPlaybackManagerStop(&app);

	pthread_mutex_destroy(&app.player.lock);
	pthread_cond_destroy(&app.player.cond);
	BarStateDestroy(&app);
}
END_TEST

Suite *playback_manager_suite(void) {
	Suite *s = suite_create("PlaybackManager");
	TCase *tc = tcase_create("State machine");
	tcase_add_test(tc, test_refresh_cached_mode_after_cleanup);
	tcase_add_test(tc, test_complete_song_cleanup_refreshes_mode);
	tcase_add_test(tc, test_handle_finished_mode_passthrough);
	tcase_add_test(tc, test_handle_finished_mode_runs_cleanup);
	tcase_add_test(tc, test_complete_song_cleanup_interrupt_on_quit);
	tcase_add_test(tc, test_complete_song_cleanup_no_interrupt_log_when_not_quitting);
	tcase_add_test(tc, test_manager_thread_one_loop_iteration);
	suite_add_tcase(s, tc);
	return s;
}

#else

START_TEST(test_playback_manager_stub_no_websocket) {
	ck_assert(1);
}
END_TEST

Suite *playback_manager_suite(void) {
	Suite *s = suite_create("PlaybackManager");
	TCase *tc = tcase_create("No-op");
	tcase_add_test(tc, test_playback_manager_stub_no_websocket);
	suite_add_tcase(s, tc);
	return s;
}

#endif
