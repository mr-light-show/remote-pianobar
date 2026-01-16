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

/* Enable POSIX functions (pthread_kill) and BSD/GNU extensions (usleep) */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

/* Playback state machine - runs in dedicated thread for WebSocket modes */

#include "playback_manager.h"
#include <signal.h>
#include <unistd.h>
#include "bar_state.h"
#include "ui.h"
#include "player.h"
#include "debug.h"
#include "websocket_bridge.h"

#include <assert.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>

/* Forward declarations of functions from main.c */
extern void BarMainGetPlaylist(BarApp_t *app);
extern void BarMainStartPlayback(BarApp_t *app, pthread_t *playerThread);
extern sig_atomic_t *interrupted;

/* Forward declaration from ui_act.c (avoid circular include with ui_act.h) */
extern void BarUiDoPandoraDisconnect(BarApp_t *app, const char *reason);

static pthread_t g_playbackThread;
static volatile bool g_running = false;

/*	Join thread with timeout - prevents deadlock if player hangs on network
 *	Returns true if thread joined successfully, false if timeout expired
 */
static bool join_thread_with_timeout(pthread_t thread, void **retval, int timeout_secs) {
	for (int i = 0; i < timeout_secs * 10; i++) {
		/* Check if thread is still alive using pthread_kill with signal 0 */
		int ret = pthread_kill(thread, 0);
		if (ret == ESRCH) {
			/* Thread no longer exists - join to clean up */
			pthread_join(thread, retval);
			return true;
		} else if (ret != 0) {
			/* Error checking thread status */
			debugPrint(DEBUG_UI, "PlaybackMgr: pthread_kill error %d\n", ret);
			return false;
		}
		usleep(100000);  /* 100ms */
	}
	return false;  /* Timeout */
}

/*	Player cleanup after song finishes
 */
static void PlaybackManagerPlayerCleanup(BarApp_t *app, pthread_t *playerThread) {
	void *threadRet = (void *)PLAYER_RET_HARDFAIL;

	BarUiStartEventCmd(&app->settings, "songfinish", BarStateGetCurrentStation(app),
			BarStateGetPlaylist(app), &app->player, BarStateGetStationList(app), 
			PIANO_RET_OK, CURLE_OK);

	BarWsBroadcastSongStop(app);

	/* Wait for player thread to complete with timeout to prevent deadlock */
	if (!join_thread_with_timeout(*playerThread, &threadRet, 10)) {
		debugPrint(DEBUG_UI, "PlaybackMgr: WARNING - player thread did not exit within 10s\n");
		
		/* Force interrupt and try again */
		pthread_mutex_lock(&app->player.lock);
		app->player.interrupted = 2;
		app->player.doQuit = true;
		pthread_cond_broadcast(&app->player.cond);
		pthread_mutex_unlock(&app->player.lock);
		
		if (!join_thread_with_timeout(*playerThread, &threadRet, 5)) {
			debugPrint(DEBUG_UI, "PlaybackMgr: ERROR - player thread hung, detaching\n");
			pthread_detach(*playerThread);  /* Accept the leak to avoid deadlock */
			threadRet = (void *)PLAYER_RET_HARDFAIL;
		}
	}

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

	pthread_mutex_lock(&app->player.lock);
	app->player.mode = PLAYER_DEAD;
	pthread_mutex_unlock(&app->player.lock);
}

/*	Playback manager thread - runs the playback state machine
 */
static void *BarPlaybackManagerThread(void *data) {
	BarApp_t *app = (BarApp_t *)data;
	pthread_t playerThread;
	static time_t lastProgressBroadcast = 0;
	
	debugPrint(DEBUG_UI, "PlaybackMgr: Thread started\n");
	
	while (!app->doQuit && g_running) {
		/* Use timed wait instead of polling - wake up on mode changes or after 1 second */
		struct timespec timeout;
		clock_gettime(CLOCK_REALTIME, &timeout);
		timeout.tv_sec += 1;  /* 1 second timeout for progress broadcast */
		
		pthread_mutex_lock(&app->player.lock);
		/* Wait for mode change or timeout (whichever comes first) */
		pthread_cond_timedwait(&app->player.cond, &app->player.lock, &timeout);
		/* Cache mode and pause state while holding lock */
		BarPlayerMode mode = app->player.mode;
		bool isPaused = app->player.doPause;
		pthread_mutex_unlock(&app->player.lock);
		
		/* Broadcast progress updates every ~1 second while playing (and not paused) */
		{
			time_t now = time(NULL);
			if ((now - lastProgressBroadcast) >= 1) {
				lastProgressBroadcast = now;
				
				/* Check if playing AND not paused */
				if (mode == PLAYER_PLAYING && !isPaused) {
					BarWsBroadcastProgress(app);
				}
			}
		}
		
		/* Check for pause timeout (auto-stop after configured minutes of pause) */
		if (app->settings.pauseTimeout > 0) {
			/* Only need to check pauseStartTime if actually paused */
			time_t pauseStart = 0;
			if (isPaused) {
				pthread_mutex_lock(&app->player.lock);
				pauseStart = app->player.pauseStartTime;
				pthread_mutex_unlock(&app->player.lock);
			}
			
			if (isPaused && pauseStart > 0) {
				time_t elapsed = time(NULL) - pauseStart;
				if (elapsed >= (time_t)(app->settings.pauseTimeout * 60)) {
					debugPrint(DEBUG_UI, "PlaybackMgr: Pause timeout expired (%u minutes), stopping\n",
					           app->settings.pauseTimeout);
					BarUiDoPandoraDisconnect(app, "idle_timeout");
				}
			}
		}
		
	/* Song finished playing - cleanup */
	if (mode == PLAYER_FINISHED) {
		debugPrint(DEBUG_UI, "PlaybackMgr: Song finished\n");
		
		/* Only quit if app->doQuit was already set (explicit quit command or SIGINT).
		 * Skip/disconnect operations set player.interrupted but should NOT quit the app.
		 * This prevents disconnect (power button) from terminating the process. */
		pthread_mutex_lock(&app->player.lock);
		if (app->player.interrupted != 0 && app->doQuit) {
			/* User pressed Ctrl+C during playback - already quitting */
			debugPrint(DEBUG_UI, "PlaybackMgr: Interrupt detected during quit\n");
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
	}
	
	/* Cleanup if player still running */
	if (BarPlayerGetMode(&app->player) != PLAYER_DEAD) {
		debugPrint(DEBUG_UI, "PlaybackMgr: Waiting for player to finish\n");
		if (!join_thread_with_timeout(playerThread, NULL, 10)) {
			debugPrint(DEBUG_UI, "PlaybackMgr: Force stopping hung player\n");
			pthread_mutex_lock(&app->player.lock);
			app->player.interrupted = 2;
			app->player.doQuit = true;
			pthread_cond_broadcast(&app->player.cond);
			pthread_mutex_unlock(&app->player.lock);
			
			if (!join_thread_with_timeout(playerThread, NULL, 5)) {
				debugPrint(DEBUG_UI, "PlaybackMgr: Detaching hung player thread\n");
				pthread_detach(playerThread);
			}
		}
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

