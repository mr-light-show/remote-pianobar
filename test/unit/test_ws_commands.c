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
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/bar_constants.h"
#include "../../src/websocket/core/commands.h"

/* Push a NUL-terminated string (without its terminator) */
static BarWsCommandResult_t push_str (BarWsCommandQueue_t *q, BarWsConnId_t id,
		const char *s) {
	return BarWsCommandQueuePush (q, id, s, strlen (s));
}

/* Pop one item and assert its requester and payload */
static void expect_pop (BarWsCommandQueue_t *q, BarWsConnId_t id, const char *s) {
	BarWsConnId_t got = 0;
	char *bytes = NULL;
	size_t len = 0;
	ck_assert (BarWsCommandQueuePop (q, &got, &bytes, &len));
	ck_assert_uint_eq (got, id);
	ck_assert_uint_eq (len, strlen (s));
	ck_assert_str_eq (bytes, s);
	free (bytes);
}

START_TEST (test_cmd_fifo_order) {
	BarWsCommandQueue_t *q = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (q);
	ck_assert_int_eq (push_str (q, 7, "one"), BAR_WS_CMD_OK);
	ck_assert_int_eq (push_str (q, 8, "two"), BAR_WS_CMD_OK);
	ck_assert_int_eq (push_str (q, 7, "three"), BAR_WS_CMD_OK);
	expect_pop (q, 7, "one");
	expect_pop (q, 8, "two");
	expect_pop (q, 7, "three");
	BarWsCommandQueueFree (q);
}
END_TEST

START_TEST (test_cmd_copies_input) {
	BarWsCommandQueue_t *q = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (q);
	char *buf = strdup ("{\"action\":\"x\"}");
	ck_assert_ptr_nonnull (buf);
	size_t n = strlen (buf);
	ck_assert_int_eq (BarWsCommandQueuePush (q, 3, buf, n), BAR_WS_CMD_OK);
	memset (buf, 'Z', n);
	free (buf);
	expect_pop (q, 3, "{\"action\":\"x\"}");
	BarWsCommandQueueFree (q);
}
END_TEST

START_TEST (test_cmd_item_limit_returns_full) {
	BarWsCommandQueue_t *q = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (q);
	for (int i = 0; i < WS_CMD_MAX_ITEMS; i++) {
		ck_assert_int_eq (push_str (q, 1, "x"), BAR_WS_CMD_OK);
	}
	ck_assert_int_eq (push_str (q, 1, "x"), BAR_WS_CMD_FULL);
	/* popping one makes room again */
	expect_pop (q, 1, "x");
	ck_assert_int_eq (push_str (q, 1, "x"), BAR_WS_CMD_OK);
	BarWsCommandQueueFree (q);
}
END_TEST

START_TEST (test_cmd_byte_limit_returns_full) {
	BarWsCommandQueue_t *q = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (q);
	/* An empty queue accepts one maximal request ... */
	char *big = malloc (WS_CMD_MAX_REQUEST_BYTES);
	ck_assert_ptr_nonnull (big);
	memset (big, 'a', WS_CMD_MAX_REQUEST_BYTES);
	int accepted = 0;
	/* ... and the byte cap, not the item cap, stops further ones */
	while (BarWsCommandQueuePush (q, 1, big, WS_CMD_MAX_REQUEST_BYTES) == BAR_WS_CMD_OK) {
		accepted++;
		ck_assert_int_le (accepted, WS_CMD_MAX_ITEMS);
	}
	ck_assert_int_eq (accepted, WS_CMD_MAX_BYTES / WS_CMD_MAX_REQUEST_BYTES);
	ck_assert_int_lt (accepted, WS_CMD_MAX_ITEMS);
	ck_assert_int_eq (BarWsCommandQueuePush (q, 1, big, WS_CMD_MAX_REQUEST_BYTES),
			BAR_WS_CMD_FULL);
	/* a single byte over the remaining room is refused, an exact fit is not */
	BarWsConnId_t id;
	char *bytes;
	size_t len;
	ck_assert (BarWsCommandQueuePop (q, &id, &bytes, &len));
	free (bytes);
	ck_assert_int_eq (BarWsCommandQueuePush (q, 1, big, WS_CMD_MAX_REQUEST_BYTES),
			BAR_WS_CMD_OK);
	/* with exactly one byte of room left, two bytes are refused, one fits */
	ck_assert (BarWsCommandQueuePop (q, &id, &bytes, &len));
	free (bytes);
	ck_assert_int_eq (BarWsCommandQueuePush (q, 1, big, WS_CMD_MAX_REQUEST_BYTES - 1),
			BAR_WS_CMD_OK);
	ck_assert_int_eq (BarWsCommandQueuePush (q, 1, big, 2), BAR_WS_CMD_FULL);
	ck_assert_int_eq (BarWsCommandQueuePush (q, 1, big, 1), BAR_WS_CMD_OK);
	ck_assert_int_eq (BarWsCommandQueuePush (q, 1, big, 1), BAR_WS_CMD_FULL);
	free (big);
	BarWsCommandQueueFree (q);
}
END_TEST

