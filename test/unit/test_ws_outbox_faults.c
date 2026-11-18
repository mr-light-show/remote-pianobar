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

/* Allocation-failure and race coverage for the WebSocket outbox.
 *
 * outbox.c is compiled into this file a second time with its externally
 * visible names prefixed (Fi*) and malloc/calloc/pthread_mutex_init routed
 * through failure-injection wrappers, so out-of-memory and lost-race paths
 * run against the real implementation without test hooks in production code.
 * The ordinary outbox tests keep using the unmodified outbox.o. */

#include <check.h>
#include <libwebsockets.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/bar_constants.h"
#include "../../src/log.h"

/* Failure injection: the allocation after fi_countdown more successful ones
 * either fails (no hook) or runs fi_hook once and then succeeds. -1 = off. */
static int fi_countdown = -1;
static void (*fi_hook) (void *);
static void *fi_hook_arg;
static bool fi_mutex_fail;

static void FiArm (int allocationsBefore, void (*hook) (void *), void *arg) {
	fi_countdown = allocationsBefore;
	fi_hook = hook;
	fi_hook_arg = arg;
}

/* True if this allocation must fail */
static bool FiShouldFail (void) {
	if (fi_countdown < 0) {
		return false;
	}
	if (fi_countdown > 0) {
		fi_countdown--;
		return false;
	}
	fi_countdown = -1;
	if (fi_hook) {
		void (*hook) (void *) = fi_hook;
		fi_hook = NULL;
		hook (fi_hook_arg);
		return false;
	}
	return true;
}

static void *FiMalloc (size_t n) {
	return FiShouldFail () ? NULL : malloc (n);
}

static void *FiCalloc (size_t n, size_t size) {
	return FiShouldFail () ? NULL : calloc (n, size);
}

static int FiMutexInit (pthread_mutex_t *m, const pthread_mutexattr_t *attr) {
	return fi_mutex_fail ? EAGAIN : pthread_mutex_init (m, attr);
}

#define malloc(n) FiMalloc (n)
#define calloc(n, size) FiCalloc (n, size)
#define pthread_mutex_init(m, attr) FiMutexInit (m, attr)

#define BarWsFrameNew FiBarWsFrameNew
#define BarWsFrameRef FiBarWsFrameRef
#define BarWsFrameUnref FiBarWsFrameUnref
#define BarWsFrameData FiBarWsFrameData
#define BarWsFrameLen FiBarWsFrameLen
#define BarWsClientQueueInit FiBarWsClientQueueInit
#define BarWsClientQueueClear FiBarWsClientQueueClear
#define BarWsClientQueueAppend FiBarWsClientQueueAppend
#define BarWsClientQueuePop FiBarWsClientQueuePop
#define BarWsOutboxNew FiBarWsOutboxNew
#define BarWsPendingFree FiBarWsPendingFree
#define BarWsOutboxFree FiBarWsOutboxFree
#define BarWsOutboxPush FiBarWsOutboxPush
#define BarWsOutboxPushProgress FiBarWsOutboxPushProgress
#define BarWsOutboxPushSongStart FiBarWsOutboxPushSongStart
#define BarWsOutboxPushPlayState FiBarWsOutboxPushPlayState
#define BarWsOutboxTake FiBarWsOutboxTake
#define BarWsOutboxTakeOverflow FiBarWsOutboxTakeOverflow
#define BarWsOutboxGeneration FiBarWsOutboxGeneration
#define BarWsOutboxAtHighWater FiBarWsOutboxAtHighWater

#include "../../src/websocket/core/outbox.c"

static void fill_ingress (BarWsOutbox_t *o, size_t n) {
	for (size_t i = 0; i < n; i++) {
		ck_assert (BarWsOutboxPush (o, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
				strdup ("x"), 1));
	}
}

static size_t pending_len (BarWsPending_t *list) {
	size_t n = 0;
	for (const BarWsPending_t *p = list; p; p = p->next) {
		n++;
	}
	return n;
}

