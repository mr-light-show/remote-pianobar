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

#include "outbox.h"
#include "../../bar_constants.h"

#include <libwebsockets.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

struct BarWsFrame {
	atomic_uint refs;
	size_t len;
	unsigned char buf[]; /* LWS_PRE padding, then len payload bytes */
};

struct BarWsQueueEntry {
	BarWsFrame_t *frame;
	BarWsKey_t key;
	uint32_t generation;
	BarWsQueueEntry_t *next;
};

/* outbox.c is linted without websocket.c, its consumer, so functions used
 * only there (or only by tests) are suppressed one by one below */
// cppcheck-suppress staticFunction
BarWsFrame_t *BarWsFrameNew (const char *text, size_t len) {
	if (len > WS_FRAME_MAX_BYTES || (!text && len > 0)) {
		return NULL;
	}
	BarWsFrame_t *f = malloc (sizeof (*f) + LWS_PRE + len);
	if (!f) {
		return NULL;
	}
	atomic_init (&f->refs, 1);
	f->len = len;
	if (len > 0) {
		memcpy (f->buf + LWS_PRE, text, len);
	}
	return f;
}

// cppcheck-suppress staticFunction
BarWsFrame_t *BarWsFrameRef (BarWsFrame_t *f) {
	if (f) {
		atomic_fetch_add_explicit (&f->refs, 1, memory_order_relaxed);
	}
	return f;
}

// cppcheck-suppress staticFunction
void BarWsFrameUnref (BarWsFrame_t *f) {
	if (f && atomic_fetch_sub_explicit (&f->refs, 1, memory_order_acq_rel) == 1) {
		free (f);
	}
}

unsigned char *BarWsFrameData (const BarWsFrame_t *f) {
	return f ? (unsigned char *) f->buf + LWS_PRE : NULL;
}

// cppcheck-suppress staticFunction
size_t BarWsFrameLen (const BarWsFrame_t *f) {
	return f ? f->len : 0;
}

// cppcheck-suppress staticFunction
void BarWsClientQueueInit (BarWsClientQueue_t *q) {
	memset (q, 0, sizeof (*q));
}

static void BarWsQueueEntryFree (BarWsQueueEntry_t *e) {
	BarWsFrameUnref (e->frame);
	free (e);
}

void BarWsClientQueueClear (BarWsClientQueue_t *q) {
	BarWsQueueEntry_t *e = q->head;
	while (e) {
		BarWsQueueEntry_t *next = e->next;
		BarWsQueueEntryFree (e);
		e = next;
	}
	BarWsClientQueueInit (q);
}

/* Latest pending entry with `key` that no ordered (unkeyed) entry follows */
static BarWsQueueEntry_t *BarWsQueueFindReplaceable (const BarWsClientQueue_t *q,
		BarWsKey_t key) {
	BarWsQueueEntry_t *candidate = NULL;
	for (BarWsQueueEntry_t *e = q->head; e; e = e->next) {
		if (e->key == BAR_WS_KEY_NONE) {
			candidate = NULL;
		} else if (e->key == key) {
			candidate = e;
		}
	}
	return candidate;
}

BarWsQResult_t BarWsClientQueueAppend (BarWsClientQueue_t *q, BarWsFrame_t *f,
		BarWsKey_t key, uint32_t generation) {
	BarWsQueueEntry_t *old = (key != BAR_WS_KEY_NONE)
			? BarWsQueueFindReplaceable (q, key) : NULL;
	size_t newBytes = q->bytes - (old ? old->frame->len : 0) + f->len;

	/* An empty queue always accepts one frame so a full-state resync fits */
	if (q->count > 0 && (newBytes > WS_CLIENT_MAX_BYTES ||
			(!old && q->count >= WS_CLIENT_MAX_MSGS))) {
		return BAR_WS_Q_OVERFLOW;
	}

	if (old) {
		BarWsFrame_t *replaced = old->frame;
		old->frame = BarWsFrameRef (f);
		BarWsFrameUnref (replaced);
		old->generation = generation;
		q->bytes = newBytes;
		return BAR_WS_Q_OK;
	}

	BarWsQueueEntry_t *e = malloc (sizeof (*e));
	if (!e) {
		return BAR_WS_Q_OVERFLOW;
	}
	e->frame = BarWsFrameRef (f);
	e->key = key;
	e->generation = generation;
	e->next = NULL;
	if (q->tail) {
		q->tail->next = e;
	} else {
		q->head = e;
	}
	q->tail = e;
	q->count++;
	q->bytes = newBytes;
	return BAR_WS_Q_OK;
}

