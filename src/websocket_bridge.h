/*
Copyright (c) 2025
	Remote Pianobar Contributors

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

/* WebSocket bridge functions
 * These functions are no-ops when WEBSOCKET_ENABLED is not defined
 */

/* Mode predicates - return false when WebSocket is disabled */
bool BarIsWebOnlyMode(const BarApp_t *app);
bool BarShouldSkipCliOutput(const BarApp_t *app);

/* Silent mode - suppresses CLI output during WebSocket action dispatch */
void BarWsSetSilentMode(bool silent);
bool BarWsIsSilentMode(void);

/* Event broadcasts */
void BarWsBroadcastVolume(BarApp_t *app);
void BarWsBroadcastExplanation(BarApp_t *app, const char *explanation);
void BarWsBroadcastUpcoming(BarApp_t *app, PianoSong_t *songs, int count);
void BarWsBroadcastSongStart(BarApp_t *app);
void BarWsBroadcastSongStop(BarApp_t *app);
void BarWsBroadcastProgress(BarApp_t *app);
void BarWsBroadcastPlayState(BarApp_t *app);
void BarWsBroadcastStations(BarApp_t *app);
void BarWsDisconnectAllClients(BarApp_t *app);

/* Lifecycle management */
bool BarWsInit(BarApp_t *app);
void BarWsDestroy(BarApp_t *app);
bool BarWsDaemonize(BarApp_t *app);
void BarWsRemovePidFile(BarApp_t *app);

/* Command processing */
void BarWsProcessCommands(BarApp_t *app);