/* Hook: another producer takes the last ingress slot mid-push */
static void hook_take_last_slot (void *arg) {
	ck_assert (BarWsOutboxPush ((BarWsOutbox_t *) arg, BAR_WS_DEST_BROADCAST,
			BAR_WS_KEY_NONE, strdup ("rival"), 5));
}

START_TEST (test_frame_new_rejects_invalid_arguments) {
	ck_assert_ptr_null (BarWsFrameNew (NULL, 3));
	ck_assert_ptr_null (BarWsFrameNew ("x", WS_FRAME_MAX_BYTES + 1));
	BarWsFrame_t *empty = BarWsFrameNew (NULL, 0);
	ck_assert_ptr_nonnull (empty);
	ck_assert_uint_eq (BarWsFrameLen (empty), 0);
	BarWsFrameUnref (empty);
}
END_TEST

START_TEST (test_frame_null_accessors_are_safe) {
	ck_assert_ptr_null (BarWsFrameRef (NULL));
	BarWsFrameUnref (NULL);
	ck_assert_ptr_null (BarWsFrameData (NULL));
	ck_assert_uint_eq (BarWsFrameLen (NULL), 0);
}
END_TEST

START_TEST (test_frame_new_returns_null_when_out_of_memory) {
	FiArm (0, NULL, NULL);
	ck_assert_ptr_null (BarWsFrameNew ("abc", 3));
	/* The injection is one-shot: the next allocation succeeds */
	BarWsFrame_t *f = BarWsFrameNew ("abc", 3);
	ck_assert_ptr_nonnull (f);
	ck_assert_int_eq (memcmp (BarWsFrameData (f), "abc", 3), 0);
	BarWsFrameUnref (f);
}
END_TEST

START_TEST (test_client_queue_append_reports_overflow_when_entry_alloc_fails) {
	BarWsClientQueue_t q;
	BarWsClientQueueInit (&q);
	BarWsFrame_t *f = BarWsFrameNew ("abc", 3);
	ck_assert_ptr_nonnull (f);
	FiArm (0, NULL, NULL);
	ck_assert_int_eq (BarWsClientQueueAppend (&q, f, BAR_WS_KEY_NONE, 0),
			BAR_WS_Q_OVERFLOW);
	ck_assert_uint_eq (q.count, 0);
	ck_assert_uint_eq (q.bytes, 0);
	ck_assert_ptr_null (q.head);
	/* No reference was taken for the failed append */
	ck_assert_uint_eq (atomic_load (&f->refs), 1);
	ck_assert_int_eq (BarWsClientQueueAppend (&q, f, BAR_WS_KEY_NONE, 0), BAR_WS_Q_OK);
	ck_assert_uint_eq (atomic_load (&f->refs), 2);
	BarWsClientQueueClear (&q);
	BarWsFrameUnref (f);
}
END_TEST

START_TEST (test_outbox_new_fails_when_calloc_fails) {
	FiArm (0, NULL, NULL);
	ck_assert_ptr_null (BarWsOutboxNew ());
}
END_TEST

START_TEST (test_outbox_new_fails_when_mutex_init_fails) {
	fi_mutex_fail = true;
	BarWsOutbox_t *o = BarWsOutboxNew ();
	fi_mutex_fail = false;
	ck_assert_ptr_null (o);
	o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	BarWsOutboxFree (o);
}
END_TEST

START_TEST (test_push_frame_alloc_failure_flags_overflow_and_queues_nothing) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	FiArm (0, NULL, NULL);
	ck_assert (!BarWsOutboxPush (o, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
			strdup ("lost"), 4));
	ck_assert (BarWsOutboxTakeOverflow (o));
	ck_assert_ptr_null (BarWsOutboxTake (o));
	ck_assert (BarWsOutboxPush (o, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
			strdup ("ok"), 2));
	BarWsPending_t *list = BarWsOutboxTake (o);
	ck_assert_uint_eq (pending_len (list), 1);
	BarWsPendingFree (list);
	BarWsOutboxFree (o);
}
END_TEST

