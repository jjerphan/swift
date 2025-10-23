#!/bin/bash

# Build script for SIL-based Swift JIT REPL

set -e

echo "Building SIL-based Swift JIT REPL..."

# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER=clang

# Build
make -j$(nproc)

echo "Build completed successfully!"
echo "Run the test with: ./build/bin/test_sil_jit"