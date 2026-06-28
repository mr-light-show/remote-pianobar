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
#include "parse_utils.h"

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
		if (concat == NULL) {
			return NULL;
		}
		snprintf (concat, len, "%s/%s", xdgConfigDir, filename);
		return concat;
	}

	return NULL;
}

/*	Expand ~/ to user’s home directory
 */
char *BarSettingsExpandTilde (const char * const path, const char * const home) {
	assert (path != NULL);

	if (home != NULL && strncmp (path, "~/", 2) == 0) {
		const size_t expandedLen = strlen (home) + 1 + strlen (path) - 2 + 1;
		char * const expanded = malloc (expandedLen * sizeof (*expanded));
		if (expanded == NULL) {
			return strdup (path);
		}
		snprintf (expanded, expandedLen, "%s/%s", home, &path[2]);
		return expanded;
	}

	return strdup (path);
}

/*
 * ============================================================================
 * Key descriptor table — table-driven dispatch for BarSettingsRead
 * ============================================================================
 */

typedef enum {
	CFG_STR,    /* free(*field); *field = strdup(val) */
	CFG_TILDE,  /* free(*field); *field = BarSettingsExpandTilde(val, home) */
	CFG_INT,    /* BarParseIntInRange(val, min, max, (int*)field) */
	CFG_UINT,   /* BarParseIntInRange with unsigned cast */
	CFG_FLOAT,  /* *(float*)field = (float)atof(val) */
	CFG_CUSTOM, /* k->custom(settings, val, home) */
} BarConfigKeyKind_t;

typedef struct {
	const char         *key;
	BarConfigKeyKind_t  kind;
	size_t              offset; /* offsetof into BarSettings_t; 0 for CFG_CUSTOM */
	int                 min, max; /* for CFG_INT / CFG_UINT */
	void (*custom) (BarSettings_t *, const char *val, const char *home);
} BarConfigKey_t;

/* Custom parsers for keys with enum/complex value dispatch */
static void cfgAudioQuality (BarSettings_t *s, const char *v, const char *h) {
	(void)h;
	if (streq (v, "low")) { s->audioQuality = PIANO_AQ_LOW; }
	else if (streq (v, "medium")) { s->audioQuality = PIANO_AQ_MEDIUM; }
	else { s->audioQuality = PIANO_AQ_HIGH; }
}
static void cfgSortOrder (BarSettings_t *s, const char *v, const char *h) {
	(void)h;
	static const char *mapping[] = {
		"name_az", "name_za",
		"quickmix_01_name_az", "quickmix_01_name_za",
		"quickmix_10_name_az", "quickmix_10_name_za",
	};
	for (size_t i = 0; i < BAR_SORT_COUNT; i++) {
		if (streq (mapping[i], v)) { s->sortOrder = i; break; }
	}
}
static void cfgVolumeMode (BarSettings_t *s, const char *v, const char *h) {
	(void)h;
	s->volumeMode = streq (v, "system") ? BAR_VOLUME_MODE_SYSTEM : BAR_VOLUME_MODE_PLAYER;
}
static void cfgAudioBackend (BarSettings_t *s, const char *v, const char *h) {
	(void)h;
	if (streq (v, "pulseaudio")) { s->audioBackend = BAR_AUDIO_BACKEND_PULSEAUDIO; }
	else if (streq (v, "alsa"))  { s->audioBackend = BAR_AUDIO_BACKEND_ALSA; }
	else if (streq (v, "jack"))  { s->audioBackend = BAR_AUDIO_BACKEND_JACK; }
	else if (streq (v, "coreaudio")) { s->audioBackend = BAR_AUDIO_BACKEND_COREAUDIO; }
	else if (streq (v, "wasapi")) { s->audioBackend = BAR_AUDIO_BACKEND_WASAPI; }
	else { s->audioBackend = BAR_AUDIO_BACKEND_AUTO; }
}
static void cfgAutoselect (BarSettings_t *s, const char *v, const char *h) {
	(void)h;
	int tmp = 0;
	if (!BarParseIntInRange (v, 0, INT_MAX, &tmp)) {
		log_write (LOG_ERROR, "settings: invalid value for autoselect: \"%s\", ignoring", v);
	} else { s->autoselect = (tmp != 0); }
}
static void cfgStationDisplayName (BarSettings_t *s, const char *v, const char *h) {
	(void)h;
	if (v[0] != '/') { return; }
	const char *p = v + 1;
	const char *pattern_end = strchr (p, '/');
	if (!pattern_end) { return; }
	const char *repl_start = pattern_end + 1;
	const char *repl_end = strchr (repl_start, '/');
	if (!repl_end) { return; }

	size_t idx = s->stationDisplayNameOverrideCount;
	BarStationDisplayNameOverride_t *newEntries = realloc (
		s->stationDisplayNameOverrides,
		(idx + 1) * sizeof (BarStationDisplayNameOverride_t));
	if (!newEntries) { return; }
	s->stationDisplayNameOverrides = newEntries;

	BarStationDisplayNameOverride_t *entry = &s->stationDisplayNameOverrides[idx];
	entry->pattern = strndup (p, (size_t)(pattern_end - p));
	if (!entry->pattern) { return; }
	entry->replacement = strndup (repl_start, (size_t)(repl_end - repl_start));
	if (!entry->replacement) { free (entry->pattern); entry->pattern = NULL; return; }

	int ret = regcomp (&entry->compiled, entry->pattern, REG_EXTENDED);
	entry->valid = (ret == 0);
	s->stationDisplayNameOverrideCount++;
	if (ret != 0) {
		log_write (LOG_ERROR, "settings: invalid regex pattern: %s\n", entry->pattern);
	}
}

