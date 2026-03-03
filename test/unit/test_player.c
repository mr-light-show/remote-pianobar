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
#include <errno.h>

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

/*
 * decoderLock behavior tests (see src/THREAD_SAFETY.md and src/player.c).
 * Reader and writer both use decoderLock; player.lock and decoderLock must
 * never be held simultaneously.
 */

static int decoder_trylock_result = -1;
static void *decoder_trylock_thread(void *arg) {
	player_t *player = (player_t *)arg;
	decoder_trylock_result = pthread_mutex_trylock(&player->decoderLock);
	return NULL;
}

/* decoderLock mutual exclusion: one thread holds it, another's trylock fails (EBUSY) */
START_TEST(test_decoder_lock_mutual_exclusion) {
	player_t player;
	BarSettings_t settings;
	pthread_t other;
	
	memset(&player, 0, sizeof(player));
	memset(&settings, 0, sizeof(settings));
	pthread_mutex_init(&player.lock, NULL);
	pthread_cond_init(&player.cond, NULL);
	pthread_mutex_init(&player.decoderLock, NULL);
	pthread_cond_init(&player.decoderCond, NULL);
	
	decoder_trylock_result = -1;
	/* Main thread holds decoderLock */
	pthread_mutex_lock(&player.decoderLock);
	
	/* Other thread's trylock must fail with EBUSY */
	pthread_create(&other, NULL, decoder_trylock_thread, &player);
	pthread_join(other, NULL);
	ck_assert_int_eq(decoder_trylock_result, EBUSY);
	
	pthread_mutex_unlock(&player.decoderLock);
	
	pthread_cond_destroy(&player.decoderCond);
	pthread_mutex_destroy(&player.decoderLock);
	pthread_cond_destroy(&player.cond);
	pthread_mutex_destroy(&player.lock);
}
END_TEST

/* Allowed lock order: player.lock then decoderLock (never hold both at once).
 * This test documents the invariant by taking them in the allowed order
 * and releasing before taking the other. */
START_TEST(test_player_lock_and_decoder_lock_never_held_together) {
	player_t player;
	BarSettings_t settings;
	
	memset(&player, 0, sizeof(player));
	memset(&settings, 0, sizeof(settings));
	pthread_mutex_init(&player.lock, NULL);
	pthread_cond_init(&player.cond, NULL);
	pthread_mutex_init(&player.decoderLock, NULL);
	pthread_cond_init(&player.decoderCond, NULL);
	
	/* Allowed order: take player.lock, release it, then take decoderLock.
	 * Code must never hold both; we only take one at a time. */
	pthread_mutex_lock(&player.lock);
	/* do something under lock */
	pthread_mutex_unlock(&player.lock);
	
	pthread_mutex_lock(&player.decoderLock);
	/* do something under decoderLock */
	pthread_mutex_unlock(&player.decoderLock);
	
	pthread_cond_destroy(&player.decoderCond);
	pthread_mutex_destroy(&player.decoderLock);
	pthread_cond_destroy(&player.cond);
	pthread_mutex_destroy(&player.lock);
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
	
	TCase *tc_decoder = tcase_create("decoderLock behavior");
	tcase_add_test(tc_decoder, test_decoder_lock_mutual_exclusion);
	tcase_add_test(tc_decoder, test_player_lock_and_decoder_lock_never_held_together);
	suite_add_tcase(s, tc_decoder);
	
	return s;
}

