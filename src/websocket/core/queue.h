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

#ifndef _WEBSOCKET_QUEUE_H
#define _WEBSOCKET_QUEUE_H

#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>

/* Message types for queue communication */
typedef enum {
	/* Broadcast messages (Main → WebSocket thread) */
	MSG_TYPE_BROADCAST_START,      /* Song started */
	MSG_TYPE_BROADCAST_STOP,       /* Song stopped */
	MSG_TYPE_BROADCAST_PROGRESS,   /* Progress update */
	MSG_TYPE_BROADCAST_VOLUME,     /* Volume changed */
	MSG_TYPE_BROADCAST_STATIONS,   /* Station list changed */
	
	/* Command messages (WebSocket → Main thread) */
	MSG_TYPE_COMMAND_ACTION,       /* User action */
	MSG_TYPE_COMMAND_CHANGE_STATION, /* Change station */
	MSG_TYPE_COMMAND_QUERY,        /* Query state */
	
	/* Control messages */
	MSG_TYPE_SHUTDOWN              /* Shutdown signal */
} BarWsMsgType_t;

/* Message structure for queue */
typedef struct BarWsMessage {
	BarWsMsgType_t type;           /* Message type */
	void *data;                    /* Message payload (owned by message) */
	size_t dataLen;                /* Payload length */
	struct BarWsMessage *next;     /* Next message in queue */
} BarWsMessage_t;

/* Thread-safe message queue */
typedef struct {
	BarWsMessage_t *head;          /* First message */
	BarWsMessage_t *tail;          /* Last message */
	pthread_mutex_t mutex;         /* Protects queue */
	pthread_cond_t cond;           /* Signals when not empty */
	size_t count;                  /* Current number of messages */
	size_t maxSize;                /* Maximum queue size */
	bool closed;                   /* Queue is closed */
	void *lwsContext;              /* libwebsockets context for lws_cancel_service() */
} BarWsQueue_t;

/* Initialize queue */
void BarWsQueueInit(BarWsQueue_t *queue, size_t maxSize, void *lwsContext);

/* Destroy queue and free all messages */
void BarWsQueueDestroy(BarWsQueue_t *queue);

/* Push message to queue (copies data)
 * Returns false if queue is full or closed */
bool BarWsQueuePush(BarWsQueue_t *queue, BarWsMsgType_t type, 
                     const void *data, size_t dataLen);

/* Pop message from queue (blocks if empty, returns NULL on timeout or closed)
 * timeout_ms: -1 = block forever, 0 = non-blocking, >0 = timeout in ms
 * Caller must free returned message with BarWsMessageFree() */
BarWsMessage_t* BarWsQueuePop(BarWsQueue_t *queue, int timeout_ms);

/* Get queue size (thread-safe) */
size_t BarWsQueueSize(BarWsQueue_t *queue);

/* Close queue (prevents new pushes, wakes up all waiters) */
void BarWsQueueClose(BarWsQueue_t *queue);

/* Free message (frees data and message itself) */
void BarWsMessageFree(BarWsMessage_t *msg);

#endif /* _WEBSOCKET_QUEUE_H */

