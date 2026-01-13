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

#include "config.h"

/* system includes */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
/* fork () */
#include <unistd.h>
#include <sys/select.h>
#include <time.h>
#include <ctype.h>
/* open () */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
/* tcset/getattr () */
#include <termios.h>
#include <pthread.h>
#include <assert.h>
#include <stdbool.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#ifdef __APPLE__
#include <execinfo.h>
#include <mach-o/dyld.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#else
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
/* waitpid () */
#include <sys/types.h>
#include <sys/wait.h>

/* pandora.com library */
#include <piano.h>

#include "main.h"
#include "debug.h"
#include "terminal.h"
#include "ui.h"
#include "ui_dispatch.h"
#include "bar_state.h"
#include "station_display.h"
#include "playback_manager.h"
#include "system_volume.h"

#ifdef WEBSOCKET_ENABLED
#include "websocket/core/websocket.h"
#include "websocket/protocol/socketio.h"
#include "websocket/daemon/daemon.h"
#endif
#include "websocket_bridge.h"
#include "ui_readline.h"

/*	authenticate user
 */
static bool BarMainLoginUser (BarApp_t *app) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataLogin_t reqData;
	bool ret;

	reqData.user = app->settings.username;
	reqData.password = app->settings.password;
	reqData.step = 0;

	BarUiMsg (&app->settings, MSG_INFO, "Login... ");
	ret = BarUiPianoCall (app, PIANO_REQUEST_LOGIN, &reqData, &pRet, &wRet);
	BarUiStartEventCmd (&app->settings, "userlogin", NULL, NULL, &app->player,
			NULL, pRet, wRet);

	return ret;
}

/*	ask for username/password if none were provided in settings
 */
static bool BarMainGetLoginCredentials (BarSettings_t *settings,
		BarReadlineFds_t *input) {

	bool usernameFromConfig = true;


	if (settings->username == NULL) {
		/* In web-only mode, stdin is not available - credentials must be in config */
		#ifdef WEBSOCKET_ENABLED
		if (settings->uiMode == BAR_UI_MODE_WEB) {
			BarUiMsg (settings, MSG_ERR, "Error: Username not found in config file. "
					"In web-only mode, credentials must be provided in the config.\n");
			return false;
		}
		#endif
		

		char nameBuf[100];

		BarUiMsg (settings, MSG_QUESTION, "Email: ");
		if (BarReadlineStr (nameBuf, sizeof (nameBuf), input, BAR_RL_DEFAULT) == 0) {
			return false;
		}
		settings->username = strdup (nameBuf);
		usernameFromConfig = false;
	} else {
	}


	if (settings->password == NULL) {
		/* In web-only mode, stdin is not available - credentials must be in config */
		#ifdef WEBSOCKET_ENABLED
		if (settings->uiMode == BAR_UI_MODE_WEB) {
			BarUiMsg (settings, MSG_ERR, "Error: Password not found in config file. "
					"In web-only mode, credentials must be provided in the config.\n");
			return false;
		}
		#endif
		
		char passBuf[100];

		if (usernameFromConfig) {
			BarUiMsg (settings, MSG_QUESTION, "Email: %s\n", settings->username);
		}

		if (settings->passwordCmd == NULL) {
			BarUiMsg (settings, MSG_QUESTION, "Password: ");
			if (BarReadlineStr (passBuf, sizeof (passBuf), input, BAR_RL_NOECHO) == 0) {
				puts ("");
				return false;
			}
			/* write missing newline */
			puts ("");
			settings->password = strdup (passBuf);
		} else {
			pid_t chld;
			int pipeFd[2];

			BarUiMsg (settings, MSG_INFO, "Requesting password from external helper... ");

			if (pipe (pipeFd) == -1) {
				BarUiMsg (settings, MSG_NONE, "Error: %s\n", strerror (errno));
				return false;
			}

			chld = fork ();
			if (chld == 0) {
				/* child */
				close (pipeFd[0]);
				dup2 (pipeFd[1], fileno (stdout));
				execl ("/bin/sh", "/bin/sh", "-c", settings->passwordCmd, (char *) NULL);
				BarUiMsg (settings, MSG_NONE, "Error: %s\n", strerror (errno));
				close (pipeFd[1]);
				exit (1);
			} else if (chld == -1) {
				BarUiMsg (settings, MSG_NONE, "Error: %s\n", strerror (errno));
				return false;
			} else {
				/* parent */
				int status;

				close (pipeFd[1]);
				memset (passBuf, 0, sizeof (passBuf));
				ssize_t bytesRead = read (pipeFd[0], passBuf, sizeof (passBuf)-1);
				close (pipeFd[0]);

				if (bytesRead < 0) {
					BarUiMsg (settings, MSG_NONE, "Error reading password: %s\n", strerror (errno));
					waitpid (chld, &status, 0);
					return false;
				}

				/* drop trailing newlines */
				ssize_t len = strlen (passBuf)-1;
				while (len >= 0 && passBuf[len] == '\n') {
					passBuf[len] = '\0';
					--len;
				}

				waitpid (chld, &status, 0);
				if (WEXITSTATUS (status) == 0) {
					settings->password = strdup (passBuf);
					BarUiMsg (settings, MSG_NONE, "Ok.\n");
				} else {
					BarUiMsg (settings, MSG_NONE, "Error: Exit status %i.\n", WEXITSTATUS (status));
					return false;
				}
			}
		} /* end else passwordCmd */
	}

	return true;
}

