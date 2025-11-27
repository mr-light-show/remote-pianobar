#!/bin/bash

# build-arm.sh - ARM64 Debug Build Script for Pianobar
# 
# This script helps build and debug pianobar on ARM64 hardware with various options:
#   - Debug builds with symbols
#   - Running under gdb
#   - Sanitizer builds for memory issue detection
#   - Creating .deb packages

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Parse command line arguments
MODE="build"
SANITIZE=false

print_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  (no args)     Build with debug symbols (-g -O0)"
    echo "  --debug       Build and run with crash capture"
    echo "  --sanitize    Build with AddressSanitizer and UndefinedBehaviorSanitizer"
    echo "  --package     Build and create .deb package"
    echo "  --linux-deb   Build Ubuntu ARM64 .deb package (requires Docker)"
    echo "  --help        Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                    # Build with debug symbols"
    echo "  $0 --debug            # Build and run with crash capture"
    echo "  $0 --sanitize         # Build with sanitizers"
    echo "  $0 --package          # Create .deb package (native OS)"
    echo "  $0 --linux-deb        # Create Ubuntu ARM64 .deb (macOS → Linux via Docker)"
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            MODE="debug"
            shift
            ;;
        --sanitize)
            SANITIZE=true
            shift
            ;;
        --package)
            MODE="package"
            shift
            ;;
        --linux-deb)
            MODE="linux-deb"
            shift
            ;;
        --help)
            print_usage
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            print_usage
            exit 1
            ;;
    esac
done

# Detect OS
detect_os() {
    OS=$(uname -s)
    if [[ "$OS" == "Darwin" ]]; then
        OS_TYPE="macos"
        echo -e "${GREEN}✓ Detected macOS${NC}"
    elif [[ "$OS" == "Linux" ]]; then
        OS_TYPE="linux"
        echo -e "${GREEN}✓ Detected Linux${NC}"
    else
        echo -e "${YELLOW}Warning: Unknown OS: $OS${NC}"
        OS_TYPE="unknown"
    fi
}

# Check if running on ARM64
check_architecture() {
    ARCH=$(uname -m)
    if [[ "$ARCH" != "aarch64" && "$ARCH" != "arm64" ]]; then
        echo -e "${YELLOW}Warning: Not running on ARM64 architecture (detected: $ARCH)${NC}"
        echo -e "${YELLOW}This script is designed for ARM64 debugging${NC}"
        read -p "Continue anyway? (y/n) " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    else
        echo -e "${GREEN}✓ Running on ARM64 ($ARCH)${NC}"
    fi
}

