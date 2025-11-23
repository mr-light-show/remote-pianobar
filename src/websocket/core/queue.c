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

#include "queue.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <libwebsockets.h>

/* Initialize queue */
void BarWsQueueInit(BarWsQueue_t *queue, size_t maxSize, void *lwsContext) {
	if (!queue) {
		return;
	}
	
	queue->head = NULL;
	queue->tail = NULL;
	queue->count = 0;
	queue->maxSize = maxSize;
	queue->closed = false;
	queue->lwsContext = lwsContext;
	
	pthread_mutex_init(&queue->mutex, NULL);
	pthread_cond_init(&queue->cond, NULL);
}

/* Destroy queue and free all messages */
void BarWsQueueDestroy(BarWsQueue_t *queue) {
	if (!queue) {
		return;
	}
	
	/* Close queue first */
	BarWsQueueClose(queue);
	
	/* Free all remaining messages */
	pthread_mutex_lock(&queue->mutex);
	
	BarWsMessage_t *msg = queue->head;
	while (msg != NULL) {
		BarWsMessage_t *next = msg->next;
		BarWsMessageFree(msg);
		msg = next;
	}
	
	queue->head = NULL;
	queue->tail = NULL;
	queue->count = 0;
	
	pthread_mutex_unlock(&queue->mutex);
	
	/* Destroy synchronization primitives */
	pthread_mutex_destroy(&queue->mutex);
	pthread_cond_destroy(&queue->cond);
}

/* Push message to queue (copies data) */
bool BarWsQueuePush(BarWsQueue_t *queue, BarWsMsgType_t type, 
                     const void *data, size_t dataLen) {
	if (!queue) {
		return false;
	}
	
	pthread_mutex_lock(&queue->mutex);
	
	/* Check if queue is closed */
	if (queue->closed) {
		pthread_mutex_unlock(&queue->mutex);
		return false;
	}
	
	/* Clear queue on state change events (start/stop) */
	if (type == MSG_TYPE_BROADCAST_START || type == MSG_TYPE_BROADCAST_STOP) {
		/* Free all existing messages */
		BarWsMessage_t *msg = queue->head;
		while (msg != NULL) {
			BarWsMessage_t *next = msg->next;
			BarWsMessageFree(msg);
			msg = next;
		}
		
		/* Reset queue */
		queue->head = NULL;
		queue->tail = NULL;
		queue->count = 0;
	}
	
	/* Check if message of this type already exists - replace it instead of queuing another */
	BarWsMessage_t *existing = queue->head;
	BarWsMessage_t *prev = NULL;
	
	while (existing != NULL) {
		if (existing->type == type) {
			/* Found existing message of same type - replace its data */
			
			/* Free old data */
			if (existing->data) {
				free(existing->data);
				existing->data = NULL;
				existing->dataLen = 0;
			}
			
			/* Copy new data if provided */
			if (data && dataLen > 0) {
				existing->data = malloc(dataLen);
				if (!existing->data) {
					pthread_mutex_unlock(&queue->mutex);
					return false;
				}
				memcpy(existing->data, data, dataLen);
				existing->dataLen = dataLen;
			}
			
			pthread_mutex_unlock(&queue->mutex);
			
			/* Still wake up the WebSocket thread in case it's waiting */
			if (queue->lwsContext) {
				lws_cancel_service((struct lws_context *)queue->lwsContext);
			}
			
			return true;
		}
		
		prev = existing;
		existing = existing->next;
	}
	
	/* No existing message of this type found - create new one */
	
	/* Check if queue is full */
	if (queue->count >= queue->maxSize) {
		/* Drop oldest message to make room */
		BarWsMessage_t *old = queue->head;
		if (old) {
			queue->head = old->next;
			if (queue->head == NULL) {
				queue->tail = NULL;
			}
			queue->count--;
			BarWsMessageFree(old);
		}
	}
	
	/* Create new message */
	BarWsMessage_t *msg = calloc(1, sizeof(BarWsMessage_t));
	if (!msg) {
		pthread_mutex_unlock(&queue->mutex);
		return false;
	}
	
	msg->type = type;
	msg->next = NULL;
	
	/* Copy data if provided */
	if (data && dataLen > 0) {
		msg->data = malloc(dataLen);
		if (!msg->data) {
			free(msg);
			pthread_mutex_unlock(&queue->mutex);
			return false;
		}
		memcpy(msg->data, data, dataLen);
		msg->dataLen = dataLen;
	} else {
		msg->data = NULL;
		msg->dataLen = 0;
	}
	
	/* Add to tail of queue */
	if (queue->tail) {
		queue->tail->next = msg;
		queue->tail = msg;
	} else {
		queue->head = msg;
		queue->tail = msg;
	}
	
	queue->count++;
	
	/* Signal waiting threads */
	pthread_cond_signal(&queue->cond);
	
	/* Wake up lws_service() immediately if context available */
	if (queue->lwsContext) {
		lws_cancel_service((struct lws_context *)queue->lwsContext);
	}
	
	pthread_mutex_unlock(&queue->mutex);
	return true;
}

