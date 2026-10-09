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
#include <stdatomic.h>
#include <stdlib.h>
#include <json-c/json.h>
#include <unistd.h>
#include "../../src/ui.h"
#include "../integration/fixture_http.h"

#include "../../src/bar_state.h"
#include "../../src/bar_constants.h"
#include "../../src/main.h"
#include "../../src/player.h"
#include "../../src/playback_manager.h"
#include "../../src/websocket/core/websocket.h"

#ifdef WEBSOCKET_ENABLED

void BarUiActSkipSong (BarApp_t *, PianoStation_t *, PianoSong_t *, int);
void BarUiActPandoraDisconnect (BarApp_t *, PianoStation_t *, PianoSong_t *, int);

static void *test_player_thread_ok(void *arg) {
	(void)arg;
	return (void *)PLAYER_RET_OK;
}

typedef struct {
	BarApp_t *app;
	ma_allocation_callbacks original;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool entered, release, disconnect;
} ManagerSetupGate;

static void manager_wait_sound_allocation (ManagerSetupGate *gate) {
	pthread_mutex_lock (&gate->lock);
	if (!gate->entered) {
		gate->entered = true;
		pthread_cond_broadcast (&gate->cond);
		while (!gate->release) { pthread_cond_wait (&gate->cond, &gate->lock); }
	}
	pthread_mutex_unlock (&gate->lock);
}
static void *manager_gated_malloc (size_t size, void *data) {
	ManagerSetupGate *gate = data;
	manager_wait_sound_allocation (gate);
	return gate->original.onMalloc (size, gate->original.pUserData);
}
static void *manager_gated_realloc (void *pointer, size_t size, void *data) {
	ManagerSetupGate *gate = data;
	manager_wait_sound_allocation (gate);
	return gate->original.onRealloc (pointer, size, gate->original.pUserData);
}
static void manager_gated_free (void *pointer, void *data) {
	ManagerSetupGate *gate = data;
	gate->original.onFree (pointer, gate->original.pUserData);
}
static PianoSong_t *setupProgressSong;
static unsigned setupProgressFetches;
static bool manager_setup_playlist (BarApp_t *app, PianoRequestType_t type, void *data,
		PianoReturn_t *pRet, CURLcode *wRet) {
	(void)app;
	if (type != PIANO_REQUEST_GET_PLAYLIST) { return false; }
	PianoRequestDataGetPlaylist_t *request = data;
	request->retPlaylist = setupProgressFetches++ == 0 ? setupProgressSong : NULL;
	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;
	return true;
}
static void *manager_supersede_setup (void *data) {
	ManagerSetupGate *gate = data;
	if (gate->disconnect) { BarUiActPandoraDisconnect (gate->app, NULL, NULL, 1); }
	else { BarUiActSkipSong (gate->app, NULL, NULL, 1); }
	return NULL;
}

/* Break caught: the real manager parks on WAITING && quit after its only
 * player exited superseded setup, instead of joining and reaching idle. */
