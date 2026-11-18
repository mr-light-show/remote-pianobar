/*
 * Failure-path tests for the WebSocket command queue, the receive path, the
 * service-loop diagnostics and timer, and the Socket.IO station handlers
 * (unknown station, failed Pandora call, rejected request).
 */

#include <arpa/inet.h>
#include <check.h>
#include <errno.h>
#include <json-c/json.h>
#include <libwebsockets.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../../src/bar_constants.h"
#include "../../src/bar_state.h"
#include "../../src/interrupt.h"
#include "../../src/log.h"
#include "../../src/main.h"
#include "../../src/settings.h"
#include "../../src/ui.h"
#include "../../src/websocket/core/commands.h"
#include "../../src/websocket/core/websocket.h"
#include "../../src/websocket/protocol/socketio.h"
#include "test_ws_capture.h"

#define TEST_CMD "2[\"noop\"]" /* a queued request the worker ignores */
#define TEST_PING "2[\"ping\"]"

/* ------------------------------------------------------------------------
 * Fixtures
 * ------------------------------------------------------------------------ */

static void
web_app_init (BarApp_t *app, BarWsContext_t *ctx)
{
	memset (app, 0, sizeof (*app));
	memset (ctx, 0, sizeof (*ctx));
	BarSettingsInit (&app->settings);
	app->settings.uiMode = BAR_UI_MODE_WEB;
	app->settings.volumeMode = BAR_VOLUME_MODE_PLAYER;
	app->wsContext = ctx;
	ctx->outbox = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (ctx->outbox);
	BarStateInit (app);
	ck_assert_int_eq (pthread_mutex_init (&app->player.lock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.cond, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.audioCond, NULL), 0);
	app->player.settings = &app->settings;
}

static void
web_app_destroy (BarApp_t *app, BarWsContext_t *ctx)
{
	pthread_cond_destroy (&app->player.cond);
	pthread_cond_destroy (&app->player.audioCond);
	pthread_mutex_destroy (&app->player.lock);
	BarStateDestroy (app);
	BarWsOutboxFree (ctx->outbox);
	ctx->outbox = NULL;
	TestWsPendingReset ();
	BarSettingsDestroy (&app->settings);
}

static FILE *g_logFile;
static int g_logSaved = -1;

static void
log_capture_start (unsigned int mask)
{
	fflush (stderr);
	g_logFile = tmpfile ();
	ck_assert_ptr_nonnull (g_logFile);
	g_logSaved = dup (STDERR_FILENO);
	ck_assert_int_ge (g_logSaved, 0);
	ck_assert_int_ge (dup2 (fileno (g_logFile), STDERR_FILENO), 0);
	log_set_debug_mask (mask);
}

/* Everything logged since log_capture_start; valid until the next start */
static const char *
log_capture_stop (void)
{
	static char text[16384];
	fflush (stderr);
	log_set_debug_mask (0);
	dup2 (g_logSaved, STDERR_FILENO);
	close (g_logSaved);
	g_logSaved = -1;
	rewind (g_logFile);
	const size_t n = fread (text, 1, sizeof (text) - 1, g_logFile);
	text[n] = '\0';
	fclose (g_logFile);
	g_logFile = NULL;
	return text;
}

/* Whether the capture so far contains `needle`; the capture goes on */
static bool
log_capture_contains (const char *needle)
{
	char text[16384];
	fflush (stderr);
	const long end = ftell (g_logFile);
	rewind (g_logFile);
	const size_t n = fread (text, 1, sizeof (text) - 1, g_logFile);
	text[n] = '\0';
	fseek (g_logFile, end, SEEK_SET);
	return strstr (text, needle) != NULL;
}

static _Atomic long long g_nowMs;

static long long
fake_clock (void)
{
	return atomic_load (&g_nowMs);
}

/* ------------------------------------------------------------------------
 * Command queue
 * ------------------------------------------------------------------------ */

