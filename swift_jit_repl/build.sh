#!/bin/bash

# Swift JIT REPL Build Script
# This script builds the Swift JIT-based REPL project

set -e

echo "=== Building Swift JIT REPL ==="

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    echo "Error: CMakeLists.txt not found. Please run this script from the swift_jit_repl directory."
    exit 1
fi

# Create build directory
echo "Creating build directory..."
mkdir -p build
cd build

# Configure with CMake
echo "Configuring with CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=17 \
    -DCMAKE_CXX_STANDARD_REQUIRED=ON

# Build the project
echo "Building project..."
make -j$(nproc)

echo "=== Build completed successfully! ==="
echo ""
echo "Executables created:"
echo "  - swift_jit_example: Basic usage examples"
echo "  - swift_jit_test: Comprehensive test suite"
echo "  - swift_jit_server: Server example with threading"
echo ""
echo "Run examples:"
echo "  ./bin/swift_jit_example"
echo "  ./bin/swift_jit_test"
echo "  ./bin/swift_jit_server"
