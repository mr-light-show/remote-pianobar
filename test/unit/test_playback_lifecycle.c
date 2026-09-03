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
#include <curl/curl.h>
#include <limits.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

#include "../../src/bar_state.h"
#include "../../src/interrupt.h"
#include "../../src/playback_lifecycle.h"
#include "../../src/settings.h"
#include "../../src/l10n.h"
#include "../../src/player.h"
#include "../../src/ui.h"

static void setup_playback_app (BarApp_t *app) {
	memset (app, 0, sizeof (*app));
	BarSettingsInit (&app->settings);
#ifdef WEBSOCKET_ENABLED
	app->settings.uiMode = BAR_UI_MODE_CLI;
#endif
	app->settings.npSongFormat = strdup ("%t");
	app->settings.loveIcon = strdup ("+");
	app->settings.banIcon = strdup ("-");
	app->settings.tiredIcon = strdup ("t");
	app->settings.atIcon = strdup ("@");
	ck_assert (BarL10nInit (&app->l10n, &app->settings));
	BarStateInit (app);
	setenv ("PIANOBAR_TEST_NO_DEVICE", "1", 1);
	BarPlayerInit (&app->player, &app->settings);
}

static void teardown_playback_app (BarApp_t *app) {
	BarStateSetPlaylist (app, NULL);
	BarPlayerDestroy (&app->player);
	BarStateDestroy (app);
	BarL10nDestroy (&app->l10n);
	BarSettingsDestroy (&app->settings);
}

/*	Session recovery needs credentials, partner keys, a Piano handle and its
 *	mutex.  BarSettingsRead supplies the partner keys PianoInit requires, so
 *	point it at a throwaway config instead of the developer's own.
 */
static void setup_playback_app_with_piano (BarApp_t *app) {
	char tmpl[] = "/tmp/piano_lifecycle_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	char sub[512];
	snprintf (sub, sizeof (sub), "%s/pianobar", tmpl);
	ck_assert_int_eq (mkdir (sub, 0700), 0);
	char cfg[512];
	snprintf (cfg, sizeof (cfg), "%s/config", sub);
	FILE *f = fopen (cfg, "w");
	ck_assert_ptr_nonnull (f);
	ck_assert_int_gt (fprintf (f, "user = testuser\npassword = testpass\n"), 0);
	fclose (f);
	ck_assert_int_eq (setenv ("HOME", tmpl, 1), 0);
	ck_assert_int_eq (setenv ("XDG_CONFIG_HOME", tmpl, 1), 0);

	memset (app, 0, sizeof (*app));
	BarSettingsInit (&app->settings);
	BarSettingsRead (&app->settings);
#ifdef WEBSOCKET_ENABLED
	app->settings.uiMode = BAR_UI_MODE_CLI;
#endif
	ck_assert (BarL10nInit (&app->l10n, &app->settings));
	BarStateInit (app);
	setenv ("PIANOBAR_TEST_NO_DEVICE", "1", 1);
	BarPlayerInit (&app->player, &app->settings);

	ck_assert_int_eq (pthread_mutex_init (&app->pianoHttpMutex, NULL), 0);
	ck_assert_int_eq (PianoInit (&app->ph, app->settings.partnerUser,
	                             app->settings.partnerPassword,
	                             app->settings.device, app->settings.inkey,
	                             app->settings.outkey), PIANO_RET_OK);
}

static void teardown_playback_app_with_piano (BarApp_t *app) {
	BarUiPianoCallClearTestHook ();
	BarStateDrainPlaylist (app);
	free (app->lastStationId);
	app->lastStationId = NULL;
	PianoDestroy (&app->ph);
	pthread_mutex_destroy (&app->pianoHttpMutex);
	teardown_playback_app (app);
}

static PianoStation_t *alloc_station (const char *id, const char *name) {
	PianoStation_t *station = calloc (1, sizeof (*station));
	ck_assert_ptr_nonnull (station);
	station->id = strdup (id);
	station->name = strdup (name);
	return station;
}

