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
