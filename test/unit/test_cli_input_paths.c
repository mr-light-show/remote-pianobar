/*
 * CLI paths: key dispatch inside the Pandora hold, the station list sent to
 * eventcmd, empty or stale answers to interactive prompts, history browsing,
 * eventcmd with a station or song that has no id, and session
 * teardown/reconnect edge cases.
 *
 * main.c is included so its static helpers can be driven directly; its
 * main () is renamed out of the way.
 */

#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../src/bar_constants.h"
#include "../../src/bar_state.h"
#include "../../src/l10n.h"
#include "../../src/log.h"
#include "../../src/player.h"
#include "../../src/settings.h"
#include "../../src/ui.h"
#include "../../src/ui_readline.h"
#include "../test_tmp.h"
#define main BarTestMainProgramCli
#include "../../src/main.c"
#undef main

void BarUiDoPandoraDisconnect (BarApp_t *app, const char *reason,
		const char *resume_station_id_override);
bool BarUiDoPandoraReconnect (BarApp_t *app,
		const char *resume_station_id_override);

typedef void (*CliAction_fn) (BarApp_t *, PianoStation_t *, PianoSong_t *,
		BarUiDispatchContext_t);

/* ------------------------------------------------------------------------
 * Fixture
 * ------------------------------------------------------------------------ */

#define MAX_CALLS 16
static PianoRequestType_t g_types[MAX_CALLS];
static unsigned g_count;
static bool g_heldAtCall[MAX_CALLS];

static unsigned
calls_of (PianoRequestType_t type)
{
	unsigned n = 0;
	for (unsigned i = 0; i < g_count; i++) {
		if (g_types[i] == type) {
			n++;
		}
	}
	return n;
}

static void *
mutex_probe (void *arg)
{
	BarApp_t *app = arg;
	const int rc = pthread_mutex_trylock (&app->pianoHttpMutex);
	if (rc == 0) {
		pthread_mutex_unlock (&app->pianoHttpMutex);
	}
	return (void *) (intptr_t) rc;
}

/* pianoHttpMutex is recursive: another thread has to ask */
static bool
piano_mutex_held (BarApp_t *app)
{
	pthread_t probe;
	void *rc = NULL;
	ck_assert_int_eq (pthread_create (&probe, NULL, mutex_probe, app), 0);
	ck_assert_int_eq (pthread_join (probe, &rc), 0);
	return (intptr_t) rc == EBUSY;
}

/* Records every request (and whether the caller held the mutex); answers
 * station info with one artist seed and modes with one mode */
static bool
recording_call (BarApp_t *app, const PianoRequestType_t type, void *data,
		PianoReturn_t *pRet, CURLcode *wRet)
{
	ck_assert_uint_lt (g_count, MAX_CALLS);
	g_heldAtCall[g_count] = piano_mutex_held (app);
	g_types[g_count++] = type;
	if (type == PIANO_REQUEST_GET_STATION_INFO) {
		PianoRequestDataGetStationInfo_t *req = data;
		PianoArtist_t *artist = calloc (1, sizeof (*artist));
		ck_assert_ptr_nonnull (artist);
		artist->name = strdup ("Seed Artist");
		req->info.artistSeeds = artist;
	} else if (type == PIANO_REQUEST_GET_STATION_MODES) {
		PianoRequestDataGetStationModes_t *req = data;
		PianoStationMode_t *mode = calloc (1, sizeof (*mode));
		ck_assert_ptr_nonnull (mode);
		mode->name = strdup ("Discovery");
		mode->description = strdup ("New music");
		req->retModes = mode;
	}
	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;
	return true;
}

static void
cli_app_init (BarApp_t *app, PianoStation_t *s1, PianoStation_t *s2)
{
	memset (app, 0, sizeof (*app));
	memset (s1, 0, sizeof (*s1));
	memset (s2, 0, sizeof (*s2));
	BarSettingsInit (&app->settings);
	app->settings.uiMode = BAR_UI_MODE_WEB;
	ck_assert (BarL10nInit (&app->l10n, &app->settings));
	BarStateInit (app);
	BarUiPianoHttpMutexInit (app);
	ck_assert_int_eq (pthread_mutex_init (&app->player.lock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.cond, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.audioCond, NULL), 0);
	ck_assert_int_eq (pthread_mutex_init (&app->player.decoderLock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.decoderCond, NULL), 0);
	app->player.settings = &app->settings;
	s1->id = "S1";
	s1->name = "Keep";
	s1->isCreator = true;
	s2->id = "S2";
	s2->name = "Other";
	s2->isCreator = true;
	ck_assert_ptr_nonnull (PianoListAppend (&s1->head, &s2->head));
	app->ph.stations = s1;
	app->settings.loveIcon = strdup ("+");
	app->settings.banIcon = strdup ("-");
	app->settings.tiredIcon = strdup ("t");
	app->settings.atIcon = strdup ("@");
	free (app->settings.listSongFormat);
	app->settings.listSongFormat = strdup ("%i) %a - %t%r");
	g_count = 0;
	BarUiPianoCallSetTestHook (recording_call);
}

