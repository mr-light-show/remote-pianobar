/*
Copyright (c) 2026

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
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../src/interrupt.h"
#include "../../src/ui_readline.h"

static const BarReadlineFlags_t RL_TEST_FLAGS =
		BAR_RL_NOECHO | BAR_RL_NOINT;

static void rl_input_from_fd (BarReadlineFds_t *input, int read_fd) {
	memset (input, 0, sizeof (*input));
	FD_ZERO (&input->set);
	FD_SET (read_fd, &input->set);
	input->fds[0] = read_fd;
	input->fds[1] = -1;
	input->maxfd = read_fd + 1;
}

static void rl_write_input (int write_fd, const char *data) {
	ck_assert (write (write_fd, data, strlen (data)) >= 0);
	close (write_fd);
}

/* Typical CLI prompt: user types a station name and presses Enter. */
START_TEST (test_readline_reads_line_until_newline)
{
	int pipefd[2];
	char buf[32];
	BarReadlineFds_t input;

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], "my station\n");
	rl_input_from_fd (&input, pipefd[0]);

	ck_assert_uint_eq (BarReadline (buf, sizeof (buf), NULL, &input,
	                                RL_TEST_FLAGS, -1), 10);
	ck_assert_str_eq (buf, "my station");
	close (pipefd[0]);
}
END_TEST

START_TEST (test_readline_str_wrapper_matches_bar_readline)
{
	int pipefd[2];
	char buf[16];
	BarReadlineFds_t input;

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], "hello\n");
	rl_input_from_fd (&input, pipefd[0]);

	ck_assert_uint_eq (BarReadlineStr (buf, sizeof (buf), &input,
	                                   RL_TEST_FLAGS), 5);
	ck_assert_str_eq (buf, "hello");
	close (pipefd[0]);
}
END_TEST

/* Numeric prompts (volume, station index) accept only digits from the mask. */
START_TEST (test_readline_mask_rejects_non_matching_chars)
{
	int pipefd[2];
	char buf[16];
	BarReadlineFds_t input;

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], "a1b2c3\n");
	rl_input_from_fd (&input, pipefd[0]);

	ck_assert_uint_eq (BarReadline (buf, sizeof (buf), "0123456789", &input,
	                                RL_TEST_FLAGS, -1), 3);
	ck_assert_str_eq (buf, "123");
	close (pipefd[0]);
}
END_TEST

START_TEST (test_readline_int_parses_valid_number)
{
	int pipefd[2];
	int value = -1;
	BarReadlineFds_t input;

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], "42\n");
	rl_input_from_fd (&input, pipefd[0]);

	ck_assert_uint_eq (BarReadlineInt (&value, &input), 2);
	ck_assert_int_eq (value, 42);
	close (pipefd[0]);
}
END_TEST

START_TEST (test_readline_int_rejects_out_of_range_values)
{
	int pipefd[2];
	int value = -1;
	BarReadlineFds_t input;

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], "2147483648\n");
	rl_input_from_fd (&input, pipefd[0]);

	BarReadlineInt (&value, &input);
	ck_assert_int_eq (value, 0);
	close (pipefd[0]);
}
END_TEST

START_TEST (test_readline_yes_no_accepts_y_and_n)
{
	int pipefd[2];
	BarReadlineFds_t input;

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], "y\n");
	rl_input_from_fd (&input, pipefd[0]);
	ck_assert (BarReadlineYesNo (false, &input));
	close (pipefd[0]);

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], "N\n");
	rl_input_from_fd (&input, pipefd[0]);
	ck_assert (!BarReadlineYesNo (true, &input));
	close (pipefd[0]);
}
END_TEST

START_TEST (test_readline_yes_no_default_on_empty_line)
{
	int pipefd[2];
	BarReadlineFds_t input;

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], "\n");
	rl_input_from_fd (&input, pipefd[0]);
	ck_assert (BarReadlineYesNo (true, &input));
	close (pipefd[0]);

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], "\n");
	rl_input_from_fd (&input, pipefd[0]);
	ck_assert (!BarReadlineYesNo (false, &input));
	close (pipefd[0]);
}
END_TEST

START_TEST (test_readline_backspace_edits_buffer)
{
	int pipefd[2];
	char buf[16];
	BarReadlineFds_t input;

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], "ab\bX\n");
	rl_input_from_fd (&input, pipefd[0]);

	ck_assert_uint_eq (BarReadline (buf, sizeof (buf), NULL, &input,
	                                RL_TEST_FLAGS, -1), 2);
	ck_assert_str_eq (buf, "aX");
	close (pipefd[0]);
}
END_TEST

START_TEST (test_readline_ctrl_u_clears_line)
{
	int pipefd[2];
	char buf[16];
	BarReadlineFds_t input;

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], "hello\x15x\n");
	rl_input_from_fd (&input, pipefd[0]);

	ck_assert_uint_eq (BarReadline (buf, sizeof (buf), NULL, &input,
	                                RL_TEST_FLAGS, -1), 1);
	ck_assert_str_eq (buf, "x");
	close (pipefd[0]);
}
END_TEST

