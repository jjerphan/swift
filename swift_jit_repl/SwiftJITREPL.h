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
#define REPL_EXTERNAL_VISIBILITY __declspec(dllexport)
#else
#define REPL_EXTERNAL_VISIBILITY __attribute__((visibility("default")))
#endif

// Forward declarations for runtime interface functions
extern "C" void REPL_EXTERNAL_VISIBILITY __swift_Interpreter_SetValueNoAlloc(
    void *This, void *OutVal, void *OpaqueType, ...);

extern "C" void REPL_EXTERNAL_VISIBILITY __swift_Interpreter_SetValueWithAlloc(
    void *This, void *OutVal, void *OpaqueType);

/**
 * Swift-specific value class (inspired by Clang's Value)
 * Represents the result of executing Swift code
 */
class SwiftValue {
private:
    std::string Value;
    std::string Type;
    bool Valid = false;
    
public:
    SwiftValue() = default;
    SwiftValue(const std::string& val, const std::string& type) 
        : Value(val), Type(type), Valid(true) {}
    
    bool isValid() const { return Valid; }
    void clear() { Value.clear(); Type.clear(); Valid = false; }
    
    const std::string& getValue() const { return Value; }
    const std::string& getType() const { return Type; }
    
    void setValue(const std::string& val, const std::string& type) {
        Value = val;
        Type = type;
        Valid = true;
    }
    
    void dump() const {
        if (Valid) {
            // Use printf to avoid iostream dependencies
            printf("Value: %s (Type: %s)\n", Value.c_str(), Type.c_str());
        }
    }
};

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
    swift::CompilerInstance* CI;
    llvm::orc::ThreadSafeContext* TSCtx;
    std::list<SwiftPartialTranslationUnit> PTUs;
    unsigned InputCount = 0;
    
public:
    SwiftIncrementalParser(swift::CompilerInstance* Instance, llvm::orc::ThreadSafeContext* TSCtx);
    ~SwiftIncrementalParser();
    
    // Parse incremental Swift input and return a partial translation unit
    llvm::Expected<SwiftPartialTranslationUnit&> Parse(llvm::StringRef Input);
    
    // Get all parsed translation units
    std::list<SwiftPartialTranslationUnit>& getPTUs() { return PTUs; }
    
    // Clean up a specific PTU
    void CleanUpPTU(SwiftPartialTranslationUnit& PTU);
    
    // Get the compiler instance
    swift::CompilerInstance* getCI() { return CI; }
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
 * Provides the main interface for incremental Swift code execution
 */
class SwiftInterpreter {
private:
    std::unique_ptr<llvm::orc::ThreadSafeContext> TSCtx;
    std::unique_ptr<SwiftIncrementalParser> IncrParser;
    std::unique_ptr<SwiftIncrementalExecutor> IncrExecutor;
    
    // Runtime interface builder for value capture
    std::unique_ptr<SwiftRuntimeInterfaceBuilder> RuntimeIB;
    
    // Function pointer for adding print value calls (similar to Clang's RuntimeInterfaceBuilder)
    using TransformExprFunction = std::function<void(llvm::Module&, llvm::Function&)>;
    TransformExprFunction* AddPrintValueCall = nullptr;
    
    // Track the initial PTU size to separate runtime code from user code
    size_t InitPTUSize = 0;
    
public:
    // Last captured value (similar to Clang's LastValue)
    SwiftValue LastValue;
    SwiftInterpreter(swift::CompilerInstance* CI);
    ~SwiftInterpreter();
    
    // Mark the start of user code (separates runtime code from user code)
    void markUserCodeStart();
    
    // Get the effective PTU size (excluding runtime PTUs)
    size_t getEffectivePTUSize() const;
    
    // Undo the last N user PTUs (runtime PTUs are not affected)
    llvm::Error Undo(unsigned N);
    
    // Parse and execute Swift code, returning the result value as SwiftValue
    llvm::Error ParseAndExecute(llvm::StringRef Code, SwiftValue* V);
    
    // Execute a partial translation unit
    llvm::Error Execute(SwiftPartialTranslationUnit& PTU);
    
    // Get the AST context
    swift::ASTContext& getASTContext();
    
    // Get the compiler instance
    swift::CompilerInstance* getCompilerInstance();
    
    // Get the execution engine
    llvm::Expected<llvm::orc::LLJIT&> getExecutionEngine();
    
    // Get the incremental parser
    SwiftIncrementalParser* getIncrementalParser();
    
    // Get the incremental executor
    SwiftIncrementalExecutor* getIncrementalExecutor();
    
    // Set up the print value call function (similar to Clang's RuntimeInterfaceBuilder)
    void setAddPrintValueCall(TransformExprFunction* func) { AddPrintValueCall = func; }
    
    // Get the last value
    const SwiftValue& getLastValue() const { return LastValue; }
    
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
     * Parse and execute Swift code, returning the result value as SwiftValue
     * @param code The Swift code to parse and execute
     * @param resultValue Output parameter to store the result value
     * @return Error if parsing or execution failed
     */
    llvm::Error ParseAndExecute(const std::string& code, SwiftValue* resultValue);
    
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
