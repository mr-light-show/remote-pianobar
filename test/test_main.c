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
#include <check.h>
#include <signal.h>
#include <stdatomic.h>
#include <pthread.h>

#include "../src/log.h"
#include "../src/main.h"
#include "test_tmp.h"

/* Global interrupted variable stub for tests */
_Atomic sig_atomic_t *interrupted = NULL;

/* Stub implementations for playback_manager dependencies */
void BarMainGetPlaylist(BarApp_t *app) {
	(void)app;
}

/* Test suite declarations */
Suite *settings_suite(void);
Suite *player_suite(void);
Suite *bar_state_suite(void);
Suite *playback_manager_suite(void);
Suite *log_suite(void);
Suite *l10n_suite(void);
Suite *playback_lifecycle_suite(void);
Suite *system_volume_suite(void);
Suite *ui_readline_suite(void);
Suite *ui_suite(void);
Suite *station_sort_suite(void);
Suite *interrupt_suite(void);
Suite *libpiano_response_suite(void);

Suite *websocket_suite(void);
Suite *ws_outbox_suite(void);
Suite *ws_outbox_faults_suite(void);
Suite *ws_service_edges_suite(void);
Suite *ws_commands_suite(void);
Suite *http_server_suite(void);
Suite *daemon_suite(void);
Suite *socketio_suite(void);
Suite *error_messages_suite(void);
Suite *ui_act_suite(void);
Suite *ui_act_station_suite(void);
Suite *ws_edge_cases_suite(void);
Suite *playback_integration_suite(void);
Suite *state_edge_paths_suite(void);
Suite *ws_failure_paths_suite(void);
Suite *cli_input_paths_suite(void);
Suite *socketio_station_failures_suite(void);

/* Under AddressSanitizer an oversized malloc/realloc aborts instead of
 * returning NULL. Out-of-memory tests (e.g. httpFetchCb) rely on the NULL
 * return, so ask the sanitizer to behave like a plain allocator. */
#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
const char *__asan_default_options (void);
const char *__asan_default_options (void) {
	return "allocator_may_return_null=1";
}
#endif

int main(void) {
	int number_failed;
	SRunner *sr;

	TestTmpRootInit ();

	sr = srunner_create(websocket_suite());
	srunner_add_suite(sr, ws_outbox_suite());
	srunner_add_suite(sr, ws_outbox_faults_suite());
	srunner_add_suite(sr, ws_service_edges_suite());
	srunner_add_suite(sr, ws_commands_suite());
	srunner_add_suite(sr, ws_failure_paths_suite());
	srunner_add_suite(sr, http_server_suite());
	srunner_add_suite(sr, daemon_suite());
	srunner_add_suite(sr, socketio_suite());
	srunner_add_suite(sr, socketio_station_failures_suite());
	srunner_add_suite(sr, error_messages_suite());
	srunner_add_suite(sr, ui_act_suite());
	srunner_add_suite(sr, ui_act_station_suite());
	srunner_add_suite(sr, ws_edge_cases_suite());
	srunner_add_suite(sr, cli_input_paths_suite());
	srunner_add_suite(sr, playback_integration_suite());

	srunner_add_suite(sr, settings_suite());
	srunner_add_suite(sr, player_suite());
	srunner_add_suite(sr, bar_state_suite());
	srunner_add_suite(sr, playback_manager_suite());
	srunner_add_suite(sr, l10n_suite());
	srunner_add_suite(sr, log_suite());
	srunner_add_suite(sr, playback_lifecycle_suite());
	srunner_add_suite(sr, system_volume_suite());
	srunner_add_suite(sr, ui_readline_suite());
	srunner_add_suite(sr, ui_suite());
	srunner_add_suite(sr, station_sort_suite());
	srunner_add_suite(sr, interrupt_suite());
	srunner_add_suite(sr, libpiano_response_suite());
	srunner_add_suite(sr, state_edge_paths_suite());

	/* PIANOBAR_TEST_DEBUG_ALL=1 (CI c-asan): every DEBUG_* kind on, so debug-only
	 * log statements run under the sanitizers. Tests that set their own debug
	 * mask override this. The output is large; scripts/test-parallel.sh with
	 * TEST_PAR_LOGS=failed prints only failing shards. */
	const char *debugAll = getenv ("PIANOBAR_TEST_DEBUG_ALL");
	if (debugAll != NULL && strcmp (debugAll, "1") == 0) {
		log_set_debug_mask ((unsigned int) DEBUG_STATE * 2u - 1u);
	}

	/* Run tests */
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	/* Check silently runs nothing for an unknown CK_RUN_SUITE or CK_RUN_CASE
	 * name; fail instead, so a typo or a renamed suite cannot drop tests
	 * (scripts/test-parallel.sh runs one suite per process). */
	if ((getenv ("CK_RUN_SUITE") != NULL || getenv ("CK_RUN_CASE") != NULL) &&
			srunner_ntests_run (sr) == 0) {
		fprintf (stderr, "pianobar_test: CK_RUN_SUITE=\"%s\" CK_RUN_CASE=\"%s\" "
				"matched no tests\n",
				getenv ("CK_RUN_SUITE") != NULL ? getenv ("CK_RUN_SUITE") : "",
				getenv ("CK_RUN_CASE") != NULL ? getenv ("CK_RUN_CASE") : "");
		number_failed = 1;
	}
	srunner_free(sr);
	TestTmpRootCleanup ();

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
