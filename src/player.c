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
#include "miniaudio.h"

#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <assert.h>
#include <inttypes.h>
#include <time.h>

#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#ifdef HAVE_LIBAVFILTER_AVCODEC_H
#include <libavfilter/avcodec.h>
#endif
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/frame.h>

#include "player.h"
#include "debug.h"
#include "ui.h"
#include "ui_types.h"

/* default sample format */
const enum AVSampleFormat avformat = AV_SAMPLE_FMT_S16;

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
	
	/* Check if paused - output silence */
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
	
	while (framesRead < frameCount) {
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
			debugPrint(DEBUG_AUDIO, "FFmpeg data source reached EOF at frame %" PRIu64 "\n", 
			           pFFmpeg->cursor);
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
			
			/* Check for quit after waking */
			if (shouldQuit(player)) {
				break;
			}
			continue;
		} else if (ret < 0) {
			/* Error */
			av_frame_free(&newFrame);
			debugPrint(DEBUG_AUDIO, "FFmpeg buffersink error: %d\n", ret);
			break;
		}
		
		/* Got a new frame - store it as the buffered frame for consumption */
		pFFmpeg->bufferedFrame = newFrame;
		pFFmpeg->bufferedFrameOffset = 0;
		/* Loop will consume from it on next iteration */
	}
	
	pthread_mutex_unlock(&player->decoderLock);
	
	/* Fill remaining with silence if we didn't get enough */
	if (framesRead < frameCount) {
		memset(output + (framesRead * pFFmpeg->channels), 0, 
		       (frameCount - framesRead) * pFFmpeg->channels * sizeof(int16_t));
	}
	
	if (pFramesRead != NULL) {
		*pFramesRead = framesRead;
	}
	
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
	
	debugPrint(DEBUG_AUDIO, "FFmpeg data source initialized: %u Hz, %u channels, %" PRIu64 " total frames (%.1f sec)\n",
	           pFFmpeg->sampleRate, pFFmpeg->channels, pFFmpeg->totalFrames, durationSecs);
	
	return MA_SUCCESS;
}

