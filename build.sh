#!/bin/bash

# Build script for Remote Pianobar
# Usage: ./build.sh [debug]

set -e  # Exit on error

BUILD_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$BUILD_DIR"

# Get webui_path from pianobar config
get_webui_path() {
    local config_file=""
    
    # Check for config in standard locations
    if [ -f "$HOME/.config/pianobar/config" ]; then
        config_file="$HOME/.config/pianobar/config"
    elif [ -f "$HOME/.pianobar/config" ]; then
        config_file="$HOME/.pianobar/config"
    fi
    
    if [ -z "$config_file" ]; then
        return 1
    fi
    
    # Extract webui_path, strip whitespace and comments
    local webui_path=$(grep -E "^[[:space:]]*webui_path[[:space:]]*=" "$config_file" | \
                       sed 's/^[[:space:]]*webui_path[[:space:]]*=[[:space:]]*//;s/[[:space:]]*#.*$//' | \
                       tr -d '\r')
    
    if [ -n "$webui_path" ]; then
        # Expand ~ to home directory
        webui_path="${webui_path/#\~/$HOME}"
        echo "$webui_path"
        return 0
    fi
    
    return 1
}

# Get log_file path from pianobar config
get_log_file() {
    local config_file=""
    
    # Check for config in standard locations
    if [ -f "$HOME/.config/pianobar/config" ]; then
        config_file="$HOME/.config/pianobar/config"
    elif [ -f "$HOME/.pianobar/config" ]; then
        config_file="$HOME/.pianobar/config"
    fi
    
    if [ -z "$config_file" ]; then
        return 1
    fi
    
    # Extract log_file, strip whitespace and comments
    local log_file=$(grep -E "^[[:space:]]*log_file[[:space:]]*=" "$config_file" | \
                     sed 's/^[[:space:]]*log_file[[:space:]]*=[[:space:]]*//;s/[[:space:]]*#.*$//' | \
                     tr -d '\r')
    
    if [ -n "$log_file" ]; then
        # Expand ~ to home directory
        log_file="${log_file/#\~/$HOME}"
        echo "$log_file"
        return 0
    fi
    
    return 1
}


# Copy WebUI to configured location
deploy_webui() {
    echo "=== Deploying Web UI ==="
    echo ""
    
    if [ ! -d "dist/webui" ]; then
        echo "Warning: dist/webui not found, skipping deployment"
        return
    fi
    
    local webui_path=$(get_webui_path)
    
    if [ -z "$webui_path" ]; then
        echo "No webui_path configured in pianobar config"
        echo "WebUI files are in: dist/webui/"
        return
    fi
    
    echo "Found webui_path: $webui_path"
    
    # Create target directory if it doesn't exist
    mkdir -p "$webui_path"
    
    # Use rsync if available for efficiency, otherwise cp
    if command -v rsync &> /dev/null; then
        echo "Syncing files to $webui_path..."
        rsync -av --delete dist/webui/ "$webui_path/"
    else
        echo "Copying files to $webui_path..."
        cp -r dist/webui/* "$webui_path/"
    fi
    
    echo "✓ WebUI deployed to: $webui_path"
    echo ""
}

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
    # Note: pianobar daemonizes, so parent exits immediately
    PIANOBAR_DEBUG=8 ./pianobar
    local exit_code=$?
    
    # Wait a moment for crash reports to be generated (macOS ReportCrash is async)
    sleep 2
    
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
            echo "No core dump generated. Checking system crash logs..."
            echo "  ~/Library/Logs/DiagnosticReports/"
            
            # Try to find recent crash logs
            if ls ~/Library/Logs/DiagnosticReports/pianobar*.crash 2>/dev/null | tail -1 | grep -q .; then
                local latest_crash=$(ls -t ~/Library/Logs/DiagnosticReports/pianobar*.crash 2>/dev/null | head -1)
                echo "Recent crash log found: $latest_crash"
                cp "$latest_crash" "$crash_file"
                echo "Crash info saved to: $crash_file"
                echo ""
                echo "=== Key Crash Information ==="
                echo "Exception Type:"
                grep -A 2 "Exception Type:" "$latest_crash" | head -3 || echo "Not found"
                echo ""
                echo "Crashed Thread Backtrace:"
                THREAD_NUM=$(grep "Crashed Thread:" "$latest_crash" 2>/dev/null | sed 's/.*Crashed Thread: *\([0-9]*\).*/\1/')
                if [ -n "$THREAD_NUM" ]; then
                    awk "/Thread $THREAD_NUM/,/Thread [0-9]+|Binary Images/" "$latest_crash" 2>/dev/null | head -30
                else
                    echo "Could not extract thread backtrace"
                fi
            else
                echo "No crash report found yet. Check manually:"
                echo "  ls -lt ~/Library/Logs/DiagnosticReports/pianobar*.crash"
            fi
        fi
    fi
}

# Check if debug mode is requested
if [ "$1" = "debug" ]; then
    echo "=== Building Remote Pianobar with Debug support ==="
    echo ""
    
    # Clean previous build
    echo "Cleaning previous build..."
    make clean
    echo ""
    
    # Build with debug flags
    echo "Building with debug symbols..."
    make DEBUG=1 CFLAGS="-g -O0 -DWEBSOCKET_ENABLED"
    echo ""
    
    echo "✓ Debug build complete!"
    echo ""
    echo "Debug output enabled. Run with:"
    echo "  PIANOBAR_DEBUG=8 ./pianobar"
    echo ""
else
    echo "=== Building Remote Pianobar ==="
    echo ""
    
    # Clean previous build
    echo "Cleaning previous build..."
    make clean
    echo ""
    
    # Build with optimization
    echo "Building optimized version..."
    make
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
    
    # Deploy WebUI to configured location
    deploy_webui

echo "=========================================="
echo "Build finished successfully!"
echo "=========================================="
echo ""
echo "Web interface will be available at:"
echo "  http://localhost:8080"
echo ""

log_file=$(get_log_file)
if [ $? -eq 0 ] && [ -f "$log_file" ]; then
    echo "Rotating log file: $log_file"
    
    # Remove oldest log if it exists
    [ -f "${log_file}.3" ] && rm "${log_file}.3"
    
    # Rotate existing logs
    [ -f "${log_file}.2" ] && mv "${log_file}.2" "${log_file}.3"
    [ -f "${log_file}.1" ] && mv "${log_file}.1" "${log_file}.2"
    [ -f "${log_file}"   ] && mv "${log_file}"   "${log_file}.1"
        
    echo "Log rotated successfully"
fi


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