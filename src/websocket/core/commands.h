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

#include "queue.h"

/* Bounded FIFO that hands WebSocket commands from the receive callbacks
 * (producers, service thread) to the command worker (one consumer).
 *
 * An item is {requester connection ID, copied request bytes}: only immutable
 * owned bytes and a stable ID cross threads, never JSON nodes, station/song
 * pointers or transport handles. Limits are WS_CMD_MAX_ITEMS and
 * WS_CMD_MAX_BYTES (bar_constants.h). */
typedef struct BarWsCommandQueue BarWsCommandQueue_t;

typedef enum {
	BAR_WS_CMD_OK,     /* queued */
	BAR_WS_CMD_FULL,   /* item or byte limit reached: admission failure */
	BAR_WS_CMD_CLOSED, /* queue closed (shutdown), distinct from FULL */
	BAR_WS_CMD_NOMEM   /* allocation failed or invalid arguments; not queued */
} BarWsCommandResult_t;

/* NULL on allocation failure */
BarWsCommandQueue_t *BarWsCommandQueueNew (void);
/* Frees the queue and any items still queued. The caller must ensure no
 * other thread is still inside a queue call (Close, then join the consumer,
 * then Free). */
void BarWsCommandQueueFree (BarWsCommandQueue_t *q);

/* Copy len bytes into a new item (the caller may modify or free bytes right
 * after) and append it. The copy is NUL-terminated (the terminator is not
 * counted in len). An empty queue accepts any request up to
 * WS_CMD_MAX_REQUEST_BYTES. NULL q returns CLOSED; NULL bytes with len > 0
 * returns NOMEM (nothing is queued). */
BarWsCommandResult_t BarWsCommandQueuePush (BarWsCommandQueue_t *q,
		BarWsConnId_t requester, const char *bytes, size_t len);

/* Block until an item is available and remove it. On true, *bytes is
 * heap-owned by the caller (free it) and any out pointer may be NULL to
 * discard that field (bytes then is freed here). Returns false, with outputs
 * untouched, once the queue is closed (queued items are dropped by Close). */
bool BarWsCommandQueuePop (BarWsCommandQueue_t *q, BarWsConnId_t *requester,
		char **bytes, size_t *len);

/* Reject all later pushes, drop and free every queued item, and wake every
 * consumer blocked in Pop. Idempotent. */
void BarWsCommandQueueClose (BarWsCommandQueue_t *q);
