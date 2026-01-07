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

/* receive/play audio stream.
 *
 * There are two threads involved here:
 * BarPlayerThread
 * 		Sets up the stream and fetches the data into a ffmpeg buffersrc
 * miniaudio callback (maDataCallback)
 * 		Reads data from the filter chain's sink and passes it to miniaudio device
 * 
 */

#include "config.h"

/* miniaudio implementation - must be defined before including player.h */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <fcntl.h>
#include <limits.h>
#include <assert.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#ifdef HAVE_LIBAVFILTER_AVCODEC_H
/* required by ffmpeg1.2 for avfilter_copy_buf_props */
#include <libavfilter/avcodec.h>
#endif
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/frame.h>

#include "player.h"
#include "debug.h"
#include "ui.h"
#include "ui_types.h"
#include "bar_state.h"  /* For lock assertions */

/* default sample format */
const enum AVSampleFormat avformat = AV_SAMPLE_FMT_S16;

/* Forward declarations for callbacks and helper functions */
static bool shouldQuit (player_t * const player);
static void maDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
static void maNotificationCallback(const ma_device_notification* pNotification);
static void* BarAudioProducerThread(void* data);

static void printError (const BarSettings_t * const settings,
		const char * const msg, int ret) {
	char avmsg[128];
	av_strerror (ret, avmsg, sizeof (avmsg));
	BarUiMsg (settings, MSG_ERR, "%s (%s)\n", msg, avmsg);
}

/*	global initialization
 */
void BarPlayerInit (player_t * const p, const BarSettings_t * const settings) {
	/* miniaudio doesn't need global initialization */
	av_log_set_level (AV_LOG_FATAL);
#ifdef HAVE_AV_REGISTER_ALL
	av_register_all ();
#endif
#ifdef HAVE_AVFILTER_REGISTER_ALL
	avfilter_register_all ();
#endif
#ifdef HAVE_AVFORMAT_NETWORK_INIT
	avformat_network_init ();
#endif

	pthread_mutex_init (&p->lock, NULL);
	pthread_cond_init (&p->cond, NULL);
	pthread_mutex_init (&p->aoplayLock, NULL);
	pthread_cond_init (&p->aoplayCond, NULL);
	pthread_mutex_init (&p->ringLock, NULL);
	pthread_cond_init (&p->ringCond, NULL);
	BarPlayerReset (p);
	p->settings = settings;
}

void BarPlayerDestroy (player_t * const p) {
	pthread_cond_destroy (&p->cond);
	pthread_mutex_destroy (&p->lock);
	pthread_cond_destroy (&p->aoplayCond);
	pthread_mutex_destroy (&p->aoplayLock);
	pthread_cond_destroy (&p->ringCond);
	pthread_mutex_destroy (&p->ringLock);

#ifdef HAVE_AVFORMAT_NETWORK_INIT
	avformat_network_deinit ();
#endif
	/* miniaudio doesn't need global cleanup */
}

void BarPlayerReset (player_t * const p) {
	p->doQuit = false;
	p->doPause = false;
	p->pauseStartTime = 0;
	p->songDuration = 0;
	p->songPlayed = 0;
	p->mode = PLAYER_DEAD;
	p->fvolume = NULL;
	p->fgraph = NULL;
	p->fctx = NULL;
	p->st = NULL;
	p->cctx = NULL;
	p->fbufsink = NULL;
	p->fabuf = NULL;
	p->streamIdx = -1;
	p->lastTimestamp = 0;
	p->interrupted = 0;
	memset(&p->maDevice, 0, sizeof(p->maDevice));
	p->deviceChanged = false;
	p->ringBuffer = NULL;
	p->ringSize = 0;
	p->ringWritePos = 0;
	p->ringReadPos = 0;
	p->producerRunning = false;
}

/*	Update volume filter
 */
