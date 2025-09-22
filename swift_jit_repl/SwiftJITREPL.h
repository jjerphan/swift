#pragma once

#include <string>
#include <memory>
#include <vector>
#include <optional>
#include <functional>
#include <list>
#include <map>

// Visibility macro for runtime interface functions
#ifdef _WIN32
#ifdef SWIFT_JIT_REPL_EXPORTS
#define REPL_EXTERNAL_VISIBILITY __declspec(dllexport)
#else
#define REPL_EXTERNAL_VISIBILITY __declspec(dllimport)
#endif
#else
#define REPL_EXTERNAL_VISIBILITY __attribute__((visibility("default")))
#endif

// Forward declarations for runtime interface functions
extern "C" void REPL_EXTERNAL_VISIBILITY __swift_Interpreter_SetValueNoAlloc(
    void *This, void *OutVal, void *OpaqueType, ...);

extern "C" void REPL_EXTERNAL_VISIBILITY __swift_Interpreter_SetValueWithAlloc(
    void *This, void *OutVal, void *OpaqueType);

// SwiftValue class removed - focusing on basic execution without value capture

// Forward declarations for LLVM types
namespace llvm {
    template<typename T> class IntrusiveRefCntPtr;
}

// Forward declarations for Swift types
namespace swift {
    class CompilerInstance;
    class CompilerInvocation;
    class Module;
    class ModuleDecl;
    class SourceFile;
    class Decl;
    class Expr;
    class Stmt;
    class ASTContext;
    struct ImplicitImportInfo;
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
    std::string error_message;  // Error message if evaluation failed
    
    // Constructor for successful evaluation
    EvaluationResult() : success(true) {}
    
    // Constructor for failed evaluation
    explicit EvaluationResult(const std::string& error) 
        : success(false), error_message(error) {}
};


/**
 * Swift-specific partial translation unit (inspired by Clang's PartialTranslationUnit)
 * Represents a piece of Swift code that has been parsed and compiled incrementally
 * Each PTU owns its own ModuleDecl for true state reuse via module imports
 */
struct SwiftPartialTranslationUnit {
    swift::ModuleDecl* ModulePart = nullptr;  // Reference to ModuleDecl (owned by ASTContext)
    std::unique_ptr<llvm::Module> TheModule;
    std::string InputCode;
    swift::ASTContext* SharedASTContext = nullptr;  // Reference to shared context
    
    SwiftPartialTranslationUnit() = default;
    
    bool operator==(const SwiftPartialTranslationUnit &other) const {
        return other.ModulePart == ModulePart && other.TheModule == TheModule;
    }
};

/**
 * Swift incremental parser (inspired by Clang's IncrementalParser)
 * Handles incremental parsing of Swift code using multiple ModuleDecl instances
 * Each Parse() call creates a new ModuleDecl with imports to previous modules for state reuse
 */
class SwiftIncrementalParser {
private:
    swift::ASTContext* sharedASTContext;
    std::vector<swift::ModuleDecl*>* modules;  // Raw pointers (owned by ASTContext)
    llvm::orc::ThreadSafeContext* TSCtx;
    swift::CompilerInstance* sharedCompilerInstance;  // Reference to shared CompilerInstance
    swift::CompilerInvocation* compilerInvocation;  // Reference to shared CompilerInvocation
    std::list<SwiftPartialTranslationUnit> PTUs;
    unsigned InputCount = 0;
    
public:
    SwiftIncrementalParser(swift::ASTContext* sharedASTContext, 
                          std::vector<swift::ModuleDecl*>* modules,
                          llvm::orc::ThreadSafeContext* TSCtx,
                          swift::CompilerInstance* sharedCompilerInstance,
                          swift::CompilerInvocation* compilerInvocation);
    ~SwiftIncrementalParser();
    
    // Parse incremental Swift input and return a partial translation unit
    llvm::Expected<SwiftPartialTranslationUnit&> Parse(llvm::StringRef Input);
    
    // Get all parsed translation units
    std::list<SwiftPartialTranslationUnit>& getPTUs() { return PTUs; }
    
    // Clean up a specific PTU
    void CleanUpPTU(SwiftPartialTranslationUnit& PTU);
    
    // Get the shared AST context
    swift::ASTContext* getASTContext() const { return sharedASTContext; }
    
private:
    swift::ImplicitImportInfo createImplicitImports();
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
    bool Initialized = false;
    
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
    
    // Run global constructors (like Clang IncrementalExecutor)
    llvm::Error runCtors() const;
    
    // Get symbol address
    llvm::Expected<llvm::orc::ExecutorAddr> getSymbolAddress(llvm::StringRef Name) const;
    
    // Clean up the JIT instance
    llvm::Error cleanUp();
    