/* Pop message from queue */
BarWsMessage_t* BarWsQueuePop(BarWsQueue_t *queue, int timeout_ms) {
	if (!queue) {
		return NULL;
	}
	
	pthread_mutex_lock(&queue->mutex);
	
	/* Wait for message or timeout */
	while (queue->head == NULL && !queue->closed) {
		if (timeout_ms == 0) {
			/* Non-blocking */
			pthread_mutex_unlock(&queue->mutex);
			return NULL;
		} else if (timeout_ms < 0) {
			/* Block forever */
			pthread_cond_wait(&queue->cond, &queue->mutex);
		} else {
			/* Timed wait */
			struct timeval now;
			struct timespec ts;
			
			gettimeofday(&now, NULL);
			ts.tv_sec = now.tv_sec + (timeout_ms / 1000);
			ts.tv_nsec = (now.tv_usec * 1000) + ((timeout_ms % 1000) * 1000000);
			
			/* Handle nanosecond overflow */
			if (ts.tv_nsec >= 1000000000) {
				ts.tv_sec++;
				ts.tv_nsec -= 1000000000;
			}
			
			int ret = pthread_cond_timedwait(&queue->cond, &queue->mutex, &ts);
			if (ret == ETIMEDOUT) {
				pthread_mutex_unlock(&queue->mutex);
				return NULL;
			}
		}
	}
	
	/* Queue is closed and empty */
	if (queue->head == NULL) {
		pthread_mutex_unlock(&queue->mutex);
		return NULL;
	}
	
	/* Remove from head of queue */
	BarWsMessage_t *msg = queue->head;
	queue->head = msg->next;
	if (queue->head == NULL) {
		queue->tail = NULL;
	}
	
	queue->count--;
	msg->next = NULL; /* Detach from queue */
	
	pthread_mutex_unlock(&queue->mutex);
	return msg;
}

/* Get queue size */
size_t BarWsQueueSize(BarWsQueue_t *queue) {
	if (!queue) {
		return 0;
	}
	
	pthread_mutex_lock(&queue->mutex);
	size_t size = queue->count;
	pthread_mutex_unlock(&queue->mutex);
	
	return size;
}

/* Close queue */
void BarWsQueueClose(BarWsQueue_t *queue) {
	if (!queue) {
		return;
	}
	
	pthread_mutex_lock(&queue->mutex);
	queue->closed = true;
	pthread_cond_broadcast(&queue->cond); /* Wake up all waiters */
	pthread_mutex_unlock(&queue->mutex);
}

/* Free message */
void BarWsMessageFree(BarWsMessage_t *msg) {
	if (!msg) {
		return;
	}
	
	if (msg->data) {
		free(msg->data);
	}
	
	free(msg);
}

