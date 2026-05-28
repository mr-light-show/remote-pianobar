/*
Copyright (c) 2026

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/main.h"
#include "../../src/settings.h"
#include "../../src/ui.h"

static char *read_tmpfile (FILE *stream)
{
	char *buf = NULL;
	size_t cap = 0;
	size_t len = 0;
	int c;

	rewind (stream);
	while ((c = fgetc (stream)) != EOF) {
		if (len + 1 >= cap) {
			cap = cap == 0 ? 256 : cap * 2;
			buf = realloc (buf, cap);
			ck_assert_ptr_nonnull (buf);
		}
		buf[len++] = (char) c;
	}
	if (buf == NULL) {
		buf = strdup ("");
	} else {
		buf[len] = '\0';
	}
	return buf;
}

START_TEST (test_print_startup_info_cli_mode_omits_web_details)
{
	BarApp_t app;
	FILE *out;
	char *text;

	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_CLI;

	out = tmpfile ();
	ck_assert_ptr_nonnull (out);
	BarPrintStartupInfo (&app, 0, false, out);
	text = read_tmpfile (out);

	ck_assert (strstr (text, "Welcome") != NULL);
	ck_assert (strstr (text, "Web interface") == NULL);

	free (text);
	fclose (out);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_print_startup_info_web_mode_includes_url)
{
	BarApp_t app;
	FILE *out;
	char *text;

	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.websocketHost = strdup ("192.0.2.9");
	app.settings.websocketPort = 8088;

	out = tmpfile ();
	ck_assert_ptr_nonnull (out);
	BarPrintStartupInfo (&app, 0, false, out);
	text = read_tmpfile (out);

	ck_assert (strstr (text, "192.0.2.9") != NULL);
	ck_assert (strstr (text, "8088") != NULL);

	free (text);
	fclose (out);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_print_startup_info_daemon_includes_pid_and_pid_file)
{
	BarApp_t app;
	FILE *out;
	char *text;

	memset (&app, 0, sizeof (app));
	BarSettingsInit (&app.settings);
	app.settings.uiMode = BAR_UI_MODE_WEB;
	app.settings.websocketHost = strdup ("127.0.0.1");
	app.settings.websocketPort = 8080;
	app.settings.pidFile = strdup ("/tmp/pianobar-test.pid");

	out = tmpfile ();
	ck_assert_ptr_nonnull (out);
	BarPrintStartupInfo (&app, 4242, true, out);
	text = read_tmpfile (out);

	ck_assert (strstr (text, "PID: 4242") != NULL);
	ck_assert (strstr (text, "/tmp/pianobar-test.pid") != NULL);

	free (text);
	fclose (out);
	BarSettingsDestroy (&app.settings);
}
END_TEST

START_TEST (test_sorted_stations_orders_by_name_az)
{
	PianoStation_t beta, alpha;
	size_t count = 0;
	PianoStation_t **sorted;

	memset (&beta, 0, sizeof (beta));
	memset (&alpha, 0, sizeof (alpha));
	beta.name = "Beta Station";
	alpha.name = "Alpha Station";
	beta.head.next = (PianoListHead_t *) &alpha;

	sorted = BarSortedStations (&beta, &count, BAR_SORT_NAME_AZ);
	ck_assert_ptr_nonnull (sorted);
	ck_assert_uint_eq (count, 2);
	ck_assert_str_eq (sorted[0]->name, "Alpha Station");
	ck_assert_str_eq (sorted[1]->name, "Beta Station");
	free (sorted);
}
END_TEST

START_TEST (test_sorted_stations_orders_by_name_za)
{
	PianoStation_t beta, alpha;
	size_t count = 0;
	PianoStation_t **sorted;

	memset (&beta, 0, sizeof (beta));
	memset (&alpha, 0, sizeof (alpha));
	beta.name = "Beta Station";
	alpha.name = "Alpha Station";
	beta.head.next = (PianoListHead_t *) &alpha;

	sorted = BarSortedStations (&beta, &count, BAR_SORT_NAME_ZA);
	ck_assert_ptr_nonnull (sorted);
	ck_assert_uint_eq (count, 2);
	ck_assert_str_eq (sorted[0]->name, "Beta Station");
	ck_assert_str_eq (sorted[1]->name, "Alpha Station");
	free (sorted);
}
END_TEST

Suite *ui_suite (void)
{
	Suite *s = suite_create ("ui");
	TCase *tc = tcase_create ("core");

	tcase_add_test (tc, test_print_startup_info_cli_mode_omits_web_details);
	tcase_add_test (tc, test_print_startup_info_web_mode_includes_url);
	tcase_add_test (tc, test_print_startup_info_daemon_includes_pid_and_pid_file);
	tcase_add_test (tc, test_sorted_stations_orders_by_name_az);
	tcase_add_test (tc, test_sorted_stations_orders_by_name_za);
	suite_add_tcase (s, tc);
	return s;
}