void BarPlayerSetVolume (player_t * const player) {
	assert (player != NULL);

	if (player->mode != PLAYER_PLAYING) {
		return;
	}

	int ret;
#ifdef HAVE_AVFILTER_GRAPH_SEND_COMMAND
	/* ffmpeg and libav disagree on the type of this option (string vs. double)
	 * -> print to string and let them parse it again */
	char strbuf[16];
	snprintf (strbuf, sizeof (strbuf), "%fdB",
			player->settings->volume + (player->gain * player->settings->gainMul));
	assert (player->fgraph != NULL);
	if ((ret = avfilter_graph_send_command (player->fgraph, "volume", "volume",
					strbuf, NULL, 0, 0)) < 0) {
#else
	/* convert from decibel */
	const double volume = pow (10, (player->settings->volume + (player->gain * player->settings->gainMul)) / 20);
	/* libav does not provide other means to set this right now. it might not
	 * even work everywhere. */
	assert (player->fvolume != NULL);
	if ((ret = av_opt_set_double (player->fvolume->priv, "volume", volume,
			0)) != 0) {
#endif
		printError (player->settings, "Cannot set volume", ret);
	}
}

#define softfail(msg) \
	printError (player->settings, msg, ret); \
	return false;

/*	ffmpeg callback for blocking functions, returns 1 to abort function
 */
static int intCb (void * const data) {
	player_t * const player = data;
	assert (player != NULL);
	if (player->interrupted > 1) {
		/* got a sigint multiple times, quit pianobar (handled by main.c). */
		pthread_mutex_lock (&player->lock);
		player->doQuit = true;
		pthread_mutex_unlock (&player->lock);
		return 1;
	} else if (player->interrupted != 0) {
		/* the request is retried with the same player context */
		player->interrupted = 0;
		return 1;
	} else {
		return 0;
	}
}

static bool openStream (player_t * const player) {
	assert (player != NULL);
	/* no leak? */
	assert (player->fctx == NULL);

	int ret;

	/* stream setup */
	player->fctx = avformat_alloc_context ();
	player->fctx->interrupt_callback.callback = intCb;
	player->fctx->interrupt_callback.opaque = player;

	/* in microseconds */
	unsigned long int timeout = player->settings->timeout*1000000;
	char timeoutStr[16];
	ret = snprintf (timeoutStr, sizeof (timeoutStr), "%lu", timeout);
	assert (ret < sizeof (timeoutStr));
	AVDictionary *options = NULL;
	av_dict_set (&options, "timeout", timeoutStr, 0);

	assert (player->url != NULL);
	if ((ret = avformat_open_input (&player->fctx, player->url, NULL, &options)) < 0) {
		softfail ("Unable to open audio file");
	}

	if ((ret = avformat_find_stream_info (player->fctx, NULL)) < 0) {
		softfail ("find_stream_info");
	}

	/* ignore all streams, undone for audio stream below */
	for (size_t i = 0; i < player->fctx->nb_streams; i++) {
		player->fctx->streams[i]->discard = AVDISCARD_ALL;
	}

	player->streamIdx = av_find_best_stream (player->fctx, AVMEDIA_TYPE_AUDIO,
			-1, -1, NULL, 0);
	if (player->streamIdx < 0) {
		softfail ("find_best_stream");
	}

	player->st = player->fctx->streams[player->streamIdx];
	player->st->discard = AVDISCARD_DEFAULT;

	/* decoder setup */
	if ((player->cctx = avcodec_alloc_context3 (NULL)) == NULL) {
		softfail ("avcodec_alloc_context3");
	}
	const AVCodecParameters * const cp = player->st->codecpar;
	if ((ret = avcodec_parameters_to_context (player->cctx, cp)) < 0) {
		softfail ("avcodec_parameters_to_context");
	}

	const AVCodec * const decoder = avcodec_find_decoder (cp->codec_id);
	if (decoder == NULL) {
		softfail ("find_decoder");
	}

	if ((ret = avcodec_open2 (player->cctx, decoder, NULL)) < 0) {
		softfail ("codec_open2");
	}

	if (player->lastTimestamp > 0) {
		av_seek_frame (player->fctx, player->streamIdx, player->lastTimestamp, 0);
	}

	const unsigned int songDuration = av_q2d (player->st->time_base) *
			(double) player->st->duration;
	pthread_mutex_lock (&player->lock);
	player->songPlayed = 0;
	player->songDuration = songDuration;
	pthread_mutex_unlock (&player->lock);

	return true;
}

/*	Get output sample rate. Default to stream sample rate
 */
static int getSampleRate (const player_t * const player) {
	AVCodecParameters const * const cp = player->st->codecpar;
	return player->settings->sampleRate == 0 ?
			cp->sample_rate :
			player->settings->sampleRate;
}

/*	setup filter chain
 */
static bool openFilter (player_t * const player) {
	/* filter setup */
	char strbuf[256];
	int ret = 0;
	AVCodecParameters * const cp = player->st->codecpar;

	if ((player->fgraph = avfilter_graph_alloc ()) == NULL) {
		softfail ("graph_alloc");
	}

	/* abuffer */
	AVRational time_base = player->st->time_base;

	char channelLayout[128];
	av_channel_layout_describe(&player->cctx->ch_layout, channelLayout, sizeof(channelLayout));
	snprintf (strbuf, sizeof (strbuf),
			"time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
			time_base.num, time_base.den, cp->sample_rate,
			av_get_sample_fmt_name (player->cctx->sample_fmt),
			channelLayout);
	if ((ret = avfilter_graph_create_filter (&player->fabuf,
			avfilter_get_by_name ("abuffer"), "source", strbuf, NULL,
			player->fgraph)) < 0) {
		softfail ("create_filter abuffer");
	}

	/* volume */
	if ((ret = avfilter_graph_create_filter (&player->fvolume,
			avfilter_get_by_name ("volume"), "volume", "0dB", NULL,
			player->fgraph)) < 0) {
		softfail ("create_filter volume");
	}

	/* aformat: convert float samples into something more usable */
	AVFilterContext *fafmt = NULL;
	snprintf (strbuf, sizeof (strbuf), "sample_fmts=%s:sample_rates=%d",
			av_get_sample_fmt_name (avformat), getSampleRate (player));
	if ((ret = avfilter_graph_create_filter (&fafmt,
					avfilter_get_by_name ("aformat"), "format", strbuf, NULL,
					player->fgraph)) < 0) {
		softfail ("create_filter aformat");
	}

	/* abuffersink */
	if ((ret = avfilter_graph_create_filter (&player->fbufsink,
			avfilter_get_by_name ("abuffersink"), "sink", NULL, NULL,
			player->fgraph)) < 0) {
		softfail ("create_filter abuffersink");
	}

	/* connect filter: abuffer -> volume -> aformat -> abuffersink */
	if (avfilter_link (player->fabuf, 0, player->fvolume, 0) != 0 ||
			avfilter_link (player->fvolume, 0, fafmt, 0) != 0 ||
			avfilter_link (fafmt, 0, player->fbufsink, 0) != 0) {
		softfail ("filter_link");
	}

	if ((ret = avfilter_graph_config (player->fgraph, NULL)) < 0) {
		softfail ("graph_config");
	}

	return true;
}

/*	miniaudio data callback - called when device needs audio data
 *	This reads from the ring buffer filled by the producer thread
 */
static void maDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
	player_t * const player = (player_t *)pDevice->pUserData;
	(void)pInput;  /* Unused for playback */
	
	if (!player || !player->ringBuffer) {
		/* Not ready yet, output silence */
		memset(pOutput, 0, frameCount * pDevice->playback.channels * sizeof(int16_t));
		return;
	}
	
	int16_t* output = (int16_t*)pOutput;
	size_t samplesNeeded = frameCount * pDevice->playback.channels;
	size_t samplesRead = 0;
	
	pthread_mutex_lock(&player->ringLock);
	
	/* Check if paused */
	if (player->doPause) {
		pthread_mutex_unlock(&player->ringLock);
		memset(pOutput, 0, samplesNeeded * sizeof(int16_t));
		return;
	}
	
	/* Read from ring buffer */
	while (samplesRead < samplesNeeded) {
		size_t available = (player->ringWritePos >= player->ringReadPos) ?
		                   (player->ringWritePos - player->ringReadPos) :
		                   (player->ringSize - player->ringReadPos + player->ringWritePos);
		
		if (available == 0) {
			/* Buffer underrun, output silence for remaining samples */
			memset(output + samplesRead, 0, (samplesNeeded - samplesRead) * sizeof(int16_t));
			break;
		}
		
		/* Read samples from ring buffer */
		size_t samplesThisRead = (samplesNeeded - samplesRead < available) ? 
		                         (samplesNeeded - samplesRead) : available;
		
		/* Handle wrap-around */
		size_t samplesToEnd = player->ringSize - player->ringReadPos;
		if (samplesThisRead > samplesToEnd) {
			/* Read to end of buffer */
			memcpy(output + samplesRead, player->ringBuffer + player->ringReadPos, 
			       samplesToEnd * sizeof(int16_t));
			samplesRead += samplesToEnd;
			player->ringReadPos = 0;
			samplesThisRead -= samplesToEnd;
		}
		
		/* Read remaining samples */
		if (samplesThisRead > 0) {
			memcpy(output + samplesRead, player->ringBuffer + player->ringReadPos, 
			       samplesThisRead * sizeof(int16_t));
			samplesRead += samplesThisRead;
			player->ringReadPos += samplesThisRead;
			if (player->ringReadPos >= player->ringSize) {
				player->ringReadPos = 0;
			}
		}
	}
	
	/* Wake up producer if it was waiting for space */
	pthread_cond_signal(&player->ringCond);
	pthread_mutex_unlock(&player->ringLock);
}

