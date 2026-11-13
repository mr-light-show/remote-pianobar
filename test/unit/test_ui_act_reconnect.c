/*
 * Unit tests for selected BarUiAct* handlers to raise coverage of ui_act.c.
 */

#include <check.h>
#include <json-c/json.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../../src/bar_constants.h"
#include "../../src/bar_state.h"
#include "../test_tmp.h"
#include "../../src/l10n.h"
#include "../../src/log.h"
#include "../../src/main.h"
#include "../../src/player.h"
#include "../../src/settings.h"
#include "../../src/system_volume.h"
#include "../../src/ui.h"
#include "../../src/ui_readline.h"
#include "../../src/websocket/core/websocket.h"
#include "../../src/websocket/protocol/socketio.h"
#include "../../src/websocket_bridge.h"
#include "../../src/playback_manager.h"
#include "test_ws_capture.h"

/* Declaration only — avoid ui_dispatch.h (pulls full shortcut table + ui_act.h). */
void BarUiActPandoraReconnect (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActHelp (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActSongInfo (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActPrintUpcoming (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActHistory (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActPandoraDisconnect (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActExplain (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActExplainTo (BarApp_t *app, const PianoStation_t *selStation,
		PianoSong_t *selSong, BarWsConnId_t requester);
void BarUiActPrintUpcomingTo (BarApp_t *app, BarWsConnId_t requester);
void BarUiActManageStation (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActCreateStationFromSong (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActPlay (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActPause (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActTogglePause (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActVolDown (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActVolUp (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActVolReset (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActLoveSong (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActBanSong (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActTempBanSong (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActDebug (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActQuit (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActSkipSong (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActDeleteStation (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActBookmark (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActSettings (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiDoPandoraDisconnect (BarApp_t *app, const char *reason,
		const char *resume_station_id_override);
bool BarUiDoPandoraReconnect (BarApp_t *app,
		const char *resume_station_id_override);
bool BarSessionTryAutoRecover (BarApp_t *app, const char *reason,
		const char *resume_station_id_override);
bool BarUiSwitchStationById (BarApp_t *app, const char *stationId);
bool BarTransformIfShared (BarApp_t *app, PianoStation_t *station);
bool BarWsTransformIfShared (BarApp_t *app, PianoStation_t *station);

static char *last_broadcast_msg = NULL;
static PianoStation_t *g_mock_reconnect_stations = NULL;

static void
mock_broadcast (const char *message, size_t len)
{
	free (last_broadcast_msg);
	last_broadcast_msg = malloc (len + 1);
	memcpy (last_broadcast_msg, message, len);
	last_broadcast_msg[len] = '\0';
}

/* Latest frame a capture client received, after running the service-thread
 * side when a capture is attached (frames are written only from writable
 * callbacks) */
static const char *
last_broadcast (void)
{
	if (test_ws_cap.ctx != NULL) {
		TestWsCaptureFlush ();
	}
	return last_broadcast_msg;
}

static void
clear_mock (void)
{
	free (last_broadcast_msg);
	last_broadcast_msg = NULL;
}

static void
rl_input_from_fd (BarReadlineFds_t *input, int read_fd)
{
	memset (input, 0, sizeof (*input));
	FD_ZERO (&input->set);
	FD_SET (read_fd, &input->set);
	input->fds[0] = read_fd;
	input->fds[1] = -1;
	input->maxfd = read_fd + 1;
}

static void
rl_write_input (int write_fd, const char *data)
{
	ck_assert (write (write_fd, data, strlen (data)) >= 0);
	close (write_fd);
}

static void
player_primitives_init (BarApp_t *app)
{
	ck_assert_int_eq (pthread_mutex_init (&app->player.lock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.cond, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.audioCond, NULL), 0);
	ck_assert_int_eq (pthread_mutex_init (&app->player.decoderLock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.decoderCond, NULL), 0);
	app->player.settings = &app->settings;
	app->player.requestedVolume = app->settings.volume;
	app->player.soundInitialized = false;
}

static void
player_primitives_destroy (BarApp_t *app)
{
	pthread_cond_destroy (&app->player.decoderCond);
	pthread_mutex_destroy (&app->player.decoderLock);
	pthread_cond_destroy (&app->player.cond);
	pthread_cond_destroy (&app->player.audioCond);
	pthread_mutex_destroy (&app->player.lock);
}

static void
ws_context_init (BarWsContext_t *ctx)
{
	memset (ctx, 0, sizeof (*ctx));
	ctx->outbox = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (ctx->outbox);
}

static void
ws_context_destroy (BarWsContext_t *ctx)
{
	BarWsOutboxFree (ctx->outbox);
	ctx->outbox = NULL;
	TestWsPendingReset ();
}

static void
attach_ws_context_for_test (BarApp_t *app, BarWsContext_t *ctx)
{
	app->settings.uiMode = BAR_UI_MODE_WEB;
	app->wsContext = ctx;
	ws_context_init (ctx);
}

/* Copy the newest queued event with `key` into last_broadcast_msg */
static void
capture_pending_message (BarWsContext_t *ctx, BarWsKey_t key)
{
	free (last_broadcast_msg);
	last_broadcast_msg = strdup (TestWsPendingLast (ctx, key));
	ck_assert_ptr_nonnull (last_broadcast ());
}

static void
teardown_web_app_with_station (BarApp_t *app, BarWsContext_t *ctx)
{
	pthread_cond_destroy (&app->player.cond);
	pthread_mutex_destroy (&app->player.lock);
	BarStateDestroy (app);
	ws_context_destroy (ctx);
	BarSettingsDestroy (&app->settings);
}

static void
setup_web_app_with_station (BarApp_t *app, BarWsContext_t *ctx,
		PianoStation_t *station, PianoSong_t *song)
{
	memset (app, 0, sizeof (*app));
	memset (ctx, 0, sizeof (*ctx));
	/* Callers pass uninitialized stack structs; snapshots read every field. */
	memset (station, 0, sizeof (*station));
	memset (song, 0, sizeof (*song));
	BarSettingsInit (&app->settings);
	app->settings.uiMode = BAR_UI_MODE_WEB;
	app->wsContext = ctx;
	ws_context_init (ctx);
	BarStateInit (app);
	ck_assert_int_eq (pthread_mutex_init (&app->player.lock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.cond, NULL), 0);
	app->player.settings = &app->settings;
	station->id = "S1";
	station->name = "Station One";
	station->isCreator = true;
	song->stationId = station->id;
	song->title = "Test Song";
	app->ph.stations = station;
	BarStateSetCurrentStation (app, station);
}

static const char *
ws_pending_payload (BarWsContext_t *ctx, BarWsKey_t key)
{
	return TestWsPendingLast (ctx, key);
}

static bool
play_state_payload_paused (BarWsContext_t *ctx)
{
	const char *payload = ws_pending_payload (ctx, BAR_WS_KEY_NONE);
	ck_assert_int_eq (payload[0], '2');
	json_object *event = json_tokener_parse (payload + 1);
	ck_assert_ptr_nonnull (event);
	ck_assert_str_eq (json_object_get_string (json_object_array_get_idx (event, 0)),
	                  "playState");
	json_object *state = json_object_array_get_idx (event, 1);
	json_object *value = NULL;
	ck_assert (json_object_object_get_ex (state, "paused", &value));
	ck_assert_int_eq (json_object_object_length (state), 1);
	const bool paused = json_object_get_boolean (value);
	json_object_put (event);
	return paused;
}

static void
assert_play_state_payload (BarWsContext_t *ctx, bool paused)
{
	ck_assert_int_eq (play_state_payload_paused (ctx), paused);
}

START_TEST (test_ui_act_pandora_reconnect_no_credentials)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);

	ck_assert (BarL10nInit (&app.l10n, &app.settings));

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();

	BarUiActPandoraReconnect (&app, NULL, NULL, 1);

	ck_assert_ptr_nonnull (last_broadcast ());
	ck_assert (strstr (last_broadcast (), "error") != NULL);

	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
	TestWsCaptureDetach (&app);
}
END_TEST

START_TEST (test_ui_act_help_runs_dispatch_loop)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	/* All keys disabled: loop runs but prints no per-key lines */
	BarUiActHelp (&app, NULL, NULL, 1);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_ui_act_play_pause_toggle)
{
	BarApp_t app;
	BarWsContext_t ctx;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	player_primitives_init (&app);
	attach_ws_context_for_test (&app, &ctx);
	app.player.mode = PLAYER_WAITING;

	BarUiActPlay (&app, NULL, NULL, 1);
	ck_assert (app.player.doPause == false);
	ck_assert_int_eq (app.player.pauseStartTime, 0);
	assert_play_state_payload (&ctx, false);

	BarUiActPause (&app, NULL, NULL, 1);
	ck_assert (app.player.doPause == true);
	ck_assert (app.player.pauseStartTime > 0);
	assert_play_state_payload (&ctx, true);

	BarUiActPlay (&app, NULL, NULL, 1);
	ck_assert (app.player.doPause == false);
	ck_assert_int_eq (app.player.pauseStartTime, 0);
	assert_play_state_payload (&ctx, false);

	BarUiActTogglePause (&app, NULL, NULL, 1);
	ck_assert (app.player.doPause == true);
	ck_assert (app.player.pauseStartTime > 0);
	assert_play_state_payload (&ctx, true);

	BarUiActTogglePause (&app, NULL, NULL, 1);
	ck_assert (app.player.doPause == false);
	ck_assert_int_eq (app.player.pauseStartTime, 0);
	assert_play_state_payload (&ctx, false);

	ws_context_destroy (&ctx);
	player_primitives_destroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_ui_act_rejected_controls_preserve_last_broadcast)
{
	BarApp_t app = {0};
	BarWsContext_t ctx;
	BarSettingsInit (&app.settings);
	player_primitives_init (&app);
	attach_ws_context_for_test (&app, &ctx);
	app.player.mode = PLAYER_WAITING;
	BarUiActPause (&app, NULL, NULL, 1);
	char *previous = strdup (ws_pending_payload (&ctx, BAR_WS_KEY_NONE));
	ck_assert_ptr_nonnull (previous);
	app.player.doQuit = true;
	const uint64_t epoch = app.player.controlEpoch;
	BarUiActPlay (&app, NULL, NULL, 1);
	ck_assert_uint_eq (TestWsPendingTake (&ctx, BAR_WS_KEY_NONE), 0);
	BarUiActPause (&app, NULL, NULL, 1);
	ck_assert_uint_eq (TestWsPendingTake (&ctx, BAR_WS_KEY_NONE), 0);
	BarUiActTogglePause (&app, NULL, NULL, 1);
	ck_assert_uint_eq (TestWsPendingTake (&ctx, BAR_WS_KEY_NONE), 0);
	ck_assert (app.player.doPause);
	ck_assert_uint_eq (app.player.controlEpoch, epoch);
	ck_assert_str_eq (ws_pending_payload (&ctx, BAR_WS_KEY_NONE), previous);
	free (previous);
	ws_context_destroy (&ctx);
	player_primitives_destroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

/* Break caught: UI reports unpaused before starting a retained sound, or
 * pause/skip only change flags while the real sound remains started. */
START_TEST (test_ui_act_controls_complete_real_sound_before_broadcast)
{
	BarApp_t app = {0};
	BarWsContext_t ctx;
	ma_audio_buffer buffer;
	static const float samples[960] = {0};
	setenv ("PIANOBAR_TEST_NO_DEVICE", "1", 1);
	BarSettingsInit (&app.settings);
	BarPlayerInit (&app.player, &app.settings);
	ck_assert (app.player.engineInitialized);
	ma_audio_buffer_config config = ma_audio_buffer_config_init (ma_format_f32, 2, 480, samples, NULL);
	config.sampleRate = 44100;
	ck_assert_int_eq (ma_audio_buffer_init (&config, &buffer), MA_SUCCESS);
	ck_assert_int_eq (ma_sound_init_from_data_source (&app.player.engine, &buffer, 0, NULL, &app.player.sound), MA_SUCCESS);
	app.player.soundInitialized = true;
	app.player.audioState = PLAYER_AUDIO_STOPPED;
	app.player.mode = PLAYER_PLAYING;
	app.player.doPause = true;
	app.player.pauseStartTime = 123;
	attach_ws_context_for_test (&app, &ctx);
	BarUiActPlay (&app, NULL, NULL, 1);
	const bool started = ma_sound_is_playing (&app.player.sound);
	assert_play_state_payload (&ctx, false);
	BarUiActPlay (&app, NULL, NULL, 1);
	ck_assert (!app.player.audioBusy);
	BarUiActPause (&app, NULL, NULL, 1);
	const bool stopped = !ma_sound_is_playing (&app.player.sound);
	const time_t timestamp = app.player.pauseStartTime;
	assert_play_state_payload (&ctx, true);
	BarUiActPause (&app, NULL, NULL, 1);
	ck_assert_int_eq (app.player.pauseStartTime, timestamp);
	ck_assert (!app.player.audioBusy);
	BarUiActTogglePause (&app, NULL, NULL, 1);
	const bool toggled = ma_sound_is_playing (&app.player.sound);
	assert_play_state_payload (&ctx, false);
	BarUiActTogglePause (&app, NULL, NULL, 1);
	assert_play_state_payload (&ctx, true);
	BarUiActSkipSong (&app, NULL, NULL, 1);
	ck_assert (app.player.doQuit);
	ck_assert (!app.player.doPause);
	ck_assert_int_eq (app.player.pauseStartTime, 0);
	ck_assert (!app.player.audioBusy);
	ck_assert_int_eq (app.player.audioState, PLAYER_AUDIO_STOPPED);
	ck_assert (!ma_sound_is_playing (&app.player.sound));
	ws_context_destroy (&ctx);
	ck_assert (BarPlayerDestroy (&app.player));
	ma_audio_buffer_uninit (&buffer);
	BarSettingsDestroy (&app.settings);
	ck_assert_msg (started, "Play must start the actual retained sound before reporting success");
	ck_assert (stopped);
	ck_assert (toggled);
}
END_TEST

typedef struct {
	player_t *player;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool entered, release;
} UiDecoderGate;

static void *hold_ui_decoder_lock (void *data) {
	UiDecoderGate *gate = data;
	pthread_mutex_lock (&gate->player->decoderLock);
	pthread_mutex_lock (&gate->lock);
	gate->entered = true;
	pthread_cond_broadcast (&gate->cond);
	while (!gate->release) { pthread_cond_wait (&gate->cond, &gate->lock); }
	pthread_mutex_unlock (&gate->lock);
	pthread_mutex_unlock (&gate->player->decoderLock);
	return NULL;
}

static void *pause_ui_thread (void *data) {
	BarUiActPause (data, NULL, NULL, 1);
	return NULL;
}

typedef struct {
	player_t *player;
	BarPlayerAudioSnapshot snapshot;
	bool result;
} UiAudioObserver;

static void *observe_ui_audio_thread (void *data) {
	UiAudioObserver *observer = data;
	/* Observation may skip a contended player.lock; retry within the same
	 * bounded fixture interval, without changing production skip semantics. */
	for (unsigned attempt = 0; attempt < 1000 && !observer->result; ++attempt) {
		observer->result = BarPlayerGetAudioSnapshot (observer->player, &observer->snapshot);
		if (!observer->result) { usleep (1000); }
	}
	return NULL;
}

/* Break caught: a completed action's delayed broadcaster samples another
 * UI action's logical pause before that action physically stops its sound. */
START_TEST (test_ui_act_pending_pause_suppresses_delayed_play_state)
{
	BarApp_t app = {0};
	BarWsContext_t ctx;
	ma_audio_buffer buffer;
	static const float samples[960] = {0};
	setenv ("PIANOBAR_TEST_NO_DEVICE", "1", 1);
	BarSettingsInit (&app.settings);
	BarPlayerInit (&app.player, &app.settings);
	ma_audio_buffer_config config = ma_audio_buffer_config_init (ma_format_f32, 2, 480, samples, NULL);
	config.sampleRate = 44100;
	ck_assert_int_eq (ma_audio_buffer_init (&config, &buffer), MA_SUCCESS);
	ck_assert_int_eq (ma_sound_init_from_data_source (&app.player.engine, &buffer, 0, NULL, &app.player.sound), MA_SUCCESS);
	app.player.soundInitialized = true;
	app.player.audioState = PLAYER_AUDIO_STOPPED;
	app.player.mode = PLAYER_PLAYING;
	app.player.doPause = true;
	attach_ws_context_for_test (&app, &ctx);
	/* Completed action A. Invoke its delayed broadcast boundary below. */
	ck_assert (BarPlayerSetPaused (&app.player, false));
	UiDecoderGate gate = {.player = &app.player};
	pthread_mutex_init (&gate.lock, NULL);
	pthread_cond_init (&gate.cond, NULL);
	pthread_t blocker, pauser;
	ck_assert_int_eq (pthread_create (&blocker, NULL, hold_ui_decoder_lock, &gate), 0);
	pthread_mutex_lock (&gate.lock);
	while (!gate.entered) { pthread_cond_wait (&gate.cond, &gate.lock); }
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_create (&pauser, NULL, pause_ui_thread, &app), 0);
	bool pending = false;
	for (unsigned attempt = 0; attempt < 1000 && !pending; ++attempt) {
		pthread_mutex_lock (&app.player.lock);
		pending = app.player.audioBusy && app.player.doPause;
		pthread_mutex_unlock (&app.player.lock);
		if (!pending) { usleep (1000); }
	}
	ck_assert_msg (pending, "Action B must publish pause while physical stop is gated");
	ck_assert (ma_sound_is_playing (&app.player.sound));
	/* Neither the test nor the broadcaster holds an application lock. */
	BarWsBroadcastPlayState (&app);
	const bool suppressed = TestWsPendingTake (&ctx, BAR_WS_KEY_NONE) == 0;
	pthread_mutex_lock (&gate.lock);
	gate.release = true;
	pthread_cond_broadcast (&gate.cond);
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_join (blocker, NULL), 0);
	ck_assert_int_eq (pthread_join (pauser, NULL), 0);
	ck_assert (!app.player.audioBusy);
	ck_assert (!ma_sound_is_playing (&app.player.sound));
	assert_play_state_payload (&ctx, true);
	BarUiActPlay (&app, NULL, NULL, 1);
	assert_play_state_payload (&ctx, false);
	pthread_cond_destroy (&gate.cond);
	pthread_mutex_destroy (&gate.lock);
	ws_context_destroy (&ctx);
	ck_assert (BarPlayerDestroy (&app.player));
	ma_audio_buffer_uninit (&buffer);
	BarSettingsDestroy (&app.settings);
	ck_assert_msg (suppressed, "An unfinished UI pause must not be broadcast by a delayed completed action");
}
END_TEST

typedef struct {
	BarApp_t *app;
	char *message;
	uint64_t epoch;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool ready, release;
} DelayedPlayState;

static char *format_play_state_for_test (bool paused) {
	json_object *data = json_object_new_object ();
	json_object_object_add (data, "paused", json_object_new_boolean (paused));
	char *message = BarSocketIoFormatEventMessage ("playState", data);
	json_object_put (data);
	return message;
}

static void *delayed_play_state_enqueue (void *data) {
	DelayedPlayState *sender = data;
	pthread_mutex_lock (&sender->lock);
	sender->ready = true;
	pthread_cond_broadcast (&sender->cond);
	while (!sender->release) { pthread_cond_wait (&sender->cond, &sender->lock); }
	pthread_mutex_unlock (&sender->lock);
	BarWebsocketBroadcastPlayStateMessage (sender->app, sender->message, sender->epoch);
	return NULL;
}

/* Break caught: an older completed producer overwrites a newer final UI
 * state, or a drained ingress forgets its watermark and resurrects old state. */
START_TEST (test_ui_act_delayed_play_state_cannot_replace_newer_final_event)
{
	BarApp_t app = {0};
	BarWsContext_t ctx;
	BarSettingsInit (&app.settings);
	player_primitives_init (&app);
	attach_ws_context_for_test (&app, &ctx);
	app.player.mode = PLAYER_WAITING;
	BarUiActPause (&app, NULL, NULL, 1);
	assert_play_state_payload (&ctx, true);
	pthread_mutex_lock (&app.player.lock);
	const uint64_t oldEpoch = app.player.controlEpoch;
	const bool oldPaused = app.player.doPause;
	pthread_mutex_unlock (&app.player.lock);
	DelayedPlayState sender = {.app = &app, .epoch = oldEpoch,
		.message = format_play_state_for_test (oldPaused)};
	pthread_mutex_init (&sender.lock, NULL);
	pthread_cond_init (&sender.cond, NULL);
	pthread_t oldProducer;
	ck_assert_int_eq (pthread_create (&oldProducer, NULL, delayed_play_state_enqueue, &sender), 0);
	pthread_mutex_lock (&sender.lock);
	while (!sender.ready) { pthread_cond_wait (&sender.cond, &sender.lock); }
	pthread_mutex_unlock (&sender.lock);
	BarUiActPlay (&app, NULL, NULL, 1);
	assert_play_state_payload (&ctx, false);
	pthread_mutex_lock (&sender.lock);
	sender.release = true;
	pthread_cond_broadcast (&sender.cond);
	pthread_mutex_unlock (&sender.lock);
	ck_assert_int_eq (pthread_join (oldProducer, NULL), 0);
	const bool oldIgnored = !play_state_payload_paused (&ctx);
	BarUiActTogglePause (&app, NULL, NULL, 1);
	assert_play_state_payload (&ctx, true);
	/* Model the service consuming the latest event; watermark must survive. */
	TestWsPendingTake (&ctx, BAR_WS_KEY_NONE);
	BarWebsocketBroadcastPlayStateMessage (&app, format_play_state_for_test (oldPaused), oldEpoch);
	const bool drainedOldIgnored = TestWsPendingTake (&ctx, BAR_WS_KEY_NONE) == 0;
	BarWsBroadcastPlayState (&app);
	assert_play_state_payload (&ctx, true);
	pthread_cond_destroy (&sender.cond);
	pthread_mutex_destroy (&sender.lock);
	ws_context_destroy (&ctx);
	player_primitives_destroy (&app);
	BarSettingsDestroy (&app.settings);
	ck_assert_msg (oldIgnored, "A delayed older pause event must not replace the newer completed play event");
	ck_assert_msg (drainedOldIgnored, "A drained ingress must still reject older play-state epochs");
}
END_TEST

/* Break caught: an unrelated observer claiming audio after helper completion
 * suppresses the only final UI event and leaves clients in the old pause. */
START_TEST (test_ui_act_completed_state_survives_unrelated_audio_owner)
{
	BarApp_t app = {0};
	BarWsContext_t ctx;
	ma_audio_buffer buffer;
	static const float samples[960] = {0};
	setenv ("PIANOBAR_TEST_NO_DEVICE", "1", 1);
	BarSettingsInit (&app.settings);
	BarPlayerInit (&app.player, &app.settings);
	ma_audio_buffer_config config = ma_audio_buffer_config_init (ma_format_f32, 2, 480, samples, NULL);
	config.sampleRate = 44100;
	ck_assert_int_eq (ma_audio_buffer_init (&config, &buffer), MA_SUCCESS);
	ck_assert_int_eq (ma_sound_init_from_data_source (&app.player.engine, &buffer, 0, NULL, &app.player.sound), MA_SUCCESS);
	app.player.soundInitialized = true;
	app.player.audioState = PLAYER_AUDIO_STOPPED;
	app.player.mode = PLAYER_PLAYING;
	app.player.doPause = true;
	attach_ws_context_for_test (&app, &ctx);
	BarUiActPause (&app, NULL, NULL, 1);
	assert_play_state_payload (&ctx, true);
	/* The actual UI helper/broadcast boundary; observer claims in between. */
	BarPlayerPlayStateSnapshot completed;
	ck_assert (BarPlayerSetPausedWithSnapshot (&app.player, false, &completed));
	UiDecoderGate gate = {.player = &app.player};
	pthread_mutex_init (&gate.lock, NULL);
	pthread_cond_init (&gate.cond, NULL);
	pthread_t blocker, reader;
	ck_assert_int_eq (pthread_create (&blocker, NULL, hold_ui_decoder_lock, &gate), 0);
	pthread_mutex_lock (&gate.lock);
	while (!gate.entered) { pthread_cond_wait (&gate.cond, &gate.lock); }
	pthread_mutex_unlock (&gate.lock);
	UiAudioObserver observer = {.player = &app.player};
	ck_assert_int_eq (pthread_create (&reader, NULL, observe_ui_audio_thread, &observer), 0);
	bool owned = false;
	for (unsigned attempt = 0; attempt < 1000 && !owned; ++attempt) {
		pthread_mutex_lock (&app.player.lock);
		owned = app.player.audioBusy && app.player.audioOwnerValid &&
			pthread_equal (app.player.audioOwner, reader);
		pthread_mutex_unlock (&app.player.lock);
		if (!owned) { usleep (1000); }
	}
	ck_assert_msg (owned, "An actual unrelated observer must own audio between completion and publication");
	BarWsBroadcastPlayStateSnapshot (&app, &completed);
	const bool newestPublished = !play_state_payload_paused (&ctx);
	pthread_mutex_lock (&gate.lock);
	gate.release = true;
	pthread_cond_broadcast (&gate.cond);
	pthread_mutex_unlock (&gate.lock);
	ck_assert_int_eq (pthread_join (blocker, NULL), 0);
	ck_assert_int_eq (pthread_join (reader, NULL), 0);
	ck_assert (observer.result && observer.snapshot.playing);
	ck_assert (!app.player.audioBusy);
	BarUiActPause (&app, NULL, NULL, 1);
	assert_play_state_payload (&ctx, true);
	/* Immutable handoff still cannot replace a newer completed control. */
	BarWsBroadcastPlayStateSnapshot (&app, &completed);
	assert_play_state_payload (&ctx, true);
	pthread_cond_destroy (&gate.cond);
	pthread_mutex_destroy (&gate.lock);
	ws_context_destroy (&ctx);
	ck_assert (BarPlayerDestroy (&app.player));
	ma_audio_buffer_uninit (&buffer);
	BarSettingsDestroy (&app.settings);
	ck_assert_msg (newestPublished, "An unrelated audio owner must not suppress the completed newest UI play state");
}
END_TEST

START_TEST (test_ui_act_volume_player_mode)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.volumeMode = BAR_VOLUME_MODE_PLAYER;
	app.settings.volume = 50;
	player_primitives_init (&app);

	BarUiActVolDown (&app, NULL, NULL, 1);
	ck_assert_int_eq (app.settings.volume, 49);

	BarUiActVolUp (&app, NULL, NULL, 1);
	ck_assert_int_eq (app.settings.volume, 50);

	BarUiActVolReset (&app, NULL, NULL, 1);
	ck_assert_int_eq (app.settings.volume, DEFAULT_VOLUME_PERCENT);

	player_primitives_destroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

typedef struct { BarApp_t *app; bool websocket; } UiVolumeUpdate;
static void *ui_concurrent_volume (void *data) {
	UiVolumeUpdate *update = data;
	for (unsigned i = 0; i < 200; ++i) {
		if (update->websocket) {
			json_object *payload = json_object_new_object ();
			json_object_object_add (payload, "volume", json_object_new_int (72));
			BarSocketIoHandleAction (update->app, "volume.set", payload, BAR_WS_DEST_BROADCAST);
			json_object_put (payload);
		} else {
			BarUiActVolDown (update->app, NULL, NULL, 1);
			BarUiActVolUp (update->app, NULL, NULL, 1);
		}
	}
	return NULL;
}

/* Break caught: real CLI and WebSocket controls bypass reservation serialization. */
START_TEST (test_ui_act_cli_and_websocket_volume_updates_share_runtime_value)
{
	BarApp_t app = {0};
	BarWsContext_t ctx;
	BarSettingsInit (&app.settings);
	app.settings.volumeMode = BAR_VOLUME_MODE_PLAYER;
	player_primitives_init (&app);
	attach_ws_context_for_test (&app, &ctx);
	pthread_mutex_init (&ctx.volumeBroadcastMutex, NULL);
	UiVolumeUpdate cli = {.app = &app}, websocket = {.app = &app, .websocket = true};
	pthread_t cliThread, websocketThread;
	ck_assert_int_eq (pthread_create (&cliThread, NULL, ui_concurrent_volume, &cli), 0);
	ck_assert_int_eq (pthread_create (&websocketThread, NULL, ui_concurrent_volume, &websocket), 0);
	ck_assert_int_eq (pthread_join (cliThread, NULL), 0);
	ck_assert_int_eq (pthread_join (websocketThread, NULL), 0);
	const int value = BarPlayerGetVolume (&app.player);
	ck_assert (value == 72 || value == 73);
	ck_assert_int_eq (app.settings.volume, value);
	ck_assert (!app.player.audioBusy && app.player.audioControlWaiters == 0);
	BarUiActVolReset (&app, NULL, NULL, 1);
	ck_assert_int_eq (BarPlayerGetVolume (&app.player), DEFAULT_VOLUME_PERCENT);
	ck_assert_int_eq (app.settings.volume, DEFAULT_VOLUME_PERCENT);
	pthread_mutex_destroy (&ctx.volumeBroadcastMutex);
	ws_context_destroy (&ctx);
	player_primitives_destroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_ui_act_debug_prints_fields)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);

	static char empty[] = "";
	PianoSong_t song;
	memset (&song, 0, sizeof (song));
	song.artist = empty;
	song.stationId = empty;
	song.album = empty;
	song.audioUrl = empty;
	song.coverArt = empty;
	song.musicId = empty;
	song.title = empty;
	song.feedbackId = empty;
	song.detailUrl = empty;
	song.trackToken = empty;
	song.seedId = empty;
	song.fileGain = 0.0f;
	song.rating = PIANO_RATE_NONE;
	song.audioFormat = PIANO_AF_UNKNOWN;

	BarUiActDebug (&app, NULL, &song, 1);

	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_ui_act_skip_song_sets_do_quit)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	player_primitives_init (&app);

	BarUiActSkipSong (&app, NULL, NULL, 1);
	ck_assert (app.player.doQuit == true);

	player_primitives_destroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_ui_act_quit)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	player_primitives_init (&app);

	BarUiActQuit (&app, NULL, NULL, 1);
	ck_assert (app.doQuit == true);
	ck_assert (app.player.doQuit == true);

	player_primitives_destroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_disconnect_player_wait_returns_fast_when_already_dead)
{
	/* Verify that BarPlayerWaitForMode (the condvar-based replacement for the
	 * old busy-wait loop) returns immediately when the player is already in
	 * the requested mode.  This ensures the stop path does not busy-spin for
	 * up to 10 s (100 * usleep(100ms)). */
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	player_primitives_init (&app);
	app.player.mode = PLAYER_DEAD;

	struct timespec t0, t1;
	clock_gettime (CLOCK_MONOTONIC, &t0);
	bool ok = BarPlayerWaitForMode (&app.player, PLAYER_DEAD,
	                                 BAR_PLAYER_STOP_TIMEOUT_MS);
	clock_gettime (CLOCK_MONOTONIC, &t1);
	long ms = (t1.tv_sec - t0.tv_sec) * 1000L
	          + (t1.tv_nsec - t0.tv_nsec) / 1000000L;
	ck_assert (ok);
	/* Old busy-wait: up to 10 s; condvar returns immediately */
	ck_assert_int_lt (ms, 100);

	player_primitives_destroy (&app);
}
END_TEST

START_TEST (test_transform_owned_station_skips_pandora_call)
{
	BarApp_t app;
	PianoStation_t station;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	BarSettingsInit (&app.settings);
	station.isCreator = true;

	ck_assert (BarTransformIfShared (&app, &station));
	ck_assert (BarWsTransformIfShared (&app, &station));

	BarSettingsDestroy (&app.settings);
}
END_TEST

static void
read_default_settings (BarSettings_t *settings)
{
	char tmpl[512];
	ck_assert_ptr_nonnull (TestTmpMkdtemp (tmpl, sizeof (tmpl), "piano_ui"));
	char sub[512];
	snprintf (sub, sizeof (sub), "%s/pianobar", tmpl);
	ck_assert_int_eq (mkdir (sub, 0700), 0);
	char cfg[512];
	snprintf (cfg, sizeof (cfg), "%s/config", sub);
	FILE *f = fopen (cfg, "w");
	ck_assert_ptr_nonnull (f);
	ck_assert_int_ge (fprintf (f, "user = testuser\npassword = testpass\n"), 30);
	fclose (f);

	ck_assert_int_eq (setenv ("HOME", tmpl, 1), 0);
	ck_assert_int_eq (setenv ("XDG_CONFIG_HOME", tmpl, 1), 0);
	BarSettingsInit (settings);
	BarSettingsRead (settings);
}

START_TEST (test_ui_do_pandora_disconnect_when_player_already_dead)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t station;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	read_default_settings (&app.settings);
	attach_ws_context_for_test (&app, &ctx);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	ck_assert_int_eq (pthread_mutex_init (&app.pianoHttpMutex, NULL), 0);
	BarStateInit (&app);
	player_primitives_init (&app);
	app.player.mode = PLAYER_DEAD;
	station.id = "resume-station";
	BarStateSetCurrentStation (&app, &station);

	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
	                            app.settings.partnerPassword, app.settings.device,
	                            app.settings.inkey, app.settings.outkey),
	                  PIANO_RET_OK);

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();

	BarUiDoPandoraDisconnect (&app, "user", NULL);
	capture_pending_message (&ctx, BAR_WS_KEY_NONE);

	ck_assert_ptr_nonnull (app.lastStationId);
	ck_assert_str_eq (app.lastStationId, "resume-station");
	ck_assert_ptr_nonnull (last_broadcast ());
	ck_assert (strstr (last_broadcast (), "pandora.disconnected") != NULL);
	ck_assert (strstr (last_broadcast (), "user") != NULL);

	free (app.lastStationId);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	TestWsCaptureDetach (&app);
	ws_context_destroy (&ctx);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
}
END_TEST

START_TEST (test_ui_do_pandora_disconnect_idle_timeout_message)
{
	BarApp_t app;
	BarWsContext_t ctx;
	memset (&app, 0, sizeof (app));
	read_default_settings (&app.settings);
	attach_ws_context_for_test (&app, &ctx);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	ck_assert_int_eq (pthread_mutex_init (&app.pianoHttpMutex, NULL), 0);
	BarStateInit (&app);
	player_primitives_init (&app);
	app.player.mode = PLAYER_DEAD;
	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
	                            app.settings.partnerPassword, app.settings.device,
	                            app.settings.inkey, app.settings.outkey),
	                  PIANO_RET_OK);

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();

	BarUiDoPandoraDisconnect (&app, "idle_timeout", "override-station");
	capture_pending_message (&ctx, BAR_WS_KEY_NONE);

	ck_assert_ptr_nonnull (app.lastStationId);
	ck_assert_str_eq (app.lastStationId, "override-station");
	ck_assert (strstr (last_broadcast (), "idle_timeout") != NULL);

	free (app.lastStationId);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	TestWsCaptureDetach (&app);
	ws_context_destroy (&ctx);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
}
END_TEST

START_TEST (test_ui_do_pandora_disconnect_clears_playback_state_before_destroy)
{
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t *song;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	read_default_settings (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	ck_assert_int_eq (pthread_mutex_init (&app.pianoHttpMutex, NULL), 0);
	BarStateInit (&app);
	player_primitives_init (&app);
	app.player.mode = PLAYER_DEAD;
	station.id = "resume-station";
	station.name = "Resume";
	song = calloc (1, sizeof (*song));
	ck_assert_ptr_nonnull (song);
	song->title = strdup ("Queued");
	ck_assert_ptr_nonnull (song->title);
	BarStateSetCurrentStation (&app, &station);
	BarStateSetNextStation (&app, &station);
	BarStateSetPlaylist (&app, song);

	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
	                            app.settings.partnerPassword, app.settings.device,
	                            app.settings.inkey, app.settings.outkey),
	                  PIANO_RET_OK);

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();

	BarUiDoPandoraDisconnect (&app, "user", NULL);

	ck_assert_ptr_null (BarStateGetNextStation (&app));
	ck_assert_ptr_null (BarStateGetPlaylist (&app));
	ck_assert_ptr_null (BarStateGetCurrentStation (&app));
	ck_assert_ptr_nonnull (app.lastStationId);
	ck_assert_str_eq (app.lastStationId, "resume-station");

	free (app.lastStationId);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
	TestWsCaptureDetach (&app);
}
END_TEST

static bool
mock_reconnect_login_stations (BarApp_t *app, const PianoRequestType_t type, void *data,
                               PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) data;
	if (type == PIANO_REQUEST_LOGIN) {
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	if (type == PIANO_REQUEST_GET_STATIONS) {
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		if (g_mock_reconnect_stations != NULL) {
			app->ph.stations = g_mock_reconnect_stations;
		}
		return true;
	}
	return false;
}

START_TEST (test_ui_act_pandora_reconnect_clears_playback_state)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t station;
	PianoSong_t *song;
	memset (&app, 0, sizeof (app));
	memset (&ctx, 0, sizeof (ctx));
	memset (&station, 0, sizeof (station));
	read_default_settings (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.wsContext = &ctx;
	ws_context_init (&ctx);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	ck_assert_int_eq (pthread_mutex_init (&app.pianoHttpMutex, NULL), 0);
	BarStateInit (&app);
	player_primitives_init (&app);
	app.player.mode = PLAYER_DEAD;
	station.id = "resume-station";
	station.name = "Resume";
	song = calloc (1, sizeof (*song));
	ck_assert_ptr_nonnull (song);
	song->title = strdup ("Queued");
	ck_assert_ptr_nonnull (song->title);
	BarStateSetCurrentStation (&app, &station);
	BarStateSetNextStation (&app, &station);
	BarStateSetPlaylist (&app, song);

	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
	                            app.settings.partnerPassword, app.settings.device,
	                            app.settings.inkey, app.settings.outkey),
	                  PIANO_RET_OK);

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();
	g_mock_reconnect_stations = &station;

	BarUiPianoCallSetTestHook (mock_reconnect_login_stations);
	BarUiActPandoraReconnect (&app, NULL, NULL, 1);
	BarUiPianoCallClearTestHook ();
	g_mock_reconnect_stations = NULL;

	ck_assert_ptr_eq (BarStateGetNextStation (&app), &station);
	ck_assert_ptr_null (app.lastStationId);
	g_mock_reconnect_stations = NULL;

	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	TestWsCaptureDetach (&app);
	ws_context_destroy (&ctx);
	BarStateDestroy (&app);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
}
END_TEST

/* Auto-recovery must not undo a stop the user (or the idle timer) asked for */
START_TEST (test_session_auto_recover_skips_intentional_stops)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);

	ck_assert (!BarSessionTryAutoRecover (&app, "user", NULL));
	ck_assert (!BarSessionTryAutoRecover (&app, "idle_timeout", NULL));
	ck_assert (!atomic_load (&app.autoRecoverFailed));

	BarSettingsDestroy (&app.settings);
}
END_TEST

/* Shutting down: there is nothing to resume into */
START_TEST (test_session_auto_recover_skips_while_quitting)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.doQuit = true;

	ck_assert (!BarSessionTryAutoRecover (&app, "playlist_session_error", NULL));

	BarSettingsDestroy (&app.settings);
}
END_TEST

/* One attempt per session death, whether the last one failed or is running */
START_TEST (test_session_auto_recover_attempts_once_per_outage)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);

	atomic_store (&app.autoRecoverFailed, true);
	ck_assert (!BarSessionTryAutoRecover (&app, "playlist_session_error", NULL));

	atomic_store (&app.autoRecoverFailed, false);
	atomic_store (&app.autoRecoverInFlight, true);
	ck_assert (!BarSessionTryAutoRecover (&app, "playlist_session_error", NULL));

	BarSettingsDestroy (&app.settings);
}
END_TEST

/* A recovered session resumes the station and stays silent about disconnects */
START_TEST (test_session_auto_recover_resumes_without_disconnect_event)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t station;
	memset (&app, 0, sizeof (app));
	memset (&ctx, 0, sizeof (ctx));
	memset (&station, 0, sizeof (station));
	read_default_settings (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.wsContext = &ctx;
	ws_context_init (&ctx);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	ck_assert_int_eq (pthread_mutex_init (&app.pianoHttpMutex, NULL), 0);
	BarStateInit (&app);
	player_primitives_init (&app);
	app.player.mode = PLAYER_DEAD;
	station.id = "resume-station";
	station.name = "Resume";

	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
	                            app.settings.partnerPassword, app.settings.device,
	                            app.settings.inkey, app.settings.outkey),
	                  PIANO_RET_OK);

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();
	g_mock_reconnect_stations = &station;
	BarUiPianoCallSetTestHook (mock_reconnect_login_stations);

	ck_assert (BarSessionTryAutoRecover (&app, "playlist_session_error",
			"resume-station"));

	BarUiPianoCallClearTestHook ();
	g_mock_reconnect_stations = NULL;

	ck_assert_ptr_eq (BarStateGetNextStation (&app), &station);
	ck_assert_ptr_null (app.lastStationId);
	ck_assert (!atomic_load (&app.autoRecoverFailed));
	ck_assert (!atomic_load (&app.autoRecoverInFlight));
	/* No pandora.disconnected event was queued or delivered */
	TestWsCaptureFlush ();
	ck_assert_uint_eq (TestWsCaptureCount (0, "pandora.disconnected"), 0);

	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	TestWsCaptureDetach (&app);
	ws_context_destroy (&ctx);
	BarStateDestroy (&app);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
}
END_TEST

static bool
mock_piano_call_fails (BarApp_t *app, const PianoRequestType_t type, void *data,
                       PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	(void) type;
	(void) data;
	*pRet = PIANO_RET_P_INTERNAL;
	*wRet = CURLE_OK;
	return false;
}

/* A failed reconnect reports the failure and blocks further attempts */
START_TEST (test_session_auto_recover_failure_is_sticky)
{
	BarApp_t app;
	BarWsContext_t ctx;
	memset (&app, 0, sizeof (app));
	memset (&ctx, 0, sizeof (ctx));
	read_default_settings (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.wsContext = &ctx;
	ws_context_init (&ctx);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	ck_assert_int_eq (pthread_mutex_init (&app.pianoHttpMutex, NULL), 0);
	BarStateInit (&app);
	player_primitives_init (&app);
	app.player.mode = PLAYER_DEAD;

	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
	                            app.settings.partnerPassword, app.settings.device,
	                            app.settings.inkey, app.settings.outkey),
	                  PIANO_RET_OK);

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();
	BarUiPianoCallSetTestHook (mock_piano_call_fails);

	ck_assert (!BarSessionTryAutoRecover (&app, "playlist_session_error", NULL));
	ck_assert (atomic_load (&app.autoRecoverFailed));
	ck_assert (!atomic_load (&app.autoRecoverInFlight));

	BarUiPianoCallClearTestHook ();

	free (app.lastStationId);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	TestWsCaptureDetach (&app);
	ws_context_destroy (&ctx);
	BarStateDestroy (&app);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
}
END_TEST

