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

/*
 * Audio playback using miniaudio's custom data source API.
 *
 * Architecture:
 * - BarPlayerThread: Opens stream, sets up ffmpeg decoder/filter, feeds filter chain
 * - ffmpeg_data_source: Custom ma_data_source that pulls from ffmpeg filter output
 * - ma_engine + ma_sound: miniaudio handles all playback, buffering, and timing
 *
 * Progress tracking via ma_sound_get_cursor_in_seconds()
 * Completion detection via ma_sound_set_end_callback()
 */

#include "config.h"
#include "bar_constants.h"
#include "miniaudio.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#ifdef HAVE_LIBAVFILTER_AVCODEC_H
#include <libavfilter/avcodec.h>
#endif
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/frame.h>
#include <string.h>

#ifdef __APPLE__
#include <mach/mach.h>
#endif

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include <time.h>
#include <errno.h>

#include "player.h"
#include "log.h"
#include "ui.h"
#include "ui_types.h"

/* default sample format */
const enum AVSampleFormat avformat = AV_SAMPLE_FMT_S16;

/*
 * Memory debugging counters for tracking frame allocations.
 * Enable with PIANOBAR_DEBUG=2 to see frame allocation stats.
 * Atomic to allow safe read from log/debug paths on other threads.
 */
static atomic_long g_framesAllocated = 0;
static atomic_long g_framesFreed = 0;
static void ffmpeg_data_source_uninit (ffmpeg_data_source_t *source);
static void audioTerminal (player_t *player, const char *operation);

/* One owner protects sound/device lifetime without holding an application
 * mutex across backend calls. Controls take priority over observations. */
enum { BAR_AUDIO_WAIT_TIMEOUT_SECONDS = 1 };
typedef enum { AUDIO_NORMAL, AUDIO_TEARDOWN, AUDIO_OBSERVATION, AUDIO_PLAYBACK_CONTROL } AudioPolicy;
static BarPlayerAudioFatalHook audioFatalHook;
static BarPlayerJoinTestHook joinTestHook;

void BarPlayerSetJoinTestHook (BarPlayerJoinTestHook hook) {
	joinTestHook = hook;
}

bool BarPlayerJoinThreadWithTimeout (player_t *player, pthread_t thread, void **retval, unsigned int timeoutSeconds) {
	bool joined = false;
	if (joinTestHook != NULL) { joined = joinTestHook (thread, retval, timeoutSeconds); }
	else {
#ifdef __linux__
	struct timespec deadline;
	clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += timeoutSeconds;
	joined = pthread_timedjoin_np (thread, retval, &deadline) == 0;
#else
	for (unsigned int i = 0; i < timeoutSeconds * 10; ++i) {
		const int ret = pthread_kill (thread, 0);
		if (ret == ESRCH) { joined = pthread_join (thread, retval) == 0; break; }
		if (ret != 0) { break; }
		usleep ((unsigned int)BAR_PLAYER_STOP_POLL_MS * 1000u);
	}
#endif
	}
	if (joined) {
		pthread_mutex_lock (&player->lock);
		player->threadJoinPending = false;
		pthread_mutex_unlock (&player->lock);
	}
	return joined;
}

static bool playbackControlMode (BarPlayerMode mode) {
	return mode == PLAYER_WAITING || mode == PLAYER_PLAYING;
}

static bool audioEligibleLocked (const player_t *player, AudioPolicy policy) {
	return !player->audioTerminalFailure && (policy == AUDIO_TEARDOWN || !player->doQuit) &&
		(policy != AUDIO_PLAYBACK_CONTROL || playbackControlMode (player->mode));
}

void BarPlayerSetAudioFatalTestHook (BarPlayerAudioFatalHook hook) {
	audioFatalHook = hook;
}

static void audioFatal (player_t *player, const char *operation) {
	if (audioFatalHook != NULL) {
		audioFatalHook (player, operation);
		return;
	}
	_Exit (EXIT_FAILURE);
}

void BarPlayerFatalShutdown (player_t *player, const char *operation) {
	pthread_mutex_lock (&player->lock);
	player->audioTerminalFailure = true;
	player->doQuit = true;
	++player->controlEpoch;
	pthread_cond_broadcast (&player->cond);
	pthread_cond_broadcast (&player->audioCond);
	pthread_mutex_unlock (&player->lock);
	log_write (LOG_ERROR, "Terminal player shutdown: %s; shared resources retained\n", operation);
	audioFatal (player, operation);
}

static bool monotonicExpired (const struct timespec *deadline) {
	struct timespec now;
	clock_gettime (CLOCK_MONOTONIC, &now);
	return now.tv_sec > deadline->tv_sec ||
		(now.tv_sec == deadline->tv_sec && now.tv_nsec >= deadline->tv_nsec);
}

/* Only this function handles platform-specific condition clocks. */
static int audioTimedWait (player_t *player, const struct timespec *deadline) {
#ifdef __APPLE__
	struct timespec now, remaining;
	clock_gettime (CLOCK_MONOTONIC, &now);
	remaining.tv_sec = deadline->tv_sec - now.tv_sec;
	remaining.tv_nsec = deadline->tv_nsec - now.tv_nsec;
	if (remaining.tv_nsec < 0) {
		--remaining.tv_sec;
		remaining.tv_nsec += 1000000000L;
	}
	if (remaining.tv_sec < 0) { return ETIMEDOUT; }
	return pthread_cond_timedwait_relative_np (&player->audioCond, &player->lock, &remaining);
#else
	return pthread_cond_timedwait (&player->audioCond, &player->lock, deadline);
#endif
}

/* All results leave player.lock held. Fatal diagnostics/callbacks run only
 * after the caller unlocks. A timeout never transfers the current ownership. */
static bool BarPlayerAudioReserveLocked (player_t *player, AudioPolicy policy,
		const char *operation) {
	if (!audioEligibleLocked (player, policy)) {
		return false;
	}
	if (player->audioBusy && player->audioOwnerValid &&
			pthread_equal (player->audioOwner, pthread_self ())) {
		/* Defer logging until no application mutex is held. */
		return false;
	}
	if (policy == AUDIO_OBSERVATION &&
			(player->audioBusy || player->audioControlWaiters != 0)) {
		return false;
	}
	struct timespec deadline;
	clock_gettime (CLOCK_MONOTONIC, &deadline);
	deadline.tv_sec += BAR_AUDIO_WAIT_TIMEOUT_SECONDS;
	const bool control = policy != AUDIO_OBSERVATION;
	if (control) { ++player->audioControlWaiters; }
	bool claimed = false;
	while (audioEligibleLocked (player, policy)) {
		if (!player->audioBusy) {
			player->audioBusy = true;
			player->audioOwner = pthread_self ();
			player->audioOwnerValid = true;
			player->audioOperation = operation;
			clock_gettime (CLOCK_MONOTONIC, &player->audioBusySince);
			claimed = true;
			break;
		}
		const int rc = audioTimedWait (player, &deadline);
		/* Recheck eligibility and availability before deciding a wait timed out. */
		if (!audioEligibleLocked (player, policy)) {
			break;
		}
		if (!player->audioBusy) { continue; }
		if ((rc == ETIMEDOUT && monotonicExpired (&deadline)) ||
				(rc != 0 && rc != ETIMEDOUT)) {
			if (policy == AUDIO_TEARDOWN) {
				player->audioTerminalFailure = true;
				player->doQuit = true;
				++player->controlEpoch;
				pthread_cond_broadcast (&player->cond);
			}
			break;
		}
	}
	if (control) { --player->audioControlWaiters; }
	if (!claimed) { pthread_cond_broadcast (&player->audioCond); }
	return claimed;
}

/* Wrapper owns the unlock for failed claims, including fatal-hook returns. */
static bool audioReserve (player_t *player, AudioPolicy policy, const char *operation) {
	pthread_mutex_lock (&player->lock);
	const bool alreadyTerminal = player->audioTerminalFailure;
	if (BarPlayerAudioReserveLocked (player, policy, operation)) { return true; }
	const bool terminal = !alreadyTerminal && player->audioTerminalFailure;
	const bool recursive = player->audioBusy && player->audioOwnerValid &&
		pthread_equal (player->audioOwner, pthread_self ());
	const char *ownerOperation = player->audioOperation;
	const pthread_t owner = player->audioOwner;
	struct timespec now;
	clock_gettime (CLOCK_MONOTONIC, &now);
	const double elapsed = (double)(now.tv_sec - player->audioBusySince.tv_sec) +
		(double)(now.tv_nsec - player->audioBusySince.tv_nsec) / 1e9;
	pthread_mutex_unlock (&player->lock);
	if (recursive) {
		log_write (LOG_ERROR, "Recursive audio reservation rejected: %s\n", operation);
	}
	if (terminal) {
		log_write (LOG_ERROR, "Audio reservation failure: %s; owner=%lu operation=%s elapsed=%.3fs\n",
			operation, (unsigned long)owner, ownerOperation != NULL ? ownerOperation : "none", elapsed);
		audioFatal (player, operation);
	}
	return false;
}

