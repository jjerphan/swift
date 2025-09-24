#include "SwiftJITREPL.h"
#include <iostream>
#include <vector>
#include <chrono>

int main() {
    std::cout << "=== Swift JIT REPL Example ===\n\n";
    
    // Check if Swift JIT is available
    if (!SwiftJITREPL::SwiftJITREPL::isAvailable()) {
        std::cerr << "Swift JIT not available on this system\n";
        return 1;
    }
    
    std::cout << "Swift JIT is available!\n\n";
    
    // Configure the REPL
    SwiftJITREPL::REPLConfig config;
    config.enable_optimizations = true;
    config.generate_debug_info = false;
    config.lazy_compilation = true;
    config.timeout_ms = 10000; // 10 seconds
    
    // Create and initialize REPL
    std::cout << "Creating SwiftJITREPL instance...\n";
    SwiftJITREPL::SwiftJITREPL repl(config);
    std::cout << "SwiftJITREPL instance created and initialized successfully\n";
    std::cout << "About to start expression evaluation...\n\n";
    
    // Test expressions - all in one evaluation to test single-module approach
    std::vector<std::string> expressions = {
        "5 + 10"
    };
    
    std::cout << "Evaluating expressions:\n";
    std::cout << "=======================\n\n";
    
    for (size_t i = 0; i < expressions.size(); ++i) {
        const auto& expr = expressions[i];
        std::cout << "Expression " << (i + 1) << ": " << expr << "\n";
        
        auto start_time = std::chrono::high_resolution_clock::now();
        auto result = repl.evaluate(expr);
        auto end_time = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
        if (result.success) {
            std::cout << "  Result: " << (result.success ? "SUCCESS" : "FAILED") << "\n";
            std::cout << "  Time: " << duration.count() / 1000.0 << " ms\n";
        } else {
            std::cout << "  Error: " << result.error_message << "\n";
        }
        
        std::cout << "\n";
    }
    
    // Show statistics
    auto stats = repl.getStats();
    std::cout << "Compilation Statistics:\n";
    std::cout << "=======================\n";
    std::cout << "Total expressions: " << stats.total_expressions << "\n";
    std::cout << "Successful compilations: " << stats.successful_compilations << "\n";
    std::cout << "Failed compilations: " << stats.failed_compilations << "\n";
    std::cout << "Total compilation time: " << stats.total_compilation_time_ms << " ms\n";
    std::cout << "Total execution time: " << stats.total_execution_time_ms << " ms\n";
    std::cout << "Average compilation time: " 
              << (stats.total_expressions > 0 ? stats.total_compilation_time_ms / stats.total_expressions : 0) 
              << " ms\n";
    
    std::cout << "\n=== Example completed successfully! ===\n";
    return 0;
}
