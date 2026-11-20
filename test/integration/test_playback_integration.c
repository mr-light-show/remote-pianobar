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
#include <arpa/inet.h>
#include <curl/curl.h>
#include <piano.h>
#include <pthread.h>
#include <errno.h>
#include <stdatomic.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <libavfilter/buffersink.h>
#include <libavutil/error.h>

#include "../../src/bar_state.h"
#include "../../src/interrupt.h"
#include "../../src/l10n.h"
#include "../../src/main.h"
#include "../../src/playback_lifecycle.h"
#include "../../src/playback_manager.h"
#include "../../src/player.h"
#include "../../src/settings.h"
#include "../../src/ui.h"
#include "../../src/websocket_bridge.h"
#include "fixture_http.h"
#include "../audio_fixture_callback.h"
#include "../test_helpers.h"


void BarUiActPlay (BarApp_t *app, PianoStation_t *selStation,
                  PianoSong_t *selSong, int context);
void BarUiActPause (BarApp_t *app, PianoStation_t *selStation,
                   PianoSong_t *selSong, int context);
void BarUiActSkipSong (BarApp_t *app, PianoStation_t *selStation,
                      PianoSong_t *selSong, int context);
void BarUiActPandoraDisconnect (BarApp_t *app, PianoStation_t *station, PianoSong_t *song, int context);
void BarUiActQuit (BarApp_t *app, PianoStation_t *station, PianoSong_t *song, int context);

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
	if (mode == PLAYER_FINISHED && player->audioNoDevice) {
		for (unsigned elapsed = 0; elapsed < timeout_ms; elapsed += 5) {
			const BarPlayerMode current = BarPlayerGetMode (player);
			if (current == PLAYER_FINISHED) { return true; }
			if (current == PLAYER_PLAYING) {
				pthread_mutex_lock (&player->lock);
				const bool stopping = player->doQuit;
				pthread_mutex_unlock (&player->lock);
				if (!stopping && !BarTestDrainAudioFixture (player)) { return false; }
				break;
			}
			usleep (5000);
		}
	}
	return BarPlayerWaitForMode (player, mode, timeout_ms);
}

static void stop_player_thread (player_t *player, pthread_t *playerThread) {
	if (playerThread == NULL || *playerThread == 0) {
		return;
	}

	BarInterruptSetTarget (&player->interrupted);
	pthread_mutex_lock (&player->lock);
	player->doQuit = true;
	pthread_cond_broadcast (&player->cond);
	pthread_mutex_unlock (&player->lock);
	ck_assert (BarPlayerJoinThreadWithTimeout (player, *playerThread, NULL, 10));
	*playerThread = 0;
	/* mirror force_join_player_thread: the player owns a copy of the URL */
	free (player->url);
	player->url = NULL;
}

static bool wait_dead_after_playing (BarApp_t *barApp, unsigned timeout_ms) {
	unsigned elapsed = 0;
	bool seen_playing = false;

	while (elapsed < timeout_ms) {
		const BarPlayerMode mode = BarPlayerGetMode (&barApp->player);
		if (mode == PLAYER_PLAYING) {
			if (!seen_playing && !BarTestDrainAudioFixture (&barApp->player)) { return false; }
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
/* Written by the playback manager thread, read by the test thread */
static _Atomic int g_playlist_fetch_count;

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
	const int fetch = atomic_fetch_add (&g_playlist_fetch_count, 1) + 1;

	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;

	if (fetch == 1) {
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

	setup_integration_app (&barApp);

	test_refused_url (g_audio_url, sizeof (g_audio_url), "tone.mp3");

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

	setup_integration_app_with_piano (&barApp);

	memset (&g_station, 0, sizeof (g_station));
	g_station.id = "station-failure";
	g_station.name = "Failure Station";
	BarStateSetNextStation (&barApp, &g_station);

	BarUiPianoCallSetTestHook (mock_get_playlist_generic_failure);
	ck_assert (!BarPlaybackFetchPlaylist (&barApp));
	ck_assert_ptr_null (BarStateGetNextStation (&barApp));

	teardown_integration_app_with_piano (&barApp);
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
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 10000));

	stop_player_thread (&barApp.player, &playerThread);
	/* The end callback precedes cleanup's transient WAITING mode. Join before
	 * checking the final published state. */
	ck_assert_int_eq (BarPlayerGetMode (&barApp.player), PLAYER_FINISHED);

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
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 10000));
	stop_player_thread (&barApp.player, &playerThread);

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
	ck_assert (wait_for_mode (&barApp.player, PLAYER_PLAYING, 10000));

	BarInterruptSetTarget (&barApp.player.interrupted);
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 10000));
	stop_player_thread (&barApp.player, &playerThread);

	teardown_integration_app (&barApp);
	BarFixtureHttpStop (&http);
}
END_TEST

/* A skipped observation is not completion. Retry only while the logical
 * predicates remain current; the production API owns the lifetime. */
static bool integration_audio_snapshot (player_t *player, BarPlayerAudioSnapshot *snapshot,
		bool *early_completion)
{
	if (*early_completion) { return false; }
	for (unsigned elapsed = 0; elapsed < 1000; elapsed += 5) {
		if (BarPlayerGetAudioSnapshot (player, snapshot)) { return true; }
		pthread_mutex_lock (&player->lock);
		const bool current = player->mode == PLAYER_PLAYING && !player->doQuit;
		pthread_mutex_unlock (&player->lock);
		if (!current) { *early_completion = true; return false; }
		usleep (5000);
	}
	return false;
}

static bool integration_sound_state (player_t *player, bool *playing,
                                     bool *early_completion)
{
	BarPlayerAudioSnapshot snapshot;
	if (!integration_audio_snapshot (player, &snapshot, early_completion)) { return false; }
	*playing = snapshot.playing;
	return true;
}

static bool integration_sound_cursor (player_t *player, ma_uint64 *cursor,
                                      bool *early_completion)
{
	BarPlayerAudioSnapshot snapshot;
	if (!integration_audio_snapshot (player, &snapshot, early_completion)) { return false; }
	*cursor = snapshot.cursorFrames;
	return true;
}

static bool integration_wait_for_cursor_advance (player_t *player,
                                                ma_uint64 saved_cursor,
                                                bool *early_completion)
{
	for (unsigned elapsed = 0; elapsed < 1000; elapsed += 5) {
		ma_uint64 cursor = 0;
		if (!integration_sound_cursor (player, &cursor, early_completion)) {
			return false;
		}
		if (cursor < saved_cursor) {
			return false;
		}
		if (cursor > saved_cursor) {
			return true;
		}
		usleep (5000);
	}
	return false;
}

/* Break caught: paused decoding continues processing buffered HTTP packets;
 * resuming fails to finish the fixture song. */
START_TEST (test_player_pause_parks_decoder_until_same_http_song_resumes)
{
	if (!integration_enabled ()) { return; }
	BarApp_t barApp;
	BarFixtureHttp_t http = {0};
	uint16_t port = 0;
	pthread_t playerThread = 0;
	setup_integration_app (&barApp);
	ck_assert_msg (barApp.player.engineInitialized, "Pause decoding requires an initialized audio backend");
	ck_assert (integration_fixture_exists ());
	ck_assert (BarFixtureHttpStart (&http, integration_fixture_path (), &port));
	snprintf (g_audio_url, sizeof (g_audio_url), "http://127.0.0.1:%u/tone.mp3", port);
	barApp.player.url = strdup (g_audio_url);
	ck_assert_ptr_nonnull (barApp.player.url);
	BarPlayerSetMode (&barApp.player, PLAYER_WAITING);
	ck_assert (BarPlayerSetPaused (&barApp.player, true));
	ck_assert_int_eq (pthread_create (&playerThread, NULL, BarPlayerThread, &barApp.player), 0);
	ck_assert (wait_for_mode (&barApp.player, PLAYER_PLAYING, 10000));
	AVFrame *frame = av_frame_alloc ();
	ck_assert_ptr_nonnull (frame);
	bool no_packets_processed = true;
	for (unsigned interval = 0; interval < 5; ++interval) {
		usleep (100000);
		/* The real filter output is an equivalent decoder counter: zero frames
		 * and no EOF throughout 500 ms, even if FFmpeg prefetched HTTP bytes. */
		pthread_mutex_lock (&barApp.player.decoderLock);
		const int peek = av_buffersink_get_frame_flags (barApp.player.fbufsink, frame, AV_BUFFERSINK_FLAG_PEEK);
		no_packets_processed = no_packets_processed && peek == AVERROR (EAGAIN) && !barApp.player.decodingFinished;
		pthread_mutex_unlock (&barApp.player.decoderLock);
		av_frame_unref (frame);
	}
	av_frame_free (&frame);
	ck_assert (BarPlayerSetPaused (&barApp.player, false));
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 10000));
	void *result;
	ck_assert_int_eq (pthread_join (playerThread, &result), 0);
	ck_assert_str_eq (barApp.player.url, g_audio_url);
	free (barApp.player.url);
	barApp.player.url = NULL;
	teardown_integration_app (&barApp);
	BarFixtureHttpStop (&http);
	ck_assert_int_eq ((uintptr_t)result, PLAYER_RET_OK);
	ck_assert_msg (no_packets_processed, "A sustained pause must process no HTTP fixture packets before resume");
}
END_TEST

