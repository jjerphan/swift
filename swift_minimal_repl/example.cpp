#include "MinimalSwiftREPL.h"
#include <iostream>
#include <vector>

int main() {
    std::cout << "🚀 Swift Minimal REPL Example\n" << std::endl;
    
    // Check if Swift REPL is available
    if (!SwiftMinimalREPL::isSwiftREPLAvailable()) {
        std::cerr << "❌ Swift REPL is not available on this system" << std::endl;
        return 1;
    }
    std::cout << "✅ Swift REPL is available" << std::endl;
    
    // Create REPL instance with custom configuration
    SwiftMinimalREPL::REPLConfig config;
    config.timeout_usec = 1000000; // 1 second timeout
    config.generate_debug_info = false;
    
    SwiftMinimalREPL::MinimalSwiftREPL repl(config);
    
    // Initialize the REPL
    if (!repl.initialize()) {
        std::cerr << "❌ Failed to initialize REPL: " << repl.getLastError() << std::endl;
        return 1;
    }
    std::cout << "✅ REPL initialized successfully\n" << std::endl;
    
    // Example 1: Simple arithmetic
    std::cout << "🧮 Example 1: Simple arithmetic" << std::endl;
    auto result1 = repl.evaluate("let a = 10; let b = 20; a + b");
    if (result1.success) {
        std::cout << "   Expression: let a = 10; let b = 20; a + b" << std::endl;
        std::cout << "   Result: " << result1.value << " (type: " << result1.type << ")" << std::endl;
    } else {
        std::cout << "   ❌ Error: " << result1.error_message << std::endl;
    }
    std::cout << std::endl;
    
    // Example 2: String operations
    std::cout << "📝 Example 2: String operations" << std::endl;
    auto result2 = repl.evaluate("let greeting = \"Hello, Swift REPL!\"; greeting.count");
    if (result2.success) {
        std::cout << "   Expression: let greeting = \"Hello, Swift REPL!\"; greeting.count" << std::endl;
        std::cout << "   Result: " << result2.value << " (type: " << result2.type << ")" << std::endl;
    } else {
        std::cout << "   ❌ Error: " << result2.error_message << std::endl;
    }
    std::cout << std::endl;
    
    // Example 3: Array operations
    std::cout << "📊 Example 3: Array operations" << std::endl;
    auto result3 = repl.evaluate("let numbers = [1, 2, 3, 4, 5]; numbers.reduce(0, +)");
    if (result3.success) {
        std::cout << "   Expression: let numbers = [1, 2, 3, 4, 5]; numbers.reduce(0, +)" << std::endl;
        std::cout << "   Result: " << result3.value << " (type: " << result3.type << ")" << std::endl;
    } else {
        std::cout << "   ❌ Error: " << result3.error_message << std::endl;
    }
    std::cout << std::endl;
    
    // Example 4: Multiple expressions at once
    std::cout << "🔢 Example 4: Multiple expressions" << std::endl;
    std::vector<std::string> expressions = {
        "let x = 42",
        "let y = x * 2",
        "let message = \"The answer is \\(y)\"",
        "message.uppercased()"
    };
    
    auto results = repl.evaluateMultiple(expressions);
    for (size_t i = 0; i < expressions.size(); ++i) {
        std::cout << "   Expression " << (i + 1) << ": " << expressions[i] << std::endl;
        if (results[i].success) {
            std::cout << "   Result: " << results[i].value << " (type: " << results[i].type << ")" << std::endl;
        } else {
            std::cout << "   ❌ Error: " << results[i].error_message << std::endl;
        }
    }
    std::cout << std::endl;
    
    // Example 5: Error handling
    std::cout << "⚠️  Example 5: Error handling" << std::endl;
    auto result5 = repl.evaluate("let invalid = undefinedVariable + 42");
    if (result5.success) {
        std::cout << "   Unexpected success: " << result5.value << std::endl;
    } else {
        std::cout << "   Expected error: " << result5.error_message << std::endl;
    }
    std::cout << std::endl;
    
    // Example 6: Using convenience function
    std::cout << "🎯 Example 6: Convenience function" << std::endl;
    auto result6 = SwiftMinimalREPL::evaluateSwiftExpression("\"Swift REPL\".reversed()");
    if (result6.success) {
        std::cout << "   One-shot evaluation: " << result6.value << " (type: " << result6.type << ")" << std::endl;
    } else {
        std::cout << "   ❌ Error: " << result6.error_message << std::endl;
    }
    std::cout << std::endl;
    
    std::cout << "🎉 Examples completed!" << std::endl;
    return 0;
}
