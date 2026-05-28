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

#include "fixture_http.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void serve_client (BarFixtureHttp_t *srv, int client_fd) {
	char request[4096];
	(void)read (client_fd, request, sizeof (request) - 1);

	if (srv->mode == BAR_FIXTURE_HTTP_NOT_FOUND) {
		const char *not_found =
		    "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
		(void)write (client_fd, not_found, strlen (not_found));
		close (client_fd);
		return;
	}

	FILE *fp = fopen (srv->filepath, "rb");
	if (fp == NULL) {
		const char *not_found =
		    "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
		(void)write (client_fd, not_found, strlen (not_found));
		close (client_fd);
		return;
	}

	fseek (fp, 0, SEEK_END);
	const long file_size = ftell (fp);
	fseek (fp, 0, SEEK_SET);

	char header[256];
	const int header_len = snprintf (header, sizeof (header),
	    "HTTP/1.1 200 OK\r\n"
	    "Content-Type: audio/mpeg\r\n"
	    "Content-Length: %ld\r\n"
	    "Connection: close\r\n\r\n",
	    file_size);
	(void)write (client_fd, header, (size_t)header_len);

	char chunk[4096];
	size_t read_bytes;
	while ((read_bytes = fread (chunk, 1, sizeof (chunk), fp)) > 0) {
		(void)write (client_fd, chunk, read_bytes);
	}
	fclose (fp);
	close (client_fd);
}

static void *fixture_http_thread (void *arg) {
	BarFixtureHttp_t *srv = arg;

	while (!srv->stop) {
		struct pollfd pfd = {
			.fd = srv->listen_fd,
			.events = POLLIN,
		};
		const int poll_ms = 100;
		const int ready = poll (&pfd, 1, poll_ms);

		if (srv->stop) {
			break;
		}
		if (ready < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		if (ready == 0) {
			continue;
		}

		struct sockaddr_in client;
		socklen_t client_len = sizeof (client);
		const int client_fd = accept (srv->listen_fd,
		                              (struct sockaddr *)&client,
		                              &client_len);
		if (client_fd < 0) {
			if (srv->stop || errno == EINTR) {
				break;
			}
			continue;
		}

		serve_client (srv, client_fd);
	}

	return NULL;
}

bool BarFixtureHttpStart (BarFixtureHttp_t *srv, const char *filepath,
                          uint16_t *port_out) {
	memset (srv, 0, sizeof (*srv));
	srv->listen_fd = -1;
	srv->mode = BAR_FIXTURE_HTTP_OK;
	strncpy (srv->filepath, filepath, sizeof (srv->filepath) - 1);

	const int fd = socket (AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		return false;
	}

	const int yes = 1;
	(void)setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (yes));

	struct sockaddr_in addr;
	memset (&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	addr.sin_port = 0;

	if (bind (fd, (struct sockaddr *)&addr, sizeof (addr)) < 0) {
		close (fd);
		return false;
	}

	socklen_t addr_len = sizeof (addr);
	if (getsockname (fd, (struct sockaddr *)&addr, &addr_len) < 0) {
		close (fd);
		return false;
	}

	srv->port = ntohs (addr.sin_port);
	srv->listen_fd = fd;

	if (listen (fd, 1) < 0) {
		close (fd);
		srv->listen_fd = -1;
		return false;
	}

	if (pthread_create (&srv->thread, NULL, fixture_http_thread, srv) != 0) {
		close (fd);
		srv->listen_fd = -1;
		return false;
	}

	if (port_out != NULL) {
		*port_out = srv->port;
	}
	return true;
}

void BarFixtureHttpStop (BarFixtureHttp_t *srv) {
	if (srv == NULL) {
		return;
	}

	srv->stop = 1;
	if (srv->listen_fd >= 0) {
		(void)shutdown (srv->listen_fd, SHUT_RDWR);
		close (srv->listen_fd);
		srv->listen_fd = -1;
	}
	pthread_join (srv->thread, NULL);
}