START_TEST (test_player_pause_resume_stops_device_and_preserves_cursor)
{
	if (!integration_enabled ()) {
		return;
	}

	BarApp_t barApp;
	pthread_t playerThread = 0;
	char fixture_path[PATH_MAX];
	char url[PATH_MAX + 16];
	bool stopped[3] = {false};
	bool stable[3] = {false};
	bool started[3] = {false};
	bool advanced[3] = {false};
	bool paused_sound_live[3] = {false};
	bool paused_sound_stopped[3] = {false};
	bool resumed_sound_live[3] = {false};
	bool resumed_sound_started[3] = {false};
	bool early_completion = false;
	bool cursor_reads_ok = true;

	memset (&barApp, 0, sizeof (barApp));
	BarSettingsInit (&barApp.settings);
	barApp.settings.uiMode = BAR_UI_MODE_CLI;
	BarPlayerInit (&barApp.player, &barApp.settings);
	const bool expect_live_sound = barApp.player.engineInitialized;
	ma_device *device = barApp.player.engineInitialized
	                   ? ma_engine_get_device (&barApp.player.engine) : NULL;
	const bool real_device = device != NULL;
	bool sample_cursor = real_device;

	ck_assert (integration_fixture_exists ());
	ck_assert_ptr_nonnull (realpath (integration_fixture_path (), fixture_path));
	ck_assert (snprintf (url, sizeof (url), "file://%s", fixture_path) < (int) sizeof (url));
	barApp.player.url = strdup (url);
	ck_assert_ptr_nonnull (barApp.player.url);
	BarPlayerSetMode (&barApp.player, PLAYER_WAITING);
	ck_assert_int_eq (pthread_create (&playerThread, NULL, BarPlayerThread,
	                                 &barApp.player), 0);
	if (barApp.player.engineInitialized) {
		ck_assert (wait_for_mode (&barApp.player, PLAYER_PLAYING, 10000));
	} else {
		/* Even an unavailable backend must still finish and permit control/cleanup. */
		ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 10000));
	}

	/* The 500 ms fixture only needs a small positive advance between pauses. */
	for (unsigned cycle = 0; cycle < 3; ++cycle) {
		ma_uint64 playing_cursor = 0;
		if (sample_cursor && !integration_sound_cursor (&barApp.player,
		                                               &playing_cursor, &early_completion)) {
			cursor_reads_ok = false;
			sample_cursor = false;
		}
		BarUiActPause (&barApp, NULL, NULL, 1);
		ck_assert (BarPlayerIsPaused (&barApp.player));
		ck_assert (barApp.player.pauseStartTime > 0);
		bool sound_playing = false;
		if (expect_live_sound) {
			paused_sound_live[cycle] = integration_sound_state (&barApp.player,
			                                                  &sound_playing, &early_completion);
			paused_sound_stopped[cycle] = paused_sound_live[cycle] && !sound_playing;
		}
		if (early_completion) {
			sample_cursor = false;
		}
		ma_uint64 saved_cursor = 0;
		if (real_device) {
			stopped[cycle] = !ma_device_is_started (device);
		}
		if (sample_cursor) {
			if (!integration_sound_cursor (&barApp.player, &saved_cursor, &early_completion)
			    || saved_cursor < playing_cursor) {
				cursor_reads_ok = false;
				sample_cursor = false;
			}
		}

		struct timespec hold_start, hold_end;
		ck_assert_int_eq (clock_gettime (CLOCK_MONOTONIC, &hold_start), 0);
		usleep (500000);
		ck_assert_int_eq (clock_gettime (CLOCK_MONOTONIC, &hold_end), 0);
		const long held_ns = (hold_end.tv_sec - hold_start.tv_sec) * 1000000000L
		                   + hold_end.tv_nsec - hold_start.tv_nsec;
		ck_assert (held_ns >= 500000000L);
		if (expect_live_sound) {
			const bool live = integration_sound_state (&barApp.player, &sound_playing,
			                                         &early_completion);
			paused_sound_live[cycle] = paused_sound_live[cycle] && live;
			paused_sound_stopped[cycle] = paused_sound_stopped[cycle] && live && !sound_playing;
		}
		if (early_completion) {
			sample_cursor = false;
		}
		if (sample_cursor) {
			ma_uint64 cursor = 0;
			if (!integration_sound_cursor (&barApp.player, &cursor, &early_completion)) {
				cursor_reads_ok = false;
				sample_cursor = false;
			} else {
				stable[cycle] = cursor == saved_cursor;
			}
		}

		BarUiActPlay (&barApp, NULL, NULL, 1);
		ck_assert (!BarPlayerIsPaused (&barApp.player));
		ck_assert_int_eq (barApp.player.pauseStartTime, 0);
		if (expect_live_sound) {
			resumed_sound_live[cycle] = integration_sound_state (&barApp.player,
			                                                   &sound_playing, &early_completion);
			resumed_sound_started[cycle] = resumed_sound_live[cycle] && sound_playing;
		}
		if (early_completion) {
			sample_cursor = false;
		}
		if (real_device) {
			started[cycle] = ma_device_is_started (device);
		}
		if (sample_cursor) {
			advanced[cycle] = integration_wait_for_cursor_advance (&barApp.player,
			                                                       saved_cursor, &early_completion);
			if (!advanced[cycle]) {
				/* A failed resume never starts another polling interval/cycle. */
				sample_cursor = false;
			}
		}
	}

	BarUiActPause (&barApp, NULL, NULL, 1);
	ck_assert (BarPlayerIsPaused (&barApp.player));
	ck_assert (barApp.player.pauseStartTime > 0);
	struct timespec stop_start, stop_end;
	ck_assert_int_eq (clock_gettime (CLOCK_MONOTONIC, &stop_start), 0);
	BarUiActSkipSong (&barApp, NULL, NULL, 1);
	ck_assert (barApp.player.doQuit);
	ck_assert (!BarPlayerIsPaused (&barApp.player));
	ck_assert_int_eq (barApp.player.pauseStartTime, 0);
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 10000));
	stop_player_thread (&barApp.player, &playerThread);
	ck_assert_int_eq (BarPlayerGetMode (&barApp.player), PLAYER_FINISHED);
	ck_assert (!barApp.player.soundInitialized);
	free (barApp.player.url);
	barApp.player.url = NULL;
	BarPlayerDestroy (&barApp.player);
	ck_assert (!barApp.player.engineInitialized);
	BarSettingsDestroy (&barApp.settings);
	ck_assert_int_eq (clock_gettime (CLOCK_MONOTONIC, &stop_end), 0);
	const long stop_ms = (stop_end.tv_sec - stop_start.tv_sec) * 1000L
	                   + (stop_end.tv_nsec - stop_start.tv_nsec) / 1000000L;
	ck_assert (stop_ms < 2000);

	ck_assert_msg (!early_completion,
	               "Playback must not finish early during pause/resume observations");
	if (expect_live_sound) {
		for (unsigned cycle = 0; cycle < 3; ++cycle) {
			ck_assert_msg (paused_sound_live[cycle], "Pause %u must keep the sound initialized", cycle + 1);
			ck_assert_msg (paused_sound_stopped[cycle], "Pause %u must stop the sound node", cycle + 1);
			ck_assert_msg (resumed_sound_live[cycle], "Resume %u must keep the sound initialized", cycle + 1);
			ck_assert_msg (resumed_sound_started[cycle], "Resume %u must start the sound node", cycle + 1);
		}
	}
	/* Keep cleanup assertions active even when the real-device checks fail. */
	if (real_device) {
		ck_assert_msg (cursor_reads_ok, "Live-sound cursor observations must succeed and not reset");
		for (unsigned cycle = 0; cycle < 3; ++cycle) {
			ck_assert_msg (stopped[cycle], "Pause %u must stop the audio device", cycle + 1);
			ck_assert_msg (stable[cycle], "Pause %u must preserve the cursor for 500 ms", cycle + 1);
			ck_assert_msg (started[cycle], "Resume %u must start the audio device", cycle + 1);
			ck_assert_msg (advanced[cycle], "Resume %u must advance from the saved cursor", cycle + 1);
		}
	}
}
END_TEST

