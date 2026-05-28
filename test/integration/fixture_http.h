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

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

typedef enum {
	BAR_FIXTURE_HTTP_OK = 0,
	BAR_FIXTURE_HTTP_NOT_FOUND,
} BarFixtureHttpMode_t;

typedef struct {
	int listen_fd;
	uint16_t port;
	char filepath[512];
	BarFixtureHttpMode_t mode;
	pthread_t thread;
	volatile int stop;
} BarFixtureHttp_t;

bool BarFixtureHttpStart (BarFixtureHttp_t *srv, const char *filepath,
                          uint16_t *port_out);
void BarFixtureHttpStop (BarFixtureHttp_t *srv);