/*	get station list
 */
static bool BarMainGetStations (BarApp_t *app) {
	PianoReturn_t pRet;
	CURLcode wRet;
	bool ret;

	BarUiMsg (&app->settings, MSG_INFO, "Get stations... ");
	ret = BarUiPianoCall (app, PIANO_REQUEST_GET_STATIONS, NULL, &pRet, &wRet);
	
	/* Update display names after stations are fetched */
	if (ret) {
		BarUpdateStationDisplayNames(app);
	}
	
	BarUiStartEventCmd (&app->settings, "usergetstations", NULL, NULL, &app->player,
			BarStateGetStationList(app), pRet, wRet);
	return ret;
}

/*	get initial station from autostart setting or user input
 */
static void BarMainGetInitialStation (BarApp_t *app) {
	/* try to get autostart station */
	if (app->settings.autostartStation != NULL) {
		PianoStation_t *station = BarStateFindStationById(app,
				app->settings.autostartStation);
		BarStateSetNextStation(app, station);
		if (station == NULL) {
			BarUiMsg (&app->settings, MSG_ERR,
					"Error: Autostart station not found.\n");
		}
	}
	
	/* In web-only mode, don't prompt for station - let the web UI handle it */
	if (BarIsWebOnlyMode(app) && BarStateGetNextStation(app) == NULL) {
		BarUiMsg (&app->settings, MSG_INFO,
				"Waiting for station selection via web interface...\n");
		return;
	}
	
	/* no autostart? ask the user */
	if (BarStateGetNextStation(app) == NULL) {
		PianoStation_t *station = BarUiSelectStation (app, BarStateGetStationList(app),
				"Select station: ", NULL, app->settings.autoselect);
		BarStateSetNextStation(app, station);
	}
}

/*	wait for user input
 */
static void BarMainHandleUserInput (BarApp_t *app) {
	char buf[2];
	if (BarReadline (buf, sizeof (buf), NULL, &app->input,
			BAR_RL_FULLRETURN | BAR_RL_NOECHO | BAR_RL_NOINT, 1) > 0) {
		/* Set context based on Pandora connection status */
		BarUiDispatchContext_t context = BAR_DC_GLOBAL;
		if (BarStateIsPandoraConnected(app)) {
			context |= BAR_DC_PANDORA_CONNECTED;
		} else {
			context |= BAR_DC_PANDORA_DISCONNECTED;
		}
		BarUiDispatch (app, buf[0], BarStateGetCurrentStation(app), 
				BarStateGetPlaylist(app), true, context);
	}
}

/*	fetch new playlist
 */
