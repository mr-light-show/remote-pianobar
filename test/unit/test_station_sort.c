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
#include <stdlib.h>
#include <string.h>

#include "../../src/station_sort.h"

START_TEST (test_sort_station_snapshots_noop_edge_cases)
{
	BarStationSnapshot_t item;

	memset (&item, 0, sizeof (item));
	item.name = "Only";

	BarSortStationSnapshots (NULL, 2, BAR_SORT_NAME_AZ);
	BarSortStationSnapshots (&item, 0, BAR_SORT_NAME_AZ);
	BarSortStationSnapshots (&item, 1, BAR_SORT_NAME_AZ);

	ck_assert_str_eq (item.name, "Only");
}
END_TEST

START_TEST (test_sort_station_snapshots_name_az)
{
	BarStationSnapshot_t items[2];

	memset (items, 0, sizeof (items));
	items[0].name = "Beta";
	items[1].name = "Alpha";

	BarSortStationSnapshots (items, 2, BAR_SORT_NAME_AZ);

	ck_assert_str_eq (items[0].name, "Alpha");
	ck_assert_str_eq (items[1].name, "Beta");
}
END_TEST

START_TEST (test_sort_station_snapshots_name_za)
{
	BarStationSnapshot_t items[2];

	memset (items, 0, sizeof (items));
	items[0].name = "Alpha";
	items[1].name = "Beta";

	BarSortStationSnapshots (items, 2, BAR_SORT_NAME_ZA);

	ck_assert_str_eq (items[0].name, "Beta");
	ck_assert_str_eq (items[1].name, "Alpha");
}
END_TEST

START_TEST (test_sort_station_snapshots_quickmix_01_name_az)
{
	BarStationSnapshot_t items[2];

	memset (items, 0, sizeof (items));
	items[0].name = "Zzz QuickMix";
	items[0].isQuickMix = true;
	items[1].name = "Alpha";

	BarSortStationSnapshots (items, 2, BAR_SORT_QUICKMIX_01_NAME_AZ);

	ck_assert_str_eq (items[0].name, "Alpha");
	ck_assert (items[1].isQuickMix);
	ck_assert_str_eq (items[1].name, "Zzz QuickMix");
}
END_TEST

START_TEST (test_sort_station_snapshots_quickmix_01_name_za)
{
	BarStationSnapshot_t items[3];

	memset (items, 0, sizeof (items));
	items[0].name = "Alpha";
	items[1].name = "Beta";
	items[2].name = "Zzz QuickMix";
	items[2].isQuickMix = true;

	BarSortStationSnapshots (items, 3, BAR_SORT_QUICKMIX_01_NAME_ZA);

	ck_assert_str_eq (items[0].name, "Beta");
	ck_assert_str_eq (items[1].name, "Alpha");
	ck_assert (items[2].isQuickMix);
}
END_TEST

START_TEST (test_sort_station_snapshots_quickmix_10_name_az)
{
	BarStationSnapshot_t items[2];

	memset (items, 0, sizeof (items));
	items[0].name = "Alpha";
	items[1].name = "Zzz QuickMix";
	items[1].isQuickMix = true;

	BarSortStationSnapshots (items, 2, BAR_SORT_QUICKMIX_10_NAME_AZ);

	ck_assert (items[0].isQuickMix);
	ck_assert_str_eq (items[0].name, "Zzz QuickMix");
	ck_assert_str_eq (items[1].name, "Alpha");
}
END_TEST

START_TEST (test_sort_station_snapshots_quickmix_10_name_za)
{
	BarStationSnapshot_t items[3];

	memset (items, 0, sizeof (items));
	items[0].name = "Alpha";
	items[1].name = "Beta";
	items[2].name = "Zzz QuickMix";
	items[2].isQuickMix = true;

	BarSortStationSnapshots (items, 3, BAR_SORT_QUICKMIX_10_NAME_ZA);

	ck_assert (items[0].isQuickMix);
	ck_assert_str_eq (items[1].name, "Beta");
	ck_assert_str_eq (items[2].name, "Alpha");
}
END_TEST

START_TEST (test_sort_station_snapshots_null_names)
{
	BarStationSnapshot_t items[2];

	memset (items, 0, sizeof (items));
	items[0].name = NULL;
	items[1].name = "Alpha";

	BarSortStationSnapshots (items, 2, BAR_SORT_NAME_AZ);

	ck_assert_ptr_null (items[0].name);
	ck_assert_str_eq (items[1].name, "Alpha");
}
END_TEST

START_TEST (test_sort_station_snapshots_quickmix_01_tiebreak_name_az)
{
	BarStationSnapshot_t items[2];

	memset (items, 0, sizeof (items));
	items[0].name = "Beta";
	items[1].name = "Alpha";

	BarSortStationSnapshots (items, 2, BAR_SORT_QUICKMIX_01_NAME_AZ);

	ck_assert_str_eq (items[0].name, "Alpha");
	ck_assert_str_eq (items[1].name, "Beta");
}
END_TEST

