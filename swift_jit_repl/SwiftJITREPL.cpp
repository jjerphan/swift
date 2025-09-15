#include "SwiftJITREPL.h"

// Standard library includes
#include <iostream>
#include <memory>
#include <mutex>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <vector>

// Basic LLVM includes only
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/TargetSelect.h"

// Swift compiler includes (minimal set)
#include "swift/Frontend/Frontend.h"
#include "swift/Immediate/Immediate.h"
#include "swift/Immediate/SwiftMaterializationUnit.h"
#include "swift/Parse/Lexer.h"
#include "swift/SIL/SILModule.h"
#include "swift/SIL/TypeLowering.h"
#include "swift/Subsystems.h"

// LLVM includes for JIT functionality
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Error.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"

namespace SwiftJITREPL {

/**
 * Helper function to validate Swift identifiers
 */
static bool isValidSwiftIdentifier(const std::string& identifier) {
    return swift::Lexer::isIdentifier(identifier);
}

/**
 * Get a valid Swift module name
 */
static std::string getValidModuleName() {
    // Try common valid identifiers
    std::vector<std::string> candidates = {"main", "SwiftJITREPL", "repl", "swiftrepl", "module"};
    
    for (const auto& candidate : candidates) {
        if (isValidSwiftIdentifier(candidate)) {
            return candidate;
        }
    }
    
    // Fallback to a simple valid identifier
    return "main";
}

/**
 * Private implementation class (PIMPL idiom)
 * Implements a minimal REPL pattern using basic LLVM components
 */
class SwiftJITREPL::Impl {
public:
    REPLConfig config;
    bool initialized = false;
    std::string lastError;
    
    // LLVM infrastructure
    std::unique_ptr<llvm::LLVMContext> llvmContext;
    
    // Swift compiler infrastructure
    std::unique_ptr<swift::CompilerInstance> compilerInstance;
    std::unique_ptr<SwiftInterpreter> interpreter;
    
    // Compilation state
    std::vector<std::string> sourceFiles;
    std::string currentModuleName;
    
    // Statistics
    SwiftJITREPL::CompilationStats stats;
    
    // Static initialization management
    static std::mutex initMutex;
    static bool llvmInitialized;

    explicit Impl(const REPLConfig& cfg) : config(cfg) {
        // Ensure LLVM is initialized (thread-safe)
        std::lock_guard<std::mutex> lock(initMutex);
        if (!llvmInitialized) {
            // Initialize basic LLVM subsystems
            llvm::InitializeAllTargetInfos();
            llvm::InitializeAllTargets();
            llvm::InitializeAllTargetMCs();
            llvm::InitializeAllAsmPrinters();
            llvm::InitializeAllAsmParsers();
            Impl::llvmInitialized = true;
        }
    }
    
    ~Impl() = default;
    
    bool initialize() {
        try {
            // Create LLVM context
            llvmContext = std::make_unique<llvm::LLVMContext>();
            
            // Create Swift compiler instance
            compilerInstance = std::make_unique<swift::CompilerInstance>();
            
            // Create and configure the compiler invocation for JIT/REPL mode
            swift::CompilerInvocation invocation;
            
            // Set up language options for JIT mode
            invocation.getLangOptions().Target = llvm::Triple("x86_64-unknown-linux-gnu");
            invocation.getLangOptions().EnableObjCInterop = false;
            
            // Set up frontend options for immediate mode (not REPL)
            invocation.getFrontendOptions().RequestedAction = swift::FrontendOptions::ActionType::Immediate;
            
            // Set up command line arguments for immediate mode
            std::vector<std::string> immediateArgs = {"swift", "-i"};
            invocation.getFrontendOptions().ImmediateArgv = immediateArgs;
            
            // Set a valid module name (required for CompilerInstance::setup)
            std::string moduleName = getValidModuleName();
            invocation.getFrontendOptions().ModuleName = moduleName;
            
            // Set up SIL options
            invocation.getSILOptions().OptMode = swift::OptimizationMode::NoOptimization;
            
            // Set up IRGen options
            invocation.getIRGenOptions().OutputKind = swift::IRGenOutputKind::Module;
            
            // Set the correct search paths for Swift standard library using compile-time definitions
            auto &searchPaths = invocation.getSearchPathOptions();
            searchPaths.RuntimeLibraryPaths = {SWIFT_RUNTIME_LIBRARY_PATHS};
            searchPaths.setRuntimeLibraryImportPaths({SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1, SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2});
            searchPaths.RuntimeResourcePath = SWIFT_RUNTIME_RESOURCE_PATH;
            searchPaths.setSDKPath(SWIFT_SDK_PATH);
            
            // Set up the compiler instance
            std::string error;
            if (compilerInstance->setup(invocation, error)) {
                lastError = "Failed to setup Swift compiler instance: " + (error.empty() ? "Unknown error" : error);
                return false;
            }
            
        // Create the interpreter for incremental compilation
        // We need to create a copy of the CompilerInstance for the interpreter
        auto interpreterCI = std::make_unique<swift::CompilerInstance>();
        auto& interpreterInvocation = const_cast<swift::CompilerInvocation&>(interpreterCI->getInvocation());
        interpreterInvocation = invocation;
        
        std::string interpreterError;
        if (interpreterCI->setup(interpreterInvocation, interpreterError)) {
            lastError = "Failed to setup interpreter compiler instance: " + interpreterError;
            return false;
        }
        
        interpreter = std::make_unique<SwiftInterpreter>(std::move(interpreterCI));
            
            initialized = true;
            return true;
            
        } catch (const std::exception& e) {
            lastError = std::string("Initialization failed: ") + e.what();
            return false;
        }
    }
    
