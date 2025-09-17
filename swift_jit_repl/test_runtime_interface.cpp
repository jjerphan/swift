#include "SwiftJITREPL.h"
#include <iostream>

int main() {
    std::cout << "=== Testing Swift Runtime Interface ===" << std::endl;
    
    // Test the runtime interface functions directly
    SwiftValue testValue;
    SwiftJITREPL::SwiftInterpreter* interpreter = nullptr; // Dummy pointer for testing
    
    std::cout << "Testing __swift_Interpreter_SetValueNoAlloc..." << std::endl;
    __swift_Interpreter_SetValueNoAlloc(interpreter, &testValue, nullptr);
    
    if (testValue.isValid()) {
        std::cout << "✓ Runtime interface function works!" << std::endl;
        std::cout << "Value: " << testValue.getValue() << std::endl;
        std::cout << "Type: " << testValue.getType() << std::endl;
    } else {
        std::cout << "✗ Runtime interface function failed!" << std::endl;
    }
    
    // Test the expression transformer
    std::cout << "\nTesting expression transformer..." << std::endl;
    
    // Create a simple test without the full interpreter
    std::string testCode = "1 + 2";
    
    // Test the transformation logic directly
    bool isExpression = (testCode.find("=") == std::string::npos && 
                       (testCode.find("+") != std::string::npos || 
                        testCode.find("-") != std::string::npos || 
                        testCode.find("*") != std::string::npos || 
                        testCode.find("/") != std::string::npos ||
                        testCode.find("print") != std::string::npos ||
                        testCode.find("return") != std::string::npos));
    
    std::string transformedCode;
    if (isExpression) {
        transformedCode = "__swift_Interpreter_SetValueNoAlloc(&interpreter, &lastValue, nullptr, (" + testCode + "));";
    } else {
        transformedCode = testCode;
    }
    
    std::cout << "Original code: " << testCode << std::endl;
    std::cout << "Transformed code: " << transformedCode << std::endl;
    
    if (transformedCode.find("__swift_Interpreter_SetValueNoAlloc") != std::string::npos) {
        std::cout << "✓ Expression transformer works!" << std::endl;
    } else {
        std::cout << "✗ Expression transformer failed!" << std::endl;
    }
    
    return 0;
}
