/*
 * miniaudio implementation file.
 *
 * miniaudio is a single-header library. When MINIAUDIO_IMPLEMENTATION is defined,
 * it includes ~95,000 lines of implementation code. By isolating this in its own
 * compilation unit, we avoid recompiling it every time player.c changes.
 */

#include "config.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

