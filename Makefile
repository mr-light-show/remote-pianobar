# makefile of pianobar

# Version is defined in src/config.h only (do not pass -DVERSION from here).

PKG_CONFIG?=pkg-config
PREFIX:=/usr/local
BINDIR:=${PREFIX}/bin
LIBDIR:=${PREFIX}/lib
INCDIR:=${PREFIX}/include
MANDIR:=${PREFIX}/share/man
LOCALEDIR:=${PREFIX}/share/pianobar/locale
DYNLINK:=0
CFLAGS?=-O2 -DNDEBUG

# Runtime debug categories (debug=N in config, PIANOBAR_DEBUG env) unless MINIMAL=1.
# Mask 0 means no DEBUG_* output. Keeps -DNDEBUG for asserts in release builds.
ifndef MINIMAL
override CFLAGS+=-DHAVE_DEBUGLOG
endif

# Feature test macros for POSIX/BSD/GNU functions (flock, usleep, clock_gettime, etc.)
# Use 'override' so these are always included even when CFLAGS is set on command line
override CFLAGS+=-D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE

ifeq (${CC},cc)
	OS := $(shell uname)
	ifeq (${OS},Darwin)
		CC:=gcc
	else ifeq (${OS},FreeBSD)
		CC:=cc
	else ifeq (${OS},OpenBSD)
		CC:=cc
	else
		CC:=cc
	endif
endif

# Default ISO C17 unless the caller already set a dialect in CFLAGS (e.g. -std=gnu11).
ifeq ($(findstring -std=,$(CFLAGS)),)
override CFLAGS+=-std=c17
endif