static bool
mock_wait_parked_idle_fail (const BarApp_t *app, unsigned int timeoutMs)
{
	(void) app;
	(void) timeoutMs;
	return false;
}

START_TEST (test_ui_do_pandora_disconnect_logs_when_manager_not_parked)
{
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t *song;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	read_default_settings (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	ck_assert_int_eq (pthread_mutex_init (&app.pianoHttpMutex, NULL), 0);
	BarStateInit (&app);
	player_primitives_init (&app);
	app.player.mode = PLAYER_DEAD;
	station.id = "resume-station";
	station.name = "Resume";
	song = calloc (1, sizeof (*song));
	ck_assert_ptr_nonnull (song);
	song->title = strdup ("Queued");
	BarStateSetCurrentStation (&app, &station);
	BarStateSetNextStation (&app, &station);
	BarStateSetPlaylist (&app, song);

	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
	                            app.settings.partnerPassword, app.settings.device,
	                            app.settings.inkey, app.settings.outkey),
	                  PIANO_RET_OK);

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();
	log_set_debug_mask (DEBUG_UI);
	BarPlaybackManagerWaitParkedIdleSetTestHook (mock_wait_parked_idle_fail);

	BarUiDoPandoraDisconnect (&app, "user", NULL);

	BarPlaybackManagerWaitParkedIdleClearTestHook ();
	log_set_debug_mask (0);
	g_mock_reconnect_stations = NULL;

	ck_assert_ptr_null (BarStateGetNextStation (&app));
	ck_assert_ptr_null (BarStateGetPlaylist (&app));

	free (app.lastStationId);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
	TestWsCaptureDetach (&app);
}
END_TEST

