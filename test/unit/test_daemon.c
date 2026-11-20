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
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/resource.h>
#include "../../src/main.h"
#include "../test_tmp.h"
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

/* Fork a child that exercises the fd handling. The child leaves through
 * exit() (not _exit()) so coverage counters are flushed, hence stdio buffers
 * are flushed first so nothing inherited is written through the redirected
 * descriptors. */
static pid_t daemon_test_fork (void) {
	fflush (NULL);
	return fork ();
}

static void daemon_test_exit (int code) {
	fflush (NULL);
	exit (code);
}

/* Per-test scratch directory, so concurrent test runs never share pid files. */
static void daemon_test_pid_path (char *dir, size_t dirSize, char *path,
		size_t pathSize, const char *prefix) {
	ck_assert_ptr_nonnull (TestTmpMkdtemp (dir, dirSize, prefix));
	snprintf (path, pathSize, "%s/pianobar.pid", dir);
}

/* Real-world: write PID file with a configured path, check process detection,
 * then remove it.  Mirrors what happens when the daemon launches. */
START_TEST(test_daemon_pid_file_write_detect_remove_roundtrip) {
	char dir[256], pidPath[300];
	daemon_test_pid_path (dir, sizeof (dir), pidPath, sizeof (pidPath),
			"daemon_roundtrip");

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
	rmdir (dir);
}
END_TEST

START_TEST(test_daemon_is_running_with_dead_pid) {
	char dir[256], pidPath[300];
	daemon_test_pid_path (dir, sizeof (dir), pidPath, sizeof (pidPath),
			"daemon_dead");
	FILE *f = fopen(pidPath, "w");
	ck_assert_ptr_nonnull(f);
	/* PID that almost certainly does not exist on test hosts. */
	fprintf(f, "2147483646\n");
	fclose(f);

	ck_assert(!BarDaemonIsRunning(pidPath));
	unlink(pidPath);
	rmdir (dir);
}
END_TEST

START_TEST(test_daemon_get_lock_file_path_is_under_config) {
	char *path = BarDaemonGetLockFilePath();
	ck_assert_ptr_nonnull(path);
	ck_assert(strstr(path, "pianobar.lock") != NULL);
	free(path);
}
END_TEST

START_TEST(test_daemon_lock_acquire_write_read_roundtrip) {
	int lockFd = BarDaemonAcquireLock();
	if (lockFd < 0) {
		/* Another instance may hold the default lock on a dev machine. */
		ck_assert(1);
		return;
	}

	ck_assert(BarDaemonWriteLockPid(lockFd));
	ck_assert_int_eq(BarDaemonReadLockPid(), getpid());
	close(lockFd);
}
END_TEST

START_TEST(test_daemon_write_lock_pid_rejects_invalid_fd) {
	ck_assert(!BarDaemonWriteLockPid(-1));
}
END_TEST

START_TEST(test_daemonize_steps_without_log_file_keeps_stderr) {
	char dir[256], pidPath[300];
	pid_t child;
	int status;

	daemon_test_pid_path (dir, sizeof (dir), pidPath, sizeof (pidPath),
			"daemon_steps");
	child = daemon_test_fork ();
	ck_assert (child >= 0);
	if (child == 0) {
		BarApp_t app;

		memset (&app, 0, sizeof (app));
		BarSettingsInit (&app.settings);
		app.settings.logFile = NULL;
		app.settings.pidFile = strdup (pidPath);
		daemon_test_exit (BarDaemonizeSteps (&app) ? 0 : 1);
	}
	ck_assert_int_eq (waitpid (child, &status, 0), child);
	ck_assert (WIFEXITED (status));
	ck_assert_int_eq (WEXITSTATUS (status), 0);
	unlink (pidPath);
	rmdir (dir);
}
END_TEST

/* True when fd is open and refers to the same character device as /dev/null. */
static bool daemon_test_fd_is_dev_null (int fd) {
	struct stat fdSt, nullSt;

	if (fcntl (fd, F_GETFD) == -1 || fstat (fd, &fdSt) != 0 ||
			stat ("/dev/null", &nullSt) != 0) {
		return false;
	}
	return S_ISCHR (fdSt.st_mode) && fdSt.st_rdev == nullSt.st_rdev;
}

/* True when fd is open and refers to the file at path. */
static bool daemon_test_fd_is_file (int fd, const char *path) {
	struct stat fdSt, fileSt;

	if (fcntl (fd, F_GETFD) == -1 || fstat (fd, &fdSt) != 0 ||
			stat (path, &fileSt) != 0) {
		return false;
	}
	return fdSt.st_ino == fileSt.st_ino && fdSt.st_dev == fileSt.st_dev;
}

