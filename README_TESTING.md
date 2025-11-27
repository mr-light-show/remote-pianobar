# Pianobar WebSocket Test Suite

This document describes how to build, run, and extend the test suite for the pianobar WebSocket implementation.

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
make WEBSOCKET=1 test
```

### Comprehensive Testing

To run all tests including linting and memory leak detection:
```bash
make WEBSOCKET=1 test-all
```

This will:
1. Run unit tests
2. Run static analysis (cppcheck)
3. Rebuild with AddressSanitizer and run tests again

### Individual Test Targets

- `make WEBSOCKET=1 test` - Run unit tests only
- `make WEBSOCKET=1 lint` - Run static analysis on source code
- `make WEBSOCKET=1 lint-test` - Run static analysis on test code
- `make WEBSOCKET=1 test-asan` - Run tests with AddressSanitizer (memory leak detection)
- `make WEBSOCKET=1 test-valgrind` - Run tests with valgrind (Linux only)

### Cleaning Test Files

```bash
make WEBSOCKET=1 test-clean
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
├── integration/             # Integration tests (future)
│   └── test_client_server.c
└── fixtures/                # Test data files
    └── test_config.ini
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
make WEBSOCKET=1 test
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
make WEBSOCKET=1 test-asan
```

This will:
1. Rebuild with `-fsanitize=address` flags
2. Run tests with memory error detection
3. Report any memory leaks, use-after-free, or buffer overflows

### Valgrind (Linux only)

On Linux, you can also use Valgrind:

```bash
make WEBSOCKET=1 test-valgrind
```

## Continuous Integration

For CI/CD pipelines, use:

```bash
make WEBSOCKET=1 test-all
```

This runs the comprehensive test suite and exits with non-zero status on any failure.

## Troubleshooting

### Tests fail to compile

Make sure you have built pianobar with WebSocket support first:
```bash
make clean
make WEBSOCKET=1
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
7. **Run Before Commit**: Always run `make WEBSOCKET=1 test` before committing

## Future Enhancements

Planned additions to the test suite:
- Integration tests for client-server communication
- Mock Pandora API for testing player integration
- Performance benchmarks
- Fuzzing tests for robustness
- Coverage reporting (gcov/lcov)