/* A request larger than the whole byte cap never fits, even when empty */
START_TEST (test_cmd_request_over_byte_cap_on_empty_queue_returns_full) {
	BarWsCommandQueue_t *q = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (q);
	char *huge = malloc (WS_CMD_MAX_BYTES + 1);
	ck_assert_ptr_nonnull (huge);
	memset (huge, 'h', WS_CMD_MAX_BYTES + 1);
	ck_assert_int_eq (BarWsCommandQueuePush (q, 1, huge, WS_CMD_MAX_BYTES + 1),
			BAR_WS_CMD_FULL);
	/* the refused request left nothing behind: the cap itself still fits */
	ck_assert_int_eq (BarWsCommandQueuePush (q, 1, huge, WS_CMD_MAX_BYTES),
			BAR_WS_CMD_OK);
	ck_assert_int_eq (BarWsCommandQueuePush (q, 1, huge, 1), BAR_WS_CMD_FULL);
	free (huge);
	BarWsCommandQueueFree (q);
}
END_TEST

START_TEST (test_cmd_push_after_close_returns_closed) {
	BarWsCommandQueue_t *q = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (q);
	BarWsCommandQueueClose (q);
	ck_assert_int_eq (push_str (q, 1, "x"), BAR_WS_CMD_CLOSED);
	/* distinct from FULL, and Close is idempotent */
	BarWsCommandQueueClose (q);
	ck_assert_int_eq (push_str (q, 1, "x"), BAR_WS_CMD_CLOSED);
	BarWsCommandQueueFree (q);
}
END_TEST

typedef struct {
	BarWsCommandQueue_t *q;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool started;
	atomic_int popResult; /* -1 until Pop returned, then 0/1 */
} PopWaiter_t;

static void *pop_waiter (void *arg) {
	PopWaiter_t *w = arg;
	pthread_mutex_lock (&w->lock);
	w->started = true;
	pthread_cond_signal (&w->cond);
	pthread_mutex_unlock (&w->lock);
	BarWsConnId_t id = 0;
	char *bytes = NULL;
	size_t len = 0;
	bool ok = BarWsCommandQueuePop (w->q, &id, &bytes, &len);
	free (bytes);
	atomic_store (&w->popResult, ok ? 1 : 0);
	return NULL;
}

START_TEST (test_cmd_close_wakes_blocked_pop) {
	PopWaiter_t w = { .q = BarWsCommandQueueNew (), .started = false };
	ck_assert_ptr_nonnull (w.q);
	atomic_init (&w.popResult, -1);
	pthread_mutex_init (&w.lock, NULL);
	pthread_cond_init (&w.cond, NULL);
	pthread_t t;
	ck_assert_int_eq (pthread_create (&t, NULL, pop_waiter, &w), 0);
	/* Handshake: the consumer is running and about to (or already) block in
	 * Pop. Close must release it whichever side wins the race. */
	pthread_mutex_lock (&w.lock);
	while (!w.started) {
		pthread_cond_wait (&w.cond, &w.lock);
	}
	pthread_mutex_unlock (&w.lock);
	BarWsCommandQueueClose (w.q);
	pthread_join (t, NULL);
	ck_assert_int_eq (atomic_load (&w.popResult), 0);
	/* later Pops do not block either */
	ck_assert (!BarWsCommandQueuePop (w.q, NULL, NULL, NULL));
	BarWsCommandQueueFree (w.q);
	pthread_cond_destroy (&w.cond);
	pthread_mutex_destroy (&w.lock);
}
END_TEST

/* Close must wake every blocked consumer (broadcast, not signal). Each
 * round lets both waiters reach Pop before closing; with a single signal the
 * second waiter would stay blocked and the join would hang (test timeout). */
