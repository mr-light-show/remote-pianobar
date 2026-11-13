/*
Copyright (c) 2026

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
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../src/main.h"
#include "../../src/settings.h"
#include "../../src/bar_state.h"
#include "../../src/ui.h"
#include "../../src/libpiano/piano.h"
#include "../../src/ui_dispatch.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "../test_tmp.h"

int progressCb (void *data, curl_off_t dltotal, curl_off_t dlnow,
		curl_off_t ultotal, curl_off_t ulnow);

static char *read_tmpfile (FILE *stream)
{
	char *buf = NULL;
	size_t cap = 0;
	size_t len = 0;
	int c;

	rewind (stream);
	while ((c = fgetc (stream)) != EOF) {
		if (len + 1 >= cap) {
			cap = cap == 0 ? 256 : cap * 2;
			char *new_buf = realloc (buf, cap);
			ck_assert_ptr_nonnull (new_buf);
			buf = new_buf;
		}
		buf[len++] = (char) c;
	}
	if (buf == NULL) {
		buf = strdup ("");
	} else {
		buf[len] = '\0';
	}
	return buf;
}

START_TEST (test_print_startup_info_cli_mode_omits_web_details)
{
	BarApp_t app;
	FILE *out;
	char *text;

	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;

	out = tmpfile ();
	ck_assert_ptr_nonnull (out);
	BarPrintStartupInfo (&app, 0, false, out);
	text = read_tmpfile (out);

	ck_assert (strstr (text, "Welcome") != NULL);
	ck_assert (strstr (text, "Web interface") == NULL);

	free (text);
	fclose (out);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_print_startup_info_web_mode_includes_url)
{
	BarApp_t app;
	FILE *out;
	char *text;

	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.websocketHost = strdup ("192.0.2.9");
	app.settings.websocketPort = 8088;

	out = tmpfile ();
	ck_assert_ptr_nonnull (out);
	BarPrintStartupInfo (&app, 0, false, out);
	text = read_tmpfile (out);

	ck_assert (strstr (text, "192.0.2.9") != NULL);
	ck_assert (strstr (text, "8088") != NULL);

	free (text);
	fclose (out);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_print_startup_info_bind_all_shows_bind_address)
{
	BarApp_t app;
	FILE *out;
	char *text;

	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_BOTH;
	app.settings.websocketHost = strdup ("0.0.0.0");
	app.settings.websocketPort = 8080;

	out = tmpfile ();
	ck_assert_ptr_nonnull (out);
	BarPrintStartupInfo (&app, 0, false, out);
	text = read_tmpfile (out);

	ck_assert (strstr (text, "http://0.0.0.0:8080/") != NULL);

	free (text);
	fclose (out);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_print_startup_info_daemon_includes_pid_and_pid_file)
{
	BarApp_t app;
	FILE *out;
	char *text;

	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.websocketHost = strdup ("127.0.0.1");
	app.settings.websocketPort = 8080;
	app.settings.pidFile = strdup ("/tmp/pianobar-test.pid");

	out = tmpfile ();
	ck_assert_ptr_nonnull (out);
	BarPrintStartupInfo (&app, 4242, true, out);
	text = read_tmpfile (out);

	ck_assert (strstr (text, "PID: 4242") != NULL);
	ck_assert (strstr (text, "/tmp/pianobar-test.pid") != NULL);

	free (text);
	fclose (out);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_sorted_stations_orders_by_name_az)
{
	PianoStation_t beta, alpha;
	size_t count = 0;
	PianoStation_t **sorted;

	memset (&beta, 0, sizeof (beta));
	memset (&alpha, 0, sizeof (alpha));
	beta.name = "Beta Station";
	alpha.name = "Alpha Station";
	beta.head.next = (PianoListHead_t *) &alpha;

	sorted = BarSortedStations (&beta, &count, BAR_SORT_NAME_AZ);
	ck_assert_ptr_nonnull (sorted);
	ck_assert_uint_eq (count, 2);
	ck_assert_str_eq (sorted[0]->name, "Alpha Station");
	ck_assert_str_eq (sorted[1]->name, "Beta Station");
	free (sorted);
}
END_TEST

START_TEST (test_sorted_stations_orders_by_name_za)
{
	PianoStation_t beta, alpha;
	size_t count = 0;
	PianoStation_t **sorted;

	memset (&beta, 0, sizeof (beta));
	memset (&alpha, 0, sizeof (alpha));
	beta.name = "Beta Station";
	alpha.name = "Alpha Station";
	beta.head.next = (PianoListHead_t *) &alpha;

	sorted = BarSortedStations (&beta, &count, BAR_SORT_NAME_ZA);
	ck_assert_ptr_nonnull (sorted);
	ck_assert_uint_eq (count, 2);
	ck_assert_str_eq (sorted[0]->name, "Beta Station");
	ck_assert_str_eq (sorted[1]->name, "Alpha Station");
	free (sorted);
}
END_TEST

START_TEST (test_progress_cb_returns_interrupt_state)
{
	_Atomic sig_atomic_t interrupted = 0;

	ck_assert_int_eq (progressCb (&interrupted, 0, 0, 0, 0), 0);
	atomic_store_explicit (&interrupted, 1, memory_order_relaxed);
	ck_assert_int_eq (progressCb (&interrupted, 0, 0, 0, 0), 1);
}
END_TEST

static bool
mock_ui_piano_logged (BarApp_t *app, const PianoRequestType_t type, void *data,
                      PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	(void) data;
	(void) type;
	*pRet = PIANO_RET_OK;
	*wRet = CURLE_OK;
	return true;
}

START_TEST (test_ui_piano_call_logged_delegates_to_hook)
{
	BarApp_t app;
	PianoReturn_t pRet = PIANO_RET_ERR;
	CURLcode wRet = CURLE_FAILED_INIT;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);

	BarUiPianoCallSetTestHook (mock_ui_piano_logged);
	ck_assert (BarUiPianoCallLogged (&app, PIANO_REQUEST_GET_SETTINGS, NULL,
	                                 "Loading settings", &pRet, &wRet));
	BarUiPianoCallClearTestHook ();

	ck_assert_int_eq (pRet, PIANO_RET_OK);
	ck_assert_int_eq (wRet, CURLE_OK);
	BarSettingsDestroy (&app.settings);
}
END_TEST

/* Real BarUiPianoCall path (no test hook): PianoRequest + HTTP, which
 * exercises progressCb and the atomic interrupt snapshot in BarPianoHttpRequest. */
