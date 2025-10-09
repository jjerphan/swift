#include "SwiftJITREPL.h"
#include <iostream>
#include <cassert>
#include <unistd.h>
#include <fcntl.h>

int main() {
    // Silence stderr during tests unless SWIFT_REPL_VERBOSE=1
    int saved_stderr = -1;
    const char* verbose = getenv("SWIFT_REPL_VERBOSE");
    if (!(verbose && std::string(verbose) == "1")) {
        saved_stderr = dup(STDERR_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
    }
    std::cout << "Testing Swift JIT REPL..." << std::endl;
    
    try {
        // Create REPL instance
        SwiftJITREPL::REPLConfig config;
        SwiftJITREPL::SwiftJITREPL repl(config);
    
        // Test 1: Basic expression evaluation
        std::cout << "\n=== Test 1: Basic Expression ===" << std::endl;
        auto result1 = repl.evaluate("let x = 42");
        std::cout << "Result: " << (result1.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result1.success) {
            std::cout << "Error: " << result1.error_message << std::endl;
        }
        
        // Test 2: Variable usage (demonstrates cross-module symbol visibility)
        std::cout << "\n=== Test 2: Variable Usage (Cross-Module Symbol Visibility) ===" << std::endl;
        auto result2 = repl.evaluate("x + 1");
        std::cout << "Result: " << (result2.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result2.success) {
            std::cout << "Error: " << result2.error_message << std::endl;
        }
        
        // Test 3: Another variable declaration
        std::cout << "\n=== Test 3: Another Variable Declaration ===" << std::endl;
        auto result3 = repl.evaluate("let y = 10");
        std::cout << "Result: " << (result3.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result3.success) {
            std::cout << "Error: " << result3.error_message << std::endl;
        }
        
        // Test 4: Using both variables (demonstrates multiple variable persistence)
        std::cout << "\n=== Test 4: Using Both Variables ===" << std::endl;
        auto result4 = repl.evaluate("x + y");
        std::cout << "Result: " << (result4.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result4.success) {
            std::cout << "Error: " << result4.error_message << std::endl;
        }
        
        // Test 5: Complex expression with multiple operations
        std::cout << "\n=== Test 5: Complex Expression ===" << std::endl;
        auto result5 = repl.evaluate("(x * 2) + (y - 5)");
        std::cout << "Result: " << (result5.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result5.success) {
            std::cout << "Error: " << result5.error_message << std::endl;
        }
        
        // Test 6: Print function with complex expression (tests IR generation stability)
        std::cout << "\n=== Test 6: Print Function with Complex Expression ===" << std::endl;
        auto result6 = repl.evaluate("print((x * 2) + (y - 5))");
        std::cout << "Result: " << (result6.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result6.success) {
            std::cout << "Error: " << result6.error_message << std::endl;
        }
        
        std::cout << "\n=== All tests completed ===" << std::endl;
        
    } catch (const std::exception& e) {
        // Restore stderr before printing exception
        if (saved_stderr >= 0) {
            dup2(saved_stderr, STDERR_FILENO);
            close(saved_stderr);
        }
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    // Restore stderr after tests
    if (saved_stderr >= 0) {
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
    }
    
    return 0;
}
