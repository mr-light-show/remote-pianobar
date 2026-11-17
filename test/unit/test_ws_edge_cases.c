/*
 * Edge-case and error-path tests for the WebSocket core, Socket.IO action
 * routing and the web-targeted UI action callbacks: full connection table,
 * missing context/arguments, failed Pandora calls, and a real client going
 * through connect / receive / disconnect against the live server.
 */

#include <arpa/inet.h>
#include <check.h>
#include <json-c/json.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../../src/bar_state.h"
#include "../../src/main.h"
#include "../../src/settings.h"
#include "../../src/ui.h"
#include "../../src/ui_dispatch.h"
#include "../../src/ui_act.h"
#include "../../src/websocket/core/outbox.h"
#include "../../src/websocket/core/websocket.h"
#include "../../src/websocket/protocol/socketio.h"
#include "../../src/websocket_bridge.h"
#include "test_ws_capture.h"

/* ---- connection table and delivery argument handling ---- */

START_TEST (test_ws_connection_add_returns_zero_when_table_full)
{
	BarWsContext_t ctx;
	BarWsConnection_t conns[2];
	char sockets[3];

	memset (&ctx, 0, sizeof (ctx));
	memset (conns, 0, sizeof (conns));
	ctx.connections = conns;
	ctx.maxConnections = 2;

	BarWsConnId_t a = BarWsConnectionAdd (&ctx, &sockets[0], "socketio");
	BarWsConnId_t b = BarWsConnectionAdd (&ctx, &sockets[1], "socketio");
	ck_assert_uint_ne (a, BAR_WS_DEST_BROADCAST);
	ck_assert_uint_ne (b, BAR_WS_DEST_BROADCAST);
	ck_assert_uint_ne (a, b);

	/* Full: no ID is handed out and the table is unchanged */
	ck_assert_uint_eq (BarWsConnectionAdd (&ctx, &sockets[2], "socketio"),
			BAR_WS_DEST_BROADCAST);
	ck_assert_uint_eq (ctx.numConnections, 2);

	/* A freed slot is reused with a fresh ID */
	BarWsConnectionRemove (&ctx, &sockets[0]);
	ck_assert_uint_eq (ctx.numConnections, 1);
	BarWsConnId_t c = BarWsConnectionAdd (&ctx, &sockets[2], "socketio");
	ck_assert_uint_ne (c, BAR_WS_DEST_BROADCAST);
	ck_assert_uint_ne (c, a);
	ck_assert_uint_ne (c, b);
	ck_assert_uint_eq (ctx.numConnections, 2);
}
END_TEST

START_TEST (test_ws_handle_receive_ignores_missing_app_or_context)
{
	BarApp_t app;
	char sock;
	memset (&app, 0, sizeof (app));

	/* None of these may crash or dispatch anything */
	BarWsHandleReceive (NULL, &sock, "2[\"x\"]", 6);
	BarWsHandleReceive (&app, &sock, "2[\"x\"]", 6);
	ck_assert_ptr_null (app.wsContext);
}
END_TEST

START_TEST (test_ws_send_rejects_missing_app_context_or_message)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));

	/* Ordered (BAR_WS_KEY_NONE) and targeted sends need app, context and data;
	 * the message is consumed in every case */
	ck_assert (!BarWebsocketSend (NULL, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
			strdup ("2[\"a\"]")));
	ck_assert (!BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
			strdup ("2[\"a\"]")));
	ck_assert (!BarWebsocketSend (&app, 7, BAR_WS_KEY_NONE, strdup ("2[\"a\"]")));

	BarWsContext_t ctx;
	memset (&ctx, 0, sizeof (ctx));
	app.wsContext = &ctx;
	ck_assert (!BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, NULL));
	ck_assert (!BarWebsocketSend (&app, 7, BAR_WS_KEY_NONE, NULL));
	app.wsContext = NULL;
}
END_TEST

static size_t g_deliverWrites;

static int
count_write (void *wsi, unsigned char *buf, size_t len)
{
	(void) wsi;
	(void) buf;
	g_deliverWrites++;
	return (int) len;
}