#ifdef WEBSOCKET_ENABLED
static void cfgUiMode (BarSettings_t *s, const char *v, const char *h) {
	(void)h;
	if (streq (v, "cli")) { s->uiMode = BAR_UI_MODE_CLI; }
	else if (streq (v, "web")) { s->uiMode = BAR_UI_MODE_WEB; }
	else { s->uiMode = BAR_UI_MODE_BOTH; }
}
#endif

/* Apply a single key entry to settings */
static void applyConfigKey (BarSettings_t *settings, const BarConfigKey_t *k,
		const char *val, const char *userhome) {
	switch (k->kind) {
		case CFG_STR: {
			char **f = (char **) ((char *) settings + k->offset);
			free (*f); *f = strdup (val);
			break;
		}
		case CFG_TILDE: {
			char **f = (char **) ((char *) settings + k->offset);
			free (*f); *f = BarSettingsExpandTilde (val, userhome);
			break;
		}
		case CFG_INT: {
			int *f = (int *) ((char *) settings + k->offset);
			if (!BarParseIntInRange (val, k->min, k->max, f)) {
				log_write (LOG_ERROR, "settings: invalid value for %s: \"%s\", ignoring",
				           k->key, val);
			}
			break;
		}
		case CFG_UINT: {
			unsigned int *f = (unsigned int *) ((char *) settings + k->offset);
			int tmp = (int) *f;
			if (!BarParseIntInRange (val, k->min, k->max, &tmp)) {
				log_write (LOG_ERROR, "settings: invalid value for %s: \"%s\", ignoring",
				           k->key, val);
			} else { *f = (unsigned int) tmp; }
			break;
		}
		case CFG_FLOAT: {
			float *f = (float *) ((char *) settings + k->offset);
			*f = (float) atof (val);
			break;
		}
		case CFG_CUSTOM:
			if (k->custom) { k->custom (settings, val, userhome); }
			break;
	}
}

