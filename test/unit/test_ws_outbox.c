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
#include "../../src/bar_constants.h"
#include "../../src/websocket/core/outbox.h"

/* Append a new frame holding text; the queue takes its own reference. */
static BarWsQResult_t append_text (BarWsClientQueue_t *q, const char *text,
		BarWsKey_t key, uint32_t gen) {
	BarWsFrame_t *f = BarWsFrameNew (text, strlen (text));
	ck_assert_ptr_nonnull (f);
	BarWsQResult_t r = BarWsClientQueueAppend (q, f, key, gen);
	BarWsFrameUnref (f);
	return r;
}

/* Pop and assert the payload equals expected (NULL expects an empty queue). */
static void expect_pop (BarWsClientQueue_t *q, uint32_t gen, const char *expected) {
	BarWsFrame_t *f = BarWsClientQueuePop (q, gen);
	if (!expected) {
		ck_assert_ptr_null (f);
		return;
	}
	ck_assert_ptr_nonnull (f);
	ck_assert_uint_eq (BarWsFrameLen (f), strlen (expected));
	ck_assert_int_eq (memcmp (BarWsFrameData (f), expected, strlen (expected)), 0);
	BarWsFrameUnref (f);
}

START_TEST (test_queue_fifo_order) {
	BarWsClientQueue_t q;
	BarWsClientQueueInit (&q);
	ck_assert_int_eq (append_text (&q, "a", BAR_WS_KEY_NONE, 0), BAR_WS_Q_OK);
	ck_assert_int_eq (append_text (&q, "b", BAR_WS_KEY_NONE, 0), BAR_WS_Q_OK);
	ck_assert_int_eq (append_text (&q, "c", BAR_WS_KEY_NONE, 0), BAR_WS_Q_OK);
	expect_pop (&q, 0, "a");
	expect_pop (&q, 0, "b");
	expect_pop (&q, 0, "c");
	expect_pop (&q, 0, NULL);
	BarWsClientQueueClear (&q);
}
END_TEST

START_TEST (test_keyed_replaces_unsent_same_key) {
	BarWsClientQueue_t q;
	BarWsClientQueueInit (&q);
	append_text (&q, "p1", BAR_WS_KEY_PROGRESS, 1);
	append_text (&q, "v1", BAR_WS_KEY_VOLUME, 1);
	append_text (&q, "p2", BAR_WS_KEY_PROGRESS, 1);
	append_text (&q, "v2", BAR_WS_KEY_VOLUME, 1);
	ck_assert_uint_eq (q.count, 2);
	expect_pop (&q, 1, "p2");
	expect_pop (&q, 1, "v2");
	expect_pop (&q, 1, NULL);
	BarWsClientQueueClear (&q);
}
END_TEST

START_TEST (test_keyed_does_not_replace_across_ordered_event) {
	BarWsClientQueue_t q;
	BarWsClientQueueInit (&q);
	append_text (&q, "p1", BAR_WS_KEY_PROGRESS, 1);
	append_text (&q, "event", BAR_WS_KEY_NONE, 1);
	append_text (&q, "p2", BAR_WS_KEY_PROGRESS, 1);
	append_text (&q, "p3", BAR_WS_KEY_PROGRESS, 1);
	expect_pop (&q, 1, "p1");
	expect_pop (&q, 1, "event");
	expect_pop (&q, 1, "p3");
	expect_pop (&q, 1, NULL);
	BarWsClientQueueClear (&q);
}
END_TEST

START_TEST (test_start_then_playstate_both_kept) {
	BarWsClientQueue_t q;
	BarWsClientQueueInit (&q);
	append_text (&q, "start", BAR_WS_KEY_NONE, 1);
	append_text (&q, "playstate", BAR_WS_KEY_NONE, 1);
	ck_assert_uint_eq (q.count, 2);
	expect_pop (&q, 1, "start");
	expect_pop (&q, 1, "playstate");
	BarWsClientQueueClear (&q);
}
END_TEST

