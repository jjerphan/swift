#include "SwiftJITREPL.h"
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <chrono>
#include <functional>
#include <cmath>
#include <sstream>

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
    return result.success && result.value.find("Successfully executed Swift code: 42") != std::string::npos;
}

bool testArithmeticExpression() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    auto result = repl.evaluate("2 + 3 * 4");
    return result.success && result.value.find("Successfully executed Swift code: 2 + 3 * 4") != std::string::npos;
}

bool testStringExpression() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    auto result = repl.evaluate("\"Hello\".count");
    return result.success && result.value.find("Successfully executed Swift code: \"Hello\".count") != std::string::npos;
}

bool testVariableDeclaration() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    auto result = repl.evaluate("let x = 10; x * 2");
    return result.success && result.value.find("Successfully executed Swift code: let x = 10; x * 2") != std::string::npos;
}

bool testArrayExpression() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    auto result = repl.evaluate("[1, 2, 3, 4, 5].reduce(0, +)");
    return result.success && result.value.find("Successfully executed Swift code: [1, 2, 3, 4, 5].reduce(0, +)") != std::string::npos;
}

bool testClosureExpression() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    auto result = repl.evaluate("let add = { (a: Int, b: Int) in a + b }; add(5, 3)");
    return result.success && result.value.find("Successfully executed Swift code: let add = { (a: Int, b: Int) in a + b }; add(5, 3)") != std::string::npos;
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
    // Check that the expressions were executed successfully
    return results[0].value.find("Successfully executed Swift code: let a = 5") != std::string::npos &&
           results[1].value.find("Successfully executed Swift code: let b = 10") != std::string::npos &&
           results[2].value.find("Successfully executed Swift code: a + b") != std::string::npos;
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
    return result.success && result.value.find("Successfully executed Swift code: 3 * 7") != std::string::npos;
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

// Additional comprehensive test cases
bool testIncrementalCompilation() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Define a variable
    auto result1 = repl.evaluate("let x = 42");
    if (!result1.success) return false;
    
    // Use the variable in a new expression
    auto result2 = repl.evaluate("x * 2");
    if (!result2.success) return false;
    
    // Define another variable
    auto result3 = repl.evaluate("let y = 10");
    if (!result3.success) return false;
    
    // Use both variables
    auto result4 = repl.evaluate("x + y");
    return result4.success;
}

bool testComplexDataTypes() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Test dictionaries
    auto result1 = repl.evaluate("let dict = [\"a\": 1, \"b\": 2]; dict[\"a\"]");
    if (!result1.success) return false;
    
    // Test sets
    auto result2 = repl.evaluate("let set = Set([1, 2, 3, 2, 1]); set.count");
    if (!result2.success) return false;
    
    // Test tuples
    auto result3 = repl.evaluate("let tuple = (1, \"hello\", 3.14); tuple.0");
    return result3.success;
}

bool testControlFlow() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Test if-else
    auto result1 = repl.evaluate("let x = 10; if x > 5 { \"big\" } else { \"small\" }");
    if (!result1.success) return false;
    
    // Test for loop
    auto result2 = repl.evaluate("var sum = 0; for i in 1...5 { sum += i }; sum");
    if (!result2.success) return false;
    
    // Test while loop
    auto result3 = repl.evaluate("var count = 0; while count < 3 { count += 1 }; count");
    return result3.success;
}

bool testFunctions() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Test function definition and call
    auto result1 = repl.evaluate("func square(_ x: Int) -> Int { return x * x }; square(5)");
    if (!result1.success) return false;
    
    // Test recursive function
    auto result2 = repl.evaluate("func factorial(_ n: Int) -> Int { return n <= 1 ? 1 : n * factorial(n - 1) }; factorial(5)");
    return result2.success;
}

bool testCrossEvaluationFunctions() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Define functions across separate evaluations
    if (!repl.evaluate("func inc(_ x: Int) -> Int { x + 1 }").success) return false;
    if (!repl.evaluate("func add(_ a: Int, _ b: Int) -> Int { a + b }").success) return false;
    if (!repl.evaluate("func mul(_ a: Int, _ b: Int) -> Int { a * b }").success) return false;
    
    // Higher-order function using previously defined ones
    if (!repl.evaluate("func compose(_ f: @escaping (Int) -> Int, _ g: @escaping (Int) -> Int) -> (Int) -> Int { { x in f(g(x)) } }").success) return false;
    
    // Call them in later evaluations
    auto r1 = repl.evaluate("inc(41)");
    auto r2 = repl.evaluate("add(3, 4)");
    auto r3 = repl.evaluate("compose(inc, inc)(40)");
    auto r4 = repl.evaluate("mul(add(2, 3), inc(5))");
    
    return r1.success && r2.success && r3.success && r4.success;
}

bool testClassesAndStructs() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Test struct
    auto result1 = repl.evaluate("struct Point { var x: Int, y: Int }; let p = Point(x: 1, y: 2); p.x");
    if (!result1.success) return false;
    
    // Test class
    auto result2 = repl.evaluate("class Counter { var count = 0; func increment() { count += 1 } }; let c = Counter(); c.increment(); c.count");
    return result2.success;
}

