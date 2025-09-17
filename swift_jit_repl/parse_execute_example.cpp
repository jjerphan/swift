#include "SwiftJITREPL.h"
#include <iostream>
#include <memory>

// Include necessary Swift headers
#include "swift/Frontend/Frontend.h"
#include "llvm/Support/Error.h"

int main() {
    std::cout << "=== Swift Parse and Execute Example ===" << std::endl;
    
    // Create and initialize SwiftJITREPL
    SwiftJITREPL::REPLConfig config;
    SwiftJITREPL::SwiftJITREPL repl(config);
    
    if (!repl.initialize()) {
        std::cerr << "Failed to initialize SwiftJITREPL: " << repl.getLastError() << std::endl;
        return 1;
    }
    
    std::cout << "✓ SwiftJITREPL initialized successfully" << std::endl;
    
    // Test code to parse and execute
    std::string testCode = "let x = 42";
    std::cout << "\nTesting code: " << testCode << std::endl;
    
    // Step 1: Parse the code
    std::cout << "1. Parsing code..." << std::endl;
    auto ptuOrError = repl.Parse(testCode);
    if (!ptuOrError) {
        std::cerr << "Parse error: " << llvm::toString(ptuOrError.takeError()) << std::endl;
        return 1;
    }
    
    auto& ptu = *ptuOrError;
    std::cout << "✓ Parsed successfully" << std::endl;
    std::cout << "  PTU input code: " << ptu.InputCode << std::endl;
    
    // Step 2: Execute the PTU
    std::cout << "2. Executing PTU..." << std::endl;
    auto execError = repl.Execute(ptu);
    if (execError) {
        std::cerr << "Execution error: " << llvm::toString(std::move(execError)) << std::endl;
        return 1;
    }
    
    std::cout << "✓ Executed successfully" << std::endl;
    
    // Test ParseAndExecute (combines both steps)
    std::cout << "\nTesting ParseAndExecute..." << std::endl;
    std::string testCode2 = "print(\"Hello from Swift!\")";
    std::cout << "Code: " << testCode2 << std::endl;
    
    SwiftValue resultValue2;
    auto parseExecError = repl.ParseAndExecute(testCode2, &resultValue2);
    if (parseExecError) {
        std::cerr << "ParseAndExecute error: " << llvm::toString(std::move(parseExecError)) << std::endl;
        return 1;
    }
    
    std::cout << "✓ ParseAndExecute completed successfully" << std::endl;
    
    // Test ParseAndExecute with result value (new overload)
    std::cout << "\nTesting ParseAndExecute with result value..." << std::endl;
    std::string testCode3 = "42 + 1";
    SwiftValue resultValue;
    std::cout << "Code: " << testCode3 << std::endl;
    
    auto parseExecWithResultError = repl.ParseAndExecute(testCode3, &resultValue);
    if (parseExecWithResultError) {
        std::cerr << "ParseAndExecute error: " << llvm::toString(std::move(parseExecWithResultError)) << std::endl;
        return 1;
    }
    
    std::cout << "✓ ParseAndExecute with result completed successfully" << std::endl;
    if (resultValue.isValid()) {
        std::cout << "  Result value: " << resultValue.getValue() << std::endl;
        std::cout << "  Result type: " << resultValue.getType() << std::endl;
    } else {
        std::cout << "  No result value captured" << std::endl;
    }
    
    // Test SwiftInterpreter::Execute method
    std::cout << "\n=== Example 2: SwiftInterpreter::Execute ===" << std::endl;
    std::string testCode4 = "let y = 100";
    std::cout << "Code: " << testCode4 << std::endl;
    
    // Parse first
    auto ptuOrError2 = repl.Parse(testCode4);
    if (!ptuOrError2) {
        std::cerr << "Parse error: " << llvm::toString(ptuOrError2.takeError()) << std::endl;
        return 1;
    }
    
    auto& ptu2 = *ptuOrError2;
    std::cout << "✓ Parsed successfully" << std::endl;
    
    // Execute using SwiftInterpreter::Execute
    auto interpreter = repl.getInterpreter();
    if (!interpreter) {
        std::cerr << "Failed to get interpreter" << std::endl;
        return 1;
    }
    
    auto execError2 = interpreter->Execute(ptu2);
    if (execError2) {
        std::cerr << "Execute error: " << llvm::toString(std::move(execError2)) << std::endl;
        return 1;
    }
    
    std::cout << "✓ Executed successfully using SwiftInterpreter::Execute" << std::endl;
    
    std::cout << "\n=== Example completed ===" << std::endl;
    std::cout << "\nMethods demonstrated:" << std::endl;
    std::cout << "- Parse(code): Parses Swift code into a PartialTranslationUnit" << std::endl;
    std::cout << "- Execute(ptu): Executes a PartialTranslationUnit" << std::endl;
    std::cout << "- ParseAndExecute(code): Combines parsing and execution in one step" << std::endl;
    std::cout << "- ParseAndExecute(code, resultValue): Same as above but returns result value" << std::endl;
    std::cout << "- Similar to Clang's Interpreter::Parse and Interpreter::Execute" << std::endl;
    
    return 0;
}
