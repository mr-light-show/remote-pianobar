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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../src/log.h"

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

START_TEST(test_log_write_debug_state) {
	suppress_stderr();
	ck_assert_int_eq(setenv("PIANOBAR_DEBUG", "64", 1), 0);
	log_init();
	log_write(DEBUG_STATE, "codecov probe\n");
	restore_stderr();
	unsetenv("PIANOBAR_DEBUG");
}
END_TEST

#else

START_TEST(test_log_stub_no_debuglog) {
	ck_assert(!log_is_any_debug_enabled());
	log_write(DEBUG_STATE, "ignored\n");
}
END_TEST

#endif

Suite *log_suite(void) {
	Suite *s = suite_create("Log");
	TCase *tc = tcase_create("DEBUG_STATE");
#ifdef HAVE_DEBUGLOG
	tcase_add_test(tc, test_log_init_debug_state_banner);
	tcase_add_test(tc, test_log_write_debug_state);
#else
	tcase_add_test(tc, test_log_stub_no_debuglog);
#endif
	suite_add_tcase(s, tc);
	return s;
}