START_TEST (test_ui_piano_call_http_hits_progress_cb)
{
	BarApp_t app;
	PianoRequestDataLogin_t loginData;
	PianoReturn_t pRet = PIANO_RET_OK;
	CURLcode wRet = CURLE_OK;

	memset (&app, 0, sizeof (app));
	memset (&loginData, 0, sizeof (loginData));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_BOTH;
	free (app.settings.rpcHost);
	free (app.settings.rpcTlsPort);
	app.settings.rpcHost = strdup ("127.0.0.1");
	app.settings.rpcTlsPort = strdup ("1");
	app.settings.timeout = 1;
	app.settings.maxRetry = 1;
	app.settings.username = strdup ("user");
	app.settings.password = strdup ("pass");
	ck_assert_ptr_nonnull (app.settings.rpcHost);
	ck_assert_ptr_nonnull (app.settings.rpcTlsPort);

	BarStateInit (&app);
	BarUiPianoHttpMutexInit (&app);
	app.http = curl_easy_init ();
	ck_assert_ptr_nonnull (app.http);
	ck_assert_int_eq (PianoInit (&app.ph, "partner-user", "partner-pass",
			"device", "0123456789abcdef", "0123456789abcdef"), PIANO_RET_OK);

	loginData.user = app.settings.username;
	loginData.password = app.settings.password;
	loginData.step = 0;
	BarUiPianoCallClearTestHook ();
	ck_assert (!BarUiPianoCall (&app, PIANO_REQUEST_LOGIN, &loginData, &pRet, &wRet));

	curl_easy_cleanup (app.http);
	app.http = NULL;
	PianoDestroy (&app.ph);
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static void *ui_piano_mutex_probe (void *arg) {
	BarApp_t *app = arg;
	const int rc = pthread_mutex_trylock (&app->pianoHttpMutex);
	if (rc == 0) {
		pthread_mutex_unlock (&app->pianoHttpMutex);
	}
	return (void *) (intptr_t) rc;
}

static bool ui_piano_mutex_held (BarApp_t *app) {
	pthread_t probe;
	void *rc = NULL;
	ck_assert_int_eq (pthread_create (&probe, NULL, ui_piano_mutex_probe, app), 0);
	ck_assert_int_eq (pthread_join (probe, &rc), 0);
	return (intptr_t) rc == EBUSY;
}

/* Suspend gives up every hold of this thread (e.g. before terminal input);
 * Resume takes the same number back */
START_TEST (test_ui_piano_hold_suspend_resume)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarUiPianoHttpMutexInit (&app);

	ck_assert_uint_eq (BarUiPianoSuspend (&app), 0);
	BarUiPianoResume (&app, 0);
	ck_assert (!ui_piano_mutex_held (&app));

	BarUiPianoHold (&app);
	BarUiPianoHold (&app);
	ck_assert (ui_piano_mutex_held (&app));
	const unsigned int holds = BarUiPianoSuspend (&app);
	ck_assert_uint_eq (holds, 2);
	ck_assert (!ui_piano_mutex_held (&app));
	BarUiPianoResume (&app, holds);
	ck_assert (ui_piano_mutex_held (&app));
	BarUiPianoRelease (&app);
	ck_assert (ui_piano_mutex_held (&app));
	BarUiPianoRelease (&app);
	ck_assert (!ui_piano_mutex_held (&app));

	BarUiPianoHttpMutexDestroy (&app);
}
END_TEST

