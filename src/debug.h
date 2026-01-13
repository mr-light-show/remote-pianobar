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

/* Enable POSIX functions on Linux (not needed on macOS/BSD)
 * MUST be defined before ANY includes
 * Force definition to ensure we get fileno() and other POSIX functions */
#if !defined(__APPLE__) && !defined(__FreeBSD__)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
/* Also ensure _DEFAULT_SOURCE for glibc to get everything */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif

#include "config.h"
#include <stdbool.h>

#ifdef HAVE_DEBUGLOG
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

/* ANSI color codes */
#define COLOR_RESET   "\033[0m"
#define COLOR_CYAN    "\033[0;36m"  /* Network */
#define COLOR_YELLOW  "\033[0;33m"  /* Audio */
#define COLOR_GREEN   "\033[0;32m"  /* UI */
#define COLOR_MAGENTA "\033[0;35m"  /* WebSocket */

/* bitfield */
typedef enum {
	DEBUG_NETWORK = 1,
	DEBUG_AUDIO = 2,
	DEBUG_UI = 4,
	DEBUG_WEBSOCKET = 8,
	DEBUG_WEBSOCKET_PROGRESS = 16,  /* Progress updates (noisy) */
} debugKind;

extern unsigned int debug;

inline static bool debugEnable () {
	const char * const debugStr = getenv("PIANOBAR_DEBUG");
	if (debugStr != NULL) {
		debug = atoi (debugStr);
	}
	return debug;
}

__attribute__((format(printf, 2, 3)))
inline static void debugPrint(debugKind kind, const char * const format, ...) {
	if (debug & kind) {
		/* Get timestamp with millisecond precision */
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		struct tm *tm_info = localtime(&ts.tv_sec);
		
		/* Only use colors if stderr is a terminal */
		const bool useColor = isatty(fileno(stderr));
		const char *color = "";
		
		if (useColor) {
			switch (kind) {
				case DEBUG_NETWORK:   color = COLOR_CYAN; break;
				case DEBUG_AUDIO:     color = COLOR_YELLOW; break;
				case DEBUG_UI:        color = COLOR_GREEN; break;
				case DEBUG_WEBSOCKET: color = COLOR_MAGENTA; break;
				case DEBUG_WEBSOCKET_PROGRESS: color = COLOR_MAGENTA; break;
				default:              color = ""; break;
			}
			fprintf(stderr, "%s", color);
		}
		
		/* Print timestamp [HH:MM:SS.mmm] */
		fprintf(stderr, "[%02d:%02d:%02d.%03ld] ",
		        tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
		        ts.tv_nsec / 1000000);
		
		va_list fmtargs;
		va_start (fmtargs, format);
		vfprintf (stderr, format, fmtargs);
		va_end (fmtargs);
		
		if (useColor) {
			fprintf(stderr, "%s", COLOR_RESET);
		}
	}
}
#else
inline static bool debugEnable () { return false; }
#define debugPrint(...)
#endif

