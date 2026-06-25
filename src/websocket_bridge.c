/*
Copyright (c) 2025
	Remote Pianobar Contributors

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

#include "websocket_bridge.h"
#include "system_volume.h"

#ifdef WEBSOCKET_ENABLED
#include "websocket/core/websocket.h"
#include "websocket/protocol/socketio.h"
#include "websocket/daemon/daemon.h"
#include "playback_manager.h"
#include "player.h"
#include "bar_constants.h"
#include "log.h"
#include "ui.h"
#include <json-c/json.h>
#include <pthread.h>
#include <unistd.h>
#include <netinet/in.h>

/* Mode predicates */
bool BarIsWebOnlyMode(const BarApp_t *app) {
	return app && app->settings.uiMode == BAR_UI_MODE_WEB;
}

bool BarShouldSkipCliOutput(const BarApp_t *app) {
	return app && app->settings.uiMode == BAR_UI_MODE_WEB;
}

/* Event broadcasts */
void BarWsBroadcastVolume(BarApp_t *app) {
	if (app && app->settings.uiMode != BAR_UI_MODE_CLI && app->wsContext) {
		int volumePercent;
		if (app->settings.volumeMode == BAR_VOLUME_MODE_SYSTEM) {
			volumePercent = BarSystemVolumeGet();
			if (volumePercent < 0) volumePercent = VOLUME_FALLBACK_PERCENT;
		} else {
			volumePercent = app->settings.volume;
		}
		struct json_object *vol = json_object_new_int (volumePercent);
		char *msg = BarSocketIoFormatEventMessage ("volume", vol);
		json_object_put (vol);
		BarWebsocketBroadcastSocketIoMessage (app, BUCKET_VOLUME, msg);
	}
}

void BarWsBroadcastExplanation(BarApp_t *app, const char *explanation) {
	if (app && app->wsContext) {
		/* Only emit if unicast target is set (WebUI request).
		 * CLI requests have no unicast target - user sees result in terminal. */
		if (BarSocketIoGetUnicastTarget() != NULL) {
			BarSocketIoEmitExplanation(app, explanation);
		}
	}
}

void BarWsBroadcastUpcoming(BarApp_t *app, PianoSong_t *songs, int count) {
	if (app && app->wsContext) {
		/* Only emit if unicast target is set (WebUI request).
		 * CLI requests have no unicast target - user sees result in terminal. */
		if (BarSocketIoGetUnicastTarget() != NULL) {
			BarSocketIoEmitUpcoming(app, songs, count);
		}
	}
}

void BarWsBroadcastSongStart(BarApp_t *app) {
	if (app && app->settings.uiMode != BAR_UI_MODE_CLI && app->wsContext) {
		struct json_object *data = BarSocketIoBuildStartPayload (app);
		char *msg = BarSocketIoFormatEventMessage ("start", data);
		json_object_put (data);
		BarWebsocketBroadcastSocketIoMessage (app, BUCKET_STATE, msg);
	}
}

void BarWsBroadcastProcess(BarApp_t *app) {
	if (app && app->settings.uiMode != BAR_UI_MODE_CLI && app->wsContext) {
		struct json_object *data = BarSocketIoBuildProcessPayload (app);
		char *msg = BarSocketIoFormatEventMessage ("process", data);
		json_object_put (data);
		BarWebsocketBroadcastSocketIoMessage (app, BUCKET_STATE, msg);
	}
}

void BarWsBroadcastSongStop(BarApp_t *app) {
	if (app && app->settings.uiMode != BAR_UI_MODE_CLI && app->wsContext) {
		char *msg = BarSocketIoFormatEventMessage ("stop", NULL);
		BarWebsocketBroadcastSocketIoMessage (app, BUCKET_STATE, msg);
	}
}

void BarWsBroadcastProgress(BarApp_t *app) {
	if (!app || app->settings.uiMode == BAR_UI_MODE_CLI || !app->wsContext) return;

	player_t *player = &app->player;
	pthread_mutex_lock (&player->lock);
	if (player->mode != PLAYER_PLAYING || player->doPause) {
		pthread_mutex_unlock (&player->lock);
		return;
	}
	unsigned int elapsed  = player->songPlayed;
	unsigned int duration = player->songDuration;
	pthread_mutex_unlock (&player->lock);

	BarWsContext_t *ctx = (BarWsContext_t *)app->wsContext;
	if (elapsed == ctx->progress.lastBroadcast) return;
	ctx->progress.lastBroadcast = elapsed;

	float pct = (duration > 0) ? (elapsed * 100.0f) / (float)duration : 0.0f;
	struct json_object *data = json_object_new_object ();
	json_object_object_add (data, "elapsed",    json_object_new_int ((int)elapsed));
	json_object_object_add (data, "duration",   json_object_new_int ((int)duration));
	json_object_object_add (data, "percentage", json_object_new_double ((double)pct));
	char *msg = BarSocketIoFormatEventMessage ("progress", data);
	json_object_put (data);
	BarWebsocketBroadcastSocketIoMessage (app, BUCKET_PROGRESS, msg);
}