static PianoSong_t *alloc_song (const char *title) {
	PianoSong_t *song = calloc (1, sizeof (*song));
	ck_assert_ptr_nonnull (song);
	song->title = strdup (title);
	song->artist = strdup ("Artist");
	song->album = strdup ("Album");
	song->detailUrl = strdup ("");
	song->audioUrl = strdup ("http://127.0.0.1:9/recovered.mp3");
	song->length = 1;
	return song;
}

/* Mock knobs for the session-recovery hook below. */
static unsigned int g_fetch_attempts;
static unsigned int g_login_attempts;
static unsigned int g_fetch_failures;
static PianoReturn_t g_fetch_pRet;
static CURLcode g_fetch_wRet;
static bool g_login_succeeds;
static const char *g_recovered_station_id;

static void reset_session_mocks (void) {
	g_fetch_attempts = 0;
	g_login_attempts = 0;
	g_fetch_failures = 1;
	g_fetch_pRet = PIANO_RET_P_INTERNAL;
	g_fetch_wRet = CURLE_OK;
	g_login_succeeds = true;
	g_recovered_station_id = "station-recover";
}

/*	getPlaylist fails g_fetch_failures times, then returns one song.
 *	LOGIN/GET_STATIONS stand in for the reconnect that auto-recovery runs.
 */
static bool mock_session_recovery (BarApp_t * const app,
                                   const PianoRequestType_t type,
                                   void * const data,
                                   PianoReturn_t * const pRet,
                                   CURLcode * const wRet) {
	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;

	switch (type) {
		case PIANO_REQUEST_GET_PLAYLIST:
			++g_fetch_attempts;
			if (g_fetch_attempts <= g_fetch_failures) {
				*pRet = g_fetch_pRet;
				*wRet = g_fetch_wRet;
				return false;
			}
			((PianoRequestDataGetPlaylist_t *) data)->retPlaylist =
					alloc_song ("Recovered Song");
			return true;
		case PIANO_REQUEST_LOGIN:
			++g_login_attempts;
			if (!g_login_succeeds) {
				*pRet = PIANO_RET_P_INTERNAL;
				return false;
			}
			return true;
		case PIANO_REQUEST_GET_STATIONS:
			app->ph.stations = alloc_station (g_recovered_station_id,
					"Recover Station");
			return true;
		default:
			return false;
	}
}

/* A dead session is reconnected and the same station keeps playing */
START_TEST (test_playback_fetch_playlist_auto_recovers_session)
{
	BarApp_t app;
	PianoStation_t *station;

	setup_playback_app_with_piano (&app);
	station = alloc_station ("station-recover", "Recover Station");
	app.ph.stations = station;
	BarStateSetNextStation (&app, station);
	BarStateSetCurrentStation (&app, station);

	reset_session_mocks ();
	BarUiPianoCallSetTestHook (mock_session_recovery);

	ck_assert (BarPlaybackFetchPlaylist (&app));

	ck_assert_uint_eq (g_login_attempts, 1);
	ck_assert_uint_eq (g_fetch_attempts, 2);
	ck_assert_ptr_nonnull (BarStateGetPlaylist (&app));
	ck_assert_str_eq (BarStateGetPlaylist (&app)->title, "Recovered Song");
	ck_assert_ptr_nonnull (BarStateGetNextStation (&app));
	ck_assert_str_eq (BarStateGetNextStation (&app)->id, "station-recover");
	ck_assert (!atomic_load (&app.autoRecoverFailed));

	teardown_playback_app_with_piano (&app);
}
END_TEST

/* A failed reconnect disconnects and is not retried until a song plays */
START_TEST (test_playback_fetch_playlist_auto_recovers_only_once)
{
	BarApp_t app;
	PianoStation_t *station;

	setup_playback_app_with_piano (&app);
	station = alloc_station ("station-gone", "Gone Station");
	app.ph.stations = station;
	BarStateSetNextStation (&app, station);
	BarStateSetCurrentStation (&app, station);

	reset_session_mocks ();
	g_fetch_failures = UINT_MAX;
	g_login_succeeds = false;
	BarUiPianoCallSetTestHook (mock_session_recovery);

	ck_assert (!BarPlaybackFetchPlaylist (&app));
	ck_assert_uint_eq (g_login_attempts, 1);
	ck_assert (atomic_load (&app.autoRecoverFailed));
	/* Disconnect kept the station id so the user can reconnect */
	ck_assert_ptr_nonnull (app.lastStationId);
	ck_assert_str_eq (app.lastStationId, "station-gone");

	/* Second failure in the same outage must not attempt another reconnect */
	station = alloc_station ("station-gone", "Gone Station");
	app.ph.stations = station;
	BarStateSetNextStation (&app, station);

	ck_assert (!BarPlaybackFetchPlaylist (&app));
	ck_assert_uint_eq (g_login_attempts, 1);

	teardown_playback_app_with_piano (&app);
}
END_TEST

