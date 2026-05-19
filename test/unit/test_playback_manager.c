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

#include "../../src/main.h"
#include "../../src/bar_state.h"
#include "../../src/player.h"
#include "../../src/playback_manager.h"
#include "../../src/settings.h"
#include "test_helpers.h"

#ifdef WEBSOCKET_ENABLED

static void *test_player_finishes_immediately(void *arg) {
	BarApp_t *app = (BarApp_t *)arg;

	pthread_mutex_lock(&app->player.lock);
	app->player.mode = PLAYER_FINISHED;
	pthread_cond_broadcast(&app->player.cond);
	pthread_mutex_unlock(&app->player.lock);
	return (void *)PLAYER_RET_OK;
}

static void test_hook_start_playback(BarApp_t *app, pthread_t *playerThread) {
	ck_assert(pthread_create(playerThread, NULL, test_player_finishes_immediately, app) == 0);
}

/* Exercises FINISHED cleanup then mode=PLAYER_DEAD cache refresh and idle handling. */
START_TEST(test_playback_manager_finished_to_idle) {
	BarApp_t app;
	PianoSong_t song;
	bool saw_dead = false;

	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.maxRetry = 3;
	app.settings.history = 0; /* avoid BarUiHistoryPrepend freeing stack song */
	BarStateInit(&app);

	memset(&song, 0, sizeof(song));
	song.head.next = NULL;
	song.title = (char *)"test";
	BarStateSetPlaylist(&app, &song);

	pthread_mutex_init(&app.player.lock, NULL);
	pthread_cond_init(&app.player.cond, NULL);

	test_set_start_playback_hook(test_hook_start_playback);
	ck_assert(BarPlaybackManagerStart(&app));

	for (int i = 0; i < 30; i++) {
		if (BarStateGetPlayerMode(&app) == PLAYER_DEAD) {
			saw_dead = true;
			break;
		}
		usleep(100000);
	}

	BarPlaybackManagerStop(&app);
	test_set_start_playback_hook(NULL);

	ck_assert(saw_dead);
	ck_assert_ptr_null(BarStateGetPlaylist(&app));

	pthread_cond_destroy(&app.player.cond);
	pthread_mutex_destroy(&app.player.lock);
	BarStateDestroy(&app);
}
END_TEST

Suite *playback_manager_suite(void) {
	Suite *s = suite_create("PlaybackManager");
	TCase *tc = tcase_create("State machine");
	tcase_add_test(tc, test_playback_manager_finished_to_idle);
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
