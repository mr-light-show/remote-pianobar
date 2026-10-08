/*
Copyright (c) 2025

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

#include "interrupt.h"
#include <stddef.h>

static _Atomic(_Atomic sig_atomic_t *) g_interrupted = NULL;

void BarInterruptSetTarget (_Atomic sig_atomic_t *target) {
	atomic_store_explicit (&g_interrupted, target, memory_order_relaxed);
}

_Atomic sig_atomic_t *BarInterruptGetTarget (void) {
	return atomic_load_explicit (&g_interrupted, memory_order_relaxed);
}

void BarInterruptIncrement (void) {
	_Atomic sig_atomic_t *target =
			atomic_load_explicit (&g_interrupted, memory_order_relaxed);
	if (target != NULL) {
		atomic_fetch_add_explicit (target, 1, memory_order_relaxed);
	}
}
