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
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdatomic.h>
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

static BarWsFrame_t *frame_of_size (size_t n, char c) {
	char *buf = malloc (n);
	ck_assert_ptr_nonnull (buf);
	memset (buf, c, n);
	BarWsFrame_t *f = BarWsFrameNew (buf, n);
	free (buf);
	ck_assert_ptr_nonnull (f);
	return f;
}

START_TEST (test_replace_with_larger_frame_updates_bytes) {
	BarWsClientQueue_t q;
	BarWsClientQueueInit (&q);
	ck_assert_int_eq (append_text (&q, "ab", BAR_WS_KEY_PROGRESS, 1), BAR_WS_Q_OK);
	ck_assert_uint_eq (q.bytes, 2);
	ck_assert_int_eq (append_text (&q, "abcdef", BAR_WS_KEY_PROGRESS, 1), BAR_WS_Q_OK);
	ck_assert_uint_eq (q.count, 1);
	ck_assert_uint_eq (q.bytes, 6);
	BarWsClientQueueClear (&q);
}
END_TEST

START_TEST (test_replace_with_smaller_frame_updates_bytes) {
	BarWsClientQueue_t q;
	BarWsClientQueueInit (&q);
	ck_assert_int_eq (append_text (&q, "abcdef", BAR_WS_KEY_VOLUME, 1), BAR_WS_Q_OK);
	ck_assert_int_eq (append_text (&q, "ab", BAR_WS_KEY_VOLUME, 1), BAR_WS_Q_OK);
	ck_assert_uint_eq (q.count, 1);
	ck_assert_uint_eq (q.bytes, 2);
	expect_pop (&q, 1, "ab");
	BarWsClientQueueClear (&q);
}
END_TEST

START_TEST (test_replace_over_byte_cap_rejected_original_intact) {
	BarWsClientQueue_t q;
	BarWsClientQueueInit (&q);
	BarWsFrame_t *big = frame_of_size (WS_FRAME_MAX_BYTES, 'x');
	for (int i = 0; i < 3; i++) {
		ck_assert_int_eq (BarWsClientQueueAppend (&q, big, BAR_WS_KEY_NONE, 0), BAR_WS_Q_OK);
	}
	BarWsFrame_t *mid = frame_of_size (40 * 1024, 'm');
	ck_assert_int_eq (BarWsClientQueueAppend (&q, mid, BAR_WS_KEY_NONE, 0), BAR_WS_Q_OK);
	ck_assert_int_eq (append_text (&q, "p", BAR_WS_KEY_PROGRESS, 1), BAR_WS_Q_OK);
	size_t before = q.bytes;
	/* replacing the 1-byte progress with a max frame would exceed the cap */
	ck_assert_int_eq (BarWsClientQueueAppend (&q, big, BAR_WS_KEY_PROGRESS, 2), BAR_WS_Q_OVERFLOW);
	ck_assert_uint_eq (q.bytes, before);
	ck_assert_uint_eq (q.count, 5);
	BarWsFrameUnref (big);
	BarWsFrameUnref (mid);
	for (int i = 0; i < 4; i++) {
		BarWsFrame_t *f = BarWsClientQueuePop (&q, 1);
		ck_assert_ptr_nonnull (f);
		BarWsFrameUnref (f);
	}
	expect_pop (&q, 1, "p");
	BarWsClientQueueClear (&q);
}
END_TEST

START_TEST (test_replace_updates_generation) {
	BarWsClientQueue_t q;
	BarWsClientQueueInit (&q);
	ck_assert_int_eq (append_text (&q, "p-gen1", BAR_WS_KEY_PROGRESS, 1), BAR_WS_Q_OK);
	ck_assert_int_eq (append_text (&q, "p-gen2", BAR_WS_KEY_PROGRESS, 2), BAR_WS_Q_OK);
	ck_assert_uint_eq (q.count, 1);
	expect_pop (&q, 2, "p-gen2");
	BarWsClientQueueClear (&q);
}
END_TEST

/* The same frame appended twice under one key replaces itself: the swap must
 * take the new reference before dropping the old one. */
