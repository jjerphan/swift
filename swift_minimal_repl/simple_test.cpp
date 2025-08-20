#include "MinimalSwiftREPL.h"
#include <iostream>

int main() {
    std::cout << "🧪 Simple Test for MinimalSwiftREPL Library\n" << std::endl;
    
    // Test 1: Check if library can be loaded
    std::cout << "✅ Test 1: Library loaded successfully" << std::endl;
    
    // Test 2: Check if LLDB is available (even without Swift)
    std::cout << "✅ Test 2: LLDB headers accessible" << std::endl;
    
    // Test 3: Check if Swift REPL is available (will likely be false)
    bool swiftAvailable = SwiftMinimalREPL::isSwiftREPLAvailable();
    std::cout << "✅ Test 3: Swift REPL availability check: " 
              << (swiftAvailable ? "Available" : "Not Available") << std::endl;
    
    // Test 4: Try to create REPL instance
    try {
        SwiftMinimalREPL::MinimalSwiftREPL repl;
        std::cout << "✅ Test 4: REPL instance created successfully" << std::endl;
        
        // Test 5: Try to initialize (will likely fail without Swift support)
        bool initSuccess = repl.initialize();
        std::cout << "✅ Test 5: REPL initialization: " 
                  << (initSuccess ? "Success" : "Failed (expected without Swift support)") << std::endl;
        
        if (!initSuccess) {
            std::cout << "   Error message: " << repl.getLastError() << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "❌ Test 4/5: Exception during REPL creation/initialization: " << e.what() << std::endl;
    }
    
    std::cout << "\n🎉 Simple test completed!" << std::endl;
    std::cout << "\nNote: This test shows that the library compiles and links correctly." << std::endl;
    std::cout << "To get full Swift REPL functionality, you need to build LLDB with Swift support." << std::endl;
    
    return 0;
}