void BarMainGetPlaylist (BarApp_t *app) {
	PianoReturn_t pRet;
	CURLcode wRet;
	PianoRequestDataGetPlaylist_t reqData;
	reqData.station = BarStateGetNextStation(app);
	reqData.quality = app->settings.audioQuality;

	BarUiMsg (&app->settings, MSG_INFO, "Receiving new playlist... ");
	if (!BarUiPianoCall (app, PIANO_REQUEST_GET_PLAYLIST,
			&reqData, &pRet, &wRet)) {
		BarStateSetNextStation(app, NULL);
	} else {
		BarStateSetPlaylist(app, reqData.retPlaylist);
		if (BarStateGetPlaylist(app) == NULL) {
			BarUiMsg (&app->settings, MSG_INFO, "No tracks left.\n");
			BarStateSetNextStation(app, NULL);
		}
	}
	PianoStation_t *nextStation = BarStateGetNextStation(app);
	BarStateSetCurrentStation(app, nextStation);
	BarUiStartEventCmd (&app->settings, "stationfetchplaylist",
			BarStateGetCurrentStation(app), BarStateGetPlaylist(app), &app->player, 
			BarStateGetStationList(app), pRet, wRet);
}

/*	start new player thread
 */
void BarMainStartPlayback (BarApp_t *app, pthread_t *playerThread) {
	assert (app != NULL);
	assert (playerThread != NULL);

	const PianoSong_t * const curSong = BarStateGetPlaylist(app);
	assert (curSong != NULL);

	PianoStation_t *curStation = BarStateGetCurrentStation(app);
	BarUiPrintSong (&app->settings, curSong, curStation->isQuickMix ?
			BarStateFindStationById(app, curSong->stationId) : NULL);

	static const char httpPrefix[] = "http://";
	/* avoid playing local files */
	if (curSong->audioUrl == NULL ||
			strncmp (curSong->audioUrl, httpPrefix, strlen (httpPrefix)) != 0) {
		BarUiMsg (&app->settings, MSG_ERR, "Invalid song url.\n");
	} else {
		player_t * const player = &app->player;
		BarPlayerReset (player);

		app->player.url = curSong->audioUrl;
		app->player.gain = curSong->fileGain;
		app->player.songDuration = curSong->length;

		assert (interrupted == &app->doQuit);
		interrupted = &app->player.interrupted;

	/* throw event */
	BarUiStartEventCmd (&app->settings, "songstart",
			curStation, curSong, &app->player, BarStateGetStationList(app),
			PIANO_RET_OK, CURLE_OK);

	BarWsBroadcastSongStart(app);

	/* prevent race condition, mode must _not_ be DEAD if
		 * thread has been started */
		app->player.mode = PLAYER_WAITING;
		/* start player */
		pthread_create (playerThread, NULL, BarPlayerThread,
				&app->player);
	}
}

/*	player is done, clean up
 */
static void BarMainPlayerCleanup (BarApp_t *app, pthread_t *playerThread) {
	void *threadRet;

	BarUiStartEventCmd (&app->settings, "songfinish", BarStateGetCurrentStation(app),
			BarStateGetPlaylist(app), &app->player, BarStateGetStationList(app), 
			PIANO_RET_OK, CURLE_OK);

	BarWsBroadcastSongStop(app);

	/* Wait for player thread with timeout to prevent infinite hang */
	bool threadExited = false;
	for (int i = 0; i < 100; i++) {  /* 10 seconds max */
		int ret = pthread_kill(*playerThread, 0);
		if (ret == ESRCH) {
			/* Thread no longer exists - join to clean up */
			pthread_join(*playerThread, &threadRet);
			threadExited = true;
			break;
		}
		usleep(100000);  /* 100ms */
	}

	if (!threadExited) {
		BarUiMsg(&app->settings, MSG_ERR, "Player thread did not exit within 10s, detaching\n");
		pthread_detach(*playerThread);
		threadRet = (void *)PLAYER_RET_HARDFAIL;
	}

	if (threadRet == (void *) PLAYER_RET_OK) {
		app->playerErrors = 0;
	} else if (threadRet == (void *) PLAYER_RET_SOFTFAIL) {
		++app->playerErrors;
		if (app->playerErrors >= app->settings.maxRetry) {
			/* don't continue playback if thread reports too many error */
			BarStateSetNextStation(app, NULL);
		}
	} else {
		BarStateSetNextStation(app, NULL);
	}

	assert (interrupted == &app->player.interrupted);
	interrupted = &app->doQuit;

	app->player.mode = PLAYER_DEAD;
}

/*	print song duration
 */