static void
cli_app_destroy (BarApp_t *app)
{
	BarUiPianoCallClearTestHook ();
	pthread_cond_destroy (&app->player.decoderCond);
	pthread_mutex_destroy (&app->player.decoderLock);
	pthread_cond_destroy (&app->player.audioCond);
	pthread_cond_destroy (&app->player.cond);
	pthread_mutex_destroy (&app->player.lock);
	BarUiPianoHttpMutexDestroy (app);
	BarL10nDestroy (&app->l10n);
	BarStateDestroy (app);
	BarSettingsDestroy (&app->settings);
}

static void
input_from_fd (BarApp_t *app, int fd)
{
	memset (&app->input, 0, sizeof (app->input));
	FD_ZERO (&app->input.set);
	FD_SET (fd, &app->input.set);
	app->input.fds[0] = fd;
	app->input.fds[1] = -1;
	app->input.maxfd = fd + 1;
}

/* Type `input` ahead; every prompt must be answered (a pipe at EOF never
 * ends a read) */
static void
type_ahead (BarApp_t *app, const char *input)
{
	int pipefd[2];
	ck_assert_int_eq (pipe (pipefd), 0);
	input_from_fd (app, pipefd[0]);
	ck_assert (write (pipefd[1], input, strlen (input)) == (ssize_t) strlen (input));
	close (pipefd[1]);
}

static void
type_ahead_done (BarApp_t *app)
{
	close (app->input.fds[0]);
}

/* Run an action as the CLI dispatcher does: inside the hold */
static void
run_action (BarApp_t *app, CliAction_fn action, PianoStation_t *station,
		PianoSong_t *song, const char *input)
{
	type_ahead (app, input);
	BarUiPianoHold (app);
	action (app, station, song, BAR_DC_GLOBAL);
	BarUiPianoRelease (app);
	type_ahead_done (app);
}

/* Types `later` once the action waits for input without the hold, after
 * unlinking a station as another thread could */
typedef struct {
	BarApp_t *app;
	int writeFd;
	const char *later;
	PianoStation_t *removeStation;
} Feeder_t;

static void *
feeder_thread (void *arg)
{
	Feeder_t *f = arg;
	for (int i = 0; i < 2000; i++) {
		if (pthread_mutex_trylock (&f->app->pianoHttpMutex) == 0) {
			pthread_rwlock_wrlock (&f->app->stateRwlock);
			if (f->removeStation != NULL) {
				f->app->ph.stations = PianoListDeleteP (f->app->ph.stations,
						f->removeStation);
			}
			pthread_rwlock_unlock (&f->app->stateRwlock);
			pthread_mutex_unlock (&f->app->pianoHttpMutex);
			break;
		}
		usleep (1000);
	}
	if (write (f->writeFd, f->later, strlen (f->later)) < 0) {
		/* the reader sees EOF; the main thread's assertions fail */
	}
	close (f->writeFd);
	return NULL;
}

static void
run_action_with_feeder (BarApp_t *app, CliAction_fn action,
		PianoStation_t *station, PianoSong_t *song, const char *ahead,
		PianoStation_t *removeStation, const char *later)
{
	int pipefd[2];
	pthread_t thread;
	Feeder_t feeder = { .app = app, .later = later, .removeStation = removeStation };
	ck_assert_int_eq (pipe (pipefd), 0);
	input_from_fd (app, pipefd[0]);
	ck_assert (write (pipefd[1], ahead, strlen (ahead)) >= 0);
	feeder.writeFd = pipefd[1];
	BarUiPianoHold (app);
	ck_assert_int_eq (pthread_create (&thread, NULL, feeder_thread, &feeder), 0);
	action (app, station, song, BAR_DC_GLOBAL);
	BarUiPianoRelease (app);
	ck_assert_int_eq (pthread_join (thread, NULL), 0);
	type_ahead_done (app);
}

/* An executable eventcmd script that appends its first argument to a marker
 * file. `dir` receives the scratch directory, `marker` the marker path. */
static void
install_eventcmd (BarApp_t *app, char *dir, size_t dirSize, char *marker,
		size_t markerSize, const char *body)
{
	char script[512];
	ck_assert_ptr_nonnull (TestTmpMkdtemp (dir, dirSize, "cli_eventcmd"));
	snprintf (marker, markerSize, "%s/marker", dir);
	snprintf (script, sizeof (script), "%s/eventcmd.sh", dir);
	FILE *f = fopen (script, "w");
	ck_assert_ptr_nonnull (f);
	fprintf (f, "#!/bin/sh\n%s\n", body);
	fclose (f);
	ck_assert_int_eq (chmod (script, 0700), 0);
	free (app->settings.eventCmd);
	app->settings.eventCmd = strdup (script);
}