/* A request length that cannot be copied is refused and nothing is queued */
START_TEST (test_cmd_push_unallocatable_length_is_refused)
{
	BarWsCommandQueue_t *q = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (q);
	ck_assert_int_eq (BarWsCommandQueuePush (q, 1, "x", SIZE_MAX), BAR_WS_CMD_NOMEM);
	ck_assert_int_eq (BarWsCommandQueuePush (q, 1, "x", SIZE_MAX - 1), BAR_WS_CMD_NOMEM);
	BarWsCommandQueueStats_t st;
	BarWsCommandQueueStats (q, &st);
	ck_assert_uint_eq (st.items, 0);
	ck_assert_uint_eq (st.bytes, 0);
	/* the queue still works afterwards */
	ck_assert_int_eq (BarWsCommandQueuePush (q, 1, "ok", 2), BAR_WS_CMD_OK);
	BarWsCommandQueueFree (q);
}
END_TEST

/* ------------------------------------------------------------------------
 * Receive path
 * ------------------------------------------------------------------------ */

/* No application or no WebSocket context: the message is dropped quietly */
START_TEST (test_receive_without_app_or_context_is_dropped)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarWsHandleReceive (NULL, NULL, TEST_CMD, strlen (TEST_CMD), true);
	BarWsHandleReceive (&app, NULL, TEST_CMD, strlen (TEST_CMD), true);
	ck_assert_ptr_null (app.wsContext);
}
END_TEST

/* An empty fragment in the middle of a message adds nothing, and the whole
 * message is still delivered once the final part arrives */
START_TEST (test_receive_empty_fragment_keeps_partial_message)
{
	BarApp_t app;
	BarWsContext_t ctx;
	web_app_init (&app, &ctx);
	TestWsCaptureAttach (&app, 1, NULL);
	ctx.commands = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (ctx.commands);
	void *wsi = &test_ws_cap.sockets[0];

	BarWsHandleReceive (&app, wsi, "2[\"no", 5, false);
	BarWsHandleReceive (&app, wsi, "", 0, false);
	BarWsHandleReceive (&app, wsi, "op\"]", 4, true);

	BarWsConnId_t from = 0;
	char *bytes = NULL;
	size_t len = 0;
	ck_assert (BarWsCommandQueuePop (ctx.commands, &from, &bytes, &len));
	ck_assert_str_eq (bytes, TEST_CMD);
	ck_assert_uint_eq (len, strlen (TEST_CMD));
	ck_assert_uint_eq (from, test_ws_cap.ids[0]);
	free (bytes);
	BarWsCommandQueueStats_t st;
	BarWsCommandQueueStats (ctx.commands, &st);
	ck_assert_uint_eq (st.items, 0);

	BarWsCommandQueueFree (ctx.commands);
	ctx.commands = NULL;
	TestWsCaptureDetach (&app);
	web_app_destroy (&app, &ctx);
}
END_TEST

/* The client keepalive is answered at ingress: it is never queued for the
 * worker and never earns the busy error, even with the queue full */
START_TEST (test_receive_ping_is_not_queued_and_never_busy)
{
	BarApp_t app;
	BarWsContext_t ctx;
	web_app_init (&app, &ctx);
	ck_assert (BarL10nInit (&app.l10n, &app.settings));
	TestWsCaptureAttach (&app, 1, NULL);
	ctx.commands = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (ctx.commands);
	void *wsi = &test_ws_cap.sockets[0];

	/* idle queue: nothing queued */
	BarWsHandleReceive (&app, wsi, TEST_PING, strlen (TEST_PING), true);
	/* with a payload and in parts */
	const char *withArg = "2[\"ping\",{}]";
	BarWsHandleReceive (&app, wsi, withArg, strlen (withArg), true);
	BarWsHandleReceive (&app, wsi, "2[\"pi", 5, false);
	BarWsHandleReceive (&app, wsi, "ng\"]", 4, true);
	BarWsCommandQueueStats_t st;
	BarWsCommandQueueStats (ctx.commands, &st);
	ck_assert_uint_eq (st.items, 0);

	/* full queue: a ping is ignored, a real request is still refused */
	for (int i = 0; i < WS_CMD_MAX_ITEMS; i++) {
		BarWsHandleReceive (&app, wsi, TEST_CMD, strlen (TEST_CMD), true);
	}
	BarWsHandleReceive (&app, wsi, TEST_PING, strlen (TEST_PING), true);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 0);
	BarWsCommandQueueStats (ctx.commands, &st);
	ck_assert_uint_eq (st.items, WS_CMD_MAX_ITEMS);
	BarWsHandleReceive (&app, wsi, TEST_CMD, strlen (TEST_CMD), true);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 1);
	ck_assert (strstr (test_ws_cap.frames[0], "busy") != NULL);

	/* a name that only starts like ping is a request */
	BarWsCommandQueuePop (ctx.commands, NULL, NULL, NULL);
	const char *pingish = "2[\"pinger\"]";
	BarWsHandleReceive (&app, wsi, pingish, strlen (pingish), true);
	BarWsCommandQueueStats (ctx.commands, &st);
	ck_assert_uint_eq (st.items, WS_CMD_MAX_ITEMS);

	/* shorter than the keepalive prefix: a request */
	BarWsCommandQueuePop (ctx.commands, NULL, NULL, NULL);
	BarWsHandleReceive (&app, wsi, "2[\"a\"]", 6, true);
	BarWsCommandQueueStats (ctx.commands, &st);
	ck_assert_uint_eq (st.items, WS_CMD_MAX_ITEMS);

	BarWsCommandQueueFree (ctx.commands);
	ctx.commands = NULL;
	TestWsCaptureDetach (&app);
	BarL10nDestroy (&app.l10n);
	web_app_destroy (&app, &ctx);
}
END_TEST

