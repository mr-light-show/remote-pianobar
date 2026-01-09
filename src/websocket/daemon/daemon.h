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

#ifndef _DAEMON_H
#define _DAEMON_H

#include <stdbool.h>

/* Note: main.h must be included before this header to get BarApp_t definition */

/* Daemonize the process (includes fork) */
bool BarDaemonize(BarApp_t *app);

/* Perform daemonization steps without forking (for relaunched processes) */
bool BarDaemonizeSteps(BarApp_t *app);

/* Write PID file */
bool BarDaemonWritePidFile(BarApp_t *app);

/* Remove PID file */
void BarDaemonRemovePidFile(BarApp_t *app);

/* Check if already running */
bool BarDaemonIsRunning(const char *pidFile);

/* Kill a running pianobar instance by reading PID from file */
bool BarDaemonKillRunning(const char *pidFile);

#endif /* _DAEMON_H */