static void
remove_eventcmd (const char *dir, const char *marker)
{
	char script[512];
	snprintf (script, sizeof (script), "%s/eventcmd.sh", dir);
	unlink (marker);
	unlink (script);
	rmdir (dir);
}

/* Marker file content (empty when the script never ran) */
static const char *
marker_text (const char *marker)
{
	static char text[256];
	text[0] = '\0';
	FILE *f = fopen (marker, "r");
	if (f != NULL) {
		const size_t n = fread (text, 1, sizeof (text) - 1, f);
		text[n] = '\0';
		fclose (f);
	}
	return text;
}

static void
script_body_for (char *body, size_t size, const char *marker)
{
	snprintf (body, size, "cat > /dev/null\necho \"$1\" >> '%s'", marker);
}

/* ------------------------------------------------------------------------
 * main.c: station fetch and key dispatch
 * ------------------------------------------------------------------------ */

/* After the station list is fetched, eventcmd gets "usergetstations" and
 * the Pandora hold is gone again */
START_TEST (test_main_get_stations_runs_eventcmd_and_releases_hold)
{
	BarApp_t app;
	PianoStation_t s1, s2;
	char dir[256], marker[300], body[400];
	cli_app_init (&app, &s1, &s2);
	/* scratch directory and script that record the event name */
	ck_assert_ptr_nonnull (TestTmpMkdtemp (dir, sizeof (dir), "cli_getstations"));
	snprintf (marker, sizeof (marker), "%s/marker", dir);
	script_body_for (body, sizeof (body), marker);
	char script[512];
	snprintf (script, sizeof (script), "%s/eventcmd.sh", dir);
	FILE *f = fopen (script, "w");
	ck_assert_ptr_nonnull (f);
	fprintf (f, "#!/bin/sh\n%s\n", body);
	fclose (f);
	ck_assert_int_eq (chmod (script, 0700), 0);
	free (app.settings.eventCmd);
	app.settings.eventCmd = strdup (script);

	ck_assert (BarMainGetStations (&app));
	ck_assert_uint_eq (calls_of (PIANO_REQUEST_GET_STATIONS), 1);
	ck_assert_str_eq (marker_text (marker), "usergetstations\n");
	ck_assert (!piano_mutex_held (&app));

	unlink (marker);
	unlink (script);
	rmdir (dir);
	cli_app_destroy (&app);
}
END_TEST

static PianoSong_t g_playing;

static void
playing_state_init (BarApp_t *app, PianoStation_t *s1)
{
	memset (&g_playing, 0, sizeof (g_playing));
	g_playing.title = "Playing";
	g_playing.artist = "Artist";
	g_playing.trackToken = "T1";
	g_playing.stationId = "S1";
	app->ph.user.listenerId = (char *) "listener";
	BarStateSetCurrentStation (app, s1);
	app->playlist = &g_playing;
}

/* A key bound to a song action runs with the Pandora hold taken, and the
 * hold is dropped again afterwards */
START_TEST (test_main_user_input_runs_song_action_inside_hold)
{
	BarApp_t app;
	PianoStation_t s1, s2;
	cli_app_init (&app, &s1, &s2);
	playing_state_init (&app, &s1);
	app.settings.keys[BAR_KS_LOVE] = '+';

	type_ahead (&app, "+");
	BarMainHandleUserInput (&app);
	type_ahead_done (&app);

	ck_assert_uint_eq (calls_of (PIANO_REQUEST_RATE_SONG), 1);
	ck_assert_msg (g_heldAtCall[0], "love ran without the Pandora hold");
	ck_assert (!piano_mutex_held (&app));

	app.playlist = NULL;
	cli_app_destroy (&app);
}
END_TEST

/* A key that is not bound to anything takes no hold and calls nothing */
START_TEST (test_main_user_input_ignores_unbound_key)
{
	BarApp_t app;
	PianoStation_t s1, s2;
	cli_app_init (&app, &s1, &s2);
	playing_state_init (&app, &s1);
	app.settings.keys[BAR_KS_LOVE] = '+';

	type_ahead (&app, "z");
	BarMainHandleUserInput (&app);
	type_ahead_done (&app);

	ck_assert_uint_eq (g_count, 0);
	ck_assert (!piano_mutex_held (&app));
	app.playlist = NULL;
	cli_app_destroy (&app);
}
END_TEST