/* The refusal episode ends only when both the item count and the queued
 * bytes have drained to half. A few large requests keep it going. */
START_TEST (test_busy_episode_waits_for_queued_bytes_to_drain)
{
	BarApp_t app;
	BarWsContext_t ctx;
	web_app_init (&app, &ctx);
	TestWsCaptureAttach (&app, 1, NULL);
	ctx.commands = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (ctx.commands);
	void *wsi = &test_ws_cap.sockets[0];
	char *big = malloc (WS_CMD_MAX_REQUEST_BYTES);
	ck_assert_ptr_nonnull (big);
	memset (big, 'a', WS_CMD_MAX_REQUEST_BYTES);

	const int fits = WS_CMD_MAX_BYTES / WS_CMD_MAX_REQUEST_BYTES;
	ck_assert_int_le (fits, WS_CMD_MAX_ITEMS / 2);
	for (int i = 0; i < fits; i++) {
		BarWsHandleReceive (&app, wsi, big, WS_CMD_MAX_REQUEST_BYTES, true);
	}
	/* the byte cap refuses the next one: the episode starts */
	log_capture_start (0);
	BarWsHandleReceive (&app, wsi, big, WS_CMD_MAX_REQUEST_BYTES, true);
	const char *log = log_capture_stop ();
	ck_assert_msg (strstr (log, "Command queue full") != NULL, "log was: %s", log);
	ck_assert (ctx.cmdRefusing);

	/* one slot free: the request is accepted, but the queue is still above
	 * half its bytes (with few items), so the episode goes on */
	ck_assert (BarWsCommandQueuePop (ctx.commands, NULL, NULL, NULL));
	BarWsHandleReceive (&app, wsi, big, WS_CMD_MAX_REQUEST_BYTES, true);
	ck_assert (ctx.cmdRefusing);

	/* drained to under half the bytes: the next accepted request ends it */
	for (int i = 0; i < fits / 2 + 1; i++) {
		ck_assert (BarWsCommandQueuePop (ctx.commands, NULL, NULL, NULL));
	}
	BarWsHandleReceive (&app, wsi, TEST_CMD, strlen (TEST_CMD), true);
	ck_assert (!ctx.cmdRefusing);

	free (big);
	BarWsCommandQueueFree (ctx.commands);
	ctx.commands = NULL;
	TestWsCaptureDetach (&app);
	web_app_destroy (&app, &ctx);
}
END_TEST

/* ------------------------------------------------------------------------
 * Worker lifecycle guards
 * ------------------------------------------------------------------------ */

START_TEST (test_worker_start_and_stop_reject_missing_context)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	ck_assert (!BarWebsocketWorkerStart (NULL));
	ck_assert (!BarWebsocketWorkerStart (&app));
	BarWebsocketWorkerStop (NULL);
	BarWebsocketWorkerStop (&app);
	ck_assert_ptr_null (app.wsContext);
}
END_TEST