START_TEST (test_push_node_alloc_failure_flags_overflow_and_releases_frame) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	/* The frame allocates, the pending node after it does not */
	FiArm (1, NULL, NULL);
	ck_assert (!BarWsOutboxPush (o, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_VOLUME,
			strdup ("lost"), 4));
	ck_assert (BarWsOutboxTakeOverflow (o));
	ck_assert_ptr_null (BarWsOutboxTake (o));
	BarWsOutboxFree (o);
}
END_TEST

START_TEST (test_push_progress_alloc_failure_flags_overflow) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	FiArm (0, NULL, NULL);
	ck_assert (!BarWsOutboxPushProgress (o, 0, strdup ("p"), 1));
	ck_assert (BarWsOutboxTakeOverflow (o));
	BarWsOutboxFree (o);
}
END_TEST

START_TEST (test_push_loses_race_for_last_ingress_slot) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	fill_ingress (o, WS_INGRESS_MAX_MSGS - 1);
	/* Room at the early check; a rival producer fills it while this push
	 * allocates its frame, so the append under the lock must fail */
	FiArm (0, hook_take_last_slot, o);
	ck_assert (!BarWsOutboxPush (o, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
			strdup ("late"), 4));
	ck_assert (BarWsOutboxTakeOverflow (o));
	BarWsPending_t *list = BarWsOutboxTake (o);
	ck_assert_uint_eq (pending_len (list), WS_INGRESS_MAX_MSGS);
	const BarWsPending_t *last = list;
	while (last->next) {
		last = last->next;
	}
	ck_assert_uint_eq (BarWsFrameLen (last->frame), 5); /* the rival's frame */
	BarWsPendingFree (list);
	BarWsOutboxFree (o);
}
END_TEST

START_TEST (test_song_start_alloc_failure_flags_overflow_but_advances_generation) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	ck_assert (BarWsOutboxPushProgress (o, 0, strdup ("p"), 1));
	FiArm (0, NULL, NULL);
	ck_assert (!BarWsOutboxPushSongStart (o, strdup ("start"), 5));
	ck_assert_uint_eq (BarWsOutboxGeneration (o), 1);
	ck_assert (BarWsOutboxTakeOverflow (o));
	/* The stale progress was still discarded */
	ck_assert_ptr_null (BarWsOutboxTake (o));
	BarWsOutboxFree (o);
}
END_TEST

START_TEST (test_song_start_null_and_oversize_do_not_flag_overflow) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	ck_assert (!BarWsOutboxPushSongStart (NULL, strdup ("s"), 1));
	ck_assert (!BarWsOutboxPushSongStart (o, NULL, 1));
	char *big = malloc (WS_FRAME_MAX_BYTES + 1);
	ck_assert_ptr_nonnull (big);
	memset (big, 'a', WS_FRAME_MAX_BYTES);
	big[WS_FRAME_MAX_BYTES] = '\0';
	ck_assert (!BarWsOutboxPushSongStart (o, big, WS_FRAME_MAX_BYTES + 1));
	ck_assert (!BarWsOutboxTakeOverflow (o));
	ck_assert_uint_eq (BarWsOutboxGeneration (o), 1);
	BarWsOutboxFree (o);
}
END_TEST

START_TEST (test_song_start_loses_race_for_last_ingress_slot) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	fill_ingress (o, WS_INGRESS_MAX_MSGS - 1);
	FiArm (0, hook_take_last_slot, o);
	ck_assert (!BarWsOutboxPushSongStart (o, strdup ("start"), 5));
	ck_assert (BarWsOutboxTakeOverflow (o));
	ck_assert_uint_eq (BarWsOutboxGeneration (o), 1);
	BarWsPending_t *list = BarWsOutboxTake (o);
	ck_assert_uint_eq (pending_len (list), WS_INGRESS_MAX_MSGS);
	BarWsPendingFree (list);
	BarWsOutboxFree (o);
}
END_TEST

