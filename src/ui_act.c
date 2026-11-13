/*
Copyright (c) 2008-2018
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

/* functions responding to user's keystrokes */

#include "config.h"
#include "bar_constants.h"

#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#include "ui.h"
#include "ui_readline.h"
#include "ui_dispatch.h"
#include "websocket_bridge.h"
#include "bar_state.h"
#include "log.h"
#include "system_volume.h"
#include "station_display.h"
#include "playback_manager.h"

#include "websocket/protocol/socketio.h"

/*	standard eventcmd call. BarUiStartEventCmd releases the caller's
 *	pianoHttpMutex hold while it waits for the script, so the selection is
 *	resolved again afterwards (NULL when it went away meanwhile).
 */
#define BarUiActDefaultEventcmd(name) BarUiActEventcmd (app, name, \
		&selStation, &selSong, pRet, wRet)

static PianoSong_t *BarUiActFindSongByToken (BarApp_t *app, const char *trackToken);

/*	standard piano call
 */
#define BarUiActDefaultPianoCall(call, arg) BarUiPianoCall (app, \
		call, arg, &pRet, &wRet)

/*	helper to _really_ skip a song (unlock mutex, quit player)
 *	@param player handle
 */
static inline void BarUiDoSkipSong (player_t * const player) {
	assert (player != NULL);
	BarPlayerRequestStop (player);
}

/* Feedback mode for transform: UI (BarUiMsg + BarUiPianoCall) vs log (log_write + BarUiPianoCallLogged) */
typedef enum { TRANSFORM_FEEDBACK_UI, TRANSFORM_FEEDBACK_LOGGED } BarTransformFeedback_t;

/*	Transform station if shared; report progress via UI or log depending on feedback.
 *	@return 0 = error, 1 = success
 */
static bool BarTransformIfSharedWithFeedback (BarApp_t *app, PianoStation_t *station,
		BarTransformFeedback_t feedback) {
	PianoReturn_t pRet;
	CURLcode wRet;

	assert (station != NULL);

	if (!station->isCreator) {
		if (feedback == TRANSFORM_FEEDBACK_UI) {
			BarUiMsg (&app->settings, MSG_INFO, "Transforming station... ");
			if (!BarUiPianoCall (app, PIANO_REQUEST_TRANSFORM_STATION, station,
					&pRet, &wRet))
				return false;
		} else {
			log_write(DEBUG_WEBSOCKET, "Transforming shared station...\n");
			if (!BarUiPianoCallLogged (app, PIANO_REQUEST_TRANSFORM_STATION, station,
					"Transforming station", &pRet, &wRet))
				return false;
		}
	}
	return true;
}

/*	Transform station if necessary to allow changes like rename, rate, ...
 *	@return false on error, true on success
 */
bool BarTransformIfShared (BarApp_t *app, PianoStation_t *station) {
	return BarTransformIfSharedWithFeedback (app, station, TRANSFORM_FEEDBACK_UI);
}

/*	Logged version of BarTransformIfShared for WebSocket thread
 *	@return false on error, true on success
 */
bool BarWsTransformIfShared (BarApp_t *app, PianoStation_t *station) {
	return BarTransformIfSharedWithFeedback (app, station, TRANSFORM_FEEDBACK_LOGGED);
}

/*	Waiting for terminal input inside a dispatched action.
 *
 *	Actions that use the selected station/song run with pianoHttpMutex held
 *	(BarUiDispatchHoldsPianoLock), which keeps stations and songs alive. That
 *	hold must never be kept while the user types, so the selection is
 *	remembered by ID, the hold is released for the wait, and the selection is
 *	resolved again afterwards. It is NULL when it went away meanwhile.
 */
typedef struct {
	char *stationId;
	char *trackToken;
	unsigned int holds;
} BarUiActInputWait_t;

/*	Find a song the user may have selected: the playing song or one in the
 *	history. Caller holds pianoHttpMutex (both lists are freed under it).
 */
static PianoSong_t *BarUiActFindSongByToken (BarApp_t *app, const char *trackToken) {
	if (trackToken == NULL) {
		return NULL;
	}
	PianoSong_t *song = BarStateGetPlaylist (app);
	if (song != NULL && song->trackToken != NULL &&
			strcmp (song->trackToken, trackToken) == 0) {
		return song;
	}
	song = app->songHistory;
	PianoListForeachP (song) {
		if (song->trackToken != NULL && strcmp (song->trackToken, trackToken) == 0) {
			return song;
		}
	}
	return NULL;
}

static void BarUiActInputBegin (BarApp_t *app, BarUiActInputWait_t *wait,
		const PianoStation_t *station, const PianoSong_t *song) {
	wait->stationId = (station != NULL && station->id != NULL) ?
			strdup (station->id) : NULL;
	wait->trackToken = (song != NULL && song->trackToken != NULL) ?
			strdup (song->trackToken) : NULL;
	wait->holds = BarUiPianoSuspend (app);
}

static void BarUiActInputEnd (BarApp_t *app, BarUiActInputWait_t *wait,
		PianoStation_t **station, PianoSong_t **song) {
	BarUiPianoResume (app, wait->holds);
	if (station != NULL) {
		*station = wait->stationId != NULL ?
				BarStateFindStationById (app, wait->stationId) : NULL;
	}
	if (song != NULL) {
		*song = BarUiActFindSongByToken (app, wait->trackToken);
	}
	free (wait->stationId);
	free (wait->trackToken);
	wait->stationId = NULL;
	wait->trackToken = NULL;
}

/*	The selection went away (deleted, disconnected) during an input wait
 */
static void BarUiActSelectionGone (BarApp_t *app, bool station) {
	BarUiMsg (&app->settings, MSG_ERR, "%s", BarL10nGet (&app->l10n,
			station ? "cli.station_gone" : "cli.song_gone"));
}

/*	Run eventcmd for an action and resolve its selection again: the script is
 *	waited for without the hold (BarUiStartEventCmd), so the station and song
 *	may be gone when it returns.
 */
static void BarUiActEventcmd (BarApp_t *app, const char *type,
		PianoStation_t **station, PianoSong_t **song, PianoReturn_t pRet,
		CURLcode wRet) {
	if (app->settings.eventCmd == NULL) {
		return;
	}
	char *stationId = (*station != NULL && (*station)->id != NULL) ?
			strdup ((*station)->id) : NULL;
	char *trackToken = (*song != NULL && (*song)->trackToken != NULL) ?
			strdup ((*song)->trackToken) : NULL;
	BarUiStartEventCmd (&app->settings, type, *station, *song, &app->player,
			BarStateGetStationList (app), pRet, wRet);
	*station = stationId != NULL ? BarStateFindStationById (app, stationId) : NULL;
	*song = BarUiActFindSongByToken (app, trackToken);
	free (stationId);
	free (trackToken);
}

/*	print current shortcut configuration
 */
BarUiActCallback(BarUiActHelp) {
	BarUiMsg (&app->settings, MSG_NONE, "\r");
	for (size_t i = 0; i < BAR_KS_COUNT; i++) {
		if (dispatchActions[i].helpText != NULL &&
				(context & dispatchActions[i].context) == dispatchActions[i].context &&
				app->settings.keys[i] != BAR_KS_DISABLED) {
			BarUiMsg (&app->settings, MSG_LIST, "%c    %s\n", app->settings.keys[i],
					dispatchActions[i].helpText);
		}
	}
}

/*	add more music to current station
 */