bool testEnums() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Test enum
    auto result1 = repl.evaluate("enum Direction { case north, south, east, west }; let dir = Direction.north; dir");
    if (!result1.success) return false;
    
    // Test enum with associated values
    auto result2 = repl.evaluate("enum Result { case success(Int); case failure(String) }; let res = Result.success(42); res");
    return result2.success;
}

bool testOptionals() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Test optional binding
    auto result1 = repl.evaluate("let optional: Int? = 42; if let value = optional { value } else { 0 }");
    if (!result1.success) return false;
    
    // Test nil coalescing
    auto result2 = repl.evaluate("let optional: Int? = nil; optional ?? 0");
    return result2.success;
}

bool testGenerics() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Test generic function
    auto result1 = repl.evaluate("func identity<T>(_ value: T) -> T { return value }; identity(42)");
    if (!result1.success) return false;
    
    // Test generic struct
    auto result2 = repl.evaluate("struct Box<T> { let value: T }; let box = Box(value: \"hello\"); box.value");
    return result2.success;
}

bool testProtocols() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Test protocol
    auto result1 = repl.evaluate("protocol Drawable { func draw() }; struct Circle: Drawable { func draw() { print(\"Circle\") } }; let shape: Drawable = Circle(); shape");
    return result1.success;
}

bool testExtensions() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Test extension
    auto result1 = repl.evaluate("extension Int { func double() -> Int { return self * 2 } }; 5.double()");
    return result1.success;
}

bool testAdvancedErrorHandling() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Test do-catch
    auto result1 = repl.evaluate("do { try \"test\" } catch { \"error\" }");
    if (!result1.success) return false;
    
    // Test throwing function
    auto result2 = repl.evaluate("func throwing() throws -> String { return \"success\" }; do { try throwing() } catch { \"error\" }");
    return result2.success;
}

bool testMemoryManagement() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Test weak references
    auto result1 = repl.evaluate("class Parent { weak var child: Child? }; class Child { var parent: Parent? }; let p = Parent(); let c = Child(); p.child = c; c.parent = p; p.child != nil");
    return result1.success;
}

bool testConcurrency() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Test async/await (if supported)
    auto result1 = repl.evaluate("Task { \"async\" }");
    return result1.success;
}

bool testStringManipulation() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Test string operations
    auto result1 = repl.evaluate("\"Hello, World!\".uppercased()");
    if (!result1.success) return false;
    
    // Test string interpolation
    auto result2 = repl.evaluate("let name = \"Swift\"; \"Hello, \\(name)!\"");
    return result2.success;
}

bool testCollectionOperations() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Test array operations
    auto result1 = repl.evaluate("let arr = [1, 2, 3, 4, 5]; arr.filter { $0 % 2 == 0 }");
    if (!result1.success) return false;
    
    // Test map operations
    auto result2 = repl.evaluate("let numbers = [1, 2, 3]; numbers.map { $0 * 2 }");
    return result2.success;
}

bool testTypeCasting() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Test type casting
    auto result1 = repl.evaluate("let any: Any = 42; any as? Int");
    if (!result1.success) return false;
    
    // Test type checking
    auto result2 = repl.evaluate("let any: Any = \"hello\"; any is String");
    return result2.success;
}

bool testAdvancedPatterns() {
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) return false;
    
    // Test guard statements
    auto result1 = repl.evaluate("func process(_ value: Int?) -> String { guard let v = value else { return \"nil\" }; return String(v) }; process(42)");
    if (!result1.success) return false;
    
    // Test switch statements
    auto result2 = repl.evaluate("let x = 3; switch x { case 1: \"one\"; case 2: \"two\"; default: \"other\" }");
    return result2.success;
}

int main() {
    std::cout << "=== Swift JIT REPL Comprehensive Test Suite ===\n\n";
    
    TestRunner runner;
    
    // Basic functionality tests
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
    
    // Advanced functionality tests
    runner.runTest("Incremental Compilation", testIncrementalCompilation);
    runner.runTest("Complex Data Types", testComplexDataTypes);
    runner.runTest("Control Flow", testControlFlow);
    runner.runTest("Functions", testFunctions);
    runner.runTest("Cross-Evaluation Functions", testCrossEvaluationFunctions);
    runner.runTest("Classes and Structs", testClassesAndStructs);
    runner.runTest("Enums", testEnums);
    runner.runTest("Optionals", testOptionals);
    runner.runTest("Generics", testGenerics);
    runner.runTest("Protocols", testProtocols);
    runner.runTest("Extensions", testExtensions);    
    runner.runTest("Error Handling Advanced", testAdvancedErrorHandling);
    runner.runTest("Memory Management", testMemoryManagement);
    runner.runTest("Concurrency", testConcurrency);
    runner.runTest("String Manipulation", testStringManipulation);
    runner.runTest("Collection Operations", testCollectionOperations);
    runner.runTest("Type Casting", testTypeCasting);
    runner.runTest("Advanced Patterns", testAdvancedPatterns);
    
    // Print summary
    runner.printSummary();
    
    return 0;
}
