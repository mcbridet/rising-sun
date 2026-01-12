#!/usr/bin/env bash
# Build Rising Sun for multiple targets

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_TYPE="${1:-debug}"
BUILD_P3="${BUILD_P3:-0}"

# Force Qt5 for better cross-distro compatibility
export QT_VERSION_MAJOR=5

echo "=== Building Rising Sun ==="

# Build for native x86_64
echo ""
echo ">>> Building for x86_64 (native)..."
if [ "$BUILD_TYPE" = "release" ]; then
    cargo build --release
else
    cargo build
fi

# Build for Pentium III (i686)
if [ "$BUILD_P3" = "1" ]; then
    echo ""
    echo ">>> Building for Pentium III (i686)..."
    
    # Ensure the target is installed
    if ! rustup target list --installed | grep -q "i686-unknown-linux-gnu"; then
        echo "Installing i686-unknown-linux-gnu target..."
        rustup target add i686-unknown-linux-gnu
    fi
    
    if [ "$BUILD_TYPE" = "release" ]; then
        cargo build --release --target i686-unknown-linux-gnu
    else
        cargo build --target i686-unknown-linux-gnu
    fi
fi

echo ""
echo "=== Build Complete ==="
echo ""
echo "Binaries:"
if [ "$BUILD_TYPE" = "release" ]; then
    echo "  x86_64:  target/release/rising-sun"
    [ "$BUILD_P3" = "1" ] && echo "  P3/i686: target/i686-unknown-linux-gnu/release/rising-sun"
else
    echo "  x86_64:  target/debug/rising-sun"
    [ "$BUILD_P3" = "1" ] && echo "  P3/i686: target/i686-unknown-linux-gnu/debug/rising-sun"
fi
