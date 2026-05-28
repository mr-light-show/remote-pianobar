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
#include <piano.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../src/bar_state.h"
#include "../../src/interrupt.h"
#include "../../src/l10n.h"
#include "../../src/main.h"
#include "../../src/playback_lifecycle.h"
#include "../../src/playback_manager.h"
#include "../../src/player.h"
#include "../../src/settings.h"
#include "../../src/ui.h"
#include "fixture_http.h"

#ifdef WEBSOCKET_ENABLED

static const char *integration_fixture_path (void) {
	return "test/fixtures/tone.mp3";
}

static bool integration_enabled (void) {
	return getenv ("PIANOBAR_INTEGRATION") != NULL;
}

static bool integration_fixture_exists (void) {
	return access (integration_fixture_path (), R_OK) == 0;
}

static bool integration_audio_available (void) {
	player_t player;
	BarSettings_t settings;

	memset (&player, 0, sizeof (player));
	BarSettingsInit (&settings);
	BarPlayerInit (&player, &settings);
	const bool ok = player.engineInitialized;
	BarPlayerDestroy (&player);
	BarSettingsDestroy (&settings);
	return ok;
}

static void setup_integration_app (BarApp_t *barApp) {
	memset (barApp, 0, sizeof (*barApp));
	BarSettingsInit (&barApp->settings);
	BarSettingsRead (&barApp->settings);
	free (barApp->settings.npSongFormat);
	barApp->settings.npSongFormat = strdup ("%t");
	free (barApp->settings.loveIcon);
	barApp->settings.loveIcon = strdup ("+");
	free (barApp->settings.banIcon);
	barApp->settings.banIcon = strdup ("-");
	free (barApp->settings.tiredIcon);
	barApp->settings.tiredIcon = strdup ("t");
	free (barApp->settings.atIcon);
	barApp->settings.atIcon = strdup ("@");
	ck_assert (BarL10nInit (&barApp->l10n, &barApp->settings));
	BarStateInit (barApp);
	BarPlayerInit (&barApp->player, &barApp->settings);
}

static void setup_integration_app_with_piano (BarApp_t *barApp) {
	setup_integration_app (barApp);
	ck_assert_int_eq (pthread_mutex_init (&barApp->pianoHttpMutex, NULL), 0);
	ck_assert_int_eq (PianoInit (&barApp->ph, barApp->settings.partnerUser,
	                             barApp->settings.partnerPassword,
	                             barApp->settings.device,
	                             barApp->settings.inkey,
	                             barApp->settings.outkey),
	                  PIANO_RET_OK);
}

static void teardown_integration_app (BarApp_t *barApp) {
	BarUiPianoCallClearTestHook ();
	BarStateSetPlaylist (barApp, NULL);
	if (barApp->songHistory != NULL) {
		PianoDestroyPlaylist (barApp->songHistory);
		barApp->songHistory = NULL;
	}
	BarPlayerDestroy (&barApp->player);
	BarStateDestroy (barApp);
	BarL10nDestroy (&barApp->l10n);
	BarSettingsDestroy (&barApp->settings);
}

static void teardown_integration_app_with_piano (BarApp_t *barApp) {
	BarUiPianoCallClearTestHook ();
	BarStateSetPlaylist (barApp, NULL);
	free (barApp->lastStationId);
	barApp->lastStationId = NULL;
	pthread_mutex_lock (&barApp->pianoHttpMutex);
	PianoDestroy (&barApp->ph);
	pthread_mutex_unlock (&barApp->pianoHttpMutex);
	pthread_mutex_destroy (&barApp->pianoHttpMutex);
	BarPlayerDestroy (&barApp->player);
	BarStateDestroy (barApp);
	BarL10nDestroy (&barApp->l10n);
	BarSettingsDestroy (&barApp->settings);
}

static bool wait_for_mode (player_t *player, BarPlayerMode mode,
                           unsigned timeout_ms) {
	return BarPlayerWaitForMode (player, mode, timeout_ms);
}

