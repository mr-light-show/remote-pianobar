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

#include <stdlib.h>
#include <check.h>
#include <signal.h>
#include <pthread.h>

#include "../src/main.h"

/* Global interrupted variable stub for tests */
sig_atomic_t *interrupted = NULL;

/* Stub implementations for playback_manager dependencies */
void BarMainGetPlaylist(BarApp_t *app) {
	(void)app;
	/* Stub for tests - playback manager not actually tested */
}

void BarMainStartPlayback(BarApp_t *app, pthread_t *playerThread) {
	(void)app;
	(void)playerThread;
	/* Stub for tests - playback manager not actually tested */
}

/* Test suite declarations */
Suite *websocket_suite(void);
Suite *http_server_suite(void);
Suite *daemon_suite(void);
Suite *socketio_suite(void);
Suite *player_suite(void);

int main(void) {
	int number_failed;
	SRunner *sr;
	
	/* Create test runner */
	sr = srunner_create(websocket_suite());
	srunner_add_suite(sr, http_server_suite());
	srunner_add_suite(sr, daemon_suite());
	srunner_add_suite(sr, socketio_suite());
	srunner_add_suite(sr, player_suite());
	
	/* Run tests */
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