static void BarMainPrintTime (BarApp_t *app) {
	unsigned int songRemaining;
	char sign[2] = {0, 0};
	player_t * const player = &app->player;

	pthread_mutex_lock (&player->lock);
	const unsigned int songDuration = player->songDuration;
	const unsigned int songPlayed = player->songPlayed;
	pthread_mutex_unlock (&player->lock);

	if (songPlayed <= songDuration) {
		songRemaining = songDuration - songPlayed;
		sign[0] = '-';
	} else {
		/* longer than expected */
		songRemaining = songPlayed - songDuration;
		sign[0] = '+';
	}

	char outstr[512], totalFormatted[16], remainingFormatted[16],
			elapsedFormatted[16];
	const char *vals[] = {totalFormatted, remainingFormatted,
			elapsedFormatted, sign};
	snprintf (totalFormatted, sizeof (totalFormatted), "%02u:%02u",
			songDuration/60, songDuration%60);
	snprintf (remainingFormatted, sizeof (remainingFormatted), "%02u:%02u",
			songRemaining/60, songRemaining%60);
	snprintf (elapsedFormatted, sizeof (elapsedFormatted), "%02u:%02u",
			songPlayed/60, songPlayed%60);
	BarUiCustomFormat (outstr, sizeof (outstr), app->settings.timeFormat,
			"tres", vals);
	BarUiMsg (&app->settings, MSG_TIME, "%s\r", outstr);
}

/*	main loop
 */
static void BarMainLoop (BarApp_t *app) {



	pthread_t playerThread;






	if (!BarMainGetLoginCredentials (&app->settings, &app->input)) {
		return;
	}

	if (!BarMainLoginUser (app)) {
		return;
	}

	if (!BarMainGetStations (app)) {
		return;
	}

	#ifdef WEBSOCKET_ENABLED
	/* Start playback manager for WebSocket modes */
	if (app->settings.uiMode != BAR_UI_MODE_CLI) {
		if (!BarPlaybackManagerStart(app)) {
			return;
		}
	}
	#endif

	player_t * const player = &app->player;
	bool promptedForStation = false;


	while (!app->doQuit) {
		/* One-time prompt if no station selected */
		if (!promptedForStation && BarStateGetNextStation(app) == NULL && 
		    BarStateGetCurrentStation(app) == NULL) {
			#ifdef WEBSOCKET_ENABLED
			if (app->settings.uiMode == BAR_UI_MODE_WEB) {
				BarUiMsg(&app->settings, MSG_INFO,
				         "Waiting for station selection via web interface...\n");
			} else {
			#endif
				BarUiMsg(&app->settings, MSG_INFO,
				         "No station selected. Press 's' to select a station.\n");
			#ifdef WEBSOCKET_ENABLED
			}
			#endif
			promptedForStation = true;
		}

		#ifndef WEBSOCKET_ENABLED
		/* Original playback state machine for non-WebSocket builds */
		/* song finished playing, clean up things/scrobble song */
		if (BarPlayerGetMode (player) == PLAYER_FINISHED) {
			if (player->interrupted != 0) {
				app->doQuit = 1;
			}
			BarMainPlayerCleanup (app, &playerThread);
		}

		/* check whether player finished playing and start playing new
		 * song */
		if (BarPlayerGetMode (player) == PLAYER_DEAD) {
			/* what's next? */
			PianoSong_t *playlist = BarStateGetPlaylist(app);
			if (playlist != NULL) {
				PianoSong_t *histsong = playlist;
				BarStateSetPlaylist(app, PianoListNextP (playlist));
				histsong->head.next = NULL;
				BarUiHistoryPrepend (app, histsong);
			}
			playlist = BarStateGetPlaylist(app);
			PianoStation_t *nextStation = BarStateGetNextStation(app);
			if (playlist == NULL && nextStation != NULL && !app->doQuit) {
				PianoStation_t *curStation = BarStateGetCurrentStation(app);
				if (nextStation != curStation) {
					BarUiPrintStation (&app->settings, nextStation);
				}
				BarMainGetPlaylist (app);
			}
			/* song ready to play */
			playlist = BarStateGetPlaylist(app);
			if (playlist != NULL) {
				BarMainStartPlayback (app, &playerThread);
			}
		}
		#else
		/* WebSocket enabled: playback manager handles state machine */
		/* Web-only mode: just sleep, no CLI needed */
		if (app->settings.uiMode == BAR_UI_MODE_WEB) {
			sleep(1);
			continue;
		}
		/* In BOTH mode: CLI just handles input, playback manager runs independently */
		#endif

		if (!BarShouldSkipCliOutput(app)) {
			BarMainHandleUserInput (app);
		}

	/* show time */
	if (!BarShouldSkipCliOutput(app) && BarPlayerGetMode (player) == PLAYER_PLAYING) {
		BarMainPrintTime (app);
	}

	/* NOTE: Progress broadcasting for WebSocket clients is now handled
	 * directly in the WebSocket thread (BarWebsocketThread) with rate
	 * limiting. This ensures it works in both "web" and "both" modes.
	 */
}

#ifdef WEBSOCKET_ENABLED
/* Stop playback manager */
	if (app->settings.uiMode != BAR_UI_MODE_CLI) {
		BarPlaybackManagerStop(app);
	}
	#endif

	#ifndef WEBSOCKET_ENABLED
	if (BarPlayerGetMode (player) != PLAYER_DEAD) {
		pthread_join (playerThread, NULL);
	}
	#endif
}

