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
#include <pthread.h>

#include "../../src/playback_lifecycle.h"

/* BarPlaybackStartSong: null app must return false without crashing */
START_TEST (test_playback_start_rejects_null_app)
{
	pthread_t t = 0;
	ck_assert (!BarPlaybackStartSong (NULL, &t));
}
END_TEST

/* BarPlaybackStartSong: null playerThread must return false without crashing */
START_TEST (test_playback_start_rejects_null_thread)
{
	BarApp_t app;
	memset (&app, 0, sizeof (app));
	ck_assert (!BarPlaybackStartSong (&app, NULL));
}
END_TEST

/* BarPlaybackStartSong: app with no playlist must return false without crashing */
START_TEST (test_playback_start_rejects_null_playlist)
{
	BarApp_t app;
	pthread_t playerThread = 0;
	memset (&app, 0, sizeof (app));

	/* playlist is NULL after memset — must return false cleanly */
	ck_assert (!BarPlaybackStartSong (&app, &playerThread));
}
END_TEST

Suite *playback_lifecycle_suite (void) {
	Suite *s = suite_create ("playback_lifecycle");
	TCase *tc = tcase_create ("core");
	tcase_add_test (tc, test_playback_start_rejects_null_app);
	tcase_add_test (tc, test_playback_start_rejects_null_thread);
	tcase_add_test (tc, test_playback_start_rejects_null_playlist);
	suite_add_tcase (s, tc);
	return s;
}