START_TEST (test_ui_dispatch_piano_lock_policy)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.keys[BAR_KS_LOVE] = '+';

	ck_assert_int_eq (BarUiDispatchKeyToId (&app, '+'), BAR_KS_LOVE);
	ck_assert_int_eq (BarUiDispatchKeyToId (&app, '\x7f'), BAR_KS_COUNT);
	ck_assert (BarUiDispatchHoldsPianoLock (BAR_KS_LOVE));
	ck_assert (BarUiDispatchHoldsPianoLock (BAR_KS_EXPLAIN));
	ck_assert (!BarUiDispatchHoldsPianoLock (BAR_KS_PLAYPAUSE));
	ck_assert (!BarUiDispatchHoldsPianoLock (BAR_KS_PANDORA_RECONNECT));
	ck_assert (!BarUiDispatchHoldsPianoLock (BAR_KS_COUNT));

	BarSettingsDestroy (&app.settings);
}
END_TEST

static void ui_input_from_string (BarApp_t *app, const char *text) {
	int pipefd[2];
	ck_assert_int_eq (pipe (pipefd), 0);
	ck_assert (write (pipefd[1], text, strlen (text)) == (ssize_t) strlen (text));
	close (pipefd[1]);
	FD_ZERO (&app->input.set);
	FD_SET (pipefd[0], &app->input.set);
	app->input.fds[0] = pipefd[0];
	app->input.fds[1] = -1;
	app->input.maxfd = pipefd[0] + 1;
}

static unsigned g_selectCallbackCalls;

static void select_callback_toggles_all (BarApp_t *app, char *buf) {
	g_selectCallbackCalls++;
	if (strcmp (buf, "t") == 0) {
		BarStateChangeQuickMixAll (app, BAR_QUICKMIX_TOGGLE_ALL);
		*buf = '\0';
	}
}