sig_atomic_t *interrupted = NULL;

static void intHandler (int signal) {
	if (interrupted != NULL) {
		debugPrint(DEBUG_UI, "Received ^C\n");
		*interrupted += 1;
	}
}

/* Crash handler - re-raise signal to get default behavior (core dump, crash report) */
static void crashHandler (int sig) {
	/* Re-raise signal to get default behavior (core dump, crash report) */
	signal(sig, SIG_DFL);
	raise(sig);
}

static void BarMainSetupSigaction () {
	struct sigaction act = {
			.sa_handler = intHandler,
			.sa_flags = 0,
			};
	sigemptyset (&act.sa_mask);
	sigaction (SIGINT, &act, NULL);
	
	/* Register crash handlers */
	act.sa_handler = crashHandler;
	sigaction (SIGSEGV, &act, NULL);
	sigaction (SIGABRT, &act, NULL);
	sigaction (SIGBUS, &act, NULL);
	sigaction (SIGILL, &act, NULL);
	sigaction (SIGFPE, &act, NULL);
}

/* Get the machine's IPv4 address (first non-loopback interface) */
static void BarMainGetIPv4Address(char *buffer, size_t buffer_size) {
	struct ifaddrs *ifaddrs_ptr = NULL;
	struct ifaddrs *ifa = NULL;
	
	buffer[0] = '\0';
	
	if (getifaddrs(&ifaddrs_ptr) != 0) {
		strncpy(buffer, "127.0.0.1", buffer_size - 1);
		buffer[buffer_size - 1] = '\0';
		return;
	}
	
	/* Find first non-loopback IPv4 address */
	for (ifa = ifaddrs_ptr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL) {
			continue;
		}
		
		/* Check for IPv4 address */
		if (ifa->ifa_addr->sa_family == AF_INET) {
			struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
			const char *ip = inet_ntoa(sin->sin_addr);
			
			/* Skip loopback addresses */
			if (strcmp(ip, "127.0.0.1") != 0 && strcmp(ip, "127.0.0.0") != 0) {
				strncpy(buffer, ip, buffer_size - 1);
				buffer[buffer_size - 1] = '\0';
				break;
			}
		}
	}
	
	freeifaddrs(ifaddrs_ptr);
	
	/* Fallback to localhost if no address found */
	if (buffer[0] == '\0') {
		strncpy(buffer, "127.0.0.1", buffer_size - 1);
		buffer[buffer_size - 1] = '\0';
	}
}

/* Check if --launched-as-daemon flag is present in command-line arguments */
static bool BarMainCheckRelaunchFlag(int argc, char **argv) {
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--launched-as-daemon") == 0) {
			return true;
		}
	}
	return false;
}

/* Relaunch pianobar as a daemon using fork+exec on macOS when ui_mode=web
 * This avoids CoreAudio XPC service issues that occur after fork() */