START_TEST (test_keyed_self_replace_keeps_frame) {
	BarWsClientQueue_t q;
	BarWsClientQueueInit (&q);
	BarWsFrame_t *f = BarWsFrameNew ("vol", 3);
	ck_assert_ptr_nonnull (f);
	ck_assert_int_eq (BarWsClientQueueAppend (&q, f, BAR_WS_KEY_VOLUME, 1), BAR_WS_Q_OK);
	ck_assert_int_eq (BarWsClientQueueAppend (&q, f, BAR_WS_KEY_VOLUME, 1), BAR_WS_Q_OK);
	BarWsFrameUnref (f);
	ck_assert_uint_eq (q.count, 1);
	ck_assert_uint_eq (q.bytes, 3);
	expect_pop (&q, 1, "vol");
	expect_pop (&q, 1, NULL);
	BarWsClientQueueClear (&q);
}
END_TEST

/* ---- ingress ---- */

static char *dup_text (const char *s) {
	char *d = strdup (s);
	ck_assert_ptr_nonnull (d);
	return d;
}

static bool push_text (BarWsOutbox_t *o, BarWsConnId_t dest, BarWsKey_t key, const char *s) {
	return BarWsOutboxPush (o, dest, key, dup_text (s), strlen (s));
}

START_TEST (test_push_does_not_deliver_until_take) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	ck_assert_ptr_null (BarWsOutboxTake (o));
	ck_assert (push_text (o, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, "a"));
	ck_assert (push_text (o, 7, BAR_WS_KEY_VOLUME, "bb"));
	ck_assert (push_text (o, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE, "c"));
	BarWsPending_t *list = BarWsOutboxTake (o);
	ck_assert_ptr_nonnull (list);
	ck_assert_uint_eq (list->dest, BAR_WS_DEST_BROADCAST);
	ck_assert_int_eq (list->key, BAR_WS_KEY_NONE);
	ck_assert_uint_eq (BarWsFrameLen (list->frame), 1);
	ck_assert_uint_eq (list->next->dest, 7);
	ck_assert_int_eq (list->next->key, BAR_WS_KEY_VOLUME);
	ck_assert_int_eq (memcmp (BarWsFrameData (list->next->frame), "bb", 2), 0);
	ck_assert_int_eq (BarWsFrameData (list->next->next->frame)[0], 'c');
	ck_assert_ptr_null (list->next->next->next);
	BarWsPendingFree (list);
	/* taken entries are gone */
	ck_assert_ptr_null (BarWsOutboxTake (o));
	BarWsOutboxFree (o);
}
END_TEST

START_TEST (test_ingress_full_returns_false_and_frees) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	for (size_t i = 0; i < WS_INGRESS_MAX_MSGS; i++) {
		ck_assert (push_text (o, 1, BAR_WS_KEY_NONE, "m"));
	}
	/* leak detection of the rejected text runs under make test-asan */
	ck_assert (!push_text (o, 1, BAR_WS_KEY_NONE, "m"));
	BarWsPending_t *list = BarWsOutboxTake (o);
	size_t n = 0;
	for (BarWsPending_t *p = list; p; p = p->next) {
		n++;
	}
	ck_assert_uint_eq (n, WS_INGRESS_MAX_MSGS);
	BarWsPendingFree (list);
	/* room again after Take */
	ck_assert (push_text (o, 1, BAR_WS_KEY_NONE, "m"));
	BarWsOutboxFree (o);
}
END_TEST

START_TEST (test_ingress_byte_cap_and_oversize_rejected) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	size_t fit = WS_INGRESS_MAX_BYTES / WS_FRAME_MAX_BYTES;
	for (size_t i = 0; i < fit; i++) {
		char *t = malloc (WS_FRAME_MAX_BYTES);
		ck_assert_ptr_nonnull (t);
		memset (t, 'x', WS_FRAME_MAX_BYTES);
		ck_assert (BarWsOutboxPush (o, 1, BAR_WS_KEY_NONE, t, WS_FRAME_MAX_BYTES));
	}
	ck_assert (!push_text (o, 1, BAR_WS_KEY_NONE, "x"));
	BarWsPendingFree (BarWsOutboxTake (o));
	char *t = malloc (WS_FRAME_MAX_BYTES + 1);
	ck_assert_ptr_nonnull (t);
	memset (t, 'x', WS_FRAME_MAX_BYTES + 1);
	ck_assert (!BarWsOutboxPush (o, 1, BAR_WS_KEY_NONE, t, WS_FRAME_MAX_BYTES + 1));
	ck_assert_ptr_null (BarWsOutboxTake (o));
	BarWsOutboxFree (o);
}
END_TEST

