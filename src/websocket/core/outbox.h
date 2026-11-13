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

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "queue.h"

/* Coalescing key for a queued frame. NONE entries are ordered events and are
 * never replaced; keyed entries are replaced by a later entry with the same
 * key unless an ordered event was queued in between. */
typedef enum {
	BAR_WS_KEY_NONE = 0,
	BAR_WS_KEY_PROGRESS,
	BAR_WS_KEY_VOLUME,
	BAR_WS_KEY_STATIONS
} BarWsKey_t;

/* Immutable, reference-counted outbound frame shared between client queues.
 * The payload is preceded by LWS_PRE bytes so it can be handed to lws_write. */
typedef struct BarWsFrame BarWsFrame_t;

/* Copy text into a new frame (refcount 1). NULL if len > WS_FRAME_MAX_BYTES
 * or out of memory. */
BarWsFrame_t *BarWsFrameNew (const char *text, size_t len);
BarWsFrame_t *BarWsFrameRef (BarWsFrame_t *f);
void BarWsFrameUnref (BarWsFrame_t *f);
/* Payload start (LWS_PRE writable bytes precede it) and payload length */
unsigned char *BarWsFrameData (const BarWsFrame_t *f);
size_t BarWsFrameLen (const BarWsFrame_t *f);

typedef struct BarWsQueueEntry BarWsQueueEntry_t;

/* Per-client pending queue. Owned by the service thread: no locking. */
typedef struct BarWsClientQueue {
	BarWsQueueEntry_t *head;
	BarWsQueueEntry_t *tail;
	size_t count;
	size_t bytes;
} BarWsClientQueue_t;

typedef enum { BAR_WS_Q_OK, BAR_WS_Q_OVERFLOW } BarWsQResult_t;

void BarWsClientQueueInit (BarWsClientQueue_t *q);
/* Drop every pending entry and release its frame reference */
void BarWsClientQueueClear (BarWsClientQueue_t *q);
/* Queue f (takes its own reference). OVERFLOW if the queue is non-empty and
 * the message or byte cap would be exceeded, or on allocation failure; an
 * empty queue always accepts one frame. */
BarWsQResult_t BarWsClientQueueAppend (BarWsClientQueue_t *q, BarWsFrame_t *f,
		BarWsKey_t key, uint32_t generation);
/* Remove and return the oldest entry, discarding progress entries whose
 * generation differs from currentGeneration. Caller must Unref the result. */
BarWsFrame_t *BarWsClientQueuePop (BarWsClientQueue_t *q, uint32_t currentGeneration);

/* Producer-facing ingress. Producer threads Push; the service thread Takes
 * everything pushed so far. One mutex guards the queue, the song
 * generation counter, the play-state watermark and the overflow flag. */
typedef struct BarWsOutbox BarWsOutbox_t;

/* One taken message. The list and its frame references are owned by the
 * caller of BarWsOutboxTake: move each frame into client queues (they take
 * their own reference) and release the list with BarWsPendingFree. */
typedef struct BarWsPending {
	BarWsConnId_t dest;
	BarWsKey_t key;
	uint32_t generation; /* song generation at push time (for progress, the
	                      * generation its producer observed) */
	BarWsFrame_t *frame;
	struct BarWsPending *next;
} BarWsPending_t;

BarWsOutbox_t *BarWsOutboxNew (void);
/* Frees anything still pending */
void BarWsOutboxFree (BarWsOutbox_t *o);

/* Every push takes ownership of text (heap, freed here on every path) and
 * copies it into a frame. A push returns false, and raises the overflow flag
 * (see BarWsOutboxTakeOverflow), when the ingress limits are reached or
 * memory is short. A frame longer than WS_FRAME_MAX_BYTES is rejected on its
 * own (logged at LOG_ERROR, flag not raised: no reconnect could deliver it);
 * so are NULL arguments. */

/* Queue an event for dest (a connection ID or BAR_WS_DEST_BROADCAST). Keyed
 * entries are tagged with the generation current at push time. The full path
 * allocates nothing. */
bool BarWsOutboxPush (BarWsOutbox_t *o, BarWsConnId_t dest, BarWsKey_t key,
		char *text, size_t len);
/* Admit a song start atomically with its broadcast frame: in one critical
 * section increment the generation, discard unsent PROGRESS still in ingress
 * and queue text as an ordered broadcast. Progress for the new song can then
 * never be ordered ahead of its start. The generation advances even when the
 * frame is rejected. */
bool BarWsOutboxPushSongStart (BarWsOutbox_t *o, char *text, size_t len);
/* Queue a progress broadcast sampled while the generation was observed
 * (BarWsOutboxGeneration, read before sampling). It keeps that tag, so a
 * sample of the previous song is dropped at dequeue even if it is pushed
 * after the next start. */
bool BarWsOutboxPushProgress (BarWsOutbox_t *o, uint32_t observedGeneration,
		char *text, size_t len);
/* Queue a completed play-state broadcast. Rejected (false, text freed, no
 * overflow) when epoch is older than the newest accepted epoch; the
 * watermark lives under the ingress lock and survives Take. */
bool BarWsOutboxPushPlayState (BarWsOutbox_t *o, uint64_t epoch, char *text,
		size_t len);

/* Service thread: detach everything pushed so far, in push order. NULL if
 * empty. Release with BarWsPendingFree. */
BarWsPending_t *BarWsOutboxTake (BarWsOutbox_t *o);
/* Release a taken list: unref each frame and free the nodes */
void BarWsPendingFree (BarWsPending_t *list);
/* Service thread: true if a push was rejected since the last call (an event
 * was lost, so clients must resync). Clears the flag. */
bool BarWsOutboxTakeOverflow (BarWsOutbox_t *o);
uint32_t BarWsOutboxGeneration (BarWsOutbox_t *o);
