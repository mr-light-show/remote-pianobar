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
#include "parse_utils.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <limits.h>

#ifdef HAVE_DEBUGLOG
#define COLOR_RESET   "\033[0m"
#define COLOR_CYAN    "\033[0;36m"  /* Network */
#define COLOR_YELLOW  "\033[0;33m"  /* Audio */
#define COLOR_GREEN   "\033[0;32m"  /* UI */
#define COLOR_MAGENTA      "\033[0;35m"  /* WebSocket progress (non-bold) */
#define COLOR_MAGENTA_BOLD "\033[1;35m"  /* WebSocket events */
#define COLOR_BLUE    "\033[1;34m"  /* CLI (BarUiMsg mirror) */
#define COLOR_WHITE_BOLD "\033[1;37m"  /* State rwlock */
#define COLOR_RED     "\033[0;31m"  /* Error */
#endif

static _Atomic unsigned int debug_mask = 0;

/* Optional copy of every emitted line (ui_mode both/cli); -1 when unset.
 * An O_APPEND descriptor written with one write() per line, so lines from
 * different threads cannot interleave and no lock is needed. */
static _Atomic int log_file_fd = -1;

void log_set_debug_mask (unsigned int mask) {
	atomic_store_explicit (&debug_mask, mask, memory_order_relaxed);
}

unsigned int log_get_debug_mask (void) {
	return atomic_load_explicit (&debug_mask, memory_order_relaxed);
}

void log_init(unsigned int config_debug)
{
#ifdef HAVE_DEBUGLOG
	const char *const s = getenv("PIANOBAR_DEBUG");
	unsigned int raw = config_debug;
	if (s != NULL) {
		int tmp = 0;
		if (!BarParseIntInRange (s, 0, INT_MAX, &tmp)) {
			fprintf (stderr, "PIANOBAR_DEBUG: invalid value \"%s\", ignoring\n", s);
		} else {
			raw = (unsigned int) tmp;
		}
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
		if (debug_mask & DEBUG_STATE) {
			fprintf(stderr, "%sSTATE%s ", COLOR_WHITE_BOLD, COLOR_RESET);
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
	(void) config_debug;
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
	case DEBUG_STATE:              return "State";
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
	struct tm tm_info;
	localtime_r(&ts.tv_sec, &tm_info);
	const char *color = "";

	switch (kind) {
		case DEBUG_NETWORK:             color = COLOR_CYAN; break;
		case DEBUG_AUDIO:               color = COLOR_YELLOW; break;
		case DEBUG_UI:                  color = COLOR_GREEN; break;
		case DEBUG_STATE:               color = COLOR_WHITE_BOLD; break;
		case DEBUG_WEBSOCKET:           color = COLOR_MAGENTA_BOLD; break;
		case DEBUG_WEBSOCKET_PROGRESS:  color = COLOR_MAGENTA; break;
		case DEBUG_CLI:                 color = COLOR_BLUE; break;
		case LOG_ERROR:                 color = COLOR_RED; break;
		default:                        color = ""; break;
	}
	/* Timestamp and kind prefix share the same color; message body is default color */
	fprintf(stderr, "%s[%02d:%02d:%02d.%03ld]%s ",
	        color,
	        tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec,
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

/* Copy one log line to the log file as plain text (no colour codes). */
static void log_to_file(logKind kind, const char *format, va_list args)
{
	const int fd = atomic_load_explicit(&log_file_fd, memory_order_relaxed);
	if (fd < 0) {
		return;
	}

	char stackBuf[2048];
	char *buf = stackBuf;
	const size_t cap = sizeof(stackBuf);
	size_t len = 0;

#ifdef HAVE_DEBUGLOG
	struct timespec ts;
	struct tm tm_info;
	clock_gettime(CLOCK_REALTIME, &ts);
	localtime_r(&ts.tv_sec, &tm_info);
	const int prefix = snprintf(buf, cap, "[%02d:%02d:%02d.%03ld] %-*s: ",
	        tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec,
	        (long)(ts.tv_nsec / 1000000),
	        LOG_KIND_LABEL_FIELD_WIDTH, log_kind_prefix_label(kind));
	len = prefix > 0 ? (size_t)prefix : 0;
#else
	(void) kind;
#endif

	va_list copy;
	va_copy(copy, args);
	const int body = vsnprintf(buf + len, cap - len, format, copy);
	va_end(copy);
	if (body < 0) {
		return;
	}

	char *heap = NULL;
	size_t out = len + (size_t)body;
	if ((size_t)body >= cap - len) {
		/* Longer than the stack buffer: format again into an exact-size one. */
		heap = malloc(out + 1);
		if (heap != NULL) {
			memcpy(heap, stackBuf, len);
			va_copy(copy, args);
			(void) vsnprintf(heap + len, (size_t)body + 1, format, copy);
			va_end(copy);
			buf = heap;
		} else {
			out = cap - 1;	/* out of memory: keep the truncated line */
		}
	}

	if (write(fd, buf, out) < 0) {
		/* Nowhere left to report a failing log file. */
	}
	free(heap);
}

bool log_set_file(const char *path)
{
	if (path == NULL || path[0] == '\0') {
		return false;
	}
	const int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	if (fd < 0) {
		log_write(LOG_ERROR, "Cannot open log file %s: %s\n", path, strerror(errno));
		return false;
	}
	const int old = atomic_exchange_explicit(&log_file_fd, fd, memory_order_relaxed);
	if (old >= 0) {
		close(old);
	}
	return true;
}

void log_close_file(void)
{
	const int old = atomic_exchange_explicit(&log_file_fd, -1, memory_order_relaxed);
	if (old >= 0) {
		close(old);
	}
}

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

bool log_is_enabled (logKind kind)
{
	if (kind == LOG_ERROR) {
		return true;
	}
#ifdef HAVE_DEBUGLOG
	return (log_get_debug_mask () & (unsigned int) kind) != 0;
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
		va_list fileArgs;
		va_copy(fileArgs, args);
#ifdef HAVE_DEBUGLOG
		log_with_timestamp(LOG_ERROR, format, args);
#else
		log_to_stderr(format, args);
#endif
		log_to_file(LOG_ERROR, format, fileArgs);
		va_end(fileArgs);
	} else {
#ifdef HAVE_DEBUGLOG
		if ((debug_mask & kind) != 0) {
			va_list fileArgs;
			va_copy(fileArgs, args);
			log_with_timestamp(kind, format, args);
			log_to_file(kind, format, fileArgs);
			va_end(fileArgs);
		}
#endif
	}

	va_end(args);
}

void log_network_request(const char *url)
{
	if (url == NULL) {
		return;
	}
	log_write(DEBUG_NETWORK, "← %s\n", url);
}

void log_network_response(const char *summary)
{
	if (summary == NULL) {
		return;
	}
	log_write(DEBUG_NETWORK, "→ %s\n", summary);
}
