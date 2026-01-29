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

#include <unistd.h>

#include "../../main.h"
#include "daemon.h"
#include "../../ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/wait.h>
#include <pwd.h>

/* Perform daemonization steps without forking (for relaunched processes) */
bool BarDaemonizeSteps(BarApp_t *app) {
	pid_t sid;
	
	if (!app) {
		return false;
	}
	
	
	/* Change the file mode mask */
	umask(0);
	

	/* Create a new SID for the child process */
	sid = setsid();
	
	if (sid < 0) {
		fprintf(stderr, "Daemon: Failed to create new session\n");
		return false;
	}
	

	/* Change the current working directory */
	if (chdir("/") < 0) {
		fprintf(stderr, "Daemon: Failed to change directory\n");
		return false;
	}
	

	/* Close standard file descriptors */
	close(STDIN_FILENO);
	

	/* Redirect stdout and stderr to log file if configured */
	if (app->settings.logFile) {
		int fd = open(app->settings.logFile,
		              O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (fd >= 0) {
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
			close(fd);
		}
	} else {
		/* Redirect to /dev/null */
		int fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
			close(fd);
		}
	}
	

	/* Write PID file */
	BarDaemonWritePidFile(app);
	

	return true;
}

/* Daemonize the process (includes fork) */
bool BarDaemonize(BarApp_t *app) {
	pid_t pid;
	
	if (!app) {
		return false;
	}
	

	/* Fork off the parent process */
	pid = fork();

	if (pid < 0) {
		fprintf(stderr, "Daemon: Failed to fork\n");
		return false;
	}
	
	/* Exit parent process */
	if (pid > 0) {
		BarPrintStartupInfo(app, pid, true, stdout);
		
		exit(EXIT_SUCCESS);
	}
	
	/* Child process continues here */

	/* Perform daemonization steps */
	return BarDaemonizeSteps(app);
}

/* Write PID file */
bool BarDaemonWritePidFile(BarApp_t *app) {
	FILE *fp;
	
	if (!app || !app->settings.pidFile) {
		return false;
	}
	
	fp = fopen(app->settings.pidFile, "w");
	if (!fp) {
		fprintf(stderr, "Daemon: Failed to create PID file: %s\n",
		        app->settings.pidFile);
		return false;
	}
	
	fprintf(fp, "%d\n", getpid());
	fclose(fp);
	
	return true;
}

/* Remove PID file */
void BarDaemonRemovePidFile(BarApp_t *app) {
	if (app && app->settings.pidFile) {
		unlink(app->settings.pidFile);
	}
}

/* Check if daemon is already running */
bool BarDaemonIsRunning(const char *pidFile) {
	FILE *fp;
	pid_t pid;
	
	if (!pidFile) {
		return false;
	}
	
	fp = fopen(pidFile, "r");
	if (!fp) {
		return false;
	}
	
	if (fscanf(fp, "%d", &pid) != 1) {
		fclose(fp);
		return false;
	}
	fclose(fp);
	
	/* Check if process is still running using kill(pid, 0)
	 * This works cross-platform (Linux, macOS, etc.)
	 * Returns 0 if process exists, -1 if it doesn't (or on error)
	 * Check errno == ESRCH to distinguish "process doesn't exist" from other errors */
	errno = 0;
	if (kill(pid, 0) == 0) {
		return true;
	}
	
	/* Process doesn't exist (errno == ESRCH) or other error */
	/* PID file exists but process doesn't - stale PID file */
	return false;
}

/* Kill a running pianobar instance by reading PID from file */
bool BarDaemonKillRunning(const char *pidFile) {
	FILE *fp;
	pid_t pid;
	int wait_count = 0;
	const int max_wait = 10; /* Wait up to 5 seconds (10 * 0.5s) */
	
	if (!pidFile) {
		return false;
	}
	
	fp = fopen(pidFile, "r");
	if (!fp) {
		return false;
	}
	
	if (fscanf(fp, "%d", &pid) != 1) {
		fclose(fp);
		return false;
	}
	fclose(fp);
	
	/* Check if process is still running */
	errno = 0;
	if (kill(pid, 0) != 0) {
		/* Process doesn't exist - stale PID file */
		unlink(pidFile);
		return true; /* Consider this success - no process to kill */
	}
	
	/* Send SIGTERM for graceful shutdown */
	if (kill(pid, SIGTERM) != 0) {
		return false;
	}
	
	/* Wait for process to terminate gracefully */
	while (wait_count < max_wait) {
		usleep(500000); /* Wait 0.5 seconds */
		errno = 0;
		if (kill(pid, 0) != 0) {
			/* Process terminated */
			unlink(pidFile);
			return true;
		}
		wait_count++;
	}
	
	/* Process didn't terminate gracefully, force kill */
	if (kill(pid, SIGKILL) != 0) {
		return false;
	}
	
	/* Wait a bit more for SIGKILL to take effect */
	usleep(500000);
	errno = 0;
	if (kill(pid, 0) != 0) {
		/* Process terminated */
		unlink(pidFile);
		return true;
	}
	
	/* Still running after SIGKILL - something is wrong */
	return false;
}

