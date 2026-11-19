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
#include <string.h>
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
			volumePercent = BarPlayerGetVolume (&app->player);
		}
		struct json_object *vol = json_object_new_int (volumePercent);
		char *msg = BarSocketIoFormatEventMessage ("volume", vol);
		json_object_put (vol);
		BarWebsocketBroadcastSocketIoMessage (app, BUCKET_VOLUME, msg);
	}
}

/* Query replies: to the requesting web client only. CLI requests never call
 * these; the user sees the result in the terminal. */
void BarWsEmitExplanationTo(BarApp_t *app, BarWsConnId_t requester,
                            const char *explanation) {
	BarSocketIoEmitExplanationTo(app, requester, explanation);
}

void BarWsEmitUpcomingTo(BarApp_t *app, BarWsConnId_t requester,
                         PianoSong_t *songs, int count) {
	BarSocketIoEmitUpcomingTo(app, requester, songs, count);
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
	if (app && app->settings.uiMode != BAR_UI_MODE_CLI && app->wsContext) {
		pthread_mutex_lock (&app->player.lock);
		/* Pause is published before physical stop. Only completed snapshots
		 * may reach clients; the owning control broadcasts after completion. */
		if (app->player.audioBusy || app->player.audioControlWaiters != 0) {
			pthread_mutex_unlock (&app->player.lock);
			return;
		}
		const BarPlayerPlayStateSnapshot snapshot = {app->player.doPause, app->player.controlEpoch};
		pthread_mutex_unlock (&app->player.lock);
		BarWsBroadcastPlayStateSnapshot (app, &snapshot);
	}
}

void BarWsBroadcastPlayStateSnapshot(BarApp_t *app,
                                      const BarPlayerPlayStateSnapshot *snapshot) {
	if (app && snapshot && app->settings.uiMode != BAR_UI_MODE_CLI && app->wsContext) {
		struct json_object *data = json_object_new_object ();
		json_object_object_add (data, "paused", json_object_new_boolean (snapshot->paused));
		char *msg = BarSocketIoFormatEventMessage ("playState", data);
		json_object_put (data);
		BarWebsocketBroadcastPlayStateMessage (app, msg, snapshot->controlEpoch);
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

/* Error event broadcasting */
void BarWsEmitError(BarApp_t *app, const char *event, const char *msg) {
	BarSocketIoEmitError(app, event, msg);
}

void BarWsEmitErrorEx(BarApp_t *app, const char *event, const char *msg,
                       const char *extra) {
	BarSocketIoEmitErrorEx(app, event, msg, extra);
}

void BarWsEmitErrorTo(BarApp_t *app, BarWsConnId_t requester,
                      const char *event, const char *msg) {
	BarSocketIoEmitErrorTo(app, requester, event, msg);
}

/* Pandora disconnect notification */
void BarWsBroadcastPandoraDisconnected(BarApp_t *app, const char *reason) {
	if (app && app->settings.uiMode != BAR_UI_MODE_CLI && app->wsContext) {
		struct json_object *data = json_object_new_object ();
		json_object_object_add(data, "reason",
				json_object_new_string(reason ? reason : "unknown"));
		char *msg = BarSocketIoFormatEventMessage ("pandora.disconnected", data);
		json_object_put (data);
		BarWebsocketBroadcastSocketIoMessage (app, BUCKET_STATE, msg);
	}
}

/* Web info printing (web or both mode) */
void BarWsPrintWebInfo(const BarApp_t *app, FILE *stream) {
	const char *bind_host;

	if (!BarWsIsWebActive(app)) {
		return;
	}
	bind_host = app->settings.websocketHost ? app->settings.websocketHost
	                                        : "127.0.0.1";
	if (app->settings.webuiPath) {
		fprintf(stream, "Web UI files: %s\n", app->settings.webuiPath);
	} else {
		fprintf(stream, "Web UI files: (using built-in)\n");
	}
	fprintf(stream, "Web interface: http://%s:%d/\n",
	        bind_host, app->settings.websocketPort);

	if (app->settings.websocketPort > 0 &&
	    (strcmp(bind_host, "0.0.0.0") == 0 || strcmp(bind_host, "::") == 0)) {
		char lan[INET_ADDRSTRLEN];

		BarDaemonGetIPv4Address(lan, sizeof(lan));
		if (lan[0] != '\0' && strcmp(lan, "127.0.0.1") != 0) {
			fprintf(stream, "Web interface: http://%s:%d/\n",
			        lan, app->settings.websocketPort);
		}
	}
}

/* Print PID file info if daemon and configured */
void BarWsPrintPidFileInfo(const BarApp_t *app, bool is_daemon, FILE *stream) {
	if (is_daemon && app && app->settings.pidFile) {
		fprintf(stream, "PID file: %s\n", app->settings.pidFile);
	}
}

/* Print startup info on stderr for web-only daemon (journal) before daemonize */
void BarWsPrintStartupInfo(const BarApp_t *app) {
	if (!BarIsWebOnlyMode(app)) {
		return;
	}
	BarPrintStartupInfo(app, getpid(), false, stderr);
}

/* Configure web-only input (no stdin/FIFO) */
void BarWsConfigureWebOnlyInput(BarApp_t *app) {
	app->input.fds[0] = -1;
	app->input.fds[1] = -1;
	app->input.maxfd = 0;
}