static bool wait_dead_after_playing (BarApp_t *barApp, unsigned timeout_ms) {
	unsigned elapsed = 0;
	bool seen_playing = false;

	while (elapsed < timeout_ms) {
		const BarPlayerMode mode = BarPlayerGetMode (&barApp->player);
		if (mode == PLAYER_PLAYING) {
			seen_playing = true;
		}
		if (seen_playing && mode == PLAYER_DEAD) {
			return true;
		}
		usleep (50000);
		elapsed += 50;
	}
	return false;
}

static PianoStation_t g_station;
static PianoSong_t g_song;
static PianoSong_t g_song2;
static char g_audio_url[256];
static char g_audio_url2[256];
static int g_playlist_fetch_count;

static void fill_mock_song (PianoSong_t *song, const char *title,
                            const char *audio_url) {
	memset (song, 0, sizeof (*song));
	song->title = (char *)title;
	song->artist = "Integration Artist";
	song->album = "Integration Album";
	song->detailUrl = "";
	song->audioUrl = (char *)audio_url;
	song->length = 1;
}

static bool mock_get_playlist (BarApp_t * const barApp,
                               const PianoRequestType_t type,
                               void * const data,
                               PianoReturn_t * const pRet,
                               CURLcode * const wRet) {
	(void)barApp;

	if (type != PIANO_REQUEST_GET_PLAYLIST) {
		return false;
	}

	PianoRequestDataGetPlaylist_t *req = data;
	fill_mock_song (&g_song, "Integration Song", g_audio_url);

	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;
	req->retPlaylist = &g_song;
	return true;
}

static bool mock_get_playlist_empty (BarApp_t * const barApp,
                                     const PianoRequestType_t type,
                                     void * const data,
                                     PianoReturn_t * const pRet,
                                     CURLcode * const wRet) {
	(void)barApp;

	if (type != PIANO_REQUEST_GET_PLAYLIST) {
		return false;
	}

	PianoRequestDataGetPlaylist_t *req = data;
	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;
	req->retPlaylist = NULL;
	return true;
}

static bool mock_get_playlist_session_error (BarApp_t * const barApp,
                                             const PianoRequestType_t type,
                                             void * const data,
                                             PianoReturn_t * const pRet,
                                             CURLcode * const wRet) {
	(void)barApp;
	(void)data;

	if (type != PIANO_REQUEST_GET_PLAYLIST) {
		return false;
	}

	*pRet = PIANO_RET_P_INTERNAL;
	*wRet = CURLE_OK;
	return false;
}

static bool mock_get_playlist_generic_failure (BarApp_t * const barApp,
                                               const PianoRequestType_t type,
                                               void * const data,
                                               PianoReturn_t * const pRet,
                                               CURLcode * const wRet) {
	(void)barApp;
	(void)data;

	if (type != PIANO_REQUEST_GET_PLAYLIST) {
		return false;
	}

	*pRet = PIANO_RET_INVALID_RESPONSE;
	*wRet = CURLE_OK;
	return false;
}

static PianoSong_t *alloc_integration_song (const char *title,
                                            const char *audio_url) {
	PianoSong_t *song = calloc (1, sizeof (*song));
	ck_assert_ptr_nonnull (song);
	song->title = strdup (title);
	song->artist = strdup ("Integration Artist");
	song->album = strdup ("Integration Album");
	song->detailUrl = strdup ("");
	song->audioUrl = strdup (audio_url);
	song->length = 1;
	return song;
}

static bool mock_get_playlist_counted (BarApp_t * const barApp,
                                       const PianoRequestType_t type,
                                       void * const data,
                                       PianoReturn_t * const pRet,
                                       CURLcode * const wRet) {
	(void)barApp;

	if (type != PIANO_REQUEST_GET_PLAYLIST) {
		return false;
	}

	PianoRequestDataGetPlaylist_t *req = data;
	++g_playlist_fetch_count;

	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;

	if (g_playlist_fetch_count == 1) {
		req->retPlaylist = alloc_integration_song ("Integration Song",
		                                           g_audio_url);
		return true;
	}

	req->retPlaylist = NULL;
	return true;
}

