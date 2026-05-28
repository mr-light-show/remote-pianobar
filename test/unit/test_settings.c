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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../../src/main.h"
#include "../../src/settings.h"
#include "../../src/parse_utils.h"

static int write_file (const char *path, const char *content) {
	FILE *f = fopen (path, "w");
	if (f == NULL) {
		return -1;
	}
	if (fputs (content, f) == EOF) {
		fclose (f);
		return -1;
	}
	return fclose (f) == 0 ? 0 : -1;
}

static int mkdir_pianobar (const char *xdg) {
	char sub[512];
	snprintf (sub, sizeof (sub), "%s/pianobar", xdg);
	if (mkdir (sub, 0700) == 0) {
		return 0;
	}
	return errno == EEXIST ? 0 : -1;
}

static char *config_path (const char *xdg, char *out, size_t outsz) {
	snprintf (out, outsz, "%s/pianobar/config", xdg);
	return out;
}

/* Run BarSettingsRead with HOME and XDG_CONFIG_HOME pointing at xdg_root. */
static void read_settings_in_dir (BarSettings_t *settings, const char *xdg_root) {
	ck_assert_int_eq (setenv ("HOME", xdg_root, 1), 0);
	ck_assert_int_eq (setenv ("XDG_CONFIG_HOME", xdg_root, 1), 0);
	BarSettingsInit (settings);
	BarSettingsRead (settings);
}

START_TEST (test_settings_expand_tilde) {
	const char *home = "/home/testuser";
	char *a = BarSettingsExpandTilde ("~/pianobar/config", home);
	ck_assert_str_eq (a, "/home/testuser/pianobar/config");
	free (a);
	char *b = BarSettingsExpandTilde ("/absolute/path", home);
	ck_assert_str_eq (b, "/absolute/path");
	free (b);
}
END_TEST

START_TEST (test_settings_file_backed_account_only) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));

	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char accpath[512];
	snprintf (accpath, sizeof (accpath), "%s/pianobar/work.conf", tmpl);
	ck_assert_int_eq (write_file (accpath,
			"user = wuser\n"
			"password = wpass\n"
			"account_label = Work\n"
			"autostart_station = S1\n"), 0);

	char cfg[512];
	config_path (tmpl, cfg, sizeof (cfg));
	ck_assert_int_eq (write_file (cfg,
			"account = work:work.conf\n"), 0);

	BarSettings_t s;
	read_settings_in_dir (&s, tmpl);

	ck_assert_uint_eq (s.accountCount, 1);
	ck_assert_str_eq (s.accounts[0].id, "work");
	ck_assert_str_eq (s.accounts[0].label, "Work");
	ck_assert_str_eq (s.accounts[0].username, "wuser");
	ck_assert_str_eq (s.accounts[0].password, "wpass");
	ck_assert_str_eq (s.accounts[0].autostartStation, "S1");

	BarSettingsDestroy (&s);
}
END_TEST

START_TEST (test_settings_main_plus_file_accounts_default) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char accpath[512];
	snprintf (accpath, sizeof (accpath), "%s/pianobar/work.conf", tmpl);
	ck_assert_int_eq (write_file (accpath,
			"user = wuser\n"
			"password = wpass\n"
			"account_label = FromFile\n"), 0);

	char cfg[512];
	config_path (tmpl, cfg, sizeof (cfg));
	ck_assert_int_eq (write_file (cfg,
			"user = mainuser\n"
			"password = mainpass\n"
			"main_account_id = primary\n"
			"account_label = MainLbl\n"
			"account = work:work.conf\n"
			"default_account = work\n"), 0);

	BarSettings_t s;
	read_settings_in_dir (&s, tmpl);

	ck_assert_uint_eq (s.accountCount, 2);
	ck_assert_str_eq (s.accounts[0].id, "primary");
	ck_assert_str_eq (s.accounts[0].label, "MainLbl");
	ck_assert_str_eq (s.accounts[0].username, "mainuser");
	ck_assert_str_eq (s.accounts[0].password, "mainpass");

	ck_assert_str_eq (s.accounts[1].id, "work");
	ck_assert_str_eq (s.accounts[1].label, "FromFile");
	ck_assert_str_eq (s.accounts[1].username, "wuser");
	ck_assert_str_eq (s.accounts[1].password, "wpass");

	ck_assert_uint_eq (s.activeAccountIndex, 1);

	BarSettingsDestroy (&s);
}
END_TEST