static void BarMainRelaunchAsDaemon(int argc, char **argv) {
#ifdef __APPLE__
	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "Failed to fork for relaunch: %s\n", strerror(errno));
		return;
	}
	
	if (pid == 0) {
		/* Child process: exec pianobar with --launched-as-daemon flag */
		/* Build new argv array with --launched-as-daemon inserted after program name */
		char **new_argv = malloc((argc + 2) * sizeof(char *));
		if (!new_argv) {
			fprintf(stderr, "Failed to allocate memory for relaunch\n");
			exit(1);
		}
		
		/* Get path to current executable */
		char exe_path[PATH_MAX];
		uint32_t size = sizeof(exe_path);
		if (_NSGetExecutablePath(exe_path, &size) != 0) {
			free(new_argv);
			fprintf(stderr, "Failed to get executable path\n");
			exit(1);
		}
		
		/* Build new argv: [exe_path, --launched-as-daemon, original_args...] */
		new_argv[0] = exe_path;
		new_argv[1] = "--launched-as-daemon";
		for (int i = 1; i < argc; i++) {
			new_argv[i + 1] = argv[i];
		}
		new_argv[argc + 1] = NULL;
		
		/* Exec the new process - this replaces the current process */
		execvp(exe_path, new_argv);
		
		/* If we get here, exec failed */
		fprintf(stderr, "Failed to exec pianobar: %s\n", strerror(errno));
		free(new_argv);
		exit(1);
	}
	
	/* Parent process: wait briefly for child to start, then exit */
	/* This allows the child (exec'd process) to detach from the terminal */
	sleep(1);
	exit(0);
#else
	/* Not macOS, no-op */
	(void)argc;
	(void)argv;
#endif
}