/* ------------------------------------------------------------------------
 * ui.c
 * ------------------------------------------------------------------------ */

#ifdef NDEBUG
/* Releasing a hold that was never taken must not unlock a free mutex; the
 * normal hold/release pair still works afterwards */
START_TEST (test_piano_release_without_hold_is_ignored)
{
	BarApp_t app;
	PianoStation_t s1, s2;
	cli_app_init (&app, &s1, &s2);

	BarUiPianoRelease (&app);
	ck_assert (!piano_mutex_held (&app));
	BarUiPianoHold (&app);
	ck_assert (piano_mutex_held (&app));
	BarUiPianoRelease (&app);
	ck_assert (!piano_mutex_held (&app));

	cli_app_destroy (&app);
}
END_TEST
#endif

/* A listed station without an id can be picked but yields no id, both by
 * number and by auto-selecting the only match */
START_TEST (test_select_station_id_of_station_without_id_is_null)
{
	BarApp_t app;
	PianoStation_t s1, s2;
	cli_app_init (&app, &s1, &s2);
	app.settings.sortOrder = BAR_SORT_NAME_AZ;
	s1.id = NULL;
	s1.name = "Alpha";
	s2.name = "Beta";

	type_ahead (&app, "0\n");
	ck_assert_ptr_null (BarUiSelectStationId (&app, "Pick: ", NULL, false));
	type_ahead_done (&app);

	type_ahead (&app, "Alp\n");
	ck_assert_ptr_null (BarUiSelectStationId (&app, "Pick: ", NULL, true));
	type_ahead_done (&app);

	cli_app_destroy (&app);
}
END_TEST

/* eventcmd that stops reading its input: the write fails and the action
 * carries on instead of hanging or dying */
START_TEST (test_eventcmd_that_closes_its_input_does_not_block)
{
	BarApp_t app;
	PianoStation_t s1, s2;
	char dir[256], script[512];
	cli_app_init (&app, &s1, &s2);
	char *big = malloc (40001);
	ck_assert_ptr_nonnull (big);
	memset (big, 'n', 40000);
	big[40000] = '\0';
	PianoStation_t s3, s4;
	memset (&s3, 0, sizeof (s3));
	memset (&s4, 0, sizeof (s4));
	s1.name = big;
	s2.name = big;
	s3.id = "S3";
	s3.name = big;
	s4.id = "S4";
	s4.name = big;
	ck_assert_ptr_nonnull (PianoListAppend (&s2.head, &s3.head));
	ck_assert_ptr_nonnull (PianoListAppend (&s3.head, &s4.head));

	ck_assert_ptr_nonnull (TestTmpMkdtemp (dir, sizeof (dir), "cli_deaf_eventcmd"));
	snprintf (script, sizeof (script), "%s/eventcmd.sh", dir);
	FILE *f = fopen (script, "w");
	ck_assert_ptr_nonnull (f);
	fputs ("#!/bin/sh\nexec 0<&-\nexit 0\n", f);
	fclose (f);
	ck_assert_int_eq (chmod (script, 0700), 0);
	free (app.settings.eventCmd);
	app.settings.eventCmd = strdup (script);

	void (*oldPipe) (int) = signal (SIGPIPE, SIG_IGN);
	BarUiStartEventCmd (&app.settings, "usergetstations", NULL, NULL, &app.player,
			app.ph.stations, PIANO_RET_OK, CURLE_OK);
	signal (SIGPIPE, oldPipe);
	/* reaching this line means the failed write ended the loop and the
	 * script was reaped */
	ck_assert (!piano_mutex_held (&app));

	unlink (script);
	rmdir (dir);
	s1.head.next = NULL;
	free (big);
	app.ph.stations = NULL;
	cli_app_destroy (&app);
}
END_TEST

/* ------------------------------------------------------------------------
 * ui_act.c: empty answers
 * ------------------------------------------------------------------------ */

/* An empty answer cancels each prompt without calling Pandora */
START_TEST (test_prompts_cancel_on_empty_answer)
{
	BarApp_t app;
	PianoStation_t s1, s2;
	cli_app_init (&app, &s1, &s2);

	run_action (&app, BarUiActAddSharedStation, &s1, NULL, "\n");
	run_action (&app, BarUiActRenameStation, &s1, NULL, "\n");
	ck_assert_str_eq (s1.name, "Keep");

	/* genre stations already known: an empty category cancels */
	PianoGenreCategory_t cat;
	memset (&cat, 0, sizeof (cat));
	cat.name = "Category";
	app.ph.genreStations = &cat;
	run_action (&app, BarUiActStationFromGenre, &s1, NULL, "\n");
	app.ph.genreStations = NULL;

	/* an empty answer picks no station */
	run_action (&app, BarUiActSelectStation, &s1, NULL, "\n");
	ck_assert_ptr_null (BarStateGetNextStation (&app));

	ck_assert_uint_eq (g_count, 0);
	ck_assert (!piano_mutex_held (&app));
	cli_app_destroy (&app);
}
END_TEST