/* Joins like the playback manager (publishing DEAD, which disconnect waits
 * for), but with a short bound and no force interrupt, so a slow stop shows. */
typedef struct { player_t *player; pthread_t thread; bool joined; void *ret; } IntegrationJoin;
static void *integration_join_player (void *data) {
	IntegrationJoin *join = data;
	join->joined = BarPlayerJoinThreadWithTimeout (join->player, join->thread, &join->ret, 2);
	if (join->joined) { BarPlayerSetMode (join->player, PLAYER_DEAD); }
	return NULL;
}

typedef enum {
	/* All audio except the final byte; EOF withheld until stop. */
	AUDIO_STREAM_UNFINISHED,
	/* headCopies of the fixture, then no data (connection open) until resume
	 * releases tailCopies, which completes Content-Length (natural EOF). */
	AUDIO_STREAM_STALL_READ,
	/* Accepts and reads the request but never answers. */
	AUDIO_STREAM_STALL_OPEN,
} AudioStreamMode;

typedef struct {
	int listenFd;
	_Atomic int clientFd;
	_Atomic bool stop, failed;
	/* Published before the test skips, disconnects, or quits the player, so a
	 * resulting EPIPE is not mistaken for a fixture failure. Unlike stop, it
	 * neither ends the thread nor releases the withheld EOF. */
	_Atomic bool cancelExpected;
	AudioStreamMode mode;
	unsigned headCopies, tailCopies;
	_Atomic bool resume;          /* STALL_READ: send the tail */
	_Atomic bool stalled;         /* request read and head (if any) sent */
	_Atomic bool tailSent;
	_Atomic unsigned accepts;     /* connections accepted (retries counted) */
	pthread_t thread;
} UnfinishedAudioStream;

/* Returns 0 on success, otherwise the errno of the failed send. */
static int stream_send_all (int fd, const char *data, size_t size) {
	while (size > 0) {
#ifdef MSG_NOSIGNAL
		const ssize_t sent = send (fd, data, size, MSG_NOSIGNAL);
#else
		const ssize_t sent = send (fd, data, size, 0);
#endif
		if (sent < 0 && errno == EINTR) { continue; }
		if (sent <= 0) { return sent < 0 ? errno : EIO; }
		data += sent;
		size -= (size_t)sent;
	}
	return 0;
}

static int stream_send_copies (int client, FILE *audio, unsigned copies) {
	int sendErr = 0;
	for (unsigned repeat = 0; repeat < copies && sendErr == 0; ++repeat) {
		char buffer[4096];
		size_t count;
		while (sendErr == 0 && (count = fread (buffer, 1, sizeof buffer, audio)) > 0) {
			sendErr = stream_send_all (client, buffer, count);
		}
		rewind (audio);
	}
	return sendErr;
}

/* Never answers: every accepted connection is held open until stop, so a
 * retrying player would show up as a second accept. */
static void *stall_open_stream_thread (UnfinishedAudioStream *stream) {
	int clients[8];
	unsigned held = 0;
	while (!atomic_load (&stream->stop)) {
		struct pollfd ready = {.fd = stream->listenFd, .events = POLLIN};
		if (poll (&ready, 1, 10) <= 0) { continue; }
		const int client = accept (stream->listenFd, NULL, NULL);
		if (client < 0) { break; }
		atomic_fetch_add (&stream->accepts, 1);
		char request[4096];
		(void) recv (client, request, sizeof request, 0);
		atomic_store (&stream->stalled, true);
		if (held < sizeof clients / sizeof clients[0]) { clients[held++] = client; }
		else { close (client); }
	}
	for (unsigned i = 0; i < held; ++i) { close (clients[i]); }
	return NULL;
}

/* UNFINISHED: send enough real MP3 audio for FFmpeg probing, but withhold
 * the final byte and socket EOF until after the player joins. The decoder
 * cannot finish naturally during any of the active-phase pause/resume
 * controls. STALL_READ: the same, but stalls after the head (see the enum). */
static void *unfinished_audio_stream_thread (void *data) {
	UnfinishedAudioStream *stream = data;
	if (stream->mode == AUDIO_STREAM_STALL_OPEN) { return stall_open_stream_thread (stream); }
	const int client = accept (stream->listenFd, NULL, NULL);
	if (client < 0) { atomic_store (&stream->failed, true); return NULL; }
	atomic_fetch_add (&stream->accepts, 1);
	atomic_store (&stream->clientFd, client);
#ifdef SO_NOSIGPIPE
	const int noSigpipe = 1;
	setsockopt (client, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof noSigpipe);
#endif
	char request[4096];
	FILE *audio = fopen (integration_fixture_path (), "rb");
	int sendErr = 0;
	bool sent = audio != NULL && recv (client, request, sizeof request, 0) > 0;
	const bool stall = stream->mode == AUDIO_STREAM_STALL_READ;
	if (sent) {
		fseek (audio, 0, SEEK_END);
		const long bytes = ftell (audio);
		rewind (audio);
		const unsigned copies = stall ? stream->headCopies + stream->tailCopies : 32;
		char header[256];
		/* UNFINISHED never sends its final byte: no natural EOF before stop. */
		const int length = snprintf (header, sizeof header,
			"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n"
			"Content-Length: %ld\r\nConnection: close\r\n\r\n",
			bytes * (long)copies + (stall ? 0 : 1));
		sendErr = stream_send_all (client, header, (size_t)length);
		if (sendErr == 0) { sendErr = stream_send_copies (client, audio, stall ? stream->headCopies : 32); }
		sent = sendErr == 0;
	}
	if (sent && stall) {
		atomic_store (&stream->stalled, true);
		while (!atomic_load (&stream->stop) && !atomic_load (&stream->resume)) { usleep (1000); }
		if (!atomic_load (&stream->stop)) {
			sendErr = stream_send_copies (client, audio, stream->tailCopies);
			sent = sendErr == 0;
			atomic_store (&stream->tailSent, sent);
		}
	}
	if (audio != NULL) { fclose (audio); }
	if (!sent && !atomic_load (&stream->stop) &&
		!FixtureSendErrorExpected (sendErr, atomic_load (&stream->cancelExpected))) {
		atomic_store (&stream->failed, true);
	}
	while (!atomic_load (&stream->stop)) { usleep (1000); }
	close (client);
	return NULL;
}

static void start_audio_stream (UnfinishedAudioStream *stream, AudioStreamMode mode,
		unsigned headCopies, unsigned tailCopies, char *url, size_t size) {
	*stream = (UnfinishedAudioStream) {.clientFd = -1, .mode = mode,
		.headCopies = headCopies, .tailCopies = tailCopies};
	stream->listenFd = socket (AF_INET, SOCK_STREAM, 0);
	ck_assert_int_ge (stream->listenFd, 0);
	struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl (INADDR_LOOPBACK)};
	ck_assert_int_eq (bind (stream->listenFd, (struct sockaddr *)&address, sizeof address), 0);
	socklen_t length = sizeof address;
	ck_assert_int_eq (getsockname (stream->listenFd, (struct sockaddr *)&address, &length), 0);
	ck_assert_int_eq (listen (stream->listenFd, 1), 0);
	ck_assert_int_lt (snprintf (url, size, "http://127.0.0.1:%u/unfinished.mp3", ntohs (address.sin_port)), (int)size);
	ck_assert_int_eq (pthread_create (&stream->thread, NULL, unfinished_audio_stream_thread, stream), 0);
}

static void start_unfinished_audio_stream (UnfinishedAudioStream *stream, char *url, size_t size) {
	start_audio_stream (stream, AUDIO_STREAM_UNFINISHED, 0, 0, url, size);
}

static void stop_unfinished_audio_stream (UnfinishedAudioStream *stream) {
	atomic_store (&stream->stop, true);
	shutdown (stream->listenFd, SHUT_RDWR);
	const int client = atomic_load (&stream->clientFd);
	if (client >= 0) { shutdown (client, SHUT_RDWR); }
	ck_assert_int_eq (pthread_join (stream->thread, NULL), 0);
	close (stream->listenFd);
	ck_assert (!atomic_load (&stream->failed));
}