START_TEST (test_manager_superseded_setup_skip_and_disconnect_reach_joined_idle)
{
	const unsigned action = _i;
	BarApp_t app = {0};
	BarSettingsInit (&app.settings);
	/* Fully owned deterministic settings, without reading user config. */
	app.settings.partnerUser = strdup ("test");
	app.settings.partnerPassword = strdup ("test");
	app.settings.device = strdup ("test");
	app.settings.inkey = strdup ("12345678");
	app.settings.outkey = strdup ("87654321");
	app.settings.npSongFormat = strdup ("%t");
	app.settings.npStationFormat = strdup ("%n");
	app.settings.timeout = 2;
	app.settings.gainMul = 1.0;
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.history = 1;
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);
	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
		app.settings.partnerPassword, app.settings.device,
		app.settings.inkey, app.settings.outkey), PIANO_RET_OK);
	setenv ("PIANOBAR_TEST_NO_DEVICE", "1", 1);
	BarPlayerInit (&app.player, &app.settings);
	BarFixtureHttp_t http;
	uint16_t port;
	ck_assert (BarFixtureHttpStart (&http, "test/fixtures/tone.mp3", &port));
	char url[256];
	snprintf (url, sizeof url, "http://127.0.0.1:%u/tone.mp3", port);
	PianoStation_t station = {.id = "setup-progress", .name = "Setup progress"};
	PianoSong_t *song = calloc (1, sizeof *song);
	ck_assert_ptr_nonnull (song);
	song->title = strdup ("Setup progress");
	song->artist = strdup ("Fixture");
	song->album = strdup ("Fixture");
	song->audioUrl = strdup (url);
	song->length = 1;
	setupProgressSong = song;
	setupProgressFetches = 0;
	BarUiPianoCallSetTestHook (manager_setup_playlist);
	ManagerSetupGate gate = {.app = &app, .disconnect = action != 0,
		.original = app.player.engine.allocationCallbacks};
	pthread_mutex_init (&gate.lock, NULL);
	pthread_cond_init (&gate.cond, NULL);
	app.player.engine.allocationCallbacks = (ma_allocation_callbacks) {
		.pUserData = &gate, .onMalloc = manager_gated_malloc,
		.onRealloc = manager_gated_realloc, .onFree = manager_gated_free,
	};
	BarStateSetNextStation (&app, &station);
	ck_assert (BarPlaybackManagerStart (&app));
	pthread_mutex_lock (&gate.lock);
	while (!gate.entered) { pthread_cond_wait (&gate.cond, &gate.lock); }
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (BarPlayerGetMode (&app.player), PLAYER_WAITING);
	pthread_t control;
	ck_assert_int_eq (pthread_create (&control, NULL, manager_supersede_setup, &gate), 0);
	bool quit = false;
	for (unsigned ms = 0; ms < 500 && !quit; ++ms) {
		pthread_mutex_lock (&app.player.lock);
		quit = app.player.doQuit;
		pthread_mutex_unlock (&app.player.lock);
		if (!quit) { usleep (1000); }
	}
	pthread_mutex_lock (&gate.lock);
	gate.release = true;
	pthread_cond_broadcast (&gate.cond);
	pthread_mutex_unlock (&gate.lock);
	const bool idle = BarPlaybackManagerWaitParkedIdle (&app, 2000);
	pthread_mutex_lock (&app.player.lock);
	const bool joined = !app.player.threadJoinPending;
	const BarPlayerMode mode = app.player.mode;
	pthread_mutex_unlock (&app.player.lock);
	/* Even RED tears down safely via the real manager shutdown/join. */
	BarPlaybackManagerStop (&app);
	if (!idle) {
		/* RED-only rescue after shutdown joined the real worker. Do not
		 * let disconnect's terminal wait delay safe fixture cleanup. */
		BarPlayerSetMode (&app.player, PLAYER_DEAD);
	}
	ck_assert_int_eq (pthread_join (control, NULL), 0);
	app.player.engine.allocationCallbacks = gate.original;
	app.player.url = NULL;
	BarStateDrainPlaylist (&app);
	PianoDestroyPlaylist (app.songHistory);
	free (app.lastStationId);
	BarUiPianoCallClearTestHook ();
	PianoDestroy (&app.ph);
	BarUiPianoHttpMutexDestroy (&app);
	ck_assert (BarPlayerDestroy (&app.player));
	BarStateDestroy (&app);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	BarFixtureHttpStop (&http);
	pthread_cond_destroy (&gate.cond);
	pthread_mutex_destroy (&gate.lock);
	ck_assert (quit);
	ck_assert_msg (idle && joined && mode == PLAYER_DEAD,
		"%s during setup must reach manager idle after a successful player join; idle=%d joined=%d mode=%d",
		action ? "Disconnect" : "Skip", idle, joined, mode);
}
END_TEST

static unsigned managerJoinCalls;
static bool manager_fail_join (pthread_t thread, void **retval, unsigned int seconds) {
	(void)retval;
	ck_assert (pthread_equal (thread, pthread_self ()));
	ck_assert_int_lt (managerJoinCalls, 2);
	ck_assert_uint_eq (seconds, managerJoinCalls == 0 ? 10 : 5);
	++managerJoinCalls;
	return false;
}
static unsigned managerFatalCalls;
static void manager_fatal_hook (player_t *player, const char *operation) {
	(void)operation;
	++managerFatalCalls;
	ck_assert_int_eq (pthread_mutex_trylock (&player->lock), 0);
	pthread_mutex_unlock (&player->lock);
	ck_assert_int_eq (pthread_mutex_trylock (&player->decoderLock), 0);
	pthread_mutex_unlock (&player->decoderLock);
	ck_assert (player->synchronizationInitialized && player->engineInitialized);
}

