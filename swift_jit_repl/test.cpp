#include "SwiftJITREPL.h"
#include <iostream>
#include <cassert>

int main() {
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
        
        // Test 2: Variable usage
        std::cout << "\n=== Test 2: Variable Usage ===" << std::endl;
        auto result2 = repl.evaluate("print(x)");
        std::cout << "Result: " << (result2.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result2.success) {
            std::cout << "Error: " << result2.error_message << std::endl;
        }
        
        // Test 3: String expression
        std::cout << "\n=== Test 3: String Expression ===" << std::endl;
        auto result3 = repl.evaluate("let greeting = \"Hello, World!\"");
        std::cout << "Result: " << (result3.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result3.success) {
            std::cout << "Error: " << result3.error_message << std::endl;
        }
        
        // Test 4: Print string
        std::cout << "\n=== Test 4: Print String ===" << std::endl;
        auto result4 = repl.evaluate("print(greeting)");
        std::cout << "Result: " << (result4.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result4.success) {
            std::cout << "Error: " << result4.error_message << std::endl;
        }
        
        // Test 5: Arithmetic expression
        std::cout << "\n=== Test 5: Arithmetic Expression ===" << std::endl;
        auto result5 = repl.evaluate("let sum = 10 + 20");
        std::cout << "Result: " << (result5.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result5.success) {
            std::cout << "Error: " << result5.error_message << std::endl;
        }
        
        // Test 6: Print arithmetic result
        std::cout << "\n=== Test 6: Print Arithmetic Result ===" << std::endl;
        auto result6 = repl.evaluate("print(sum)");
        std::cout << "Result: " << (result6.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result6.success) {
            std::cout << "Error: " << result6.error_message << std::endl;
        }
        
        // Test 7: Function definition
        std::cout << "\n=== Test 7: Function Definition ===" << std::endl;
        auto result7 = repl.evaluate("func add(a: Int, b: Int) -> Int { return a + b }");
        std::cout << "Result: " << (result7.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result7.success) {
            std::cout << "Error: " << result7.error_message << std::endl;
        }
        
        // Test 8: Function call
        std::cout << "\n=== Test 8: Function Call ===" << std::endl;
        auto result8 = repl.evaluate("let result = add(a: 5, b: 3)");
        std::cout << "Result: " << (result8.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result8.success) {
            std::cout << "Error: " << result8.error_message << std::endl;
        }
        
        // Test 9: Print function result
        std::cout << "\n=== Test 9: Print Function Result ===" << std::endl;
        auto result9 = repl.evaluate("print(result)");
        std::cout << "Result: " << (result9.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result9.success) {
            std::cout << "Error: " << result9.error_message << std::endl;
        }
        
        // Test 10: Error handling
        std::cout << "\n=== Test 10: Error Handling ===" << std::endl;
        auto result10 = repl.evaluate("let invalid = undefinedVariable");
        std::cout << "Result: " << (result10.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result10.success) {
            std::cout << "Error: " << result10.error_message << std::endl;
        }
        
        std::cout << "\n=== All Tests Completed ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
