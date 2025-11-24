# Pianobar Development Guide

This guide covers setting up your development environment for contributing to Pianobar with WebSocket support.

## Table of Contents

1. [System Requirements](#system-requirements)
2. [Installing Dependencies](#installing-dependencies)
3. [Building Pianobar](#building-pianobar)
4. [Running Tests](#running-tests)
5. [Development Workflow](#development-workflow)
6. [Project Structure](#project-structure)
7. [Debugging](#debugging)

---

## System Requirements

### Operating System
- macOS 10.15+ or Linux (Ubuntu 20.04+, Debian 11+, Fedora 35+)
- Windows via WSL2

### Required Tools
- C compiler (gcc or clang)
- Make
- pkg-config
- Git
- Node.js 18+ and npm 9+

### Required Libraries
- libao
- libmad or libmpg123
- libfaad
- libavcodec, libavformat, libavutil (ffmpeg)
- libjson-c
- libgcrypt
- libcurl
- libwebsockets (2.4+)

---

## Installing Dependencies

### macOS

```bash
# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install C development tools and libraries
brew install \
  libao \
  mad \
  faad2 \
  ffmpeg \
  json-c \
  libgcrypt \
  curl \
  libwebsockets \
  pkg-config

# Install Node.js and npm
brew install node

# Verify installations
gcc --version
make --version
node --version
npm --version
pkg-config --version
```

### Ubuntu/Debian

```bash
# Update package list
sudo apt update

# Install build tools
sudo apt install -y \
  build-essential \
  pkg-config \
  git

# Install required libraries
sudo apt install -y \
  libao-dev \
  libmad0-dev \
  libfaad-dev \
  libavcodec-dev \
  libavformat-dev \
  libavutil-dev \
  libjson-c-dev \
  libgcrypt20-dev \
  libcurl4-openssl-dev \
  libwebsockets-dev

# Install Node.js 18+ (Ubuntu/Debian may have older versions)
curl -fsSL https://deb.nodesource.com/setup_18.x | sudo -E bash -
sudo apt install -y nodejs

# Verify installations
gcc --version
make --version
node --version
npm --version
pkg-config --version
```

### Fedora/RHEL

```bash
# Install development tools
sudo dnf groupinstall "Development Tools"
sudo dnf install -y pkg-config git

# Install required libraries
sudo dnf install -y \
  libao-devel \
  libmad-devel \
  faad2-devel \
  ffmpeg-devel \
  json-c-devel \
  libgcrypt-devel \
  libcurl-devel \
  libwebsockets-devel

# Install Node.js
sudo dnf install -y nodejs npm

# Verify installations
gcc --version
make --version
node --version
npm --version
pkg-config --version
```

---

## Building Pianobar

### Basic Build (CLI only)

```bash
# Clone the repository
git clone https://github.com/yourusername/pianobar.git
cd pianobar

# Build without WebSocket support
make clean
make

# Run
./pianobar
```

### WebSocket-Enabled Build

```bash
# Clean previous builds
make clean WEBSOCKET=1

# Build with WebSocket support and debug symbols
make WEBSOCKET=1 CFLAGS="-g -O0 -DWEBSOCKET_ENABLED"

# The binary will be created as ./pianobar
```

### Build with Debug Logging

```bash
# Build with all debug flags enabled
make clean WEBSOCKET=1
make WEBSOCKET=1 CFLAGS="-g -O0 -DWEBSOCKET_ENABLED -DPIANOBAR_DEBUG=0xFFFFFFFF"
```

Available debug flags (defined in `src/debug.h`):
- `PIANOBAR_DEBUG_WS` - WebSocket operations
- `PIANOBAR_DEBUG_HTTP` - HTTP server
- `PIANOBAR_DEBUG_SOCKETIO` - Socket.IO protocol
- `PIANOBAR_DEBUG_DAEMON` - Daemon operations
- `PIANOBAR_DEBUG_BROADCAST` - Broadcast messages
- `PIANOBAR_DEBUG_COMMAND` - Command queue

### Building the Web UI

```bash
# Navigate to web UI directory
cd webui

# Install dependencies (first time only)
npm install

# Development build (with hot reload)
npm run dev
# Opens on http://localhost:3000 with proxy to WebSocket server

# Production build
npm run build
# Outputs to ../dist/webui/
```

### Unified Build Script

Use the provided build script to build both backend and frontend:

```bash
# Build everything
./build.sh

# Build with debug flags
./build.sh --debug

# Clean build
./build.sh --clean
```

---

## Running Tests

### C Unit Tests

The C codebase uses the Check framework for unit testing.

```bash
# Install Check framework
# macOS:
brew install check

# Ubuntu/Debian:
sudo apt install check

# Fedora:
sudo dnf install check-devel

# Build test binary
make clean WEBSOCKET=1
make WEBSOCKET=1 test

# Run tests
./pianobar_test

# Run with verbose output
./pianobar_test --verbose
```

### TypeScript/JavaScript Unit Tests

The web UI uses Vitest for unit testing.

```bash
cd webui

# Install dependencies (if not done already)
npm install

# Run unit tests (watch mode)
npm test

# Run tests once
npm test -- --run

# Run with UI
npm run test:ui

# Generate coverage report
npm run coverage
```

### End-to-End Tests

E2E tests use Playwright to test the full application.

```bash
cd webui

# Install Playwright (first time only)
npx playwright install

# Install system dependencies for Playwright browsers
npx playwright install-deps

# Run E2E tests (headless)
npm run test:e2e

# Run with UI (interactive)
npm run test:e2e:ui

# Debug mode (step through tests)
npm run test:e2e:debug

# Run specific test file
npx playwright test test/e2e/connection.spec.ts

# Run tests with pianobar server running
PIANOBAR_RUNNING=1 npm run test:e2e
```

**Note:** Most E2E tests require a running pianobar instance with WebSocket enabled. Tests without this requirement run by default; others are skipped.

### Running All Tests

```bash
# From project root
./build.sh --test

# Or manually:
make WEBSOCKET=1 test && \
  ./pianobar_test && \
  cd webui && \
  npm test -- --run && \
  npm run test:e2e
```

---

## Development Workflow

### 1. Setting Up Configuration

Create a config file for testing:

```bash
# Copy example config
mkdir -p ~/.config/pianobar
cp contrib/config-example ~/.config/pianobar/config

# Edit with your Pandora credentials
vim ~/.config/pianobar/config
```

Example WebSocket configuration:

```ini
# ~/.config/pianobar/config

# Your Pandora credentials
user = your_email@example.com
password = your_password

# WebSocket settings
ui_mode = both          # Options: cli, web, both (default: both)
websocket_port = 8080
websocket_host = 0.0.0.0
webui_enabled = 1
webui_path = ./dist/webui

# Optional: Logging
log_file = /tmp/pianobar.log
```

### 2. Development Mode (Hot Reload)

**Terminal 1:** Run Pianobar with WebSocket

```bash
# Build with debug symbols
make clean WEBSOCKET=1
make WEBSOCKET=1 CFLAGS="-g -O0 -DWEBSOCKET_ENABLED"

# Run pianobar
./pianobar
```

**Terminal 2:** Run Web UI Dev Server

```bash
cd webui
npm run dev
# Opens http://localhost:3000
# Proxies WebSocket to http://localhost:8080
```

The dev server will automatically reload when you make changes to TypeScript/CSS files.

### 3. Making Changes

#### C Code Changes

1. Edit files in `src/` or `src/websocket/`
2. Rebuild: `make WEBSOCKET=1`
3. Restart pianobar
4. Add/update tests in `test/unit/`

#### Web UI Changes

1. Edit files in `webui/src/`
2. Changes auto-reload in dev server
3. Add/update tests in `webui/test/unit/` or `webui/test/e2e/`
4. Run tests: `npm test`

### 4. Testing Your Changes

```bash
# Run C tests
make WEBSOCKET=1 test && ./pianobar_test

# Run web UI tests
cd webui
npm test -- --run
npm run test:e2e

# Manual testing
# Start pianobar and open http://localhost:8080
```

### 5. Building Production Release

```bash
# Clean and build everything
./build.sh --clean

# Test the production build
./pianobar

# Web UI is served from dist/webui/
```

---

## Project Structure

```
pianobar/
├── src/                      # C source code
│   ├── main.c               # Main application
│   ├── player.c             # Audio playback
│   ├── ui*.c                # User interface
│   ├── settings.c           # Configuration
│   ├── debug.c              # Debug logging
│   ├── libpiano/            # Pandora API client
│   └── websocket/           # WebSocket implementation
│       ├── core/            # WebSocket server core
│       │   ├── websocket.c  # Main WebSocket logic
│       │   └── queue.c      # Message queue
│       ├── http/            # HTTP static file server
│       │   └── http_server.c
│       ├── protocol/        # Socket.IO protocol
│       │   └── socketio.c
│       └── daemon/          # Daemon mode
│           └── daemon.c
├── webui/                   # Web UI (TypeScript + Lit)
│   ├── src/
│   │   ├── app.ts           # Main application
│   │   ├── components/      # UI components
│   │   │   ├── album-art.ts
│   │   │   ├── playback-controls.ts
│   │   │   ├── progress-bar.ts
│   │   │   ├── volume-control.ts
│   │   │   └── reconnect-button.ts
│   │   ├── services/        # Business logic
│   │   │   └── socket-service.ts
│   │   └── styles/          # CSS
│   ├── test/
│   │   ├── unit/            # Component unit tests
│   │   └── e2e/             # End-to-end tests
│   ├── package.json
│   ├── vite.config.js       # Build config
│   └── playwright.config.ts # E2E test config
├── test/                    # C unit tests
│   ├── unit/
│   │   ├── test_websocket.c
│   │   ├── test_socketio.c
│   │   ├── test_http_server.c
│   │   └── test_daemon.c
│   └── fixtures/            # Test data
├── dist/                    # Build output
│   └── webui/              # Production web UI
├── contrib/                 # Example configs and scripts
├── Makefile                 # Build system
├── build.sh                 # Unified build script
└── README.rst              # User documentation
```

---

## Debugging

### Debugging C Code with GDB

```bash
# Build with debug symbols
make clean WEBSOCKET=1
make WEBSOCKET=1 CFLAGS="-g -O0 -DWEBSOCKET_ENABLED"

# Run with GDB
gdb ./pianobar

# GDB commands:
(gdb) break main
(gdb) run
(gdb) next
(gdb) print variable_name
(gdb) backtrace
```

### Debugging C Code with LLDB (macOS)

```bash
# Build with debug symbols
make clean WEBSOCKET=1
make WEBSOCKET=1 CFLAGS="-g -O0 -DWEBSOCKET_ENABLED"

# Run with LLDB
lldb ./pianobar

# LLDB commands:
(lldb) breakpoint set --name main
(lldb) run
(lldb) next
(lldb) print variable_name
(lldb) bt
```

### Debug Logging

Enable debug output for specific subsystems:

```bash
# Build with all debug flags
make WEBSOCKET=1 CFLAGS="-g -O0 -DWEBSOCKET_ENABLED -DPIANOBAR_DEBUG=0xFFFFFFFF"

# Or selective flags:
# WebSocket only
make WEBSOCKET=1 CFLAGS="-g -O0 -DWEBSOCKET_ENABLED -DPIANOBAR_DEBUG_WS"

# Run and redirect output
./pianobar 2>&1 | tee debug.log
```

See `WEBSOCKET_DEBUG.md` for detailed debug flag documentation.

### Debugging TypeScript/Web UI

```bash
# In webui directory
npm run dev

# Open browser DevTools (F12)
# Source maps are enabled automatically

# Or use VS Code debugger:
# 1. Set breakpoints in .ts files
# 2. Press F5 or use Debug panel
# 3. Select "Chrome" or "Firefox" configuration
```

### Browser Developer Tools

1. Open DevTools (F12 or Cmd+Option+I)
2. **Console tab**: View logs, errors
3. **Network tab**: Monitor WebSocket messages
   - Filter by "WS" to see WebSocket traffic
   - Click connection to see frames
4. **Sources tab**: Debug TypeScript with source maps
5. **Application tab**: Inspect storage, cache

### Testing WebSocket Protocol

Use the test HTML page for manual protocol testing:

```bash
# Start pianobar with WebSocket
./pianobar

# Open test page in browser
open test/socketio_test.html

# Or use curl for raw WebSocket testing
curl -i -N \
  -H "Connection: Upgrade" \
  -H "Upgrade: websocket" \
  -H "Sec-WebSocket-Key: test" \
  -H "Sec-WebSocket-Protocol: socketio" \
  http://localhost:8080/socket.io
```

### Memory Leak Detection (Valgrind)

```bash
# Install valgrind
# macOS: Not officially supported on recent versions
# Linux:
sudo apt install valgrind  # Ubuntu/Debian
sudo dnf install valgrind  # Fedora

# Run with valgrind
valgrind --leak-check=full \
  --show-leak-kinds=all \
  --track-origins=yes \
  --verbose \
  ./pianobar
```

---

## Common Issues

### Issue: `libwebsockets not found`

```bash
# macOS
brew install libwebsockets

# Ubuntu/Debian
sudo apt install libwebsockets-dev

# Verify
pkg-config --modversion libwebsockets
```

### Issue: `npm install` fails

```bash
# Clear npm cache
npm cache clean --force

# Remove node_modules and package-lock.json
rm -rf node_modules package-lock.json

# Reinstall
npm install
```

### Issue: Playwright browsers not installing

```bash
# Install system dependencies
npx playwright install-deps

# Install browsers
npx playwright install
```

### Issue: Port 8080 already in use

```bash
# Find process using port 8080
lsof -i :8080  # macOS/Linux
netstat -ano | findstr :8080  # Windows

# Kill the process or change port in config:
# ~/.config/pianobar/config
websocket_port = 8081
```

### Issue: WebSocket connection fails

1. Check pianobar is running with WebSocket enabled
2. Check firewall settings
3. Verify `ui_mode` in config (should be `web` or `both`)
4. Check logs: `tail -f /tmp/pianobar.log`

---

## Contributing

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make your changes
4. Add/update tests
5. Run all tests: `./build.sh --test`
6. Commit with clear messages
7. Push and create a Pull Request

### Code Style

**C Code:**
- Follow existing style (K&R-inspired)
- Use tabs for indentation
- Keep functions focused and small
- Comment complex logic

**TypeScript/Web UI:**
- Follow existing style (ESLint configuration)
- Use 2 spaces for indentation
- Prefer arrow functions
- Use TypeScript types

---

## Additional Resources

- [WebSocket Plans](WEBSOCKET_WEB_INTERFACE_PLAN.md)
- [Threading Documentation](WEBSOCKET_THREADING_PLAN.md)
- [Debug Flags Reference](WEBSOCKET_DEBUG.md)
- [Home Assistant Integration](WEBSOCKET_HOMEASSISTANT_PLAN.md)
- [Testing Documentation](README_TESTING.md)

---

## Getting Help

- **Issues**: GitHub Issues
- **Discussions**: GitHub Discussions
- **IRC**: #pianobar on Libera.Chat
- **Documentation**: README.rst, man page (contrib/pianobar.1)

