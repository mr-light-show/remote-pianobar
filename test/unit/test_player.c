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
#include <unistd.h>
#include <limits.h>
#include <stdio.h>

#include <libavutil/error.h>

#include "../../src/interrupt.h"
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

/* BarIsAvErrStaleCdnUrl: 403 is stale CDN, success codes and other errors are not */
START_TEST(test_stale_cdn_403) {
	ck_assert(BarIsAvErrStaleCdnUrl(0) == false);
	ck_assert(BarIsAvErrStaleCdnUrl(-(int)ENOMEM) == false);
#if defined(AVERROR_HTTP_FORBIDDEN)
	ck_assert(BarIsAvErrStaleCdnUrl(AVERROR_HTTP_FORBIDDEN) == true);
#endif
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

START_TEST (test_player_wait_for_mode_returns_true_when_already_in_mode)
{
	player_t player;
	BarSettings_t settings;
	memset (&player, 0, sizeof (player));
	BarSettingsInit (&settings);
	BarPlayerInit (&player, &settings);
	/* Default mode after init is PLAYER_DEAD — wait for PLAYER_DEAD */
	ck_assert (BarPlayerWaitForMode (&player, PLAYER_DEAD, 10));
	BarPlayerDestroy (&player);
	BarSettingsDestroy (&settings);
}
END_TEST

START_TEST (test_player_wait_for_mode_times_out_when_mode_differs)
{
	player_t player;
	BarSettings_t settings;
	memset (&player, 0, sizeof (player));
	BarSettingsInit (&settings);
	BarPlayerInit (&player, &settings);
	player.mode = PLAYER_PLAYING;
	/* 1 ms timeout — must return false quickly */
	ck_assert (!BarPlayerWaitForMode (&player, PLAYER_DEAD, 1));
	player.mode = PLAYER_DEAD;
	BarPlayerDestroy (&player);
	BarSettingsDestroy (&settings);
}
END_TEST

START_TEST (test_player_wait_for_mode_null_returns_false)
{
	ck_assert (!BarPlayerWaitForMode (NULL, PLAYER_DEAD, 100));
}
END_TEST

typedef struct {
	player_t *player;
	bool result;
} WaitForModeArgs_t;

static void *wait_for_dead_thread (void *arg)
{
	WaitForModeArgs_t *args = (WaitForModeArgs_t *)arg;
	args->result = BarPlayerWaitForMode (args->player, PLAYER_DEAD, 1000);
	return NULL;
}

START_TEST (test_player_set_mode_wakes_waiter)
{
	player_t player;
	BarSettings_t settings;
	pthread_t waiter;
	WaitForModeArgs_t args;
	memset (&player, 0, sizeof (player));
	BarSettingsInit (&settings);
	BarPlayerInit (&player, &settings);
	BarPlayerSetMode (&player, PLAYER_PLAYING);

	args.player = &player;
	args.result = false;
	ck_assert_int_eq (pthread_create (&waiter, NULL, wait_for_dead_thread, &args), 0);
	usleep (20000);
	BarPlayerSetMode (&player, PLAYER_DEAD);
	ck_assert_int_eq (pthread_join (waiter, NULL), 0);
	ck_assert (args.result);

	BarPlayerDestroy (&player);
	BarSettingsDestroy (&settings);
}
END_TEST

START_TEST (test_player_set_mode_null_is_noop)
{
	BarPlayerSetMode (NULL, PLAYER_PLAYING);
	ck_assert (1);
}
END_TEST

/* --- BarPlayerThread / openStream coverage (headless via PIANOBAR_TEST_NO_DEVICE) --- */

static void player_thread_test_setup (player_t *player, BarSettings_t *settings)
{
	setenv ("PIANOBAR_TEST_NO_DEVICE", "1", 1);
	memset (player, 0, sizeof (*player));
	memset (settings, 0, sizeof (*settings));
	BarSettingsInit (settings);
	settings->uiMode = BAR_UI_MODE_CLI;
	settings->timeout = 2;
	BarPlayerInit (player, settings);
}

static void player_thread_test_teardown (player_t *player, BarSettings_t *settings)
{
	BarPlayerDestroy (player);
	BarSettingsDestroy (settings);
}

static uintptr_t run_player_thread_sync (player_t *player, const char *url)
{
	player->url = strdup (url);
	ck_assert_ptr_nonnull (player->url);
	void *ret = BarPlayerThread (player);
	free (player->url);
	player->url = NULL;
	return (uintptr_t) ret;
}

static bool player_mp3_fixture_path (char *buf, size_t len)
{
	const char *rel = "test/fixtures/tone.mp3";
	if (access (rel, R_OK) != 0) {
		return false;
	}
	char resolved[PATH_MAX];
	if (realpath (rel, resolved) == NULL) {
		return false;
	}
	return snprintf (buf, len, "file://%s", resolved) < (int) len;
}

START_TEST (test_player_thread_rejects_invalid_url)
{
	player_t player;
	BarSettings_t settings;
	player_thread_test_setup (&player, &settings);

	const uintptr_t ret = run_player_thread_sync (&player, "not-a-valid-scheme://x");
	ck_assert_int_eq (ret, PLAYER_RET_SOFTFAIL);
	ck_assert_int_eq (BarPlayerGetMode (&player), PLAYER_FINISHED);

	player_thread_test_teardown (&player, &settings);
}
END_TEST

START_TEST (test_player_thread_connection_refused)
{
	player_t player;
	BarSettings_t settings;
	player_thread_test_setup (&player, &settings);

	const uintptr_t ret = run_player_thread_sync (&player,
	                                              "http://127.0.0.1:9/refused.mp3");
	ck_assert_int_eq (ret, PLAYER_RET_SOFTFAIL);
	ck_assert_int_eq (BarPlayerGetMode (&player), PLAYER_FINISHED);

	player_thread_test_teardown (&player, &settings);
}
END_TEST

START_TEST (test_player_thread_missing_local_file)
{
	player_t player;
	BarSettings_t settings;
	player_thread_test_setup (&player, &settings);

	const uintptr_t ret = run_player_thread_sync (&player,
	                                              "file:///tmp/pianobar-nonexistent-test.mp3");
	ck_assert_int_eq (ret, PLAYER_RET_SOFTFAIL);
	ck_assert_int_eq (BarPlayerGetMode (&player), PLAYER_FINISHED);

	player_thread_test_teardown (&player, &settings);
}
END_TEST

START_TEST (test_player_thread_no_audio_stream_in_container)
{
	player_t player;
	BarSettings_t settings;
	char tmppath[] = "/tmp/pianobar-noaudio-XXXXXX";
	const int fd = mkstemp (tmppath);
	ck_assert (fd >= 0);
	const char payload[] = "not an audio container\n";
	ck_assert_int_eq ((int) write (fd, payload, sizeof payload - 1),
	                  (int) (sizeof payload - 1));
	close (fd);

	player_thread_test_setup (&player, &settings);
	char url[PATH_MAX + 16];
	snprintf (url, sizeof url, "file://%s", tmppath);
	const uintptr_t ret = run_player_thread_sync (&player, url);
	ck_assert_int_eq (ret, PLAYER_RET_SOFTFAIL);
	unlink (tmppath);

	player_thread_test_teardown (&player, &settings);
}
END_TEST

START_TEST (test_player_thread_quit_before_open)
{
	player_t player;
	BarSettings_t settings;
	player_thread_test_setup (&player, &settings);

	BarInterruptSetTarget (&player.interrupted);
	pthread_mutex_lock (&player.lock);
	player.doQuit = true;
	pthread_mutex_unlock (&player.lock);

	const uintptr_t ret = run_player_thread_sync (&player, "http://127.0.0.1:9/x.mp3");
	ck_assert_int_eq (ret, PLAYER_RET_OK);
	ck_assert_int_eq (BarPlayerGetMode (&player), PLAYER_FINISHED);

	player_thread_test_teardown (&player, &settings);
}
END_TEST

START_TEST (test_player_thread_hardfail_when_engine_uninitialized)
{
	player_t player;
	BarSettings_t settings;
	char url[PATH_MAX + 16];

	if (!player_mp3_fixture_path (url, sizeof url)) {
		return;
	}

	player_thread_test_setup (&player, &settings);
	player.engineInitialized = false;

	const uintptr_t ret = run_player_thread_sync (&player, url);
	ck_assert_int_eq (ret, PLAYER_RET_HARDFAIL);
	ck_assert_int_eq (BarPlayerGetMode (&player), PLAYER_FINISHED);

	player_thread_test_teardown (&player, &settings);
}
END_TEST

START_TEST (test_player_thread_plays_local_mp3_fixture)
{
	player_t player;
	BarSettings_t settings;
	char url[PATH_MAX + 16];

	if (!player_mp3_fixture_path (url, sizeof url)) {
		return;
	}
	player_thread_test_setup (&player, &settings);
	if (!player.engineInitialized) {
		player_thread_test_teardown (&player, &settings);
		return;
	}

	const uintptr_t ret = run_player_thread_sync (&player, url);
	ck_assert_int_eq (ret, PLAYER_RET_OK);
	ck_assert_int_eq (BarPlayerGetMode (&player), PLAYER_FINISHED);

	player_thread_test_teardown (&player, &settings);
}
END_TEST

START_TEST (test_player_thread_interrupt_during_playback)
{
	player_t player;
	BarSettings_t settings;
	char url[PATH_MAX + 16];
	pthread_t thread;
	uintptr_t thread_ret = PLAYER_RET_OK;

	if (!player_mp3_fixture_path (url, sizeof url)) {
		return;
	}
	player_thread_test_setup (&player, &settings);
	if (!player.engineInitialized) {
		player_thread_test_teardown (&player, &settings);
		return;
	}

	player.url = strdup (url);
	ck_assert_ptr_nonnull (player.url);
	ck_assert_int_eq (pthread_create (&thread, NULL, BarPlayerThread, &player), 0);
	ck_assert (BarPlayerWaitForMode (&player, PLAYER_PLAYING, 10000));

	BarInterruptSetTarget (&player.interrupted);
	BarInterruptIncrement ();
	pthread_mutex_lock (&player.lock);
	player.doQuit = true;
	pthread_cond_broadcast (&player.cond);
	pthread_mutex_unlock (&player.lock);

	ck_assert_int_eq (pthread_join (thread, (void **) &thread_ret), 0);
	free (player.url);
	player.url = NULL;
	ck_assert_int_eq (BarPlayerGetMode (&player), PLAYER_FINISHED);

	player_thread_test_teardown (&player, &settings);
}
END_TEST

START_TEST (test_player_reinit_reuses_existing_engine)
{
	player_t player;
	BarSettings_t settings;
	player_thread_test_setup (&player, &settings);
	if (!player.engineInitialized) {
		player_thread_test_teardown (&player, &settings);
		return;
	}
	const ma_engine *engine_before = &player.engine;
	BarPlayerInit (&player, &settings);
	ck_assert (player.engineInitialized);
	ck_assert_ptr_eq (&player.engine, engine_before);
	player_thread_test_teardown (&player, &settings);
}
END_TEST

START_TEST (test_player_thread_video_only_container)
{
	player_t player;
	BarSettings_t settings;
	char tmppath[] = "/tmp/pianobar-videoonly-XXXXXX";
	char url[PATH_MAX + 32];

	if (system ("command -v ffmpeg >/dev/null 2>&1") != 0) {
		return;
	}

	close (mkstemp (tmppath));
	char cmd[512];
	snprintf (cmd, sizeof cmd,
	          "ffmpeg -y -loglevel quiet -f lavfi -i color=c=black:s=64x64:d=0.1 "
	          "-c:v libx264 -an '%s' 2>/dev/null", tmppath);
	if (system (cmd) != 0) {
		unlink (tmppath);
		return;
	}

	player_thread_test_setup (&player, &settings);
	snprintf (url, sizeof url, "file://%s", tmppath);
	const uintptr_t ret = run_player_thread_sync (&player, url);
	ck_assert_int_eq (ret, PLAYER_RET_SOFTFAIL);
	unlink (tmppath);
	player_thread_test_teardown (&player, &settings);
}
END_TEST

START_TEST (test_player_thread_double_interrupt_during_playback)
{
	player_t player;
	BarSettings_t settings;
	char url[PATH_MAX + 16];
	pthread_t thread;
	uintptr_t thread_ret = PLAYER_RET_OK;

	if (!player_mp3_fixture_path (url, sizeof url)) {
		return;
	}
	player_thread_test_setup (&player, &settings);
	if (!player.engineInitialized) {
		player_thread_test_teardown (&player, &settings);
		return;
	}

	player.url = strdup (url);
	ck_assert_ptr_nonnull (player.url);
	BarInterruptSetTarget (&player.interrupted);
	ck_assert_int_eq (pthread_create (&thread, NULL, BarPlayerThread, &player), 0);
	ck_assert (BarPlayerWaitForMode (&player, PLAYER_PLAYING, 10000));

	BarInterruptIncrement ();
	BarInterruptIncrement ();
	pthread_mutex_lock (&player.lock);
	player.doQuit = true;
	pthread_cond_broadcast (&player.cond);
	pthread_mutex_unlock (&player.lock);

	ck_assert_int_eq (pthread_join (thread, (void **) &thread_ret), 0);
	free (player.url);
	player.url = NULL;
	ck_assert_int_eq (BarPlayerGetMode (&player), PLAYER_FINISHED);

	player_thread_test_teardown (&player, &settings);
}
END_TEST

START_TEST (test_player_thread_truncated_mp3_fixture_fails_open)
{
	player_t player;
	BarSettings_t settings;
	char src[PATH_MAX];
	char tmppath[] = "/tmp/pianobar-trunc-XXXXXX";
	char url[PATH_MAX + 32];
	FILE *in, *out;
	unsigned char buf[128];
	size_t n;

	if (!player_mp3_fixture_path (url, sizeof url)) {
		return;
	}
	snprintf (src, sizeof src, "%s", url + strlen ("file://"));

	close (mkstemp (tmppath));
	in = fopen (src, "rb");
	out = fopen (tmppath, "wb");
	ck_assert_ptr_nonnull (in);
	ck_assert_ptr_nonnull (out);
	n = fread (buf, 1, sizeof buf, in);
	ck_assert (n > 0);
	ck_assert_int_eq (fwrite (buf, 1, n, out), n);
	fclose (in);
	fclose (out);

	player_thread_test_setup (&player, &settings);
	snprintf (url, sizeof url, "file://%s", tmppath);
	const uintptr_t ret = run_player_thread_sync (&player, url);
	ck_assert_int_eq (ret, PLAYER_RET_SOFTFAIL);
	unlink (tmppath);
	player_thread_test_teardown (&player, &settings);
}
END_TEST

Suite *player_suite(void) {
	Suite *s;
	TCase *tc_basic;
	
	s = suite_create("Player");
	
	tc_basic = tcase_create("Basic Functions");
	tcase_add_test(tc_basic, test_player_is_paused);
	tcase_add_test(tc_basic, test_stale_cdn_403);
	tcase_add_test(tc_basic, test_player_get_mode);
	tcase_add_test(tc_basic, test_player_reset_initializes_fields);
	suite_add_tcase(s, tc_basic);
	
	TCase *tc_decoder = tcase_create("decoderLock behavior");
	tcase_add_test(tc_decoder, test_decoder_lock_mutual_exclusion);
	tcase_add_test(tc_decoder, test_player_lock_and_decoder_lock_never_held_together);
	suite_add_tcase(s, tc_decoder);

	TCase *tc_wait = tcase_create ("BarPlayerWaitForMode");
	tcase_add_test (tc_wait, test_player_wait_for_mode_returns_true_when_already_in_mode);
	tcase_add_test (tc_wait, test_player_wait_for_mode_times_out_when_mode_differs);
	tcase_add_test (tc_wait, test_player_wait_for_mode_null_returns_false);
	tcase_add_test (tc_wait, test_player_set_mode_wakes_waiter);
	tcase_add_test (tc_wait, test_player_set_mode_null_is_noop);
	suite_add_tcase (s, tc_wait);

	TCase *tc_thread = tcase_create ("BarPlayerThread");
	tcase_add_test (tc_thread, test_player_thread_rejects_invalid_url);
	tcase_add_test (tc_thread, test_player_thread_connection_refused);
	tcase_add_test (tc_thread, test_player_thread_missing_local_file);
	tcase_add_test (tc_thread, test_player_thread_no_audio_stream_in_container);
	tcase_add_test (tc_thread, test_player_thread_quit_before_open);
	tcase_add_test (tc_thread, test_player_thread_hardfail_when_engine_uninitialized);
	tcase_add_test (tc_thread, test_player_thread_plays_local_mp3_fixture);
	tcase_add_test (tc_thread, test_player_thread_interrupt_during_playback);
	tcase_add_test (tc_thread, test_player_reinit_reuses_existing_engine);
	tcase_add_test (tc_thread, test_player_thread_video_only_container);
	tcase_add_test (tc_thread, test_player_thread_double_interrupt_during_playback);
	tcase_add_test (tc_thread, test_player_thread_truncated_mp3_fixture_fails_open);
	suite_add_tcase (s, tc_thread);
	
	return s;
}

