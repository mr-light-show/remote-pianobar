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
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

#include "../../src/bar_state.h"
#include "../../src/interrupt.h"
#include "../../src/playback_lifecycle.h"
#include "../../src/settings.h"
#include "../../src/l10n.h"
#include "../../src/player.h"

static void setup_playback_app (BarApp_t *app) {
	memset (app, 0, sizeof (*app));
	BarSettingsInit (&app->settings);
	app->settings.uiMode = BAR_UI_MODE_CLI;
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
	suite_add_tcase (s, tc);
	return s;
}
