#include <iostream>
#include <string>
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBExpressionOptions.h"
#include "lldb/API/SBValue.h"

int main() {
    std::cout << "🚀 Testing SwiftREPL with C++ API..." << std::endl;
    
    try {
        // Initialize LLDB
        lldb::SBDebugger::Initialize();
        
        // Create debugger instance
        lldb::SBDebugger debugger = lldb::SBDebugger::Create(false);
        if (!debugger.IsValid()) {
            std::cerr << "❌ Failed to create LLDB debugger" << std::endl;
            return 1;
        }
        std::cout << "✅ LLDB Debugger created successfully" << std::endl;
        
        // Set debugger to synchronous mode
        debugger.SetAsync(false);
        
        // Create a target (empty target for expression evaluation)
        lldb::SBTarget target = debugger.CreateTarget("");
        if (!target.IsValid()) {
            std::cerr << "❌ Failed to create target" << std::endl;
            return 1;
        }
        std::cout << "✅ Target created successfully" << std::endl;
        
        // Test Swift expression evaluation
        std::cout << "\n🧪 Testing Swift expression evaluation..." << std::endl;
        
        // Simple Swift addition expression
        std::string swift_expr = "let a = 10; let b = 20; a + b";
        std::cout << "Expression: " << swift_expr << std::endl;
        
        // Create expression options
        lldb::SBExpressionOptions options;
        options.SetLanguage(lldb::eLanguageTypeSwift);
        options.SetFetchDynamicValue(lldb::eDynamicDontRunTarget);
        
        // Evaluate the Swift expression
        lldb::SBValue result = target.EvaluateExpression(swift_expr.c_str(), options);
        
        if (result.IsValid()) {
            std::cout << "✅ Swift expression evaluated successfully!" << std::endl;
            std::cout << "Result: " << result.GetValue() << std::endl;
            std::cout << "Type: " << result.GetTypeName() << std::endl;
        } else {
            std::cout << "❌ Failed to evaluate Swift expression" << std::endl;
            std::cout << "Error: " << result.GetError().GetCString() << std::endl;
        }
        
        // Test another Swift expression
        std::cout << "\n🧪 Testing another Swift expression..." << std::endl;
        std::string swift_expr2 = "let greeting = \"Hello, SwiftREPL!\"; greeting.count";
        std::cout << "Expression: " << swift_expr2 << std::endl;
        
        lldb::SBValue result2 = target.EvaluateExpression(swift_expr2.c_str(), options);
        
        if (result2.IsValid()) {
            std::cout << "✅ Second expression evaluated successfully!" << std::endl;
            std::cout << "Result: " << result2.GetValue() << std::endl;
            std::cout << "Type: " << result2.GetTypeName() << std::endl;
        } else {
            std::cout << "❌ Failed to evaluate second expression" << std::endl;
            std::cout << "Error: " << result2.GetError().GetCString() << std::endl;
        }
        
        // Test Swift array operations
        std::cout << "\n🧪 Testing Swift array operations..." << std::endl;
        std::string swift_expr3 = "let numbers = [1, 2, 3, 4, 5]; numbers.reduce(0, +)";
        std::cout << "Expression: " << swift_expr3 << std::endl;
        
        lldb::SBValue result3 = target.EvaluateExpression(swift_expr3.c_str(), options);
        
        if (result3.IsValid()) {
            std::cout << "✅ Array operation evaluated successfully!" << std::endl;
            std::cout << "Result: " << result3.GetValue() << std::endl;
            std::cout << "Type: " << result3.GetTypeName() << std::endl;
        } else {
            std::cout << "❌ Failed to evaluate array operation" << std::endl;
            std::cout << "Error: " << result3.GetError().GetCString() << std::endl;
        }
        
        std::cout << "\n🎉 SwiftREPL test completed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception occurred: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "❌ Unknown exception occurred" << std::endl;
        return 1;
    }
    
    // Cleanup
    lldb::SBDebugger::Terminate();
    
    return 0;
}