/* Common (non-websocket) key table */
static const BarConfigKey_t commonKeys[] = {
	/* simple string fields */
	{"control_proxy",      CFG_STR,    offsetof (BarSettings_t, controlProxy),      0, 0, NULL},
	{"proxy",              CFG_STR,    offsetof (BarSettings_t, proxy),              0, 0, NULL},
	{"bind_to",            CFG_STR,    offsetof (BarSettings_t, bindTo),             0, 0, NULL},
	{"user",               CFG_STR,    offsetof (BarSettings_t, username),           0, 0, NULL},
	{"password",           CFG_STR,    offsetof (BarSettings_t, password),           0, 0, NULL},
	{"password_command",   CFG_STR,    offsetof (BarSettings_t, passwordCmd),        0, 0, NULL},
	{"rpc_host",           CFG_STR,    offsetof (BarSettings_t, rpcHost),            0, 0, NULL},
	{"rpc_tls_port",       CFG_STR,    offsetof (BarSettings_t, rpcTlsPort),         0, 0, NULL},
	{"partner_user",       CFG_STR,    offsetof (BarSettings_t, partnerUser),        0, 0, NULL},
	{"partner_password",   CFG_STR,    offsetof (BarSettings_t, partnerPassword),    0, 0, NULL},
	{"device",             CFG_STR,    offsetof (BarSettings_t, device),             0, 0, NULL},
	{"encrypt_password",   CFG_STR,    offsetof (BarSettings_t, outkey),             0, 0, NULL},
	{"decrypt_password",   CFG_STR,    offsetof (BarSettings_t, inkey),              0, 0, NULL},
	{"ca_bundle",          CFG_STR,    offsetof (BarSettings_t, caBundle),           0, 0, NULL},
	{"autostart_station",  CFG_STR,    offsetof (BarSettings_t, autostartStation),   0, 0, NULL},
	{"love_icon",          CFG_STR,    offsetof (BarSettings_t, loveIcon),           0, 0, NULL},
	{"ban_icon",           CFG_STR,    offsetof (BarSettings_t, banIcon),            0, 0, NULL},
	{"tired_icon",         CFG_STR,    offsetof (BarSettings_t, tiredIcon),          0, 0, NULL},
	{"at_icon",            CFG_STR,    offsetof (BarSettings_t, atIcon),             0, 0, NULL},
	{"format_nowplaying_song",     CFG_STR, offsetof (BarSettings_t, npSongFormat),     0, 0, NULL},
	{"format_nowplaying_station",  CFG_STR, offsetof (BarSettings_t, npStationFormat),  0, 0, NULL},
	{"format_list_song",           CFG_STR, offsetof (BarSettings_t, listSongFormat),   0, 0, NULL},
	{"format_time",                CFG_STR, offsetof (BarSettings_t, timeFormat),        0, 0, NULL},
	{"locale",             CFG_STR,    offsetof (BarSettings_t, locale),             0, 0, NULL},
	{"alsa_mixer",         CFG_STR,    offsetof (BarSettings_t, alsaMixer),          0, 0, NULL},
	/* tilde-expanding paths */
	{"event_command",      CFG_TILDE,  offsetof (BarSettings_t, eventCmd),           0, 0, NULL},
	{"fifo",               CFG_TILDE,  offsetof (BarSettings_t, fifo),               0, 0, NULL},
	{"audio_pipe",         CFG_TILDE,  offsetof (BarSettings_t, audioPipe),          0, 0, NULL},
	/* integer range fields */
	{"history",            CFG_UINT,   offsetof (BarSettings_t, history),            0, 10000, NULL},
	{"max_retry",          CFG_UINT,   offsetof (BarSettings_t, maxRetry),           0, 100, NULL},
	{"timeout",            CFG_UINT,   offsetof (BarSettings_t, timeout),            1, 600, NULL},
	{"pause_timeout",      CFG_UINT,   offsetof (BarSettings_t, pauseTimeout),       0, 86400, NULL},
	{"buffer_seconds",     CFG_UINT,   offsetof (BarSettings_t, bufferSecs),         1, 300, NULL},
	{"volume",             CFG_INT,    offsetof (BarSettings_t, volume),             0, 100, NULL},
	{"system_volume_player_gain", CFG_INT, offsetof (BarSettings_t, systemVolumePlayerGain), -60, 60, NULL},
	{"max_gain",           CFG_INT,    offsetof (BarSettings_t, maxGain),            0, 100, NULL},
	{"sample_rate",        CFG_INT,    offsetof (BarSettings_t, sampleRate),         8000, 192000, NULL},
	/* float field */
	{"gain_mul",           CFG_FLOAT,  offsetof (BarSettings_t, gainMul),            0, 0, NULL},
	/* custom enum/complex parsers */
	{"audio_quality",               CFG_CUSTOM, 0, 0, 0, cfgAudioQuality},
	{"sort",                        CFG_CUSTOM, 0, 0, 0, cfgSortOrder},
	{"volume_mode",                 CFG_CUSTOM, 0, 0, 0, cfgVolumeMode},
	{"audio_backend",               CFG_CUSTOM, 0, 0, 0, cfgAudioBackend},
	{"autoselect",                  CFG_CUSTOM, 0, 0, 0, cfgAutoselect},
	{"station_display_name_override", CFG_CUSTOM, 0, 0, 0, cfgStationDisplayName},
	{NULL, CFG_STR, 0, 0, 0, NULL}  /* sentinel */
};

#ifdef WEBSOCKET_ENABLED
static const BarConfigKey_t wsKeys[] = {
	{"ui_mode",       CFG_CUSTOM, 0, 0, 0, cfgUiMode},
	{"websocket_port",CFG_INT,    offsetof (BarSettings_t, websocketPort), 1, 65535, NULL},
	{"websocket_host",CFG_STR,    offsetof (BarSettings_t, websocketHost), 0, 0, NULL},
	{"webui_path",    CFG_STR,    offsetof (BarSettings_t, webuiPath),     0, 0, NULL},
	{"pid_file",      CFG_TILDE,  offsetof (BarSettings_t, pidFile),       0, 0, NULL},
	{"log_file",      CFG_TILDE,  offsetof (BarSettings_t, logFile),       0, 0, NULL},
	{NULL, CFG_STR, 0, 0, 0, NULL}
};
#endif