START_TEST (test_playback_fetch_playlist_with_mock_piano)
{
	if (!integration_enabled ()) {
		return;
	}

	BarApp_t barApp;
	BarFixtureHttp_t http = {0};
	uint16_t port = 0;

	setup_integration_app (&barApp);

	ck_assert (integration_fixture_exists ());
	ck_assert (BarFixtureHttpStart (&http, integration_fixture_path (), &port));

	snprintf (g_audio_url, sizeof (g_audio_url),
	          "http://127.0.0.1:%u/tone.mp3", port);

	memset (&g_station, 0, sizeof (g_station));
	g_station.id = "station-integration";
	g_station.name = "Integration Station";
	BarStateSetNextStation (&barApp, &g_station);

	BarUiPianoCallSetTestHook (mock_get_playlist);
	ck_assert (BarPlaybackFetchPlaylist (&barApp));
	ck_assert_ptr_nonnull (BarStateGetPlaylist (&barApp));
	ck_assert_ptr_eq (BarStateGetCurrentStation (&barApp), &g_station);
	ck_assert_str_eq (BarStateGetPlaylist (&barApp)->title, "Integration Song");

	teardown_integration_app (&barApp);
	BarFixtureHttpStop (&http);
}
END_TEST

START_TEST (test_playback_fetch_empty_playlist_clears_station)
{
	if (!integration_enabled ()) {
		return;
	}

	BarApp_t barApp;

	setup_integration_app (&barApp);

	memset (&g_station, 0, sizeof (g_station));
	g_station.id = "station-empty";
	g_station.name = "Empty Station";
	BarStateSetNextStation (&barApp, &g_station);

	BarUiPianoCallSetTestHook (mock_get_playlist_empty);
	ck_assert (!BarPlaybackFetchPlaylist (&barApp));
	ck_assert_ptr_null (BarStateGetPlaylist (&barApp));
	ck_assert_ptr_null (BarStateGetNextStation (&barApp));

	teardown_integration_app (&barApp);
}
END_TEST

START_TEST (test_playback_fetch_session_error_disconnects)
{
	if (!integration_enabled ()) {
		return;
	}

	BarApp_t barApp;

	setup_integration_app_with_piano (&barApp);

	memset (&g_station, 0, sizeof (g_station));
	g_station.id = "station-session-error";
	g_station.name = "Session Error Station";
	BarStateSetNextStation (&barApp, &g_station);
	BarStateSetCurrentStation (&barApp, &g_station);

	BarUiPianoCallSetTestHook (mock_get_playlist_session_error);
	ck_assert (!BarPlaybackFetchPlaylist (&barApp));
	ck_assert_ptr_null (BarStateGetPlaylist (&barApp));
	ck_assert_ptr_null (BarStateGetNextStation (&barApp));
	ck_assert_ptr_nonnull (barApp.lastStationId);
	ck_assert_str_eq (barApp.lastStationId, "station-session-error");

	teardown_integration_app_with_piano (&barApp);
}
END_TEST

START_TEST (test_playback_fetch_generic_failure_clears_next_station)
{
	if (!integration_enabled ()) {
		return;
	}

	BarApp_t barApp;

	setup_integration_app (&barApp);

	memset (&g_station, 0, sizeof (g_station));
	g_station.id = "station-failure";
	g_station.name = "Failure Station";
	BarStateSetNextStation (&barApp, &g_station);

	BarUiPianoCallSetTestHook (mock_get_playlist_generic_failure);
	ck_assert (!BarPlaybackFetchPlaylist (&barApp));
	ck_assert_ptr_null (BarStateGetNextStation (&barApp));

	teardown_integration_app (&barApp);
}
END_TEST

