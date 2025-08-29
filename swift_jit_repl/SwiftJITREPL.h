#pragma once

#include <string>
#include <memory>
#include <vector>
#include <optional>
#include <functional>

// Forward declarations for Swift types
namespace swift {
    class CompilerInstance;
    class Module;
    class SourceFile;
    class Decl;
    class Expr;
    class Stmt;
}

// Forward declarations for LLVM types
namespace llvm {
    class Module;
    class Function;
    class Value;
}

namespace SwiftJITREPL {

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
 * Configuration options for the JIT REPL
 */
struct REPLConfig {
    bool enable_optimizations = true;
    bool generate_debug_info = false;
    bool lazy_compilation = true;
    int timeout_ms = 5000; // 5 seconds default timeout
    std::string stdlib_path = ""; // Path to Swift standard library
    std::vector<std::string> module_search_paths;
    std::vector<std::string> framework_search_paths;
};

/**
 * Swift JIT-based REPL implementation
 * 
 * This class provides a clean API to evaluate Swift expressions
 * using the SwiftJIT infrastructure without LLDB debugging abstractions.
 * It compiles Swift code directly to machine code and executes it.
 */
class SwiftJITREPL {
public:
    /**
     * Constructor
     * @param config Configuration options for the REPL
     */
    explicit SwiftJITREPL(const REPLConfig& config = REPLConfig{});
    
    /**
     * Destructor - ensures proper cleanup
     */
    ~SwiftJITREPL();
    
    // Disable copy constructor and assignment
    SwiftJITREPL(const SwiftJITREPL&) = delete;
    SwiftJITREPL& operator=(const SwiftJITREPL&) = delete;
    
    // Enable move constructor and assignment
    SwiftJITREPL(SwiftJITREPL&& other) noexcept;
    SwiftJITREPL& operator=(SwiftJITREPL&& other) noexcept;
    
    /**
     * Initialize the JIT REPL
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
     * Add a Swift source file to the compilation context
     * @param source_code Swift source code
     * @param filename Optional filename for error reporting
     * @return true if addition was successful
     */
    bool addSourceFile(const std::string& source_code, const std::string& filename = "input.swift");
    
    /**
     * Reset the REPL context (clears all compiled code and state)
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
    static bool isSwiftJITAvailable();
    
    /**
     * Get compilation statistics
     */
    struct CompilationStats {
        size_t total_expressions = 0;
        size_t successful_compilations = 0;
        size_t failed_compilations = 0;
        double total_compilation_time_ms = 0.0;
        double total_execution_time_ms = 0.0;
    };
    
    CompilationStats getStats() const;

private:
    class Impl; // PIMPL idiom for hiding implementation details
    std::unique_ptr<Impl> pImpl;
};

/**
 * Convenience function to evaluate a single Swift expression
 * Creates a temporary JIT REPL instance for one-off evaluations
 * @param expression The Swift expression to evaluate
 * @param config Optional configuration
 * @return Result of the evaluation
 */
EvaluationResult evaluateSwiftExpression(const std::string& expression, 
                                       const REPLConfig& config = REPLConfig{});

/**
 * Convenience function to check if Swift JIT functionality is available
 * @return true if the system supports Swift JIT compilation
 */
bool isSwiftJITAvailable();

} // namespace SwiftJITREPL