/* Reconnect worked but Pandora still refuses playlists: disconnect */
START_TEST (test_playback_fetch_playlist_disconnects_when_retry_fails)
{
	BarApp_t app;
	PianoStation_t *station;

	setup_playback_app_with_piano (&app);
	station = alloc_station ("station-recover", "Recover Station");
	app.ph.stations = station;
	BarStateSetNextStation (&app, station);
	BarStateSetCurrentStation (&app, station);

	reset_session_mocks ();
	g_fetch_failures = UINT_MAX;
	BarUiPianoCallSetTestHook (mock_session_recovery);

	ck_assert (!BarPlaybackFetchPlaylist (&app));

	ck_assert_uint_eq (g_login_attempts, 1);
	ck_assert_uint_eq (g_fetch_attempts, 2);
	ck_assert_ptr_null (BarStateGetPlaylist (&app));
	ck_assert_ptr_null (BarStateGetNextStation (&app));
	/* The reconnect itself worked, so recovery stays armed */
	ck_assert (!atomic_load (&app.autoRecoverFailed));
	ck_assert_ptr_nonnull (app.lastStationId);
	ck_assert_str_eq (app.lastStationId, "station-recover");

	teardown_playback_app_with_piano (&app);
}
END_TEST

/* Recovery is pointless while quitting: leave the session alone */
START_TEST (test_playback_fetch_playlist_interrupted_skips_recovery)
{
	BarApp_t app;
	PianoStation_t *station;

	setup_playback_app_with_piano (&app);
	station = alloc_station ("station-interrupted", "Interrupted Station");
	app.ph.stations = station;
	BarStateSetNextStation (&app, station);
	BarStateSetCurrentStation (&app, station);

	reset_session_mocks ();
	g_fetch_failures = UINT_MAX;
	g_fetch_pRet = PIANO_RET_OK;
	g_fetch_wRet = CURLE_ABORTED_BY_CALLBACK;
	BarUiPianoCallSetTestHook (mock_session_recovery);

	ck_assert (!BarPlaybackFetchPlaylist (&app));

	ck_assert_uint_eq (g_login_attempts, 0);
	ck_assert_ptr_null (BarStateGetNextStation (&app));
	/* No teardown ran, so no resume id was saved */
	ck_assert_ptr_null (app.lastStationId);

	teardown_playback_app_with_piano (&app);
}
END_TEST

START_TEST (test_playback_fetch_playlist_doquit_skips_recovery)
{
	BarApp_t app;
	PianoStation_t *station;

	setup_playback_app_with_piano (&app);
	station = alloc_station ("station-doquit", "Quit Station");
	app.ph.stations = station;
	BarStateSetNextStation (&app, station);
	BarStateSetCurrentStation (&app, station);
	atomic_store_explicit (&app.doQuit, 1, memory_order_relaxed);

	reset_session_mocks ();
	g_fetch_failures = UINT_MAX;
	g_fetch_pRet = PIANO_RET_OK;
	g_fetch_wRet = CURLE_COULDNT_CONNECT;
	BarUiPianoCallSetTestHook (mock_session_recovery);

	ck_assert (!BarPlaybackFetchPlaylist (&app));

	ck_assert_uint_eq (g_login_attempts, 0);
	ck_assert_ptr_null (BarStateGetNextStation (&app));
	ck_assert_ptr_null (app.lastStationId);

	teardown_playback_app_with_piano (&app);
}
END_TEST