/* Break caught: a paused decoder or post-decode monitor prevents skip,
 * disconnect, or application quit from joining and stopping the device. */
START_TEST (test_player_paused_active_and_decoded_stop_paths_join_promptly)
{
	if (!integration_enabled ()) { return; }
	for (unsigned phase = 0; phase < 2; ++phase) {
		for (unsigned action = 0; action < 3; ++action) {
			BarApp_t app;
			setup_integration_app_with_piano (&app);
			ck_assert (app.player.engineInitialized);
			char path[PATH_MAX], url[PATH_MAX + 16];
			UnfinishedAudioStream stream;
			if (phase == 0) { start_unfinished_audio_stream (&stream, url, sizeof url); }
			else {
				ck_assert_ptr_nonnull (realpath (integration_fixture_path (), path));
				ck_assert_int_lt (snprintf (url, sizeof url, "file://%s", path), (int)sizeof url);
			}
			app.player.url = strdup (url);
			BarPlayerSetMode (&app.player, PLAYER_WAITING);
			ck_assert (BarPlayerSetPaused (&app.player, true));
			app.player.threadJoinPending = true;
			pthread_t worker;
			ck_assert_int_eq (pthread_create (&worker, NULL, BarPlayerThread, &app.player), 0);
			ck_assert (wait_for_mode (&app.player, PLAYER_PLAYING, 10000));
			pthread_mutex_lock (&app.player.decoderLock);
			ck_assert (!app.player.decodingFinished);
			pthread_mutex_unlock (&app.player.decoderLock);
			BarTestAssertDeviceStarted (&app.player, false);
			ck_assert (BarPlayerSetPaused (&app.player, false));
			BarTestAssertDeviceStarted (&app.player, true);
			if (phase == 1) {
				bool decoded = false;
				for (unsigned ms = 0; ms < 1000 && !decoded; ms += 1) {
					pthread_mutex_lock (&app.player.decoderLock);
					decoded = app.player.decodingFinished;
					pthread_mutex_unlock (&app.player.decoderLock);
					if (!decoded) { usleep (1000); }
				}
				ck_assert (decoded);
			}
			ck_assert (BarPlayerSetPaused (&app.player, true));
			ck_assert (BarPlayerSetPaused (&app.player, false));
			ck_assert (BarPlayerSetPaused (&app.player, true));
			BarTestAssertDeviceStarted (&app.player, false);
			IntegrationJoin join = {.player = &app.player, .thread = worker};
			pthread_t joiner;
			ck_assert_int_eq (pthread_create (&joiner, NULL, integration_join_player, &join), 0);
			struct timespec start, end;
			clock_gettime (CLOCK_MONOTONIC, &start);
			pthread_mutex_lock (&app.player.decoderLock);
			const bool decodingFinishedBeforeStop = app.player.decodingFinished;
			pthread_mutex_unlock (&app.player.decoderLock);
			ck_assert_msg (decodingFinishedBeforeStop == (phase == 1),
				"Immediately before %s, phase=%s must observe decodingFinished=%d, observed %d",
				action == 0 ? "skip" : action == 1 ? "disconnect" : "quit",
				phase ? "decoded" : "active", phase == 1, decodingFinishedBeforeStop);
			if (phase == 0) { atomic_store (&stream.cancelExpected, true); }
			if (action == 0) { BarUiActSkipSong (&app, NULL, NULL, 1); }
			else if (action == 1) { BarUiActPandoraDisconnect (&app, NULL, NULL, 1); }
			else { BarUiActQuit (&app, NULL, NULL, 1); }
			ck_assert_int_eq (pthread_join (joiner, NULL), 0);
			clock_gettime (CLOCK_MONOTONIC, &end);
			ck_assert (join.joined);
			const long latencyMs = (end.tv_sec - start.tv_sec) * 1000L + (end.tv_nsec - start.tv_nsec) / 1000000L;
			ck_assert_int_lt (latencyMs, 2000);
			fprintf (stderr, "pause-stop phase=%s action=%s decodingFinished-before-stop=%d latency=%ldms backend=%s\n", phase ? "decoded" : "active", action == 0 ? "skip" : action == 1 ? "disconnect" : "quit", decodingFinishedBeforeStop, latencyMs, app.player.audioNoDevice ? "no-device" : "device");
			ck_assert (!app.player.audioBusy && !app.player.threadJoinPending && !app.player.soundInitialized);
			BarTestAssertDeviceStarted (&app.player, false);
			if (phase == 0) { stop_unfinished_audio_stream (&stream); }
			free (app.player.url);
			app.player.url = NULL;
			teardown_integration_app_with_piano (&app);
		}
	}
}
END_TEST

/*
 * Stalled-stream stop. The fixture stops sending without closing, so the
 * player thread sits in an FFmpeg network read (or open) that only the
 * interrupt callback can end. Without it a stop waits for the playback
 * manager's join timeout or the HTTP timeout (settings->timeout).
 */

/* 1 s locally; 2 s on slow shared CI runners. */
static long stall_latency_bound_ms (void) {
	return getenv ("CI") != NULL ? 2000 : 1000;
}

static long stall_elapsed_ms (const struct timespec *start) {
	struct timespec now;
	clock_gettime (CLOCK_MONOTONIC, &now);
	return (now.tv_sec - start->tv_sec) * 1000L + (now.tv_nsec - start->tv_nsec) / 1000000L;
}

static const char *stall_action_name (unsigned action) {
	return action == 0 ? "skip" : action == 1 ? "disconnect" : "quit";
}

static void stall_prepare_player (BarApp_t *app, const char *url) {
	app->player.url = strdup (url);
	ck_assert_ptr_nonnull (app->player.url);
	BarPlayerSetMode (&app->player, PLAYER_WAITING);
}

static void stall_start_worker (BarApp_t *app, pthread_t *worker) {
	app->player.threadJoinPending = true;
	ck_assert_int_eq (pthread_create (worker, NULL, BarPlayerThread, &app->player), 0);
}

static bool stall_wait_flag (_Atomic bool *flag, unsigned timeoutMs) {
	for (unsigned elapsed = 0; elapsed < timeoutMs; elapsed += 5) {
		if (atomic_load (flag)) { return true; }
		usleep (5000);
	}
	return atomic_load (flag);
}

static bool stall_decoding_finished (player_t *player) {
	pthread_mutex_lock (&player->decoderLock);
	const bool finished = player->decodingFinished;
	pthread_mutex_unlock (&player->decoderLock);
	return finished;
}

/* Runs one user stop while a joiner waits 2 s (the manager waits 10 s). */
static long stall_stop_and_join (BarApp_t *app, pthread_t worker, unsigned action, void **ret) {
	IntegrationJoin join = {.player = &app->player, .thread = worker};
	pthread_t joiner;
	ck_assert_int_eq (pthread_create (&joiner, NULL, integration_join_player, &join), 0);
	struct timespec start;
	clock_gettime (CLOCK_MONOTONIC, &start);
	if (action == 0) { BarUiActSkipSong (app, NULL, NULL, 1); }
	else if (action == 1) { BarUiActPandoraDisconnect (app, NULL, NULL, 1); }
	else { BarUiActQuit (app, NULL, NULL, 1); }
	ck_assert_int_eq (pthread_join (joiner, NULL), 0);
	const long latencyMs = stall_elapsed_ms (&start);
	ck_assert_msg (join.joined, "%s during a stalled stream must join within 2 s",
		stall_action_name (action));
	*ret = join.ret;
	return latencyMs;
}

static void stall_free_url (BarApp_t *app) {
	free (app->player.url);
	app->player.url = NULL;
}

/* Starts a player on a stream that stalls mid-song after headCopies. */
static void stall_start_playing (BarApp_t *app, UnfinishedAudioStream *stream,
		unsigned headCopies, unsigned tailCopies, pthread_t *worker) {
	char url[128];
	start_audio_stream (stream, AUDIO_STREAM_STALL_READ, headCopies, tailCopies, url, sizeof url);
	stall_prepare_player (app, url);
	stall_start_worker (app, worker);
	ck_assert (wait_for_mode (&app->player, PLAYER_PLAYING, 10000));
	ck_assert (stall_wait_flag (&stream->stalled, 5000));
	/* Let the decoder consume the head and block in the read. */
	usleep (200000);
	ck_assert_msg (!stall_decoding_finished (&app->player),
		"The decoder must still be waiting for the stalled stream");
}