START_TEST (test_cmd_close_wakes_two_blocked_pops) {
	for (int round = 0; round < 20; round++) {
		BarWsCommandQueue_t *q = BarWsCommandQueueNew ();
		ck_assert_ptr_nonnull (q);
		PopWaiter_t w[2];
		pthread_t t[2];
		for (int i = 0; i < 2; i++) {
			w[i].q = q;
			w[i].started = false;
			atomic_init (&w[i].popResult, -1);
			pthread_mutex_init (&w[i].lock, NULL);
			pthread_cond_init (&w[i].cond, NULL);
			ck_assert_int_eq (pthread_create (&t[i], NULL, pop_waiter, &w[i]), 0);
		}
		for (int i = 0; i < 2; i++) {
			pthread_mutex_lock (&w[i].lock);
			while (!w[i].started) {
				pthread_cond_wait (&w[i].cond, &w[i].lock);
			}
			pthread_mutex_unlock (&w[i].lock);
		}
		/* Give both a chance to block in Pop; the outcome does not depend on
		 * it, only how strongly a signal-only Close is caught does. */
		for (int i = 0; i < 1000; i++) {
			sched_yield ();
		}
		BarWsCommandQueueClose (q);
		for (int i = 0; i < 2; i++) {
			pthread_join (t[i], NULL);
			ck_assert_int_eq (atomic_load (&w[i].popResult), 0);
			pthread_cond_destroy (&w[i].cond);
			pthread_mutex_destroy (&w[i].lock);
		}
		BarWsCommandQueueFree (q);
	}
}
END_TEST

START_TEST (test_cmd_pop_blocks_until_push) {
	PopWaiter_t w = { .q = BarWsCommandQueueNew (), .started = false };
	ck_assert_ptr_nonnull (w.q);
	atomic_init (&w.popResult, -1);
	pthread_mutex_init (&w.lock, NULL);
	pthread_cond_init (&w.cond, NULL);
	pthread_t t;
	ck_assert_int_eq (pthread_create (&t, NULL, pop_waiter, &w), 0);
	pthread_mutex_lock (&w.lock);
	while (!w.started) {
		pthread_cond_wait (&w.cond, &w.lock);
	}
	pthread_mutex_unlock (&w.lock);
	ck_assert_int_eq (push_str (w.q, 1, "wake"), BAR_WS_CMD_OK);
	pthread_join (t, NULL);
	ck_assert_int_eq (atomic_load (&w.popResult), 1);
	BarWsCommandQueueFree (w.q);
	pthread_cond_destroy (&w.cond);
	pthread_mutex_destroy (&w.lock);
}
END_TEST

START_TEST (test_cmd_close_frees_queued) {
	BarWsCommandQueue_t *q = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (q);
	ck_assert_int_eq (push_str (q, 1, "a"), BAR_WS_CMD_OK);
	ck_assert_int_eq (push_str (q, 2, "b"), BAR_WS_CMD_OK);
	BarWsCommandQueueClose (q);
	/* dropped, not delivered; leak detection runs under make test-asan */
	BarWsConnId_t id = 99;
	char *bytes = NULL;
	ck_assert (!BarWsCommandQueuePop (q, &id, &bytes, NULL));
	ck_assert_uint_eq (id, 99);
	ck_assert_ptr_null (bytes);
	BarWsCommandQueueFree (q);
}
END_TEST

START_TEST (test_cmd_free_releases_queued) {
	BarWsCommandQueue_t *q = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (q);
	ck_assert_int_eq (push_str (q, 1, "a"), BAR_WS_CMD_OK);
	ck_assert_int_eq (push_str (q, 2, "b"), BAR_WS_CMD_OK);
	BarWsCommandQueueFree (q); /* leak detection under make test-asan */
}
END_TEST

START_TEST (test_cmd_nomem_returns_nomem) {
	BarWsCommandQueue_t *q = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (q);
	/* len + 1 (for the terminator) is not representable: the copy cannot be
	 * allocated. The bytes are never read. */
	ck_assert_int_eq (BarWsCommandQueuePush (q, 1, "x", SIZE_MAX), BAR_WS_CMD_NOMEM);
	/* invalid arguments queue nothing */
	ck_assert_int_eq (BarWsCommandQueuePush (q, 1, NULL, 4), BAR_WS_CMD_NOMEM);
	ck_assert_int_eq (BarWsCommandQueuePush (NULL, 1, "x", 1), BAR_WS_CMD_CLOSED);
	/* the queue is still usable and empty */
	ck_assert_int_eq (push_str (q, 5, "ok"), BAR_WS_CMD_OK);
	expect_pop (q, 5, "ok");
	BarWsCommandQueueFree (q);
}
END_TEST

