/*
 * Unit tests for selected BarUiAct* handlers to raise coverage of ui_act.c.
 */

#include <check.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/bar_constants.h"
#include "../../src/l10n.h"
#include "../../src/main.h"
#include "../../src/player.h"
#include "../../src/settings.h"
#include "../../src/system_volume.h"
#include "../../src/websocket/protocol/socketio.h"

/* Declaration only — avoid ui_dispatch.h (pulls full shortcut table + ui_act.h). */
void BarUiActPandoraReconnect (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActHelp (BarApp_t *app, PianoStation_t *selStation,
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
void BarUiActDebug (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActQuit (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);
void BarUiActSkipSong (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);

static char *last_broadcast_msg = NULL;

static void
mock_broadcast (const char *message, size_t len)
{
	free (last_broadcast_msg);
	last_broadcast_msg = malloc (len + 1);
	memcpy (last_broadcast_msg, message, len);
	last_broadcast_msg[len] = '\0';
}

static void
clear_mock (void)
{
	free (last_broadcast_msg);
	last_broadcast_msg = NULL;
}

static void
player_primitives_init (BarApp_t *app)
{
	ck_assert_int_eq (pthread_mutex_init (&app->player.lock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.cond, NULL), 0);
	ck_assert_int_eq (pthread_mutex_init (&app->player.decoderLock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.decoderCond, NULL), 0);
	app->player.settings = &app->settings;
	app->player.soundInitialized = false;
}

static void
player_primitives_destroy (BarApp_t *app)
{
	pthread_cond_destroy (&app->player.decoderCond);
	pthread_mutex_destroy (&app->player.decoderLock);
	pthread_cond_destroy (&app->player.cond);
	pthread_mutex_destroy (&app->player.lock);
}

START_TEST (test_ui_act_pandora_reconnect_no_credentials)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);

	ck_assert (BarL10nInit (&app.l10n, &app.settings));

	BarSocketIoSetBroadcastCallback (mock_broadcast);
	clear_mock ();

	BarUiActPandoraReconnect (&app, NULL, NULL, 1);

	ck_assert_ptr_nonnull (last_broadcast_msg);
	ck_assert (strstr (last_broadcast_msg, "error") != NULL);

	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
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
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	player_primitives_init (&app);

	BarUiActPlay (&app, NULL, NULL, 1);
	ck_assert (app.player.doPause == false);

	BarUiActPause (&app, NULL, NULL, 1);
	ck_assert (app.player.doPause == true);

	BarUiActTogglePause (&app, NULL, NULL, 1);

	player_primitives_destroy (&app);
	BarSettingsDestroy (&app.settings);
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

Suite *
ui_act_suite (void)
{
	Suite *s = suite_create ("ui_act");
	TCase *tc = tcase_create ("actions");
	tcase_add_test (tc, test_ui_act_pandora_reconnect_no_credentials);
	tcase_add_test (tc, test_ui_act_help_runs_dispatch_loop);
	tcase_add_test (tc, test_ui_act_play_pause_toggle);
	tcase_add_test (tc, test_ui_act_volume_player_mode);
	tcase_add_test (tc, test_ui_act_debug_prints_fields);
	tcase_add_test (tc, test_ui_act_skip_song_sets_do_quit);
	tcase_add_test (tc, test_ui_act_quit);
	suite_add_tcase (s, tc);
	return s;
}
