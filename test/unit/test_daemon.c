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

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include "../../src/main.h"
#include "../../src/websocket/daemon/daemon.h"

/* Test: Write PID file with NULL app should fail */
START_TEST(test_daemon_write_pid_null_app) {
	bool result = BarDaemonWritePidFile(NULL);
	ck_assert_int_eq(result, false);
}
END_TEST

/* Test: Remove PID file with NULL app should not crash */
START_TEST(test_daemon_remove_pid_null_app) {
	BarDaemonRemovePidFile(NULL);
	/* If we get here without crashing, test passes */
	ck_assert(1);
}
END_TEST

/* Test: Daemonize with NULL app should fail */
START_TEST(test_daemon_daemonize_null) {
	bool result = BarDaemonize(NULL);
	ck_assert_int_eq(result, false);
}
END_TEST

/* Test: Check if daemon is running with NULL pid file */
START_TEST(test_daemon_is_running_null) {
	bool result = BarDaemonIsRunning(NULL);
	ck_assert_int_eq(result, false);
}
END_TEST

/* Test: Check if daemon is running with non-existent pid file */
START_TEST(test_daemon_is_running_nonexistent) {
	bool result = BarDaemonIsRunning("/tmp/nonexistent_pianobar.pid");
	ck_assert_int_eq(result, false);
}
END_TEST

START_TEST(test_daemon_get_ipv4_address_returns_nonempty_address) {
	char addr[64];
	memset(addr, 0, sizeof(addr));

	BarDaemonGetIPv4Address(addr, sizeof(addr));

	ck_assert_str_ne(addr, "");
}
END_TEST

/* Real-world: write PID file with a configured path, check process detection,
 * then remove it.  Mirrors what happens when the daemon launches. */
START_TEST(test_daemon_pid_file_write_detect_remove_roundtrip) {
	const char *pidPath = "/tmp/test_pianobar_daemon_roundtrip.pid";
	unlink(pidPath);

	BarApp_t app;
	memset(&app, 0, sizeof(app));
	app.settings.pidFile = strdup(pidPath);

	ck_assert(BarDaemonWritePidFile(&app));
	struct stat st;
	ck_assert_int_eq(stat(pidPath, &st), 0);

	/* The PID written is our own — BarDaemonIsRunning should see us alive. */
	ck_assert(BarDaemonIsRunning(pidPath));

	BarDaemonRemovePidFile(&app);
	ck_assert_int_ne(stat(pidPath, &st), 0);

	free(app.settings.pidFile);
}
END_TEST

START_TEST(test_daemon_is_running_with_dead_pid) {
	const char *pidPath = "/tmp/test_pianobar_daemon_dead.pid";
	FILE *f = fopen(pidPath, "w");
	ck_assert_ptr_nonnull(f);
	/* PID that almost certainly does not exist on test hosts. */
	fprintf(f, "2147483646\n");
	fclose(f);

	ck_assert(!BarDaemonIsRunning(pidPath));
	unlink(pidPath);
}
END_TEST

START_TEST(test_daemon_get_lock_file_path_is_under_config) {
	char *path = BarDaemonGetLockFilePath();
	ck_assert_ptr_nonnull(path);
	ck_assert(strstr(path, "pianobar.lock") != NULL);
	free(path);
}
END_TEST

/* Create test suite */
Suite *daemon_suite(void) {
	Suite *s;
	TCase *tc_core;
	
	s = suite_create("Daemon");
	tc_core = tcase_create("Core");
	
	tcase_add_test(tc_core, test_daemon_write_pid_null_app);
	tcase_add_test(tc_core, test_daemon_remove_pid_null_app);
	tcase_add_test(tc_core, test_daemon_daemonize_null);
	tcase_add_test(tc_core, test_daemon_is_running_null);
	tcase_add_test(tc_core, test_daemon_is_running_nonexistent);
	tcase_add_test(tc_core, test_daemon_get_ipv4_address_returns_nonempty_address);
	tcase_add_test(tc_core, test_daemon_pid_file_write_detect_remove_roundtrip);
	tcase_add_test(tc_core, test_daemon_is_running_with_dead_pid);
	tcase_add_test(tc_core, test_daemon_get_lock_file_path_is_under_config);
	
	suite_add_tcase(s, tc_core);
	
	return s;
}

