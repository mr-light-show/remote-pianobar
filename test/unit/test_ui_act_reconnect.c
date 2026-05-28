/*
 * Unit tests for selected BarUiAct* handlers to raise coverage of ui_act.c.
 */

#include <check.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../../src/bar_constants.h"
#include "../../src/bar_state.h"
#include "../../src/l10n.h"
#include "../../src/main.h"
#include "../../src/player.h"
#include "../../src/settings.h"
#include "../../src/system_volume.h"
#include "../../src/ui.h"
#include "../../src/websocket/core/websocket.h"
#include "../../src/websocket/protocol/socketio.h"
#include "../../src/websocket_bridge.h"

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
void BarUiDoPandoraDisconnect (BarApp_t *app, const char *reason,
		const char *resume_station_id_override);
void BarUiSwitchStation (BarApp_t *app, PianoStation_t *station);
bool BarTransformIfShared (BarApp_t *app, PianoStation_t *station);
bool BarWsTransformIfShared (BarApp_t *app, PianoStation_t *station);

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

static void
ws_context_init (BarWsContext_t *ctx)
{
	memset (ctx, 0, sizeof (*ctx));
	for (int i = 0; i < BUCKET_COUNT; i++) {
		pthread_mutex_init (&ctx->buckets[i].mutex, NULL);
	}
}

static void
ws_context_destroy (BarWsContext_t *ctx)
{
	for (int i = 0; i < BUCKET_COUNT; i++) {
		pthread_mutex_destroy (&ctx->buckets[i].mutex);
	}
}

static void
setup_web_app_with_station (BarApp_t *app, BarWsContext_t *ctx,
		PianoStation_t *station, PianoSong_t *song)
{
	memset (app, 0, sizeof (*app));
	BarSettingsInit (&app->settings);
	app->settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit (app);
	player_primitives_init (app);
	ws_context_init (ctx);
	app->wsContext = ctx;
	station->id = "S1";
	station->name = "Station One";
	station->isCreator = true;
	song->stationId = station->id;
	song->title = "Test Song";
	app->ph.stations = station;
	BarStateSetCurrentStation (app, station);
}

static const char *
ws_bucket_payload (BarWsContext_t *ctx, BarWsBucketType_t bucket)
{
	ck_assert_ptr_nonnull (ctx->buckets[bucket].message);
	ck_assert_ptr_nonnull (ctx->buckets[bucket].message->data);
	return (const char *) ctx->buckets[bucket].message->data;
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
	char tmpl[] = "/tmp/piano_ui_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	char sub[512];
	snprintf (sub, sizeof (sub), "%s/pianobar", tmpl);
	ck_assert_int_eq (mkdir (sub, 0700), 0);
	char cfg[512];
	snprintf (cfg, sizeof (cfg), "%s/config", sub);
	FILE *f = fopen (cfg, "w");
	ck_assert_ptr_nonnull (f);
	fclose (f);

	ck_assert_int_eq (setenv ("HOME", tmpl, 1), 0);
	ck_assert_int_eq (setenv ("XDG_CONFIG_HOME", tmpl, 1), 0);
	BarSettingsInit (settings);
	BarSettingsRead (settings);
}

START_TEST (test_ui_do_pandora_disconnect_when_player_already_dead)
{
	BarApp_t app;
	PianoStation_t station;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	read_default_settings (&app.settings);
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

	BarSocketIoSetBroadcastCallback (mock_broadcast);
	clear_mock ();

	BarUiDoPandoraDisconnect (&app, "user", NULL);

	ck_assert_ptr_nonnull (app.lastStationId);
	ck_assert_str_eq (app.lastStationId, "resume-station");
	ck_assert_ptr_nonnull (last_broadcast_msg);
	ck_assert (strstr (last_broadcast_msg, "pandora.disconnected") != NULL);
	ck_assert (strstr (last_broadcast_msg, "user") != NULL);

	free (app.lastStationId);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
}
END_TEST

START_TEST (test_ui_do_pandora_disconnect_idle_timeout_message)
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

	BarSocketIoSetBroadcastCallback (mock_broadcast);
	clear_mock ();

	BarUiDoPandoraDisconnect (&app, "idle_timeout", "override-station");

	ck_assert_ptr_nonnull (app.lastStationId);
	ck_assert_str_eq (app.lastStationId, "override-station");
	ck_assert (strstr (last_broadcast_msg, "idle_timeout") != NULL);

	free (app.lastStationId);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
}
END_TEST

