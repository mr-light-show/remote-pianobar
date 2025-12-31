# makefile of pianobar

PKG_CONFIG?=pkg-config
PREFIX:=/usr/local
BINDIR:=${PREFIX}/bin
LIBDIR:=${PREFIX}/lib
INCDIR:=${PREFIX}/include
MANDIR:=${PREFIX}/share/man
DYNLINK:=0
CFLAGS?=-O2 -DNDEBUG

ifeq (${CC},cc)
	OS := $(shell uname)
	ifeq (${OS},Darwin)
		CC:=gcc -std=c99
	else ifeq (${OS},FreeBSD)
		CC:=cc -std=c99
	else ifeq (${OS},OpenBSD)
		CC:=cc -std=c99
	else
		CC:=c99
	endif
endif

PIANOBAR_DIR:=src
PIANOBAR_SRC:=\
		${PIANOBAR_DIR}/main.c \
		${PIANOBAR_DIR}/debug.c \
		${PIANOBAR_DIR}/player.c \
		${PIANOBAR_DIR}/bar_state.c \
		${PIANOBAR_DIR}/playback_manager.c \
		${PIANOBAR_DIR}/settings.c \
		${PIANOBAR_DIR}/terminal.c \
		${PIANOBAR_DIR}/ui_act.c \
		${PIANOBAR_DIR}/ui.c \
		${PIANOBAR_DIR}/ui_readline.c \
		${PIANOBAR_DIR}/ui_dispatch.c \
		${PIANOBAR_DIR}/websocket_bridge.c \
		${PIANOBAR_DIR}/system_volume.c

# WebSocket support (conditional compilation)
WEBSOCKET?=0
WEBSOCKET_DIR:=src/websocket
ifeq ($(WEBSOCKET),1)
	PIANOBAR_SRC+=\
		${WEBSOCKET_DIR}/core/websocket.c \
		${WEBSOCKET_DIR}/core/queue.c \
		${WEBSOCKET_DIR}/http/http_server.c \
		${WEBSOCKET_DIR}/protocol/socketio.c \
		${WEBSOCKET_DIR}/protocol/error_messages.c \
		${WEBSOCKET_DIR}/daemon/daemon.c
endif

PIANOBAR_OBJ:=${PIANOBAR_SRC:.c=.o}

LIBPIANO_DIR:=src/libpiano
LIBPIANO_SRC:=\
		${LIBPIANO_DIR}/crypt.c \
		${LIBPIANO_DIR}/piano.c \
		${LIBPIANO_DIR}/request.c \
		${LIBPIANO_DIR}/response.c \
		${LIBPIANO_DIR}/list.c
LIBPIANO_OBJ:=${LIBPIANO_SRC:.c=.o}
LIBPIANO_RELOBJ:=${LIBPIANO_SRC:.c=.lo}
LIBPIANO_INCLUDE:=${LIBPIANO_DIR}

LIBAV_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libavcodec libavformat libavutil libavfilter)
LIBAV_LDFLAGS:=$(shell $(PKG_CONFIG) --libs libavcodec libavformat libavutil libavfilter)

LIBCURL_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libcurl)
LIBCURL_LDFLAGS:=$(shell $(PKG_CONFIG) --libs libcurl)

LIBGCRYPT_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libgcrypt)
LIBGCRYPT_LDFLAGS:=$(shell $(PKG_CONFIG) --libs libgcrypt)

LIBJSONC_CFLAGS:=$(shell $(PKG_CONFIG) --cflags json-c 2>/dev/null || $(PKG_CONFIG) --cflags json)
LIBJSONC_LDFLAGS:=$(shell $(PKG_CONFIG) --libs json-c 2>/dev/null || $(PKG_CONFIG) --libs json)

LIBAO_CFLAGS:=$(shell $(PKG_CONFIG) --cflags ao)
LIBAO_LDFLAGS:=$(shell $(PKG_CONFIG) --libs ao)

# System volume control - platform-specific
OS := $(shell uname)
ifeq (${OS},Darwin)
	# macOS - CoreAudio is always available
	SYSVOLUME_LDFLAGS:=-framework CoreAudio -framework AudioToolbox
else ifeq (${OS},Linux)
	# Linux - try PulseAudio library, fallback to CLI tools at runtime
	HAVE_PULSEAUDIO:=$(shell $(PKG_CONFIG) --exists libpulse && echo yes)
	ifeq (${HAVE_PULSEAUDIO},yes)
		SYSVOLUME_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libpulse) -DHAVE_PULSEAUDIO
		SYSVOLUME_LDFLAGS:=$(shell $(PKG_CONFIG) --libs libpulse)
	endif
endif