BarUiActCallback(BarUiActAddMusic) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataAddSeed_t reqData;

	assert (selStation != NULL);

	BarUiActInputWait_t wait;
	BarUiActInputBegin (app, &wait, selStation, selSong);
	reqData.musicId = BarUiSelectMusicId (app, NULL,
			"Add artist or title to station: ");
	BarUiActInputEnd (app, &wait, &selStation, &selSong);
	if (reqData.musicId != NULL) {
		if (selStation == NULL) {
			BarUiActSelectionGone (app, true);
			free (reqData.musicId);
			return;
		}
		if (!BarTransformIfShared (app, selStation)) {
			return;
		}
		reqData.station = selStation;

		BarUiMsg (&app->settings, MSG_INFO, "Adding music to station... ");
		BarUiActDefaultPianoCall (PIANO_REQUEST_ADD_SEED, &reqData);

		free (reqData.musicId);

		BarUiActDefaultEventcmd ("stationaddmusic");
	}
}

/*	ban song
 */
BarUiActCallback(BarUiActBanSong) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoStation_t *realStation;

	assert (selStation != NULL);
	assert (selSong != NULL);
	assert (selSong->stationId != NULL);

	if ((realStation = BarStateFindStationById(app, selSong->stationId)) == NULL) {
		assert (0);
		return;
	}
	if (!BarTransformIfShared (app, realStation)) {
		return;
	}

	PianoRequestDataRateSong_t reqData;
	reqData.song = selSong;
	reqData.rating = PIANO_RATE_BAN;

	BarUiMsg (&app->settings, MSG_INFO, "Banning song... ");
	if (BarUiActDefaultPianoCall (PIANO_REQUEST_RATE_SONG, &reqData)) {
		/* WebSocket: full `process` (not `start`) so clients get song.rating without a new track */
		BarWsBroadcastProcess(app);
		if (selSong == BarStateGetPlaylist(app)) {
			BarUiDoSkipSong (&app->player);
		}
	}
	BarUiActDefaultEventcmd ("songban");
}

/*	create new station
 */
BarUiActCallback(BarUiActCreateStation) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataCreateStation_t reqData;

	reqData.type = PIANO_MUSICTYPE_INVALID;
	BarUiActInputWait_t wait;
	BarUiActInputBegin (app, &wait, selStation, selSong);
	reqData.token = BarUiSelectMusicId (app, NULL,
			"Create station from artist or title: ");
	BarUiActInputEnd (app, &wait, &selStation, &selSong);
	if (reqData.token != NULL) {
		BarUiMsg (&app->settings, MSG_INFO, "Creating station... ");
		if (BarUiActDefaultPianoCall (PIANO_REQUEST_CREATE_STATION, &reqData)) {
			/* Update display names for newly created station */
			BarUpdateStationDisplayNames(app);
			/* Notify WebSocket clients of station list change */
			BarWsBroadcastStations(app);
		}
		free (reqData.token);
		BarUiActDefaultEventcmd ("stationcreate");
	}
}

/*	create new station
 */
BarUiActCallback(BarUiActCreateStationFromSong) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataCreateStation_t reqData;
	char selectBuf[2];

	reqData.type = PIANO_MUSICTYPE_INVALID;

	BarUiMsg (&app->settings, MSG_QUESTION, "Create station from [s]ong or [a]rtist? ");
	BarUiActInputWait_t wait;
	BarUiActInputBegin (app, &wait, selStation, selSong);
	BarReadline (selectBuf, sizeof (selectBuf), "sa", &app->input,
			BAR_RL_FULLRETURN, -1);
	BarUiActInputEnd (app, &wait, &selStation, &selSong);
	if (selSong == NULL) {
		BarUiActSelectionGone (app, false);
		return;
	}
	reqData.token = selSong->trackToken;
	switch (selectBuf[0]) {
		case 's':
			reqData.type = PIANO_MUSICTYPE_SONG;
			break;

		case 'a':
			reqData.type = PIANO_MUSICTYPE_ARTIST;
			break;
	}
	if (reqData.type != PIANO_MUSICTYPE_INVALID) {
		BarUiMsg (&app->settings, MSG_INFO, "Creating station... ");
		if (BarUiActDefaultPianoCall (PIANO_REQUEST_CREATE_STATION, &reqData)) {
			/* Update display names for newly created station */
			BarUpdateStationDisplayNames(app);
			/* Notify WebSocket clients of station list change */
			BarWsBroadcastStations(app);
		}
		BarUiActDefaultEventcmd ("stationcreate");
	}
}

/*	add shared station by id
 */
BarUiActCallback(BarUiActAddSharedStation) {
	PianoReturn_t pRet;
	CURLcode wRet;
	char stationId[BAR_STATION_ID_MAX];
	PianoRequestDataCreateStation_t reqData;

	reqData.token = stationId;
	reqData.type = PIANO_MUSICTYPE_INVALID;

	BarUiMsg (&app->settings, MSG_QUESTION, "Station id: ");
	BarUiActInputWait_t wait;
	BarUiActInputBegin (app, &wait, selStation, selSong);
	const size_t idLen = BarReadline (stationId, sizeof (stationId), "0123456789",
			&app->input, BAR_RL_DEFAULT, -1);
	BarUiActInputEnd (app, &wait, &selStation, &selSong);
	if (idLen > 0) {
		BarUiMsg (&app->settings, MSG_INFO, "Adding shared station... ");
		if (BarUiActDefaultPianoCall (PIANO_REQUEST_CREATE_STATION, &reqData)) {
			/* Update display names for newly created station */
			BarUpdateStationDisplayNames(app);
			/* Notify WebSocket clients of station list change */
			BarWsBroadcastStations(app);
		}
		BarUiActDefaultEventcmd ("stationaddshared");
	}
}

static void drainPlaylist (BarApp_t * const app) {
	BarUiDoSkipSong (&app->player);
	BarStateTruncatePlaylistTail(app);
}

/*	Switch to a station (for WebSocket/programmatic use)
 */
void BarUiSwitchStation (BarApp_t * const app, PianoStation_t * const station) {
	assert (app != NULL);
	assert (station != NULL);
	
	drainPlaylist (app);
	BarStatePrepareStationSwitch(app, station);
}

/*	Switch to a station by id without exposing station pointers across the
 *	state lock boundary (for WebSocket/programmatic use).
 */
bool BarUiSwitchStationById (BarApp_t * const app, const char *stationId) {
	assert (app != NULL);

	if (stationId == NULL) {
		return false;
	}

	if (!BarStatePrepareStationSwitchById(app, stationId)) {
		return false;
	}

	BarUiDoSkipSong (&app->player);
	return true;
}

/*	delete current station
 */
BarUiActCallback(BarUiActDeleteStation) {
	PianoReturn_t pRet;
	CURLcode wRet;

	assert (selStation != NULL);

	BarUiMsg (&app->settings, MSG_QUESTION, "Really delete \"%s\"? [yN] ",
			selStation->name);
	BarUiActInputWait_t wait;
	BarUiActInputBegin (app, &wait, selStation, selSong);
	const bool confirmed = BarReadlineYesNo (false, &app->input);
	BarUiActInputEnd (app, &wait, &selStation, &selSong);
	if (confirmed && selStation == NULL) {
		BarUiActSelectionGone (app, true);
	} else if (confirmed) {
		BarUiMsg (&app->settings, MSG_INFO, "Deleting station... ");
		/* The response clears current/next when they point at the deleted
		 * station (BarStatePianoResponse), so compare before the call. */
		const bool wasCurrent = selStation == BarStateGetCurrentStation(app);
		if (BarUiActDefaultPianoCall (PIANO_REQUEST_DELETE_STATION, selStation)) {
			/* DELETE_STATION destroys the station struct */
			selStation = NULL;
			/* Notify WebSocket clients of station list change */
			BarWsBroadcastStations(app);
			if (wasCurrent) {
				drainPlaylist (app);
			}
		}
		BarUiActDefaultEventcmd ("stationdelete");
	}
}

