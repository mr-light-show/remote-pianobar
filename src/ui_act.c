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
#include "debug.h"
#include "system_volume.h"

/*	standard eventcmd call
 */
#define BarUiActDefaultEventcmd(name) BarUiStartEventCmd (&app->settings, \
		name, selStation, selSong, &app->player, BarStateGetStationList(app), \
		pRet, wRet)

/*	standard piano call
 */
#define BarUiActDefaultPianoCall(call, arg) BarUiPianoCall (app, \
		call, arg, &pRet, &wRet)

/*	helper to _really_ skip a song (unlock mutex, quit player)
 *	@param player handle
 */
static inline void BarUiDoSkipSong (player_t * const player) {
	assert (player != NULL);

	pthread_mutex_lock (&player->lock);
	player->doQuit = true;
	player->doPause = false;
	player->pauseStartTime = 0;  /* Clear pause timer */
	pthread_cond_broadcast (&player->cond);
	pthread_mutex_unlock (&player->lock);
	pthread_mutex_lock (&player->aoplayLock);
	pthread_cond_broadcast (&player->aoplayCond);
	pthread_mutex_unlock (&player->aoplayLock);
}

/*	transform station if necessary to allow changes like rename, rate, ...
 *	@param piano handle
 *	@param transform this station
 *	@return 0 = error, 1 = everything went well
 */
int BarTransformIfShared (BarApp_t *app, PianoStation_t *station) {
	PianoReturn_t pRet;
	CURLcode wRet;

	assert (station != NULL);

	/* shared stations must be transformed */
	if (!station->isCreator) {
		BarUiMsg (&app->settings, MSG_INFO, "Transforming station... ");
		if (!BarUiPianoCall (app, PIANO_REQUEST_TRANSFORM_STATION, station,
				&pRet, &wRet)) {
			return 0;
		}
	}
	return 1;
}

/*	Silent version of BarTransformIfShared for WebSocket thread
 *	Does not write to terminal (no BarUiMsg calls)
 *	Returns error message via errorMsg parameter
 */
