/*
Copyright (c) 2008-2011
	Lars-Dominik Braun <lars@6xq.net>

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

#include "main.h"
#include <piano.h>

/* Initialize/destroy state mutex */
void BarStateInit(BarApp_t *app);
void BarStateDestroy(BarApp_t *app);

/* Station access (thread-safe) */
PianoStation_t *BarStateGetNextStation(const BarApp_t *app);
void BarStateSetNextStation(BarApp_t *app, PianoStation_t *station);
PianoStation_t *BarStateGetCurrentStation(const BarApp_t *app);
void BarStateSetCurrentStation(BarApp_t *app, PianoStation_t *station);
PianoStation_t *BarStateFindStationById(const BarApp_t *app, const char *id);
PianoStation_t *BarStateGetStationList(const BarApp_t *app);

/* Playlist access (thread-safe) */
PianoSong_t *BarStateGetPlaylist(const BarApp_t *app);
void BarStateSetPlaylist(BarApp_t *app, PianoSong_t *playlist);
void BarStateDrainPlaylist(BarApp_t *app);
void BarStateSwitchStation(BarApp_t *app, PianoStation_t *station);

/* Player state access (thread-safe) */
BarPlayerMode BarStateGetPlayerMode(const BarApp_t *app);
void BarStateGetPlayerTime(const BarApp_t *app, unsigned int *played, unsigned int *duration);
bool BarStateGetPlayerPaused(const BarApp_t *app);

/* Pandora API calls (thread-safe) */
bool BarStateCallPandora(BarApp_t *app, PianoRequestType_t type,
                         void *data, PianoReturn_t *pRet, CURLcode *wRet);