/*	miniaudio notification callback - called when device state changes
 */
static void maNotificationCallback(const ma_device_notification* pNotification) {
	player_t * const player = (player_t *)pNotification->pDevice->pUserData;
	
	if (!player) {
		return;
	}
	
	switch (pNotification->type) {
		case ma_device_notification_type_started:
			debugPrint(DEBUG_AUDIO, "miniaudio: Device started\n");
			break;
			
		case ma_device_notification_type_stopped:
			debugPrint(DEBUG_AUDIO, "miniaudio: Device stopped\n");
			break;
			
		case ma_device_notification_type_rerouted:
			debugPrint(DEBUG_AUDIO, "miniaudio: Device rerouted (possible default device change)\n");
			player->deviceChanged = true;
			break;
			
		case ma_device_notification_type_interruption_began:
			debugPrint(DEBUG_AUDIO, "miniaudio: Interruption began\n");
			break;
			
		case ma_device_notification_type_interruption_ended:
			debugPrint(DEBUG_AUDIO, "miniaudio: Interruption ended\n");
			break;
			
		default:
			break;
	}
}

/*	Map BarAudioBackendType to ma_backend
 */
static ma_backend settingsToMaBackend(BarAudioBackendType backend) {
	switch (backend) {
		case BAR_AUDIO_BACKEND_PULSEAUDIO:
			return ma_backend_pulseaudio;
		case BAR_AUDIO_BACKEND_ALSA:
			return ma_backend_alsa;
		case BAR_AUDIO_BACKEND_JACK:
			return ma_backend_jack;
		case BAR_AUDIO_BACKEND_COREAUDIO:
			return ma_backend_coreaudio;
		case BAR_AUDIO_BACKEND_WASAPI:
			return ma_backend_wasapi;
		case BAR_AUDIO_BACKEND_AUTO:
		default:
			return ma_backend_null;  /* Use null as terminator, let miniaudio auto-select */
	}
}

