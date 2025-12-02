# Remote Pianobar Testing Guide

Comprehensive testing documentation for Remote Pianobar.

## Test Coverage Summary

| Category | Framework | Files | Tests | Status |
|----------|-----------|-------|-------|--------|
| C Unit Tests | Check | 1 | 12 | ✅ Passing |
| Socket.IO Protocol | Check | 1 | 12 | ✅ Passing |
| Web UI Components | Vitest | 5 | 36 | ✅ Passing |
| E2E Browser Tests | Playwright | 3 | 21 | ✅ 13 passing, 8 skipped* |

\* Skipped tests require a running pianobar instance

**Total Test Coverage:** 81 tests across C and TypeScript codebases

---

## Quick Start

```bash
# Run all tests
./build.sh --test

# Or run each suite individually:

# 1. C unit tests
make WEBSOCKET=1 test && ./pianobar_test

# 2. Web UI unit tests
cd webui && npm test -- --run

# 3. E2E browser tests
cd webui && npm run test:e2e
```

---

## C Unit Tests

### Setup

**Install Check framework:**

```bash
# macOS
brew install check

# Ubuntu/Debian
sudo apt install check

# Fedora
sudo dnf install check-devel
```

### Running Tests

```bash
# Build test binary
make clean WEBSOCKET=1
make WEBSOCKET=1 test

# Run all tests
./pianobar_test

# Run with verbose output
CK_VERBOSITY=verbose ./pianobar_test

# Run specific suite
./pianobar_test --suite socketio
```

### Test Files

**`test/unit/test_socketio.c`** - Socket.IO protocol tests (12 tests)
- Message formatting (connect, event, disconnect)
- Message parsing (ping/pong, events with data)
- Error handling (malformed JSON, invalid types)
- Buffer management

### Writing C Tests

```c
#include <check.h>
#include "../../src/websocket/protocol/socketio.h"

START_TEST(test_example) {
    // Arrange
    char buffer[256];
    
    // Act
    int result = BarSocketIOFormatMessage(buffer, sizeof(buffer), 
                                         SOCKETIO_EVENT, "test", "{}");
    
    // Assert
    ck_assert_int_gt(result, 0);
    ck_assert_str_eq(buffer, "2[\"test\",{}]");
}
END_TEST

Suite *example_suite(void) {
    Suite *s = suite_create("Example");
    TCase *tc = tcase_create("Core");
    
    tcase_add_test(tc, test_example);
    suite_add_tcase(s, tc);
    
    return s;
}
```

---

## Web UI Unit Tests (Vitest)

### Setup

```bash
cd webui
npm install
```

### Running Tests

```bash
# Watch mode (re-runs on file changes)
npm test

# Run once
npm test -- --run

# With UI
npm run test:ui

# Generate coverage report
npm run coverage
```

### Test Files

**Component Tests (36 tests total):**

1. **`test/unit/playback-controls.test.ts`** (8 tests)
   - Rendering play/pause button states
   - Event emission
   - Accessibility (button titles)
   - Removed previous button verification

2. **`test/unit/progress-bar.test.ts`** (6 tests)
   - Progress percentage calculation
   - Time formatting (MM:SS)
   - Zero state handling

3. **`test/unit/album-art.test.ts`** (5 tests)
   - Image display
   - Fallback disc icon
   - Dynamic src updates

4. **`test/unit/reconnect-button.test.ts`** (4 tests)
   - Button rendering
   - Pulsing animation
   - Event emission
   - Accessibility

5. **`test/unit/socket-service.test.ts`** (13 tests)
   - WebSocket connection lifecycle
   - Connection state notifications
   - Socket.IO message parsing/formatting
   - Event listener management
   - Reconnection logic
   - Error handling

### Writing Component Tests

