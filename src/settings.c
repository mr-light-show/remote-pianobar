/*
Copyright (c) 2008-2015
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

/* application settings */

#include "config.h"
#include "bar_constants.h"
#include "log.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include <ctype.h>

#include <piano.h>

#include "settings.h"
#include "config.h"
#include "ui.h"
#include "ui_dispatch.h"

#define streq(a, b) (strcmp (a, b) == 0)

/*	Get current user’s home directory
 */
static char *BarSettingsGetHome () {
	char *home;

	/* try environment variable */
	if ((home = getenv ("HOME")) != NULL && strlen (home) > 0) {
		return strdup (home);
	}

	/* try passwd mechanism */
	struct passwd * const pw = getpwuid (getuid ());
	if (pw != NULL && pw->pw_dir != NULL && strlen (pw->pw_dir) > 0) {
		return strdup (pw->pw_dir);
	}

	return NULL;
}

/*	Get XDG config directory, which is set by BarSettingsRead (if not set)
 */
static char *BarGetXdgConfigDir (const char * const filename) {
	assert (filename != NULL);

	char *xdgConfigDir;

	if ((xdgConfigDir = getenv ("XDG_CONFIG_HOME")) != NULL &&
			strlen (xdgConfigDir) > 0) {
		const size_t len = (strlen (xdgConfigDir) + 1 +
				strlen (filename) + 1);
		char * const concat = malloc (len * sizeof (*concat));
		snprintf (concat, len, "%s/%s", xdgConfigDir, filename);
		return concat;
	}

	return NULL;
}

/*	Expand ~/ to user’s home directory
 */
char *BarSettingsExpandTilde (const char * const path, const char * const home) {
	assert (path != NULL);
	assert (home != NULL);

	if (strncmp (path, "~/", 2) == 0) {
		char * const expanded = malloc ((strlen (home) + 1 + strlen (path)-2 + 1) *
				sizeof (*expanded));
		sprintf (expanded, "%s/%s", home, &path[2]);
		return expanded;
	}

	return strdup (path);
}

/*	initialize settings structure
 *	@param settings struct
 */
void BarSettingsInit (BarSettings_t *settings) {
	memset (settings, 0, sizeof (*settings));
}

/*	free an account entry's strings
 */
static void BarAccountDestroy (BarAccount_t *acct) {
	free (acct->id);
	free (acct->label);
	free (acct->username);
	free (acct->password);
	free (acct->passwordCmd);
	free (acct->autostartStation);
}

/*	free settings structure, zero it afterwards
 *	@oaram pointer to struct
 */
void BarSettingsDestroy (BarSettings_t *settings) {
	free (settings->controlProxy);
	free (settings->proxy);
	free (settings->bindTo);
	free (settings->username);
	free (settings->password);
	free (settings->passwordCmd);
	free (settings->autostartStation);
	free (settings->eventCmd);
	free (settings->loveIcon);
	free (settings->banIcon);
	free (settings->tiredIcon);
	free (settings->atIcon);
	free (settings->npSongFormat);
	free (settings->npStationFormat);
	free (settings->listSongFormat);
	free (settings->timeFormat);
	free (settings->fifo);
	free (settings->audioPipe);
	free (settings->rpcHost);
	free (settings->rpcTlsPort);
	free (settings->partnerUser);
	free (settings->partnerPassword);
	free (settings->device);
	free (settings->inkey);
	free (settings->outkey);
	for (size_t i = 0; i < MSG_COUNT; i++) {
		free (settings->msgFormat[i].prefix);
		free (settings->msgFormat[i].postfix);
	}
	
	/* WebSocket cleanup */
	#ifdef WEBSOCKET_ENABLED
	free (settings->websocketHost);
	free (settings->webuiPath);
	free (settings->pidFile);
	free (settings->logFile);
	#endif
	
	/* ALSA mixer cleanup */
	free (settings->alsaMixer);
	
	/* Free station display name overrides */
	if (settings->stationDisplayNameOverrides) {
		for (size_t i = 0; i < settings->stationDisplayNameOverrideCount; i++) {
			free (settings->stationDisplayNameOverrides[i].pattern);
			free (settings->stationDisplayNameOverrides[i].replacement);
			if (settings->stationDisplayNameOverrides[i].valid) {
				regfree (&settings->stationDisplayNameOverrides[i].compiled);
			}
		}
		free (settings->stationDisplayNameOverrides);
	}

	/* Free account list */
	if (settings->accounts) {
		for (size_t i = 0; i < settings->accountCount; i++) {
			BarAccountDestroy (&settings->accounts[i]);
		}
		free (settings->accounts);
	}
	
	memset (settings, 0, sizeof (*settings));
}

