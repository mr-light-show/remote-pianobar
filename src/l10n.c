/*
Copyright (c) 2026
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

#include "l10n.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	char *key;
	char *val;
} BarL10nPair_t;

static int bar_l10n_pair_cmp (const void *a, const void *b) {
	const BarL10nPair_t *pa = (const BarL10nPair_t *) a;
	const BarL10nPair_t *pb = (const BarL10nPair_t *) b;
	return strcmp (pa->key, pb->key);
}

static char *bar_l10n_unescape_value (const char *in) {
	size_t len = strlen (in);
	char *out = malloc (len + 1);
	if (!out) {
		return NULL;
	}
	size_t o = 0;
	for (size_t i = 0; i < len; i++) {
		if (in[i] == '\\' && i + 1 < len) {
			if (in[i + 1] == 'n') {
				out[o++] = '\n';
				i++;
				continue;
			}
			if (in[i + 1] == 'r') {
				out[o++] = '\r';
				i++;
				continue;
			}
			if (in[i + 1] == '\\') {
				out[o++] = '\\';
				i++;
				continue;
			}
		}
		out[o++] = in[i];
	}
	out[o] = '\0';
	return out;
}

static bool bar_l10n_append_pair (BarL10nPair_t **pairs, size_t *n, size_t *cap,
		const char *key, const char *val_raw) {
	if (*n >= *cap) {
		size_t nc = *cap ? (*cap * 2) : 16;
		BarL10nPair_t *np = realloc (*pairs, nc * sizeof (**pairs));
		if (!np) {
			return false;
		}
		*pairs = np;
		*cap = nc;
	}
	char *vk = strdup (key);
	char *vv = bar_l10n_unescape_value (val_raw);
	if (!vk || !vv) {
		free (vk);
		free (vv);
		return false;
	}
	(*pairs)[*n].key = vk;
	(*pairs)[*n].val = vv;
	++*n;
	return true;
}

static bool bar_l10n_load_file (const char *path, BarL10nPair_t **pairs,
		size_t *n, size_t *cap) {
	FILE *f = fopen (path, "r");
	if (!f) {
		return false;
	}
	char line[4096];
	while (fgets (line, sizeof (line), f)) {
		char *s = line;
		while (*s && isspace ((unsigned char) *s)) {
			s++;
		}
		if (*s == '#' || *s == '\0' || *s == '\r' || *s == '\n') {
			continue;
		}
		char *eq = strchr (s, '=');
		if (!eq) {
			continue;
		}
		*eq = '\0';
		char *val = eq + 1;
		if (*val == ' ' || *val == '\t') {
			val++;
		}
		char *ke = s + strlen (s);
		while (ke > s && isspace ((unsigned char) ke[-1])) {
			*--ke = '\0';
		}
		char *ve = val + strlen (val);
		while (ve > val && (ve[-1] == '\n' || ve[-1] == '\r')) {
			*--ve = '\0';
		}
		if (*s == '\0') {
			continue;
		}
		if (!bar_l10n_append_pair (pairs, n, cap, s, val)) {
			fclose (f);
			return false;
		}
	}
	fclose (f);
	return true;
}

static void bar_l10n_free_pairs (BarL10nPair_t *pairs, size_t n) {
	for (size_t i = 0; i < n; i++) {
		free (pairs[i].key);
		free (pairs[i].val);
	}
	free (pairs);
}

static void bar_l10n_normalize_lang (const char *in, char *out, size_t outsz) {
	if (!in || !*in) {
		strncpy (out, "en", outsz);
		out[outsz - 1] = '\0';
		return;
	}
	size_t o = 0;
	for (size_t i = 0; in[i] && in[i] != '.' && in[i] != '@' && in[i] != '_'; i++) {
		if (o + 1 >= outsz) {
			break;
		}
		out[o++] = (char) tolower ((unsigned char) in[i]);
	}
	out[o] = '\0';
	if (out[0] == '\0') {
		strncpy (out, "en", outsz);
		out[outsz - 1] = '\0';
	}
}

static bool bar_l10n_try_path (const char *path, BarL10nPair_t **pairs, size_t *n,
		size_t *cap) {
	return bar_l10n_load_file (path, pairs, n, cap);
}

void BarL10nDestroy (BarL10nContext_t *ctx) {
	if (!ctx) {
		return;
	}
	for (size_t i = 0; i < ctx->count; i++) {
		free (ctx->keys[i]);
		free (ctx->values[i]);
	}
	free (ctx->keys);
	free (ctx->values);
	memset (ctx, 0, sizeof (*ctx));
}

static const char *bar_l10n_lookup_pairs (const BarL10nContext_t *ctx,
		const char *key) {
	if (!ctx || !key) {
		return NULL;
	}
	size_t lo = 0, hi = ctx->count;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int c = strcmp (key, ctx->keys[mid]);
		if (c == 0) {
			return ctx->values[mid];
		}
		if (c < 0) {
			hi = mid;
		} else {
			lo = mid + 1;
		}
	}
	return NULL;
}

const char *BarL10nGet (const BarL10nContext_t *ctx, const char *key) {
	if (ctx && ctx->count > 0) {
		const char *f = bar_l10n_lookup_pairs (ctx, key);
		if (f) {
			return f;
		}
	}
	return BarL10nDefaultLookup (key);
}

const char *BarL10nGetModule (const BarL10nContext_t *ctx, const char *base,
		const char *module) {
	char buf[512];
	if (module && module[0]) {
		snprintf (buf, sizeof (buf), "%s.%s", base, module);
		{
			const char *r = BarL10nGet (ctx, buf);
			if (r) {
				return r;
			}
		}
		snprintf (buf, sizeof (buf), "%s.default", base);
		{
			const char *r = BarL10nGet (ctx, buf);
			if (r) {
				return r;
			}
		}
	}
	return BarL10nGet (ctx, base);
}

void BarL10nFormat (const BarL10nContext_t *ctx, char *buf, size_t len,
		const char *key, ...) {
	const char *fmt = BarL10nGet (ctx, key);
	if (!fmt) {
		if (len) {
			buf[0] = '\0';
		}
		return;
	}
	va_list ap;
	va_start (ap, key);
	vsnprintf (buf, len, fmt, ap);
	va_end (ap);
}

bool BarL10nInit (BarL10nContext_t *ctx, const BarSettings_t *settings) {
	assert (ctx);
	memset (ctx, 0, sizeof (*ctx));
	BarL10nPair_t *pairs = NULL;
	size_t n = 0, cap = 0;

	char lang[32];
	if (settings && settings->locale && settings->locale[0]) {
		bar_l10n_normalize_lang (settings->locale, lang, sizeof (lang));
	} else {
		const char *lc = getenv ("LC_MESSAGES");
		if (!lc || !*lc) {
			lc = getenv ("LANG");
		}
		bar_l10n_normalize_lang (lc ? lc : "en", lang, sizeof (lang));
	}

	const char *overrideDir = getenv ("PIANOBAR_LOCALE_DIR");
	char path[PATH_MAX];

	if (overrideDir && overrideDir[0]) {
		snprintf (path, sizeof (path), "%s/%s", overrideDir, lang);
		if (bar_l10n_try_path (path, &pairs, &n, &cap)) {
			goto have_locale_file;
		}
	}

	{
		const char *xdg = getenv ("XDG_CONFIG_HOME");
		if (xdg && xdg[0]) {
			snprintf (path, sizeof (path), "%s/pianobar/locale/%s", xdg, lang);
		} else {
			const char *home = getenv ("HOME");
			if (home && home[0]) {
				snprintf (path, sizeof (path), "%s/.config/pianobar/locale/%s",
						home, lang);
			} else {
				path[0] = '\0';
			}
		}
		if (path[0] && bar_l10n_try_path (path, &pairs, &n, &cap)) {
			goto have_locale_file;
		}
	}

	{
		const char *pfx = getenv ("PIANOBAR_INSTALL_PREFIX");
		if (!pfx || !pfx[0]) {
			pfx = "/usr/local";
		}
		snprintf (path, sizeof (path), "%s/share/pianobar/locale/%s", pfx, lang);
		bar_l10n_try_path (path, &pairs, &n, &cap);
	}

have_locale_file:

	if (n == 0) {
		return true;
	}

	qsort (pairs, n, sizeof (pairs[0]), bar_l10n_pair_cmp);

	ctx->keys = calloc (n, sizeof (*ctx->keys));
	ctx->values = calloc (n, sizeof (*ctx->values));
	if (!ctx->keys || !ctx->values) {
		free (ctx->keys);
		free (ctx->values);
		ctx->keys = NULL;
		ctx->values = NULL;
		bar_l10n_free_pairs (pairs, n);
		return false;
	}
	ctx->count = n;
	for (size_t i = 0; i < n; i++) {
		ctx->keys[i] = pairs[i].key;
		ctx->values[i] = pairs[i].val;
		pairs[i].key = NULL;
		pairs[i].val = NULL;
	}
	free (pairs);
	return true;
}