```typescript
import { describe, it, expect } from 'vitest';
import { fixture, html } from '@open-wc/testing';
import '../src/components/my-component';
import type { MyComponent } from '../src/components/my-component';

describe('MyComponent', () => {
  it('renders correctly', async () => {
    const element: MyComponent = await fixture(html`
      <my-component prop="value"></my-component>
    `);

    expect(element).toBeTruthy();
    const content = element.shadowRoot!.querySelector('.content');
    expect(content).toBeTruthy();
  });

  it('handles events', async () => {
    const element: MyComponent = await fixture(html`
      <my-component></my-component>
    `);

    let eventFired = false;
    element.addEventListener('custom-event', () => {
      eventFired = true;
    });

    const button = element.shadowRoot!.querySelector('button');
    button!.click();

    expect(eventFired).toBe(true);
  });
});
```

---

## End-to-End Browser Tests (Playwright)

### Setup

```bash
cd webui
npm install

# Install Playwright browsers (first time only)
npx playwright install

# Install system dependencies
npx playwright install-deps
```

### Running Tests

```bash
# Run all E2E tests (headless)
npm run test:e2e

# Run with UI (interactive)
npm run test:e2e:ui

# Debug mode (step through tests)
npm run test:e2e:debug

# Run specific test file
npx playwright test test/e2e/connection.spec.ts

# Run specific browser
npx playwright test --project=chromium
npx playwright test --project=firefox
npx playwright test --project=webkit

# Generate HTML report
npx playwright show-report
```

### Test Files

**Connection Tests** (`test/e2e/connection.spec.ts` - 4 tests)
- Disconnected state display
- Reconnect button visibility and interaction
- Connection to live server (skipped without `PIANOBAR_RUNNING=1`)

**Playback Tests** (`test/e2e/playback.spec.ts` - 7 tests)
- Playback controls visibility
- Play/pause toggle
- Next track button
- Progress bar display
- Volume control
- *(Most require running server, skipped by default)*

**UI States Tests** (`test/e2e/ui-states.spec.ts` - 10 tests)
- Disconnected state UI
- Fallback album art
- Responsive design (mobile, tablet, desktop)
- Accessibility checks
- Track change updates *(requires running server)*

### Testing with Live Server

To run tests that require a live pianobar instance:

```bash
# Terminal 1: Start Remote Pianobar
./pianobar

# Terminal 2: Run E2E tests with server flag
cd webui
PIANOBAR_RUNNING=1 npm run test:e2e
```

### Writing E2E Tests

```typescript
import { test, expect } from '@playwright/test';

test.describe('Feature Name', () => {
  test('does something', async ({ page }) => {
    await page.goto('/');

    // Wait for element
    await expect(page.locator('my-component')).toBeVisible();

    // Interact with element
    await page.locator('button').click();

    // Assert result
    await expect(page.locator('.result')).toContainText('Success');
  });

  test.skip('requires server', async ({ page }) => {
    // This test needs pianobar running
    // Use test.skip to skip by default
    // Run with: PIANOBAR_RUNNING=1 npm run test:e2e
  });
});
```

---

## Continuous Integration

### GitHub Actions Example

```yaml
name: Tests

on: [push, pull_request]

jobs:
  c-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Install dependencies
        run: |
          sudo apt update
          sudo apt install -y libao-dev libmad0-dev libfaad-dev \
            libavcodec-dev libavformat-dev libavutil-dev libjson-c-dev \
            libgcrypt20-dev libcurl4-openssl-dev libwebsockets-dev check
      
      - name: Build and test
        run: |
          make clean WEBSOCKET=1
          make WEBSOCKET=1 test
          ./pianobar_test

  web-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Setup Node.js
        uses: actions/setup-node@v3
        with:
          node-version: '18'
      
      - name: Install dependencies
        run: cd webui && npm ci
      
      - name: Unit tests
        run: cd webui && npm test -- --run
      
      - name: E2E tests
        run: |
          cd webui
          npx playwright install --with-deps chromium
          npm run test:e2e -- --project=chromium
```

---

## Test Coverage Goals

### Current Coverage