START_TEST (test_play_state_rejects_null_and_oversize_arguments) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	ck_assert (!BarWsOutboxPushPlayState (NULL, 1, strdup ("s"), 1));
	ck_assert (!BarWsOutboxPushPlayState (o, 1, NULL, 1));
	char *big = malloc (WS_FRAME_MAX_BYTES + 1);
	ck_assert_ptr_nonnull (big);
	memset (big, 'a', WS_FRAME_MAX_BYTES);
	big[WS_FRAME_MAX_BYTES] = '\0';
	ck_assert (!BarWsOutboxPushPlayState (o, 1, big, WS_FRAME_MAX_BYTES + 1));
	ck_assert (!BarWsOutboxTakeOverflow (o));
	/* None of those advanced the epoch watermark */
	ck_assert (BarWsOutboxPushPlayState (o, 0, strdup ("s"), 1));
	BarWsPendingFree (BarWsOutboxTake (o));
	BarWsOutboxFree (o);
}
END_TEST

START_TEST (test_play_state_alloc_failure_flags_overflow_and_keeps_epoch) {
	BarWsOutbox_t *o = BarWsOutboxNew ();
	ck_assert_ptr_nonnull (o);
	FiArm (0, NULL, NULL);
	ck_assert (!BarWsOutboxPushPlayState (o, 5, strdup ("s5"), 2));
	ck_assert (BarWsOutboxTakeOverflow (o));
	ck_assert_ptr_null (BarWsOutboxTake (o));
	/* Epoch 5 was accepted although its frame was lost: older is refused,
	 * the same or newer epoch is queued */
	ck_assert (!BarWsOutboxPushPlayState (o, 4, strdup ("s4"), 2));
	ck_assert (!BarWsOutboxTakeOverflow (o));
	ck_assert (BarWsOutboxPushPlayState (o, 5, strdup ("s5"), 2));
	ck_assert (BarWsOutboxPushPlayState (o, 6, strdup ("s6"), 2));
	BarWsPending_t *list = BarWsOutboxTake (o);
	ck_assert_uint_eq (pending_len (list), 2);
	BarWsPendingFree (list);
	BarWsOutboxFree (o);
}
END_TEST

Suite *ws_outbox_faults_suite (void);
Suite *ws_outbox_faults_suite (void) {
	Suite *s = suite_create ("ws_outbox_faults");
	TCase *tc = tcase_create ("Core");
	tcase_add_test (tc, test_frame_new_rejects_invalid_arguments);
	tcase_add_test (tc, test_frame_null_accessors_are_safe);
	tcase_add_test (tc, test_frame_new_returns_null_when_out_of_memory);
	tcase_add_test (tc, test_client_queue_append_reports_overflow_when_entry_alloc_fails);
	tcase_add_test (tc, test_outbox_new_fails_when_calloc_fails);
	tcase_add_test (tc, test_outbox_new_fails_when_mutex_init_fails);
	tcase_add_test (tc, test_push_frame_alloc_failure_flags_overflow_and_queues_nothing);
	tcase_add_test (tc, test_push_node_alloc_failure_flags_overflow_and_releases_frame);
	tcase_add_test (tc, test_push_progress_alloc_failure_flags_overflow);
	tcase_add_test (tc, test_push_loses_race_for_last_ingress_slot);
	tcase_add_test (tc, test_song_start_alloc_failure_flags_overflow_but_advances_generation);
	tcase_add_test (tc, test_song_start_null_and_oversize_do_not_flag_overflow);
	tcase_add_test (tc, test_song_start_loses_race_for_last_ingress_slot);
	tcase_add_test (tc, test_play_state_rejects_null_and_oversize_arguments);
	tcase_add_test (tc, test_play_state_alloc_failure_flags_overflow_and_keeps_epoch);
	suite_add_tcase (s, tc);
	return s;
}