int BarWsTransformIfShared (BarApp_t *app, PianoStation_t *station, char **errorMsg) {
	PianoReturn_t pRet;
	CURLcode wRet;

	assert (station != NULL);

	/* shared stations must be transformed */
	if (!station->isCreator) {
		debugPrint(DEBUG_WEBSOCKET, "Transforming shared station...\n");
		if (!BarWsPianoCall (app, PIANO_REQUEST_TRANSFORM_STATION, station,
				&pRet, &wRet, errorMsg)) {
			return 0;
		}
	}
	return 1;
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

	reqData.musicId = BarUiSelectMusicId (app, selStation,
			"Add artist or title to station: ");
	if (reqData.musicId != NULL) {
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
		/* Notify WebSocket clients of rating change */
		BarWsBroadcastSongStart(app);
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
	reqData.token = BarUiSelectMusicId (app, NULL,
			"Create station from artist or title: ");
	if (reqData.token != NULL) {
		BarUiMsg (&app->settings, MSG_INFO, "Creating station... ");
		if (BarUiActDefaultPianoCall (PIANO_REQUEST_CREATE_STATION, &reqData)) {
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

	reqData.token = selSong->trackToken;
	reqData.type = PIANO_MUSICTYPE_INVALID;

	BarUiMsg (&app->settings, MSG_QUESTION, "Create station from [s]ong or [a]rtist? ");
	BarReadline (selectBuf, sizeof (selectBuf), "sa", &app->input,
			BAR_RL_FULLRETURN, -1);
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
	char stationId[50];
	PianoRequestDataCreateStation_t reqData;

	reqData.token = stationId;
	reqData.type = PIANO_MUSICTYPE_INVALID;

	BarUiMsg (&app->settings, MSG_QUESTION, "Station id: ");
	if (BarReadline (stationId, sizeof (stationId), "0123456789", &app->input,
			BAR_RL_DEFAULT, -1) > 0) {
		BarUiMsg (&app->settings, MSG_INFO, "Adding shared station... ");
		if (BarUiActDefaultPianoCall (PIANO_REQUEST_CREATE_STATION, &reqData)) {
			/* Notify WebSocket clients of station list change */
			BarWsBroadcastStations(app);
		}
		BarUiActDefaultEventcmd ("stationaddshared");
	}
}

static void drainPlaylist (BarApp_t * const app) {
	BarUiDoSkipSong (&app->player);
	PianoSong_t *playlist = BarStateGetPlaylist(app);
	if (playlist != NULL) {
		/* drain playlist */
		PianoDestroyPlaylist (PianoListNextP (playlist));
		playlist->head.next = NULL;
		BarStateSetPlaylist(app, playlist);
	}
}

/*	Switch to a station (for WebSocket/programmatic use)
 */
void BarUiSwitchStation (BarApp_t * const app, PianoStation_t * const station) {
	assert (app != NULL);
	assert (station != NULL);
	
	BarStateSetNextStation(app, station);
	drainPlaylist (app);
}

/*	delete current station
 */
BarUiActCallback(BarUiActDeleteStation) {
	PianoReturn_t pRet;
	CURLcode wRet;

	assert (selStation != NULL);

	BarUiMsg (&app->settings, MSG_QUESTION, "Really delete \"%s\"? [yN] ",
			selStation->name);
	if (BarReadlineYesNo (false, &app->input)) {
		BarUiMsg (&app->settings, MSG_INFO, "Deleting station... ");
		if (BarUiActDefaultPianoCall (PIANO_REQUEST_DELETE_STATION, selStation)) {
			/* Notify WebSocket clients of station list change */
			BarWsBroadcastStations(app);
			if (selStation == BarStateGetCurrentStation(app)) {
				drainPlaylist (app);
				BarStateSetNextStation(app, NULL);
				/* XXX: usually we shoudn't touch cur*, but DELETE_STATION destroys
				 * station struct */
				BarStateSetCurrentStation(app, NULL);
				selStation = NULL;
			}
		}
		BarUiActDefaultEventcmd ("stationdelete");
	}
}

/*	explain pandora's song choice
 */
BarUiActCallback(BarUiActExplain) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataExplain_t reqData;

	assert (selSong != NULL);

	reqData.song = selSong;

	BarUiMsg (&app->settings, MSG_INFO, "Receiving explanation... ");
	if (BarUiActDefaultPianoCall (PIANO_REQUEST_EXPLAIN, &reqData)) {
		if (reqData.retExplain == NULL) {
			BarUiMsg (&app->settings, MSG_ERR, "No explanation provided.\n");
		} else {
		BarUiMsg (&app->settings, MSG_INFO, "%s\n", reqData.retExplain);
		
		BarWsBroadcastExplanation(app, reqData.retExplain);
		
		free (reqData.retExplain);
		}
	}
	BarUiActDefaultEventcmd ("songexplain");
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

	do {
		/* select category or exit */
		BarUiMsg (&app->settings, MSG_QUESTION, "Select category: ");
		if (BarReadlineInt (&i, &app->input) == 0) {
			return;
		}
		curCat = PianoListGetP (app->ph.genreStations, i);
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
		if (BarReadlineInt (&i, &app->input) == 0) {
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
		/* Notify WebSocket clients of rating change */
		BarWsBroadcastSongStart(app);
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
	/* LOCK HIERARCHY: player.lock is Lock #2 in the hierarchy
	 * PROTECTS: player.doPause, player.pauseStartTime
	 * DURATION: Held for microseconds (just to set flags)
	 * BROADCAST: BarWsBroadcastPlayState() called AFTER lock is released
	 * See src/THREAD_SAFETY.md for details */
	pthread_mutex_lock (&app->player.lock);
	app->player.doPause = false;
	app->player.pauseStartTime = 0;  /* Clear pause timer */
	pthread_cond_broadcast (&app->player.cond);
	pthread_mutex_unlock (&app->player.lock);
	
	/* Notify WebSocket clients of state change */
	BarWsBroadcastPlayState(app);
}

/*	pause
 */
BarUiActCallback(BarUiActPause) {
	/* LOCK HIERARCHY: player.lock is Lock #2 in the hierarchy
	 * PROTECTS: player.doPause, player.pauseStartTime
	 * See src/THREAD_SAFETY.md for details */
	pthread_mutex_lock (&app->player.lock);
	app->player.doPause = true;
	app->player.pauseStartTime = time(NULL);  /* Start pause timer */
	pthread_cond_broadcast (&app->player.cond);
	pthread_mutex_unlock (&app->player.lock);
	
	/* Notify WebSocket clients of state change */
	BarWsBroadcastPlayState(app);
}

/*	toggle pause
 */
BarUiActCallback(BarUiActTogglePause) {
	/* LOCK HIERARCHY: player.lock is Lock #2 in the hierarchy
	 * PROTECTS: player.doPause, player.pauseStartTime
	 * See src/THREAD_SAFETY.md for details */
	pthread_mutex_lock (&app->player.lock);
	app->player.doPause = !app->player.doPause;
	/* Update pause timer */
	if (app->player.doPause) {
		app->player.pauseStartTime = time(NULL);
	} else {
		app->player.pauseStartTime = 0;
	}
	pthread_cond_broadcast (&app->player.cond);
	pthread_mutex_unlock (&app->player.lock);
	
	/* Notify WebSocket clients of state change */
	BarWsBroadcastPlayState(app);
}

/*	rename current station
 */
BarUiActCallback(BarUiActRenameStation) {
	PianoReturn_t pRet;
	CURLcode wRet;
	char lineBuf[100];

	assert (selStation != NULL);

	BarUiMsg (&app->settings, MSG_QUESTION, "New name: ");
	if (BarReadlineStr (lineBuf, sizeof (lineBuf), &app->input, BAR_RL_DEFAULT) > 0) {
		PianoRequestDataRenameStation_t reqData;
		if (!BarTransformIfShared (app, selStation)) {
			return;
		}

		reqData.station = selStation;
		reqData.newName = lineBuf;

		BarUiMsg (&app->settings, MSG_INFO, "Renaming station... ");
		if (BarUiActDefaultPianoCall (PIANO_REQUEST_RENAME_STATION, &reqData)) {
			/* Notify WebSocket clients of station list change */
			BarWsBroadcastStations(app);
		}
		BarUiActDefaultEventcmd ("stationrename");
	}
}

/*	play another station
 */
BarUiActCallback(BarUiActSelectStation) {
	PianoStation_t *newStation = BarUiSelectStation (app, BarStateGetStationList(app),
			"Select station: ", NULL, app->settings.autoselect);
	if (newStation != NULL) {
		BarStateSetNextStation(app, newStation);
		drainPlaylist (app);
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

/*	print upcoming songs
 */
BarUiActCallback(BarUiActPrintUpcoming) {
	PianoSong_t * const nextSong = PianoListNextP (selSong);
	if (nextSong != NULL) {
		BarUiListSongs (app, nextSong, NULL);
		BarWsBroadcastUpcoming(app, nextSong, 5);
	} else {
		BarUiMsg (&app->settings, MSG_INFO, "No songs in queue.\n");
	}
}

/*	selectStation callback used by BarUiActSelectQuickMix; toggle, select
 *	all/none
 */
static void BarUiActQuickmixCallback (BarApp_t *app, char *buf) {
	PianoStation_t *curStation = BarStateGetStationList(app);

	/* do nothing if buf is empty/contains more than one character */
	if (buf[0] == '\0' || buf[1] != '\0') {
		return;
	}

	switch (*buf) {
		case 't':
			/* toggle */
			PianoListForeachP (curStation) {
				curStation->useQuickMix = !curStation->useQuickMix;
			}
			*buf = '\0';
			break;

		case 'a':
			/* enable all */
			PianoListForeachP (curStation) {
				curStation->useQuickMix = true;
			}
			*buf = '\0';
			break;

		case 'n':
			/* enable none */
			PianoListForeachP (curStation) {
				curStation->useQuickMix = false;
			}
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
		PianoStation_t *toggleStation;
		while ((toggleStation = BarUiSelectStation (app, BarStateGetStationList(app),
				"Toggle QuickMix for station: ",
				BarUiActQuickmixCallback, false)) != NULL) {
			toggleStation->useQuickMix = !toggleStation->useQuickMix;
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

/*	Internal stop helper - full reset without quitting
 *	Stops playback, clears station/playlist, disconnects from Pandora
 *	@param app application handle
 *	@param reason "user" for manual stop, "idle_timeout" for pause timeout
 */
void BarUiDoPandoraDisconnect(BarApp_t *app, const char *reason) {
	/* Stop current playback */
	BarUiDoSkipSong(&app->player);
	
	/* Clear playlist and stations */
	BarStateDrainPlaylist(app);
	BarStateSetCurrentStation(app, NULL);
	BarStateSetNextStation(app, NULL);
	
	/* Free song history */
	if (app->songHistory != NULL) {
		PianoDestroyPlaylist(app->songHistory);
		app->songHistory = NULL;
	}
	
	/* Disconnect from Pandora (destroys stations, user info, partner) */
	PianoDestroy(&app->ph);
	
	/* Re-initialize for next login */
	PianoInit(&app->ph, app->settings.partnerUser, app->settings.partnerPassword,
	          app->settings.device, app->settings.inkey, app->settings.outkey);
	
	/* Disconnect all WebSocket clients - they'll reconnect with fresh state */
	BarWsDisconnectAllClients(app);
	
	if (strcmp(reason, "idle_timeout") == 0) {
		BarUiMsg(&app->settings, MSG_INFO, "Playback stopped due to inactivity.\n");
	} else {
		BarUiMsg(&app->settings, MSG_INFO, "Playback stopped, disconnected from Pandora.\n");
	}
}

/*	stop - full reset without quitting (UI action wrapper)
 */
BarUiActCallback(BarUiActPandoraDisconnect) {
	BarUiDoPandoraDisconnect(app, "user");
}

/*	song history
 */
BarUiActCallback(BarUiActHistory) {
	char buf[2];
	PianoSong_t *histSong;

	if (app->songHistory != NULL) {
		histSong = BarUiSelectSong (app, app->songHistory,
				&app->input);
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

				if (BarReadline (buf, sizeof (buf), NULL, &app->input,
						BAR_RL_FULLRETURN, -1) > 0) {
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
	BarReadline (selectBuf, sizeof (selectBuf), "sa", &app->input,
			BAR_RL_FULLRETURN, -1);
	if (selectBuf[0] == 's') {
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
		--app->settings.volume;
		BarPlayerSetVolume (&app->player);
	}
	BarWsBroadcastVolume(app);
}

/*	increase volume
 */
BarUiActCallback(BarUiActVolUp) {
	if (app->settings.volumeMode == BAR_VOLUME_MODE_SYSTEM) {
		int currentVol = BarSystemVolumeGet();
		if (currentVol >= 0 && currentVol < 100) {
			int newVol = currentVol + 5;  /* 5% step for system volume */
			if (newVol > 100) newVol = 100;
			BarSystemVolumeSet(newVol);
			/* Don't modify settings.volume - it stays at 0dB for player */
		}
	} else {
		++app->settings.volume;
		BarPlayerSetVolume (&app->player);
	}
	BarWsBroadcastVolume(app);
}

/*	reset volume
 */
BarUiActCallback(BarUiActVolReset) {
	if (app->settings.volumeMode == BAR_VOLUME_MODE_SYSTEM) {
		BarSystemVolumeSet(50);  /* Reset to 50% for system volume */
		/* Don't modify settings.volume - it stays at 0dB for player */
	} else {
		app->settings.volume = 0;
		BarPlayerSetVolume (&app->player);
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

/*	manage station (remove seeds or feedback)
 */
BarUiActCallback(BarUiActManageStation) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataGetStationInfo_t reqData;
	char selectBuf[2], allowedActions[6], *allowedPos = allowedActions;
	char question[128];

	memset (&reqData, 0, sizeof (reqData));
	reqData.station = selStation;

	BarUiMsg (&app->settings, MSG_INFO, "Fetching station info... ");
	const bool bret = BarUiActDefaultPianoCall (PIANO_REQUEST_GET_STATION_INFO,
			&reqData);
	BarUiActDefaultEventcmd ("stationfetchinfo");
	if (!bret) {
		return;
	}

	/* enable submenus depending on data availability */
	if (reqData.info.artistSeeds != NULL ||
			reqData.info.songSeeds != NULL ||
			reqData.info.stationSeeds != NULL ||
			reqData.info.feedback != NULL) {
		strcpy (question, "Delete ");
	}
	if (reqData.info.artistSeeds != NULL) {
		strcat (question, "[a]rtist");
		*allowedPos++ = 'a';
	}
	if (reqData.info.songSeeds != NULL) {
		if (allowedPos != allowedActions) {
			strcat (question, "/");
		}
		strcat (question, "[s]ong");
		*allowedPos++ = 's';
	}
	if (reqData.info.stationSeeds != NULL) {
		if (allowedPos != allowedActions) {
			strcat (question, "/");
		}
		strcat (question, "s[t]ation");
		*allowedPos++ = 't';
	}
	if (allowedPos != allowedActions) {
		strcat (question, " seeds");
	}
	if (reqData.info.feedback != NULL) {
		if (allowedPos != allowedActions) {
			strcat (question, " or ");
		}
		strcat (question, "[f]eedback");
		*allowedPos++ = 'f';
	}
	if (allowedPos != allowedActions) {
		strcat (question, "? ");
	}
	/* station mode is not available for QuickMix. */
	if (!selStation->isQuickMix) {
		strcat (question, "Manage [m]ode? ");
		*allowedPos++ = 'm';
	}

	*allowedPos = '\0';

	if (allowedPos == allowedActions) {
		BarUiMsg (&app->settings, MSG_INFO, "No actions available.\n");
		return;
	}

	assert (strlen (question) < sizeof (question) / sizeof (*question));

	BarUiMsg (&app->settings, MSG_QUESTION, "%s", question);
	if (BarReadline (selectBuf, sizeof (selectBuf), allowedActions, &app->input,
					BAR_RL_FULLRETURN, -1)) {
		if (selectBuf[0] == 'a') {
			PianoArtist_t *artist = BarUiSelectArtist (app,
					reqData.info.artistSeeds);
			if (artist != NULL) {
				PianoRequestDataDeleteSeed_t subReqData;

				memset (&subReqData, 0, sizeof (subReqData));
				subReqData.artist = artist;

				BarUiMsg (&app->settings, MSG_INFO, "Deleting artist seed... ");
				BarUiActDefaultPianoCall (PIANO_REQUEST_DELETE_SEED, &subReqData);
				BarUiActDefaultEventcmd ("stationdeleteartistseed");
			}
		} else if (selectBuf[0] == 's') {
			PianoSong_t *song = BarUiSelectSong (app,
					reqData.info.songSeeds, &app->input);
			if (song != NULL) {
				PianoRequestDataDeleteSeed_t subReqData;

				memset (&subReqData, 0, sizeof (subReqData));
				subReqData.song = song;

				BarUiMsg (&app->settings, MSG_INFO, "Deleting song seed... ");
				BarUiActDefaultPianoCall (PIANO_REQUEST_DELETE_SEED, &subReqData);
				BarUiActDefaultEventcmd ("stationdeletesongseed");
			}
		} else if (selectBuf[0] == 't') {
			PianoStation_t *station = BarUiSelectStation (app,
					reqData.info.stationSeeds, "Delete seed station: ", NULL,
					false);
			if (station != NULL) {
				PianoRequestDataDeleteSeed_t subReqData;

				memset (&subReqData, 0, sizeof (subReqData));
				subReqData.station = station;

				BarUiMsg (&app->settings, MSG_INFO, "Deleting station seed... ");
				BarUiActDefaultPianoCall (PIANO_REQUEST_DELETE_SEED, &subReqData);
				BarUiActDefaultEventcmd ("stationdeletestationseed");
			}
		} else if (selectBuf[0] == 'f') {
			PianoSong_t *song = BarUiSelectSong (app,
					reqData.info.feedback, &app->input);
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
				if (BarReadlineInt (&selected, &app->input) == 0) {
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
 */
BarUiActCallback(BarUiActPandoraReconnect) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataLogin_t reqData;
	
	/* Check if credentials are available */
	if (app->settings.username == NULL || app->settings.password == NULL) {
		BarUiMsg(&app->settings, MSG_ERR, 
			"Cannot reconnect: No credentials configured.\n");
		return;
	}
	
	/* Re-authenticate with Pandora */
	reqData.user = app->settings.username;
	reqData.password = app->settings.password;
	reqData.step = 0;
	
	BarUiMsg(&app->settings, MSG_INFO, "Reconnecting to Pandora... ");
	if (!BarUiPianoCall(app, PIANO_REQUEST_LOGIN, &reqData, &pRet, &wRet)) {
		BarUiMsg(&app->settings, MSG_ERR, "Failed to reconnect.\n");
		return;
	}
	
	/* Fetch stations */
	BarUiMsg(&app->settings, MSG_INFO, "Get stations... ");
	if (!BarUiPianoCall(app, PIANO_REQUEST_GET_STATIONS, NULL, &pRet, &wRet)) {
		BarUiMsg(&app->settings, MSG_ERR, "Failed to get stations.\n");
		return;
	}
	
	/* Broadcast updated stations to WebSocket clients */
	BarWsBroadcastStations(app);
	
	BarUiMsg(&app->settings, MSG_INFO, "Reconnected to Pandora.\n");
}