/* Temporary storage for account = id:path lines during config parsing */
typedef struct {
	char *id;
	char *path;
} BarAccountLine_t;

/*	Resolve an account file path relative to the config directory.
 *	Absolute paths and ~ paths are returned as-is (after tilde expansion).
 *	Relative paths are resolved against configDir.
 */
static char *BarSettingsResolveAccountPath (const char *path, const char *configDir,
		const char *userhome) {
	if (path[0] == '/' || path[0] == '~') {
		return BarSettingsExpandTilde (path, userhome);
	}
	/* relative to config dir */
	size_t len = strlen (configDir) + 1 + strlen (path) + 1;
	char *resolved = malloc (len);
	snprintf (resolved, len, "%s/%s", configDir, path);
	return resolved;
}

/*	Read credentials and per-account keys from a file, populating an account entry.
 *	Only credential-related and per-account keys are read (user, password,
 *	password_command, account_label, autostart_station).
 */
static void BarSettingsReadAccountFile (BarAccount_t *acct, const char *filepath) {
	FILE *fd = fopen (filepath, "r");
	if (fd == NULL) {
		log_write (LOG_ERROR, "Warning: Cannot open account file: %s\n", filepath);
		return;
	}

	char line[1024];
	while (fgets (line, sizeof (line), fd) != NULL) {
		char *key = line;
		while (isspace ((unsigned char) *key)) ++key;
		if (*key == '#' || *key == '\0') continue;

		char *val = strchr (line, '=');
		if (val == NULL) continue;
		*val = '\0';
		++val;

		/* trim key */
		char *keyend = &key[strlen (key) - 1];
		while (keyend >= key && isspace ((unsigned char) *keyend)) {
			*keyend = '\0';
			--keyend;
		}

		/* strip leading space, trim trailing cr/lf */
		if (isspace ((unsigned char) val[0])) ++val;
		char *valend = &val[strlen (val) - 1];
		while (valend >= val && (*valend == '\r' || *valend == '\n')) {
			*valend = '\0';
			--valend;
		}

		if (streq ("user", key)) {
			free (acct->username);
			acct->username = strdup (val);
		} else if (streq ("password", key)) {
			free (acct->password);
			acct->password = strdup (val);
		} else if (streq ("password_command", key)) {
			free (acct->passwordCmd);
			acct->passwordCmd = strdup (val);
		} else if (streq ("account_label", key)) {
			free (acct->label);
			acct->label = strdup (val);
		} else if (streq ("autostart_station", key)) {
			free (acct->autostartStation);
			acct->autostartStation = strdup (val);
		}
	}
	fclose (fd);
}

/*	Build the accounts array from parsed config.
 *	If account lines exist: build list with optional primary from main config.
 *	If no account lines: build single account from main config credentials.
 */
