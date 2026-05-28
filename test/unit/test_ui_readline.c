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
	sig_atomic_t saved = 7;
	sig_atomic_t local = 0;

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
	suite_add_tcase (s, tc);
	return s;
}
