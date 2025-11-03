#include "SwiftJITREPL.h"
#include <iostream>

int main() {
    std::cout << "Testing SIL-based Swift JIT REPL initialization..." << std::endl;
    
    // Check if Swift JIT is available
    if (!SwiftJITREPL::SwiftJITREPL::isAvailable()) {
        std::cerr << "Swift JIT is not available on this system" << std::endl;
        return 1;
    }
    
    std::cout << "✓ Swift JIT is available!" << std::endl;
    
    // Create REPL instance
    SwiftJITREPL::REPLConfig config;
    config.enable_optimizations = false;
    config.generate_debug_info = false;
    
    std::cout << "Creating REPL instance..." << std::endl;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    std::cout << "✓ REPL instance created successfully!" << std::endl;
    
    // Test basic functionality
    std::cout << "Testing basic REPL functionality..." << std::endl;
    
    // Test a simple Swift program using top-level code (no @main)
    auto result = repl.evaluate(
        "import Swift\n"
        "print(\"Hello from SIL JIT!\")\n"
    );
    
    if (result.success) {
        std::cout << "✓ Expression evaluation successful!" << std::endl;
    } else {
        std::cerr << "✗ Expression evaluation failed: " << result.error_message << std::endl;
        return 1;
    }

    // Execute all materialization units via the JIT (synthesized main)
    int exitCode = repl.executeAll();
    if (exitCode != 0) {
        std::cerr << "✗ JIT execution failed with code: " << exitCode << std::endl;
        return 1;
    }

    return 0;
}