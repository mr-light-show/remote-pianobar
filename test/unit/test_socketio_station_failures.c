/*
 * Socket.IO station handlers: unknown station ids, failed Pandora calls and
 * rejected shared-station transforms. A failure reaches the requesting
 * clients as an error event, never as a station list, and every path must
 * release pianoHttpMutex.
 */

#include <check.h>
#include <errno.h>
#include <json-c/json.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/bar_state.h"
#include "../../src/main.h"
#include "../../src/settings.h"
#include "../../src/ui.h"
#include "../../src/websocket/core/websocket.h"
#include "../../src/websocket/protocol/socketio.h"
#include "test_ws_capture.h"

typedef struct {
	BarApp_t app;
	PianoStation_t current, plain, quickMix;
} Fixture_t;

static void
fixture_init (Fixture_t *f, bool withQuickMix)
{
	memset (f, 0, sizeof (*f));
	BarSettingsInit (&f->app.settings);
	f->app.settings.uiMode = BAR_UI_MODE_WEB;
	f->app.settings.volumeMode = BAR_VOLUME_MODE_PLAYER;
	BarStateInit (&f->app);
	ck_assert_int_eq (pthread_mutex_init (&f->app.player.lock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&f->app.player.cond, NULL), 0);
	ck_assert_int_eq (pthread_mutex_init (&f->app.player.decoderLock, NULL), 0);
	ck_assert_int_eq (pthread_cond_init (&f->app.player.decoderCond, NULL), 0);
	f->app.player.settings = &f->app.settings;
	BarUiPianoHttpMutexInit (&f->app);
	f->current.id = "S1";
	f->current.name = "Current";
	f->current.isCreator = true;
	f->plain.id = "S2";
	f->plain.name = "Plain";
	f->plain.isCreator = true;
	f->quickMix.id = "QM";
	f->quickMix.name = "QuickMix";
	f->quickMix.isQuickMix = true;
	f->current.head.next = &f->plain.head;
	if (withQuickMix) {
		f->plain.head.next = &f->quickMix.head;
	}
	f->app.ph.stations = &f->current;
	BarStateSetCurrentStation (&f->app, &f->current);
	TestWsCaptureAttach (&f->app, 1, NULL);
}

static void
fixture_destroy (Fixture_t *f)
{
	TestWsCaptureDetach (&f->app);
	BarUiPianoHttpMutexDestroy (&f->app);
	pthread_cond_destroy (&f->app.player.decoderCond);
	pthread_mutex_destroy (&f->app.player.decoderLock);
	pthread_cond_destroy (&f->app.player.cond);
	pthread_mutex_destroy (&f->app.player.lock);
	BarStateDestroy (&f->app);
	BarSettingsDestroy (&f->app.settings);
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
piano_mutex_is_free (BarApp_t *app)
{
	pthread_t probe;
	void *rc = NULL;
	ck_assert_int_eq (pthread_create (&probe, NULL, mutex_probe, app), 0);
	ck_assert_int_eq (pthread_join (probe, &rc), 0);
	return (intptr_t) rc == 0;
}

static int g_failType = -1;
static unsigned g_calls;

static bool
failing_piano_call (BarApp_t *app, const PianoRequestType_t type, void *data,
		PianoReturn_t *pRet, CURLcode *wRet)
{
	(void) app;
	(void) data;
	g_calls++;
	*wRet = CURLE_OK;
	if ((int) type == g_failType) {
		*pRet = PIANO_RET_ERR;
		return false;
	}
	*pRet = PIANO_RET_OK;
	return true;
}

static void
hook_install (int failType)
{
	g_failType = failType;
	g_calls = 0;
	BarUiPianoCallSetTestHook (failing_piano_call);
}

static json_object *
parse (const char *json)
{
	json_object *o = json_tokener_parse (json);
	ck_assert_ptr_nonnull (o);
	return o;
}

typedef void (*Handler_t) (BarApp_t *, json_object *);

/* Requester-aware handlers, adapted to Handler_t: no requester, so replies
 * are broadcast */
static void
handle_get_station_modes (BarApp_t *app, json_object *data)
{
	BarSocketIoHandleGetStationModes (app, data, BAR_WS_DEST_BROADCAST);
}

static void
handle_get_station_info (BarApp_t *app, json_object *data)
{
	BarSocketIoHandleGetStationInfo (app, data, BAR_WS_DEST_BROADCAST);
}

/* An unknown station id: nothing is called, the mutex the handler took for
 * the lookup is released again, and only the two reply-style handlers (their
 * requester is a broadcast here) tell anyone, with an error */
START_TEST (test_station_handlers_ignore_unknown_station)
{
	static const struct {
		Handler_t handler;
		const char *json;
	} cases[] = {
		{BarSocketIoHandleAddMusic, "{\"musicId\":\"M\",\"stationId\":\"nope\"}"},
		{BarSocketIoHandleRenameStation, "{\"stationId\":\"nope\",\"newName\":\"N\"}"},
		{handle_get_station_modes, "{\"stationId\":\"nope\"}"},
		{BarSocketIoHandleSetStationMode, "{\"stationId\":\"nope\",\"modeId\":1}"},
		{handle_get_station_info, "{\"stationId\":\"nope\"}"},
		{BarSocketIoHandleDeleteSeed,
				"{\"seedId\":\"X\",\"seedType\":\"artist\",\"stationId\":\"nope\"}"},
		{BarSocketIoHandleDeleteFeedback, "{\"feedbackId\":\"F\",\"stationId\":\"nope\"}"},
		{BarSocketIoHandleDeleteStation, "{\"stationId\":\"nope\"}"},
	};
	Fixture_t f;
	fixture_init (&f, true);
	hook_install (-1);
	for (size_t i = 0; i < sizeof (cases) / sizeof (cases[0]); i++) {
		json_object *data = parse (cases[i].json);
		cases[i].handler (&f.app, data);
		json_object_put (data);
		ck_assert_msg (piano_mutex_is_free (&f.app), "case %zu kept the mutex", i);
	}
	BarUiPianoCallClearTestHook ();
	ck_assert_uint_eq (g_calls, 0);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 2);
	for (size_t i = 0; i < test_ws_cap.frameCount; i++) {
		ck_assert_msg (strstr (test_ws_cap.frames[i], "\"error\"") != NULL,
				"frame: %s", test_ws_cap.frames[i]);
	}
	fixture_destroy (&f);
}
END_TEST

