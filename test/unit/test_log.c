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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <check.h>

#include "../../src/log.h"

/* --- debug mask API (always available) --- */

START_TEST (test_log_debug_mask_set_get_roundtrip)
{
	log_set_debug_mask (0xFF);
	ck_assert_uint_eq (log_get_debug_mask (), 0xFF);
	log_set_debug_mask (0x00);
	ck_assert_uint_eq (log_get_debug_mask (), 0x00);
}
END_TEST

START_TEST (test_log_debug_mask_zero_default)
{
	log_set_debug_mask (0);
	ck_assert_uint_eq (log_get_debug_mask (), 0);
}
END_TEST

START_TEST (test_log_debug_mask_partial_bits)
{
	log_set_debug_mask (0x05);
	ck_assert_uint_eq (log_get_debug_mask (), 0x05);
	log_set_debug_mask (0);
}
END_TEST

/* --- HAVE_DEBUGLOG path --- */

#ifndef HAVE_DEBUGLOG

START_TEST(test_log_stub_no_debuglog) {
	ck_assert(!log_is_any_debug_enabled());
	log_write(DEBUG_STATE, "ignored\n");
}
END_TEST

#endif /* !HAVE_DEBUGLOG */

#ifdef HAVE_DEBUGLOG

static int stderr_dup = -1;

static void suppress_stderr(void) {
	stderr_dup = dup(STDERR_FILENO);
	ck_assert(stderr_dup >= 0);
	FILE *null_out = fopen("/dev/null", "w");
	ck_assert(null_out != NULL);
	ck_assert(dup2(fileno(null_out), STDERR_FILENO) >= 0);
	fclose(null_out);
}

static void restore_stderr(void) {
	if (stderr_dup >= 0) {
		dup2(stderr_dup, STDERR_FILENO);
		close(stderr_dup);
		stderr_dup = -1;
	}
}

START_TEST(test_log_init_debug_state_banner) {
	suppress_stderr();
	ck_assert_int_eq(setenv("PIANOBAR_DEBUG", "64", 1), 0);
	log_init();
	ck_assert(log_is_any_debug_enabled());
	restore_stderr();
	unsetenv("PIANOBAR_DEBUG");
}
END_TEST

START_TEST(test_log_init_debug_ui_without_state) {
	suppress_stderr();
	ck_assert_int_eq(setenv("PIANOBAR_DEBUG", "8", 1), 0);
	log_init();
	ck_assert(log_is_any_debug_enabled());
	restore_stderr();
	unsetenv("PIANOBAR_DEBUG");
}
END_TEST

START_TEST(test_log_write_debug_state) {
	suppress_stderr();
	ck_assert_int_eq(setenv("PIANOBAR_DEBUG", "64", 1), 0);
	log_init();
	log_write(DEBUG_STATE, "codecov probe\n");
	restore_stderr();
	unsetenv("PIANOBAR_DEBUG");
}
END_TEST

START_TEST(test_log_write_unknown_kind) {
	suppress_stderr();
	ck_assert_int_eq(setenv("PIANOBAR_DEBUG", "255", 1), 0);
	log_init();
	log_write((logKind)128, "unknown kind probe\n");
	restore_stderr();
	unsetenv("PIANOBAR_DEBUG");
}
END_TEST

static void capture_stderr_to_pipe(int pipefd[2], int *saved_stderr)
{
	ck_assert_int_eq(pipe(pipefd), 0);
	*saved_stderr = dup(STDERR_FILENO);
	ck_assert(*saved_stderr >= 0);
	ck_assert(dup2(pipefd[1], STDERR_FILENO) >= 0);
	close(pipefd[1]);
}

START_TEST(test_log_network_request_and_response) {
	int pipefd[2];
	int saved_stderr;
	char buf[4096];
	ssize_t n;

	capture_stderr_to_pipe(pipefd, &saved_stderr);
	ck_assert_int_eq(setenv("PIANOBAR_DEBUG", "1", 1), 0);
	log_init();
	log_network_request("http://cdn.example/track.mp3");
	log_network_response("ok");
	fflush(stderr);
	dup2(saved_stderr, STDERR_FILENO);
	close(saved_stderr);
	n = read(pipefd[0], buf, sizeof(buf) - 1);
	close(pipefd[0]);
	ck_assert(n > 0);
	buf[n] = '\0';
	ck_assert(strstr(buf, "Network") != NULL);
	ck_assert(strstr(buf, "← http://cdn.example/track.mp3") != NULL);
	ck_assert(strstr(buf, "→ ok") != NULL);
	unsetenv("PIANOBAR_DEBUG");
}
END_TEST

START_TEST(test_log_network_respects_mask) {
	int pipefd[2];
	int saved_stderr;
	char buf[256];
	ssize_t n;

	capture_stderr_to_pipe(pipefd, &saved_stderr);
	ck_assert_int_eq(setenv("PIANOBAR_DEBUG", "2", 1), 0);
	log_init();
	log_network_request("http://cdn.example/track.mp3");
	fflush(stderr);
	dup2(saved_stderr, STDERR_FILENO);
	close(saved_stderr);
	n = read(pipefd[0], buf, sizeof(buf));
	close(pipefd[0]);
	ck_assert_int_eq(n, 0);
	unsetenv("PIANOBAR_DEBUG");
}
END_TEST

#endif /* HAVE_DEBUGLOG */

Suite *log_suite (void) {
	Suite *s = suite_create ("Log");

	TCase *tc_mask = tcase_create ("debug_mask");
	tcase_add_test (tc_mask, test_log_debug_mask_set_get_roundtrip);
	tcase_add_test (tc_mask, test_log_debug_mask_zero_default);
	tcase_add_test (tc_mask, test_log_debug_mask_partial_bits);
	suite_add_tcase (s, tc_mask);

	TCase *tc_debug = tcase_create ("DEBUG_STATE");
#ifdef HAVE_DEBUGLOG
	tcase_add_test (tc_debug, test_log_init_debug_state_banner);
	tcase_add_test (tc_debug, test_log_init_debug_ui_without_state);
	tcase_add_test (tc_debug, test_log_write_debug_state);
	tcase_add_test (tc_debug, test_log_write_unknown_kind);
	tcase_add_test (tc_debug, test_log_network_request_and_response);
	tcase_add_test (tc_debug, test_log_network_respects_mask);
#endif
#ifndef HAVE_DEBUGLOG
	tcase_add_test (tc_debug, test_log_stub_no_debuglog);
#endif
	suite_add_tcase (s, tc_debug);

	return s;
}