static void ffmpeg_data_source_uninit(ffmpeg_data_source_t* pFFmpeg) {
	/* Free any buffered frame */
	if (pFFmpeg->bufferedFrame != NULL) {
		av_frame_free(&pFFmpeg->bufferedFrame);
		pFFmpeg->bufferedFrame = NULL;
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
	
	debugPrint(DEBUG_AUDIO, "Song end callback fired\n");
	
	pthread_mutex_lock(&player->lock);
	player->mode = PLAYER_FINISHED;
	pthread_cond_broadcast(&player->cond);
	pthread_mutex_unlock(&player->lock);
}

/*
 * ============================================================================
 * Player Initialization and Cleanup
 * ============================================================================
 */

void BarPlayerInit(player_t * const p, const BarSettings_t * const settings) {

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

	pthread_mutex_init(&p->lock, NULL);
	pthread_cond_init(&p->cond, NULL);
	pthread_mutex_init(&p->decoderLock, NULL);
	pthread_cond_init(&p->decoderCond, NULL);
	
	/* Initialize miniaudio engine once
	 * On macOS, engine MUST be initialized AFTER fork to avoid CoreAudio thread issues.
	 * CoreAudio creates background threads that don't survive fork properly. */
	
	if (p->engineInitialized) {
		// Already initialized, just reset and return
		BarPlayerReset(p);
		p->settings = settings;
		return;
	}
	
	ma_engine_config engineConfig = ma_engine_config_init();
	engineConfig.noDevice = MA_FALSE;
	ma_result result = ma_engine_init(&engineConfig, &p->engine);
	if (result != MA_SUCCESS) {
		fprintf(stderr, "Failed to initialize audio engine: %d\n", result);
		p->engineInitialized = false;
	} else {
		p->engineInitialized = true;
		debugPrint(DEBUG_AUDIO, "Audio engine initialized (sample rate: %u)\n",
		           ma_engine_get_sample_rate(&p->engine));
	}
	
	BarPlayerReset(p);
	p->settings = settings;
}

void BarPlayerDestroy(player_t * const p) {
	/* Uninit engine */
	if (p->engineInitialized) {
		debugPrint(DEBUG_AUDIO, "BarPlayerDestroy: Stopping engine before uninit\n");
		
		/* Stop engine playback before uninit to avoid audio drain delay on Linux */
		ma_engine_stop(&p->engine);
		
		debugPrint(DEBUG_AUDIO, "BarPlayerDestroy: Calling ma_engine_uninit\n");
		ma_engine_uninit(&p->engine);
		debugPrint(DEBUG_AUDIO, "BarPlayerDestroy: ma_engine_uninit completed\n");
		
		p->engineInitialized = false;
	}
	
	pthread_cond_destroy(&p->cond);
	pthread_mutex_destroy(&p->lock);
	pthread_cond_destroy(&p->decoderCond);
	pthread_mutex_destroy(&p->decoderLock);

#ifdef HAVE_AVFORMAT_NETWORK_INIT
	avformat_network_deinit();
#endif
}

void BarPlayerReset(player_t * const p) {
	// #region agent log
	debugPrint(DEBUG_AUDIO, "AGENT_LOG: BarPlayerReset ENTRY soundInit=%d\n", p->soundInitialized);
	// #endregion
	
	/* Clean up sound from previous song */
	if (p->soundInitialized) {
		/* Stop the sound (not the engine!) before uninit to prevent audio drain delay.
		 * ma_sound_stop() stops this specific sound instance.
		 * ma_engine_stop() would stop ALL sounds and the engine itself (wrong!).
		 * The engine must keep running for the next song. */
		// #region agent log
		debugPrint(DEBUG_AUDIO, "AGENT_LOG: Calling ma_sound_stop [H1,H2]\n");
		// #endregion
		ma_sound_stop(&p->sound);
		// #region agent log
		debugPrint(DEBUG_AUDIO, "AGENT_LOG: ma_sound_stop completed [H1]\n");
		debugPrint(DEBUG_AUDIO, "AGENT_LOG: Calling ma_sound_uninit [H2]\n");
		// #endregion
		
		ma_sound_uninit(&p->sound);
		// #region agent log
		debugPrint(DEBUG_AUDIO, "AGENT_LOG: ma_sound_uninit completed [H2]\n");
		// #endregion
		debugPrint(DEBUG_AUDIO, "Cleaned up old sound in reset\n");
	}
	p->soundInitialized = false;
	// #region agent log
	debugPrint(DEBUG_AUDIO, "AGENT_LOG: BarPlayerReset EXIT\n");
	// #endregion
	
	/* Free any buffered frame in the data source before zeroing */
	if (p->dataSource.bufferedFrame != NULL) {
		av_frame_free(&p->dataSource.bufferedFrame);
		p->dataSource.bufferedFrame = NULL;
	}
	
	/* Reset all fields */
	p->doQuit = false;
	p->doPause = false;
	p->pauseStartTime = 0;
	p->songDuration = 0;
	p->songPlayed = 0;
	p->mode = PLAYER_DEAD;
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
}

/*
 * ============================================================================
 * Volume Control
 * ============================================================================
 */

void BarPlayerSetVolume(player_t * const player) {
	assert(player != NULL);

	if (!player->soundInitialized) {
		return;
	}

	/* User volume: 0-100 linear scale -> 0.0-1.0 */
	float userVolume = (float)player->settings->volume / 100.0f;
	
	/* ReplayGain: convert dB to linear multiplier */
	float replayGain = powf(10.0f, (player->gain * player->settings->gainMul) / 20.0f);
	
	/* Apply combined volume to miniaudio sound */
	ma_sound_set_volume(&player->sound, userVolume * replayGain);
}

/*
 * ============================================================================
 * Stream and Filter Setup
 * ============================================================================
 */

#define softfail(msg) \
	printError(player->settings, msg, ret); \
	return false;

/* ffmpeg callback for blocking functions */
static int intCb(void * const data) {
	player_t * const player = data;
	assert(player != NULL);
	if (player->interrupted > 1) {
		pthread_mutex_lock(&player->lock);
		player->doQuit = true;
		pthread_mutex_unlock(&player->lock);
		return 1;
	} else if (player->interrupted != 0) {
		player->interrupted = 0;
		return 1;
	} else {
		return 0;
	}
}

static bool openStream(player_t * const player) {
	assert(player != NULL);
	assert(player->fctx == NULL);

	int ret;

	player->fctx = avformat_alloc_context();
	player->fctx->interrupt_callback.callback = intCb;
	player->fctx->interrupt_callback.opaque = player;

	unsigned long int timeout = player->settings->timeout * 1000000;
	char timeoutStr[16];
	ret = snprintf(timeoutStr, sizeof(timeoutStr), "%lu", timeout);
	assert(ret < (int)sizeof(timeoutStr));
	AVDictionary *options = NULL;
	av_dict_set(&options, "timeout", timeoutStr, 0);

	assert(player->url != NULL);
	if ((ret = avformat_open_input(&player->fctx, player->url, NULL, &options)) < 0) {
		softfail("Unable to open audio file");
	}

	if ((ret = avformat_find_stream_info(player->fctx, NULL)) < 0) {
		softfail("find_stream_info");
	}

	for (size_t i = 0; i < player->fctx->nb_streams; i++) {
		player->fctx->streams[i]->discard = AVDISCARD_ALL;
	}

	player->streamIdx = av_find_best_stream(player->fctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (player->streamIdx < 0) {
		softfail("find_best_stream");
	}

	player->st = player->fctx->streams[player->streamIdx];
	player->st->discard = AVDISCARD_DEFAULT;

	if ((player->cctx = avcodec_alloc_context3(NULL)) == NULL) {
		softfail("avcodec_alloc_context3");
	}
	const AVCodecParameters * const cp = player->st->codecpar;
	if ((ret = avcodec_parameters_to_context(player->cctx, cp)) < 0) {
		softfail("avcodec_parameters_to_context");
	}

	const AVCodec * const decoder = avcodec_find_decoder(cp->codec_id);
	if (decoder == NULL) {
		softfail("find_decoder");
	}

	if ((ret = avcodec_open2(player->cctx, decoder, NULL)) < 0) {
		softfail("codec_open2");
	}

	if (player->lastTimestamp > 0) {
		av_seek_frame(player->fctx, player->streamIdx, player->lastTimestamp, 0);
	}

	const unsigned int songDuration = av_q2d(player->st->time_base) *
			(double)player->st->duration;
	pthread_mutex_lock(&player->lock);
	player->songPlayed = 0;
	player->songDuration = songDuration;
	pthread_mutex_unlock(&player->lock);

	return true;
}

static int getSampleRate(const player_t * const player) {
	AVCodecParameters const * const cp = player->st->codecpar;
	return player->settings->sampleRate == 0 ?
			cp->sample_rate :
			player->settings->sampleRate;
}

static bool openFilter(player_t * const player) {
	char strbuf[256];
	int ret = 0;
	AVCodecParameters * const cp = player->st->codecpar;

	if ((player->fgraph = avfilter_graph_alloc()) == NULL) {
		softfail("graph_alloc");
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
		softfail("create_filter abuffer");
	}

	/* Create aformat filter (sample format conversion) */
	AVFilterContext *fafmt = NULL;
	snprintf(strbuf, sizeof(strbuf), "sample_fmts=%s:sample_rates=%d",
			av_get_sample_fmt_name(avformat), getSampleRate(player));
	if ((ret = avfilter_graph_create_filter(&fafmt,
					avfilter_get_by_name("aformat"), "format", strbuf, NULL,
					player->fgraph)) < 0) {
		softfail("create_filter aformat");
	}

	/* Create abuffersink (sink) filter */
	if ((ret = avfilter_graph_create_filter(&player->fbufsink,
			avfilter_get_by_name("abuffersink"), "sink", NULL, NULL,
			player->fgraph)) < 0) {
		softfail("create_filter abuffersink");
	}

	/* Link filters: abuffer -> aformat -> abuffersink
	 * (volume control is handled by miniaudio, not FFmpeg) */
	if (avfilter_link(player->fabuf, 0, fafmt, 0) != 0 ||
			avfilter_link(fafmt, 0, player->fbufsink, 0) != 0) {
		softfail("filter_link");
	}

	if ((ret = avfilter_graph_config(player->fgraph, NULL)) < 0) {
		softfail("graph_config");
	}

	return true;
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

bool BarPlayerIsPaused(player_t * const player) {
	pthread_mutex_lock(&player->lock);
	const bool ret = player->doPause;
	pthread_mutex_unlock(&player->lock);
	return ret;
}

static void changeMode(player_t * const player, unsigned int mode) {
	pthread_mutex_lock(&player->lock);
	player->mode = mode;
	pthread_cond_broadcast(&player->cond);
	pthread_mutex_unlock(&player->lock);
}

BarPlayerMode BarPlayerGetMode(player_t * const player) {
	pthread_mutex_lock(&player->lock);
	const BarPlayerMode ret = player->mode;
	pthread_mutex_unlock(&player->lock);
	return ret;
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
			ret = av_read_frame(player->fctx, pkt);
			if (ret == AVERROR_EOF) {
				drainMode = DRAIN;
				avcodec_send_packet(cctx, NULL);
				debugPrint(DEBUG_AUDIO, "Decoder entering drain mode after EOF\n");
			} else if (pkt->stream_index != player->streamIdx) {
				av_packet_unref(pkt);
				continue;
			} else if (ret < 0) {
				char error[AV_ERROR_MAX_STRING_SIZE];
				if (av_strerror(ret, error, sizeof(error)) < 0) {
					strncpy(error, "(unknown)", sizeof(error) - 1);
				}
			debugPrint(DEBUG_AUDIO, "av_read_frame failed with code %i (%s)\n", ret, error);
			
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
			debugPrint(DEBUG_AUDIO, "Decoder drained, sending NULL frame\n");
			
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
	
	debugPrint(DEBUG_AUDIO, "Decoder finished\n");
	return ret;
}

/*
 * ============================================================================
 * Sound Setup and Playback
 * ============================================================================
 */

static bool setupSound(player_t * const player) {
	if (!player->engineInitialized) {
		BarUiMsg(player->settings, MSG_ERR, "Audio engine not initialized\n");
		return false;
	}
	
	/* Initialize the ffmpeg data source */
	ma_result result = ffmpeg_data_source_init(&player->dataSource, player);
	if (result != MA_SUCCESS) {
		BarUiMsg(player->settings, MSG_ERR, "Failed to init data source: %d\n", result);
		return false;
	}
	
	/* Create sound from data source */
	result = ma_sound_init_from_data_source(&player->engine, 
	                                        &player->dataSource, 
	                                        0,    /* flags */
	                                        NULL, /* group */
	                                        &player->sound);
	if (result != MA_SUCCESS) {
		BarUiMsg(player->settings, MSG_ERR, "Failed to init sound: %d\n", result);
		ffmpeg_data_source_uninit(&player->dataSource);
		return false;
	}
	
	player->soundInitialized = true;
	
	/* Set end callback for clean completion detection */
	ma_sound_set_end_callback(&player->sound, onSongEnd, player);
	
	/* Start playback */
	result = ma_sound_start(&player->sound);
	if (result != MA_SUCCESS) {
		BarUiMsg(player->settings, MSG_ERR, "Failed to start sound: %d\n", result);
		ma_sound_uninit(&player->sound);
		player->soundInitialized = false;
		ffmpeg_data_source_uninit(&player->dataSource);
		return false;
	}
	
	debugPrint(DEBUG_AUDIO, "Sound started successfully\n");
	return true;
}

static void cleanupSound(player_t * const player) {
	// #region agent log
	debugPrint(DEBUG_AUDIO, "AGENT_LOG: cleanupSound ENTRY soundInit=%d [H6,H7]\n", player->soundInitialized);
	// #endregion
	
	if (player->soundInitialized) {
		ma_sound_stop(&player->sound);
		
		/* Stop engine before uninit to prevent audio drain delay on Linux (ALSA/PulseAudio).
		 * Without this, ma_sound_uninit() can hang for 15+ seconds on Ubuntu.
		 * Same fix as in BarPlayerDestroy() (commit 1d2352d). */
		if (player->engineInitialized) {
			// #region agent log
			debugPrint(DEBUG_AUDIO, "AGENT_LOG: cleanupSound calling ma_engine_stop [H6]\n");
			// #endregion
			ma_engine_stop(&player->engine);
			// #region agent log
			debugPrint(DEBUG_AUDIO, "AGENT_LOG: cleanupSound ma_engine_stop completed [H6]\n");
			// #endregion
		}
		
		ma_sound_uninit(&player->sound);
		player->soundInitialized = false;
		debugPrint(DEBUG_AUDIO, "Sound cleaned up\n");
	}
	
	// #region agent log
	debugPrint(DEBUG_AUDIO, "AGENT_LOG: cleanupSound EXIT [H6,H7]\n");
	// #endregion
	
	ffmpeg_data_source_uninit(&player->dataSource);
}

static void finish(player_t * const player) {
	/* Clean up miniaudio sound */
	cleanupSound(player);
	
	/* Clean up ffmpeg resources */
	if (player->fgraph != NULL) {
		avfilter_graph_free(&player->fgraph);
		player->fgraph = NULL;
	}
	if (player->cctx != NULL) {
		avcodec_free_context(&player->cctx);
		player->cctx = NULL;
	}
	if (player->fctx != NULL) {
		avformat_close_input(&player->fctx);
	}
	
	debugPrint(DEBUG_AUDIO, "Finish cleanup complete\n");
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
			debugPrint(DEBUG_AUDIO, "Player: Quit detected before stream open\n");
			break;
		}
		
		if (openStream(player)) {
			if (openFilter(player) && setupSound(player)) {
				changeMode(player, PLAYER_PLAYING);
				BarPlayerSetVolume(player);
				
			/* Run decoder - feeds frames to filter chain which miniaudio reads from */
			const int ret = decode(player);
			
			/* Check quit after decode completes */
			if (shouldQuit(player)) {
				debugPrint(DEBUG_AUDIO, "Player: Quit detected after decode\n");
				break;
			}
			
		/* Wait for playback to complete (end callback will signal) */
		while (!shouldQuit(player) && BarPlayerGetMode(player) == PLAYER_PLAYING) {
			/* Check quit first and stop audio immediately */
			if (shouldQuit(player)) {
				// #region agent log
				debugPrint(DEBUG_AUDIO, "AGENT_LOG: Player thread detected quit [H4]\n");
				// #endregion
				debugPrint(DEBUG_AUDIO, "Player: Quit requested, stopping sound immediately\n");
				if (player->soundInitialized) {
					ma_sound_stop(&player->sound);
					// #region agent log
					debugPrint(DEBUG_AUDIO, "AGENT_LOG: Player thread stopped sound [H5]\n");
					// #endregion
				}
				break;
			}
				
				/* Update progress from miniaudio's cursor */
				float cursor;
				if (ma_sound_get_cursor_in_seconds(&player->sound, &cursor) == MA_SUCCESS) {
					pthread_mutex_lock(&player->lock);
					player->songPlayed = (unsigned int)cursor;
					pthread_mutex_unlock(&player->lock);
				}
				
				/* Check if song ended */
				if (ma_sound_at_end(&player->sound)) {
					debugPrint(DEBUG_AUDIO, "ma_sound_at_end() returned true\n");
					changeMode(player, PLAYER_FINISHED);
					break;
				}
				
			usleep(100000);  /* 100ms update interval */
		}
			
		/* Check quit after playback before retry logic */
		if (shouldQuit(player)) {
			// #region agent log
			debugPrint(DEBUG_AUDIO, "AGENT_LOG: Quit after playback, about to break [H6]\n");
			// #endregion
			debugPrint(DEBUG_AUDIO, "Player: Quit detected after playback\n");
			break;
		}
		
		// #region agent log
		debugPrint(DEBUG_AUDIO, "AGENT_LOG: Checking retry logic [H6]\n");
		// #endregion
		
		retry = (ret == AVERROR_INVALIDDATA ||
					 ret == -ECONNRESET) &&
					!player->interrupted;
		} else {
			pret = PLAYER_RET_HARDFAIL;
		}
	} else {
		pret = PLAYER_RET_SOFTFAIL;
}
// #region agent log
debugPrint(DEBUG_AUDIO, "AGENT_LOG: After nested blocks, before changeMode [H6]\n");
// #endregion
changeMode(player, PLAYER_WAITING);
// #region agent log
debugPrint(DEBUG_AUDIO, "AGENT_LOG: Calling finish() [H6,H7]\n");
// #endregion
finish(player);
// #region agent log
debugPrint(DEBUG_AUDIO, "AGENT_LOG: finish() completed [H6,H7]\n");
// #endregion

/* Check quit after cleanup before retry */
if (shouldQuit(player)) {
	debugPrint(DEBUG_AUDIO, "Player: Quit detected after cleanup\n");
	break;
}
} while (retry);

// #region agent log
debugPrint(DEBUG_AUDIO, "AGENT_LOG: AFTER do-while loop, about to set PLAYER_FINISHED\n");
// #endregion

changeMode(player, PLAYER_FINISHED);

// #region agent log
debugPrint(DEBUG_AUDIO, "AGENT_LOG: Thread exiting normally\n");
// #endregion

return (void *)pret;
}