BarWsFrame_t *BarWsClientQueuePop (BarWsClientQueue_t *q, uint32_t currentGeneration) {
	while (q->head) {
		BarWsQueueEntry_t *e = q->head;
		q->head = e->next;
		if (!q->head) {
			q->tail = NULL;
		}
		q->count--;
		q->bytes -= e->frame->len;

		BarWsFrame_t *f = e->frame;
		bool stale = e->key == BAR_WS_KEY_PROGRESS && e->generation != currentGeneration;
		free (e);
		if (!stale) {
			return f;
		}
		BarWsFrameUnref (f);
	}
	return NULL;
}

struct BarWsOutbox {
	pthread_mutex_t lock;
	BarWsPending_t *head;
	BarWsPending_t *tail;
	size_t count;
	size_t bytes;
	uint32_t generation;
	uint64_t playStateEpoch;   /* newest accepted completed play-state epoch */
	bool playStateEpochValid;
	bool overflowed;           /* a push was rejected since the last check */
};

BarWsOutbox_t *BarWsOutboxNew (void) {
	BarWsOutbox_t *o = calloc (1, sizeof (*o));
	if (!o) {
		return NULL;
	}
	if (pthread_mutex_init (&o->lock, NULL) != 0) {
		free (o);
		return NULL;
	}
	return o;
}

// cppcheck-suppress staticFunction
void BarWsPendingFree (BarWsPending_t *list) {
	while (list) {
		BarWsPending_t *next = list->next;
		BarWsFrameUnref (list->frame);
		free (list);
		list = next;
	}
}

void BarWsOutboxFree (BarWsOutbox_t *o) {
	if (!o) {
		return;
	}
	BarWsPendingFree (o->head);
	pthread_mutex_destroy (&o->lock);
	free (o);
}

/* Caller holds o->lock */
static bool BarWsOutboxHasRoom (const BarWsOutbox_t *o, size_t len) {
	return o->count < WS_INGRESS_MAX_MSGS && o->bytes + len <= WS_INGRESS_MAX_BYTES;
}

/* Copy text into a new pending node and free text. NULL if the frame is too
 * large or memory is short. */
static BarWsPending_t *BarWsPendingNew (BarWsConnId_t dest, BarWsKey_t key,
		char *text, size_t len) {
	BarWsFrame_t *frame = BarWsFrameNew (text, len);
	free (text);
	BarWsPending_t *p = frame ? malloc (sizeof (*p)) : NULL;
	if (!p) {
		BarWsFrameUnref (frame);
		return NULL;
	}
	p->dest = dest;
	p->key = key;
	p->generation = 0;
	p->frame = frame;
	p->next = NULL;
	return p;
}

/* Caller holds o->lock. Appends p if there is room; otherwise records the
 * overflow and leaves p to the caller. */
static bool BarWsOutboxAppendLocked (BarWsOutbox_t *o, BarWsPending_t *p) {
	size_t len = BarWsFrameLen (p->frame);
	if (!BarWsOutboxHasRoom (o, len)) {
		o->overflowed = true;
		return false;
	}
	if (o->tail) {
		o->tail->next = p;
	} else {
		o->head = p;
	}
	o->tail = p;
	o->count++;
	o->bytes += len;
	return true;
}

/* Caller holds o->lock. Unlinks unsent PROGRESS entries and returns them
 * for the caller to free after unlocking. */
static BarWsPending_t *BarWsOutboxUnlinkProgressLocked (BarWsOutbox_t *o) {
	BarWsPending_t *discard = NULL, *discardTail = NULL;
	BarWsPending_t *prev = NULL;
	BarWsPending_t *p = o->head;
	while (p) {
		BarWsPending_t *next = p->next;
		if (p->key == BAR_WS_KEY_PROGRESS) {
			if (prev) {
				prev->next = next;
			} else {
				o->head = next;
			}
			if (o->tail == p) {
				o->tail = prev;
			}
			o->count--;
			o->bytes -= BarWsFrameLen (p->frame);
			p->next = NULL;
			if (discardTail) {
				discardTail->next = p;
			} else {
				discard = p;
			}
			discardTail = p;
		} else {
			prev = p;
		}
		p = next;
	}
	return discard;
}

static void BarWsOutboxMarkOverflow (BarWsOutbox_t *o) {
	pthread_mutex_lock (&o->lock);
	o->overflowed = true;
	pthread_mutex_unlock (&o->lock);
}

