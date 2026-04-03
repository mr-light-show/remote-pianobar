/*
 * Unit tests for BarL10n (generated locale file format + defaults fallback).
 */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../src/l10n.h"
#include "../../src/settings.h"

START_TEST (test_l10n_format_with_placeholder) {
	char buf[256];
	BarL10nFormat (NULL, buf, sizeof (buf), "cli.email_echo", "user@example.com");
	ck_assert_str_eq (buf, "Email: user@example.com\n");
}
END_TEST

START_TEST (test_l10n_get_module_ws) {
	const char *r = BarL10nGetModule (NULL, "cli.disconnect", "ws");
	ck_assert_str_eq (r, "Pandora is disconnected.");
}
END_TEST

START_TEST (test_l10n_destroy_null) {
	BarL10nDestroy (NULL);
	ck_assert (1);
}
END_TEST

START_TEST (test_l10n_parse_skips_bad_line_and_unescape_cr) {
	unsetenv ("PIANOBAR_LOCALE_DIR");
	char tmpl[] = "/tmp/pb_l10n_test_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	char fp[512];
	snprintf (fp, sizeof (fp), "%s/en", tmpl);
	FILE *out = fopen (fp, "w");
	ck_assert_ptr_nonnull (out);
	fprintf (out,
			"# comment line\n"
			"   \n"
			"not_a_key_line\n"
			"cli.login = X\n"
			"cli.escape = a\\rb\n");
	fclose (out);

	setenv ("PIANOBAR_LOCALE_DIR", tmpl, 1);

	BarSettings_t settings;
	BarSettingsInit (&settings);
	settings.locale = strdup ("en");

	BarL10nContext_t ctx;
	ck_assert (BarL10nInit (&ctx, &settings));
	ck_assert_str_eq (BarL10nGet (&ctx, "cli.login"), "X");
	ck_assert_str_eq (BarL10nGet (&ctx, "cli.escape"), "a\rb");

	BarL10nDestroy (&ctx);
	BarSettingsDestroy (&settings);
	unsetenv ("PIANOBAR_LOCALE_DIR");
	unlink (fp);
	rmdir (tmpl);
}
END_TEST

START_TEST (test_l10n_init_lang_from_env) {
	const char *lang_was = getenv ("LANG");
	const char *lc_was = getenv ("LC_MESSAGES");
	char *lang_backup = lang_was ? strdup (lang_was) : NULL;
	char *lc_backup = lc_was ? strdup (lc_was) : NULL;
	unsetenv ("LC_MESSAGES");

	char tmpl[] = "/tmp/pb_l10n_lang_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	char fp[512];
	snprintf (fp, sizeof (fp), "%s/en", tmpl);
	FILE *out = fopen (fp, "w");
	ck_assert_ptr_nonnull (out);
	fprintf (out, "cli.login = FromEnv\n");
	fclose (out);

	setenv ("PIANOBAR_LOCALE_DIR", tmpl, 1);
	setenv ("LANG", "en_US.UTF-8", 1);

	BarSettings_t settings;
	BarSettingsInit (&settings);
	/* locale NULL -> bar_l10n_normalize_lang uses LANG -> "en" */
	settings.locale = NULL;

	BarL10nContext_t ctx;
	ck_assert (BarL10nInit (&ctx, &settings));
	ck_assert_str_eq (BarL10nGet (&ctx, "cli.login"), "FromEnv");

	BarL10nDestroy (&ctx);
	BarSettingsDestroy (&settings);
	unsetenv ("PIANOBAR_LOCALE_DIR");
	if (lang_backup) {
		setenv ("LANG", lang_backup, 1);
		free (lang_backup);
	} else {
		unsetenv ("LANG");
	}
	if (lc_backup) {
		setenv ("LC_MESSAGES", lc_backup, 1);
		free (lc_backup);
	} else {
		unsetenv ("LC_MESSAGES");
	}
	unlink (fp);
	rmdir (tmpl);
}
END_TEST

START_TEST (test_l10n_defaults_lookup) {
	const char *v = BarL10nDefaultLookup ("cli.login");
	ck_assert_ptr_nonnull (v);
	ck_assert_str_eq (v, "Login... ");
}
END_TEST

START_TEST (test_l10n_get_null_ctx_uses_defaults) {
	const char *v = BarL10nGet (NULL, "error.ws.unknown");
	ck_assert_ptr_nonnull (v);
	ck_assert_str_eq (v, "An unknown error occurred");
}
END_TEST

START_TEST (test_l10n_load_override_file) {
	unsetenv ("PIANOBAR_LOCALE_DIR");
	char tmpl[] = "/tmp/pb_l10n_test_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	char fp[512];
	snprintf (fp, sizeof (fp), "%s/en", tmpl);
	FILE *out = fopen (fp, "w");
	ck_assert_ptr_nonnull (out);
	fprintf (out, "cli.login = CustomLogin\n");
	fprintf (out, "cli.escape = line1\\nline2\n");
	fclose (out);

	ck_assert_int_eq (access (fp, R_OK), 0);

	setenv ("PIANOBAR_LOCALE_DIR", tmpl, 1);

	BarSettings_t settings;
	BarSettingsInit (&settings);
	settings.locale = strdup ("en");

	BarL10nContext_t ctx;
	ck_assert (BarL10nInit (&ctx, &settings));
	ck_assert_str_eq (BarL10nGet (&ctx, "cli.login"), "CustomLogin");
	ck_assert_str_eq (BarL10nGet (&ctx, "cli.escape"), "line1\nline2");
	/* Missing key falls back to defaults */
	ck_assert_str_eq (BarL10nGet (&ctx, "error.ws.network"),
			"Unable to connect to Pandora (check your network connection)");

	BarL10nDestroy (&ctx);
	BarSettingsDestroy (&settings);
	unsetenv ("PIANOBAR_LOCALE_DIR");
	unlink (fp);
	rmdir (tmpl);
}
END_TEST

START_TEST (test_l10n_module_fallback) {
	BarL10nContext_t ctx;
	memset (&ctx, 0, sizeof (ctx));
	const char *r = BarL10nGetModule (&ctx, "cli.disconnect", "ws");
	ck_assert_ptr_nonnull (r);
	ck_assert_str_eq (r, "Pandora is disconnected.");
}
END_TEST

Suite *l10n_suite (void) {
	Suite *s = suite_create ("l10n");
	TCase *tc = tcase_create ("core");
	tcase_add_test (tc, test_l10n_defaults_lookup);
	tcase_add_test (tc, test_l10n_get_null_ctx_uses_defaults);
	tcase_add_test (tc, test_l10n_load_override_file);
	tcase_add_test (tc, test_l10n_module_fallback);
	tcase_add_test (tc, test_l10n_format_with_placeholder);
	tcase_add_test (tc, test_l10n_get_module_ws);
	tcase_add_test (tc, test_l10n_destroy_null);
	tcase_add_test (tc, test_l10n_parse_skips_bad_line_and_unescape_cr);
	tcase_add_test (tc, test_l10n_init_lang_from_env);
	suite_add_tcase (s, tc);
	return s;
}