/*	explain pandora's song choice (terminal)
 */
BarUiActCallback(BarUiActExplain) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataExplain_t reqData;

	assert (selSong != NULL);

	reqData.song = selSong;
	reqData.retExplain = NULL; /* Initialize to NULL to avoid freeing garbage */

	BarUiMsg (&app->settings, MSG_INFO, "Receiving explanation... ");
	if (BarUiActDefaultPianoCall(PIANO_REQUEST_EXPLAIN, &reqData)) {
		if (reqData.retExplain == NULL) {
			BarUiMsg (&app->settings, MSG_ERR, "No explanation provided.\n");
		} else {
			BarUiMsg (&app->settings, MSG_INFO, "%s\n", reqData.retExplain);
			free (reqData.retExplain);
		}
	}
	BarUiActDefaultEventcmd ("songexplain");
}

/*	explain pandora's song choice to the requesting web client only
 */
void BarUiActExplainTo (BarApp_t *app, const PianoStation_t *selStation,
		PianoSong_t *selSong, BarWsConnId_t requester) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataExplain_t reqData;

	assert (selSong != NULL);

	reqData.song = selSong;
	reqData.retExplain = NULL; /* Initialize to NULL to avoid freeing garbage */

	if (!BarUiPianoCallLogged(app, PIANO_REQUEST_EXPLAIN, &reqData,
			"Receiving explanation", &pRet, &wRet)) {
		BarWsEmitErrorTo(app, requester, "song.explain", "Failed to receive explanation");
	} else if (reqData.retExplain == NULL) {
		BarWsEmitErrorTo(app, requester, "song.explain", "No explanation provided by Pandora");
	} else {
		BarWsEmitExplanationTo(app, requester, reqData.retExplain);
		free (reqData.retExplain);
	}
	BarUiStartEventCmd (&app->settings, "songexplain", selStation, selSong,
			&app->player, BarStateGetStationList (app), pRet, wRet);
}

/*	choose genre station and add it as shared station
 */
BarUiActCallback(BarUiActStationFromGenre) {
	PianoReturn_t pRet;
	CURLcode wRet;
	const PianoGenreCategory_t *curCat;
	const PianoGenre_t *curGenre;
	int i;

	/* receive genre stations list if not yet available */
	if (app->ph.genreStations == NULL) {
		BarUiMsg (&app->settings, MSG_INFO, "Receiving genre stations... ");
		const bool ret = BarUiActDefaultPianoCall (
				PIANO_REQUEST_GET_GENRE_STATIONS, NULL);
		BarUiActDefaultEventcmd ("stationfetchgenre");
		if (!ret) {
			return;
		}
	}

	/* print all available categories */
	curCat = app->ph.genreStations;
	i = 0;
	PianoListForeachP (curCat) {
		BarUiMsg (&app->settings, MSG_LIST, "%2i) %s\n", i, curCat->name);
		i++;
	}

	/* The genre list is freed by a session reset (inside pianoHttpMutex):
	 * re-resolve by index after every input wait. */
	BarUiActInputWait_t wait;
	int catIndex;
	do {
		/* select category or exit */
		BarUiMsg (&app->settings, MSG_QUESTION, "Select category: ");
		BarUiActInputBegin (app, &wait, selStation, selSong);
		const size_t len = BarReadlineInt (&catIndex, &app->input);
		BarUiActInputEnd (app, &wait, &selStation, &selSong);
		if (len == 0) {
			return;
		}
		curCat = PianoListGetP (app->ph.genreStations, catIndex);
	} while (curCat == NULL);

	/* print all available stations */
	i = 0;
	curGenre = curCat->genres;
	PianoListForeachP (curGenre) {
		BarUiMsg (&app->settings, MSG_LIST, "%2i) %s\n", i, curGenre->name);
		i++;
	}

	do {
		BarUiMsg (&app->settings, MSG_QUESTION, "Select genre: ");
		BarUiActInputBegin (app, &wait, selStation, selSong);
		const size_t len = BarReadlineInt (&i, &app->input);
		BarUiActInputEnd (app, &wait, &selStation, &selSong);
		if (len == 0) {
			return;
		}
		curCat = PianoListGetP (app->ph.genreStations, catIndex);
		if (curCat == NULL) {
			return;
		}
		curGenre = PianoListGetP (curCat->genres, i);
	} while (curGenre == NULL);

	/* create station */
	PianoRequestDataCreateStation_t reqData;
	reqData.token = curGenre->musicId;
	reqData.type = PIANO_MUSICTYPE_INVALID;
	BarUiMsg (&app->settings, MSG_INFO, "Adding genre station \"%s\"... ",
			curGenre->name);
	BarUiActDefaultPianoCall (PIANO_REQUEST_CREATE_STATION, &reqData);
	BarUiActDefaultEventcmd ("stationaddgenre");
}

/*	print verbose song information
 */
BarUiActCallback(BarUiActSongInfo) {
	assert (selStation != NULL);
	assert (selSong != NULL);

	BarUiPrintStation (&app->settings, selStation);
	/* print real station if quickmix */
	BarUiPrintSong (&app->settings, selSong,
			selStation->isQuickMix ?
			BarStateFindStationById(app, selSong->stationId) :
			NULL);
}

/*	print some debugging information
 */
BarUiActCallback(BarUiActDebug) {
	assert (selSong != NULL);

	/* print debug-alike infos */
	BarUiMsg (&app->settings, MSG_NONE,
			"album:\t%s\n"
			"artist:\t%s\n"
			"audioFormat:\t%i\n"
			"audioUrl:\t%s\n"
			"coverArt:\t%s\n"
			"detailUrl:\t%s\n"
			"fileGain:\t%f\n"
			"musicId:\t%s\n"
			"rating:\t%i\n"
			"stationId:\t%s\n"
			"title:\t%s\n"
			"trackToken:\t%s\n",
			selSong->album,
			selSong->artist,
			selSong->audioFormat,
			selSong->audioUrl,
			selSong->coverArt,
			selSong->detailUrl,
			selSong->fileGain,
			selSong->musicId,
			selSong->rating,
			selSong->stationId,
			selSong->title,
			selSong->trackToken);
}

/*	rate current song
 */
BarUiActCallback(BarUiActLoveSong) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoStation_t *realStation;

	assert (selStation != NULL);
	assert (selSong != NULL);
	assert (selSong->stationId != NULL);

	if ((realStation = BarStateFindStationById(app, selSong->stationId)) == NULL) {
		assert (0);
		return;
	}
	if (!BarTransformIfShared (app, realStation)) {
		return;
	}

	PianoRequestDataRateSong_t reqData;
	reqData.song = selSong;
	reqData.rating = PIANO_RATE_LOVE;

	BarUiMsg (&app->settings, MSG_INFO, "Loving song... ");
	if (BarUiActDefaultPianoCall (PIANO_REQUEST_RATE_SONG, &reqData)) {
		/* WebSocket: full `process` (not `start`) so clients get song.rating without a new track */
		BarWsBroadcastProcess(app);
	}
	BarUiActDefaultEventcmd ("songlove");
}