/* Push with the generation either current at push time or observed by the
 * producer. */
static bool BarWsOutboxPushTagged (BarWsOutbox_t *o, BarWsConnId_t dest,
		BarWsKey_t key, char *text, size_t len, const uint32_t *observed) {
	if (!o || !text) {
		free (text);
		return false;
	}

	/* Cheap early check so a full ingress fails without allocating */
	pthread_mutex_lock (&o->lock);
	bool room = BarWsOutboxHasRoom (o, len);
	if (!room) {
		o->overflowed = true;
	}
	pthread_mutex_unlock (&o->lock);
	if (!room) {
		free (text);
		return false;
	}

	BarWsPending_t *p = BarWsPendingNew (dest, key, text, len);
	if (!p) {
		BarWsOutboxMarkOverflow (o);
		return false;
	}

	pthread_mutex_lock (&o->lock);
	p->generation = observed ? *observed : o->generation;
	/* May lose a race with another producer after the early check */
	bool queued = BarWsOutboxAppendLocked (o, p);
	pthread_mutex_unlock (&o->lock);
	if (!queued) {
		BarWsPendingFree (p);
	}
	return queued;
}

bool BarWsOutboxPush (BarWsOutbox_t *o, BarWsConnId_t dest, BarWsKey_t key,
		char *text, size_t len) {
	return BarWsOutboxPushTagged (o, dest, key, text, len, NULL);
}

bool BarWsOutboxPushProgress (BarWsOutbox_t *o, uint32_t observedGeneration,
		char *text, size_t len) {
	return BarWsOutboxPushTagged (o, BAR_WS_DEST_BROADCAST, BAR_WS_KEY_PROGRESS,
			text, len, &observedGeneration);
}

bool BarWsOutboxPushSongStart (BarWsOutbox_t *o, char *text, size_t len) {
	if (!o || !text) {
		free (text);
		return false;
	}
	BarWsPending_t *p = BarWsPendingNew (BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
			text, len);

	pthread_mutex_lock (&o->lock);
	uint32_t gen = ++o->generation;
	BarWsPending_t *discard = BarWsOutboxUnlinkProgressLocked (o);
	bool queued = false;
	if (p) {
		p->generation = gen;
		queued = BarWsOutboxAppendLocked (o, p);
	} else {
		o->overflowed = true;
	}
	pthread_mutex_unlock (&o->lock);

	BarWsPendingFree (discard);
	if (!queued) {
		BarWsPendingFree (p);
	}
	return queued;
}

bool BarWsOutboxPushPlayState (BarWsOutbox_t *o, uint64_t epoch, char *text,
		size_t len) {
	if (!o || !text) {
		free (text);
		return false;
	}
	BarWsPending_t *p = BarWsPendingNew (BAR_WS_DEST_BROADCAST, BAR_WS_KEY_NONE,
			text, len);

	pthread_mutex_lock (&o->lock);
	bool queued = false;
	if (o->playStateEpochValid && epoch < o->playStateEpoch) {
		/* An older completed state lost the race: never publish it */
	} else {
		/* Accepted even if the frame is rejected below: clients then resync
		 * from current state, which an older epoch must not overwrite */
		o->playStateEpoch = epoch;
		o->playStateEpochValid = true;
		if (p) {
			p->generation = o->generation;
			queued = BarWsOutboxAppendLocked (o, p);
		} else {
			o->overflowed = true;
		}
	}
	pthread_mutex_unlock (&o->lock);

	if (!queued) {
		BarWsPendingFree (p);
	}
	return queued;
}

BarWsPending_t *BarWsOutboxTake (BarWsOutbox_t *o) {
	if (!o) {
		return NULL;
	}
	pthread_mutex_lock (&o->lock);
	BarWsPending_t *list = o->head;
	o->head = o->tail = NULL;
	o->count = 0;
	o->bytes = 0;
	pthread_mutex_unlock (&o->lock);
	return list;
}

bool BarWsOutboxTakeOverflow (BarWsOutbox_t *o) {
	if (!o) {
		return false;
	}
	pthread_mutex_lock (&o->lock);
	bool overflowed = o->overflowed;
	o->overflowed = false;
	pthread_mutex_unlock (&o->lock);
	return overflowed;
}

uint32_t BarWsOutboxGeneration (BarWsOutbox_t *o) {
	if (!o) {
		return 0;
	}
	pthread_mutex_lock (&o->lock);
	uint32_t gen = o->generation;
	pthread_mutex_unlock (&o->lock);
	return gen;
}