static void daemon_test_wait_ok (pid_t child) {
	int status;

	ck_assert_int_eq (waitpid (child, &status, 0), child);
	ck_assert (WIFEXITED (status));
	ck_assert_int_eq (WEXITSTATUS (status), 0);
}

/* The daemon must keep fd 0 on /dev/null: a closed fd 0 is silently reused by
 * the next open() (lws's /dev/urandom, an audio device) and then handed to
 * every child as "stdin". */
START_TEST(test_daemonize_steps_reopens_stdin_on_dev_null) {
	char dir[256], pidPath[300];
	pid_t child;

	daemon_test_pid_path (dir, sizeof (dir), pidPath, sizeof (pidPath),
			"daemon_stdin");
	child = daemon_test_fork ();
	ck_assert (child >= 0);
	if (child == 0) {
		BarApp_t app;

		memset (&app, 0, sizeof (app));
		BarSettingsInit (&app.settings);
		app.settings.logFile = NULL;
		app.settings.pidFile = strdup (pidPath);
		if (!BarDaemonizeSteps (&app)) {
			daemon_test_exit (1);
		}
		daemon_test_exit (daemon_test_fd_is_dev_null (STDIN_FILENO) &&
				daemon_test_fd_is_dev_null (STDOUT_FILENO) ? 0 : 2);
	}
	daemon_test_wait_ok (child);
	unlink (pidPath);
	rmdir (dir);
}
END_TEST

START_TEST(test_daemonize_steps_with_log_file_keeps_stdin_open) {
	char dir[256], pidPath[300], logPath[300], buf[64];
	pid_t child;
	FILE *f;

	daemon_test_pid_path (dir, sizeof (dir), pidPath, sizeof (pidPath),
			"daemon_logfile");
	snprintf (logPath, sizeof (logPath), "%s/daemon.log", dir);
	child = daemon_test_fork ();
	ck_assert (child >= 0);
	if (child == 0) {
		BarApp_t app;

		memset (&app, 0, sizeof (app));
		BarSettingsInit (&app.settings);
		app.settings.logFile = strdup (logPath);
		app.settings.pidFile = strdup (pidPath);
		if (!BarDaemonizeSteps (&app)) {
			daemon_test_exit (1);
		}
		if (!daemon_test_fd_is_dev_null (STDIN_FILENO)) {
			daemon_test_exit (2);
		}
		if (!daemon_test_fd_is_file (STDOUT_FILENO, logPath) ||
				!daemon_test_fd_is_file (STDERR_FILENO, logPath)) {
			daemon_test_exit (3);
		}
		if (write (STDOUT_FILENO, "from-stdout\n", 12) != 12) {
			daemon_test_exit (4);
		}
		daemon_test_exit (0);
	}
	daemon_test_wait_ok (child);
	f = fopen (logPath, "r");
	ck_assert_ptr_nonnull (f);
	ck_assert_ptr_nonnull (fgets (buf, sizeof (buf), f));
	ck_assert_str_eq (buf, "from-stdout\n");
	fclose (f);
	unlink (logPath);
	unlink (pidPath);
	rmdir (dir);
}
END_TEST

/* Started from launchd/nohup with the standard fds already closed: open()
 * returns 0, 1 or 2, and the helper must not close what it just placed there. */
START_TEST(test_daemon_redirect_stdio_with_std_fds_closed) {
	pid_t child;

	child = daemon_test_fork ();
	ck_assert (child >= 0);
	if (child == 0) {
		struct stat before, after;

		if (fstat (STDERR_FILENO, &before) != 0) {
			daemon_test_exit (1);
		}
		close (STDIN_FILENO);
		close (STDOUT_FILENO);
		if (!BarDaemonRedirectStdio (NULL)) {
			daemon_test_exit (2);
		}
		if (!daemon_test_fd_is_dev_null (STDIN_FILENO) ||
				!daemon_test_fd_is_dev_null (STDOUT_FILENO)) {
			daemon_test_exit (3);
		}
		if (fstat (STDERR_FILENO, &after) != 0 ||
				before.st_ino != after.st_ino ||
				before.st_dev != after.st_dev) {
			daemon_test_exit (4);
		}
		daemon_test_exit (0);
	}
	daemon_test_wait_ok (child);
}
END_TEST

