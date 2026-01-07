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
#include <stdint.h>
#include <signal.h>

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

	/* private attributes _not_ protected by mutex */

	/* libav - decoder and filter chain */
	AVFilterGraph *fgraph;
	AVFormatContext *fctx;
	AVStream *st;
	AVCodecContext *cctx;
	AVFilterContext *fbufsink, *fabuf;
	int streamIdx;
	int64_t lastTimestamp;
	sig_atomic_t interrupted;

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

	/* settings (must be set before starting the thread) */
	double gain;
	char *url;
	const BarSettings_t *settings;
};

enum {PLAYER_RET_OK = 0, PLAYER_RET_HARDFAIL = 1, PLAYER_RET_SOFTFAIL = 2};

void *BarPlayerThread (void *data);
void BarPlayerSetVolume (player_t * const player);
void BarPlayerInit (player_t * const p, const BarSettings_t * const settings);
void BarPlayerReset (player_t * const p);
void BarPlayerDestroy (player_t * const p);
BarPlayerMode BarPlayerGetMode (player_t * const player);
bool BarPlayerIsPaused (player_t * const player);

