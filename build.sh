#!/bin/bash

# Build script for pianobar with WebSocket support
# Usage: ./build.sh [debug]

set -e  # Exit on error

BUILD_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$BUILD_DIR"

# Check if debug mode is requested
if [ "$1" = "debug" ]; then
    echo "=== Building pianobar with WebSocket + Debug support ==="
    echo ""
    
    # Clean previous build
    echo "Cleaning previous build..."
    make clean WEBSOCKET=1
    echo ""
    
    # Build with debug flags
    echo "Building with debug symbols..."
    make WEBSOCKET=1 CFLAGS="-g -O0 -DWEBSOCKET_ENABLED"
    echo ""
    
    echo "✓ Debug build complete!"
    echo ""
    echo "Debug output enabled. Run with:"
    echo "  PIANOBAR_DEBUG=8 ./pianobar"
    echo ""
else
    echo "=== Building pianobar with WebSocket support ==="
    echo ""
    
    # Clean previous build
    echo "Cleaning previous build..."
    make clean WEBSOCKET=1
    echo ""
    
    # Build with optimization
    echo "Building optimized version..."
    make WEBSOCKET=1
    echo ""
    
    echo "✓ Production build complete!"
    echo ""
    echo "Run with: ./pianobar"
    echo ""
fi

# Build web UI
    echo "=== Building Web UI ==="
    echo ""
    cd webui
    
    if [ ! -d "node_modules" ]; then
        echo "Installing dependencies..."
        npm install
        echo ""
    fi
    
    echo "Building web UI..."
    npm run build
    echo ""
    
    cd "$BUILD_DIR"
    echo "✓ Web UI build complete!"
    echo ""

echo "=========================================="
echo "Build finished successfully!"
echo "=========================================="
echo ""
echo "Web interface will be available at:"
echo "  http://localhost:8080"
echo ""

# If debug mode, pause and ask to start pianobar
if [ "$1" = "debug" ]; then
    echo "Starting pianobar with debug output..."
    echo ""
    PIANOBAR_DEBUG=8 ./pianobar
else    
    echo "Starting pianobar..."
    echo ""
    ./pianobar
fi


# cd lldb ./pianobar -o "run" -o "bt" 2>&1 | head -100