/* Reconnect worked but the station is gone: stay connected, play nothing */
START_TEST (test_playback_fetch_playlist_recovers_without_station)
{
	BarApp_t app;
	PianoStation_t *station;

	setup_playback_app_with_piano (&app);
	station = alloc_station ("station-deleted", "Deleted Station");
	app.ph.stations = station;
	BarStateSetNextStation (&app, station);
	BarStateSetCurrentStation (&app, station);

	reset_session_mocks ();
	g_fetch_failures = UINT_MAX;
	g_recovered_station_id = "some-other-station";
	BarUiPianoCallSetTestHook (mock_session_recovery);

	ck_assert (!BarPlaybackFetchPlaylist (&app));

	ck_assert_uint_eq (g_login_attempts, 1);
	/* Only the first fetch ran: nothing to retry without a station */
	ck_assert_uint_eq (g_fetch_attempts, 1);
	ck_assert_ptr_null (BarStateGetNextStation (&app));
	ck_assert (!atomic_load (&app.autoRecoverFailed));

	teardown_playback_app_with_piano (&app);
}
END_TEST

/* BarPlaybackStartSong: null app must return false without crashing */
START_TEST (test_playback_start_rejects_null_app)
{
	pthread_t t = 0;
	ck_assert (!BarPlaybackStartSong (NULL, &t));
}
END_TEST

/* BarPlaybackStartSong: null playerThread must return false without crashing */
START_TEST (test_playback_start_rejects_null_thread)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	ck_assert (!BarPlaybackStartSong (&app, NULL));
}
END_TEST

/* BarPlaybackStartSong: app with no playlist must return false without crashing */
START_TEST (test_playback_start_rejects_null_playlist)
{
	BarApp_t app;
	pthread_t playerThread = 0;
	memset (&app, 0, sizeof (app));

	/* playlist is NULL after memset — must return false cleanly */
	ck_assert (!BarPlaybackStartSong (&app, &playerThread));
}
END_TEST

START_TEST (test_playback_start_rejects_missing_current_station)
{
	BarApp_t app;
	PianoSong_t song;
	pthread_t playerThread = 0;
	memset (&song, 0, sizeof (song));
	setup_playback_app (&app);

	app.playlist = &song;

	ck_assert (!BarPlaybackStartSong (&app, &playerThread));

	teardown_playback_app (&app);
}
END_TEST

START_TEST (test_playback_start_rejects_non_http_audio_url)
{
	BarApp_t app;
	PianoSong_t song;
	PianoStation_t station;
	pthread_t playerThread = 0;
	memset (&song, 0, sizeof (song));
	memset (&station, 0, sizeof (station));
	setup_playback_app (&app);

	station.id = "station-1";
	station.name = "Station One";
	song.title = "Bad URL Song";
	song.artist = "Artist";
	song.album = "Album";
	song.detailUrl = "";
	song.audioUrl = "file:///tmp/song.mp3";
	app.curStation = &station;
	app.playlist = &song;

	ck_assert (!BarPlaybackStartSong (&app, &playerThread));

	teardown_playback_app (&app);
}
END_TEST

START_TEST (test_playback_start_succeeds_with_http_url)
{
	BarApp_t app;
	PianoSong_t song;
	PianoStation_t station;
	pthread_t playerThread = 0;

	memset (&song, 0, sizeof (song));
	memset (&station, 0, sizeof (station));
	setup_playback_app (&app);

	station.id = "station-http";
	station.name = "HTTP Station";
	song.title = "HTTP Song";
	song.artist = "Artist";
	song.audioUrl = "http://127.0.0.1:9/unreachable.mp3";
	song.fileGain = 1.0;
	song.length = 30;

	BarStateSetCurrentStation (&app, &station);
	BarStateSetPlaylist (&app, &song);

	ck_assert (BarPlaybackStartSong (&app, &playerThread));
	ck_assert (playerThread != 0);
	ck_assert_int_eq (BarPlayerGetMode (&app.player), PLAYER_WAITING);

	BarInterruptSetTarget (&app.player.interrupted);
	pthread_mutex_lock (&app.player.lock);
	app.player.doQuit = true;
	pthread_cond_broadcast (&app.player.cond);
	pthread_mutex_unlock (&app.player.lock);
	(void) pthread_join (playerThread, NULL);

	teardown_playback_app (&app);
}
END_TEST