/* Requires lock. Setup uses this variant to commit mode and audio together. */
static void audioCompleteLocked (player_t *player, BarPlayerAudioState state, bool signal) {
	player->audioState = state;
	player->audioBusy = false;
	player->audioOwnerValid = false;
	memset (&player->audioOwner, 0, sizeof (player->audioOwner));
	memset (&player->audioBusySince, 0, sizeof (player->audioBusySince));
	player->audioOperation = NULL;
	pthread_cond_broadcast (&player->audioCond);
	if (signal) { pthread_cond_broadcast (&player->cond); }
	pthread_mutex_unlock (&player->lock);
}

static void audioComplete (player_t *player, BarPlayerAudioState state) {
	pthread_mutex_lock (&player->lock);
	audioCompleteLocked (player, state, false);
}

/* These helpers require the reservation, never player.lock. Device state is
 * queried directly: a test no-device engine has no physical transition. */
static bool deviceSetStartedReserved (player_t *player, bool started, const char *operation) {
	if (!player->engineInitialized) { return !started; }
	ma_device *device = ma_engine_get_device (&player->engine);
	if (device == NULL) { return player->audioNoDevice; }
	if ((ma_device_is_started (device) != MA_FALSE) == started) { return true; }
	const ma_result result = started ? ma_engine_start (&player->engine) : ma_engine_stop (&player->engine);
	if (result != MA_SUCCESS) {
		log_write (LOG_ERROR, "Audio device %s failed: %d\n", started ? "start" : "stop", result);
		return false;
	}
	const bool transitioned = (ma_device_is_started (device) != MA_FALSE) == started;
	if (transitioned) {
		log_write (DEBUG_AUDIO, "Audio device %s (%s)\n",
			started ? "started" : "stopped", operation);
	}
	return transitioned;
}

static bool stopSoundReserved (player_t *player, bool live, const char *operation) {
	/* Device stop joins its callback. Publish a persistent source predicate
	 * before that join, including rollback before any decoder has started. */
	pthread_mutex_lock (&player->decoderLock);
	player->sourceReadCancelled = true;
	pthread_cond_broadcast (&player->decoderCond);
	pthread_mutex_unlock (&player->decoderLock);
	bool ok = true;
	if (live) {
		const ma_result result = ma_sound_stop (&player->sound);
		if (result != MA_SUCCESS) {
			log_write (LOG_ERROR, "Audio sound stop failed: %d\n", result);
			ok = false;
		}
	}
	return deviceSetStartedReserved (player, false, operation) && ok;
}

static bool cleanupSoundReserved (player_t *player, bool live, const char *operation) {
	if (!stopSoundReserved (player, live, operation)) { return false; }
	if (live) { ma_sound_uninit (&player->sound); }
	if (player->dataSourceInitialized) {
		ffmpeg_data_source_uninit (&player->dataSource);
		player->dataSourceInitialized = false;
	}
	return true;
}

/* Get current RSS (Resident Set Size) in KB for memory tracking */
static long getCurrentRSSKB(void) {
#ifdef __APPLE__
	/* Use Mach API on macOS */
	struct mach_task_basic_info info;
	mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
	if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, 
	              (task_info_t)&info, &count) == KERN_SUCCESS) {
		return info.resident_size / 1024;
	}
	return -1;
#else
	/* Linux: read from /proc/self/status */
	FILE *f = fopen("/proc/self/status", "r");
	if (!f) return -1;
	
	char line[BAR_BUF_SMALL];
	long rss = -1;
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "VmRSS:", 6) == 0) {
			sscanf(line + 6, "%ld", &rss);
			break;
		}
	}
	fclose(f);
	return rss;  /* Already in KB */
#endif
}

/* Right-aligned field for log lines; fits up to 9,999,999 KB ("9,999,999"). */
#define RSS_KB_FIELD_WIDTH 9

static void formatULongWithCommas(char *out, size_t outlen, unsigned long n)
{
	char num[24];
	snprintf(num, sizeof num, "%lu", n);
	const int L = (int)strlen(num);
	char tmp[32];
	int t = 0;
	int count = 0;
	for (int i = L - 1; i >= 0; i--) {
		if (count > 0 && count % 3 == 0) {
			tmp[t++] = ',';
		}
		tmp[t++] = num[i];
		count++;
	}
	tmp[t] = '\0';
	char rev[32];
	for (int i = 0; i < t; i++) {
		rev[i] = tmp[t - 1 - i];
	}
	rev[t] = '\0';
	snprintf(out, outlen, "%s", rev);
}

static void formatRSSKBField(char *out, size_t outlen, long kb)
{
	if (kb < 0) {
		snprintf(out, outlen, "%*s", RSS_KB_FIELD_WIDTH, "?");
		return;
	}
	char tmp[32];
	formatULongWithCommas(tmp, sizeof tmp, (unsigned long)kb);
	snprintf(out, outlen, "%*s", RSS_KB_FIELD_WIDTH, tmp);
}

/* Delta vs high-water RSS for parenthetical part; e.g. (+ 32 KB), (- 1,234 KB). */
static void formatRSSDeltaParen(char *out, size_t outlen, long delta)
{
	if (delta == 0) {
		snprintf(out, outlen, "(0 KB)");
		return;
	}
	unsigned long mag;
	if (delta < 0) {
		mag = (unsigned long)(-(unsigned long long)delta);
	} else {
		mag = (unsigned long)delta;
	}
	char magbuf[32];
	formatULongWithCommas(magbuf, sizeof magbuf, mag);
	if (delta > 0) {
		snprintf(out, outlen, "(+ %s KB)", magbuf);
	} else {
		snprintf(out, outlen, "(- %s KB)", magbuf);
	}
}

/* Peak RSS (KB) for DEBUG_AUDIO lines from logRSSAudio; process-wide, BarPlayerThread only. */
static long s_rssAudioHwmKb = -1;

#if defined(__GLIBC__)
/* RSS (KB) recorded after the last malloc_trim; -1 = never trimmed yet. */
static long s_rssAfterLastTrimKb = -1;
#endif

static void logRSSAudio(const char *fmt, ...)
	__attribute__((format(printf, 1, 2)));

static void logRSSAudio(const char *fmt, ...)
{
	char label[512];
	va_list ap;
	va_start(ap, fmt);
	(void)vsnprintf(label, sizeof label, fmt, ap);
	va_end(ap);

	const long cur = getCurrentRSSKB();
	char absbuf[RSS_KB_FIELD_WIDTH + 1];
	formatRSSKBField(absbuf, sizeof absbuf, cur);

	char paren[64];
	if (cur < 0) {
		snprintf(paren, sizeof paren, "(n/a)");
	} else if (s_rssAudioHwmKb < 0) {
		snprintf(paren, sizeof paren, "(n/a)");
	} else {
		formatRSSDeltaParen(paren, sizeof paren, cur - s_rssAudioHwmKb);
	}

	log_write(DEBUG_AUDIO, "%s KB %s RSS: %s\n", absbuf, paren, label);

	if (cur >= 0) {
		if (s_rssAudioHwmKb < 0 || cur > s_rssAudioHwmKb) {
			s_rssAudioHwmKb = cur;
		}
	}
}

/* Forward declarations */
static bool shouldQuit(player_t * const player);

static void printError(const BarSettings_t * const settings,
		const char * const msg, int ret) {
	char avmsg[128];
	av_strerror(ret, avmsg, sizeof(avmsg));
	BarUiMsg(settings, MSG_ERR, "%s (%s)\n", msg, avmsg);
}

/*
 * ============================================================================
 * Custom FFmpeg Data Source for miniaudio
 * ============================================================================
 */

