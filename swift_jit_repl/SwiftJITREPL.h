#pragma once

#include <string>
#include <memory>
#include <vector>
#include <optional>
#include <functional>
#include <list>
#include <map>

// Forward declarations for LLVM types
namespace llvm {
    template<typename T> class IntrusiveRefCntPtr;
}

// Forward declarations for Swift types
namespace swift {
    class CompilerInstance;
    class Module;
    class ModuleDecl;
    class SourceFile;
    class Decl;
    class Expr;
    class Stmt;
    class ASTContext;
}

// Forward declarations for LLVM types
namespace llvm {
    class Module;
    class Function;
    class Value;
    class LLVMContext;
    class StringRef;
    template<typename T> class Expected;
    class Error;
    namespace orc {
        class LLJIT;
        class LLJITBuilder;
        class ThreadSafeContext;
        class ExecutorAddr;
        class ResourceTracker;
        using ResourceTrackerSP = IntrusiveRefCntPtr<ResourceTracker>;
    }
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
 * Swift-specific partial translation unit (inspired by Clang's PartialTranslationUnit)
 * Represents a piece of Swift code that has been parsed and compiled incrementally
 */
struct SwiftPartialTranslationUnit {
    swift::ModuleDecl *ModulePart = nullptr;
    std::unique_ptr<llvm::Module> TheModule;
    std::string InputCode;
    
    bool operator==(const SwiftPartialTranslationUnit &other) const {
        return other.ModulePart == ModulePart && other.TheModule == TheModule;
    }
};

/**
 * Swift incremental parser (inspired by Clang's IncrementalParser)
 * Handles incremental parsing of Swift code without requiring a main function
 */
class SwiftIncrementalParser {
private:
    std::unique_ptr<swift::CompilerInstance> CI;
    std::list<SwiftPartialTranslationUnit> PTUs;
    unsigned InputCount = 0;
    
public:
    SwiftIncrementalParser(std::unique_ptr<swift::CompilerInstance> Instance);
    ~SwiftIncrementalParser();
    
    // Parse incremental Swift input and return a partial translation unit
    llvm::Expected<SwiftPartialTranslationUnit&> Parse(llvm::StringRef Input);
    
    // Get all parsed translation units
    std::list<SwiftPartialTranslationUnit>& getPTUs() { return PTUs; }
    
    // Clean up a specific PTU
    void CleanUpPTU(SwiftPartialTranslationUnit& PTU);
    
    // Get the compiler instance
    swift::CompilerInstance* getCI() { return CI.get(); }
};

/**
 * Swift incremental executor (inspired by Clang's IncrementalExecutor)
 * Manages JIT execution of partial translation units
 */
class SwiftIncrementalExecutor {
private:
    std::unique_ptr<llvm::orc::LLJIT> Jit;
    llvm::orc::ThreadSafeContext& TSCtx;
    std::map<const SwiftPartialTranslationUnit*, llvm::orc::ResourceTrackerSP> ResourceTrackers;
    
public:
    SwiftIncrementalExecutor(llvm::orc::ThreadSafeContext& TSC, 
                            std::unique_ptr<llvm::orc::LLJIT> JIT);
    ~SwiftIncrementalExecutor();
    
    // Add a partial translation unit to the JIT
    llvm::Error addModule(SwiftPartialTranslationUnit& PTU);
    
    // Remove a partial translation unit from the JIT
    llvm::Error removeModule(SwiftPartialTranslationUnit& PTU);
    
    // Execute the JIT'd code
    llvm::Error execute();
    
    // Get symbol address
    llvm::Expected<llvm::orc::ExecutorAddr> getSymbolAddress(llvm::StringRef Name) const;
    
    // Get the execution engine
    llvm::orc::LLJIT& GetExecutionEngine() { return *Jit; }
};

/**
 * Main Swift interpreter class (inspired by Clang's Interpreter)
 * Provides the main interface for incremental Swift code execution
 */
class SwiftInterpreter {
private:
    std::unique_ptr<llvm::orc::ThreadSafeContext> TSCtx;
    std::unique_ptr<SwiftIncrementalParser> IncrParser;
    std::unique_ptr<SwiftIncrementalExecutor> IncrExecutor;
    
public:
    SwiftInterpreter(std::unique_ptr<swift::CompilerInstance> CI);
    ~SwiftInterpreter();
    
    // Parse and execute Swift code incrementally
    llvm::Error ParseAndExecute(llvm::StringRef Code);
    
    // Get the AST context
    swift::ASTContext& getASTContext();
    
    // Get the compiler instance
    swift::CompilerInstance* getCompilerInstance();
    
    // Get the execution engine
    llvm::Expected<llvm::orc::LLJIT&> getExecutionEngine();
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