/* ------------------------------------------------------------------------
 * ui_act.c: history
 * ------------------------------------------------------------------------ */

static void
history_init (PianoSong_t *a, PianoSong_t *b)
{
	memset (a, 0, sizeof (*a));
	memset (b, 0, sizeof (*b));
	a->title = "Alpha";
	a->artist = "Artist";
	a->trackToken = "A1";
	a->stationId = "S1";
	b->title = "Beta";
	b->artist = "Artist";
	b->stationId = "S1"; /* no track token */
	ck_assert_ptr_nonnull (PianoListAppend (&a->head, &b->head));
}

/* Out-of-range numbers and filters re-list the history; an empty answer
 * leaves without doing anything */
START_TEST (test_history_out_of_range_and_filter_answers_relist)
{
	BarApp_t app;
	PianoStation_t s1, s2;
	PianoSong_t a, b;
	cli_app_init (&app, &s1, &s2);
	history_init (&a, &b);
	app.songHistory = &a;

	run_action (&app, BarUiActHistory, NULL, NULL, "99\nzzz\n\n");
	ck_assert_uint_eq (g_count, 0);
	ck_assert_ptr_eq (app.songHistory, &a);

	/* the second song has no track token: it cannot be found again */
	run_action (&app, BarUiActHistory, NULL, NULL, "1\n");
	ck_assert_uint_eq (g_count, 0);

	app.songHistory = NULL;
	cli_app_destroy (&app);
}
END_TEST

/* The station or song of the chosen history entry vanishes while the user
 * decides what to do with it: the action is refused */
START_TEST (test_history_choice_gone_while_choosing_action)
{
	BarApp_t app;
	PianoStation_t s1, s2;
	PianoSong_t a, b;
	cli_app_init (&app, &s1, &s2);
	history_init (&a, &b);
	app.songHistory = &a;
	app.settings.keys[BAR_KS_LOVE] = '+';

	run_action_with_feeder (&app, BarUiActHistory, NULL, NULL, "0\n", &s1, "+\n");
	ck_assert_uint_eq (calls_of (PIANO_REQUEST_RATE_SONG), 0);
	ck_assert (!piano_mutex_held (&app));

	app.songHistory = NULL;
	app.ph.stations = &s1;
	cli_app_destroy (&app);
}
END_TEST

/* ------------------------------------------------------------------------
 * ui_act.c: bookmark, manage station
 * ------------------------------------------------------------------------ */

START_TEST (test_bookmark_song_and_artist)
{
	BarApp_t app;
	PianoStation_t s1, s2;
	cli_app_init (&app, &s1, &s2);
	playing_state_init (&app, &s1);

	run_action (&app, BarUiActBookmark, &s1, &g_playing, "s");
	ck_assert_uint_eq (calls_of (PIANO_REQUEST_BOOKMARK_SONG), 1);
	ck_assert_uint_eq (calls_of (PIANO_REQUEST_BOOKMARK_ARTIST), 0);
	run_action (&app, BarUiActBookmark, &s1, &g_playing, "a");
	ck_assert_uint_eq (calls_of (PIANO_REQUEST_BOOKMARK_SONG), 1);
	ck_assert_uint_eq (calls_of (PIANO_REQUEST_BOOKMARK_ARTIST), 1);

	app.playlist = NULL;
	cli_app_destroy (&app);
}
END_TEST

/* The station vanishes while the user chooses what to remove */
START_TEST (test_manage_station_gone_while_choosing_action)
{
	BarApp_t app;
	PianoStation_t s1, s2;
	cli_app_init (&app, &s1, &s2);

	run_action_with_feeder (&app, BarUiActManageStation, &s2, NULL, "", &s2, "a\n");
	ck_assert_uint_eq (calls_of (PIANO_REQUEST_GET_STATION_INFO), 1);
	ck_assert_uint_eq (calls_of (PIANO_REQUEST_DELETE_SEED), 0);
	ck_assert (!piano_mutex_held (&app));

	app.ph.stations = &s1;
	cli_app_destroy (&app);
}
END_TEST

/* Choosing "mode" and then answering nothing leaves the mode alone */
START_TEST (test_manage_station_empty_mode_answer_keeps_mode)
{
	BarApp_t app;
	PianoStation_t s1, s2;
	cli_app_init (&app, &s1, &s2);

	run_action (&app, BarUiActManageStation, &s1, NULL, "m\n");
	ck_assert_uint_eq (calls_of (PIANO_REQUEST_GET_STATION_MODES), 1);
	ck_assert_uint_eq (calls_of (PIANO_REQUEST_SET_STATION_MODE), 0);

	cli_app_destroy (&app);
}
END_TEST

