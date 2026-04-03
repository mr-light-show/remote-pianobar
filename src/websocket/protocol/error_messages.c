/*
Copyright (c) 2024
	Kyle Hawes

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, publish, distribute, sublicense, and/or sell
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

#include "error_messages.h"
#include "../../l10n.h"
#include <string.h>

static const char *bar_ws_op_fallback_key (const char *operation) {
	if (strcmp (operation, "station.rename") == 0) {
		return "error.ws.op.station_rename";
	}
	if (strcmp (operation, "station.addMusic") == 0) {
		return "error.ws.op.station_add_music";
	}
	if (strcmp (operation, "station.addGenre") == 0) {
		return "error.ws.op.station_add_genre";
	}
	if (strcmp (operation, "station.addShared") == 0) {
		return "error.ws.op.station_add_shared";
	}
	if (strcmp (operation, "station.getGenres") == 0) {
		return "error.ws.op.station_get_genres";
	}
	if (strcmp (operation, "station.getStationModes") == 0) {
		return "error.ws.op.station_get_station_modes";
	}
	if (strcmp (operation, "station.setStationMode") == 0) {
		return "error.ws.op.station_set_station_mode";
	}
	if (strcmp (operation, "station.getStationInfo") == 0) {
		return "error.ws.op.station_get_station_info";
	}
	if (strcmp (operation, "station.deleteSeed") == 0) {
		return "error.ws.op.station_delete_seed";
	}
	if (strcmp (operation, "station.deleteFeedback") == 0) {
		return "error.ws.op.station_delete_feedback";
	}
	if (strcmp (operation, "music.search") == 0) {
		return "error.ws.op.music_search";
	}
	if (strcmp (operation, "station.setQuickMix") == 0) {
		return "error.ws.op.station_set_quick_mix";
	}
	if (strcmp (operation, "station.delete") == 0) {
		return "error.ws.op.station_delete";
	}
	if (strcmp (operation, "station.createFrom") == 0) {
		return "error.ws.op.station_create_from";
	}
	return NULL;
}

const char* BarWsGetFriendlyErrorMessage(const BarL10nContext_t *l10n,
		const char *operation, const char *originalError) {
	if (!originalError) {
		return BarL10nGet (l10n, "error.ws.unknown");
	}

	if (strstr (originalError, "Call not allowed") != NULL ||
	    strstr (originalError, "not allowed") != NULL) {

		if (strcmp (operation, "station.rename") == 0) {
			return BarL10nGet (l10n, "error.ws.not_allowed.station_rename");
		}
		if (strcmp (operation, "station.addMusic") == 0) {
			return BarL10nGet (l10n, "error.ws.not_allowed.station_add_music");
		}
		if (strcmp (operation, "station.deleteSeed") == 0) {
			return BarL10nGet (l10n, "error.ws.not_allowed.station_delete_seed");
		}
		if (strcmp (operation, "station.deleteFeedback") == 0) {
			return BarL10nGet (l10n, "error.ws.not_allowed.station_delete_feedback");
		}
		if (strcmp (operation, "station.setStationMode") == 0) {
			return BarL10nGet (l10n, "error.ws.not_allowed.station_set_station_mode");
		}
		return BarL10nGet (l10n, "error.ws.not_allowed.generic");
	}

	if (strstr (originalError, "Network error") != NULL ||
	    strstr (originalError, "network") != NULL ||
	    strstr (originalError, "Could not resolve host") != NULL ||
	    strstr (originalError, "Timeout") != NULL) {
		return BarL10nGet (l10n, "error.ws.network");
	}

	if (strstr (originalError, "auth") != NULL ||
	    strstr (originalError, "login") != NULL ||
	    strstr (originalError, "Invalid") != NULL) {
		return BarL10nGet (l10n, "error.ws.auth");
	}

	if (strstr (originalError, "interrupted") != NULL ||
	    strstr (originalError, "Interrupted") != NULL) {
		return BarL10nGet (l10n, "error.ws.interrupted");
	}

	if (strstr (originalError, "Failed to transform") != NULL) {
		if (strcmp (operation, "station.addMusic") == 0) {
			return BarL10nGet (l10n, "web.station_add_music_transform");
		}
		if (strcmp (operation, "station.rename") == 0) {
			return BarL10nGet (l10n, "web.station_rename_transform");
		}
	}

	if (strcmp (operation, "app.pandora-reconnect") == 0) {
		if (strstr (originalError, "Unknown account") != NULL) {
			return BarL10nGet (l10n, "web.pandora_reconnect_unknown_account");
		}
		if (strstr (originalError, "No credentials") != NULL) {
			return BarL10nGet (l10n, "web.pandora_reconnect_no_credentials");
		}
		if (strstr (originalError, "Last station was deleted") != NULL) {
			return BarL10nGet (l10n, "web.pandora_reconnect_station_deleted");
		}
	}

	if (strcmp (operation, "song.explain") == 0) {
		if (strstr (originalError, "Failed to receive") != NULL) {
			return BarL10nGet (l10n, "web.song_explain_failed");
		}
		if (strstr (originalError, "No explanation") != NULL) {
			return BarL10nGet (l10n, "web.song_explain_empty");
		}
	}

	if (strcmp (operation, "query.upcoming") == 0 &&
	    strstr (originalError, "No songs") != NULL) {
		return BarL10nGet (l10n, "web.query_upcoming_empty");
	}

	if (strcmp (operation, "station.change") == 0 &&
	    strstr (originalError, "Station not found") != NULL) {
		return BarL10nGet (l10n, "web.station_change_not_found");
	}

	if (strcmp (originalError, "No song is currently playing") == 0) {
		return BarL10nGet (l10n, "web.socket.no_song_playing");
	}
	if (strcmp (originalError, "No station is selected") == 0) {
		return BarL10nGet (l10n, "web.socket.no_station_selected");
	}
	if (strcmp (originalError, "Not connected to Pandora") == 0) {
		return BarL10nGet (l10n, "web.socket.not_connected");
	}
	if (strcmp (originalError, "Action cannot be performed in current context") == 0) {
		return BarL10nGet (l10n, "web.socket.action_context");
	}

	const char *fk = bar_ws_op_fallback_key (operation);
	if (fk) {
		return BarL10nGet (l10n, fk);
	}

	return originalError;
}
