#include "SwiftJITREPL.h"
#include <iostream>
#include <vector>
#include <chrono>

int main() {
    std::cout << "=== Swift JIT REPL Example ===\n\n";
    
    // Check if Swift JIT is available
    if (!SwiftJITREPL::isSwiftJITAvailable()) {
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
    std::cout << "SwiftJITREPL instance created successfully\n";
    
    std::cout << "Initializing Swift JIT REPL...\n";
    if (!repl.initialize()) {
        std::cerr << "Failed to initialize REPL: " << repl.getLastError() << "\n";
        return 1;
    }
    
    std::cout << "REPL initialized successfully!\n";
    std::cout << "About to start expression evaluation...\n\n";
    
    // Test expressions
    std::vector<std::string> expressions = {
        "42",
        "3.14 * 2",
        "1 + 2 + 3 + 4 + 5",
        "let x = 10; x * x",
        "\"Hello, Swift!\".count",
        "Array(1...10).reduce(0, +)",
        "let numbers = [1, 2, 3, 4, 5]; numbers.map { $0 * 2 }.reduce(0, +)",
        "let factorial = { (n: Int) -> Int in n <= 1 ? 1 : n * factorial(n - 1) }; factorial(5)"
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
            std::cout << "  Result: " << result.value << " (type: " << result.type << ")\n";
            std::cout << "  Time: " << duration.count() / 1000.0 << " ms\n";
        } else {
            std::cout << "  Error: " << result.error_message << "\n";
        }
        
        std::cout << "\n";
    }
    
    // Test batch evaluation
    std::cout << "Batch evaluation:\n";
    std::cout << "=================\n\n";
    
    std::vector<std::string> batch_expressions = {
        "let a = 5",
        "let b = 10",
        "a + b",
        "a * b",
        "b / a"
    };
    
    auto batch_results = repl.evaluateMultiple(batch_expressions);
    
    for (size_t i = 0; i < batch_expressions.size(); ++i) {
        std::cout << "Batch " << (i + 1) << ": " << batch_expressions[i] << "\n";
        
        if (batch_results[i].success) {
            std::cout << "  Result: " << batch_results[i].value << " (type: " << batch_results[i].type << ")\n";
        } else {
            std::cout << "  Error: " << batch_results[i].error_message << "\n";
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
    
    // Test convenience function
    std::cout << "\nTesting convenience function:\n";
    std::cout << "============================\n\n";
    
    auto quick_result = SwiftJITREPL::evaluateSwiftExpression("2 + 2 * 3");
    if (quick_result.success) {
        std::cout << "Quick evaluation: 2 + 2 * 3 = " << quick_result.value << "\n";
    } else {
        std::cout << "Quick evaluation failed: " << quick_result.error_message << "\n";
    }
    
    std::cout << "\n=== Example completed successfully! ===\n";
    return 0;
}