/* Read PCM frames from ffmpeg filter chain with proper partial frame buffering */
static ma_result ffmpeg_data_source_read(ma_data_source* pDataSource, 
		void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead) {
	ffmpeg_data_source_t* pFFmpeg = (ffmpeg_data_source_t*)pDataSource;
	player_t* player = pFFmpeg->player;
	
	if (pFramesRead != NULL) {
		*pFramesRead = 0;
	}
	
	if (pFFmpeg->reachedEnd) {
		return MA_AT_END;
	}
	
	/* Transition safeguard: the engine should normally be stopped for a
	 * sustained pause. Keep silence until backend race testing proves this
	 * callback cannot overlap the physical pause transition. */
	if (BarPlayerIsPaused(player)) {
		memset(pFramesOut, 0, frameCount * pFFmpeg->channels * sizeof(int16_t));
		if (pFramesRead != NULL) {
			*pFramesRead = frameCount;
		}
		return MA_SUCCESS;
	}
	
	int16_t* output = (int16_t*)pFramesOut;
	ma_uint64 framesRead = 0;
	
	pthread_mutex_lock(&player->decoderLock);
	
	while (framesRead < frameCount && !player->sourceReadCancelled) {
		/* First, consume from any buffered frame */
		if (pFFmpeg->bufferedFrame != NULL) {
			const int numChannels = pFFmpeg->bufferedFrame->ch_layout.nb_channels;
			const int totalSamples = pFFmpeg->bufferedFrame->nb_samples;
			const int samplesRemaining = totalSamples - pFFmpeg->bufferedFrameOffset;
			const int16_t* frameData = (const int16_t*)pFFmpeg->bufferedFrame->data[0];
			
			/* Calculate how many samples to copy from the buffered frame */
			int samplesToCopy = samplesRemaining;
			if ((ma_uint64)samplesToCopy > frameCount - framesRead) {
				samplesToCopy = (int)(frameCount - framesRead);
			}
			
			/* Copy from the offset position in the buffered frame */
			memcpy(output + (framesRead * numChannels),
			       frameData + (pFFmpeg->bufferedFrameOffset * numChannels),
			       samplesToCopy * numChannels * sizeof(int16_t));
			
			framesRead += samplesToCopy;
			pFFmpeg->cursor += samplesToCopy;
			pFFmpeg->bufferedFrameOffset += samplesToCopy;
			
			/* Free the frame only when fully consumed */
			if (pFFmpeg->bufferedFrameOffset >= totalSamples) {
				av_frame_free(&pFFmpeg->bufferedFrame);
				pFFmpeg->bufferedFrame = NULL;
				pFFmpeg->bufferedFrameOffset = 0;
				g_framesFreed++;
			}
			
			continue;  /* Check if we need more data */
		}
		
		/* No buffered frame - get a new one from the filter chain */
		AVFrame* newFrame = av_frame_alloc();
		if (!newFrame) {
			pthread_mutex_unlock(&player->decoderLock);
			return MA_OUT_OF_MEMORY;
		}
		
		int ret = av_buffersink_get_frame(player->fbufsink, newFrame);
		
		if (ret == AVERROR_EOF) {
			/* End of stream */
			av_frame_free(&newFrame);
			pFFmpeg->reachedEnd = true;
		log_write(DEBUG_AUDIO, "FFmpeg data source reached EOF at frame %llu\n", 
		           (unsigned long long)pFFmpeg->cursor);
			break;
		} else if (ret == AVERROR(EAGAIN)) {
			/* No data available yet - wait for decoder to produce more */
			av_frame_free(&newFrame);
			
			/* Check if decoding is finished but filter still draining */
			if (player->decodingFinished) {
				/* Decoder done, but filter returned EAGAIN - might need to flush */
				break;
			}
			
			/* Wait for decoder to signal new data */
			pthread_cond_wait(&player->decoderCond, &player->decoderLock);
			
			/* The loop predicate prevents another wait after physical stop. */
			continue;
		} else if (ret < 0) {
			/* Error */
			av_frame_free(&newFrame);
			log_write(DEBUG_AUDIO, "FFmpeg buffersink error: %d\n", ret);
			break;
		}
		
		/* Got a new frame - store it as the buffered frame for consumption */
		pFFmpeg->bufferedFrame = newFrame;
		pFFmpeg->bufferedFrameOffset = 0;
		g_framesAllocated++;
		
		/* Periodic frame stats logging - uncomment to debug memory leaks
		if (g_framesAllocated % 1000 == 0) {
			logRSSAudio("frame stats (alloc=%ld, freed=%ld, delta=%ld)",
			            g_framesAllocated, g_framesFreed,
			            g_framesAllocated - g_framesFreed);
		}
		*/
		/* Loop will consume from it on next iteration */
	}
	
	const bool cancelled = player->sourceReadCancelled;
	pthread_mutex_unlock(&player->decoderLock);
	
	/* Fill remaining with silence if we didn't get enough */
	if (framesRead < frameCount) {
		memset(output + (framesRead * pFFmpeg->channels), 0, 
		       (frameCount - framesRead) * pFFmpeg->channels * sizeof(int16_t));
	}
	
	if (pFramesRead != NULL) {
		/* A stop wake returns bounded silence, not natural EOF. This keeps a
		 * retained node/cursor resumable without firing its end callback. */
		*pFramesRead = cancelled ? frameCount : framesRead;
	}
	if (cancelled) { return MA_SUCCESS; }
	
	/* Return AT_END only if we read nothing AND we're at the end */
	if (framesRead == 0 && pFFmpeg->reachedEnd) {
		return MA_AT_END;
	}
	
	return MA_SUCCESS;
}

/* Seeking not supported for streaming */
static ma_result ffmpeg_data_source_seek(ma_data_source* pDataSource, ma_uint64 frameIndex) {
	(void)pDataSource;
	(void)frameIndex;
	return MA_NOT_IMPLEMENTED;
}

/* Return audio format */
static ma_result ffmpeg_data_source_get_data_format(ma_data_source* pDataSource,
		ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate,
		ma_channel* pChannelMap, size_t channelMapCap) {
	ffmpeg_data_source_t* pFFmpeg = (ffmpeg_data_source_t*)pDataSource;
	
	if (pFormat != NULL) {
		*pFormat = ma_format_s16;
	}
	if (pChannels != NULL) {
		*pChannels = pFFmpeg->channels;
	}
	if (pSampleRate != NULL) {
		*pSampleRate = pFFmpeg->sampleRate;
	}
	if (pChannelMap != NULL && channelMapCap > 0) {
		ma_channel_map_init_standard(ma_standard_channel_map_default, 
		                             pChannelMap, channelMapCap, pFFmpeg->channels);
	}
	
	return MA_SUCCESS;
}

/* Return current cursor position */
static ma_result ffmpeg_data_source_get_cursor(ma_data_source* pDataSource, ma_uint64* pCursor) {
	ffmpeg_data_source_t* pFFmpeg = (ffmpeg_data_source_t*)pDataSource;
	
	if (pCursor == NULL) {
		return MA_INVALID_ARGS;
	}
	
	*pCursor = pFFmpeg->cursor;
	return MA_SUCCESS;
}

/* Return total length */
static ma_result ffmpeg_data_source_get_length(ma_data_source* pDataSource, ma_uint64* pLength) {
	ffmpeg_data_source_t* pFFmpeg = (ffmpeg_data_source_t*)pDataSource;
	
	if (pLength == NULL) {
		return MA_INVALID_ARGS;
	}
	
	*pLength = pFFmpeg->totalFrames;
	return MA_SUCCESS;
}

/* Data source vtable */
static ma_data_source_vtable g_ffmpeg_data_source_vtable = {
	ffmpeg_data_source_read,
	ffmpeg_data_source_seek,
	ffmpeg_data_source_get_data_format,
	ffmpeg_data_source_get_cursor,
	ffmpeg_data_source_get_length,
	NULL,  /* onSetLooping */
	0      /* flags */
};

/* Initialize the ffmpeg data source */
static ma_result ffmpeg_data_source_init(ffmpeg_data_source_t* pFFmpeg, player_t* player) {
	ma_data_source_config baseConfig;
	pthread_mutex_lock (&player->decoderLock);
	player->sourceReadCancelled = false;
	pthread_mutex_unlock (&player->decoderLock);
	
	baseConfig = ma_data_source_config_init();
	baseConfig.vtable = &g_ffmpeg_data_source_vtable;
	
	ma_result result = ma_data_source_init(&baseConfig, &pFFmpeg->base);
	if (result != MA_SUCCESS) {
		return result;
	}
	
	pFFmpeg->player = player;
	pFFmpeg->cursor = 0;
	pFFmpeg->reachedEnd = false;
	pFFmpeg->bufferedFrame = NULL;
	pFFmpeg->bufferedFrameOffset = 0;
	
	/* Get format info from the stream */
	const AVCodecParameters* cp = player->st->codecpar;
	pFFmpeg->channels = cp->ch_layout.nb_channels;
	pFFmpeg->sampleRate = player->settings->sampleRate != 0 ? 
	                      player->settings->sampleRate : cp->sample_rate;
	
	/* Calculate total frames from stream duration */
	double durationSecs = av_q2d(player->st->time_base) * (double)player->st->duration;
	pFFmpeg->totalFrames = (ma_uint64)(durationSecs * pFFmpeg->sampleRate);
	
	log_write(DEBUG_AUDIO, "FFmpeg data source initialized: %u Hz, %u channels, %llu total frames (%.1f sec)\n",
	           pFFmpeg->sampleRate, pFFmpeg->channels, (unsigned long long)pFFmpeg->totalFrames, durationSecs);
	
	return MA_SUCCESS;
}

static void ffmpeg_data_source_uninit(ffmpeg_data_source_t* pFFmpeg) {
	/* Free any buffered frame */
	if (pFFmpeg->bufferedFrame != NULL) {
		av_frame_free(&pFFmpeg->bufferedFrame);
		pFFmpeg->bufferedFrame = NULL;
		g_framesFreed++;
	}
	ma_data_source_uninit(&pFFmpeg->base);
	memset(pFFmpeg, 0, sizeof(*pFFmpeg));
}

/*
 * ============================================================================
 * Song End Callback
 * ============================================================================
 */