# WebSocket library flags (if enabled)
ifeq ($(WEBSOCKET),1)
	CFLAGS+=-DWEBSOCKET_ENABLED
	LIBWEBSOCKETS_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libwebsockets openssl)
	LIBWEBSOCKETS_LDFLAGS:=$(shell $(PKG_CONFIG) --libs libwebsockets openssl)
	CHECK_CFLAGS:=$(shell $(PKG_CONFIG) --cflags check)
	CHECK_LDFLAGS:=$(shell $(PKG_CONFIG) --libs check)
endif

# combine all flags
ALL_CFLAGS:=${CFLAGS} -I ${LIBPIANO_INCLUDE} \
			${LIBAV_CFLAGS} ${LIBCURL_CFLAGS} \
			${LIBGCRYPT_CFLAGS} ${LIBJSONC_CFLAGS} \
			${LIBAO_CFLAGS} ${SYSVOLUME_CFLAGS}
ALL_LDFLAGS:=${LDFLAGS} -lpthread -lm \
			${LIBAV_LDFLAGS} ${LIBCURL_LDFLAGS} \
			${LIBGCRYPT_LDFLAGS} ${LIBJSONC_LDFLAGS} \
			${LIBAO_LDFLAGS} ${SYSVOLUME_LDFLAGS}

# Add WebSocket flags if enabled
ifeq ($(WEBSOCKET),1)
	ALL_CFLAGS+=${LIBWEBSOCKETS_CFLAGS}
	ALL_LDFLAGS+=${LIBWEBSOCKETS_LDFLAGS}
endif

# Be verbose if V=1 (gnu autotools’ --disable-silent-rules)
SILENTCMD:=@
SILENTECHO:=@echo
ifeq (${V},1)
	SILENTCMD:=
	SILENTECHO:=@true
endif

# build pianobar
ifeq (${DYNLINK},1)
pianobar: ${PIANOBAR_OBJ} libpiano.so.0
	${SILENTECHO} "  LINK  $@"
	${SILENTCMD}${CC} -o $@ ${PIANOBAR_OBJ} -L. -lpiano ${ALL_LDFLAGS}
else
pianobar: ${PIANOBAR_OBJ} ${LIBPIANO_OBJ}
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


-include $(PIANOBAR_SRC:.c=.d)
-include $(LIBPIANO_SRC:.c=.d)

# Test-specific compilation rules (must come before general %.o: %.c rule)
ifeq ($(WEBSOCKET),1)
test/%.o: test/%.c
	${SILENTECHO} "    CC  $< (test)"
	${SILENTCMD}${CC} -c -o $@ ${ALL_CFLAGS} ${CHECK_CFLAGS} -MMD -MF $*.d -MP $<

test/unit/%.o: test/unit/%.c
	${SILENTECHO} "    CC  $< (test)"
	${SILENTCMD}${CC} -c -o $@ ${ALL_CFLAGS} ${CHECK_CFLAGS} -MMD -MF $*.d -MP $<
endif

# build standard object files
%.o: %.c
	${SILENTECHO} "    CC  $<"
	${SILENTCMD}${CC} -c -o $@ ${ALL_CFLAGS} -MMD -MF $*.d -MP $<

# create position independent code (for shared libraries)
%.lo: %.c
	${SILENTECHO} "    CC  $< (PIC)"
	${SILENTCMD}${CC} -c -fPIC -o $@ ${ALL_CFLAGS} -MMD -MF $*.d -MP $<

clean:
	${SILENTECHO} " CLEAN"
	${SILENTCMD}${RM} ${PIANOBAR_OBJ} ${LIBPIANO_OBJ} \
			${LIBPIANO_RELOBJ} pianobar libpiano.so* \
			libpiano.a $(PIANOBAR_SRC:.c=.d) $(LIBPIANO_SRC:.c=.d)

all: pianobar
	@echo "Build complete. Running tests..."
	@${MAKE} test

ifeq (${DYNLINK},1)
install: pianobar install-libpiano
else
install: pianobar
endif
	install -d ${DESTDIR}${BINDIR}/
	install -m755 pianobar ${DESTDIR}${BINDIR}/
	install -d ${DESTDIR}${MANDIR}/man1/
	install -m644 contrib/pianobar.1 ${DESTDIR}${MANDIR}/man1/

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

# Test suite (only available when WEBSOCKET=1)
ifeq ($(WEBSOCKET),1)
TEST_DIR:=test
TEST_SRC:=\
		${TEST_DIR}/test_main.c \
		${TEST_DIR}/unit/test_websocket.c \
		${TEST_DIR}/unit/test_http_server.c \
		${TEST_DIR}/unit/test_daemon.c \
		${TEST_DIR}/unit/test_socketio.c
TEST_OBJ:=${TEST_SRC:.c=.o}
TEST_BIN:=pianobar_test