START_TEST (test_playback_start_song_plays_http_fixture)
{
	if (!integration_enabled ()) {
		return;
	}
	if (!integration_audio_available ()) {
		return;
	}

	BarApp_t barApp;
	BarFixtureHttp_t http = {0};
	uint16_t port = 0;
	pthread_t playerThread = 0;

	setup_integration_app (&barApp);

	ck_assert (integration_fixture_exists ());
	ck_assert (BarFixtureHttpStart (&http, integration_fixture_path (), &port));

	snprintf (g_audio_url, sizeof (g_audio_url),
	          "http://127.0.0.1:%u/tone.mp3", port);

	memset (&g_station, 0, sizeof (g_station));
	g_station.id = "station-integration";
	g_station.name = "Integration Station";

	fill_mock_song (&g_song, "Integration Song", g_audio_url);

	BarStateSetCurrentStation (&barApp, &g_station);
	BarStateSetPlaylist (&barApp, &g_song);

	ck_assert (BarPlaybackStartSong (&barApp, &playerThread));
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 30000));
	ck_assert_int_eq (BarPlayerGetMode (&barApp.player), PLAYER_FINISHED);

	BarInterruptSetTarget (&barApp.player.interrupted);
	ck_assert_int_eq (pthread_join (playerThread, NULL), 0);

	teardown_integration_app (&barApp);
	BarFixtureHttpStop (&http);
}
END_TEST

START_TEST (test_player_http_404_finishes_without_hang)
{
	if (!integration_enabled ()) {
		return;
	}
	if (!integration_audio_available ()) {
		return;
	}

	BarApp_t barApp;
	BarFixtureHttp_t http = {0};
	uint16_t port = 0;
	pthread_t playerThread = 0;

	setup_integration_app (&barApp);

	ck_assert (BarFixtureHttpStart (&http, integration_fixture_path (), &port));
	http.mode = BAR_FIXTURE_HTTP_NOT_FOUND;

	snprintf (g_audio_url, sizeof (g_audio_url),
	          "http://127.0.0.1:%u/missing.mp3", port);

	memset (&g_station, 0, sizeof (g_station));
	g_station.id = "station-404";
	g_station.name = "404 Station";
	fill_mock_song (&g_song, "Missing Song", g_audio_url);

	BarStateSetCurrentStation (&barApp, &g_station);
	BarStateSetPlaylist (&barApp, &g_song);

	ck_assert (BarPlaybackStartSong (&barApp, &playerThread));
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 30000));
	ck_assert_int_eq (pthread_join (playerThread, NULL), 0);

	teardown_integration_app (&barApp);
	BarFixtureHttpStop (&http);
}
END_TEST

START_TEST (test_player_interrupt_mid_playback)
{
	if (!integration_enabled ()) {
		return;
	}
	if (!integration_audio_available ()) {
		return;
	}

	BarApp_t barApp;
	BarFixtureHttp_t http = {0};
	uint16_t port = 0;
	pthread_t playerThread = 0;

	setup_integration_app (&barApp);

	ck_assert (integration_fixture_exists ());
	ck_assert (BarFixtureHttpStart (&http, integration_fixture_path (), &port));

	snprintf (g_audio_url, sizeof (g_audio_url),
	          "http://127.0.0.1:%u/tone.mp3", port);

	memset (&g_station, 0, sizeof (g_station));
	g_station.id = "station-interrupt";
	g_station.name = "Interrupt Station";
	fill_mock_song (&g_song, "Interrupt Song", g_audio_url);

	BarStateSetCurrentStation (&barApp, &g_station);
	BarStateSetPlaylist (&barApp, &g_song);

	ck_assert (BarPlaybackStartSong (&barApp, &playerThread));
	ck_assert (wait_for_mode (&barApp.player, PLAYER_PLAYING, 30000));

	BarInterruptSetTarget (&barApp.player.interrupted);
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 30000));
	ck_assert_int_eq (pthread_join (playerThread, NULL), 0);

	teardown_integration_app (&barApp);
	BarFixtureHttpStop (&http);
}
END_TEST

