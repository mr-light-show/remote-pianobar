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

#pragma once

#include <stdbool.h>

/* Volume control mode */
typedef enum {
	BAR_VOLUME_MODE_PLAYER = 0,  /* Player gain control (default) */
	BAR_VOLUME_MODE_SYSTEM = 1   /* System mixer volume */
} BarVolumeModeType;

/* Initialize system volume backend (call at startup)
 * Returns true if system volume control is available */
bool BarSystemVolumeInit(void);

/* Cleanup system volume resources */
void BarSystemVolumeDestroy(void);

/* Get current system volume as 0-100 percentage
 * Returns -1 on error */
int BarSystemVolumeGet(void);

/* Set system volume as 0-100 percentage
 * Returns true on success */
bool BarSystemVolumeSet(int percent);

/* Check if system volume control is available
 * (backend initialized successfully) */
bool BarSystemVolumeAvailable(void);

/* Check if default output device changed and refresh if needed
 * Returns true if device was refreshed, false otherwise
 * Safe to call from any thread */
bool BarSystemVolumeRefreshDevice(void);

