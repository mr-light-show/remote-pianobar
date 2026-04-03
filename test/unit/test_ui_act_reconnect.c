/*
 * Covers BarUiActPandoraReconnect early exit (no credentials) and WebSocket error emit.
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/l10n.h"
#include "../../src/main.h"
#include "../../src/settings.h"
#include "../../src/websocket/protocol/socketio.h"

/* Declaration only — avoid ui_dispatch.h (pulls full shortcut table + ui_act.h). */
void BarUiActPandoraReconnect (BarApp_t *app, PianoStation_t *selStation,
		PianoSong_t *selSong, int context);

static char *last_broadcast_msg = NULL;

static void mock_broadcast (const char *message, size_t len) {
	free (last_broadcast_msg);
	last_broadcast_msg = malloc (len + 1);
	memcpy (last_broadcast_msg, message, len);
	last_broadcast_msg[len] = '\0';
}

static void clear_mock (void) {
	free (last_broadcast_msg);
	last_broadcast_msg = NULL;
}

START_TEST (test_ui_act_pandora_reconnect_no_credentials) {
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);

	ck_assert (BarL10nInit (&app.l10n, &app.settings));

	BarSocketIoSetBroadcastCallback (mock_broadcast);
	clear_mock ();

	/* BAR_DC_GLOBAL == 1 */
	BarUiActPandoraReconnect (&app, NULL, NULL, 1);

	ck_assert_ptr_nonnull (last_broadcast_msg);
	ck_assert (strstr (last_broadcast_msg, "error") != NULL);

	BarL10nDestroy (&app.l10n);
	BarSettingsDestroy (&app.settings);
	clear_mock ();
}
END_TEST

Suite *ui_act_suite (void) {
	Suite *s = suite_create ("ui_act");
	TCase *tc = tcase_create ("pandora_reconnect");
	tcase_add_test (tc, test_ui_act_pandora_reconnect_no_credentials);
	suite_add_tcase (s, tc);
	return s;
}
