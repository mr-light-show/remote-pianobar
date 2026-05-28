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
#include <stdbool.h>

/* True when playlist/station pointers are shared across threads (web and both; not cli). */
static inline bool BarStateUsesRwlock(const BarApp_t *app) {
#ifdef WEBSOCKET_ENABLED
	return app != NULL && app->settings.uiMode != BAR_UI_MODE_CLI;
#else
	(void)app;
	return false;
#endif
}

/* Debug assertions for lock state (enabled with -DDEBUG) */
#ifdef DEBUG
#include <pthread.h>
#include <errno.h>

/* Assert that stateRwlock is held by some thread (cannot distinguish calling thread with rwlock).
 * Only when BarStateUsesRwlock. Uses trywrlock: EBUSY => someone holds it; 0 => was free, unlock and fail. */
#ifdef WEBSOCKET_ENABLED
#define ASSERT_STATE_LOCK_HELD(app) \
	do { \
		if (BarStateUsesRwlock(app)) { \
			int _trylock_result = pthread_rwlock_trywrlock((pthread_rwlock_t *)&(app)->stateRwlock); \
			if (_trylock_result == 0) { \
				pthread_rwlock_unlock((pthread_rwlock_t *)&(app)->stateRwlock); \
				assert(0 && "stateRwlock is NOT held (expected to be held)"); \
			} else { \
				assert(_trylock_result == EBUSY && "stateRwlock should be held"); \
			} \
		} \
	} while (0)

/* Assert that stateRwlock is NOT currently held (trywrlock succeeds => was free; then unlock). */
#define ASSERT_STATE_LOCK_NOT_HELD(app) \
	do { \
		if (BarStateUsesRwlock(app)) { \
			int _trylock_result = pthread_rwlock_trywrlock((pthread_rwlock_t *)&(app)->stateRwlock); \
			assert(_trylock_result == 0 && "stateRwlock is held (expected to be free)"); \
			if (_trylock_result == 0) { \
				pthread_rwlock_unlock((pthread_rwlock_t *)&(app)->stateRwlock); \
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
#define ASSERT_DECODER_LOCK_HELD(player) \
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
#define ASSERT_DECODER_LOCK_NOT_HELD(player) \
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
#define ASSERT_DECODER_LOCK_HELD(player) ((void)0)
#define ASSERT_DECODER_LOCK_NOT_HELD(player) ((void)0)
#endif

/* Snapshot types — heap-allocated copies of state fields; safe to use without holding any lock */

typedef struct {
	char *id;
	char *name;
	char *displayName;
	bool  isQuickMix;
	bool  isQuickMixed; /* useQuickMix — included in QuickMix */
} BarStationSnapshot_t;

typedef struct {
	BarStationSnapshot_t *items;
	size_t                count;
} BarStationSnapshotList_t;

typedef struct {
	char        *stationId;
	char        *stationName;
	char        *songTitle;
	char        *songArtist;
	char        *songAlbum;
	char        *songCoverArt;
	char        *trackToken;
	char        *songStationId;   /* song->stationId for songStationName lookup */
	unsigned int duration;
	int          rating;
	bool         hasSong;
	bool         hasStation;
} BarPlaybackSnapshot_t;

/* Copy all station fields under read lock; caller frees with BarStateFreeStationSnapshot. */
bool BarStateSnapshotStations (const BarApp_t *app, BarStationSnapshotList_t *out);
void BarStateFreeStationSnapshot (BarStationSnapshotList_t *snap);

/* Copy current playback fields (station + song) under lock; caller frees with BarStateFreePlaybackSnapshot. */
void BarStateSnapshotPlayback (const BarApp_t *app, BarPlaybackSnapshot_t *out);
void BarStateFreePlaybackSnapshot (BarPlaybackSnapshot_t *snap);

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

/* Pandora HTTP/API: use BarUiPianoCall (serialized via app->pianoHttpMutex); see THREAD_SAFETY.md */

/* Check if logged in to Pandora */
bool BarStateIsPandoraConnected(const BarApp_t *app);

