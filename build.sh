#!/bin/bash

# Binance Dashboard Build Script
# Automates the build process for the C++ application

set -e  # Exit on any error

echo "🚀 Building Binance Dashboard..."
echo "================================="

# Check if vcpkg is available
if [ -z "$VCPKG_ROOT" ]; then
    echo "❌ VCPKG_ROOT not set. Please install and configure vcpkg first:"
    echo ""
    echo "Setup instructions:"
    echo "  git clone https://github.com/Microsoft/vcpkg.git ~/vcpkg"
    echo "  cd ~/vcpkg && ./bootstrap-vcpkg.sh"
    echo "  export VCPKG_ROOT=~/vcpkg"
    echo "  export PATH=\$VCPKG_ROOT:\$PATH"
    echo ""
    echo "Then run this script again."
    exit 1
fi

# Expand the path properly
VCPKG_ROOT=$(eval echo $VCPKG_ROOT)
echo "✅ Found vcpkg at: $VCPKG_ROOT"

# Check if vcpkg executable exists
if [ ! -f "$VCPKG_ROOT/vcpkg" ]; then
    echo "❌ vcpkg executable not found at $VCPKG_ROOT/vcpkg"
    echo "Please make sure vcpkg is properly installed and bootstrapped."
    exit 1
fi

# Check if dependencies are installed
echo "🔍 Checking dependencies..."
DEPS_MISSING=false

# Check for grep command
if ! command -v grep >/dev/null 2>&1; then
    echo "⚠️  grep command not found, skipping dependency check"
    echo "📦 Installing all dependencies to be safe..."
    DEPS_MISSING=true
else
    for dep in boost-beast boost-system boost-thread boost-chrono boost-random nlohmann-json openssl; do
        if ! "$VCPKG_ROOT/vcpkg" list 2>/dev/null | grep -q "$dep"; then
            echo "❌ Missing dependency: $dep"
            DEPS_MISSING=true
        else
            echo "✅ Found: $dep"
        fi
    done
fi

# Install missing dependencies
if [ "$DEPS_MISSING" = true ]; then
    echo "📦 Installing missing dependencies..."
    echo "Using Boost.Beast instead of websocketpp (more modern and reliable)"
    "$VCPKG_ROOT/vcpkg" install boost-beast boost-system boost-thread boost-chrono boost-random nlohmann-json openssl
else
    echo "✅ All dependencies are installed"
fi

# Create build directory
echo "📁 Creating build directory..."
mkdir -p build
cd build

# Configure with CMake
echo "⚙️  Configuring with CMake..."
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release

# Build the project
echo "🔨 Building project..."
make -j$(nproc 2>/dev/null || echo 4)

# Check if build was successful
if [ -f "binance_dashboard" ]; then
    echo ""
    echo "🎉 Build successful!"
    echo "==================="
    echo ""
    echo "To run the application:"
    echo "  cd build"
    echo "  ./binance_dashboard"
    echo ""
    echo "The dashboard will connect to Binance and display real-time crypto data."
    echo "Press Ctrl+C to stop the application."
else
    echo ""
    echo "❌ Build failed!"
    echo "Check the error messages above for details."
    exit 1
fi