PIANOBAR_DIR:=src
# Third-party miniaudio wrapper: omit from cppcheck (vendored, multi-config noise)
PIANOBAR_LINT_C:=$(filter-out ${PIANOBAR_DIR}/miniaudio_impl.c,$(wildcard ${PIANOBAR_DIR}/*.c))
# Every WebSocket source we own (lws itself is a system library, not linted)
PIANOBAR_LINT_WS:=\
		src/websocket/core/websocket.c \
		src/websocket/core/websocket_lws_log.c \
		src/websocket/core/outbox.c \
		src/websocket/core/commands.c \
		src/websocket/http/http_server.c \
		src/websocket/protocol/socketio.c \
		src/websocket/protocol/error_messages.c \
		src/websocket/daemon/daemon.c
LOCALE_YAML:=$(wildcard locale/*.yaml)
LOCALE_GEN_MARKER:=${PIANOBAR_DIR}/l10n_defaults_gen.c
# Updated when codegen runs; avoids re-running Python on every `make locale-codegen` (e.g. after
# root `make`, webui `npm run prebuild` only prints GEN if YAML/script changed).
LOCALE_CODEGEN_STAMP:=.locale-codegen.stamp
PIANOBAR_SRC:=\
		${PIANOBAR_DIR}/main.c \
		${PIANOBAR_DIR}/interrupt.c \
		${PIANOBAR_DIR}/playback_lifecycle.c \
		${PIANOBAR_DIR}/log.c \
		${PIANOBAR_DIR}/miniaudio_impl.c \
		${PIANOBAR_DIR}/parse_utils.c \
		${PIANOBAR_DIR}/player.c \
		${PIANOBAR_DIR}/bar_state.c \
		${PIANOBAR_DIR}/playback_manager.c \
		${PIANOBAR_DIR}/settings.c \
		${PIANOBAR_DIR}/l10n.c \
		${PIANOBAR_DIR}/l10n_defaults_gen.c \
		${PIANOBAR_DIR}/station_display.c \
		${PIANOBAR_DIR}/station_sort.c \
		${PIANOBAR_DIR}/terminal.c \
		${PIANOBAR_DIR}/ui_act.c \
		${PIANOBAR_DIR}/ui.c \
		${PIANOBAR_DIR}/ui_readline.c \
		${PIANOBAR_DIR}/ui_dispatch.c \
		${PIANOBAR_DIR}/websocket_bridge.c \
		${PIANOBAR_DIR}/system_volume.c

# WebSocket support
WEBSOCKET_DIR:=src/websocket
PIANOBAR_SRC+=\
	${WEBSOCKET_DIR}/core/websocket.c \
	${WEBSOCKET_DIR}/core/websocket_lws_log.c \
	${WEBSOCKET_DIR}/core/outbox.c \
	${WEBSOCKET_DIR}/core/commands.c \
	${WEBSOCKET_DIR}/http/http_server.c \
	${WEBSOCKET_DIR}/protocol/socketio.c \
	${WEBSOCKET_DIR}/protocol/error_messages.c \
	${WEBSOCKET_DIR}/daemon/daemon.c

# Objects, dependency files and binaries go in a per-variant directory, so the
# default, coverage and sanitizer builds never share objects and can coexist
# (and be built at the same time). Variant targets set their own BUILD_DIR.
BUILD_DIR?=build/default
B:=${BUILD_DIR}

PIANOBAR_OBJ:=$(addprefix ${B}/,${PIANOBAR_SRC:.c=.o})

LIBPIANO_DIR:=src/libpiano
LIBPIANO_SRC:=\
		${LIBPIANO_DIR}/crypt.c \
		${LIBPIANO_DIR}/piano.c \
		${LIBPIANO_DIR}/request.c \
		${LIBPIANO_DIR}/response.c \
		${LIBPIANO_DIR}/list.c
LIBPIANO_OBJ:=$(addprefix ${B}/,${LIBPIANO_SRC:.c=.o})
LIBPIANO_RELOBJ:=$(addprefix ${B}/,${LIBPIANO_SRC:.c=.lo})
LIBPIANO_INCLUDE:=${LIBPIANO_DIR}

LIBAV_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libavcodec libavformat libavutil libavfilter)
LIBAV_LDFLAGS:=$(shell $(PKG_CONFIG) --libs libavcodec libavformat libavutil libavfilter)

LIBCURL_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libcurl)
LIBCURL_LDFLAGS:=$(shell $(PKG_CONFIG) --libs libcurl)

LIBGCRYPT_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libgcrypt)
LIBGCRYPT_LDFLAGS:=$(shell $(PKG_CONFIG) --libs libgcrypt)

LIBJSONC_CFLAGS:=$(shell $(PKG_CONFIG) --cflags json-c 2>/dev/null || $(PKG_CONFIG) --cflags json)
LIBJSONC_LDFLAGS:=$(shell $(PKG_CONFIG) --libs json-c 2>/dev/null || $(PKG_CONFIG) --libs json)

# miniaudio is header-only, no linking required

# System volume control - platform-specific
OS := $(shell uname)
ifeq (${OS},Darwin)
	# macOS - CoreAudio is always available (requires macOS 12.0+)
	SYSVOLUME_LDFLAGS:=-framework CoreAudio -framework AudioToolbox
	CFLAGS+=-mmacosx-version-min=12.0 -DMAC_OS_X_VERSION_MAX_ALLOWED=260000 -DMAC_OS_VERSION_12_0=120000
	# macOS does not need BLAS/LAPACK
	BLAS_LAPACK_LDFLAGS:=
else ifeq (${OS},Linux)
	# Linux - try PulseAudio library, always link ALSA for fallback
	HAVE_PULSEAUDIO:=$(shell $(PKG_CONFIG) --exists libpulse && echo yes)
	ifeq (${HAVE_PULSEAUDIO},yes)
		SYSVOLUME_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libpulse) -DHAVE_PULSEAUDIO
		SYSVOLUME_LDFLAGS:=$(shell $(PKG_CONFIG) --libs libpulse)
	endif
	# Always link ALSA on Linux (for native API fallback)
	SYSVOLUME_LDFLAGS+=$(shell $(PKG_CONFIG) --libs alsa)
	# BLAS/LAPACK required by libsphinxbase (transitive dep of FFmpeg/PulseAudio)
	BLAS_LAPACK_LDFLAGS:=-lblas -llapack
endif

# WebSocket library flags
# Fail early only for goals that compile or link; clean/lint/codegen need no libwebsockets.
ifeq ($(filter-out clean distclean locale-codegen lint lint-test test-ci-local uninstall,$(or $(MAKECMDGOALS),pianobar)),)
else ifneq ($(shell $(PKG_CONFIG) --exists libwebsockets openssl && echo yes),yes)
$(error libwebsockets (4.0+) and openssl development files not found; install them, see DEVELOPMENT.md)
endif
LIBWEBSOCKETS_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libwebsockets openssl)
LIBWEBSOCKETS_LDFLAGS:=$(shell $(PKG_CONFIG) --libs libwebsockets openssl)

# Check unit-test framework
CHECK_CFLAGS:=$(shell $(PKG_CONFIG) --cflags check 2>/dev/null)
CHECK_LDFLAGS:=$(shell $(PKG_CONFIG) --libs check 2>/dev/null)

# First real rule was locale-codegen; without this, bare `make` only runs codegen and
# never builds pianobar. Default goal must be the binary (locale-codegen is a dep of pianobar).
.DEFAULT_GOAL := pianobar

$(LOCALE_CODEGEN_STAMP): $(LOCALE_YAML) scripts/locale_codegen.py
	@echo "  GEN   locale catalogs"
	@python3 scripts/locale_codegen.py --repo-root .
	@touch $(LOCALE_CODEGEN_STAMP)

.PHONY: locale-codegen
locale-codegen: $(LOCALE_CODEGEN_STAMP)

.PHONY: locale-check-sync
locale-check-sync:
	python3 scripts/check_locale_sync.py

# combine all flags
ALL_CFLAGS:=${CFLAGS} -I ${LIBPIANO_INCLUDE} \
			${LIBAV_CFLAGS} ${LIBCURL_CFLAGS} \
			${LIBGCRYPT_CFLAGS} ${LIBJSONC_CFLAGS} \
			${SYSVOLUME_CFLAGS}
ALL_LDFLAGS:=${LDFLAGS} -lpthread -lm \
		${LIBAV_LDFLAGS} ${LIBCURL_LDFLAGS} \
		${LIBGCRYPT_LDFLAGS} ${LIBJSONC_LDFLAGS} \
		${SYSVOLUME_LDFLAGS} ${BLAS_LAPACK_LDFLAGS}

# Add WebSocket flags
ALL_CFLAGS+=${LIBWEBSOCKETS_CFLAGS}
ALL_LDFLAGS+=${LIBWEBSOCKETS_LDFLAGS}

# Be verbose if V=1 (gnu autotools’ --disable-silent-rules)
SILENTCMD:=@
SILENTECHO:=@echo
ifeq (${V},1)
	SILENTCMD:=
	SILENTECHO:=@true
endif

# Compiler, flags and link flags of this build directory. Objects depend on
# it, so a build with different CFLAGS/LDFLAGS (e.g. CFLAGS="-g -O0") rebuilds
# instead of mixing objects; the file only changes when the flags do.
BUILD_FLAGS:=${CC} ${ALL_CFLAGS} ${CHECK_CFLAGS} ${ALL_LDFLAGS} ${CHECK_LDFLAGS}
BUILD_FLAGS_FILE:=${B}/.build-flags
${BUILD_FLAGS_FILE}: FORCE
	@mkdir -p $(@D)
	@printf '%s\n' '$(subst ','\'',${BUILD_FLAGS})' | cmp -s - $@ || \
		printf '%s\n' '$(subst ','\'',${BUILD_FLAGS})' > $@

.PHONY: FORCE
FORCE:

# build pianobar (in ${B}/), then copy it to ./pianobar for install, packaging
# and scripts
.PHONY: pianobar
pianobar: ${B}/pianobar
	${SILENTCMD}cmp -s $< $@ || cp $< $@

ifeq (${DYNLINK},1)
${B}/pianobar: locale-codegen ${PIANOBAR_OBJ} libpiano.so.0 ${BUILD_FLAGS_FILE}
	${SILENTECHO} "  LINK  $@"
	${SILENTCMD}${CC} -o $@ ${PIANOBAR_OBJ} -L. -lpiano ${ALL_LDFLAGS}
else
${B}/pianobar: locale-codegen ${PIANOBAR_OBJ} ${LIBPIANO_OBJ} ${BUILD_FLAGS_FILE}
	${SILENTECHO} "  LINK  $@"
	${SILENTCMD}${CC} -o $@ ${PIANOBAR_OBJ} ${LIBPIANO_OBJ} ${ALL_LDFLAGS}
endif

# build shared and static libpiano
libpiano.so.0: ${LIBPIANO_RELOBJ} ${LIBPIANO_OBJ}
	${SILENTECHO} "  LINK  $@"
	${SILENTCMD}${CC} -shared -Wl,-soname,libpiano.so.0 -o libpiano.so.0.0.0 \
			${LIBPIANO_RELOBJ} ${ALL_LDFLAGS}
	${SILENTCMD}ln -fs libpiano.so.0.0.0 libpiano.so.0
	${SILENTCMD}ln -fs libpiano.so.0 libpiano.so
	${SILENTECHO} "    AR  libpiano.a"
	${SILENTCMD}${AR} rcs libpiano.a ${LIBPIANO_OBJ}


-include $(PIANOBAR_OBJ:.o=.d)
-include $(LIBPIANO_OBJ:.o=.d)

# Test-specific compilation rule (must come before the general ${B}/%.o rule;
# GNU make 3.81 takes the first matching pattern rule)
${B}/test/%.o: test/%.c ${BUILD_FLAGS_FILE}
	@mkdir -p $(@D)
	${SILENTECHO} "    CC  $< (test)"
	${SILENTCMD}${CC} -c -o $@ ${ALL_CFLAGS} ${CHECK_CFLAGS} -MMD -MF $(@:.o=.d) -MP $<

# Special rule for miniaudio_impl.c - suppress deprecation warnings from third-party library
${B}/src/miniaudio_impl.o: src/miniaudio_impl.c ${BUILD_FLAGS_FILE}
	@mkdir -p $(@D)
	${SILENTECHO} "    CC  $<"
	${SILENTCMD}${CC} -c -o $@ ${ALL_CFLAGS} -Wno-deprecated-declarations -MMD -MF $(@:.o=.d) -MP $<

# Ensure generated strings are up to date before compiling (safe with parallel -j).
${B}/${PIANOBAR_DIR}/l10n_defaults_gen.o: $(LOCALE_CODEGEN_STAMP)

# build standard object files
${B}/%.o: %.c ${BUILD_FLAGS_FILE}
	@mkdir -p $(@D)
	${SILENTECHO} "    CC  $<"
	${SILENTCMD}${CC} -c -o $@ ${ALL_CFLAGS} -MMD -MF $(@:.o=.d) -MP $<

# create position independent code (for shared libraries)
${B}/%.lo: %.c ${BUILD_FLAGS_FILE}
	@mkdir -p $(@D)
	${SILENTECHO} "    CC  $< (PIC)"
	${SILENTCMD}${CC} -c -fPIC -o $@ ${ALL_CFLAGS} -MMD -MF $(@:.lo=.d) -MP $<

# Removes every build directory, the top-level copies, and objects left in the
# source tree by the old in-tree layout.
clean:
	${SILENTECHO} " CLEAN"
	${SILENTCMD}${RM} -r build
	${SILENTCMD}${RM} pianobar pianobar_test libpiano.so* libpiano.a \
			$(LOCALE_CODEGEN_STAMP) \
			${PIANOBAR_SRC:.c=.o} ${LIBPIANO_SRC:.c=.o} ${LIBPIANO_SRC:.c=.lo} \
			${PIANOBAR_SRC:.c=.d} ${LIBPIANO_SRC:.c=.d} \
			${TEST_SRC:.c=.o} ${TEST_SRC:.c=.d}

distclean: clean
	${SILENTECHO} " DISTCLEAN"
	${SILENTCMD}find . -name '*.gcda' -delete
	${SILENTCMD}find . -name '*.gcno' -delete
	${SILENTCMD}find . -name '*.gcov' -delete

all: pianobar
	@echo "Build complete. Running tests..."
	@${MAKE} test

.PHONY: minimal
minimal:
	@$(MAKE) MINIMAL=1 BUILD_DIR=build/minimal pianobar

ifeq (${DYNLINK},1)
install: pianobar install-libpiano
else
install: pianobar
endif
	install -d ${DESTDIR}${BINDIR}/
	install -m755 pianobar ${DESTDIR}${BINDIR}/
	install -d ${DESTDIR}${MANDIR}/man1/
	install -m644 contrib/pianobar.1 ${DESTDIR}${MANDIR}/man1/
	install -d ${DESTDIR}${LOCALEDIR}/
	@for f in locale/*; do \
		case $$f in *.yaml) ;; *) test -f "$$f" && install -m644 "$$f" "${DESTDIR}${LOCALEDIR}/" ;; esac; \
	done

install-libpiano:
	install -d ${DESTDIR}${LIBDIR}/
	install -m644 libpiano.so.0.0.0 ${DESTDIR}${LIBDIR}/
	ln -fs libpiano.so.0.0.0 ${DESTDIR}${LIBDIR}/libpiano.so.0
	ln -fs libpiano.so.0 ${DESTDIR}${LIBDIR}/libpiano.so
	install -m644 libpiano.a ${DESTDIR}${LIBDIR}/
	install -d ${DESTDIR}${INCDIR}/
	install -m644 src/libpiano/piano.h ${DESTDIR}${INCDIR}/

uninstall:
	$(RM) ${DESTDIR}/${BINDIR}/pianobar \
	${DESTDIR}/${MANDIR}/man1/pianobar.1 \
	${DESTDIR}/${LIBDIR}/libpiano.so.0.0.0 \
	${DESTDIR}/${LIBDIR}/libpiano.so.0 \
	${DESTDIR}/${LIBDIR}/libpiano.so \
	${DESTDIR}/${LIBDIR}/libpiano.a \
	${DESTDIR}/${INCDIR}/piano.h

# Test suite: split into base (no WebSocket deps) and WS-only parts
TEST_DIR:=test
TEST_BIN:=${B}/pianobar_test

# Base tests (linked with websocket_bridge.o via BASE_TEST_LINK_OBJ)
BASE_TEST_SRC:=\
		${TEST_DIR}/test_main.c \
		${TEST_DIR}/unit/test_l10n.c \
		${TEST_DIR}/unit/test_settings.c \
		${TEST_DIR}/unit/test_player.c \
		${TEST_DIR}/unit/test_bar_state.c \
		${TEST_DIR}/unit/test_log.c \
		${TEST_DIR}/unit/test_playback_manager.c \
		${TEST_DIR}/unit/test_playback_lifecycle.c \
		${TEST_DIR}/unit/test_system_volume.c \
		${TEST_DIR}/unit/test_ui_readline.c \
		${TEST_DIR}/unit/test_ui.c \
		${TEST_DIR}/unit/test_station_sort.c \
		${TEST_DIR}/unit/test_interrupt.c \
		${TEST_DIR}/unit/test_libpiano_response.c \
		${TEST_DIR}/unit/test_state_edge_paths.c

# Tests that require WebSocket objects
WS_TEST_SRC:=\
		${TEST_DIR}/unit/test_websocket.c \
		${TEST_DIR}/unit/test_ws_outbox.c \
		${TEST_DIR}/unit/test_ws_outbox_faults.c \
		${TEST_DIR}/unit/test_ws_service_edges.c \
		${TEST_DIR}/unit/test_ws_commands.c \
		${TEST_DIR}/unit/test_ws_failure_paths.c \
		${TEST_DIR}/unit/test_http_server.c \
		${TEST_DIR}/unit/test_daemon.c \
		${TEST_DIR}/unit/test_socketio.c \
		${TEST_DIR}/unit/test_socketio_station_failures.c \
		${TEST_DIR}/unit/test_error_messages.c \
		${TEST_DIR}/unit/test_ui_act_reconnect.c \
		${TEST_DIR}/unit/test_ui_act_station.c \
		${TEST_DIR}/unit/test_ws_edge_cases.c \
		${TEST_DIR}/unit/test_cli_input_paths.c \
		${TEST_DIR}/integration/fixture_http.c \
		${TEST_DIR}/integration/test_playback_integration.c

TEST_SRC:=${BASE_TEST_SRC} ${WS_TEST_SRC}

TEST_OBJ:=$(addprefix ${B}/,${TEST_SRC:.c=.o})
-include $(TEST_OBJ:.o=.d)

# Base objects (no WebSocket objects)
BASE_TEST_LINK_OBJ:=$(addprefix ${B}/,src/interrupt.o src/playback_lifecycle.o src/log.o src/miniaudio_impl.o src/parse_utils.o src/bar_state.o src/playback_manager.o src/websocket_bridge.o src/ui.o src/ui_act.o src/ui_dispatch.o src/ui_readline.o src/terminal.o src/player.o src/settings.o src/station_display.o src/station_sort.o src/system_volume.o src/l10n.o src/l10n_defaults_gen.o) ${LIBPIANO_OBJ}
WS_TEST_LINK_OBJ:=$(addprefix ${B}/${WEBSOCKET_DIR}/,core/websocket.o core/websocket_lws_log.o core/outbox.o core/commands.o http/http_server.o protocol/socketio.o protocol/error_messages.o daemon/daemon.o)

# Link all objects including WebSocket modules
${TEST_BIN}: locale-codegen ${TEST_OBJ} ${BASE_TEST_LINK_OBJ} ${WS_TEST_LINK_OBJ} ${BUILD_FLAGS_FILE}
	${SILENTECHO} "  LINK  $@"
	${SILENTCMD}${CC} -o $@ ${TEST_OBJ} ${BASE_TEST_LINK_OBJ} ${WS_TEST_LINK_OBJ} ${ALL_LDFLAGS} ${CHECK_LDFLAGS}

# ./pianobar_test: copy of the default build's test binary (docs, CK_RUN_SUITE
# one-liners)
.PHONY: pianobar_test
pianobar_test: ${TEST_BIN}
	${SILENTCMD}cmp -s $< $@ || cp $< $@

# Run tests
test: pianobar_test
	${SILENTECHO} "   TEST  Running test suite..."
	${SILENTCMD}./${TEST_BIN}

# Same tests as `make test`, one suite per process, several at a time
# (scripts/test-parallel.sh; TEST_JOBS=N workers, TEST_JOBS=1 is the serial run)
TEST_PAR:=./scripts/test-parallel.sh ./${TEST_BIN}
test-par: pianobar_test
	${SILENTECHO} "   TEST  Running test suite in parallel shards..."
	${SILENTCMD}${TEST_PAR}

# Integration tests (local HTTP audio fixture + mocked Pandora playlist)
test-integration: pianobar_test
	${SILENTECHO} "   TEST  Running integration tests..."
	${SILENTCMD}PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 ./${TEST_BIN}

# Web UI Playwright E2E against the real pianobar binary and a local fake
# Pandora API (no account, network or sound card needed)
test-e2e-fake: pianobar
	${SILENTECHO} "   TEST  Running web UI E2E against fake Pandora..."
	${SILENTCMD}chmod +x scripts/e2e-fake.sh && PIANOBAR_BIN=./pianobar ./scripts/e2e-fake.sh

# Web-only daemon startup smoke test against the real binary (no account,
# network or sound card needed): fds 0/1/2 stay valid and the failure is shown
test-startup-smoke: pianobar
	${SILENTECHO} "   TEST  Running web-only startup smoke test..."
	${SILENTCMD}PIANOBAR_BIN=./pianobar ./test/integration/web_mode_startup.sh

# Reproduce GitHub Actions c-tests in Docker (headless Ubuntu, no sound card)
test-ci-local:
	${SILENTECHO} "   TEST  Running CI-local Docker repro..."
	${SILENTCMD}chmod +x scripts/test-ci-local.sh && ./scripts/test-ci-local.sh

# Run static analysis (requires cppcheck)
# Suppressions: .cppcheck-suppress (miniaudio.h and project-specific false positives)
# PIANOBAR_LINT_C omits miniaudio_impl.c (vendored wrapper, noisy configs)
# CPPCHECK_LENIENT=1: cppcheck < 2.18 (Ubuntu 24.04 apt) reports many existing suppressions as
# unmatched; the default stays strict so stale suppressions are caught on current cppcheck.
lint:
	${SILENTECHO} "   LINT  Running static analysis..."
	${SILENTCMD}cppcheck --enable=all --max-configs=3 --suppress=missingIncludeSystem --suppress=unusedFunction \
		--suppress=toomanyconfigs --suppress=normalCheckLevelMaxBranches --suppress=checkersReport \
		--inline-suppr --error-exitcode=1 --suppressions-list=.cppcheck-suppress \
		$(if $(CPPCHECK_LENIENT),--suppress=unmatchedSuppression) \
		-I ${LIBPIANO_INCLUDE} ${PIANOBAR_LINT_C} ${PIANOBAR_LINT_WS} ${LIBPIANO_DIR}/*.c

# Run linter on test files
lint-test:
	${SILENTECHO} "   LINT  Running static analysis on tests..."
	${SILENTCMD}cppcheck --enable=all --suppress=missingIncludeSystem \
		--inline-suppr --error-exitcode=1 \
		-I ${LIBPIANO_INCLUDE} -I ${PIANOBAR_DIR} ${TEST_DIR}/**/*.c ${TEST_DIR}/*.c

# Comprehensive test suite: unit tests + linting
# Tests for the shell helpers under scripts/ (change-scope classifier)
test-scripts:
	${SILENTECHO} "   TEST  scripts/changed-scope.sh..."
	${SILENTCMD}bash scripts/test-changed-scope.sh

test-all: test lint test-scripts
	${SILENTECHO} "   TEST  All tests passed!"

# Build and run tests with coverage instrumentation, in build/coverage. Only
# the counters (.gcda) are reset; objects are rebuilt when sources or flags
# change. The report is build/coverage/coverage.info, copied to ./coverage.info
# (what CI uploads to Codecov).
COV_DIR:=build/coverage
test-coverage: locale-codegen
	${SILENTECHO} "   TEST  Building with coverage instrumentation..."
	${SILENTCMD}${MAKE} BUILD_DIR=${COV_DIR} ${COV_DIR}/pianobar_test CFLAGS="${CFLAGS} --coverage -fprofile-update=atomic -g -O0" LDFLAGS="${LDFLAGS} --coverage"
	${SILENTCMD}find ${COV_DIR} -name '*.gcda' -delete
	${SILENTCMD}${RM} ${COV_DIR}/coverage.info coverage.info
	${SILENTECHO} "   TEST  Running test suite (CI parity: integration + headless audio)..."
	${SILENTCMD}PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 ./scripts/test-parallel.sh ./${COV_DIR}/pianobar_test
	${SILENTECHO} "   COV   Generating coverage report..."
	${SILENTCMD}lcov --capture --directory ${COV_DIR} --base-directory . --output-file ${COV_DIR}/coverage.info \
		--rc lcov_branch_coverage=1 --ignore-errors inconsistent,unused
	${SILENTCMD}lcov --remove ${COV_DIR}/coverage.info '/usr/*' '*/test/*' '*l10n_defaults_gen.c' \
		--output-file ${COV_DIR}/coverage.info \
		--rc lcov_branch_coverage=1 --ignore-errors inconsistent,unused
	${SILENTCMD}cp ${COV_DIR}/coverage.info coverage.info
	${SILENTECHO} "   COV   Coverage report: coverage.info"

# Clean coverage files
coverage-clean:
	${SILENTECHO} " CLEAN  coverage"
	${SILENTCMD}find . -name '*.gcno' -delete
	${SILENTCMD}find . -name '*.gcda' -delete
	${SILENTCMD}${RM} coverage.info ${COV_DIR}/coverage.info

# Clean test files
test-clean:
	${SILENTECHO} " CLEAN  tests"
	${SILENTCMD}${RM} ${TEST_OBJ} ${TEST_BIN} pianobar_test

# Run tests with AddressSanitizer (memory errors, leaks) and UndefinedBehaviorSanitizer.
# Everything linked into the test binary is built with ASan, in its own build/asan.
# -fno-sanitize-recover=undefined makes any UB finding abort the run with a non-zero exit.
SANITIZE_FLAGS:=-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
ASAN_DIR:=build/asan
test-asan: locale-codegen
	${SILENTECHO} "   TEST  Building with AddressSanitizer + UndefinedBehaviorSanitizer..."
	${SILENTCMD}${MAKE} BUILD_DIR=${ASAN_DIR} ${ASAN_DIR}/pianobar_test CFLAGS="${CFLAGS} ${SANITIZE_FLAGS} -g" LDFLAGS="${LDFLAGS} ${SANITIZE_FLAGS}"
	${SILENTECHO} "   TEST  Running test suite with memory and undefined-behavior checks..."
	${SILENTCMD}if [ "$$(uname -s)" = Linux ]; then ASAN_OPTIONS="$${ASAN_OPTIONS:-detect_leaks=1}"; export ASAN_OPTIONS; fi; \
		UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1" ./scripts/test-parallel.sh ./${ASAN_DIR}/pianobar_test

clean-test-asan:
	${SILENTCMD}${RM} -r ${ASAN_DIR}

# Run tests with ThreadSanitizer, in build/tsan. Known third-party races are listed in test/tsan.supp.
TSAN_FLAGS:=-fsanitize=thread -fno-omit-frame-pointer
TSAN_DIR:=build/tsan
test-tsan: locale-codegen
	${SILENTECHO} "   TEST  Building with ThreadSanitizer..."
	${SILENTCMD}${MAKE} BUILD_DIR=${TSAN_DIR} ${TSAN_DIR}/pianobar_test CFLAGS="${CFLAGS} ${TSAN_FLAGS} -O1 -g" LDFLAGS="${LDFLAGS} ${TSAN_FLAGS}"
	${SILENTECHO} "   TEST  Running test suite with data-race checks..."
	${SILENTCMD}TSAN_OPTIONS="suppressions=test/tsan.supp:exitcode=66" ./scripts/test-parallel.sh ./${TSAN_DIR}/pianobar_test

# Run tests with valgrind (Linux only, optional)
test-valgrind: ${TEST_BIN}
	${SILENTECHO} "   TEST  Running test suite with valgrind..."
	${SILENTCMD}valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./${TEST_BIN}

# Pre-push gates at the same time from this tree (scripts/gates.sh): Docker
# (make test-ci-local), test-coverage, test-all, test-asan and test-tsan, at
# most GATES_JOBS (default 3) at once; a failed step is rerun alone once.
gates: locale-codegen
	${SILENTCMD}./scripts/gates.sh

.PHONY: install install-libpiano uninstall test test-par test-scripts test-integration test-e2e-fake test-startup-smoke test-ci-local test-all test-coverage coverage-clean lint lint-test test-clean test-asan clean-test-asan test-tsan test-valgrind gates minimal all locale-codegen clean distclean
