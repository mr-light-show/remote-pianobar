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
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <dlfcn.h>
#include <math.h>

#include <libavutil/error.h>
#include <libavfilter/buffersink.h>

#include "../../src/interrupt.h"
#include "../../src/log.h"
#include "../../src/player.h"
#include "../../src/settings.h"
#include "../audio_fixture_callback.h"
/* Exercise the real CLI cleanup in both WebSocket and NOWEBSOCKET builds. */
#define main BarTestMainProgram
#include "../../src/main.c"
#undef main

static void player_thread_test_setup (player_t *player, BarSettings_t *settings);
static void player_thread_test_teardown (player_t *player, BarSettings_t *settings);

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

/* Set up an actual stopped node without a device callback; no worker exists
 * while the fixture is installed. Its external buffer outlives sound cleanup. */
static void player_stopped_sound_fixture (player_t *player, ma_audio_buffer *buffer) {
	static const float samples[960] = {0};
	ma_audio_buffer_config config = ma_audio_buffer_config_init (ma_format_f32, 2, 480, samples, NULL);
	config.sampleRate = 44100;
	ck_assert_int_eq (ma_audio_buffer_init (&config, buffer), MA_SUCCESS);
	ck_assert_int_eq (ma_sound_init_from_data_source (&player->engine, buffer, 0, NULL, &player->sound), MA_SUCCESS);
	pthread_mutex_lock (&player->lock);
	player->audioState = PLAYER_AUDIO_STOPPED;
	player->soundInitialized = true;
	player->mode = PLAYER_PLAYING;
	++player->controlEpoch;
	pthread_mutex_unlock (&player->lock);
}

typedef struct {
	player_t *player;
	bool result;
	_Atomic unsigned skippedReads;
	BarPlayerAudioSnapshot snapshot;
} AudioSnapshotArgs;

static void *audio_snapshot_thread (void *data) {
	AudioSnapshotArgs *args = data;
	struct timespec deadline;
	clock_gettime (CLOCK_MONOTONIC, &deadline);
	++deadline.tv_sec;
	for (;;) {
		args->result = BarPlayerGetAudioSnapshot (args->player, &args->snapshot);
		if (args->result) { break; }
		atomic_fetch_add (&args->skippedReads, 1);
		/* Production observations intentionally skip transient contention. The
		 * fixture retries within its existing one-second ownership bound, and
		 * exits immediately once quit/end/terminal failure makes it ineligible. */
		pthread_mutex_lock (&args->player->lock);
		const bool eligible = !args->player->doQuit && !args->player->audioTerminalFailure &&
			args->player->mode == PLAYER_PLAYING;
		pthread_mutex_unlock (&args->player->lock);
		struct timespec now;
		clock_gettime (CLOCK_MONOTONIC, &now);
		if (!eligible || now.tv_sec > deadline.tv_sec ||
			(now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) { break; }
		usleep (5000);
	}
	return NULL;
}

static void wait_for_audio_owner (player_t *player, pthread_t owner) {
	bool owned = false;
	for (unsigned elapsed = 0; elapsed < 1000; elapsed += 5) {
		pthread_mutex_lock (&player->lock);
		owned = player->audioBusy && player->audioOwnerValid && pthread_equal (player->audioOwner, owner);
		pthread_mutex_unlock (&player->lock);
		if (owned) { break; }
		usleep (5000);
	}
	pthread_mutex_lock (&player->lock);
	const bool busy = player->audioBusy, ownerValid = player->audioOwnerValid, quit = player->doQuit;
	const BarPlayerMode mode = player->mode;
	const BarPlayerAudioState state = player->audioState;
	const unsigned waiters = player->audioControlWaiters;
	pthread_mutex_unlock (&player->lock);
	ck_assert_msg (owned, "Observation must reserve the actual sound before accessing its source: busy=%d ownerValid=%d quit=%d mode=%d state=%d waiters=%u",
		busy, ownerValid, quit, mode, state, waiters);
}

static void wait_for_audio_waiters (player_t *player, unsigned count) {
	bool waiting = false;
	for (unsigned elapsed = 0; elapsed < 1000; elapsed += 5) {
		pthread_mutex_lock (&player->lock);
		waiting = player->audioControlWaiters == count;
		pthread_mutex_unlock (&player->lock);
		if (waiting) { break; }
		usleep (5000);
	}
	ck_assert_msg (waiting, "Control operation must reach its reservation wait");
}

/* Break caught: the test observation worker treats an intentionally skipped
 * trylock sample as its final answer instead of retrying like real callers. */
START_TEST (test_player_snapshot_reader_retries_transient_control_lock)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	AudioSnapshotArgs args = {.player = &player};
	pthread_t reader;
	pthread_mutex_lock (&player.lock);
	ck_assert_int_eq (pthread_create (&reader, NULL, audio_snapshot_thread, &args), 0);
	for (unsigned elapsed = 0; atomic_load (&args.skippedReads) == 0 && elapsed < 1000; elapsed += 5) {
		usleep (5000);
	}
	const unsigned skipped = atomic_load (&args.skippedReads);
	const bool noOwner = !player.audioBusy && !player.audioOwnerValid;
	const bool eligible = !player.doQuit && !player.audioTerminalFailure && player.mode == PLAYER_PLAYING &&
		player.audioState == PLAYER_AUDIO_STOPPED && player.audioControlWaiters == 0;
	pthread_mutex_unlock (&player.lock);
	ck_assert_int_eq (pthread_join (reader, NULL), 0);
	ck_assert_msg (skipped > 0 && noOwner && eligible,
		"A held player.lock must skip a snapshot despite an eligible stopped sound and no owner");
	const bool observed = args.result;
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
	ck_assert_msg (observed, "The test reader must retry a skipped sample after transient lock contention ends");
}
END_TEST

typedef struct {
	player_t *player;
	bool result;
	BarPlayerPlayStateSnapshot *snapshot;
} AudioControlArgs;
static void *set_waiting_volume (void *data) {
	AudioControlArgs *args = data;
	args->result = BarPlayerSetVolume (args->player, 81);
	return NULL;
}
static void *stop_waiting_audio (void *data) {
	AudioControlArgs *args = data;
	args->result = BarPlayerStopAudio (args->player);
	return NULL;
}

static bool fatal_hook_saw_unlocked_player, fatal_hook_saw_unlocked_decoder;
static unsigned fatal_hook_calls;
static void returning_audio_fatal_hook (player_t *player, const char *operation) {
	(void)operation;
	++fatal_hook_calls;
	fatal_hook_saw_unlocked_player = pthread_mutex_trylock (&player->lock) == 0;
	if (fatal_hook_saw_unlocked_player) { pthread_mutex_unlock (&player->lock); }
	fatal_hook_saw_unlocked_decoder = pthread_mutex_trylock (&player->decoderLock) == 0;
	if (fatal_hook_saw_unlocked_decoder) { pthread_mutex_unlock (&player->decoderLock); }
}

static bool fail_final_join (pthread_t thread, void **retval, unsigned int seconds) {
	(void)retval;
	ck_assert (pthread_equal (thread, pthread_self ()));
	ck_assert_uint_eq (seconds, 10);
	return false;
}

static void *joined_player_worker (void *data) { return data; }

/* Break caught: an exited but unjoined worker allows condition/engine teardown. */
START_TEST (test_player_destroy_requires_successful_join)
{
	player_t player;
	BarSettings_t settings;
	player_thread_test_setup (&player, &settings);
	player.threadJoinPending = true;
	pthread_t worker;
	ck_assert_int_eq (pthread_create (&worker, NULL, joined_player_worker, (void *)42), 0);
	BarPlayerSetAudioFatalTestHook (returning_audio_fatal_hook);
	fatal_hook_calls = 0;
	ck_assert (!BarPlayerDestroy (&player));
	ck_assert_int_eq (fatal_hook_calls, 1);
	ck_assert (fatal_hook_saw_unlocked_player && fatal_hook_saw_unlocked_decoder);
	ck_assert (player.engineInitialized && player.synchronizationInitialized && player.threadJoinPending);
	void *result = NULL;
	ck_assert (BarPlayerJoinThreadWithTimeout (&player, worker, &result, 1));
	ck_assert_ptr_eq (result, (void *)42);
	ck_assert (!player.threadJoinPending);
	BarPlayerSetAudioFatalTestHook (NULL);
	player.audioTerminalFailure = false; /* Only the joined test harness may recover. */
	ck_assert (BarPlayerDestroy (&player));
	BarSettingsDestroy (&settings);
}
END_TEST

/* Break caught: production fatal shutdown returns or runs normal destructors. */
START_TEST (test_player_production_fatal_shutdown_exits_failure)
{
	player_t player;
	BarSettings_t settings;
	player_thread_test_setup (&player, &settings);
	BarPlayerSetAudioFatalTestHook (NULL);
	const pid_t child = fork ();
	ck_assert_int_ge (child, 0);
	if (child == 0) {
		BarPlayerFatalShutdown (&player, "terminal hook contract");
		_Exit (77);
	}
	int status;
	ck_assert_int_eq (waitpid (child, &status, 0), child);
	ck_assert (WIFEXITED (status));
	ck_assert_int_eq (WEXITSTATUS (status), EXIT_FAILURE);
	player_thread_test_teardown (&player, &settings);
}
END_TEST

/* Break caught: a failed CLI join permits teardown of a live player's app. */
START_TEST (test_main_final_join_is_terminal_before_shared_state_cleanup)
{
	BarApp_t app = {0};
	player_thread_test_setup (&app.player, &app.settings);
	BarPlayerSetMode (&app.player, PLAYER_FINISHED);
	BarPlayerSetJoinTestHook (fail_final_join);
	BarPlayerSetAudioFatalTestHook (returning_audio_fatal_hook);
	fatal_hook_calls = 0;
	pthread_t worker = pthread_self ();
	BarMainPlayerCleanup (&app, &worker);
	ck_assert_int_eq (fatal_hook_calls, 1);
	ck_assert (fatal_hook_saw_unlocked_player && fatal_hook_saw_unlocked_decoder);
	ck_assert (app.player.engineInitialized && app.player.synchronizationInitialized);
	ck_assert_int_eq (BarPlayerGetMode (&app.player), PLAYER_FINISHED);
	ck_assert (!BarPlayerDestroy (&app.player));
	BarPlayerSetJoinTestHook (NULL);
	BarPlayerSetAudioFatalTestHook (NULL);
	app.player.audioTerminalFailure = false;
	player_thread_test_teardown (&app.player, &app.settings);
}
END_TEST

/* Break caught: main enters shared-resource destruction after a terminal
 * player failure whose test hook returned to its caller. */
START_TEST (test_main_finalization_rejects_terminal_player)
{
	BarApp_t app = {0};
	app.player.audioTerminalFailure = true;
	ck_assert_int_eq (BarMainFinalizationStatus (&app), EXIT_FAILURE);
	app.player.audioTerminalFailure = false;
	ck_assert_int_eq (BarMainFinalizationStatus (&app), EXIT_SUCCESS);
}
END_TEST

typedef struct {
	player_t *player;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool ready, release;
} DecoderGate;

static void *hold_decoder_until_released (void *data) {
	DecoderGate *gate = data;
	pthread_mutex_lock (&gate->player->decoderLock);
	pthread_mutex_lock (&gate->lock);
	gate->ready = true;
	pthread_cond_broadcast (&gate->cond);
	while (!gate->release) { pthread_cond_wait (&gate->cond, &gate->lock); }
	pthread_mutex_unlock (&gate->lock);
	pthread_mutex_unlock (&gate->player->decoderLock);
	return NULL;
}

typedef struct {
	player_t *player;
	BarApp_t *app;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool expired, returnWait;
	int waitResult;
} AudioWaitBoundary;
static _Atomic (AudioWaitBoundary *) audioWaitBoundary;

/* Forward to the real OS wait. Only the deadline test gates its return, after
 * the command's actual timeout, modeling an owner winning mutex reacquisition
 * at expiry. The original owner must finish its real reservation; this helper
 * never changes audio ownership or the wait result. */
static int gate_audio_wait_boundary (pthread_cond_t *cond, pthread_mutex_t *mutex, int result) {
	AudioWaitBoundary *boundary = atomic_load (&audioWaitBoundary);
	if (boundary == NULL || cond != &boundary->player->audioCond || result != ETIMEDOUT) {
		return result;
	}
	ck_assert_ptr_eq (mutex, &boundary->player->lock);
	/* The OS returned with player.lock held. Release it before touching any
	 * other lock, and restore the pthread wait contract before returning. */
	ck_assert_int_eq (pthread_mutex_unlock (mutex), 0);
	pthread_mutex_lock (&boundary->lock);
	boundary->waitResult = result;
	boundary->expired = true;
	pthread_cond_broadcast (&boundary->cond);
	while (!boundary->returnWait) { pthread_cond_wait (&boundary->cond, &boundary->lock); }
	pthread_mutex_unlock (&boundary->lock);
	/* The harness has joined the decoder holder and the reservation owner.
	 * Check both other application locks separately, never nested. */
	ck_assert_int_eq (pthread_mutex_trylock (&boundary->player->decoderLock), 0);
	ck_assert_int_eq (pthread_mutex_unlock (&boundary->player->decoderLock), 0);
#ifdef WEBSOCKET_ENABLED
	ck_assert_int_eq (pthread_rwlock_trywrlock (&boundary->app->stateRwlock), 0);
	ck_assert_int_eq (pthread_rwlock_unlock (&boundary->app->stateRwlock), 0);
#endif
	ck_assert_int_eq (pthread_mutex_lock (mutex), 0);
	return result;
}

#ifdef __APPLE__
int pthread_cond_timedwait_relative_np (pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *duration) {
	int (*realWait) (pthread_cond_t *, pthread_mutex_t *, const struct timespec *) =
		dlsym (RTLD_NEXT, "pthread_cond_timedwait_relative_np");
	ck_assert_ptr_nonnull (realWait);
	return gate_audio_wait_boundary (cond, mutex, realWait (cond, mutex, duration));
}
#else
int pthread_cond_timedwait (pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *deadline) {
	int (*realWait) (pthread_cond_t *, pthread_mutex_t *, const struct timespec *) =
		dlsym (RTLD_NEXT, "pthread_cond_timedwait");
	ck_assert_ptr_nonnull (realWait);
	return gate_audio_wait_boundary (cond, mutex, realWait (cond, mutex, deadline));
}
#endif

/* Break caught: stop steals a blocked owner's sound or leaks its timeout waiter. */
START_TEST (test_player_stop_timeout_preserves_blocked_owner_and_unlocks_before_fatal)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	DecoderGate gate = {.player = &player};
	pthread_mutex_init (&gate.lock, NULL);
	pthread_cond_init (&gate.cond, NULL);
	pthread_t holder, owner;
	ck_assert_int_eq (pthread_create (&holder, NULL, hold_decoder_until_released, &gate), 0);
	pthread_mutex_lock (&gate.lock);
	while (!gate.ready) { pthread_cond_wait (&gate.cond, &gate.lock); }
	pthread_mutex_unlock (&gate.lock);
	AudioSnapshotArgs observation = {.player = &player};
	ck_assert_int_eq (pthread_create (&owner, NULL, audio_snapshot_thread, &observation), 0);
	wait_for_audio_owner (&player, owner);
	BarPlayerAudioSnapshot skipped = {.cursorFrames = 777};
	struct timespec start, end;
	clock_gettime (CLOCK_MONOTONIC, &start);
	ck_assert (!BarPlayerGetAudioSnapshot (&player, &skipped));
	clock_gettime (CLOCK_MONOTONIC, &end);
	ck_assert_int_eq (skipped.cursorFrames, 777);
	ck_assert_int_lt ((end.tv_sec - start.tv_sec) * 1000L + (end.tv_nsec - start.tv_nsec) / 1000000L, 100);
	BarPlayerSetAudioFatalTestHook (returning_audio_fatal_hook);
	fatal_hook_calls = 0;
	BarPlayerRequestStop (&player);
	ck_assert_int_eq (fatal_hook_calls, 1);
	ck_assert (fatal_hook_saw_unlocked_player);
	ck_assert (player.audioBusy && pthread_equal (player.audioOwner, owner));
	ck_assert_int_eq (player.audioControlWaiters, 0);
	ck_assert (player.soundInitialized && player.engineInitialized && player.synchronizationInitialized);
	pthread_mutex_lock (&gate.lock);
	gate.release = true;
	pthread_cond_broadcast (&gate.cond);
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_join (holder, NULL), 0);
	ck_assert_int_eq (pthread_join (owner, NULL), 0);
	ck_assert (!observation.result); /* stop superseded this owner's sample */
	ck_assert (!player.audioBusy && !player.audioOwnerValid);
	BarPlayerSetAudioFatalTestHook (NULL);
	player.audioTerminalFailure = false;
	ck_assert (BarPlayerDestroy (&player));
	ma_audio_buffer_uninit (&buffer);
	BarSettingsDestroy (&settings);
	pthread_cond_destroy (&gate.cond);
	pthread_mutex_destroy (&gate.lock);
}
END_TEST