void BarWsBroadcastPlayState(BarApp_t *app) {
	if (app && app->wsContext) {
		BarSocketIoEmitPlayState(app);
	}
}

void BarWsBroadcastStations(BarApp_t *app) {
	if (app && app->settings.uiMode != BAR_UI_MODE_CLI && app->wsContext) {
		struct json_object *data = BarSocketIoBuildStationsPayload (app);
		char *msg = BarSocketIoFormatEventMessage ("stations", data);
		json_object_put (data);
		BarWebsocketBroadcastSocketIoMessage (app, BUCKET_STATIONS, msg);
	}
}

void BarWsDisconnectAllClients(BarApp_t *app) {
	if (app && app->wsContext) {
		BarWebsocketDisconnectAllClients(app);
	}
}

/* Lifecycle management */
bool BarWsInit(BarApp_t *app) {
	if (app && app->settings.uiMode != BAR_UI_MODE_CLI) {
		return BarWebsocketInit(app);
	}
	return true;
}

void BarWsDestroy(BarApp_t *app) {
	if (app && app->settings.uiMode != BAR_UI_MODE_CLI) {
		BarWebsocketDestroy(app);
	}
}

bool BarWsDaemonize(BarApp_t *app) {
	if (app && app->settings.uiMode == BAR_UI_MODE_WEB) {
		return BarDaemonize(app);
	}
	return true;
}

void BarWsRemovePidFile(BarApp_t *app) {
	if (app && app->settings.uiMode == BAR_UI_MODE_WEB) {
		BarDaemonRemovePidFile(app);
	}
}

/* Predicate: true when web UI is active (WEB or BOTH mode) */
bool BarWsIsWebActive(const BarApp_t *app) {
	return app &&
	       (app->settings.uiMode == BAR_UI_MODE_WEB ||
	        app->settings.uiMode == BAR_UI_MODE_BOTH);
}

bool BarWsSettingsIsWebActive(const BarSettings_t *s) {
	return s &&
	       (s->uiMode == BAR_UI_MODE_WEB || s->uiMode == BAR_UI_MODE_BOTH);
}

/* Playback manager lifecycle */
bool BarWsStartPlaybackManager(BarApp_t *app) {
	if (app && app->settings.uiMode != BAR_UI_MODE_CLI) {
		return BarPlaybackManagerStart(app);
	}
	return true;
}

void BarWsStopPlaybackManager(BarApp_t *app) {
	if (app && app->settings.uiMode != BAR_UI_MODE_CLI) {
		BarPlaybackManagerStop(app);
	}
}

/* Daemon singleton lock — returns false if lock cannot be acquired */
bool BarWsAcquireSingletonLock(BarApp_t *app) {
	if (!app) {
		return true;
	}
	if (app->settings.uiMode == BAR_UI_MODE_CLI) {
		app->lockFd = -1;
		return true;
	}
	app->lockFd = BarDaemonAcquireLock();
	if (app->lockFd < 0) {
		BarDaemonKillExistingInstance();
		for (int i = 0; i < BAR_DAEMON_LOCK_RETRY_COUNT && app->lockFd < 0; i++) {
			usleep ((unsigned int)BAR_DAEMON_LOCK_RETRY_MS * 1000u);
			app->lockFd = BarDaemonAcquireLock();
		}
		if (app->lockFd < 0) {
			log_write (LOG_ERROR,
			           "Could not acquire lock. Another instance may still be running.\n");
			return false;
		}
	}
	BarDaemonWriteLockPid(app->lockFd);
	return true;
}

/* Release the singleton lock file fd */
void BarWsReleaseSingletonLock(BarApp_t *app) {
	if (app && app->lockFd >= 0) {
		close(app->lockFd);
		app->lockFd = -1;
	}
}

/* Unicast request detection */
bool BarWsIsUnicastRequest(void) {
	return BarSocketIoGetUnicastTarget() != NULL;
}

/* Error event broadcasting */
void BarWsEmitError(BarApp_t *app, const char *event, const char *msg) {
	BarSocketIoEmitError(app, event, msg);
}

void BarWsEmitErrorEx(BarApp_t *app, const char *event, const char *msg,
                       const char *extra) {
	BarSocketIoEmitErrorEx(app, event, msg, extra);
}

/* Pandora disconnect notification */
void BarWsBroadcastPandoraDisconnected(BarApp_t *app, const char *reason) {
	(void)app;
	BarSocketIoEmitPandoraDisconnected(reason);
}