int main (int argc, char **argv) {
	static BarApp_t app;


	/* Check for --launched-as-daemon flag early (before any initialization) */
	bool launched_as_daemon = BarMainCheckRelaunchFlag(argc, argv);

	memset (&app, 0, sizeof (app));


	/* Read settings EARLY to check ui_mode before any CoreAudio initialization
	 * This is the ONLY initialization we do before detecting ui_mode=web */
	BarSettingsInit (&app.settings);
	BarSettingsRead (&app.settings);

	/* Check if pianobar is already running in web mode and kill it if so */
#ifdef WEBSOCKET_ENABLED
	if (app.settings.uiMode == BAR_UI_MODE_WEB && app.settings.pidFile) {
		if (BarDaemonIsRunning(app.settings.pidFile)) {
			/* Read PID for message */
			FILE *fp = fopen(app.settings.pidFile, "r");
			pid_t old_pid = 0;
			if (fp) {
				int scanned = fscanf(fp, "%d", &old_pid);
				(void)scanned;  /* Ignore scan result - just for message */
				fclose(fp);
			}
			
			fprintf(stderr, "Pianobar is already running (PID: %d). Stopping it and starting a new instance...\n", 
			        (int)old_pid);
			
			if (BarDaemonKillRunning(app.settings.pidFile)) {
				/* Wait a moment for process to fully terminate */
				usleep(500000); /* 0.5 seconds */
			} else {
				fprintf(stderr, "Warning: Failed to kill running instance. Continuing anyway...\n");
			}
		}
	}
#endif

	/* On macOS with ui_mode=web, relaunch using fork+exec to avoid CoreAudio XPC issues */
#ifdef __APPLE__
	#ifdef WEBSOCKET_ENABLED
	if (!launched_as_daemon && app.settings.uiMode == BAR_UI_MODE_WEB) {
		BarMainRelaunchAsDaemon(argc, argv);
		/* If we get here, relaunch failed - continue anyway */
	}
	#endif
#endif

	/* Output UI path and URL before starting in web or both mode
	 * Do this AFTER relaunch check so it only prints once (in the final process) */
#ifdef WEBSOCKET_ENABLED
	if (app.settings.uiMode == BAR_UI_MODE_WEB || app.settings.uiMode == BAR_UI_MODE_BOTH) {
		fprintf(stderr, "Starting pianobar\n");
		
		/* Get IPv4 address */
		char ipv4_addr[INET_ADDRSTRLEN];
		BarMainGetIPv4Address(ipv4_addr, sizeof(ipv4_addr));
		
		/* Output webui_path */
		if (app.settings.webuiPath) {
			fprintf(stderr, "Web UI files: %s\n", app.settings.webuiPath);
		} else {
			fprintf(stderr, "Web UI files: (using built-in)\n");
		}
		
		/* Output URL with IPv4 address */
		if (app.settings.websocketPort > 0) {
			fprintf(stderr, "Web interface: http://%s:%d/\n", ipv4_addr, app.settings.websocketPort);
		}
	}
#endif

	/* NOW do other initialization (after relaunch check, if any) */
	debugEnable();


	/* Disable Objective-C runtime fork safety check on macOS
	 * CoreAudio uses Objective-C internally, and macOS crashes if Objective-C
	 * classes were being initialized in another thread when fork() was called.
	 * This allows CoreAudio to work after fork. */
#ifdef __APPLE__
	setenv("OBJC_DISABLE_INITIALIZE_FORK_SAFETY", "YES", 1);
#endif

	/* signals */
	signal (SIGPIPE, SIG_IGN);
	BarMainSetupSigaction ();
	interrupted = &app.doQuit;

	/* init some things */
	gcry_check_version (NULL);
	gcry_control (GCRYCTL_DISABLE_SECMEM, 0);
	gcry_control (GCRYCTL_INITIALIZATION_FINISHED, 0);
	

	/* Initialize state but NOT player yet - player init must come after daemonization
	 * because ma_engine_init() opens audio device file descriptors */
	BarStateInit (&app);


	/* Initialize system volume control BEFORE daemonization
	 * popen() and CoreAudio APIs require working file descriptors and user session access */
	if (app.settings.volumeMode == BAR_VOLUME_MODE_SYSTEM) {
		if (BarSystemVolumeInit()) {
			/* In system mode, set player to configured initial gain.
			 * Default 75% leaves headroom for ReplayGain boosts.
			 * The OS mixer controls actual volume, not the player's gain stage.
			 * System volume is read directly when needed for display/broadcast. */
			app.settings.volume = app.settings.systemVolumePlayerGain;
		} else {
			/* Fall back to player volume if system volume unavailable */
			fprintf(stderr, "Warning: System volume control unavailable, falling back to player volume\n");
			app.settings.volumeMode = BAR_VOLUME_MODE_PLAYER;
		}
	}
	
	/* Initialize libcurl BEFORE daemonization
	 * curl_global_init() must happen BEFORE fork. OpenSSL (used by curl) can detect
	 * if it's running in a forked child and may refuse to work. However, curl_easy_init()
	 * (which creates the actual handle) should be called AFTER fork in the child process. */
	
	
	curl_global_init (CURL_GLOBAL_DEFAULT);


	/* NOTE: ma_engine_init() must happen AFTER fork in the child process.
	 * CoreAudio creates background threads that don't survive fork properly.
	 * Initializing before fork causes CoreAudio threads to crash when cleaning up. */


	/* Daemonize if running in web-only mode - BEFORE player init
	 * On macOS with --launched-as-daemon flag, we've already been relaunched via exec,
	 * so we just need to perform daemonization steps without forking again. */
	if (app.settings.uiMode == BAR_UI_MODE_WEB) {
#ifdef __APPLE__
		if (launched_as_daemon) {
			
			/* Do daemonization steps FIRST to detach from terminal immediately */
			if (!BarDaemonizeSteps(&app)) {
				fprintf(stderr, "Failed to perform daemonization steps\n");
				return 1;
			}
			
			/* Print info after daemonization (stdout/stderr are redirected, but
			 * messages may still appear if logFile is configured) */
			fprintf(stderr, "Pianobar daemon started (PID: %d)\n", getpid());
			fprintf(stderr, "Web interface: http://%s:%d/\n",
			       app.settings.websocketHost ? app.settings.websocketHost : "127.0.0.1",
			       app.settings.websocketPort);
			
			if (app.settings.pidFile) {
				fprintf(stderr, "PID file: %s\n", app.settings.pidFile);
			}
		} else {
			/* Should have been relaunched earlier - this shouldn't happen */
			fprintf(stderr, "Error: ui_mode=web on macOS requires relaunch, but relaunch was skipped\n");
			return 1;
		}
#else
		
		/* Normal daemonization on non-macOS platforms */
		if (!BarWsDaemonize(&app)) {
			fprintf(stderr, "Failed to daemonize\n");
			return 1;
		}

#endif

	} else {
	}


	/* Initialize player AFTER daemonization */
	BarPlayerInit (&app.player, &app.settings);


	/* save terminal attributes, before disabling echoing */
	if (!BarShouldSkipCliOutput(&app)) {
		BarTermInit ();
	}


	PianoReturn_t pret;
	if ((pret = PianoInit (&app.ph, app.settings.partnerUser,
			app.settings.partnerPassword, app.settings.device,
			app.settings.inkey, app.settings.outkey)) != PIANO_RET_OK) {
		BarUiMsg (&app.settings, MSG_ERR, "Initialization failed:"
				" %s\n", PianoErrorToStr (pret));
		return 0;
	}
	

	if (!BarShouldSkipCliOutput(&app)) {
		BarUiMsg (&app.settings, MSG_NONE,
				"Welcome to " PACKAGE " (" VERSION ")! ");
		if (app.settings.keys[BAR_KS_HELP] == BAR_KS_DISABLED) {
			BarUiMsg (&app.settings, MSG_NONE, "\n");
		} else {
			BarUiMsg (&app.settings, MSG_NONE,
					"Press %c for a list of commands.\n",
					app.settings.keys[BAR_KS_HELP]);
		}
	}


	app.http = curl_easy_init ();
	

	assert (app.http != NULL);


	/* init fds */
	FD_ZERO(&app.input.set);
	

	#ifdef WEBSOCKET_ENABLED
	if (app.settings.uiMode != BAR_UI_MODE_WEB) {
	#endif
		app.input.fds[0] = STDIN_FILENO;
		FD_SET(app.input.fds[0], &app.input.set);
		
		/* open fifo read/write so it won't EOF if nobody writes to it */
		assert (sizeof (app.input.fds) / sizeof (*app.input.fds) >= 2);
		app.input.fds[1] = open (app.settings.fifo, O_RDWR);
		if (app.input.fds[1] != -1) {
			struct stat s;
			
			/* check for file type, must be fifo */
			fstat (app.input.fds[1], &s);
			if (!S_ISFIFO (s.st_mode)) {
				BarUiMsg (&app.settings, MSG_ERR, "File at %s is not a fifo\n", app.settings.fifo);
				close (app.input.fds[1]);
				app.input.fds[1] = -1;
			} else {
				FD_SET(app.input.fds[1], &app.input.set);
				BarUiMsg (&app.settings, MSG_INFO, "Control fifo at %s opened\n",
						app.settings.fifo);
			}
		}
		app.input.maxfd = app.input.fds[0] > app.input.fds[1] ? app.input.fds[0] :
				app.input.fds[1];
		++app.input.maxfd;
	#ifdef WEBSOCKET_ENABLED
	} else {
		/* Web-only mode: no stdin/FIFO needed */
		app.input.fds[0] = -1;
		app.input.fds[1] = -1;
		app.input.maxfd = 0;
	}
	#endif


	/* Initialize WebSocket server if enabled */
	if (!BarWsInit(&app)) {
		BarUiMsg (&app.settings, MSG_ERR, "Failed to start WebSocket server\n");
	} else {
	}



	/* Log immediately before call - avoid using local variables that might be on corrupted stack */

	/* Call BarMainLoop directly - crash happens here or in function prologue
	 * Using &app directly avoids any stack variable issues */
	BarMainLoop (&app);


	if (app.input.fds[1] != -1) {
		close (app.input.fds[1]);
	}

	/* write statefile */
	BarSettingsWrite (app.curStation, &app.settings);

	PianoDestroy (&app.ph);
	PianoDestroyPlaylist (app.songHistory);
	PianoDestroyPlaylist (app.playlist);
	curl_easy_cleanup (app.http);
	curl_global_cleanup ();
	BarPlayerDestroy (&app.player);
	
	/* Cleanup WebSocket server */
	BarWsDestroy(&app);
	
	/* Remove PID file if we created one */
	BarWsRemovePidFile(&app);
	
	/* Cleanup system volume control */
	BarSystemVolumeDestroy();
	
	BarStateDestroy (&app);
	BarSettingsDestroy (&app.settings);

	/* restore terminal attributes, zsh doesn't need this, bash does... */
	if (!BarShouldSkipCliOutput(&app)) {
		BarTermRestore ();
	}

	return 0;
}

