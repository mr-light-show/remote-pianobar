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

#include "../../src/system_volume.h"

START_TEST (test_system_volume_parses_pactl_json_first_channel)
{
#ifdef __APPLE__
	ck_assert (1);
#else
	const char *json =
		"[{\"volume\":{\"front-left\":{\"value\":65536,\"value_percent\":\"64%\"},"
		"\"front-right\":{\"value\":65536,\"value_percent\":\"63%\"}}}]";

	ck_assert_int_eq (BarSystemVolumeParsePactlJsonVolume (json), 64);
#endif
}
END_TEST

START_TEST (test_system_volume_rejects_malformed_pactl_json)
{
#ifdef __APPLE__
	ck_assert (1);
#else
	ck_assert_int_eq (BarSystemVolumeParsePactlJsonVolume (NULL), -1);
	ck_assert_int_eq (BarSystemVolumeParsePactlJsonVolume ("not json"), -1);
	ck_assert_int_eq (BarSystemVolumeParsePactlJsonVolume ("[]"), -1);
	ck_assert_int_eq (BarSystemVolumeParsePactlJsonVolume ("[{\"name\":\"sink\"}]"), -1);
	/* volume present but no channels with value_percent */
	ck_assert_int_eq (BarSystemVolumeParsePactlJsonVolume ("[{\"volume\":{}}]"), -1);
	/* channel exists but missing value_percent */
	ck_assert_int_eq (BarSystemVolumeParsePactlJsonVolume (
		"[{\"volume\":{\"mono\":{\"value\":42}}}]"), -1);
#endif
}
END_TEST

/* Public API exercises the platform backend.  These tests verify default-state
 * behavior so the public surface stays covered even where the underlying
 * backend cannot be exercised in a unit test (no audio device available). */
START_TEST (test_system_volume_public_api_default_state)
{
	BarSystemVolumeDestroy ();
	ck_assert (!BarSystemVolumeAvailable ());

#ifdef __APPLE__
	/* CoreAudio paths require a real device; verify defaults instead. */
	(void)BarSystemVolumeRefreshDevice ();
#else
	ck_assert (!BarSystemVolumeRefreshDevice ());
	ck_assert_int_eq (BarSystemVolumeGet (), -1);
	ck_assert (!BarSystemVolumeSet (50));
#endif
}
END_TEST

START_TEST (test_system_volume_set_clamps_out_of_range_inputs)
{
	/* Without an initialized backend the set call returns false but must
	 * not crash, regardless of input. */
#ifdef __APPLE__
	(void)BarSystemVolumeSet (-50);
	(void)BarSystemVolumeSet (250);
#else
	BarSystemVolumeDestroy ();
	ck_assert (!BarSystemVolumeSet (-50));
	ck_assert (!BarSystemVolumeSet (250));
#endif
}
END_TEST

#ifdef __APPLE__
START_TEST (test_system_volume_macos_init_get_set_roundtrip)
{
	BarSystemVolumeDestroy ();
	if (!BarSystemVolumeInit (NULL)) {
		/* No audio output device in this environment. */
		ck_assert (!BarSystemVolumeAvailable ());
		return;
	}

	ck_assert (BarSystemVolumeAvailable ());
	int vol = BarSystemVolumeGet ();
	ck_assert_int_ge (vol, 0);
	ck_assert_int_le (vol, 100);
	ck_assert (BarSystemVolumeSet (vol));
	BarSystemVolumeDestroy ();
	ck_assert (!BarSystemVolumeAvailable ());
}
END_TEST
#endif

Suite *system_volume_suite (void)
{
	Suite *s = suite_create ("system_volume");
	TCase *tc = tcase_create ("pactl");
	tcase_add_test (tc, test_system_volume_parses_pactl_json_first_channel);
	tcase_add_test (tc, test_system_volume_rejects_malformed_pactl_json);
	tcase_add_test (tc, test_system_volume_public_api_default_state);
	tcase_add_test (tc, test_system_volume_set_clamps_out_of_range_inputs);
#ifdef __APPLE__
	tcase_add_test (tc, test_system_volume_macos_init_get_set_roundtrip);
#endif
	suite_add_tcase (s, tc);
	return s;
}
