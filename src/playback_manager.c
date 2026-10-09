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
#include "playback_lifecycle.h"
#include "interrupt.h"
#include "bar_constants.h"
#include <stdatomic.h>
#include <signal.h>
#include <unistd.h>
#include "bar_state.h"
#include "ui.h"
#include "player.h"
#include "log.h"
#include "websocket_bridge.h"

#include <assert.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>

/* Timeout constants */
#define PLAYER_JOIN_TIMEOUT_SECS 10
#define PLAYER_FORCE_JOIN_TIMEOUT_SECS 5
#define PROGRESS_BROADCAST_INTERVAL_SECS 1

/* Forward declaration from ui_act.c (avoid circular include with ui_act.h) */
extern void BarUiDoPandoraDisconnect(BarApp_t *app, const char *reason,
		const char *resume_station_id_override);

static pthread_t g_playbackThread;
static _Atomic bool g_running = false;
static _Atomic bool g_idleLogged = false;
static _Atomic bool g_parkedLogged = false;
static BarPlaybackManagerWaitParkedIdleTestHook_fn g_waitParkedIdleTestHook = NULL;

void BarPlaybackManagerWaitParkedIdleSetTestHook(
		BarPlaybackManagerWaitParkedIdleTestHook_fn hook) {
	g_waitParkedIdleTestHook = hook;
}

void BarPlaybackManagerWaitParkedIdleClearTestHook(void) {
	g_waitParkedIdleTestHook = NULL;
}

bool BarPlaybackShouldParkIdle(const BarApp_t *app)
{
	BarPlayerMode mode = BarPlayerGetMode((player_t *)&app->player);
	if (mode != PLAYER_DEAD) {
		return false;
	}
	return BarStateGetPlaylist(app) == NULL
	    && BarStateGetNextStation(app) == NULL;
}

BarPlaybackWait BarPlaybackManagerSelectWait (const player_t *player,
		bool parkIdle, unsigned int pauseTimeout, bool stopping,
		struct timespec now, struct timespec *deadline) {
	if (stopping || player->mode == PLAYER_FINISHED) {
		return BAR_PLAYBACK_WAIT_READY;
	}
	if (player->mode == PLAYER_DEAD) {
		return parkIdle ? BAR_PLAYBACK_WAIT_INDEFINITE : BAR_PLAYBACK_WAIT_READY;
	}
	if (player->doQuit) {
		/* Skip already requested: wait for the worker's terminal mode wake. */
		return BAR_PLAYBACK_WAIT_INDEFINITE;
	}
	now.tv_sec += now.tv_nsec / 1000000000L;
	now.tv_nsec %= 1000000000L;
	if (now.tv_nsec < 0) {
		--now.tv_sec;
		now.tv_nsec += 1000000000L;
	}
	if (player->doPause) {
		if (pauseTimeout == 0 || player->pauseStartTime <= 0) {
			return BAR_PLAYBACK_WAIT_INDEFINITE;
		}
		*deadline = (struct timespec) {
			player->pauseStartTime + (time_t) pauseTimeout * 60, 0
		};
		return now.tv_sec >= deadline->tv_sec ? BAR_PLAYBACK_WAIT_PAUSE_EXPIRED :
				BAR_PLAYBACK_WAIT_TIMED;
	}
	*deadline = now;
	deadline->tv_sec += PROGRESS_BROADCAST_INTERVAL_SECS;
	return BAR_PLAYBACK_WAIT_TIMED;
}

bool BarPlaybackManagerWaitParkedIdle(const BarApp_t *app, unsigned int timeoutMs) {
	if (g_waitParkedIdleTestHook != NULL) {
		return g_waitParkedIdleTestHook(app, timeoutMs);
	}

	if (!atomic_load(&g_running)) {
		return true;
	}

	unsigned int elapsed = 0;
	while (elapsed < timeoutMs) {
		if (BarPlaybackShouldParkIdle(app)) {
			return true;
		}
		usleep((unsigned int)BAR_PLAYER_STOP_POLL_MS * 1000u);
		elapsed += BAR_PLAYER_STOP_POLL_MS;
	}
	return BarPlaybackShouldParkIdle(app);
}

/*	Force join player thread with timeout, interrupt if needed
 *	A returning test fatal hook leaves the app terminal and intact.
 */
static bool force_join_player_thread(BarApp_t *app, pthread_t *playerThread, 
                                      void **retval, const char *context) {
	if (!BarPlayerJoinThreadWithTimeout (&app->player, *playerThread, retval, PLAYER_JOIN_TIMEOUT_SECS)) {
		log_write(DEBUG_UI, "PlaybackMgr: WARNING - %s did not exit within %ds\n",
		           context, PLAYER_JOIN_TIMEOUT_SECS);
		
		/* Force interrupt and try again */
		atomic_store (&app->player.interrupted, 2);
		BarPlayerRequestStop (&app->player);
		
		if (!BarPlayerJoinThreadWithTimeout (&app->player, *playerThread, retval, PLAYER_FORCE_JOIN_TIMEOUT_SECS)) {
			BarPlayerFatalShutdown (&app->player, context);
			atomic_store (&app->doQuit, 1);
			return false;
		}
	}
	return true;
}