START_TEST (test_ui_act_pandora_reconnect_logs_when_manager_not_parked)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t station;
	PianoSong_t *song;
	memset (&app, 0, sizeof (app));
	memset (&ctx, 0, sizeof (ctx));
	memset (&station, 0, sizeof (station));
	read_default_settings (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.wsContext = &ctx;
	ws_context_init (&ctx);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	ck_assert_int_eq (pthread_mutex_init (&app.pianoHttpMutex, NULL), 0);
	BarStateInit (&app);
	player_primitives_init (&app);
	app.player.mode = PLAYER_DEAD;
	station.id = "resume-station";
	station.name = "Resume";
	song = calloc (1, sizeof (*song));
	ck_assert_ptr_nonnull (song);
	song->title = strdup ("Queued");
	BarStateSetCurrentStation (&app, &station);
	BarStateSetNextStation (&app, &station);
	BarStateSetPlaylist (&app, song);

	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
	                            app.settings.partnerPassword, app.settings.device,
	                            app.settings.inkey, app.settings.outkey),
	                  PIANO_RET_OK);

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();
	log_set_debug_mask (DEBUG_UI);
	g_mock_reconnect_stations = &station;
	BarPlaybackManagerWaitParkedIdleSetTestHook (mock_wait_parked_idle_fail);
	BarUiPianoCallSetTestHook (mock_reconnect_login_stations);

	BarUiActPandoraReconnect (&app, NULL, NULL, 1);

	BarUiPianoCallClearTestHook ();
	BarPlaybackManagerWaitParkedIdleClearTestHook ();
	log_set_debug_mask (0);
	g_mock_reconnect_stations = NULL;

	ck_assert_ptr_eq (BarStateGetNextStation (&app), &station);
	ck_assert_ptr_null (app.lastStationId);

	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	TestWsCaptureDetach (&app);
	ws_context_destroy (&ctx);
	BarStateDestroy (&app);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
}
END_TEST

