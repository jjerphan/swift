#pragma once

#include <string>
#include <memory>
#include <vector>

// Forward declarations
namespace llvm {
    class Error;
    template<typename T> class Expected;
}

namespace swift {
    class CompilerInstance;
    class SwiftJIT;
    class SILModule;
}

namespace SwiftJITREPL {

/**
 * Configuration options for the SIL-based JIT REPL
 */
struct REPLConfig {
    bool enable_optimizations = false;
    bool generate_debug_info = false;
    int timeout_ms = 5000;
    std::string stdlib_path = "";
    std::vector<std::string> module_search_paths;
};

/**
 * Result of evaluating a Swift expression
 */
 struct EvaluationResult {
    bool success;
    std::string error_message;  // Error message if evaluation failed
    int exit_code;              // Exit code from JIT execution
    
    // Constructor for successful evaluation
    EvaluationResult() : success(true), exit_code(0) {}
    
    // Constructor for failed evaluation
    explicit EvaluationResult(const std::string& error) 
        : success(false), error_message(error), exit_code(-1) {}
    
    // Constructor for evaluation with exit code
    explicit EvaluationResult(int code) 
        : success(code == 0), exit_code(code) {
        if (code != 0) {
            error_message = "Execution failed with exit code: " + std::to_string(code);
        }
    }
};

/**
 * SIL-based Swift JIT REPL implementation
 * 
 * This implementation stops at SIL level and uses EagerSwiftMaterializationUnit
 * to JIT the resulting SILModule without lowering to LLVM IR.
 */
class SwiftJITREPL {
public:
    /**
     * Constructor
     * @param config Configuration options for the REPL
     */
    explicit SwiftJITREPL(const REPLConfig& config = REPLConfig{});
    
    /**
     * Destructor
     */
    ~SwiftJITREPL();
    
    // Disable copy constructor and assignment
    SwiftJITREPL(const SwiftJITREPL&) = delete;
    SwiftJITREPL& operator=(const SwiftJITREPL&) = delete;
    
    /**
     * Evaluate a Swift expression
     * @param expression The Swift expression to evaluate
     * @return Result of the evaluation
     */
    EvaluationResult evaluate(const std::string& expression);
    // Execute all materialized units via the JIT (runs synthesized/main entry)
    // executeAll() removed; evaluation runs main internally
    
    /**
     * Reset the REPL context
     * @return true if reset was successful
     */
    bool reset();
    
    /**
     * Get the last error message
     */
    std::string getLastError() const;
    
    /**
     * Check if Swift JIT support is available
     * @return true if the system supports Swift JIT compilation
     */
    static bool isAvailable();

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace SwiftJITREPL