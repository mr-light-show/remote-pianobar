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

#include "playback_lifecycle.h"
#include "interrupt.h"
#include "bar_state.h"
#include "ui.h"
#include "player.h"
#include "l10n.h"
#include "websocket_bridge.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* Forward declarations to avoid pulling in ui_dispatch.h's static dispatch table */
extern void BarUiDoPandoraDisconnect (BarApp_t *app, const char *reason,
		const char *resume_station_id_override);
extern bool BarSessionTryAutoRecover (BarApp_t *app, const char *reason,
		const char *resume_station_id_override);

/*	One getPlaylist attempt for the next station.
 *	@param pRet libpiano status of the attempt
 *	@param wRet curl status of the attempt
 *	@return true when the request succeeded (the playlist may still be empty)
 */
static bool BarPlaybackFetchPlaylistOnce (BarApp_t *app, PianoReturn_t *pRet,
		CURLcode *wRet) {
	PianoRequestDataGetPlaylist_t reqData;
	reqData.station = BarStateGetNextStation (app);
	reqData.quality = app->settings.audioQuality;

	BarUiMsg (&app->settings, MSG_INFO, "%s",
	          BarL10nGet (&app->l10n, "cli.receiving_playlist"));
	if (!BarUiPianoCall (app, PIANO_REQUEST_GET_PLAYLIST,
	                     &reqData, pRet, wRet)) {
		return false;
	}

	BarStateSetPlaylist (app, reqData.retPlaylist);
	if (BarStateGetPlaylist (app) == NULL) {
		BarUiMsg (&app->settings, MSG_INFO, "%s",
		          BarL10nGet (&app->l10n, "cli.no_tracks_left"));
		BarStateSetNextStation (app, NULL);
	}
	return true;
}

/*	Fetch the next playlist for the current station.
 *	Logic extracted from BarMainGetPlaylist in main.c.
 *	A failed request usually means the Pandora session died while we were
 *	streaming (hours of audio never refresh the RPC session), so try one
 *	automatic reconnect and one more fetch before giving up and asking the user
 *	to reconnect.
 */
bool BarPlaybackFetchPlaylist (BarApp_t *app) {
	PianoReturn_t pRet = PIANO_RET_OK;
	CURLcode wRet = CURLE_OK;

	if (!BarPlaybackFetchPlaylistOnce (app, &pRet, &wRet)) {
		/* Station pointers do not survive the session teardown: keep our own
		 * copy of the id we were fetching so it can be resumed. */
		const PianoStation_t * const station = BarStateGetNextStation (app);
		char *resumeId = NULL;
		if (station != NULL && station->id != NULL) {
			resumeId = strdup (station->id);
		}

		if (wRet == CURLE_ABORTED_BY_CALLBACK) {
			BarStateSetNextStation (app, NULL);
		} else if (atomic_load_explicit (&app->doQuit, memory_order_relaxed)) {
			/* Interrupted or shutting down: leave the session alone. */
			BarStateSetNextStation (app, NULL);
		} else if (BarSessionTryAutoRecover (app, "playlist_session_error",
				resumeId)) {
			/* Session is healthy again.  Retry the fetch when a station was
			 * resumed; otherwise stay connected and let the user pick one. */
			if (BarStateGetNextStation (app) != NULL &&
					!BarPlaybackFetchPlaylistOnce (app, &pRet, &wRet)) {
				BarUiDoPandoraDisconnect (app, "playlist_session_error",
						resumeId);
			}
		} else {
			BarUiDoPandoraDisconnect (app, "playlist_session_error", resumeId);
		}

		free (resumeId);
	}

	PianoStation_t *nextStation = BarStateGetNextStation (app);
	BarStateSetCurrentStation (app, nextStation);
	BarUiStartEventCmd (&app->settings, "stationfetchplaylist",
	                    BarStateGetCurrentStation (app), BarStateGetPlaylist (app),
	                    &app->player, BarStateGetStationList (app), pRet, wRet);
	return BarStateGetPlaylist (app) != NULL;
}

/*	Start a player thread for the first song in app->playlist.
 *	Logic extracted from BarMainStartPlayback in main.c.
 */
bool BarPlaybackStartSong (BarApp_t *app, pthread_t *playerThread) {
	if (app == NULL || playerThread == NULL) {
		return false;
	}

	const PianoSong_t * const curSong = BarStateGetPlaylist (app);
	if (curSong == NULL) {
		return false;
	}

	PianoStation_t *curStation = BarStateGetCurrentStation (app);
	if (curStation == NULL) {
		return false;
	}

	BarUiPrintSong (&app->settings, curSong,
	                curStation->isQuickMix ?
	                BarStateFindStationById (app, curSong->stationId) : NULL);

	static const char httpPrefix[] = "http://";
	if (curSong->audioUrl == NULL ||
	    strncmp (curSong->audioUrl, httpPrefix, strlen (httpPrefix)) != 0) {
		BarUiMsg (&app->settings, MSG_ERR, "%s",
		          BarL10nGet (&app->l10n, "cli.invalid_song_url"));
		return false;
	}

	player_t * const player = &app->player;
	BarPlayerReset (player);

	app->player.url = curSong->audioUrl;
	app->player.gain = curSong->fileGain;
	app->player.songDuration = curSong->length;

	BarInterruptSetTarget (&app->player.interrupted);

	BarUiStartEventCmd (&app->settings, "songstart",
	                    curStation, curSong, &app->player,
	                    BarStateGetStationList (app), PIANO_RET_OK, CURLE_OK);

	BarWsBroadcastSongStart (app);

	/* Prevent race condition: mode must not be DEAD when thread starts */
	BarPlayerSetMode (&app->player, PLAYER_WAITING);
	if (pthread_create (playerThread, NULL, BarPlayerThread, &app->player) != 0) {
		BarInterruptSetTarget (&app->doQuit);
		BarPlayerSetMode (&app->player, PLAYER_DEAD);
		return false;
	}

	/* Playback is alive: re-arm auto-recovery for the next session failure. */
	atomic_store (&app->autoRecoverFailed, false);
	return true;
}