static void
ignore_writable (void *wsi)
{
	(void) wsi;
}

/* Delivery guards: the service pass and the writable handler ignore a
 * missing context/outbox/connection, and an event reaches the client only
 * through the writable handler's single write. */
START_TEST (test_ws_deliver_ignores_null_ctx_message_or_empty)
{
	BarApp_t app;
	BarWsContext_t ctx;
	BarWsConnection_t conns[1];
	char sock;
	char other;

	memset (&app, 0, sizeof (app));
	memset (&ctx, 0, sizeof (ctx));
	memset (conns, 0, sizeof (conns));
	ctx.connections = conns;
	ctx.maxConnections = 1;
	ctx.writeFrame = count_write;
	ctx.requestWritable = ignore_writable;
	ctx.outbox = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (ctx.outbox);
	app.wsContext = &ctx;
	BarWsConnId_t id = BarWsConnectionAdd (&ctx, &sock, "socketio");
	ck_assert_uint_ne (id, 0);

	g_deliverWrites = 0;
	BarWsServiceOutbox (NULL);
	ck_assert_int_eq (BarWsHandleWritable (&ctx, &other), 0); /* untracked */
	ck_assert_int_eq (BarWsHandleWritable (&ctx, &sock), 0); /* nothing queued */
	ck_assert (!BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, NULL));
	BarWsServiceOutbox (&ctx);
	ck_assert_int_eq (BarWsHandleWritable (&ctx, &sock), 0);
	ck_assert_uint_eq (g_deliverWrites, 0);

	ck_assert (BarWebsocketSend (&app, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
			strdup ("2[\"a\"]")));
	BarWsServiceOutbox (&ctx);
	ck_assert_int_eq (BarWsHandleWritable (&ctx, &sock), 0);
	ck_assert_uint_eq (g_deliverWrites, 1);

	BarWsConnectionRemove (&ctx, &sock);
	BarWsOutboxFree (ctx.outbox);
}
END_TEST

/* ---- live server: connect, receive, disconnect ---- */

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

static int
connect_with_retry (int port)
{
	for (int attempt = 0; attempt < 100; attempt++) {
		int fd = socket (AF_INET, SOCK_STREAM, 0);
		ck_assert_int_ge (fd, 0);
		struct sockaddr_in addr;
		memset (&addr, 0, sizeof (addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons (port);
		addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
		if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) == 0) {
			return fd;
		}
		close (fd);
		usleep (50000);
	}
	return -1;
}

/* Read until `n` bytes arrived or the peer is silent for 5 s */
static ssize_t
read_some (int fd, char *buf, size_t cap)
{
	struct pollfd p = { .fd = fd, .events = POLLIN };
	if (poll (&p, 1, 5000) <= 0) {
		return -1;
	}
	return read (fd, buf, cap);
}

static size_t
live_connections (BarWsContext_t *ctx)
{
	/* Lock-free: the service thread updates the atomic counter */
	return atomic_load (&ctx->numConnections);
}

static bool
wait_for_connections (BarWsContext_t *ctx, size_t want)
{
	for (int i = 0; i < 2000; i++) {
		if (live_connections (ctx) == want) {
			return true;
		}
		usleep (5000);
	}
	return false;
}