/* ------------------------------------------------------------------------
 * ui_act.c: eventcmd with a selection that has no id or token
 * ------------------------------------------------------------------------ */

/* A station without an id and a song without a track token cannot be found
 * again after eventcmd, but the action and its eventcmd still run */
START_TEST (test_love_runs_eventcmd_for_station_and_song_without_ids)
{
	BarApp_t app;
	PianoStation_t s1, s2, anonymous;
	PianoSong_t song;
	char dir[256], marker[300], body[400];
	cli_app_init (&app, &s1, &s2);
	memset (&anonymous, 0, sizeof (anonymous));
	memset (&song, 0, sizeof (song));
	anonymous.name = "Anonymous";
	song.title = "Untokened";
	song.stationId = "S1";
	ck_assert_ptr_nonnull (TestTmpMkdtemp (dir, sizeof (dir), "cli_noid"));
	snprintf (marker, sizeof (marker), "%s/marker", dir);
	script_body_for (body, sizeof (body), marker);
	char script[512];
	snprintf (script, sizeof (script), "%s/eventcmd.sh", dir);
	FILE *f = fopen (script, "w");
	ck_assert_ptr_nonnull (f);
	fprintf (f, "#!/bin/sh\n%s\n", body);
	fclose (f);
	ck_assert_int_eq (chmod (script, 0700), 0);
	free (app.settings.eventCmd);
	app.settings.eventCmd = strdup (script);

	run_action (&app, BarUiActLoveSong, &anonymous, &song, "");
	ck_assert_uint_eq (calls_of (PIANO_REQUEST_RATE_SONG), 1);
	ck_assert_str_eq (marker_text (marker), "songlove\n");
	ck_assert (!piano_mutex_held (&app));

	unlink (marker);
	unlink (script);
	rmdir (dir);
	cli_app_destroy (&app);
}
END_TEST

/* A history song is found again after eventcmd by its track token, whether
 * the playing song has another token or none */
START_TEST (test_love_runs_eventcmd_for_history_song)
{
	BarApp_t app;
	PianoStation_t s1, s2;
	PianoSong_t untokened, other, target, playing;
	char dir[256], marker[300], body[400];
	cli_app_init (&app, &s1, &s2);
	memset (&untokened, 0, sizeof (untokened));
	memset (&other, 0, sizeof (other));
	memset (&target, 0, sizeof (target));
	memset (&playing, 0, sizeof (playing));
	untokened.stationId = "S1";
	other.trackToken = "H1";
	other.stationId = "S1";
	target.trackToken = "H2";
	target.stationId = "S1";
	ck_assert_ptr_nonnull (PianoListAppend (&untokened.head, &other.head));
	ck_assert_ptr_nonnull (PianoListAppend (&other.head, &target.head));
	app.songHistory = &untokened;
	ck_assert_ptr_nonnull (TestTmpMkdtemp (dir, sizeof (dir), "cli_history_love"));
	snprintf (marker, sizeof (marker), "%s/marker", dir);
	script_body_for (body, sizeof (body), marker);
	char script[512];
	snprintf (script, sizeof (script), "%s/eventcmd.sh", dir);
	FILE *f = fopen (script, "w");
	ck_assert_ptr_nonnull (f);
	fprintf (f, "#!/bin/sh\n%s\n", body);
	fclose (f);
	ck_assert_int_eq (chmod (script, 0700), 0);
	free (app.settings.eventCmd);
	app.settings.eventCmd = strdup (script);

	/* nothing playing */
	run_action (&app, BarUiActLoveSong, &s1, &target, "");
	/* playing song with another token */
	playing.trackToken = "P1";
	app.playlist = &playing;
	run_action (&app, BarUiActLoveSong, &s1, &target, "");
	/* playing song without a token */
	playing.trackToken = NULL;
	run_action (&app, BarUiActLoveSong, &s1, &target, "");

	ck_assert_uint_eq (calls_of (PIANO_REQUEST_RATE_SONG), 3);
	ck_assert_str_eq (marker_text (marker), "songlove\nsonglove\nsonglove\n");

	unlink (marker);
	unlink (script);
	rmdir (dir);
	app.playlist = NULL;
	app.songHistory = NULL;
	cli_app_destroy (&app);
}
END_TEST

/* ------------------------------------------------------------------------
 * Session teardown and reconnect
 * ------------------------------------------------------------------------ */