/* A failed Pandora call is reported to the clients as an error for that
 * operation, with the mutex released and no station list sent */
static void
expect_failure_reported (Fixture_t *f, Handler_t handler, const char *json,
		PianoRequestType_t failing, const char *operation)
{
	hook_install ((int) failing);
	json_object *data = parse (json);
	handler (&f->app, data);
	json_object_put (data);
	BarUiPianoCallClearTestHook ();
	ck_assert_uint_gt (g_calls, 0);
	ck_assert (piano_mutex_is_free (&f->app));
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 1);
	ck_assert_msg (strstr (test_ws_cap.frames[0], "\"error\"") != NULL,
			"frame: %s", test_ws_cap.frames[0]);
	ck_assert_msg (strstr (test_ws_cap.frames[0], operation) != NULL,
			"frame: %s", test_ws_cap.frames[0]);
	ck_assert_uint_eq (TestWsCaptureCount (0, "\"stations\""), 0);
	TestWsCaptureClear ();
}

START_TEST (test_station_handlers_report_failed_pandora_calls)
{
	Fixture_t f;
	fixture_init (&f, true);
	expect_failure_reported (&f, BarSocketIoHandleRenameStation,
			"{\"stationId\":\"S2\",\"newName\":\"New\"}",
			PIANO_REQUEST_RENAME_STATION, "station.rename");
	expect_failure_reported (&f, handle_get_station_modes,
			"{\"stationId\":\"S2\"}", PIANO_REQUEST_GET_STATION_MODES,
			"station.getStationModes");
	expect_failure_reported (&f, handle_get_station_info,
			"{\"stationId\":\"S2\"}", PIANO_REQUEST_GET_STATION_INFO,
			"station.getStationInfo");
	expect_failure_reported (&f, BarSocketIoHandleDeleteSeed,
			"{\"seedId\":\"X\",\"seedType\":\"artist\",\"stationId\":\"S2\"}",
			PIANO_REQUEST_GET_STATION_INFO, "station.deleteSeed");
	expect_failure_reported (&f, BarSocketIoHandleDeleteFeedback,
			"{\"feedbackId\":\"F\",\"stationId\":\"S2\"}",
			PIANO_REQUEST_GET_STATION_INFO, "station.deleteFeedback");
	expect_failure_reported (&f, BarSocketIoHandleDeleteStation,
			"{\"stationId\":\"S2\"}", PIANO_REQUEST_DELETE_STATION,
			"station.delete");
	/* the failed delete changed nothing */
	ck_assert_ptr_eq (BarStateFindStationById (&f.app, "S2"), &f.plain);
	ck_assert_ptr_eq (BarStateGetCurrentStation (&f.app), &f.current);
	fixture_destroy (&f);
}
END_TEST

/* A shared station that cannot be transformed is not changed; the client is
 * told, and the mutex is released */
START_TEST (test_shared_station_transform_failure_is_reported)
{
	Fixture_t f;
	fixture_init (&f, true);
	f.plain.isCreator = false;

	hook_install (PIANO_REQUEST_TRANSFORM_STATION);
	json_object *data = parse ("{\"musicId\":\"M\",\"stationId\":\"S2\"}");
	BarSocketIoHandleAddMusic (&f.app, data);
	json_object_put (data);
	ck_assert (piano_mutex_is_free (&f.app));
	TestWsCaptureFlush ();
	ck_assert_uint_eq (TestWsCaptureCount (0, "Failed to transform station"), 1);
	ck_assert_uint_eq (TestWsCaptureCount (0, "station.addMusic"), 1);
	TestWsCaptureClear ();

	data = parse ("{\"stationId\":\"S2\",\"newName\":\"New\"}");
	BarSocketIoHandleRenameStation (&f.app, data);
	json_object_put (data);
	BarUiPianoCallClearTestHook ();
	ck_assert (piano_mutex_is_free (&f.app));
	TestWsCaptureFlush ();
	ck_assert_uint_eq (TestWsCaptureCount (0, "Failed to transform station"), 1);
	ck_assert_uint_eq (TestWsCaptureCount (0, "station.rename"), 1);
	ck_assert_uint_eq (TestWsCaptureCount (0, "\"stations\""), 0);
	/* the rename and seed calls never happened: only transforms were tried */
	ck_assert_uint_eq (g_calls, 2);
	ck_assert_str_eq (f.plain.name, "Plain");
	fixture_destroy (&f);
}
END_TEST