/*	Producer thread - pulls frames from ffmpeg and writes to ring buffer
 */
static void* BarAudioProducerThread(void* data) {
	player_t * const player = (player_t *)data;
	
	while (player->producerRunning && !shouldQuit(player)) {
		/* Check if paused */
		pthread_mutex_lock(&player->lock);
		if (player->doPause) {
			pthread_mutex_unlock(&player->lock);
			usleep(10000);  /* Sleep 10ms while paused */
			continue;
		}
		pthread_mutex_unlock(&player->lock);
		
		/* Get frame from ffmpeg */
		pthread_mutex_lock(&player->aoplayLock);
		
		AVFrame *filteredFrame = av_frame_alloc();
		if (!filteredFrame) {
			pthread_mutex_unlock(&player->aoplayLock);
			break;
		}
		
		int ret = av_buffersink_get_frame(player->fbufsink, filteredFrame);
		
		if (ret == AVERROR_EOF || shouldQuit(player)) {
			av_frame_free(&filteredFrame);
			pthread_mutex_unlock(&player->aoplayLock);
			break;
		} else if (ret == AVERROR(EAGAIN) || ret < 0) {
			/* No data available yet, wait for decoder */
			av_frame_free(&filteredFrame);
			pthread_cond_wait(&player->aoplayCond, &player->aoplayLock);
			pthread_mutex_unlock(&player->aoplayLock);
			continue;
		}
		
		/* Update playback position */
		const double timeBase = av_q2d(av_buffersink_get_time_base(player->fbufsink));
		const double timestamp = (double)filteredFrame->pts * timeBase;
		const unsigned int songPlayed = (unsigned int)timestamp;
		
		pthread_mutex_lock(&player->lock);
		player->songPlayed = songPlayed;
		pthread_mutex_unlock(&player->lock);
		
		/* Update last timestamp for buffer management */
		const double timeBaseSt = av_q2d(player->st->time_base);
		const int64_t lastTimestamp = (int64_t)(timestamp / timeBaseSt);
		player->lastTimestamp = lastTimestamp;
		pthread_cond_broadcast(&player->aoplayCond);
		
		pthread_mutex_unlock(&player->aoplayLock);
		
		/* Write to ring buffer */
		const int numChannels = filteredFrame->ch_layout.nb_channels;
		const size_t frameSamples = filteredFrame->nb_samples * numChannels;
		const int16_t* frameData = (const int16_t*)filteredFrame->data[0];
		
		pthread_mutex_lock(&player->ringLock);
		
		/* Wait for space in ring buffer */
		while (player->producerRunning) {
			size_t available = (player->ringReadPos > player->ringWritePos) ?
			                   (player->ringReadPos - player->ringWritePos - 1) :
			                   (player->ringSize - player->ringWritePos + player->ringReadPos - 1);
			
			if (available >= frameSamples) {
				break;
			}
			
			/* Wait for consumer to free up space */
			pthread_cond_wait(&player->ringCond, &player->ringLock);
		}
		
		if (!player->producerRunning) {
			pthread_mutex_unlock(&player->ringLock);
			av_frame_free(&filteredFrame);
			break;
		}
		
		/* Write samples to ring buffer */
		size_t samplesWritten = 0;
		while (samplesWritten < frameSamples) {
			size_t samplesToEnd = player->ringSize - player->ringWritePos;
			size_t samplesThisWrite = (frameSamples - samplesWritten < samplesToEnd) ?
			                          (frameSamples - samplesWritten) : samplesToEnd;
			
			memcpy(player->ringBuffer + player->ringWritePos, 
			       frameData + samplesWritten,
			       samplesThisWrite * sizeof(int16_t));
			
			samplesWritten += samplesThisWrite;
			player->ringWritePos += samplesThisWrite;
			if (player->ringWritePos >= player->ringSize) {
				player->ringWritePos = 0;
			}
		}
		
		pthread_mutex_unlock(&player->ringLock);
		av_frame_free(&filteredFrame);
	}
	
	debugPrint(DEBUG_AUDIO, "Producer thread exiting\n");
	return NULL;
}