static void BarSettingsBuildAccounts (BarSettings_t *settings,
		const char *mainAccountId, const char *defaultAccount,
		const char *accountLabel,
		BarAccountLine_t *accountLines, size_t accountLineCount,
		const char *configDir, const char *userhome) {
	bool hasMainCredentials = (settings->username != NULL);
	size_t total = accountLineCount;
	size_t startIndex = 0;

	if (hasMainCredentials) {
		total += 1;  /* primary account from main config */
		startIndex = 1;
	}

	if (total == 0) {
		/* no credentials anywhere */
		return;
	}

	settings->accounts = calloc (total, sizeof (BarAccount_t));
	settings->accountCount = total;

	/* Primary account from main config credentials */
	if (hasMainCredentials) {
		BarAccount_t *primary = &settings->accounts[0];
		primary->id = strdup (mainAccountId ? mainAccountId : "default");
		primary->label = strdup (accountLabel ? accountLabel :
				(mainAccountId ? mainAccountId : "default"));
		primary->username = strdup (settings->username);
		if (settings->password) {
			primary->password = strdup (settings->password);
		}
		if (settings->passwordCmd) {
			primary->passwordCmd = strdup (settings->passwordCmd);
		}
		if (settings->autostartStation) {
			primary->autostartStation = strdup (settings->autostartStation);
		}
	}

	/* File-backed accounts */
	for (size_t i = 0; i < accountLineCount; i++) {
		BarAccount_t *acct = &settings->accounts[startIndex + i];
		acct->id = strdup (accountLines[i].id);
		/* label defaults to id, may be overridden by account_label in file */
		acct->label = strdup (accountLines[i].id);

		/* Inherit main config credentials as defaults for merge */
		if (settings->username) acct->username = strdup (settings->username);
		if (settings->password) acct->password = strdup (settings->password);
		if (settings->passwordCmd) acct->passwordCmd = strdup (settings->passwordCmd);

		/* Read account file (overrides inherited fields) */
		char *resolved = BarSettingsResolveAccountPath (accountLines[i].path,
				configDir, userhome);
		BarSettingsReadAccountFile (acct, resolved);
		free (resolved);
	}

	/* Resolve default_account to activeAccountIndex */
	settings->activeAccountIndex = 0;
	if (defaultAccount) {
		bool found = false;
		for (size_t i = 0; i < settings->accountCount; i++) {
			if (streq (settings->accounts[i].id, defaultAccount)) {
				settings->activeAccountIndex = i;
				found = true;
				break;
			}
		}
		if (!found) {
			log_write (LOG_ERROR, "Warning: default_account '%s' not found, "
					"using first account\n", defaultAccount);
		}
	}
}

/*	read app settings from file; format is: key = value\n
 *	@param where to save these settings
 *	@return nothing yet
 */
