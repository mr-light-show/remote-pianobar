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

#include "station_sort.h"

#include <assert.h>
#include <stdlib.h>
#include <strings.h>

typedef struct {
	bool isQuickMix;
	const char *name;
} BarStationSortKey_t;

/* Set for the duration of qsort; not re-entrant (matches prior ui.c usage). */
static BarStationSorting_t g_sortOrder;

static inline BarStationSortKey_t BarStationSortKeyFromPiano (const PianoStation_t *st) {
	return (BarStationSortKey_t) {
		.isQuickMix = st != NULL && st->isQuickMix,
		.name = (st != NULL && st->name != NULL) ? st->name : "",
	};
}

static inline BarStationSortKey_t BarStationSortKeyFromSnapshot (
		const BarStationSnapshot_t *st) {
	return (BarStationSortKey_t) {
		.isQuickMix = st != NULL && st->isQuickMix,
		.name = (st != NULL && st->name != NULL) ? st->name : "",
	};
}

static int BarStationSortCompareKeys (const BarStationSortKey_t *a,
		const BarStationSortKey_t *b, BarStationSorting_t order) {
	int qmc;
	int nameCmp = strcasecmp (a->name, b->name);

	switch (order) {
	case BAR_SORT_NAME_AZ:
		return nameCmp;

	case BAR_SORT_NAME_ZA:
		return -nameCmp;

	case BAR_SORT_QUICKMIX_01_NAME_AZ:
		qmc = (int) a->isQuickMix - (int) b->isQuickMix;
		return qmc != 0 ? qmc : nameCmp;

	case BAR_SORT_QUICKMIX_01_NAME_ZA:
		qmc = (int) a->isQuickMix - (int) b->isQuickMix;
		return qmc != 0 ? qmc : -nameCmp;

	case BAR_SORT_QUICKMIX_10_NAME_AZ:
		qmc = (int) b->isQuickMix - (int) a->isQuickMix;
		return qmc != 0 ? qmc : nameCmp;

	case BAR_SORT_QUICKMIX_10_NAME_ZA:
		qmc = (int) b->isQuickMix - (int) a->isQuickMix;
		return qmc != 0 ? qmc : -nameCmp;

	default:
		assert (0);
		return 0;
	}
}

static int BarStationSortComparePianoPtrs (const void *a, const void *b) {
	const PianoStation_t * const *pa = (const PianoStation_t * const *) a;
	const PianoStation_t * const *pb = (const PianoStation_t * const *) b;
	BarStationSortKey_t ka = BarStationSortKeyFromPiano (*pa);
	BarStationSortKey_t kb = BarStationSortKeyFromPiano (*pb);
	return BarStationSortCompareKeys (&ka, &kb, g_sortOrder);
}

static int BarStationSortCompareSnapshots (const void *a, const void *b) {
	BarStationSortKey_t ka = BarStationSortKeyFromSnapshot (
			(const BarStationSnapshot_t *) a);
	BarStationSortKey_t kb = BarStationSortKeyFromSnapshot (
			(const BarStationSnapshot_t *) b);
	return BarStationSortCompareKeys (&ka, &kb, g_sortOrder);
}

void BarSortStationSnapshots (BarStationSnapshot_t *items, size_t count,
		BarStationSorting_t order) {
	if (items == NULL || count <= 1) {
		return;
	}
	assert (order < BAR_SORT_COUNT);
	g_sortOrder = order;
	qsort (items, count, sizeof (*items), BarStationSortCompareSnapshots);
}

PianoStation_t **BarSortedStations (PianoStation_t *unsortedStations,
		size_t *retStationCount, BarStationSorting_t order) {
	PianoStation_t **stationArray = NULL;
	PianoStation_t *currStation = NULL;
	size_t stationCount = 0;
	size_t i;

	assert (order < BAR_SORT_COUNT);

	stationCount = PianoListCountP (unsortedStations);
	stationArray = calloc (stationCount, sizeof (*stationArray));
	if (stationArray == NULL) {
		*retStationCount = 0;
		return NULL;
	}

	i = 0;
	currStation = unsortedStations;
	PianoListForeachP (currStation) {
		stationArray[i] = currStation;
		++i;
	}

	g_sortOrder = order;
	qsort (stationArray, stationCount, sizeof (*stationArray),
			BarStationSortComparePianoPtrs);

	*retStationCount = stationCount;
	return stationArray;
}