/* Break caught: ETIMEDOUT rejects a now-free reservation without rechecking
 * availability after the original owner wins mutex reacquisition at expiry. */
START_TEST (test_player_owner_release_at_command_deadline_never_steals_audio)
{
	BarApp_t app = {0};
	player_t *player = &app.player;
	ma_audio_buffer buffer;
	player_thread_test_setup (player, &app.settings);
#ifdef WEBSOCKET_ENABLED
	app.settings.uiMode = BAR_UI_MODE_BOTH;
#endif
	BarStateInit (&app);
	player_stopped_sound_fixture (player, &buffer);
	ck_assert (BarPlayerSetVolume (player, 50));
	DecoderGate gate = {.player = player};
	pthread_mutex_init (&gate.lock, NULL);
	pthread_cond_init (&gate.cond, NULL);
	pthread_t holder, owner, command;
	ck_assert_int_eq (pthread_create (&holder, NULL, hold_decoder_until_released, &gate), 0);
	pthread_mutex_lock (&gate.lock);
	while (!gate.ready) { pthread_cond_wait (&gate.cond, &gate.lock); }
	pthread_mutex_unlock (&gate.lock);
	AudioSnapshotArgs observation = {.player = player};
	ck_assert_int_eq (pthread_create (&owner, NULL, audio_snapshot_thread, &observation), 0);
	wait_for_audio_owner (player, owner);
	AudioControlArgs volumeCommand = {.player = player};
	AudioWaitBoundary boundary = {.player = player, .app = &app};
	pthread_mutex_init (&boundary.lock, NULL);
	pthread_cond_init (&boundary.cond, NULL);
	atomic_store (&audioWaitBoundary, &boundary);
	ck_assert_int_eq (pthread_create (&command, NULL, set_waiting_volume, &volumeCommand), 0);
	pthread_mutex_lock (&boundary.lock);
	while (!boundary.expired) { pthread_cond_wait (&boundary.cond, &boundary.lock); }
	pthread_mutex_unlock (&boundary.lock);
	/* The command's own OS timeout has occurred; it cannot recheck yet. The
	 * original owner still owns the real node until its source is unblocked. */
	pthread_mutex_lock (&player->lock);
	ck_assert (player->audioBusy && pthread_equal (player->audioOwner, owner));
	ck_assert_uint_eq (player->audioControlWaiters, 1);
	ck_assert_int_eq (player->requestedVolume, 50);
	pthread_mutex_unlock (&player->lock);
	pthread_mutex_lock (&gate.lock);
	gate.release = true;
	pthread_cond_broadcast (&gate.cond);
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_join (holder, NULL), 0);
	ck_assert_int_eq (pthread_join (owner, NULL), 0);
	ck_assert (observation.result);
	pthread_mutex_lock (&player->lock);
	ck_assert (!player->audioBusy && !player->audioOwnerValid);
	ck_assert_uint_eq (player->audioControlWaiters, 1);
	ck_assert_int_eq (player->requestedVolume, 50);
	pthread_mutex_unlock (&player->lock);
	pthread_mutex_lock (&boundary.lock);
	boundary.returnWait = true;
	pthread_cond_broadcast (&boundary.cond);
	pthread_mutex_unlock (&boundary.lock);
	ck_assert_int_eq (pthread_join (command, NULL), 0);
	atomic_store (&audioWaitBoundary, NULL);
	ck_assert_int_eq (boundary.waitResult, ETIMEDOUT);
	ck_assert_msg (volumeCommand.result,
		"The command must acquire the reservation freed by its owner at the real timed-wait boundary");
	ck_assert_int_eq (BarPlayerGetVolume (player), 81);
	ck_assert_int_eq (player->audioControlWaiters, 0);
	ck_assert (!player->audioBusy && !player->audioOwnerValid);
	BarStateDestroy (&app);
	player_thread_test_teardown (player, &app.settings);
	ma_audio_buffer_uninit (&buffer);
	pthread_cond_destroy (&gate.cond);
	pthread_mutex_destroy (&gate.lock);
	pthread_cond_destroy (&boundary.cond);
	pthread_mutex_destroy (&boundary.lock);
}
END_TEST

/* Break caught: a cursor query accesses a freed node, a volume timeout steals
 * its lifetime, or a returning fatal hook lets destroy continue into uninit. */
START_TEST (test_player_snapshot_reserves_lifetime_and_timeout_does_not_steal)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	ck_assert (BarPlayerSetVolume (&player, 50));
	player_stopped_sound_fixture (&player, &buffer);
	AudioSnapshotArgs args = {.player = &player};
	pthread_t reader, holder;
	DecoderGate gate = {.player = &player};
	pthread_mutex_init (&gate.lock, NULL);
	pthread_cond_init (&gate.cond, NULL);
	ck_assert_int_eq (pthread_create (&holder, NULL, hold_decoder_until_released, &gate), 0);
	pthread_mutex_lock (&gate.lock);
	while (!gate.ready) { pthread_cond_wait (&gate.cond, &gate.lock); }
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_create (&reader, NULL, audio_snapshot_thread, &args), 0);
	wait_for_audio_owner (&player, reader);
	ck_assert (!BarPlayerSetVolume (&player, 72));
	pthread_mutex_lock (&player.lock);
	ck_assert (player.audioBusy);
	ck_assert (pthread_equal (player.audioOwner, reader));
	ck_assert_int_eq (player.audioControlWaiters, 0);
	ck_assert_int_eq (player.requestedVolume, 50);
	pthread_mutex_unlock (&player.lock);

	BarPlayerSetAudioFatalTestHook (returning_audio_fatal_hook);
	fatal_hook_calls = 0;
	fatal_hook_saw_unlocked_player = false;
	ck_assert (!BarPlayerDestroy (&player));
	ck_assert_int_eq (fatal_hook_calls, 1);
	ck_assert (fatal_hook_saw_unlocked_player);
	/* Another thread deliberately holds decoderLock. The calling thread owns
	 * no application lock while invoking the fatal hook. */
	ck_assert (player.engineInitialized);
	ck_assert (player.soundInitialized);
	ck_assert (player.synchronizationInitialized);
	ck_assert (player.audioBusy);
	ck_assert (pthread_equal (player.audioOwner, reader));
	ck_assert_int_eq (player.audioControlWaiters, 0);
	pthread_mutex_lock (&gate.lock);
	gate.release = true;
	pthread_cond_broadcast (&gate.cond);
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_join (holder, NULL), 0);
	ck_assert_int_eq (pthread_join (reader, NULL), 0);
	pthread_cond_destroy (&gate.cond);
	pthread_mutex_destroy (&gate.lock);
	ck_assert (!player.audioBusy);
	BarPlayerSetAudioFatalTestHook (NULL);
	/* Test-only cleanup after all owners have returned. Production remains terminal. */
	pthread_mutex_lock (&player.lock);
	player.audioTerminalFailure = false;
	player.doQuit = false;
	++player.controlEpoch;
	pthread_mutex_unlock (&player.lock);
	ck_assert (BarPlayerDestroy (&player));
	ma_audio_buffer_uninit (&buffer);
	BarSettingsDestroy (&settings);
}
END_TEST

static void *interrupt_callback_thread (void *data) {
	return (void *)(intptr_t)BarPlayerFfmpegInterruptCb (data);
}

/* Break caught: a normal waiter claims after quit, leaks its waiter count,
 * or an abort/release leaves an eligible teardown waiter asleep. */
START_TEST (test_player_quit_aborts_normal_waiter_and_releases_teardown)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	ck_assert (BarPlayerSetVolume (&player, 42));
	player_stopped_sound_fixture (&player, &buffer);
	DecoderGate gate = {.player = &player};
	pthread_mutex_init (&gate.lock, NULL);
	pthread_cond_init (&gate.cond, NULL);
	pthread_t holder, reader, volume, stopper, interruptor;
	ck_assert_int_eq (pthread_create (&holder, NULL, hold_decoder_until_released, &gate), 0);
	pthread_mutex_lock (&gate.lock);
	while (!gate.ready) { pthread_cond_wait (&gate.cond, &gate.lock); }
	pthread_mutex_unlock (&gate.lock);
	AudioSnapshotArgs snapshot = {.player = &player};
	ck_assert_int_eq (pthread_create (&reader, NULL, audio_snapshot_thread, &snapshot), 0);
	wait_for_audio_owner (&player, reader);
	AudioControlArgs update = {.player = &player}, stop = {.player = &player};
	ck_assert_int_eq (pthread_create (&volume, NULL, set_waiting_volume, &update), 0);
	wait_for_audio_waiters (&player, 1);
	atomic_store (&player.interrupted, 2);
	/* Forced interrupt now completes physical teardown, so its worker waits
	 * concurrently with the explicitly tested teardown worker below. */
	ck_assert_int_eq (pthread_create (&interruptor, NULL, interrupt_callback_thread, &player), 0);
	ck_assert_int_eq (pthread_join (volume, NULL), 0);
	ck_assert (!update.result);
	ck_assert_int_eq (BarPlayerGetVolume (&player), 42);
	ck_assert_int_eq (pthread_create (&stopper, NULL, stop_waiting_audio, &stop), 0);
	wait_for_audio_waiters (&player, 2);
	BarPlayerAudioSnapshot skipped;
	ck_assert (!BarPlayerGetAudioSnapshot (&player, &skipped));
	pthread_mutex_lock (&gate.lock);
	gate.release = true;
	pthread_cond_broadcast (&gate.cond);
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_join (holder, NULL), 0);
	ck_assert_int_eq (pthread_join (reader, NULL), 0);
	void *interruptResult = NULL;
	ck_assert_int_eq (pthread_join (interruptor, &interruptResult), 0);
	ck_assert_int_eq ((intptr_t)interruptResult, 1);
	ck_assert_int_eq (pthread_join (stopper, NULL), 0);
	ck_assert (!snapshot.result);
	ck_assert (stop.result);
	ck_assert (!player.audioBusy);
	ck_assert_int_eq (player.audioControlWaiters, 0);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_STOPPED);
	pthread_cond_destroy (&gate.cond);
	pthread_mutex_destroy (&gate.lock);
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
}
END_TEST

/* Break caught: observation never queries the real cursor or leaks ownership. */
START_TEST (test_player_snapshot_reads_actual_stopped_node)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	BarPlayerAudioSnapshot snapshot;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	ck_assert (BarPlayerGetAudioSnapshot (&player, &snapshot));
	ck_assert_int_eq (snapshot.state, PLAYER_AUDIO_STOPPED);
	ck_assert (!snapshot.playing);
	ck_assert (!snapshot.atEnd);
	ck_assert (!snapshot.deviceStarted);
	ck_assert_uint_eq (snapshot.cursorFrames, 0);
	ck_assert_float_eq (snapshot.cursorSeconds, 0.0f);
	ck_assert (!player.audioBusy);
	ck_assert (!player.audioOwnerValid);
	ck_assert_ptr_null (player.audioOperation);
	ck_assert (BarPlayerSetVolume (&player, 25));
	ck_assert_float_eq (ma_sound_get_volume (&player.sound), 0.25f);
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
}
END_TEST

/* Break caught: repeated commands restart/seek the retained node, reset the
 * pause timer, or toggle twice from one stale flag value. */
START_TEST (test_player_controls_are_idempotent_and_preserve_retained_cursor)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	ck_assert_int_eq (ma_sound_seek_to_pcm_frame (&player.sound, 120), MA_SUCCESS);
	ck_assert (BarPlayerSetPaused (&player, false));
	ck_assert (ma_sound_is_playing (&player.sound));
	ck_assert (BarPlayerSetPaused (&player, false));
	ck_assert (!player.audioBusy);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_RUNNING);
	ck_assert (BarPlayerSetPaused (&player, true));
	const uint64_t epoch = player.controlEpoch;
	const time_t timestamp = player.pauseStartTime;
	ck_assert (timestamp > 0);
	ck_assert (BarPlayerSetPaused (&player, true));
	ck_assert_uint_eq (player.controlEpoch, epoch);
	ck_assert_int_eq (player.pauseStartTime, timestamp);
	ck_assert (!ma_sound_is_playing (&player.sound));
	ck_assert (!player.audioBusy);
	ck_assert (player.sourceReadCancelled);
	bool paused;
	ck_assert (BarPlayerTogglePaused (&player, &paused));
	ck_assert (!paused);
	ck_assert (!player.sourceReadCancelled);
	ck_assert (ma_sound_is_playing (&player.sound));
	ck_assert (BarPlayerTogglePaused (&player, &paused));
	ck_assert (paused);
	ck_assert (!player.audioBusy);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_STOPPED);
	ma_uint64 cursor;
	ck_assert_int_eq (ma_sound_get_cursor_in_pcm_frames (&player.sound, &cursor), MA_SUCCESS);
	ck_assert_uint_eq (cursor, 120);
	BarPlayerRequestStop (&player);
	ck_assert (player.doQuit);
	ck_assert (!player.doPause);
	ck_assert_int_eq (player.pauseStartTime, 0);
	ck_assert (!player.audioBusy);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_STOPPED);
	ck_assert (!ma_sound_is_playing (&player.sound));
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
}
END_TEST

/* Break caught: an ended/dead retained sound is restarted by a command, or
 * NULL input dereferences instead of rejecting the command. */
START_TEST (test_player_controls_reject_terminal_modes_and_null_inputs)
{
	ck_assert (!BarPlayerSetPaused (NULL, true));
	ck_assert (!BarPlayerTogglePaused (NULL, NULL));
	BarPlayerPlayStateSnapshot completed = {false, 987};
	ck_assert (!BarPlayerSetPausedWithSnapshot (NULL, true, &completed));
	ck_assert (!BarPlayerTogglePausedWithSnapshot (NULL, &completed));
	ck_assert (!completed.paused);
	ck_assert_uint_eq (completed.controlEpoch, 987);
	BarPlayerRequestStop (NULL);
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	ck_assert (BarPlayerSetPaused (&player, true));
	const BarPlayerMode modes[] = {PLAYER_FINISHED, PLAYER_DEAD};
	for (unsigned i = 0; i < 2; ++i) {
		BarPlayerSetMode (&player, modes[i]);
		ck_assert (!BarPlayerSetPaused (&player, false));
		ck_assert (!BarPlayerTogglePaused (&player, NULL));
		ck_assert (!BarPlayerSetPausedWithSnapshot (&player, false, &completed));
		ck_assert (!BarPlayerTogglePausedWithSnapshot (&player, &completed));
		ck_assert (!completed.paused);
		ck_assert_uint_eq (completed.controlEpoch, 987);
		ck_assert (player.doPause);
		ck_assert (!ma_sound_is_playing (&player.sound));
		ck_assert (!player.audioBusy);
		ck_assert_int_eq (player.audioControlWaiters, 0);
		ck_assert_int_eq (player.audioState, PLAYER_AUDIO_STOPPED);
	}
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
}
END_TEST

/* A backend claiming device support but exposing no device cannot be
 * reconciled. Inject that capability mismatch below the player boundary;
 * keep the real engine and node to verify they survive a returning fatal hook. */