/* Break caught: skip, disconnect or quit during a stalled read waits for the
 * join timeout because the FFmpeg read ignores the stop request. */
START_TEST (test_stop_during_stalled_read_joins_promptly)
{
	if (!integration_enabled ()) { return; }
	BarApp_t app;
	setup_integration_app_with_piano (&app);
	ck_assert (app.player.engineInitialized);
	UnfinishedAudioStream stream;
	pthread_t worker;
	stall_start_playing (&app, &stream, 8, 1, &worker);

	atomic_store (&stream.cancelExpected, true);
	void *ret = (void *)PLAYER_RET_HARDFAIL;
	const long latencyMs = stall_stop_and_join (&app, worker, (unsigned)_i, &ret);
	fprintf (stderr, "stalled-read action=%s latency=%ldms backend=%s\n",
		stall_action_name ((unsigned)_i), latencyMs,
		app.player.audioNoDevice ? "no-device" : "device");
	ck_assert_int_lt (latencyMs, stall_latency_bound_ms ());
	ck_assert_int_eq ((intptr_t)ret, PLAYER_RET_OK);
	ck_assert_uint_eq (atomic_load (&stream.accepts), 1);
	ck_assert (!app.player.audioBusy && !app.player.threadJoinPending && !app.player.soundInitialized);
	BarTestAssertDeviceStarted (&app.player, false);

	stop_unfinished_audio_stream (&stream);
	stall_free_url (&app);
	teardown_integration_app_with_piano (&app);
}
END_TEST

/* Break caught: a stop during a stalled open is reported as a stream
 * failure (error message, soft-fail count, retry) or waits for the HTTP
 * timeout. */
START_TEST (test_stop_during_stalled_open_aborts_quietly)
{
	if (!integration_enabled ()) { return; }
	BarApp_t app;
	setup_integration_app_with_piano (&app);
	ck_assert (app.player.engineInitialized);
	char url[128];
	UnfinishedAudioStream stream;
	start_audio_stream (&stream, AUDIO_STREAM_STALL_OPEN, 0, 0, url, sizeof url);

	fflush (stdout);
	FILE *capture = tmpfile ();
	ck_assert_ptr_nonnull (capture);
	const int savedStdout = dup (STDOUT_FILENO);
	ck_assert_int_ge (savedStdout, 0);
	ck_assert_int_ge (dup2 (fileno (capture), STDOUT_FILENO), 0);

	stall_prepare_player (&app, url);
	pthread_t worker;
	stall_start_worker (&app, &worker);
	ck_assert (stall_wait_flag (&stream.stalled, 5000));
	usleep (100000);
	ck_assert_int_eq (BarPlayerGetMode (&app.player), PLAYER_WAITING);
	void *ret = (void *)PLAYER_RET_HARDFAIL;
	const long latencyMs = stall_stop_and_join (&app, worker, 0, &ret);

	fflush (stdout);
	ck_assert_int_ge (dup2 (savedStdout, STDOUT_FILENO), 0);
	close (savedStdout);
	char output[4096] = {0};
	rewind (capture);
	(void) fread (output, 1, sizeof output - 1, capture);
	fclose (capture);

	fprintf (stderr, "stalled-open latency=%ldms\n", latencyMs);
	ck_assert_int_lt (latencyMs, stall_latency_bound_ms ());
	/* Soft fail would count toward maxRetry; stale URLs would drain the playlist. */
	ck_assert_int_eq ((intptr_t)ret, PLAYER_RET_OK);
	ck_assert_msg (strstr (output, "Unable to open audio file") == NULL,
		"A requested stop must not print an open error, got: %s", output);
	usleep (50000);
	ck_assert_uint_eq (atomic_load (&stream.accepts), 1);

	stop_unfinished_audio_stream (&stream);
	stall_free_url (&app);
	teardown_integration_app_with_piano (&app);
}
END_TEST

/* Break caught: a stop published after the new-song reset but before the
 * worker opens its stream is lost, and the open runs. */
START_TEST (test_stop_requested_before_open_aborts_open)
{
	if (!integration_enabled ()) { return; }
	BarApp_t app;
	setup_integration_app_with_piano (&app);
	char url[128];
	UnfinishedAudioStream stream;
	start_audio_stream (&stream, AUDIO_STREAM_STALL_OPEN, 0, 0, url, sizeof url);

	ck_assert (BarPlayerReset (&app.player));
	stall_prepare_player (&app, url);
	BarPlayerRequestStop (&app.player);
	struct timespec start;
	clock_gettime (CLOCK_MONOTONIC, &start);
	pthread_t worker;
	stall_start_worker (&app, &worker);
	void *ret = (void *)PLAYER_RET_HARDFAIL;
	ck_assert (BarPlayerJoinThreadWithTimeout (&app.player, worker, &ret, 2));
	const long latencyMs = stall_elapsed_ms (&start);
	ck_assert_int_lt (latencyMs, stall_latency_bound_ms ());
	ck_assert_int_eq ((intptr_t)ret, PLAYER_RET_OK);
	usleep (50000);
	ck_assert_uint_eq (atomic_load (&stream.accepts), 0);

	stop_unfinished_audio_stream (&stream);
	stall_free_url (&app);
	teardown_integration_app_with_piano (&app);
}
END_TEST

/* Break caught: the stop request is never cleared, so the skip that ended a
 * stalled song also aborts the next song's open. */
START_TEST (test_stop_flag_does_not_abort_next_song)
{
	if (!integration_enabled ()) { return; }
	BarApp_t app;
	BarFixtureHttp_t http = {0};
	uint16_t port = 0;
	setup_integration_app_with_piano (&app);
	ck_assert (app.player.engineInitialized);
	ck_assert (integration_fixture_exists ());
	ck_assert (BarFixtureHttpStart (&http, integration_fixture_path (), &port));

	UnfinishedAudioStream stream;
	start_audio_stream (&stream, AUDIO_STREAM_STALL_READ, 8, 1, g_audio_url, sizeof g_audio_url);
	snprintf (g_audio_url2, sizeof (g_audio_url2), "http://127.0.0.1:%u/tone.mp3", port);
	memset (&g_station, 0, sizeof (g_station));
	g_station.id = "station-stall";
	g_station.name = "Stall Station";
	fill_mock_song (&g_song, "Stalled Song", g_audio_url);
	fill_mock_song (&g_song2, "Next Song", g_audio_url2);
	g_song.head.next = &g_song2.head;
	g_song2.head.next = NULL;
	BarStateSetCurrentStation (&app, &g_station);
	BarStateSetPlaylist (&app, &g_song);

	pthread_t playerThread = 0;
	ck_assert (BarPlaybackStartSong (&app, &playerThread));
	ck_assert (wait_for_mode (&app.player, PLAYER_PLAYING, 10000));
	ck_assert (stall_wait_flag (&stream.stalled, 5000));
	usleep (200000);
	atomic_store (&stream.cancelExpected, true);
	void *ret = (void *)PLAYER_RET_HARDFAIL;
	const long latencyMs = stall_stop_and_join (&app, playerThread, 0, &ret);
	ck_assert_int_lt (latencyMs, stall_latency_bound_ms ());
	ck_assert_int_eq ((intptr_t)ret, PLAYER_RET_OK);
	stall_free_url (&app);
	playerThread = 0;

	BarStateSetPlaylist (&app, PianoListNextP (&g_song));
	ck_assert (BarPlaybackStartSong (&app, &playerThread));
	ck_assert_msg (wait_for_mode (&app.player, PLAYER_PLAYING, 10000),
		"The song after a skipped stalled song must open and play");
	ck_assert (wait_for_mode (&app.player, PLAYER_FINISHED, 10000));
	stop_player_thread (&app.player, &playerThread);

	stop_unfinished_audio_stream (&stream);
	BarStateSetPlaylist (&app, NULL);
	teardown_integration_app_with_piano (&app);
	BarFixtureHttpStop (&http);
}
END_TEST

/* Break caught: pause is treated as a stop and aborts a stalled read, so the
 * song ends early instead of resuming when the network recovers. */