START_TEST (test_sort_station_snapshots_quickmix_10_tiebreak_name_az)
{
	BarStationSnapshot_t items[2];

	memset (items, 0, sizeof (items));
	items[0].name = "Beta";
	items[1].name = "Alpha";

	BarSortStationSnapshots (items, 2, BAR_SORT_QUICKMIX_10_NAME_AZ);

	ck_assert_str_eq (items[0].name, "Alpha");
	ck_assert_str_eq (items[1].name, "Beta");
}
END_TEST

START_TEST (test_sort_station_snapshots_quickmix_01_both_quickmix_name_az)
{
	BarStationSnapshot_t items[2];

	memset (items, 0, sizeof (items));
	items[0].name = "Beta QuickMix";
	items[0].isQuickMix = true;
	items[1].name = "Alpha QuickMix";
	items[1].isQuickMix = true;

	BarSortStationSnapshots (items, 2, BAR_SORT_QUICKMIX_01_NAME_AZ);

	ck_assert (items[0].isQuickMix);
	ck_assert_str_eq (items[0].name, "Alpha QuickMix");
	ck_assert (items[1].isQuickMix);
	ck_assert_str_eq (items[1].name, "Beta QuickMix");
}
END_TEST

START_TEST (test_sort_station_snapshots_quickmix_10_both_quickmix_name_az)
{
	BarStationSnapshot_t items[2];

	memset (items, 0, sizeof (items));
	items[0].name = "Beta QuickMix";
	items[0].isQuickMix = true;
	items[1].name = "Alpha QuickMix";
	items[1].isQuickMix = true;

	BarSortStationSnapshots (items, 2, BAR_SORT_QUICKMIX_10_NAME_AZ);

	ck_assert (items[0].isQuickMix);
	ck_assert_str_eq (items[0].name, "Alpha QuickMix");
	ck_assert (items[1].isQuickMix);
	ck_assert_str_eq (items[1].name, "Beta QuickMix");
}
END_TEST

START_TEST (test_sorted_stations_null_name)
{
	PianoStation_t named, unnamed;
	PianoStation_t **sorted = NULL;
	size_t count = 0;

	memset (&named, 0, sizeof (named));
	memset (&unnamed, 0, sizeof (unnamed));
	named.name = "Alpha";
	unnamed.name = NULL;
	named.head.next = (PianoListHead_t *) &unnamed;

	sorted = BarSortedStations (&named, &count, BAR_SORT_NAME_AZ);
	ck_assert_ptr_nonnull (sorted);
	ck_assert_uint_eq (count, 2);
	ck_assert_ptr_null (sorted[0]->name);
	ck_assert_str_eq (sorted[1]->name, "Alpha");
	free (sorted);
}
END_TEST

static void *
mock_calloc_fail (size_t nmemb, size_t size)
{
	(void) nmemb;
	(void) size;
	return NULL;
}

START_TEST (test_sorted_stations_calloc_failure)
{
	PianoStation_t alpha;
	PianoStation_t **sorted = NULL;
	size_t count = 99;

	memset (&alpha, 0, sizeof (alpha));
	alpha.name = "Alpha";

	BarStationSortSetCallocTestHook (mock_calloc_fail);
	sorted = BarSortedStations (&alpha, &count, BAR_SORT_NAME_AZ);
	BarStationSortClearCallocTestHook ();

	ck_assert_ptr_null (sorted);
	ck_assert_uint_eq (count, 0);
}
END_TEST

START_TEST (test_sorted_stations_quickmix_01_name_az)
{
	PianoStation_t alpha, quickmix;
	PianoStation_t **sorted = NULL;
	size_t count = 0;

	memset (&alpha, 0, sizeof (alpha));
	memset (&quickmix, 0, sizeof (quickmix));
	quickmix.name = "Zzz QuickMix";
	quickmix.isQuickMix = 1;
	alpha.name = "Alpha";
	quickmix.head.next = (PianoListHead_t *) &alpha;

	sorted = BarSortedStations (&quickmix, &count, BAR_SORT_QUICKMIX_01_NAME_AZ);
	ck_assert_ptr_nonnull (sorted);
	ck_assert_uint_eq (count, 2);
	ck_assert_str_eq (sorted[0]->name, "Alpha");
	ck_assert (sorted[1]->isQuickMix);
	free (sorted);
}
END_TEST