/* B4: stations are picked from a copy and returned by id */
START_TEST (test_ui_select_station_id_returns_copied_id)
{
	BarApp_t app;
	PianoStation_t a, b;
	memset (&app, 0, sizeof (app));
	memset (&a, 0, sizeof (a));
	memset (&b, 0, sizeof (b));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;
	app.settings.sortOrder = BAR_SORT_NAME_AZ;
	BarStateInit (&app);
	a.id = "id-a";
	a.name = "Alpha";
	b.id = "id-b";
	b.name = "Beta";
	b.displayName = "Bravo";
	app.ph.stations = PianoListAppendP (&a, &b);

	/* "t" goes to the callback (which changes the list), then pick #1 */
	g_selectCallbackCalls = 0;
	ui_input_from_string (&app, "t\n1\n");
	char *id = BarUiSelectStationId (&app, "Pick: ", select_callback_toggles_all, false);
	ck_assert_str_eq (id, "id-b");
	ck_assert_uint_eq (g_selectCallbackCalls, 1);
	ck_assert (a.useQuickMix && b.useQuickMix);
	free (id);
	close (app.input.fds[0]);

	/* Autoselect the only station matching the filter */
	ui_input_from_string (&app, "Bra\n");
	id = BarUiSelectStationId (&app, "Pick: ", NULL, true);
	ck_assert_str_eq (id, "id-b");
	free (id);
	close (app.input.fds[0]);

	/* Empty input aborts */
	ui_input_from_string (&app, "\n");
	ck_assert_ptr_null (BarUiSelectStationId (&app, "Pick: ", NULL, false));
	close (app.input.fds[0]);

	/* No stations */
	app.ph.stations = NULL;
	ck_assert_ptr_null (BarUiSelectStationId (&app, "Pick: ", NULL, false));

	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

typedef struct {
	BarApp_t *app;
	const char *fifo;
	atomic_bool sawReleased;
} BarTestEventcmdProbe_t;

/* Waits (bounded) until pianoHttpMutex can be taken, then lets the fake
 * eventcmd exit by writing to the FIFO it blocks on. */
static void *eventcmd_probe_thread (void *arg) {
	BarTestEventcmdProbe_t *p = arg;
	for (int i = 0; i < 2000; i++) {
		if (pthread_mutex_trylock (&p->app->pianoHttpMutex) == 0) {
			pthread_mutex_unlock (&p->app->pianoHttpMutex);
			atomic_store (&p->sawReleased, true);
			break;
		}
		usleep (1000);
	}
	int fd = -1;
	for (int i = 0; i < 5000 && fd == -1; i++) {
		fd = open (p->fifo, O_WRONLY | O_NONBLOCK);
		if (fd == -1) {
			usleep (1000);
		}
	}
	ck_assert_int_ne (fd, -1);
	ck_assert (write (fd, "x\n", 2) == 2);
	close (fd);
	return NULL;
}

/* The eventcmd child is reaped without the caller's pianoHttpMutex hold, so
 * a slow script does not stall other Pandora users */
START_TEST (test_ui_eventcmd_wait_releases_piano_hold)
{
	BarApp_t app;
	char dir[512], script[600], fifo[600];
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	BarUiPianoHttpMutexInit (&app);
	ck_assert_int_eq (pthread_mutex_init (&app.player.lock, NULL), 0);

	ck_assert_ptr_nonnull (TestTmpMkdtemp (dir, sizeof (dir), "eventcmd"));
	snprintf (fifo, sizeof (fifo), "%s/release", dir);
	ck_assert_int_eq (mkfifo (fifo, 0600), 0);
	snprintf (script, sizeof (script), "%s/eventcmd.sh", dir);
	FILE *f = fopen (script, "w");
	ck_assert_ptr_nonnull (f);
	fprintf (f, "#!/bin/sh\ncat > /dev/null\nread x < '%s'\n", fifo);
	fclose (f);
	ck_assert_int_eq (chmod (script, 0700), 0);
	app.settings.eventCmd = strdup (script);

	BarTestEventcmdProbe_t probe = { .app = &app, .fifo = fifo };
	atomic_init (&probe.sawReleased, false);
	pthread_t thread;
	BarUiPianoHold (&app);
	ck_assert_int_eq (pthread_create (&thread, NULL, eventcmd_probe_thread, &probe), 0);
	BarUiStartEventCmd (&app.settings, "songstart", NULL, NULL, &app.player,
			NULL, PIANO_RET_OK, CURLE_OK);
	/* The hold is restored for the caller */
	ck_assert (ui_piano_mutex_held (&app));
	BarUiPianoRelease (&app);
	ck_assert_int_eq (pthread_join (thread, NULL), 0);
	ck_assert (atomic_load (&probe.sawReleased));

	pthread_mutex_destroy (&app.player.lock);
	BarUiPianoHttpMutexDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

Suite *ui_suite (void)
{
	Suite *s = suite_create ("ui");
	TCase *tc = tcase_create ("core");

	tcase_add_test (tc, test_print_startup_info_cli_mode_omits_web_details);
	tcase_add_test (tc, test_print_startup_info_web_mode_includes_url);
	tcase_add_test (tc, test_print_startup_info_bind_all_shows_bind_address);
	tcase_add_test (tc, test_print_startup_info_daemon_includes_pid_and_pid_file);
	tcase_add_test (tc, test_sorted_stations_orders_by_name_az);
	tcase_add_test (tc, test_sorted_stations_orders_by_name_za);
	tcase_add_test (tc, test_progress_cb_returns_interrupt_state);
	tcase_add_test (tc, test_ui_piano_call_logged_delegates_to_hook);
	tcase_add_test (tc, test_ui_piano_call_http_hits_progress_cb);
	tcase_add_test (tc, test_ui_piano_hold_suspend_resume);
	tcase_add_test (tc, test_ui_dispatch_piano_lock_policy);
	tcase_add_test (tc, test_ui_select_station_id_returns_copied_id);
	tcase_add_test (tc, test_ui_eventcmd_wait_releases_piano_hold);
	tcase_set_timeout (tc, 15);
	suite_add_tcase (s, tc);
	return s;
}
