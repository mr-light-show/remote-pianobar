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
	{ ".woff", "font/woff" },
	{ ".woff2", "font/woff2" },
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

/* Serve static file with manual HTTP response construction */
int BarHttpServeFile(struct lws *wsi, const char *filepath) {
	unsigned char buffer[LWS_PRE + 4096];
	unsigned char *start = &buffer[LWS_PRE];
	unsigned char *p = start;
	unsigned char *end = &buffer[sizeof(buffer) - 1];
	FILE *fp;
	long filesize;
	char lenstr[32];
	int n;
	
	if (!wsi || !filepath) {
		return -1;
	}
	
	/* Open and validate file */
	fp = fopen(filepath, "rb");
	if (!fp) {
		lws_return_http_status(wsi, HTTP_STATUS_NOT_FOUND, NULL);
		return -1;
	}
	
	/* Get file size */
	fseek(fp, 0, SEEK_END);
	filesize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	/* Build HTTP response headers */
	if (lws_add_http_header_status(wsi, HTTP_STATUS_OK, &p, end)) {
		fclose(fp);
		return -1;
	}
	
	/* Content-Type */
	const char *mime = BarHttpGetMimeType(filepath);
	if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
	                                  (unsigned char *)mime, strlen(mime), &p, end)) {
		fclose(fp);
		return -1;
	}
	
	/* Content-Length */
	snprintf(lenstr, sizeof(lenstr), "%ld", filesize);
	if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_LENGTH,
	                                  (unsigned char *)lenstr, strlen(lenstr), &p, end)) {
		fclose(fp);
		return -1;
	}
	
	/* Content-Security-Policy - Allow Google Fonts */
	const char *csp = "default-src 'self'; "
	                  "style-src 'self' 'unsafe-inline' https://fonts.googleapis.com; "
	                  "font-src 'self' https://fonts.gstatic.com data:; "
	                  "connect-src 'self' ws: wss:; "
	                  "img-src 'self' http: https: data:; "
	                  "frame-ancestors 'none'; "
	                  "base-uri 'none'; "
	                  "form-action 'self';";
	if (lws_add_http_header_by_name(wsi, (unsigned char *)"content-security-policy:",
	                                 (unsigned char *)csp, strlen(csp), &p, end)) {
		fclose(fp);
		return -1;
	}
	
	/* Additional Security Headers */
	if (lws_add_http_header_by_name(wsi, (unsigned char *)"referrer-policy:",
	                                 (unsigned char *)"no-referrer", 11, &p, end)) {
		fclose(fp);
		return -1;
	}
	if (lws_add_http_header_by_name(wsi, (unsigned char *)"x-content-type-options:",
	                                 (unsigned char *)"nosniff", 7, &p, end)) {
		fclose(fp);
		return -1;
	}
	if (lws_add_http_header_by_name(wsi, (unsigned char *)"x-frame-options:",
	                                 (unsigned char *)"deny", 4, &p, end)) {
		fclose(fp);
		return -1;
	}
	if (lws_add_http_header_by_name(wsi, (unsigned char *)"x-xss-protection:",
	                                 (unsigned char *)"1; mode=block", 13, &p, end)) {
		fclose(fp);
		return -1;
	}
	
	/* Finalize headers */
	if (lws_finalize_http_header(wsi, &p, end)) {
		fclose(fp);
		return -1;
	}
	
	/* Send headers */
	n = lws_write(wsi, start, p - start, LWS_WRITE_HTTP_HEADERS);
	if (n < 0) {
		fclose(fp);
		return -1;
	}
	
	/* Stream file content */
	while (!feof(fp)) {
		n = fread(start, 1, sizeof(buffer) - LWS_PRE, fp);
		if (n > 0) {
			if (lws_write(wsi, start, n, LWS_WRITE_HTTP) < 0) {
				fclose(fp);
				return -1;
			}
		}
	}
	
	fclose(fp);
	
	/* Complete HTTP transaction */
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

