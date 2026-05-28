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
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

/* Forward-declare only what this test exercises to avoid pulling in the full
   ui_act.h → ui_dispatch.h static dispatch table */
int BarUiActBuildManageStationQuestion (char *buf, size_t bufLen,
		bool hasArtistSeeds, bool hasSongSeeds, bool hasStationSeeds,
		bool hasFeedback, bool isQuickMix);

START_TEST (test_manage_station_question_all_actions_no_overflow)
{
	char buf[512];
	int ret = BarUiActBuildManageStationQuestion (buf, sizeof (buf),
			true, true, true, true, false);
	ck_assert_int_ge (ret, 0);
	ck_assert (strlen (buf) < sizeof (buf));
	ck_assert (strstr (buf, "[a]rtist") != NULL);
	ck_assert (strstr (buf, "[s]ong") != NULL);
	ck_assert (strstr (buf, "s[t]ation") != NULL);
	ck_assert (strstr (buf, "[f]eedback") != NULL);
}
END_TEST

START_TEST (test_manage_station_question_short_buffer_truncates_safely)
{
	char buf[16];
	int ret = BarUiActBuildManageStationQuestion (buf, sizeof (buf),
			true, true, true, true, false);
	/* Must not overflow regardless of how small the buffer is */
	ck_assert_int_ge (ret, 0);
	ck_assert (strlen (buf) < sizeof (buf));
}
END_TEST

START_TEST (test_manage_station_question_no_seeds_shows_mode_only)
{
	char buf[512];
	/* No seeds, no feedback, not a QuickMix — only mode should appear */
	int ret = BarUiActBuildManageStationQuestion (buf, sizeof (buf),
			false, false, false, false, false);
	ck_assert_int_ge (ret, 0);
	ck_assert (strstr (buf, "Manage [m]ode?") != NULL);
	ck_assert (strstr (buf, "[a]rtist") == NULL);
}
END_TEST

START_TEST (test_manage_station_question_quickmix_no_mode_option)
{
	char buf[512];
	/* QuickMix stations do not offer mode management */
	int ret = BarUiActBuildManageStationQuestion (buf, sizeof (buf),
			false, false, false, false, true);
	ck_assert_int_ge (ret, 0);
	ck_assert (strstr (buf, "Manage [m]ode?") == NULL);
}
END_TEST

START_TEST (test_manage_station_question_artist_seed_only)
{
	char buf[512];
	int ret = BarUiActBuildManageStationQuestion (buf, sizeof (buf),
			true, false, false, false, false);
	ck_assert_int_ge (ret, 0);
	ck_assert (strstr (buf, "Delete") != NULL);
	ck_assert (strstr (buf, "[a]rtist") != NULL);
	ck_assert (strstr (buf, "[s]ong") == NULL);
	ck_assert (strstr (buf, "s[t]ation") == NULL);
}
END_TEST

Suite *ui_act_station_suite (void) {
	Suite *s = suite_create ("ui_act_station");
	TCase *tc = tcase_create ("core");
	tcase_add_test (tc, test_manage_station_question_all_actions_no_overflow);
	tcase_add_test (tc, test_manage_station_question_short_buffer_truncates_safely);
	tcase_add_test (tc, test_manage_station_question_no_seeds_shows_mode_only);
	tcase_add_test (tc, test_manage_station_question_quickmix_no_mode_option);
	tcase_add_test (tc, test_manage_station_question_artist_seed_only);
	suite_add_tcase (s, tc);
	return s;
}
