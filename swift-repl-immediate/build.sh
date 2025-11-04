#!/bin/bash

# Build script for the immediate-mode Swift JIT REPL

set -euo pipefail

echo "[swift-repl-immediate] ================================================"
echo "[swift-repl-immediate] Building immediate-mode Swift JIT REPL"
echo "[swift-repl-immediate] ================================================"

# 1) Prepare build directory
echo "[1/4] Preparing build directory: ./build"
mkdir -p build
cd build

# 2) Configure CMake (toolchain + build type)
echo "[2/4] Configuring CMake project"
echo "      - C/C++ compilers: clang / clang++"
echo "      - Build type: Release"
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER=clang

# 3) Build all targets (library, tests, and REPL executable)
echo "[3/4] Building targets (library, tests, swift-repl)"
make -j"$(nproc)"

# 4) Summarize outputs and run hints
echo "[4/4] Build completed successfully"
echo
echo "Artifacts:"
echo "  - Library:        ./build/lib/libSwiftJITREPL.*"
echo "  - Test binary:    ./build/bin/test_sil_jit"
echo "  - REPL executable:./build/bin/swift-repl"
echo
echo "Run examples:"
echo "  - Test: ./build/bin/test_sil_jit"
echo "  - REPL: ./build/bin/swift-repl"
echo "          Then type Swift code (e.g. 'print(\"hello\")')"
echo "          Type 'help' for commands, 'quit' to exit"
echo "[swift-repl-immediate] ================================================"