START_TEST (test_ui_do_pandora_disconnect_waits_for_running_manager)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	read_default_settings (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	ck_assert_int_eq (pthread_mutex_init (&app.pianoHttpMutex, NULL), 0);
	BarStateInit (&app);
	player_primitives_init (&app);
	app.player.mode = PLAYER_DEAD;

	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
	                            app.settings.partnerPassword, app.settings.device,
	                            app.settings.inkey, app.settings.outkey),
	                  PIANO_RET_OK);

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();
	BarPlaybackManagerWaitParkedIdleClearTestHook ();
	ck_assert (BarPlaybackManagerStart (&app));
	usleep (50000);

	BarUiDoPandoraDisconnect (&app, "user", NULL);

	BarPlaybackManagerStop (&app);

	free (app.lastStationId);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
	TestWsCaptureDetach (&app);
}
END_TEST

START_TEST (test_ui_do_pandora_disconnect_waits_for_parked_manager)
{
	BarApp_t app;
	PianoStation_t station;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	read_default_settings (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	ck_assert_int_eq (pthread_mutex_init (&app.pianoHttpMutex, NULL), 0);
	BarStateInit (&app);
	player_primitives_init (&app);
	app.player.mode = PLAYER_PLAYING;
	station.id = "park-timeout";
	BarStateSetNextStation (&app, &station);

	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
	                            app.settings.partnerPassword, app.settings.device,
	                            app.settings.inkey, app.settings.outkey),
	                  PIANO_RET_OK);

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();
	log_set_debug_mask (DEBUG_UI);

	ck_assert (BarPlaybackManagerStart (&app));
	usleep (50000);
	ck_assert (!BarPlaybackManagerWaitParkedIdle (&app, BAR_PLAYER_STOP_POLL_MS));
	BarPlaybackManagerStop (&app);

	log_set_debug_mask (0);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
	TestWsCaptureDetach (&app);
}
END_TEST

/* Formerly through BarUiSwitchStation (pointer API, removed: no production
 * caller); the same behaviour through the production switch by ID */
START_TEST (test_ui_switch_station_sets_next_and_drains_playlist)
{
	BarApp_t app;
	PianoStation_t from, to;
	PianoSong_t song;
	PianoSong_t *tail;
	memset (&app, 0, sizeof (app));
	memset (&from, 0, sizeof (from));
	memset (&to, 0, sizeof (to));
	memset (&song, 0, sizeof (song));
	tail = calloc (1, sizeof (*tail));
	ck_assert_ptr_nonnull (tail);
	BarSettingsInit (&app.settings);
	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);
	player_primitives_init (&app);
	from.id = "from"; from.name = "From";
	to.id = "to"; to.name = "To";
	song.head.next = &tail->head;
	app.curStation = &from;
	app.playlist = &song;
	app.ph.stations = &to;

	ck_assert (BarUiSwitchStationById (&app, "to"));

	ck_assert_ptr_eq (BarStateGetNextStation (&app), &to);
	{
		PianoSong_t *pl = BarStateGetPlaylist (&app);
		ck_assert_ptr_nonnull (pl);
		ck_assert_ptr_null (pl->head.next);
	}

	player_primitives_destroy (&app);
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_ui_switch_station_by_id_missing_keeps_playlist)
{
	BarApp_t app;
	PianoStation_t from;
	PianoSong_t song;
	PianoSong_t *tail;
	memset (&app, 0, sizeof (app));
	memset (&from, 0, sizeof (from));
	memset (&song, 0, sizeof (song));
	tail = calloc (1, sizeof (*tail));
	ck_assert_ptr_nonnull (tail);
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_BOTH;
	BarStateInit (&app);
	from.id = "from"; from.name = "From";
	song.head.next = &tail->head;
	app.ph.stations = &from;
	app.playlist = &song;

	ck_assert (!BarUiSwitchStationById (&app, "missing"));

	ck_assert_ptr_null (BarStateGetNextStation (&app));
	ck_assert_ptr_eq (BarStateGetPlaylist (&app), &song);
	ck_assert_ptr_eq (song.head.next, &tail->head);

	free (tail);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_ui_switch_station_by_id_null_returns_false)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));

	ck_assert (!BarUiSwitchStationById (&app, NULL));
}
END_TEST

START_TEST (test_ui_switch_station_by_id_found_sets_next)
{
	BarApp_t app;
	PianoStation_t station;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_BOTH;
	BarStateInit (&app);
	player_primitives_init (&app);
	station.id = "live";
	station.name = "Live";
	app.ph.stations = &station;

	ck_assert (BarUiSwitchStationById (&app, "live"));
	ck_assert_ptr_eq (BarStateGetNextStation (&app), &station);

	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_ui_act_song_info_quickmix_resolves_child_station)
{
	BarApp_t app;
	PianoStation_t quickmix, child;
	PianoSong_t song;
	memset (&app, 0, sizeof (app));
	memset (&quickmix, 0, sizeof (quickmix));
	memset (&child, 0, sizeof (child));
	memset (&song, 0, sizeof (song));
	BarSettingsInit (&app.settings);
	free (app.settings.npSongFormat);
	app.settings.npSongFormat = strdup ("%t");
	free (app.settings.npStationFormat);
	app.settings.npStationFormat = strdup ("%n");
	app.settings.loveIcon = strdup ("+");
	app.settings.banIcon = strdup ("-");
	app.settings.tiredIcon = strdup ("t");
	app.settings.atIcon = strdup ("@");
	BarStateInit (&app);
	quickmix.id = "qm";
	quickmix.name = "QuickMix";
	quickmix.isQuickMix = true;
	child.id = "child";
	child.name = "Child Station";
	song.title = "Song";
	song.artist = "Artist";
	song.album = "Album";
	song.detailUrl = "";
	song.rating = PIANO_RATE_NONE;
	song.stationId = child.id;
	app.ph.stations = &child;

	BarUiActSongInfo (&app, &quickmix, &song, 1);

	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_ui_act_print_upcoming_lists_and_broadcasts)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t station;
	PianoSong_t cur, next;
	memset (&app, 0, sizeof (app));
	memset (&ctx, 0, sizeof (ctx));
	memset (&station, 0, sizeof (station));
	memset (&cur, 0, sizeof (cur));
	memset (&next, 0, sizeof (next));
	BarSettingsInit (&app.settings);
	app.wsContext = &ctx;
	cur.title = "Current";
	cur.head.next = &next.head;
	next.title = "Next Song";
	next.artist = "Next Artist";
	next.stationId = "s1";
	next.length = 180;
	station.id = "s1";
	station.name = "Station One";
	app.ph.stations = &station;
	BarStateInit (&app);
	BarStateSetCurrentStation (&app, &station);

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();

	app.playlist = &cur;
	BarUiActPrintUpcomingTo (&app, test_ws_cap.ids[0]);

	ck_assert_ptr_nonnull (last_broadcast ());
	ck_assert (strstr (last_broadcast (), "query.upcoming.result") != NULL);

	TestWsCaptureDetach (&app);
	clear_mock ();
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_ui_act_print_upcoming_empty_queue_cli_message)
{
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t cur;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	memset (&cur, 0, sizeof (cur));
	BarSettingsInit (&app.settings);
	cur.title = "Only";

	BarUiActPrintUpcoming (&app, &station, &cur, 1);

	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_ui_act_history_disabled)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.history = 0;

	BarUiActHistory (&app, NULL, NULL, 1);

	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_ui_act_pandora_disconnect_delegates_to_helper)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t station;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	read_default_settings (&app.settings);
	attach_ws_context_for_test (&app, &ctx);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	ck_assert_int_eq (pthread_mutex_init (&app.pianoHttpMutex, NULL), 0);
	BarStateInit (&app);
	player_primitives_init (&app);
	app.player.mode = PLAYER_DEAD;
	station.id = "stop-station";
	BarStateSetCurrentStation (&app, &station);
	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
	                            app.settings.partnerPassword, app.settings.device,
	                            app.settings.inkey, app.settings.outkey),
	                  PIANO_RET_OK);

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();

	BarUiActPandoraDisconnect (&app, NULL, NULL, 1);
	capture_pending_message (&ctx, BAR_WS_KEY_NONE);

	ck_assert_ptr_nonnull (last_broadcast ());
	ck_assert (strstr (last_broadcast (), "pandora.disconnected") != NULL);

	free (app.lastStationId);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	TestWsCaptureDetach (&app);
	ws_context_destroy (&ctx);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
}
END_TEST

static bool
mock_transform_station (BarApp_t *app, const PianoRequestType_t type, void *data,
                        PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	(void) data;
	if (type == PIANO_REQUEST_TRANSFORM_STATION) {
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_transform_shared_station_invokes_transform_request)
{
	BarApp_t app;
	PianoStation_t station;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	BarSettingsInit (&app.settings);
	station.isCreator = false;

	BarUiPianoCallSetTestHook (mock_transform_station);
	ck_assert (BarTransformIfShared (&app, &station));
	ck_assert (BarWsTransformIfShared (&app, &station));
	BarUiPianoCallClearTestHook ();

	BarSettingsDestroy (&app.settings);
}
END_TEST

static bool
mock_explain_empty (BarApp_t *app, const PianoRequestType_t type, void *data,
                    PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	if (type == PIANO_REQUEST_EXPLAIN) {
		PianoRequestDataExplain_t *req = data;
		req->retExplain = NULL;
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_ui_act_explain_handles_empty_response)
{
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	memset (&song, 0, sizeof (song));
	BarSettingsInit (&app.settings);
	song.title = "Explain Me";

	BarUiPianoCallSetTestHook (mock_explain_empty);
	BarUiActExplain (&app, &station, &song, 1);
	BarUiPianoCallClearTestHook ();

	BarSettingsDestroy (&app.settings);
}
END_TEST

typedef struct {
	player_t *player;
} SetDeadArgs_t;

static void *
set_player_dead_thread (void *arg)
{
	SetDeadArgs_t *args = (SetDeadArgs_t *) arg;
	usleep (20000);
	BarPlayerSetMode (args->player, PLAYER_DEAD);
	return NULL;
}

START_TEST (test_ui_do_pandora_disconnect_waits_for_player_stop)
{
	BarApp_t app;
	BarWsContext_t ctx;
	pthread_t helper;
	SetDeadArgs_t args;
	memset (&app, 0, sizeof (app));
	read_default_settings (&app.settings);
	attach_ws_context_for_test (&app, &ctx);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	ck_assert_int_eq (pthread_mutex_init (&app.pianoHttpMutex, NULL), 0);
	BarStateInit (&app);
	player_primitives_init (&app);
	BarPlayerSetMode (&app.player, PLAYER_PLAYING);
	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
	                            app.settings.partnerPassword, app.settings.device,
	                            app.settings.inkey, app.settings.outkey),
	                  PIANO_RET_OK);

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();

	args.player = &app.player;
	ck_assert_int_eq (pthread_create (&helper, NULL, set_player_dead_thread, &args), 0);
	BarUiDoPandoraDisconnect (&app, "user", NULL);
	ck_assert_int_eq (pthread_join (helper, NULL), 0);
	capture_pending_message (&ctx, BAR_WS_KEY_NONE);

	ck_assert_ptr_nonnull (last_broadcast ());
	free (app.lastStationId);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	TestWsCaptureDetach (&app);
	ws_context_destroy (&ctx);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
}
END_TEST