/* Break caught: manager marks a still-live player joined and returns to idle. */
START_TEST (test_manager_final_join_is_terminal_before_shared_state_cleanup)
{
	BarApp_t app = {0};
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;
	setenv ("PIANOBAR_TEST_NO_DEVICE", "1", 1);
	BarPlayerInit (&app.player, &app.settings);
	BarPlayerSetMode (&app.player, PLAYER_FINISHED);
	BarPlayerSetJoinTestHook (manager_fail_join);
	BarPlayerSetAudioFatalTestHook (manager_fatal_hook);
	managerFatalCalls = 0;
	managerJoinCalls = 0;
	bool started = true;
	pthread_t worker = pthread_self ();
	ck_assert_int_eq (BarPlaybackManagerCompleteSongCleanup (&app, &worker, &started, PLAYER_FINISHED), PLAYER_FINISHED);
	ck_assert (started);
	ck_assert_int_eq (managerFatalCalls, 1);
	ck_assert_int_eq (managerJoinCalls, 2);
	ck_assert (!BarPlayerDestroy (&app.player));
	BarPlayerSetJoinTestHook (NULL);
	BarPlayerSetAudioFatalTestHook (NULL);
	app.player.audioTerminalFailure = false;
	ck_assert (BarPlayerDestroy (&app.player));
	BarSettingsDestroy (&app.settings);
}
END_TEST

typedef struct {
	BarApp_t *app;
	bool stopped;
} TimeoutPlayer;

/* A real stop request must wake the player before the old one-second poll.
 * Publish DEAD like a completed worker so real session teardown can finish. */
static void *timeout_player (void *arg) {
	TimeoutPlayer *worker = arg;
	player_t *player = &worker->app->player;
	struct timespec deadline;
	clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_nsec += 300000000L;
	if (deadline.tv_nsec >= 1000000000L) {
		++deadline.tv_sec;
		deadline.tv_nsec -= 1000000000L;
	}
	pthread_mutex_lock (&player->lock);
	int ret = 0;
	while (!player->doQuit && ret == 0) {
		ret = pthread_cond_timedwait (&player->cond, &player->lock, &deadline);
	}
	worker->stopped = player->doQuit;
	pthread_mutex_unlock (&player->lock);
	BarPlayerSetMode (player, PLAYER_DEAD);
	return NULL;
}

START_TEST(test_manager_expired_pause_disconnects_without_poll_delay) {
	BarApp_t app = {0};
	PianoStation_t station = { .id = "paused-station", .name = "Paused" };
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.pauseTimeout = 1;
	app.settings.partnerUser = "test";
	app.settings.partnerPassword = "test";
	app.settings.device = "test";
	app.settings.inkey = "12345678";
	app.settings.outkey = "87654321";
	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);
	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
			app.settings.partnerPassword, app.settings.device,
			app.settings.inkey, app.settings.outkey), PIANO_RET_OK);
	pthread_mutex_init (&app.player.lock, NULL);
	pthread_cond_init (&app.player.cond, NULL);
	pthread_cond_init (&app.player.audioCond, NULL);
	pthread_mutex_init (&app.player.decoderLock, NULL);
	pthread_cond_init (&app.player.decoderCond, NULL);
	app.player.settings = &app.settings;
	app.player.mode = PLAYER_PLAYING;
	app.player.doPause = true;
	app.player.pauseStartTime = time (NULL) - 61;
	BarStateSetCurrentStation (&app, &station);
	TimeoutPlayer worker = { .app = &app };
	pthread_t workerThread;
	ck_assert_int_eq (pthread_create (&workerThread, NULL, timeout_player, &worker), 0);
	ck_assert (BarPlaybackManagerStart (&app));
	ck_assert_int_eq (pthread_join (workerThread, NULL), 0);
	BarPlaybackManagerStop (&app);
	ck_assert_msg (worker.stopped,
			"An already expired pause must disconnect immediately, without a one-second poll");
	ck_assert_ptr_null (BarStateGetCurrentStation (&app));
	ck_assert_str_eq (app.lastStationId, "paused-station");
	free (app.lastStationId);
	PianoDestroy (&app.ph);
	BarUiPianoHttpMutexDestroy (&app);
	pthread_cond_destroy (&app.player.decoderCond);
	pthread_mutex_destroy (&app.player.decoderLock);
	pthread_cond_destroy (&app.player.audioCond);
	pthread_cond_destroy (&app.player.cond);
	pthread_mutex_destroy (&app.player.lock);
	BarStateDestroy (&app);
}
END_TEST

/* Fixtures use literal CLOCK_REALTIME values: pause at 1000, timeout 1 minute
 * means exactly 1060. A polling deadline, restarted timer or stale control
 * predicate must fail these decisions. */