static void
session_app_init (BarApp_t *app)
{
	char dir[512];
	memset (app, 0, sizeof (*app));
	ck_assert_ptr_nonnull (TestTmpMkdtemp (dir, sizeof (dir), "cli_session"));
	char sub[560];
	snprintf (sub, sizeof (sub), "%s/pianobar", dir);
	ck_assert_int_eq (mkdir (sub, 0700), 0);
	char cfg[640];
	snprintf (cfg, sizeof (cfg), "%s/config", sub);
	FILE *f = fopen (cfg, "w");
	ck_assert_ptr_nonnull (f);
	fputs ("user = testuser\npassword = testpass\n", f);
	fclose (f);
	ck_assert_int_eq (setenv ("HOME", dir, 1), 0);
	ck_assert_int_eq (setenv ("XDG_CONFIG_HOME", dir, 1), 0);
	BarSettingsInit (&app->settings);
	BarSettingsRead (&app->settings);
	ck_assert (BarL10nInit (&app->l10n, &app->settings));
	ck_assert_int_eq (pthread_mutex_init (&app->pianoHttpMutex, NULL), 0);
	BarStateInit (app);
	ck_assert_int_eq (pthread_mutex_init (&app->player.lock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.cond, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.audioCond, NULL), 0);
	ck_assert_int_eq (pthread_mutex_init (&app->player.decoderLock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.decoderCond, NULL), 0);
	app->player.settings = &app->settings;
	app->player.mode = PLAYER_DEAD;
	ck_assert_int_eq (PianoInit (&app->ph, app->settings.partnerUser,
			app->settings.partnerPassword, app->settings.device,
			app->settings.inkey, app->settings.outkey), PIANO_RET_OK);
}

static void
session_app_destroy (BarApp_t *app)
{
	free (app->lastStationId);
	/* session_app_init's handle, or the one a disconnect reset created */
	PianoDestroy (&app->ph);
	pthread_cond_destroy (&app->player.decoderCond);
	pthread_mutex_destroy (&app->player.decoderLock);
	pthread_cond_destroy (&app->player.audioCond);
	pthread_cond_destroy (&app->player.cond);
	pthread_mutex_destroy (&app->player.lock);
	pthread_mutex_destroy (&app->pianoHttpMutex);
	BarL10nDestroy (&app->l10n);
	BarStateDestroy (app);
	BarSettingsDestroy (&app->settings);
}

/* Disconnecting discards the song history */
START_TEST (test_disconnect_discards_song_history)
{
	BarApp_t app;
	session_app_init (&app);
	PianoSong_t *song = calloc (1, sizeof (*song));
	ck_assert_ptr_nonnull (song);
	song->title = strdup ("Old");
	song->trackToken = strdup ("O1");
	app.songHistory = song;

	BarUiDoPandoraDisconnect (&app, "user", NULL);
	ck_assert_ptr_null (app.songHistory);

	session_app_destroy (&app);
}
END_TEST

static PianoStation_t g_hookStation;

static bool
reconnect_call (BarApp_t *app, const PianoRequestType_t type, void *data,
		PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) data;
	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;
	if (type == PIANO_REQUEST_GET_STATIONS && g_hookStation.id != NULL) {
		BarStateSetNextStation (app, &g_hookStation);
	}
	return type == PIANO_REQUEST_LOGIN || type == PIANO_REQUEST_GET_STATIONS;
}

/* Reconnecting with nothing to resume leaves the next station unset and
 * does not report a deleted station */
START_TEST (test_reconnect_without_saved_station_resumes_nothing)
{
	BarApp_t app;
	session_app_init (&app);
	memset (&g_hookStation, 0, sizeof (g_hookStation));
	BarUiPianoCallSetTestHook (reconnect_call);

	ck_assert (BarUiDoPandoraReconnect (&app, NULL));
	BarUiPianoCallClearTestHook ();
	ck_assert_ptr_null (BarStateGetNextStation (&app));
	ck_assert_ptr_null (app.lastStationId);

	session_app_destroy (&app);
}
END_TEST

/* A station that is already queued is not replaced by the saved one */
START_TEST (test_reconnect_keeps_queued_station_over_saved_one)
{
	BarApp_t app;
	session_app_init (&app);
	memset (&g_hookStation, 0, sizeof (g_hookStation));
	g_hookStation.id = "queued";
	g_hookStation.name = "Queued";
	BarUiPianoCallSetTestHook (reconnect_call);

	ck_assert (BarUiDoPandoraReconnect (&app, "saved-station"));
	BarUiPianoCallClearTestHook ();
	ck_assert_ptr_eq (BarStateGetNextStation (&app), &g_hookStation);

	BarStateSetNextStation (&app, NULL);
	session_app_destroy (&app);
}
END_TEST

/* ------------------------------------------------------------------------
 * main.c: log_file in the non-daemon UI modes
 * ------------------------------------------------------------------------ */

/* Open (or not) the log file for one mode, log one error line, and return
 * what reached the file ("" when the file was not created). */