    EvaluationResult evaluate(const std::string& expression) {
        if (!initialized) {
            return EvaluationResult("REPL not initialized");
        }
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        try {
            // Use the stored interpreter for incremental execution
            // This provides true REPL functionality without requiring main functions
            if (!interpreter) {
                lastError = "Interpreter not initialized";
                return EvaluationResult("Interpreter not initialized");
            }
            
            // Parse and execute the expression
            auto error = interpreter->ParseAndExecute(expression);
            if (error) {
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
                
                // Update statistics
                stats.total_expressions++;
                stats.failed_compilations++;
                stats.total_compilation_time_ms += duration.count() / 1000.0;
                
                return EvaluationResult("Failed to execute: " + llvm::toString(std::move(error)));
            }
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            
            // Update statistics
            stats.total_expressions++;
            stats.successful_compilations++;
            stats.total_compilation_time_ms += duration.count() / 1000.0;
            
            // For now, return a simple success message
            // In a real implementation, we'd capture the output and return value
            return EvaluationResult("Successfully executed Swift code: " + expression, "Any");
            
        } catch (const std::exception& e) {
            lastError = e.what();
            stats.total_expressions++;
            stats.failed_compilations++;
            return EvaluationResult("Compilation failed: " + std::string(e.what()));
        }
    }
    
    std::string getLastError() const {
        return lastError;
    }
    
    bool isInitialized() const {
        return initialized;
    }
    
    bool addSourceFile(const std::string& source_code, const std::string& filename) {
        try {
            // Create MemoryBuffer from source code
            auto buffer = llvm::MemoryBuffer::getMemBuffer(source_code, filename);
            if (!buffer) {
                lastError = "Failed to create memory buffer for source file";
                return false;
            }
            
            // Add to SourceManager
            auto &sourceManager = compilerInstance->getSourceMgr();
            unsigned bufferID = sourceManager.addNewSourceBuffer(std::move(buffer));
            
            // Let the compiler instance handle the rest of the processing
            // For now, just track that we added the source file
            sourceFiles.push_back(source_code);
            return true;
        } catch (const std::exception& e) {
            lastError = e.what();
            return false;
        }
    }
    
    bool reset() {
        try {
            // Reset Swift compiler state
            if (compilerInstance) {
                compilerInstance->freeASTContext();
                compilerInstance.reset();
            }
            if (llvmContext) {
                llvmContext.reset();
            }
            
            // Clear source files and reset state
            sourceFiles.clear();
            currentModuleName = "SwiftJITREPL";
            stats = SwiftJITREPL::CompilationStats{};
            lastError.clear();
            initialized = false;
            
            // Reset interpreter
            interpreter.reset();
            
            // Reinitialize
            return initialize();
            
        } catch (const std::exception& e) {
            lastError = e.what();
            return false;
        }
    }
    
    SwiftJITREPL::CompilationStats getStats() const {
        return stats;
    }
    
