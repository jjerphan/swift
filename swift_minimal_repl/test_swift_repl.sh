#!/bin/bash

# Test script for Swift Minimal REPL
# This script builds and tests the project to ensure it works with LLDB Swift support

set -e

echo "Testing Swift Minimal REPL with LLDB Swift support..."
echo "=================================================="

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    echo "Error: Please run this script from the swift_minimal_repl directory"
    exit 1
fi

# Build the project
echo "Step 1: Building the project..."
./build.sh

# Test basic functionality
echo ""
echo "Step 2: Testing basic functionality..."
cd build

echo "Testing simple_test..."
./simple_test

echo ""
echo "Testing swift_repl_example..."
./swift_repl_example

echo ""
echo "Step 3: Testing Swift REPL availability..."
echo "Running availability check..."

# Create a simple test program to check Swift REPL availability
cat > test_availability.cpp << 'EOF'
#include "MinimalSwiftREPL.h"
#include <iostream>

int main() {
    std::cout << "Checking Swift REPL availability..." << std::endl;
    
    if (SwiftMinimalREPL::isSwiftREPLAvailable()) {
        std::cout << "✓ Swift REPL is AVAILABLE!" << std::endl;
        
        // Try to create and initialize a REPL
        SwiftMinimalREPL::MinimalSwiftREPL repl;
        if (repl.initialize()) {
            std::cout << "✓ REPL initialization successful!" << std::endl;
            
            // Try a simple Swift expression
            auto result = repl.evaluate("1 + 1");
            if (result.success) {
                std::cout << "✓ Swift expression evaluation successful!" << std::endl;
                std::cout << "  Result: " << result.value << " (type: " << result.type << ")" << std::endl;
            } else {
                std::cout << "✗ Swift expression evaluation failed: " << result.error_message << std::endl;
            }
        } else {
            std::cout << "✗ REPL initialization failed: " << repl.getLastError() << std::endl;
        }
    } else {
        std::cout << "✗ Swift REPL is NOT available" << std::endl;
        return 1;
    }
    
    return 0;
}
EOF

# Compile and run the test
echo "Compiling availability test..."
g++ -std=c++17 -I.. -I/home/jjerphan/dev/llvm-project/lldb/include -I/home/jjerphan/dev/llvm-project/llvm/include -I/home/jjerphan/dev/llvm-project/clang/include -L. -lMinimalSwiftREPL -L/home/jjerphan/dev/build/Ninja-RelWithDebInfoAssert/lldb-linux-x86_64/lib -llldb -lpthread -ldl -lz -lxml2 -lncurses -ledit -llzma -lrt -lm -lstdc++ -o test_availability test_availability.cpp

echo "Running availability test..."
./test_availability

echo ""
echo "Test completed!"
echo "If you see '✓ Swift REPL is AVAILABLE!' above, the integration is working correctly."
