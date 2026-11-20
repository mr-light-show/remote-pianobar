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
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <check.h>
#include <wchar.h>
#include <pthread.h>
#include <sys/stat.h>

#include "../../src/log.h"
#include "../test_tmp.h"

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

/* Whether log_write would emit a kind: errors always; DEBUG_* only for set
 * bits, and never without HAVE_DEBUGLOG */
START_TEST (test_log_is_enabled)
{
	log_set_debug_mask (0);
	ck_assert (log_is_enabled (LOG_ERROR));
	ck_assert (!log_is_enabled (DEBUG_WEBSOCKET));
	log_set_debug_mask (DEBUG_WEBSOCKET);
	ck_assert (log_is_enabled (LOG_ERROR));
#ifdef HAVE_DEBUGLOG
	ck_assert (log_is_enabled (DEBUG_WEBSOCKET));
#else
	ck_assert (!log_is_enabled (DEBUG_WEBSOCKET));
#endif
	ck_assert (!log_is_enabled (DEBUG_AUDIO));
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
	log_init(0);
	ck_assert(log_is_any_debug_enabled());
	restore_stderr();
	unsetenv("PIANOBAR_DEBUG");
}
END_TEST

START_TEST(test_log_init_config_debug_mask) {
	suppress_stderr();
	unsetenv("PIANOBAR_DEBUG");
	log_init(15);
	ck_assert(log_is_any_debug_enabled());
	ck_assert((log_get_debug_mask() & DEBUG_NETWORK) != 0);
	ck_assert((log_get_debug_mask() & DEBUG_WEBSOCKET) != 0);
	log_set_debug_mask(0);
	restore_stderr();
}
END_TEST

START_TEST(test_log_init_env_overrides_config) {
	suppress_stderr();
	ck_assert_int_eq(setenv("PIANOBAR_DEBUG", "64", 1), 0);
	log_init(15);
	ck_assert((log_get_debug_mask() & DEBUG_STATE) != 0);
	ck_assert((log_get_debug_mask() & DEBUG_NETWORK) == 0);
	log_set_debug_mask(0);
	restore_stderr();
	unsetenv("PIANOBAR_DEBUG");
}
END_TEST

START_TEST(test_log_init_debug_ui_without_state) {
	suppress_stderr();
	ck_assert_int_eq(setenv("PIANOBAR_DEBUG", "8", 1), 0);
	log_init(0);
	ck_assert(log_is_any_debug_enabled());
	restore_stderr();
	unsetenv("PIANOBAR_DEBUG");
}
END_TEST

START_TEST(test_log_write_debug_state) {
	suppress_stderr();
	ck_assert_int_eq(setenv("PIANOBAR_DEBUG", "64", 1), 0);
	log_init(0);
	log_write(DEBUG_STATE, "codecov probe\n");
	restore_stderr();
	unsetenv("PIANOBAR_DEBUG");
}
END_TEST

START_TEST(test_log_write_unknown_kind) {
	suppress_stderr();
	ck_assert_int_eq(setenv("PIANOBAR_DEBUG", "255", 1), 0);
	log_init(0);
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
	log_init(0);
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
	/* Use log_set_debug_mask (not log_init) so stderr is not polluted by the
	 * PIANOBAR_DEBUG startup banner when only non-Network bits are enabled. */
	log_set_debug_mask(DEBUG_AUDIO);
	log_network_request("http://cdn.example/track.mp3");
	fflush(stderr);
	dup2(saved_stderr, STDERR_FILENO);
	close(saved_stderr);
	n = read(pipefd[0], buf, sizeof(buf));
	close(pipefd[0]);
	ck_assert_int_eq(n, 0);
	log_set_debug_mask(0);
}
END_TEST

#endif /* HAVE_DEBUGLOG */

/* --- log_file: copy of the log lines for ui_mode both/cli --- */

static int file_test_stderr_saved = -1;

static void file_test_silence_stderr (void) {
	fflush (stderr);
	file_test_stderr_saved = dup (STDERR_FILENO);
	ck_assert (file_test_stderr_saved >= 0);
	int nullFd = open ("/dev/null", O_WRONLY);
	ck_assert (nullFd >= 0);
	ck_assert (dup2 (nullFd, STDERR_FILENO) >= 0);
	close (nullFd);
}

static void file_test_restore_stderr (void) {
	fflush (stderr);
	ck_assert (dup2 (file_test_stderr_saved, STDERR_FILENO) >= 0);
	close (file_test_stderr_saved);
	file_test_stderr_saved = -1;
}