START_TEST (test_readline_fullreturn_when_buffer_fills)
{
	int pipefd[2];
	char buf[4];
	BarReadlineFds_t input;

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], "abcd");
	rl_input_from_fd (&input, pipefd[0]);

	ck_assert_uint_eq (BarReadline (buf, sizeof (buf), NULL, &input,
	                                BAR_RL_NOECHO | BAR_RL_NOINT |
	                                BAR_RL_FULLRETURN, -1), 3);
	ck_assert_str_eq (buf, "abc");
	close (pipefd[0]);
}
END_TEST

START_TEST (test_readline_timeout_returns_empty)
{
	int pipefd[2];
	char buf[16];
	BarReadlineFds_t input;

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_input_from_fd (&input, pipefd[0]);

	ck_assert_uint_eq (BarReadline (buf, sizeof (buf), NULL, &input,
	                                RL_TEST_FLAGS, 0), 0);
	ck_assert_str_eq (buf, "");
	close (pipefd[0]);
	close (pipefd[1]);
}
END_TEST

START_TEST (test_readline_restores_interrupt_target)
{
	int pipefd[2];
	char buf[8];
	BarReadlineFds_t input;
	_Atomic sig_atomic_t saved = 7;
	_Atomic sig_atomic_t local = 0;

	BarInterruptSetTarget (&saved);
	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], "x\n");
	rl_input_from_fd (&input, pipefd[0]);

	BarReadline (buf, sizeof (buf), NULL, &input, BAR_RL_NOECHO, -1);
	ck_assert_ptr_eq (BarInterruptGetTarget (), &saved);
	(void)local;
	close (pipefd[0]);
}
END_TEST

/* Web-only mode has no terminal: the input set is empty and maxfd is 0.
 * A prompt must return as if cancelled instead of blocking in select()
 * forever (which would stall the WebSocket command worker). */
START_TEST (test_readline_without_input_fds_returns_immediately)
{
	char buf[8] = "junk";
	BarReadlineFds_t input;

	memset (&input, 0, sizeof (input));
	FD_ZERO (&input.set);

	ck_assert_uint_eq (BarReadline (buf, sizeof (buf), NULL, &input,
	                                RL_TEST_FLAGS, -1), 0);
	ck_assert_str_eq (buf, "");
}
END_TEST

START_TEST (test_readline_wrappers_without_input_fds_do_not_block)
{
	char buf[8];
	int num = 5;
	BarReadlineFds_t input;

	memset (&input, 0, sizeof (input));
	FD_ZERO (&input.set);

	ck_assert_uint_eq (BarReadlineStr (buf, sizeof (buf), &input,
	                                   RL_TEST_FLAGS), 0);
	ck_assert_uint_eq (BarReadlineInt (&num, &input), 0);
	ck_assert (BarReadlineYesNo (true, &input));
	ck_assert (!BarReadlineYesNo (false, &input));
}
END_TEST

/* Run BarReadline over raw bytes (may contain NUL-free control bytes) into a
 * heap buffer of exactly bufSize bytes, so ASan sees any access outside it. */
static size_t rl_run_heap (const char *data, size_t bufSize,
		BarReadlineFlags_t flags, char *out, size_t outSize) {
	int pipefd[2];
	BarReadlineFds_t input;
	char *buf = malloc (bufSize);
	ck_assert_ptr_nonnull (buf);

	ck_assert_int_eq (pipe (pipefd), 0);
	rl_write_input (pipefd[1], data);
	rl_input_from_fd (&input, pipefd[0]);

	const size_t len = BarReadline (buf, bufSize, NULL, &input, flags, -1);
	ck_assert_uint_lt (len, bufSize);
	ck_assert_uint_eq (strlen (buf), len);
	ck_assert_uint_lt (len, outSize);
	memcpy (out, buf, len + 1);
	free (buf);
	close (pipefd[0]);
	return len;
}

/* Full buffer ending in a multi-byte char: backspace must not read past the
 * end of the buffer. */
START_TEST (test_readline_backspace_full_buffer_multibyte)
{
	char out[16];

	ck_assert_uint_eq (rl_run_heap ("abcde\xc3\xa9\x7f\n", 8, RL_TEST_FLAGS,
	                                out, sizeof (out)), 5);
	ck_assert_str_eq (out, "abcde");
	ck_assert_uint_eq (rl_run_heap ("abcd\xe2\x82\xac\x7f\n", 8, RL_TEST_FLAGS,
	                                out, sizeof (out)), 4);
	ck_assert_str_eq (out, "abcd");
}
END_TEST