START_TEST (test_worker_second_start_is_refused_and_stop_is_idempotent)
{
	BarApp_t app;
	BarWsContext_t ctx;
	web_app_init (&app, &ctx);
	BarInterruptSetTarget (NULL);
	BarUiPianoHttpMutexInit (&app);

	ck_assert (BarWebsocketWorkerStart (&app));
	ck_assert (ctx.workerStarted);
	BarWsCommandQueue_t *first = ctx.commands;
	ck_assert (!BarWebsocketWorkerStart (&app));
	ck_assert_ptr_eq (ctx.commands, first); /* nothing was replaced */
	ck_assert (ctx.workerStarted);

	BarWebsocketWorkerStop (&app);
	ck_assert (!ctx.workerStarted);
	BarWebsocketWorkerStop (&app); /* already stopped: no-op */
	ck_assert (!ctx.workerStarted);

	BarWsCommandQueueFree (ctx.commands);
	ctx.commands = NULL;
	BarUiPianoHttpMutexDestroy (&app);
	web_app_destroy (&app, &ctx);
}
END_TEST

/* ------------------------------------------------------------------------
 * Heartbeat
 * ------------------------------------------------------------------------ */

/* Queue one targeted frame of `size` bytes for client `id` */
static void
queue_frame_for (BarApp_t *app, BarWsConnId_t id, size_t size)
{
	char *text = malloc (size + 1);
	ck_assert_ptr_nonnull (text);
	memset (text, 'x', size);
	text[size] = '\0';
	ck_assert (BarWebsocketSend (app, id, BAR_WS_KEY_NONE, text));
}

/* Run the heartbeat once and return its status line */
static const char *
heartbeat_line (BarWsContext_t *ctx, unsigned int mask)
{
	atomic_store (&g_nowMs, 1000);
	BarWebsocketSetTestClock (fake_clock);
	ctx->heartbeatNextMs = 0;
	memset (&ctx->lastHeartbeat, 0, sizeof (ctx->lastHeartbeat));
	log_capture_start (mask);
	(void) BarWsServiceDiagnostics (ctx); /* schedules the first heartbeat */
	atomic_store (&g_nowMs, 1000 + 600000);
	(void) BarWsServiceDiagnostics (ctx);
	const char *log = log_capture_stop ();
	BarWebsocketSetTestClock (NULL);
	return log;
}

/* The status line names the client with the most pending bytes, whichever
 * slot it is in, and ignores clients with nothing pending */
START_TEST (test_heartbeat_reports_client_with_most_pending_bytes)
{
	BarApp_t app;
	BarWsContext_t ctx;
	web_app_init (&app, &ctx);
	TestWsCaptureAttach (&app, 2, NULL);
	ctx.commands = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (ctx.commands);
	const BarWsConnId_t first = test_ws_cap.ids[0];
	const BarWsConnId_t second = test_ws_cap.ids[1];

	/* only the second client has something pending */
	queue_frame_for (&app, second, 20);
	BarWsServiceOutbox (&ctx);
	char want[64];
	const char *log = heartbeat_line (&ctx, DEBUG_WEBSOCKET);
#ifdef HAVE_DEBUGLOG
	snprintf (want, sizeof (want), "worst pending 1 msgs/20 bytes (client %llu)",
			(unsigned long long) second);
	ck_assert_msg (strstr (log, want) != NULL, "log was: %s", log);
#else
	(void) log;
#endif

	/* both pending, the first holds more: it stays the worst */
	queue_frame_for (&app, first, 50);
	BarWsServiceOutbox (&ctx);
	log = heartbeat_line (&ctx, DEBUG_WEBSOCKET);
#ifdef HAVE_DEBUGLOG
	snprintf (want, sizeof (want), "worst pending 1 msgs/50 bytes (client %llu)",
			(unsigned long long) first);
	ck_assert_msg (strstr (log, want) != NULL, "log was: %s", log);
#else
	(void) log;
#endif

	/* both pending, the second holds more: it takes over */
	TestWsCaptureFlush ();
	queue_frame_for (&app, first, 30);
	queue_frame_for (&app, second, 70);
	BarWsServiceOutbox (&ctx);
	log = heartbeat_line (&ctx, DEBUG_WEBSOCKET);
#ifdef HAVE_DEBUGLOG
	snprintf (want, sizeof (want), "worst pending 1 msgs/70 bytes (client %llu)",
			(unsigned long long) second);
	ck_assert_msg (strstr (log, want) != NULL, "log was: %s", log);
#else
	(void) log;
#endif

	TestWsCaptureFlush ();
	BarWsCommandQueueFree (ctx.commands);
	ctx.commands = NULL;
	TestWsCaptureDetach (&app);
	web_app_destroy (&app, &ctx);
}
END_TEST

