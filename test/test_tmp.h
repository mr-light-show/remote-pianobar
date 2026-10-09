#pragma once

/*	Scratch directories for unit tests.
 *
 *	main() creates one run-wide root with TestTmpRootInit and removes it with
 *	TestTmpRootCleanup.  Tests run in forked children, so the root is passed
 *	through the environment; TestTmpMkdtemp creates its directories below it
 *	and nothing is left behind in /tmp.
 */

#define _XOPEN_SOURCE 700
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define TEST_TMP_ROOT_ENV "PIANOBAR_TEST_TMPROOT"

static inline void TestTmpRootInit (void) {
	char root[] = "/tmp/pianobar-test-XXXXXX";
	if (mkdtemp (root) != NULL) {
		setenv (TEST_TMP_ROOT_ENV, root, 1);
	}
}

static inline int TestTmpRemoveCb (const char *path, const struct stat *sb,
		int type, struct FTW *ftw) {
	(void) sb;
	(void) type;
	(void) ftw;
	return remove (path);
}

static inline void TestTmpRootCleanup (void) {
	const char *root = getenv (TEST_TMP_ROOT_ENV);
	if (root != NULL) {
		nftw (root, TestTmpRemoveCb, 16, FTW_DEPTH | FTW_PHYS);
		unsetenv (TEST_TMP_ROOT_ENV);
	}
}

/*	Create "<root>/<prefix>_XXXXXX" in out; falls back to /tmp if the root
 *	was not initialised.  Returns out, or NULL on failure.
 */
static inline char *TestTmpMkdtemp (char *out, size_t size, const char *prefix) {
	const char *root = getenv (TEST_TMP_ROOT_ENV);
	snprintf (out, size, "%s/%s_XXXXXX", root != NULL ? root : "/tmp", prefix);
	return mkdtemp (out);
}