START_TEST (test_ui_switch_station_sets_next_and_drains_playlist)
{
	BarApp_t app;
	PianoStation_t from, to;
	PianoSong_t song;
	memset (&app, 0, sizeof (app));
	memset (&from, 0, sizeof (from));
	memset (&to, 0, sizeof (to));
	memset (&song, 0, sizeof (song));
	BarSettingsInit (&app.settings);
	BarStateInit (&app);
	from.id = "from"; from.name = "From";
	to.id = "to"; to.name = "To";
	app.curStation = &from;
	app.playlist = &song;

	BarUiSwitchStation (&app, &to);

	ck_assert_ptr_eq (BarStateGetNextStation (&app), &to);
	{
		PianoSong_t *pl = BarStateGetPlaylist (&app);
		ck_assert_ptr_nonnull (pl);
		ck_assert_ptr_null (pl->head.next);
	}

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

	BarSocketIoSetBroadcastCallback (mock_broadcast);
	BarSocketIoSetUnicastTarget ((void *) 0x1);
	clear_mock ();

	BarUiActPrintUpcoming (&app, &station, &cur, 1);

	ck_assert_ptr_nonnull (last_broadcast_msg);
	ck_assert (strstr (last_broadcast_msg, "query.upcoming.result") != NULL);

	BarSocketIoSetUnicastTarget (NULL);
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
	PianoStation_t station;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	read_default_settings (&app.settings);
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

	BarSocketIoSetBroadcastCallback (mock_broadcast);
	clear_mock ();

	BarUiActPandoraDisconnect (&app, NULL, NULL, 1);

	ck_assert_ptr_nonnull (last_broadcast_msg);
	ck_assert (strstr (last_broadcast_msg, "pandora.disconnected") != NULL);

	free (app.lastStationId);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
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
	pthread_t helper;
	SetDeadArgs_t args;
	memset (&app, 0, sizeof (app));
	read_default_settings (&app.settings);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	ck_assert_int_eq (pthread_mutex_init (&app.pianoHttpMutex, NULL), 0);
	BarStateInit (&app);
	player_primitives_init (&app);
	BarPlayerSetMode (&app.player, PLAYER_PLAYING);
	ck_assert_int_eq (PianoInit (&app.ph, app.settings.partnerUser,
	                            app.settings.partnerPassword, app.settings.device,
	                            app.settings.inkey, app.settings.outkey),
	                  PIANO_RET_OK);

	BarSocketIoSetBroadcastCallback (mock_broadcast);
	clear_mock ();

	args.player = &app.player;
	ck_assert_int_eq (pthread_create (&helper, NULL, set_player_dead_thread, &args), 0);
	BarUiDoPandoraDisconnect (&app, "user", NULL);
	ck_assert_int_eq (pthread_join (helper, NULL), 0);

	ck_assert_ptr_nonnull (last_broadcast_msg);
	free (app.lastStationId);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
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

	BarSocketIoSetBroadcastCallback (mock_broadcast);
	clear_mock ();

	BarUiActVolDown (&app, NULL, NULL, 1);
	BarUiActVolUp (&app, NULL, NULL, 1);
	BarUiActVolReset (&app, NULL, NULL, 1);

	clear_mock ();
	player_primitives_destroy (&app);
	BarSettingsDestroy (&app.settings);
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

	ck_assert (strstr (ws_bucket_payload (&ctx, BUCKET_STATE), "process") != NULL);
	ws_context_destroy (&ctx);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
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

	ck_assert (strstr (ws_bucket_payload (&ctx, BUCKET_STATE), "process") != NULL);
	ws_context_destroy (&ctx);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
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

	BarSocketIoSetBroadcastCallback (mock_broadcast);
	BarSocketIoSetUnicastTarget ((void *) 0x1);
	clear_mock ();
	BarUiPianoCallSetTestHook (mock_explain_fail);
	BarUiActExplain (&app, &station, &song, 1);
	BarUiPianoCallClearTestHook ();
	BarSocketIoSetUnicastTarget (NULL);

	ck_assert_ptr_nonnull (last_broadcast_msg);
	ck_assert (strstr (last_broadcast_msg, "error") != NULL);
	ck_assert (strstr (last_broadcast_msg, "song.explain") != NULL);

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

	BarSocketIoSetBroadcastCallback (mock_broadcast);
	BarSocketIoSetUnicastTarget ((void *) 0x1);
	clear_mock ();
	BarUiPianoCallSetTestHook (mock_explain_with_text);
	BarUiActExplain (&app, &station, &song, 1);
	BarUiPianoCallClearTestHook ();
	BarSocketIoSetUnicastTarget (NULL);

	ck_assert_ptr_nonnull (last_broadcast_msg);
	ck_assert (strstr (last_broadcast_msg, "Because you enjoy jazz.") != NULL);

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

	BarSocketIoSetBroadcastCallback (mock_broadcast);
	BarSocketIoSetUnicastTarget ((void *) 0x1);
	clear_mock ();
	BarUiActPrintUpcoming (&app, &station, &cur, 1);
	BarSocketIoSetUnicastTarget (NULL);

	ck_assert_ptr_nonnull (last_broadcast_msg);
	ck_assert (strstr (last_broadcast_msg, "error") != NULL);
	ck_assert (strstr (last_broadcast_msg, "query.upcoming") != NULL);

	clear_mock ();
	ws_context_destroy (&ctx);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_ui_do_pandora_disconnect_playlist_session_error_message)
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

	BarSocketIoSetBroadcastCallback (mock_broadcast);
	clear_mock ();

	BarUiDoPandoraDisconnect (&app, "playlist_session_error", NULL);

	ck_assert_ptr_nonnull (last_broadcast_msg);

	free (app.lastStationId);
	pthread_mutex_destroy (&app.pianoHttpMutex);
	player_primitives_destroy (&app);
	BarStateDestroy (&app);
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
	tcase_add_test (tc, test_disconnect_player_wait_returns_fast_when_already_dead);
	tcase_add_test (tc, test_transform_owned_station_skips_pandora_call);
	tcase_add_test (tc, test_ui_do_pandora_disconnect_when_player_already_dead);
	tcase_add_test (tc, test_ui_do_pandora_disconnect_idle_timeout_message);
	tcase_add_test (tc, test_ui_switch_station_sets_next_and_drains_playlist);
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
	tcase_add_test (tc, test_ui_do_pandora_disconnect_playlist_session_error_message);
	tcase_add_test (tc, test_ui_do_pandora_disconnect_piano_reinit_failure);
	suite_add_tcase (s, tc);
	return s;
}