/* Fresh scratch dir and log path for one test. */
static void file_test_paths (char *dir, size_t dirSize, char *path,
		size_t pathSize, const char *prefix) {
	ck_assert_ptr_nonnull (TestTmpMkdtemp (dir, dirSize, prefix));
	snprintf (path, pathSize, "%s/pianobar.log", dir);
}

static char *file_test_read (const char *path) {
	FILE *f = fopen (path, "r");
	ck_assert_ptr_nonnull (f);
	fseek (f, 0, SEEK_END);
	long len = ftell (f);
	ck_assert (len >= 0);
	rewind (f);
	char *buf = calloc ((size_t) len + 1, 1);
	ck_assert_ptr_nonnull (buf);
	ck_assert_int_eq ((long) fread (buf, 1, (size_t) len, f), len);
	fclose (f);
	return buf;
}

static void file_test_cleanup (const char *dir, const char *path) {
	unlink (path);
	rmdir (dir);
}

START_TEST (test_log_file_receives_error_lines)
{
	char dir[256], path[300];
	file_test_paths (dir, sizeof (dir), path, sizeof (path), "log_err");

	log_set_debug_mask (0);
	file_test_silence_stderr ();
	ck_assert (log_set_file (path));
	log_write (LOG_ERROR, "boom %d\n", 1);
	log_close_file ();
	file_test_restore_stderr ();

	char *text = file_test_read (path);
	ck_assert_ptr_nonnull (strstr (text, "boom 1\n"));
	ck_assert_ptr_null (strstr (text, "\033["));
#ifdef HAVE_DEBUGLOG
	ck_assert_int_eq (text[0], '[');
	ck_assert_ptr_nonnull (strstr (text, "Error"));
#endif
	free (text);
	file_test_cleanup (dir, path);
}
END_TEST

/* Lines are appended, and a later log_set_file switches files. */
START_TEST (test_log_file_appends_and_reopens)
{
	char dir[256], path[300], path2[300];
	file_test_paths (dir, sizeof (dir), path, sizeof (path), "log_app");
	snprintf (path2, sizeof (path2), "%s/second.log", dir);

	log_set_debug_mask (0);
	file_test_silence_stderr ();
	ck_assert (log_set_file (path));
	log_write (LOG_ERROR, "first\n");
	log_close_file ();
	ck_assert (log_set_file (path));
	log_write (LOG_ERROR, "second\n");
	ck_assert (log_set_file (path2));
	log_write (LOG_ERROR, "third\n");
	log_close_file ();
	log_close_file ();	/* idempotent */
	log_write (LOG_ERROR, "after close\n");
	file_test_restore_stderr ();

	char *a = file_test_read (path);
	char *b = file_test_read (path2);
	ck_assert_ptr_nonnull (strstr (a, "first\n"));
	ck_assert_ptr_nonnull (strstr (a, "second\n"));
	ck_assert_ptr_null (strstr (a, "third"));
	ck_assert_ptr_null (strstr (a, "after close"));
	ck_assert_ptr_nonnull (strstr (b, "third\n"));
	ck_assert_ptr_null (strstr (b, "second"));
	free (a);
	free (b);
	unlink (path2);
	file_test_cleanup (dir, path);
}
END_TEST

START_TEST (test_log_file_rejects_null_and_empty_path)
{
	file_test_silence_stderr ();
	ck_assert (!log_set_file (NULL));
	ck_assert (!log_set_file (""));
	file_test_restore_stderr ();
}
END_TEST

/* A log line longer than the formatting buffer is written whole. */
/* A format vsnprintf rejects (an unconvertible wide character) must not
 * write a partial line to the file or disturb later lines. */
START_TEST (test_log_file_skips_unformattable_line)
{
	char dir[256], path[300];
	const wchar_t bad[] = { (wchar_t) 0x7fffffff, 0 };
	file_test_paths (dir, sizeof (dir), path, sizeof (path), "log_badfmt");

	log_set_debug_mask (0);
	file_test_silence_stderr ();
	ck_assert (log_set_file (path));
	log_write (LOG_ERROR, "bad %ls\n", bad);
	log_write (LOG_ERROR, "good line\n");
	log_close_file ();
	file_test_restore_stderr ();

	char *text = file_test_read (path);
	ck_assert_ptr_nonnull (strstr (text, "good line\n"));
	ck_assert_ptr_null (strstr (text, "bad "));
	free (text);
	file_test_cleanup (dir, path);
}
END_TEST

