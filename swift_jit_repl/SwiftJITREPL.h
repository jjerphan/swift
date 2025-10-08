#pragma once

#include "Common.h"
#include "SwiftPartialTranslationUnit.h"
#include "SwiftInterpreter.h"

namespace SwiftJITREPL {

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
     * Evaluate a Swift expression
     * @param expression The Swift expression to evaluate
     * @return Result of the evaluation
     */
    EvaluationResult evaluate(const std::string& expression);

    /**
     * Undo the last N user expressions (runtime code is not affected)
     * @param N Number of expressions to undo
     * @return Error if undo failed
     */
    llvm::Error undo(unsigned N);
    
    /**
     * Evaluate multiple Swift expressions in sequence
     * @param expressions Vector of expressions to evaluate
     * @return Vector of results corresponding to each expression
     */
    std::vector<EvaluationResult> evaluateMultiple(const std::vector<std::string>& expressions);

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
     * Get the interpreter instance
     * @return Pointer to the SwiftInterpreter instance
     */
    SwiftInterpreter* getInterpreter();
    
    /**
     * Check if Swift JIT support is available
     * @return true if the system supports Swift JIT compilation
     */
    static bool isAvailable();
    
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
    /**
     * Parse Swift code into a PartialTranslationUnit
     * @param code The Swift code to parse
     * @return Expected containing a reference to the parsed PTU or an error
     */
    llvm::Expected<SwiftPartialTranslationUnit&> parse(const std::string& code);
    
    /**
     * Execute a PartialTranslationUnit
     * @param ptu The PTU to execute
     * @return Error if execution failed
     */
    llvm::Error execute(SwiftPartialTranslationUnit& ptu);
    
    /**
     * Parse and execute Swift code
     * @param code The Swift code to parse and execute
     * @return Error if parsing or execution failed
     */
    llvm::Error parseAndExecute(const std::string& code);
    
    class Impl; // PIMPL idiom for hiding implementation details
    std::unique_ptr<Impl> pImpl;
};

} // namespace SwiftJITREPL