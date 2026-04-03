/*
 * Unit tests for BarL10n (generated locale file format + defaults fallback).
 */

#include <check.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../src/l10n.h"
#include "../../src/settings.h"

/** Create intermediate directories for a path ending in a file (mkdir -p). */
static int
test_mkdir_p_for_file (const char *filepath)
{
	char dir[512];
	const char *slash = strrchr (filepath, '/');
	if (!slash || slash == filepath) {
		return -1;
	}
	size_t n = (size_t) (slash - filepath);
	if (n >= sizeof (dir)) {
		return -1;
	}
	memcpy (dir, filepath, n);
	dir[n] = '\0';
	for (size_t i = 1; i < n; i++) {
		if (dir[i] == '/') {
			dir[i] = '\0';
			if (mkdir (dir, 0755) != 0 && errno != EEXIST) {
				return -1;
			}
			dir[i] = '/';
		}
	}
	if (mkdir (dir, 0755) != 0 && errno != EEXIST) {
		return -1;
	}
	return 0;
}

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

START_TEST (test_l10n_format_missing_key_clears_buf) {
	char buf[32];
	strcpy (buf, "keep");
	BarL10nFormat (NULL, buf, sizeof (buf), "no.such.key.for.format.test", "arg");
	ck_assert_int_eq (buf[0], '\0');
}
END_TEST

START_TEST (test_l10n_get_module_null_or_empty_module_uses_base) {
	const char *r1 = BarL10nGetModule (NULL, "cli.login", NULL);
	ck_assert_str_eq (r1, BarL10nDefaultLookup ("cli.login"));
	const char *r2 = BarL10nGetModule (NULL, "cli.login", "");
	ck_assert_str_eq (r2, BarL10nDefaultLookup ("cli.login"));
}
END_TEST

START_TEST (test_l10n_unescape_backslash) {
	unsetenv ("PIANOBAR_LOCALE_DIR");
	char tmpl[] = "/tmp/pb_l10n_bs_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	char fp[512];
	snprintf (fp, sizeof (fp), "%s/en", tmpl);
	FILE *out = fopen (fp, "w");
	ck_assert_ptr_nonnull (out);
	fprintf (out, "cli.login = path\\\\to\\\\file\n");
	fclose (out);

	setenv ("PIANOBAR_LOCALE_DIR", tmpl, 1);

	BarSettings_t settings;
	BarSettingsInit (&settings);
	settings.locale = strdup ("en");

	BarL10nContext_t ctx;
	ck_assert (BarL10nInit (&ctx, &settings));
	ck_assert_str_eq (BarL10nGet (&ctx, "cli.login"), "path\\to\\file");

	BarL10nDestroy (&ctx);
	BarSettingsDestroy (&settings);
	unsetenv ("PIANOBAR_LOCALE_DIR");
	unlink (fp);
	rmdir (tmpl);
}
END_TEST

START_TEST (test_l10n_load_from_xdg_config_home) {
	const char *xdg_was = getenv ("XDG_CONFIG_HOME");
	char *xdg_backup = xdg_was ? strdup (xdg_was) : NULL;
	unsetenv ("PIANOBAR_LOCALE_DIR");

	char base[] = "/tmp/pb_l10n_xdg_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (base));
	char fp[512];
	snprintf (fp, sizeof (fp), "%s/pianobar/locale/en", base);
	ck_assert_int_eq (test_mkdir_p_for_file (fp), 0);
	FILE *out = fopen (fp, "w");
	ck_assert_ptr_nonnull (out);
	fprintf (out, "cli.login = FromXdg\n");
	fclose (out);

	setenv ("XDG_CONFIG_HOME", base, 1);

	BarSettings_t settings;
	BarSettingsInit (&settings);
	settings.locale = strdup ("en");

	BarL10nContext_t ctx;
	ck_assert (BarL10nInit (&ctx, &settings));
	ck_assert_str_eq (BarL10nGet (&ctx, "cli.login"), "FromXdg");

	BarL10nDestroy (&ctx);
	BarSettingsDestroy (&settings);
	unlink (fp);
	{
		char d[512];
		snprintf (d, sizeof (d), "%s/pianobar/locale", base);
		rmdir (d);
		snprintf (d, sizeof (d), "%s/pianobar", base);
		rmdir (d);
	}
	rmdir (base);
	if (xdg_backup) {
		setenv ("XDG_CONFIG_HOME", xdg_backup, 1);
		free (xdg_backup);
	} else {
		unsetenv ("XDG_CONFIG_HOME");
	}
}
END_TEST