/* An idle heartbeat that repeats the previous numbers is suppressed: after
 * activity the first idle status is reported once, then silence */
START_TEST (test_heartbeat_idle_and_unchanged_is_not_repeated)
{
	BarApp_t app;
	BarWsContext_t ctx;
	web_app_init (&app, &ctx);
	TestWsCaptureAttach (&app, 1, NULL);
	ctx.commands = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (ctx.commands);

	queue_frame_for (&app, test_ws_cap.ids[0], 20);
	BarWsServiceOutbox (&ctx);
	atomic_store (&g_nowMs, 1000);
	BarWebsocketSetTestClock (fake_clock);
	log_capture_start (DEBUG_WEBSOCKET);
	(void) BarWsServiceDiagnostics (&ctx);
	atomic_store (&g_nowMs, 1000 + 40000);
	(void) BarWsServiceDiagnostics (&ctx); /* busy: logged */
	TestWsCaptureFlush ();
	atomic_store (&g_nowMs, 1000 + 80000);
	(void) BarWsServiceDiagnostics (&ctx); /* idle but changed: logged */
	atomic_store (&g_nowMs, 1000 + 120000);
	(void) BarWsServiceDiagnostics (&ctx); /* idle, same numbers: silent */
	atomic_store (&g_nowMs, 1000 + 160000);
	(void) BarWsServiceDiagnostics (&ctx); /* still silent */
	const char *log = log_capture_stop ();
	BarWebsocketSetTestClock (NULL);
#ifdef HAVE_DEBUGLOG
	size_t lines = 0;
	for (const char *p = strstr (log, "Status: "); p; p = strstr (p + 1, "Status: ")) {
		lines++;
	}
	ck_assert_uint_eq (lines, 2);
	ck_assert (strstr (log, "worker idle") != NULL);
#else
	(void) log;
#endif

	BarWsCommandQueueFree (ctx.commands);
	ctx.commands = NULL;
	TestWsCaptureDetach (&app);
	web_app_destroy (&app, &ctx);
}
END_TEST

/* ------------------------------------------------------------------------
 * Service loop timer (real service thread)
 * ------------------------------------------------------------------------ */

