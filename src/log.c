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

#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#ifdef HAVE_DEBUGLOG
#define COLOR_RESET   "\033[0m"
#define COLOR_CYAN    "\033[0;36m"  /* Network */
#define COLOR_YELLOW  "\033[0;33m"  /* Audio */
#define COLOR_GREEN   "\033[0;32m"  /* UI */
#define COLOR_MAGENTA      "\033[0;35m"  /* WebSocket progress (non-bold) */
#define COLOR_MAGENTA_BOLD "\033[1;35m"  /* WebSocket events */
#define COLOR_BLUE    "\033[1;34m"  /* CLI (BarUiMsg mirror) */
#define COLOR_RED     "\033[0;31m"  /* Error */
#endif

static unsigned int debug_mask = 0;

void log_init(void)
{
#ifdef HAVE_DEBUGLOG
	const char *const s = getenv("PIANOBAR_DEBUG");
	unsigned int raw = 0;
	if (s != NULL) {
		raw = (unsigned int)atoi(s);
	}
	debug_mask = raw;
	if (debug_mask != 0) {
		debug_mask |= DEBUG_CLI;   /* Turn on CLI debugging */
	}
	if (debug_mask != 0) {
		fprintf(stderr, "PIANOBAR_DEBUG=%u: ", raw);
		if (debug_mask & DEBUG_NETWORK) {
			fprintf(stderr, "%sNETWORK%s ", COLOR_CYAN, COLOR_RESET);
		}
		if (debug_mask & DEBUG_AUDIO) {
			fprintf(stderr, "%sAUDIO%s ", COLOR_YELLOW, COLOR_RESET);
		}
		if (debug_mask & DEBUG_UI) {
			fprintf(stderr, "%sUI%s ", COLOR_GREEN, COLOR_RESET);
		}
		if (debug_mask & DEBUG_WEBSOCKET) {
			fprintf(stderr, "%sWEBSOCKET%s ", COLOR_MAGENTA_BOLD, COLOR_RESET);
		}
		if (debug_mask & DEBUG_WEBSOCKET_PROGRESS) {
			fprintf(stderr, "%sWS_PROGRESS%s ", COLOR_MAGENTA, COLOR_RESET);
		}
		if (debug_mask & DEBUG_CLI) {
			fprintf(stderr, "%sCLI%s ", COLOR_BLUE, COLOR_RESET);
		}
		fprintf(stderr, "\n");
	}
#else
	(void)0;
#endif
}

static void log_to_stderr(const char *format, va_list args)
{
	va_list args2;
	va_copy(args2, args);
	vfprintf(stderr, format, args2);
	va_end(args2);
}

#ifdef HAVE_DEBUGLOG
/* Longest label is "WS_Progress" (11). Pad so ':' and message body start in the same column. */
#define LOG_KIND_LABEL_FIELD_WIDTH 11

/* Label after [time]; same strings as log_init() banner (NETWORK, AUDIO, …). */
static const char *log_kind_prefix_label(logKind kind)
{
	switch (kind) {
	case LOG_ERROR:                return "Error";
	case DEBUG_NETWORK:            return "Network";
	case DEBUG_AUDIO:             return "Audio";
	case DEBUG_UI:                 return "UI";
	case DEBUG_WEBSOCKET:          return "WebSocket";
	case DEBUG_WEBSOCKET_PROGRESS: return "WS_Progress";
	case DEBUG_CLI:                return "CLI";
	default:                       return "?";
	}
}

static void log_with_timestamp(logKind kind, const char *format, va_list args)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	const struct tm *tm_info = localtime(&ts.tv_sec);
	const char *color = "";

	switch (kind) {
		case DEBUG_NETWORK:             color = COLOR_CYAN; break;
		case DEBUG_AUDIO:               color = COLOR_YELLOW; break;
		case DEBUG_UI:                  color = COLOR_GREEN; break;
		case DEBUG_WEBSOCKET:           color = COLOR_MAGENTA_BOLD; break;
		case DEBUG_WEBSOCKET_PROGRESS:  color = COLOR_MAGENTA; break;
		case DEBUG_CLI:                 color = COLOR_BLUE; break;
		case LOG_ERROR:                 color = COLOR_RED; break;
		default:                        color = ""; break;
	}
	/* Timestamp and kind prefix share the same color; message body is default color */
	fprintf(stderr, "%s[%02d:%02d:%02d.%03ld]%s ",
	        color,
	        tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
	        (long)(ts.tv_nsec / 1000000),
	        COLOR_RESET);
	fprintf(stderr, "%s%-*s:%s ",
	        color,
	        LOG_KIND_LABEL_FIELD_WIDTH,
	        log_kind_prefix_label(kind),
	        COLOR_RESET);
	vfprintf(stderr, format, args);
}
#endif

bool log_is_any_debug_enabled(void)
{
#ifdef HAVE_DEBUGLOG
	return debug_mask != 0;
#else
	return false;
#endif
}

bool log_is_debug_cli_enabled(void)
{
#ifdef HAVE_DEBUGLOG
	return (debug_mask & DEBUG_CLI) != 0;
#else
	return false;
#endif
}

void log_write(logKind kind, const char *format, ...)
{
	va_list args;
	va_start(args, format);

	if (kind == LOG_ERROR) {
		/* Always log errors */
#ifdef HAVE_DEBUGLOG
		log_with_timestamp(LOG_ERROR, format, args);
#else
		log_to_stderr(format, args);
#endif
	} else {
#ifdef HAVE_DEBUGLOG
		if ((debug_mask & kind) != 0) {
			log_with_timestamp(kind, format, args);
		}
#endif
	}

	va_end(args);
}
