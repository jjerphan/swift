#!/bin/bash

# Build script for Swift Minimal REPL
# This script builds the project using the user's built LLDB with Swift support

set -e

echo "Building Swift Minimal REPL with LLDB Swift support..."

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    echo "Error: Please run this script from the swift_minimal_repl directory"
    exit 1
fi

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring with CMake..."
cmake .. \
    -DLLDB_BUILD_DIR="/home/jjerphan/dev/build/Ninja-RelWithDebInfoAssert/lldb-linux-x86_64" \
    -DCMAKE_BUILD_TYPE=Release

# Build
echo "Building..."
make -j$(nproc)

echo "Build completed successfully!"
echo ""
echo "Executables created:"
echo "  - swift_repl_example"
echo "  - swift_repl_server"
echo "  - swift_repl_test"
echo "  - simple_test"
echo ""
echo "You can now run the examples:"
echo "  ./swift_repl_example"
echo "  ./swift_repl_server"
echo "  ./swift_repl_test"
echo "  ./simple_test"
