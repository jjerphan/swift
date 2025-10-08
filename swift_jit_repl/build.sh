#!/bin/bash

# Swift JIT REPL Build Script
# This script builds the Swift JIT-based REPL project

set -e

echo "=== Building Swift JIT REPL ==="
echo "Modular structure:"
echo "  - Common.h: Common types and forward declarations"
echo "  - SwiftPartialTranslationUnit.h: Data structure for PTUs"
echo "  - SwiftIncrementalParser.h/.cpp: Parsing logic"
echo "  - SwiftIncrementalExecutor.h/.cpp: JIT execution logic"
echo "  - SwiftInterpreter.h/.cpp: Main interpreter"
echo "  - SwiftJITREPL.h/.cpp: Main REPL interface"
echo ""

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
echo "Library created:"
echo "  - lib/libSwiftJITREPL.so: Main Swift JIT REPL library"
echo ""
echo "Executables created:"
echo "  - bin/swift_jit_example: Basic usage examples"
echo "  - bin/swift_jit_test: Comprehensive test suite"
echo ""
echo "Header files installed:"
echo "  - Common.h: Common types and forward declarations"
echo "  - SwiftPartialTranslationUnit.h: Data structure for PTUs"
echo "  - SwiftIncrementalParser.h: Parsing logic"
echo "  - SwiftIncrementalExecutor.h: JIT execution logic"
echo "  - SwiftInterpreter.h: Main interpreter"
echo "  - SwiftJITREPL.h: Main REPL interface"
echo ""
echo "Run examples:"
echo "  ./bin/swift_jit_example"
echo "  ./bin/swift_jit_test"