START_TEST (test_ui_act_volume_system_mode)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	player_primitives_init (&app);
	app.settings.volumeMode = BAR_VOLUME_MODE_SYSTEM;

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();

	BarUiActVolDown (&app, NULL, NULL, 1);
	BarUiActVolUp (&app, NULL, NULL, 1);
	BarUiActVolReset (&app, NULL, NULL, 1);

	clear_mock ();
	player_primitives_destroy (&app);
	BarSettingsDestroy (&app.settings);
	TestWsCaptureDetach (&app);
}
END_TEST

static bool
mock_transform_fail (BarApp_t *app, const PianoRequestType_t type, void *data,
                     PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	(void) data;
	if (type == PIANO_REQUEST_TRANSFORM_STATION) {
		*pRet = PIANO_RET_ERR;
		*wRet = CURLE_OK;
		return false;
	}
	return false;
}

START_TEST (test_transform_shared_station_failure_logged)
{
	BarApp_t app;
	PianoStation_t station;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	BarSettingsInit (&app.settings);
	station.isCreator = false;

	BarUiPianoCallSetTestHook (mock_transform_fail);
	ck_assert (!BarTransformIfShared (&app, &station));
	ck_assert (!BarWsTransformIfShared (&app, &station));
	BarUiPianoCallClearTestHook ();

	BarSettingsDestroy (&app.settings);
}
END_TEST

static bool
mock_rate_song_ok (BarApp_t *app, const PianoRequestType_t type, void *data,
                   PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	(void) data;
	if (type == PIANO_REQUEST_RATE_SONG) {
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_ui_act_love_song_broadcasts_process)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t station;
	PianoSong_t song;
	setup_web_app_with_station (&app, &ctx, &station, &song);
	BarStateSetPlaylist (&app, &song);

	BarUiPianoCallSetTestHook (mock_rate_song_ok);
	BarUiActLoveSong (&app, &station, &song, 1);
	BarUiPianoCallClearTestHook ();

	ck_assert (strstr (ws_pending_payload (&ctx, BAR_WS_KEY_NONE), "process") != NULL);
	teardown_web_app_with_station (&app, &ctx);
}
END_TEST

START_TEST (test_ui_act_ban_song_skips_current_playlist_track)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t station;
	PianoSong_t song;
	setup_web_app_with_station (&app, &ctx, &station, &song);
	BarStateSetPlaylist (&app, &song);
	BarPlayerSetMode (&app.player, PLAYER_PLAYING);

	BarUiPianoCallSetTestHook (mock_rate_song_ok);
	BarUiActBanSong (&app, &station, &song, 1);
	BarUiPianoCallClearTestHook ();

	ck_assert (strstr (ws_pending_payload (&ctx, BAR_WS_KEY_NONE), "process") != NULL);
	teardown_web_app_with_station (&app, &ctx);
}
END_TEST

static bool
mock_tired_song_ok (BarApp_t *app, const PianoRequestType_t type, void *data,
                    PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	(void) data;
	if (type == PIANO_REQUEST_ADD_TIRED_SONG) {
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_ui_act_temp_ban_skips_current_playlist_track)
{
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	memset (&song, 0, sizeof (song));
	BarSettingsInit (&app.settings);
	player_primitives_init (&app);
	station.id = "S1";
	song.stationId = station.id;
	BarStateInit (&app);
	BarStateSetPlaylist (&app, &song);

	BarUiPianoCallSetTestHook (mock_tired_song_ok);
	BarUiActTempBanSong (&app, &station, &song, 1);
	BarUiPianoCallClearTestHook ();

	pthread_mutex_lock (&app.player.lock);
	ck_assert (app.player.doQuit);
	pthread_mutex_unlock (&app.player.lock);

	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static bool
mock_explain_fail (BarApp_t *app, const PianoRequestType_t type, void *data,
                   PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	(void) data;
	if (type == PIANO_REQUEST_EXPLAIN) {
		*pRet = PIANO_RET_ERR;
		*wRet = CURLE_OK;
		return false;
	}
	return false;
}

START_TEST (test_ui_act_explain_websocket_failure_emits_error)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t station;
	PianoSong_t song;
	setup_web_app_with_station (&app, &ctx, &station, &song);

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();
	BarUiPianoCallSetTestHook (mock_explain_fail);
	BarUiActExplainTo (&app, &station, &song, test_ws_cap.ids[0]);
	BarUiPianoCallClearTestHook ();
	TestWsCaptureFlush ();
	TestWsCaptureDetach (&app);

	ck_assert_ptr_nonnull (last_broadcast ());
	ck_assert (strstr (last_broadcast (), "error") != NULL);
	ck_assert (strstr (last_broadcast (), "song.explain") != NULL);

	clear_mock ();
	ws_context_destroy (&ctx);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static bool
mock_explain_with_text (BarApp_t *app, const PianoRequestType_t type, void *data,
                        PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	if (type == PIANO_REQUEST_EXPLAIN) {
		PianoRequestDataExplain_t *req = data;
		req->retExplain = strdup ("Because you enjoy jazz.");
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_ui_act_explain_websocket_success_broadcasts)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t station;
	PianoSong_t song;
	setup_web_app_with_station (&app, &ctx, &station, &song);

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();
	BarUiPianoCallSetTestHook (mock_explain_with_text);
	BarUiActExplainTo (&app, &station, &song, test_ws_cap.ids[0]);
	BarUiPianoCallClearTestHook ();
	TestWsCaptureFlush ();
	TestWsCaptureDetach (&app);

	ck_assert_ptr_nonnull (last_broadcast ());
	ck_assert (strstr (last_broadcast (), "Because you enjoy jazz.") != NULL);

	clear_mock ();
	ws_context_destroy (&ctx);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_ui_act_print_upcoming_websocket_empty_emits_error)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t station;
	PianoSong_t cur;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	memset (&cur, 0, sizeof (cur));
	BarSettingsInit (&app.settings);
	ws_context_init (&ctx);
	app.wsContext = &ctx;
	cur.title = "Only";

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();
	app.playlist = &cur;
	BarUiActPrintUpcomingTo (&app, test_ws_cap.ids[0]);
	TestWsCaptureFlush ();
	TestWsCaptureDetach (&app);

	ck_assert_ptr_nonnull (last_broadcast ());
	ck_assert (strstr (last_broadcast (), "error") != NULL);
	ck_assert (strstr (last_broadcast (), "query.upcoming") != NULL);

	clear_mock ();
	ws_context_destroy (&ctx);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_query_explain_reaches_only_requester)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t station;
	PianoSong_t song;
	setup_web_app_with_station (&app, &ctx, &station, &song);
	TestWsCaptureAttach (&app, 2, NULL);

	BarUiPianoCallSetTestHook (mock_explain_with_text);
	BarUiActExplainTo (&app, &station, &song, test_ws_cap.ids[1]);
	BarUiPianoCallClearTestHook ();

	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 1);
	ck_assert_int_eq (test_ws_cap.frameClient[0], 1);
	ck_assert_uint_eq (TestWsCaptureCount (1, "song.explanation"), 1);
	ck_assert_uint_eq (TestWsCaptureCount (1, "Because you enjoy jazz."), 1);

	/* Failure is reported to the requester only, too */
	TestWsCaptureClear ();
	BarUiPianoCallSetTestHook (mock_explain_fail);
	BarUiActExplainTo (&app, &station, &song, test_ws_cap.ids[0]);
	BarUiPianoCallClearTestHook ();
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 1);
	ck_assert_uint_eq (TestWsCaptureCount (0, "song.explain"), 1);
	ck_assert_uint_eq (TestWsCaptureCount (1, ""), 0);

	/* Pandora answers without an explanation: requester-only error */
	TestWsCaptureClear ();
	BarUiPianoCallSetTestHook (mock_explain_empty);
	BarUiActExplainTo (&app, &station, &song, test_ws_cap.ids[1]);
	BarUiPianoCallClearTestHook ();
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 1);
	ck_assert_uint_eq (TestWsCaptureCount (1, "song.explain"), 1);
	ck_assert_uint_eq (TestWsCaptureCount (1, "error"), 1);
	ck_assert_uint_eq (TestWsCaptureCount (0, ""), 0);

	TestWsCaptureDetach (&app);
	teardown_web_app_with_station (&app, &ctx);
}
END_TEST

START_TEST (test_query_upcoming_reaches_only_requester)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t station;
	PianoSong_t cur, next;
	setup_web_app_with_station (&app, &ctx, &station, &cur);
	memset (&next, 0, sizeof (next));
	cur.head.next = &next.head;
	next.title = "Next Song";
	next.stationId = station.id;
	TestWsCaptureAttach (&app, 2, NULL);
	app.playlist = &cur;

	BarUiActPrintUpcomingTo (&app, test_ws_cap.ids[0]);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 1);
	ck_assert_uint_eq (TestWsCaptureCount (0, "query.upcoming.result"), 1);
	ck_assert_uint_eq (TestWsCaptureCount (0, "Next Song"), 1);

	/* Empty queue: the error goes to the requester only */
	TestWsCaptureClear ();
	cur.head.next = NULL;
	BarUiActPrintUpcomingTo (&app, test_ws_cap.ids[1]);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 1);
	ck_assert_uint_eq (TestWsCaptureCount (1, "query.upcoming"), 1);
	ck_assert_uint_eq (TestWsCaptureCount (1, "error"), 1);
	ck_assert_uint_eq (TestWsCaptureCount (0, ""), 0);

	TestWsCaptureDetach (&app);
	teardown_web_app_with_station (&app, &ctx);
}
END_TEST

START_TEST (test_cli_explain_prints_terminal_output_and_no_web_reply)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t station;
	PianoSong_t song;
	int pipefd[2];
	char out[1024];
	setup_web_app_with_station (&app, &ctx, &station, &song);
	app.settings.uiMode = BAR_UI_MODE_BOTH; /* terminal and web clients */
	TestWsCaptureAttach (&app, 2, NULL);

	ck_assert_int_eq (pipe (pipefd), 0);
	fflush (stdout);
	int saved_stdout = dup (STDOUT_FILENO);
	ck_assert_int_ge (saved_stdout, 0);
	ck_assert_int_ge (dup2 (pipefd[1], STDOUT_FILENO), 0);
	close (pipefd[1]);

	BarUiPianoCallSetTestHook (mock_explain_with_text);
	BarUiActExplain (&app, &station, &song, 1);
	BarUiPianoCallClearTestHook ();

	fflush (stdout);
	ck_assert_int_ge (dup2 (saved_stdout, STDOUT_FILENO), 0);
	close (saved_stdout);
	ssize_t n = read (pipefd[0], out, sizeof (out) - 1);
	close (pipefd[0]);
	ck_assert_int_gt (n, 0);
	out[n] = '\0';

	ck_assert_ptr_nonnull (strstr (out, "Because you enjoy jazz."));
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 0);

	TestWsCaptureDetach (&app);
	teardown_web_app_with_station (&app, &ctx);
}
END_TEST

/* Run `fn` with stdout redirected to a pipe; return what it printed. */
static void
capture_stdout (void (*fn) (void *), void *arg, char *out, size_t outSize)
{
	int pipefd[2];
	ck_assert_int_eq (pipe (pipefd), 0);
	fflush (stdout);
	int saved_stdout = dup (STDOUT_FILENO);
	ck_assert_int_ge (saved_stdout, 0);
	ck_assert_int_ge (dup2 (pipefd[1], STDOUT_FILENO), 0);
	close (pipefd[1]);
	fn (arg);
	fflush (stdout);
	ck_assert_int_ge (dup2 (saved_stdout, STDOUT_FILENO), 0);
	close (saved_stdout);
	ssize_t n = read (pipefd[0], out, outSize - 1);
	close (pipefd[0]);
	ck_assert_int_gt (n, 0);
	out[n] = '\0';
}

typedef struct {
	BarApp_t *app;
	PianoStation_t *station;
	PianoSong_t *song;
} UpcomingArgs_t;

static void
run_cli_upcoming (void *arg)
{
	UpcomingArgs_t *a = arg;
	BarUiActPrintUpcoming (a->app, a->station, a->song, 1);
}

START_TEST (test_cli_upcoming_prints_terminal_list_and_no_web_reply)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t station;
	PianoSong_t cur, next;
	char out[2048];
	setup_web_app_with_station (&app, &ctx, &station, &cur);
	/* Real defaults: the song list needs the configured list format */
	BarSettingsDestroy (&app.settings);
	read_default_settings (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_BOTH; /* terminal and web clients */
	memset (&next, 0, sizeof (next));
	cur.head.next = &next.head;
	next.title = "Queued Song";
	next.artist = "Queued Artist";
	next.album = "Queued Album";
	next.stationId = station.id;
	TestWsCaptureAttach (&app, 2, NULL);

	UpcomingArgs_t args = { &app, &station, &cur };
	capture_stdout (run_cli_upcoming, &args, out, sizeof (out));

	ck_assert_ptr_nonnull (strstr (out, "Queued Song"));
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 0);

	TestWsCaptureDetach (&app);
	teardown_web_app_with_station (&app, &ctx);
}
END_TEST

START_TEST (test_ui_do_pandora_disconnect_playlist_session_error_message)
{
	BarApp_t app;
	BarWsContext_t ctx;
	memset (&app, 0, sizeof (app));
	read_default_settings (&app.settings);
	attach_ws_context_for_test (&app, &ctx);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	ck_assert_int_eq (pthread_mutex_init (&app.pianoHttpMutex, NULL), 0);
	BarStateInit (&app);
	player_primitives_init (&app);
	app.player.mode = PLAYER_DEAD;
	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
	                            app.settings.partnerPassword, app.settings.device,
	                            app.settings.inkey, app.settings.outkey),
	                  PIANO_RET_OK);

	TestWsCaptureAttach (&app, 1, mock_broadcast);
	clear_mock ();

	BarUiDoPandoraDisconnect (&app, "playlist_session_error", NULL);
	capture_pending_message (&ctx, BAR_WS_KEY_NONE);

	ck_assert_ptr_nonnull (last_broadcast ());

	free (app.lastStationId);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	TestWsCaptureDetach (&app);
	ws_context_destroy (&ctx);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
}
END_TEST