- **C Code:**
  - Socket.IO protocol: ✅ Comprehensive
  - WebSocket core: ⚠️ Partial (integration tests needed)
  - HTTP server: ⚠️ Minimal
  - Daemon mode: ⚠️ Minimal

- **TypeScript:**
  - Components: ✅ Comprehensive (100% of components)
  - Services: ✅ Comprehensive (socket service fully tested)
  - Integration: ✅ E2E tests cover main flows

### Future Work

1. **C Integration Tests**
   - Full WebSocket server lifecycle
   - Multi-client scenarios
   - Message queue stress testing
   - HTTP static file serving

2. **Web UI Integration Tests**
   - Full playback flow with mock server
   - Station switching
   - Volume persistence

3. **Performance Tests**
   - WebSocket throughput
   - Browser memory usage
   - Multiple concurrent connections

---

## Debugging Tests

### C Tests

```bash
# Run under GDB
gdb ./pianobar_test
(gdb) run
(gdb) backtrace  # if it crashes

# Run under Valgrind
valgrind --leak-check=full ./pianobar_test
```

### Web UI Tests

```bash
# Vitest with browser debugging
npm test -- --inspect-brk

# Playwright with browser visible
npx playwright test --headed

# Playwright with slow motion
npx playwright test --slow-mo=1000

# Playwright debug mode (step through)
npm run test:e2e:debug
```

### Test Logs

```bash
# C tests with debug output
PIANOBAR_DEBUG=0xFFFFFFFF ./pianobar_test

# Vitest verbose
npm test -- --reporter=verbose

# Playwright with trace
npx playwright test --trace on
npx playwright show-trace trace.zip
```

---

## Best Practices

### Test Organization

1. **Follow AAA pattern:** Arrange, Act, Assert
2. **One assertion per test** (when possible)
3. **Descriptive test names** that explain what's being tested
4. **Use test.skip** for tests requiring external resources
5. **Clean up after tests** (no state leakage)

### Mocking

```typescript
// Mock WebSocket in unit tests
class MockWebSocket {
  // Implement minimal interface for testing
}
global.WebSocket = MockWebSocket as any;

// Use fixtures for consistent test data
const mockTrack = {
  title: 'Test Song',
  artist: 'Test Artist',
  albumArt: 'https://example.com/art.jpg'
};
```

### Async Testing

```typescript
// Wait for conditions
await expect(element).toBeVisible({ timeout: 5000 });

// Use waitFor for complex conditions
await page.waitForFunction(() => {
  return document.querySelector('.loaded') !== null;
});
```

---

## Troubleshooting

### Common Issues

**Issue: C tests fail to build**
```bash
# Check that Check framework is installed
pkg-config --modversion check

# Reinstall if needed
brew install check  # macOS
sudo apt install check  # Ubuntu
```

**Issue: Vitest can't find modules**
```bash
# Clear cache and reinstall
rm -rf node_modules package-lock.json
npm install
```

**Issue: Playwright tests timeout**
```bash
# Increase timeout in test
test('my test', async ({ page }) => {
  test.setTimeout(60000);  // 60 seconds
  // ...
});

# Or globally in playwright.config.ts
export default defineConfig({
  timeout: 60000,
});
```

**Issue: E2E tests can't connect**
```bash
# Check if preview server is running
lsof -i :4173

# Try rebuilding the web UI
cd webui && npm run build
```

---

## Resources

- [Check Framework Manual](https://libcheck.github.io/check/)
- [Vitest Documentation](https://vitest.dev/)
- [Playwright Documentation](https://playwright.dev/)
- [Testing Library](https://testing-library.com/)
- [Lit Testing](https://lit.dev/docs/tools/testing/)

---

## Contributing Tests

When adding new features:

1. **Write tests first** (TDD approach)
2. **Add unit tests** for individual functions/components
3. **Add integration tests** for feature workflows
4. **Update this documentation** with new test files
5. **Ensure CI passes** before submitting PR

See [DEVELOPMENT.md](DEVELOPMENT.md) for full development setup.