/* A stray continuation byte at buf[0]: backspace must not walk before it. */
START_TEST (test_readline_backspace_stray_continuation_byte)
{
	char out[16];

	ck_assert_uint_eq (rl_run_heap ("\x80\x7f\n", 8, RL_TEST_FLAGS,
	                                out, sizeof (out)), 0);
	ck_assert_str_eq (out, "");
	ck_assert_uint_eq (rl_run_heap ("\x80\x7fok\n", 8, RL_TEST_FLAGS,
	                                out, sizeof (out)), 2);
	ck_assert_str_eq (out, "ok");
}
END_TEST

/* Buffer holding only continuation bytes: each backspace stays in bounds and
 * the line can be fully erased. */
START_TEST (test_readline_backspace_all_continuation_bytes)
{
	char out[16];

	ck_assert_uint_eq (rl_run_heap ("\x80\xbf\x80\x7fx\n", 8, RL_TEST_FLAGS,
	                                out, sizeof (out)), 1);
	ck_assert_str_eq (out, "x");
	ck_assert_uint_eq (rl_run_heap ("\x80\x80\x80\x80\x80\x80\x80\x7f\n", 8,
	                                RL_TEST_FLAGS, out, sizeof (out)), 0);
	ck_assert_str_eq (out, "");
}
END_TEST

/* Ctrl-U with echo walks the buffer backwards char by char; it must stop at
 * buf[0] even when the buffer starts with continuation bytes. */
START_TEST (test_readline_ctrl_u_stray_continuation_bytes)
{
	char out[16];
	const BarReadlineFlags_t echoFlags = BAR_RL_NOINT;

	ck_assert_uint_eq (rl_run_heap ("\x80\x15x\n", 8, echoFlags,
	                                out, sizeof (out)), 1);
	ck_assert_str_eq (out, "x");
	ck_assert_uint_eq (rl_run_heap ("\x80\x80\x80\x15y\n", 8, echoFlags,
	                                out, sizeof (out)), 1);
	ck_assert_str_eq (out, "y");
	ck_assert_uint_eq (rl_run_heap ("a\xc3\xa9\xe2\x82\xac\x15z\n", 8,
	                                echoFlags, out, sizeof (out)), 1);
	ck_assert_str_eq (out, "z");
}
END_TEST

/* Valid UTF-8: backspace removes the whole 2- or 3-byte character. */
START_TEST (test_readline_backspace_removes_whole_utf8_char)
{
	char out[16];

	ck_assert_uint_eq (rl_run_heap ("x\xc3\xa9\x7f\n", 16, RL_TEST_FLAGS,
	                                out, sizeof (out)), 1);
	ck_assert_str_eq (out, "x");
	ck_assert_uint_eq (rl_run_heap ("x\xe2\x82\xac\x7f\n", 16, RL_TEST_FLAGS,
	                                out, sizeof (out)), 1);
	ck_assert_str_eq (out, "x");
	ck_assert_uint_eq (rl_run_heap ("\xc3\xa9\xe2\x82\xac\x7f\n", 16,
	                                RL_TEST_FLAGS, out, sizeof (out)), 2);
	ck_assert_str_eq (out, "\xc3\xa9");
	ck_assert_uint_eq (rl_run_heap ("\xc3\xa9\x7f\x7f\n", 16, RL_TEST_FLAGS,
	                                out, sizeof (out)), 0);
	ck_assert_str_eq (out, "");
}
END_TEST

Suite *ui_readline_suite (void)
{
	Suite *s = suite_create ("ui_readline");
	TCase *tc = tcase_create ("core");

	tcase_add_test (tc, test_readline_reads_line_until_newline);
	tcase_add_test (tc, test_readline_str_wrapper_matches_bar_readline);
	tcase_add_test (tc, test_readline_mask_rejects_non_matching_chars);
	tcase_add_test (tc, test_readline_int_parses_valid_number);
	tcase_add_test (tc, test_readline_int_rejects_out_of_range_values);
	tcase_add_test (tc, test_readline_yes_no_accepts_y_and_n);
	tcase_add_test (tc, test_readline_yes_no_default_on_empty_line);
	tcase_add_test (tc, test_readline_backspace_edits_buffer);
	tcase_add_test (tc, test_readline_ctrl_u_clears_line);
	tcase_add_test (tc, test_readline_fullreturn_when_buffer_fills);
	tcase_add_test (tc, test_readline_timeout_returns_empty);
	tcase_add_test (tc, test_readline_restores_interrupt_target);
	tcase_add_test (tc, test_readline_without_input_fds_returns_immediately);
	tcase_add_test (tc, test_readline_wrappers_without_input_fds_do_not_block);
	tcase_add_test (tc, test_readline_backspace_full_buffer_multibyte);
	tcase_add_test (tc, test_readline_backspace_stray_continuation_byte);
	tcase_add_test (tc, test_readline_backspace_all_continuation_bytes);
	tcase_add_test (tc, test_readline_ctrl_u_stray_continuation_bytes);
	tcase_add_test (tc, test_readline_backspace_removes_whole_utf8_char);
	suite_add_tcase (s, tc);
	return s;
}