/*	skip song
 */
BarUiActCallback(BarUiActSkipSong) {
	BarUiDoSkipSong (&app->player);
}

/*	play
 */
BarUiActCallback(BarUiActPlay) {
	BarPlayerPlayStateSnapshot snapshot;
	if (BarPlayerSetPausedWithSnapshot (&app->player, false, &snapshot)) {
		BarWsBroadcastPlayStateSnapshot (app, &snapshot);
	}
}

/*	pause
 */
BarUiActCallback(BarUiActPause) {
	BarPlayerPlayStateSnapshot snapshot;
	if (BarPlayerSetPausedWithSnapshot (&app->player, true, &snapshot)) {
		BarWsBroadcastPlayStateSnapshot (app, &snapshot);
	}
}

/*	toggle pause
 */
BarUiActCallback(BarUiActTogglePause) {
	BarPlayerPlayStateSnapshot snapshot;
	if (BarPlayerTogglePausedWithSnapshot (&app->player, &snapshot)) {
		BarWsBroadcastPlayStateSnapshot (app, &snapshot);
	}
}

/*	rename current station
 */
BarUiActCallback(BarUiActRenameStation) {
	PianoReturn_t pRet;
	CURLcode wRet;
	char lineBuf[BAR_INPUT_MAX];

	assert (selStation != NULL);

	BarUiMsg (&app->settings, MSG_QUESTION, "New name: ");
	BarUiActInputWait_t wait;
	BarUiActInputBegin (app, &wait, selStation, selSong);
	const size_t len = BarReadlineStr (lineBuf, sizeof (lineBuf), &app->input,
			BAR_RL_DEFAULT);
	BarUiActInputEnd (app, &wait, &selStation, &selSong);
	if (len > 0 && selStation == NULL) {
		BarUiActSelectionGone (app, true);
	} else if (len > 0) {
		PianoRequestDataRenameStation_t reqData;
		if (!BarTransformIfShared (app, selStation)) {
			return;
		}

		reqData.station = selStation;
		reqData.newName = lineBuf;

		BarUiMsg (&app->settings, MSG_INFO, "Renaming station... ");
		if (BarUiActDefaultPianoCall (PIANO_REQUEST_RENAME_STATION, &reqData)) {
			/* Update display names after rename */
			BarUpdateStationDisplayNames(app);
			/* Notify WebSocket clients of station list change */
			BarWsBroadcastStations(app);
		}
		BarUiActDefaultEventcmd ("stationrename");
	}
}

/*	play another station
 */
BarUiActCallback(BarUiActSelectStation) {
	/* Select by ID; no pianoHttpMutex hold while the user picks */
	const unsigned int holds = BarUiPianoSuspend (app);
	char *stationId = BarUiSelectStationId (app, "Select station: ", NULL,
			app->settings.autoselect);
	BarUiPianoResume (app, holds);
	if (stationId != NULL) {
		BarUiPianoHold (app);
		PianoStation_t *newStation = BarStateFindStationById (app, stationId);
		if (newStation != NULL) {
			/* Clear saved station - user made explicit choice */
			free(BarStateTakeLastStationId(app));
			BarStateSetNextStation(app, newStation);
		}
		BarUiPianoRelease (app);
		if (newStation != NULL) {
			drainPlaylist (app);
		} else {
			BarUiActSelectionGone (app, true);
		}
		free (stationId);
	}
}

/*	ban song for 1 month
 */
BarUiActCallback(BarUiActTempBanSong) {
	PianoReturn_t pRet;
	CURLcode wRet;

	assert (selSong != NULL);

	BarUiMsg (&app->settings, MSG_INFO, "Putting song on shelf... ");
	if (BarUiActDefaultPianoCall (PIANO_REQUEST_ADD_TIRED_SONG, selSong) &&
			selSong == BarStateGetPlaylist(app)) {
		BarUiDoSkipSong (&app->player);
	}
	BarUiActDefaultEventcmd ("songshelf");
}

/*	print upcoming songs (terminal)
 */
BarUiActCallback(BarUiActPrintUpcoming) {
	PianoSong_t * const nextSong = PianoListNextP (selSong);

	if (nextSong != NULL) {
		BarUiListSongs (app, nextSong, NULL);
	} else {
		BarUiMsg (&app->settings, MSG_INFO, "No songs in queue.\n");
	}
}

/*	send upcoming songs to the requesting web client only; the queue is read
 *	from state under its lock rather than through a borrowed song pointer
 */
void BarUiActPrintUpcomingTo (BarApp_t *app, BarWsConnId_t requester) {
	if (!BarWsEmitUpcomingTo(app, requester, 5)) {
		BarWsEmitErrorTo(app, requester, "query.upcoming", "No songs in queue");
	}
}

/*	selectStation callback used by BarUiActSelectQuickMix; toggle, select
 *	all/none
 */
static void BarUiActQuickmixCallback (BarApp_t *app, char *buf) {
	/* do nothing if buf is empty/contains more than one character */
	if (buf[0] == '\0' || buf[1] != '\0') {
		return;
	}

	switch (*buf) {
		case 't':
			/* toggle */
			BarStateChangeQuickMixAll (app, BAR_QUICKMIX_TOGGLE_ALL);
			*buf = '\0';
			break;

		case 'a':
			/* enable all */
			BarStateChangeQuickMixAll (app, BAR_QUICKMIX_SELECT_ALL);
			*buf = '\0';
			break;

		case 'n':
			/* enable none */
			BarStateChangeQuickMixAll (app, BAR_QUICKMIX_SELECT_NONE);
			*buf = '\0';
			break;
	}
}

/*	if current station is a quickmix: select stations that are played in
 *	quickmix
 */
BarUiActCallback(BarUiActSelectQuickMix) {
	PianoReturn_t pRet;
	CURLcode wRet;

	assert (selStation != NULL);

	if (selStation->isQuickMix) {
		char *toggleId;
		BarUiActInputWait_t wait;
		while (true) {
			BarUiActInputBegin (app, &wait, selStation, selSong);
			toggleId = BarUiSelectStationId (app, "Toggle QuickMix for station: ",
					BarUiActQuickmixCallback, false);
			BarUiActInputEnd (app, &wait, &selStation, &selSong);
			if (toggleId == NULL) {
				break;
			}
			BarStateToggleQuickMixById (app, toggleId);
			free (toggleId);
		}
		BarUiMsg (&app->settings, MSG_INFO, "Setting QuickMix stations... ");
		if (BarUiActDefaultPianoCall (PIANO_REQUEST_SET_QUICKMIX, NULL)) {
			/* Notify WebSocket clients of station list change */
			BarWsBroadcastStations(app);
		}
		BarUiActDefaultEventcmd ("stationquickmixtoggle");
	} else {
		BarUiMsg (&app->settings, MSG_ERR, "Please select a QuickMix station first.\n");
	}
}

/*	quit
 */
BarUiActCallback(BarUiActQuit) {
	BarUiMsg(&app->settings, MSG_INFO, "Exiting...\n");
	app->doQuit = true;
	
	BarUiDoSkipSong (&app->player);
}

/*	Tear down the Pandora session so the handle is clean for a fresh login
 *	Saves the resume station id, stops playback, waits for the playback manager
 *	to park, then re-initializes the Piano handle.
 *	@param app application handle
 *	@param resume_station_id_override if non-NULL/non-empty, used for lastStationId
 *		(e.g. attempted next station); else current station id
 *	@param context "disconnect" or "reconnect", for the park-idle log line
 *	@param freeSongHistory drop the song history (disconnect stops for good; a
 *		reconnect resumes playback and keeps the list)
 *	@return false when the Piano handle could not be re-initialized
 */
