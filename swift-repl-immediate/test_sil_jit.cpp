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
    
    // Test a simple expression
    auto result = repl.evaluate("print(\"Hello from SIL JIT!\")");
    
    if (result.success) {
        std::cout << "✓ Expression evaluation successful!" << std::endl;
    } else {
        std::cerr << "✗ Expression evaluation failed: " << result.error_message << std::endl;
        return 1;
    }
    
    // Test first variable definition
    std::cout << "Testing first variable definition: let a = 10" << std::endl;
    result = repl.evaluate("let a = 10");

    if (result.success) {
        std::cout << "✓ First variable assignment executed successfully!" << std::endl;
    } else {
        std::cerr << "✗ First variable assignment failed: " << result.error_message << std::endl;
        return 1;
    }

    // Test second variable definition
    std::cout << "Testing second variable definition: let b = 20" << std::endl;
    result = repl.evaluate("let b = 20");

    if (result.success) {
        std::cout << "✓ Second variable assignment executed successfully!" << std::endl;
    } else {
        std::cerr << "✗ Second variable assignment failed: " << result.error_message << std::endl;
        return 1;
    }

    // Test addition of the two variables
    std::cout << "Testing addition of variables: print(a + b)" << std::endl;
    result = repl.evaluate("print(a + b)");

    if (result.success) {
        std::cout << "✓ Addition expression executed successfully!" << std::endl;
    } else {
        std::cerr << "✗ Addition expression failed: " << result.error_message << std::endl;
        return 1;
    }
    
    // Test reset functionality
    std::cout << "Testing REPL reset..." << std::endl;
    if (repl.reset()) {
        std::cout << "✓ REPL reset successful!" << std::endl;
    } else {
        std::cerr << "✗ REPL reset failed: " << repl.getLastError() << std::endl;
        return 1;
    }
    
    std::cout << "\n🎉 All tests passed! SIL-based Swift JIT REPL is working correctly." << std::endl;
    return 0;
}