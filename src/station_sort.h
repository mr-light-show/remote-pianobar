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

#pragma once

#include <stddef.h>

#include <piano.h>

#include "bar_state.h"
#include "settings.h"

/* Copy linked list to a sorted pointer array; caller frees the result. */
PianoStation_t **BarSortedStations (PianoStation_t *unsortedStations,
		size_t *retStationCount, BarStationSorting_t order);

/* Sort snapshot items in place using the same rules as BarSortedStations. */
void BarSortStationSnapshots (BarStationSnapshot_t *items, size_t count,
		BarStationSorting_t order);
