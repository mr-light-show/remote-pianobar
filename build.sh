#!/bin/bash

# Build script for pianobar with WebSocket support
# Usage: ./build.sh [debug]

set -e  # Exit on error

BUILD_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$BUILD_DIR"

# Function to run pianobar with crash capture only
run_with_crash_capture() {
    local timestamp=$(date +%Y%m%d-%H%M%S)
    local crash_file="pianobar-crash-${timestamp}.log"
    
    echo "Running pianobar with debug output..."
    echo "  (Crash info will be saved if crash occurs)"
    echo ""
    
    # Enable core dumps for crash analysis
    ulimit -c unlimited
    
    # Run pianobar with debug output - normal CLI interaction
    PIANOBAR_DEBUG=8 ./pianobar
    local exit_code=$?
    
    # Check if pianobar crashed (abnormal exit)
    if [ $exit_code -ne 0 ] && [ $exit_code -ne 1 ]; then
        echo ""
        echo "=== CRASH DETECTED (exit code: $exit_code) ==="
        
        # Check for core dump
        if ls core* 2>/dev/null | grep -q .; then
            echo "Extracting backtrace from core dump..."
            lldb -c core* -b -o "thread backtrace all" -o "quit" ./pianobar > "$crash_file" 2>&1
            echo "Crash backtrace saved to: $crash_file"
            echo "Core dump available for analysis"
        else
            echo "No core dump generated. Consider checking system crash logs:"
            echo "  ~/Library/Logs/DiagnosticReports/"
            
            # Try to find recent crash logs
            if ls ~/Library/Logs/DiagnosticReports/pianobar*.crash 2>/dev/null | tail -1 | grep -q .; then
                local latest_crash=$(ls -t ~/Library/Logs/DiagnosticReports/pianobar*.crash 2>/dev/null | head -1)
                echo "Recent crash log found: $latest_crash"
                cp "$latest_crash" "$crash_file"
                echo "Crash info saved to: $crash_file"
            fi
        fi
    fi
}

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

# If debug mode, run with crash capture
if [ "$1" = "debug" ]; then
    echo "Starting pianobar with debug output and crash capture..."
    echo ""
    run_with_crash_capture
else    
    echo "Starting pianobar..."
    echo ""
    ./pianobar
fi