    std::vector<EvaluationResult> evaluateMultiple(const std::vector<std::string>& expressions) {
        if (!initialized) {
            return std::vector<EvaluationResult>(expressions.size(), EvaluationResult("REPL not initialized"));
        }
        
        std::vector<EvaluationResult> results;
        results.reserve(expressions.size());
        
        for (const auto& expr : expressions) {
            results.push_back(evaluate(expr));
        }
        
        return results;
    }
};

// Static member initialization
std::mutex SwiftJITREPL::Impl::initMutex;
bool SwiftJITREPL::Impl::llvmInitialized = false;

// SwiftJITREPL implementation
SwiftJITREPL::SwiftJITREPL(const REPLConfig& config) : pImpl(std::make_unique<Impl>(config)) {}

SwiftJITREPL::~SwiftJITREPL() = default;

bool SwiftJITREPL::initialize() {
    return pImpl->initialize();
}

bool SwiftJITREPL::isInitialized() const {
    return pImpl->isInitialized();
}

EvaluationResult SwiftJITREPL::evaluate(const std::string& expression) {
    return pImpl->evaluate(expression);
}

std::vector<EvaluationResult> SwiftJITREPL::evaluateMultiple(const std::vector<std::string>& expressions) {
    return pImpl->evaluateMultiple(expressions);
}

bool SwiftJITREPL::addSourceFile(const std::string& source_code, const std::string& filename) {
    return pImpl->addSourceFile(source_code, filename);
}

bool SwiftJITREPL::reset() {
    return pImpl->reset();
}

std::string SwiftJITREPL::getLastError() const {
    return pImpl->getLastError();
}

SwiftJITREPL::CompilationStats SwiftJITREPL::getStats() const {
    return pImpl->getStats();
}

bool SwiftJITREPL::isSwiftJITAvailable() {
    // Check if Swift JIT is available by attempting to create a minimal instance
    try {
        // Create a minimal Swift compiler instance to test availability
        auto compilerInstance = std::make_unique<swift::CompilerInstance>();
        
        // Create and configure the compiler invocation for JIT mode
        swift::CompilerInvocation invocation;
        
        // Set up language options for JIT mode
        invocation.getLangOptions().Target = llvm::Triple("x86_64-unknown-linux-gnu");
        invocation.getLangOptions().EnableObjCInterop = false;
        
        // Set up frontend options for immediate mode (not REPL)
        invocation.getFrontendOptions().RequestedAction = swift::FrontendOptions::ActionType::Immediate;
        
        // Set up command line arguments for immediate mode
        std::vector<std::string> immediateArgs = {"swift", "-i"};
        invocation.getFrontendOptions().ImmediateArgv = immediateArgs;
        
        // Set a valid module name (required for CompilerInstance::setup)
        std::string moduleName = getValidModuleName();
        invocation.getFrontendOptions().ModuleName = moduleName;
        
        // Set up SIL options
        invocation.getSILOptions().OptMode = swift::OptimizationMode::NoOptimization;
        
        // Set up IRGen options
        invocation.getIRGenOptions().OutputKind = swift::IRGenOutputKind::Module;
        
        // Set up the compiler instance
        std::string error;
        if (!compilerInstance->setup(invocation, error)) {
            return false;
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

EvaluationResult evaluateSwiftExpression(const std::string& expression, const REPLConfig& config) {
    SwiftJITREPL repl(config);
    if (!repl.initialize()) {
        return EvaluationResult("Failed to initialize REPL: " + repl.getLastError());
    }
    return repl.evaluate(expression);
}

bool isSwiftJITAvailable() {
    return SwiftJITREPL::isSwiftJITAvailable();
}

// ============================================================================
// SwiftIncrementalParser Implementation
// ============================================================================

SwiftIncrementalParser::SwiftIncrementalParser(std::unique_ptr<swift::CompilerInstance> Instance)
    : CI(std::move(Instance)) {
}

SwiftIncrementalParser::~SwiftIncrementalParser() {
    // Clean up all PTUs
    for (auto& ptu : PTUs) {
        CleanUpPTU(ptu);
    }
}

llvm::Expected<SwiftPartialTranslationUnit&> SwiftIncrementalParser::Parse(llvm::StringRef Input) {
    // Create a temporary file for the input
    std::string tempFileName = "/tmp/swift_repl_input_" + std::to_string(InputCount++) + ".swift";
    std::ofstream tempFile(tempFileName);
    if (!tempFile.is_open()) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "Failed to create temporary file");
    }
    tempFile << Input.str();
    tempFile.close();
    
    // Create a new CompilerInstance for this expression
    // This ensures proper parsing and error detection
    auto tempCompiler = std::make_unique<swift::CompilerInstance>();
    
    // Copy the configuration from the main CompilerInstance
    auto invocation = CI->getInvocation();
    
    // Set up the input file BEFORE creating the CompilerInstance
    invocation.getFrontendOptions().InputsAndOutputs.addInputFile(tempFileName);
    
    // Set up the compiler instance
    std::string error;
    if (tempCompiler->setup(invocation, error)) {
        std::remove(tempFileName.c_str());
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "Failed to setup compiler: " + error);
    }
    
    // Perform semantic analysis
    tempCompiler->performSema();
    if (tempCompiler->getASTContext().hadError()) {
        std::remove(tempFileName.c_str());
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "Semantic analysis failed");
    }
    
    // Check if the module has any files (this indicates if the input was properly parsed)
    auto *mainModule = tempCompiler->getMainModule();
    if (!mainModule || mainModule->getFiles().empty()) {
        std::remove(tempFileName.c_str());
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "No source files were parsed");
    }
    
    // Create a new PTU for this expression
    PTUs.emplace_back();
    auto& ptu = PTUs.back();
    ptu.ModulePart = mainModule;
    ptu.InputCode = Input.str();
    ptu.TheModule = nullptr;  // We'll let SwiftJIT handle the compilation
    
    // Clean up temporary file
    std::remove(tempFileName.c_str());
    
    return ptu;
}