START_TEST (test_settings_set_active_account_by_id) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char a1[512], a2[512];
	snprintf (a1, sizeof (a1), "%s/pianobar/a.conf", tmpl);
	snprintf (a2, sizeof (a2), "%s/pianobar/b.conf", tmpl);
	ck_assert_int_eq (write_file (a1, "user = ua\npassword = pa\n"), 0);
	ck_assert_int_eq (write_file (a2, "user = ub\npassword = pb\n"), 0);

	char cfg[512];
	config_path (tmpl, cfg, sizeof (cfg));
	ck_assert_int_eq (write_file (cfg,
			"account = a:a.conf\n"
			"account = b:b.conf\n"), 0);

	BarSettings_t s;
	read_settings_in_dir (&s, tmpl);

	ck_assert (BarSettingsSetActiveAccountById (&s, "b"));
	ck_assert_uint_eq (s.activeAccountIndex, 1);
	ck_assert_str_eq (BarSettingsGetActiveAccount (&s)->id, "b");

	ck_assert (!BarSettingsSetActiveAccountById (&s, "nope"));
	ck_assert_uint_eq (s.activeAccountIndex, 1);

	ck_assert (!BarSettingsSetActiveAccountById (&s, NULL));

	BarSettingsDestroy (&s);
}
END_TEST

START_TEST (test_settings_get_active_account_empty) {
	BarSettings_t s;
	BarSettingsInit (&s);
	ck_assert_ptr_null (BarSettingsGetActiveAccount (&s));
	BarSettingsDestroy (&s);
}
END_TEST

START_TEST (test_settings_locale_from_config) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char cfg[512];
	config_path (tmpl, cfg, sizeof (cfg));
	ck_assert_int_eq (write_file (cfg,
			"user = u\n"
			"password = p\n"
			"locale = de_AT.UTF-8\n"), 0);

	BarSettings_t s;
	read_settings_in_dir (&s, tmpl);

	ck_assert_ptr_nonnull (s.locale);
	ck_assert_str_eq (s.locale, "de_AT.UTF-8");

	BarSettingsDestroy (&s);
}
END_TEST

START_TEST (test_settings_table_dispatch_realistic_player_and_web_config) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char cfg[512];
	config_path (tmpl, cfg, sizeof (cfg));
	ck_assert_int_eq (write_file (cfg,
			"user = dispatch-user\n"
			"password = dispatch-pass\n"
			"audio_quality = medium\n"
			"sort = quickmix_10_name_za\n"
			"volume_mode = system\n"
			"audio_backend = alsa\n"
			"autoselect = 1\n"
			"station_display_name_override = /^(.*)$/Station: \\\\1/\n"
			"event_command = ~/bin/eventcmd\n"
			"fifo = ~/pianobar.fifo\n"
			"audio_pipe = ~/audio.pipe\n"
			"history = 7\n"
			"max_retry = 4\n"
			"timeout = 19\n"
			"pause_timeout = 11\n"
			"buffer_seconds = 9\n"
			"volume = 62\n"
			"system_volume_player_gain = -12\n"
			"max_gain = 10\n"
			"sample_rate = 44100\n"
			"gain_mul = 1.5\n"
			"ui_mode = web\n"
			"websocket_port = 8123\n"
			"websocket_host = 0.0.0.0\n"
			"webui_path = /srv/pianobar-web\n"
			"pid_file = ~/pianobar.pid\n"
			"log_file = ~/pianobar.log\n"), 0);

	BarSettings_t s;
	read_settings_in_dir (&s, tmpl);

	ck_assert_str_eq (s.username, "dispatch-user");
	ck_assert_str_eq (s.password, "dispatch-pass");
	ck_assert_int_eq (s.audioQuality, PIANO_AQ_MEDIUM);
	ck_assert_int_eq (s.sortOrder, BAR_SORT_QUICKMIX_10_NAME_ZA);
	ck_assert_int_eq (s.volumeMode, BAR_VOLUME_MODE_SYSTEM);
	ck_assert_int_eq (s.audioBackend, BAR_AUDIO_BACKEND_ALSA);
	ck_assert (s.autoselect);
	ck_assert_uint_eq (s.stationDisplayNameOverrideCount, 1);
	ck_assert (s.stationDisplayNameOverrides[0].valid);
	ck_assert_str_eq (s.stationDisplayNameOverrides[0].pattern, "^(.*)$");
	ck_assert_str_eq (s.stationDisplayNameOverrides[0].replacement, "Station: \\\\1");
	char expected[512];
	snprintf (expected, sizeof (expected), "%s/bin/eventcmd", tmpl);
	ck_assert_str_eq (s.eventCmd, expected);
	snprintf (expected, sizeof (expected), "%s/pianobar.fifo", tmpl);
	ck_assert_str_eq (s.fifo, expected);
	snprintf (expected, sizeof (expected), "%s/audio.pipe", tmpl);
	ck_assert_str_eq (s.audioPipe, expected);
	ck_assert_uint_eq (s.history, 7);
	ck_assert_uint_eq (s.maxRetry, 4);
	ck_assert_uint_eq (s.timeout, 19);
	ck_assert_uint_eq (s.pauseTimeout, 11);
	ck_assert_uint_eq (s.bufferSecs, 9);
	ck_assert_int_eq (s.volume, 62);
	ck_assert_int_eq (s.systemVolumePlayerGain, -12);
	ck_assert_int_eq (s.maxGain, 10);
	ck_assert_int_eq (s.sampleRate, 44100);
	ck_assert (s.gainMul > 1.49f && s.gainMul < 1.51f);
