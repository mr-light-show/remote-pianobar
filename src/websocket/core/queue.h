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

/* Message types for bucket communication */
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

/* Message structure for buckets */
typedef struct BarWsMessage {
	BarWsMsgType_t type;           /* Message type */
	void *data;                    /* Message payload (owned by message) */
	size_t dataLen;                /* Payload length */
	struct BarWsMessage *next;     /* Next message in linked list */
} BarWsMessage_t;

/* Free message (frees data and message itself) */
void BarWsMessageFree(BarWsMessage_t *msg);

#endif /* _WEBSOCKET_QUEUE_H */