/*	setup miniaudio device
 */
static bool openDevice (player_t * const player) {
	const AVCodecParameters * const cp = player->st->codecpar;

	/* Allocate ring buffer: 3 seconds of audio */
	const size_t sampleRate = getSampleRate(player);
	const size_t channels = cp->ch_layout.nb_channels;
	player->ringSize = sampleRate * channels * 3;  /* 3 seconds */
	player->ringBuffer = (int16_t*)calloc(player->ringSize, sizeof(int16_t));
	if (!player->ringBuffer) {
		BarUiMsg(player->settings, MSG_ERR, "Failed to allocate ring buffer\n");
		return false;
	}
	player->ringWritePos = 0;
	player->ringReadPos = 0;
	debugPrint(DEBUG_AUDIO, "Ring buffer allocated: %zu samples (%.1f seconds)\n",
	           player->ringSize, (double)player->ringSize / (sampleRate * channels));

	/* Configure miniaudio device with optimized settings */
	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.format   = ma_format_s16;  /* Matches avformat */
	config.playback.channels = channels;
	config.sampleRate        = sampleRate;
	config.dataCallback      = maDataCallback;
	config.notificationCallback = maNotificationCallback;
	config.pUserData         = player;  /* Pass player context to callbacks */
	
	/* Optimize for low-latency playback */
	config.periodSizeInMilliseconds = 10;  /* 10ms periods */
	config.periods = 3;  /* Triple buffering */
	config.performanceProfile = ma_performance_profile_low_latency;
	
	/* macOS-specific: prevent sample rate changes */
	config.coreaudio.allowNominalSampleRateChange = MA_FALSE;
	
	/* Let miniaudio auto-select backend */
	ma_result result = ma_device_init(NULL, &config, &player->maDevice);
	if (result != MA_SUCCESS) {
		BarUiMsg(player->settings, MSG_ERR, "Failed to initialize audio device: %d\n", result);
		free(player->ringBuffer);
		player->ringBuffer = NULL;
		return false;
	}
	
	debugPrint(DEBUG_AUDIO, "miniaudio device initialized: %s (backend: %s, sample_rate: %u, channels: %u)\n",
	           player->maDevice.playback.name,
	           ma_get_backend_name(player->maDevice.pContext->backend),
	           player->maDevice.sampleRate,
	           player->maDevice.playback.channels);
	
	/* Start the device */
	if ((result = ma_device_start(&player->maDevice)) != MA_SUCCESS) {
		BarUiMsg(player->settings, MSG_ERR, "Failed to start audio device: %d\n", result);
		ma_device_uninit(&player->maDevice);
		free(player->ringBuffer);
		player->ringBuffer = NULL;
		return false;
	}
	
	/* Start producer thread */
	player->producerRunning = true;
	if (pthread_create(&player->producerThread, NULL, BarAudioProducerThread, player) != 0) {
		BarUiMsg(player->settings, MSG_ERR, "Failed to create producer thread\n");
		ma_device_stop(&player->maDevice);
		ma_device_uninit(&player->maDevice);
		free(player->ringBuffer);
		player->ringBuffer = NULL;
		player->producerRunning = false;
		return false;
	}
	
	return true;
}

