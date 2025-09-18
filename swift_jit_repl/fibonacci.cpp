#include "SwiftJITREPL.h"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "Testing Swift Fibonacci function interpretation..." << std::endl;
    
    try {
        // Create REPL instance
        SwiftJITREPL::REPLConfig config;
        SwiftJITREPL::SwiftJITREPL repl(config);
        
        // Test 1: Define Fibonacci function
        std::cout << "\n=== Test 1: Define Fibonacci Function ===" << std::endl;
        auto result1 = repl.evaluate(R"(
func fibonacci(_ n: Int) -> Int {
    if n <= 1 {
        return n
    }
    return fibonacci(n - 1) + fibonacci(n - 2)
}
)");
        std::cout << "Result: " << (result1.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result1.success) {
            std::cout << "Error: " << result1.error_message << std::endl;
        }
        
        // Test 2: Call Fibonacci function with small values
        std::cout << "\n=== Test 2: Call Fibonacci(5) ===" << std::endl;
        auto result2 = repl.evaluate("let fib5 = fibonacci(5)");
        std::cout << "Result: " << (result2.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result2.success) {
            std::cout << "Error: " << result2.error_message << std::endl;
        }
        
        // Test 3: Print the result
        std::cout << "\n=== Test 3: Print Fibonacci(5) ===" << std::endl;
        auto result3 = repl.evaluate("print(fib5)");
        std::cout << "Result: " << (result3.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result3.success) {
            std::cout << "Error: " << result3.error_message << std::endl;
        }
        
        // Test 4: Call Fibonacci function with another value
        std::cout << "\n=== Test 4: Call Fibonacci(10) ===" << std::endl;
        auto result4 = repl.evaluate("let fib10 = fibonacci(10)");
        std::cout << "Result: " << (result4.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result4.success) {
            std::cout << "Error: " << result4.error_message << std::endl;
        }
        
        // Test 5: Print the second result
        std::cout << "\n=== Test 5: Print Fibonacci(10) ===" << std::endl;
        auto result5 = repl.evaluate("print(fib10)");
        std::cout << "Result: " << (result5.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result5.success) {
            std::cout << "Error: " << result5.error_message << std::endl;
        }
        
        // Test 6: Test with a loop to see multiple calls
        std::cout << "\n=== Test 6: Fibonacci Sequence ===" << std::endl;
        auto result6 = repl.evaluate(
            "for i in 0...7 {\n"
            "    print(\"fibonacci(\\(i)) = \\(fibonacci(i))\")\n"
            "}"
        );
        std::cout << "Result: " << (result6.success ? "SUCCESS" : "FAILED") << std::endl;
        if (!result6.success) {
            std::cout << "Error: " << result6.error_message << std::endl;
        }
        
        std::cout << "\n=== Fibonacci Test Completed ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