START_TEST (test_ws_live_client_is_tracked_greeted_and_removed)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.websocketHost = strdup ("127.0.0.1");
	app.settings.websocketPort = free_loopback_port ();
	BarStateInit (&app);
	ck_assert (BarWebsocketInit (&app));
	BarWsContext_t *ctx = app.wsContext;

	int fd = connect_with_retry (app.settings.websocketPort);
	ck_assert_int_ge (fd, 0);
	const char *req =
		"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n"
		"Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
		"Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: socketio\r\n\r\n";
	ck_assert_int_eq (write (fd, req, strlen (req)), (ssize_t) strlen (req));

	/* Handshake accepted, then the new client alone gets the current state
	 * through the real lws write path */
	char buf[8192];
	size_t have = 0;
	buf[0] = '\0';
	while (strstr (buf, "process") == NULL && have < sizeof (buf) - 1) {
		ssize_t n = read_some (fd, buf + have, sizeof (buf) - 1 - have);
		ck_assert_int_gt (n, 0);
		have += (size_t) n;
		buf[have] = '\0';
	}
	ck_assert_ptr_nonnull (strstr (buf, "101"));
	ck_assert (wait_for_connections (ctx, 1));
	ck_assert_uint_ne (ctx->connections[0].id, BAR_WS_DEST_BROADCAST);

	/* A masked text frame is dispatched to the Socket.IO handler */
	const unsigned char mask[4] = { 1, 2, 3, 4 };
	const char payload[] = "2[\"no.such.event\"]";
	unsigned char frame[64];
	size_t plen = sizeof (payload) - 1;
	frame[0] = 0x81;
	frame[1] = 0x80 | (unsigned char) plen;
	memcpy (&frame[2], mask, 4);
	for (size_t i = 0; i < plen; i++) {
		frame[6 + i] = (unsigned char) payload[i] ^ mask[i % 4];
	}
	ck_assert_int_eq (write (fd, frame, 6 + plen), (ssize_t) (6 + plen));

	/* Dropping the socket removes the client from the table */
	close (fd);
	ck_assert (wait_for_connections (ctx, 0));
	ck_assert_ptr_null (ctx->connections[0].wsi);

	atomic_store_explicit (&app.doQuit, true, memory_order_relaxed);
	BarWebsocketDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static void
send_ws_upgrade (int fd)
{
	const char *req =
		"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n"
		"Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
		"Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: socketio\r\n\r\n";
	ck_assert_int_eq (write (fd, req, strlen (req)), (ssize_t) strlen (req));
}

/* Read until EOF/reset or `needle` appears; true if the peer closed first.
 * A silent peer (socket left open) fails the test. */
static bool
read_until_needle_or_eof (int fd, const char *needle, char *buf, size_t cap)
{
	size_t have = 0;
	buf[0] = '\0';
	while (strstr (buf, needle) == NULL && have < cap - 1) {
		struct pollfd p = { .fd = fd, .events = POLLIN };
		ck_assert_msg (poll (&p, 1, 3000) > 0, "peer neither closed nor sent");
		ssize_t n = read (fd, buf + have, cap - 1 - have);
		if (n <= 0) {
			return true;
		}
		have += (size_t) n;
		buf[have] = '\0';
	}
	return false;
}