void BarSettingsRead (BarSettings_t *settings) {
	char * const configfiles[] = {PACKAGE "/state", PACKAGE "/config"};
	char * const userhome = BarSettingsGetHome ();
	assert (userhome != NULL);
	/* set xdg config path (if not set) */
	char * const defaultxdg = malloc (strlen (userhome) + strlen ("/.config") + 1);
	sprintf (defaultxdg, "%s/.config", userhome);
	setenv ("XDG_CONFIG_HOME", defaultxdg, 0);
	free (defaultxdg);

	assert (sizeof (settings->keys) / sizeof (*settings->keys) ==
			sizeof (dispatchActions) / sizeof (*dispatchActions));

	/* apply defaults */
	settings->audioQuality = PIANO_AQ_HIGH;
	settings->autoselect = true;
	settings->history = 5;
	settings->volume = DEFAULT_VOLUME_PERCENT;
	settings->systemVolumePlayerGain = 75;  /* 75% default for system volume mode */
	settings->timeout = 30; /* seconds */
	settings->pauseTimeout = 30; /* minutes, 0 = disabled */
	settings->gainMul = 1.0;
	settings->maxGain = 10;
	/* should be > 4, otherwise expired audio urls (403) can stop playback */
	settings->maxRetry = 5;
	settings->bufferSecs = 5;
	settings->sortOrder = BAR_SORT_NAME_AZ;
	settings->loveIcon = strdup (" <3");
	settings->banIcon = strdup (" </3");
	settings->tiredIcon = strdup (" zZ");
	settings->atIcon = strdup (" @ ");
	settings->npSongFormat = strdup ("\"%t\" by \"%a\" on \"%l\"%r%@%s");
	settings->npStationFormat = strdup ("Station \"%n\" (%i)");
	settings->listSongFormat = strdup ("%i) %a - %t%r");
	settings->timeFormat = strdup ("%s%r/%t");
	settings->rpcHost = strdup (PIANO_RPC_HOST);
	settings->rpcTlsPort = strdup ("443");
	settings->partnerUser = strdup ("android");
	settings->partnerPassword = strdup ("AC7IBG09A3DTSYM4R41UJWL07VLN8JI7");
	settings->device = strdup ("android-generic");
	settings->inkey = strdup ("R=U!LH$O2B#");
	settings->outkey = strdup ("6#26FRL$ZWD");
	settings->fifo = BarGetXdgConfigDir (PACKAGE "/ctl");
	settings->audioPipe = NULL;
	assert (settings->fifo != NULL);
	settings->sampleRate = 0; /* default to stream sample rate */
	
	settings->stationDisplayNameOverrides = NULL;
	settings->stationDisplayNameOverrideCount = 0;

	settings->accounts = NULL;
	settings->accountCount = 0;
	settings->activeAccountIndex = 0;

	/* Temporary multi-account parsing state */
	char *mainAccountId = NULL;
	char *defaultAccount = NULL;
	char *accountLabel = NULL;
	BarAccountLine_t *accountLines = NULL;
	size_t accountLineCount = 0;
	char *configDir = NULL;

	settings->msgFormat[MSG_NONE].prefix = NULL;
	settings->msgFormat[MSG_NONE].postfix = NULL;
	settings->msgFormat[MSG_INFO].prefix = strdup ("(i) ");
	settings->msgFormat[MSG_INFO].postfix = NULL;
	settings->msgFormat[MSG_PLAYING].prefix = strdup ("|>  ");
	settings->msgFormat[MSG_PLAYING].postfix = NULL;
	settings->msgFormat[MSG_TIME].prefix = strdup ("#   ");
	settings->msgFormat[MSG_TIME].postfix = NULL;
	settings->msgFormat[MSG_ERR].prefix = strdup ("/!\\ ");
	settings->msgFormat[MSG_ERR].postfix = NULL;
	settings->msgFormat[MSG_QUESTION].prefix = strdup ("[?] ");
	settings->msgFormat[MSG_QUESTION].postfix = NULL;
	settings->msgFormat[MSG_LIST].prefix = strdup ("\t");
	settings->msgFormat[MSG_LIST].postfix = NULL;

	for (size_t i = 0; i < BAR_KS_COUNT; i++) {
		settings->keys[i] = dispatchActions[i].defaultKey;
	}

	/* read config files */
	for (size_t j = 0; j < sizeof (configfiles) / sizeof (*configfiles); j++) {
		static const char *formatMsgPrefix = "format_msg_";
		FILE *configfd;
		char line[BAR_BUF_MEDIUM];
		size_t lineNum = 0;

		char * const path = BarGetXdgConfigDir (configfiles[j]);
		assert (path != NULL);
		if ((configfd = fopen (path, "r")) == NULL) {
			free (path);
			continue;
		}

		while (1) {
			++lineNum;
			char * const ret = fgets (line, sizeof (line), configfd);
			if (ret == NULL) {
				/* EOF or error */
				break;
			}
			if (strchr (line, '\n') == NULL && !feof (configfd)) {
				BarUiMsg (settings, MSG_INFO, "Line %s:%zu too long, "
						"ignoring\n", path, lineNum);
				continue;
			}
			/* parse lines that match "^\s*(.*?)\s?=\s?(.*)$". Windows and Unix
			 * line terminators are supported. */
			char *key = line;

			/* skip leading spaces */
			while (isspace ((unsigned char) key[0])) {
				++key;
			}

			/* skip comments */
			if (key[0] == '#') {
				continue;
			}

			/* search for delimiter and split key-value pair */
			char *val = strchr (line, '=');
			if (val == NULL) {
				/* no warning for empty lines */
				if (key[0] != '\0') {
					BarUiMsg (settings, MSG_INFO,
							"Invalid line at %s:%zu\n", path, lineNum);
				}
				/* invalid line */
				continue;
			}
			*val = '\0';
			++val;

			/* drop spaces at the end */
			char *keyend = &key[strlen (key)-1];
			while (keyend >= key && isspace ((unsigned char) *keyend)) {
				*keyend = '\0';
				--keyend;
			}

			/* strip at most one space, legacy cruft, required for values with
			 * leading spaces like love_icon */
			if (isspace ((unsigned char) val[0])) {
				++val;
			}
			/* drop trailing cr/lf */
			char *valend = &val[strlen (val)-1];
			while (valend >= val && (*valend == '\r' || *valend == '\n')) {
				*valend = '\0';
				--valend;
			}

			if (streq ("control_proxy", key)) {
				settings->controlProxy = strdup (val);
			} else if (streq ("proxy", key)) {
				settings->proxy = strdup (val);
			} else if (streq ("bind_to", key)) {
				settings->bindTo = strdup (val);
			} else if (streq ("user", key)) {
				settings->username = strdup (val);
			} else if (streq ("password", key)) {
				settings->password = strdup (val);
			} else if (streq ("password_command", key)) {
				settings->passwordCmd = strdup (val);
			} else if (streq ("rpc_host", key)) {
				free (settings->rpcHost);
				settings->rpcHost = strdup (val);
			} else if (streq ("rpc_tls_port", key)) {
				free (settings->rpcTlsPort);
				settings->rpcTlsPort = strdup (val);
			} else if (streq ("partner_user", key)) {
				free (settings->partnerUser);
				settings->partnerUser = strdup (val);
			} else if (streq ("partner_password", key)) {
				free (settings->partnerPassword);
				settings->partnerPassword = strdup (val);
			} else if (streq ("device", key)) {
				free (settings->device);
				settings->device = strdup (val);
			} else if (streq ("encrypt_password", key)) {
				free (settings->outkey);
				settings->outkey = strdup (val);
			} else if (streq ("decrypt_password", key)) {
				free (settings->inkey);
				settings->inkey = strdup (val);
			} else if (streq ("ca_bundle", key)) {
				free (settings->caBundle);
				settings->caBundle = strdup (val);
			} else if (memcmp ("act_", key, 4) == 0) {
				size_t i;
				/* keyboard shortcuts */
				for (i = 0; i < BAR_KS_COUNT; i++) {
					if (streq (dispatchActions[i].configKey, key)) {
						if (streq (val, "disabled")) {
							settings->keys[i] = BAR_KS_DISABLED;
						} else {
							settings->keys[i] = val[0];
						}
						break;
					}
				}
			} else if (streq ("audio_quality", key)) {
				if (streq (val, "low")) {
					settings->audioQuality = PIANO_AQ_LOW;
				} else if (streq (val, "medium")) {
					settings->audioQuality = PIANO_AQ_MEDIUM;
				} else if (streq (val, "high")) {
					settings->audioQuality = PIANO_AQ_HIGH;
				}
			} else if (streq ("autostart_station", key)) {
				free (settings->autostartStation);
				settings->autostartStation = strdup (val);
			} else if (streq ("event_command", key)) {
				settings->eventCmd = BarSettingsExpandTilde (val, userhome);
			} else if (streq ("history", key)) {
				settings->history = atoi (val);
			} else if (streq ("max_retry", key)) {
				settings->maxRetry = atoi (val);
			} else if (streq ("timeout", key)) {
				settings->timeout = atoi (val);
			} else if (streq ("pause_timeout", key)) {
				settings->pauseTimeout = atoi (val);
			} else if (streq ("buffer_seconds", key)) {
				settings->bufferSecs = atoi (val);
			} else if (streq ("sort", key)) {
				size_t i;
				static const char *mapping[] = {"name_az",
						"name_za",
						"quickmix_01_name_az",
						"quickmix_01_name_za",
						"quickmix_10_name_az",
						"quickmix_10_name_za",
						};
				for (i = 0; i < BAR_SORT_COUNT; i++) {
					if (streq (mapping[i], val)) {
						settings->sortOrder = i;
						break;
					}
				}
			} else if (streq ("love_icon", key)) {
				free (settings->loveIcon);
				settings->loveIcon = strdup (val);
			} else if (streq ("ban_icon", key)) {
				free (settings->banIcon);
				settings->banIcon = strdup (val);
			} else if (streq ("tired_icon", key)) {
				free (settings->tiredIcon);
				settings->tiredIcon = strdup (val);
			} else if (streq ("at_icon", key)) {
				free (settings->atIcon);
				settings->atIcon = strdup (val);
			} else if (streq ("volume", key)) {
				settings->volume = atoi (val);
			} else if (streq ("system_volume_player_gain", key)) {
				settings->systemVolumePlayerGain = atoi (val);
			} else if (streq ("station_display_name_override", key)) {
				/* Parse /regex/replacement/ format */
				if (val[0] == '/') {
					const char *p = val + 1;
					const char *pattern_start = p;
					const char *pattern_end = strchr (p, '/');
					
					if (pattern_end) {
						const char *repl_start = pattern_end + 1;
						const char *repl_end = strchr (repl_start, '/');
						
						if (repl_end) {
							/* Valid format found */
							size_t idx = settings->stationDisplayNameOverrideCount;
							settings->stationDisplayNameOverrides = realloc (
								settings->stationDisplayNameOverrides,
								(idx + 1) * sizeof (BarStationDisplayNameOverride_t));
							
							/* Copy pattern */
							size_t pattern_len = pattern_end - pattern_start;
							settings->stationDisplayNameOverrides[idx].pattern = 
								strndup (pattern_start, pattern_len);
							if (!settings->stationDisplayNameOverrides[idx].pattern) {
								continue;  /* Skip this entry on allocation failure */
							}
							
							/* Copy replacement */
							size_t repl_len = repl_end - repl_start;
							settings->stationDisplayNameOverrides[idx].replacement = 
								strndup (repl_start, repl_len);
							if (!settings->stationDisplayNameOverrides[idx].replacement) {
								free (settings->stationDisplayNameOverrides[idx].pattern);
								continue;  /* Skip this entry on allocation failure */
							}
							
							/* Compile regex */
							int ret = regcomp (
								&settings->stationDisplayNameOverrides[idx].compiled,
								settings->stationDisplayNameOverrides[idx].pattern,
								REG_EXTENDED);
							
							settings->stationDisplayNameOverrides[idx].valid = (ret == 0);
							settings->stationDisplayNameOverrideCount++;
							
							if (ret != 0) {
								log_write(LOG_ERROR, "Warning: Invalid regex pattern: %s\n",
									settings->stationDisplayNameOverrides[idx].pattern);
							}
						}
					}
				}
			} else if (streq ("volume_mode", key)) {
				if (streq (val, "system")) {
					settings->volumeMode = BAR_VOLUME_MODE_SYSTEM;
				} else {
					settings->volumeMode = BAR_VOLUME_MODE_PLAYER;
				}
			} else if (streq ("audio_backend", key)) {
				if (streq (val, "auto")) {
					settings->audioBackend = BAR_AUDIO_BACKEND_AUTO;
				} else if (streq (val, "pulseaudio")) {
					settings->audioBackend = BAR_AUDIO_BACKEND_PULSEAUDIO;
				} else if (streq (val, "alsa")) {
					settings->audioBackend = BAR_AUDIO_BACKEND_ALSA;
				} else if (streq (val, "jack")) {
					settings->audioBackend = BAR_AUDIO_BACKEND_JACK;
				} else if (streq (val, "coreaudio")) {
					settings->audioBackend = BAR_AUDIO_BACKEND_COREAUDIO;
				} else if (streq (val, "wasapi")) {
					settings->audioBackend = BAR_AUDIO_BACKEND_WASAPI;
				} else {
					settings->audioBackend = BAR_AUDIO_BACKEND_AUTO;
				}
			} else if (streq ("gain_mul", key)) {
				settings->gainMul = atof (val);
			} else if (streq ("max_gain", key)) {
				settings->maxGain = atoi (val);
			} else if (streq ("format_nowplaying_song", key)) {
				free (settings->npSongFormat);
				settings->npSongFormat = strdup (val);
			} else if (streq ("format_nowplaying_station", key)) {
				free (settings->npStationFormat);
				settings->npStationFormat = strdup (val);
			} else if (streq ("format_list_song", key)) {
				free (settings->listSongFormat);
				settings->listSongFormat = strdup (val);
			} else if (streq ("format_time", key)) {
				free (settings->timeFormat);
				settings->timeFormat = strdup (val);
			} else if (streq ("fifo", key)) {
				free (settings->fifo);
				settings->fifo = BarSettingsExpandTilde (val, userhome);
			} else if (streq ("audio_pipe", key)) {
				free (settings->audioPipe);
				settings->audioPipe = BarSettingsExpandTilde (val, userhome);
			} else if (streq ("autoselect", key)) {
				settings->autoselect = atoi (val);
			} else if (streq ("sample_rate", key)) {
				settings->sampleRate = atoi (val);
			} else if (strncmp (formatMsgPrefix, key,
					strlen (formatMsgPrefix)) == 0) {
				static const char *mapping[] = {"none", "info", "nowplaying",
						"time", "err", "question", "list"};
				const char *typeStart = key + strlen (formatMsgPrefix);
				for (size_t i = 0; i < sizeof (mapping) / sizeof (*mapping); i++) {
					if (streq (typeStart, mapping[i])) {
						const char *formatPos = strstr (val, "%s");
						
						/* keep default if there is no format character */
						if (formatPos != NULL) {
							BarMsgFormatStr_t *format = &settings->msgFormat[i];

							free (format->prefix);
							free (format->postfix);

							const size_t prefixLen = formatPos - val;
							format->prefix = calloc (prefixLen + 1,
									sizeof (*format->prefix));
							memcpy (format->prefix, val, prefixLen);

							const size_t postfixLen = strlen (val) -
									(formatPos-val) - 2;
							format->postfix = calloc (postfixLen + 1,
									sizeof (*format->postfix));
							memcpy (format->postfix, formatPos+2, postfixLen);
						}
						break;
					}
				}
		#ifdef WEBSOCKET_ENABLED
		} else if (streq ("ui_mode", key)) {
			if (streq (val, "cli")) {
				settings->uiMode = BAR_UI_MODE_CLI;
			} else if (streq (val, "web")) {
				settings->uiMode = BAR_UI_MODE_WEB;
			} else if (streq (val, "both")) {
				settings->uiMode = BAR_UI_MODE_BOTH;
			} else {
				/* Invalid value defaults to 'both' */
				settings->uiMode = BAR_UI_MODE_BOTH;
			}
		} else if (streq ("websocket_port", key)) {
				settings->websocketPort = atoi (val);
			} else if (streq ("websocket_host", key)) {
				free (settings->websocketHost);
				settings->websocketHost = strdup (val);
			} else if (streq ("webui_path", key)) {
				free (settings->webuiPath);
				settings->webuiPath = strdup (val);
			} else if (streq ("pid_file", key)) {
				free (settings->pidFile);
				settings->pidFile = BarSettingsExpandTilde (val, userhome);
			} else if (streq ("log_file", key)) {
				free (settings->logFile);
				settings->logFile = BarSettingsExpandTilde (val, userhome);
			#endif
		} else if (streq ("alsa_mixer", key)) {
			free (settings->alsaMixer);
			settings->alsaMixer = strdup (val);
		} else if (streq ("account", key)) {
			/* account = id:path */
			char *colon = strchr (val, ':');
			if (colon && colon != val && *(colon + 1) != '\0') {
				*colon = '\0';
				BarAccountLine_t *newLines = realloc (accountLines,
						(accountLineCount + 1) * sizeof (BarAccountLine_t));
				if (newLines == NULL) {
					BarUiMsg (settings, MSG_ERR,
							"Out of memory reading account lines at %s:%zu\n",
							path, lineNum);
				} else {
					accountLines = newLines;
					accountLines[accountLineCount].id = strdup (val);
					accountLines[accountLineCount].path = strdup (colon + 1);
					accountLineCount++;
				}
			} else {
				BarUiMsg (settings, MSG_INFO,
						"Invalid account format at %s:%zu (expected id:path)\n",
						path, lineNum);
			}
		} else if (streq ("main_account_id", key)) {
			free (mainAccountId);
			mainAccountId = strdup (val);
		} else if (streq ("default_account", key)) {
			free (defaultAccount);
			defaultAccount = strdup (val);
		} else if (streq ("account_label", key)) {
			free (accountLabel);
			accountLabel = strdup (val);
		} else {
				BarUiMsg (settings, MSG_INFO,
						"Unrecognized key %s at %s:%zu\n", key, path, lineNum);
			}
		}

		/* Remember the config file's directory for resolving relative account paths.
		 * Always update so we use the last file processed (config, not state). */
		free (configDir);
		configDir = strdup (path);
		char *lastSlash = strrchr (configDir, '/');
		if (lastSlash) *lastSlash = '\0';

		fclose (configfd);
		free (path);
	}

	/* Build account list from parsed config + account files */
	if (accountLineCount > 0 || settings->username != NULL) {
		BarSettingsBuildAccounts (settings, mainAccountId, defaultAccount,
				accountLabel, accountLines, accountLineCount,
				configDir ? configDir : ".", userhome);
	}

	/* Free temporary account parsing state */
	for (size_t i = 0; i < accountLineCount; i++) {
		free (accountLines[i].id);
		free (accountLines[i].path);
	}
	free (accountLines);
	free (mainAccountId);
	free (defaultAccount);
	free (accountLabel);
	free (configDir);

	/* check environment variable if proxy is not set explicitly */
	if (settings->proxy == NULL) {
		char *tmpProxy = getenv ("http_proxy");
		if (tmpProxy != NULL && strlen (tmpProxy) > 0) {
			settings->proxy = strdup (tmpProxy);
		}
	}

	/* ffmpeg does not support setting an http proxy explicitly */
	if (settings->proxy != NULL) {
		setenv ("http_proxy", settings->proxy, 1);
	}

	free (userhome);
}