/* Deleting the playing station switches to the QuickMix station, skipping
 * the stations before it in the list */
START_TEST (test_delete_current_station_skips_plain_stations_to_quickmix)
{
	Fixture_t f;
	fixture_init (&f, true);
	hook_install (-1);
	json_object *data = parse ("{\"stationId\":\"S1\"}");
	BarSocketIoHandleDeleteStation (&f.app, data);
	json_object_put (data);
	BarUiPianoCallClearTestHook ();
	ck_assert_ptr_eq (BarStateGetNextStation (&f.app), &f.quickMix);
	TestWsCaptureFlush ();
	ck_assert_uint_eq (TestWsCaptureCount (0, "\"stations\""), 1);
	f.app.ph.stations = &f.current; /* the hook did not unlink it */
	fixture_destroy (&f);
}
END_TEST

/* With no QuickMix station there is nothing to switch to: the client still
 * gets the new station list and nothing is selected */
START_TEST (test_delete_current_station_without_quickmix_selects_nothing)
{
	Fixture_t f;
	fixture_init (&f, false);
	hook_install (-1);
	json_object *data = parse ("{\"stationId\":\"S1\"}");
	BarSocketIoHandleDeleteStation (&f.app, data);
	json_object_put (data);
	BarUiPianoCallClearTestHook ();
	ck_assert_ptr_null (BarStateGetNextStation (&f.app));
	TestWsCaptureFlush ();
	ck_assert_uint_eq (TestWsCaptureCount (0, "\"stations\""), 1);
	fixture_destroy (&f);
}
END_TEST

/* The QuickMix entry without an id cannot be switched to */
START_TEST (test_delete_current_station_ignores_quickmix_without_id)
{
	Fixture_t f;
	fixture_init (&f, true);
	f.quickMix.id = NULL;
	hook_install (-1);
	json_object *data = parse ("{\"stationId\":\"S1\"}");
	BarSocketIoHandleDeleteStation (&f.app, data);
	json_object_put (data);
	BarUiPianoCallClearTestHook ();
	ck_assert_ptr_null (BarStateGetNextStation (&f.app));
	fixture_destroy (&f);
}
END_TEST

/* Upcoming songs: no app or a non-positive limit sends nothing */
START_TEST (test_emit_upcoming_rejects_missing_app_and_bad_limit)
{
	Fixture_t f;
	fixture_init (&f, true);
	ck_assert (!BarSocketIoEmitUpcomingTo (NULL, test_ws_cap.ids[0], 5));
	ck_assert (!BarSocketIoEmitUpcomingTo (&f.app, test_ws_cap.ids[0], 0));
	ck_assert (!BarSocketIoEmitUpcomingTo (&f.app, test_ws_cap.ids[0], -3));
	TestWsCaptureFlush ();
	ck_assert_uint_eq (test_ws_cap.frameCount, 0);
	fixture_destroy (&f);
}
END_TEST

/* "play" while Pandora is disconnected is a reconnect request, and without
 * credentials the clients are told so */
START_TEST (test_play_while_disconnected_becomes_reconnect)
{
	Fixture_t f;
	fixture_init (&f, true);
	ck_assert (!BarStateIsPandoraConnected (&f.app));
	BarSocketIoHandleAction (&f.app, "playback.play", NULL, test_ws_cap.ids[0]);
	TestWsCaptureFlush ();
	ck_assert_uint_ge (TestWsCaptureCount (0, "\"error\""), 1);
	fixture_destroy (&f);
}
END_TEST

Suite *
socketio_station_failures_suite (void)
{
	Suite *s = suite_create ("SocketIoStationFailures");
	TCase *tc = tcase_create ("Core");
	tcase_set_timeout (tc, 30);
	tcase_add_test (tc, test_station_handlers_ignore_unknown_station);
	tcase_add_test (tc, test_station_handlers_report_failed_pandora_calls);
	tcase_add_test (tc, test_shared_station_transform_failure_is_reported);
	tcase_add_test (tc, test_delete_current_station_skips_plain_stations_to_quickmix);
	tcase_add_test (tc, test_delete_current_station_without_quickmix_selects_nothing);
	tcase_add_test (tc, test_delete_current_station_ignores_quickmix_without_id);
	tcase_add_test (tc, test_emit_upcoming_rejects_missing_app_and_bad_limit);
	tcase_add_test (tc, test_play_while_disconnected_becomes_reconnect);
	suite_add_tcase (s, tc);
	return s;
}