START_TEST (test_playback_two_song_manual_advance)
{
	if (!integration_enabled ()) {
		return;
	}
	if (!integration_audio_available ()) {
		return;
	}

	BarApp_t barApp;
	BarFixtureHttp_t http = {0};
	uint16_t port = 0;
	pthread_t playerThread = 0;

	setup_integration_app (&barApp);

	ck_assert (integration_fixture_exists ());
	ck_assert (BarFixtureHttpStart (&http, integration_fixture_path (), &port));

	snprintf (g_audio_url, sizeof (g_audio_url),
	          "http://127.0.0.1:%u/tone.mp3", port);
	snprintf (g_audio_url2, sizeof (g_audio_url2),
	          "http://127.0.0.1:%u/tone.mp3", port);

	memset (&g_station, 0, sizeof (g_station));
	g_station.id = "station-two-songs";
	g_station.name = "Two Song Station";

	fill_mock_song (&g_song, "Song One", g_audio_url);
	fill_mock_song (&g_song2, "Song Two", g_audio_url2);
	g_song.head.next = &g_song2.head;
	g_song2.head.next = NULL;

	BarStateSetCurrentStation (&barApp, &g_station);
	BarStateSetPlaylist (&barApp, &g_song);

	ck_assert (BarPlaybackStartSong (&barApp, &playerThread));
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 30000));
	ck_assert_int_eq (pthread_join (playerThread, NULL), 0);

	BarStateSetPlaylist (&barApp, PianoListNextP (&g_song));
	ck_assert_ptr_eq (BarStateGetPlaylist (&barApp), &g_song2);

	ck_assert (BarPlaybackStartSong (&barApp, &playerThread));
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 30000));
	ck_assert_int_eq (pthread_join (playerThread, NULL), 0);

	teardown_integration_app (&barApp);
	BarFixtureHttpStop (&http);
}
END_TEST

START_TEST (test_playback_manager_plays_fixture_song)
{
	if (!integration_enabled ()) {
		return;
	}
	if (!integration_audio_available ()) {
		return;
	}

	BarApp_t barApp;
	BarFixtureHttp_t http = {0};
	uint16_t port = 0;

	setup_integration_app (&barApp);

	ck_assert (integration_fixture_exists ());
	ck_assert (BarFixtureHttpStart (&http, integration_fixture_path (), &port));

	snprintf (g_audio_url, sizeof (g_audio_url),
	          "http://127.0.0.1:%u/tone.mp3", port);

	g_playlist_fetch_count = 0;
	memset (&g_station, 0, sizeof (g_station));
	g_station.id = "station-manager";
	g_station.name = "Manager Station";
	BarStateSetNextStation (&barApp, &g_station);

	BarUiPianoCallSetTestHook (mock_get_playlist_counted);
	ck_assert (BarPlaybackManagerStart (&barApp));

	ck_assert (wait_dead_after_playing (&barApp, 30000));
	ck_assert_int_ge (g_playlist_fetch_count, 1);

	BarPlaybackManagerStop (&barApp);
	BarStateDrainPlaylist (&barApp);

	teardown_integration_app (&barApp);
	BarFixtureHttpStop (&http);
}
END_TEST

Suite *playback_integration_suite (void) {
	Suite *s = suite_create ("playback_integration");
	TCase *tc = tcase_create ("core");
	tcase_add_test (tc, test_playback_fetch_playlist_with_mock_piano);
	tcase_add_test (tc, test_playback_fetch_empty_playlist_clears_station);
	tcase_add_test (tc, test_playback_fetch_session_error_disconnects);
	tcase_add_test (tc, test_playback_fetch_generic_failure_clears_next_station);
	tcase_add_test (tc, test_playback_start_song_plays_http_fixture);
	tcase_add_test (tc, test_player_http_404_finishes_without_hang);
	tcase_add_test (tc, test_player_interrupt_mid_playback);
	tcase_add_test (tc, test_playback_two_song_manual_advance);
	tcase_add_test (tc, test_playback_manager_plays_fixture_song);
	suite_add_tcase (s, tc);
	return s;
}

#else

Suite *playback_integration_suite (void) {
	return NULL;
}

#endif