static void assert_missing_device_failure_is_terminal (unsigned operation) {
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	player.audioNoDevice = false;
	BarPlayerSetAudioFatalTestHook (returning_audio_fatal_hook);
	fatal_hook_calls = 0;
	fatal_hook_saw_unlocked_player = fatal_hook_saw_unlocked_decoder = false;
	bool result;
	if (operation == 0) { result = BarPlayerStopAudio (&player); }
	else if (operation == 1) { result = BarPlayerReset (&player); }
	else { result = BarPlayerDestroy (&player); }
	ck_assert (!result);
	ck_assert_int_eq (fatal_hook_calls, 1);
	ck_assert (fatal_hook_saw_unlocked_player);
	ck_assert (fatal_hook_saw_unlocked_decoder);
	ck_assert (player.doQuit);
	ck_assert (player.audioTerminalFailure);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_FAILED);
	ck_assert (!player.audioBusy);
	ck_assert (!player.audioOwnerValid);
	ck_assert_int_eq (player.audioControlWaiters, 0);
	ck_assert (player.engineInitialized);
	ck_assert (player.soundInitialized);
	ck_assert (player.synchronizationInitialized);
	ck_assert (!BarPlayerSetVolume (&player, 81));
	ck_assert_int_eq (fatal_hook_calls, 1);
	/* Test-only recovery after the failed operation returned and no workers exist. */
	BarPlayerSetAudioFatalTestHook (NULL);
	player.audioNoDevice = true;
	pthread_mutex_lock (&player.lock);
	player.audioTerminalFailure = false;
	player.doQuit = false;
	++player.controlEpoch;
	pthread_mutex_unlock (&player.lock);
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
}

/* Break caught: physical stop claims success after an unreconciled device. */
START_TEST (test_player_stop_unreconciled_device_is_terminal) {
	assert_missing_device_failure_is_terminal (0);
}
END_TEST
/* Break caught: reset clears control fields/frees sound after a failed stop. */
START_TEST (test_player_reset_unreconciled_device_is_terminal) {
	assert_missing_device_failure_is_terminal (1);
}
END_TEST
/* Break caught: destroy uninitializes a live engine after a failed stop. */
START_TEST (test_player_destroy_unreconciled_device_is_terminal) {
	assert_missing_device_failure_is_terminal (2);
}
END_TEST

/* Break caught: reset leaves a previously started device running at idle. */
START_TEST (test_player_reset_stops_started_device)
{
	player_t player = {0};
	BarSettings_t settings;
	BarSettingsInit (&settings);
	BarPlayerInit (&player, &settings);
	ck_assert (player.engineInitialized);
	ma_device *device = ma_engine_get_device (&player.engine);
	if (device != NULL) {
		ck_assert_int_eq (ma_engine_start (&player.engine), MA_SUCCESS);
		ck_assert (ma_device_is_started (device));
	}
	BarPlayerReset (&player);
	const bool started = device != NULL && ma_device_is_started (device);
	BarPlayerDestroy (&player);
	BarSettingsDestroy (&settings);
	ck_assert_msg (!started, "Reset must stop the idle engine");
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

/* An initialized idle player must not start the audio callback/device. */
START_TEST (test_player_init_leaves_audio_device_stopped)
{
	player_t player;
	BarSettings_t settings;
	memset (&player, 0, sizeof (player));
	BarSettingsInit (&settings);
	BarPlayerInit (&player, &settings);

	const bool engine_initialized = player.engineInitialized;
	ma_device *device = engine_initialized ? ma_engine_get_device (&player.engine) : NULL;
	const bool device_started = device != NULL && ma_device_is_started (device);

	BarPlayerDestroy (&player);
	BarSettingsDestroy (&settings);

	ck_assert_msg (engine_initialized, "Player initialization must create an audio engine");
	ck_assert_msg (!device_started, "An idle player's audio device must remain stopped");
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
#ifdef WEBSOCKET_ENABLED
	settings->uiMode = BAR_UI_MODE_CLI;
#endif
	settings->timeout = 2;
	BarPlayerInit (player, settings);
}

static void player_thread_test_teardown (player_t *player, BarSettings_t *settings)
{
	BarPlayerDestroy (player);
	BarSettingsDestroy (settings);
}

START_TEST (test_player_public_audio_controls_reject_null)
{
	player_t uninitialized = {0};
	ck_assert (!BarPlayerDestroy (NULL));
	ck_assert (!BarPlayerDestroy (&uninitialized));
	ck_assert (!BarPlayerReset (NULL));
	ck_assert (!BarPlayerStopAudio (NULL));
	ck_assert (!BarPlayerSetVolume (NULL, 80));
	ck_assert (!BarPlayerAdjustVolume (NULL, 1));
	ck_assert_int_eq (BarPlayerGetVolume (NULL), -1);
	BarPlayerAudioSnapshot snapshot;
	ck_assert (!BarPlayerGetAudioSnapshot (NULL, &snapshot));
	player_t player;
	BarSettings_t settings;
	player_thread_test_setup (&player, &settings);
	ck_assert (!BarPlayerGetAudioSnapshot (&player, NULL));
	BarPlayerSetMode (&player, PLAYER_PLAYING);
	ck_assert (!BarPlayerGetAudioSnapshot (&player, &snapshot));
	ck_assert (!player.audioBusy);
	player_thread_test_teardown (&player, &settings);
}
END_TEST

START_TEST (test_player_init_clamps_volume_and_honors_sample_rate)
{
	const int volumes[] = {-1, VOLUME_MAX_PERCENT + 1};
	const int expected[] = {0, VOLUME_MAX_PERCENT};
	setenv ("PIANOBAR_TEST_NO_DEVICE", "1", 1);
	for (unsigned i = 0; i < 2; ++i) {
		player_t player = {0};
		BarSettings_t settings;
		BarSettingsInit (&settings);
		settings.volume = volumes[i];
		settings.sampleRate = 48000;
		BarPlayerInit (&player, &settings);
		ck_assert (player.engineInitialized);
		ck_assert_int_eq (BarPlayerGetVolume (&player), expected[i]);
		ck_assert_int_eq (settings.volume, expected[i]);
		ck_assert_uint_eq (ma_engine_get_sample_rate (&player.engine), 48000);
		/* An engine initialization failure still leaves synchronization to clean up. */
		ma_engine_uninit (&player.engine);
		player.engineInitialized = false;
		ck_assert (BarPlayerDestroy (&player));
		BarSettingsDestroy (&settings);
	}
}
END_TEST

static ma_result failing_audio_cursor (ma_data_source *source, ma_uint64 *cursor) {
	(void) source;
	(void) cursor;
	return MA_ERROR;
}

static ma_result failing_audio_format (ma_data_source *source, ma_format *format,
		ma_uint32 *channels, ma_uint32 *sampleRate, ma_channel *map, size_t capacity) {
	(void) source; (void) format; (void) channels; (void) sampleRate;
	(void) map; (void) capacity;
	return MA_ERROR;
}

START_TEST (test_player_snapshot_source_errors_preserve_output_and_release_owner)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	ma_data_source_base *source = (ma_data_source_base *) &buffer;
	const ma_data_source_vtable *original = source->vtable;
	for (unsigned i = 0; i < 2; ++i) {
		ma_data_source_vtable failing = *original;
		if (i == 0) { failing.onGetCursor = failing_audio_cursor; }
		else { failing.onGetDataFormat = failing_audio_format; }
		source->vtable = &failing;
		BarPlayerAudioSnapshot snapshot = {.cursorFrames = 987, .cursorSeconds = 123};
		ck_assert (!BarPlayerGetAudioSnapshot (&player, &snapshot));
		ck_assert_uint_eq (snapshot.cursorFrames, 987);
		ck_assert (snapshot.cursorSeconds == 123);
		ck_assert (!player.audioBusy && !player.audioOwnerValid);
		ck_assert (player.soundInitialized);
		source->vtable = original;
		ck_assert (BarPlayerGetAudioSnapshot (&player, &snapshot));
		ck_assert_uint_eq (snapshot.cursorFrames, 0);
	}
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
}
END_TEST

typedef struct {
	player_t *player;
	pthread_cond_t readyCond;
	bool ready, expected, observed;
	time_t timestamp;
} PauseConditionWaiter;

static void *wait_for_pause_condition (void *data) {
	PauseConditionWaiter *waiter = data;
	struct timespec deadline;
	clock_gettime (CLOCK_REALTIME, &deadline);
	++deadline.tv_sec;
	pthread_mutex_lock (&waiter->player->lock);
	waiter->ready = true;
	pthread_cond_broadcast (&waiter->readyCond);
	bool woke = false;
	while (waiter->player->doPause != waiter->expected) {
		if (pthread_cond_timedwait (&waiter->player->cond, &waiter->player->lock, &deadline) != 0) { break; }
		woke = waiter->player->doPause == waiter->expected;
	}
	waiter->observed = woke && waiter->player->doPause == waiter->expected;
	waiter->timestamp = waiter->player->pauseStartTime;
	pthread_mutex_unlock (&waiter->player->lock);
	return NULL;
}

/* Break caught: logical controls omit the timestamp or condition wake, change
 * NONE to a device state, leak ownership, or return a stale toggle result. */
START_TEST (test_player_pause_helpers_without_sound_wake_condition_waiters)
{
	player_t player;
	BarSettings_t settings;
	player_thread_test_setup (&player, &settings);
	ck_assert (player.engineInitialized);
	BarPlayerSetMode (&player, PLAYER_WAITING);
	ck_assert (!player.soundInitialized);
	ck_assert (!player.audioBusy);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_NONE);
	ck_assert (!player.doPause);
	ck_assert_int_eq (player.pauseStartTime, 0);
	const bool expected[] = {true, false, true, false};
	for (unsigned i = 0; i < 4; ++i) {
		PauseConditionWaiter waiter = {.player = &player, .expected = expected[i]};
		pthread_cond_init (&waiter.readyCond, NULL);
		pthread_t thread;
		ck_assert_int_eq (pthread_create (&thread, NULL, wait_for_pause_condition, &waiter), 0);
		pthread_mutex_lock (&player.lock);
		while (!waiter.ready) { pthread_cond_wait (&waiter.readyCond, &player.lock); }
		pthread_mutex_unlock (&player.lock);
		bool paused = !expected[i];
		const bool result = i < 2 ? BarPlayerSetPaused (&player, expected[i]) : BarPlayerTogglePaused (&player, &paused);
		ck_assert_int_eq (pthread_join (thread, NULL), 0);
		pthread_cond_destroy (&waiter.readyCond);
		ck_assert (result);
		ck_assert_msg (waiter.observed, "A waiting player.cond consumer must observe the completed control");
		pthread_mutex_lock (&player.lock);
		ck_assert_int_eq (player.doPause, expected[i]);
		ck_assert_int_eq (waiter.timestamp, player.pauseStartTime);
		if (expected[i]) { ck_assert (player.pauseStartTime > 0); }
		else { ck_assert_int_eq (player.pauseStartTime, 0); }
		if (i >= 2) { ck_assert_int_eq (paused, player.doPause); }
		ck_assert_int_eq (player.audioState, PLAYER_AUDIO_NONE);
		ck_assert (!player.audioBusy);
		pthread_mutex_unlock (&player.lock);
	}
	ck_assert (BarPlayerDestroy (&player));
	ck_assert (!player.engineInitialized);
	ck_assert (!player.synchronizationInitialized);
	BarSettingsDestroy (&settings);
}
END_TEST

/* Break caught: idle volume updates are discarded or escape the 0..100 range. */
START_TEST (test_player_volume_is_canonical_and_clamped_without_sound)
{
	player_t player;
	BarSettings_t settings;
	player_thread_test_setup (&player, &settings);
	ck_assert (BarPlayerSetVolume (&player, 73));
	ck_assert_int_eq (BarPlayerGetVolume (&player), 73);
	ck_assert_int_eq (settings.volume, 73);
	ck_assert (BarPlayerAdjustVolume (&player, 1000));
	ck_assert_int_eq (BarPlayerGetVolume (&player), 100);
	ck_assert (BarPlayerAdjustVolume (&player, -1000));
	ck_assert_int_eq (BarPlayerGetVolume (&player), 0);
	ck_assert_int_eq (settings.volume, 0);
	ck_assert (!player.audioBusy);
	ck_assert_int_eq (player.audioControlWaiters, 0);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_NONE);
	player_thread_test_teardown (&player, &settings);
}
END_TEST

/* Break caught: observations wait, steal a live owner, or bypass queued controls. */
START_TEST (test_player_audio_observation_yields_to_owner_and_controls)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	BarPlayerAudioSnapshot snapshot;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	pthread_mutex_lock (&player.lock);
	player.audioBusy = true;
	player.audioOwner = pthread_self ();
	player.audioOwnerValid = true;
	pthread_mutex_unlock (&player.lock);
	ck_assert (!BarPlayerGetAudioSnapshot (&player, &snapshot));
	ck_assert (player.audioBusy);
	ck_assert (!BarPlayerSetVolume (&player, 65));
	ck_assert (player.audioBusy);
	ck_assert_int_eq (player.audioControlWaiters, 0);
	pthread_mutex_lock (&player.lock);
	player.audioBusy = false;
	player.audioOwnerValid = false;
	player.audioControlWaiters = 1;
	pthread_mutex_unlock (&player.lock);
	ck_assert (!BarPlayerGetAudioSnapshot (&player, &snapshot));
	ck_assert_int_eq (player.audioControlWaiters, 1);
	player.audioControlWaiters = 0;
	ck_assert (BarPlayerGetAudioSnapshot (&player, &snapshot));
	ck_assert_int_eq (snapshot.state, PLAYER_AUDIO_STOPPED);
	ck_assert (!player.audioBusy);
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
}
END_TEST

/* Break caught: normal controls claim a reservation after quit. */
START_TEST (test_player_volume_aborts_after_quit_without_mutation)
{
	player_t player;
	BarSettings_t settings;
	player_thread_test_setup (&player, &settings);
	ck_assert (BarPlayerSetVolume (&player, 42));
	pthread_mutex_lock (&player.lock);
	player.doQuit = true;
	++player.controlEpoch;
	pthread_mutex_unlock (&player.lock);
	ck_assert (!BarPlayerSetVolume (&player, 81));
	ck_assert (!BarPlayerAdjustVolume (&player, 9));
	ck_assert_int_eq (BarPlayerGetVolume (&player), 42);
	ck_assert_int_eq (settings.volume, 42);
	ck_assert_int_eq (player.audioControlWaiters, 0);
	ck_assert (!player.audioBusy);
	ck_assert (BarPlayerReset (&player));
	ck_assert (!player.doQuit);
	ck_assert (!player.audioBusy);
	player_thread_test_teardown (&player, &settings);
}
END_TEST