START_TEST (test_playback_start_quickmix_uses_song_station_lookup)
{
	BarApp_t app;
	PianoSong_t song;
	PianoStation_t station;
	PianoStation_t mixStation;
	pthread_t playerThread = 0;

	memset (&song, 0, sizeof (song));
	memset (&station, 0, sizeof (station));
	memset (&mixStation, 0, sizeof (mixStation));
	setup_playback_app (&app);

	station.id = "quickmix";
	station.name = "QuickMix";
	station.isQuickMix = true;
	mixStation.id = "child-station";
	mixStation.name = "Child Station";
	song.stationId = mixStation.id;
	song.title = "Mix Song";
	song.artist = "Artist";
	song.audioUrl = "http://127.0.0.1:9/mix.mp3";

	BarStateSetCurrentStation (&app, &station);
	BarStateSetPlaylist (&app, &song);
	app.ph.stations = &mixStation;

	ck_assert (BarPlaybackStartSong (&app, &playerThread));
	BarInterruptSetTarget (&app.player.interrupted);
	pthread_mutex_lock (&app.player.lock);
	app.player.doQuit = true;
	pthread_cond_broadcast (&app.player.cond);
	pthread_mutex_unlock (&app.player.lock);
	(void) pthread_join (playerThread, NULL);

	teardown_playback_app (&app);
}
END_TEST

/* Starting a song re-arms auto-recovery for the next session failure */
START_TEST (test_playback_start_song_rearms_auto_recover)
{
	BarApp_t app;
	PianoSong_t song;
	PianoStation_t station;
	pthread_t playerThread = 0;

	memset (&song, 0, sizeof (song));
	memset (&station, 0, sizeof (station));
	setup_playback_app (&app);

	station.id = "station-rearm";
	station.name = "Rearm Station";
	song.title = "Rearm Song";
	song.artist = "Artist";
	song.audioUrl = "http://127.0.0.1:9/unreachable.mp3";

	BarStateSetCurrentStation (&app, &station);
	BarStateSetPlaylist (&app, &song);
	atomic_store (&app.autoRecoverFailed, true);

	ck_assert (BarPlaybackStartSong (&app, &playerThread));
	ck_assert (!atomic_load (&app.autoRecoverFailed));

	BarInterruptSetTarget (&app.player.interrupted);
	pthread_mutex_lock (&app.player.lock);
	app.player.doQuit = true;
	pthread_cond_broadcast (&app.player.cond);
	pthread_mutex_unlock (&app.player.lock);
	(void) pthread_join (playerThread, NULL);

	teardown_playback_app (&app);
}
END_TEST

Suite *playback_lifecycle_suite (void) {
	Suite *s = suite_create ("playback_lifecycle");
	TCase *tc = tcase_create ("core");
	tcase_add_test (tc, test_playback_start_rejects_null_app);
	tcase_add_test (tc, test_playback_start_rejects_null_thread);
	tcase_add_test (tc, test_playback_start_rejects_null_playlist);
	tcase_add_test (tc, test_playback_start_rejects_missing_current_station);
	tcase_add_test (tc, test_playback_start_rejects_non_http_audio_url);
	tcase_add_test (tc, test_playback_start_succeeds_with_http_url);
	tcase_add_test (tc, test_playback_start_quickmix_uses_song_station_lookup);
	tcase_add_test (tc, test_playback_start_song_rearms_auto_recover);
	tcase_add_test (tc, test_playback_fetch_playlist_auto_recovers_session);
	tcase_add_test (tc, test_playback_fetch_playlist_auto_recovers_only_once);
	tcase_add_test (tc, test_playback_fetch_playlist_disconnects_when_retry_fails);
	tcase_add_test (tc, test_playback_fetch_playlist_interrupted_skips_recovery);
	tcase_add_test (tc, test_playback_fetch_playlist_doquit_skips_recovery);
	tcase_add_test (tc, test_playback_fetch_playlist_recovers_without_station);
	suite_add_tcase (s, tc);
	return s;
}
