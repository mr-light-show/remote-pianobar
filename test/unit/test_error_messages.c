/*
 * Unit tests for BarWsGetFriendlyErrorMessage (WebSocket error mapping).
 */

#include <check.h>
#include <string.h>

#include "../../src/l10n.h"
#include "../../src/websocket/protocol/error_messages.h"

static const char *default_str (const char *key) {
	return BarL10nGet (NULL, key);
}

START_TEST (test_ws_err_null_original) {
	const char *r = BarWsGetFriendlyErrorMessage (NULL, "any.op", NULL);
	ck_assert_str_eq (r, default_str ("error.ws.unknown"));
}
END_TEST

START_TEST (test_ws_err_not_allowed_rename) {
	const char *r = BarWsGetFriendlyErrorMessage (NULL, "station.rename",
			"Call not allowed");
	ck_assert_str_eq (r, default_str ("error.ws.not_allowed.station_rename"));
}
END_TEST

START_TEST (test_ws_err_not_allowed_add_music) {
	const char *r = BarWsGetFriendlyErrorMessage (NULL, "station.addMusic",
			"not allowed here");
	ck_assert_str_eq (r, default_str ("error.ws.not_allowed.station_add_music"));
}
END_TEST

START_TEST (test_ws_err_not_allowed_delete_seed) {
	const char *r = BarWsGetFriendlyErrorMessage (NULL, "station.deleteSeed",
			"not allowed");
	ck_assert_str_eq (r, default_str ("error.ws.not_allowed.station_delete_seed"));
}
END_TEST

START_TEST (test_ws_err_not_allowed_delete_feedback) {
	const char *r = BarWsGetFriendlyErrorMessage (NULL, "station.deleteFeedback",
			"not allowed");
	ck_assert_str_eq (r, default_str ("error.ws.not_allowed.station_delete_feedback"));
}
END_TEST

START_TEST (test_ws_err_not_allowed_set_station_mode) {
	const char *r = BarWsGetFriendlyErrorMessage (NULL, "station.setStationMode",
			"not allowed");
	ck_assert_str_eq (r, default_str ("error.ws.not_allowed.station_set_station_mode"));
}
END_TEST

START_TEST (test_ws_err_not_allowed_generic) {
	const char *r = BarWsGetFriendlyErrorMessage (NULL, "station.delete",
			"not allowed");
	ck_assert_str_eq (r, default_str ("error.ws.not_allowed.generic"));
}
END_TEST

START_TEST (test_ws_err_network) {
	const char *r1 = BarWsGetFriendlyErrorMessage (NULL, "music.search",
			"Network error");
	ck_assert_str_eq (r1, default_str ("error.ws.network"));
	const char *r2 = BarWsGetFriendlyErrorMessage (NULL, "music.search",
			"a network failure");
	ck_assert_str_eq (r2, default_str ("error.ws.network"));
	const char *r3 = BarWsGetFriendlyErrorMessage (NULL, "music.search",
			"Could not resolve host");
	ck_assert_str_eq (r3, default_str ("error.ws.network"));
	const char *r4 = BarWsGetFriendlyErrorMessage (NULL, "music.search",
			"Timeout");
	ck_assert_str_eq (r4, default_str ("error.ws.network"));
}
END_TEST

START_TEST (test_ws_err_auth) {
	const char *r1 = BarWsGetFriendlyErrorMessage (NULL, "x", "auth failed");
	ck_assert_str_eq (r1, default_str ("error.ws.auth"));
	const char *r2 = BarWsGetFriendlyErrorMessage (NULL, "x", "please login");
	ck_assert_str_eq (r2, default_str ("error.ws.auth"));
	const char *r3 = BarWsGetFriendlyErrorMessage (NULL, "x", "Invalid token");
	ck_assert_str_eq (r3, default_str ("error.ws.auth"));
}
END_TEST

START_TEST (test_ws_err_interrupted) {
	const char *r1 = BarWsGetFriendlyErrorMessage (NULL, "x", "interrupted");
	ck_assert_str_eq (r1, default_str ("error.ws.interrupted"));
	const char *r2 = BarWsGetFriendlyErrorMessage (NULL, "x", "Interrupted");
	ck_assert_str_eq (r2, default_str ("error.ws.interrupted"));
}
END_TEST

START_TEST (test_ws_err_failed_to_transform) {
	const char *r1 = BarWsGetFriendlyErrorMessage (NULL, "station.addMusic",
			"Failed to transform station xyz");
	ck_assert_str_eq (r1, default_str ("web.station_add_music_transform"));
	const char *r2 = BarWsGetFriendlyErrorMessage (NULL, "station.rename",
			"Failed to transform");
	ck_assert_str_eq (r2, default_str ("web.station_rename_transform"));
}
END_TEST

START_TEST (test_ws_err_pandora_reconnect) {
	const char *r1 = BarWsGetFriendlyErrorMessage (NULL, "app.pandora-reconnect",
			"Unknown account x");
	ck_assert_str_eq (r1, default_str ("web.pandora_reconnect_unknown_account"));
	const char *r2 = BarWsGetFriendlyErrorMessage (NULL, "app.pandora-reconnect",
			"No credentials");
	ck_assert_str_eq (r2, default_str ("web.pandora_reconnect_no_credentials"));
	const char *r3 = BarWsGetFriendlyErrorMessage (NULL, "app.pandora-reconnect",
			"Last station was deleted");
	ck_assert_str_eq (r3, default_str ("web.pandora_reconnect_station_deleted"));
}
END_TEST

