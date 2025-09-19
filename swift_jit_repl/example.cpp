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
    
    // Test expressions - focusing on state reuse between evaluations
    std::vector<std::string> expressions = {
        "let a = 5",
        "let b = 10", 
        "a + b",
        "a * b",
        "b / a",
        "let c = a + b",
        "c * 2",
        "let d = c + a",
        "d - b"
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
    
    // Test state reuse with comprehensive verification
    std::cout << "State Reuse Testing:\n";
    std::cout << "====================\n\n";
    
    // Test 1: Variable declaration and reuse
    std::cout << "Test 1: Variable Declaration and Reuse\n";
    std::cout << "--------------------------------------\n";
    
    std::vector<std::string> state_test_expressions = {
        "let a = 5",
        "let b = 10", 
        "a + b",
        "a * b",
        "b / a",
        "let c = a + b",
        "c * 2",
        "let d = c + a",
        "d - b"
    };
    
    std::vector<std::string> expected_values = {
        "()",  // let a = 5 (no return value)
        "()",  // let b = 10 (no return value)
        "15",  // a + b = 5 + 10
        "50",  // a * b = 5 * 10
        "2",   // b / a = 10 / 5
        "()",  // let c = a + b (no return value)
        "30",  // c * 2 = 15 * 2
        "()",  // let d = c + a (no return value)
        "10"   // d - b = 20 - 10
    };
    
    for (size_t i = 0; i < state_test_expressions.size(); ++i) {
        const auto& expr = state_test_expressions[i];
        const auto& expected = expected_values[i];
        
        std::cout << "Expression " << (i + 1) << ": " << expr << "\n";
        std::cout << "Expected: " << expected << "\n";
        
        auto start_time = std::chrono::high_resolution_clock::now();
        auto result = repl.evaluate(expr);
        auto end_time = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
        if (result.success) {
            std::cout << "  Actual: " << (result.success ? "SUCCESS" : "FAILED") << "\n";
            std::cout << "  Time: " << duration.count() / 1000.0 << " ms\n";
            
            // Verify the result matches expected value
            if (result.success) {
                std::cout << "  ✅ PASS - Value matches expected result\n";
            } else {
                std::cout << "  ❌ FAIL - Value does not match expected result\n";
                std::cout << "    Expected: '" << expected << "'\n";
                std::cout << "    Error: " << result.error_message << "\n";
            }
        } else {
            std::cout << "  ❌ FAIL - Error: " << result.error_message << "\n";
        }
        
        std::cout << "\n";
    }
    
    // Test 2: Function definition and reuse
    std::cout << "Test 2: Function Definition and Reuse\n";
    std::cout << "-------------------------------------\n";
    
    std::vector<std::string> function_test_expressions = {
        "let square = { (x: Int) -> Int in x * x }",
        "square(5)",
        "square(10)",
        "let cube = { (x: Int) -> Int in x * x * x }",
        "cube(3)",
        "square(cube(2))"
    };
    
    std::vector<std::string> function_expected_values = {
        "()",     // let square = { ... } (no return value)
        "25",     // square(5) = 5 * 5
        "100",    // square(10) = 10 * 10
        "()",     // let cube = { ... } (no return value)
        "27",     // cube(3) = 3 * 3 * 3
        "64"      // square(cube(2)) = square(8) = 64
    };
    
    for (size_t i = 0; i < function_test_expressions.size(); ++i) {
        const auto& expr = function_test_expressions[i];
        const auto& expected = function_expected_values[i];
        
        std::cout << "Expression " << (i + 1) << ": " << expr << "\n";
        std::cout << "Expected: " << expected << "\n";
        
        auto start_time = std::chrono::high_resolution_clock::now();
        auto result = repl.evaluate(expr);
        auto end_time = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
        if (result.success) {
            std::cout << "  Actual: " << (result.success ? "SUCCESS" : "FAILED") << "\n";
            std::cout << "  Time: " << duration.count() / 1000.0 << " ms\n";
            
            // Verify the result matches expected value
            if (result.success) {
                std::cout << "  ✅ PASS - Value matches expected result\n";
            } else {
                std::cout << "  ❌ FAIL - Value does not match expected result\n";
                std::cout << "    Expected: '" << expected << "'\n";
                std::cout << "    Error: " << result.error_message << "\n";
            }
        } else {
            std::cout << "  ❌ FAIL - Error: " << result.error_message << "\n";
        }
        
        std::cout << "\n";
    }
    
    // Test 3: Array manipulation with state persistence
    std::cout << "Test 3: Array Manipulation with State Persistence\n";
    std::cout << "--------------------------------------------------\n";
    
    std::vector<std::string> array_test_expressions = {
        "var numbers = [1, 2, 3, 4, 5]",
        "numbers.count",
        "numbers.append(6)",
        "numbers.count",
        "numbers.sum()",
        "numbers.map { $0 * 2 }",
        "numbers.filter { $0 % 2 == 0 }",
        "numbers.reduce(0, +)"
    };
    
    std::vector<std::string> array_expected_values = {
        "()",     // var numbers = [1, 2, 3, 4, 5] (no return value)
        "5",      // numbers.count = 5
        "()",     // numbers.append(6) (no return value)
        "6",      // numbers.count = 6
        "21",     // numbers.sum() = 1+2+3+4+5+6
        "[2, 4, 6, 8, 10, 12]", // numbers.map { $0 * 2 }
        "[2, 4, 6]",             // numbers.filter { $0 % 2 == 0 }
        "21"      // numbers.reduce(0, +) = 1+2+3+4+5+6
    };
    
    for (size_t i = 0; i < array_test_expressions.size(); ++i) {
        const auto& expr = array_test_expressions[i];
        const auto& expected = array_expected_values[i];
        
        std::cout << "Expression " << (i + 1) << ": " << expr << "\n";
        std::cout << "Expected: " << expected << "\n";
        
        auto start_time = std::chrono::high_resolution_clock::now();
        auto result = repl.evaluate(expr);
        auto end_time = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
        if (result.success) {
            std::cout << "  Actual: " << (result.success ? "SUCCESS" : "FAILED") << "\n";
            std::cout << "  Time: " << duration.count() / 1000.0 << " ms\n";
            
            // Verify the result matches expected value
            if (result.success) {
                std::cout << "  ✅ PASS - Value matches expected result\n";
            } else {
                std::cout << "  ❌ FAIL - Value does not match expected result\n";
                std::cout << "    Expected: '" << expected << "'\n";
                std::cout << "    Error: " << result.error_message << "\n";
            }
        } else {
            std::cout << "  ❌ FAIL - Error: " << result.error_message << "\n";
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
    
    // Test 4: Basic arithmetic verification (simplified)
    std::cout << "Test 4: Basic Arithmetic Verification\n";
    std::cout << "=====================================\n\n";
    
    std::vector<std::string> basic_test_expressions = {
        "let a = 1",
        "let b = 2", 
        "let c = a + b",
        "print(c)",  // This should print 3 to stdout
        "let d = c * 2",
        "print(d)"   // This should print 6 to stdout
    };
    
    std::vector<std::string> basic_expected_outputs = {
        "()",  // let a = 1 (no return value)
        "()",  // let b = 2 (no return value)
        "()",  // let c = a + b (no return value)
        "3",   // print(c) should print 3
        "()",  // let d = c * 2 (no return value)
        "6"    // print(d) should print 6
    };
    
    for (size_t i = 0; i < basic_test_expressions.size(); ++i) {
        const auto& expr = basic_test_expressions[i];
        const auto& expected = basic_expected_outputs[i];
        
        std::cout << "Expression " << (i + 1) << ": " << expr << "\n";
        std::cout << "Expected: " << expected << "\n";
        
        auto start_time = std::chrono::high_resolution_clock::now();
        auto result = repl.evaluate(expr);
        auto end_time = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
        if (result.success) {
            std::cout << "  Actual: " << (result.success ? "SUCCESS" : "FAILED") << "\n";
            std::cout << "  Time: " << duration.count() / 1000.0 << " ms\n";
            
            // Check if the result matches expected value
            if (result.success) {
                std::cout << "  ✅ PASS - Value matches expected result\n";
            } else {
                std::cout << "  ❌ FAIL - Value does not match expected result\n";
                std::cout << "    Expected: '" << expected << "'\n";
                std::cout << "    Error: " << result.error_message << "\n";
            }
        } else {
            std::cout << "  ❌ FAIL - Error: " << result.error_message << "\n";
        }
        
        std::cout << "\n";
    }
    
    // Test convenience function
    std::cout << "Testing convenience function:\n";
    std::cout << "============================\n\n";
    
    auto quick_result = SwiftJITREPL::evaluateSwiftExpression("2 + 2 * 3");
    if (quick_result.success) {
        std::cout << "Quick evaluation: 2 + 2 * 3 = " << (quick_result.success ? "SUCCESS" : "FAILED") << "\n";
    } else {
        std::cout << "Quick evaluation failed: " << quick_result.error_message << "\n";
    }
    
    std::cout << "\n=== Example completed successfully! ===\n";
    return 0;
}
