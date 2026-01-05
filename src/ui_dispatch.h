/*
Copyright (c) 2010-2013
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

/* bit-mask */
typedef enum {
	BAR_DC_UNDEFINED = 0,
	BAR_DC_GLOBAL = 1, /* top-level action */
	BAR_DC_STATION = 2, /* station selected */
	BAR_DC_SONG = 4, /* song selected */
	BAR_DC_PANDORA_CONNECTED = 8, /* logged in to Pandora */
	BAR_DC_PANDORA_DISCONNECTED = 16, /* logged out from Pandora */
} BarUiDispatchContext_t;

#include "settings.h"
#include "main.h"

typedef void (*BarKeyShortcutFunc_t) (BarApp_t *, PianoStation_t *,
		PianoSong_t *, BarUiDispatchContext_t);

typedef struct {
	char defaultKey;
	BarUiDispatchContext_t context;
	BarKeyShortcutFunc_t function;
	const char * const helpText;
	const char * const configKey;
} BarUiDispatchAction_t;

#include "ui_act.h"

/* see settings.h */
static const BarUiDispatchAction_t dispatchActions[BAR_KS_COUNT] = {
		{'?', BAR_DC_UNDEFINED, BarUiActHelp, NULL, "act_help"},
		{'+', BAR_DC_SONG | BAR_DC_PANDORA_CONNECTED, BarUiActLoveSong, "love song",
				"act_songlove"},
		{'-', BAR_DC_SONG | BAR_DC_PANDORA_CONNECTED, BarUiActBanSong, "ban song", "act_songban"},
		{'a', BAR_DC_STATION | BAR_DC_PANDORA_CONNECTED, BarUiActAddMusic, "add music to station",
				"act_stationaddmusic"},
		{'c', BAR_DC_GLOBAL | BAR_DC_PANDORA_CONNECTED, BarUiActCreateStation, "create new station",
				"act_stationcreate"},
		{'d', BAR_DC_STATION | BAR_DC_PANDORA_CONNECTED, BarUiActDeleteStation, "delete station",
				"act_stationdelete"},
		{'e', BAR_DC_SONG | BAR_DC_PANDORA_CONNECTED, BarUiActExplain, "explain why this song is played",
				"act_songexplain"},
		{'g', BAR_DC_GLOBAL | BAR_DC_PANDORA_CONNECTED, BarUiActStationFromGenre, "add genre station",
				"act_stationaddbygenre"},
		{'h', BAR_DC_GLOBAL | BAR_DC_PANDORA_CONNECTED, BarUiActHistory, "song history", "act_history"},
		{'i', BAR_DC_GLOBAL | BAR_DC_STATION | BAR_DC_SONG | BAR_DC_PANDORA_CONNECTED, BarUiActSongInfo,
				"print information about song/station", "act_songinfo"},
		{'j', BAR_DC_GLOBAL | BAR_DC_PANDORA_CONNECTED, BarUiActAddSharedStation, "add shared station",
				"act_addshared"},
		{'n', BAR_DC_GLOBAL | BAR_DC_STATION | BAR_DC_PANDORA_CONNECTED, BarUiActSkipSong, "next song",
				"act_songnext"},
		{'p', BAR_DC_GLOBAL | BAR_DC_STATION | BAR_DC_PANDORA_CONNECTED, BarUiActTogglePause, "pause/resume playback",
				"act_songpausetoggle"},
		{'q', BAR_DC_GLOBAL, BarUiActQuit, "quit", "act_quit"},
		{'r', BAR_DC_STATION | BAR_DC_PANDORA_CONNECTED, BarUiActRenameStation, "rename station",
				"act_stationrename"},
		{'s', BAR_DC_GLOBAL | BAR_DC_PANDORA_CONNECTED, BarUiActSelectStation, "change station",
				"act_stationchange"},
		{'t', BAR_DC_SONG | BAR_DC_PANDORA_CONNECTED, BarUiActTempBanSong, "tired (ban song for 1 month)",
				"act_songtired"},
		{'u', BAR_DC_GLOBAL | BAR_DC_STATION | BAR_DC_PANDORA_CONNECTED, BarUiActPrintUpcoming,
				"upcoming songs", "act_upcoming"},
		{'x', BAR_DC_STATION | BAR_DC_PANDORA_CONNECTED, BarUiActSelectQuickMix, "select quickmix stations",
				"act_stationselectquickmix"},
		{'$', BAR_DC_SONG | BAR_DC_PANDORA_CONNECTED, BarUiActDebug, NULL, "act_debug"},
		{'b', BAR_DC_SONG | BAR_DC_PANDORA_CONNECTED, BarUiActBookmark, "bookmark song/artist",
				"act_bookmark"},
		{'(', BAR_DC_GLOBAL, BarUiActVolDown, "decrease volume",
				"act_voldown"},
		{')', BAR_DC_GLOBAL, BarUiActVolUp, "increase volume",
				"act_volup"},
		{'=', BAR_DC_STATION | BAR_DC_PANDORA_CONNECTED, BarUiActManageStation, "manage station seeds/feedback/mode",
				"act_managestation"},
		{' ', BAR_DC_GLOBAL | BAR_DC_STATION | BAR_DC_PANDORA_CONNECTED, BarUiActTogglePause, NULL,
				"act_songpausetoggle2"},
		{'v', BAR_DC_SONG | BAR_DC_PANDORA_CONNECTED, BarUiActCreateStationFromSong,
				"create new station from song or artist", "act_stationcreatefromsong"},
		{'P', BAR_DC_GLOBAL | BAR_DC_STATION | BAR_DC_PANDORA_CONNECTED, BarUiActPlay, "resume playback",
				"act_songplay"},
		{'S', BAR_DC_GLOBAL | BAR_DC_STATION | BAR_DC_PANDORA_CONNECTED, BarUiActPause, "pause playback",
				"act_songpause"},
		{'^', BAR_DC_GLOBAL, BarUiActVolReset, "reset volume",
				"act_volreset"},
		{'!', BAR_DC_GLOBAL | BAR_DC_PANDORA_CONNECTED, BarUiActSettings, "change settings",
				"act_settings"},
		{'D', BAR_DC_GLOBAL | BAR_DC_PANDORA_CONNECTED, BarUiActPandoraDisconnect, "disconnect from Pandora",
				"act_pandoradisconnect"},
		{'R', BAR_DC_GLOBAL | BAR_DC_PANDORA_DISCONNECTED, BarUiActPandoraReconnect, "reconnect to Pandora",
				"act_pandorareconnect"},
		};

#include <piano.h>
#include <stdbool.h>
#include <stdio.h>

BarKeyShortcutId_t BarUiDispatch (BarApp_t *, const char, PianoStation_t *, PianoSong_t *,
		const bool, BarUiDispatchContext_t);

BarKeyShortcutId_t BarUiDispatchById (BarApp_t *, BarKeyShortcutId_t, PianoStation_t *, 
		PianoSong_t *, const bool, BarUiDispatchContext_t);

const BarUiDispatchAction_t *BarUiDispatchLookupById(BarKeyShortcutId_t id);