static bool push_start (BarWsOutbox_t *o, const char *s) {
	return BarWsOutboxPushSongStart (o, dup_text (s), strlen (s));
}

/* Frame payloads of a taken list, comma separated (for order assertions) */
static void expect_taken (BarWsOutbox_t *o, const char *expected) {
	char got[256] = "";
	BarWsPending_t *list = BarWsOutboxTake (o);
	for (BarWsPending_t *p = list; p; p = p->next) {
		size_t used = strlen (got);
		ck_assert_uint_lt (used + BarWsFrameLen (p->frame) + 2, sizeof (got));
		if (used > 0) {
			got[used++] = ',';
		}
		memcpy (got + used, BarWsFrameData (p->frame), BarWsFrameLen (p->frame));
		got[used + BarWsFrameLen (p->frame)] = '\0';
	}
	BarWsPendingFree (list);
	ck_assert_str_eq (got, expected);
}

START_TEST (test_progress_pushed_before_start_is_dropped_after_start) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	ck_assert_uint_eq (BarWsOutboxGeneration (o), 0);
	ck_assert (push_text (o, 0, BAR_WS_KEY_PROGRESS, "old-p1"));
	ck_assert (push_text (o, 0, BAR_WS_KEY_NONE, "event"));
	ck_assert (push_text (o, 0, BAR_WS_KEY_PROGRESS, "old-p2"));
	ck_assert (push_text (o, 0, BAR_WS_KEY_VOLUME, "vol"));
	ck_assert (push_start (o, "start"));
	ck_assert_uint_eq (BarWsOutboxGeneration (o), 1);
	expect_taken (o, "event,vol,start");
	/* byte/message accounting stayed consistent: a full batch still fits */
	for (size_t i = 0; i < WS_INGRESS_MAX_MSGS; i++) {
		ck_assert (push_text (o, 0, BAR_WS_KEY_NONE, "m"));
	}
	BarWsOutboxFree (o);
}
END_TEST

START_TEST (test_start_discards_progress_at_head_and_tail_keeps_order) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	ck_assert (push_text (o, 0, BAR_WS_KEY_PROGRESS, "p-head"));
	ck_assert (push_text (o, 0, BAR_WS_KEY_NONE, "a"));
	ck_assert (push_text (o, 0, BAR_WS_KEY_PROGRESS, "p-tail"));
	ck_assert (push_start (o, "start"));
	ck_assert (push_text (o, 0, BAR_WS_KEY_NONE, "b"));
	expect_taken (o, "a,start,b");
	BarWsOutboxFree (o);
}
END_TEST

START_TEST (test_progress_for_new_song_survives) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	ck_assert (push_text (o, 0, BAR_WS_KEY_PROGRESS, "old"));
	ck_assert (push_start (o, "start"));
	uint32_t gen = BarWsOutboxGeneration (o);
	ck_assert (push_text (o, 0, BAR_WS_KEY_PROGRESS, "new"));
	BarWsPending_t *list = BarWsOutboxTake (o);
	ck_assert_ptr_nonnull (list);
	ck_assert_ptr_nonnull (list->next);
	ck_assert_ptr_null (list->next->next);
	ck_assert_int_eq (memcmp (BarWsFrameData (list->next->frame), "new", 3), 0);
	ck_assert_uint_eq (list->next->generation, gen);
	BarWsPendingFree (list);
	BarWsOutboxFree (o);
}
END_TEST

/* R2: progress keeps the generation its producer observed before sampling,
 * so a sample taken for the old song is stale even if pushed after a start. */
START_TEST (test_push_progress_keeps_observed_generation) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	uint32_t observed = BarWsOutboxGeneration (o);
	ck_assert (push_start (o, "start"));
	ck_assert (BarWsOutboxPushProgress (o, observed, dup_text ("late"), 4));
	BarWsPending_t *list = BarWsOutboxTake (o);
	ck_assert_ptr_nonnull (list);
	ck_assert_ptr_nonnull (list->next);
	ck_assert_int_eq (list->next->key, BAR_WS_KEY_PROGRESS);
	ck_assert_uint_eq (list->next->generation, observed);
	ck_assert_uint_ne (list->next->generation, BarWsOutboxGeneration (o));
	BarWsPendingFree (list);
	BarWsOutboxFree (o);
}
END_TEST