#ifdef WEBSOCKET_ENABLED
	ck_assert_int_eq (s.uiMode, BAR_UI_MODE_WEB);
	ck_assert_int_eq (s.websocketPort, 8123);
	ck_assert_str_eq (s.websocketHost, "0.0.0.0");
	ck_assert_str_eq (s.webuiPath, "/srv/pianobar-web");
	snprintf (expected, sizeof (expected), "%s/pianobar.pid", tmpl);
	ck_assert_str_eq (s.pidFile, expected);
	snprintf (expected, sizeof (expected), "%s/pianobar.log", tmpl);
	ck_assert_str_eq (s.logFile, expected);
#endif

	BarSettingsDestroy (&s);
}
END_TEST

START_TEST (test_settings_table_dispatch_handles_typical_user_overrides_and_typos) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char cfg[512];
	config_path (tmpl, cfg, sizeof (cfg));
	ck_assert_int_eq (write_file (cfg,
			"audio_quality = high\n"
			"audio_backend = coreaudio\n"
			"ui_mode = both\n"
			"autoselect = maybe\n"
			"volume = 200\n"
			"history = -1\n"
			"station_display_name_override = /[/broken/\n"
			"act_songlove = L\n"
			"act_songban = disabled\n"
			"format_msg_info = [info] %s\\n"), 0);

	BarSettings_t s;
	read_settings_in_dir (&s, tmpl);

	ck_assert_int_eq (s.audioQuality, PIANO_AQ_HIGH);
	ck_assert_int_eq (s.audioBackend, BAR_AUDIO_BACKEND_COREAUDIO);
#ifdef WEBSOCKET_ENABLED
	ck_assert_int_eq (s.uiMode, BAR_UI_MODE_BOTH);
#endif
	/* Invalid values should not clobber defaults. */
	ck_assert_int_ne (s.volume, 200);
	ck_assert_uint_ne (s.history, (unsigned int)-1);
	ck_assert_uint_eq (s.stationDisplayNameOverrideCount, 1);
	ck_assert (!s.stationDisplayNameOverrides[0].valid);
	ck_assert_int_eq (s.keys[BAR_KS_LOVE], 'L');
	ck_assert_int_eq (s.keys[BAR_KS_BAN], BAR_KS_DISABLED);
	ck_assert_str_eq (s.msgFormat[MSG_INFO].prefix, "[info] ");
	ck_assert_str_eq (s.msgFormat[MSG_INFO].postfix, "\\n");

	BarSettingsDestroy (&s);
}
END_TEST

START_TEST (test_settings_table_dispatch_remaining_backends_sorts_and_accounts) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char cfg[512];
	char acct[512];
	config_path (tmpl, cfg, sizeof (cfg));
	snprintf (acct, sizeof (acct), "%s/pianobar/work.conf", tmpl);
	ck_assert_int_eq (write_file (acct,
			"user = work-user\n"
			"password = work-pass\n"
			"account_label = Work Account\n"
			"autostart_station = ws-1\n"), 0);
	ck_assert_int_eq (write_file (cfg,
			"user = main-user\n"
			"password = main-pass\n"
			"sort = quickmix_01_name_za\n"
			"audio_backend = jack\n"
			"volume_mode = player\n"
			"ui_mode = cli\n"
			"account = work:work.conf\n"
			"default_account = work\n"
			"main_account_id = home\n"
			"account_label = Home Account\n"
			"format_msg_err = ERR:%s!\n"
			"unknown_future_key = ignored\n"), 0);

	BarSettings_t s;
	read_settings_in_dir (&s, tmpl);

	ck_assert_int_eq (s.sortOrder, BAR_SORT_QUICKMIX_01_NAME_ZA);
	ck_assert_int_eq (s.audioBackend, BAR_AUDIO_BACKEND_JACK);
	ck_assert_int_eq (s.volumeMode, BAR_VOLUME_MODE_PLAYER);