START_TEST (test_ws_err_song_explain) {
	const char *r1 = BarWsGetFriendlyErrorMessage (NULL, "song.explain",
			"Failed to receive");
	ck_assert_str_eq (r1, default_str ("web.song_explain_failed"));
	const char *r2 = BarWsGetFriendlyErrorMessage (NULL, "song.explain",
			"No explanation");
	ck_assert_str_eq (r2, default_str ("web.song_explain_empty"));
}
END_TEST

START_TEST (test_ws_err_query_upcoming_empty) {
	const char *r = BarWsGetFriendlyErrorMessage (NULL, "query.upcoming",
			"No songs in queue");
	ck_assert_str_eq (r, default_str ("web.query_upcoming_empty"));
}
END_TEST

START_TEST (test_ws_err_station_change_not_found) {
	const char *r = BarWsGetFriendlyErrorMessage (NULL, "station.change",
			"Station not found");
	ck_assert_str_eq (r, default_str ("web.station_change_not_found"));
}
END_TEST

START_TEST (test_ws_err_exact_socket_strings) {
	const char *r1 = BarWsGetFriendlyErrorMessage (NULL, "x",
			"No song is currently playing");
	ck_assert_str_eq (r1, default_str ("web.socket.no_song_playing"));
	const char *r2 = BarWsGetFriendlyErrorMessage (NULL, "x",
			"No station is selected");
	ck_assert_str_eq (r2, default_str ("web.socket.no_station_selected"));
	const char *r3 = BarWsGetFriendlyErrorMessage (NULL, "x",
			"Not connected to Pandora");
	ck_assert_str_eq (r3, default_str ("web.socket.not_connected"));
	const char *r4 = BarWsGetFriendlyErrorMessage (NULL, "x",
			"Action cannot be performed in current context");
	ck_assert_str_eq (r4, default_str ("web.socket.action_context"));
}
END_TEST

START_TEST (test_ws_err_op_fallback_each) {
	const struct {
		const char *op;
		const char *key;
	} rows[] = {
		{"station.rename", "error.ws.op.station_rename"},
		{"station.addMusic", "error.ws.op.station_add_music"},
		{"station.addGenre", "error.ws.op.station_add_genre"},
		{"station.addShared", "error.ws.op.station_add_shared"},
		{"station.getGenres", "error.ws.op.station_get_genres"},
		{"station.getStationModes", "error.ws.op.station_get_station_modes"},
		{"station.setStationMode", "error.ws.op.station_set_station_mode"},
		{"station.getStationInfo", "error.ws.op.station_get_station_info"},
		{"station.deleteSeed", "error.ws.op.station_delete_seed"},
		{"station.deleteFeedback", "error.ws.op.station_delete_feedback"},
		{"music.search", "error.ws.op.music_search"},
		{"station.setQuickMix", "error.ws.op.station_set_quick_mix"},
		{"station.delete", "error.ws.op.station_delete"},
		{"station.createFrom", "error.ws.op.station_create_from"},
	};
	for (size_t i = 0; i < sizeof (rows) / sizeof (rows[0]); i++) {
		const char *r = BarWsGetFriendlyErrorMessage (NULL, rows[i].op,
				"opaque server failure");
		ck_assert_str_eq (r, default_str (rows[i].key));
	}
}
END_TEST

START_TEST (test_ws_err_returns_original_when_no_mapping) {
	const char *raw = "unique raw error xyz123";
	const char *r = BarWsGetFriendlyErrorMessage (NULL, "nonexistent.operation",
			raw);
	ck_assert_ptr_eq (r, raw);
}
END_TEST

Suite *error_messages_suite (void) {
	Suite *s = suite_create ("WebSocket error messages");
	TCase *tc = tcase_create ("friendly");
	tcase_add_test (tc, test_ws_err_null_original);
	tcase_add_test (tc, test_ws_err_not_allowed_rename);
	tcase_add_test (tc, test_ws_err_not_allowed_add_music);
	tcase_add_test (tc, test_ws_err_not_allowed_delete_seed);
	tcase_add_test (tc, test_ws_err_not_allowed_delete_feedback);
	tcase_add_test (tc, test_ws_err_not_allowed_set_station_mode);
	tcase_add_test (tc, test_ws_err_not_allowed_generic);
	tcase_add_test (tc, test_ws_err_network);
	tcase_add_test (tc, test_ws_err_auth);
	tcase_add_test (tc, test_ws_err_interrupted);
	tcase_add_test (tc, test_ws_err_failed_to_transform);
	tcase_add_test (tc, test_ws_err_pandora_reconnect);
	tcase_add_test (tc, test_ws_err_song_explain);
	tcase_add_test (tc, test_ws_err_query_upcoming_empty);
	tcase_add_test (tc, test_ws_err_station_change_not_found);
	tcase_add_test (tc, test_ws_err_exact_socket_strings);
	tcase_add_test (tc, test_ws_err_op_fallback_each);
	tcase_add_test (tc, test_ws_err_returns_original_when_no_mapping);
	suite_add_tcase (s, tc);
	return s;
}