START_TEST(test_wait_paused_timeout_disabled_is_indefinite) {
	player_t player = { .mode = PLAYER_PLAYING, .doPause = true, .pauseStartTime = 1000 };
	struct timespec deadline;
	ck_assert_int_eq (BarPlaybackManagerSelectWait (&player, false, 0, false,
			(struct timespec) {1042, 250000000L}, &deadline), BAR_PLAYBACK_WAIT_INDEFINITE);
}
END_TEST

START_TEST(test_wait_paused_future_deadline_is_exact) {
	player_t player = { .mode = PLAYER_PLAYING, .doPause = true, .pauseStartTime = 1000 };
	struct timespec deadline;
	ck_assert_int_eq (BarPlaybackManagerSelectWait (&player, false, 1, false,
			(struct timespec) {1042, 250000000L}, &deadline), BAR_PLAYBACK_WAIT_TIMED);
	ck_assert_int_eq (deadline.tv_sec, 1060);
	ck_assert_int_eq (deadline.tv_nsec, 0);
}
END_TEST

START_TEST(test_wait_paused_deadline_expired_at_equality) {
	player_t player = { .mode = PLAYER_PLAYING, .doPause = true, .pauseStartTime = 1000 };
	struct timespec deadline;
	ck_assert_int_eq (BarPlaybackManagerSelectWait (&player, false, 1, false,
			(struct timespec) {1060, 0}, &deadline), BAR_PLAYBACK_WAIT_PAUSE_EXPIRED);
}
END_TEST

START_TEST(test_wait_paused_without_start_time_is_indefinite) {
	player_t player = { .mode = PLAYER_PLAYING, .doPause = true };
	struct timespec deadline;
	ck_assert_int_eq (BarPlaybackManagerSelectWait (&player, false, 1, false,
			(struct timespec) {1042, 0}, &deadline), BAR_PLAYBACK_WAIT_INDEFINITE);
}
END_TEST

START_TEST(test_wait_resume_before_deadline_restores_progress_cadence) {
	player_t player = { .mode = PLAYER_PLAYING, .doPause = true, .pauseStartTime = 1000 };
	struct timespec deadline;
	BarPlaybackManagerSelectWait (&player, false, 1, false,
			(struct timespec) {1042, 0}, &deadline);
	player.doPause = false;
	player.pauseStartTime = 0;
	ck_assert_int_eq (BarPlaybackManagerSelectWait (&player, false, 1, false,
			(struct timespec) {1045, 250000000L}, &deadline), BAR_PLAYBACK_WAIT_TIMED);
	ck_assert_int_eq (deadline.tv_sec, 1046);
	ck_assert_int_eq (deadline.tv_nsec, 250000000L);
}
END_TEST

START_TEST(test_wait_skip_before_deadline_cancels_pause_timer) {
	player_t player = { .mode = PLAYER_PLAYING, .doPause = true,
		.pauseStartTime = 1000, .doQuit = true };
	struct timespec deadline;
	ck_assert_int_eq (BarPlaybackManagerSelectWait (&player, false, 1, false,
			(struct timespec) {1042, 0}, &deadline), BAR_PLAYBACK_WAIT_INDEFINITE);
	ck_assert_int_eq (BarPlaybackManagerSelectWait (&player, false, 1, false,
			(struct timespec) {1061, 0}, &deadline), BAR_PLAYBACK_WAIT_INDEFINITE);
}
END_TEST

START_TEST(test_wait_shutdown_before_deadline_is_immediate) {
	player_t player = { .mode = PLAYER_PLAYING, .doPause = true, .pauseStartTime = 1000 };
	struct timespec deadline;
	ck_assert_int_eq (BarPlaybackManagerSelectWait (&player, false, 1, true,
			(struct timespec) {1042, 0}, &deadline), BAR_PLAYBACK_WAIT_READY);
}
END_TEST

START_TEST(test_wait_finished_mode_cancels_expired_pause) {
	player_t player = { .mode = PLAYER_FINISHED, .doPause = true, .pauseStartTime = 1000 };
	struct timespec deadline;
	ck_assert_int_eq (BarPlaybackManagerSelectWait (&player, false, 1, false,
			(struct timespec) {1061, 0}, &deadline), BAR_PLAYBACK_WAIT_READY);
}
END_TEST

START_TEST(test_wait_spurious_wake_keeps_original_pause_deadline) {
	player_t player = { .mode = PLAYER_PLAYING, .doPause = true, .pauseStartTime = 1000 };
	struct timespec deadline;
	ck_assert_int_eq (BarPlaybackManagerSelectWait (&player, false, 1, false,
			(struct timespec) {1042, 0}, &deadline), BAR_PLAYBACK_WAIT_TIMED);
	ck_assert_int_eq (deadline.tv_sec, 1060);
	ck_assert_int_eq (BarPlaybackManagerSelectWait (&player, false, 1, false,
			(struct timespec) {1059, 999999999L}, &deadline), BAR_PLAYBACK_WAIT_TIMED);
	ck_assert_int_eq (deadline.tv_sec, 1060);
	ck_assert_int_eq (deadline.tv_nsec, 0);
}
END_TEST

