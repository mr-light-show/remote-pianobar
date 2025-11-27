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
#include <stdlib.h>
#include <time.h>
#include "../../src/main.h"
#include "../../src/websocket/core/websocket.h"

/* Test: WebSocket initialization with NULL app should fail */
START_TEST(test_websocket_init_null) {
	bool result = BarWebsocketInit(NULL);
	ck_assert_int_eq(result, false);
}
END_TEST

/* Test: WebSocket cleanup with NULL app should not crash */
START_TEST(test_websocket_destroy_null) {
	BarWebsocketDestroy(NULL);
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Test: WebSocket service with NULL app should not crash */
START_TEST(test_websocket_service_null) {
	BarWebsocketService(NULL, 100);
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Test: Get elapsed time with NULL app should return 0 */
START_TEST(test_websocket_elapsed_null) {
	unsigned int elapsed = BarWebsocketGetElapsed(NULL);
	ck_assert_int_eq(elapsed, 0);
}
END_TEST

/* Test: Broadcast functions with NULL app should not crash */
START_TEST(test_websocket_broadcast_null) {
	BarWebsocketBroadcastState(NULL);
	BarWebsocketBroadcastSongStart(NULL);
	BarWebsocketBroadcastSongStop(NULL);
	BarWebsocketBroadcastVolume(NULL, 50);
	BarWebsocketBroadcastProgress(NULL);
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Create test suite */
Suite *websocket_suite(void) {
	Suite *s;
	TCase *tc_core;
	
	s = suite_create("WebSocket");
	tc_core = tcase_create("Core");
	
	tcase_add_test(tc_core, test_websocket_init_null);
	tcase_add_test(tc_core, test_websocket_destroy_null);
	tcase_add_test(tc_core, test_websocket_service_null);
	tcase_add_test(tc_core, test_websocket_elapsed_null);
	tcase_add_test(tc_core, test_websocket_broadcast_null);
	
	suite_add_tcase(s, tc_core);
	
	return s;
}