START_TEST (test_song_start_advances_generation_even_when_full) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	for (size_t i = 0; i < WS_INGRESS_MAX_MSGS; i++) {
		ck_assert (push_text (o, 0, BAR_WS_KEY_NONE, "m"));
	}
	ck_assert (!BarWsOutboxTakeOverflow (o));
	ck_assert (!push_start (o, "start"));
	ck_assert_uint_eq (BarWsOutboxGeneration (o), 1);
	ck_assert (BarWsOutboxTakeOverflow (o));
	BarWsOutboxFree (o);
}
END_TEST

/* R3: every rejected push (room, size, memory) is recorded once for the
 * service thread; reading the flag clears it. */
START_TEST (test_overflow_flag_set_by_rejected_push_and_cleared_on_take) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	ck_assert (!BarWsOutboxTakeOverflow (o));
	for (size_t i = 0; i < WS_INGRESS_MAX_MSGS; i++) {
		ck_assert (push_text (o, 1, BAR_WS_KEY_NONE, "m"));
	}
	ck_assert (!BarWsOutboxTakeOverflow (o));
	ck_assert (!push_text (o, 1, BAR_WS_KEY_NONE, "m"));
	ck_assert (!BarWsOutboxPushProgress (o, 0, dup_text ("p"), 1));
	ck_assert (BarWsOutboxTakeOverflow (o));
	ck_assert (!BarWsOutboxTakeOverflow (o));
	BarWsPendingFree (BarWsOutboxTake (o));
	char *t = malloc (WS_FRAME_MAX_BYTES + 1);
	ck_assert_ptr_nonnull (t);
	memset (t, 'x', WS_FRAME_MAX_BYTES + 1);
	ck_assert (!BarWsOutboxPush (o, 1, BAR_WS_KEY_NONE, t, WS_FRAME_MAX_BYTES + 1));
	ck_assert (BarWsOutboxTakeOverflow (o));
	BarWsOutboxFree (o);
}
END_TEST

/* The completed play-state watermark lives under the outbox lock: an older
 * epoch is rejected (not an overflow), also after the newer one was taken. */
START_TEST (test_play_state_rejects_older_epoch_after_take) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	ck_assert (BarWsOutboxPushPlayState (o, 5, dup_text ("paused"), 6));
	ck_assert (push_text (o, 0, BAR_WS_KEY_NONE, "generic"));
	expect_taken (o, "paused,generic");
	ck_assert (!BarWsOutboxPushPlayState (o, 4, dup_text ("older"), 5));
	ck_assert (BarWsOutboxPushPlayState (o, 5, dup_text ("same"), 4));
	ck_assert (BarWsOutboxPushPlayState (o, 6, dup_text ("newer"), 5));
	expect_taken (o, "same,newer");
	ck_assert (!BarWsOutboxTakeOverflow (o));
	BarWsOutboxFree (o);
}
END_TEST

START_TEST (test_outbox_null_safe) {
	BarWsOutboxFree (NULL);
	BarWsPendingFree (NULL);
	ck_assert_ptr_null (BarWsOutboxTake (NULL));
	ck_assert_uint_eq (BarWsOutboxGeneration (NULL), 0);
	ck_assert (!BarWsOutboxTakeOverflow (NULL));
	ck_assert (!BarWsOutboxPush (NULL, 0, BAR_WS_KEY_NONE, dup_text ("x"), 1));
	ck_assert (!BarWsOutboxPushSongStart (NULL, dup_text ("x"), 1));
	ck_assert (!BarWsOutboxPushProgress (NULL, 0, dup_text ("x"), 1));
	ck_assert (!BarWsOutboxPushPlayState (NULL, 0, dup_text ("x"), 1));
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	ck_assert (!BarWsOutboxPush (o, 0, BAR_WS_KEY_NONE, NULL, 0));
	ck_assert (!BarWsOutboxPushSongStart (o, NULL, 0));
	ck_assert (!BarWsOutboxTakeOverflow (o));
	BarWsOutboxFree (o);
}
END_TEST