START_TEST (test_ui_do_pandora_disconnect_piano_reinit_failure)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	read_default_settings (&app.settings);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	ck_assert_int_eq (pthread_mutex_init (&app.pianoHttpMutex, NULL), 0);
	BarStateInit (&app);
	player_primitives_init (&app);
	app.player.mode = PLAYER_DEAD;
	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
	                            app.settings.partnerPassword, app.settings.device,
	                            app.settings.inkey, app.settings.outkey),
	                  PIANO_RET_OK);

	free (app.settings.inkey);
	free (app.settings.outkey);
	app.settings.inkey = strdup ("");
	app.settings.outkey = strdup ("");

	BarUiDoPandoraDisconnect (&app, "user", NULL);

	ck_assert_int_ne (app.player.interrupted, 0);

	free (app.lastStationId);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_ui_do_pandora_reconnect_piano_reinit_failure)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	read_default_settings (&app.settings);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	ck_assert_int_eq (pthread_mutex_init (&app.pianoHttpMutex, NULL), 0);
	BarStateInit (&app);
	player_primitives_init (&app);
	app.player.mode = PLAYER_DEAD;
	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
	                            app.settings.partnerPassword, app.settings.device,
	                            app.settings.inkey, app.settings.outkey),
	                  PIANO_RET_OK);

	free (app.settings.inkey);
	free (app.settings.outkey);
	app.settings.inkey = strdup ("");
	app.settings.outkey = strdup ("");

	ck_assert (!BarUiDoPandoraReconnect (&app, NULL));

	free (app.lastStationId);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static bool
mock_reconnect_login_ok_stations_fail (BarApp_t *app, const PianoRequestType_t type,
		void *data, PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	(void) data;
	if (type == PIANO_REQUEST_LOGIN) {
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	if (type == PIANO_REQUEST_GET_STATIONS) {
		*pRet = PIANO_RET_P_INTERNAL;
		*wRet = CURLE_OK;
		return false;
	}
	return false;
}

START_TEST (test_ui_do_pandora_reconnect_get_stations_failure)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	read_default_settings (&app.settings);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	ck_assert_int_eq (pthread_mutex_init (&app.pianoHttpMutex, NULL), 0);
	BarStateInit (&app);
	player_primitives_init (&app);
	app.player.mode = PLAYER_DEAD;
	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
	                            app.settings.partnerPassword, app.settings.device,
	                            app.settings.inkey, app.settings.outkey),
	                  PIANO_RET_OK);

	BarUiPianoCallSetTestHook (mock_reconnect_login_ok_stations_fail);
	ck_assert (!BarUiDoPandoraReconnect (&app, NULL));
	BarUiPianoCallClearTestHook ();

	free (app.lastStationId);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static bool
mock_delete_station_ok (BarApp_t *app, const PianoRequestType_t type, void *data,
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

START_TEST (test_ui_act_delete_station_confirmed)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t current, doomed;
	PianoSong_t song;
	int pipefd[2];

	memset (&app, 0, sizeof (app));
	memset (&ctx, 0, sizeof (ctx));
	memset (&current, 0, sizeof (current));
	memset (&doomed, 0, sizeof (doomed));
	memset (&song, 0, sizeof (song));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.wsContext = &ctx;
	ws_context_init (&ctx);
	BarStateInit (&app);
	ck_assert_int_eq (pthread_mutex_init (&app.player.lock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app.player.cond, NULL), 0);
	app.player.settings = &app.settings;
	current.id = "S1";
	current.name = "Keep";
	current.isCreator = true;
	doomed.id = "S2";
	doomed.name = "Delete Me";
	doomed.isCreator = true;
	ck_assert_ptr_nonnull (PianoListAppend (&current.head, &doomed.head));
	app.ph.stations = &current;
	BarStateSetCurrentStation (&app, &current);

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], "y\n");
	rl_input_from_fd (&app.input, pipefd[0]);

	BarUiPianoCallSetTestHook (mock_delete_station_ok);
	BarUiActDeleteStation (&app, &doomed, &song, 1);
	BarUiPianoCallClearTestHook ();

	ck_assert_ptr_eq (BarStateGetCurrentStation (&app), &current);
	ck_assert (strstr (ws_pending_payload (&ctx, BAR_WS_KEY_STATIONS), "\"stations\"") != NULL);
	close (pipefd[0]);
	teardown_web_app_with_station (&app, &ctx);
}
END_TEST

/* --- B4: CLI actions never hold pianoHttpMutex while waiting for input --- */

typedef struct {
	BarApp_t *app;
	int writeFd;
	const char *input;
	PianoStation_t *removeStation; /* unlinked while the input is pending */
	atomic_bool sawReleased;
} BarTestInputFeeder_t;

/* Types the answer only once the prompting thread has released
 * pianoHttpMutex (the waiting thread cannot test its own recursive hold).
 * Gives up after ~2 s so a regression fails instead of hanging. */
static void *input_feeder_thread (void *arg) {
	BarTestInputFeeder_t *f = arg;
	for (int i = 0; i < 2000; i++) {
		if (pthread_mutex_trylock (&f->app->pianoHttpMutex) == 0) {
			atomic_store (&f->sawReleased, true);
			if (f->removeStation != NULL) {
				/* A delete on another thread, while the user types */
				pthread_rwlock_wrlock (&f->app->stateRwlock);
				f->app->ph.stations = PianoListDeleteP (f->app->ph.stations,
						f->removeStation);
				pthread_rwlock_unlock (&f->app->stateRwlock);
			}
			pthread_mutex_unlock (&f->app->pianoHttpMutex);
			break;
		}
		usleep (1000);
	}
	rl_write_input (f->writeFd, f->input);
	return NULL;
}

static unsigned g_deleteCalls;
static bool g_deleteCallHeld;

static void *ui_act_mutex_probe (void *arg) {
	BarApp_t *app = arg;
	const int rc = pthread_mutex_trylock (&app->pianoHttpMutex);
	if (rc == 0) {
		pthread_mutex_unlock (&app->pianoHttpMutex);
	}
	return (void *) (intptr_t) rc;
}

static bool
mock_delete_station_records_hold (BarApp_t *app, const PianoRequestType_t type,
                                  void *data, PianoReturn_t *pRet, CURLcode *wRet)
{
	ck_assert_int_eq (type, PIANO_REQUEST_DELETE_STATION);
	ck_assert_ptr_nonnull (data);
	pthread_t probe;
	void *rc = NULL;
	ck_assert_int_eq (pthread_create (&probe, NULL, ui_act_mutex_probe, app), 0);
	ck_assert_int_eq (pthread_join (probe, &rc), 0);
	g_deleteCallHeld = (intptr_t) rc == EBUSY;
	g_deleteCalls++;
	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;
	return true;
}

static void run_delete_with_feeder (BarApp_t *app, PianoStation_t *selected,
		PianoStation_t *removeStation, BarTestInputFeeder_t *feeder) {
	int pipefd[2];
	pthread_t thread;
	ck_assert_int_eq (pipe (pipefd), 0);
	rl_input_from_fd (&app->input, pipefd[0]);
	feeder->app = app;
	feeder->writeFd = pipefd[1];
	feeder->input = "y\n";
	feeder->removeStation = removeStation;
	atomic_init (&feeder->sawReleased, false);
	g_deleteCalls = 0;
	g_deleteCallHeld = false;

	BarUiPianoCallSetTestHook (mock_delete_station_records_hold);
	/* As the CLI dispatcher does for this action */
	BarUiPianoHold (app);
	ck_assert_int_eq (pthread_create (&thread, NULL, input_feeder_thread, feeder), 0);
	BarUiActDeleteStation (app, selected, NULL, 1);
	BarUiPianoRelease (app);
	ck_assert_int_eq (pthread_join (thread, NULL), 0);
	BarUiPianoCallClearTestHook ();
	close (pipefd[0]);
}

static void delete_wait_app_setup (BarApp_t *app, BarWsContext_t *ctx,
		PianoStation_t *keep, PianoStation_t *doomed) {
	memset (app, 0, sizeof (*app));
	memset (keep, 0, sizeof (*keep));
	memset (doomed, 0, sizeof (*doomed));
	BarSettingsInit (&app->settings);
	app->settings.uiMode = BAR_UI_MODE_WEB;
	ck_assert (BarL10nInit (&app->l10n, &app->settings));
	app->wsContext = ctx;
	ws_context_init (ctx);
	BarStateInit (app);
	BarUiPianoHttpMutexInit (app);
	ck_assert_int_eq (pthread_mutex_init (&app->player.lock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.cond, NULL), 0);
	app->player.settings = &app->settings;
	keep->id = "S1";
	keep->name = "Keep";
	doomed->id = "S2";
	doomed->name = "Delete Me";
	ck_assert_ptr_nonnull (PianoListAppend (&keep->head, &doomed->head));
	app->ph.stations = keep;
}

static void delete_wait_app_teardown (BarApp_t *app, BarWsContext_t *ctx) {
	BarUiPianoHttpMutexDestroy (app);
	BarL10nDestroy (&app->l10n);
	teardown_web_app_with_station (app, ctx);
}

/* The hold is released while the user answers and taken back for the call */
START_TEST (test_ui_act_delete_station_releases_hold_during_prompt)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t keep, doomed;
	BarTestInputFeeder_t feeder;
	delete_wait_app_setup (&app, &ctx, &keep, &doomed);

	run_delete_with_feeder (&app, &doomed, NULL, &feeder);
	ck_assert (atomic_load (&feeder.sawReleased));
	ck_assert_uint_eq (g_deleteCalls, 1);
	ck_assert (g_deleteCallHeld);

	delete_wait_app_teardown (&app, &ctx);
}
END_TEST

/* A station deleted elsewhere while the user answers is not used */
START_TEST (test_ui_act_delete_station_gone_after_prompt_skips_call)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t keep, doomed;
	BarTestInputFeeder_t feeder;
	delete_wait_app_setup (&app, &ctx, &keep, &doomed);

	run_delete_with_feeder (&app, &doomed, &doomed, &feeder);
	ck_assert (atomic_load (&feeder.sawReleased));
	ck_assert_uint_eq (g_deleteCalls, 0);

	delete_wait_app_teardown (&app, &ctx);
}
END_TEST

/* --- eventcmd waits without the hold; the selection is resolved again --- */

typedef struct {
	BarApp_t *app;
	const char *fifo;
	PianoStation_t *removeStation;
	atomic_bool sawReleased;
} BarTestEventcmdFeeder_t;

static void *eventcmd_feeder_thread (void *arg) {
	BarTestEventcmdFeeder_t *f = arg;
	for (int i = 0; i < 2000; i++) {
		if (pthread_mutex_trylock (&f->app->pianoHttpMutex) == 0) {
			atomic_store (&f->sawReleased, true);
			pthread_rwlock_wrlock (&f->app->stateRwlock);
			f->app->ph.stations = PianoListDeleteP (f->app->ph.stations,
					f->removeStation);
			pthread_rwlock_unlock (&f->app->stateRwlock);
			/* Freed like DELETE_STATION does (ASan flags any later use) */
			free (f->removeStation->id);
			free (f->removeStation->name);
			free (f->removeStation);
			pthread_mutex_unlock (&f->app->pianoHttpMutex);
			break;
		}
		usleep (1000);
	}
	int fd = -1;
	for (int i = 0; i < 5000 && fd == -1; i++) {
		fd = open (f->fifo, O_WRONLY | O_NONBLOCK);
		if (fd == -1) {
			usleep (1000);
		}
	}
	ck_assert_int_ne (fd, -1);
	ck_assert (write (fd, "x\n", 2) == 2);
	close (fd);
	return NULL;
}

static unsigned g_manageModeFetches;

static bool
mock_manage_station_info_ok (BarApp_t *app, const PianoRequestType_t type,
                             void *data, PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	(void) data;
	if (type == PIANO_REQUEST_GET_STATION_MODES) {
		g_manageModeFetches++;
	}
	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;
	return true;
}

/* The station is deleted (and freed) while the fetchinfo eventcmd runs;
 * the action stops instead of using it */
START_TEST (test_ui_act_manage_station_gone_after_eventcmd_stops)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t keep, doomed;
	char dir[512], script[600], fifo[600], marker[600];
	int pipefd[2];
	delete_wait_app_setup (&app, &ctx, &keep, &doomed);

	ck_assert_ptr_nonnull (TestTmpMkdtemp (dir, sizeof (dir), "manage_eventcmd"));
	snprintf (fifo, sizeof (fifo), "%s/release", dir);
	snprintf (marker, sizeof (marker), "%s/released", dir);
	ck_assert_int_eq (mkfifo (fifo, 0600), 0);
	snprintf (script, sizeof (script), "%s/eventcmd.sh", dir);
	FILE *f = fopen (script, "w");
	ck_assert_ptr_nonnull (f);
	/* Only the first event waits for the feeder */
	fprintf (f, "#!/bin/sh\ncat > /dev/null\n[ -e '%s' ] || { read x < '%s'; touch '%s'; }\n",
			marker, fifo, marker);
	fclose (f);
	ck_assert_int_eq (chmod (script, 0700), 0);
	app.settings.eventCmd = strdup (script);

	/* "m" would fetch the station's modes if the action went on */
	ck_assert_int_eq (pipe (pipefd), 0);
	rl_input_from_fd (&app.input, pipefd[0]);
	rl_write_input (pipefd[1], "m\n");

	PianoStation_t *heapDoomed = calloc (1, sizeof (*heapDoomed));
	ck_assert_ptr_nonnull (heapDoomed);
	heapDoomed->id = strdup ("S3");
	heapDoomed->name = strdup ("Freed");
	app.ph.stations = PianoListAppendP (app.ph.stations, heapDoomed);
	BarTestEventcmdFeeder_t feeder = { .app = &app, .fifo = fifo,
			.removeStation = heapDoomed };
	atomic_init (&feeder.sawReleased, false);
	g_manageModeFetches = 0;
	pthread_t thread;
	BarUiPianoCallSetTestHook (mock_manage_station_info_ok);
	BarUiPianoHold (&app);
	ck_assert_int_eq (pthread_create (&thread, NULL, eventcmd_feeder_thread, &feeder), 0);
	BarUiActManageStation (&app, heapDoomed, NULL, 1);
	BarUiPianoRelease (&app);
	ck_assert_int_eq (pthread_join (thread, NULL), 0);
	BarUiPianoCallClearTestHook ();
	close (pipefd[0]);

	ck_assert (atomic_load (&feeder.sawReleased));
	ck_assert_uint_eq (g_manageModeFetches, 0);

	delete_wait_app_teardown (&app, &ctx);
}
END_TEST

/* History picks parse the number like before (strtoul base 0: "010" is 8).
 * Only song 8's station exists, so the follow-up prompt reads the rest of
 * the input only when song 8 was picked. */