START_TEST(test_wait_new_pause_replaces_previous_deadline) {
	player_t player = { .mode = PLAYER_PLAYING, .doPause = true, .pauseStartTime = 1000 };
	struct timespec deadline;
	BarPlaybackManagerSelectWait (&player, false, 1, false,
			(struct timespec) {1042, 0}, &deadline);
	player.pauseStartTime = 1050;
	ck_assert_int_eq (BarPlaybackManagerSelectWait (&player, false, 1, false,
			(struct timespec) {1061, 0}, &deadline), BAR_PLAYBACK_WAIT_TIMED);
	ck_assert_int_eq (deadline.tv_sec, 1110);
	ck_assert_int_eq (deadline.tv_nsec, 0);
}
END_TEST

START_TEST(test_wait_active_normalizes_nanosecond_overflow) {
	player_t player = { .mode = PLAYER_PLAYING };
	struct timespec deadline;
	ck_assert_int_eq (BarPlaybackManagerSelectWait (&player, false, 1, false,
			(struct timespec) {1042, 1500000000L}, &deadline), BAR_PLAYBACK_WAIT_TIMED);
	ck_assert_int_eq (deadline.tv_sec, 1044);
	ck_assert_int_eq (deadline.tv_nsec, 500000000L);
}
END_TEST

START_TEST(test_wait_active_normalizes_negative_nanoseconds) {
	player_t player = { .mode = PLAYER_PLAYING };
	struct timespec deadline;
	ck_assert_int_eq (BarPlaybackManagerSelectWait (&player, false, 1, false,
			(struct timespec) {1042, -1}, &deadline), BAR_PLAYBACK_WAIT_TIMED);
	ck_assert_int_eq (deadline.tv_sec, 1042);
	ck_assert_int_eq (deadline.tv_nsec, 999999999L);
}
END_TEST

START_TEST(test_wait_idle_after_skip_parks_despite_player_quit) {
	player_t player = { .mode = PLAYER_DEAD, .doQuit = true };
	struct timespec deadline;
	ck_assert_int_eq (BarPlaybackManagerSelectWait (&player, true, 1, false,
			(struct timespec) {1042, 0}, &deadline), BAR_PLAYBACK_WAIT_INDEFINITE);
}
END_TEST

START_TEST(test_wait_idle_with_queue_work_is_immediate) {
	player_t player = { .mode = PLAYER_DEAD };
	struct timespec deadline;
	ck_assert_int_eq (BarPlaybackManagerSelectWait (&player, false, 1, false,
			(struct timespec) {1042, 0}, &deadline), BAR_PLAYBACK_WAIT_READY);
}
END_TEST

START_TEST(test_wait_stale_idle_snapshot_cannot_park_active_player) {
	player_t player = { .mode = PLAYER_PLAYING };
	struct timespec deadline;
	ck_assert_int_eq (BarPlaybackManagerSelectWait (&player, true, 1, false,
			(struct timespec) {1042, 0}, &deadline), BAR_PLAYBACK_WAIT_TIMED);
	ck_assert_int_eq (deadline.tv_sec, 1043);
}
END_TEST

