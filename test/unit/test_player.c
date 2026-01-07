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

#include "../../src/player.h"
#include "../../src/settings.h"

/* Test: BarPlayerIsPaused helper function */
START_TEST(test_player_is_paused) {
	player_t player;
	BarSettings_t settings;
	
	memset(&player, 0, sizeof(player));
	memset(&settings, 0, sizeof(settings));
	
	/* Initialize player */
	pthread_mutex_init(&player.lock, NULL);
	pthread_cond_init(&player.cond, NULL);
	pthread_mutex_init(&player.decoderLock, NULL);
	pthread_cond_init(&player.decoderCond, NULL);
	
	player.settings = &settings;
	player.doPause = false;
	
	/* Test not paused */
	ck_assert(BarPlayerIsPaused(&player) == false);
	
	/* Test paused */
	player.doPause = true;
	ck_assert(BarPlayerIsPaused(&player) == true);
	
	/* Test not paused again */
	player.doPause = false;
	ck_assert(BarPlayerIsPaused(&player) == false);
	
	/* Cleanup */
	pthread_cond_destroy(&player.decoderCond);
	pthread_mutex_destroy(&player.decoderLock);
	pthread_cond_destroy(&player.cond);
	pthread_mutex_destroy(&player.lock);
}
END_TEST

/* Test: BarPlayerGetMode returns correct mode */
START_TEST(test_player_get_mode) {
	player_t player;
	BarSettings_t settings;
	
	memset(&player, 0, sizeof(player));
	memset(&settings, 0, sizeof(settings));
	
	/* Initialize player */
	pthread_mutex_init(&player.lock, NULL);
	pthread_cond_init(&player.cond, NULL);
	pthread_mutex_init(&player.decoderLock, NULL);
	pthread_cond_init(&player.decoderCond, NULL);
	
	player.settings = &settings;
	player.mode = PLAYER_DEAD;
	
	/* Test initial mode */
	ck_assert(BarPlayerGetMode(&player) == PLAYER_DEAD);
	
	/* Test mode change */
	player.mode = PLAYER_PLAYING;
	ck_assert(BarPlayerGetMode(&player) == PLAYER_PLAYING);
	
	player.mode = PLAYER_FINISHED;
	ck_assert(BarPlayerGetMode(&player) == PLAYER_FINISHED);
	
	/* Cleanup */
	pthread_cond_destroy(&player.decoderCond);
	pthread_mutex_destroy(&player.decoderLock);
	pthread_cond_destroy(&player.cond);
	pthread_mutex_destroy(&player.lock);
}
END_TEST

/* Test: Player reset initializes fields correctly */
START_TEST(test_player_reset_initializes_fields) {
	player_t player;
	BarSettings_t settings;
	
	memset(&player, 0, sizeof(player));
	memset(&settings, 0, sizeof(settings));
	
	/* Initialize player - this initializes engine and mutexes */
	BarPlayerInit(&player, &settings);
	
	/* Reset player */
	BarPlayerReset(&player);
	
	/* Verify fields are initialized */
	ck_assert(player.decodingFinished == false);
	ck_assert(player.mode == PLAYER_DEAD);
	ck_assert(player.soundInitialized == false);
	
	/* Cleanup */
	BarPlayerDestroy(&player);
}
END_TEST

Suite *player_suite(void) {
	Suite *s;
	TCase *tc_basic;
	
	s = suite_create("Player");
	
	tc_basic = tcase_create("Basic Functions");
	tcase_add_test(tc_basic, test_player_is_paused);
	tcase_add_test(tc_basic, test_player_get_mode);
	tcase_add_test(tc_basic, test_player_reset_initializes_fields);
	suite_add_tcase(s, tc_basic);
	
	return s;
}