START_TEST (test_log_file_long_line_not_truncated)
{
	char dir[256], path[300];
	char payload[6000];
	file_test_paths (dir, sizeof (dir), path, sizeof (path), "log_long");

	memset (payload, 'x', sizeof (payload) - 1);
	payload[sizeof (payload) - 1] = '\0';
	log_set_debug_mask (0);
	file_test_silence_stderr ();
	ck_assert (log_set_file (path));
	log_write (LOG_ERROR, "%s END\n", payload);
	log_close_file ();
	file_test_restore_stderr ();

	char *text = file_test_read (path);
	ck_assert_ptr_nonnull (strstr (text, payload));
	ck_assert_ptr_nonnull (strstr (text, " END\n"));
	free (text);
	file_test_cleanup (dir, path);
}
END_TEST

START_TEST (test_log_file_unwritable_path_is_ignored)
{
	char dir[256], path[300];
	int pipefd[2];
	char buf[512];
	ssize_t n;

	ck_assert_ptr_nonnull (TestTmpMkdtemp (dir, sizeof (dir), "log_bad"));
	snprintf (path, sizeof (path), "%s/missing/x.log", dir);

	log_set_debug_mask (0);
	fflush (stderr);
	ck_assert_int_eq (pipe (pipefd), 0);
	int saved = dup (STDERR_FILENO);
	ck_assert (saved >= 0);
	ck_assert (dup2 (pipefd[1], STDERR_FILENO) >= 0);
	close (pipefd[1]);

	ck_assert (!log_set_file (path));
	log_write (LOG_ERROR, "still works\n");

	fflush (stderr);
	ck_assert (dup2 (saved, STDERR_FILENO) >= 0);
	close (saved);
	n = read (pipefd[0], buf, sizeof (buf) - 1);
	close (pipefd[0]);
	ck_assert (n > 0);
	buf[n] = '\0';
	ck_assert_ptr_nonnull (strstr (buf, path));
	ck_assert_ptr_nonnull (strstr (buf, "still works"));
	rmdir (dir);
}
END_TEST

#define LOG_FILE_THREADS 4
#define LOG_FILE_LINES 200

static void *file_test_writer (void *arg) {
	const int t = (int) (intptr_t) arg;

	for (int i = 0; i < LOG_FILE_LINES; i++) {
		log_write (LOG_ERROR, "T%d L%d payload-0123456789-0123456789-0123456789 END\n", t, i);
	}
	return NULL;
}

/* One write per line: concurrent writers never interleave within a line. */
START_TEST (test_log_file_concurrent_writers_do_not_interleave)
{
	char dir[256], path[300];
	pthread_t threads[LOG_FILE_THREADS];
	file_test_paths (dir, sizeof (dir), path, sizeof (path), "log_conc");

	log_set_debug_mask (0);
	file_test_silence_stderr ();
	ck_assert (log_set_file (path));
	for (intptr_t t = 0; t < LOG_FILE_THREADS; t++) {
		ck_assert_int_eq (pthread_create (&threads[t], NULL, file_test_writer, (void *) t), 0);
	}
	for (int t = 0; t < LOG_FILE_THREADS; t++) {
		ck_assert_int_eq (pthread_join (threads[t], NULL), 0);
	}
	log_close_file ();
	file_test_restore_stderr ();

	static bool seen[LOG_FILE_THREADS][LOG_FILE_LINES];
	memset (seen, 0, sizeof (seen));
	char *text = file_test_read (path);
	int lines = 0;
	for (char *line = strtok (text, "\n"); line != NULL; line = strtok (NULL, "\n")) {
		int t = -1, i = -1;
		const char *marker = strstr (line, "T");
		/* prefix (debug builds) may contain letters: find the "T<n> L<n>" part */
		while (marker != NULL && sscanf (marker, "T%d L%d payload", &t, &i) != 2) {
			marker = strstr (marker + 1, "T");
		}
		ck_assert_ptr_nonnull (marker);
		ck_assert (t >= 0 && t < LOG_FILE_THREADS && i >= 0 && i < LOG_FILE_LINES);
		ck_assert_str_eq (marker + strlen (marker) - strlen (" payload-0123456789-0123456789-0123456789 END"),
				" payload-0123456789-0123456789-0123456789 END");
		ck_assert (!seen[t][i]);
		seen[t][i] = true;
		lines++;
	}
	ck_assert_int_eq (lines, LOG_FILE_THREADS * LOG_FILE_LINES);
	free (text);
	file_test_cleanup (dir, path);
}
END_TEST