# Check for required dependencies
check_dependencies() {
    echo -e "${BLUE}Checking dependencies...${NC}"
    
    MISSING_DEPS=()
    MISSING_BREW=()
    
    # Build tools
    command -v gcc >/dev/null 2>&1 || command -v clang >/dev/null 2>&1 || MISSING_DEPS+=("gcc/clang")
    command -v pkg-config >/dev/null 2>&1 || MISSING_DEPS+=("pkg-config") && MISSING_BREW+=("pkg-config")
    command -v make >/dev/null 2>&1 || MISSING_DEPS+=("make")
    
    # Libraries (check for pkg-config files)
    pkg-config --exists ao 2>/dev/null || { MISSING_DEPS+=("libao"); MISSING_BREW+=("libao"); }
    pkg-config --exists libcurl 2>/dev/null || { MISSING_DEPS+=("libcurl"); MISSING_BREW+=("curl"); }
    pkg-config --exists libgcrypt 2>/dev/null || { MISSING_DEPS+=("libgcrypt"); MISSING_BREW+=("libgcrypt"); }
    pkg-config --exists json-c 2>/dev/null || { MISSING_DEPS+=("json-c"); MISSING_BREW+=("json-c"); }
    pkg-config --exists libavcodec 2>/dev/null || { MISSING_DEPS+=("ffmpeg"); MISSING_BREW+=("ffmpeg"); }
    pkg-config --exists libavformat 2>/dev/null || true  # Included with ffmpeg
    pkg-config --exists libavfilter 2>/dev/null || true  # Included with ffmpeg
    pkg-config --exists libavutil 2>/dev/null || true  # Included with ffmpeg
    pkg-config --exists libswresample 2>/dev/null || true  # Included with ffmpeg
    pkg-config --exists libwebsockets 2>/dev/null || { MISSING_DEPS+=("libwebsockets"); MISSING_BREW+=("libwebsockets"); }
    pkg-config --exists check 2>/dev/null || { MISSING_DEPS+=("check"); MISSING_BREW+=("check"); }
    
    # Node.js and npm for WebUI
    command -v node >/dev/null 2>&1 || { MISSING_DEPS+=("node"); MISSING_BREW+=("node"); }
    command -v npm >/dev/null 2>&1 || true  # npm comes with node
    
    # gdb/lldb for debugging (optional)
    if [[ "$MODE" == "debug" ]]; then
        if [[ "$OS_TYPE" == "macos" ]]; then
            command -v lldb >/dev/null 2>&1 || MISSING_DEPS+=("lldb (comes with Xcode)")
        else
            command -v gdb >/dev/null 2>&1 || { MISSING_DEPS+=("gdb"); MISSING_BREW+=("gdb"); }
        fi
    fi
    
    if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
        echo -e "${RED}✗ Missing dependencies:${NC}"
        for dep in "${MISSING_DEPS[@]}"; do
            echo "  - $dep"
        done
        echo ""
        
        if [[ "$OS_TYPE" == "macos" ]]; then
            echo -e "${YELLOW}Install with Homebrew:${NC}"
            if [ ${#MISSING_BREW[@]} -gt 0 ]; then
                echo "  brew install ${MISSING_BREW[@]}"
            fi
            if [[ " ${MISSING_DEPS[@]} " =~ " lldb " ]]; then
                echo ""
                echo "  lldb requires Xcode Command Line Tools:"
                echo "  xcode-select --install"
            fi
        else
            echo -e "${YELLOW}Install with:${NC}"
            echo "  sudo apt-get update"
            echo "  sudo apt-get install -y build-essential pkg-config libao-dev libcurl4-openssl-dev"
            echo "  sudo apt-get install -y libgcrypt20-dev libjson-c-dev libavcodec-dev libavformat-dev"
            echo "  sudo apt-get install -y libavfilter-dev libavutil-dev libswresample-dev libwebsockets-dev"
            echo "  sudo apt-get install -y check nodejs npm gdb"
        fi
        exit 1
    fi
    
    echo -e "${GREEN}✓ All dependencies installed${NC}"
}

# Enable core dumps
enable_core_dumps() {
    echo -e "${BLUE}Enabling core dumps...${NC}"
    ulimit -c unlimited
    
    # Try to set core pattern (requires sudo)
    if [ -w /proc/sys/kernel/core_pattern ]; then
        echo "/tmp/core.%e.%p" > /proc/sys/kernel/core_pattern
        echo -e "${GREEN}✓ Core dumps enabled in /tmp/${NC}"
    else
        echo -e "${YELLOW}Note: Cannot set core_pattern (needs sudo), using default location${NC}"
        echo -e "${YELLOW}Run: echo '/tmp/core.%e.%p' | sudo tee /proc/sys/kernel/core_pattern${NC}"
    fi
}

# Clean build
clean_build() {
    echo -e "${BLUE}Cleaning previous build...${NC}"
    make WEBSOCKET=1 clean 2>/dev/null || true
    rm -f pianobar pianobar_test
    echo -e "${GREEN}✓ Clean complete${NC}"
}

# Build pianobar
build_pianobar() {
    echo -e "${BLUE}Building pianobar...${NC}"
    
    # Base flags
    CFLAGS="-g -O0 -DWEBSOCKET_ENABLED"
    
    # Add sanitizer flags if requested
    if [ "$SANITIZE" = true ]; then
        echo -e "${YELLOW}Building with sanitizers (AddressSanitizer + UndefinedBehaviorSanitizer)${NC}"
        CFLAGS="$CFLAGS -fsanitize=address -fsanitize=undefined -fsanitize=alignment -fno-strict-aliasing -fno-omit-frame-pointer"
        export ASAN_OPTIONS="detect_leaks=1:symbolize=1:abort_on_error=1"
        export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
    fi
    
    echo -e "${YELLOW}CFLAGS: $CFLAGS${NC}"
    
    make WEBSOCKET=1 CFLAGS="$CFLAGS"
    
    if [ ! -f "pianobar" ]; then
        echo -e "${RED}✗ Build failed - pianobar binary not found${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}✓ Pianobar built successfully${NC}"
}

# Build WebUI
build_webui() {
    echo -e "${BLUE}Building WebUI...${NC}"
    
    if [ ! -d "webui" ]; then
        echo -e "${YELLOW}WebUI directory not found, skipping${NC}"
        return
    fi
    
    cd webui
    
    # Install dependencies if needed
    if [ ! -d "node_modules" ]; then
        echo -e "${YELLOW}Installing npm dependencies...${NC}"
        npm ci
    fi
    
    # Build
    npm run build
    
    cd ..
    
    if [ -d "dist/webui" ]; then
        echo -e "${GREEN}✓ WebUI built successfully${NC}"
    else
        echo -e "${YELLOW}Warning: WebUI build may have failed${NC}"
    fi
}

# Run with crash capture
run_with_crash_capture() {
    if [[ "$OS_TYPE" == "macos" ]]; then
        echo -e "${BLUE}Running pianobar with crash capture...${NC}"
        echo -e "${YELLOW}  (Crash reports will be in ~/Library/Logs/DiagnosticReports/)${NC}"
        echo ""
        
        # Enable core dumps
        ulimit -c unlimited
        
        # Run normally with debug output
        PIANOBAR_DEBUG=8 ./pianobar
        local exit_code=$?
        
        if [ $exit_code -ne 0 ] && [ $exit_code -ne 1 ]; then
            echo ""
            echo -e "${RED}=== CRASH DETECTED (exit code: $exit_code) ===${NC}"
            echo ""
            echo -e "${YELLOW}Looking for crash report...${NC}"
            
            # Find the most recent crash report for pianobar
            local crash_report=$(find ~/Library/Logs/DiagnosticReports -name "pianobar*.crash" -o -name "pianobar*.ips" 2>/dev/null | sort -r | head -n 1)
            
            if [ -n "$crash_report" ]; then
                echo -e "${GREEN}Found crash report: $crash_report${NC}"
                echo ""
                echo -e "${YELLOW}=== Crash Report ===${NC}"
                cat "$crash_report"
                echo ""
                echo -e "${GREEN}Full report saved at: $crash_report${NC}"
            else
                echo -e "${YELLOW}No crash report found yet. It may take a moment to generate.${NC}"
                echo -e "${YELLOW}Check: ~/Library/Logs/DiagnosticReports/${NC}"
            fi
            
            # Also check if core dump was created
            if ls core* 2>/dev/null | grep -q .; then
                echo ""
                echo -e "${YELLOW}Core dump found. Analyzing with lldb...${NC}"
                local core_file=$(ls -t core* 2>/dev/null | head -n 1)
                lldb -c "$core_file" ./pianobar -o "bt all" -o "quit"
            fi
        fi
    else
        echo -e "${BLUE}Running pianobar with crash capture...${NC}"
        echo -e "${YELLOW}  (Crash info will be saved if crash occurs)${NC}"
        echo ""
        
        local timestamp=$(date +%Y%m%d-%H%M%S)
        local crash_file="pianobar-crash-${timestamp}.log"
        
        ulimit -c unlimited
        PIANOBAR_DEBUG=8 ./pianobar
        local exit_code=$?
        
        if [ $exit_code -ne 0 ] && [ $exit_code -ne 1 ]; then
            echo ""
            echo -e "${RED}=== CRASH DETECTED (exit code: $exit_code) ===${NC}"
            if ls core* 2>/dev/null | grep -q .; then
                gdb -batch -ex "thread apply all bt full" -c core* ./pianobar > "$crash_file" 2>&1
                echo -e "${GREEN}Crash backtrace saved to: $crash_file${NC}"
                echo ""
                cat "$crash_file"
            else
                echo -e "${YELLOW}No core dump generated.${NC}"
            fi
        fi
    fi
}

# Create .deb package
create_package() {
    echo -e "${BLUE}Creating .deb package...${NC}"
    
    VERSION=$(date +%Y.%m.%d)-debug-arm64
    PKG_NAME="pianobar-websockets-debug"
    PKG_DIR="${PKG_NAME}_${VERSION}"
    
    # Create package structure
    rm -rf "$PKG_DIR"
    mkdir -p "$PKG_DIR/DEBIAN"
    mkdir -p "$PKG_DIR/usr/local/bin"
    mkdir -p "$PKG_DIR/usr/local/share/pianobar/webui"
    mkdir -p "$PKG_DIR/usr/share/doc/$PKG_NAME"
    
    # Copy binary
    cp pianobar "$PKG_DIR/usr/local/bin/"
    strip --strip-debug "$PKG_DIR/usr/local/bin/pianobar" 2>/dev/null || true
    
    # Copy WebUI if exists
    if [ -d "dist/webui" ]; then
        cp -r dist/webui/* "$PKG_DIR/usr/local/share/pianobar/webui/"
    fi
    
    # Copy documentation
    [ -f README.rst ] && cp README.rst "$PKG_DIR/usr/share/doc/$PKG_NAME/"
    [ -f COPYING ] && cp COPYING "$PKG_DIR/usr/share/doc/$PKG_NAME/"
    
    # Get installed size (in KB)
    INSTALLED_SIZE=$(du -sk "$PKG_DIR" | cut -f1)
    
    # Create control file
    cat > "$PKG_DIR/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $VERSION
Section: sound
Priority: optional
Architecture: arm64
Maintainer: Local ARM64 Build
Installed-Size: $INSTALLED_SIZE
Depends: libao4, libcurl4, libgcrypt20, libjson-c5, libavcodec60 | libavcodec59 | libavcodec58, libavformat60 | libavformat59 | libavformat58, libavfilter9 | libavfilter8 | libavfilter7, libavutil58 | libavutil57 | libavutil56, libswresample4 | libswresample3, libwebsockets19 | libwebsockets17 | libwebsockets16 | libwebsockets15
Description: Console-based Pandora client with WebSocket support (ARM64 debug)
 Pianobar is a console-based music player for Pandora.
 .
 This debug build includes WebSocket support for custom UIs and includes
 a modern web interface. Built with debug symbols for ARM64 architecture.
Homepage: https://github.com/mr-light-show/pianobar-websockets
EOF
    
    # Build package
    dpkg-deb --build "$PKG_DIR"
    
    if [ -f "${PKG_DIR}.deb" ]; then
        echo -e "${GREEN}✓ Package created: ${PKG_DIR}.deb${NC}"
        echo ""
        echo -e "${YELLOW}Install with:${NC}"
        echo "  sudo dpkg -i ${PKG_DIR}.deb"
        echo "  sudo apt-get install -f  # Fix dependencies if needed"
    else
        echo -e "${RED}✗ Package creation failed${NC}"
        exit 1
    fi
    
    # Cleanup
    rm -rf "$PKG_DIR"
}

# Build Linux ARM64 .deb using Docker (for cross-platform builds)
build_linux_deb_docker() {
    echo -e "${BLUE}Building Ubuntu ARM64 .deb package using Docker...${NC}"
    
    # Check if Docker is installed
    if ! command -v docker &> /dev/null; then
        echo -e "${RED}Error: Docker is not installed${NC}"
        echo -e "${YELLOW}Install Docker Desktop: https://www.docker.com/products/docker-desktop${NC}"
        exit 1
    fi
    
    # Check if Docker is running
    if ! docker info &> /dev/null; then
        echo -e "${RED}Error: Docker is not running${NC}"
        echo -e "${YELLOW}Start Docker Desktop and try again${NC}"
        exit 1
    fi
    
    echo -e "${YELLOW}This will build for Ubuntu 24.04 ARM64${NC}"
    echo -e "${YELLOW}It may take several minutes...${NC}"
    echo ""
    
    HOST_ARCH=$(uname -m)
    if [[ "$HOST_ARCH" != "aarch64" && "$HOST_ARCH" != "arm64" ]]; then
        echo -e "${RED}Error: This script only supports native ARM64 builds${NC}"
        echo -e "${YELLOW}Use GitHub Actions or an ARM64 machine for cross-compilation${NC}"
        exit 1
    fi
    
    VERSION=$(date +%Y.%m.%d)-debug-arm64
    PKG_NAME="pianobar-websockets-debug"
    
    # Create a build script to run inside the container
    cat > /tmp/docker-build.sh << 'DOCKER_EOF'
#!/bin/bash
set -e

echo "=== Installing dependencies ==="
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq \
    build-essential \
    pkg-config \
    libao-dev \
    libcurl4-openssl-dev \
    libgcrypt20-dev \
    libjson-c-dev \
    libavcodec-dev \
    libavformat-dev \
    libavfilter-dev \
    libavutil-dev \
    libswresample-dev \
    libwebsockets-dev \
    check \
    curl

# Install Node.js 20.x
curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
apt-get install -y -qq nodejs

cd /workspace

echo "=== Building pianobar ==="
make WEBSOCKET=1 clean || true
make WEBSOCKET=1 CFLAGS="-g -O0 -DWEBSOCKET_ENABLED"

echo "=== Building WebUI ==="
cd webui
npm ci --quiet
npm run build
cd ..

echo "=== Creating .deb package ==="
PKG_DIR="PKG_NAME_VERSION"
mkdir -p "$PKG_DIR/DEBIAN"
mkdir -p "$PKG_DIR/usr/local/bin"
mkdir -p "$PKG_DIR/usr/local/share/pianobar/webui"
mkdir -p "$PKG_DIR/usr/share/doc/PKG_NAME_ONLY"

# Copy binary
cp pianobar "$PKG_DIR/usr/local/bin/"
strip --strip-debug "$PKG_DIR/usr/local/bin/pianobar" || true

# Copy WebUI
if [ -d "dist/webui" ]; then
    cp -r dist/webui/* "$PKG_DIR/usr/local/share/pianobar/webui/"
fi

# Copy documentation
[ -f README.rst ] && cp README.rst "$PKG_DIR/usr/share/doc/PKG_NAME_ONLY/"
[ -f COPYING ] && cp COPYING "$PKG_DIR/usr/share/doc/PKG_NAME_ONLY/"

# Get installed size
INSTALLED_SIZE=$(du -sk "$PKG_DIR" | cut -f1)

# Create control file
cat > "$PKG_DIR/DEBIAN/control" <<EOF
Package: PKG_NAME_ONLY
Version: VERSION_HERE
Section: sound
Priority: optional
Architecture: arm64
Maintainer: Pianobar WebSocket Build
Installed-Size: $INSTALLED_SIZE
Depends: libao4, libcurl4, libgcrypt20, libjson-c5, libavcodec60 | libavcodec59 | libavcodec58, libavformat60 | libavformat59 | libavformat58, libavfilter9 | libavfilter8 | libavfilter7, libavutil58 | libavutil57 | libavutil56, libswresample4 | libswresample3, libwebsockets19 | libwebsockets17 | libwebsockets16
Description: Console-based Pandora client with WebSocket support (ARM64 debug)
 Pianobar is a console-based music player for Pandora.
 .
 This debug build includes WebSocket support for custom UIs and includes
 a modern web interface. Built with debug symbols for ARM64 architecture.
Homepage: https://github.com/mr-light-show/pianobar-websockets
EOF

# Build package
dpkg-deb --build "$PKG_DIR"

echo "=== Package built successfully ==="
ls -lh "${PKG_DIR}.deb"
DOCKER_EOF
    
    # Replace placeholders
    sed -i.bak "s/PKG_NAME_VERSION/${PKG_NAME}_${VERSION}/g" /tmp/docker-build.sh
    sed -i.bak "s/PKG_NAME_ONLY/${PKG_NAME}/g" /tmp/docker-build.sh
    sed -i.bak "s/VERSION_HERE/${VERSION}/g" /tmp/docker-build.sh
    rm /tmp/docker-build.sh.bak
    
    chmod +x /tmp/docker-build.sh
    
    # Create Dockerfile to avoid unpigz issues
    cat > /tmp/Dockerfile.pianobar << 'DOCKERFILE_EOF'
FROM --platform=linux/arm64 ubuntu:24.04
WORKDIR /workspace
COPY . /workspace/
RUN chmod +x /docker-build.sh && bash /docker-build.sh
DOCKERFILE_EOF
    
    # Build using Dockerfile (avoids unpigz extraction issues)
    echo -e "${YELLOW}Building Docker image...${NC}"
    docker build --platform linux/arm64 -f /tmp/Dockerfile.pianobar \
        --build-arg BUILDKIT_INLINE_CACHE=1 \
        -t pianobar-builder \
        "$SCRIPT_DIR"
    
    # Extract the .deb from the built image
    echo -e "${YELLOW}Extracting .deb package...${NC}"
    docker create --name pianobar-extract pianobar-builder
    docker cp "pianobar-extract:/workspace/${PKG_NAME}_${VERSION}.deb" "$SCRIPT_DIR/" 2>/dev/null || \
        echo -e "${RED}Warning: Could not extract .deb${NC}"
    docker rm pianobar-extract
    docker rmi pianobar-builder
    
    # Move the .deb to current directory
    DEB_FILE="${PKG_NAME}_${VERSION}.deb"
    if [ -f "$DEB_FILE" ]; then
        echo ""
        echo -e "${GREEN}✓ Package created: $DEB_FILE${NC}"
        echo ""
        echo -e "${YELLOW}To test on Ubuntu ARM64:${NC}"
        echo "  1. Copy to your ARM64 Ubuntu machine:"
        echo "     scp $DEB_FILE user@arm-machine:"
        echo ""
        echo "  2. Install on the ARM64 machine:"
        echo "     sudo dpkg -i $DEB_FILE"
        echo "     sudo apt-get install -f"
    else
        echo -e "${RED}✗ Package creation failed${NC}"
        exit 1
    fi
    
    # Cleanup
    rm -f /tmp/docker-build.sh
}

# Main execution
main() {
    echo -e "${BLUE}======================================${NC}"
    echo -e "${BLUE}Pianobar ARM64 Build Script${NC}"
    echo -e "${BLUE}======================================${NC}"
    echo ""
    
    # For Docker Linux build, skip local build steps
    if [[ "$MODE" == "linux-deb" ]]; then
        detect_os
        build_linux_deb_docker
        return
    fi
    
    detect_os
    check_architecture
    check_dependencies
    enable_core_dumps
    
    clean_build
    build_pianobar
    build_webui
    
    echo ""
    echo -e "${GREEN}======================================${NC}"
    echo -e "${GREEN}Build Complete!${NC}"
    echo -e "${GREEN}======================================${NC}"
    echo ""
    
    case "$MODE" in
        debug)
            echo -e "${YELLOW}Starting debug mode with crash capture...${NC}"
            echo ""
            run_with_crash_capture
            ;;
        package)
            create_package
            ;;
        linux-deb)
            # Skip normal build for Docker cross-compile
            echo -e "${YELLOW}Building Linux ARM64 .deb package...${NC}"
            build_linux_deb_docker
            ;;
        build)
            echo -e "${YELLOW}Pianobar built successfully!${NC}"
            echo ""
            echo -e "Run pianobar:"
            echo -e "  ${GREEN}./pianobar${NC}"
            echo ""
            echo -e "Run with debug output:"
            echo -e "  ${GREEN}PIANOBAR_DEBUG=8 ./pianobar${NC}"
            echo ""
            if [[ "$OS_TYPE" == "macos" ]]; then
                echo -e "Run under lldb:"
            else
                echo -e "Run under gdb:"
            fi
            echo -e "  ${GREEN}$0 --debug${NC}"
            echo ""
            echo -e "Build with sanitizers:"
            echo -e "  ${GREEN}$0 --sanitize${NC}"
            echo ""
            echo -e "Create .deb package:"
            echo -e "  ${GREEN}$0 --package${NC}"
            echo ""
            if [[ "$OS_TYPE" == "macos" ]]; then
                echo -e "Build Ubuntu ARM64 .deb:"
                echo -e "  ${GREEN}$0 --linux-deb${NC}"
            fi
            ;;
    esac
}

main