static uintptr_t run_player_thread_sync (player_t *player, const char *url)
{
	player->url = strdup (url);
	ck_assert_ptr_nonnull (player->url);
	BarPlayerSetMode (player, PLAYER_WAITING);
	pthread_t thread;
	ck_assert_int_eq (pthread_create (&thread, NULL, BarPlayerThread, player), 0);
	for (unsigned elapsed = 0; BarPlayerGetMode (player) == PLAYER_WAITING && elapsed < 10000; elapsed += 5) {
		usleep (5000);
	}
	if (BarPlayerGetMode (player) == PLAYER_PLAYING) {
		ck_assert (BarTestDrainAudioFixture (player));
	}
	void *ret;
	ck_assert_int_eq (pthread_join (thread, &ret), 0);
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

typedef struct {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool entered, release, failAllocation;
	bool blockFree, freeEntered, releaseFree;
	player_t *player;
	bool checkedLocks, locksFree;
	bool reenter, recursiveRejected, ownerRetained;
	long recursiveElapsedMs;
	ma_allocation_callbacks original;
} SoundAllocationGate;

/* Deterministic backend gates have no competing control/decoder lock owner.
 * Probe separately: the test itself never nests these application locks. */
static void probe_backend_application_locks (SoundAllocationGate *gate) {
	if (gate->player == NULL) { return; }
	const int control = pthread_mutex_trylock (&gate->player->lock);
	if (control == 0) { pthread_mutex_unlock (&gate->player->lock); }
	const int decoder = pthread_mutex_trylock (&gate->player->decoderLock);
	if (decoder == 0) { pthread_mutex_unlock (&gate->player->decoderLock); }
	gate->checkedLocks = true;
	gate->locksFree = gate->locksFree && control == 0 && decoder == 0;
}

static void gate_sound_allocation (SoundAllocationGate *gate) {
	probe_backend_application_locks (gate);
	if (gate->reenter) {
		struct timespec start, end;
		clock_gettime (CLOCK_MONOTONIC, &start);
		gate->recursiveRejected = !BarPlayerAdjustVolume (gate->player, 1);
		clock_gettime (CLOCK_MONOTONIC, &end);
		gate->recursiveElapsedMs = (end.tv_sec - start.tv_sec) * 1000L + (end.tv_nsec - start.tv_nsec) / 1000000L;
		pthread_mutex_lock (&gate->player->lock);
		gate->ownerRetained = gate->player->audioBusy && gate->player->audioOwnerValid &&
			pthread_equal (gate->player->audioOwner, pthread_self ()) && gate->player->audioControlWaiters == 0;
		pthread_mutex_unlock (&gate->player->lock);
	}
	pthread_mutex_lock (&gate->lock);
	if (!gate->entered) {
		gate->entered = true;
		pthread_cond_broadcast (&gate->cond);
		while (!gate->release) { pthread_cond_wait (&gate->cond, &gate->lock); }
	}
	pthread_mutex_unlock (&gate->lock);
}
static void *gated_sound_malloc (size_t size, void *data) {
	SoundAllocationGate *gate = data;
	gate_sound_allocation (gate);
	if (gate->failAllocation) { return NULL; }
	return gate->original.onMalloc (size, gate->original.pUserData);
}
static void *gated_sound_realloc (void *pointer, size_t size, void *data) {
	SoundAllocationGate *gate = data;
	gate_sound_allocation (gate);
	if (gate->failAllocation) { return NULL; }
	return gate->original.onRealloc (pointer, size, gate->original.pUserData);
}
static void gated_sound_free (void *pointer, void *data) {
	SoundAllocationGate *gate = data;
	probe_backend_application_locks (gate);
	pthread_mutex_lock (&gate->lock);
	if (gate->blockFree && !gate->freeEntered) {
		gate->freeEntered = true;
		pthread_cond_broadcast (&gate->cond);
		while (!gate->releaseFree) { pthread_cond_wait (&gate->cond, &gate->lock); }
	}
	pthread_mutex_unlock (&gate->lock);
	gate->original.onFree (pointer, gate->original.pUserData);
}

typedef struct {
	player_t *player;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool entered, done;
	ma_result result;
} EmptySourceRead;

static void *read_empty_ffmpeg_source (void *data) {
	EmptySourceRead *read = data;
	int16_t frames[960];
	ma_uint64 count;
	pthread_mutex_lock (&read->lock);
	read->entered = true;
	pthread_cond_broadcast (&read->cond);
	pthread_mutex_unlock (&read->lock);
	read->result = ma_data_source_read_pcm_frames (&read->player->dataSource, frames, 480, &count);
	pthread_mutex_lock (&read->lock);
	read->done = true;
	pthread_cond_broadcast (&read->cond);
	pthread_mutex_unlock (&read->lock);
	return NULL;
}

static bool wait_for_empty_source_read (EmptySourceRead *read, unsigned milliseconds) {
	struct timespec deadline;
	clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += milliseconds / 1000;
	deadline.tv_nsec += (long)(milliseconds % 1000) * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000L; }
	pthread_mutex_lock (&read->lock);
	while (!read->done) {
		if (pthread_cond_timedwait (&read->cond, &read->lock, &deadline) == ETIMEDOUT) { break; }
	}
	const bool done = read->done;
	pthread_mutex_unlock (&read->lock);
	return done;
}

/* Break caught: setup rollback never wakes a real source callback waiting
 * on an empty filter, or the wake allows that callback to wait again. The
 * allocator/free gates retain the real source until the reader is joined. */
START_TEST (test_player_setup_rollback_terminates_empty_source_callback)
{
	player_t player;
	BarSettings_t settings;
	char url[PATH_MAX + 16];
	ck_assert (player_mp3_fixture_path (url, sizeof url));
	player_thread_test_setup (&player, &settings);
	SoundAllocationGate gate = {.original = player.engine.allocationCallbacks, .blockFree = true};
	pthread_mutex_init (&gate.lock, NULL);
	pthread_cond_init (&gate.cond, NULL);
	player.engine.allocationCallbacks = (ma_allocation_callbacks) {
		.pUserData = &gate, .onMalloc = gated_sound_malloc,
		.onRealloc = gated_sound_realloc, .onFree = gated_sound_free,
	};
	player.url = strdup (url);
	BarPlayerSetMode (&player, PLAYER_WAITING);
	pthread_t worker, reader;
	ck_assert_int_eq (pthread_create (&worker, NULL, BarPlayerThread, &player), 0);
	pthread_mutex_lock (&gate.lock);
	while (!gate.entered) { pthread_cond_wait (&gate.cond, &gate.lock); }
	pthread_mutex_unlock (&gate.lock);
	EmptySourceRead read = {.player = &player};
	pthread_mutex_init (&read.lock, NULL);
	pthread_cond_init (&read.cond, NULL);
	ck_assert_int_eq (pthread_create (&reader, NULL, read_empty_ffmpeg_source, &read), 0);
	pthread_mutex_lock (&read.lock);
	while (!read.entered) { pthread_cond_wait (&read.cond, &read.lock); }
	pthread_mutex_unlock (&read.lock);
	const bool emptyReadBlocked = !wait_for_empty_source_read (&read, 100);
	BarPlayerSetMode (&player, PLAYER_DEAD);
	pthread_mutex_lock (&gate.lock);
	gate.release = true;
	pthread_cond_broadcast (&gate.cond);
	while (!gate.freeEntered) { pthread_cond_wait (&gate.cond, &gate.lock); }
	pthread_mutex_unlock (&gate.lock);
	const bool stoppedRead = wait_for_empty_source_read (&read, 1000);
	/* Rescue only the pre-fix failing callback while sound/source lifetime is
	 * still held by the cleanup gate. Never free a source with an active read. */
	if (!stoppedRead) {
		pthread_mutex_lock (&player.decoderLock);
		player.decodingFinished = true;
		pthread_cond_broadcast (&player.decoderCond);
		pthread_mutex_unlock (&player.decoderLock);
	}
	ck_assert_int_eq (pthread_join (reader, NULL), 0);
	pthread_mutex_lock (&gate.lock);
	gate.releaseFree = true;
	pthread_cond_broadcast (&gate.cond);
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_join (worker, NULL), 0);
	player.engine.allocationCallbacks = gate.original;
	free (player.url);
	player_thread_test_teardown (&player, &settings);
	pthread_cond_destroy (&read.cond);
	pthread_mutex_destroy (&read.lock);
	pthread_cond_destroy (&gate.cond);
	pthread_mutex_destroy (&gate.lock);
	ck_assert (emptyReadBlocked);
	ck_assert_msg (stoppedRead, "Physical stop must terminate an empty-source callback before uninitialization");
	ck_assert_int_eq (read.result, MA_SUCCESS);
}
END_TEST

static void *reset_audio_thread (void *data) {
	AudioControlArgs *args = data;
	args->result = BarPlayerReset (args->player);
	return NULL;
}

typedef struct {
	player_t *player;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool entered, release, failStart, locksFree, sawPaused, sawRearmed, sawSoundStopped;
	bool primeEngine, primeSoundStopped, primeSilent;
	_Atomic bool primeDone;
	ma_result primeResult;
	ma_result (*originalStart) (ma_device *device);
	ma_context context;
	ma_device device;
} DeviceStartGate;

/* Gate/fail the real null backend below miniaudio's device-state transition.
 * The device intentionally emits silence; actual sound state remains real. */
static ma_result gated_device_start (ma_device *device) {
	DeviceStartGate *gate = device->pUserData;
	gate->sawSoundStopped = !ma_sound_is_playing (&gate->player->sound);
	const int control = pthread_mutex_trylock (&gate->player->lock);
	if (control == 0) {
		gate->sawPaused = gate->player->doPause && gate->player->pauseStartTime == 123;
		pthread_mutex_unlock (&gate->player->lock);
	}
	const int decoder = pthread_mutex_trylock (&gate->player->decoderLock);
	if (decoder == 0) {
		gate->sawRearmed = !gate->player->sourceReadCancelled;
		pthread_mutex_unlock (&gate->player->decoderLock);
	}
	gate->locksFree = control == 0 && decoder == 0;
	pthread_mutex_lock (&gate->lock);
	gate->entered = true;
	pthread_cond_broadcast (&gate->cond);
	while (!gate->release) { pthread_cond_wait (&gate->cond, &gate->lock); }
	const bool fail = gate->failStart;
	pthread_mutex_unlock (&gate->lock);
	if (gate->primeEngine) {
		/* PulseAudio primes synchronously before signaling device start. Read
		 * the actual engine here, on that same backend startup boundary. */
		float frames[960] = {0};
		gate->primeSoundStopped = !ma_sound_is_playing (&gate->player->sound);
		gate->primeResult = ma_engine_read_pcm_frames (&gate->player->engine, frames, 480, NULL);
		gate->primeSilent = true;
		for (unsigned i = 0; i < 960; ++i) { gate->primeSilent &= frames[i] == 0; }
		atomic_store (&gate->primeDone, true);
	}
	return fail ? MA_ERROR : gate->originalStart != NULL ? gate->originalStart (device) : MA_SUCCESS;
}

static void install_device_start_gate (player_t *player, DeviceStartGate *gate) {
	gate->player = player;
	pthread_mutex_init (&gate->lock, NULL);
	pthread_cond_init (&gate->cond, NULL);
	const ma_backend backend = ma_backend_null;
	ck_assert_int_eq (ma_context_init (&backend, 1, NULL, &gate->context), MA_SUCCESS);
	ma_device_config config = ma_device_config_init (ma_device_type_playback);
	config.sampleRate = 44100;
	config.playback.format = ma_format_f32;
	config.playback.channels = 2;
	config.pUserData = gate;
	ck_assert_int_eq (ma_device_init (&gate->context, &config, &gate->device), MA_SUCCESS);
	gate->originalStart = gate->context.callbacks.onDeviceStart;
	gate->context.callbacks.onDeviceStart = gated_device_start;
	player->engine.pDevice = &gate->device;
	player->audioNoDevice = false;
}

static void wait_device_start_gate (DeviceStartGate *gate) {
	pthread_mutex_lock (&gate->lock);
	while (!gate->entered) { pthread_cond_wait (&gate->cond, &gate->lock); }
	pthread_mutex_unlock (&gate->lock);
}

static void release_device_start_gate (DeviceStartGate *gate) {
	pthread_mutex_lock (&gate->lock);
	gate->release = true;
	pthread_cond_broadcast (&gate->cond);
	pthread_mutex_unlock (&gate->lock);
}

static void remove_device_start_gate (player_t *player, DeviceStartGate *gate) {
	ck_assert_int_eq (ma_device_stop (&gate->device), MA_SUCCESS);
	player->engine.pDevice = NULL;
	player->audioNoDevice = true;
	gate->context.callbacks.onDeviceStart = gate->originalStart;
	ma_device_uninit (&gate->device);
	ma_context_uninit (&gate->context);
	pthread_cond_destroy (&gate->cond);
	pthread_mutex_destroy (&gate->lock);
}

static FILE *capture_audio_debug (int *savedStderr) {
	FILE *capture = tmpfile ();
	ck_assert_ptr_nonnull (capture);
	*savedStderr = dup (STDERR_FILENO);
	ck_assert_int_ge (*savedStderr, 0);
	ck_assert_int_ge (dup2 (fileno (capture), STDERR_FILENO), 0);
	log_set_debug_mask (DEBUG_AUDIO);
	return capture;
}

static void finish_audio_debug_capture (FILE *capture, int savedStderr,
		char *output, size_t outputSize) {
	fflush (stderr);
	ck_assert_int_ge (dup2 (savedStderr, STDERR_FILENO), 0);
	close (savedStderr);
	log_set_debug_mask (0);
	rewind (capture);
	const size_t count = fread (output, 1, outputSize - 1, capture);
	output[count] = '\0';
	fclose (capture);
}

/* Break caught: successful physical audio-device transitions are invisible at
 * DEBUG_AUDIO, or their log omits which lifecycle operation caused them. */
START_TEST (test_player_audio_debug_logs_pause_resume_restart_and_stop_transitions)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	DeviceStartGate gate = {.release = true};
	install_device_start_gate (&player, &gate);
	int savedStderr;
	char output[4096];
	FILE *capture = capture_audio_debug (&savedStderr);
	ck_assert (BarPlayerSetPaused (&player, false));
	ck_assert (BarPlayerSetPaused (&player, true));
	ck_assert (BarPlayerSetPaused (&player, false));
	ck_assert (BarPlayerStopAudio (&player));
	finish_audio_debug_capture (capture, savedStderr, output, sizeof output);
	ck_assert_ptr_nonnull (strstr (output, "Audio device started (resume/restart)"));
	ck_assert_ptr_nonnull (strstr (output, "Audio device stopped (pause)"));
	ck_assert_ptr_nonnull (strstr (output, "Audio device stopped (stop)"));
	remove_device_start_gate (&player, &gate);
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
}
END_TEST

/* Break caught: startup primes a started sound before its own decoder can
 * produce frames, blocking ma_engine_start and the only decoding worker. */
START_TEST (test_player_fresh_setup_primes_silence_before_starting_each_song)
{
	player_t player;
	BarSettings_t settings;
	char url[PATH_MAX + 16];
	ck_assert (player_mp3_fixture_path (url, sizeof url));
	player_thread_test_setup (&player, &settings);
	DeviceStartGate gate = {.primeEngine = true};
	ck_assert (BarPlayerSetVolume (&player, 75));
	install_device_start_gate (&player, &gate);
	ma_node_graph *const lifetimeGraph = ma_engine_get_node_graph (&player.engine);
	for (unsigned song = 0; song < 2; ++song) {
		ck_assert (BarPlayerReset (&player));
		gate.entered = gate.release = false;
		atomic_store (&gate.primeDone, false);
		player.url = strdup (url);
		BarPlayerSetMode (&player, PLAYER_WAITING);
		player.threadJoinPending = true;
		pthread_t worker;
		ck_assert_int_eq (pthread_create (&worker, NULL, BarPlayerThread, &player), 0);
		wait_device_start_gate (&gate);
		release_device_start_gate (&gate);
		for (unsigned ms = 0; ms < 300 && !atomic_load (&gate.primeDone); ++ms) { usleep (1000); }
		const bool primedWithoutDecoder = atomic_load (&gate.primeDone);
		if (!primedWithoutDecoder) {
			/* RED-only rescue: wake the blocked real source before any uninit.
			 * This must not turn the priming verdict into success. */
			pthread_mutex_lock (&player.decoderLock);
			player.sourceReadCancelled = true;
			pthread_cond_broadcast (&player.decoderCond);
			pthread_mutex_unlock (&player.decoderLock);
		}
		ck_assert (BarPlayerWaitForMode (&player, PLAYER_PLAYING, 1000));
		float frames[960];
		ck_assert_int_eq (ma_engine_read_pcm_frames (&player.engine, frames, 480, NULL), MA_SUCCESS);
		bool nonSilent = false;
		for (unsigned i = 0; i < 960; ++i) { nonSilent |= fabsf (frames[i]) > 0.00001f; }
		BarPlayerRequestStop (&player);
		ck_assert (BarPlayerJoinThreadWithTimeout (&player, worker, NULL, 2));
		free (player.url);
		player.url = NULL;
		ck_assert (!player.audioBusy && !player.soundInitialized);
		ck_assert (!ma_device_is_started (&gate.device));
		ck_assert_ptr_eq (ma_engine_get_node_graph (&player.engine), lifetimeGraph);
		ck_assert_msg (primedWithoutDecoder,
			"Song %u startup must finish its synchronous engine prime before decode can begin", song + 1);
		ck_assert (gate.primeSoundStopped && gate.primeSilent && gate.locksFree);
		ck_assert_int_eq (gate.primeResult, MA_SUCCESS);
		ck_assert_msg (nonSilent, "Song %u must emit real decoded tone samples after silent priming", song + 1);
	}
	remove_device_start_gate (&player, &gate);
	player_thread_test_teardown (&player, &settings);
}
END_TEST

static void *resume_audio_thread (void *data) {
	AudioControlArgs *args = data;
	args->result = BarPlayerSetPausedWithSnapshot (args->player, false, args->snapshot);
	return NULL;
}

static void *toggle_audio_thread (void *data) {
	AudioControlArgs *args = data;
	args->result = BarPlayerTogglePaused (args->player, NULL);
	return NULL;
}

static void *request_stop_thread (void *data) {
	BarPlayerRequestStop (data);
	return NULL;
}

/* Real device startup must leave the node stopped until it succeeds and its
 * setup epoch is admitted; failure/end/quit must fully roll back ownership. */
