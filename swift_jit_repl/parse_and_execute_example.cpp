#include "SwiftJITREPL.h"
#include <iostream>
#include <memory>

// Include necessary Swift headers
#include "swift/Frontend/Frontend.h"
#include "llvm/Support/Error.h"

int main() {
    std::cout << "=== Swift ParseAndExecute Example ===" << std::endl;
    
    // Create and initialize SwiftJITREPL
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) {
        std::cerr << "Failed to initialize SwiftJITREPL: " << repl.getLastError() << std::endl;
        return 1;
    }
    
    std::cout << "✓ SwiftJITREPL initialized successfully" << std::endl;
    
    // Test expressions and statements
    std::vector<std::string> testCases = {
        "let x = 42",
        "let y = 10",
        "x + y",
        "print(\"Hello from Swift!\")",
        "func add(a: Int, b: Int) -> Int { return a + b }",
        "add(a: 5, b: 3)"
    };
    
    std::cout << "\nTesting ParseAndExecute method:" << std::endl;
    std::cout << "=================================" << std::endl;
    
    for (const auto& code : testCases) {
        std::cout << "\nCode: " << code << std::endl;
        
        SwiftValue result;
        auto error = repl.ParseAndExecute(code, &result);
        
        if (error) {
            std::cerr << "Error: " << llvm::toString(std::move(error)) << std::endl;
        } else {
            std::cout << "✓ Executed successfully" << std::endl;
            if (result.isValid()) {
                std::cout << "  Result: " << result.getValue() << " (Type: " << result.getType() << ")" << std::endl;
            }
        }
    }
    
    std::cout << "\n=== Example completed ===" << std::endl;
    std::cout << "\nThe ParseAndExecute method:" << std::endl;
    std::cout << "- Parses Swift code using IncrementalParser" << std::endl;
    std::cout << "- Executes code using IncrementalExecutor" << std::endl;
    std::cout << "- Returns the result value in the output parameter" << std::endl;
    std::cout << "- Similar to Clang's Interpreter::ParseAndExecute" << std::endl;
    
    return 0;
}
