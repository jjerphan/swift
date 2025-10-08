#pragma once

#include "Common.h"
#include "SwiftPartialTranslationUnit.h"
#include "SwiftIncrementalParser.h"
#include "SwiftIncrementalExecutor.h"

namespace SwiftJITREPL {

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
    llvm::Error undo(unsigned N);
    
    // Parse and execute Swift code
    llvm::Error parseAndExecute(llvm::StringRef Code);
    
    // Execute a partial translation unit
    llvm::Error execute(SwiftPartialTranslationUnit& PTU);
    
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

} // namespace SwiftJITREPL