static bool BarSessionTeardown(BarApp_t *app,
		const char *resume_station_id_override, const char *context,
		bool freeSongHistory) {
	/* Save station ID for auto-resume on reconnect. The current station is
	 * read inside pianoHttpMutex, which keeps stations alive. */
	if (resume_station_id_override != NULL && resume_station_id_override[0] != '\0') {
		BarStateSetLastStationId(app, resume_station_id_override);
	} else {
		pthread_mutex_lock(&app->pianoHttpMutex);
		const PianoStation_t *curStation = BarStateGetCurrentStation(app);
		BarStateSetLastStationId(app, curStation ? curStation->id : NULL);
		pthread_mutex_unlock(&app->pianoHttpMutex);
	}
	
	/* Clear next station first so the playback manager cannot fetch a playlist
	 * using station pointers that PianoDestroy will free. */
	BarStateSetNextStation(app, NULL);
	BarStateDrainPlaylist(app);
	BarStateSetCurrentStation(app, NULL);

	/* Stop current playback and wait for the playback manager to finish
	 * cleaning up the player thread.  This prevents a race where we
	 * destroy the Piano handle while the playback manager is still
	 * referencing songs from the old playlist. */
	if (BarPlayerGetMode(&app->player) != PLAYER_DEAD) {
		BarUiDoSkipSong(&app->player);
		if (!BarPlayerWaitForMode (&app->player, PLAYER_DEAD,
		                            BAR_PLAYER_STOP_TIMEOUT_MS)) {
			BarUiMsg (&app->settings, MSG_ERR, "%s",
			          BarL10nGet (&app->l10n, "cli.player_stop_timeout"));
		}
	}

	if (!BarPlaybackManagerWaitParkedIdle (app, BAR_PLAYER_STOP_TIMEOUT_MS)) {
		log_write (DEBUG_UI, "PlaybackMgr: %s proceeding before park idle\n",
		           context);
	}
	
	if (freeSongHistory) {
		pthread_mutex_lock(&app->pianoHttpMutex);
		if (app->songHistory != NULL) {
			BarStateFreeSongs(app, app->songHistory);
			app->songHistory = NULL;
		}
		pthread_mutex_unlock(&app->pianoHttpMutex);
	}
	
	/* Disconnect from Pandora (destroys stations, user info, partner).
	 * Serialize with BarUiPianoCall — same app->ph. */
	pthread_mutex_lock(&app->pianoHttpMutex);
	PianoReturn_t piRet = BarStateResetPianoHandle(app);
	pthread_mutex_unlock(&app->pianoHttpMutex);
	if (piRet != PIANO_RET_OK) {
		BarUiMsg(&app->settings, MSG_ERR,
				BarL10nGet(&app->l10n, "cli.piano_reinit_failed"),
				PianoErrorToStr (piRet));
		app->player.interrupted = 1;
		return false;
	}
	return true;
}

/*	Internal stop helper - full reset without quitting
 *	Stops playback, clears station/playlist, disconnects from Pandora
 *	@param app application handle
 *	@param reason "user" for manual stop, "idle_timeout" for pause timeout,
 *		"playlist_session_error" when getPlaylist fails and auto-recovery did
 *		not restore the session
 *	@param resume_station_id_override if non-NULL/non-empty, used for lastStationId
 *		(e.g. attempted next station); else current station id
 */
void BarUiDoPandoraDisconnect(BarApp_t *app, const char *reason,
		const char *resume_station_id_override) {
	if (!BarSessionTeardown(app, resume_station_id_override, "disconnect", true)) {
		return;
	}
	
	BarWsBroadcastPandoraDisconnected(app, reason);
	
	if (strcmp(reason, "idle_timeout") == 0) {
		BarUiMsg(&app->settings, MSG_INFO, "Playback stopped due to inactivity.\n");
	} else if (strcmp(reason, "playlist_session_error") == 0) {
		BarUiMsg(&app->settings, MSG_INFO, "%s",
				BarL10nGet(&app->l10n, "cli.playlist_session_error"));
	} else {
		BarUiMsg(&app->settings, MSG_INFO, "Playback stopped, disconnected from Pandora.\n");
	}
}

/*	stop - full reset without quitting (UI action wrapper)
 */
BarUiActCallback(BarUiActPandoraDisconnect) {
	/* Teardown waits for the playback manager, which may need pianoHttpMutex */
	const unsigned int holds = BarUiPianoSuspend (app);
	BarUiDoPandoraDisconnect(app, "user", NULL);
	BarUiPianoResume (app, holds);
}

/*	Let the user pick a song from the history. The history is listed inside
 *	pianoHttpMutex, the hold is released while the user types, and the choice
 *	is resolved again by track token.
 *	@return selected history song or NULL
 */
static PianoSong_t *BarUiActSelectHistorySong (BarApp_t *app) {
	char buf[BAR_INPUT_MAX];

	memset (buf, 0, sizeof (buf));

	while (true) {
		const size_t count = BarUiListSongs (app, app->songHistory, buf);
		char **tokens = count > 0 ? calloc (count, sizeof (*tokens)) : NULL;
		if (count > 0 && tokens == NULL) {
			return NULL;
		}
		const PianoSong_t *listed = app->songHistory;
		for (size_t i = 0; i < count && listed != NULL;
				i++, listed = PianoListNextP (listed)) {
			tokens[i] = listed->trackToken ? strdup (listed->trackToken) : NULL;
		}

		BarUiMsg (&app->settings, MSG_QUESTION, "Select song: ");
		BarUiActInputWait_t wait;
		BarUiActInputBegin (app, &wait, NULL, NULL);
		const size_t len = BarReadlineStr (buf, sizeof (buf), &app->input,
				BAR_RL_DEFAULT);
		BarUiActInputEnd (app, &wait, NULL, NULL);

		PianoSong_t *song = NULL;
		bool chosen = false;
		if (len > 0 && strspn (buf, "0123456789") == len) {
			const unsigned long i = strtoul (buf, NULL, 10);
			if (i < count) {
				chosen = true;
				song = BarUiActFindSongByToken (app, tokens[i]);
			}
		}
		for (size_t i = 0; i < count; i++) {
			free (tokens[i]);
		}
		free (tokens);

		if (len == 0) {
			return NULL;
		}
		if (chosen) {
			if (song == NULL) {
				BarUiActSelectionGone (app, false);
			}
			return song;
		}
	}
}

/*	song history
 */
BarUiActCallback(BarUiActHistory) {
	char buf[2];
	PianoSong_t *histSong;

	if (app->songHistory != NULL) {
		histSong = BarUiActSelectHistorySong (app);
		if (histSong != NULL) {
			BarKeyShortcutId_t action;
			PianoStation_t *songStation = BarStateFindStationById(app, 
					histSong->stationId);

			if (songStation == NULL) {
				BarUiMsg (&app->settings, MSG_ERR, "Station does not exist any more.\n");
				return;
			}

			do {
				action = BAR_KS_COUNT;

				BarUiMsg (&app->settings, MSG_QUESTION, "What to do with this song? ");

				BarUiActInputWait_t wait;
				BarUiActInputBegin (app, &wait, songStation, histSong);
				const size_t len = BarReadline (buf, sizeof (buf), NULL,
						&app->input, BAR_RL_FULLRETURN, -1);
				BarUiActInputEnd (app, &wait, &songStation, &histSong);
				if (len > 0) {
					if (songStation == NULL || histSong == NULL) {
						BarUiActSelectionGone (app, songStation == NULL);
						return;
					}
					/* actions assume that selStation is the song's original
					 * station */
					action = BarUiDispatch (app, buf[0], songStation, histSong,
							false, BAR_DC_UNDEFINED);
				}
			} while (action == BAR_KS_HELP);
		} /* end if histSong != NULL */
	} else {
		BarUiMsg (&app->settings, MSG_INFO, (app->settings.history == 0) ? "History disabled.\n" :
				"No history yet.\n");
	}
}