START_TEST(test_manager_progress_only_while_active_after_pause_wakes) {
	BarApp_t app = {0};
	BarWsContext_t ctx = {0};
	PianoStation_t station = { .id = "progress-station", .name = "Progress" };
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.pauseTimeout = 1;
	BarStateInit (&app);
	pthread_mutex_init (&app.player.lock, NULL);
	pthread_cond_init (&app.player.cond, NULL);
	for (int i = 0; i < BUCKET_COUNT; ++i) {
		pthread_mutex_init (&ctx.buckets[i].mutex, NULL);
	}
	app.wsContext = &ctx;
	app.player.mode = PLAYER_PLAYING;
	app.player.doPause = true;
	app.player.pauseStartTime = time (NULL);
	app.player.songPlayed = 7;
	app.player.songDuration = 70;
	BarStateSetCurrentStation (&app, &station);
	ck_assert (BarPlaybackManagerStart (&app));
	for (int i = 0; i < 3; ++i) {
		BarStateSignalPlaybackManager (&app);
		usleep (50000);
	}
	ck_assert_ptr_eq (BarStateGetCurrentStation (&app), &station);
	pthread_mutex_lock (&ctx.buckets[BUCKET_PROGRESS].mutex);
	ck_assert_ptr_null (ctx.buckets[BUCKET_PROGRESS].message);
	pthread_mutex_unlock (&ctx.buckets[BUCKET_PROGRESS].mutex);
	/* Resume before the minute deadline; the next ordinary tick must publish
	 * actual progress, proving the paused deadline was abandoned on its wake. */
	pthread_mutex_lock (&app.player.lock);
	app.player.doPause = false;
	app.player.pauseStartTime = 0;
	pthread_cond_broadcast (&app.player.cond);
	pthread_mutex_unlock (&app.player.lock);
	usleep (1200000);
	pthread_mutex_lock (&ctx.buckets[BUCKET_PROGRESS].mutex);
	BarWsMessage_t *message = ctx.buckets[BUCKET_PROGRESS].message;
	ck_assert_ptr_nonnull (message);
	json_object *event = json_tokener_parse ((char *) message->data + 1);
	ck_assert_ptr_nonnull (event);
	ck_assert_str_eq (json_object_get_string (json_object_array_get_idx (event, 0)), "progress");
	json_object *payload = json_object_array_get_idx (event, 1);
	ck_assert_int_eq (json_object_get_int (json_object_object_get (payload, "elapsed")), 7);
	json_object_put (event);
	BarWsMessageFree (message);
	ctx.buckets[BUCKET_PROGRESS].message = NULL;
	pthread_mutex_unlock (&ctx.buckets[BUCKET_PROGRESS].mutex);
	pthread_mutex_lock (&app.player.lock);
	app.player.doPause = true;
	app.player.pauseStartTime = time (NULL);
	app.player.songPlayed = 8;
	pthread_cond_broadcast (&app.player.cond);
	pthread_mutex_unlock (&app.player.lock);
	usleep (1200000);
	pthread_mutex_lock (&ctx.buckets[BUCKET_PROGRESS].mutex);
	ck_assert_ptr_null (ctx.buckets[BUCKET_PROGRESS].message);
	pthread_mutex_unlock (&ctx.buckets[BUCKET_PROGRESS].mutex);
	BarPlaybackManagerStop (&app);
	for (int i = 0; i < BUCKET_COUNT; ++i) {
		BarWsMessageFree (ctx.buckets[i].message);
		pthread_mutex_destroy (&ctx.buckets[i].mutex);
	}
	pthread_cond_destroy (&app.player.cond);
	pthread_mutex_destroy (&app.player.lock);
	BarStateDestroy (&app);
}
END_TEST

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
	atomic_store_explicit (&app.player.interrupted, 1, memory_order_relaxed);
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

/* Covers idle PLAYER_DEAD path: BarStateAdvancePlaylist + BarUiHistoryPrepend. */
START_TEST(test_manager_idle_advances_playlist) {
	BarApp_t app;
	PianoSong_t song;

	memset(&song, 0, sizeof(song));
	song.title = (char *)"idle-advance";

	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.history = 10;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	pthread_cond_init(&app.player.cond, NULL);
	app.player.mode = PLAYER_DEAD;
	BarStateSetPlaylist(&app, &song);

	ck_assert(BarPlaybackManagerStart(&app));
	pthread_mutex_lock(&app.player.lock);
	pthread_cond_broadcast(&app.player.cond);
	pthread_mutex_unlock(&app.player.lock);
	usleep(100000);

	BarPlaybackManagerStop(&app);

	ck_assert_ptr_null(BarStateGetPlaylist(&app));
	ck_assert_ptr_eq(app.songHistory, &song);

	pthread_mutex_destroy(&app.player.lock);
	pthread_cond_destroy(&app.player.cond);
	BarStateDestroy(&app);
}
END_TEST

static bool
mock_get_playlist_ok (BarApp_t *app, const PianoRequestType_t type, void *data,
                      PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	if (type == PIANO_REQUEST_GET_PLAYLIST) {
		((PianoRequestDataGetPlaylist_t *) data)->retPlaylist = NULL;
	}
	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;
	return true;
}

