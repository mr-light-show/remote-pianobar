/*
Copyright (c) 2025
	Kyle Hawes

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

/* Station display name transformation */

#include "station_display.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* Trim leading and trailing whitespace from a string in-place
 * Returns the new start position
 */
static char* trimWhitespace(char *str) {
	if (!str) {
		return NULL;
	}
	
	/* Trim leading whitespace */
	while (isspace((unsigned char)*str)) {
		str++;
	}
	
	if (*str == '\0') {
		return str;  /* All spaces */
	}
	
	/* Trim trailing whitespace */
	char *end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end)) {
		end--;
	}
	end[1] = '\0';
	
	return str;
}

/* Apply regex replacement to a string
 * Returns newly allocated string or NULL if no match/error
 */
static char* applyRegexReplacement(const char *input, regex_t *regex, const char *replacement) {
	regmatch_t match;
	
	if (regexec(regex, input, 1, &match, 0) != 0) {
		return NULL;  /* No match */
	}
	
	/* Calculate result size */
	size_t prefix_len = match.rm_so;
	size_t suffix_len = strlen(input) - match.rm_eo;
	size_t repl_len = strlen(replacement);
	size_t result_len = prefix_len + repl_len + suffix_len;
	
	char *result = malloc(result_len + 1);
	if (!result) {
		return NULL;
	}
	
	/* Build result: prefix + replacement + suffix */
	memcpy(result, input, prefix_len);
	memcpy(result + prefix_len, replacement, repl_len);
	memcpy(result + prefix_len + repl_len, input + match.rm_eo, suffix_len);
	result[result_len] = '\0';
	
	/* Trim whitespace from result */
	char *trimmed = trimWhitespace(result);
	if (trimmed != result) {
		/* Need to move string to beginning */
		memmove(result, trimmed, strlen(trimmed) + 1);
	}
	
	return result;
}

/* Apply all configured overrides to a station name
 * Returns newly allocated string or NULL if no changes
 */
char* BarApplyStationNameOverrides(const BarSettings_t *settings, const char *originalName) {
	if (!settings->stationDisplayNameOverrides || !originalName) {
		return NULL;
	}
	
	char *current = strdup(originalName);
	if (!current) {
		return NULL;
	}
	
	bool changed = false;
	
	/* Apply each override in order */
	for (size_t i = 0; i < settings->stationDisplayNameOverrideCount; i++) {
		if (!settings->stationDisplayNameOverrides[i].valid) {
			continue;  /* Skip invalid regex */
		}
		
		char *transformed = applyRegexReplacement(
			current,
			&settings->stationDisplayNameOverrides[i].compiled,
			settings->stationDisplayNameOverrides[i].replacement);
		
		if (transformed) {
			free(current);
			current = transformed;
			changed = true;
		}
	}
	
	if (!changed) {
		free(current);
		return NULL;
	}
	
	return current;
}

/* Update displayName for all stations in the list */
void BarUpdateStationDisplayNames(BarApp_t *app) {
	if (!app || !app->ph.stations) {
		return;
	}
	
	PianoStation_t *station = app->ph.stations;
	while (station) {
		/* Free old displayName */
		free(station->displayName);
		station->displayName = NULL;
		
		/* Compute new displayName */
		if (station->name != NULL) {
			station->displayName = BarApplyStationNameOverrides(&app->settings, station->name);
		}
		
		station = (PianoStation_t *)station->head.next;
	}
}

