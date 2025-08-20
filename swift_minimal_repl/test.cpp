#include "MinimalSwiftREPL.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <functional>

/**
 * Simple test suite for MinimalSwiftREPL
 */
class TestSuite {
private:
    int totalTests = 0;
    int passedTests = 0;
    
public:
    void runTest(const std::string& testName, bool condition) {
        totalTests++;
        std::cout << "🧪 " << testName << ": ";
        if (condition) {
            std::cout << "✅ PASS" << std::endl;
            passedTests++;
        } else {
            std::cout << "❌ FAIL" << std::endl;
        }
    }
    
    void runTest(const std::string& testName, std::function<bool()> testFunc) {
        totalTests++;
        std::cout << "🧪 " << testName << ": ";
        try {
            bool result = testFunc();
            if (result) {
                std::cout << "✅ PASS" << std::endl;
                passedTests++;
            } else {
                std::cout << "❌ FAIL" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cout << "❌ FAIL (Exception: " << e.what() << ")" << std::endl;
        } catch (...) {
            std::cout << "❌ FAIL (Unknown exception)" << std::endl;
        }
    }
    
    void printSummary() {
        std::cout << "\n📊 Test Summary:" << std::endl;
        std::cout << "   Total tests: " << totalTests << std::endl;
        std::cout << "   Passed: " << passedTests << std::endl;
        std::cout << "   Failed: " << (totalTests - passedTests) << std::endl;
        
        if (passedTests == totalTests) {
            std::cout << "🎉 All tests passed!" << std::endl;
        } else {
            std::cout << "⚠️  Some tests failed!" << std::endl;
        }
    }
    
    bool allTestsPassed() const {
        return passedTests == totalTests;
    }
};

int main() {
    std::cout << "🔬 MinimalSwiftREPL Test Suite\n" << std::endl;
    
    TestSuite suite;
    
    // Test 1: Check if Swift REPL is available
    suite.runTest("Swift REPL availability check", 
                 SwiftMinimalREPL::isSwiftREPLAvailable());
    
    // If Swift REPL is not available, skip the rest of the tests
    if (!SwiftMinimalREPL::isSwiftREPLAvailable()) {
        std::cout << "\n⚠️  Swift REPL is not available. Skipping remaining tests." << std::endl;
        suite.printSummary();
        return suite.allTestsPassed() ? 0 : 1;
    }
    
    // Test 2: REPL initialization
    SwiftMinimalREPL::MinimalSwiftREPL repl;
    suite.runTest("REPL initialization", [&]() {
        return repl.initialize();
    });
    
    // Test 3: Check if REPL is initialized
    suite.runTest("REPL initialized state check", repl.isInitialized());
    
    // Test 4: Simple arithmetic evaluation
    suite.runTest("Simple arithmetic evaluation", [&]() {
        auto result = repl.evaluate("2 + 3");
        return result.success && result.value == "5";
    });
    
    // Test 5: String evaluation
    suite.runTest("String evaluation", [&]() {
        auto result = repl.evaluate("\"Hello\".count");
        return result.success && result.value == "5";
    });
    
    // Test 6: Variable assignment and usage
    suite.runTest("Variable assignment and usage", [&]() {
        auto result1 = repl.evaluate("let x = 42");
        auto result2 = repl.evaluate("x * 2");
        return result1.success && result2.success && result2.value == "84";
    });
    
    // Test 7: Array operations
    suite.runTest("Array operations", [&]() {
        auto result = repl.evaluate("[1, 2, 3].count");
        return result.success && result.value == "3";
    });
    
    // Test 8: Error handling
    suite.runTest("Error handling for invalid expression", [&]() {
        auto result = repl.evaluate("undefinedVariable + 1");
        return !result.success && !result.error_message.empty();
    });
    
    // Test 9: Multiple expressions
    suite.runTest("Multiple expressions evaluation", [&]() {
        std::vector<std::string> expressions = {
            "let a = 10",
            "let b = 20", 
            "a + b"
        };
        auto results = repl.evaluateMultiple(expressions);
        return results.size() == 3 && 
               results[0].success && 
               results[1].success && 
               results[2].success && 
               results[2].value == "30";
    });
    
    // Test 10: Context reset
    suite.runTest("Context reset functionality", [&]() {
        // First, set a variable
        auto result1 = repl.evaluate("let testVar = 123");
        if (!result1.success) return false;
        
        // Reset the context
        bool resetSuccess = repl.reset();
        if (!resetSuccess) return false;
        
        // Try to access the variable (should fail)
        auto result2 = repl.evaluate("testVar");
        return !result2.success; // Should fail because variable is no longer defined
    });
    
    // Test 11: Convenience function
    suite.runTest("Convenience function evaluation", [&]() {
        auto result = SwiftMinimalREPL::evaluateSwiftExpression("5 * 6");
        return result.success && result.value == "30";
    });
    
    // Test 12: Configuration test
    suite.runTest("Custom configuration", [&]() {
        SwiftMinimalREPL::REPLConfig config;
        config.timeout_usec = 100000; // 0.1 second
        
        SwiftMinimalREPL::MinimalSwiftREPL customRepl(config);
        bool initSuccess = customRepl.initialize();
        if (!initSuccess) return false;
        
        auto result = customRepl.evaluate("1 + 1");
        return result.success && result.value == "2";
    });
    
    // Test 13: Type information
    suite.runTest("Type information retrieval", [&]() {
        auto result = repl.evaluate("42");
        return result.success && !result.type.empty();
    });
    
    // Test 14: Complex expression
    suite.runTest("Complex expression evaluation", [&]() {
        auto result = repl.evaluate("let words = [\"Swift\", \"is\", \"awesome\"]; words.joined(separator: \" \")");
        return result.success && result.value.find("Swift is awesome") != std::string::npos;
    });
    
    // Test 15: Move semantics
    suite.runTest("Move constructor and assignment", [&]() {
        SwiftMinimalREPL::MinimalSwiftREPL repl1;
        bool init1 = repl1.initialize();
        if (!init1) return false;
        
        // Test move constructor
        SwiftMinimalREPL::MinimalSwiftREPL repl2 = std::move(repl1);
        auto result1 = repl2.evaluate("100 + 200");
        if (!result1.success || result1.value != "300") return false;
        
        // Test move assignment
        SwiftMinimalREPL::MinimalSwiftREPL repl3;
        repl3 = std::move(repl2);
        auto result2 = repl3.evaluate("\"Test\".uppercased()");
        return result2.success;
    });
    
    suite.printSummary();
    return suite.allTestsPassed() ? 0 : 1;
}