/* === New flock-based instance detection === */

/* Get the default lock file path (~/.config/pianobar/pianobar.lock) */
char *BarDaemonGetLockFilePath(void) {
	const char *home = getenv("HOME");
	if (!home) {
		struct passwd *pw = getpwuid(getuid());
		if (pw) {
			home = pw->pw_dir;
		}
	}
	if (!home) {
		return NULL;
	}
	
	/* Allocate space for path: home + /.config/pianobar/pianobar.lock + null */
	size_t len = strlen(home) + strlen("/.config/pianobar/pianobar.lock") + 1;
	char *path = malloc(len);
	if (!path) {
		return NULL;
	}
	
	snprintf(path, len, "%s/.config/pianobar/pianobar.lock", home);
	return path;
}

/* Try to acquire exclusive lock on the lock file */
int BarDaemonAcquireLock(void) {
	char *lockPath = BarDaemonGetLockFilePath();
	if (!lockPath) {
		return -1;
	}
	
	/* Open or create the lock file */
	int fd = open(lockPath, O_RDWR | O_CREAT, 0644);
	free(lockPath);
	
	if (fd < 0) {
		return -1;
	}
	
	/* Try to acquire exclusive non-blocking lock */
	if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
		/* Lock failed - another instance holds the lock */
		close(fd);
		return -1;
	}
	
	return fd;
}

/* Read PID from lock file */
pid_t BarDaemonReadLockPid(void) {
	char *lockPath = BarDaemonGetLockFilePath();
	if (!lockPath) {
		return -1;
	}
	
	FILE *fp = fopen(lockPath, "r");
	free(lockPath);
	
	if (!fp) {
		return -1;
	}
	
	pid_t pid = -1;
	if (fscanf(fp, "%d", &pid) != 1) {
		pid = -1;
	}
	fclose(fp);
	
	return pid;
}

/* Write current PID to lock file */
bool BarDaemonWriteLockPid(int lockFd) {
	if (lockFd < 0) {
		return false;
	}
	
	/* Truncate file and write new PID */
	if (ftruncate(lockFd, 0) < 0) {
		return false;
	}
	if (lseek(lockFd, 0, SEEK_SET) < 0) {
		return false;
	}
	
	char buf[32];
	int len = snprintf(buf, sizeof(buf), "%d\n", getpid());
	if (write(lockFd, buf, len) != len) {
		return false;
	}
	
	return true;
}

/* Kill existing instance using lock file PID */
bool BarDaemonKillExistingInstance(void) {
	pid_t oldPid = BarDaemonReadLockPid();
	if (oldPid <= 0) {
		/* No PID in file or can't read - try to acquire lock anyway */
		return true;
	}
	
	/* Check if process is still running */
	errno = 0;
	if (kill(oldPid, 0) != 0) {
		/* Process doesn't exist - lock will be released */
		return true;
	}
	
	fprintf(stderr, "Pianobar already running (PID: %d). Stopping it...\n", (int)oldPid);
	
	/* Send SIGTERM for graceful shutdown */
	if (kill(oldPid, SIGTERM) != 0) {
		fprintf(stderr, "Failed to send SIGTERM to PID %d\n", (int)oldPid);
		return false;
	}
	
	/* Wait for process to terminate gracefully (up to 5 seconds) */
	int wait_count = 0;
	const int max_wait = 10; /* 10 * 0.5s = 5 seconds */
	
	while (wait_count < max_wait) {
		usleep(500000); /* Wait 0.5 seconds */
		errno = 0;
		if (kill(oldPid, 0) != 0) {
			/* Process terminated */
			return true;
		}
		wait_count++;
	}
	
	/* Process didn't terminate gracefully, force kill */
	fprintf(stderr, "Process didn't respond to SIGTERM, sending SIGKILL...\n");
	if (kill(oldPid, SIGKILL) != 0) {
		return false;
	}
	
	/* Wait a bit more for SIGKILL to take effect */
	usleep(500000);
	errno = 0;
	if (kill(oldPid, 0) != 0) {
		/* Process terminated */
		return true;
	}
	
	fprintf(stderr, "Warning: Failed to kill existing instance (PID: %d)\n", (int)oldPid);
	return false;
}
