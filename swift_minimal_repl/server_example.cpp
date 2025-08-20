#include "MinimalSwiftREPL.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <sstream>
#include <vector>

/**
 * Simple HTTP-like server example showing how to use MinimalSwiftREPL
 * in a server context without stdin/stdout interaction.
 * 
 * This is a demonstration - in a real server you'd use a proper HTTP library.
 */
class SwiftREPLServer {
private:
    SwiftMinimalREPL::MinimalSwiftREPL repl;
    bool initialized = false;
    
public:
    SwiftREPLServer() {
        SwiftMinimalREPL::REPLConfig config;
        config.timeout_usec = 2000000; // 2 second timeout for server requests
        config.unwind_on_error = true;
        config.ignore_breakpoints = true;
        
        repl = SwiftMinimalREPL::MinimalSwiftREPL(config);
    }
    
    bool start() {
        std::cout << "🚀 Starting Swift REPL Server..." << std::endl;
        
        if (!SwiftMinimalREPL::isSwiftREPLAvailable()) {
            std::cerr << "❌ Swift REPL is not available" << std::endl;
            return false;
        }
        
        if (!repl.initialize()) {
            std::cerr << "❌ Failed to initialize REPL: " << repl.getLastError() << std::endl;
            return false;
        }
        
        initialized = true;
        std::cout << "✅ Swift REPL Server started successfully" << std::endl;
        return true;
    }
    
    /**
     * Process a client request to evaluate Swift code
     * Returns a JSON-like response string
     */
    std::string processRequest(const std::string& expression) {
        if (!initialized) {
            return R"({"success": false, "error": "Server not initialized"})";
        }
        
        std::cout << "📝 Processing request: " << expression << std::endl;
        
        auto result = repl.evaluate(expression);
        
        std::ostringstream response;
        response << "{";
        response << "\"success\": " << (result.success ? "true" : "false");
        
        if (result.success) {
            response << ", \"value\": \"" << escapeJson(result.value) << "\"";
            response << ", \"type\": \"" << escapeJson(result.type) << "\"";
        } else {
            response << ", \"error\": \"" << escapeJson(result.error_message) << "\"";
        }
        
        response << "}";
        return response.str();
    }
    
    /**
     * Process multiple expressions in a batch
     */
    std::string processBatchRequest(const std::vector<std::string>& expressions) {
        if (!initialized) {
            return R"({"success": false, "error": "Server not initialized"})";
        }
        
        std::cout << "📝 Processing batch request with " << expressions.size() << " expressions" << std::endl;
        
        auto results = repl.evaluateMultiple(expressions);
        
        std::ostringstream response;
        response << "{\"success\": true, \"results\": [";
        
        for (size_t i = 0; i < results.size(); ++i) {
            if (i > 0) response << ", ";
            
            response << "{";
            response << "\"success\": " << (results[i].success ? "true" : "false");
            
            if (results[i].success) {
                response << ", \"value\": \"" << escapeJson(results[i].value) << "\"";
                response << ", \"type\": \"" << escapeJson(results[i].type) << "\"";
            } else {
                response << ", \"error\": \"" << escapeJson(results[i].error_message) << "\"";
            }
            
            response << "}";
        }
        
        response << "]}";
        return response.str();
    }
    
    /**
     * Reset the REPL context
     */
    std::string resetContext() {
        if (!initialized) {
            return R"({"success": false, "error": "Server not initialized"})";
        }
        
        std::cout << "🔄 Resetting REPL context" << std::endl;
        
        bool success = repl.reset();
        if (success) {
            return R"({"success": true, "message": "Context reset successfully"})";
        } else {
            return R"({"success": false, "error": "Failed to reset context"})";
        }
    }
    
private:
    /**
     * Simple JSON string escaping
     */
    std::string escapeJson(const std::string& input) {
        std::string output;
        output.reserve(input.size());
        
        for (char c : input) {
            switch (c) {
                case '"': output += "\\\""; break;
                case '\\': output += "\\\\"; break;
                case '\n': output += "\\n"; break;
                case '\r': output += "\\r"; break;
                case '\t': output += "\\t"; break;
                default: output += c; break;
            }
        }
        
        return output;
    }
};

/**
 * Simulate client requests to demonstrate server usage
 */
void simulateClientRequests(SwiftREPLServer& server) {
    std::cout << "\n🔄 Simulating client requests...\n" << std::endl;
    
    // Request 1: Simple arithmetic
    std::cout << "📞 Client Request 1:" << std::endl;
    std::string response1 = server.processRequest("let x = 10; let y = 20; x + y");
    std::cout << "📤 Response: " << response1 << std::endl;
    std::cout << std::endl;
    
    // Request 2: String manipulation
    std::cout << "📞 Client Request 2:" << std::endl;
    std::string response2 = server.processRequest("\"Hello, Server!\".uppercased()");
    std::cout << "📤 Response: " << response2 << std::endl;
    std::cout << std::endl;
    
    // Request 3: Array operations
    std::cout << "📞 Client Request 3:" << std::endl;
    std::string response3 = server.processRequest("let numbers = [1, 2, 3, 4, 5]; numbers.map { $0 * 2 }");
    std::cout << "📤 Response: " << response3 << std::endl;
    std::cout << std::endl;
    
    // Request 4: Batch request
    std::cout << "📞 Client Batch Request:" << std::endl;
    std::vector<std::string> batchExpressions = {
        "let name = \"Swift\"",
        "let version = 5.9",
        "\"\\(name) \\(version) is awesome!\"",
        "name.count + Int(version)"
    };
    std::string batchResponse = server.processBatchRequest(batchExpressions);
    std::cout << "📤 Batch Response: " << batchResponse << std::endl;
    std::cout << std::endl;
    
    // Request 5: Error case
    std::cout << "📞 Client Request with Error:" << std::endl;
    std::string errorResponse = server.processRequest("undefinedVariable + 42");
    std::cout << "📤 Error Response: " << errorResponse << std::endl;
    std::cout << std::endl;
    
    // Request 6: Reset context
    std::cout << "📞 Client Reset Request:" << std::endl;
    std::string resetResponse = server.resetContext();
    std::cout << "📤 Reset Response: " << resetResponse << std::endl;
    std::cout << std::endl;
    
    // Request 7: After reset (previous variables should be gone)
    std::cout << "📞 Client Request After Reset:" << std::endl;
    std::string afterResetResponse = server.processRequest("name"); // This should fail
    std::cout << "📤 Response: " << afterResetResponse << std::endl;
    std::cout << std::endl;
}

int main() {
    std::cout << "🌐 Swift REPL Server Example\n" << std::endl;
    
    // Create and start server
    SwiftREPLServer server;
    if (!server.start()) {
        return 1;
    }
    
    // Simulate some client requests
    simulateClientRequests(server);
    
    std::cout << "🎉 Server example completed!" << std::endl;
    std::cout << "\nIn a real server implementation, you would:" << std::endl;
    std::cout << "• Use a proper HTTP server library (like cpp-httplib, Crow, etc.)" << std::endl;
    std::cout << "• Handle concurrent requests with thread pools" << std::endl;
    std::cout << "• Add proper authentication and rate limiting" << std::endl;
    std::cout << "• Implement request validation and sanitization" << std::endl;
    std::cout << "• Add logging and monitoring" << std::endl;
    std::cout << "• Handle resource cleanup and memory management" << std::endl;
    
    return 0;
}