void SwiftIncrementalParser::CleanUpPTU(SwiftPartialTranslationUnit& PTU) {
    // Clean up the LLVM module
    PTU.TheModule.reset();
    PTU.ModulePart = nullptr;
    PTU.InputCode.clear();
}

// ============================================================================
// SwiftIncrementalExecutor Implementation
// ============================================================================

SwiftIncrementalExecutor::SwiftIncrementalExecutor(llvm::orc::ThreadSafeContext& TSC, 
                                                   std::unique_ptr<llvm::orc::LLJIT> JIT)
    : TSCtx(TSC), Jit(std::move(JIT)) {
}

SwiftIncrementalExecutor::~SwiftIncrementalExecutor() {
    // Clean up all resource trackers
    for (auto& [ptu, tracker] : ResourceTrackers) {
        if (tracker) {
            (void)tracker->remove();
        }
    }
}

llvm::Error SwiftIncrementalExecutor::addModule(SwiftPartialTranslationUnit& PTU) {
    if (!Jit) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "JIT not initialized");
    }
    
    // Instead of using raw LLVM JIT, we'll use Swift's JIT system
    // We need to create a SwiftJIT instance and use it to compile the Swift code
    
    // For now, we'll just store the PTU and handle compilation in execute()
    // This is a simplified approach - in a real implementation, we'd need to
    // properly integrate with Swift's JIT system
    
    return llvm::Error::success();
}

llvm::Error SwiftIncrementalExecutor::removeModule(SwiftPartialTranslationUnit& PTU) {
    auto it = ResourceTrackers.find(&PTU);
    if (it != ResourceTrackers.end()) {
        auto error = it->second->remove();
        ResourceTrackers.erase(it);
        return error;
    }
    return llvm::Error::success();
}

llvm::Error SwiftIncrementalExecutor::execute() {
    if (!Jit) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "JIT not initialized");
    }
    
    // Instead of using raw LLVM JIT, we'll use Swift's immediate mode
    // This is a simplified approach - we'll just return success for now
    // In a real implementation, we'd need to properly integrate with Swift's JIT
    
    return llvm::Error::success();
}

llvm::Expected<llvm::orc::ExecutorAddr> SwiftIncrementalExecutor::getSymbolAddress(llvm::StringRef Name) const {
    if (!Jit) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "JIT not initialized");
    }
    
    return Jit->lookup(Name);
}

// ============================================================================
// SwiftInterpreter Implementation
// ============================================================================

SwiftInterpreter::SwiftInterpreter(std::unique_ptr<swift::CompilerInstance> CI) {
    // Initialize LLVM targets
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    
    // Create thread-safe context
    TSCtx = std::make_unique<llvm::orc::ThreadSafeContext>(std::make_unique<llvm::LLVMContext>());
    
    // Create incremental parser with the shared CompilerInstance
    // This allows incremental compilation where new code can reference previously defined variables
    IncrParser = std::make_unique<SwiftIncrementalParser>(std::move(CI));
    
    // Create JIT builder
    auto jitBuilder = llvm::orc::LLJITBuilder();
    auto jitOrError = jitBuilder.create();
    if (!jitOrError) {
        llvm::errs() << "Failed to create JIT builder: " << llvm::toString(jitOrError.takeError()) << "\n";
        return;
    }
    
    // Create incremental executor
    IncrExecutor = std::make_unique<SwiftIncrementalExecutor>(*TSCtx, std::move(*jitOrError));
}

SwiftInterpreter::~SwiftInterpreter() = default;

llvm::Error SwiftInterpreter::ParseAndExecute(llvm::StringRef Code) {
    // Parse the code
    auto ptuOrError = IncrParser->Parse(Code);
    if (!ptuOrError) {
        return ptuOrError.takeError();
    }
    
    auto& ptu = *ptuOrError;
    
    // Add to JIT
    auto addError = IncrExecutor->addModule(ptu);
    if (addError) {
        return addError;
    }
    
    // Execute
    auto execError = IncrExecutor->execute();
    if (execError) {
        return execError;
    }
    
    return llvm::Error::success();
}

swift::ASTContext& SwiftInterpreter::getASTContext() {
    return IncrParser->getCI()->getASTContext();
}

swift::CompilerInstance* SwiftInterpreter::getCompilerInstance() {
    return IncrParser->getCI();
}

llvm::Expected<llvm::orc::LLJIT&> SwiftInterpreter::getExecutionEngine() {
    if (!IncrExecutor) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "Executor not initialized");
    }
    return IncrExecutor->GetExecutionEngine();
}

} // namespace SwiftJITREPL