/*	write statefile
 */
void BarSettingsWrite (PianoStation_t *station, BarSettings_t *settings) {
	FILE *fd;

	assert (settings != NULL);

	char * const path = BarGetXdgConfigDir (PACKAGE "/state");
	assert (path != NULL);
	if ((fd = fopen (path, "w")) == NULL) {
		free (path);
		return;
	}

	fputs ("# do not edit this file\n", fd);
	/* Don't save volume in system mode - it's controlled by the OS */
	if (settings->volumeMode != BAR_VOLUME_MODE_SYSTEM) {
		fprintf (fd, "volume = %i\n", settings->volume);
	}
	if (station != NULL) {
		fprintf (fd, "autostart_station = %s\n", station->id);
	}

	fclose (fd);
	free (path);
}

/*	Get the active account (or NULL if no accounts configured)
 */
const BarAccount_t *BarSettingsGetActiveAccount (const BarSettings_t *settings) {
	if (settings->accounts == NULL || settings->accountCount == 0) {
		return NULL;
	}
	if (settings->activeAccountIndex >= settings->accountCount) {
		return &settings->accounts[0];
	}
	return &settings->accounts[settings->activeAccountIndex];
}

/*	Set active account by id string.
 *	@return true if found and set, false if id not found
 */
bool BarSettingsSetActiveAccountById (BarSettings_t *settings, const char *id) {
	if (settings->accounts == NULL || id == NULL) {
		return false;
	}
	for (size_t i = 0; i < settings->accountCount; i++) {
		if (strcmp (settings->accounts[i].id, id) == 0) {
			settings->activeAccountIndex = i;
			return true;
		}
	}
	return false;
}
