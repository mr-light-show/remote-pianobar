#pragma once

/*	Shared helpers for unit tests that run threads or sockets.
 *
 *	free_loopback_port: a loopback TCP port the kernel handed out and that was
 *	free a moment ago (nothing listens on it, so a connect is refused). Use it
 *	instead of hard-coded port numbers.
 *
 *	test_refused_url: an http:// URL on such a port, for "unreachable stream"
 *	tests.
 *
 *	wait_until: poll a predicate until it holds or timeout_ms passes. Use it
 *	instead of a fixed sleep that only lets another thread "get there".
 */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static inline int free_loopback_port (void) {
	int fd = socket (AF_INET, SOCK_STREAM, 0);
	ck_assert_int_ge (fd, 0);
	struct sockaddr_in addr;
	memset (&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	addr.sin_port = 0;
	ck_assert_int_eq (bind (fd, (struct sockaddr *) &addr, sizeof (addr)), 0);
	socklen_t len = sizeof (addr);
	ck_assert_int_eq (getsockname (fd, (struct sockaddr *) &addr, &len), 0);
	close (fd);
	return ntohs (addr.sin_port);
}

/*	A listening loopback socket on an ephemeral port (stored in *port) */
static inline int bind_loopback_listener (int *port) {
	int fd = socket (AF_INET, SOCK_STREAM, 0);
	ck_assert_int_ge (fd, 0);
	struct sockaddr_in addr;
	memset (&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	addr.sin_port = 0;
	ck_assert_int_eq (bind (fd, (struct sockaddr *) &addr, sizeof (addr)), 0);
	ck_assert_int_eq (listen (fd, 1), 0);
	socklen_t len = sizeof (addr);
	ck_assert_int_eq (getsockname (fd, (struct sockaddr *) &addr, &len), 0);
	*port = ntohs (addr.sin_port);
	return fd;
}

/*	True when something accepts a TCP connection on this loopback port */
static inline bool loopback_port_accepts (int port) {
	int fd = socket (AF_INET, SOCK_STREAM, 0);
	ck_assert_int_ge (fd, 0);
	struct sockaddr_in addr;
	memset (&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	addr.sin_port = htons (port);
	const bool accepts = connect (fd, (struct sockaddr *) &addr, sizeof (addr)) == 0;
	close (fd);
	return accepts;
}

/*	"http://127.0.0.1:<free port>/<path>" in out; returns out. Another process
 *	may take the port between the kernel handing it out and the caller using
 *	it, so the port is probed once and replaced by a second one if something
 *	already listens there. */
static inline char *test_refused_url (char *out, size_t size, const char *path) {
	int port = free_loopback_port ();
	if (loopback_port_accepts (port)) {
		port = free_loopback_port ();
	}
	snprintf (out, size, "http://127.0.0.1:%d/%s", port, path);
	return out;
}

typedef bool (*TestPredicate_fn) (void *arg);

static inline long long test_monotonic_ms (void) {
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return (long long) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/*	True as soon as pred(arg) holds; false if it still does not after
 *	timeout_ms (checked once more at the deadline) */
static inline bool wait_until (TestPredicate_fn pred, void *arg,
		unsigned int timeout_ms) {
	const long long deadline = test_monotonic_ms () + (long long) timeout_ms;
	while (!pred (arg)) {
		if (test_monotonic_ms () >= deadline) {
			return pred (arg);
		}
		usleep (1000);
	}
	return true;
}