START_TEST (test_l10n_load_from_home_dot_config) {
	const char *home_was = getenv ("HOME");
	const char *xdg_was = getenv ("XDG_CONFIG_HOME");
	char *home_backup = home_was ? strdup (home_was) : NULL;
	char *xdg_backup = xdg_was ? strdup (xdg_was) : NULL;
	unsetenv ("PIANOBAR_LOCALE_DIR");
	unsetenv ("XDG_CONFIG_HOME");

	char base[] = "/tmp/pb_l10n_home_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (base));
	char fp[512];
	snprintf (fp, sizeof (fp), "%s/.config/pianobar/locale/en", base);
	ck_assert_int_eq (test_mkdir_p_for_file (fp), 0);
	FILE *out = fopen (fp, "w");
	ck_assert_ptr_nonnull (out);
	fprintf (out, "cli.login = FromHomeConfig\n");
	fclose (out);

	setenv ("HOME", base, 1);

	BarSettings_t settings;
	BarSettingsInit (&settings);
	settings.locale = strdup ("en");

	BarL10nContext_t ctx;
	ck_assert (BarL10nInit (&ctx, &settings));
	ck_assert_str_eq (BarL10nGet (&ctx, "cli.login"), "FromHomeConfig");

	BarL10nDestroy (&ctx);
	BarSettingsDestroy (&settings);
	unlink (fp);
	/* remove .config/pianobar/locale */
	{
		char d[512];
		snprintf (d, sizeof (d), "%s/.config/pianobar/locale", base);
		rmdir (d);
		snprintf (d, sizeof (d), "%s/.config/pianobar", base);
		rmdir (d);
		snprintf (d, sizeof (d), "%s/.config", base);
		rmdir (d);
	}
	rmdir (base);
	if (home_backup) {
		setenv ("HOME", home_backup, 1);
		free (home_backup);
	} else {
		unsetenv ("HOME");
	}
	if (xdg_backup) {
		setenv ("XDG_CONFIG_HOME", xdg_backup, 1);
		free (xdg_backup);
	}
}
END_TEST

START_TEST (test_l10n_load_from_install_prefix) {
	const char *pfx_was = getenv ("PIANOBAR_INSTALL_PREFIX");
	const char *home_was = getenv ("HOME");
	const char *xdg_was = getenv ("XDG_CONFIG_HOME");
	char *pfx_backup = pfx_was ? strdup (pfx_was) : NULL;
	char *home_backup = home_was ? strdup (home_was) : NULL;
	char *xdg_backup = xdg_was ? strdup (xdg_was) : NULL;

	unsetenv ("PIANOBAR_LOCALE_DIR");
	unsetenv ("XDG_CONFIG_HOME");
	unsetenv ("HOME");

	char base[] = "/tmp/pb_l10n_pfx_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (base));
	char fp[512];
	snprintf (fp, sizeof (fp), "%s/share/pianobar/locale/en", base);
	ck_assert_int_eq (test_mkdir_p_for_file (fp), 0);
	FILE *out = fopen (fp, "w");
	ck_assert_ptr_nonnull (out);
	fprintf (out, "cli.login = FromPrefix\n");
	fclose (out);

	setenv ("PIANOBAR_INSTALL_PREFIX", base, 1);

	BarSettings_t settings;
	BarSettingsInit (&settings);
	settings.locale = strdup ("en");

	BarL10nContext_t ctx;
	ck_assert (BarL10nInit (&ctx, &settings));
	ck_assert_str_eq (BarL10nGet (&ctx, "cli.login"), "FromPrefix");

	BarL10nDestroy (&ctx);
	BarSettingsDestroy (&settings);
	unlink (fp);
	{
		char d[512];
		snprintf (d, sizeof (d), "%s/share/pianobar/locale", base);
		rmdir (d);
		snprintf (d, sizeof (d), "%s/share/pianobar", base);
		rmdir (d);
		snprintf (d, sizeof (d), "%s/share", base);
		rmdir (d);
	}
	rmdir (base);

	if (pfx_backup) {
		setenv ("PIANOBAR_INSTALL_PREFIX", pfx_backup, 1);
		free (pfx_backup);
	} else {
		unsetenv ("PIANOBAR_INSTALL_PREFIX");
	}
	if (home_backup) {
		setenv ("HOME", home_backup, 1);
		free (home_backup);
	}
	if (xdg_backup) {
		setenv ("XDG_CONFIG_HOME", xdg_backup, 1);
		free (xdg_backup);
	}
}
END_TEST

START_TEST (test_l10n_init_prefers_lc_messages_over_lang) {
	const char *lang_was = getenv ("LANG");
	const char *lc_was = getenv ("LC_MESSAGES");
	char *lang_backup = lang_was ? strdup (lang_was) : NULL;
	char *lc_backup = lc_was ? strdup (lc_was) : NULL;

	char tmpl[] = "/tmp/pb_l10n_lc_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	char fp[512];
	snprintf (fp, sizeof (fp), "%s/en", tmpl);
	FILE *out = fopen (fp, "w");
	ck_assert_ptr_nonnull (out);
	fprintf (out, "cli.login = FromLC\n");
	fclose (out);

	setenv ("PIANOBAR_LOCALE_DIR", tmpl, 1);
	setenv ("LC_MESSAGES", "en_US.UTF-8", 1);
	setenv ("LANG", "fr_FR.UTF-8", 1);

	BarSettings_t settings;
	BarSettingsInit (&settings);
	settings.locale = NULL;

	BarL10nContext_t ctx;
	ck_assert (BarL10nInit (&ctx, &settings));
	ck_assert_str_eq (BarL10nGet (&ctx, "cli.login"), "FromLC");

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
	tcase_add_test (tc, test_l10n_format_missing_key_clears_buf);
	tcase_add_test (tc, test_l10n_get_module_null_or_empty_module_uses_base);
	tcase_add_test (tc, test_l10n_unescape_backslash);
	tcase_add_test (tc, test_l10n_load_from_xdg_config_home);
	tcase_add_test (tc, test_l10n_load_from_home_dot_config);
	tcase_add_test (tc, test_l10n_load_from_install_prefix);
	tcase_add_test (tc, test_l10n_init_prefers_lc_messages_over_lang);
	suite_add_tcase (s, tc);
	return s;
}