/*	Operating on shared variables and must be protected by mutex
 */

static bool shouldQuit (player_t * const player) {
	pthread_mutex_lock (&player->lock);
	const bool ret = player->doQuit;
	pthread_mutex_unlock (&player->lock);
	return ret;
}

static void changeMode (player_t * const player, unsigned int mode) {
	pthread_mutex_lock (&player->lock);
	player->mode = mode;
	pthread_mutex_unlock (&player->lock);
}

BarPlayerMode BarPlayerGetMode (player_t * const player) {
	pthread_mutex_lock (&player->lock);
	const BarPlayerMode ret = player->mode;
	pthread_mutex_unlock (&player->lock);
	return ret;
}

/*	decode and play stream. returns 0 or av error code.
 */
static int play (player_t * const player) {
	assert (player != NULL);
	const int64_t minBufferHealth = player->settings->bufferSecs;
	AVCodecContext * const cctx = player->cctx;

	AVPacket *pkt = av_packet_alloc ();
	assert (pkt != NULL);
	pkt->data = NULL;
	pkt->size = 0;

	AVFrame *frame = NULL;
	frame = av_frame_alloc ();
	assert (frame != NULL);
	
	/* No separate playback thread needed - miniaudio callback handles it */
	enum { FILL, DRAIN, DONE } drainMode = FILL;
	int ret = 0;
	const double timeBase = av_q2d (player->st->time_base);
	while (!shouldQuit (player) && drainMode != DONE) {
		if (drainMode == FILL) {
			ret = av_read_frame (player->fctx, pkt);
			if (ret == AVERROR_EOF) {
				/* enter drain mode */
				drainMode = DRAIN;
				avcodec_send_packet (cctx, NULL);
				debugPrint (DEBUG_AUDIO, "decoder entering drain mode after EOF\n");
			} else if (pkt->stream_index != player->streamIdx) {
				/* unused packet */
				av_packet_unref (pkt);
				continue;
			} else if (ret < 0) {
				/* error, abort */
				/* mark the EOF, so that BarAoPlayThread can quit*/
				char error[AV_ERROR_MAX_STRING_SIZE];
				if (av_strerror(ret, error, sizeof(error)) < 0) {
					strncpy (error, "(unknown)", sizeof(error)-1);
				}
				debugPrint (DEBUG_AUDIO, "av_read_frame failed with code %i (%s), "
						"sending NULL frame\n", ret, error);
				pthread_mutex_lock (&player->aoplayLock);
				const int rt = av_buffersrc_add_frame (player->fabuf, NULL);
				assert (rt == 0);
				pthread_cond_broadcast (&player->aoplayCond);
				pthread_mutex_unlock (&player->aoplayLock);
				break;
			} else {
				/* fill buffer */
				avcodec_send_packet (cctx, pkt);
			}
		}

		while (!shouldQuit (player)) {
			ret = avcodec_receive_frame (cctx, frame);
			if (ret == AVERROR_EOF) {
				/* done draining */
				drainMode = DONE;
				/* mark the EOF*/
				debugPrint (DEBUG_AUDIO, "receive_frame got EOF, sending NULL frame\n");
				pthread_mutex_lock (&player->aoplayLock);
				const int rt = av_buffersrc_add_frame (player->fabuf, NULL);
				assert (rt == 0);
				pthread_cond_broadcast (&player->aoplayCond);
				pthread_mutex_unlock (&player->aoplayLock);
				break;
			} else if (ret != 0) {
				/* no more output */
				break;
			}

			/* XXX: suppresses warning from resample filter */
			if (frame->pts == (int64_t) AV_NOPTS_VALUE) {
				frame->pts = 0;
			}
			pthread_mutex_lock (&player->aoplayLock);
			ret = av_buffersrc_write_frame (player->fabuf, frame);
			assert (ret >= 0);
			pthread_mutex_unlock (&player->aoplayLock);
		
		int64_t bufferHealth = 0;
		do {
			pthread_mutex_lock (&player->aoplayLock);
			bufferHealth = timeBase * (double) (frame->pts - player->lastTimestamp);
			if (bufferHealth > minBufferHealth) {
				debugPrint (DEBUG_AUDIO, "decoding buffer filled health %"PRIi64" minHealth %"PRIi64"\n",
						bufferHealth, minBufferHealth);
				/* Buffer get healthy, resume */
				pthread_cond_broadcast (&player->aoplayCond);
				/* Buffer is healthy enough, wait */
				pthread_cond_wait (&player->aoplayCond, &player->aoplayLock);
				debugPrint (DEBUG_AUDIO, "ao play signalled it needs more data health %"PRIi64" minHealth %"PRIi64"\n",
						bufferHealth, minBufferHealth);
				
				/* Check if we should quit after waking */
				if (shouldQuit(player)) {
					pthread_mutex_unlock (&player->aoplayLock);
					break;
				}
			}
			pthread_mutex_unlock (&player->aoplayLock);
		} while (bufferHealth > minBufferHealth && !shouldQuit(player));
	}

		av_packet_unref (pkt);
	}
	av_frame_free (&frame);
	av_packet_free (&pkt);
	debugPrint (DEBUG_AUDIO, "decoder is done\n");
	/* miniaudio callback will continue playing buffered data until empty */

	return ret;
}