/* Dispatch a single key=val pair through the descriptor tables.
 * Returns true if the key was recognised and handled. */
static bool dispatchConfigKey (BarSettings_t *settings, const char *key,
		const char *val, const char *userhome) {
	for (const BarConfigKey_t *k = commonKeys; k->key != NULL; k++) {
		if (streq (k->key, key)) {
			applyConfigKey (settings, k, val, userhome);
			return true;
		}
	}
#ifdef WEBSOCKET_ENABLED
	for (const BarConfigKey_t *k = wsKeys; k->key != NULL; k++) {
		if (streq (k->key, key)) {
			applyConfigKey (settings, k, val, userhome);
			return true;
		}
	}
#endif
	return false;
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
	free (settings->locale);

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

static bool g_testAppendReallocFail = false;
static size_t g_testAppendReallocFailWhenCount = SIZE_MAX;
static bool g_testResolveAccountPathFail = false;

void BarSettingsTestSetAppendReallocFailWhenCount (size_t accountCount) {
	g_testAppendReallocFail = true;
	g_testAppendReallocFailWhenCount = accountCount;
}

void BarSettingsTestSetResolveAccountPathFail (bool fail) {
	g_testResolveAccountPathFail = fail;
}

void BarSettingsTestResetAccountHooks (void) {
	g_testAppendReallocFail = false;
	g_testAppendReallocFailWhenCount = SIZE_MAX;
	g_testResolveAccountPathFail = false;
}

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
	if (resolved == NULL) {
		return NULL;
	}
	snprintf (resolved, len, "%s/%s", configDir, path);
	return resolved;
}

/*	Read credentials and per-account keys from a file, populating an account entry.
 *	Only credential-related and per-account keys are read (user, password,
 *	password_command, account_label, autostart_station).
 *	@return true if the file was opened and read, false if missing
 */