START_TEST (test_pause_during_stalled_read_does_not_abort)
{
	if (!integration_enabled ()) { return; }
	BarApp_t app;
	setup_integration_app_with_piano (&app);
	ck_assert (app.player.engineInitialized);
	UnfinishedAudioStream stream;
	pthread_t worker;
	stall_start_playing (&app, &stream, 4, 2, &worker);

	ck_assert (BarPlayerSetPaused (&app.player, true));
	usleep (1000000);
	ck_assert_msg (!stall_decoding_finished (&app.player),
		"Pause during a stalled read must not abort the read");
	ck_assert (BarPlayerSetPaused (&app.player, false));
	atomic_store (&stream.resume, true);
	ck_assert (stall_wait_flag (&stream.tailSent, 5000));
	for (unsigned ms = 0; ms < 5000 && !stall_decoding_finished (&app.player); ms += 5) {
		usleep (5000);
	}
	ck_assert_msg (stall_decoding_finished (&app.player),
		"The resumed stream must decode to its natural end");
	/* No-device: stand in for the device callback until the song ends. */
	for (unsigned drains = 0; drains < 10 && BarPlayerGetMode (&app.player) == PLAYER_PLAYING; ++drains) {
		ck_assert (BarTestDrainAudioFixture (&app.player));
	}
	ck_assert (BarPlayerWaitForMode (&app.player, PLAYER_FINISHED, 10000));
	void *ret = (void *)PLAYER_RET_HARDFAIL;
	ck_assert (BarPlayerJoinThreadWithTimeout (&app.player, worker, &ret, 2));
	ck_assert_int_eq ((intptr_t)ret, PLAYER_RET_OK);
	ck_assert_uint_eq (atomic_load (&stream.accepts), 1);

	stop_unfinished_audio_stream (&stream);
	stall_free_url (&app);
	teardown_integration_app_with_piano (&app);
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
	BarTestAssertDeviceStarted (&barApp.player, false);

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
	ck_assert (wait_for_mode (&barApp.player, PLAYER_PLAYING, 10000));
	BarTestAssertDeviceStarted (&barApp.player, true);
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 10000));
	stop_player_thread (&barApp.player, &playerThread);
	BarTestAssertDeviceStarted (&barApp.player, false);

	BarStateSetPlaylist (&barApp, PianoListNextP (&g_song));
	ck_assert_ptr_eq (BarStateGetPlaylist (&barApp), &g_song2);

	ck_assert (BarPlaybackStartSong (&barApp, &playerThread));
	ck_assert (wait_for_mode (&barApp.player, PLAYER_PLAYING, 10000));
	BarTestAssertDeviceStarted (&barApp.player, true);
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 10000));
	stop_player_thread (&barApp.player, &playerThread);
	BarTestAssertDeviceStarted (&barApp.player, false);

	teardown_integration_app (&barApp);
	BarFixtureHttpStop (&http);
}
END_TEST

/* Runs once per ui_mode (both, cli): the bridge must start the playback
 * manager without a WebSocket server, and a station chosen after the manager
 * has parked must wake it to fetch and play one fixture song. */
static const BarUiMode_t g_manager_modes[] = { BAR_UI_MODE_BOTH, BAR_UI_MODE_CLI };

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
	barApp.settings.uiMode = g_manager_modes[_i];
	ck_assert_ptr_null (barApp.wsContext);

	ck_assert (integration_fixture_exists ());
	ck_assert (BarFixtureHttpStart (&http, integration_fixture_path (), &port));

	snprintf (g_audio_url, sizeof (g_audio_url),
	          "http://127.0.0.1:%u/tone.mp3", port);

	g_playlist_fetch_count = 0;
	memset (&g_station, 0, sizeof (g_station));
	g_station.id = "station-manager";
	g_station.name = "Manager Station";

	BarUiPianoCallSetTestHook (mock_get_playlist_counted);
	ck_assert (BarWsStartPlaybackManager (&barApp));
	usleep (50000); /* let the manager park with no station */
	BarStateSetNextStation (&barApp, &g_station);

	ck_assert (wait_dead_after_playing (&barApp, 15000));
	ck_assert_int_ge (g_playlist_fetch_count, 1);
	ck_assert (BarPlaybackManagerWaitParkedIdle (&barApp, 1000));
	BarTestAssertDeviceStarted (&barApp.player, false);

	BarWsStopPlaybackManager (&barApp);
	BarStateDrainPlaylist (&barApp);
	ck_assert_ptr_null (barApp.wsContext);

	teardown_integration_app (&barApp);
	BarFixtureHttpStop (&http);
}
END_TEST

START_TEST (test_fixture_epipe_before_cancel_is_error)
{
	ck_assert (!FixtureSendErrorExpected (EPIPE, false));
	ck_assert (!FixtureSendErrorExpected (ECONNRESET, false));
}
END_TEST

START_TEST (test_fixture_epipe_after_cancel_is_expected)
{
	ck_assert (FixtureSendErrorExpected (EPIPE, true));
	ck_assert (FixtureSendErrorExpected (ECONNRESET, true));
}
END_TEST

START_TEST (test_fixture_other_error_after_cancel_is_error)
{
	ck_assert (!FixtureSendErrorExpected (0, true));
	ck_assert (!FixtureSendErrorExpected (EIO, true));
	ck_assert (!FixtureSendErrorExpected (ETIMEDOUT, true));
}
END_TEST

/*
 * Null-backend device tests. miniaudio's null backend runs a real device
 * thread that pulls the data callback in real time without hardware, so the
 * decoder/device handoff, player.lock, decoderLock and the audio reservation
 * run concurrently, as in production. They take precedence over
 * PIANOBAR_TEST_NO_DEVICE for this test case only (checked fixture: the flag
 * is set in the forked test process before BarPlayerInit and cleared after).
 */
static void null_audio_setup (void) {
	BarPlayerSetNullAudioTestBackend (true);
}

static void null_audio_teardown (void) {
	BarPlayerSetNullAudioTestBackend (false);
}

/* True only when the engine really runs on the null backend's device. */
static bool null_audio_ready (const char *test, player_t *player) {
	if (!integration_enabled ()) { return false; }
	if (player->engineInitialized && player->audioNullBackend && !player->audioNoDevice &&
	    ma_engine_get_device (&player->engine) != NULL) {
		return true;
	}
	fprintf (stderr, "SKIP %s: miniaudio null audio backend unavailable in this build\n", test);
	return false;
}

static void null_audio_ran (const char *test) {
	fprintf (stderr, "null-audio: %s ran on the null device backend\n", test);
}

static bool null_audio_app_start (BarApp_t *barApp, BarFixtureHttp_t *http, const char *test) {
	uint16_t port = 0;
	setup_integration_app (barApp);
	if (!null_audio_ready (test, &barApp->player)) {
		teardown_integration_app (barApp);
		return false;
	}
	BarTestAssertDeviceStarted (&barApp->player, false);
	ck_assert (integration_fixture_exists ());
	ck_assert (BarFixtureHttpStart (http, integration_fixture_path (), &port));
	snprintf (g_audio_url, sizeof (g_audio_url), "http://127.0.0.1:%u/tone.mp3", port);
	memset (&g_station, 0, sizeof (g_station));
	g_station.id = "station-null-audio";
	g_station.name = "Null Audio Station";
	fill_mock_song (&g_song, "Null Audio Song", g_audio_url);
	BarStateSetCurrentStation (barApp, &g_station);
	BarStateSetPlaylist (barApp, &g_song);
	return true;
}

static void null_audio_app_stop (BarApp_t *barApp, BarFixtureHttp_t *http) {
	teardown_integration_app (barApp);
	BarFixtureHttpStop (http);
}

/* The device callback, not the test, must advance the real cursor. */
/* BarPlayerGetAudioSnapshot never blocks: it returns false while the player
 * lock or audio reservation is briefly held by another thread. Retry for up
 * to 2 s while the song is still playing instead of asserting one attempt. */
static bool null_audio_snapshot (player_t *player, BarPlayerAudioSnapshot *snapshot) {
	for (unsigned elapsed = 0; elapsed < 2000; elapsed += 5) {
		if (BarPlayerGetAudioSnapshot (player, snapshot)) { return true; }
		if (BarPlayerGetMode (player) != PLAYER_PLAYING) { return false; }
		usleep (5000);
	}
	return false;
}

static bool null_audio_wait_cursor_at_least (player_t *player, ma_uint64 frames) {
	for (unsigned elapsed = 0; elapsed < 2000; elapsed += 5) {
		BarPlayerAudioSnapshot snapshot;
		if (BarPlayerGetAudioSnapshot (player, &snapshot) && snapshot.cursorFrames >= frames) {
			return true;
		}
		if (BarPlayerGetMode (player) != PLAYER_PLAYING) { return false; }
		usleep (5000);
	}
	return false;
}