static void finish (player_t * const player) {
	/* Stop producer thread */
	if (player->producerRunning) {
		player->producerRunning = false;
		pthread_cond_signal(&player->ringCond);  /* Wake up if waiting */
		pthread_join(player->producerThread, NULL);
		debugPrint(DEBUG_AUDIO, "Producer thread joined\n");
	}
	
	/* Stop and uninit miniaudio device */
	if (player->maDevice.pContext != NULL) {
		ma_device_stop(&player->maDevice);
		ma_device_uninit(&player->maDevice);
		memset(&player->maDevice, 0, sizeof(player->maDevice));
	}
	
	/* Free ring buffer */
	if (player->ringBuffer != NULL) {
		free(player->ringBuffer);
		player->ringBuffer = NULL;
		player->ringSize = 0;
		player->ringWritePos = 0;
		player->ringReadPos = 0;
		debugPrint(DEBUG_AUDIO, "Ring buffer freed\n");
	}
	
	if (player->fgraph != NULL) {
		avfilter_graph_free (&player->fgraph);
		player->fgraph = NULL;
	}
	if (player->cctx != NULL) {
		avcodec_free_context (&player->cctx);
		player->cctx = NULL;
	}
	if (player->fctx != NULL) {
		avformat_close_input (&player->fctx);
	}
}

/*	player thread; for every song a new thread is started
 *	@param audioPlayer structure
 *	@return PLAYER_RET_*
 */
void *BarPlayerThread (void *data) {
	assert (data != NULL);

	player_t * const player = data;
	uintptr_t pret = PLAYER_RET_OK;

	bool retry;
	do {
		retry = false;
		if (openStream (player)) {
			if (openFilter (player) && openDevice (player)) {
				changeMode (player, PLAYER_PLAYING);
				BarPlayerSetVolume (player);
				const int ret = play (player);
				retry = (ret == AVERROR_INVALIDDATA ||
						 ret == -ECONNRESET) &&
						!player->interrupted;
			} else {
				/* filter missing or audio device busy */
				pret = PLAYER_RET_HARDFAIL;
			}
		} else {
			/* stream not found */
			pret = PLAYER_RET_SOFTFAIL;
		}
		changeMode (player, PLAYER_WAITING);
		finish (player);
	} while (retry);

	changeMode (player, PLAYER_FINISHED);

	return (void *) pret;
}

/* BarAoPlayThread is no longer used with miniaudio.
 * The miniaudio data callback (maDataCallback) handles audio playback instead.
 * Old function has been removed. See git history if needed for reference.
 */