/*	create song bookmark
 */
BarUiActCallback(BarUiActBookmark) {
	PianoReturn_t pRet;
	CURLcode wRet;
	char selectBuf[2];

	assert (selSong != NULL);

	BarUiMsg (&app->settings, MSG_QUESTION, "Bookmark [s]ong or [a]rtist? ");
	BarUiActInputWait_t wait;
	BarUiActInputBegin (app, &wait, selStation, selSong);
	BarReadline (selectBuf, sizeof (selectBuf), "sa", &app->input,
			BAR_RL_FULLRETURN, -1);
	BarUiActInputEnd (app, &wait, &selStation, &selSong);
	if ((selectBuf[0] == 's' || selectBuf[0] == 'a') && selSong == NULL) {
		BarUiActSelectionGone (app, false);
	} else if (selectBuf[0] == 's') {
		BarUiMsg (&app->settings, MSG_INFO, "Bookmarking song... ");
		BarUiActDefaultPianoCall (PIANO_REQUEST_BOOKMARK_SONG, selSong);
		BarUiActDefaultEventcmd ("songbookmark");
	} else if (selectBuf[0] == 'a') {
		BarUiMsg (&app->settings, MSG_INFO, "Bookmarking artist... ");
		BarUiActDefaultPianoCall (PIANO_REQUEST_BOOKMARK_ARTIST, selSong);
		BarUiActDefaultEventcmd ("artistbookmark");
	}
}

/*	decrease volume
 */
BarUiActCallback(BarUiActVolDown) {
	if (app->settings.volumeMode == BAR_VOLUME_MODE_SYSTEM) {
		int currentVol = BarSystemVolumeGet();
		if (currentVol > 0) {
			int newVol = currentVol - 5;  /* 5% step for system volume */
			if (newVol < 0) newVol = 0;
			BarSystemVolumeSet(newVol);
			/* Don't modify settings.volume - it stays at 0dB for player */
		}
	} else {
		BarPlayerAdjustVolume (&app->player, -1);
	}
	BarWsBroadcastVolume(app);
}

/*	increase volume
 */
BarUiActCallback(BarUiActVolUp) {
	if (app->settings.volumeMode == BAR_VOLUME_MODE_SYSTEM) {
		int currentVol = BarSystemVolumeGet();
		if (currentVol >= 0 && currentVol < VOLUME_MAX_PERCENT) {
			int newVol = currentVol + 5;  /* 5% step for system volume */
			if (newVol > VOLUME_MAX_PERCENT) newVol = VOLUME_MAX_PERCENT;
			BarSystemVolumeSet(newVol);
			/* Don't modify settings.volume - it stays at 0dB for player */
		}
	} else {
		BarPlayerAdjustVolume (&app->player, 1);
	}
	BarWsBroadcastVolume(app);
}

/*	reset volume
 */
BarUiActCallback(BarUiActVolReset) {
	if (app->settings.volumeMode == BAR_VOLUME_MODE_SYSTEM) {
		BarSystemVolumeSet(DEFAULT_VOLUME_PERCENT);  /* Reset for system volume */
	} else {
		BarPlayerSetVolume (&app->player, DEFAULT_VOLUME_PERCENT);
	}
	BarWsBroadcastVolume(app);
}

static const char *boolToYesNo (const bool value) {
	return value ? "yes" : "no";
}

/*	change pandora settings
 */
BarUiActCallback(BarUiActSettings) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoSettings_t settings;
	PianoRequestDataChangeSettings_t reqData;
	bool modified = false;

	memset (&settings, 0, sizeof (settings));
	memset (&reqData, 0, sizeof (reqData));

	BarUiMsg (&app->settings, MSG_INFO, "Retrieving settings... ");
	bool bret = BarUiActDefaultPianoCall (PIANO_REQUEST_GET_SETTINGS, &settings);
	BarUiActDefaultEventcmd ("settingsget");
	if (!bret) {
		return;
	}

	BarUiMsg (&app->settings, MSG_LIST, " 0) Username (%s)\n", settings.username);
	BarUiMsg (&app->settings, MSG_LIST, " 1) Password (*****)\n");
	BarUiMsg (&app->settings, MSG_LIST, " 2) Explicit content filter (%s)\n",
			boolToYesNo (settings.explicitContentFilter));

	/* No Pandora data is used while the user edits settings */
	BarUiActInputWait_t wait;
	BarUiActInputBegin (app, &wait, selStation, selSong);
	while (true) {
		int val;

		BarUiMsg (&app->settings, MSG_QUESTION, "Change setting: ");
		if (BarReadlineInt (&val, &app->input) == 0) {
			break;
		}

		switch (val) {
			case 0: {
				/* username */
				char buf[80];
				BarUiMsg (&app->settings, MSG_QUESTION, "New username: ");
				if (BarReadlineStr (buf, sizeof (buf), &app->input,
						BAR_RL_DEFAULT) > 0) {
					reqData.newUsername = strdup (buf);
					modified = true;
				}
				break;
			}

			case 1: {
				/* password */
				char buf[80];
				BarUiMsg (&app->settings, MSG_QUESTION, "New password: ");
				if (BarReadlineStr (buf, sizeof (buf), &app->input,
						BAR_RL_NOECHO) > 0) {
					reqData.newPassword = strdup (buf);
					modified = true;
				}
				/* write missing newline */
				puts ("");
				break;
			}

			case 2: {
				/* explicit content filter */
				BarUiMsg (&app->settings, MSG_QUESTION,
						"Enable explicit content filter? [yn] ");
				reqData.explicitContentFilter =
						BarReadlineYesNo (settings.explicitContentFilter,
						&app->input) ? PIANO_TRUE : PIANO_FALSE;
				modified = true;
				break;
			}

			default:
				/* continue */
				break;
		}
	}
	BarUiActInputEnd (app, &wait, &selStation, &selSong);

	if (modified) {
		reqData.currentUsername = app->settings.username;
		reqData.currentPassword = app->settings.password;
		BarUiMsg (&app->settings, MSG_INFO, "Changing settings... ");
		BarUiActDefaultPianoCall (PIANO_REQUEST_CHANGE_SETTINGS, &reqData);
		BarUiActDefaultEventcmd ("settingschange");
		/* we want to be able to change settings after a username/password
		 * change, so update our internal structs too. the user will have to
		 * update his config file by himself though */
		if (reqData.newUsername != NULL) {
			free (app->settings.username);
			app->settings.username = reqData.newUsername;
		}
		if (reqData.newPassword != NULL) {
			free (app->settings.password);
			app->settings.password = reqData.newPassword;
		}
	}
}

/*	Build the manage-station action prompt into buf (size bufLen).
 *	Returns the number of characters that would have been written (like snprintf).
 */