START_TEST(test_daemon_redirect_stdio_all_std_fds_closed) {
	pid_t child;

	child = daemon_test_fork ();
	ck_assert (child >= 0);
	if (child == 0) {
		close (STDIN_FILENO);
		close (STDOUT_FILENO);
		close (STDERR_FILENO);
		if (!BarDaemonRedirectStdio (NULL)) {
			daemon_test_exit (1);
		}
		daemon_test_exit (daemon_test_fd_is_dev_null (STDIN_FILENO) &&
				daemon_test_fd_is_dev_null (STDOUT_FILENO) &&
				daemon_test_fd_is_dev_null (STDERR_FILENO) ? 0 : 2);
	}
	daemon_test_wait_ok (child);
}
END_TEST

START_TEST(test_daemon_redirect_stdio_log_file_with_std_fds_closed) {
	char dir[256], logPath[300];
	pid_t child;

	ck_assert_ptr_nonnull (TestTmpMkdtemp (dir, sizeof (dir), "daemon_closed_log"));
	snprintf (logPath, sizeof (logPath), "%s/daemon.log", dir);
	child = daemon_test_fork ();
	ck_assert (child >= 0);
	if (child == 0) {
		close (STDIN_FILENO);
		close (STDOUT_FILENO);
		close (STDERR_FILENO);
		if (!BarDaemonRedirectStdio (logPath)) {
			daemon_test_exit (1);
		}
		daemon_test_exit (daemon_test_fd_is_dev_null (STDIN_FILENO) &&
				daemon_test_fd_is_file (STDOUT_FILENO, logPath) &&
				daemon_test_fd_is_file (STDERR_FILENO, logPath) ? 0 : 2);
	}
	daemon_test_wait_ok (child);
	unlink (logPath);
	rmdir (dir);
}
END_TEST

/* Every combination of closed standard fds, with and without a log file:
 * whichever descriptor open() hands back (0, 1, 2 or above), the result must
 * be stdin on /dev/null and stdout/stderr on the log file or /dev/null (an
 * open stderr is kept when there is no log file). */
START_TEST(test_daemon_redirect_stdio_every_closed_fd_combination) {
	char dir[256], logPath[300];
	int mask, useLog;

	ck_assert_ptr_nonnull (TestTmpMkdtemp (dir, sizeof (dir), "daemon_combo"));
	snprintf (logPath, sizeof (logPath), "%s/daemon.log", dir);
	for (useLog = 0; useLog < 2; useLog++) {
		for (mask = 0; mask < 8; mask++) {
			pid_t child = daemon_test_fork ();
			ck_assert (child >= 0);
			if (child == 0) {
				struct stat before, after;
				bool errOpen = fcntl (STDERR_FILENO, F_GETFD) != -1;

				if (!errOpen || fstat (STDERR_FILENO, &before) != 0) {
					daemon_test_exit (1);
				}
				for (int fd = 0; fd < 3; fd++) {
					if (mask & (1 << fd)) {
						close (fd);
					}
				}
				errOpen = !(mask & (1 << STDERR_FILENO));
				if (!BarDaemonRedirectStdio (useLog ? logPath : NULL)) {
					daemon_test_exit (2);
				}
				if (!daemon_test_fd_is_dev_null (STDIN_FILENO)) {
					daemon_test_exit (3);
				}
				if (useLog) {
					if (!daemon_test_fd_is_file (STDOUT_FILENO, logPath) ||
							!daemon_test_fd_is_file (STDERR_FILENO, logPath)) {
						daemon_test_exit (4);
					}
				} else {
					if (!daemon_test_fd_is_dev_null (STDOUT_FILENO)) {
						daemon_test_exit (5);
					}
					if (errOpen) {
						if (fstat (STDERR_FILENO, &after) != 0 ||
								before.st_ino != after.st_ino ||
								before.st_dev != after.st_dev) {
							daemon_test_exit (6);
						}
					} else if (!daemon_test_fd_is_dev_null (STDERR_FILENO)) {
						daemon_test_exit (7);
					}
				}
				daemon_test_exit (0);
			}
			daemon_test_wait_ok (child);
		}
	}
	unlink (logPath);
	rmdir (dir);
}
END_TEST

