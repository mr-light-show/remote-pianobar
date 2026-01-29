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

/* Debug assertions for lock state (enabled with -DDEBUG) */
#ifdef DEBUG
#include <pthread.h>
#include <errno.h>

/* Assert that stateMutex is currently held by calling thread.
 * Only works in BOTH mode (when mutex is actually initialized).
 * In CLI-only mode or without WEBSOCKET_ENABLED, this is a no-op. */
#ifdef WEBSOCKET_ENABLED
#define ASSERT_STATE_LOCK_HELD(app) \
	do { \
		if ((app)->settings.uiMode == BAR_UI_MODE_BOTH) { \
			int _trylock_result = pthread_mutex_trylock(&(app)->stateMutex); \
			if (_trylock_result == 0) { \
				pthread_mutex_unlock(&(app)->stateMutex); \
				assert(0 && "stateMutex is NOT held (expected to be held)"); \
			} else { \
				assert(_trylock_result == EBUSY && "stateMutex should be held"); \
			} \
		} \
	} while (0)

/* Assert that stateMutex is NOT currently held by calling thread */
#define ASSERT_STATE_LOCK_NOT_HELD(app) \
	do { \
		if ((app)->settings.uiMode == BAR_UI_MODE_BOTH) { \
			int _trylock_result = pthread_mutex_trylock(&(app)->stateMutex); \
			assert(_trylock_result == 0 && "stateMutex is held (expected to be free)"); \
			if (_trylock_result == 0) { \
				pthread_mutex_unlock(&(app)->stateMutex); \
			} \
		} \
	} while (0)
#else
#define ASSERT_STATE_LOCK_HELD(app) ((void)0)
#define ASSERT_STATE_LOCK_NOT_HELD(app) ((void)0)
#endif

/* Assert that player.lock is currently held */
#define ASSERT_PLAYER_LOCK_HELD(player) \
	do { \
		int _trylock_result = pthread_mutex_trylock(&(player)->lock); \
		if (_trylock_result == 0) { \
			pthread_mutex_unlock(&(player)->lock); \
			assert(0 && "player.lock is NOT held (expected to be held)"); \
		} else { \
			assert(_trylock_result == EBUSY && "player.lock should be held"); \
		} \
	} while (0)

/* Assert that player.lock is NOT currently held */
#define ASSERT_PLAYER_LOCK_NOT_HELD(player) \
	do { \
		int _trylock_result = pthread_mutex_trylock(&(player)->lock); \
		assert(_trylock_result == 0 && "player.lock is held (expected to be free)"); \
		if (_trylock_result == 0) { \
			pthread_mutex_unlock(&(player)->lock); \
		} \
	} while (0)

/* Assert that player.decoderLock is currently held */
#define ASSERT_AOPLAY_LOCK_HELD(player) \
	do { \
		int _trylock_result = pthread_mutex_trylock(&(player)->decoderLock); \
		if (_trylock_result == 0) { \
			pthread_mutex_unlock(&(player)->decoderLock); \
			assert(0 && "decoderLock is NOT held (expected to be held)"); \
		} else { \
			assert(_trylock_result == EBUSY && "decoderLock should be held"); \
		} \
	} while (0)

/* Assert that player.decoderLock is NOT currently held */
#define ASSERT_AOPLAY_LOCK_NOT_HELD(player) \
	do { \
		int _trylock_result = pthread_mutex_trylock(&(player)->decoderLock); \
		assert(_trylock_result == 0 && "decoderLock is held (expected to be free)"); \
		if (_trylock_result == 0) { \
			pthread_mutex_unlock(&(player)->decoderLock); \
		} \
	} while (0)

#else
/* Release builds: assertions are no-ops */
#define ASSERT_STATE_LOCK_HELD(app) ((void)0)
#define ASSERT_STATE_LOCK_NOT_HELD(app) ((void)0)
#define ASSERT_PLAYER_LOCK_HELD(player) ((void)0)
#define ASSERT_PLAYER_LOCK_NOT_HELD(player) ((void)0)
#define ASSERT_AOPLAY_LOCK_HELD(player) ((void)0)
#define ASSERT_AOPLAY_LOCK_NOT_HELD(player) ((void)0)
#endif

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

/* Check if logged in to Pandora */
bool BarStateIsPandoraConnected(const BarApp_t *app);