START_TEST (test_free_releases_pending) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	ck_assert (push_text (o, 0, BAR_WS_KEY_NONE, "left"));
	ck_assert (push_text (o, 0, BAR_WS_KEY_PROGRESS, "behind"));
	BarWsOutboxFree (o); /* leak detection runs under make test-asan */
}
END_TEST

#define CONC_PRODUCERS 4
#define CONC_PER_PRODUCER 2000

typedef struct {
	BarWsOutbox_t *o;
	int id;
	size_t accepted;
	pthread_mutex_t *gateLock;
	pthread_cond_t *gateCond;
	int *ready;
	bool *go;
} ConcProducer_t;

static void *conc_producer (void *arg) {
	ConcProducer_t *c = arg;
	/* barrier: all producers (and the consumer) start together */
	pthread_mutex_lock (c->gateLock);
	(*c->ready)++;
	pthread_cond_broadcast (c->gateCond);
	while (!*c->go) {
		pthread_cond_wait (c->gateCond, c->gateLock);
	}
	pthread_mutex_unlock (c->gateLock);

	for (int seq = 0; seq < CONC_PER_PRODUCER; seq++) {
		char buf[32];
		int n = snprintf (buf, sizeof (buf), "%d:%d", c->id, seq);
		char *text = malloc ((size_t) n + 1);
		ck_assert_ptr_nonnull (text);
		memcpy (text, buf, (size_t) n + 1);
		/* a full ingress means the consumer is behind; retry that same seq
		 * only after it drains (the module itself never retries) */
		while (!BarWsOutboxPush (c->o, 0, BAR_WS_KEY_NONE, text, (size_t) n)) {
			text = malloc ((size_t) n + 1);
			ck_assert_ptr_nonnull (text);
			memcpy (text, buf, (size_t) n + 1);
			sched_yield ();
		}
		c->accepted++;
	}
	return NULL;
}

START_TEST (test_concurrent_push_take_no_loss_no_leak) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	pthread_mutex_t gateLock = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t gateCond = PTHREAD_COND_INITIALIZER;
	int ready = 0;
	bool go = false;
	pthread_t th[CONC_PRODUCERS];
	ConcProducer_t pc[CONC_PRODUCERS];
	for (int i = 0; i < CONC_PRODUCERS; i++) {
		pc[i] = (ConcProducer_t) {o, i, 0, &gateLock, &gateCond, &ready, &go};
		ck_assert_int_eq (pthread_create (&th[i], NULL, conc_producer, &pc[i]), 0);
	}
	pthread_mutex_lock (&gateLock);
	while (ready < CONC_PRODUCERS) {
		pthread_cond_wait (&gateCond, &gateLock);
	}
	go = true;
	pthread_cond_broadcast (&gateCond);
	pthread_mutex_unlock (&gateLock);

	int next[CONC_PRODUCERS] = {0};
	size_t received = 0;
	const size_t total = (size_t) CONC_PRODUCERS * CONC_PER_PRODUCER;
	while (received < total) {
		BarWsPending_t *list = BarWsOutboxTake (o);
		if (!list) {
			sched_yield ();
			continue;
		}
		for (BarWsPending_t *p = list; p; p = p->next) {
			char buf[32] = {0};
			size_t len = BarWsFrameLen (p->frame);
			ck_assert_uint_lt (len, sizeof (buf));
			memcpy (buf, BarWsFrameData (p->frame), len);
			int id, seq;
			ck_assert_int_eq (sscanf (buf, "%d:%d", &id, &seq), 2);
			/* per-producer order preserved, nothing lost or duplicated */
			ck_assert_int_eq (seq, next[id]);
			next[id]++;
			received++;
		}
		BarWsPendingFree (list);
	}
	for (int i = 0; i < CONC_PRODUCERS; i++) {
		pthread_join (th[i], NULL);
		ck_assert_uint_eq (pc[i].accepted, CONC_PER_PRODUCER);
		ck_assert_int_eq (next[i], CONC_PER_PRODUCER);
	}
	ck_assert_ptr_null (BarWsOutboxTake (o));
	BarWsOutboxFree (o);
}
END_TEST

/* R1: a start and its generation are admitted together, so progress for the
 * new song can never be taken ahead of that song's start frame. The totals
 * stay under the ingress limit so nothing is rejected. */
#define R1_STARTS 40
#define R1_PROGRESS 80