START_TEST (test_player_fresh_setup_device_boundary_rolls_back_failure_and_supersession)
{
	player_t player;
	BarSettings_t settings;
	char url[PATH_MAX + 16];
	ck_assert (player_mp3_fixture_path (url, sizeof url));
	player_thread_test_setup (&player, &settings);
	DeviceStartGate gate = {.failStart = _i == 0};
	install_device_start_gate (&player, &gate);
	player.url = strdup (url);
	BarPlayerSetMode (&player, PLAYER_WAITING);
	player.threadJoinPending = true;
	pthread_t worker, stopper;
	ck_assert_int_eq (pthread_create (&worker, NULL, BarPlayerThread, &player), 0);
	wait_device_start_gate (&gate);
	const bool stoppedAtStart = gate.sawSoundStopped;
	uint64_t expectedEpoch;
	const BarPlayerMode expectedMode = _i == 2 ? PLAYER_DEAD : PLAYER_FINISHED;
	if (_i == 3) {
		ck_assert_int_eq (pthread_create (&stopper, NULL, request_stop_thread, &player), 0);
		wait_for_audio_waiters (&player, 1);
	} else if (_i != 0) {
		BarPlayerSetMode (&player, expectedMode);
	}
	pthread_mutex_lock (&player.lock);
	expectedEpoch = player.controlEpoch + (_i == 0 || _i == 3 ? 1 : 0);
	if (_i == 1 || _i == 2) {
		player.doPause = true;
		player.pauseStartTime = 123;
		player.requestedVolume = 37;
	}
	pthread_mutex_unlock (&player.lock);
	release_device_start_gate (&gate);
	void *result;
	ck_assert (BarPlayerJoinThreadWithTimeout (&player, worker, &result, 2));
	if (_i == 3) { ck_assert_int_eq (pthread_join (stopper, NULL), 0); }
	ck_assert_int_eq ((uintptr_t) result, _i == 0 ? PLAYER_RET_HARDFAIL : PLAYER_RET_OK);
	ck_assert_int_eq (player.mode, expectedMode);
	ck_assert_uint_eq (player.controlEpoch, expectedEpoch);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_NONE);
	ck_assert (!player.soundInitialized && !player.dataSourceInitialized);
	ck_assert (!player.audioBusy && !player.threadJoinPending);
	ck_assert_int_eq (player.audioControlWaiters, 0);
	ck_assert (!ma_device_is_started (&gate.device));
	if (_i == 1 || _i == 2) {
		ck_assert (player.doPause);
		ck_assert_int_eq (player.pauseStartTime, 123);
		ck_assert_int_eq (player.requestedVolume, 37);
	}
	if (_i == 3) { ck_assert (player.doQuit && !player.doPause); }
	remove_device_start_gate (&player, &gate);
	free (player.url);
	player_thread_test_teardown (&player, &settings);
	ck_assert_msg (stoppedAtStart && gate.locksFree,
		"Fresh setup must start the device without running its undecoded sound or holding an application lock");
}
END_TEST

static void pause_fixture (player_t *player) {
	ck_assert (BarPlayerSetPaused (player, true));
	pthread_mutex_lock (&player->lock);
	player->pauseStartTime = 123;
	pthread_mutex_unlock (&player->lock);
}

/* Break caught: recursive public control waits for its own reserved operation. */
START_TEST (test_player_release_build_reentrancy_rejects_without_waiting)
{
	player_t player;
	BarSettings_t settings;
	player_thread_test_setup (&player, &settings);
	SoundAllocationGate gate = {.player = &player, .reenter = true, .release = true, .failAllocation = true, .locksFree = true};
	pthread_mutex_init (&gate.lock, NULL);
	pthread_cond_init (&gate.cond, NULL);
	gate.original = player.engine.allocationCallbacks;
	player.engine.allocationCallbacks = (ma_allocation_callbacks) {&gate, gated_sound_malloc, gated_sound_realloc, gated_sound_free};
	char url[PATH_MAX + 16];
	ck_assert (player_mp3_fixture_path (url, sizeof url));
	ck_assert_int_eq (run_player_thread_sync (&player, url), PLAYER_RET_HARDFAIL);
	ck_assert_msg (gate.recursiveRejected && gate.ownerRetained, "rejected=%d retained=%d elapsed=%ldms entered=%d", gate.recursiveRejected, gate.ownerRetained, gate.recursiveElapsedMs, gate.entered);
	ck_assert_int_lt (gate.recursiveElapsedMs, 100);
	ck_assert (!player.audioBusy);
	player.engine.allocationCallbacks = gate.original;
	player_thread_test_teardown (&player, &settings);
	pthread_cond_destroy (&gate.cond);
	pthread_mutex_destroy (&gate.lock);
}
END_TEST

/* Break caught: cursor/end polling wins another reservation ahead of a queued control. */
START_TEST (test_player_queued_control_has_priority_over_cursor_polling)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	pause_fixture (&player);
	DeviceStartGate startGate = {0};
	install_device_start_gate (&player, &startGate);
	DecoderGate gate = {.player = &player};
	pthread_mutex_init (&gate.lock, NULL);
	pthread_cond_init (&gate.cond, NULL);
	pthread_t holder, observer, controller;
	ck_assert_int_eq (pthread_create (&holder, NULL, hold_decoder_until_released, &gate), 0);
	pthread_mutex_lock (&gate.lock);
	while (!gate.ready) { pthread_cond_wait (&gate.cond, &gate.lock); }
	pthread_mutex_unlock (&gate.lock);
	AudioSnapshotArgs observation = {.player = &player};
	ck_assert_int_eq (pthread_create (&observer, NULL, audio_snapshot_thread, &observation), 0);
	wait_for_audio_owner (&player, observer);
	AudioControlArgs control = {.player = &player};
	ck_assert_int_eq (pthread_create (&controller, NULL, resume_audio_thread, &control), 0);
	wait_for_audio_waiters (&player, 1);
	BarPlayerAudioSnapshot snapshot;
	for (unsigned i = 0; i < 100; ++i) { ck_assert (!BarPlayerGetAudioSnapshot (&player, &snapshot)); }
	pthread_mutex_lock (&gate.lock);
	gate.release = true;
	pthread_cond_broadcast (&gate.cond);
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_join (holder, NULL), 0);
	ck_assert_int_eq (pthread_join (observer, NULL), 0);
	wait_device_start_gate (&startGate);
	ck_assert (observation.result);
	pthread_mutex_lock (&player.lock);
	ck_assert (player.audioBusy && pthread_equal (player.audioOwner, controller));
	pthread_mutex_unlock (&player.lock);
	for (unsigned i = 0; i < 100; ++i) { ck_assert (!BarPlayerGetAudioSnapshot (&player, &snapshot)); }
	release_device_start_gate (&startGate);
	ck_assert_int_eq (pthread_join (controller, NULL), 0);
	ck_assert (control.result);
	ck_assert (BarPlayerGetAudioSnapshot (&player, &snapshot));
	ck_assert_int_eq (player.audioControlWaiters, 0);
	ck_assert (BarPlayerStopAudio (&player));
	remove_device_start_gate (&player, &startGate);
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
	pthread_cond_destroy (&gate.cond);
	pthread_mutex_destroy (&gate.lock);
}
END_TEST

typedef struct { player_t *player; bool absolute, result; } ConcurrentVolume;
static void *concurrent_volume_update (void *data) {
	ConcurrentVolume *update = data;
	update->result = update->absolute ? BarPlayerSetVolume (update->player, 72) : BarPlayerAdjustVolume (update->player, -3);
	return NULL;
}

/* Break caught: a queued relative update uses 50 before the absolute 72 commit. */
START_TEST (test_player_concurrent_absolute_relative_volume_uses_reserved_value)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	ck_assert (BarPlayerSetVolume (&player, 50));
	DecoderGate gate = {.player = &player};
	pthread_mutex_init (&gate.lock, NULL);
	pthread_cond_init (&gate.cond, NULL);
	pthread_t holder, observer, absolute, relative;
	ck_assert_int_eq (pthread_create (&holder, NULL, hold_decoder_until_released, &gate), 0);
	pthread_mutex_lock (&gate.lock);
	while (!gate.ready) { pthread_cond_wait (&gate.cond, &gate.lock); }
	pthread_mutex_unlock (&gate.lock);
	AudioSnapshotArgs observation = {.player = &player};
	ck_assert_int_eq (pthread_create (&observer, NULL, audio_snapshot_thread, &observation), 0);
	wait_for_audio_owner (&player, observer);
	ConcurrentVolume abs = {.player = &player, .absolute = true}, rel = {.player = &player};
	ck_assert_int_eq (pthread_create (&absolute, NULL, concurrent_volume_update, &abs), 0);
	wait_for_audio_waiters (&player, 1);
	ck_assert_int_eq (pthread_create (&relative, NULL, concurrent_volume_update, &rel), 0);
	wait_for_audio_waiters (&player, 2);
	pthread_mutex_lock (&gate.lock);
	gate.release = true;
	pthread_cond_broadcast (&gate.cond);
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_join (holder, NULL), 0);
	ck_assert_int_eq (pthread_join (observer, NULL), 0);
	ck_assert_int_eq (pthread_join (absolute, NULL), 0);
	ck_assert_int_eq (pthread_join (relative, NULL), 0);
	ck_assert (abs.result && rel.result && observation.result);
	const int volume = BarPlayerGetVolume (&player);
	ck_assert_msg (volume == 69 || volume == 72, "Serialized order permits 69 or 72, never stale relative 47; got %d", volume);
	ck_assert_int_eq (settings.volume, volume);
	ck_assert_float_eq_tol (ma_sound_get_volume (&player.sound), volume / 100.0f, 0.0001f);
	ck_assert (!player.audioBusy && player.audioControlWaiters == 0);
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
	pthread_cond_destroy (&gate.cond);
	pthread_mutex_destroy (&gate.lock);
}
END_TEST

typedef struct { player_t *player; _Atomic unsigned cycles; } PauseStopStress;
static void *stress_pause_resume (void *data) {
	PauseStopStress *stress = data;
	for (unsigned i = 0; i < 100; ++i) {
		if (!BarPlayerSetPaused (stress->player, true)) { break; }
		if (!BarPlayerSetPaused (stress->player, false)) { break; }
		atomic_fetch_add (&stress->cycles, 1);
	}
	return NULL;
}
static void *stress_stop (void *data) {
	PauseStopStress *stress = data;
	while (atomic_load (&stress->cycles) < 3) { sched_yield (); }
	BarPlayerRequestStop (stress->player);
	return NULL;
}

/* Break caught: pause/resume touches an uninitialized node after concurrent stop. */
START_TEST (test_player_pause_resume_stop_stress_finishes_without_owner)
{
	for (unsigned round = 0; round < 10; ++round) {
		player_t player;
		BarSettings_t settings;
		player_thread_test_setup (&player, &settings);
		char url[PATH_MAX + 16];
		ck_assert (player_mp3_fixture_path (url, sizeof url));
		player.url = strdup (url);
		BarPlayerSetMode (&player, PLAYER_WAITING);
		ck_assert (BarPlayerSetPaused (&player, true));
		player.threadJoinPending = true;
		pthread_t worker;
		ck_assert_int_eq (pthread_create (&worker, NULL, BarPlayerThread, &player), 0);
		ck_assert (BarPlayerWaitForMode (&player, PLAYER_PLAYING, 10000));
		PauseStopStress stress = {.player = &player};
		pthread_t controls, stopper;
		ck_assert_int_eq (pthread_create (&controls, NULL, stress_pause_resume, &stress), 0);
		ck_assert_int_eq (pthread_create (&stopper, NULL, stress_stop, &stress), 0);
		ck_assert_int_eq (pthread_join (controls, NULL), 0);
		ck_assert_int_eq (pthread_join (stopper, NULL), 0);
		/* The real player cleans/uninitializes its sound concurrently with
		 * controls rejecting quit; all three workers must join before destroy. */
		ck_assert (BarPlayerJoinThreadWithTimeout (&player, worker, NULL, 1));
		ck_assert (!player.audioBusy && !player.audioOwnerValid);
		ck_assert_int_eq (player.audioControlWaiters, 0);
		ck_assert (player.doQuit && !player.doPause);
		ck_assert_int_eq (player.audioState, PLAYER_AUDIO_NONE);
		ck_assert (!player.soundInitialized && !player.threadJoinPending);
		ck_assert (BarPlayerDestroy (&player));
		free (player.url);
		BarSettingsDestroy (&settings);
	}
}
END_TEST

/* Break caught: failed device start leaves the sound started or loses pause,
 * cancellation, ownership, queued controls, or the ability to retry. */
START_TEST (test_player_resume_failure_releases_waiters_and_allows_retry)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	pause_fixture (&player);
	DeviceStartGate gate = {.failStart = true};
	install_device_start_gate (&player, &gate);
	AudioControlArgs resume = {.player = &player}, volume = {.player = &player};
	pthread_t resumer, updater;
	ck_assert_int_eq (pthread_create (&resumer, NULL, resume_audio_thread, &resume), 0);
	wait_device_start_gate (&gate);
	ck_assert_int_eq (pthread_create (&updater, NULL, set_waiting_volume, &volume), 0);
	wait_for_audio_waiters (&player, 1);
	release_device_start_gate (&gate);
	ck_assert_int_eq (pthread_join (resumer, NULL), 0);
	ck_assert_int_eq (pthread_join (updater, NULL), 0);
	ck_assert (!resume.result);
	ck_assert (volume.result);
	ck_assert (gate.locksFree && gate.sawPaused && gate.sawRearmed);
	ck_assert (player.doPause);
	ck_assert_int_eq (player.pauseStartTime, 123);
	ck_assert (player.sourceReadCancelled);
	ck_assert (!player.audioBusy);
	ck_assert_int_eq (player.audioControlWaiters, 0);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_STOPPED);
	ck_assert (!ma_sound_is_playing (&player.sound));
	ck_assert (!ma_device_is_started (&gate.device));
	pthread_mutex_lock (&gate.lock);
	gate.failStart = false;
	pthread_mutex_unlock (&gate.lock);
	ck_assert (BarPlayerSetPaused (&player, false));
	ck_assert (!player.doPause);
	ck_assert (!player.sourceReadCancelled);
	ck_assert (ma_sound_is_playing (&player.sound));
	ck_assert (ma_device_is_started (&gate.device));
	ck_assert (BarPlayerSetPaused (&player, true));
	ck_assert (!ma_device_is_started (&gate.device));
	remove_device_start_gate (&player, &gate);
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
}
END_TEST

/* A failed restart of physically stopped but logically unpaused audio must
 * publish pause, not leave the logical state claiming playback succeeded. */
START_TEST (test_player_failed_restart_restores_pause_and_allows_retry)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	DeviceStartGate gate = {.release = true, .failStart = true};
	install_device_start_gate (&player, &gate);
	const uint64_t epoch = player.controlEpoch;
	ck_assert (!BarPlayerSetPaused (&player, false));
	ck_assert (player.doPause && player.pauseStartTime > 0);
	ck_assert_uint_eq (player.controlEpoch, epoch + 1);
	ck_assert (player.sourceReadCancelled);
	ck_assert (!player.audioBusy && !player.audioOwnerValid);
	ck_assert (!ma_sound_is_playing (&player.sound));
	ck_assert (!ma_device_is_started (&gate.device));
	bool paused = false;
	ck_assert (!BarPlayerTogglePaused (&player, &paused));
	ck_assert (paused);
	gate.failStart = false;
	ck_assert (BarPlayerSetPaused (&player, false));
	ck_assert (BarPlayerSetVolume (&player, 81));
	ck_assert (fabsf (ma_sound_get_volume (&player.sound) - 0.81f) < 0.001f);
	ck_assert (BarPlayerSetPaused (&player, true));
	remove_device_start_gate (&player, &gate);
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
}
END_TEST

START_TEST (test_player_resume_at_sound_end_does_not_restart)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	ck_assert_int_eq (ma_sound_start (&player.sound), MA_SUCCESS);
	float frames[2048];
	ck_assert_int_eq (ma_engine_read_pcm_frames (&player.engine, frames, 1024, NULL), MA_SUCCESS);
	ck_assert (ma_sound_at_end (&player.sound));
	ck_assert (!BarPlayerSetPaused (&player, false));
	ck_assert (player.doPause && player.sourceReadCancelled);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_STOPPED);
	ck_assert (!ma_sound_is_playing (&player.sound));
	ck_assert (!player.audioBusy);
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
}
END_TEST

/* Break caught: resume clears a published quit while its device start was
 * outstanding; a normal waiter steals the next reservation from teardown. */