int BarUiActBuildManageStationQuestion (char *buf, size_t bufLen,
		bool hasArtistSeeds, bool hasSongSeeds, bool hasStationSeeds,
		bool hasFeedback, bool isQuickMix) {
	char *pos = buf;
	size_t remaining = bufLen;
	int total = 0;
	bool hasSeed = hasArtistSeeds || hasSongSeeds || hasStationSeeds ||
			hasFeedback;

#define APPEND(str) \
	do { \
		int _n = snprintf (pos, remaining, "%s", (str)); \
		if (_n > 0) { \
			size_t _advance = (size_t)_n < remaining ? (size_t)_n : remaining - 1; \
			pos += _advance; \
			remaining -= _advance; \
		} \
		total += _n; \
	} while (0)

	bool needsSep = false;
	if (hasSeed) {
		APPEND ("Delete ");
	}
	if (hasArtistSeeds) {
		APPEND ("[a]rtist");
		needsSep = true;
	}
	if (hasSongSeeds) {
		if (needsSep) { APPEND ("/"); }
		APPEND ("[s]ong");
		needsSep = true;
	}
	if (hasStationSeeds) {
		if (needsSep) { APPEND ("/"); }
		APPEND ("s[t]ation");
		needsSep = true;
	}
	if (hasArtistSeeds || hasSongSeeds || hasStationSeeds) {
		APPEND (" seeds");
	}
	if (hasFeedback) {
		if (needsSep) { APPEND (" or "); }
		APPEND ("[f]eedback");
	}
	if (hasSeed) {
		APPEND ("? ");
	}
	if (!isQuickMix) {
		APPEND ("Manage [m]ode? ");
	}

#undef APPEND

	return total;
}

/*	manage station (remove seeds or feedback)
 */
BarUiActCallback(BarUiActManageStation) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataGetStationInfo_t reqData;
	char selectBuf[2], allowedActions[6], *allowedPos = allowedActions;
	char question[512];

	memset (&reqData, 0, sizeof (reqData));
	reqData.station = selStation;

	BarUiMsg (&app->settings, MSG_INFO, "Fetching station info... ");
	const bool bret = BarUiActDefaultPianoCall (PIANO_REQUEST_GET_STATION_INFO,
			&reqData);
	BarUiActDefaultEventcmd ("stationfetchinfo");
	if (!bret) {
		return;
	}
	if (selStation == NULL) {
		BarUiActSelectionGone (app, true);
		PianoDestroyStationInfo (&reqData.info);
		return;
	}

	/* Build the menu prompt using a bounded helper — no strcat overflow risk */
	BarUiActBuildManageStationQuestion (question, sizeof (question),
			reqData.info.artistSeeds != NULL,
			reqData.info.songSeeds != NULL,
			reqData.info.stationSeeds != NULL,
			reqData.info.feedback != NULL,
			selStation->isQuickMix);

	/* Populate the allowed-actions string to match the prompt */
	if (reqData.info.artistSeeds != NULL) { *allowedPos++ = 'a'; }
	if (reqData.info.songSeeds != NULL)   { *allowedPos++ = 's'; }
	if (reqData.info.stationSeeds != NULL) { *allowedPos++ = 't'; }
	if (reqData.info.feedback != NULL)    { *allowedPos++ = 'f'; }
	if (!selStation->isQuickMix)          { *allowedPos++ = 'm'; }
	*allowedPos = '\0';

	if (allowedPos == allowedActions) {
		BarUiMsg (&app->settings, MSG_INFO, "No actions available.\n");
		return;
	}

	/* Seeds and feedback are owned by reqData; only the station is shared */
	BarUiMsg (&app->settings, MSG_QUESTION, "%s", question);
	BarUiActInputWait_t wait;
	BarUiActInputBegin (app, &wait, selStation, selSong);
	const size_t len = BarReadline (selectBuf, sizeof (selectBuf), allowedActions,
			&app->input, BAR_RL_FULLRETURN, -1);
	BarUiActInputEnd (app, &wait, &selStation, &selSong);
	if (len > 0 && selStation == NULL) {
		BarUiActSelectionGone (app, true);
	} else if (len > 0) {
		if (selectBuf[0] == 'a') {
			BarUiActInputBegin (app, &wait, selStation, selSong);
			PianoArtist_t *artist = BarUiSelectArtist (app,
					reqData.info.artistSeeds);
			BarUiActInputEnd (app, &wait, &selStation, &selSong);
			if (artist != NULL) {
				PianoRequestDataDeleteSeed_t subReqData;

				memset (&subReqData, 0, sizeof (subReqData));
				subReqData.artist = artist;

				BarUiMsg (&app->settings, MSG_INFO, "Deleting artist seed... ");
				BarUiActDefaultPianoCall (PIANO_REQUEST_DELETE_SEED, &subReqData);
				BarUiActDefaultEventcmd ("stationdeleteartistseed");
			}
		} else if (selectBuf[0] == 's') {
			BarUiActInputBegin (app, &wait, selStation, selSong);
			PianoSong_t *song = BarUiSelectSong (app,
					reqData.info.songSeeds, &app->input);
			BarUiActInputEnd (app, &wait, &selStation, &selSong);
			if (song != NULL) {
				PianoRequestDataDeleteSeed_t subReqData;

				memset (&subReqData, 0, sizeof (subReqData));
				subReqData.song = song;

				BarUiMsg (&app->settings, MSG_INFO, "Deleting song seed... ");
				BarUiActDefaultPianoCall (PIANO_REQUEST_DELETE_SEED, &subReqData);
				BarUiActDefaultEventcmd ("stationdeletesongseed");
			}
		} else if (selectBuf[0] == 't') {
			BarUiActInputBegin (app, &wait, selStation, selSong);
			PianoStation_t *station = BarUiSelectStation (app,
					reqData.info.stationSeeds, "Delete seed station: ", NULL,
					false);
			BarUiActInputEnd (app, &wait, &selStation, &selSong);
			if (station != NULL) {
				PianoRequestDataDeleteSeed_t subReqData;

				memset (&subReqData, 0, sizeof (subReqData));
				subReqData.station = station;

				BarUiMsg (&app->settings, MSG_INFO, "Deleting station seed... ");
				BarUiActDefaultPianoCall (PIANO_REQUEST_DELETE_SEED, &subReqData);
				BarUiActDefaultEventcmd ("stationdeletestationseed");
			}
		} else if (selectBuf[0] == 'f') {
			BarUiActInputBegin (app, &wait, selStation, selSong);
			PianoSong_t *song = BarUiSelectSong (app,
					reqData.info.feedback, &app->input);
			BarUiActInputEnd (app, &wait, &selStation, &selSong);
			if (song != NULL) {
				BarUiMsg (&app->settings, MSG_INFO, "Deleting feedback... ");
				BarUiActDefaultPianoCall (PIANO_REQUEST_DELETE_FEEDBACK, song);
				BarUiActDefaultEventcmd ("stationdeletefeedback");
			}
		} else if (selectBuf[0] == 'm') {
			PianoRequestDataGetStationModes_t subReqData =
					{ .station = selStation };
			BarUiMsg (&app->settings, MSG_INFO, "Fetching modes... ");
			BarUiActDefaultPianoCall (PIANO_REQUEST_GET_STATION_MODES,
					&subReqData);
			BarUiActDefaultEventcmd ("stationgetmodes");

			const PianoStationMode_t *curMode = subReqData.retModes;
			unsigned int i = 0;
			PianoListForeachP (curMode) {
				BarUiMsg (&app->settings, MSG_LIST, "%2i) %s: %s%s\n", i,
						curMode->name, curMode->description,
						curMode->active ? " (active)" : "");
				i++;
			}

			BarUiMsg (&app->settings, MSG_QUESTION, "Pick a new mode: ");
			int selected;
			while (true) {
				BarUiActInputBegin (app, &wait, selStation, selSong);
				const size_t modeLen = BarReadlineInt (&selected, &app->input);
				BarUiActInputEnd (app, &wait, &selStation, &selSong);
				if (modeLen == 0) {
					break;
				}
				if (selStation == NULL) {
					BarUiActSelectionGone (app, true);
					break;
				}

				const PianoStationMode_t * const selMode =
						PianoListGetP (subReqData.retModes, selected);
				if (selMode != NULL) {
					PianoRequestDataSetStationMode_t subReqDataSet =
							{.station = selStation, .id = selected};
					BarUiMsg (&app->settings, MSG_INFO,
							"Selecting mode \"%s\"... ", selMode->name);
					if (BarUiActDefaultPianoCall (
							PIANO_REQUEST_SET_STATION_MODE, &subReqDataSet)) {
						drainPlaylist (app);
					}
					BarUiActDefaultEventcmd ("stationsetmode");
					break;
				}
			}

			PianoDestroyStationMode (subReqData.retModes);
		}
	}

	PianoDestroyStationInfo (&reqData.info);
}

