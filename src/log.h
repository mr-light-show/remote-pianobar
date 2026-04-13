/*
Copyright (c) 2008-2018
	Lars-Dominik Braun <lars@6xq.net>

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

#include "config.h"
#include <stdbool.h>

/* Log level / category: LOG_ERROR (0) always logs; others only when enabled via PIANOBAR_DEBUG. */
typedef enum {
	LOG_ERROR = 0,
	DEBUG_NETWORK = 1,
	DEBUG_AUDIO = 2,
	DEBUG_UI = 4,
	DEBUG_WEBSOCKET = 8,
	DEBUG_WEBSOCKET_PROGRESS = 16,  /* Progress updates (noisy) */
	DEBUG_CLI = 32,                 /* BarUiMsg mirror; emits when any PIANOBAR_DEBUG bit is set */
} logKind;

/* Initialize log module (reads PIANOBAR_DEBUG for debug mask). Call once at startup. */
void log_init(void);

/* True after log_init if PIANOBAR_DEBUG was non-zero (HAVE_DEBUGLOG only). */
bool log_is_any_debug_enabled(void);

/* Log message. LOG_ERROR always emits; DEBUG_CLI emits when any debug bit is set;
 * other DEBUG_* kinds emit when the corresponding PIANOBAR_DEBUG bit is set. */
void log_write(logKind kind, const char *format, ...)
	__attribute__((format(printf, 2, 3)));
