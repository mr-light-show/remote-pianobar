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
#include "player.h"

/* Mode predicates */
bool BarIsWebOnlyMode(const BarApp_t *app) {
	return app && app->settings.uiMode == BAR_UI_MODE_WEB;
}

bool BarShouldSkipCliOutput(const BarApp_t *app) {
	return app && app->settings.uiMode == BAR_UI_MODE_WEB;
}

/* Event broadcasts */
void BarWsBroadcastVolume(BarApp_t *app) {
	if (app && app->wsContext) {
		int volumePercent;
		if (app->settings.volumeMode == BAR_VOLUME_MODE_SYSTEM) {
			/* In system mode, read current volume from OS (already 0-100%) */
			volumePercent = BarSystemVolumeGet();
			if (volumePercent < 0) volumePercent = 50;  /* Fallback */
		} else {
			/* Player mode - volume is already 0-100 linear */
			volumePercent = app->settings.volume;
		}
		BarSocketIoEmitVolume(app, volumePercent);
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
	if (app && app->settings.uiMode != BAR_UI_MODE_CLI) {
		BarWebsocketBroadcastSongStart(app);
	}
}

void BarWsBroadcastSongStop(BarApp_t *app) {
	if (app && app->settings.uiMode != BAR_UI_MODE_CLI) {
		BarWebsocketBroadcastSongStop(app);
	}
}

void BarWsBroadcastProgress(BarApp_t *app) {
	if (app && app->settings.uiMode != BAR_UI_MODE_CLI) {
		if (BarPlayerGetMode(&app->player) == PLAYER_PLAYING) {
			BarWebsocketBroadcastProgress(app);
		}
	}
}

void BarWsBroadcastPlayState(BarApp_t *app) {
	if (app && app->wsContext) {
		BarSocketIoEmitPlayState(app);
	}
}

void BarWsBroadcastStations(BarApp_t *app) {
	if (app && app->wsContext) {
		BarSocketIoEmitStations(app);
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

/* Command processing */
void BarWsProcessCommands(BarApp_t *app) {
	/* Command processing is handled inline in main.c due to complexity */
	/* This function is kept for API completeness but currently unused */
	(void)app;
}

#else
/* No-op implementations when WebSocket support is disabled */
bool BarIsWebOnlyMode(const BarApp_t *app) { (void)app; return false; }
bool BarShouldSkipCliOutput(const BarApp_t *app) { (void)app; return false; }

void BarWsBroadcastVolume(BarApp_t *app) { (void)app; }
void BarWsBroadcastExplanation(BarApp_t *app, const char *explanation) { 
	(void)app; (void)explanation; 
}
void BarWsBroadcastUpcoming(BarApp_t *app, PianoSong_t *songs, int count) { 
	(void)app; (void)songs; (void)count;
}
void BarWsBroadcastSongStart(BarApp_t *app) { (void)app; }
void BarWsBroadcastSongStop(BarApp_t *app) { (void)app; }
void BarWsBroadcastProgress(BarApp_t *app) { (void)app; }
void BarWsBroadcastPlayState(BarApp_t *app) { (void)app; }
void BarWsBroadcastStations(BarApp_t *app) { (void)app; }
void BarWsDisconnectAllClients(BarApp_t *app) { (void)app; }

bool BarWsInit(BarApp_t *app) { (void)app; return true; }
void BarWsDestroy(BarApp_t *app) { (void)app; }
bool BarWsDaemonize(BarApp_t *app) { (void)app; return true; }
void BarWsRemovePidFile(BarApp_t *app) { (void)app; }

void BarWsProcessCommands(BarApp_t *app) { (void)app; }
#endif