START_TEST (test_null_audio_song_plays_to_natural_end)
{
	BarApp_t barApp;
	BarFixtureHttp_t http = {0};
	pthread_t playerThread = 0;
	if (!null_audio_app_start (&barApp, &http, "song_plays_to_natural_end")) { return; }

	ck_assert (BarPlaybackStartSong (&barApp, &playerThread));
	ck_assert (wait_for_mode (&barApp.player, PLAYER_PLAYING, 10000));
	BarTestAssertDeviceStarted (&barApp.player, true);
	/* No test drain: only the null device thread can consume frames. */
	ck_assert (null_audio_wait_cursor_at_least (&barApp.player, 1));
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 10000));
	stop_player_thread (&barApp.player, &playerThread);
	ck_assert_int_eq (BarPlayerGetMode (&barApp.player), PLAYER_FINISHED);
	BarTestAssertDeviceStarted (&barApp.player, false);
	ck_assert (!barApp.player.soundInitialized);

	null_audio_app_stop (&barApp, &http);
	null_audio_ran ("song_plays_to_natural_end");
}
END_TEST

START_TEST (test_null_audio_pause_resume_stops_and_restarts_device)
{
	BarApp_t barApp;
	BarFixtureHttp_t http = {0};
	pthread_t playerThread = 0;
	if (!null_audio_app_start (&barApp, &http, "pause_resume_stops_and_restarts_device")) { return; }

	ck_assert (BarPlaybackStartSong (&barApp, &playerThread));
	ck_assert (wait_for_mode (&barApp.player, PLAYER_PLAYING, 10000));
	ck_assert (null_audio_wait_cursor_at_least (&barApp.player, 1));
	for (unsigned cycle = 0; cycle < 3; ++cycle) {
		BarUiActPause (&barApp, NULL, NULL, 1);
		ck_assert (BarPlayerIsPaused (&barApp.player));
		BarTestAssertDeviceStarted (&barApp.player, false);
		BarPlayerAudioSnapshot before, after;
		ck_assert (null_audio_snapshot (&barApp.player, &before));
		ck_assert (!before.playing);
		usleep (100000);
		ck_assert (null_audio_snapshot (&barApp.player, &after));
		ck_assert_msg (after.cursorFrames == before.cursorFrames,
		               "a stopped device must not advance the cursor");
		BarUiActPlay (&barApp, NULL, NULL, 1);
		ck_assert (!BarPlayerIsPaused (&barApp.player));
		BarTestAssertDeviceStarted (&barApp.player, true);
		/* Short advance only: the fixture is 0.5 s and must not end mid-cycle. */
		ck_assert (null_audio_wait_cursor_at_least (&barApp.player, before.cursorFrames + 1));
	}
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 10000));
	stop_player_thread (&barApp.player, &playerThread);
	BarTestAssertDeviceStarted (&barApp.player, false);

	null_audio_app_stop (&barApp, &http);
	null_audio_ran ("pause_resume_stops_and_restarts_device");
}
END_TEST

START_TEST (test_null_audio_volume_changes_while_playing_and_paused)
{
	BarApp_t barApp;
	BarFixtureHttp_t http = {0};
	pthread_t playerThread = 0;
	if (!null_audio_app_start (&barApp, &http, "volume_changes_while_playing_and_paused")) { return; }

	ck_assert (BarPlaybackStartSong (&barApp, &playerThread));
	ck_assert (wait_for_mode (&barApp.player, PLAYER_PLAYING, 10000));
	ck_assert (BarPlayerSetVolume (&barApp.player, 50));
	ck_assert_int_eq (BarPlayerGetVolume (&barApp.player), 50);
	ck_assert (BarPlayerAdjustVolume (&barApp.player, 10));
	ck_assert_int_eq (BarPlayerGetVolume (&barApp.player), 60);
	ck_assert_int_eq (barApp.settings.volume, 60);
	ck_assert (BarPlayerSetPaused (&barApp.player, true));
	ck_assert (BarPlayerAdjustVolume (&barApp.player, -1000));
	ck_assert_int_eq (BarPlayerGetVolume (&barApp.player), 0);
	ck_assert (BarPlayerSetPaused (&barApp.player, false));
	ck_assert (BarPlayerSetVolume (&barApp.player, 80));
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 10000));
	stop_player_thread (&barApp.player, &playerThread);
	ck_assert_int_eq (BarPlayerGetVolume (&barApp.player), 80);

	null_audio_app_stop (&barApp, &http);
	null_audio_ran ("volume_changes_while_playing_and_paused");
}
END_TEST

typedef struct {
	player_t *player;
	_Atomic bool stop;
	unsigned volumeOps, snapshots;
} NullAudioStress_t;

static void *null_audio_volume_worker (void *data) {
	NullAudioStress_t *stress = data;
	while (!atomic_load (&stress->stop)) {
		if (BarPlayerAdjustVolume (stress->player, (stress->volumeOps & 1) ? 5 : -5)) {
			++stress->volumeOps;
		}
		usleep (1000);
	}
	return NULL;
}

static void *null_audio_snapshot_worker (void *data) {
	NullAudioStress_t *stress = data;
	while (!atomic_load (&stress->stop)) {
		BarPlayerAudioSnapshot snapshot;
		if (BarPlayerGetAudioSnapshot (stress->player, &snapshot)) { ++stress->snapshots; }
		usleep (1000);
	}
	return NULL;
}

/* Controls, observations and the device callback contend for the same
 * reservation and decoderLock until the song ends naturally. */
START_TEST (test_null_audio_concurrent_controls_until_song_end)
{
	BarApp_t barApp;
	BarFixtureHttp_t http = {0};
	pthread_t playerThread = 0, volumeThread, snapshotThread;
	if (!null_audio_app_start (&barApp, &http, "concurrent_controls_until_song_end")) { return; }
	NullAudioStress_t stress = { .player = &barApp.player };

	ck_assert (BarPlaybackStartSong (&barApp, &playerThread));
	ck_assert_int_eq (pthread_create (&volumeThread, NULL, null_audio_volume_worker, &stress), 0);
	ck_assert_int_eq (pthread_create (&snapshotThread, NULL, null_audio_snapshot_worker, &stress), 0);
	ck_assert (wait_for_mode (&barApp.player, PLAYER_PLAYING, 10000));
	for (unsigned i = 0; i < 6 && BarPlayerGetMode (&barApp.player) == PLAYER_PLAYING; ++i) {
		bool paused = false;
		BarPlayerTogglePaused (&barApp.player, &paused);
		usleep (20000);
	}
	BarPlayerSetPaused (&barApp.player, false);
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 10000));
	stop_player_thread (&barApp.player, &playerThread);
	atomic_store (&stress.stop, true);
	ck_assert_int_eq (pthread_join (volumeThread, NULL), 0);
	ck_assert_int_eq (pthread_join (snapshotThread, NULL), 0);
	BarTestAssertDeviceStarted (&barApp.player, false);
	ck_assert_uint_gt (stress.volumeOps, 0);

	null_audio_app_stop (&barApp, &http);
	null_audio_ran ("concurrent_controls_until_song_end");
}
END_TEST

START_TEST (test_null_audio_skip_mid_song_stops_device)
{
	BarApp_t barApp;
	BarFixtureHttp_t http = {0};
	pthread_t playerThread = 0;
	if (!null_audio_app_start (&barApp, &http, "skip_mid_song_stops_device")) { return; }

	ck_assert (BarPlaybackStartSong (&barApp, &playerThread));
	ck_assert (wait_for_mode (&barApp.player, PLAYER_PLAYING, 10000));
	ck_assert (null_audio_wait_cursor_at_least (&barApp.player, 1));
	BarPlayerAudioSnapshot snapshot;
	ck_assert (null_audio_snapshot (&barApp.player, &snapshot));
	ck_assert (!snapshot.atEnd);
	BarUiActSkipSong (&barApp, NULL, NULL, 1);
	ck_assert (barApp.player.doQuit);
	BarTestAssertDeviceStarted (&barApp.player, false);
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 10000));
	stop_player_thread (&barApp.player, &playerThread);
	BarTestAssertDeviceStarted (&barApp.player, false);
	ck_assert (!barApp.player.soundInitialized);

	null_audio_app_stop (&barApp, &http);
	null_audio_ran ("skip_mid_song_stops_device");
}
END_TEST

/* Stop at a different point of each start (before the stream opens, while
 * the device starts, mid-song, near the end, paused) to catch lifecycle races. */