#ifdef WEBSOCKET_ENABLED
	ck_assert_int_eq (s.uiMode, BAR_UI_MODE_CLI);
#endif
	ck_assert_uint_eq (s.accountCount, 2);
	ck_assert_str_eq (s.accounts[0].id, "home");
	ck_assert_str_eq (s.accounts[0].label, "Home Account");
	ck_assert_str_eq (s.accounts[0].username, "main-user");
	ck_assert_str_eq (s.accounts[1].id, "work");
	ck_assert_str_eq (s.accounts[1].label, "Work Account");
	ck_assert_str_eq (s.accounts[1].username, "work-user");
	ck_assert_str_eq (s.accounts[1].password, "work-pass");
	ck_assert_str_eq (s.accounts[1].autostartStation, "ws-1");
	ck_assert_uint_eq (s.activeAccountIndex, 1);
	ck_assert_str_eq (s.msgFormat[MSG_ERR].prefix, "ERR:");
	ck_assert_str_eq (s.msgFormat[MSG_ERR].postfix, "!");

	BarSettingsDestroy (&s);
}
END_TEST

START_TEST (test_settings_table_dispatch_auto_and_pulseaudio_backends) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char cfg[512];
	config_path (tmpl, cfg, sizeof (cfg));
	ck_assert_int_eq (write_file (cfg,
			"audio_backend = pulseaudio\n"), 0);

	BarSettings_t s;
	read_settings_in_dir (&s, tmpl);
	ck_assert_int_eq (s.audioBackend, BAR_AUDIO_BACKEND_PULSEAUDIO);
	BarSettingsDestroy (&s);

	ck_assert_int_eq (write_file (cfg, "audio_backend = wasapi\n"), 0);
	read_settings_in_dir (&s, tmpl);
	ck_assert_int_eq (s.audioBackend, BAR_AUDIO_BACKEND_WASAPI);
	BarSettingsDestroy (&s);

	ck_assert_int_eq (write_file (cfg, "audio_backend = unknown-backend\n"), 0);
	read_settings_in_dir (&s, tmpl);
	ck_assert_int_eq (s.audioBackend, BAR_AUDIO_BACKEND_AUTO);
	BarSettingsDestroy (&s);
}
END_TEST

START_TEST (test_settings_table_dispatch_invalid_websocket_port_ignored) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char cfg[512];
	config_path (tmpl, cfg, sizeof (cfg));
	ck_assert_int_eq (write_file (cfg, "websocket_port = 70000\n"), 0);

	BarSettings_t s;
	read_settings_in_dir (&s, tmpl);
#ifdef WEBSOCKET_ENABLED
	/* Out-of-range values are ignored; default remains unset (0). */
	ck_assert_int_eq (s.websocketPort, 0);
#endif
	BarSettingsDestroy (&s);
}
END_TEST

START_TEST (test_settings_table_dispatch_rejects_malformed_account_line) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char cfg[512];
	config_path (tmpl, cfg, sizeof (cfg));
	ck_assert_int_eq (write_file (cfg,
			"user = solo\n"
			"password = solo\n"
			"account = missing-colon\n"
			"default_account = ghost\n"), 0);

	BarSettings_t s;
	read_settings_in_dir (&s, tmpl);
	ck_assert_uint_eq (s.accountCount, 1);
	ck_assert_str_eq (s.accounts[0].username, "solo");
	ck_assert_uint_eq (s.activeAccountIndex, 0);

	BarSettingsDestroy (&s);
}
END_TEST

START_TEST (test_settings_table_dispatch_all_sort_orders) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char cfg[512];
	config_path (tmpl, cfg, sizeof (cfg));

	static const struct { const char *key; BarStationSorting_t expect; } cases[] = {
		{"name_za", BAR_SORT_NAME_ZA},
		{"quickmix_01_name_az", BAR_SORT_QUICKMIX_01_NAME_AZ},
		{"quickmix_10_name_az", BAR_SORT_QUICKMIX_10_NAME_AZ},
		{"quickmix_10_name_za", BAR_SORT_QUICKMIX_10_NAME_ZA},
	};

	for (size_t i = 0; i < sizeof (cases) / sizeof (cases[0]); i++) {
		char body[64];
		snprintf (body, sizeof (body), "sort = %s\n", cases[i].key);
		ck_assert_int_eq (write_file (cfg, body), 0);
		BarSettings_t s;
		read_settings_in_dir (&s, tmpl);
		ck_assert_int_eq (s.sortOrder, cases[i].expect);
		BarSettingsDestroy (&s);
	}
}
END_TEST