    // Get the execution engine
    llvm::orc::LLJIT& GetExecutionEngine() { return *Jit; }
};

/**
 * Swift Runtime Interface Builder for value capture
 * Similar to Clang's RuntimeInterfaceBuilder
 */
class SwiftRuntimeInterfaceBuilder {
public:
    virtual ~SwiftRuntimeInterfaceBuilder() = default;
    
    // Function type for transforming expressions to capture values
    using TransformExprFunction = std::function<std::string(const std::string&)>;
    
    // Get the print value transformer function
    virtual TransformExprFunction* getPrintValueTransformer() = 0;
};

/**
 * Main Swift interpreter class (inspired by Clang's Interpreter)
 * Provides the main interface for incremental Swift code execution using multiple modules
 */
class SwiftInterpreter {
private:
    std::unique_ptr<llvm::orc::ThreadSafeContext> TSCtx;
    std::unique_ptr<swift::ASTContext, std::function<void(swift::ASTContext*)>> sharedASTContext;
    std::vector<swift::ModuleDecl*> modules;  // Raw pointers (owned by ASTContext)
    std::unique_ptr<SwiftIncrementalParser> IncrParser;
    std::unique_ptr<SwiftIncrementalExecutor> IncrExecutor;
    std::unique_ptr<swift::CompilerInstance> compilerInstance;  // For proper SourceManager initialization
    swift::CompilerInvocation* compilerInvocation;  // Reference to shared compiler invocation
    
    // Runtime interface builder for value capture
    std::unique_ptr<SwiftRuntimeInterfaceBuilder> RuntimeIB;
    
    // Function pointer for adding print value calls (similar to Clang's RuntimeInterfaceBuilder)
    using TransformExprFunction = std::function<void(llvm::Module&, llvm::Function&)>;
    TransformExprFunction* AddPrintValueCall = nullptr;
    
    // Track the initial PTU size to separate runtime code from user code
    size_t InitPTUSize = 0;
    
public:
    // LastValue removed - focusing on basic execution without value capture
    SwiftInterpreter(swift::CompilerInvocation* invocation);
    ~SwiftInterpreter();
    
    // Mark the start of user code (separates runtime code from user code)
    void markUserCodeStart();
    
    // Get the effective PTU size (excluding runtime PTUs)
    size_t getEffectivePTUSize() const;
    
    // Undo the last N user PTUs (runtime PTUs are not affected)
    llvm::Error Undo(unsigned N);
    
    // Parse and execute Swift code
    llvm::Error ParseAndExecute(llvm::StringRef Code);
    
    // Execute a partial translation unit
    llvm::Error Execute(SwiftPartialTranslationUnit& PTU);
    
    // Get the shared AST context
    swift::ASTContext& getASTContext();
    
    // Get all modules
    const std::vector<swift::ModuleDecl*>& getModules() const { return modules; }
    
    // Get the execution engine
    llvm::Expected<llvm::orc::LLJIT&> getExecutionEngine();
    
    // Get the incremental parser
    SwiftIncrementalParser* getIncrementalParser();
    
    // Get the incremental executor
    SwiftIncrementalExecutor* getIncrementalExecutor();
    
    // Set up the print value call function (similar to Clang's RuntimeInterfaceBuilder)
    void setAddPrintValueCall(TransformExprFunction* func) { AddPrintValueCall = func; }
    
    // Get the last value
    // getLastValue removed - no value capture for now
    
    // Find and initialize the runtime interface builder
    std::unique_ptr<SwiftRuntimeInterfaceBuilder> findRuntimeInterface();
    
    // Transform Swift code to capture values (similar to Clang's SynthesizeExpr)
    std::string synthesizeExpr(const std::string& code);
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
     * Evaluate a Swift expression
     * @param expression The Swift expression to evaluate
     * @return Result of the evaluation
     */
    EvaluationResult evaluate(const std::string& expression);
    
    /**
     * Parse Swift code into a PartialTranslationUnit
     * @param code The Swift code to parse
     * @return Expected containing a reference to the parsed PTU or an error
     */
    llvm::Expected<SwiftPartialTranslationUnit&> Parse(const std::string& code);
    
    /**
     * Execute a PartialTranslationUnit
     * @param ptu The PTU to execute
     * @return Error if execution failed
     */
    llvm::Error Execute(SwiftPartialTranslationUnit& ptu);
    
    /**
     * Parse and execute Swift code
     * @param code The Swift code to parse and execute
     * @return Error if parsing or execution failed
     */
    llvm::Error ParseAndExecute(const std::string& code);
    
    /**
     * Undo the last N user expressions (runtime code is not affected)
     * @param N Number of expressions to undo
     * @return Error if undo failed
     */
    llvm::Error Undo(unsigned N);
    
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