static void onSongEnd(void* pUserData, ma_sound* pSound) {
	(void)pSound;
	player_t* player = (player_t*)pUserData;
	
	log_write(DEBUG_AUDIO, "Song end callback fired\n");
	BarPlayerSetMode (player, PLAYER_FINISHED);
}

/*
 * ============================================================================
 * Player Initialization and Cleanup
 * ============================================================================
 */

void BarPlayerInit(player_t * const p, BarSettings_t * const settings) {

	av_log_set_level(AV_LOG_FATAL);
#ifdef HAVE_AV_REGISTER_ALL
	av_register_all();
#endif
#ifdef HAVE_AVFILTER_REGISTER_ALL
	avfilter_register_all();
#endif
#ifdef HAVE_AVFORMAT_NETWORK_INIT
	avformat_network_init();
#endif

	if (!p->synchronizationInitialized) {
		pthread_mutex_init (&p->lock, NULL);
		pthread_cond_init (&p->cond, NULL);
		pthread_mutex_init (&p->decoderLock, NULL);
		pthread_cond_init (&p->decoderCond, NULL);
		pthread_condattr_t attr;
		pthread_condattr_init (&attr);
#ifndef __APPLE__
		pthread_condattr_setclock (&attr, CLOCK_MONOTONIC);
#endif
		pthread_cond_init (&p->audioCond, &attr);
		pthread_condattr_destroy (&attr);
		p->synchronizationInitialized = true;
		p->settings = settings;
		p->requestedVolume = settings->volume < 0 ? 0 :
			(settings->volume > VOLUME_MAX_PERCENT ? VOLUME_MAX_PERCENT : settings->volume);
		settings->volume = p->requestedVolume;
	}
	
	/* Initialize miniaudio engine once
	 * On macOS, engine MUST be initialized AFTER fork to avoid CoreAudio thread issues.
	 * CoreAudio creates background threads that don't survive fork properly. */
	
	if (p->engineInitialized) {
		/* Already initialized, just reset and return */
		if (!BarPlayerReset (p)) { return; }
		pthread_mutex_lock (&p->lock);
		p->settings = settings;
		settings->volume = p->requestedVolume;
		pthread_mutex_unlock (&p->lock);
		s_rssAudioHwmKb = -1;
#if defined(__GLIBC__)
		s_rssAfterLastTrimKb = -1;
#endif
		return;
	}
	
	ma_engine_config engineConfig = ma_engine_config_init();
	engineConfig.noAutoStart = MA_TRUE;
	if (getenv ("PIANOBAR_TEST_NO_DEVICE") != NULL) {
		engineConfig.noDevice = MA_TRUE;
		engineConfig.sampleRate = settings->sampleRate != 0 ? settings->sampleRate : 44100;
		engineConfig.channels = 2;
		p->audioNoDevice = true;
	} else {
		engineConfig.noDevice = MA_FALSE;
	}
	ma_result result = ma_engine_init(&engineConfig, &p->engine);
	if (result != MA_SUCCESS) {
		log_write(LOG_ERROR, "Failed to initialize audio engine: %d\n", result);
		p->engineInitialized = false;
	} else {
		p->engineInitialized = true;
		log_write(DEBUG_AUDIO, "Audio engine initialized (sample rate: %u)\n",
		           ma_engine_get_sample_rate(&p->engine));
	}
	
	if (!BarPlayerReset (p)) { return; }
	s_rssAudioHwmKb = -1;
#if defined(__GLIBC__)
	s_rssAfterLastTrimKb = -1;
#endif
}

bool BarPlayerDestroy(player_t * const p) {
	if (p == NULL || !p->synchronizationInitialized) { return false; }
	pthread_mutex_lock (&p->lock);
	const bool joinPending = p->threadJoinPending;
	pthread_mutex_unlock (&p->lock);
	if (joinPending) { BarPlayerFatalShutdown (p, "destroy before player join"); return false; }
	if (!audioReserve (p, AUDIO_TEARDOWN, "destroy")) { return false; }
	const bool live = p->audioState != PLAYER_AUDIO_NONE;
	pthread_mutex_unlock (&p->lock);
	if (!cleanupSoundReserved (p, live, "destroy")) {
		audioTerminal (p, "destroy");
		return false;
	}
	if (p->engineInitialized) { ma_engine_uninit (&p->engine); }
	pthread_mutex_lock (&p->lock);
	p->soundInitialized = false;
	p->engineInitialized = false;
	audioCompleteLocked (p, PLAYER_AUDIO_NONE, true);
	pthread_cond_destroy (&p->audioCond);
	pthread_cond_destroy(&p->cond);
	pthread_mutex_destroy(&p->lock);
	pthread_cond_destroy(&p->decoderCond);
	pthread_mutex_destroy(&p->decoderLock);

#ifdef HAVE_AVFORMAT_NETWORK_INIT
	avformat_network_deinit();
#endif
	p->synchronizationInitialized = false;
	return true;
}

bool BarPlayerReset(player_t * const p) {
	if (p == NULL || !audioReserve (p, AUDIO_TEARDOWN, "reset")) { return false; }
	const bool live = p->audioState != PLAYER_AUDIO_NONE;
	const uint64_t epoch = p->controlEpoch;
	pthread_mutex_unlock (&p->lock);
	if (!cleanupSoundReserved (p, live, "reset")) {
		audioTerminal (p, "reset");
		return false;
	}
	pthread_mutex_lock (&p->lock);
	p->soundInitialized = false;
	if (p->controlEpoch != epoch) {
		/* Physical cleanup succeeded, but a newer control owns the logical
		 * decision. The lifecycle caller must not start a replacement worker. */
		audioCompleteLocked (p, PLAYER_AUDIO_NONE, true);
		return false;
	}
	/* Reset all fields */
	p->doQuit = false;
	p->doPause = false;
	p->pauseStartTime = 0;
	p->songDuration = 0;
	p->songPlayed = 0;
	p->mode = PLAYER_DEAD;
	++p->controlEpoch;
	p->fgraph = NULL;
	p->fctx = NULL;
	p->st = NULL;
	p->cctx = NULL;
	p->fbufsink = NULL;
	p->fabuf = NULL;
	p->streamIdx = -1;
	p->lastTimestamp = 0;
	p->interrupted = 0;
	p->decodingFinished = false;
	memset(&p->dataSource, 0, sizeof(p->dataSource));
	audioCompleteLocked (p, PLAYER_AUDIO_NONE, true);
	return true;
}

/*
 * ============================================================================
 * Volume Control
 * ============================================================================
 */

static void setVolumeReserved (player_t *player, int volume, double gain, double gainMul) {
	ma_sound_set_volume (&player->sound, (float)volume / 100.0f *
		powf (10.0f, (float)(gain * gainMul) / 20.0f));
}

static bool updateVolume (player_t *player, int value, bool relative) {
	if (player == NULL || !audioReserve (player, AUDIO_NORMAL, "volume")) { return false; }
	const BarPlayerAudioState state = player->audioState;
	int64_t volume = relative ? (int64_t)player->requestedVolume + value : value;
	if (volume < 0) { volume = 0; }
	if (volume > VOLUME_MAX_PERCENT) { volume = VOLUME_MAX_PERCENT; }
	player->requestedVolume = (int)volume;
	player->settings->volume = (int)volume;
	const double gain = player->gain, gainMul = player->settings->gainMul;
	pthread_mutex_unlock (&player->lock);
	if (state == PLAYER_AUDIO_RUNNING || state == PLAYER_AUDIO_STOPPED) {
		setVolumeReserved (player, (int)volume, gain, gainMul);
	}
	audioComplete (player, state);
	return true;
}

bool BarPlayerSetVolume (player_t *player, int requestedVolume) {
	return updateVolume (player, requestedVolume, false);
}

bool BarPlayerAdjustVolume (player_t *player, int delta) {
	return updateVolume (player, delta, true);
}

int BarPlayerGetVolume (player_t *player) {
	if (player == NULL) { return -1; }
	pthread_mutex_lock (&player->lock);
	const int volume = player->requestedVolume;
	pthread_mutex_unlock (&player->lock);
	return volume;
}

/*
 * ============================================================================
 * Stream and Filter Setup
 * ============================================================================
 */

/* softfail macro retired: use explicit goto cleanup in openStream / openFilter */

bool BarIsAvErrStaleCdnUrl(int av_err) {
	if (av_err >= 0) {
		return false;
	}
#if defined(AVERROR_HTTP_FORBIDDEN)
	if (av_err == AVERROR_HTTP_FORBIDDEN) {
		return true;
	}
#endif
	char errbuf[AV_ERROR_MAX_STRING_SIZE];
	if (av_strerror(av_err, errbuf, sizeof errbuf) != 0) {
		return false;
	}
	if (strstr(errbuf, "403") != NULL) {
		return true;
	}
	if (strstr(errbuf, "Forbidden") != NULL) {
		return true;
	}
	return false;
}