START_TEST (test_cmd_empty_and_null_safe) {
	BarWsCommandQueue_t *q = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (q);
	/* a zero-length request is a valid (empty) item */
	ck_assert_int_eq (BarWsCommandQueuePush (q, 4, NULL, 0), BAR_WS_CMD_OK);
	ck_assert (BarWsCommandQueuePop (q, NULL, NULL, NULL)); /* discards */
	ck_assert_int_eq (push_str (q, 4, "x"), BAR_WS_CMD_OK);
	char *bytes = NULL;
	ck_assert (BarWsCommandQueuePop (q, NULL, &bytes, NULL));
	ck_assert_str_eq (bytes, "x");
	free (bytes);
	BarWsCommandQueueFree (q);
	BarWsCommandQueueFree (NULL);
	BarWsCommandQueueClose (NULL);
	ck_assert (!BarWsCommandQueuePop (NULL, NULL, NULL, NULL));
}
END_TEST

#define STRESS_PRODUCERS 4
#define STRESS_PER_PRODUCER 3000

typedef struct {
	BarWsCommandQueue_t *q;
	unsigned id; /* 1-based requester */
} StressProducer_t;

static void *stress_producer (void *arg) {
	StressProducer_t *p = arg;
	for (unsigned i = 0; i < STRESS_PER_PRODUCER; i++) {
		char buf[32];
		int n = snprintf (buf, sizeof (buf), "%u", i);
		BarWsCommandResult_t r;
		while ((r = BarWsCommandQueuePush (p->q, p->id, buf, (size_t) n)) ==
				BAR_WS_CMD_FULL) {
			sched_yield ();
		}
		if (r != BAR_WS_CMD_OK) {
			return (void *) 1;
		}
	}
	return NULL;
}

START_TEST (test_cmd_stress_fifo_per_producer_no_loss) {
	BarWsCommandQueue_t *q = BarWsCommandQueueNew ();
	ck_assert_ptr_nonnull (q);
	pthread_t th[STRESS_PRODUCERS];
	StressProducer_t pr[STRESS_PRODUCERS];
	for (unsigned i = 0; i < STRESS_PRODUCERS; i++) {
		pr[i].q = q;
		pr[i].id = i + 1;
		ck_assert_int_eq (pthread_create (&th[i], NULL, stress_producer, &pr[i]), 0);
	}
	unsigned next[STRESS_PRODUCERS] = { 0 };
	for (unsigned got = 0; got < STRESS_PRODUCERS * STRESS_PER_PRODUCER; got++) {
		BarWsConnId_t id;
		char *bytes;
		size_t len;
		ck_assert (BarWsCommandQueuePop (q, &id, &bytes, &len));
		ck_assert_uint_ge (id, 1);
		ck_assert_uint_le (id, STRESS_PRODUCERS);
		char want[32];
		snprintf (want, sizeof (want), "%u", next[id - 1]);
		ck_assert_uint_eq (len, strlen (want));
		ck_assert_str_eq (bytes, want); /* FIFO per producer, no duplicates */
		next[id - 1]++;
		free (bytes);
	}
	for (unsigned i = 0; i < STRESS_PRODUCERS; i++) {
		void *rc = (void *) 1;
		pthread_join (th[i], &rc);
		ck_assert_ptr_null (rc);
		ck_assert_uint_eq (next[i], STRESS_PER_PRODUCER); /* no loss */
	}
	/* nothing extra was queued: Close then Pop returns false immediately */
	BarWsCommandQueueClose (q);
	ck_assert (!BarWsCommandQueuePop (q, NULL, NULL, NULL));
	BarWsCommandQueueFree (q); /* leak detection under make test-asan */
}
END_TEST

Suite *ws_commands_suite (void) {
	Suite *s = suite_create ("ws_commands");
	TCase *tc = tcase_create ("Core");
	tcase_add_test (tc, test_cmd_fifo_order);
	tcase_add_test (tc, test_cmd_copies_input);
	tcase_add_test (tc, test_cmd_item_limit_returns_full);
	tcase_add_test (tc, test_cmd_byte_limit_returns_full);
	tcase_add_test (tc, test_cmd_request_over_byte_cap_on_empty_queue_returns_full);
	tcase_add_test (tc, test_cmd_push_after_close_returns_closed);
	tcase_add_test (tc, test_cmd_close_wakes_blocked_pop);
	tcase_add_test (tc, test_cmd_close_wakes_two_blocked_pops);
	tcase_add_test (tc, test_cmd_pop_blocks_until_push);
	tcase_add_test (tc, test_cmd_close_frees_queued);
	tcase_add_test (tc, test_cmd_free_releases_queued);
	tcase_add_test (tc, test_cmd_nomem_returns_nomem);
	tcase_add_test (tc, test_cmd_empty_and_null_safe);
	tcase_add_test (tc, test_cmd_stress_fifo_per_producer_no_loss);
	tcase_set_timeout (tc, 30);
	suite_add_tcase (s, tc);
	return s;
}