#ifdef HAVE_DEBUGLOG
/* Only the enabled debug kinds reach the file. */
START_TEST (test_log_file_receives_enabled_debug_lines_only)
{
	char dir[256], path[300];
	file_test_paths (dir, sizeof (dir), path, sizeof (path), "log_dbg");

	file_test_silence_stderr ();
	log_set_debug_mask (DEBUG_UI);
	ck_assert (log_set_file (path));
	log_write (DEBUG_UI, "ui line\n");
	log_write (DEBUG_NETWORK, "network line\n");
	log_close_file ();
	log_set_debug_mask (0);
	file_test_restore_stderr ();

	char *text = file_test_read (path);
	ck_assert_ptr_nonnull (strstr (text, "ui line\n"));
	ck_assert_ptr_null (strstr (text, "network line"));
	ck_assert_ptr_null (strstr (text, "\033["));
	free (text);
	file_test_cleanup (dir, path);
}
END_TEST

/* The terminal still gets the coloured line when a log file is set. */
START_TEST (test_log_file_terminal_output_unchanged)
{
	char dir[256], path[300];
	int pipefd[2];
	char buf[512];
	ssize_t n;
	file_test_paths (dir, sizeof (dir), path, sizeof (path), "log_tty");

	log_set_debug_mask (0);
	fflush (stderr);
	ck_assert_int_eq (pipe (pipefd), 0);
	int saved = dup (STDERR_FILENO);
	ck_assert (saved >= 0);
	ck_assert (dup2 (pipefd[1], STDERR_FILENO) >= 0);
	close (pipefd[1]);

	ck_assert (log_set_file (path));
	log_write (LOG_ERROR, "both places\n");
	log_close_file ();

	fflush (stderr);
	ck_assert (dup2 (saved, STDERR_FILENO) >= 0);
	close (saved);
	n = read (pipefd[0], buf, sizeof (buf) - 1);
	close (pipefd[0]);
	ck_assert (n > 0);
	buf[n] = '\0';
	ck_assert_ptr_nonnull (strstr (buf, "\033[0;31m"));
	ck_assert_ptr_nonnull (strstr (buf, "both places\n"));

	char *text = file_test_read (path);
	ck_assert_ptr_nonnull (strstr (text, "both places\n"));
	free (text);
	file_test_cleanup (dir, path);
}
END_TEST
#endif /* HAVE_DEBUGLOG */

Suite *log_suite (void) {
	Suite *s = suite_create ("Log");

	TCase *tc_mask = tcase_create ("debug_mask");
	tcase_add_test (tc_mask, test_log_debug_mask_set_get_roundtrip);
	tcase_add_test (tc_mask, test_log_debug_mask_zero_default);
	tcase_add_test (tc_mask, test_log_debug_mask_partial_bits);
	tcase_add_test (tc_mask, test_log_is_enabled);
	suite_add_tcase (s, tc_mask);

	TCase *tc_debug = tcase_create ("DEBUG_STATE");
#ifdef HAVE_DEBUGLOG
	tcase_add_test (tc_debug, test_log_init_debug_state_banner);
	tcase_add_test (tc_debug, test_log_init_config_debug_mask);
	tcase_add_test (tc_debug, test_log_init_env_overrides_config);
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

	TCase *tc_file = tcase_create ("log_file");
	tcase_add_test (tc_file, test_log_file_receives_error_lines);
	tcase_add_test (tc_file, test_log_file_appends_and_reopens);
	tcase_add_test (tc_file, test_log_file_rejects_null_and_empty_path);
	tcase_add_test (tc_file, test_log_file_long_line_not_truncated);
	tcase_add_test (tc_file, test_log_file_skips_unformattable_line);
	tcase_add_test (tc_file, test_log_file_unwritable_path_is_ignored);
	tcase_add_test (tc_file, test_log_file_concurrent_writers_do_not_interleave);
#ifdef HAVE_DEBUGLOG
	tcase_add_test (tc_file, test_log_file_receives_enabled_debug_lines_only);
	tcase_add_test (tc_file, test_log_file_terminal_output_unchanged);
#endif
	suite_add_tcase (s, tc_file);

	return s;
}