START_TEST (test_ui_act_history_pick_parses_number_like_before)
{
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t songs[11];
	char tokens[11][8];
	int pipefd[2];
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	memset (songs, 0, sizeof (songs));
	read_default_settings (&app.settings);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	BarStateInit (&app);
	station.id = "real";
	station.name = "Real";
	app.ph.stations = &station;
	for (size_t i = 0; i < 11; i++) {
		snprintf (tokens[i], sizeof (tokens[i]), "t%zu", i);
		songs[i].trackToken = tokens[i];
		songs[i].title = "Song";
		songs[i].artist = "Artist";
		songs[i].stationId = i == 8 ? "real" : "gone";
		if (i > 0) {
			songs[i - 1].head.next = &songs[i].head;
		}
	}
	app.songHistory = &songs[0];

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_input_from_fd (&app.input, pipefd[0]);
	rl_write_input (pipefd[1], "010\nZ");
	BarUiActHistory (&app, NULL, NULL, 1);

	/* The one-key action prompt consumed "Z": nothing is left */
	char rest[8];
	ck_assert_int_eq (read (pipefd[0], rest, sizeof (rest)), 0);
	close (pipefd[0]);

	app.songHistory = NULL;
	BarStateDestroy (&app);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static unsigned g_createFromSongCalls;

static bool
mock_create_from_song_records (BarApp_t *app, const PianoRequestType_t type,
                               void *data, PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	(void) data;
	if (type == PIANO_REQUEST_CREATE_STATION) {
		g_createFromSongCalls++;
	}
	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;
	return true;
}

/* Runs create-from-song with `input` for a song that is no longer queued;
 * returns whether the "no longer available" message was printed */
static bool run_create_from_gone_song (const char *input) {
	BarApp_t app;
	PianoSong_t song;
	int pipefd[2];
	char out[4096];
	memset (&app, 0, sizeof (app));
	memset (&song, 0, sizeof (song));
	read_default_settings (&app.settings);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	BarStateInit (&app);
	song.trackToken = "gone-token";

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_input_from_fd (&app.input, pipefd[0]);
	rl_write_input (pipefd[1], input);

	FILE *capture = tmpfile ();
	ck_assert_ptr_nonnull (capture);
	fflush (stdout);
	const int savedStdout = dup (STDOUT_FILENO);
	ck_assert_int_ne (savedStdout, -1);
	ck_assert_int_ne (dup2 (fileno (capture), STDOUT_FILENO), -1);
	BarUiPianoCallSetTestHook (mock_create_from_song_records);
	BarUiActCreateStationFromSong (&app, NULL, &song, 1);
	BarUiPianoCallClearTestHook ();
	fflush (stdout);
	ck_assert_int_ne (dup2 (savedStdout, STDOUT_FILENO), -1);
	close (savedStdout);
	rewind (capture);
	const size_t n = fread (out, 1, sizeof (out) - 1, capture);
	out[n] = '\0';
	fclose (capture);
	close (pipefd[0]);

	const bool printed = strstr (out, BarL10nGet (&app.l10n, "cli.song_gone")) != NULL;
	BarStateDestroy (&app);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	return printed;
}

/* Aborting (neither s nor a) is not reported as a vanished song; choosing a
 * type for a vanished song is, and no station is created */
START_TEST (test_ui_act_create_from_song_gone_only_reported_on_choice)
{
	g_createFromSongCalls = 0;
	ck_assert (!run_create_from_gone_song ("\n"));
	ck_assert (run_create_from_gone_song ("s\n"));
	ck_assert_uint_eq (g_createFromSongCalls, 0);
}
END_TEST

static bool
mock_bookmark_song_ok (BarApp_t *app, const PianoRequestType_t type, void *data,
                       PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	(void) data;
	if (type == PIANO_REQUEST_BOOKMARK_SONG) {
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_ui_act_bookmark_song)
{
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	int pipefd[2];

	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	memset (&song, 0, sizeof (song));
	BarSettingsInit (&app.settings);
	BarStateInit (&app);
	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], "s\n");
	rl_input_from_fd (&app.input, pipefd[0]);

	BarUiPianoCallSetTestHook (mock_bookmark_song_ok);
	BarUiActBookmark (&app, &station, &song, 1);
	BarUiPianoCallClearTestHook ();

	close (pipefd[0]);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static bool
mock_get_settings_ok (BarApp_t *app, const PianoRequestType_t type, void *data,
                      PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	if (type == PIANO_REQUEST_GET_SETTINGS) {
		PianoSettings_t *settings = data;
		settings->username = "olduser";
		settings->explicitContentFilter = false;
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	if (type == PIANO_REQUEST_CHANGE_SETTINGS) {
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_ui_act_settings_change_username)
{
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	int pipefd[2];

	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	memset (&song, 0, sizeof (song));
	BarSettingsInit (&app.settings);
	app.settings.username = strdup ("olduser");
	app.settings.password = strdup ("oldpass");
	BarStateInit (&app);

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], "0\nnewuser\n\n");
	rl_input_from_fd (&app.input, pipefd[0]);

	BarUiPianoCallSetTestHook (mock_get_settings_ok);
	BarUiActSettings (&app, &station, &song, 1);
	BarUiPianoCallClearTestHook ();

	ck_assert_str_eq (app.settings.username, "newuser");
	close (pipefd[0]);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static bool
mock_bookmark_artist_ok (BarApp_t *app, const PianoRequestType_t type, void *data,
                         PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	(void) data;
	if (type == PIANO_REQUEST_BOOKMARK_ARTIST) {
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_ui_act_bookmark_artist)
{
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	int pipefd[2];

	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	memset (&song, 0, sizeof (song));
	BarSettingsInit (&app.settings);
	BarStateInit (&app);
	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], "a\n");
	rl_input_from_fd (&app.input, pipefd[0]);

	BarUiPianoCallSetTestHook (mock_bookmark_artist_ok);
	BarUiActBookmark (&app, &station, &song, 1);
	BarUiPianoCallClearTestHook ();

	close (pipefd[0]);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST


/* --- Interactive CLI actions: prompt, re-resolve by ID, "gone" paths --- */

typedef void (*BarTestCliAction_fn) (BarApp_t *, PianoStation_t *,
		PianoSong_t *, int);
void BarUiActSelectStation (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActSelectQuickMix (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActStationFromGenre (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActCreateStation (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActAddSharedStation (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActAddMusic (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActRenameStation (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);

#define CLI_MAX_CALLS 16
static PianoRequestType_t g_cliTypes[CLI_MAX_CALLS];
static unsigned g_cliCount;
static atomic_int g_cliModesFetched;

static unsigned cli_calls (PianoRequestType_t type) {
	unsigned n = 0;
	for (unsigned i = 0; i < g_cliCount; i++) {
		if (g_cliTypes[i] == type) {
			n++;
		}
	}
	return n;
}

/* Records every Pandora request (main thread); answers station info with
 * one seed of each kind, modes with one mode, search with one artist */
static bool
mock_cli_record (BarApp_t *app, const PianoRequestType_t type, void *data,
                 PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	ck_assert_uint_lt (g_cliCount, CLI_MAX_CALLS);
	g_cliTypes[g_cliCount++] = type;
	if (type == PIANO_REQUEST_GET_STATION_INFO) {
		PianoRequestDataGetStationInfo_t *req = data;
		PianoArtist_t *artist = calloc (1, sizeof (*artist));
		PianoSong_t *seed = calloc (1, sizeof (*seed));
		PianoStation_t *seedStation = calloc (1, sizeof (*seedStation));
		PianoSong_t *feedback = calloc (1, sizeof (*feedback));
		ck_assert (artist && seed && seedStation && feedback);
		artist->name = strdup ("Seed Artist");
		seed->title = strdup ("Seed Song");
		seed->artist = strdup ("Seed Artist");
		seedStation->name = strdup ("Seed Station");
		seedStation->id = strdup ("SEED");
		feedback->title = strdup ("Liked Song");
		feedback->artist = strdup ("Liked Artist");
		seed->stationId = strdup ("S1");
		feedback->stationId = strdup ("S1");
		req->info.artistSeeds = artist;
		req->info.songSeeds = seed;
		req->info.stationSeeds = seedStation;
		req->info.feedback = feedback;
	} else if (type == PIANO_REQUEST_GET_STATION_MODES) {
		PianoRequestDataGetStationModes_t *req = data;
		PianoStationMode_t *mode = calloc (1, sizeof (*mode));
		ck_assert_ptr_nonnull (mode);
		mode->name = strdup ("Discovery");
		mode->description = strdup ("New music");
		req->retModes = mode;
		atomic_store (&g_cliModesFetched, 1);
	} else if (type == PIANO_REQUEST_SEARCH) {
		PianoRequestDataSearch_t *req = data;
		memset (&req->searchResult, 0, sizeof (req->searchResult));
		PianoArtist_t *artist = calloc (1, sizeof (*artist));
		ck_assert_ptr_nonnull (artist);
		artist->name = strdup ("Found Artist");
		artist->musicId = strdup ("M42");
		req->searchResult.artists = artist;
	}
	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;
	return true;
}

static void cli_app_setup (BarApp_t *app, BarWsContext_t *ctx,
		PianoStation_t *keep, PianoStation_t *other) {
	delete_wait_app_setup (app, ctx, keep, other);
	ck_assert_int_eq (pthread_cond_init (&app->player.audioCond, NULL), 0);
	ck_assert_int_eq (pthread_mutex_init (&app->player.decoderLock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.decoderCond, NULL), 0);
	keep->isCreator = true;
	other->isCreator = true;
	/* song lists are printed with these */
	app->settings.loveIcon = strdup ("+");
	app->settings.banIcon = strdup ("-");
	app->settings.tiredIcon = strdup ("t");
	app->settings.atIcon = strdup ("@");
	free (app->settings.listSongFormat);
	app->settings.listSongFormat = strdup ("%i) %a - %t%r");
	g_cliCount = 0;
	atomic_store (&g_cliModesFetched, 0);
	BarUiPianoCallSetTestHook (mock_cli_record);
}

static void cli_app_teardown (BarApp_t *app, BarWsContext_t *ctx) {
	BarUiPianoCallClearTestHook ();
	pthread_cond_destroy (&app->player.decoderCond);
	pthread_mutex_destroy (&app->player.decoderLock);
	pthread_cond_destroy (&app->player.audioCond);
	delete_wait_app_teardown (app, ctx);
}

/* Run an action as the CLI dispatcher does (inside the hold) with all of
 * its input typed ahead. Every prompt must be answered: a pipe at EOF never
 * ends a read (BarReadline only drops an EOFed stdin). */
static void cli_run (BarApp_t *app, BarTestCliAction_fn action,
		PianoStation_t *station, PianoSong_t *song, const char *input) {
	int pipefd[2];
	ck_assert_int_eq (pipe (pipefd), 0);
	rl_input_from_fd (&app->input, pipefd[0]);
	rl_write_input (pipefd[1], input);
	BarUiPianoHold (app);
	action (app, station, song, 1 /* BAR_DC_GLOBAL */);
	BarUiPianoRelease (app);
	close (pipefd[0]);
}

/* Types `input` once the action waits for input without the hold
 * and, with `gate`, only after the gate opened; before typing it unlinks a
 * station or empties the history, as another thread could. Results are
 * read by the main thread after the join (no assertions here). */
typedef struct {
	BarApp_t *app;
	int writeFd;
	const char *input;
	atomic_int *gate;
	PianoStation_t *removeStation;
	bool clearHistory;
	atomic_bool sawReleased;
} BarTestCliFeeder_t;

static void *cli_feeder_thread (void *arg) {
	BarTestCliFeeder_t *f = arg;
	for (int i = 0; i < 2000; i++) {
		if ((f->gate == NULL || atomic_load (f->gate) != 0) &&
				pthread_mutex_trylock (&f->app->pianoHttpMutex) == 0) {
			atomic_store (&f->sawReleased, true);
			pthread_rwlock_wrlock (&f->app->stateRwlock);
			if (f->removeStation != NULL) {
				f->app->ph.stations = PianoListDeleteP (f->app->ph.stations,
						f->removeStation);
			}
			if (f->clearHistory) {
				f->app->songHistory = NULL;
			}
			pthread_rwlock_unlock (&f->app->stateRwlock);
			pthread_mutex_unlock (&f->app->pianoHttpMutex);
			break;
		}
		usleep (1000);
	}
	if (write (f->writeFd, f->input, strlen (f->input)) < 0) {
		/* the reader sees EOF and the main thread's assertions fail */
	}
	close (f->writeFd);
	return NULL;
}

/* Like cli_run, but the feeder types `later` after mutating the state;
 * `ahead` is typed before the action starts */
static void cli_run_with_feeder (BarApp_t *app, BarTestCliAction_fn action,
		PianoStation_t *station, PianoSong_t *song, const char *ahead,
		BarTestCliFeeder_t *feeder, const char *later) {
	int pipefd[2];
	pthread_t thread;
	ck_assert_int_eq (pipe (pipefd), 0);
	rl_input_from_fd (&app->input, pipefd[0]);
	ck_assert (write (pipefd[1], ahead, strlen (ahead)) >= 0);
	feeder->app = app;
	feeder->writeFd = pipefd[1];
	feeder->input = later;
	atomic_init (&feeder->sawReleased, false);
	BarUiPianoHold (app);
	ck_assert_int_eq (pthread_create (&thread, NULL, cli_feeder_thread, feeder), 0);
	action (app, station, song, 1 /* BAR_DC_GLOBAL */);
	BarUiPianoRelease (app);
	ck_assert_int_eq (pthread_join (thread, NULL), 0);
	close (pipefd[0]);
}

START_TEST (test_cli_select_station_by_index_sets_next)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t keep, other;
	cli_app_setup (&app, &ctx, &keep, &other);
	app.ph.stations = &keep; /* one station: index 0 */
	keep.head.next = NULL;
	BarStateSetLastStationId (&app, "old");

	cli_run (&app, BarUiActSelectStation, NULL, NULL, "0\n");
	ck_assert_ptr_eq (BarStateGetNextStation (&app), &keep);
	ck_assert_ptr_null (app.lastStationId); /* explicit choice clears it */

	cli_app_teardown (&app, &ctx);
}
END_TEST

START_TEST (test_cli_select_station_gone_while_choosing)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t keep, other;
	BarTestCliFeeder_t feeder = { 0 };
	cli_app_setup (&app, &ctx, &keep, &other);
	app.ph.stations = &keep;
	keep.head.next = NULL;

	/* listed, then deleted before the user's "0" arrives */
	feeder.removeStation = &keep;
	cli_run_with_feeder (&app, BarUiActSelectStation, NULL, NULL, "", &feeder, "0\n");
	ck_assert (atomic_load (&feeder.sawReleased));
	ck_assert_ptr_null (BarStateGetNextStation (&app));

	cli_app_teardown (&app, &ctx);
}
END_TEST

/* Every manage-station branch: delete an artist/song/station seed or
 * feedback, or pick a mode */
START_TEST (test_cli_manage_station_branches)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t keep, other;
	cli_app_setup (&app, &ctx, &keep, &other);

	cli_run (&app, BarUiActManageStation, &keep, NULL, "a0\n");
	cli_run (&app, BarUiActManageStation, &keep, NULL, "s0\n");
	cli_run (&app, BarUiActManageStation, &keep, NULL, "t0\n");
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_DELETE_SEED), 3);
	cli_run (&app, BarUiActManageStation, &keep, NULL, "f0\n");
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_DELETE_FEEDBACK), 1);
	cli_run (&app, BarUiActManageStation, &keep, NULL, "m0\n");
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_GET_STATION_MODES), 1);
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_SET_STATION_MODE), 1);
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_GET_STATION_INFO), 5);
	/* Aborting at a selection deletes nothing */
	cli_run (&app, BarUiActManageStation, &keep, NULL, "a\n");
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_DELETE_SEED), 3);

	cli_app_teardown (&app, &ctx);
}
END_TEST