static bool BarSettingsReadAccountFile (BarAccount_t *acct, const char *filepath) {
	FILE *fd = fopen (filepath, "r");
	if (fd == NULL) {
		log_write (LOG_ERROR,
				"Error: Account file not found: %s\n", filepath);
		return false;
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
	return true;
}

static bool BarAccountUsernamesEqual (const BarAccount_t *a,
		const BarAccount_t *b) {
	if (a == NULL || b == NULL) {
		return false;
	}
	if (a->username == NULL || b->username == NULL) {
		return false;
	}
	return strcmp (a->username, b->username) == 0;
}

bool BarSettingsAccountUsernamesEqualForTest (const BarAccount_t *a,
		const BarAccount_t *b) {
	return BarAccountUsernamesEqual (a, b);
}

static const BarAccount_t *BarSettingsFindDuplicateAccount (
		const BarSettings_t *settings, const BarAccount_t *candidate) {
	for (size_t i = 0; i < settings->accountCount; i++) {
		if (BarAccountUsernamesEqual (candidate, &settings->accounts[i])) {
			return &settings->accounts[i];
		}
	}
	return NULL;
}

static bool BarSettingsAppendAccount (BarSettings_t *settings, BarAccount_t *acct) {
	BarAccount_t *newAccounts;
	if (g_testAppendReallocFail
			&& settings->accountCount == g_testAppendReallocFailWhenCount) {
		newAccounts = NULL;
	} else {
		newAccounts = realloc (settings->accounts,
				(settings->accountCount + 1) * sizeof (BarAccount_t));
	}
	if (newAccounts == NULL) {
		log_write (LOG_ERROR, "Out of memory building account list\n");
		return false;
	}
	settings->accounts = newAccounts;
	settings->accounts[settings->accountCount] = *acct;
	settings->accountCount++;
	memset (acct, 0, sizeof (*acct));
	return true;
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
	settings->accounts = NULL;
	settings->accountCount = 0;

	bool hasMainCredentials = (settings->username != NULL);

	if (hasMainCredentials) {
		BarAccount_t primary = {0};
		primary.id = strdup (mainAccountId ? mainAccountId : "default");
		primary.label = strdup (accountLabel ? accountLabel :
				(mainAccountId ? mainAccountId : "default"));
		primary.username = strdup (settings->username);
		if (settings->password) {
			primary.password = strdup (settings->password);
		}
		if (settings->passwordCmd) {
			primary.passwordCmd = strdup (settings->passwordCmd);
		}
		if (settings->autostartStation) {
			primary.autostartStation = strdup (settings->autostartStation);
		}
		if (!BarSettingsAppendAccount (settings, &primary)) {
			BarAccountDestroy (&primary);
		}
	}

	for (size_t i = 0; i < accountLineCount; i++) {
		BarAccount_t acct = {0};
		acct.id = strdup (accountLines[i].id);
		acct.label = strdup (accountLines[i].id);

		if (settings->username) {
			acct.username = strdup (settings->username);
		}
		if (settings->password) {
			acct.password = strdup (settings->password);
		}
		if (settings->passwordCmd) {
			acct.passwordCmd = strdup (settings->passwordCmd);
		}

		char *resolved = NULL;
		if (g_testResolveAccountPathFail) {
			resolved = NULL;
		} else {
			resolved = BarSettingsResolveAccountPath (accountLines[i].path,
					configDir, userhome);
		}
		if (resolved == NULL) {
			log_write (LOG_ERROR,
					"Error: Out of memory resolving account file for '%s'\n",
					accountLines[i].id);
			BarAccountDestroy (&acct);
			continue;
		}

		if (!BarSettingsReadAccountFile (&acct, resolved)) {
			log_write (LOG_ERROR,
					"Error: Account '%s' not loaded (missing file: %s)\n",
					accountLines[i].id, resolved);
			BarAccountDestroy (&acct);
			free (resolved);
			continue;
		}
		free (resolved);

		const BarAccount_t *dup = BarSettingsFindDuplicateAccount (settings, &acct);
		if (dup != NULL) {
			log_write (LOG_ERROR,
					"Error: Account '%s' has the same username as account '%s' "
					"(not loaded)\n",
					accountLines[i].id,
					dup->id ? dup->id : "(unknown)");
			BarAccountDestroy (&acct);
			continue;
		}

		if (!BarSettingsAppendAccount (settings, &acct)) {
			BarAccountDestroy (&acct);
		}
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
	const size_t defaultxdgLen = strlen (userhome) + strlen ("/.config") + 1;
	char * const defaultxdg = malloc (defaultxdgLen);
	if (defaultxdg == NULL) {
		log_write (LOG_ERROR, "settings: out of memory for XDG config path");
		return;
	}
	snprintf (defaultxdg, defaultxdgLen, "%s/.config", userhome);
	setenv ("XDG_CONFIG_HOME", defaultxdg, 0);
	free (defaultxdg);

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

			/* act_* keyboard shortcuts (prefix-based, not exact match) */
			if (memcmp ("act_", key, 4) == 0) {
				for (size_t i = 0; i < BAR_KS_COUNT; i++) {
					if (streq (dispatchActions[i].configKey, key)) {
						settings->keys[i] = streq (val, "disabled") ?
						                    BAR_KS_DISABLED : val[0];
						break;
					}
				}
			/* format_msg_* (prefix-based) */
			} else if (strncmp (formatMsgPrefix, key, strlen (formatMsgPrefix)) == 0) {
				static const char *fmtNames[] = {"none", "info", "nowplaying",
						"time", "err", "question", "list"};
				const char *typeStart = key + strlen (formatMsgPrefix);
				for (size_t i = 0; i < sizeof (fmtNames) / sizeof (*fmtNames); i++) {
					if (!streq (typeStart, fmtNames[i])) { continue; }
					const char *formatPos = strstr (val, "%s");
					if (formatPos != NULL) {
						BarMsgFormatStr_t *fmt = &settings->msgFormat[i];
						free (fmt->prefix); free (fmt->postfix);
						const size_t plen = (size_t)(formatPos - val);
						fmt->prefix = calloc (plen + 1, 1);
						if (fmt->prefix) { memcpy (fmt->prefix, val, plen); }
						const size_t slen = strlen (val) - plen - 2;
						fmt->postfix = calloc (slen + 1, 1);
						if (fmt->postfix) { memcpy (fmt->postfix, formatPos + 2, slen); }
					}
					break;
				}
			/* account (uses local parsing state, not BarSettings_t fields) */
			} else if (streq ("account", key)) {
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
				free (mainAccountId); mainAccountId = strdup (val);
			} else if (streq ("default_account", key)) {
				free (defaultAccount); defaultAccount = strdup (val);
			} else if (streq ("account_label", key)) {
				free (accountLabel); accountLabel = strdup (val);
			/* table-driven dispatch for all remaining keys */
			} else if (!dispatchConfigKey (settings, key, val, userhome)) {
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