/* ffmpeg callback for blocking functions */
int BarPlayerFfmpegInterruptCb (void * const data) {
	player_t * const player = data;
	assert (player != NULL);
	const sig_atomic_t interrupted = atomic_load_explicit (&player->interrupted, memory_order_relaxed);
	if (interrupted > 1) {
		/* FFmpeg invokes this on its open/read worker, not a signal or audio
		 * callback. No application lock or audio reservation is held here. */
		BarPlayerRequestStop (player);
		return 1;
	} else if (interrupted != 0) {
		sig_atomic_t expected = interrupted;
		atomic_compare_exchange_strong_explicit (&player->interrupted, &expected, 0, memory_order_relaxed, memory_order_relaxed);
		return 1;
	} else {
		return 0;
	}
}

/* staleCdn403: set when avformat_open_input fails (optional, may be NULL) */
static bool openStream(player_t * const player, bool *staleCdn403) {
	assert(player != NULL);
	assert(player->fctx == NULL);
	if (staleCdn403 != NULL) {
		*staleCdn403 = false;
	}

	bool ok = false;
	int ret;
	AVDictionary *options = NULL;

	player->fctx = avformat_alloc_context();
	player->fctx->interrupt_callback.callback = BarPlayerFfmpegInterruptCb;
	player->fctx->interrupt_callback.opaque = player;

	unsigned long int timeout = player->settings->timeout * 1000000;
	char timeoutStr[16];
	ret = snprintf(timeoutStr, sizeof(timeoutStr), "%lu", timeout);
	assert(ret < (int)sizeof(timeoutStr));
	av_dict_set(&options, "timeout", timeoutStr, 0);

	assert(player->url != NULL);
	log_network_request(player->url);
	if ((ret = avformat_open_input(&player->fctx, player->url, NULL, &options)) < 0) {
		av_dict_free(&options);
		options = NULL;
		if (staleCdn403 != NULL) {
			*staleCdn403 = BarIsAvErrStaleCdnUrl(ret);
		}
		char avmsg[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(ret, avmsg, sizeof avmsg);
		char errSummary[160];
		snprintf(errSummary, sizeof errSummary, "error: %s",
			avmsg[0] != '\0' ? avmsg : "unknown");
		log_network_response(errSummary);
		printError(player->settings, "Unable to open audio file", ret);
		/* avformat_open_input frees fctx on failure (or it wasn't opened); clear the pointer */
		if (player->fctx != NULL) {
			avformat_free_context(player->fctx);
			player->fctx = NULL;
		}
		return false;
	}
	av_dict_free(&options);
	options = NULL;
	log_network_response("ok");

	if ((ret = avformat_find_stream_info(player->fctx, NULL)) < 0) {
		printError(player->settings, "find_stream_info", ret);
		goto cleanup;
	}

	for (size_t i = 0; i < player->fctx->nb_streams; i++) {
		player->fctx->streams[i]->discard = AVDISCARD_ALL;
	}

	player->streamIdx = av_find_best_stream(player->fctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (player->streamIdx < 0) {
		ret = player->streamIdx;
		printError(player->settings, "find_best_stream", ret);
		goto cleanup;
	}

	player->st = player->fctx->streams[player->streamIdx];
	player->st->discard = AVDISCARD_DEFAULT;

	if ((player->cctx = avcodec_alloc_context3(NULL)) == NULL) {
		ret = AVERROR(ENOMEM);
		printError(player->settings, "avcodec_alloc_context3", ret);
		goto cleanup;
	}
	{
		const AVCodecParameters * const cp = player->st->codecpar;
		if ((ret = avcodec_parameters_to_context(player->cctx, cp)) < 0) {
			printError(player->settings, "avcodec_parameters_to_context", ret);
			goto cleanup;
		}

		const AVCodec * const decoder = avcodec_find_decoder(cp->codec_id);
		if (decoder == NULL) {
			ret = AVERROR_DECODER_NOT_FOUND;
			printError(player->settings, "find_decoder", ret);
			goto cleanup;
		}

		if ((ret = avcodec_open2(player->cctx, decoder, NULL)) < 0) {
			printError(player->settings, "codec_open2", ret);
			goto cleanup;
		}
	}

	if (player->lastTimestamp > 0) {
		av_seek_frame(player->fctx, player->streamIdx, player->lastTimestamp, 0);
	}

	{
		const unsigned int songDuration = av_q2d(player->st->time_base) *
				(double)player->st->duration;
		pthread_mutex_lock(&player->lock);
		player->songPlayed = 0;
		player->songDuration = songDuration;
		pthread_mutex_unlock(&player->lock);
	}

	ok = true;
cleanup:
	if (!ok) {
		if (player->cctx != NULL) {
			avcodec_free_context(&player->cctx);
		}
		if (player->fctx != NULL) {
			avformat_close_input(&player->fctx);
		}
	}
	return ok;
}

static int getSampleRate(const player_t * const player) {
	AVCodecParameters const * const cp = player->st->codecpar;
	return player->settings->sampleRate == 0 ?
			cp->sample_rate :
			player->settings->sampleRate;
}

static bool openFilter(player_t * const player) {
	char strbuf[BAR_BUF_SMALL];
	bool ok = false;
	int ret = 0;
	AVCodecParameters * const cp = player->st->codecpar;
	AVFilterContext *fafmt = NULL;

	if ((player->fgraph = avfilter_graph_alloc()) == NULL) {
		ret = AVERROR(ENOMEM);
		printError(player->settings, "graph_alloc", ret);
		return false;
	}

	AVRational time_base = player->st->time_base;

	/* Create abuffer (source) filter */
	char channelLayout[128];
	av_channel_layout_describe(&player->cctx->ch_layout, channelLayout, sizeof(channelLayout));
	snprintf(strbuf, sizeof(strbuf),
			"time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
			time_base.num, time_base.den, cp->sample_rate,
			av_get_sample_fmt_name(player->cctx->sample_fmt),
			channelLayout);
	if ((ret = avfilter_graph_create_filter(&player->fabuf,
			avfilter_get_by_name("abuffer"), "source", strbuf, NULL,
			player->fgraph)) < 0) {
		printError(player->settings, "create_filter abuffer", ret);
		goto cleanup;
	}

	/* Create aformat filter (sample format conversion) */
	snprintf(strbuf, sizeof(strbuf), "sample_fmts=%s:sample_rates=%d",
			av_get_sample_fmt_name(avformat), getSampleRate(player));
	if ((ret = avfilter_graph_create_filter(&fafmt,
					avfilter_get_by_name("aformat"), "format", strbuf, NULL,
					player->fgraph)) < 0) {
		printError(player->settings, "create_filter aformat", ret);
		goto cleanup;
	}

	/* Create abuffersink (sink) filter */
	if ((ret = avfilter_graph_create_filter(&player->fbufsink,
			avfilter_get_by_name("abuffersink"), "sink", NULL, NULL,
			player->fgraph)) < 0) {
		printError(player->settings, "create_filter abuffersink", ret);
		goto cleanup;
	}

	/* Link filters: abuffer -> aformat -> abuffersink
	 * (volume control is handled by miniaudio, not FFmpeg) */
	if (avfilter_link(player->fabuf, 0, fafmt, 0) != 0 ||
			avfilter_link(fafmt, 0, player->fbufsink, 0) != 0) {
		ret = AVERROR_UNKNOWN;
		printError(player->settings, "filter_link", ret);
		goto cleanup;
	}

	if ((ret = avfilter_graph_config(player->fgraph, NULL)) < 0) {
		printError(player->settings, "graph_config", ret);
		goto cleanup;
	}

	ok = true;
cleanup:
	if (!ok && player->fgraph != NULL) {
		avfilter_graph_free(&player->fgraph);
		player->fgraph = NULL;
	}
	return ok;
}

/*
 * ============================================================================
 * Playback Control
 * ============================================================================
 */

static bool shouldQuit(player_t * const player) {
	pthread_mutex_lock(&player->lock);
	const bool ret = player->doQuit;
	pthread_mutex_unlock(&player->lock);
	return ret;
}

/* Packet-boundary wait: decoderLock is never held while awaiting controls. */
static bool BarPlayerWaitWhilePaused (player_t *player) {
	pthread_mutex_lock (&player->lock);
	while (player->doPause && !player->doQuit) {
		/* An end callback can supersede resume while pause remains set. */
		if (player->mode != PLAYER_PLAYING) { break; }
		pthread_cond_wait (&player->cond, &player->lock);
	}
	const bool keepDecoding = !player->doQuit && player->mode == PLAYER_PLAYING;
	pthread_mutex_unlock (&player->lock);
	return keepDecoding;
}

bool BarPlayerIsPaused(player_t * const player) {
	pthread_mutex_lock(&player->lock);
	const bool ret = player->doPause;
	pthread_mutex_unlock(&player->lock);
	return ret;
}

void BarPlayerSetMode (player_t * const player, BarPlayerMode mode) {
	if (player == NULL) { return; }

	pthread_mutex_lock(&player->lock);
	if (player->mode != mode) {
		player->mode = mode;
		++player->controlEpoch;
	}
	pthread_cond_broadcast(&player->cond);
	pthread_cond_broadcast(&player->audioCond);
	pthread_mutex_unlock(&player->lock);
}

BarPlayerMode BarPlayerGetMode(player_t * const player) {
	pthread_mutex_lock(&player->lock);
	const BarPlayerMode ret = player->mode;
	pthread_mutex_unlock(&player->lock);
	return ret;
}

bool BarPlayerWaitForMode (player_t * const player,
                            BarPlayerMode mode,
                            unsigned int timeoutMs)
{
	if (player == NULL) { return false; }

	struct timespec deadline;
	clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec  += (time_t)(timeoutMs / 1000);
	deadline.tv_nsec += (long)((timeoutMs % 1000) * 1000000L);
	if (deadline.tv_nsec >= 1000000000L) {
		deadline.tv_sec  += 1;
		deadline.tv_nsec -= 1000000000L;
	}

	pthread_mutex_lock (&player->lock);
	while (player->mode != mode) {
		int rc = pthread_cond_timedwait (&player->cond, &player->lock, &deadline);
		if (rc == ETIMEDOUT) {
			pthread_mutex_unlock (&player->lock);
			return false;
		}
	}
	pthread_mutex_unlock (&player->lock);
	return true;
}

/*
 * ============================================================================
 * Decoding Loop - Feeds frames to filter chain
 * ============================================================================
 */

static int decode(player_t * const player) {
	assert(player != NULL);
	AVCodecContext * const cctx = player->cctx;

	AVPacket *pkt = av_packet_alloc();
	assert(pkt != NULL);
	pkt->data = NULL;
	pkt->size = 0;

	AVFrame *frame = av_frame_alloc();
	assert(frame != NULL);

	enum { FILL, DRAIN, DONE } drainMode = FILL;
	int ret = 0;
	
	while (!shouldQuit(player) && drainMode != DONE) {
		if (drainMode == FILL) {
			if (!BarPlayerWaitWhilePaused (player)) { break; }
			ret = av_read_frame(player->fctx, pkt);
			if (ret == AVERROR_EOF) {
				drainMode = DRAIN;
				avcodec_send_packet(cctx, NULL);
				log_write(DEBUG_AUDIO, "Decoder entering drain mode after EOF\n");
			} else if (pkt->stream_index != player->streamIdx) {
				av_packet_unref(pkt);
				continue;
			} else if (ret < 0) {
				char error[AV_ERROR_MAX_STRING_SIZE];
				if (av_strerror(ret, error, sizeof(error)) < 0) {
					strncpy(error, "(unknown)", sizeof(error) - 1);
				}
			log_write(DEBUG_AUDIO, "av_read_frame failed with code %i (%s)\n", ret, error);
			
			pthread_mutex_lock(&player->decoderLock);
			int flush_ret = av_buffersrc_add_frame(player->fabuf, NULL);
			(void)flush_ret;  /* Ignore return - flushing on error */
			pthread_cond_broadcast(&player->decoderCond);
			pthread_mutex_unlock(&player->decoderLock);
			break;
			} else {
				avcodec_send_packet(cctx, pkt);
			}
		}

		while (!shouldQuit(player)) {
			ret = avcodec_receive_frame(cctx, frame);
			if (ret == AVERROR_EOF) {
			drainMode = DONE;
			log_write(DEBUG_AUDIO, "Decoder drained, sending NULL frame\n");
			
			pthread_mutex_lock(&player->decoderLock);
			int flush_ret = av_buffersrc_add_frame(player->fabuf, NULL);
			(void)flush_ret;  /* Ignore return - flushing on drain */
			pthread_cond_broadcast(&player->decoderCond);
			pthread_mutex_unlock(&player->decoderLock);
			break;
			} else if (ret != 0) {
				break;
			}

			if (frame->pts == (int64_t)AV_NOPTS_VALUE) {
				frame->pts = 0;
			}
			
			pthread_mutex_lock(&player->decoderLock);
			ret = av_buffersrc_write_frame(player->fabuf, frame);
			assert(ret >= 0);
			pthread_cond_broadcast(&player->decoderCond);
			pthread_mutex_unlock(&player->decoderLock);
		}

		av_packet_unref(pkt);
	}
	
	av_frame_free(&frame);
	av_packet_free(&pkt);
	
	/* Mark decoding as finished */
	pthread_mutex_lock(&player->decoderLock);
	player->decodingFinished = true;
	pthread_cond_broadcast(&player->decoderCond);
	pthread_mutex_unlock(&player->decoderLock);
	
	log_write(DEBUG_AUDIO, "Decoder finished\n");
	return ret;
}

/*
 * ============================================================================
 * Sound Setup and Playback
 * ============================================================================
 */

static void audioTerminal (player_t *player, const char *operation) {
	pthread_mutex_lock (&player->lock);
	player->audioTerminalFailure = true;
	player->doQuit = true;
	++player->controlEpoch;
	audioCompleteLocked (player, PLAYER_AUDIO_FAILED, true);
	audioFatal (player, operation);
}

bool BarPlayerStopAudio (player_t *player) {
	if (player == NULL || !audioReserve (player, AUDIO_TEARDOWN, "stop")) { return false; }
	const bool live = player->audioState != PLAYER_AUDIO_NONE;
	pthread_mutex_unlock (&player->lock);
	if (!stopSoundReserved (player, live, "stop")) {
		audioTerminal (player, "stop");
		return false;
	}
	audioComplete (player, live ? PLAYER_AUDIO_STOPPED : PLAYER_AUDIO_NONE);
	return true;
}

static bool pauseControlCurrentLocked (const player_t *player, uint64_t epoch, BarPlayerMode mode) {
	return !player->doQuit && !player->audioTerminalFailure && player->controlEpoch == epoch &&
		player->mode == mode && playbackControlMode (player->mode);
}

/* Choose toggle direction only after claiming ownership. Each backend phase
 * releases player.lock, and decoder rearm/cancellation holds decoderLock alone. */
static bool setPaused (player_t *player, bool paused, bool toggle, bool *finalPaused,
		BarPlayerPlayStateSnapshot *snapshot) {
	if (player == NULL || !audioReserve (player, AUDIO_PLAYBACK_CONTROL, toggle ? "toggle" : paused ? "pause" : "resume")) {
		return false;
	}
	if (toggle) { paused = !player->doPause; }
	const BarPlayerAudioState state = player->audioState;
	const BarPlayerMode mode = player->mode;
	const bool live = state != PLAYER_AUDIO_NONE;
	if (paused == player->doPause && (paused ? state != PLAYER_AUDIO_RUNNING : state != PLAYER_AUDIO_STOPPED)) {
		if (finalPaused != NULL) { *finalPaused = player->doPause; }
		if (snapshot != NULL) { *snapshot = (BarPlayerPlayStateSnapshot) {player->doPause, player->controlEpoch}; }
		audioCompleteLocked (player, state, true);
		return true;
	}
	if (paused || !live) {
		player->doPause = paused;
		player->pauseStartTime = paused ? time (NULL) : 0;
		++player->controlEpoch;
		pthread_cond_broadcast (&player->cond);
	}
	const uint64_t epoch = player->controlEpoch;
	if (!live || (paused && state == PLAYER_AUDIO_STOPPED)) {
		if (finalPaused != NULL) { *finalPaused = player->doPause; }
		if (snapshot != NULL) { *snapshot = (BarPlayerPlayStateSnapshot) {player->doPause, player->controlEpoch}; }
		audioCompleteLocked (player, state, true);
		return true;
	}
	pthread_mutex_unlock (&player->lock);
	if (paused) {
		if (!stopSoundReserved (player, true, "pause")) {
			audioTerminal (player, "pause");
			return false;
		}
		pthread_mutex_lock (&player->lock);
		const bool current = pauseControlCurrentLocked (player, epoch, mode);
		if (finalPaused != NULL) { *finalPaused = player->doPause; }
		if (current && snapshot != NULL) { *snapshot = (BarPlayerPlayStateSnapshot) {player->doPause, player->controlEpoch}; }
		audioCompleteLocked (player, PLAYER_AUDIO_STOPPED, true);
		return current;
	}

	/* A retained node must be rearmed before a device can invoke its callback. */
	pthread_mutex_lock (&player->decoderLock);
	player->sourceReadCancelled = false;
	pthread_mutex_unlock (&player->decoderLock);
	pthread_mutex_lock (&player->lock);
	bool current = pauseControlCurrentLocked (player, epoch, mode);
	pthread_mutex_unlock (&player->lock);
	bool started = false;
	if (current && !ma_sound_at_end (&player->sound)) {
		const ma_result result = ma_sound_start (&player->sound);
		if (result != MA_SUCCESS) {
			log_write (LOG_ERROR, "Audio sound start failed: %d\n", result);
		} else {
			pthread_mutex_lock (&player->lock);
			current = pauseControlCurrentLocked (player, epoch, mode);
			pthread_mutex_unlock (&player->lock);
			if (current) { started = deviceSetStartedReserved (player, true, "resume/restart"); }
		}
	}
	pthread_mutex_lock (&player->lock);
	current = pauseControlCurrentLocked (player, epoch, mode);
	if (started && current) {
		player->doPause = false;
		player->pauseStartTime = 0;
		++player->controlEpoch;
		if (finalPaused != NULL) { *finalPaused = false; }
		if (snapshot != NULL) { *snapshot = (BarPlayerPlayStateSnapshot) {false, player->controlEpoch}; }
		audioCompleteLocked (player, PLAYER_AUDIO_RUNNING, true);
		return true;
	}
	pthread_mutex_unlock (&player->lock);
	/* Failure or supersession retains both lifetime and cancellation until the
 * physical start is rolled back. Only a current failure may change pause. */
	if (!stopSoundReserved (player, true, "resume rollback")) {
		audioTerminal (player, "resume rollback");
		return false;
	}
	pthread_mutex_lock (&player->lock);
	if (pauseControlCurrentLocked (player, epoch, mode) && !player->doPause) {
		player->doPause = true;
		player->pauseStartTime = time (NULL);
		++player->controlEpoch;
	}
	if (finalPaused != NULL) { *finalPaused = player->doPause; }
	audioCompleteLocked (player, PLAYER_AUDIO_STOPPED, true);
	return false;
}

bool BarPlayerSetPaused (player_t *player, bool paused) {
	return BarPlayerSetPausedWithSnapshot (player, paused, NULL);
}

bool BarPlayerTogglePaused (player_t *player, bool *paused) {
	return setPaused (player, false, true, paused, NULL);
}

bool BarPlayerSetPausedWithSnapshot (player_t *player, bool paused,
		BarPlayerPlayStateSnapshot *snapshot) {
	return setPaused (player, paused, false, NULL, snapshot);
}

bool BarPlayerTogglePausedWithSnapshot (player_t *player,
		BarPlayerPlayStateSnapshot *snapshot) {
	return setPaused (player, false, true, NULL, snapshot);
}

void BarPlayerRequestStop (player_t *player) {
	if (player == NULL) { return; }
	/* Publish before waiting so the current owner cannot commit stale success. */
	pthread_mutex_lock (&player->lock);
	player->doQuit = true;
	++player->controlEpoch;
	pthread_cond_broadcast (&player->cond);
	pthread_cond_broadcast (&player->audioCond);
	pthread_mutex_unlock (&player->lock);
	if (!audioReserve (player, AUDIO_TEARDOWN, "request stop")) { return; }
	const bool live = player->audioState != PLAYER_AUDIO_NONE;
	player->doPause = false;
	player->pauseStartTime = 0;
	pthread_cond_broadcast (&player->cond);
	pthread_mutex_unlock (&player->lock);
	if (!stopSoundReserved (player, live, "request stop")) {
		audioTerminal (player, "request stop");
		return;
	}
	audioComplete (player, live ? PLAYER_AUDIO_STOPPED : PLAYER_AUDIO_NONE);
	/* Ownership and player.lock are released before the final decoder wake. */
	pthread_mutex_lock (&player->decoderLock);
	pthread_cond_broadcast (&player->decoderCond);
	pthread_mutex_unlock (&player->decoderLock);
}

bool BarPlayerGetAudioSnapshot (player_t *player, BarPlayerAudioSnapshot *snapshot) {
	if (player == NULL || snapshot == NULL || pthread_mutex_trylock (&player->lock) != 0) {
		return false;
	}
	if (player->audioBusy && player->audioOwnerValid && pthread_equal (player->audioOwner, pthread_self ())) {
		pthread_mutex_unlock (&player->lock);
		log_write (LOG_ERROR, "Recursive audio reservation rejected: snapshot\n");
		return false;
	}
	if (player->audioBusy || player->audioControlWaiters != 0 || player->mode != PLAYER_PLAYING ||
			(player->audioState != PLAYER_AUDIO_RUNNING && player->audioState != PLAYER_AUDIO_STOPPED) ||
			!BarPlayerAudioReserveLocked (player, AUDIO_OBSERVATION, "snapshot")) {
		pthread_mutex_unlock (&player->lock);
		return false;
	}
	const BarPlayerAudioState state = player->audioState;
	const uint64_t epoch = player->controlEpoch;
	pthread_mutex_unlock (&player->lock);
	BarPlayerAudioSnapshot sample = {.state = state};
	/* Source callbacks mutate the FFmpeg cursor under decoderLock. Never
	 * hold it together with player.lock; the reservation retains lifetime. */
	pthread_mutex_lock (&player->decoderLock);
	const ma_result frames = ma_sound_get_cursor_in_pcm_frames (&player->sound, &sample.cursorFrames);
	const ma_result seconds = ma_sound_get_cursor_in_seconds (&player->sound, &sample.cursorSeconds);
	pthread_mutex_unlock (&player->decoderLock);
	sample.playing = ma_sound_is_playing (&player->sound) != MA_FALSE;
	sample.atEnd = ma_sound_at_end (&player->sound) != MA_FALSE;
	ma_device *device = ma_engine_get_device (&player->engine);
	sample.deviceStarted = device != NULL && ma_device_is_started (device);
	pthread_mutex_lock (&player->lock);
	const bool current = !player->doQuit && player->mode == PLAYER_PLAYING &&
		player->controlEpoch == epoch;
	if (current && frames == MA_SUCCESS && seconds == MA_SUCCESS) { *snapshot = sample; }
	audioCompleteLocked (player, state, false);
	return current && frames == MA_SUCCESS && seconds == MA_SUCCESS;
}

typedef enum { SETUP_PLAYING, SETUP_SUPERSEDED, SETUP_HARDFAIL } SetupResult;

static bool setupCurrentLocked (const player_t *player, uint64_t epoch) {
	return player->controlEpoch == epoch && player->mode == PLAYER_WAITING && !player->doQuit;
}

static SetupResult setupSound (player_t *player, uint64_t *setupEpoch) {
	if (!audioReserve (player, AUDIO_NORMAL, "setup")) { return SETUP_SUPERSEDED; }
	if (player->mode != PLAYER_WAITING) {
		audioCompleteLocked (player, player->audioState, false);
		return SETUP_SUPERSEDED;
	}
	const uint64_t epoch = player->controlEpoch;
	*setupEpoch = epoch;
	const bool paused = player->doPause;
	const int volume = player->requestedVolume;
	const double gain = player->gain, gainMul = player->settings->gainMul;
	pthread_mutex_unlock (&player->lock);
	SetupResult outcome = SETUP_HARDFAIL;
	bool live = false;
	if (!player->engineInitialized) {
		log_write (LOG_ERROR, "Audio engine not initialized\n");
		goto cleanup;
	}
	ma_result result = ffmpeg_data_source_init (&player->dataSource, player);
	if (result != MA_SUCCESS) {
		log_write (LOG_ERROR, "Failed to init data source: %d\n", result);
		goto cleanup;
	}
	player->dataSourceInitialized = true;
	result = ma_sound_init_from_data_source (&player->engine, &player->dataSource,
		0, NULL, &player->sound);
	if (result != MA_SUCCESS) {
		log_write (LOG_ERROR, "Failed to init sound: %d\n", result);
		goto cleanup;
	}
	live = true;
	ma_sound_set_end_callback (&player->sound, onSongEnd, player);
	setVolumeReserved (player, volume, gain, gainMul);
	pthread_mutex_lock (&player->lock);
	bool current = setupCurrentLocked (player, epoch);
	pthread_mutex_unlock (&player->lock);
	if (!current) { outcome = SETUP_SUPERSEDED; goto cleanup; }
	if (!paused) {
		/* Some backends synchronously prime the engine during device start.
		 * Keep this fresh node stopped so priming reads silence, not frames
		 * that this same worker cannot decode until setup returns. */
		if (!deviceSetStartedReserved (player, true, "start/restart")) { goto cleanup; }
		pthread_mutex_lock (&player->lock);
		current = setupCurrentLocked (player, epoch);
		pthread_mutex_unlock (&player->lock);
		if (!current) { outcome = SETUP_SUPERSEDED; goto cleanup; }
		result = ma_sound_start (&player->sound);
		if (result != MA_SUCCESS) {
			log_write (LOG_ERROR, "Failed to start sound: %d\n", result);
			goto cleanup;
		}
		/* A stop can publish quit/epoch while setup owns the reservation. */
		pthread_mutex_lock (&player->lock);
		current = setupCurrentLocked (player, epoch);
		pthread_mutex_unlock (&player->lock);
		if (!current) { outcome = SETUP_SUPERSEDED; goto cleanup; }
	}
	pthread_mutex_lock (&player->lock);
	if (setupCurrentLocked (player, epoch)) {
		player->soundInitialized = true;
		player->mode = PLAYER_PLAYING;
		++player->controlEpoch;
		audioCompleteLocked (player, paused ? PLAYER_AUDIO_STOPPED : PLAYER_AUDIO_RUNNING, true);
		return SETUP_PLAYING;
	}
	pthread_mutex_unlock (&player->lock);
	outcome = SETUP_SUPERSEDED;
cleanup:
	if (!cleanupSoundReserved (player, live, "start rollback")) {
		pthread_mutex_lock (&player->lock);
		player->soundInitialized = live;
		pthread_mutex_unlock (&player->lock);
		audioTerminal (player, "setup rollback");
		return SETUP_HARDFAIL;
	}
	pthread_mutex_lock (&player->lock);
	player->soundInitialized = false;
	/* Allocation/start failures can be superseded just like successful setup.
	 * Recheck after rollback too, since backend cleanup released the lock. */
	if (!setupCurrentLocked (player, epoch)) { outcome = SETUP_SUPERSEDED; }
	audioCompleteLocked (player, PLAYER_AUDIO_NONE, false);
	return outcome;
}

static bool cleanupSound (player_t *player) {
	if (!audioReserve (player, AUDIO_TEARDOWN, "cleanup")) { return false; }
	const bool live = player->audioState != PLAYER_AUDIO_NONE;
	pthread_mutex_unlock (&player->lock);
	if (!cleanupSoundReserved (player, live, "cleanup")) {
		audioTerminal (player, "cleanup");
		return false;
	}
	pthread_mutex_lock (&player->lock);
	player->soundInitialized = false;
	audioCompleteLocked (player, PLAYER_AUDIO_NONE, true);
	return true;
}

static bool finish(player_t * const player) {
	logRSSAudio("at finish() start");

	/* Clean up miniaudio sound */
	if (!cleanupSound (player)) { return false; }
	logRSSAudio("after cleanupSound");
	
	/* Drain any remaining frames from buffersink before freeing graph.
	 * Frames can accumulate if playback stops mid-song or if miniaudio
	 * didn't consume all decoded frames. */
	if (player->fbufsink != NULL) {
		AVFrame *drainFrame = av_frame_alloc();
		if (drainFrame) {
			int drainCount = 0;
			while (av_buffersink_get_frame(player->fbufsink, drainFrame) >= 0) {
				av_frame_unref(drainFrame);
				drainCount++;
			}
			av_frame_free(&drainFrame);
			if (drainCount > 0) {
				log_write(DEBUG_AUDIO, "Drained %d frames from buffersink\n", drainCount);
			}
		}
	}
	logRSSAudio("after drain");

	/* Clean up ffmpeg resources */
	if (player->fgraph != NULL) {
		avfilter_graph_free(&player->fgraph);
		player->fgraph = NULL;
	}
	logRSSAudio("after avfilter_graph_free");

	if (player->cctx != NULL) {
		avcodec_free_context(&player->cctx);
		player->cctx = NULL;
	}
	logRSSAudio("after avcodec_free_context");

	if (player->fctx != NULL) {
		avformat_close_input(&player->fctx);
	}
	logRSSAudio("after avformat_close_input");

	logRSSAudio("song cleanup complete (frames alloc=%ld, freed=%ld, delta=%ld)",
	            g_framesAllocated, g_framesFreed,
	            g_framesAllocated - g_framesFreed);

#if defined(__GLIBC__)
	{
		const long rssNow = getCurrentRSSKB();
		if (rssNow > 0) {
			const long growth = (s_rssAfterLastTrimKb > 0)
			                    ? (rssNow - s_rssAfterLastTrimKb)
			                    : 0;
			if (s_rssAfterLastTrimKb < 0 || growth >= 512) {
				malloc_trim(0);
				s_rssAfterLastTrimKb = getCurrentRSSKB();
				logRSSAudio("after malloc_trim (reclaimed %ld KB)",
				            rssNow - s_rssAfterLastTrimKb);
			}
		}
	}
#endif
	return true;
}

/*
 * ============================================================================
 * Player Thread - Main Entry Point
 * ============================================================================
 */

void *BarPlayerThread(void *data) {
	assert(data != NULL);

	player_t * const player = data;
	uintptr_t pret = PLAYER_RET_OK;

	bool retry;
	do {
		retry = false;

		/* Check quit before starting/retrying */
		if (shouldQuit(player)) {
			log_write(DEBUG_AUDIO, "Player: Quit detected before stream open\n");
			break;
		}

		logRSSAudio("before openStream");

		bool staleCdn403 = false;
		if (openStream(player, &staleCdn403)) {
			logRSSAudio("after openStream");

			pthread_mutex_lock (&player->lock);
			uint64_t setupEpoch = player->controlEpoch;
			pthread_mutex_unlock (&player->lock);
			const SetupResult setup = openFilter (player) ? setupSound (player, &setupEpoch) : SETUP_HARDFAIL;
			if (setup != SETUP_PLAYING) {
				if (!finish (player)) { return (void *)PLAYER_RET_HARDFAIL; }
				pthread_mutex_lock (&player->lock);
				const bool failedCurrentSetup = setup == SETUP_HARDFAIL && setupCurrentLocked (player, setupEpoch);
				/* The worker is exiting even when a newer control superseded
				 * setup. Publish completion only from WAITING, preserving any
				 * newer terminal mode and waking the manager to join us. */
				if (player->mode == PLAYER_WAITING) {
					player->mode = PLAYER_FINISHED;
					++player->controlEpoch;
					pthread_cond_broadcast (&player->cond);
				}
				pthread_mutex_unlock (&player->lock);
				return (void *)(uintptr_t)(failedCurrentSetup ? PLAYER_RET_HARDFAIL : PLAYER_RET_OK);
			}
			if (setup == SETUP_PLAYING) {
				logRSSAudio("after openFilter+setupSound");

				/* Run decoder - feeds frames to filter chain which miniaudio reads from */
				const int ret = decode(player);
				logRSSAudio("after decode");

				/* Check quit after decode completes */
				if (shouldQuit(player)) {
					log_write(DEBUG_AUDIO, "Player: Quit detected after decode\n");
					if (!finish (player)) { return (void *)PLAYER_RET_HARDFAIL; }
					break;
				}

				/* Wait for playback to complete (end callback will signal) */
				while (!shouldQuit(player) && BarPlayerGetMode(player) == PLAYER_PLAYING) {
					pthread_mutex_lock (&player->lock);
					while (player->doPause && !player->doQuit && player->mode == PLAYER_PLAYING) {
						pthread_cond_wait (&player->cond, &player->lock);
					}
					const bool observe = !player->doQuit && player->mode == PLAYER_PLAYING;
					pthread_mutex_unlock (&player->lock);
					if (!observe) { break; }

					/* Update progress from miniaudio's cursor */
					BarPlayerAudioSnapshot sample;
					if (BarPlayerGetAudioSnapshot (player, &sample)) {
						pthread_mutex_lock(&player->lock);
						player->songPlayed = (unsigned int)sample.cursorSeconds;
						if (sample.atEnd && player->mode == PLAYER_PLAYING && !player->doQuit) {
							player->mode = PLAYER_FINISHED;
							++player->controlEpoch;
							pthread_cond_broadcast (&player->cond);
						}
						pthread_mutex_unlock(&player->lock);
					}

					pthread_mutex_lock (&player->lock);
					/* Keep the active 100 ms cursor cadence. A pause/mode/quit
					 * broadcast ends this tick; sustained pause waits above. */
					struct timespec deadline;
					clock_gettime (CLOCK_REALTIME, &deadline);
					deadline.tv_nsec += 100000000L;
					if (deadline.tv_nsec >= 1000000000L) {
						++deadline.tv_sec;
						deadline.tv_nsec -= 1000000000L;
					}
					int waitResult = 0;
					while (!player->doPause && !player->doQuit && player->mode == PLAYER_PLAYING && waitResult == 0) {
						waitResult = pthread_cond_timedwait (&player->cond, &player->lock, &deadline);
					}
					pthread_mutex_unlock (&player->lock);
				}

				/* Check quit after playback before retry logic */
				if (shouldQuit(player)) {
					log_write(DEBUG_AUDIO, "Player: Quit detected after playback\n");
					if (!finish (player)) { return (void *)PLAYER_RET_HARDFAIL; }
					break;
				}

				retry = (ret == AVERROR_INVALIDDATA || ret == -ECONNRESET) &&
						(atomic_load_explicit (&player->interrupted, memory_order_relaxed) == 0);
			}
		} else {
			if (staleCdn403) {
				pret = PLAYER_RET_STALE_URLS;
			} else {
				pret = PLAYER_RET_SOFTFAIL;
			}
		}
		BarPlayerSetMode(player, PLAYER_WAITING);
		if (!finish (player)) { return (void *)PLAYER_RET_HARDFAIL; }

		/* Check quit after cleanup before retry */
		if (shouldQuit(player)) {
			log_write(DEBUG_AUDIO, "Player: Quit detected after cleanup\n");
			break;
		}
	} while (retry);

	BarPlayerSetMode(player, PLAYER_FINISHED);

	return (void *) pret;
}