START_TEST(test_daemon_redirect_stdio_bad_log_file_keeps_stderr) {
	char dir[256], badPath[300];
	pid_t child;

	ck_assert_ptr_nonnull (TestTmpMkdtemp (dir, sizeof (dir), "daemon_badlog"));
	/* The parent directory does not exist, so open(O_CREAT) fails. */
	snprintf (badPath, sizeof (badPath), "%s/missing/x.log", dir);
	child = daemon_test_fork ();
	ck_assert (child >= 0);
	if (child == 0) {
		struct stat before, after;

		if (fstat (STDERR_FILENO, &before) != 0) {
			daemon_test_exit (1);
		}
		if (!BarDaemonRedirectStdio (badPath)) {
			daemon_test_exit (2);
		}
		if (!daemon_test_fd_is_dev_null (STDIN_FILENO) ||
				!daemon_test_fd_is_dev_null (STDOUT_FILENO)) {
			daemon_test_exit (3);
		}
		if (fstat (STDERR_FILENO, &after) != 0 ||
				before.st_ino != after.st_ino ||
				before.st_dev != after.st_dev) {
			daemon_test_exit (4);
		}
		daemon_test_exit (0);
	}
	daemon_test_wait_ok (child);
	rmdir (dir);
}
END_TEST

/* /dev/null cannot be opened (here: fd table full, as in a locked-down
 * sandbox): report failure and leave the standard fds alone. */
START_TEST(test_daemon_redirect_stdio_dev_null_unavailable_returns_false) {
	pid_t child;

	child = daemon_test_fork ();
	ck_assert (child >= 0);
	if (child == 0) {
		struct rlimit lim, saved;
		struct stat before, after;
		int i;

		/* Make sure 0, 1 and 2 are all open, then forbid fd 3. */
		for (i = 0; i < 3; i++) {
			if (fcntl (i, F_GETFD) == -1 && open ("/dev/null", O_RDWR) != i) {
				daemon_test_exit (1);
			}
		}
		if (fstat (STDIN_FILENO, &before) != 0) {
			daemon_test_exit (2);
		}
		if (getrlimit (RLIMIT_NOFILE, &saved) != 0) {
			daemon_test_exit (3);
		}
		/* Lower only the soft limit, so it can be restored for the coverage
		 * flush at exit. */
		lim = saved;
		lim.rlim_cur = 3;
		if (setrlimit (RLIMIT_NOFILE, &lim) != 0) {
			daemon_test_exit (3);
		}
		const bool redirected = BarDaemonRedirectStdio (NULL);
		if (setrlimit (RLIMIT_NOFILE, &saved) != 0) {
			daemon_test_exit (3);
		}
		if (redirected) {
			daemon_test_exit (4);
		}
		if (fstat (STDIN_FILENO, &after) != 0 ||
				before.st_ino != after.st_ino ||
				before.st_dev != after.st_dev) {
			daemon_test_exit (5);
		}
		daemon_test_exit (0);
	}
	daemon_test_wait_ok (child);
}
END_TEST

START_TEST(test_daemonize_steps_rejects_null_app) {
	ck_assert(!BarDaemonizeSteps(NULL));
}
END_TEST

/* Create test suite */
START_TEST(test_daemon_kill_running_nonexistent_pid_file) {
	ck_assert(!BarDaemonKillRunning("/tmp/nonexistent_pianobar_kill.pid"));
}
END_TEST

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
	tcase_add_test(tc_core, test_daemon_lock_acquire_write_read_roundtrip);
	tcase_add_test(tc_core, test_daemon_write_lock_pid_rejects_invalid_fd);
	tcase_add_test(tc_core, test_daemonize_steps_without_log_file_keeps_stderr);
	tcase_add_test(tc_core, test_daemonize_steps_reopens_stdin_on_dev_null);
	tcase_add_test(tc_core, test_daemonize_steps_with_log_file_keeps_stdin_open);
	tcase_add_test(tc_core, test_daemon_redirect_stdio_with_std_fds_closed);
	tcase_add_test(tc_core, test_daemon_redirect_stdio_all_std_fds_closed);
	tcase_add_test(tc_core, test_daemon_redirect_stdio_log_file_with_std_fds_closed);
	tcase_add_test(tc_core, test_daemon_redirect_stdio_every_closed_fd_combination);
	tcase_add_test(tc_core, test_daemon_redirect_stdio_bad_log_file_keeps_stderr);
	tcase_add_test(tc_core, test_daemon_redirect_stdio_dev_null_unavailable_returns_false);
	tcase_add_test(tc_core, test_daemonize_steps_rejects_null_app);
	tcase_add_test(tc_core, test_daemon_kill_running_nonexistent_pid_file);
	
	suite_add_tcase(s, tc_core);
	
	return s;
}

