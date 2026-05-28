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
#include <string.h>
#include <time.h>
#include "../../src/main.h"
#include "../../src/websocket/core/websocket.h"
#include "../../src/websocket/protocol/socketio.h"
#include <json-c/json.h>

/* Wire-compat capture buffer */
static char g_compatBuf[8192];
static void compatCapture (const char *msg, size_t len) {
	size_t copy = len < sizeof (g_compatBuf) - 1 ? len : sizeof (g_compatBuf) - 1;
	memcpy (g_compatBuf, msg, copy);
	g_compatBuf[copy] = '\0';
}

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

/* Test: Get elapsed time with NULL app should return 0 */
START_TEST(test_websocket_elapsed_null) {
	unsigned int elapsed = BarWebsocketGetElapsed(NULL);
	ck_assert_int_eq(elapsed, 0);
}
END_TEST

/* Test: Broadcast message with NULL app should not crash */
START_TEST(test_websocket_broadcast_null) {
	/* The new API: NULL app frees message and returns */
	char *msg = strdup ("2[\"test\"]");
	BarWebsocketBroadcastSocketIoMessage (NULL, BUCKET_STATE, msg);
	/* If we get here without crashing, test passes */
	ck_assert (1);
}
END_TEST

/* Wire-compat: progress must be an object, not bare values */
START_TEST (test_wire_compat_progress_is_object)
{
	g_compatBuf[0] = '\0';
	BarSocketIoSetBroadcastCallback (compatCapture);

	struct json_object *data = json_object_new_object ();
	json_object_object_add (data, "elapsed",    json_object_new_int (42));
	json_object_object_add (data, "duration",   json_object_new_int (240));
	json_object_object_add (data, "percentage", json_object_new_int (17));
	BarSocketIoEmit ("progress", data);
	json_object_put (data);

	ck_assert (strncmp (g_compatBuf, "2[", 2) == 0);
	ck_assert (strstr (g_compatBuf, "\"progress\"") != NULL);
	ck_assert (strstr (g_compatBuf, "\"elapsed\"")  != NULL);
	ck_assert (strstr (g_compatBuf, "\"duration\"") != NULL);

	BarSocketIoSetBroadcastCallback (NULL);
}
END_TEST

/* Wire-compat: volume must be a bare integer, not wrapped in an object */
START_TEST (test_wire_compat_volume_is_bare_integer)
{
	g_compatBuf[0] = '\0';
	BarSocketIoSetBroadcastCallback (compatCapture);

	BarSocketIoEmit ("volume", json_object_new_int (50));

	ck_assert (strncmp (g_compatBuf, "2[", 2) == 0);
	ck_assert (strstr (g_compatBuf, "\"volume\"") != NULL);
	ck_assert (strstr (g_compatBuf, "{\"volume\"") == NULL);

	BarSocketIoSetBroadcastCallback (NULL);
}
END_TEST

/* Wire-compat: stop must have no payload element — no comma after "stop" */
START_TEST (test_wire_compat_stop_has_no_payload)
{
	g_compatBuf[0] = '\0';
	BarSocketIoSetBroadcastCallback (compatCapture);

	BarSocketIoEmit ("stop", NULL);

	ck_assert (strncmp (g_compatBuf, "2[", 2) == 0);
	const char *stopPos = strstr (g_compatBuf, "\"stop\"");
	ck_assert_ptr_nonnull (stopPos);
	/* No comma after "stop" means no second (payload) element */
	ck_assert_ptr_null (strchr (stopPos, ','));

	BarSocketIoSetBroadcastCallback (NULL);
}
END_TEST

/* Wire-compat: stations payload must open as an array bracket, not an object */
START_TEST (test_wire_compat_stations_is_array)
{
	g_compatBuf[0] = '\0';
	BarSocketIoSetBroadcastCallback (compatCapture);

	struct json_object *arr = json_object_new_array ();
	struct json_object *s   = json_object_new_object ();
	json_object_object_add (s, "id",         json_object_new_string ("sid"));
	json_object_object_add (s, "name",       json_object_new_string ("My Station"));
	json_object_object_add (s, "isQuickMix", json_object_new_boolean (0));
	json_object_array_add (arr, s);
	BarSocketIoEmit ("stations", arr);
	json_object_put (arr);

	ck_assert (strncmp (g_compatBuf, "2[", 2) == 0);
	ck_assert (strstr (g_compatBuf, "\"stations\"") != NULL);
	const char *after = strstr (g_compatBuf, "\"stations\"");
	ck_assert_ptr_nonnull (after);
	after += strlen ("\"stations\"");
	while (*after == ',' || *after == ' ') after++;
	ck_assert_int_eq (*after, '[');

	BarSocketIoSetBroadcastCallback (NULL);
}
END_TEST

/* Create test suite */
Suite *websocket_suite(void) {
	Suite *s;
	TCase *tc_core;
	TCase *tc_compat;
	
	s = suite_create("WebSocket");
	tc_core = tcase_create("Core");
	
	tcase_add_test(tc_core, test_websocket_init_null);
	tcase_add_test(tc_core, test_websocket_destroy_null);
	tcase_add_test(tc_core, test_websocket_elapsed_null);
	tcase_add_test(tc_core, test_websocket_broadcast_null);
	
	suite_add_tcase(s, tc_core);

	tc_compat = tcase_create ("wire_compat");
	tcase_add_test (tc_compat, test_wire_compat_progress_is_object);
	tcase_add_test (tc_compat, test_wire_compat_volume_is_bare_integer);
	tcase_add_test (tc_compat, test_wire_compat_stop_has_no_payload);
	tcase_add_test (tc_compat, test_wire_compat_stations_is_array);
	suite_add_tcase (s, tc_compat);
	
	return s;
}

