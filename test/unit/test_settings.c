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

Suite *settings_suite (void) {
	Suite *s = suite_create ("Settings");
	TCase *tc = tcase_create ("Core");
	tcase_add_test (tc, test_settings_expand_tilde);
	tcase_add_test (tc, test_settings_file_backed_account_only);
	tcase_add_test (tc, test_settings_main_plus_file_accounts_default);
	tcase_add_test (tc, test_settings_set_active_account_by_id);
	tcase_add_test (tc, test_settings_get_active_account_empty);
	tcase_add_test (tc, test_settings_locale_from_config);
	suite_add_tcase (s, tc);
	return s;
}
