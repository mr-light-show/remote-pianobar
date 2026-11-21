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
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include "../test_tmp.h"
#include "../../src/interrupt.h"
#include "../../src/log.h"

int progressCb (void *data, curl_off_t dltotal, curl_off_t dlnow,
		curl_off_t ultotal, curl_off_t ulnow);

/* httpFetchCb is non-static (like progressCb) so tests can reach it; the
 * buffer layout mirrors the private struct in src/ui.c. */
typedef struct {
	char *data;
	size_t pos;
} HttpFetchBuffer_t;

size_t httpFetchCb (char *ptr, size_t size, size_t nmemb, void *userdata);

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

/* While shutdown is requested curl's progress callback aborts the request,
 * whatever the request's own interrupt flag says */
START_TEST (test_progress_cb_aborts_on_shutdown)
{
	_Atomic sig_atomic_t interrupted = 0;

	BarInterruptSetShutdown (true);
	ck_assert_int_eq (progressCb (&interrupted, 0, 0, 0, 0), 1);
	BarInterruptSetShutdown (false);
	ck_assert_int_eq (progressCb (&interrupted, 0, 0, 0, 0), 0);
}
END_TEST

/* A loopback listener on a kernel-chosen port */
static int ui_listen_loopback (uint16_t *port) {
	struct sockaddr_in addr;
	socklen_t len = sizeof (addr);
	const int fd = socket (AF_INET, SOCK_STREAM, 0);
	ck_assert_int_ge (fd, 0);
	memset (&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	ck_assert_int_eq (bind (fd, (struct sockaddr *) &addr, sizeof (addr)), 0);
	ck_assert_int_eq (listen (fd, 8), 0);
	ck_assert_int_eq (getsockname (fd, (struct sockaddr *) &addr, &len), 0);
	*port = ntohs (addr.sin_port);
	return fd;
}

/* App for a real login request (partner login, TLS) to 127.0.0.1:port */
static void ui_http_app_init (BarApp_t *app, PianoRequestDataLogin_t *login,
		uint16_t port, unsigned int maxRetry) {
	char portStr[8];

	memset (app, 0, sizeof (*app));
	memset (login, 0, sizeof (*login));
	BarSettingsInit (&app->settings);
	app->settings.uiMode = BAR_UI_MODE_BOTH;
	free (app->settings.rpcHost);
	free (app->settings.rpcTlsPort);
	snprintf (portStr, sizeof (portStr), "%u", (unsigned int) port);
	app->settings.rpcHost = strdup ("127.0.0.1");
	app->settings.rpcTlsPort = strdup (portStr);
	app->settings.timeout = 5;
	app->settings.maxRetry = maxRetry;
	app->settings.username = strdup ("user");
	app->settings.password = strdup ("pass");
	BarStateInit (app);
	BarUiPianoHttpMutexInit (app);
	app->http = curl_easy_init ();
	ck_assert_ptr_nonnull (app->http);
	ck_assert_int_eq (PianoInit (&app->ph, "partner-user", "partner-pass",
			"device", "0123456789abcdef", "0123456789abcdef"), PIANO_RET_OK);
	login->user = app->settings.username;
	login->password = app->settings.password;
	BarUiPianoCallClearTestHook ();
}

static void ui_http_app_free (BarApp_t *app) {
	curl_easy_cleanup (app->http);
	app->http = NULL;
	PianoDestroy (&app->ph);
	BarUiPianoHttpMutexDestroy (app);
	BarStateDestroy (app);
	BarSettingsDestroy (&app->settings);
}

/* Shutdown requested before the call: no connection is made at all */
START_TEST (test_ui_piano_call_not_sent_after_shutdown)
{
	BarApp_t app;
	PianoRequestDataLogin_t login;
	PianoReturn_t pRet = PIANO_RET_ERR;
	CURLcode wRet = CURLE_OK;
	uint16_t port;
	const int lfd = ui_listen_loopback (&port);

	ui_http_app_init (&app, &login, port, 1);
	BarInterruptSetShutdown (true);
	ck_assert (!BarUiPianoCall (&app, PIANO_REQUEST_LOGIN, &login, &pRet, &wRet));
	BarInterruptSetShutdown (false);
	ck_assert_int_eq (wRet, CURLE_ABORTED_BY_CALLBACK);

	struct pollfd pfd = { .fd = lfd, .events = POLLIN };
	ck_assert_int_eq (poll (&pfd, 1, 200), 0); /* nobody connected */
	close (lfd);
	ui_http_app_free (&app);
}
END_TEST

/* Accepts connections and hangs up on them; requests shutdown before
 * hanging up on the first, so the retry that would follow must not start */
typedef struct {
	int lfd;
	int accepted;
} UiHangupServer_t;

static void *ui_hangup_server (void *arg) {
	UiHangupServer_t *srv = arg;
	struct pollfd pfd = { .fd = srv->lfd, .events = POLLIN };
	while (poll (&pfd, 1, 1000) > 0) {
		const int fd = accept (srv->lfd, NULL, NULL);
		if (fd < 0) {
			break;
		}
		if (++srv->accepted == 1) {
			BarInterruptSetShutdown (true);
		}
		close (fd);
	}
	return NULL;
}

START_TEST (test_ui_piano_call_shutdown_stops_retries)
{
	BarApp_t app;
	PianoRequestDataLogin_t login;
	PianoReturn_t pRet = PIANO_RET_ERR;
	CURLcode wRet = CURLE_OK;
	UiHangupServer_t srv = { .accepted = 0 };
	uint16_t port;
	pthread_t thread;

	srv.lfd = ui_listen_loopback (&port);
	ui_http_app_init (&app, &login, port, 5);
	ck_assert_int_eq (pthread_create (&thread, NULL, ui_hangup_server, &srv), 0);
	ck_assert (!BarUiPianoCall (&app, PIANO_REQUEST_LOGIN, &login, &pRet, &wRet));
	ck_assert_int_eq (pthread_join (thread, NULL), 0);
	BarInterruptSetShutdown (false);

	ck_assert_int_eq (wRet, CURLE_ABORTED_BY_CALLBACK);
	ck_assert_int_eq (srv.accepted, 1); /* no retry after the hang-up */
	close (srv.lfd);
	ui_http_app_free (&app);
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

/* Capture what is written to stdout and stderr between begin and end. */
typedef struct {
	char *out;
	char *err;
	FILE *outFile;
	FILE *errFile;
	int savedOut;
	int savedErr;
} UiCapture_t;

static void ui_capture_begin (UiCapture_t *cap)
{
	cap->outFile = tmpfile ();
	cap->errFile = tmpfile ();
	ck_assert_ptr_nonnull (cap->outFile);
	ck_assert_ptr_nonnull (cap->errFile);
	fflush (stdout);
	fflush (stderr);
	cap->savedOut = dup (STDOUT_FILENO);
	cap->savedErr = dup (STDERR_FILENO);
	ck_assert (cap->savedOut >= 0 && cap->savedErr >= 0);
	ck_assert (dup2 (fileno (cap->outFile), STDOUT_FILENO) >= 0);
	ck_assert (dup2 (fileno (cap->errFile), STDERR_FILENO) >= 0);
}

static void ui_capture_end (UiCapture_t *cap)
{
	fflush (stdout);
	fflush (stderr);
	ck_assert (dup2 (cap->savedOut, STDOUT_FILENO) >= 0);
	ck_assert (dup2 (cap->savedErr, STDERR_FILENO) >= 0);
	close (cap->savedOut);
	close (cap->savedErr);
	cap->out = read_tmpfile (cap->outFile);
	cap->err = read_tmpfile (cap->errFile);
	fclose (cap->outFile);
	fclose (cap->errFile);
}

static void ui_capture_msg (const BarSettings_t *settings, BarUiMsg_t type,
		const char *text, UiCapture_t *cap)
{
	ui_capture_begin (cap);
	BarUiMsg (settings, type, "%s", text);
	ui_capture_end (cap);
}

static void ui_capture_free (UiCapture_t *cap)
{
	free (cap->out);
	free (cap->err);
}

/* The web-only daemon's stdout is /dev/null: without a log_file its fatal
 * MSG_ERR text would be lost, so it goes to stderr (the journal). */
START_TEST (test_ui_msg_err_goes_to_stderr_in_web_only_without_log_file)
{
	BarSettings_t settings;
	UiCapture_t cap;

	log_set_debug_mask (0);
	BarSettingsInit (&settings);
	settings.uiMode = BAR_UI_MODE_WEB;
	free (settings.logFile);
	settings.logFile = NULL;

	ui_capture_msg (&settings, MSG_ERR, "boom\n", &cap);
	ck_assert_ptr_nonnull (strstr (cap.err, "boom"));
	ck_assert_ptr_null (strstr (cap.err, "\033[2K"));
	ck_assert_str_eq (cap.out, "");
	ui_capture_free (&cap);
	BarSettingsDestroy (&settings);
}
END_TEST

START_TEST (test_ui_msg_err_in_web_only_uses_configured_format)
{
	BarSettings_t settings;
	UiCapture_t cap;

	log_set_debug_mask (0);
	BarSettingsInit (&settings);
	settings.uiMode = BAR_UI_MODE_WEB;
	free (settings.logFile);
	settings.logFile = NULL;
	free (settings.msgFormat[MSG_ERR].prefix);
	free (settings.msgFormat[MSG_ERR].postfix);
	settings.msgFormat[MSG_ERR].prefix = strdup ("PRE<");
	settings.msgFormat[MSG_ERR].postfix = strdup (">POST");

	ui_capture_msg (&settings, MSG_ERR, "boom", &cap);
	ck_assert_str_eq (cap.err, "PRE<boom>POST");
	ui_capture_free (&cap);
	BarSettingsDestroy (&settings);
}
END_TEST

START_TEST (test_ui_msg_info_stays_on_stdout_in_web_only)
{
	BarSettings_t settings;
	UiCapture_t cap;

	log_set_debug_mask (0);
	BarSettingsInit (&settings);
	settings.uiMode = BAR_UI_MODE_WEB;
	free (settings.logFile);
	settings.logFile = NULL;

	ui_capture_msg (&settings, MSG_INFO, "hello\n", &cap);
	ck_assert_ptr_nonnull (strstr (cap.out, "hello"));
	ck_assert_str_eq (cap.err, "");
	ui_capture_free (&cap);
	BarSettingsDestroy (&settings);
}
END_TEST

START_TEST (test_ui_msg_err_stays_on_stdout_with_log_file)
{
	BarSettings_t settings;
	UiCapture_t cap;

	log_set_debug_mask (0);
	BarSettingsInit (&settings);
	settings.uiMode = BAR_UI_MODE_WEB;
	free (settings.logFile);
	settings.logFile = strdup ("/nonexistent/pianobar-test.log");

	ui_capture_msg (&settings, MSG_ERR, "boom\n", &cap);
	ck_assert_ptr_nonnull (strstr (cap.out, "boom"));
	ck_assert_str_eq (cap.err, "");
	ui_capture_free (&cap);
	BarSettingsDestroy (&settings);
}
END_TEST

START_TEST (test_ui_msg_err_stays_on_stdout_in_cli_and_both)
{
	BarSettings_t settings;
	UiCapture_t cap;
	const BarUiMode_t modes[] = {BAR_UI_MODE_CLI, BAR_UI_MODE_BOTH};

	log_set_debug_mask (0);
	BarSettingsInit (&settings);
	free (settings.logFile);
	settings.logFile = NULL;
	for (size_t i = 0; i < sizeof (modes) / sizeof (modes[0]); i++) {
		settings.uiMode = modes[i];
		ui_capture_msg (&settings, MSG_ERR, "boom\n", &cap);
		ck_assert_ptr_nonnull (strstr (cap.out, "boom"));
		ck_assert_str_eq (cap.err, "");
		ui_capture_free (&cap);
	}
	BarSettingsDestroy (&settings);
}
END_TEST

/* One-shot HTTP server on loopback in a forked child: answers the first
 * request with a 200 and body, then exits. Returns the port. */
static int ui_fake_http_server (const char *body, pid_t *pid)
{
	struct sockaddr_in addr;
	socklen_t addrLen = sizeof (addr);
	const int lfd = socket (AF_INET, SOCK_STREAM, 0);

	ck_assert (lfd >= 0);
	memset (&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	ck_assert_int_eq (bind (lfd, (struct sockaddr *) &addr, sizeof (addr)), 0);
	ck_assert_int_eq (listen (lfd, 1), 0);
	ck_assert_int_eq (getsockname (lfd, (struct sockaddr *) &addr, &addrLen), 0);

	fflush (NULL);
	*pid = fork ();
	ck_assert (*pid >= 0);
	if (*pid == 0) {
		char req[8192], resp[1024];
		size_t have = 0;
		const int cfd = (alarm (10), accept (lfd, NULL, NULL));

		/* Read the headers and the whole body before answering. */
		while (cfd >= 0 && have < sizeof (req) - 1) {
			const ssize_t n = read (cfd, req + have, sizeof (req) - 1 - have);
			if (n <= 0) {
				break;
			}
			have += (size_t) n;
			req[have] = '\0';
			const char *end = strstr (req, "\r\n\r\n");
			if (end != NULL) {
				const char *cl = strstr (req, "Content-Length: ");
				const size_t want = (size_t) (end + 4 - req) +
						(cl != NULL ? (size_t) atol (cl + 16) : 0);
				if (have >= want) {
					break;
				}
			}
		}
		snprintf (resp, sizeof (resp), "HTTP/1.1 200 OK\r\n"
				"Content-Type: text/plain\r\nContent-Length: %zu\r\n"
				"Connection: close\r\n\r\n%s", strlen (body), body);
		if (cfd >= 0 && write (cfd, resp, strlen (resp)) < 0) {
			_exit (1);
		}
		_exit (0);
	}
	close (lfd);
	return ntohs (addr.sin_port);
}

/* Run a real login against a closed loopback port, so BarUiPianoCall reports
 * a network error, and capture where that text goes. With proxyPort set,
 * run a plain-HTTP GET_SETTINGS through that proxy instead, so the request
 * succeeds and whatever the proxy answers is the Pandora response. */
static void ui_piano_call_run (BarUiMode_t mode, unsigned int debugMask,
		int proxyPort, UiCapture_t *cap)
{
	BarApp_t app;
	PianoRequestDataLogin_t loginData;
	PianoReturn_t pRet = PIANO_RET_OK;
	CURLcode wRet = CURLE_OK;

	log_set_debug_mask (debugMask);
	memset (&app, 0, sizeof (app));
	memset (&loginData, 0, sizeof (loginData));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = mode;
	free (app.settings.logFile);
	app.settings.logFile = NULL;
	free (app.settings.rpcHost);
	free (app.settings.rpcTlsPort);
	app.settings.rpcHost = strdup ("127.0.0.1");
	app.settings.rpcTlsPort = strdup ("1");
	app.settings.timeout = 1;
	app.settings.maxRetry = 1;
	app.settings.username = strdup ("user");
	app.settings.password = strdup ("pass");

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

	if (proxyPort > 0) {
		char proxy[64];

		snprintf (proxy, sizeof (proxy), "http://127.0.0.1:%d", proxyPort);
		app.settings.controlProxy = strdup (proxy);
		free (app.settings.rpcHost);
		app.settings.rpcHost = strdup ("pandora.test.invalid");
		app.ph.user.authToken = strdup ("token");
		app.ph.user.listenerId = strdup ("listener");
	}

	ui_capture_begin (cap);
	if (proxyPort > 0) {
		ck_assert (!BarUiPianoCall (&app, PIANO_REQUEST_GET_SETTINGS, NULL,
				&pRet, &wRet));
		ck_assert_int_eq (wRet, CURLE_OK);
		ck_assert_int_eq (pRet, PIANO_RET_P_CALL_NOT_ALLOWED);
	} else {
		ck_assert (!BarUiPianoCall (&app, PIANO_REQUEST_LOGIN, &loginData,
				&pRet, &wRet));
	}
	ui_capture_end (cap);
	log_set_debug_mask (0);

	curl_easy_cleanup (app.http);
	app.http = NULL;
	PianoDestroy (&app.ph);
	BarUiPianoHttpMutexDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}

/* The login/network failure that ends a web-only startup is reported with
 * MSG_NONE; without a log_file it must still reach stderr. */
START_TEST (test_ui_piano_call_error_reaches_stderr_in_web_only_without_log_file)
{
	UiCapture_t cap;

	ui_piano_call_run (BAR_UI_MODE_WEB, 0, 0, &cap);
	ck_assert_ptr_nonnull (strstr (cap.err, "Network error:"));
	ck_assert_str_eq (cap.out, "");
	ui_capture_free (&cap);
}
END_TEST

#ifdef HAVE_DEBUGLOG
/* With the debug CLI mirror on, the error is logged once, not twice. */
START_TEST (test_ui_piano_call_error_logged_once_with_debug_cli_in_web_only)
{
	UiCapture_t cap;
	const char *first;

	ui_piano_call_run (BAR_UI_MODE_WEB, DEBUG_CLI, 0, &cap);
	first = strstr (cap.err, "Network error:");
	ck_assert_ptr_nonnull (first);
	ck_assert_ptr_null (strstr (first + 1, "Network error:"));
	ui_capture_free (&cap);
}
END_TEST
#endif

/* A Pandora error response (HTTP fine, API says no) is reported the same
 * way: stderr in a log-file-less web-only daemon, stdout otherwise. */
START_TEST (test_ui_piano_call_api_error_reaches_stderr_in_web_only)
{
	UiCapture_t cap;
	pid_t server;
	int status;
	const int port = ui_fake_http_server (
			"{\"stat\":\"fail\",\"message\":\"no\",\"code\":1008}", &server);

	ui_piano_call_run (BAR_UI_MODE_WEB, 0, port, &cap);
	ck_assert_int_eq (waitpid (server, &status, 0), server);
	ck_assert (WIFEXITED (status) && WEXITSTATUS (status) == 0);
	ck_assert_ptr_nonnull (strstr (cap.err, "Error: "));
	ck_assert_ptr_nonnull (strstr (cap.err,
			PianoErrorToStr (PIANO_RET_P_CALL_NOT_ALLOWED)));
	ck_assert_str_eq (cap.out, "");
	ui_capture_free (&cap);
}
END_TEST

START_TEST (test_ui_piano_call_api_error_stays_on_stdout_in_both)
{
	UiCapture_t cap;
	pid_t server;
	int status;
	const int port = ui_fake_http_server (
			"{\"stat\":\"fail\",\"message\":\"no\",\"code\":1008}", &server);

	ui_piano_call_run (BAR_UI_MODE_BOTH, 0, port, &cap);
	ck_assert_int_eq (waitpid (server, &status, 0), server);
	ck_assert (WIFEXITED (status) && WEXITSTATUS (status) == 0);
	ck_assert_ptr_nonnull (strstr (cap.out, "Error: "));
	ck_assert_str_eq (cap.err, "");
	ui_capture_free (&cap);
}
END_TEST

START_TEST (test_ui_piano_call_error_stays_on_stdout_in_both)
{
	UiCapture_t cap;

	ui_piano_call_run (BAR_UI_MODE_BOTH, 0, 0, &cap);
	ck_assert_ptr_nonnull (strstr (cap.out, "Network error:"));
	ck_assert_str_eq (cap.err, "");
	ui_capture_free (&cap);
}
END_TEST

START_TEST (test_http_fetch_cb_accumulates_chunks)
{
	HttpFetchBuffer_t buf = {NULL, 0};
	char a[] = "abc";
	char b[] = "def";

	ck_assert_uint_eq (httpFetchCb (a, 1, 3, &buf), 3);
	ck_assert_uint_eq (httpFetchCb (b, 1, 3, &buf), 3);
	ck_assert_ptr_nonnull (buf.data);
	ck_assert_str_eq (buf.data, "abcdef");
	ck_assert_uint_eq (buf.pos, 6);
	free (buf.data);
}
END_TEST

/* A failed realloc must not leave a dangling buffer behind: the caller
 * (BarPianoHttpRequest) hands buffer.data to the response/cleanup path,
 * which would otherwise read and free freed memory. */
START_TEST (test_http_fetch_cb_realloc_failure_clears_buffer)
{
	HttpFetchBuffer_t buf = {NULL, 0};
	char a[] = "abc";
	char b[] = "x";

	ck_assert_uint_eq (httpFetchCb (a, 1, 3, &buf), 3);
	/* size*nmemb is far beyond what realloc can satisfy */
	ck_assert_uint_eq (httpFetchCb (b, SIZE_MAX / 2, 1, &buf), 0);
	ck_assert_ptr_null (buf.data);
	ck_assert_uint_eq (buf.pos, 0);

	/* buffer is reusable after the failure */
	ck_assert_uint_eq (httpFetchCb (a, 1, 3, &buf), 3);
	ck_assert_str_eq (buf.data, "abc");
	free (buf.data);
}
END_TEST

START_TEST (test_http_fetch_cb_initial_alloc_failure_returns_zero)
{
	HttpFetchBuffer_t buf = {NULL, 0};
	char b[] = "x";

	ck_assert_uint_eq (httpFetchCb (b, SIZE_MAX / 2, 1, &buf), 0);
	ck_assert_ptr_null (buf.data);
	ck_assert_uint_eq (buf.pos, 0);
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
	tcase_add_test (tc, test_progress_cb_aborts_on_shutdown);
	tcase_add_test (tc, test_ui_piano_call_not_sent_after_shutdown);
	tcase_add_test (tc, test_ui_piano_call_shutdown_stops_retries);
	tcase_add_test (tc, test_ui_piano_hold_suspend_resume);
	tcase_add_test (tc, test_ui_dispatch_piano_lock_policy);
	tcase_add_test (tc, test_ui_select_station_id_returns_copied_id);
	tcase_add_test (tc, test_ui_eventcmd_wait_releases_piano_hold);
	tcase_add_test (tc, test_ui_msg_err_goes_to_stderr_in_web_only_without_log_file);
	tcase_add_test (tc, test_ui_msg_err_in_web_only_uses_configured_format);
	tcase_add_test (tc, test_ui_msg_info_stays_on_stdout_in_web_only);
	tcase_add_test (tc, test_ui_msg_err_stays_on_stdout_with_log_file);
	tcase_add_test (tc, test_ui_msg_err_stays_on_stdout_in_cli_and_both);
	tcase_add_test (tc, test_ui_piano_call_error_reaches_stderr_in_web_only_without_log_file);
	tcase_add_test (tc, test_ui_piano_call_error_stays_on_stdout_in_both);
	tcase_add_test (tc, test_ui_piano_call_api_error_reaches_stderr_in_web_only);
	tcase_add_test (tc, test_ui_piano_call_api_error_stays_on_stdout_in_both);
#ifdef HAVE_DEBUGLOG
	tcase_add_test (tc, test_ui_piano_call_error_logged_once_with_debug_cli_in_web_only);
#endif
	tcase_add_test (tc, test_http_fetch_cb_accumulates_chunks);
	tcase_add_test (tc, test_http_fetch_cb_realloc_failure_clears_buffer);
	tcase_add_test (tc, test_http_fetch_cb_initial_alloc_failure_returns_zero);
	tcase_set_timeout (tc, 15);
	suite_add_tcase (s, tc);
	return s;
}
