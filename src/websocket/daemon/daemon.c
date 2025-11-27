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

#include "../../main.h"
#include "daemon.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>

/* Daemonize the process */
bool BarDaemonize(BarApp_t *app) {
	pid_t pid, sid;
	
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
		/* Print info before exiting parent */
		printf("Pianobar daemon started (PID: %d)\n", pid);
		printf("Web interface: http://%s:%d/\n",
		       app->settings.websocketHost ? app->settings.websocketHost : "127.0.0.1",
		       app->settings.websocketPort);
		
		if (app->settings.pidFile) {
			printf("PID file: %s\n", app->settings.pidFile);
		}
		
		exit(EXIT_SUCCESS);
	}
	
	/* Child process continues here */
	
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
	char procPath[256];
	
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
	snprintf(procPath, sizeof(procPath), "/proc/%d", pid);
	if (access(procPath, F_OK) == 0) {
		return true;
	}
	
	/* PID file exists but process doesn't - stale PID file */
	return false;
}