/* The station is deleted while the user picks a mode: no mode is set */
START_TEST (test_cli_manage_station_gone_while_picking_mode)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t keep, other;
	BarTestCliFeeder_t feeder = { 0 };
	cli_app_setup (&app, &ctx, &keep, &other);

	feeder.gate = &g_cliModesFetched;
	feeder.removeStation = &other;
	cli_run_with_feeder (&app, BarUiActManageStation, &other, NULL, "m", &feeder, "0\n");
	ck_assert (atomic_load (&feeder.sawReleased));
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_GET_STATION_MODES), 1);
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_SET_STATION_MODE), 0);

	cli_app_teardown (&app, &ctx);
}
END_TEST

/* QuickMix: t/a/n toggle all/select all/none, a number toggles one, an
 * empty answer saves */
START_TEST (test_cli_quickmix_edit_and_save)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t keep, other;
	cli_app_setup (&app, &ctx, &keep, &other);
	keep.isQuickMix = true;

	cli_run (&app, BarUiActSelectQuickMix, &keep, NULL, "n\n\n");
	ck_assert (!keep.useQuickMix && !other.useQuickMix);
	cli_run (&app, BarUiActSelectQuickMix, &keep, NULL, "a\n\n");
	ck_assert (other.useQuickMix);
	cli_run (&app, BarUiActSelectQuickMix, &keep, NULL, "t\n\n");
	ck_assert (!other.useQuickMix);
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_SET_QUICKMIX), 3);
	/* toggle one station by number: exactly one changes */
	cli_run (&app, BarUiActSelectQuickMix, &keep, NULL, "0\n\n");
	ck_assert_int_eq ((int) keep.useQuickMix + (int) other.useQuickMix, 1);
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_SET_QUICKMIX), 4);

	cli_app_teardown (&app, &ctx);
}
END_TEST

START_TEST (test_cli_genre_station_by_category_and_genre)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t keep, other;
	PianoGenreCategory_t cat;
	PianoGenre_t genre;
	cli_app_setup (&app, &ctx, &keep, &other);
	memset (&cat, 0, sizeof (cat));
	memset (&genre, 0, sizeof (genre));
	cat.name = "Jazz";
	cat.genres = &genre;
	genre.name = "Bebop";
	genre.musicId = "G1";
	app.ph.genreStations = &cat;

	/* out-of-range answers are asked again */
	cli_run (&app, BarUiActStationFromGenre, NULL, NULL, "7\n0\n9\n0\n");
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_CREATE_STATION), 1);
	/* aborting at the genre creates nothing */
	cli_run (&app, BarUiActStationFromGenre, NULL, NULL, "0\n\n");
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_CREATE_STATION), 1);
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_GET_GENRE_STATIONS), 0);

	app.ph.genreStations = NULL;
	cli_app_teardown (&app, &ctx);
}
END_TEST

/* Station creation prompts: from a search, from the playing song, by id;
 * adding music to a station */
START_TEST (test_cli_create_add_and_rename_prompts)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t keep, other;
	PianoSong_t song;
	cli_app_setup (&app, &ctx, &keep, &other);
	memset (&song, 0, sizeof (song));
	song.title = "Playing";
	song.trackToken = "T1";
	app.playlist = &song; /* re-resolved by token after the prompt */

	cli_run (&app, BarUiActCreateStation, NULL, NULL, "query\n0\n");
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_SEARCH), 1);
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_CREATE_STATION), 1);
	cli_run (&app, BarUiActCreateStationFromSong, &keep, &song, "s");
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_CREATE_STATION), 2);
	cli_run (&app, BarUiActAddSharedStation, &keep, &song, "123\n");
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_CREATE_STATION), 3);
	cli_run (&app, BarUiActAddMusic, &keep, &song, "query\n0\n");
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_ADD_SEED), 1);
	cli_run (&app, BarUiActRenameStation, &keep, &song, "Renamed\n");
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_RENAME_STATION), 1);

	app.playlist = NULL;
	cli_app_teardown (&app, &ctx);
}
END_TEST

/* Selection deleted while typing: add music and rename stop */
START_TEST (test_cli_add_music_and_rename_gone_while_typing)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t keep, other;
	BarTestCliFeeder_t feeder = { 0 };
	cli_app_setup (&app, &ctx, &keep, &other);

	feeder.removeStation = &other;
	cli_run_with_feeder (&app, BarUiActRenameStation, &other, NULL, "", &feeder, "New\n");
	ck_assert (atomic_load (&feeder.sawReleased));
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_RENAME_STATION), 0);

	memset (&feeder, 0, sizeof (feeder));
	ck_assert_ptr_nonnull (PianoListAppend (&keep.head, &other.head));
	feeder.removeStation = &other;
	cli_run_with_feeder (&app, BarUiActAddMusic, &other, NULL, "", &feeder, "query\n0\n");
	ck_assert (atomic_load (&feeder.sawReleased));
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_SEARCH), 1);
	ck_assert_uint_eq (cli_calls (PIANO_REQUEST_ADD_SEED), 0);

	cli_app_teardown (&app, &ctx);
}
END_TEST

/* History: abort, pick, and the picked song gone while choosing */
START_TEST (test_cli_history_pick_abort_and_gone)
{
	BarApp_t app;
	BarWsContext_t ctx;
	PianoStation_t keep, other;
	PianoSong_t hist;
	BarTestCliFeeder_t feeder = { 0 };
	cli_app_setup (&app, &ctx, &keep, &other);
	memset (&hist, 0, sizeof (hist));
	hist.title = "Earlier";
	hist.artist = "Someone";
	hist.trackToken = "H1";
	hist.stationId = "S1";
	app.songHistory = &hist;

	/* an empty answer at the song prompt: nothing happens */
	cli_run (&app, BarUiActHistory, NULL, NULL, "\n");
	ck_assert_uint_eq (g_cliCount, 0);
	/* pick it and leave the action prompt empty. (An action key cannot be
	 * used here: history dispatches with BAR_DC_UNDEFINED, so actions that
	 * need BAR_DC_PANDORA_CONNECTED or BAR_DC_GLOBAL are refused; a
	 * pre-existing limit, reported.) "?" re-asks. */
	cli_run (&app, BarUiActHistory, NULL, NULL, "0\n?\n");
	ck_assert_uint_eq (g_cliCount, 0);
	ck_assert_ptr_eq (app.songHistory, &hist);
	/* the history goes away before the choice arrives */
	feeder.clearHistory = true;
	cli_run_with_feeder (&app, BarUiActHistory, NULL, NULL, "", &feeder, "0\n");
	ck_assert (atomic_load (&feeder.sawReleased));
	ck_assert_ptr_null (app.songHistory);
	ck_assert_uint_eq (g_cliCount, 0);

	app.songHistory = NULL;
	cli_app_teardown (&app, &ctx);
}
END_TEST

Suite *
ui_act_suite (void)
{
	Suite *s = suite_create ("ui_act");
	TCase *tc = tcase_create ("actions");
	tcase_add_test (tc, test_ui_act_pandora_reconnect_no_credentials);
	tcase_add_test (tc, test_ui_act_help_runs_dispatch_loop);
	tcase_add_test (tc, test_ui_act_play_pause_toggle);
	tcase_add_test (tc, test_ui_act_rejected_controls_preserve_last_broadcast);
	tcase_add_test (tc, test_ui_act_controls_complete_real_sound_before_broadcast);
	tcase_add_test (tc, test_ui_act_pending_pause_suppresses_delayed_play_state);
	tcase_add_test (tc, test_ui_act_delayed_play_state_cannot_replace_newer_final_event);
	tcase_add_test (tc, test_ui_act_completed_state_survives_unrelated_audio_owner);
	tcase_add_test (tc, test_ui_act_volume_player_mode);
	tcase_add_test (tc, test_ui_act_cli_and_websocket_volume_updates_share_runtime_value);
	tcase_add_test (tc, test_ui_act_debug_prints_fields);
	tcase_add_test (tc, test_ui_act_skip_song_sets_do_quit);
	tcase_add_test (tc, test_ui_act_quit);
	tcase_add_test (tc, test_disconnect_player_wait_returns_fast_when_already_dead);
	tcase_add_test (tc, test_transform_owned_station_skips_pandora_call);
	tcase_add_test (tc, test_ui_do_pandora_disconnect_when_player_already_dead);
	tcase_add_test (tc, test_ui_do_pandora_disconnect_idle_timeout_message);
	tcase_add_test (tc, test_ui_do_pandora_disconnect_clears_playback_state_before_destroy);
	tcase_add_test (tc, test_ui_act_pandora_reconnect_clears_playback_state);
	tcase_add_test (tc, test_session_auto_recover_skips_intentional_stops);
	tcase_add_test (tc, test_session_auto_recover_skips_while_quitting);
	tcase_add_test (tc, test_session_auto_recover_attempts_once_per_outage);
	tcase_add_test (tc, test_session_auto_recover_resumes_without_disconnect_event);
	tcase_add_test (tc, test_session_auto_recover_failure_is_sticky);
	tcase_add_test (tc, test_ui_do_pandora_disconnect_logs_when_manager_not_parked);
	tcase_add_test (tc, test_ui_act_pandora_reconnect_logs_when_manager_not_parked);
	tcase_add_test (tc, test_ui_do_pandora_disconnect_waits_for_running_manager);
	tcase_add_test (tc, test_ui_do_pandora_disconnect_waits_for_parked_manager);
	tcase_add_test (tc, test_ui_switch_station_sets_next_and_drains_playlist);
	tcase_add_test (tc, test_ui_switch_station_by_id_missing_keeps_playlist);
	tcase_add_test (tc, test_ui_switch_station_by_id_null_returns_false);
	tcase_add_test (tc, test_ui_switch_station_by_id_found_sets_next);
	tcase_add_test (tc, test_ui_act_song_info_quickmix_resolves_child_station);
	tcase_add_test (tc, test_ui_act_print_upcoming_lists_and_broadcasts);
	tcase_add_test (tc, test_ui_act_print_upcoming_empty_queue_cli_message);
	tcase_add_test (tc, test_ui_act_history_disabled);
	tcase_add_test (tc, test_ui_act_pandora_disconnect_delegates_to_helper);
	tcase_add_test (tc, test_transform_shared_station_invokes_transform_request);
	tcase_add_test (tc, test_ui_act_explain_handles_empty_response);
	tcase_add_test (tc, test_ui_do_pandora_disconnect_waits_for_player_stop);
	tcase_add_test (tc, test_ui_act_volume_system_mode);
	tcase_add_test (tc, test_transform_shared_station_failure_logged);
	tcase_add_test (tc, test_ui_act_love_song_broadcasts_process);
	tcase_add_test (tc, test_ui_act_ban_song_skips_current_playlist_track);
	tcase_add_test (tc, test_ui_act_temp_ban_skips_current_playlist_track);
	tcase_add_test (tc, test_ui_act_explain_websocket_failure_emits_error);
	tcase_add_test (tc, test_ui_act_explain_websocket_success_broadcasts);
	tcase_add_test (tc, test_ui_act_print_upcoming_websocket_empty_emits_error);
	tcase_add_test (tc, test_query_explain_reaches_only_requester);
	tcase_add_test (tc, test_query_upcoming_reaches_only_requester);
	tcase_add_test (tc, test_cli_explain_prints_terminal_output_and_no_web_reply);
	tcase_add_test (tc, test_cli_upcoming_prints_terminal_list_and_no_web_reply);
	tcase_add_test (tc, test_ui_act_delete_station_confirmed);
	tcase_add_test (tc, test_ui_act_delete_station_releases_hold_during_prompt);
	tcase_add_test (tc, test_ui_act_delete_station_gone_after_prompt_skips_call);
	tcase_add_test (tc, test_ui_act_manage_station_gone_after_eventcmd_stops);
	tcase_add_test (tc, test_cli_select_station_by_index_sets_next);
	tcase_add_test (tc, test_cli_select_station_gone_while_choosing);
	tcase_add_test (tc, test_cli_manage_station_branches);
	tcase_add_test (tc, test_cli_manage_station_gone_while_picking_mode);
	tcase_add_test (tc, test_cli_quickmix_edit_and_save);
	tcase_add_test (tc, test_cli_genre_station_by_category_and_genre);
	tcase_add_test (tc, test_cli_create_add_and_rename_prompts);
	tcase_add_test (tc, test_cli_add_music_and_rename_gone_while_typing);
	tcase_add_test (tc, test_cli_history_pick_abort_and_gone);
	tcase_add_test (tc, test_ui_act_history_pick_parses_number_like_before);
	tcase_add_test (tc, test_ui_act_create_from_song_gone_only_reported_on_choice);
	tcase_add_test (tc, test_ui_act_bookmark_song);
	tcase_add_test (tc, test_ui_act_bookmark_artist);
	tcase_add_test (tc, test_ui_act_settings_change_username);
	tcase_add_test (tc, test_ui_do_pandora_disconnect_playlist_session_error_message);
	tcase_add_test (tc, test_ui_do_pandora_disconnect_piano_reinit_failure);
	tcase_add_test (tc, test_ui_do_pandora_reconnect_piano_reinit_failure);
	tcase_add_test (tc, test_ui_do_pandora_reconnect_get_stations_failure);
	suite_add_tcase (s, tc);
	return s;
}