/*	pandora reconnect - re-authenticate and fetch stations
 *	Uses active account credentials (set by BarSocketIoHandleAction for account switches)
 */
bool BarUiDoPandoraReconnect (BarApp_t *app,
		const char *resume_station_id_override) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataLogin_t reqData;

	/* Resolve credentials from active account or main settings */
	const BarAccount_t *acct = BarSettingsGetActiveAccount (&app->settings);
	const char *user = acct ? acct->username : app->settings.username;
	const char *pass = acct ? acct->password : app->settings.password;

	if (user == NULL || pass == NULL) {
		BarUiMsg(&app->settings, MSG_ERR, 
			"Cannot reconnect: No credentials configured.\n");
		BarWsEmitError(app, "app.pandora-reconnect", "No credentials configured");
		return false;
	}

	reqData.user = (char *)user;
	reqData.password = (char *)pass;
	reqData.step = 0;

	/* Clean up existing Pandora session so GET_STATIONS doesn't append
	 * to the old station list (libpiano appends, never replaces). */
	if (!BarSessionTeardown(app, resume_station_id_override, "reconnect", false)) {
		return false;
	}

	if (acct && acct->label) {
		BarUiMsg(&app->settings, MSG_INFO, "Reconnecting to Pandora (%s)... ",
				acct->label);
	} else {
		BarUiMsg(&app->settings, MSG_INFO, "Reconnecting to Pandora... ");
	}
	if (!BarUiPianoCall(app, PIANO_REQUEST_LOGIN, &reqData, &pRet, &wRet)) {
		BarUiMsg(&app->settings, MSG_ERR, "Failed to reconnect.\n");
		return false;
	}
	
	/* Fetch stations */
	BarUiMsg(&app->settings, MSG_INFO, "Get stations... ");
	if (!BarUiPianoCall(app, PIANO_REQUEST_GET_STATIONS, NULL, &pRet, &wRet)) {
		BarUiMsg(&app->settings, MSG_ERR, "Failed to get stations.\n");
		return false;
	}
	
	/* Update display names after stations are fetched */
	BarUpdateStationDisplayNames(app);
	
	/* Broadcast updated stations to WebSocket clients */
	BarWsBroadcastStations(app);
	
	/* Auto-resume: prefer per-account autostart, then last station */
	const char *autostation = (acct && acct->autostartStation) ?
			acct->autostartStation : NULL;
	/* Stations are resolved and stored inside pianoHttpMutex, so a concurrent
	 * delete cannot free one between lookup and BarStateSetNextStation. */
	char *lastStationId = NULL;
	bool resumed = false;
	pthread_mutex_lock(&app->pianoHttpMutex);
	if (autostation && BarStateGetNextStation(app) == NULL) {
		PianoStation_t *station = BarStateFindStationById(app, autostation);
		if (station) {
			BarUiMsg(&app->settings, MSG_INFO, "Starting account station: %s\n",
					station->name);
			BarStateSetNextStation(app, station);
		}
	} else if (BarStateGetNextStation(app) == NULL &&
			(lastStationId = BarStateTakeLastStationId(app)) != NULL) {
		PianoStation_t *station = BarStateFindStationById(app, lastStationId);
		if (station) {
			BarUiMsg(&app->settings, MSG_INFO, "Resuming station: %s\n", station->name);
			BarStateSetNextStation(app, station);
			resumed = true;
		}
	}
	pthread_mutex_unlock(&app->pianoHttpMutex);
	if (lastStationId != NULL && !resumed) {
		BarWsEmitErrorEx(app, "app.pandora-reconnect", "Last station was deleted",
		                  lastStationId);
	}
	free(lastStationId);
	
	BarUiMsg(&app->settings, MSG_INFO, "Reconnected to Pandora.\n");
	/* A working session again: allow auto-recovery for the next failure. */
	atomic_store (&app->autoRecoverFailed, false);
	return true;
}

/*	pandora reconnect (UI/WebSocket action wrapper)
 */
BarUiActCallback(BarUiActPandoraReconnect) {
	/* Teardown waits for the playback manager, which may need pianoHttpMutex */
	const unsigned int holds = BarUiPianoSuspend (app);
	(void) BarUiDoPandoraReconnect (app, NULL);
	BarUiPianoResume (app, holds);
}

/*	Restore a session that died without the user asking for it
 *	@param reason disconnect reason that triggered recovery
 *	@param resume_station_id_override station to resume (station pointers do not
 *		survive the teardown, so callers pass an owned copy of the id)
 *	@return true when the session is usable and a station is queued again
 */
bool BarSessionTryAutoRecover (BarApp_t *app, const char *reason,
		const char *resume_station_id_override) {
	assert (app != NULL);
	assert (reason != NULL);

	/* The user asked for silence: stay stopped. */
	if (strcmp (reason, "user") == 0 || strcmp (reason, "idle_timeout") == 0) {
		return false;
	}

	/* Shutting down: nothing to resume into. */
	if (atomic_load_explicit (&app->doQuit, memory_order_relaxed)) {
		return false;
	}

	/* One attempt per session death (cleared when a song starts) so a Pandora
	 * outage cannot turn into a reconnect loop. */
	if (atomic_load (&app->autoRecoverInFlight)) {
		return false;
	}
	if (atomic_load (&app->autoRecoverFailed)) {
		return false;
	}

	atomic_store (&app->autoRecoverInFlight, true);
	log_write (DEBUG_UI, "Session: auto-recovering after %s\n", reason);
	BarUiMsg (&app->settings, MSG_INFO, "%s",
	          BarL10nGet (&app->l10n, "cli.session_auto_recover"));

	const bool recovered = BarUiDoPandoraReconnect (app,
			resume_station_id_override);

	atomic_store (&app->autoRecoverInFlight, false);
	atomic_store (&app->autoRecoverFailed, !recovered);

	if (!recovered) {
		log_write (DEBUG_UI, "Session: auto-recovery failed after %s\n", reason);
		BarUiMsg (&app->settings, MSG_ERR, "%s",
		          BarL10nGet (&app->l10n, "cli.session_auto_recover_failed"));
	}
	return recovered;
}