START_TEST (test_player_stop_supersedes_resume_and_aborts_normal_waiters)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	pause_fixture (&player);
	DeviceStartGate gate = {0};
	install_device_start_gate (&player, &gate);
	AudioControlArgs resume = {.player = &player}, volume = {.player = &player};
	pthread_t resumer, updater, stopper;
	ck_assert_int_eq (pthread_create (&resumer, NULL, resume_audio_thread, &resume), 0);
	wait_device_start_gate (&gate);
	ck_assert_int_eq (pthread_create (&updater, NULL, set_waiting_volume, &volume), 0);
	wait_for_audio_waiters (&player, 1);
	ck_assert_int_eq (pthread_create (&stopper, NULL, request_stop_thread, &player), 0);
	ck_assert_int_eq (pthread_join (updater, NULL), 0);
	ck_assert (!volume.result);
	wait_for_audio_waiters (&player, 1);
	pthread_mutex_lock (&player.lock);
	ck_assert (player.doQuit);
	ck_assert (player.doPause);
	ck_assert_int_eq (player.pauseStartTime, 123);
	ck_assert (player.audioBusy && pthread_equal (player.audioOwner, resumer));
	pthread_mutex_unlock (&player.lock);
	release_device_start_gate (&gate);
	ck_assert_int_eq (pthread_join (resumer, NULL), 0);
	ck_assert_int_eq (pthread_join (stopper, NULL), 0);
	ck_assert (!resume.result);
	ck_assert (player.doQuit);
	ck_assert (!player.doPause);
	ck_assert_int_eq (player.pauseStartTime, 0);
	ck_assert (!player.audioBusy);
	ck_assert_int_eq (player.audioControlWaiters, 0);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_STOPPED);
	ck_assert (!ma_sound_is_playing (&player.sound));
	ck_assert (!ma_device_is_started (&gate.device));
	ck_assert (player.sourceReadCancelled);
	remove_device_start_gate (&player, &gate);
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
}
END_TEST

/* Break caught: two concurrent toggles both choose resume before reservation
 * ownership, producing one transition instead of two. */
START_TEST (test_player_concurrent_toggles_choose_direction_after_reservation)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	pause_fixture (&player);
	DeviceStartGate gate = {0};
	install_device_start_gate (&player, &gate);
	AudioControlArgs first = {.player = &player}, second = {.player = &player};
	pthread_t one, two;
	ck_assert_int_eq (pthread_create (&one, NULL, toggle_audio_thread, &first), 0);
	wait_device_start_gate (&gate);
	ck_assert_int_eq (pthread_create (&two, NULL, toggle_audio_thread, &second), 0);
	wait_for_audio_waiters (&player, 1);
	release_device_start_gate (&gate);
	ck_assert_int_eq (pthread_join (one, NULL), 0);
	ck_assert_int_eq (pthread_join (two, NULL), 0);
	ck_assert (first.result && second.result);
	ck_assert (player.doPause);
	ck_assert (!player.audioBusy);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_STOPPED);
	ck_assert (!ma_sound_is_playing (&player.sound));
	ck_assert (!ma_device_is_started (&gate.device));
	remove_device_start_gate (&player, &gate);
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
}
END_TEST

/* Break caught: an ended mode does not wake a queued resume, or the queued
 * command restarts the node after the current lifetime owner completes. */
START_TEST (test_player_ended_mode_aborts_resume_wait_before_owner_releases)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	pause_fixture (&player);
	DecoderGate gate = {.player = &player};
	pthread_mutex_init (&gate.lock, NULL);
	pthread_cond_init (&gate.cond, NULL);
	pthread_t holder, reader, resumer;
	ck_assert_int_eq (pthread_create (&holder, NULL, hold_decoder_until_released, &gate), 0);
	pthread_mutex_lock (&gate.lock);
	while (!gate.ready) { pthread_cond_wait (&gate.cond, &gate.lock); }
	pthread_mutex_unlock (&gate.lock);
	AudioSnapshotArgs snapshot = {.player = &player};
	ck_assert_int_eq (pthread_create (&reader, NULL, audio_snapshot_thread, &snapshot), 0);
	wait_for_audio_owner (&player, reader);
	AudioControlArgs resume = {.player = &player};
	ck_assert_int_eq (pthread_create (&resumer, NULL, resume_audio_thread, &resume), 0);
	wait_for_audio_waiters (&player, 1);
	BarPlayerSetMode (&player, PLAYER_FINISHED);
	ck_assert_int_eq (pthread_join (resumer, NULL), 0);
	ck_assert (!resume.result);
	pthread_mutex_lock (&player.lock);
	ck_assert (player.audioBusy && pthread_equal (player.audioOwner, reader));
	ck_assert_int_eq (player.audioControlWaiters, 0);
	pthread_mutex_unlock (&player.lock);
	pthread_mutex_lock (&gate.lock);
	gate.release = true;
	pthread_cond_broadcast (&gate.cond);
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_join (holder, NULL), 0);
	ck_assert_int_eq (pthread_join (reader, NULL), 0);
	ck_assert (!snapshot.result);
	ck_assert (player.doPause);
	ck_assert (!ma_sound_is_playing (&player.sound));
	ck_assert (!player.audioBusy);
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
	pthread_cond_destroy (&gate.cond);
	pthread_mutex_destroy (&gate.lock);
}
END_TEST

/* Break caught: resume starts an object that cleanup already uninitialized,
 * instead of rechecking mode and lifetime after its reservation wait. */
START_TEST (test_player_resume_waits_for_cleanup_and_rejects_dead_mode)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	SoundAllocationGate gate = {.original = player.engine.allocationCallbacks,
		.release = true, .blockFree = true, .player = &player, .locksFree = true};
	pthread_mutex_init (&gate.lock, NULL);
	pthread_cond_init (&gate.cond, NULL);
	player.engine.allocationCallbacks = (ma_allocation_callbacks) {
		.pUserData = &gate, .onMalloc = gated_sound_malloc,
		.onRealloc = gated_sound_realloc, .onFree = gated_sound_free,
	};
	player_stopped_sound_fixture (&player, &buffer);
	pause_fixture (&player);
	AudioControlArgs cleanup = {.player = &player}, resume = {.player = &player};
	pthread_t cleaner, resumer;
	ck_assert_int_eq (pthread_create (&cleaner, NULL, reset_audio_thread, &cleanup), 0);
	pthread_mutex_lock (&gate.lock);
	while (!gate.freeEntered) { pthread_cond_wait (&gate.cond, &gate.lock); }
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_create (&resumer, NULL, resume_audio_thread, &resume), 0);
	wait_for_audio_waiters (&player, 1);
	pthread_mutex_lock (&gate.lock);
	gate.releaseFree = true;
	pthread_cond_broadcast (&gate.cond);
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_join (cleaner, NULL), 0);
	ck_assert_int_eq (pthread_join (resumer, NULL), 0);
	ck_assert (cleanup.result);
	ck_assert (!resume.result);
	ck_assert_int_eq (player.mode, PLAYER_DEAD);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_NONE);
	ck_assert (!player.soundInitialized);
	ck_assert (!player.audioBusy);
	ck_assert_int_eq (player.audioControlWaiters, 0);
	player.engine.allocationCallbacks = gate.original;
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
	pthread_cond_destroy (&gate.cond);
	pthread_mutex_destroy (&gate.lock);
}
END_TEST

/* Break caught: quit published while resume waits on decoder rearm is ignored
 * before the first backend start, or rollback leaves cancellation disarmed. */
START_TEST (test_player_stop_supersedes_resume_before_backend_start)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	pause_fixture (&player);
	DecoderGate decoder = {.player = &player};
	pthread_mutex_init (&decoder.lock, NULL);
	pthread_cond_init (&decoder.cond, NULL);
	pthread_t holder, resumer, stopper;
	ck_assert_int_eq (pthread_create (&holder, NULL, hold_decoder_until_released, &decoder), 0);
	pthread_mutex_lock (&decoder.lock);
	while (!decoder.ready) { pthread_cond_wait (&decoder.cond, &decoder.lock); }
	pthread_mutex_unlock (&decoder.lock);
	AudioControlArgs resume = {.player = &player};
	ck_assert_int_eq (pthread_create (&resumer, NULL, resume_audio_thread, &resume), 0);
	wait_for_audio_owner (&player, resumer);
	/* player.lock is available while the resume owner waits on decoderLock. */
	ck_assert_int_eq (pthread_create (&stopper, NULL, request_stop_thread, &player), 0);
	wait_for_audio_waiters (&player, 1);
	pthread_mutex_lock (&decoder.lock);
	decoder.release = true;
	pthread_cond_broadcast (&decoder.cond);
	pthread_mutex_unlock (&decoder.lock);
	ck_assert_int_eq (pthread_join (holder, NULL), 0);
	ck_assert_int_eq (pthread_join (resumer, NULL), 0);
	ck_assert_int_eq (pthread_join (stopper, NULL), 0);
	ck_assert (!resume.result);
	ck_assert (player.doQuit);
	ck_assert (!player.doPause);
	ck_assert (player.sourceReadCancelled);
	ck_assert (!player.audioBusy);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_STOPPED);
	ck_assert (!ma_sound_is_playing (&player.sound));
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
	pthread_cond_destroy (&decoder.cond);
	pthread_mutex_destroy (&decoder.lock);
}
END_TEST

/* Break caught: an end callback arriving during device start is overwritten
 * by resume, clearing its newer mode/epoch or the existing pause decision. */
START_TEST (test_player_end_callback_supersedes_outstanding_resume)
{
	player_t player;
	BarSettings_t settings;
	char url[PATH_MAX + 16];
	ck_assert (player_mp3_fixture_path (url, sizeof url));
	player_thread_test_setup (&player, &settings);
	player.url = strdup (url);
	BarPlayerSetMode (&player, PLAYER_WAITING);
	ck_assert (BarPlayerSetPaused (&player, true));
	pthread_t worker, resumer;
	ck_assert_int_eq (pthread_create (&worker, NULL, BarPlayerThread, &player), 0);
	ck_assert (BarPlayerWaitForMode (&player, PLAYER_PLAYING, 10000));
	pause_fixture (&player);
	DeviceStartGate gate = {0};
	install_device_start_gate (&player, &gate);
	BarPlayerPlayStateSnapshot completed = {false, 987};
	AudioControlArgs resume = {.player = &player, .snapshot = &completed};
	ck_assert_int_eq (pthread_create (&resumer, NULL, resume_audio_thread, &resume), 0);
	wait_device_start_gate (&gate);
	/* Invoke the actual callback installed by production setup at its backend
 * boundary; no substitute callback or test mutation of mode/epoch is used. */
	ck_assert_ptr_nonnull (player.sound.endCallback);
	player.sound.endCallback (player.sound.pEndCallbackUserData, &player.sound);
	ck_assert (BarPlayerGetMode (&player) != PLAYER_PLAYING);
	release_device_start_gate (&gate);
	ck_assert_int_eq (pthread_join (resumer, NULL), 0);
	ck_assert_int_eq (pthread_join (worker, NULL), 0);
	ck_assert (!resume.result);
	ck_assert (!completed.paused);
	ck_assert_uint_eq (completed.controlEpoch, 987);
	ck_assert_int_eq (player.mode, PLAYER_FINISHED);
	ck_assert (player.doPause);
	ck_assert_int_eq (player.pauseStartTime, 123);
	ck_assert (!player.doQuit);
	ck_assert (!player.audioBusy);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_NONE);
	ck_assert (!player.soundInitialized);
	ck_assert (!ma_device_is_started (&gate.device));
	remove_device_start_gate (&player, &gate);
	free (player.url);
	player_thread_test_teardown (&player, &settings);
}
END_TEST

/* Break caught: setup commits PLAYING after immediate stop published quit,
 * or teardown cannot take the reservation after setup rolls back. */
START_TEST (test_player_request_stop_supersedes_setup_and_releases_waiters)
{
	player_t player;
	BarSettings_t settings;
	char url[PATH_MAX + 16];
	ck_assert (player_mp3_fixture_path (url, sizeof url));
	player_thread_test_setup (&player, &settings);
	SoundAllocationGate gate = {.original = player.engine.allocationCallbacks,
		.player = &player, .locksFree = true};
	pthread_mutex_init (&gate.lock, NULL);
	pthread_cond_init (&gate.cond, NULL);
	player.engine.allocationCallbacks = (ma_allocation_callbacks) {
		.pUserData = &gate, .onMalloc = gated_sound_malloc,
		.onRealloc = gated_sound_realloc, .onFree = gated_sound_free,
	};
	player.url = strdup (url);
	BarPlayerSetMode (&player, PLAYER_WAITING);
	ck_assert (BarPlayerSetPaused (&player, true));
	pthread_t worker, resumer, stopper;
	ck_assert_int_eq (pthread_create (&worker, NULL, BarPlayerThread, &player), 0);
	pthread_mutex_lock (&gate.lock);
	while (!gate.entered) { pthread_cond_wait (&gate.cond, &gate.lock); }
	pthread_mutex_unlock (&gate.lock);
	AudioControlArgs resume = {.player = &player};
	ck_assert_int_eq (pthread_create (&resumer, NULL, resume_audio_thread, &resume), 0);
	wait_for_audio_waiters (&player, 1);
	ck_assert_int_eq (pthread_create (&stopper, NULL, request_stop_thread, &player), 0);
	ck_assert_int_eq (pthread_join (resumer, NULL), 0);
	ck_assert (!resume.result);
	wait_for_audio_waiters (&player, 1);
	pthread_mutex_lock (&player.lock);
	ck_assert (player.doQuit && player.doPause);
	const uint64_t stoppedEpoch = player.controlEpoch;
	ck_assert_int_eq (player.mode, PLAYER_WAITING);
	pthread_mutex_unlock (&player.lock);
	pthread_mutex_lock (&gate.lock);
	gate.release = true;
	pthread_cond_broadcast (&gate.cond);
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_join (worker, NULL), 0);
	ck_assert_int_eq (pthread_join (stopper, NULL), 0);
	ck_assert (player.doQuit);
	ck_assert (!player.doPause);
	ck_assert_int_eq (player.pauseStartTime, 0);
	ck_assert_int_eq (player.mode, PLAYER_FINISHED);
	ck_assert_uint_eq (player.controlEpoch, stoppedEpoch + 1);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_NONE);
	ck_assert (!player.soundInitialized);
	ck_assert (!player.dataSourceInitialized);
	ck_assert (!player.audioBusy);
	ck_assert_int_eq (player.audioControlWaiters, 0);
	player.engine.allocationCallbacks = gate.original;
	free (player.url);
	player_thread_test_teardown (&player, &settings);
	pthread_cond_destroy (&gate.cond);
	pthread_mutex_destroy (&gate.lock);
}
END_TEST

typedef struct {
	player_t *player;
	pthread_cond_t readyCond;
	bool ready, sawCancelled;
} StopDecoderWaiter;

static void *wait_for_stop_decoder_wake (void *data) {
	StopDecoderWaiter *waiter = data;
	pthread_mutex_lock (&waiter->player->decoderLock);
	waiter->ready = true;
	pthread_cond_broadcast (&waiter->readyCond);
	while (!waiter->player->sourceReadCancelled) {
		pthread_cond_wait (&waiter->player->decoderCond, &waiter->player->decoderLock);
	}
	waiter->sawCancelled = true;
	pthread_mutex_unlock (&waiter->player->decoderLock);
	return NULL;
}

/* Break caught: immediate stop leaves an existing decoder-condition waiter
 * asleep or omits the persistent cancellation predicate. */
START_TEST (test_player_request_stop_wakes_decoder_waiter)
{
	player_t player;
	BarSettings_t settings;
	player_thread_test_setup (&player, &settings);
	BarPlayerSetMode (&player, PLAYER_WAITING);
	ck_assert (BarPlayerSetPaused (&player, true));
	StopDecoderWaiter waiter = {.player = &player};
	pthread_cond_init (&waiter.readyCond, NULL);
	pthread_t thread;
	ck_assert_int_eq (pthread_create (&thread, NULL, wait_for_stop_decoder_wake, &waiter), 0);
	pthread_mutex_lock (&player.decoderLock);
	while (!waiter.ready) { pthread_cond_wait (&waiter.readyCond, &player.decoderLock); }
	pthread_mutex_unlock (&player.decoderLock);
	BarPlayerRequestStop (&player);
	ck_assert_int_eq (pthread_join (thread, NULL), 0);
	ck_assert (waiter.sawCancelled);
	ck_assert (player.doQuit);
	ck_assert (!player.doPause);
	ck_assert_int_eq (player.pauseStartTime, 0);
	ck_assert (!player.audioBusy);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_NONE);
	pthread_cond_destroy (&waiter.readyCond);
	player_thread_test_teardown (&player, &settings);
}
END_TEST

