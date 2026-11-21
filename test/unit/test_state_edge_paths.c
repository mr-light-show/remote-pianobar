/*
 * Edge-path tests for state snapshots, quickmix changes, failed station
 * deletes, locale parsing, daemon lock path fallback and the playback
 * manager's station announcement.
 */

#include <check.h>
#include <pwd.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../../src/main.h"
#include "../../src/bar_state.h"
#include "../../src/l10n.h"
#include "../../src/playback_manager.h"
#include "../../src/settings.h"
#include "../../src/ui.h"
#include "../../src/websocket/daemon/daemon.h"
#include "../test_tmp.h"

static void
state_app_init (BarApp_t *app)
{
	memset (app, 0, sizeof (*app));
	BarSettingsInit (&app->settings);
	app->settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit (app);
}

static void
state_app_destroy (BarApp_t *app)
{
	BarStateDestroy (app);
	BarSettingsDestroy (&app->settings);
}

/* An unknown change value leaves every station's selection alone */
START_TEST (test_state_quickmix_unknown_change_keeps_selection)
{
	BarApp_t app;
	PianoStation_t a, b;
	state_app_init (&app);
	memset (&a, 0, sizeof (a));
	memset (&b, 0, sizeof (b));
	a.useQuickMix = true;
	app.ph.stations = PianoListAppendP (&a, &b);

	BarStateChangeQuickMixAll (&app, (BarQuickMixChange_t) 99);
	ck_assert (a.useQuickMix);
	ck_assert (!b.useQuickMix);

	state_app_destroy (&app);
}
END_TEST

/* Upcoming snapshot: a station without an id is skipped when looking up the
 * song's station name, and a song without a title keeps a NULL title */
START_TEST (test_state_upcoming_snapshot_skips_station_without_id)
{
	BarApp_t app;
	PianoStation_t noId, named;
	PianoSong_t cur, next;
	state_app_init (&app);
	memset (&noId, 0, sizeof (noId));
	memset (&named, 0, sizeof (named));
	memset (&cur, 0, sizeof (cur));
	memset (&next, 0, sizeof (next));
	noId.name = "Anonymous";
	named.id = "sid";
	named.name = "Named";
	app.ph.stations = PianoListAppendP (&noId, &named);
	next.stationId = "sid";
	cur.head.next = &next.head;
	app.playlist = &cur;

	BarSongSnapshotList_t snap;
	ck_assert (BarStateSnapshotUpcoming (&app, 5, &snap));
	ck_assert_uint_eq (snap.count, 1);
	ck_assert_ptr_null (snap.items[0].title);
	ck_assert_str_eq (snap.items[0].stationName, "Named");
	BarStateFreeSongSnapshot (&snap);

	state_app_destroy (&app);
}
END_TEST

/* A failed DELETE_STATION response keeps the stations even when the request
 * names no station */
START_TEST (test_state_failed_delete_without_station_keeps_selection)
{
	BarApp_t app;
	PianoRequest_t req;
	PianoStation_t station;
	state_app_init (&app);
	ck_assert_int_eq (pthread_mutex_init (&app.player.lock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app.player.cond, NULL), 0);
	memset (&station, 0, sizeof (station));
	station.id = "keep";
	app.ph.stations = &station;
	app.curStation = &station;
	app.nextStation = &station;

	memset (&req, 0, sizeof (req));
	req.type = PIANO_REQUEST_DELETE_STATION;
	req.data = NULL;
	req.responseData = strdup ("{\"stat\":\"fail\",\"code\":0,\"message\":\"x\"}");
	ck_assert_ptr_nonnull (req.responseData);
	ck_assert_int_ne (BarStatePianoResponse (&app, &req), PIANO_RET_OK);
	free (req.responseData);
	ck_assert_ptr_eq (BarStateGetCurrentStation (&app), &station);
	ck_assert_ptr_eq (BarStateGetNextStation (&app), &station);
	ck_assert_ptr_eq (BarStateGetStationList (&app), &station);

	pthread_cond_destroy (&app.player.cond);
	pthread_mutex_destroy (&app.player.lock);
	app.ph.stations = NULL;
	state_app_destroy (&app);
}
END_TEST