START_TEST (test_stop_then_start_both_kept) {
	BarWsClientQueue_t q;
	BarWsClientQueueInit (&q);
	append_text (&q, "stop", BAR_WS_KEY_NONE, 1);
	append_text (&q, "start", BAR_WS_KEY_NONE, 1);
	ck_assert_uint_eq (q.count, 2);
	expect_pop (&q, 1, "stop");
	expect_pop (&q, 1, "start");
	BarWsClientQueueClear (&q);
}
END_TEST

START_TEST (test_progress_with_old_generation_dropped_on_pop) {
	BarWsClientQueue_t q;
	BarWsClientQueueInit (&q);
	append_text (&q, "old-progress", BAR_WS_KEY_PROGRESS, 1);
	append_text (&q, "event", BAR_WS_KEY_NONE, 1);
	append_text (&q, "old-volume", BAR_WS_KEY_VOLUME, 1);
	append_text (&q, "new-progress", BAR_WS_KEY_PROGRESS, 2);
	expect_pop (&q, 2, "event");
	expect_pop (&q, 2, "old-volume");
	expect_pop (&q, 2, "new-progress");
	expect_pop (&q, 2, NULL);
	ck_assert_uint_eq (q.bytes, 0);
	BarWsClientQueueClear (&q);
}
END_TEST

START_TEST (test_byte_cap_overflow_returns_overflow) {
	BarWsClientQueue_t q;
	BarWsClientQueueInit (&q);
	char *big = malloc (WS_FRAME_MAX_BYTES);
	ck_assert_ptr_nonnull (big);
	memset (big, 'x', WS_FRAME_MAX_BYTES);
	BarWsFrame_t *f = BarWsFrameNew (big, WS_FRAME_MAX_BYTES);
	free (big);
	ck_assert_ptr_nonnull (f);

	size_t fit = WS_CLIENT_MAX_BYTES / WS_FRAME_MAX_BYTES;
	for (size_t i = 0; i < fit; i++) {
		ck_assert_int_eq (BarWsClientQueueAppend (&q, f, BAR_WS_KEY_NONE, 0), BAR_WS_Q_OK);
	}
	ck_assert_int_eq (BarWsClientQueueAppend (&q, f, BAR_WS_KEY_NONE, 0), BAR_WS_Q_OVERFLOW);
	ck_assert_uint_eq (q.count, fit);
	BarWsFrameUnref (f);
	BarWsClientQueueClear (&q);
}
END_TEST

START_TEST (test_message_cap_overflow_returns_overflow) {
	BarWsClientQueue_t q;
	BarWsClientQueueInit (&q);
	for (size_t i = 0; i < WS_CLIENT_MAX_MSGS; i++) {
		ck_assert_int_eq (append_text (&q, "m", BAR_WS_KEY_NONE, 0), BAR_WS_Q_OK);
	}
	ck_assert_int_eq (append_text (&q, "m", BAR_WS_KEY_NONE, 0), BAR_WS_Q_OVERFLOW);
	BarWsClientQueueClear (&q);
}
END_TEST

START_TEST (test_keyed_appends_coalesce_instead_of_growing) {
	BarWsClientQueue_t q;
	BarWsClientQueueInit (&q);
	append_text (&q, "p1", BAR_WS_KEY_PROGRESS, 1);
	for (size_t i = 1; i < WS_CLIENT_MAX_MSGS; i++) {
		append_text (&q, "m", BAR_WS_KEY_VOLUME, 1);
	}
	/* repeated keyed appends coalesce rather than consume message slots */
	ck_assert_uint_eq (q.count, 2);
	BarWsClientQueueClear (&q);
}
END_TEST

START_TEST (test_empty_queue_accepts_one_max_size_full_state_frame) {
	BarWsClientQueue_t q;
	BarWsClientQueueInit (&q);
	char *big = malloc (WS_FRAME_MAX_BYTES);
	ck_assert_ptr_nonnull (big);
	memset (big, 's', WS_FRAME_MAX_BYTES);
	BarWsFrame_t *f = BarWsFrameNew (big, WS_FRAME_MAX_BYTES);
	free (big);
	ck_assert_ptr_nonnull (f);
	ck_assert_int_eq (BarWsClientQueueAppend (&q, f, BAR_WS_KEY_STATIONS, 0), BAR_WS_Q_OK);
	ck_assert_uint_eq (q.bytes, WS_FRAME_MAX_BYTES);
	BarWsFrameUnref (f);
	BarWsClientQueueClear (&q);
}
END_TEST

