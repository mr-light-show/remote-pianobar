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


#include "commands.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../bar_constants.h"

/* An empty queue must always accept one maximal request */
_Static_assert (WS_CMD_MAX_BYTES >= WS_CMD_MAX_REQUEST_BYTES,
		"command byte cap must fit one maximal request");

typedef struct BarWsCommandItem BarWsCommandItem_t;

struct BarWsCommandItem {
	BarWsConnId_t requester;
	char *bytes; /* owned, NUL-terminated copy */
	size_t len;
	BarWsCommandItem_t *next;
};

/* Lock discipline: one mutex (lock) guards every field except the item
 * payloads, which are immutable once queued and owned by exactly one party
 * (the queue, then the popper). Nothing is called out of this module while
 * the mutex is held except pthread_cond_wait on notEmpty (which releases it);
 * allocation and free of payloads happen outside the critical section. */
struct BarWsCommandQueue {
	pthread_mutex_t lock;
	pthread_cond_t notEmpty;
	BarWsCommandItem_t *head;
	BarWsCommandItem_t *tail;
	size_t count;
	size_t bytes;
	bool closed;
};

static void item_free (BarWsCommandItem_t *it) {
	free (it->bytes);
	free (it);
}

static void item_list_free (BarWsCommandItem_t *it) {
	while (it) {
		BarWsCommandItem_t *next = it->next;
		item_free (it);
		it = next;
	}
}

BarWsCommandQueue_t *BarWsCommandQueueNew (void) {
	BarWsCommandQueue_t *q = calloc (1, sizeof (*q));
	if (!q) {
		return NULL;
	}
	if (pthread_mutex_init (&q->lock, NULL) != 0) {
		free (q);
		return NULL;
	}
	if (pthread_cond_init (&q->notEmpty, NULL) != 0) {
		pthread_mutex_destroy (&q->lock);
		free (q);
		return NULL;
	}
	return q;
}

void BarWsCommandQueueFree (BarWsCommandQueue_t *q) {
	if (!q) {
		return;
	}
	item_list_free (q->head);
	pthread_cond_destroy (&q->notEmpty);
	pthread_mutex_destroy (&q->lock);
	free (q);
}

BarWsCommandResult_t BarWsCommandQueuePush (BarWsCommandQueue_t *q,
		BarWsConnId_t requester, const char *bytes, size_t len) {
	if (!q) {
		return BAR_WS_CMD_CLOSED;
	}
	if ((!bytes && len > 0) || len == SIZE_MAX) {
		return BAR_WS_CMD_NOMEM;
	}
	BarWsCommandItem_t *it = calloc (1, sizeof (*it));
	char *copy = malloc (len + 1);
	if (!it || !copy) {
		free (it);
		free (copy);
		return BAR_WS_CMD_NOMEM;
	}
	if (len > 0) {
		memcpy (copy, bytes, len);
	}
	copy[len] = '\0';
	it->requester = requester;
	it->bytes = copy;
	it->len = len;

	BarWsCommandResult_t result = BAR_WS_CMD_OK;
	pthread_mutex_lock (&q->lock);
	if (q->closed) {
		result = BAR_WS_CMD_CLOSED;
	} else if (q->count >= WS_CMD_MAX_ITEMS || len > WS_CMD_MAX_BYTES - q->bytes) {
		result = BAR_WS_CMD_FULL;
	} else {
		if (q->tail) {
			q->tail->next = it;
		} else {
			q->head = it;
		}
		q->tail = it;
		q->count++;
		q->bytes += len;
		pthread_cond_signal (&q->notEmpty);
	}
	pthread_mutex_unlock (&q->lock);
	if (result != BAR_WS_CMD_OK) {
		item_free (it);
	}
	return result;
}

bool BarWsCommandQueuePop (BarWsCommandQueue_t *q, BarWsConnId_t *requester,
		char **bytes, size_t *len) {
	if (!q) {
		return false;
	}
	pthread_mutex_lock (&q->lock);
	while (!q->closed && !q->head) {
		pthread_cond_wait (&q->notEmpty, &q->lock);
	}
	if (q->closed) {
		pthread_mutex_unlock (&q->lock);
		return false;
	}
	BarWsCommandItem_t *it = q->head;
	q->head = it->next;
	if (!q->head) {
		q->tail = NULL;
	}
	q->count--;
	q->bytes -= it->len;
	pthread_mutex_unlock (&q->lock);

	if (requester) {
		*requester = it->requester;
	}
	if (len) {
		*len = it->len;
	}
	if (bytes) {
		*bytes = it->bytes;
		it->bytes = NULL; /* ownership moves to the caller */
	}
	item_free (it);
	return true;
}

void BarWsCommandQueueClose (BarWsCommandQueue_t *q) {
	if (!q) {
		return;
	}
	pthread_mutex_lock (&q->lock);
	q->closed = true;
	BarWsCommandItem_t *dropped = q->head;
	q->head = NULL;
	q->tail = NULL;
	q->count = 0;
	q->bytes = 0;
	pthread_cond_broadcast (&q->notEmpty);
	pthread_mutex_unlock (&q->lock);
	item_list_free (dropped);
}