/*	Player cleanup after song finishes
 */
static bool PlaybackManagerPlayerCleanup(BarApp_t *app, pthread_t *playerThread) {
	void *threadRet = (void *)PLAYER_RET_HARDFAIL;

	BarUiStartEventCmd(&app->settings, "songfinish", BarStateGetCurrentStation(app),
			BarStateGetPlaylist(app), &app->player, BarStateGetStationList(app), 
			PIANO_RET_OK, CURLE_OK);

	BarWsBroadcastSongStop(app);

	/* Wait for player thread to complete with timeout to prevent deadlock */
	if (!force_join_player_thread(app, playerThread, &threadRet, "player thread")) {
		return false;
	}

	if (threadRet == (void *) PLAYER_RET_OK) {
		app->playerErrors = 0;
	} else if (threadRet == (void *) PLAYER_RET_STALE_URLS) {
		BarStateDrainPlaylist(app);
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

	/* Restore interrupt target to app->doQuit after song finishes */
	BarInterruptSetTarget (&app->doQuit);

	BarPlayerSetMode (&app->player, PLAYER_DEAD);
	return true;
}

BarPlayerMode BarPlaybackManagerRefreshCachedModeAfterCleanup(
	const BarApp_t *app, BarPlayerMode cached_mode) {
	(void)cached_mode;
	return BarPlayerGetMode((player_t *)&app->player);
}

BarPlayerMode BarPlaybackManagerCompleteSongCleanup(
	BarApp_t *app, pthread_t *playerThread, bool *playerStarted, BarPlayerMode mode) {
	log_write(DEBUG_UI, "PlaybackMgr: Song finished\n");

	/* Only quit if app->doQuit was already set (explicit quit command or SIGINT).
	 * Skip/disconnect operations set player.interrupted but should NOT quit the app.
	 * This prevents disconnect (power button) from terminating the process. */
	pthread_mutex_lock(&app->player.lock);
	if (atomic_load_explicit (&app->player.interrupted, memory_order_relaxed) != 0) {
		if (atomic_load_explicit (&app->doQuit, memory_order_relaxed)) {
			log_write(DEBUG_UI, "PlaybackMgr: Interrupt detected during quit\n");
		}
	}
	pthread_mutex_unlock(&app->player.lock);

	if (!PlaybackManagerPlayerCleanup(app, playerThread)) { return mode; }
	*playerStarted = false;
	return BarPlaybackManagerRefreshCachedModeAfterCleanup(app, mode);
}

BarPlayerMode BarPlaybackManagerHandleFinishedMode(
	BarApp_t *app, pthread_t *playerThread, bool *playerStarted, BarPlayerMode mode) {
	if (mode == PLAYER_FINISHED) {
		return BarPlaybackManagerCompleteSongCleanup(app, playerThread,
		                                             playerStarted, mode);
	}
	return mode;
}

/*	Playback manager thread - runs the playback state machine
 */
static void *BarPlaybackManagerThread(void *data) {
	BarApp_t *app = (BarApp_t *)data;
	pthread_t playerThread = 0;
	bool playerStarted = false;
	time_t lastProgressBroadcast = 0;
	
	log_write(DEBUG_UI, "PlaybackMgr: Thread started\n");
	
	while (!atomic_load_explicit (&app->doQuit, memory_order_relaxed) && atomic_load_explicit (&g_running, memory_order_relaxed)) {
		const bool park_idle = BarPlaybackShouldParkIdle(app);

		pthread_mutex_lock(&app->player.lock);
		struct timespec waitNow, deadline;
		clock_gettime (CLOCK_REALTIME, &waitNow);
		bool stopping = atomic_load_explicit (&app->doQuit, memory_order_relaxed) ||
				!atomic_load_explicit (&g_running, memory_order_relaxed);
		BarPlaybackWait wait = BarPlaybackManagerSelectWait (&app->player,
				park_idle, app->settings.pauseTimeout, stopping, waitNow, &deadline);
		if (wait == BAR_PLAYBACK_WAIT_INDEFINITE) {
			if (park_idle && app->player.mode == PLAYER_DEAD && !atomic_load(&g_parkedLogged)) {
				atomic_store(&g_parkedLogged, true);
				log_write(DEBUG_UI, "PlaybackMgr: Parked (waiting for station)\n");
			}
			pthread_cond_wait(&app->player.cond, &app->player.lock);
		} else {
			atomic_store(&g_parkedLogged, false);
			if (wait == BAR_PLAYBACK_WAIT_TIMED) {
				pthread_cond_timedwait (&app->player.cond, &app->player.lock, &deadline);
			}
		}
		/* Every wake, including broadcasts, must recheck the current controls. */
		clock_gettime (CLOCK_REALTIME, &waitNow);
		stopping = atomic_load_explicit (&app->doQuit, memory_order_relaxed) ||
				!atomic_load_explicit (&g_running, memory_order_relaxed);
		wait = BarPlaybackManagerSelectWait (&app->player, park_idle,
				app->settings.pauseTimeout, stopping, waitNow, &deadline);
		BarPlayerMode mode = app->player.mode;
		bool isPaused = app->player.doPause;
		pthread_mutex_unlock(&app->player.lock);
		if (stopping) { break; }
		
		/* Broadcast progress updates every ~1 second while playing (and not paused) */
		{
			time_t now = time(NULL);
			if ((now - lastProgressBroadcast) >= PROGRESS_BROADCAST_INTERVAL_SECS) {
				lastProgressBroadcast = now;
				
				/* Check if playing AND not paused */
				if (mode == PLAYER_PLAYING && !isPaused) {
					BarWsBroadcastProgress(app);
				}
			}
		}
		
		/* Check for pause timeout (auto-stop after configured minutes of pause) */
		if (wait == BAR_PLAYBACK_WAIT_PAUSE_EXPIRED) {
			log_write(DEBUG_UI, "PlaybackMgr: Pause timeout expired (%u minutes), stopping\n",
					app->settings.pauseTimeout);
			BarUiDoPandoraDisconnect(app, "idle_timeout", NULL);
			continue;
		}
		
		/* Song finished playing - cleanup */
		mode = BarPlaybackManagerHandleFinishedMode(app, &playerThread,
		                                            &playerStarted, mode);
		
		/* Player idle - check for next song */
		if (mode == PLAYER_DEAD) {
			if (!g_idleLogged) {
				g_idleLogged = true;
				log_write(DEBUG_UI, "PlaybackMgr: Player idle\n");
			}
			
			/* Advance playlist (atomic under state lock — safe vs DrainPlaylist) */
			PianoSong_t *finished = BarStateAdvancePlaylist(app);
			if (finished != NULL) {
				BarUiHistoryPrepend(app, finished);
			}

			/* Fetch more songs if needed */
			PianoSong_t *playlist = BarStateGetPlaylist(app);
			PianoStation_t *nextStation = BarStateGetNextStation(app);
			
			if (playlist == NULL && nextStation != NULL && !atomic_load_explicit (&app->doQuit, memory_order_relaxed)) {
				PianoStation_t *curStation = BarStateGetCurrentStation(app);
				
				if (nextStation != curStation) {
					BarUiPrintStation(&app->settings, nextStation);
				}
				
			/* Fetch playlist from Pandora */
			BarPlaybackFetchPlaylist (app);

			playlist = BarStateGetPlaylist(app);
		}
		
		/* Start next song */
		if (playlist != NULL) {
			g_idleLogged = false;  /* log "Player idle" once when we next become idle */
			log_write(DEBUG_UI, "PlaybackMgr: Starting next song\n");
			BarPlaybackStartSong (app, &playerThread);
			playerStarted = true;
		}
		}
	}
	
	/* Cleanup if player still running */
	if (playerStarted && BarPlayerGetMode(&app->player) != PLAYER_DEAD) {
		log_write(DEBUG_UI, "PlaybackMgr: Waiting for player to finish\n");
		force_join_player_thread(app, &playerThread, NULL, "player thread (shutdown)");
	}
	
	log_write(DEBUG_UI, "PlaybackMgr: Thread stopped\n");
	return NULL;
}

/*	Start playback manager thread
 */
bool BarPlaybackManagerStart(BarApp_t *app) {
	assert(app != NULL);
	
	log_write(DEBUG_UI, "PlaybackMgr: Starting playback manager thread\n");
	
	g_running = true;
	if (pthread_create(&g_playbackThread, NULL, BarPlaybackManagerThread, app) != 0) {
		log_write(LOG_ERROR, "Failed to create playback manager thread\n");
		g_running = false;
		return false;
	}
	
	return true;
}

/*	Stop playback manager thread
 */
void BarPlaybackManagerStop(BarApp_t *app) {
	assert(app != NULL);
	
	log_write(DEBUG_UI, "PlaybackMgr: Stopping playback manager thread\n");
	
	g_running = false;
	
	/* Wake the thread if it's waiting on condition variable */
	pthread_mutex_lock(&app->player.lock);
	pthread_cond_broadcast(&app->player.cond);
	pthread_mutex_unlock(&app->player.lock);
	
	pthread_join(g_playbackThread, NULL);
	
	log_write(DEBUG_UI, "PlaybackMgr: Thread joined\n");
}