/* Break caught: second FFmpeg interrupt only sets quit, leaving a retained
 * paused node's timer and decoder waiter alive instead of completing stop. */
START_TEST (test_player_second_interrupt_completes_retained_paused_stop)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	DeviceStartGate gate = {.release = true};
	install_device_start_gate (&player, &gate);
	ck_assert (BarPlayerSetPaused (&player, false));
	ck_assert (ma_sound_is_playing (&player.sound));
	ck_assert (ma_device_is_started (&gate.device));
	pause_fixture (&player);
	ck_assert (!ma_sound_is_playing (&player.sound));
	ck_assert (!ma_device_is_started (&gate.device));
	/* An empty-source teardown waiter must receive the interrupt's own wake,
 * not reuse the earlier pause's cancellation as evidence. */
	pthread_mutex_lock (&player.decoderLock);
	player.sourceReadCancelled = false;
	pthread_mutex_unlock (&player.decoderLock);
	StopDecoderWaiter decoder = {.player = &player};
	pthread_cond_init (&decoder.readyCond, NULL);
	pthread_t reader;
	ck_assert_int_eq (pthread_create (&reader, NULL, wait_for_stop_decoder_wake, &decoder), 0);
	pthread_mutex_lock (&player.decoderLock);
	while (!decoder.ready) { pthread_cond_wait (&decoder.readyCond, &player.decoderLock); }
	pthread_mutex_unlock (&player.decoderLock);
	PauseConditionWaiter control = {.player = &player, .expected = false};
	pthread_cond_init (&control.readyCond, NULL);
	pthread_t waiter;
	ck_assert_int_eq (pthread_create (&waiter, NULL, wait_for_pause_condition, &control), 0);
	pthread_mutex_lock (&player.lock);
	while (!control.ready) { pthread_cond_wait (&control.readyCond, &player.lock); }
	const uint64_t previousEpoch = player.controlEpoch;
	pthread_mutex_unlock (&player.lock);
	atomic_store (&player.interrupted, 2);
	ck_assert_int_eq (BarPlayerFfmpegInterruptCb (&player), 1);
	ck_assert_msg (!player.doPause, "Second interrupt must clear retained logical pause");
	ck_assert_int_eq (player.pauseStartTime, 0);
	ck_assert (player.doQuit);
	ck_assert (player.controlEpoch > previousEpoch);
	ck_assert_int_eq (player.mode, PLAYER_PLAYING);
	ck_assert (!player.audioBusy && !player.audioOwnerValid);
	ck_assert_int_eq (player.audioControlWaiters, 0);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_STOPPED);
	ck_assert (!ma_sound_is_playing (&player.sound));
	ck_assert (!ma_device_is_started (&gate.device));
	ck_assert_int_eq (pthread_join (reader, NULL), 0);
	ck_assert_int_eq (pthread_join (waiter, NULL), 0);
	ck_assert (decoder.sawCancelled && control.observed);
	ck_assert_int_eq (control.timestamp, 0);
	/* Repeated forced shutdown cannot restore mode or acquire a stale owner. */
	BarPlayerSetMode (&player, PLAYER_FINISHED);
	ck_assert_int_eq (BarPlayerFfmpegInterruptCb (&player), 1);
	ck_assert_int_eq (player.mode, PLAYER_FINISHED);
	ck_assert (!player.audioBusy);
	pthread_cond_destroy (&control.readyCond);
	pthread_cond_destroy (&decoder.readyCond);
	remove_device_start_gate (&player, &gate);
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
}
END_TEST

static void assert_control_missing_device_is_terminal (unsigned operation) {
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	player_stopped_sound_fixture (&player, &buffer);
	if (operation == 0) { ck_assert (BarPlayerSetPaused (&player, false)); }
	else { pause_fixture (&player); }
	player.audioNoDevice = false;
	BarPlayerSetAudioFatalTestHook (returning_audio_fatal_hook);
	fatal_hook_calls = 0;
	fatal_hook_saw_unlocked_player = fatal_hook_saw_unlocked_decoder = false;
	if (operation == 2) { BarPlayerRequestStop (&player); }
	else { ck_assert (!BarPlayerSetPaused (&player, operation == 0)); }
	ck_assert_int_eq (fatal_hook_calls, 1);
	ck_assert (fatal_hook_saw_unlocked_player && fatal_hook_saw_unlocked_decoder);
	ck_assert (player.doQuit);
	ck_assert (player.sourceReadCancelled);
	ck_assert (player.audioTerminalFailure);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_FAILED);
	ck_assert (!player.audioBusy);
	ck_assert (!player.audioOwnerValid);
	ck_assert_int_eq (player.audioControlWaiters, 0);
	ck_assert (player.soundInitialized);
	ck_assert (!ma_sound_is_playing (&player.sound));
	BarPlayerSetAudioFatalTestHook (NULL);
	player.audioNoDevice = true;
	/* Recovery for fixture cleanup only after every owner has returned. */
	player.audioTerminalFailure = false;
	player.doQuit = false;
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
}

START_TEST (test_player_pause_failure_requests_terminal_shutdown) {
	assert_control_missing_device_is_terminal (0);
}
END_TEST
START_TEST (test_player_resume_rollback_failure_requests_terminal_shutdown) {
	assert_control_missing_device_is_terminal (1);
}
END_TEST
START_TEST (test_player_request_stop_failure_releases_reservation) {
	assert_control_missing_device_is_terminal (2);
}
END_TEST

/* Break caught: Reset overwrites a newer stop/pause/mode decision while
 * physical cleanup has released player.lock. The free gate is below the
 * actual sound uninitialization, not a replacement for player behavior. */
START_TEST (test_player_reset_preserves_controls_changed_during_cleanup)
{
	player_t player;
	BarSettings_t settings;
	ma_audio_buffer buffer;
	player_thread_test_setup (&player, &settings);
	SoundAllocationGate gate = {.original = player.engine.allocationCallbacks,
		.release = true, .blockFree = true, .player = &player, .locksFree = true};
	pthread_mutex_init (&gate.lock, NULL);
	pthread_cond_init (&gate.cond, NULL);
	player.engine.allocationCallbacks = (ma_allocation_callbacks) {
		.pUserData = &gate, .onMalloc = gated_sound_malloc,
		.onRealloc = gated_sound_realloc, .onFree = gated_sound_free,
	};
	player_stopped_sound_fixture (&player, &buffer);
	AudioControlArgs args = {.player = &player};
	pthread_t resetter;
	ck_assert_int_eq (pthread_create (&resetter, NULL, reset_audio_thread, &args), 0);
	pthread_mutex_lock (&gate.lock);
	while (!gate.freeEntered) { pthread_cond_wait (&gate.cond, &gate.lock); }
	pthread_mutex_unlock (&gate.lock);
	pthread_mutex_lock (&player.lock);
	ck_assert (player.audioBusy);
	player.doQuit = true;
	player.doPause = true;
	player.pauseStartTime = 123;
	player.mode = PLAYER_FINISHED;
	const uint64_t newerEpoch = ++player.controlEpoch;
	pthread_mutex_unlock (&player.lock);
	pthread_mutex_lock (&gate.lock);
	gate.releaseFree = true;
	pthread_cond_broadcast (&gate.cond);
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_join (resetter, NULL), 0);
	const bool quit = player.doQuit, paused = player.doPause;
	const time_t pauseTime = player.pauseStartTime;
	const BarPlayerMode mode = BarPlayerGetMode (&player);
	const uint64_t finalEpoch = player.controlEpoch;
	const bool cleaned = player.audioState == PLAYER_AUDIO_NONE && !player.audioBusy && !player.soundInitialized;
	player.engine.allocationCallbacks = gate.original;
	player_thread_test_teardown (&player, &settings);
	ma_audio_buffer_uninit (&buffer);
	pthread_cond_destroy (&gate.cond);
	pthread_mutex_destroy (&gate.lock);
	ck_assert (cleaned);
	ck_assert (gate.checkedLocks && gate.locksFree);
	ck_assert_msg (!args.result, "A superseded Reset must report failure to its lifecycle caller");
	ck_assert (quit);
	ck_assert (paused);
	ck_assert_int_eq (pauseTime, 123);
	ck_assert_int_eq (mode, PLAYER_FINISHED);
	ck_assert_uint_eq (finalEpoch, newerEpoch);
}
END_TEST

/* Break caught: a superseded setup republishes WAITING/PLAYING over an end
 * transition. The allocator gate is below the player; sound setup is real. */
static void assert_superseded_setup_preserves_mode (BarPlayerMode newerMode, bool failAllocation) {
	player_t player;
	BarSettings_t settings;
	char url[PATH_MAX + 16];
	ck_assert (player_mp3_fixture_path (url, sizeof url));
	player_thread_test_setup (&player, &settings);
	SoundAllocationGate gate = {.original = player.engine.allocationCallbacks,
		.failAllocation = failAllocation, .player = &player, .locksFree = true};
	pthread_mutex_init (&gate.lock, NULL);
	pthread_cond_init (&gate.cond, NULL);
	player.engine.allocationCallbacks = (ma_allocation_callbacks) {
		.pUserData = &gate, .onMalloc = gated_sound_malloc,
		.onRealloc = gated_sound_realloc, .onFree = gated_sound_free,
	};
	player.url = strdup (url);
	BarPlayerSetMode (&player, PLAYER_WAITING);
	pthread_t thread;
	ck_assert_int_eq (pthread_create (&thread, NULL, BarPlayerThread, &player), 0);
	pthread_mutex_lock (&gate.lock);
	while (!gate.entered) { pthread_cond_wait (&gate.cond, &gate.lock); }
	pthread_mutex_unlock (&gate.lock);
	BarPlayerSetMode (&player, newerMode);
	pthread_mutex_lock (&player.lock);
	const uint64_t newerEpoch = player.controlEpoch;
	ck_assert (player.audioBusy);
	pthread_mutex_unlock (&player.lock);
	pthread_mutex_lock (&gate.lock);
	gate.release = true;
	pthread_cond_broadcast (&gate.cond);
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_join (thread, NULL), 0);
	const uint64_t finalEpoch = player.controlEpoch;
	ck_assert_int_eq (BarPlayerGetMode (&player), newerMode);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_NONE);
	ck_assert (!player.soundInitialized);
	ck_assert (!player.audioBusy);
	player.engine.allocationCallbacks = gate.original;
	free (player.url);
	player_thread_test_teardown (&player, &settings);
	pthread_cond_destroy (&gate.cond);
	pthread_mutex_destroy (&gate.lock);
	ck_assert_msg (finalEpoch == newerEpoch,
		"Superseded setup must not publish later mode transitions");
	ck_assert (gate.checkedLocks && gate.locksFree);
}

START_TEST (test_player_superseded_setup_preserves_finished_mode)
{
	assert_superseded_setup_preserves_mode (PLAYER_FINISHED, false);
}
END_TEST

/* Break caught: the final thread transition overwrites a newer DEAD mode
 * even though setup rollback correctly kept its own logical commit empty. */
START_TEST (test_player_superseded_setup_preserves_dead_mode)
{
	assert_superseded_setup_preserves_mode (PLAYER_DEAD, false);
}
END_TEST

/* Break caught: a genuine setup allocation error bypasses supersession and
 * the thread republishes WAITING/FINISHED over newer control decisions. */
START_TEST (test_player_failed_setup_preserves_newer_dead_mode)
{
	assert_superseded_setup_preserves_mode (PLAYER_DEAD, true);
}
END_TEST

START_TEST (test_player_failed_setup_preserves_newer_finished_epoch)
{
	assert_superseded_setup_preserves_mode (PLAYER_FINISHED, true);
}
END_TEST

/* Break caught: setup starts a node for a song that was paused before setup. */
START_TEST (test_player_paused_setup_retains_stopped_sound)
{
	player_t player;
	BarSettings_t settings;
	char url[PATH_MAX + 16];
	ck_assert (player_mp3_fixture_path (url, sizeof url));
	player_thread_test_setup (&player, &settings);
	pthread_mutex_lock (&player.lock);
	player.doPause = true;
	player.pauseStartTime = time (NULL);
	++player.controlEpoch;
	pthread_mutex_unlock (&player.lock);
	player.url = strdup (url);
	BarPlayerSetMode (&player, PLAYER_WAITING);
	pthread_t thread;
	ck_assert_int_eq (pthread_create (&thread, NULL, BarPlayerThread, &player), 0);
	ck_assert (BarPlayerWaitForMode (&player, PLAYER_PLAYING, 10000));
	BarPlayerAudioSnapshot snapshot;
	bool observed = false;
	for (unsigned elapsed = 0; elapsed < 1000; elapsed += 5) {
		if (BarPlayerGetAudioSnapshot (&player, &snapshot)) { observed = true; break; }
		usleep (5000);
	}
	atomic_store (&player.interrupted, 2);
	ck_assert_int_eq (BarPlayerFfmpegInterruptCb (&player), 1);
	ck_assert_int_eq (pthread_join (thread, NULL), 0);
	ck_assert (!player.audioBusy);
	ck_assert (!player.soundInitialized);
	ck_assert (!player.dataSourceInitialized);
	free (player.url);
	player_thread_test_teardown (&player, &settings);
	ck_assert (observed);
	ck_assert_int_eq (snapshot.state, PLAYER_AUDIO_STOPPED);
	ck_assert (!snapshot.playing);
	ck_assert (!snapshot.deviceStarted);
	ck_assert_uint_eq (snapshot.cursorFrames, 0);
}
END_TEST

/* A backend capability mismatch during fresh start must retain the sound and
 * FFmpeg resources when rollback cannot prove device shutdown. */
START_TEST (test_player_setup_unreconciled_device_retains_resources)
{
	player_t player;
	BarSettings_t settings;
	char url[PATH_MAX + 16];
	ck_assert (player_mp3_fixture_path (url, sizeof url));
	player_thread_test_setup (&player, &settings);
	player.audioNoDevice = false;
	BarPlayerSetAudioFatalTestHook (returning_audio_fatal_hook);
	fatal_hook_calls = 0;
	fatal_hook_saw_unlocked_player = fatal_hook_saw_unlocked_decoder = false;
	player.url = strdup (url);
	ck_assert_ptr_nonnull (player.url);
	BarPlayerSetMode (&player, PLAYER_WAITING);
	pthread_t worker;
	ck_assert_int_eq (pthread_create (&worker, NULL, BarPlayerThread, &player), 0);
	void *result;
	ck_assert_int_eq (pthread_join (worker, &result), 0);
	ck_assert_uint_eq ((uintptr_t) result, PLAYER_RET_HARDFAIL);
	ck_assert_int_eq (fatal_hook_calls, 1);
	ck_assert (fatal_hook_saw_unlocked_player && fatal_hook_saw_unlocked_decoder);
	ck_assert (player.audioTerminalFailure && player.doQuit);
	ck_assert (!BarPlayerStopAudio (&player));
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_FAILED);
	ck_assert (!player.audioBusy && !player.audioOwnerValid);
	ck_assert (player.soundInitialized && player.dataSourceInitialized);
	ck_assert_ptr_nonnull (player.fgraph);
	ck_assert_ptr_nonnull (player.cctx);
	ck_assert_ptr_nonnull (player.fctx);
	/* Reinitialization must not erase terminal failure or reuse retained audio. */
	BarPlayerInit (&player, &settings);
	ck_assert (player.audioTerminalFailure && player.soundInitialized);
	/* Test-only recovery after the worker has joined; no callback/device exists. */
	BarPlayerSetAudioFatalTestHook (NULL);
	player.audioNoDevice = true;
	player.audioTerminalFailure = false;
	player.doQuit = false;
	ck_assert (BarPlayerDestroy (&player));
	avfilter_graph_free (&player.fgraph);
	avcodec_free_context (&player.cctx);
	avformat_close_input (&player.fctx);
	free (player.url);
	BarSettingsDestroy (&settings);
}
END_TEST

/* A worker whose mode was canceled before setup must not create a sound or
 * republish PLAYING. Even this no-sound path must make cleanup failure terminal. */