START_TEST(test_manager_fetches_playlist_when_next_station_set) {
	BarApp_t app;
	PianoStation_t st;

	memset(&st, 0, sizeof(st));
	st.id = "fetch-st";
	st.name = "Fetch";

	memset(&app, 0, sizeof(app));
	/* Match the owned station-format default from BarSettingsRead; do not
	 * load the user's configuration into this isolated manager fixture. */
	app.settings.npStationFormat = strdup ("Station \"%n\" (%i)");
	ck_assert_ptr_nonnull (app.settings.npStationFormat);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	pthread_cond_init(&app.player.cond, NULL);
	app.player.mode = PLAYER_DEAD;
	BarStateSetNextStation(&app, &st);
	BarUiPianoCallSetTestHook (mock_get_playlist_ok);

	ck_assert(BarPlaybackManagerStart(&app));
	pthread_mutex_lock(&app.player.lock);
	pthread_cond_broadcast(&app.player.cond);
	pthread_mutex_unlock(&app.player.lock);
	usleep(150000);

	atomic_store_explicit (&app.doQuit, true, memory_order_relaxed);
	BarPlaybackManagerStop(&app);
	BarUiPianoCallClearTestHook ();

	pthread_mutex_destroy(&app.player.lock);
	pthread_cond_destroy(&app.player.cond);
	BarStateDestroy(&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST(test_should_park_idle_when_dead_and_empty) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	app.player.mode = PLAYER_DEAD;
	ck_assert(BarPlaybackShouldParkIdle(&app));
	pthread_mutex_destroy(&app.player.lock);
	BarStateDestroy(&app);
}
END_TEST

START_TEST(test_should_not_park_when_next_station_set) {
	BarApp_t app;
	PianoStation_t st = { .id = "1", .name = "Test" };
	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	app.player.mode = PLAYER_DEAD;
	BarStateSetNextStation(&app, &st);
	ck_assert(!BarPlaybackShouldParkIdle(&app));
	pthread_mutex_destroy(&app.player.lock);
	BarStateDestroy(&app);
}
END_TEST

START_TEST(test_should_not_park_when_not_dead) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	app.player.mode = PLAYER_PLAYING;
	ck_assert(!BarPlaybackShouldParkIdle(&app));
	pthread_mutex_destroy(&app.player.lock);
	BarStateDestroy(&app);
}
END_TEST

START_TEST(test_should_not_park_when_playlist_set) {
	BarApp_t app;
	PianoSong_t pl;
	memset(&app, 0, sizeof(app));
	memset(&pl, 0, sizeof(pl));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	app.player.mode = PLAYER_DEAD;
	BarStateSetPlaylist(&app, &pl);
	ck_assert(!BarPlaybackShouldParkIdle(&app));
	pthread_mutex_destroy(&app.player.lock);
	BarStateDestroy(&app);
}
END_TEST

START_TEST(test_manager_thread_uses_timed_wait_when_not_parked) {
	BarApp_t app;

	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	pthread_cond_init(&app.player.cond, NULL);
	app.player.mode = PLAYER_PLAYING;

	ck_assert(BarPlaybackManagerStart(&app));
	usleep(100000);
	BarPlaybackManagerStop(&app);

	pthread_mutex_destroy(&app.player.lock);
	pthread_cond_destroy(&app.player.cond);
	BarStateDestroy(&app);
}
END_TEST

START_TEST(test_manager_wakes_on_state_signal) {
	BarApp_t app;

	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	pthread_cond_init(&app.player.cond, NULL);
	app.player.mode = PLAYER_DEAD;

	ck_assert(BarPlaybackManagerStart(&app));
	usleep(50000);
	BarStateSignalPlaybackManager(&app);
	BarPlaybackManagerStop(&app);

	pthread_mutex_destroy(&app.player.lock);
	pthread_cond_destroy(&app.player.cond);
	BarStateDestroy(&app);
}
END_TEST

START_TEST(test_wait_parked_idle_when_already_idle) {
	BarApp_t app;

	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	app.player.mode = PLAYER_DEAD;

	ck_assert(BarPlaybackManagerWaitParkedIdle(&app, BAR_PLAYER_STOP_TIMEOUT_MS));

	pthread_mutex_destroy(&app.player.lock);
	BarStateDestroy(&app);
}
END_TEST

START_TEST(test_wait_parked_idle_when_manager_running_and_parked) {
	BarApp_t app;

	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	pthread_cond_init(&app.player.cond, NULL);
	app.player.mode = PLAYER_DEAD;

	ck_assert(BarPlaybackManagerStart(&app));
	usleep(50000);
	ck_assert(BarPlaybackManagerWaitParkedIdle(&app, BAR_PLAYER_STOP_TIMEOUT_MS));

	BarPlaybackManagerStop(&app);
	pthread_mutex_destroy(&app.player.lock);
	pthread_cond_destroy(&app.player.cond);
	BarStateDestroy(&app);
}
END_TEST