/* Web info printing (web or both mode) */
void BarWsPrintWebInfo(const BarApp_t *app, FILE *stream) {
	if (!BarWsIsWebActive(app)) { return; }
	if (app->settings.webuiPath) {
		fprintf(stream, "Web UI files: %s\n", app->settings.webuiPath);
	} else {
		fprintf(stream, "Web UI files: (using built-in)\n");
	}
	fprintf(stream, "Web interface: http://%s:%d/\n",
	        app->settings.websocketHost ? app->settings.websocketHost : "127.0.0.1",
	        app->settings.websocketPort);
}

/* Print PID file info if daemon and configured */
void BarWsPrintPidFileInfo(const BarApp_t *app, bool is_daemon, FILE *stream) {
	if (is_daemon && app && app->settings.pidFile) {
		fprintf(stream, "PID file: %s\n", app->settings.pidFile);
	}
}

/* Print startup info for web/both modes (web interface URL, IPv4 address) */
void BarWsPrintStartupInfo(BarApp_t *app) {
	if (!BarWsIsWebActive(app)) { return; }
	log_write(LOG_ERROR, "Starting pianobar\n");
	char ipv4_addr[INET_ADDRSTRLEN];
	BarDaemonGetIPv4Address(ipv4_addr, sizeof(ipv4_addr));
	BarPrintStartupInfo(app, getpid(), false, stderr);
	if (app->settings.websocketPort > 0) {
		log_write(LOG_ERROR, "Web interface: http://%s:%d/\n",
		          ipv4_addr, app->settings.websocketPort);
	}
}

/* Configure web-only input (no stdin/FIFO) */
void BarWsConfigureWebOnlyInput(BarApp_t *app) {
	app->input.fds[0] = -1;
	app->input.fds[1] = -1;
	app->input.maxfd = 0;
}

#else
/* No-op implementations when WebSocket support is disabled */
bool BarIsWebOnlyMode(const BarApp_t *app) { (void)app; return false; }
bool BarShouldSkipCliOutput(const BarApp_t *app) { (void)app; return false; }
bool BarWsIsWebActive(const BarApp_t *app) { (void)app; return false; }

void BarWsBroadcastVolume(BarApp_t *app) { (void)app; }
void BarWsBroadcastExplanation(BarApp_t *app, const char *explanation) { 
	(void)app; (void)explanation; 
}
void BarWsBroadcastUpcoming(BarApp_t *app, PianoSong_t *songs, int count) { 
	(void)app; (void)songs; (void)count;
}
void BarWsBroadcastSongStart(BarApp_t *app) { (void)app; }
void BarWsBroadcastProcess(BarApp_t *app) { (void)app; }
void BarWsBroadcastSongStop(BarApp_t *app) { (void)app; }
void BarWsBroadcastProgress(BarApp_t *app) { (void)app; }
void BarWsBroadcastPlayState(BarApp_t *app) { (void)app; }
void BarWsBroadcastStations(BarApp_t *app) { (void)app; }
void BarWsDisconnectAllClients(BarApp_t *app) { (void)app; }

bool BarWsInit(BarApp_t *app) { (void)app; return true; }
void BarWsDestroy(BarApp_t *app) { (void)app; }
bool BarWsDaemonize(BarApp_t *app) { (void)app; return true; }
void BarWsRemovePidFile(BarApp_t *app) { (void)app; }

bool BarWsStartPlaybackManager(BarApp_t *app) { (void)app; return true; }
void BarWsStopPlaybackManager(BarApp_t *app) { (void)app; }
bool BarWsAcquireSingletonLock(BarApp_t *app) { (void)app; return true; }
void BarWsReleaseSingletonLock(BarApp_t *app) { (void)app; }
bool BarWsIsUnicastRequest(void) { return false; }
void BarWsEmitError(BarApp_t *app, const char *event, const char *msg) {
	(void)app; (void)event; (void)msg;
}
void BarWsEmitErrorEx(BarApp_t *app, const char *event, const char *msg,
                       const char *extra) {
	(void)app; (void)event; (void)msg; (void)extra;
}
void BarWsBroadcastPandoraDisconnected(BarApp_t *app, const char *reason) {
	(void)app; (void)reason;
}
void BarWsPrintWebInfo(BarApp_t *app, FILE *stream) { (void)app; (void)stream; }
void BarWsConfigureWebOnlyInput(BarApp_t *app) { (void)app; }
bool BarWsSettingsIsWebActive(const BarSettings_t *s) { (void)s; return false; }
void BarWsPrintPidFileInfo(BarApp_t *app, bool is_daemon, FILE *stream) {
	(void)app; (void)is_daemon; (void)stream;
}
void BarWsPrintStartupInfo(BarApp_t *app) { (void)app; }
#endif