START_TEST (test_null_audio_repeated_start_stop)
{
	BarApp_t barApp;
	BarFixtureHttp_t http = {0};
	pthread_t playerThread = 0;
	if (!null_audio_app_start (&barApp, &http, "repeated_start_stop")) { return; }

	static const unsigned delays_us[] = { 0, 1000, 5000, 20000, 60000, 150000, 300000, 450000 };
	const unsigned cycles = sizeof (delays_us) / sizeof (delays_us[0]);
	for (unsigned cycle = 0; cycle < cycles * 2; ++cycle) {
		ck_assert (BarPlaybackStartSong (&barApp, &playerThread));
		if (cycle >= cycles) {
			ck_assert (wait_for_mode (&barApp.player, PLAYER_PLAYING, 10000));
		}
		usleep (delays_us[cycle % cycles]);
		if (cycle % 3 == 2) { BarPlayerSetPaused (&barApp.player, true); }
		BarUiActSkipSong (&barApp, NULL, NULL, 1);
		ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 10000));
		stop_player_thread (&barApp.player, &playerThread);
		BarTestAssertDeviceStarted (&barApp.player, false);
		ck_assert (!barApp.player.soundInitialized);
	}
	/* The lifetime engine still plays a full song after the churn. */
	ck_assert (BarPlaybackStartSong (&barApp, &playerThread));
	ck_assert (wait_for_mode (&barApp.player, PLAYER_PLAYING, 10000));
	ck_assert (null_audio_wait_cursor_at_least (&barApp.player, 1));
	ck_assert (wait_for_mode (&barApp.player, PLAYER_FINISHED, 10000));
	stop_player_thread (&barApp.player, &playerThread);

	null_audio_app_stop (&barApp, &http);
	null_audio_ran ("repeated_start_stop");
}
END_TEST

/* Engine/device-thread creation and teardown, including the reinit path. */
START_TEST (test_null_audio_engine_init_destroy_cycles)
{
	for (unsigned cycle = 0; cycle < 4; ++cycle) {
		player_t player;
		BarSettings_t settings;
		memset (&player, 0, sizeof (player));
		BarSettingsInit (&settings);
		BarPlayerInit (&player, &settings);
		if (!null_audio_ready ("engine_init_destroy_cycles", &player)) {
			BarPlayerDestroy (&player);
			BarSettingsDestroy (&settings);
			return;
		}
		BarPlayerInit (&player, &settings);
		ck_assert (player.engineInitialized);
		ck_assert (player.nullContextInitialized);
		BarTestAssertDeviceStarted (&player, false);
		ck_assert (BarPlayerDestroy (&player));
		ck_assert (!player.nullContextInitialized);
		BarSettingsDestroy (&settings);
	}
	null_audio_ran ("engine_init_destroy_cycles");
}
END_TEST

/* PIANOBAR_TEST_NULL_AUDIO selects the same backend for a whole run. */
START_TEST (test_null_audio_env_selects_null_backend)
{
	if (!integration_enabled ()) { return; }
	player_t player;
	BarSettings_t settings;
	BarPlayerSetNullAudioTestBackend (false);
	ck_assert_int_eq (setenv ("PIANOBAR_TEST_NULL_AUDIO", "1", 1), 0);
	memset (&player, 0, sizeof (player));
	BarSettingsInit (&settings);
	BarPlayerInit (&player, &settings);
	ck_assert_int_eq (unsetenv ("PIANOBAR_TEST_NULL_AUDIO"), 0);
	const bool ready = null_audio_ready ("env_selects_null_backend", &player);
	ck_assert (BarPlayerDestroy (&player));
	BarSettingsDestroy (&settings);
	if (ready) { null_audio_ran ("env_selects_null_backend"); }
}
END_TEST

/* An engine lost after setup is reopened on the same lifetime context. */
START_TEST (test_null_audio_reinit_reuses_context)
{
	player_t player;
	BarSettings_t settings;
	memset (&player, 0, sizeof (player));
	BarSettingsInit (&settings);
	BarPlayerInit (&player, &settings);
	if (null_audio_ready ("reinit_reuses_context", &player)) {
		ma_engine_uninit (&player.engine);
		player.engineInitialized = false;
		BarPlayerInit (&player, &settings);
		ck_assert (player.engineInitialized);
		ck_assert (player.nullContextInitialized);
		ck_assert_ptr_nonnull (ma_engine_get_device (&player.engine));
		null_audio_ran ("reinit_reuses_context");
	}
	BarPlayerDestroy (&player);
	BarSettingsDestroy (&settings);
}
END_TEST

Suite *playback_integration_suite (void) {
	Suite *s = suite_create ("playback_integration");
	TCase *tc = tcase_create ("core");
	tcase_set_timeout (tc, 120.0);
	tcase_add_test (tc, test_playback_fetch_playlist_with_mock_piano);
	tcase_add_test (tc, test_playback_fetch_empty_playlist_clears_station);
	tcase_add_test (tc, test_playback_fetch_session_error_disconnects);
	tcase_add_test (tc, test_playback_fetch_generic_failure_clears_next_station);
	tcase_add_test (tc, test_playback_start_song_plays_http_fixture);
	tcase_add_test (tc, test_player_http_404_finishes_without_hang);
	tcase_add_test (tc, test_player_interrupt_mid_playback);
	tcase_add_test (tc, test_player_pause_resume_stops_device_and_preserves_cursor);
	tcase_add_test (tc, test_player_pause_parks_decoder_until_same_http_song_resumes);
	tcase_add_test (tc, test_playback_two_song_manual_advance);
	tcase_add_loop_test (tc, test_playback_manager_plays_fixture_song, 0,
	                     (int) (sizeof (g_manager_modes) / sizeof (g_manager_modes[0])));
	tcase_add_test (tc, test_fixture_epipe_before_cancel_is_error);
	tcase_add_test (tc, test_fixture_epipe_after_cancel_is_expected);
	tcase_add_test (tc, test_fixture_other_error_after_cancel_is_error);
	suite_add_tcase (s, tc);

	TCase *shutdown = tcase_create ("stream_shutdown");
	tcase_set_timeout (shutdown, 120.0);
	tcase_add_test (shutdown, test_player_paused_active_and_decoded_stop_paths_join_promptly);
	tcase_add_loop_test (shutdown, test_stop_during_stalled_read_joins_promptly, 0, 3);
	tcase_add_test (shutdown, test_stop_during_stalled_open_aborts_quietly);
	tcase_add_test (shutdown, test_stop_requested_before_open_aborts_open);
	tcase_add_test (shutdown, test_stop_flag_does_not_abort_next_song);
	tcase_add_test (shutdown, test_pause_during_stalled_read_does_not_abort);
	suite_add_tcase (s, shutdown);

	/* Real device thread on miniaudio's null backend (no hardware needed). */
	TCase *nullAudio = tcase_create ("null_audio_device");
	tcase_set_timeout (nullAudio, 120.0);
	tcase_add_checked_fixture (nullAudio, null_audio_setup, null_audio_teardown);
	tcase_add_test (nullAudio, test_null_audio_song_plays_to_natural_end);
	tcase_add_test (nullAudio, test_null_audio_pause_resume_stops_and_restarts_device);
	tcase_add_test (nullAudio, test_null_audio_volume_changes_while_playing_and_paused);
	tcase_add_test (nullAudio, test_null_audio_concurrent_controls_until_song_end);
	tcase_add_test (nullAudio, test_null_audio_skip_mid_song_stops_device);
	tcase_add_test (nullAudio, test_null_audio_repeated_start_stop);
	tcase_add_test (nullAudio, test_null_audio_engine_init_destroy_cycles);
	tcase_add_test (nullAudio, test_null_audio_env_selects_null_backend);
	tcase_add_test (nullAudio, test_null_audio_reinit_reuses_context);
	/* Existing device-aware tests, rerun with a real (null) device. */
	tcase_add_test (nullAudio, test_playback_start_song_plays_http_fixture);
	tcase_add_test (nullAudio, test_player_pause_resume_stops_device_and_preserves_cursor);
	tcase_add_test (nullAudio, test_playback_two_song_manual_advance);
	tcase_add_test (nullAudio, test_playback_manager_plays_fixture_song);
	tcase_add_test (nullAudio, test_player_paused_active_and_decoded_stop_paths_join_promptly);
	tcase_add_loop_test (nullAudio, test_stop_during_stalled_read_joins_promptly, 0, 3);
	tcase_add_test (nullAudio, test_pause_during_stalled_read_does_not_abort);
	suite_add_tcase (s, nullAudio);
	return s;
}