START_TEST (test_player_canceled_before_setup_preserves_dead_mode)
{
	player_t player;
	BarSettings_t settings;
	char url[PATH_MAX + 16];
	ck_assert (player_mp3_fixture_path (url, sizeof url));
	player_thread_test_setup (&player, &settings);
	player.url = strdup (url);
	ck_assert_ptr_nonnull (player.url);
	BarPlayerSetMode (&player, PLAYER_WAITING);
	BarPlayerSetMode (&player, PLAYER_DEAD);
	const uint64_t epoch = player.controlEpoch;
	pthread_t worker;
	ck_assert_int_eq (pthread_create (&worker, NULL, BarPlayerThread, &player), 0);
	void *result;
	ck_assert_int_eq (pthread_join (worker, &result), 0);
	ck_assert_uint_eq ((uintptr_t) result, PLAYER_RET_OK);
	ck_assert_int_eq (player.mode, PLAYER_DEAD);
	ck_assert_uint_eq (player.controlEpoch, epoch);
	ck_assert (!player.audioBusy && !player.soundInitialized && !player.dataSourceInitialized);
	ck_assert_ptr_null (player.fctx);
	ck_assert_ptr_null (player.cctx);
	ck_assert_ptr_null (player.fgraph);
	free (player.url);
	player_thread_test_teardown (&player, &settings);
}
END_TEST

START_TEST (test_player_canceled_setup_cleanup_failure_retains_stream)
{
	player_t player;
	BarSettings_t settings;
	char url[PATH_MAX + 16];
	ck_assert (player_mp3_fixture_path (url, sizeof url));
	player_thread_test_setup (&player, &settings);
	player.audioNoDevice = false;
	BarPlayerSetAudioFatalTestHook (returning_audio_fatal_hook);
	fatal_hook_calls = 0;
	fatal_hook_saw_unlocked_player = fatal_hook_saw_unlocked_decoder = false;
	player.url = strdup (url);
	ck_assert_ptr_nonnull (player.url);
	/* A canceled worker can finish opening the stream, but not start playback. */
	BarPlayerSetMode (&player, PLAYER_DEAD);
	pthread_t worker;
	ck_assert_int_eq (pthread_create (&worker, NULL, BarPlayerThread, &player), 0);
	void *result;
	ck_assert_int_eq (pthread_join (worker, &result), 0);
	ck_assert_uint_eq ((uintptr_t) result, PLAYER_RET_HARDFAIL);
	ck_assert_int_eq (fatal_hook_calls, 1);
	ck_assert (fatal_hook_saw_unlocked_player && fatal_hook_saw_unlocked_decoder);
	ck_assert (player.audioTerminalFailure && player.doQuit);
	ck_assert_int_eq (player.mode, PLAYER_DEAD);
	ck_assert_int_eq (player.audioState, PLAYER_AUDIO_FAILED);
	ck_assert (!player.audioBusy && !player.audioOwnerValid);
	ck_assert (!player.soundInitialized && !player.dataSourceInitialized);
	ck_assert_ptr_nonnull (player.fctx);
	ck_assert_ptr_nonnull (player.cctx);
	ck_assert_ptr_nonnull (player.fgraph);
	/* Test-only recovery after join, with no sound or physical callback. */
	BarPlayerSetAudioFatalTestHook (NULL);
	player.audioNoDevice = true;
	player.audioTerminalFailure = false;
	player.doQuit = false;
	ck_assert (BarPlayerDestroy (&player));
	avfilter_graph_free (&player.fgraph);
	avcodec_free_context (&player.cctx);
	avformat_close_input (&player.fctx);
	free (player.url);
	BarSettingsDestroy (&settings);
}
END_TEST

/* Break caught: decode reads packets during logical pause, or
 * treats an unrelated condition broadcast as permission to decode. */
START_TEST (test_player_paused_decoder_waits_through_spurious_wakes_until_resume)
{
	player_t player;
	BarSettings_t settings;
	char url[PATH_MAX + 16];
	ck_assert (player_mp3_fixture_path (url, sizeof url));
	player_thread_test_setup (&player, &settings);
	player.url = strdup (url);
	BarPlayerSetMode (&player, PLAYER_WAITING);
	ck_assert (BarPlayerSetPaused (&player, true));
	pthread_t thread;
	ck_assert_int_eq (pthread_create (&thread, NULL, BarPlayerThread, &player), 0);
	ck_assert (BarPlayerWaitForMode (&player, PLAYER_PLAYING, 10000));
	AVFrame *frame = av_frame_alloc ();
	ck_assert_ptr_nonnull (frame);
	bool parked = true;
	for (unsigned wake = 0; wake < 5; ++wake) {
		pthread_mutex_lock (&player.lock);
		pthread_cond_broadcast (&player.cond);
		pthread_mutex_unlock (&player.lock);
		usleep (100000);
		pthread_mutex_lock (&player.decoderLock);
		const int peek = av_buffersink_get_frame_flags (player.fbufsink, frame, AV_BUFFERSINK_FLAG_PEEK);
		parked = parked && peek == AVERROR (EAGAIN) && !player.decodingFinished;
		pthread_mutex_unlock (&player.decoderLock);
		av_frame_unref (frame);
	}
	av_frame_free (&frame);
	ck_assert (BarPlayerSetPaused (&player, false));
	ck_assert (BarTestDrainAudioFixture (&player));
	ck_assert (BarPlayerWaitForMode (&player, PLAYER_FINISHED, 10000));
	void *result;
	ck_assert_int_eq (pthread_join (thread, &result), 0);
	free (player.url);
	player_thread_test_teardown (&player, &settings);
	ck_assert_int_eq ((uintptr_t)result, PLAYER_RET_OK);
	ck_assert_msg (parked, "Paused decoding must produce no filter frame or EOF after unrelated wakes");
}
END_TEST

/* Break caught: a paused monitor keeps reserving the sound for cursor reads,
 * so a blocked observation makes an otherwise-ready volume control time out. */
START_TEST (test_player_paused_monitor_leaves_audio_available_for_volume)
{
	player_t player;
	BarSettings_t settings;
	char url[PATH_MAX + 16];
	ck_assert (player_mp3_fixture_path (url, sizeof url));
	player_thread_test_setup (&player, &settings);
	player.url = strdup (url);
	BarPlayerSetMode (&player, PLAYER_WAITING);
	pthread_t thread;
	ck_assert_int_eq (pthread_create (&thread, NULL, BarPlayerThread, &player), 0);
	ck_assert (BarPlayerWaitForMode (&player, PLAYER_PLAYING, 10000));
	bool decoded = false;
	for (unsigned elapsed = 0; elapsed < 1000 && !decoded; elapsed += 5) {
		pthread_mutex_lock (&player.decoderLock);
		decoded = player.decodingFinished;
		pthread_mutex_unlock (&player.decoderLock);
		if (!decoded) { usleep (5000); }
	}
	ck_assert (decoded);
	ck_assert (BarPlayerSetPaused (&player, true));
	DecoderGate gate = {.player = &player};
	pthread_mutex_init (&gate.lock, NULL);
	pthread_cond_init (&gate.cond, NULL);
	pthread_t holder;
	ck_assert_int_eq (pthread_create (&holder, NULL, hold_decoder_until_released, &gate), 0);
	pthread_mutex_lock (&gate.lock);
	while (!gate.ready) { pthread_cond_wait (&gate.cond, &gate.lock); }
	pthread_mutex_unlock (&gate.lock);
	/* Longer than two active ticks. The decoder is already finished; any
	 * cursor observation here would retain audio while blocked at this gate. */
	usleep (250000);
	const bool volumeSet = BarPlayerSetVolume (&player, 72);
	const int volume = BarPlayerGetVolume (&player);
	pthread_mutex_lock (&gate.lock);
	gate.release = true;
	pthread_cond_broadcast (&gate.cond);
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_join (holder, NULL), 0);
	/* A mode change must also release the indefinite paused monitor wait. */
	BarPlayerSetMode (&player, PLAYER_FINISHED);
	void *result;
	ck_assert_int_eq (pthread_join (thread, &result), 0);
	pthread_cond_destroy (&gate.cond);
	pthread_mutex_destroy (&gate.lock);
	free (player.url);
	player_thread_test_teardown (&player, &settings);
	ck_assert_int_eq ((uintptr_t)result, PLAYER_RET_OK);
	ck_assert_msg (volumeSet, "Paused monitoring must leave the reservation available for volume control");
	ck_assert_int_eq (volume, 72);
}
END_TEST

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
	const bool engineInitialized = player.engineInitialized;
	player.engineInitialized = false;

	const uintptr_t ret = run_player_thread_sync (&player, url);
	ck_assert_int_eq (ret, PLAYER_RET_HARDFAIL);
	ck_assert_int_eq (BarPlayerGetMode (&player), PLAYER_FINISHED);
	player.engineInitialized = engineInitialized;

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
	BarPlayerSetMode (&player, PLAYER_WAITING);
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
	BarPlayerSetMode (&player, PLAYER_WAITING);
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

START_TEST (test_player_ffmpeg_interrupt_cb_skip_and_quit)
{
	player_t player;
	BarSettings_t settings;

	player_thread_test_setup (&player, &settings);

	ck_assert_int_eq (BarPlayerFfmpegInterruptCb (&player), 0);

	atomic_store_explicit (&player.interrupted, 1, memory_order_relaxed);
	ck_assert_int_eq (BarPlayerFfmpegInterruptCb (&player), 1);
	ck_assert_int_eq (atomic_load_explicit (&player.interrupted, memory_order_relaxed), 0);
	pthread_mutex_lock (&player.lock);
	ck_assert (!player.doQuit);
	pthread_mutex_unlock (&player.lock);

	atomic_store_explicit (&player.interrupted, 2, memory_order_relaxed);
	ck_assert_int_eq (BarPlayerFfmpegInterruptCb (&player), 1);
	pthread_mutex_lock (&player.lock);
	ck_assert (player.doQuit);
	pthread_mutex_unlock (&player.lock);

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
	tcase_add_test (tc_basic, test_player_init_leaves_audio_device_stopped);
	tcase_add_test (tc_basic, test_player_reset_stops_started_device);
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
	tcase_add_test (tc_thread, test_player_paused_decoder_waits_through_spurious_wakes_until_resume);
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
	tcase_add_test (tc_thread, test_player_ffmpeg_interrupt_cb_skip_and_quit);
	suite_add_tcase (s, tc_thread);
	TCase *tc_audio = tcase_create ("Audio reservation");
	TCase *tc_priming = tcase_create ("Startup priming");
	tcase_set_timeout (tc_priming, 10);
	tcase_add_test (tc_priming, test_player_fresh_setup_primes_silence_before_starting_each_song);
	tcase_add_loop_test (tc_priming, test_player_fresh_setup_device_boundary_rolls_back_failure_and_supersession, 0, 4);
	suite_add_tcase (s, tc_priming);
	TCase *tc_lifecycle = tcase_create ("Lifecycle guards");
	tcase_add_test (tc_lifecycle, test_player_destroy_requires_successful_join);
	tcase_add_test (tc_lifecycle, test_player_production_fatal_shutdown_exits_failure);
	suite_add_tcase (s, tc_lifecycle);
	TCase *tc_regression = tcase_create ("Concurrent regression guards");
	tcase_set_timeout (tc_regression, 10);
	tcase_add_test (tc_regression, test_player_release_build_reentrancy_rejects_without_waiting);
	tcase_add_test (tc_regression, test_player_queued_control_has_priority_over_cursor_polling);
	tcase_add_test (tc_regression, test_player_concurrent_absolute_relative_volume_uses_reserved_value);
	tcase_add_test (tc_regression, test_player_pause_resume_stop_stress_finishes_without_owner);
	suite_add_tcase (s, tc_regression);
	tcase_set_timeout (tc_audio, 10);
	tcase_add_test (tc_audio, test_main_final_join_is_terminal_before_shared_state_cleanup);
	tcase_add_test (tc_audio, test_main_finalization_rejects_terminal_player);
	tcase_add_test (tc_audio, test_player_stop_timeout_preserves_blocked_owner_and_unlocks_before_fatal);
	tcase_add_test (tc_audio, test_player_owner_release_at_command_deadline_never_steals_audio);
	tcase_add_test (tc_audio, test_player_pause_helpers_without_sound_wake_condition_waiters);
	tcase_add_test (tc_audio, test_player_controls_are_idempotent_and_preserve_retained_cursor);
	tcase_add_test (tc_audio, test_player_audio_debug_logs_pause_resume_restart_and_stop_transitions);
	tcase_add_test (tc_audio, test_player_controls_reject_terminal_modes_and_null_inputs);
	tcase_add_test (tc_audio, test_player_public_audio_controls_reject_null);
	tcase_add_test (tc_audio, test_player_init_clamps_volume_and_honors_sample_rate);
	tcase_add_test (tc_audio, test_player_snapshot_source_errors_preserve_output_and_release_owner);
	tcase_add_test (tc_audio, test_player_failed_restart_restores_pause_and_allows_retry);
	tcase_add_test (tc_audio, test_player_resume_at_sound_end_does_not_restart);
	tcase_add_test (tc_audio, test_player_resume_failure_releases_waiters_and_allows_retry);
	tcase_add_test (tc_audio, test_player_stop_supersedes_resume_and_aborts_normal_waiters);
	tcase_add_test (tc_audio, test_player_concurrent_toggles_choose_direction_after_reservation);
	tcase_add_test (tc_audio, test_player_ended_mode_aborts_resume_wait_before_owner_releases);
	tcase_add_test (tc_audio, test_player_resume_waits_for_cleanup_and_rejects_dead_mode);
	tcase_add_test (tc_audio, test_player_stop_supersedes_resume_before_backend_start);
	tcase_add_test (tc_audio, test_player_end_callback_supersedes_outstanding_resume);
	tcase_add_test (tc_audio, test_player_request_stop_supersedes_setup_and_releases_waiters);
	tcase_add_test (tc_audio, test_player_request_stop_wakes_decoder_waiter);
	tcase_add_test (tc_audio, test_player_second_interrupt_completes_retained_paused_stop);
	tcase_add_test (tc_audio, test_player_pause_failure_requests_terminal_shutdown);
	tcase_add_test (tc_audio, test_player_resume_rollback_failure_requests_terminal_shutdown);
	tcase_add_test (tc_audio, test_player_request_stop_failure_releases_reservation);
	tcase_add_test (tc_audio, test_player_volume_is_canonical_and_clamped_without_sound);
	tcase_add_test (tc_audio, test_player_audio_observation_yields_to_owner_and_controls);
	tcase_add_test (tc_audio, test_player_volume_aborts_after_quit_without_mutation);
	tcase_add_test (tc_audio, test_player_snapshot_reads_actual_stopped_node);
	tcase_add_test (tc_audio, test_player_snapshot_reserves_lifetime_and_timeout_does_not_steal);
	tcase_add_test (tc_audio, test_player_quit_aborts_normal_waiter_and_releases_teardown);
	tcase_add_test (tc_audio, test_player_superseded_setup_preserves_finished_mode);
	tcase_add_test (tc_audio, test_player_superseded_setup_preserves_dead_mode);
	tcase_add_test (tc_audio, test_player_setup_rollback_terminates_empty_source_callback);
	tcase_add_test (tc_audio, test_player_reset_preserves_controls_changed_during_cleanup);
	tcase_add_test (tc_audio, test_player_failed_setup_preserves_newer_dead_mode);
	tcase_add_test (tc_audio, test_player_failed_setup_preserves_newer_finished_epoch);
	tcase_add_test (tc_audio, test_player_paused_setup_retains_stopped_sound);
	tcase_add_test (tc_audio, test_player_setup_unreconciled_device_retains_resources);
	tcase_add_test (tc_audio, test_player_canceled_before_setup_preserves_dead_mode);
	tcase_add_test (tc_audio, test_player_canceled_setup_cleanup_failure_retains_stream);
	tcase_add_test (tc_audio, test_player_stop_unreconciled_device_is_terminal);
	tcase_add_test (tc_audio, test_player_reset_unreconciled_device_is_terminal);
	tcase_add_test (tc_audio, test_player_destroy_unreconciled_device_is_terminal);
	suite_add_tcase (s, tc_audio);
	TCase *tc_snapshot = tcase_create ("Audio snapshot contention");
	tcase_add_test (tc_snapshot, test_player_snapshot_reader_retries_transient_control_lock);
	suite_add_tcase (s, tc_snapshot);
	TCase *tc_monitor = tcase_create ("Paused monitor");
	tcase_set_timeout (tc_monitor, 10);
	tcase_add_test (tc_monitor, test_player_paused_monitor_leaves_audio_available_for_volume);
	suite_add_tcase (s, tc_monitor);
	
	return s;
}