# Build test suite (only link the modules being tested)
${TEST_BIN}: ${TEST_OBJ} src/debug.o src/bar_state.o src/playback_manager.o src/websocket_bridge.o src/ui.o src/ui_act.o src/ui_dispatch.o src/ui_readline.o src/terminal.o src/player.o src/settings.o src/system_volume.o ${WEBSOCKET_DIR}/core/websocket.o ${WEBSOCKET_DIR}/core/queue.o ${WEBSOCKET_DIR}/http/http_server.o ${WEBSOCKET_DIR}/protocol/socketio.o ${WEBSOCKET_DIR}/daemon/daemon.o ${LIBPIANO_OBJ}
	${SILENTECHO} "  LINK  $@"
	${SILENTCMD}${CC} -o $@ ${TEST_OBJ} src/debug.o src/bar_state.o src/playback_manager.o src/websocket_bridge.o src/ui.o src/ui_act.o src/ui_dispatch.o src/ui_readline.o src/terminal.o src/player.o src/settings.o src/system_volume.o ${WEBSOCKET_DIR}/core/websocket.o ${WEBSOCKET_DIR}/core/queue.o ${WEBSOCKET_DIR}/http/http_server.o ${WEBSOCKET_DIR}/protocol/socketio.o ${WEBSOCKET_DIR}/daemon/daemon.o ${LIBPIANO_OBJ} ${ALL_LDFLAGS} ${CHECK_LDFLAGS}

# Run tests
test: ${TEST_BIN}
	${SILENTECHO} "   TEST  Running test suite..."
	${SILENTCMD}./${TEST_BIN}

# Run tests with memory leak detection using AddressSanitizer
test-asan: clean-test-asan
	${SILENTECHO} "   TEST  Building with AddressSanitizer..."
	${SILENTCMD}${MAKE} ${TEST_BIN} CFLAGS="${CFLAGS} -fsanitize=address -fno-omit-frame-pointer -g" LDFLAGS="${LDFLAGS} -fsanitize=address"
	${SILENTECHO} "   TEST  Running test suite with memory leak detection..."
	${SILENTCMD}./${TEST_BIN}

clean-test-asan:
	${SILENTCMD}${RM} ${TEST_OBJ} ${TEST_BIN}

# Run tests with valgrind (Linux only, optional)
test-valgrind: ${TEST_BIN}
	${SILENTECHO} "   TEST  Running test suite with valgrind..."
	${SILENTCMD}valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./${TEST_BIN}

# Run static analysis (requires cppcheck)
lint:
	${SILENTECHO} "   LINT  Running static analysis..."
	${SILENTCMD}cppcheck --enable=all --suppress=missingIncludeSystem --suppress=unusedFunction \
		--inline-suppr --error-exitcode=1 \
		-I ${LIBPIANO_INCLUDE} ${PIANOBAR_DIR}/*.c ${LIBPIANO_DIR}/*.c

# Run linter on test files
lint-test:
	${SILENTECHO} "   LINT  Running static analysis on tests..."
	${SILENTCMD}cppcheck --enable=all --suppress=missingIncludeSystem \
		--inline-suppr --error-exitcode=1 \
		-I ${LIBPIANO_INCLUDE} -I ${PIANOBAR_DIR} ${TEST_DIR}/**/*.c ${TEST_DIR}/*.c

# Comprehensive test suite: unit tests + memory checks + linting
test-all: test lint test-asan
	${SILENTECHO} "   TEST  All tests passed!"

# Clean test files
test-clean:
	${SILENTECHO} " CLEAN  tests"
	${SILENTCMD}${RM} ${TEST_OBJ} ${TEST_BIN}
else
test:
	@echo "Tests are only available with WEBSOCKET=1"
	@echo "Run: make WEBSOCKET=1 test"
	@exit 1

test-asan:
	@echo "Tests are only available with WEBSOCKET=1"
	@echo "Run: make WEBSOCKET=1 test-asan"
	@exit 1

test-valgrind:
	@echo "Tests are only available with WEBSOCKET=1"
	@echo "Run: make WEBSOCKET=1 test-valgrind"
	@exit 1

test-all:
	@echo "Tests are only available with WEBSOCKET=1"
	@echo "Run: make WEBSOCKET=1 test-all"
	@exit 1

lint:
	${SILENTECHO} "   LINT  Running static analysis..."
	${SILENTCMD}cppcheck --enable=all --suppress=missingIncludeSystem --suppress=unusedFunction \
		--inline-suppr --error-exitcode=1 \
		-I ${LIBPIANO_INCLUDE} ${PIANOBAR_DIR}/*.c ${LIBPIANO_DIR}/*.c

lint-test:
	@echo "Tests are only available with WEBSOCKET=1"
	@exit 1

test-clean:
	@true

clean-test-asan:
	@true
endif

.PHONY: install install-libpiano uninstall test test-asan test-valgrind test-all lint lint-test test-clean clean-test-asan debug all
