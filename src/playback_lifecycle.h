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

#pragma once
#include <stdbool.h>
#include <pthread.h>
#include "main.h"

/*
 * Fetch the next playlist for the current station from Pandora.
 * Returns true when a playable playlist is ready.
 * Returns false when the station was cleared, an internal error occurred,
 * or no tracks were returned.
 */
bool BarPlaybackFetchPlaylist (BarApp_t *app);

/*
 * Start a player thread for the first song in app->playlist.
 * Returns true after the thread is created successfully.
 * Returns false for: null app, null thread, missing playlist,
 * missing station, invalid URL, or pthread_create failure.
 */
bool BarPlaybackStartSong (BarApp_t *app, pthread_t *playerThread);
