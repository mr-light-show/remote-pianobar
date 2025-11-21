/*
Copyright (c) 2025
    Kyle Hawes <khawes@netflix.com>

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

#include "http_server.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <libwebsockets.h>

/* MIME type mappings */
typedef struct {
	const char *ext;
	const char *mime;
} MimeMapping_t;

static const MimeMapping_t mimeTypes[] = {
	{ ".html", "text/html" },
	{ ".htm", "text/html" },
	{ ".css", "text/css" },
	{ ".js", "application/javascript" },
	{ ".json", "application/json" },
	{ ".png", "image/png" },
	{ ".jpg", "image/jpeg" },
	{ ".jpeg", "image/jpeg" },
	{ ".gif", "image/gif" },
	{ ".svg", "image/svg+xml" },
	{ ".ico", "image/x-icon" },
	{ ".txt", "text/plain" },
	{ NULL, NULL }
};

/* Get MIME type from file extension */
const char *BarHttpGetMimeType(const char *path) {
	const char *ext;
	int i;
	
	if (!path) {
		return "application/octet-stream";
	}
	
	/* Find file extension */
	ext = strrchr(path, '.');
	if (!ext) {
		return "application/octet-stream";
	}
	
	/* Look up MIME type */
	for (i = 0; mimeTypes[i].ext != NULL; i++) {
		if (strcmp(ext, mimeTypes[i].ext) == 0) {
			return mimeTypes[i].mime;
		}
	}
	
	return "application/octet-stream";
}

/* Serve static file */
int BarHttpServeFile(struct lws *wsi, const char *filepath) {
	unsigned char buf[4096 + LWS_PRE];
	unsigned char *p = &buf[LWS_PRE];
	FILE *fp;
	size_t n;
	int m;
	const char *mimetype;
	
	if (!wsi || !filepath) {
		return -1;
	}
	
	fp = fopen(filepath, "rb");
	if (!fp) {
		/* File not found */
		lws_return_http_status(wsi, HTTP_STATUS_NOT_FOUND, NULL);
		return -1;
	}
	
	/* Get file size */
	fseek(fp, 0, SEEK_END);
	long filesize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	/* Get MIME type */
	mimetype = BarHttpGetMimeType(filepath);
	
	/* Send HTTP headers */
	if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
	                                 mimetype, filesize,
	                                 &p, &buf[sizeof(buf)]) < 0) {
		fclose(fp);
		return -1;
	}
	
	if (lws_finalize_write_http_header(wsi, buf + LWS_PRE,
	                                    &p, &buf[sizeof(buf)]) < 0) {
		fclose(fp);
		return -1;
	}
	
	/* Send file content */
	while ((n = fread(p, 1, sizeof(buf) - LWS_PRE, fp)) > 0) {
		m = lws_write(wsi, p, n, LWS_WRITE_HTTP);
		if (m < 0) {
			fclose(fp);
			return -1;
		}
	}
	
	fclose(fp);
	
	/* Close connection */
	if (lws_http_transaction_completed(wsi)) {
		return -1;
	}
	
	return 0;
}

/* Serve directory index (index.html) */
int BarHttpServeIndex(struct lws *wsi, const char *webui_path) {
	char filepath[512];
	
	if (!wsi || !webui_path) {
		return -1;
	}
	
	snprintf(filepath, sizeof(filepath), "%s/index.html", webui_path);
	return BarHttpServeFile(wsi, filepath);
}

/* HTTP callback handler */
int BarHttpCallback(struct lws *wsi, int reason,
                    void *user, void *in, size_t len) {
	/* TODO: Implement full HTTP callback handling */
	/* This is called by libwebsockets for HTTP protocol */
	/* For now, this is a stub - will be implemented when integrating */
	return 0;
}