START_TEST (test_ws_client_refused_when_table_full_then_reconnects)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.websocketHost = strdup ("127.0.0.1");
	app.settings.websocketPort = free_loopback_port ();
	BarStateInit (&app);
	ck_assert (BarWebsocketInit (&app));
	BarWsContext_t *ctx = app.wsContext;

	/* Occupy every slot with a placeholder socket */
	char fake[64];
	for (size_t i = 0; i < ctx->maxConnections; i++) {
		ck_assert_uint_ne (BarWsConnectionAdd (ctx, &fake[i], "socketio"),
				BAR_WS_DEST_BROADCAST);
	}
	ck_assert_uint_eq (live_connections (ctx), ctx->maxConnections);

	/* The extra client is closed instead of lingering untracked */
	char buf[8192];
	int fd = connect_with_retry (app.settings.websocketPort);
	ck_assert_int_ge (fd, 0);
	send_ws_upgrade (fd);
	ck_assert (read_until_needle_or_eof (fd, "process", buf, sizeof (buf)));
	ck_assert_ptr_null (strstr (buf, "process"));
	close (fd);
	ck_assert_uint_eq (live_connections (ctx), ctx->maxConnections);

	/* Once a slot frees up, a reconnect is tracked and greeted */
	BarWsConnectionRemove (ctx, &fake[0]);
	fd = connect_with_retry (app.settings.websocketPort);
	ck_assert_int_ge (fd, 0);
	send_ws_upgrade (fd);
	ck_assert (!read_until_needle_or_eof (fd, "process", buf, sizeof (buf)));
	ck_assert (wait_for_connections (ctx, ctx->maxConnections));
	close (fd);
	ck_assert (wait_for_connections (ctx, ctx->maxConnections - 1));

	atomic_store_explicit (&app.doQuit, true, memory_order_relaxed);
	BarWebsocketDestroy (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

/* ---- Socket.IO action routing ---- */

START_TEST (test_socketio_action_with_null_name_is_ignored)
{
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	memset (&song, 0, sizeof (song));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit (&app);
	TestWsCaptureAttach (&app, 1, NULL);

	/* JSON null for "action" / "command" yields no action name: nothing is
	 * dispatched and nothing is sent back */
	BarSocketIoHandleMessage (&app, "2[\"action\",{\"action\":null}]",
			test_ws_cap.ids[0]);
	BarSocketIoHandleMessage (&app, "2[\"action\",{\"command\":null}]",
			test_ws_cap.ids[0]);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 0);

	TestWsCaptureDetach (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

static int g_rateCalls;

static bool
rate_song_ok (BarApp_t *app, const PianoRequestType_t type, void *data,
		PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	(void) data;
	if (type == PIANO_REQUEST_RATE_SONG) {
		g_rateCalls++;
		*pRet = PIANO_RET_OK;
		*wRet = CURLE_OK;
		return true;
	}
	return false;
}

START_TEST (test_socketio_upcoming_without_requester_runs_terminal_callback)
{
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	memset (&song, 0, sizeof (song));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit (&app);
	station.id = "S1";
	station.name = "Station";
	station.isCreator = true;
	song.stationId = station.id;
	song.title = "Only";
	app.ph.stations = &station;
	BarStateSetCurrentStation (&app, &station);
	BarStateSetPlaylist (&app, &song);
	TestWsCaptureAttach (&app, 2, NULL);

	BarUiPianoHttpMutexInit (&app);
	app.ph.user.listenerId = "listener";

	/* Direct caller (no requester): the terminal callback runs, which
	 * prints locally and sends no web-targeted reply to any client */
	BarSocketIoHandleAction (&app, "query.upcoming", NULL, BAR_WS_DEST_BROADCAST);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 0);

	/* With a requester the same action answers that client only */
	BarSocketIoHandleAction (&app, "query.upcoming", NULL, test_ws_cap.ids[0]);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 1);
	ck_assert_uint_eq (TestWsCaptureCount (0, "query.upcoming"), 1);
	ck_assert_uint_eq (TestWsCaptureCount (1, "query.upcoming"), 0);

	/* Other actions are not query actions: a requester does not turn them
	 * into a targeted upcoming/explain reply */
	TestWsCaptureClear ();
	g_rateCalls = 0;
	BarUiPianoCallSetTestHook (rate_song_ok);
	BarSocketIoHandleAction (&app, "song.love", NULL, test_ws_cap.ids[0]);
	BarUiPianoCallClearTestHook ();
	TestWsCaptureFlush ();
	ck_assert_int_eq (g_rateCalls, 1);
	ck_assert_uint_eq (TestWsCaptureCount (0, "query.upcoming"), 0);
	ck_assert_uint_eq (TestWsCaptureCount (0, "song.explanation"), 0);

	app.ph.user.listenerId = NULL;
	BarUiPianoHttpMutexDestroy (&app);
	TestWsCaptureDetach (&app);
	pthread_cond_destroy (&app.player.cond);
	pthread_mutex_destroy (&app.player.lock);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

/* Object-form action messages carry the name under "action" or "command" */
START_TEST (test_socketio_object_action_and_command_forms_dispatch)
{
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	memset (&song, 0, sizeof (song));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	BarStateInit (&app);
	station.id = "S1";
	station.name = "Station";
	station.isCreator = true;
	song.stationId = station.id;
	song.title = "Only";
	app.ph.stations = &station;
	BarStateSetCurrentStation (&app, &station);
	BarStateSetPlaylist (&app, &song);
	BarUiPianoHttpMutexInit (&app);
	app.ph.user.listenerId = "listener";
	TestWsCaptureAttach (&app, 2, NULL);

	BarSocketIoHandleMessage (&app,
			"2[\"action\",{\"action\":\"query.upcoming\"}]", test_ws_cap.ids[1]);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (TestWsCaptureCount (1, "query.upcoming"), 1);
	BarSocketIoHandleMessage (&app,
			"2[\"action\",{\"command\":\"query.upcoming\"}]", test_ws_cap.ids[1]);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (TestWsCaptureCount (1, "query.upcoming"), 2);
	ck_assert_uint_eq (TestWsCaptureCount (0, "query.upcoming"), 0);

	app.ph.user.listenerId = NULL;
	BarUiPianoHttpMutexDestroy (&app);
	TestWsCaptureDetach (&app);
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

/* ---- UI actions ---- */

static int g_explainCalls;

static bool
explain_call_fails (BarApp_t *app, const PianoRequestType_t type, void *data,
		PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	(void) data;
	if (type == PIANO_REQUEST_EXPLAIN) {
		g_explainCalls++;
	}
	*pRet = PIANO_RET_ERR;
	*wRet = CURLE_OK;
	return type != PIANO_REQUEST_EXPLAIN;
}

START_TEST (test_ui_act_explain_terminal_failed_request_prints_nothing_more)
{
	BarApp_t app;
	PianoStation_t station;
	PianoSong_t song;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	memset (&song, 0, sizeof (song));
	BarSettingsInit (&app.settings);
	song.title = "Explain Me";

	g_explainCalls = 0;
	BarUiPianoCallSetTestHook (explain_call_fails);
	BarUiActExplain (&app, &station, &song, BAR_DC_GLOBAL);
	BarUiPianoCallClearTestHook ();

	/* The request was attempted once and its failure ended the action
	 * without touching (or freeing) any explanation text */
	ck_assert_int_eq (g_explainCalls, 1);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_ui_act_print_upcoming_to_without_current_song_reports_empty)
{
	BarApp_t app;
	PianoStation_t station;
	memset (&app, 0, sizeof (app));
	memset (&station, 0, sizeof (station));
	BarSettingsInit (&app.settings);
	TestWsCaptureAttach (&app, 2, NULL);

	BarUiActPrintUpcomingTo (&app, &station, NULL, test_ws_cap.ids[1]);
	TestWsCaptureFlush ();

	ck_assert_uint_eq (test_ws_cap.frameCount, 1);
	ck_assert_uint_eq (TestWsCaptureCount (1, "No songs in queue"), 1);
	ck_assert_uint_eq (TestWsCaptureCount (0, "No songs in queue"), 0);

	TestWsCaptureDetach (&app);
	BarSettingsDestroy (&app.settings);
}
END_TEST

Suite *
ws_edge_cases_suite (void)
{
	Suite *s = suite_create ("WebSocketEdgeCases");
	TCase *tc = tcase_create ("EdgeCases");
	tcase_set_timeout (tc, 30);
	tcase_add_test (tc, test_ws_connection_add_returns_zero_when_table_full);
	tcase_add_test (tc, test_ws_handle_receive_ignores_missing_app_or_context);
	tcase_add_test (tc, test_ws_send_rejects_missing_app_context_or_message);
	tcase_add_test (tc, test_ws_deliver_ignores_null_ctx_message_or_empty);
	tcase_add_test (tc, test_ws_live_client_is_tracked_greeted_and_removed);
	tcase_add_test (tc, test_ws_client_refused_when_table_full_then_reconnects);
	tcase_add_test (tc, test_socketio_action_with_null_name_is_ignored);
	tcase_add_test (tc, test_socketio_upcoming_without_requester_runs_terminal_callback);
	tcase_add_test (tc, test_socketio_object_action_and_command_forms_dispatch);
	tcase_add_test (tc, test_ui_act_explain_terminal_failed_request_prints_nothing_more);
	tcase_add_test (tc, test_ui_act_print_upcoming_to_without_current_song_reports_empty);
	suite_add_tcase (s, tc);
	return s;
}
