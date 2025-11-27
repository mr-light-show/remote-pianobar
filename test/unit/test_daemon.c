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
	
	suite_add_tcase(s, tc_core);
	
	return s;
}