/* A locale line with an empty key is ignored; later lines still load */
START_TEST (test_l10n_load_ignores_empty_key_line)
{
	unsetenv ("PIANOBAR_LOCALE_DIR");
	char dir[256];
	ck_assert_ptr_nonnull (TestTmpMkdtemp (dir, sizeof (dir), "l10n_empty_key"));
	char path[512];
	snprintf (path, sizeof (path), "%s/en", dir);
	FILE *out = fopen (path, "w");
	ck_assert_ptr_nonnull (out);
	fputs ("=orphan value\n  = spaced orphan\ncli.login = Loaded\n", out);
	fclose (out);
	setenv ("PIANOBAR_LOCALE_DIR", dir, 1);

	BarSettings_t settings;
	BarSettingsInit (&settings);
	settings.locale = strdup ("en");
	BarL10nContext_t ctx;
	ck_assert (BarL10nInit (&ctx, &settings));
	ck_assert_str_eq (BarL10nGet (&ctx, "cli.login"), "Loaded");

	BarL10nDestroy (&ctx);
	BarSettingsDestroy (&settings);
	unsetenv ("PIANOBAR_LOCALE_DIR");
	unlink (path);
	rmdir (dir);
}
END_TEST

/* Without $HOME the lock file lives under the passwd entry's home */
START_TEST (test_daemon_lock_path_falls_back_to_passwd_home)
{
	const struct passwd *pw = getpwuid (getuid ());
	ck_assert_ptr_nonnull (pw);
	/* getpwuid returns static storage that BarDaemonGetLockFilePath's own
	 * getpwuid call may overwrite: build the expected path first. */
	char want[1024];
	snprintf (want, sizeof (want), "%s/.config/pianobar/pianobar.lock", pw->pw_dir);
	char *savedHome = getenv ("HOME") ? strdup (getenv ("HOME")) : NULL;
	unsetenv ("HOME");

	char *path = BarDaemonGetLockFilePath ();
	if (savedHome != NULL) {
		setenv ("HOME", savedHome, 1);
	}
	free (savedHome);

	ck_assert_ptr_nonnull (path);
	ck_assert_str_eq (path, want);
	free (path);
}
END_TEST

static _Atomic int g_fetches;

static bool
count_playlist_fetch (BarApp_t *app, const PianoRequestType_t type, void *data,
		PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	if (type == PIANO_REQUEST_GET_PLAYLIST) {
		((PianoRequestDataGetPlaylist_t *) data)->retPlaylist = NULL;
		atomic_fetch_add (&g_fetches, 1);
	}
	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;
	return true;
}

/* Re-selecting the station that is already current still refills the
 * playlist; only the "now playing station" line is skipped */
START_TEST (test_manager_refetches_playlist_for_current_station)
{
	BarApp_t app;
	PianoStation_t st;
	memset (&st, 0, sizeof (st));
	st.id = "same";
	st.name = "Same";
	memset (&app, 0, sizeof (app));
	app.settings.npStationFormat = strdup ("Station \"%n\" (%i)");
	ck_assert_ptr_nonnull (app.settings.npStationFormat);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit (&app);
	pthread_mutex_init (&app.player.lock, NULL);
	pthread_cond_init (&app.player.cond, NULL);
	app.player.mode = PLAYER_DEAD;
	BarStateSetCurrentStation (&app, &st);
	BarStateSetNextStation (&app, &st);
	atomic_store (&g_fetches, 0);
	BarUiPianoCallSetTestHook (count_playlist_fetch);

	ck_assert (BarPlaybackManagerStart (&app));
	pthread_mutex_lock (&app.player.lock);
	pthread_cond_broadcast (&app.player.cond);
	pthread_mutex_unlock (&app.player.lock);
	for (int i = 0; i < 500 && atomic_load (&g_fetches) == 0; i++) {
		usleep (10000);
	}
	ck_assert_int_gt (atomic_load (&g_fetches), 0);

	atomic_store_explicit (&app.doQuit, true, memory_order_relaxed);
	BarPlaybackManagerStop (&app);
	BarUiPianoCallClearTestHook ();

	pthread_mutex_destroy (&app.player.lock);
	pthread_cond_destroy (&app.player.cond);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

Suite *
state_edge_paths_suite (void)
{
	Suite *s = suite_create ("StateEdgePaths");
	TCase *tc = tcase_create ("Core");
	tcase_set_timeout (tc, 30);
	tcase_add_test (tc, test_state_quickmix_unknown_change_keeps_selection);
	tcase_add_test (tc, test_state_upcoming_snapshot_skips_station_without_id);
	tcase_add_test (tc, test_state_failed_delete_without_station_keeps_selection);
	tcase_add_test (tc, test_l10n_load_ignores_empty_key_line);
	tcase_add_test (tc, test_daemon_lock_path_falls_back_to_passwd_home);
	tcase_add_test (tc, test_manager_refetches_playlist_for_current_station);
	suite_add_tcase (s, tc);
	return s;
}