START_TEST (test_parse_int_in_range_valid)
{
	int out = -1;
	ck_assert (BarParseIntInRange ("42", 0, 100, &out));
	ck_assert_int_eq (out, 42);
}
END_TEST

START_TEST (test_parse_int_in_range_empty_string_fails)
{
	int out = 99;
	ck_assert (!BarParseIntInRange ("", 0, 100, &out));
	ck_assert_int_eq (out, 99);
}
END_TEST

START_TEST (test_parse_int_in_range_non_numeric_fails)
{
	int out = 99;
	ck_assert (!BarParseIntInRange ("abc", 0, 100, &out));
	ck_assert_int_eq (out, 99);
}
END_TEST

START_TEST (test_parse_int_in_range_below_min_fails)
{
	int out = 99;
	ck_assert (!BarParseIntInRange ("-1", 0, 100, &out));
	ck_assert_int_eq (out, 99);
}
END_TEST

START_TEST (test_parse_int_in_range_above_max_fails)
{
	int out = 99;
	ck_assert (!BarParseIntInRange ("101", 0, 100, &out));
	ck_assert_int_eq (out, 99);
}
END_TEST

START_TEST (test_parse_int_in_range_null_string_fails)
{
	int out = 99;
	ck_assert (!BarParseIntInRange (NULL, 0, 100, &out));
	ck_assert_int_eq (out, 99);
}
END_TEST

START_TEST (test_parse_int_in_range_null_out_fails)
{
	ck_assert (!BarParseIntInRange ("42", 0, 100, NULL));
}
END_TEST

START_TEST (test_parse_int_in_range_boundary_min)
{
	int out = -1;
	ck_assert (BarParseIntInRange ("0", 0, 100, &out));
	ck_assert_int_eq (out, 0);
}
END_TEST

START_TEST (test_parse_int_in_range_boundary_max)
{
	int out = -1;
	ck_assert (BarParseIntInRange ("100", 0, 100, &out));
	ck_assert_int_eq (out, 100);
}
END_TEST

START_TEST (test_parse_int_in_range_trailing_garbage_fails)
{
	int out = 99;
	ck_assert (!BarParseIntInRange ("42abc", 0, 100, &out));
	ck_assert_int_eq (out, 99);
}
END_TEST

Suite *settings_suite (void) {
	Suite *s = suite_create ("Settings");
	TCase *tc = tcase_create ("Core");
	tcase_add_test (tc, test_settings_expand_tilde);
	tcase_add_test (tc, test_settings_file_backed_account_only);
	tcase_add_test (tc, test_settings_main_plus_file_accounts_default);
	tcase_add_test (tc, test_settings_set_active_account_by_id);
	tcase_add_test (tc, test_settings_get_active_account_empty);
	tcase_add_test (tc, test_settings_locale_from_config);
	tcase_add_test (tc, test_settings_table_dispatch_realistic_player_and_web_config);
	tcase_add_test (tc, test_settings_table_dispatch_handles_typical_user_overrides_and_typos);
	tcase_add_test (tc, test_settings_table_dispatch_remaining_backends_sorts_and_accounts);
	tcase_add_test (tc, test_settings_table_dispatch_auto_and_pulseaudio_backends);
	tcase_add_test (tc, test_settings_table_dispatch_invalid_websocket_port_ignored);
	tcase_add_test (tc, test_settings_table_dispatch_rejects_malformed_account_line);
	tcase_add_test (tc, test_settings_table_dispatch_all_sort_orders);

	TCase *tc_parse = tcase_create ("ParseIntInRange");
	tcase_add_test (tc_parse, test_parse_int_in_range_valid);
	tcase_add_test (tc_parse, test_parse_int_in_range_empty_string_fails);
	tcase_add_test (tc_parse, test_parse_int_in_range_non_numeric_fails);
	tcase_add_test (tc_parse, test_parse_int_in_range_below_min_fails);
	tcase_add_test (tc_parse, test_parse_int_in_range_above_max_fails);
	tcase_add_test (tc_parse, test_parse_int_in_range_null_string_fails);
	tcase_add_test (tc_parse, test_parse_int_in_range_null_out_fails);
	tcase_add_test (tc_parse, test_parse_int_in_range_boundary_min);
	tcase_add_test (tc_parse, test_parse_int_in_range_boundary_max);
	tcase_add_test (tc_parse, test_parse_int_in_range_trailing_garbage_fails);

	suite_add_tcase (s, tc);
	suite_add_tcase (s, tc_parse);
	return s;
}