typedef struct {
	BarWsOutbox_t *o;
	atomic_int *ready;
	atomic_bool *go;
	atomic_int *done;
} R1Arg_t;

static void r1_wait (R1Arg_t *a) {
	atomic_fetch_add (a->ready, 1);
	while (!atomic_load (a->go)) {
		sched_yield ();
	}
}

static void *r1_starter (void *arg) {
	R1Arg_t *a = arg;
	r1_wait (a);
	for (int i = 0; i < R1_STARTS; i++) {
		ck_assert (push_start (a->o, "s"));
		sched_yield ();
	}
	atomic_fetch_add (a->done, 1);
	return NULL;
}

static void *r1_progress (void *arg) {
	R1Arg_t *a = arg;
	r1_wait (a);
	for (int i = 0; i < R1_PROGRESS; i++) {
		uint32_t observed = BarWsOutboxGeneration (a->o);
		ck_assert (BarWsOutboxPushProgress (a->o, observed, dup_text ("p"), 1));
		sched_yield ();
	}
	atomic_fetch_add (a->done, 1);
	return NULL;
}

START_TEST (test_start_is_ordered_before_new_progress) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	atomic_int ready = 0, done = 0;
	atomic_bool go = false;
	R1Arg_t arg = {o, &ready, &go, &done};
	pthread_t starter, progress;
	ck_assert_int_eq (pthread_create (&starter, NULL, r1_starter, &arg), 0);
	ck_assert_int_eq (pthread_create (&progress, NULL, r1_progress, &arg), 0);
	while (atomic_load (&ready) < 2) {
		sched_yield ();
	}
	atomic_store (&go, true);

	/* Progress still unsent at a later start is discarded, so count starts
	 * only; stop once both producers finished and ingress is empty. */
	uint32_t startsSeen = 0;
	bool finished = false;
	while (!finished) {
		finished = atomic_load (&done) == 2;
		BarWsPending_t *list = BarWsOutboxTake (o);
		for (BarWsPending_t *p = list; p; p = p->next) {
			if (p->key == BAR_WS_KEY_PROGRESS) {
				/* generation g exists only after the g-th start was queued */
				ck_assert_uint_le (p->generation, startsSeen);
			} else {
				startsSeen++;
			}
		}
		BarWsPendingFree (list);
		sched_yield ();
	}
	pthread_join (starter, NULL);
	pthread_join (progress, NULL);
	ck_assert_uint_eq (startsSeen, R1_STARTS);
	ck_assert_uint_eq (BarWsOutboxGeneration (o), R1_STARTS);
	ck_assert (!BarWsOutboxTakeOverflow (o));
	BarWsOutboxFree (o);
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
	tcase_add_test (tc, test_replace_with_larger_frame_updates_bytes);
	tcase_add_test (tc, test_replace_with_smaller_frame_updates_bytes);
	tcase_add_test (tc, test_replace_over_byte_cap_rejected_original_intact);
	tcase_add_test (tc, test_replace_updates_generation);
	tcase_add_test (tc, test_keyed_self_replace_keeps_frame);
	tcase_add_test (tc, test_push_does_not_deliver_until_take);
	tcase_add_test (tc, test_ingress_full_returns_false_and_frees);
	tcase_add_test (tc, test_ingress_byte_cap_and_oversize_rejected);
	tcase_add_test (tc, test_progress_pushed_before_start_is_dropped_after_start);
	tcase_add_test (tc, test_start_discards_progress_at_head_and_tail_keeps_order);
	tcase_add_test (tc, test_progress_for_new_song_survives);
	tcase_add_test (tc, test_push_progress_keeps_observed_generation);
	tcase_add_test (tc, test_song_start_advances_generation_even_when_full);
	tcase_add_test (tc, test_overflow_flag_set_by_rejected_push_and_cleared_on_take);
	tcase_add_test (tc, test_play_state_rejects_older_epoch_after_take);
	tcase_add_test (tc, test_start_is_ordered_before_new_progress);
	tcase_add_test (tc, test_outbox_null_safe);
	tcase_add_test (tc, test_free_releases_pending);
	tcase_add_test (tc, test_concurrent_push_take_no_loss_no_leak);
	tcase_set_timeout (tc, 30);
	suite_add_tcase (s, tc);
	return s;
}
