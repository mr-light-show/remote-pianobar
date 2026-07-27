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

#include "websocket_lws_log.h"
#include "../../log.h"

#include <libwebsockets.h>
#include <string.h>

const char *BarWsLwsLogMessage (const char *line)
{
	const char *p;

	if (line == NULL) {
		return "";
	}
	p = strchr (line, ']');
	if (p != NULL && p[1] != '\0') {
		p++;
		while (*p == ' ') {
			p++;
		}
		if (p[0] != '\0' && p[1] == ':' && p[2] == ' ') {
			p += 3;
		}
		return p;
	}
	return line;
}

void BarWsLwsLogEmit (int level, const char *line)
{
	const char *msg = BarWsLwsLogMessage (line);

	if (msg[0] == '\0') {
		return;
	}

	if (level == LLL_ERR || level == LLL_WARN) {
		log_write (LOG_ERROR, "lws: %s", msg);
		return;
	}

	log_write (DEBUG_WEBSOCKET, "lws: %s", msg);
}

void BarWsLwsConfigureLogging (void)
{
	int lws_level = LLL_ERR | LLL_WARN;

	if (log_get_debug_mask () & DEBUG_WEBSOCKET) {
		lws_level |= LLL_NOTICE;
	}
	lws_set_log_level (lws_level, BarWsLwsLogEmit);
}
