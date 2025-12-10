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

/* Playback state machine - runs in dedicated thread for WebSocket modes */

#include "playback_manager.h"
#include "bar_state.h"
#include "ui.h"
#include "player.h"
#include "debug.h"
#include "websocket_bridge.h"

#include <unistd.h>
#include <assert.h>
#include <signal.h>

/* Forward declarations of functions from main.c */
extern void BarMainGetPlaylist(BarApp_t *app);
extern void BarMainStartPlayback(BarApp_t *app, pthread_t *playerThread);
extern sig_atomic_t *interrupted;

static pthread_t g_playbackThread;
static volatile bool g_running = false;

/*	Player cleanup after song finishes
 */
static void PlaybackManagerPlayerCleanup(BarApp_t *app, pthread_t *playerThread) {
	void *threadRet;

	BarUiStartEventCmd(&app->settings, "songfinish", BarStateGetCurrentStation(app),
			BarStateGetPlaylist(app), &app->player, BarStateGetStationList(app), 
			PIANO_RET_OK, CURLE_OK);

	BarWsBroadcastSongStop(app);

	/* Wait for player thread to complete */
	pthread_join(*playerThread, &threadRet);

	if (threadRet == (void *) PLAYER_RET_OK) {
		app->playerErrors = 0;
	} else if (threadRet == (void *) PLAYER_RET_SOFTFAIL) {
		++app->playerErrors;
		if (app->playerErrors >= app->settings.maxRetry) {
			/* don't continue playback if thread reports too many errors */
			BarStateSetNextStation(app, NULL);
		}
	} else {
		BarStateSetNextStation(app, NULL);
	}

	/* In WebSocket mode, interrupted may be temporarily changed by readline */
	interrupted = &app->doQuit;

	app->player.mode = PLAYER_DEAD;
}

/*	Playback manager thread - runs the playback state machine
 */
static void *BarPlaybackManagerThread(void *data) {
	BarApp_t *app = (BarApp_t *)data;
	pthread_t playerThread;
	
	debugPrint(DEBUG_UI, "PlaybackMgr: Thread started\n");
	
	while (!app->doQuit && g_running) {
		BarPlayerMode mode = BarPlayerGetMode(&app->player);
		
		/* Song finished playing - cleanup */
		if (mode == PLAYER_FINISHED) {
			debugPrint(DEBUG_UI, "PlaybackMgr: Song finished\n");
			
			/* Check for interrupt */
			pthread_mutex_lock(&app->player.lock);
			if (app->player.interrupted != 0) {
				app->doQuit = 1;
			}
			pthread_mutex_unlock(&app->player.lock);
			
			PlaybackManagerPlayerCleanup(app, &playerThread);
		}
		
		/* Player idle - check for next song */
		if (mode == PLAYER_DEAD) {
			debugPrint(DEBUG_UI, "PlaybackMgr: Player idle\n");
			
			/* Advance playlist */
			PianoSong_t *playlist = BarStateGetPlaylist(app);
			if (playlist != NULL) {
				PianoSong_t *histsong = playlist;
				BarStateSetPlaylist(app, PianoListNextP(playlist));
				histsong->head.next = NULL;
				BarUiHistoryPrepend(app, histsong);
			}
			
			/* Fetch more songs if needed */
			playlist = BarStateGetPlaylist(app);
			PianoStation_t *nextStation = BarStateGetNextStation(app);
			
			if (playlist == NULL && nextStation != NULL && !app->doQuit) {
				PianoStation_t *curStation = BarStateGetCurrentStation(app);
				
				if (nextStation != curStation) {
					BarUiPrintStation(&app->settings, nextStation);
				}
				
				/* Fetch playlist from Pandora */
				BarMainGetPlaylist(app);
				
				playlist = BarStateGetPlaylist(app);
			}
			
			/* Start next song */
			if (playlist != NULL) {
				debugPrint(DEBUG_UI, "PlaybackMgr: Starting next song\n");
				BarMainStartPlayback(app, &playerThread);
				BarWsBroadcastSongStart(app);
			}
		}
		
		/* Sleep briefly to avoid busy-wait */
		usleep(100000);  // 100ms
	}
	
	/* Cleanup if player still running */
	if (BarPlayerGetMode(&app->player) != PLAYER_DEAD) {
		debugPrint(DEBUG_UI, "PlaybackMgr: Waiting for player to finish\n");
		pthread_join(playerThread, NULL);
	}
	
	debugPrint(DEBUG_UI, "PlaybackMgr: Thread stopped\n");
	return NULL;
}

/*	Start playback manager thread
 */
bool BarPlaybackManagerStart(BarApp_t *app) {
	assert(app != NULL);
	
	debugPrint(DEBUG_UI, "PlaybackMgr: Starting playback manager thread\n");
	
	g_running = true;
	if (pthread_create(&g_playbackThread, NULL, BarPlaybackManagerThread, app) != 0) {
		fprintf(stderr, "Error: Failed to create playback manager thread\n");
		g_running = false;
		return false;
	}
	
	return true;
}

/*	Stop playback manager thread
 */
void BarPlaybackManagerStop(BarApp_t *app) {
	assert(app != NULL);
	
	debugPrint(DEBUG_UI, "PlaybackMgr: Stopping playback manager thread\n");
	
	g_running = false;
	pthread_join(g_playbackThread, NULL);
	
	debugPrint(DEBUG_UI, "PlaybackMgr: Thread joined\n");
}

