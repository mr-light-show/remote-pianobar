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

#pragma once

#include "config.h"

/* required for freebsd */
#include <sys/types.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <stdatomic.h>
#include <time.h>

#include "miniaudio.h"
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavcodec/avcodec.h>
#include <piano.h>

#include "settings.h"

typedef enum {
	/* not running */
	PLAYER_DEAD = 0,
	/* running, but not ready to play music yet */
	PLAYER_WAITING,
	/* currently playing a song */
	PLAYER_PLAYING,
	/* finished playing a song */
	PLAYER_FINISHED,
} BarPlayerMode;

typedef enum {
	PLAYER_AUDIO_NONE = 0,
	PLAYER_AUDIO_STOPPED,
	PLAYER_AUDIO_RUNNING,
	PLAYER_AUDIO_FAILED,
} BarPlayerAudioState;

typedef struct {
	BarPlayerAudioState state;
	bool playing, atEnd, deviceStarted;
	ma_uint64 cursorFrames;
	float cursorSeconds;
} BarPlayerAudioSnapshot;

/* Immutable result of a successful completed play/pause control. Internal
 * epoch orders publication; it is not part of the WebSocket payload. */
typedef struct {
	bool paused;
	uint64_t controlEpoch;
} BarPlayerPlayStateSnapshot;

/* Forward declaration */
typedef struct player player_t;

/* Custom data source that wraps ffmpeg decoding for miniaudio */
typedef struct {
	ma_data_source_base base;      /* Must be first member */
	player_t *player;              /* Reference back to player for ffmpeg state */
	ma_uint64 cursor;              /* Current position in PCM frames */
	ma_uint64 totalFrames;         /* Total length in PCM frames */
	ma_uint32 sampleRate;          /* Sample rate for this stream */
	ma_uint32 channels;            /* Number of channels */
	bool reachedEnd;               /* Whether we've reached EOF from ffmpeg */
	
	/* Buffering for partial frame consumption */
	AVFrame *bufferedFrame;        /* Current frame being consumed (NULL if none) */
	int bufferedFrameOffset;       /* Offset into buffered frame in samples */
} ffmpeg_data_source_t;

struct player {
	/* public attributes protected by mutex */
	pthread_mutex_t lock;
	pthread_cond_t cond;           /* broadcast mode changes */
	bool doQuit, doPause;

	/* measured in seconds */
	unsigned int songDuration;
	unsigned int songPlayed;

	/* Pause timeout tracking */
	time_t pauseStartTime;  /* When pause began (0 = not paused or timer cleared) */

	BarPlayerMode mode;

	/* Audio reservation, diagnostics and runtime volume: protected by lock.
	 * audioState is stable only when audioBusy is false. No miniaudio call
	 * may hold lock; callbacks are the only non-reserving sound consumers. */
	pthread_cond_t audioCond;
	bool audioBusy, audioOwnerValid;
	BarPlayerAudioState audioState;
	uint64_t controlEpoch;
	unsigned int audioControlWaiters;
	pthread_t audioOwner;
	struct timespec audioBusySince;
	const char *audioOperation;
	int requestedVolume;
	bool synchronizationInitialized, audioNoDevice, dataSourceInitialized;
	bool audioTerminalFailure;
	bool threadJoinPending; /* lock: set before pthread_create, cleared only after join */

	/* Private decoder state and audio objects. Audio objects require the
	 * reservation; decoder/filter data require decoderLock while callbacks run. */

	/* libav - decoder and filter chain */
	AVFilterGraph *fgraph;
	AVFormatContext *fctx;
	AVStream *st;
	AVCodecContext *cctx;
	AVFilterContext *fbufsink, *fabuf;
	int streamIdx;
	int64_t lastTimestamp;
	_Atomic sig_atomic_t interrupted;

	/* miniaudio - high-level engine and sound */
	ma_engine engine;
	ma_sound sound;
	ffmpeg_data_source_t dataSource;
	bool engineInitialized;
	bool soundInitialized;