START_TEST(test_wait_parked_idle_times_out_when_not_parked) {
	BarApp_t app;

	memset(&app, 0, sizeof(app));
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit(&app);
	pthread_mutex_init(&app.player.lock, NULL);
	pthread_cond_init(&app.player.cond, NULL);
	app.player.mode = PLAYER_PLAYING;

	ck_assert(BarPlaybackManagerStart(&app));
	usleep(50000);
	ck_assert(!BarPlaybackManagerWaitParkedIdle(&app, BAR_PLAYER_STOP_POLL_MS));

	BarPlaybackManagerStop(&app);
	pthread_mutex_destroy(&app.player.lock);
	pthread_cond_destroy(&app.player.cond);
	BarStateDestroy(&app);
}
END_TEST

Suite *playback_manager_suite(void) {
	Suite *s = suite_create("PlaybackManager");
	TCase *tc = tcase_create("State machine");
	TCase *waitTc = tcase_create("Wait selection");
	TCase *setupTc = tcase_create ("Superseded setup progress");
	tcase_set_timeout (setupTc, 10);
	tcase_add_loop_test (setupTc, test_manager_superseded_setup_skip_and_disconnect_reach_joined_idle, 0, 2);
	suite_add_tcase (s, setupTc);
	tcase_add_test(tc, test_manager_final_join_is_terminal_before_shared_state_cleanup);
	tcase_add_test(tc, test_manager_expired_pause_disconnects_without_poll_delay);
	tcase_add_test(waitTc, test_wait_paused_timeout_disabled_is_indefinite);
	tcase_add_test(waitTc, test_wait_paused_future_deadline_is_exact);
	tcase_add_test(waitTc, test_wait_paused_deadline_expired_at_equality);
	tcase_add_test(waitTc, test_wait_paused_without_start_time_is_indefinite);
	tcase_add_test(waitTc, test_wait_resume_before_deadline_restores_progress_cadence);
	tcase_add_test(waitTc, test_wait_skip_before_deadline_cancels_pause_timer);
	tcase_add_test(waitTc, test_wait_shutdown_before_deadline_is_immediate);
	tcase_add_test(waitTc, test_wait_finished_mode_cancels_expired_pause);
	tcase_add_test(waitTc, test_wait_spurious_wake_keeps_original_pause_deadline);
	tcase_add_test(waitTc, test_wait_new_pause_replaces_previous_deadline);
	tcase_add_test(waitTc, test_wait_active_normalizes_nanosecond_overflow);
	tcase_add_test(waitTc, test_wait_active_normalizes_negative_nanoseconds);
	tcase_add_test(waitTc, test_wait_idle_after_skip_parks_despite_player_quit);
	tcase_add_test(waitTc, test_wait_idle_with_queue_work_is_immediate);
	tcase_add_test(waitTc, test_wait_stale_idle_snapshot_cannot_park_active_player);
	tcase_add_test(tc, test_manager_progress_only_while_active_after_pause_wakes);
	tcase_add_test(tc, test_refresh_cached_mode_after_cleanup);
	tcase_add_test(tc, test_complete_song_cleanup_refreshes_mode);
	tcase_add_test(tc, test_handle_finished_mode_passthrough);
	tcase_add_test(tc, test_handle_finished_mode_runs_cleanup);
	tcase_add_test(tc, test_complete_song_cleanup_interrupt_on_quit);
	tcase_add_test(tc, test_complete_song_cleanup_no_interrupt_log_when_not_quitting);
	tcase_add_test(tc, test_manager_thread_one_loop_iteration);
	tcase_add_test(tc, test_manager_idle_advances_playlist);
	tcase_add_test(tc, test_manager_fetches_playlist_when_next_station_set);
	tcase_add_test(tc, test_should_park_idle_when_dead_and_empty);
	tcase_add_test(tc, test_should_not_park_when_next_station_set);
	tcase_add_test(tc, test_should_not_park_when_not_dead);
	tcase_add_test(tc, test_should_not_park_when_playlist_set);
	tcase_add_test(tc, test_manager_wakes_on_state_signal);
	tcase_add_test(tc, test_manager_thread_uses_timed_wait_when_not_parked);
	tcase_add_test(tc, test_wait_parked_idle_when_already_idle);
	tcase_add_test(tc, test_wait_parked_idle_when_manager_running_and_parked);
	tcase_add_test(tc, test_wait_parked_idle_times_out_when_not_parked);
	suite_add_tcase(s, tc);
	suite_add_tcase(s, waitTc);
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