static char *
log_file_after_open (BarUiMode_t mode, bool withLogFile, bool *opened)
{
	char dir[256], path[300];
	BarApp_t app;
	char *text = NULL;

	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = mode;
	ck_assert_ptr_nonnull (TestTmpMkdtemp (dir, sizeof (dir), "main_logfile"));
	snprintf (path, sizeof (path), "%s/pianobar.log", dir);
	free (app.settings.logFile);
	app.settings.logFile = withLogFile ? strdup (path) : NULL;

	log_set_debug_mask (0);
	fflush (stderr);
	const int saved = dup (STDERR_FILENO);
	const int nullFd = open ("/dev/null", O_WRONLY);
	ck_assert (saved >= 0 && nullFd >= 0);
	ck_assert (dup2 (nullFd, STDERR_FILENO) >= 0);
	close (nullFd);

	*opened = BarMainOpenLogFile (&app);
	log_write (LOG_ERROR, "main-log-probe\n");
	log_close_file ();

	fflush (stderr);
	ck_assert (dup2 (saved, STDERR_FILENO) >= 0);
	close (saved);

	FILE *f = fopen (path, "r");
	if (f != NULL) {
		char buf[512] = "";
		size_t n = fread (buf, 1, sizeof (buf) - 1, f);
		buf[n] = '\0';
		fclose (f);
		text = strdup (buf);
		unlink (path);
	} else {
		text = strdup ("");
	}
	rmdir (dir);
	BarSettingsDestroy (&app.settings);
	return text;
}

START_TEST (test_main_open_log_file_in_both_and_cli_modes)
{
	const BarUiMode_t modes[] = {BAR_UI_MODE_BOTH, BAR_UI_MODE_CLI};

	for (size_t i = 0; i < sizeof (modes) / sizeof (modes[0]); i++) {
		bool opened = false;
		char *text = log_file_after_open (modes[i], true, &opened);
		ck_assert (opened);
		ck_assert_ptr_nonnull (strstr (text, "main-log-probe"));
		free (text);
	}
}
END_TEST

/* The web-only daemon redirects stderr to log_file itself (BarDaemonizeSteps). */
START_TEST (test_main_open_log_file_skipped_in_web_mode)
{
	bool opened = true;
	char *text = log_file_after_open (BAR_UI_MODE_WEB, true, &opened);
	ck_assert (!opened);
	ck_assert_str_eq (text, "");
	free (text);
}
END_TEST

START_TEST (test_main_open_log_file_skipped_without_setting)
{
	bool opened = true;
	char *text = log_file_after_open (BAR_UI_MODE_BOTH, false, &opened);
	ck_assert (!opened);
	ck_assert_str_eq (text, "");
	free (text);
}
END_TEST

Suite *
cli_input_paths_suite (void)
{
	Suite *s = suite_create ("CliInputPaths");
	TCase *tc = tcase_create ("Core");
	tcase_set_timeout (tc, 30);
	tcase_add_test (tc, test_main_get_stations_runs_eventcmd_and_releases_hold);
	tcase_add_test (tc, test_main_user_input_runs_song_action_inside_hold);
	tcase_add_test (tc, test_main_user_input_ignores_unbound_key);
	tcase_add_test (tc, test_main_open_log_file_in_both_and_cli_modes);
	tcase_add_test (tc, test_main_open_log_file_skipped_in_web_mode);
	tcase_add_test (tc, test_main_open_log_file_skipped_without_setting);
#ifdef NDEBUG
	tcase_add_test (tc, test_piano_release_without_hold_is_ignored);
#endif
	tcase_add_test (tc, test_select_station_id_of_station_without_id_is_null);
	tcase_add_test (tc, test_eventcmd_that_closes_its_input_does_not_block);
	tcase_add_test (tc, test_prompts_cancel_on_empty_answer);
	tcase_add_test (tc, test_history_out_of_range_and_filter_answers_relist);
	tcase_add_test (tc, test_history_choice_gone_while_choosing_action);
	tcase_add_test (tc, test_bookmark_song_and_artist);
	tcase_add_test (tc, test_manage_station_gone_while_choosing_action);
	tcase_add_test (tc, test_manage_station_empty_mode_answer_keeps_mode);
	tcase_add_test (tc, test_love_runs_eventcmd_for_station_and_song_without_ids);
	tcase_add_test (tc, test_love_runs_eventcmd_for_history_song);
	tcase_add_test (tc, test_disconnect_discards_song_history);
	tcase_add_test (tc, test_reconnect_without_saved_station_resumes_nothing);
	tcase_add_test (tc, test_reconnect_keeps_queued_station_over_saved_one);
	suite_add_tcase (s, tc);
	return s;
}
