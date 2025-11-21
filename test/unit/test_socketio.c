/*
Copyright (c) 2025
    Kyle Hawes <khawes@netflix.com>

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
#include "../../src/main.h"
#include "../../src/websocket/protocol/socketio.h"

/* Test: Handle message with NULL app should not crash */
START_TEST(test_socketio_handle_null_app) {
	BarSocketIoHandleMessage(NULL, "2[\"query\"]");
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Test: Handle NULL message should not crash */
START_TEST(test_socketio_handle_null_message) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	BarSocketIoHandleMessage(&app, NULL);
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Test: Emit with NULL event name should not crash */
START_TEST(test_socketio_emit_null_event) {
	BarSocketIoEmit(NULL, NULL);
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Test: Emit stop with NULL app should not crash */
START_TEST(test_socketio_emit_stop_null) {
	BarSocketIoEmitStop(NULL);
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Test: Emit volume with NULL app should not crash */
START_TEST(test_socketio_emit_volume_null) {
	BarSocketIoEmitVolume(NULL, 50);
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Test: Emit progress with NULL app should not crash */
START_TEST(test_socketio_emit_progress_null) {
	BarSocketIoEmitProgress(NULL, 30, 180);
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Test: Emit stations with NULL app should not crash */
START_TEST(test_socketio_emit_stations_null) {
	BarSocketIoEmitStations(NULL);
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Test: Emit process with NULL app should not crash */
START_TEST(test_socketio_emit_process_null) {
	BarSocketIoEmitProcess(NULL);
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Test: Handle action with NULL app should not crash */
START_TEST(test_socketio_handle_action_null_app) {
	BarSocketIoHandleAction(NULL, "p");
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Test: Handle action with NULL action should not crash */
START_TEST(test_socketio_handle_action_null) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	BarSocketIoHandleAction(&app, NULL);
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Test: Handle changeStation with NULL app should not crash */
START_TEST(test_socketio_handle_changestation_null_app) {
	BarSocketIoHandleChangeStation(NULL, "Jazz");
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Test: Handle changeStation with NULL station should not crash */
START_TEST(test_socketio_handle_changestation_null) {
	BarApp_t app;
	memset(&app, 0, sizeof(app));
	BarSocketIoHandleChangeStation(&app, NULL);
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Test: Handle query with NULL app should not crash */
START_TEST(test_socketio_handle_query_null) {
	BarSocketIoHandleQuery(NULL);
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Create test suite */
Suite *socketio_suite(void) {
	Suite *s;
	TCase *tc_emitters;
	TCase *tc_handlers;
	
	s = suite_create("Socket.IO");
	
	tc_emitters = tcase_create("Emitters");
	tcase_add_test(tc_emitters, test_socketio_emit_null_event);
	tcase_add_test(tc_emitters, test_socketio_emit_stop_null);
	tcase_add_test(tc_emitters, test_socketio_emit_volume_null);
	tcase_add_test(tc_emitters, test_socketio_emit_progress_null);
	tcase_add_test(tc_emitters, test_socketio_emit_stations_null);
	tcase_add_test(tc_emitters, test_socketio_emit_process_null);
	suite_add_tcase(s, tc_emitters);
	
	tc_handlers = tcase_create("Handlers");
	tcase_add_test(tc_handlers, test_socketio_handle_null_app);
	tcase_add_test(tc_handlers, test_socketio_handle_null_message);
	tcase_add_test(tc_handlers, test_socketio_handle_action_null_app);
	tcase_add_test(tc_handlers, test_socketio_handle_action_null);
	tcase_add_test(tc_handlers, test_socketio_handle_changestation_null_app);
	tcase_add_test(tc_handlers, test_socketio_handle_changestation_null);
	tcase_add_test(tc_handlers, test_socketio_handle_query_null);
	suite_add_tcase(s, tc_handlers);
	
	return s;
}

