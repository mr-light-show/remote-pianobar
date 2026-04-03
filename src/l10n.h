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

#pragma once

#include "settings.h"

#include <stddef.h>
#include <stdbool.h>

typedef struct {
	char **keys;
	char **values;
	size_t count;
} BarL10nContext_t;

/* Embedded defaults table (generated). */
const char *BarL10nDefaultLookup (const char *key);

bool BarL10nInit (BarL10nContext_t *ctx, const BarSettings_t *settings);
void BarL10nDestroy (BarL10nContext_t *ctx);

/* Lookup in loaded file, then embedded defaults. */
const char *BarL10nGet (const BarL10nContext_t *ctx, const char *key);

/* Try key.module, then key.default, then key. module may be NULL. */
const char *BarL10nGetModule (const BarL10nContext_t *ctx, const char *baseKey,
		const char *module);

/* Format string comes from locale at runtime (BarL10nGet(key)); not a compile-time literal. */
void BarL10nFormat (const BarL10nContext_t *ctx, char *buf, size_t len,
		const char *key, ...);
