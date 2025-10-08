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

// Forward declarations for LLVM types
namespace llvm {
    template<typename T> class IntrusiveRefCntPtr;
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

} // namespace SwiftJITREPL