START_TEST (test_sorted_stations_quickmix_01_name_za)
{
	PianoStation_t alpha, beta, quickmix;
	PianoStation_t **sorted = NULL;
	size_t count = 0;

	memset (&alpha, 0, sizeof (alpha));
	memset (&beta, 0, sizeof (beta));
	memset (&quickmix, 0, sizeof (quickmix));
	beta.name = "Beta";
	alpha.name = "Alpha";
	quickmix.name = "Zzz QuickMix";
	quickmix.isQuickMix = 1;
	beta.head.next = (PianoListHead_t *) &alpha;
	alpha.head.next = (PianoListHead_t *) &quickmix;

	sorted = BarSortedStations (&beta, &count, BAR_SORT_QUICKMIX_01_NAME_ZA);
	ck_assert_ptr_nonnull (sorted);
	ck_assert_uint_eq (count, 3);
	ck_assert_str_eq (sorted[0]->name, "Beta");
	ck_assert_str_eq (sorted[1]->name, "Alpha");
	ck_assert (sorted[2]->isQuickMix);
	free (sorted);
}
END_TEST

START_TEST (test_sorted_stations_quickmix_10_name_az)
{
	PianoStation_t alpha, quickmix;
	PianoStation_t **sorted = NULL;
	size_t count = 0;

	memset (&alpha, 0, sizeof (alpha));
	memset (&quickmix, 0, sizeof (quickmix));
	alpha.name = "Alpha";
	quickmix.name = "Zzz QuickMix";
	quickmix.isQuickMix = 1;
	alpha.head.next = (PianoListHead_t *) &quickmix;

	sorted = BarSortedStations (&alpha, &count, BAR_SORT_QUICKMIX_10_NAME_AZ);
	ck_assert_ptr_nonnull (sorted);
	ck_assert_uint_eq (count, 2);
	ck_assert (sorted[0]->isQuickMix);
	ck_assert_str_eq (sorted[0]->name, "Zzz QuickMix");
	ck_assert_str_eq (sorted[1]->name, "Alpha");
	free (sorted);
}
END_TEST

START_TEST (test_sorted_stations_quickmix_10_name_za)
{
	PianoStation_t alpha, beta, quickmix;
	PianoStation_t **sorted = NULL;
	size_t count = 0;

	memset (&alpha, 0, sizeof (alpha));
	memset (&beta, 0, sizeof (beta));
	memset (&quickmix, 0, sizeof (quickmix));
	alpha.name = "Alpha";
	beta.name = "Beta";
	quickmix.name = "Zzz QuickMix";
	quickmix.isQuickMix = 1;
	alpha.head.next = (PianoListHead_t *) &beta;
	beta.head.next = (PianoListHead_t *) &quickmix;

	sorted = BarSortedStations (&alpha, &count, BAR_SORT_QUICKMIX_10_NAME_ZA);
	ck_assert_ptr_nonnull (sorted);
	ck_assert_uint_eq (count, 3);
	ck_assert (sorted[0]->isQuickMix);
	ck_assert_str_eq (sorted[1]->name, "Beta");
	ck_assert_str_eq (sorted[2]->name, "Alpha");
	free (sorted);
}
END_TEST

Suite *station_sort_suite (void)
{
	Suite *s = suite_create ("station_sort");
	TCase *tc = tcase_create ("core");

	tcase_add_test (tc, test_sort_station_snapshots_noop_edge_cases);
	tcase_add_test (tc, test_sort_station_snapshots_name_az);
	tcase_add_test (tc, test_sort_station_snapshots_name_za);
	tcase_add_test (tc, test_sort_station_snapshots_quickmix_01_name_az);
	tcase_add_test (tc, test_sort_station_snapshots_quickmix_01_name_za);
	tcase_add_test (tc, test_sort_station_snapshots_quickmix_10_name_az);
	tcase_add_test (tc, test_sort_station_snapshots_quickmix_10_name_za);
	tcase_add_test (tc, test_sort_station_snapshots_null_names);
	tcase_add_test (tc, test_sort_station_snapshots_quickmix_01_tiebreak_name_az);
	tcase_add_test (tc, test_sort_station_snapshots_quickmix_10_tiebreak_name_az);
	tcase_add_test (tc, test_sort_station_snapshots_quickmix_01_both_quickmix_name_az);
	tcase_add_test (tc, test_sort_station_snapshots_quickmix_10_both_quickmix_name_az);
	tcase_add_test (tc, test_sorted_stations_null_name);
	tcase_add_test (tc, test_sorted_stations_calloc_failure);
	tcase_add_test (tc, test_sorted_stations_quickmix_01_name_az);
	tcase_add_test (tc, test_sorted_stations_quickmix_01_name_za);
	tcase_add_test (tc, test_sorted_stations_quickmix_10_name_az);
	tcase_add_test (tc, test_sorted_stations_quickmix_10_name_za);
	suite_add_tcase (s, tc);
	return s;
}
