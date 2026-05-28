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
}

void BarMainStartPlayback(BarApp_t *app, pthread_t *playerThread) {
	(void)app;
	(void)playerThread;
}

/* Test suite declarations — base (run in all configurations) */
Suite *settings_suite(void);
Suite *player_suite(void);
Suite *bar_state_suite(void);
Suite *playback_manager_suite(void);
Suite *log_suite(void);
Suite *l10n_suite(void);
Suite *playback_lifecycle_suite(void);
Suite *libpiano_response_suite(void);

/* Test suite declarations — WebSocket-only */
#ifdef WEBSOCKET_ENABLED
Suite *websocket_suite(void);
Suite *http_server_suite(void);
Suite *daemon_suite(void);
Suite *socketio_suite(void);
Suite *error_messages_suite(void);
Suite *ui_act_suite(void);
Suite *ui_act_station_suite(void);
#endif

int main(void) {
	int number_failed;
	SRunner *sr;

#ifdef WEBSOCKET_ENABLED
	/* Start with WebSocket suite; add remaining WebSocket suites */
	sr = srunner_create(websocket_suite());
	srunner_add_suite(sr, http_server_suite());
	srunner_add_suite(sr, daemon_suite());
	srunner_add_suite(sr, socketio_suite());
	srunner_add_suite(sr, error_messages_suite());
	srunner_add_suite(sr, ui_act_suite());
	srunner_add_suite(sr, ui_act_station_suite());
#else
	/* NOWEBSOCKET=1: no WebSocket suites — start with a base suite */
	sr = srunner_create(settings_suite());
#endif

	/* Base suites always run */
#ifdef WEBSOCKET_ENABLED
	srunner_add_suite(sr, settings_suite());
#endif
	srunner_add_suite(sr, player_suite());
	srunner_add_suite(sr, bar_state_suite());
	srunner_add_suite(sr, playback_manager_suite());
	srunner_add_suite(sr, l10n_suite());
	srunner_add_suite(sr, log_suite());
	srunner_add_suite(sr, playback_lifecycle_suite());
	srunner_add_suite(sr, libpiano_response_suite());

	/* Run tests */
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
