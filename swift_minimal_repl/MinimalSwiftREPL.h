#pragma once

#include <string>
#include <memory>
#include <vector>
#include <optional>

// Forward declarations for LLDB types
namespace lldb {
    class SBDebugger;
    class SBTarget;
    class SBExpressionOptions;
    class SBValue;
    class SBError;
}

namespace SwiftMinimalREPL {

/**
 * Result of evaluating a Swift expression
 */
struct EvaluationResult {
    bool success;
    std::string value;          // String representation of the result value
    std::string type;           // Type name of the result
    std::string error_message;  // Error message if evaluation failed
    
    // Constructor for successful evaluation
    EvaluationResult(const std::string& val, const std::string& typ) 
        : success(true), value(val), type(typ) {}
    
    // Constructor for failed evaluation
    explicit EvaluationResult(const std::string& error) 
        : success(false), error_message(error) {}
};

/**
 * Configuration options for the REPL
 */
struct REPLConfig {
    bool fetch_dynamic_values = true;
    bool allow_jit = true;
    bool ignore_breakpoints = true;
    bool unwind_on_error = true;
    bool generate_debug_info = false;
    bool try_all_threads = false;
    int timeout_usec = 500000; // 0.5 seconds default timeout
};

/**
 * Minimal Swift REPL implementation for programmatic use
 * 
 * This class provides a simple API to evaluate Swift expressions
 * without requiring stdin/stdout interaction. Perfect for server
 * applications that need to evaluate Swift code on demand.
 */
class MinimalSwiftREPL {
public:
    /**
     * Constructor
     * @param config Configuration options for the REPL
     */
    explicit MinimalSwiftREPL(const REPLConfig& config = REPLConfig{});
    
    /**
     * Destructor - ensures proper cleanup
     */
    ~MinimalSwiftREPL();
    
    // Disable copy constructor and assignment
    MinimalSwiftREPL(const MinimalSwiftREPL&) = delete;
    MinimalSwiftREPL& operator=(const MinimalSwiftREPL&) = delete;
    
    // Enable move constructor and assignment
    MinimalSwiftREPL(MinimalSwiftREPL&& other) noexcept;
    MinimalSwiftREPL& operator=(MinimalSwiftREPL&& other) noexcept;
    
    /**
     * Initialize the REPL
     * @return true if initialization was successful
     */
    bool initialize();
    
    /**
     * Check if the REPL is properly initialized
     */
    bool isInitialized() const;
    
    /**
     * Evaluate a Swift expression
     * @param expression The Swift expression to evaluate
     * @return Result of the evaluation
     */
    EvaluationResult evaluate(const std::string& expression);
    
    /**
     * Evaluate multiple Swift expressions in sequence
     * @param expressions Vector of expressions to evaluate
     * @return Vector of results corresponding to each expression
     */
    std::vector<EvaluationResult> evaluateMultiple(const std::vector<std::string>& expressions);
    
    /**
     * Reset the REPL context (clears all variables and state)
     * @return true if reset was successful
     */
    bool reset();
    
    /**
     * Get the last error message
     */
    std::string getLastError() const;
    
    /**
     * Check if LLDB Swift support is available
     * @return true if Swift evaluation is supported
     */
    static bool isSwiftSupportAvailable();

private:
    class Impl; // PIMPL idiom for hiding LLDB dependencies
    std::unique_ptr<Impl> pImpl;
};

/**
 * Convenience function to evaluate a single Swift expression
 * Creates a temporary REPL instance for one-off evaluations
 * @param expression The Swift expression to evaluate
 * @param config Optional configuration
 * @return Result of the evaluation
 */
EvaluationResult evaluateSwiftExpression(const std::string& expression, 
                                       const REPLConfig& config = REPLConfig{});

/**
 * Convenience function to check if Swift REPL functionality is available
 * @return true if the system supports Swift evaluation
 */
bool isSwiftREPLAvailable();

} // namespace SwiftMinimalREPL