static int
free_loopback_port (void)
{
	int fd = socket (AF_INET, SOCK_STREAM, 0);
	ck_assert_int_ge (fd, 0);
	struct sockaddr_in addr;
	memset (&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	ck_assert_int_eq (bind (fd, (struct sockaddr *) &addr, sizeof (addr)), 0);
	socklen_t len = sizeof (addr);
	ck_assert_int_eq (getsockname (fd, (struct sockaddr *) &addr, &len), 0);
	close (fd);
	return ntohs (addr.sin_port);
}

static void
server_app_init (BarApp_t *app, BarVolumeModeType mode)
{
	memset (app, 0, sizeof (*app));
	BarSettingsInit (&app->settings);
	app->settings.uiMode = BAR_UI_MODE_WEB;
	app->settings.volumeMode = mode;
	app->settings.websocketHost = strdup ("127.0.0.1");
	app->settings.websocketPort = free_loopback_port ();
	BarStateInit (app);
	ck_assert_int_eq (pthread_mutex_init (&app->player.lock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.cond, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&app->player.audioCond, NULL), 0);
	app->player.settings = &app->settings;
	BarInterruptSetTarget (NULL);
	BarUiPianoHttpMutexInit (app);
	ck_assert (BarWebsocketInit (app));
}

static void
server_app_destroy (BarApp_t *app)
{
	BarWebsocketDestroy (app);
	BarUiPianoHttpMutexDestroy (app);
	pthread_cond_destroy (&app->player.audioCond);
	pthread_cond_destroy (&app->player.cond);
	pthread_mutex_destroy (&app->player.lock);
	BarStateDestroy (app);
	BarSettingsDestroy (&app->settings);
}

static bool
volume_broadcast_pending (BarWsContext_t *ctx)
{
	pthread_mutex_lock (&ctx->volumeBroadcastMutex);
	const bool pending = ctx->delayedVolumeBroadcast.pending;
	pthread_mutex_unlock (&ctx->volumeBroadcastMutex);
	return pending;
}

/* Wait (bounded) until the debounced volume broadcast has fired */
static bool
wait_volume_broadcast_fired (BarWsContext_t *ctx)
{
	for (int i = 0; i < 300; i++) {
		if (!volume_broadcast_pending (ctx)) {
			return true;
		}
		usleep (10000);
	}
	return false;
}

/* In system volume mode the timer is capped at the one-second poll, and a
 * debounced volume broadcast scheduled further out or sooner still fires on
 * time without any other traffic */
START_TEST (test_system_volume_mode_timer_still_fires_debounced_volume)
{
	BarApp_t app;
	server_app_init (&app, BAR_VOLUME_MODE_SYSTEM);
	BarWsContext_t *ctx = app.wsContext;

	/* idle: the loop arms the one-second poll; wake it once */
	lws_cancel_service (ctx->context);

	/* due well inside the poll interval */
	BarWsScheduleVolumeBroadcast (ctx, 100);
	ck_assert (wait_volume_broadcast_fired (ctx));

	/* due after the poll interval: the poll wakes the loop meanwhile and
	 * the broadcast still fires once it is due (shortened by rescheduling) */
	BarWsScheduleVolumeBroadcast (ctx, 5000);
	lws_cancel_service (ctx->context);
	usleep (50000);
	ck_assert (volume_broadcast_pending (ctx));
	BarWsScheduleVolumeBroadcast (ctx, 50);
	ck_assert (wait_volume_broadcast_fired (ctx));

	server_app_destroy (&app);
}
END_TEST

/* With diagnostics on, the otherwise idle service loop arms a timer for the
 * long-running-command check and reports a stuck command when it is due */
START_TEST (test_service_loop_reports_stuck_command_when_due)
{
	BarApp_t app;
	atomic_store (&g_nowMs, 5000);
	BarWebsocketSetTestClock (fake_clock);
	server_app_init (&app, BAR_VOLUME_MODE_PLAYER);
	BarWsContext_t *ctx = app.wsContext;

	log_capture_start (DEBUG_WEBSOCKET);
	BarWsCommandQueueBegin (ctx->commands, "2[\"action\",\"stuck.cmd\"]", 5000);
	lws_cancel_service (ctx->context);  /* the loop notices the running command */
	usleep (100000);
	ck_assert (!log_capture_contains ("Command running for"));
	atomic_store (&g_nowMs, 5000 + 25000);
	lws_cancel_service (ctx->context);  /* past the stall threshold */
	for (int i = 0; i < 300 && !log_capture_contains ("Command running for"); i++) {
		usleep (10000);
	}
	const bool reported = log_capture_contains ("stuck.cmd");
	BarWsCommandQueueEnd (ctx->commands);
	server_app_destroy (&app);
	(void) log_capture_stop ();
	BarWebsocketSetTestClock (NULL);
#ifdef HAVE_DEBUGLOG
	ck_assert (reported);
#else
	(void) reported;
#endif
}
END_TEST

Suite *
ws_failure_paths_suite (void)
{
	Suite *s = suite_create ("WsFailurePaths");
	TCase *tc = tcase_create ("Core");
	tcase_set_timeout (tc, 30);
	tcase_add_test (tc, test_cmd_push_unallocatable_length_is_refused);
	tcase_add_test (tc, test_receive_without_app_or_context_is_dropped);
	tcase_add_test (tc, test_receive_empty_fragment_keeps_partial_message);
	tcase_add_test (tc, test_receive_ping_is_not_queued_and_never_busy);
	tcase_add_test (tc, test_busy_episode_waits_for_queued_bytes_to_drain);
	tcase_add_test (tc, test_worker_start_and_stop_reject_missing_context);
	tcase_add_test (tc, test_worker_second_start_is_refused_and_stop_is_idempotent);
	tcase_add_test (tc, test_heartbeat_reports_client_with_most_pending_bytes);
	tcase_add_test (tc, test_heartbeat_idle_and_unchanged_is_not_repeated);
	tcase_add_test (tc, test_system_volume_mode_timer_still_fires_debounced_volume);
	tcase_add_test (tc, test_service_loop_reports_stuck_command_when_due);
	suite_add_tcase (s, tc);
	return s;
}
