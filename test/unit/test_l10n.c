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
	suite_add_tcase (s, tc);
	return s;
}