	/* Decoder thread synchronization */
	pthread_mutex_t decoderLock;   /* Protects ffmpeg filter chain access */
	pthread_cond_t decoderCond;    /* Signals when new data is available */
	bool decodingFinished;         /* Set when decoder reaches EOF */
	bool sourceReadCancelled;      /* decoderLock: wake/abort reads before device stop */

	/* settings (must be set before starting the thread) */
	double gain;
	char *url;
	BarSettings_t *settings;
};

enum {PLAYER_RET_OK = 0, PLAYER_RET_HARDFAIL = 1, PLAYER_RET_SOFTFAIL = 2,
	/* Stream open failed with HTTP 403 (expired CDN URL); clear playlist and refetch */
	PLAYER_RET_STALE_URLS = 3};

/* True if FFmpeg av_err from avformat_open_input is HTTP 403 (stale track URL). */
bool BarIsAvErrStaleCdnUrl(int av_err);

void *BarPlayerThread (void *data);
/* ffmpeg interrupt callback; tests call this directly to cover skip vs quit. */
int BarPlayerFfmpegInterruptCb (void *data);
bool BarPlayerSetVolume (player_t *player, int requestedVolume);
bool BarPlayerAdjustVolume (player_t *player, int delta);
int BarPlayerGetVolume (player_t *player);
void BarPlayerInit (player_t * const p, BarSettings_t * const settings);
bool BarPlayerReset (player_t * const p);
bool BarPlayerDestroy (player_t * const p);
/* Skips a sample when any control operation owns or is waiting for audio. */
bool BarPlayerGetAudioSnapshot (player_t *player, BarPlayerAudioSnapshot *snapshot);
/* Physical stop infrastructure. Does not change logical pause/quit flags. */
bool BarPlayerStopAudio (player_t *player);
/* Default hook terminates the process. Tests may install a returning hook
 * before starting workers; return remains terminal and never permits teardown. */
typedef void (*BarPlayerAudioFatalHook) (player_t *player, const char *operation);
void BarPlayerSetAudioFatalTestHook (BarPlayerAudioFatalHook hook);
/* Bounded OS join clears pending lifetime only after success.
 * Install test hooks before creating workers. */
typedef bool (*BarPlayerJoinTestHook) (pthread_t thread, void **retval, unsigned int timeoutSeconds);
void BarPlayerSetJoinTestHook (BarPlayerJoinTestHook hook);
bool BarPlayerJoinThreadWithTimeout (player_t *player, pthread_t thread, void **retval, unsigned int timeoutSeconds);
/* Leaves all shared resources intact, even when a test fatal hook returns. */
void BarPlayerFatalShutdown (player_t *player, const char *operation);
BarPlayerMode BarPlayerGetMode (player_t * const player);
void BarPlayerSetMode (player_t * const player, BarPlayerMode mode);
bool BarPlayerIsPaused (player_t * const player);
/* Serialize logical controls and retained sound/device transitions. */
bool BarPlayerSetPaused (player_t *player, bool paused);
bool BarPlayerTogglePaused (player_t *player, bool *paused);
/* Capture under player.lock at successful commit, before releasing audio.
 * A failed/superseded control leaves the optional snapshot unchanged. */
bool BarPlayerSetPausedWithSnapshot (player_t *player, bool paused,
		BarPlayerPlayStateSnapshot *snapshot);
bool BarPlayerTogglePausedWithSnapshot (player_t *player,
		BarPlayerPlayStateSnapshot *snapshot);
/* Publish quit before waiting for the physical stop reservation. */
void BarPlayerRequestStop (player_t *player);

/*
 * Block until player->mode == mode or timeoutMs elapses.
 * Returns true if the mode was reached; false on timeout or NULL player.
 * Uses CLOCK_REALTIME for the timed wait.
 */
bool BarPlayerWaitForMode (player_t * const player,
                            BarPlayerMode mode,
                            unsigned int timeoutMs);
