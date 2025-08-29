#include "SwiftJITREPL.h"
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <chrono>

// Test utilities
class TestRunner {
private:
    size_t totalTests = 0;
    size_t passedTests = 0;
    size_t failedTests = 0;
    
public:
    void runTest(const std::string& testName, std::function<bool()> testFunc) {
        totalTests++;
        std::cout << "Running test: " << testName << "... ";
        
        try {
            bool result = testFunc();
            if (result) {
                std::cout << "PASSED\n";
                passedTests++;
            } else {
                std::cout << "FAILED\n";
                failedTests++;
            }
        } catch (const std::exception& e) {
            std::cout << "FAILED (Exception: " << e.what() << ")\n";
            failedTests++;
        } catch (...) {
            std::cout << "FAILED (Unknown exception)\n";
            failedTests++;
        }
    }
    
    void printSummary() const {
        std::cout << "\n=== Test Summary ===\n";
        std::cout << "Total tests: " << totalTests << "\n";
        std::cout << "Passed: " << passedTests << "\n";
        std::cout << "Failed: " << failedTests << "\n";
        std::cout << "Success rate: " << (totalTests > 0 ? (passedTests * 100.0 / totalTests) : 0) << "%\n";
    }
};

// Test functions
bool testBasicInitialization() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    return repl.initialize();
}

bool testSimpleExpression() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    auto result = repl.evaluate("42");
    return result.success && result.value == "42" && result.type == "Int";
}

bool testArithmeticExpression() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    auto result = repl.evaluate("2 + 3 * 4");
    return result.success && result.value == "14" && result.type == "Int";
}

bool testStringExpression() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    auto result = repl.evaluate("\"Hello\".count");
    return result.success && result.value == "5" && result.type == "Int";
}

bool testVariableDeclaration() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    auto result = repl.evaluate("let x = 10; x * 2");
    return result.success && result.value == "20" && result.type == "Int";
}

bool testArrayExpression() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    auto result = repl.evaluate("[1, 2, 3, 4, 5].reduce(0, +)");
    return result.success && result.value == "15" && result.type == "Int";
}

bool testClosureExpression() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    auto result = repl.evaluate("let add = { (a: Int, b: Int) in a + b }; add(5, 3)");
    return result.success && result.value == "8" && result.type == "Int";
}

bool testMultipleExpressions() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    std::vector<std::string> expressions = {
        "let a = 5",
        "let b = 10",
        "a + b"
    };
    
    auto results = repl.evaluateMultiple(expressions);
    
    if (results.size() != 3) return false;
    if (!results[0].success || !results[1].success || !results[2].success) return false;
    if (results[2].value != "15") return false;
    
    return true;
}

bool testErrorHandling() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Test invalid syntax
    auto result = repl.evaluate("let x = ;");
    return !result.success && !result.error_message.empty();
}

bool testResetFunctionality() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Add some state
    repl.evaluate("let x = 42");
    
    // Reset
    if (!repl.reset()) return false;
    
    // Try to use the variable (should fail)
    auto result = repl.evaluate("x");
    return !result.success;
}

bool testConfigurationOptions() {
    // Test with optimizations disabled
    SwiftJITREPL::REPLConfig config1;
    config1.enable_optimizations = false;
    SwiftJITREPL::SwiftJITREPL repl1(config1);
    
    if (!repl1.initialize()) return false;
    
    auto result1 = repl1.evaluate("1 + 1");
    if (!result1.success) return false;
    
    // Test with debug info enabled
    SwiftJITREPL::REPLConfig config2;
    config2.generate_debug_info = true;
    SwiftJITREPL::SwiftJITREPL repl2(config2);
    
    if (!repl2.initialize()) return false;
    
    auto result2 = repl2.evaluate("2 + 2");
    return result2.success;
}

bool testConvenienceFunction() {
    auto result = SwiftJITREPL::evaluateSwiftExpression("3 * 7");
    return result.success && result.value == "21" && result.type == "Int";
}

bool testPerformance() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Evaluate multiple expressions
    for (int i = 0; i < 10; ++i) {
        auto result = repl.evaluate(std::to_string(i) + " + " + std::to_string(i));
        if (!result.success) return false;
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    // Should complete in reasonable time (less than 1 second)
    return duration.count() < 1000000;
}

bool testStatistics() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Evaluate some expressions
    repl.evaluate("1 + 1");
    repl.evaluate("2 + 2");
    repl.evaluate("3 + 3");
    
    auto stats = repl.getStats();
    
    return stats.total_expressions == 3 && 
           stats.successful_compilations == 3 &&
           stats.failed_compilations == 0;
}

int main() {
    std::cout << "=== Swift JIT REPL Test Suite ===\n\n";
    
    TestRunner runner;
    
    // Run all tests
    runner.runTest("Basic Initialization", testBasicInitialization);
    runner.runTest("Simple Expression", testSimpleExpression);
    runner.runTest("Arithmetic Expression", testArithmeticExpression);
    runner.runTest("String Expression", testStringExpression);
    runner.runTest("Variable Declaration", testVariableDeclaration);
    runner.runTest("Array Expression", testArrayExpression);
    runner.runTest("Closure Expression", testClosureExpression);
    runner.runTest("Multiple Expressions", testMultipleExpressions);
    runner.runTest("Error Handling", testErrorHandling);
    runner.runTest("Reset Functionality", testResetFunctionality);
    runner.runTest("Configuration Options", testConfigurationOptions);
    runner.runTest("Convenience Function", testConvenienceFunction);
    runner.runTest("Performance", testPerformance);
    runner.runTest("Statistics", testStatistics);
    
    // Print summary
    runner.printSummary();
    
    return 0;
}
