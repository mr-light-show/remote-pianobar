# Remote Pianobar Test Suite

This document describes how to build, run, and extend the test suite for Remote Pianobar.

## Prerequisites

The test suite requires the following tools:
- **Check** - Unit testing framework for C
- **cppcheck** - Static analysis tool (optional, for linting)
- **clang/gcc** with AddressSanitizer support (for memory leak detection)

### Installing Prerequisites

On macOS with Homebrew:
```bash
brew install check cppcheck
```

On Debian/Ubuntu:
```bash
sudo apt-get install check cppcheck
```

## Running Tests

### Quick Test Run

To run the unit tests:
```bash
make test
```

### Comprehensive Testing

To run all tests plus static analysis:
```bash
make test-all
```

This runs `make test` (unit tests) and `make lint` (cppcheck). It does not run
the sanitizers; use `make test-asan` or `make test-tsan` for those.

Builds are serial by default. Pass `-jN` (for example
`make -j"$(getconf _NPROCESSORS_ONLN)" test-all`) to compile in parallel and to
overlap lint with the tests; the output of the two is then interleaved.

### Individual Test Targets

- `make test` - Run unit tests only
- `make test-par` - Run the same tests as `make test`, one suite per process, several at a time (see [Parallel test runs](#parallel-test-runs)); `make test-coverage`, `make test-asan` and `make test-tsan` run their tests this way
- `make test-integration` - Run integration tests (sets `PIANOBAR_INTEGRATION=1`)
- `make test-e2e-fake` - Run the web UI Playwright E2E suite against the real `pianobar` binary and a local fake Pandora API (no account; needs Python 3, openssl, Node and Playwright's Chromium); see [TESTING.md](TESTING.md#e2e-without-a-pandora-account-fake-pandora)
- `make test-startup-smoke` - Start the real `pianobar` as a web-only daemon against a closed Pandora port (no account, network or sound card; needs Python 3) and check that stdin stays on `/dev/null` (no `lws: ZERO RANDOM FD`) and the startup failure appears on stderr; not part of `make test-all`, run in the CI `c-tests` job
- `make gates` - Run the pre-push gates concurrently: Docker `test-ci-local` (coverage and `test-asan`), `test-coverage`, `test-all`, `test-asan`, `test-tsan` (see DEVELOPMENT.md, "All gates at once")
- `make lint` - Run static analysis on source code
- `make lint-test` - Run static analysis on test code
- `make test-asan` - Run tests with AddressSanitizer and UndefinedBehaviorSanitizer (memory errors and UB abort the run; on Linux LeakSanitizer is on too via `detect_leaks=1`, overridable with `ASAN_OPTIONS`, and runs in the CI `c-asan` job)
- `make test-tsan` - Run tests with ThreadSanitizer (known miniaudio races are suppressed in `test/tsan.supp`)
- `make test-valgrind` - Run tests with valgrind (Linux only)

### Parallel test runs

`make test-par` runs `scripts/test-parallel.sh ./pianobar_test`: every
`suite_create("...")` name under `test/` runs as its own
`CK_RUN_SUITE="<name>" ./pianobar_test` process, and `playback_integration`
(the longest suite) is split further into one process per tcase
(`CK_RUN_CASE`). The environment (`PIANOBAR_INTEGRATION`, `ASAN_OPTIONS`, ...)
is passed through unchanged.

- `TEST_JOBS=N` sets the worker count. The default is `min(4, ncpu/2)`,
  because the playback suites use real-time waits and can flake on an
  oversubscribed host. `TEST_JOBS=1` runs `./pianobar_test` once, unsharded,
  which is exactly the serial run.
- `TEST_SPLIT_SUITES="playback_integration Player"` splits more suites by tcase.
- Each shard's log is printed when it finishes, then the summed count:
  `test-parallel: Checks: <N> in <shards> shards`. It equals the `Checks:`
  count of a serial `./pianobar_test` run.
- Guards: the run fails if a shard exits non-zero, prints no `Checks:` line or
  reports `Checks: 0`, or if a `suite_create`/`tcase_create` argument is not a
  string literal. `pianobar_test` itself fails when `CK_RUN_SUITE` or
  `CK_RUN_CASE` matches no test, so a renamed or unregistered suite cannot be
  skipped silently.
- Coverage counters from the shards are merged by gcov at process exit
  (`-fprofile-update=atomic`); `coverage.info` totals equal the serial run's.
- `CK_XML_LOG_FILE_NAME` and `CK_LOG_FILE_NAME` are per process: with
  `make test-par` every shard would overwrite the same file. Use
  `./pianobar_test` (or `TEST_JOBS=1`) when you need one Check log.

Each variant builds in its own directory (`build/default`, `build/coverage`,
`build/asan`, `build/tsan`; see DEVELOPMENT.md, "Build directories"), so
running `make test-coverage` and then `make test-asan` keeps both builds, and
switching back rebuilds nothing.

### Cleaning Test Files

```bash
make test-clean
```

## Test Structure

The test suite is organized as follows:

```
test/
├── test_main.c              # Test runner (entry point)
├── unit/                    # Unit tests
│   ├── test_websocket.c     # WebSocket server tests
│   ├── test_http_server.c   # HTTP file serving tests
│   ├── test_daemon.c        # Daemonization tests
│   └── test_socketio.c      # Socket.IO protocol tests (future)
├── integration/             # Integration tests (opt-in)
│   ├── fixture_http.c       # Local HTTP server for audio fixtures
│   └── test_playback_integration.c
└── fixtures/                # Test data files
    ├── test_config.ini
    └── tone.mp3             # Short MP3 for player integration tests
```

## Current Test Coverage

### WebSocket Module (`test_websocket.c`)
- NULL parameter safety checks
- Initialization and cleanup
- Service loop handling
- Broadcast functions

### HTTP Server Module (`test_http_server.c`)
- MIME type detection for common file types:
  - HTML, CSS, JavaScript, JSON
  - PNG, SVG images
  - Unknown/default types
- NULL and edge case handling

### Daemon Module (`test_daemon.c`)
- NULL parameter safety checks
- Daemonization process
- PID file operations
- Process detection

**Total Test Cases**: 19

### Integration tests (`test/integration/`)

Heavy tests for `playback_lifecycle.c` and `player.c` that exercise real HTTP audio
decoding and mocked Pandora playlist fetch. They are **opt-in** locally:

```bash
make test-integration
# or
PIANOBAR_INTEGRATION=1 ./pianobar_test
```

CI runs them during `make test-coverage` (`PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1`).

Requirements:
- `test/fixtures/tone.mp3` (committed; regenerate with ffmpeg if needed). It is a deliberately near-silent 440 Hz sine (peak about -60 dBFS, amplitude 0.001) so a local run on speakers does not beep. It is not pure digital silence because `test_player.c` asserts decoded samples exceed `0.00001f`. Regenerate with:
  ```sh
  ffmpeg -f lavfi -i "aevalsrc=0.001*sin(2*PI*440*t):s=44100:d=0.5:c=mono" \
    -c:a libmp3lame -b:a 64k -ar 44100 -ac 1 test/fixtures/tone.mp3
  ```
  Keep duration (0.5 s), rate (44100 Hz), mono, and 64 kb/s so timing-based tests keep their assumptions. Verify with `ffmpeg -i test/fixtures/tone.mp3 -af astats -f null -`: the peak should be well above 0.00001 (about -100 dBFS) and below about -55 dBFS.
- Headless CI uses `PIANOBAR_TEST_NO_DEVICE=1` (miniaudio `noDevice`); without it, audio tests skip if the engine cannot init
- `BarSettingsRead()`-style defaults (partner keys, station format) for lifecycle/manager paths

#### Null audio backend (real device thread, no hardware)

`PIANOBAR_TEST_NO_DEVICE=1` replaces the device with test-driven engine reads, so
the device thread, the decoder/device handoff and their locks never run. The
`null_audio_device` test case instead opens the lifetime engine on miniaudio's
**null backend** (`ma_backend_null`): a real device thread pulls the data callback
in real time without audio hardware. It covers natural song end, pause/resume,
volume changes, skip mid-song, concurrent controls, repeated start/stop and
engine init/destroy, and reruns the device-aware integration tests on that device.

- Tests select it with `BarPlayerSetNullAudioTestBackend (true)` (a checked fixture,
  so it is set in the forked test process before `BarPlayerInit`); this takes
  precedence over `PIANOBAR_TEST_NO_DEVICE`. Setting `PIANOBAR_TEST_NULL_AUDIO=1`
  does the same for a whole run.
- It never falls back to a hardware backend. If the null backend is unavailable
  the engine stays uninitialized and each test prints
  `SKIP <test>: miniaudio null audio backend unavailable in this build`.
- Each test that ran prints `null-audio: <test> ran on the null device backend`.
- Requires `PIANOBAR_INTEGRATION=1`; it runs in CI and in `make test-ci-local`.
  Run it alone with
  `CK_RUN_SUITE=playback_integration CK_RUN_CASE=null_audio_device PIANOBAR_INTEGRATION=1 ./pianobar_test`.
- Production default is unchanged.

Production code exposes `BarUiPianoCallSetTestHook()` / `BarUiPianoCallClearTestHook()`
so playlist fetch can be mocked without network access.

Coverage includes lifecycle failure paths (empty playlist, session error, generic failure),
player error/interrupt paths, two-song manual advance, and playback-manager end-to-end.

## Code coverage and build variants

Codecov patch coverage reflects **only what CI uploads** — it does **not** merge runs from different `make` invocations automatically.

| Target / env | What runs | Uploaded to Codecov? | Notes |
|--------------|-----------|----------------------|-------|
| `make test-coverage` | Full WebSocket test binary + integration + headless audio | Yes (`c-tests` flag) | Matches GitHub Actions `test.yml`; always use this before judging patch % |
| `make test` | Unit tests only (no integration) | No | Fast dev loop; lower coverage than CI |
| `PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1 make test-all` | Same tests as CI except no `--coverage` | No | Pre-push CI parity when `Makefile` / `test/**` changed |
| `make test-asan` | ASan + UBSan rebuild + unit tests | No | Memory and undefined-behavior checking |
| `make test-tsan` | TSan rebuild + unit tests | No | Data races; suppressions in `test/tsan.supp` |
| `make test-e2e-fake` | Playwright E2E (Chromium) against real `pianobar` + `test/e2e-fake-pandora/fake_pandora.py` | No | Not part of `make test-all`; exercises the web UI end to end, no coverage data |
| `cd webui && npm test -- --run --coverage` | Vitest | Yes (`web-tests` flag) | Separate upload; combined with `c-tests` for default patch gate |

**Implication:** production lines exercised only when integration env vars are set, can show as uncovered on the PR even if another local run hit them. The authoritative patch number is the CI run on `make test-coverage` with `PIANOBAR_INTEGRATION=1` and `PIANOBAR_TEST_NO_DEVICE=1`.

The `codecov/patch/c-tests` gate scopes to **`src/**/*.c` only** — changed headers (`*.h`) and docs (`*.md`) in `src/` are excluded from the patch denominator because they are not executable.

Unit tests in `test/unit/test_player.c` also exercise `BarPlayerThread` error paths (invalid URL, connection refused, non-audio file) without requiring `PIANOBAR_INTEGRATION=1`.

## Adding New Tests

### 1. Create a New Test File

Create a new file in `test/unit/` following this template:

```c
#include <check.h>
#include "../../src/your_module.h"

START_TEST(test_your_function) {
    // Test code here
    ck_assert_int_eq(expected, actual);
}
END_TEST

Suite *your_module_suite(void) {
    Suite *s;
    TCase *tc_core;
    
    s = suite_create("YourModule");
    tc_core = tcase_create("Core");
    
    tcase_add_test(tc_core, test_your_function);
    
    suite_add_tcase(s, tc_core);
    
    return s;
}
```

### 2. Update test_main.c

Add your suite to the test runner:

```c
// Add declaration at top
Suite *your_module_suite(void);

// Add to main()
srunner_add_suite(sr, your_module_suite());
```

### 3. Update Makefile

Add your test file to `TEST_SRC`:

```makefile
TEST_SRC:=\
    ${TEST_DIR}/test_main.c \
    ${TEST_DIR}/unit/test_websocket.c \
    ${TEST_DIR}/unit/test_http_server.c \
    ${TEST_DIR}/unit/test_daemon.c \
    ${TEST_DIR}/unit/test_your_module.c
```

### 4. Run the Tests

```bash
make test
```

## Check Framework Assertions

Common assertions you can use:
- `ck_assert(condition)` - Assert condition is true
- `ck_assert_int_eq(expected, actual)` - Assert integers are equal
- `ck_assert_int_ne(expected, actual)` - Assert integers are not equal
- `ck_assert_str_eq(expected, actual)` - Assert strings are equal
- `ck_assert_ptr_null(pointer)` - Assert pointer is NULL
- `ck_assert_ptr_nonnull(pointer)` - Assert pointer is not NULL

For a complete list, see: https://libcheck.github.io/check/doc/check_html/check_4.html

## Memory Leak Detection

### AddressSanitizer (Cross-platform)

AddressSanitizer is built into modern compilers and works on macOS, Linux, and other platforms:

```bash
make test-asan
```

This will:
1. Rebuild with `-fsanitize=address` flags
2. Run tests with memory error detection
3. Report any memory leaks, use-after-free, or buffer overflows

### Valgrind (Linux only)

On Linux, you can also use Valgrind:

```bash
make test-valgrind
```

## Continuous Integration

For CI/CD pipelines, use:

```bash
make test-all
```

This runs the comprehensive test suite and exits with non-zero status on any failure.

## Troubleshooting

### Tests fail to compile

Make sure you have built Remote Pianobar first:
```bash
make clean
make
```

### "Check library not found"

Install the Check framework:
```bash
brew install check  # macOS
# or
sudo apt-get install check  # Debian/Ubuntu
```

### Linker errors about missing symbols

The test suite only links the modules under test. If you're testing a module
that depends on other modules, you may need to update the `TEST_BIN` target
in the Makefile to include those dependencies.

## Best Practices

1. **Test NULL Safety**: Always test that functions handle NULL parameters gracefully
2. **Test Edge Cases**: Test boundary conditions (empty strings, zero values, max values)
3. **Keep Tests Fast**: Unit tests should run in < 5 seconds total
4. **Test One Thing**: Each test should verify one specific behavior
5. **Use Descriptive Names**: Test names should clearly indicate what they test
6. **Clean Up**: Tests should not leave temporary files or state
7. **Run Before Commit**: Always run `make test` before committing

## Future Enhancements

Planned additions to the test suite:
- Integration tests for client-server communication
- Mock Pandora API for testing player integration
- Performance benchmarks
- Fuzzing tests for robustness
- Coverage reporting (gcov/lcov)