START_TEST (test_oversize_frame_rejected) {
	char *big = malloc (WS_FRAME_MAX_BYTES + 1);
	ck_assert_ptr_nonnull (big);
	memset (big, 'x', WS_FRAME_MAX_BYTES + 1);
	ck_assert_ptr_null (BarWsFrameNew (big, WS_FRAME_MAX_BYTES + 1));
	free (big);
}
END_TEST

START_TEST (test_clear_frees_pending) {
	BarWsClientQueue_t q;
	BarWsClientQueueInit (&q);
	append_text (&q, "a", BAR_WS_KEY_NONE, 0);
	append_text (&q, "b", BAR_WS_KEY_PROGRESS, 0);
	append_text (&q, "c", BAR_WS_KEY_NONE, 0);
	BarWsClientQueueClear (&q);
	ck_assert_ptr_null (q.head);
	ck_assert_uint_eq (q.count, 0);
	ck_assert_uint_eq (q.bytes, 0);
	/* leak detection runs under make test-asan */
}
END_TEST

START_TEST (test_shared_frame_survives_other_queue_clear) {
	BarWsClientQueue_t q1, q2;
	BarWsClientQueueInit (&q1);
	BarWsClientQueueInit (&q2);
	BarWsFrame_t *f = BarWsFrameNew ("shared", 6);
	ck_assert_ptr_nonnull (f);
	ck_assert_int_eq (BarWsClientQueueAppend (&q1, f, BAR_WS_KEY_NONE, 0), BAR_WS_Q_OK);
	ck_assert_int_eq (BarWsClientQueueAppend (&q2, f, BAR_WS_KEY_NONE, 0), BAR_WS_Q_OK);
	BarWsFrameUnref (f);
	BarWsClientQueueClear (&q1);
	expect_pop (&q2, 0, "shared");
	BarWsClientQueueClear (&q2);
}
END_TEST

START_TEST (test_frame_null_and_empty_safe) {
	BarWsFrameUnref (NULL);
	ck_assert_ptr_null (BarWsFrameRef (NULL));
	ck_assert_ptr_null (BarWsFrameNew (NULL, 4));
	BarWsFrame_t *f = BarWsFrameNew ("", 0);
	ck_assert_ptr_nonnull (f);
	ck_assert_uint_eq (BarWsFrameLen (f), 0);
	BarWsFrameUnref (f);
}
END_TEST

Suite *ws_outbox_suite (void) {
	Suite *s = suite_create ("ws_outbox");
	TCase *tc = tcase_create ("Core");
	tcase_add_test (tc, test_queue_fifo_order);
	tcase_add_test (tc, test_keyed_replaces_unsent_same_key);
	tcase_add_test (tc, test_keyed_does_not_replace_across_ordered_event);
	tcase_add_test (tc, test_start_then_playstate_both_kept);
	tcase_add_test (tc, test_stop_then_start_both_kept);
	tcase_add_test (tc, test_progress_with_old_generation_dropped_on_pop);
	tcase_add_test (tc, test_byte_cap_overflow_returns_overflow);
	tcase_add_test (tc, test_message_cap_overflow_returns_overflow);
	tcase_add_test (tc, test_keyed_appends_coalesce_instead_of_growing);
	tcase_add_test (tc, test_empty_queue_accepts_one_max_size_full_state_frame);
	tcase_add_test (tc, test_oversize_frame_rejected);
	tcase_add_test (tc, test_clear_frees_pending);
	tcase_add_test (tc, test_shared_frame_survives_other_queue_clear);
	tcase_add_test (tc, test_frame_null_and_empty_safe);
	suite_add_tcase (s, tc);
	return s;
}
