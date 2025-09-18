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
#include <cstdarg>

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
#include "llvm/ADT/ArrayRef.h"

// Swift compiler includes (minimal set)
#include "swift/Frontend/Frontend.h"
#include "swift/Immediate/Immediate.h"
#include "swift/Immediate/SwiftMaterializationUnit.h"
#include "swift/Parse/Lexer.h"
#include "swift/SIL/SILModule.h"
#include "swift/SIL/TypeLowering.h"
#include "swift/AST/SILGenRequests.h"
#include "swift/AST/IRGenRequests.h"
#include "swift/Subsystems.h"

// LLVM IR parsing utilities (to reparse IR into our shared LLVMContext)
#include "llvm/AsmParser/Parser.h"
#include "llvm/Support/SourceMgr.h"

// Swift runtime includes for proper value capture
#include "swift/Runtime/Reflection.h"
#include "swift/ABI/Metadata.h"
#include "swift/ABI/ValueWitnessTable.h"
#include "swift/Demangling/Demangle.h"

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
 * Lower Swift code to LLVM IR using Swift's built-in utilities
 * This uses the same pipeline as Swift's immediate mode
 */
static std::unique_ptr<llvm::Module> lowerSwiftCodeToLLVMModule(swift::CompilerInstance* CI, 
                                                                swift::ModuleDecl* module,
                                                                llvm::LLVMContext* llvmCtx) {
    if (!CI || !module) {
        llvm::errs() << "[lowerSwiftCodeToLLVMModule] ERROR: Invalid parameters - CI: " << (CI ? "valid" : "null") 
                     << ", module: " << (module ? "valid" : "null") << "\n";
        return nullptr;
    }

    // Use Swift's IRGen utility to generate LLVM IR from the module,
    // then reparse the IR into our shared LLVMContext.
    auto &invocation = CI->getInvocation();
    const auto &IRGenOpts = invocation.getIRGenOptions();
    const auto &TBDOpts = invocation.getTBDGenOptions();
    const auto PSPs = CI->getPrimarySpecificPathsForAtMostOnePrimary();

    // Try to generate SIL first, then use SIL-based IR generation (like SwiftMaterializationUnit.cpp)
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] About to generate SIL module\n";
    
    // Generate SIL module using the same approach as SwiftMaterializationUnit
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Creating TypeConverter for module: " << module->getName().str() << "\n";
    auto typeConverter = std::make_unique<swift::Lowering::TypeConverter>(*module);
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] TypeConverter created, calling performASTLowering...\n";
    auto silModule = swift::performASTLowering(module, *typeConverter, CI->getSILOptions());
    if (!silModule) {
        llvm::errs() << "[lowerSwiftCodeToLLVMModule] ERROR: Failed to generate SIL module\n";
        return nullptr;
    }
    
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] SIL module generated successfully\n";
    
    // Dump SIL module to see what was generated
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Dumping SIL module contents:\n";
    silModule->dump();
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] End of SIL module dump\n";
    
    // Check if SIL module has any functions
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] SIL module function count: " << silModule->getFunctionList().size() << "\n";
    for (auto &func : silModule->getFunctionList()) {
        llvm::errs() << "[lowerSwiftCodeToLLVMModule] SIL function: " << func.getName().str() << "\n";
    }
    
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] About to call performIRGeneration with SIL module\n";
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Module name: " << module->getName().str() << "\n";
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] PSPs output filename: " << PSPs.OutputFilename << "\n";
    
    swift::GeneratedModule GenModule = swift::performIRGeneration(
        module, IRGenOpts, TBDOpts, std::move(silModule),
        module->getName().str(), PSPs,
        /*parallelOutputFilenames*/ llvm::ArrayRef<std::string>(),
        /*outModuleHash*/ nullptr);
    
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] performIRGeneration completed\n";

    auto *Produced = GenModule.getModule();
    if (!Produced) {
        llvm::errs() << "[lowerSwiftCodeToLLVMModule] ERROR: performIRGeneration produced no module\n";
        return nullptr;
    }
    
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Generated module successfully, module name: " << Produced->getName().str() << "\n";
    
    // Check what functions were generated in the LLVM module
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] LLVM module function count: " << Produced->getFunctionList().size() << "\n";
    for (auto &func : Produced->getFunctionList()) {
        llvm::errs() << "[lowerSwiftCodeToLLVMModule] LLVM function: " << func.getName().str() << "\n";
    }
    
    // Check global variables
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] LLVM module global variable count: " << Produced->global_size() << "\n";
    for (auto &global : Produced->globals()) {
        llvm::errs() << "[lowerSwiftCodeToLLVMModule] LLVM global: " << global.getName().str() << "\n";
    }

    // Serialize to IR text
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Serializing LLVM module to IR text...\n";
    std::string irText;
    {
        llvm::raw_string_ostream os(irText);
        Produced->print(os, nullptr);
    }
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] IR text serialized, size: " << irText.size() << " bytes\n";

    // Parse back into our shared context
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Parsing IR text back into shared LLVM context...\n";
    llvm::SMDiagnostic diag;
    std::unique_ptr<llvm::Module> Parsed = llvm::parseAssemblyString(irText, diag, *llvmCtx);
    if (!Parsed) {
        llvm::errs() << "[lowerSwiftCodeToLLVMModule] ERROR parsing IR: " << diag.getMessage() << "\n";
        llvm::errs() << "[lowerSwiftCodeToLLVMModule] IR text that failed to parse:\n" << irText << "\n";
        return nullptr;
    }
    
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Successfully parsed IR into shared context\n";
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Parsed module name: " << Parsed->getName().str() << "\n";
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Parsed module data layout: " << Parsed->getDataLayoutStr() << "\n";

    return Parsed;
}

// Swift Runtime Interface Functions for Value Capture
// These functions are called during execution to capture actual values

// Simple runtime interface - no value capture for now
const char *const SwiftRuntimes = R"(
    // Empty runtime interface for now
)";

// Runtime interface functions removed - focusing on basic execution first

/**
 * In-process Swift Runtime Interface Builder
 * Similar to Clang's InProcessRuntimeInterfaceBuilder
 */
class InProcessSwiftRuntimeInterfaceBuilder : public SwiftRuntimeInterfaceBuilder {
    SwiftInterpreter &Interp;
    SwiftRuntimeInterfaceBuilder::TransformExprFunction transformer;
    
public:
    InProcessSwiftRuntimeInterfaceBuilder(SwiftInterpreter &Interp) : Interp(Interp) {
        transformer = [this](const std::string& code) -> std::string {
            return transformForValuePrinting(code);
        };
    }
    
    SwiftRuntimeInterfaceBuilder::TransformExprFunction* getPrintValueTransformer() override {
        return &transformer;
    }
    
private:
    std::string transformForValuePrinting(const std::string& code) {
        llvm::errs() << "[transformForValuePrinting] Input code: '" << code << "'\n";
        
        // Try different approaches to see which one generates SIL functions
        // Approach 1: Top-level code (Swift should auto-wrap in main)
        std::string result = code + "\n";
        
        llvm::errs() << "[transformForValuePrinting] Generated Swift code (top-level):\n" << result << "\n";
        return result;
    }
};

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
        
    // Swift compiler infrastructure
    std::unique_ptr<swift::CompilerInstance> compilerInstance;
    std::unique_ptr<SwiftInterpreter> interpreter;
    
    // Compilation state
    std::vector<std::string> sourceFiles;
    std::string currentModuleName;
    
    // Generate a unique module name to avoid any reuse after reset
    static std::string generateUniqueModuleName() {
        using namespace std::chrono;
        auto now = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
        return std::string("SwiftJITREPL_") + std::to_string(now);
    }
    unsigned inputCount = 0;
    bool postResetPending = false;
    
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
            
            // Create Swift compiler instance
            compilerInstance = std::make_unique<swift::CompilerInstance>();
            
            // Create and configure the compiler invocation for JIT/REPL mode
            swift::CompilerInvocation invocation;
            
            // Set up language options for JIT mode
            invocation.getLangOptions().Target = llvm::Triple("x86_64-unknown-linux-gnu");
            invocation.getLangOptions().EnableObjCInterop = false;
            
            // Set up frontend options for SIL generation (to generate SIL functions)
            invocation.getFrontendOptions().RequestedAction = swift::FrontendOptions::ActionType::EmitSILGen;
            
            // Set up command line arguments for immediate mode
            std::vector<std::string> immediateArgs = {"swift", "-i"};
            invocation.getFrontendOptions().ImmediateArgv = immediateArgs;
            
            // Set a valid and preferably unique module name
            std::string moduleName = currentModuleName.empty() ? generateUniqueModuleName() : currentModuleName;
            invocation.getFrontendOptions().ModuleName = moduleName;
            
            // Set up SIL options based on configuration
            invocation.getSILOptions().OptMode = config.enable_optimizations ? 
                swift::OptimizationMode::ForSpeed : swift::OptimizationMode::NoOptimization;
            
            // Set additional SIL options for immediate mode
            invocation.getSILOptions().EnableSILOpaqueValues = true;
            
            // Set up IRGen options
            invocation.getIRGenOptions().OutputKind = swift::IRGenOutputKind::Module;
            
            // Set debug info generation based on configuration
            if (config.generate_debug_info) {
                invocation.getIRGenOptions().DebugInfoFormat = swift::IRGenDebugInfoFormat::DWARF;
            }
            
            // Set the correct search paths for Swift standard library using compile time values
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
            // Record the actual module name used
            currentModuleName = moduleName;
            
            // Create the interpreter for incremental compilation
            // Use the same CompilerInstance for consistency
            interpreter = std::make_unique<SwiftInterpreter>(compilerInstance.get());
            
            initialized = true;
            return true;
            
        } catch (const std::exception& e) {
            lastError = std::string("Initialization failed: ") + e.what();
            return false;
        }
    }
    
    EvaluationResult evaluate(const std::string& expression) {
        llvm::errs() << "[SwiftJITREPL::evaluate] Starting evaluation of: " << expression << "\n";
        
        if (!initialized) {
            llvm::errs() << "[SwiftJITREPL::evaluate] ERROR: REPL not initialized\n";
            return EvaluationResult("REPL not initialized");
        }
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        try {
            // Use the interpreter to parse and execute the code
            if (!interpreter) {
                llvm::errs() << "[SwiftJITREPL::evaluate] ERROR: Interpreter not initialized\n";
                lastError = "Interpreter not initialized";
                return EvaluationResult("Interpreter not initialized");
            }
            
            llvm::errs() << "[SwiftJITREPL::evaluate] About to call ParseAndExecute...\n";
            // Use ParseAndExecute to handle the compilation and execution
            auto error = interpreter->ParseAndExecute(expression);
            llvm::errs() << "[SwiftJITREPL::evaluate] ParseAndExecute completed\n";
            if (error) {
                lastError = "Failed to execute: " + llvm::toString(std::move(error));
                stats.total_expressions++;
                stats.failed_compilations++;
                return EvaluationResult("Failed to execute: " + llvm::toString(std::move(error)));
            }
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            
            // Update statistics
            stats.total_expressions++;
            stats.successful_compilations++;
            stats.total_compilation_time_ms += duration.count() / 1000.0;
            
            // Return success (no value capture for now)
            return EvaluationResult();
            
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
            
            // Clear source files and reset state
            sourceFiles.clear();
            // Force a new unique module name on next initialize
            currentModuleName.clear();
            inputCount = 0;  // Reset input counter
            postResetPending = true; // enforce stricter first eval
            stats = SwiftJITREPL::CompilationStats{};
            lastError.clear();
            initialized = false;
            
            // Reset interpreter
            interpreter.reset();
            
            // Reinitialize with a fresh CompilerInstance
            // This will create a completely new CompilerInstance with no input files
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
        
        // For multiple expressions, we'll evaluate each one individually
        // Stop on first failure to avoid cascading errors
        for (const auto& expr : expressions) {
            llvm::errs() << "[SwiftJITREPL] Evaluating expression: " << expr << "\n";
            auto result = evaluate(expr);
            results.push_back(result);
            
            if (!result.success) {
                llvm::errs() << "[SwiftJITREPL] Stopping evaluation due to failure: " << result.error_message << "\n";
                // Fill remaining results with failure
                while (results.size() < expressions.size()) {
                    results.push_back(EvaluationResult("Stopped due to previous failure"));
                }
                break;
            }
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

llvm::Expected<SwiftPartialTranslationUnit&> SwiftJITREPL::Parse(const std::string& code) {
    if (!pImpl->interpreter) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "Interpreter not initialized");
    }
    
    return pImpl->interpreter->getIncrementalParser()->Parse(code);
}

llvm::Error SwiftJITREPL::Execute(SwiftPartialTranslationUnit& ptu) {
    if (!pImpl->interpreter) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "Interpreter not initialized");
    }
    
    return pImpl->interpreter->getIncrementalExecutor()->addModule(ptu);
}

llvm::Error SwiftJITREPL::ParseAndExecute(const std::string& code) {
    if (!pImpl->interpreter) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "Interpreter not initialized");
    }
    
    return pImpl->interpreter->ParseAndExecute(code);
}

llvm::Error SwiftJITREPL::Undo(unsigned N) {
    if (!pImpl->interpreter) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "Interpreter not initialized");
    }
    
    return pImpl->interpreter->Undo(N);
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

SwiftInterpreter* SwiftJITREPL::getInterpreter() {
    return pImpl->interpreter.get();
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
        
        // Set up frontend options for SIL generation (to generate SIL functions)
        invocation.getFrontendOptions().RequestedAction = swift::FrontendOptions::ActionType::EmitSILGen;
        
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

SwiftIncrementalParser::SwiftIncrementalParser(swift::CompilerInstance* Instance, llvm::orc::ThreadSafeContext* TSCtx)
    : CI(Instance), TSCtx(TSCtx) {
}

SwiftIncrementalParser::~SwiftIncrementalParser() {
    // Clean up all PTUs
    for (auto& ptu : PTUs) {
        CleanUpPTU(ptu);
    }
}

llvm::Expected<SwiftPartialTranslationUnit&> SwiftIncrementalParser::Parse(llvm::StringRef Input) {
    llvm::errs() << "[SwiftIncrementalParser] Parse called with input: " << Input << "\n";
    
    // Create a MemoryBuffer for this expression (like Clang's IncrementalParser)
    // This is more efficient than using temporary files
    std::ostringstream sourceName;
    sourceName << "swift_repl_input_" << InputCount++;
    
    llvm::errs() << "[SwiftIncrementalParser] Source name: " << sourceName.str() << "\n";
    
    // Create an uninitialized memory buffer, copy code in and append "\n"
    size_t inputSize = Input.str().size(); // don't include trailing 0
    std::unique_ptr<llvm::MemoryBuffer> memoryBuffer(
        llvm::WritableMemoryBuffer::getNewUninitMemBuffer(inputSize + 1, sourceName.str()));
    char *bufferStart = const_cast<char *>(memoryBuffer->getBufferStart());
    memcpy(bufferStart, Input.str().data(), inputSize);
    bufferStart[inputSize] = '\n';
    
    llvm::errs() << "[SwiftIncrementalParser] MemoryBuffer created with size: " << (inputSize + 1) << "\n";
    
    // Use the shared CompilerInstance to maintain access to runtime interface functions
    // This allows incremental compilation where new code can reference previously defined functions
    llvm::errs() << "[SwiftIncrementalParser] Using shared CompilerInstance for incremental parsing\n";
    auto invocation = CI->getInvocation();
    
    llvm::errs() << "[SwiftIncrementalParser] Got invocation from shared CI\n";
    
    // Set up the shared CompilerInstance to use the MemoryBuffer
    invocation.getFrontendOptions().InputsAndOutputs.clearInputs();
    invocation.getFrontendOptions().InputsAndOutputs.addInputFile(sourceName.str(), memoryBuffer.get());
    
    llvm::errs() << "[SwiftIncrementalParser] Added input file to invocation\n";
    
    // Check if CompilerInstance is already set up
    if (!CI->hasASTContext()) {
        // Setup the shared CompilerInstance for incremental parsing
        std::string setupError;
        llvm::errs() << "[SwiftIncrementalParser] Setting up shared CompilerInstance for incremental parsing...\n";
        llvm::errs() << "[SwiftIncrementalParser] About to call CompilerInstance::setup()\n";
        int setupResult = CI->setup(invocation, setupError);
        llvm::errs() << "[SwiftIncrementalParser] CompilerInstance::setup() returned: " << setupResult << "\n";
        if (setupResult) {
            llvm::errs() << "[SwiftIncrementalParser] ERROR: Failed to setup compiler: " << setupError << "\n";
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "Failed to setup compiler: " + setupError);
        }
        llvm::errs() << "[SwiftIncrementalParser] CompilerInstance setup successful\n";
    } else {
        llvm::errs() << "[SwiftIncrementalParser] CompilerInstance already set up, reusing existing context\n";
        // The CompilerInstance is already set up, we can reuse the existing context
        // The runtime interface functions should be available in the existing context
    }
    llvm::errs() << "[SwiftIncrementalParser] Checking AST context state after setup...\n";
    llvm::errs() << "[SwiftIncrementalParser] AST context had error after setup: " << CI->getASTContext().hadError() << "\n";
    llvm::errs() << "[SwiftIncrementalParser] Diagnostics had error after setup: " << CI->getDiags().hadAnyError() << "\n";
    
    // Add the Swift code as a source file to the module
    llvm::errs() << "[SwiftIncrementalParser] Adding Swift code as source file to module...\n";
    
    // Get the main module first
    auto* mainModule = CI->getMainModule();
    if (!mainModule) {
        llvm::errs() << "[SwiftIncrementalParser] ERROR: No main module found\n";
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "No main module found");
    }
    
    // Create a source buffer for the Swift code
    auto sourceBuffer = llvm::MemoryBuffer::getMemBufferCopy(Input.str(), "input.swift");
    auto bufferID = CI->getSourceMgr().addNewSourceBuffer(std::move(sourceBuffer));
    llvm::errs() << "[SwiftIncrementalParser] Source buffer created with ID: " << bufferID << "\n";
    
    // Create a source file for the main module using a different approach
    // Since createSourceFileForMainModule is private, we'll create the source file directly
    auto isPrimary = true; // This is a primary input
    swift::SourceFile::ParsingOptions opts; // Use default parsing options
    
    auto* sourceFile = new (CI->getASTContext())
        swift::SourceFile(*mainModule, swift::SourceFileKind::Main, bufferID, opts, isPrimary);
    
    llvm::errs() << "[SwiftIncrementalParser] Source file created successfully\n";
    
    // Perform semantic analysis
    llvm::errs() << "[SwiftIncrementalParser] Performing semantic analysis...\n";
    llvm::errs() << "[SwiftIncrementalParser] About to call performSema()\n";
    CI->performSema();
    llvm::errs() << "[SwiftIncrementalParser] performSema() completed\n";
    
    llvm::errs() << "[SwiftIncrementalParser] Checking for errors after performSema...\n";
    bool astHadError = CI->getASTContext().hadError();
    bool diagsHadError = CI->getDiags().hadAnyError();
    llvm::errs() << "[SwiftIncrementalParser] AST had error: " << astHadError << "\n";
    llvm::errs() << "[SwiftIncrementalParser] Diagnostics had error: " << diagsHadError << "\n";
    
    if (astHadError || diagsHadError) {
        llvm::errs() << "[SwiftIncrementalParser] ERROR: Semantic analysis failed\n";
        llvm::errs() << "[SwiftIncrementalParser] AST context had error: " << CI->getASTContext().hadError() << "\n";
        llvm::errs() << "[SwiftIncrementalParser] Diagnostics had error: " << CI->getDiags().hadAnyError() << "\n";
        
        // Print diagnostic messages
        llvm::errs() << "[SwiftIncrementalParser] Diagnostic messages:\n";
        
        // Try to get diagnostic information
        auto& diags = CI->getDiags();
        llvm::errs() << "  Diagnostics had any error: " << diags.hadAnyError() << "\n";
        
        // Note: Swift's DiagnosticEngine doesn't provide easy access to individual diagnostics
        // We can only check if there were errors, not get the specific messages
        
        // Print AST information
        llvm::errs() << "[SwiftIncrementalParser] AST information:\n";
        auto* module = CI->getMainModule();
        if (module) {
            llvm::errs() << "  Module name: " << module->getName() << "\n";
            
            // Get top-level declarations
            llvm::SmallVector<swift::Decl*, 32> topLevelDecls;
            module->getTopLevelDecls(topLevelDecls);
            llvm::errs() << "  Module declarations: " << topLevelDecls.size() << "\n";
            
            // Print top-level declarations
            for (auto* decl : topLevelDecls) {
                llvm::errs() << "    Declaration: ";
                if (auto* func = llvm::dyn_cast<swift::FuncDecl>(decl)) {
                    llvm::errs() << "func " << func->getName() << "\n";
                } else if (auto* var = llvm::dyn_cast<swift::VarDecl>(decl)) {
                    llvm::errs() << "var " << var->getName() << "\n";
                } else if (auto* import = llvm::dyn_cast<swift::ImportDecl>(decl)) {
                    llvm::errs() << "import " << import->getModulePath().front().Item << "\n";
                } else {
                    llvm::errs() << "unknown declaration type\n";
                }
            }
        } else {
            llvm::errs() << "  No main module found\n";
        }
        
        // Try to get source file information
        auto& sourceMgr = CI->getSourceMgr();
        llvm::errs() << "[SwiftIncrementalParser] Source manager info:\n";
        llvm::errs() << "  Source manager available: true\n";
        
        // Print AST context information
        auto& astCtx = CI->getASTContext();
        llvm::errs() << "[SwiftIncrementalParser] AST context info:\n";
        llvm::errs() << "  Had error: " << astCtx.hadError() << "\n";
        llvm::errs() << "  Lang options: " << astCtx.LangOpts.Target.getTriple() << "\n";
        llvm::errs() << "  Search paths: " << astCtx.SearchPathOpts.RuntimeLibraryPaths.size() << " runtime paths\n";
        
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "Semantic analysis failed");
    }
    
    llvm::errs() << "[SwiftIncrementalParser] Semantic analysis successful\n";
    
    // Get the main module (already obtained above)
    llvm::errs() << "[SwiftIncrementalParser] Getting main module...\n";
    if (!mainModule) {
        llvm::errs() << "[SwiftIncrementalParser] ERROR: No main module found\n";
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "No main module found");
    }
    
    llvm::errs() << "[SwiftIncrementalParser] Main module found: " << mainModule->getName() << "\n";
    
    // Create a new PTU for this expression
    llvm::errs() << "[SwiftIncrementalParser] Creating new PTU\n";
    PTUs.emplace_back();
    auto& ptu = PTUs.back();
    ptu.ModulePart = mainModule;
    ptu.InputCode = Input.str();
    
    llvm::errs() << "[SwiftIncrementalParser] PTU created, lowering Swift to an LLVM module...\n";
    
    // Log the input code that was parsed
    llvm::errs() << "[SwiftIncrementalParser] Input code that was parsed: '" << Input.str() << "'\n";
    
    // Verify the state of CI, mainModule, and llvmCtx before calling lowerSwiftCodeToLLVMModule
    llvm::errs() << "[SwiftIncrementalParser] === STATE VERIFICATION ===\n";
    
    // Check CompilerInstance state
    llvm::errs() << "[SwiftIncrementalParser] CompilerInstance state:\n";
    llvm::errs() << "[SwiftIncrementalParser]   - CI valid: " << (CI ? "YES" : "NO") << "\n";
    if (CI) {
        llvm::errs() << "[SwiftIncrementalParser]   - Has AST context: " << (CI->hasASTContext() ? "YES" : "NO") << "\n";
        llvm::errs() << "[SwiftIncrementalParser]   - AST context had error: " << (CI->getASTContext().hadError() ? "YES" : "NO") << "\n";
    }
    
    // Check mainModule state
    llvm::errs() << "[SwiftIncrementalParser] MainModule state:\n";
    llvm::errs() << "[SwiftIncrementalParser]   - Module valid: " << (mainModule ? "YES" : "NO") << "\n";
    if (mainModule) {
        llvm::errs() << "[SwiftIncrementalParser]   - Module name: " << mainModule->getName().str() << "\n";
        llvm::errs() << "[SwiftIncrementalParser]   - Module filename: " << (mainModule->getModuleFilename().empty() ? "NONE" : mainModule->getModuleFilename().str()) << "\n";
        
        // Check what declarations are in the module
        llvm::SmallVector<swift::Decl*, 10> topLevelDecls;
        mainModule->getTopLevelDecls(topLevelDecls);
        llvm::errs() << "[SwiftIncrementalParser]   - Module declarations count: " << topLevelDecls.size() << "\n";
        for (size_t i = 0; i < topLevelDecls.size() && i < 5; ++i) {
            auto decl = topLevelDecls[i];
            llvm::errs() << "[SwiftIncrementalParser]     [" << i << "] " << swift::Decl::getKindName(decl->getKind()) << "\n";
        }
        if (topLevelDecls.size() > 5) {
            llvm::errs() << "[SwiftIncrementalParser]     ... and " << (topLevelDecls.size() - 5) << " more\n";
        }
        
        // Check if the module has any source files
        llvm::errs() << "[SwiftIncrementalParser]   - Source files count: " << mainModule->getFiles().size() << "\n";
        for (size_t i = 0; i < mainModule->getFiles().size() && i < 3; ++i) {
            auto file = mainModule->getFiles()[i];
            llvm::errs() << "[SwiftIncrementalParser]     [" << i << "] " << static_cast<int>(file->getKind()) << "\n";
        }
    }
    
    // Check llvmCtx state
    llvm::errs() << "[SwiftIncrementalParser] LLVMContext state:\n";
    auto llvmCtx = TSCtx->getContext();
    llvm::errs() << "[SwiftIncrementalParser]   - LLVMContext valid: " << (llvmCtx ? "YES" : "NO") << "\n";
    if (llvmCtx) {
        llvm::errs() << "[SwiftIncrementalParser]   - LLVMContext address: " << llvmCtx << "\n";
    }
    
    llvm::errs() << "[SwiftIncrementalParser] === END STATE VERIFICATION ===\n";
    
    // Lower Swift code to an LLVM module
    auto llvmModule = lowerSwiftCodeToLLVMModule(CI, mainModule, llvmCtx);
    if (llvmModule) {
        llvm::errs() << "[SwiftIncrementalParser] LLVM module generated successfully\n";
        ptu.TheModule = std::move(llvmModule);
    } else {
        llvm::errs() << "[SwiftIncrementalParser] WARNING: LLVM module generation failed\n";
        // If lowering fails, we'll still create the PTU but without an LLVM module
        ptu.TheModule = nullptr;
    }
    
    llvm::errs() << "[SwiftIncrementalParser] Parse completed successfully\n";
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
            auto Err = tracker->remove();
            if (Err) {
                // Log the error but don't throw in destructor
                llvm::consumeError(std::move(Err));
            }
        }
    }
}

llvm::Error SwiftIncrementalExecutor::addModule(SwiftPartialTranslationUnit& PTU) {
    llvm::errs() << "[SwiftIncrementalExecutor] addModule called\n";
    
    if (!Jit) {
        llvm::errs() << "[SwiftIncrementalExecutor] ERROR: JIT not initialized\n";
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "JIT not initialized");
    }
    
    llvm::errs() << "[SwiftIncrementalExecutor] JIT is initialized, creating resource tracker\n";
    
    // Create a resource tracker for this PTU
    llvm::orc::ResourceTrackerSP RT =
        Jit->getMainJITDylib().createResourceTracker();
    ResourceTrackers[&PTU] = RT;
    
    llvm::errs() << "[SwiftIncrementalExecutor] Resource tracker created for PTU\n";
    
    // If the PTU has an LLVM module, add it to the JIT
    if (PTU.TheModule) {
        llvm::errs() << "[SwiftIncrementalExecutor] PTU has LLVM module, adding to JIT\n";
        llvm::errs() << "[SwiftIncrementalExecutor] Module name: " << PTU.TheModule->getName() << "\n";
        llvm::errs() << "[SwiftIncrementalExecutor] Module functions: ";
        for (auto& F : *PTU.TheModule) {
            llvm::errs() << F.getName() << " ";
        }
        llvm::errs() << "\n";
        
        // Move the module to the JIT, but release it from the PTU to avoid double-deletion
        llvm::errs() << "[SwiftIncrementalExecutor] About to add module to JIT...\n";
        llvm::errs() << "[SwiftIncrementalExecutor] Dumping LLVM IR content:\n";
        PTU.TheModule->print(llvm::errs(), nullptr);
        llvm::errs() << "[SwiftIncrementalExecutor] End of LLVM IR dump\n";
        auto result = Jit->addIRModule(RT, {std::move(PTU.TheModule), TSCtx});
        // Release the module from the PTU to prevent double-deletion
        PTU.TheModule.release();
        if (result) {
            llvm::errs() << "[SwiftIncrementalExecutor] ERROR: Failed to add IR module to JIT: " 
                        << llvm::toString(std::move(result)) << "\n";
        } else {
            llvm::errs() << "[SwiftIncrementalExecutor] Successfully added IR module to JIT\n";
            llvm::errs() << "[SwiftIncrementalExecutor] JIT module addition completed\n";
        }
        return result;
    }
    
    // If no LLVM module, we need to compile the Swift code to LLVM IR first
    llvm::errs() << "[SwiftIncrementalExecutor] PTU has no LLVM module\n";
    llvm::errs() << "[SwiftIncrementalExecutor] Input code: " << PTU.InputCode << "\n";
    llvm::errs() << "[SwiftIncrementalExecutor] Module part: " << (PTU.ModulePart ? "present" : "null") << "\n";    
    llvm::errs() << "[SwiftIncrementalExecutor] Returning success (no LLVM module to add)\n";
    return llvm::Error::success();
}

llvm::Error SwiftIncrementalExecutor::removeModule(SwiftPartialTranslationUnit& PTU) {
    llvm::orc::ResourceTrackerSP RT = std::move(ResourceTrackers[&PTU]);
    if (!RT)
        return llvm::Error::success();

    ResourceTrackers.erase(&PTU);
    if (llvm::Error Err = RT->remove())
        return Err;
    return llvm::Error::success();
}

llvm::Error SwiftIncrementalExecutor::execute() {
    llvm::errs() << "[SwiftIncrementalExecutor::execute] Starting execution\n";
    
    if (!Jit) {
        llvm::errs() << "[SwiftIncrementalExecutor::execute] ERROR: JIT not initialized\n";
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "JIT not initialized");
    }
    
    // Check if we have any modules to execute
    if (ResourceTrackers.empty()) {
        llvm::errs() << "[SwiftIncrementalExecutor::execute] No modules to execute\n";
        return llvm::Error::success();
    }
    
    llvm::errs() << "[SwiftIncrementalExecutor::execute] Found " << ResourceTrackers.size() << " modules to execute\n";
    
    // Initialize the JIT dylib only once (equivalent to Clang's runCtors)
    if (!Initialized) {
        llvm::errs() << "[SwiftIncrementalExecutor::execute] Initializing JIT dylib...\n";
        auto& MainJITDylib = Jit->getMainJITDylib();
        if (auto Err = Jit->initialize(MainJITDylib)) {
            llvm::errs() << "[SwiftIncrementalExecutor::execute] ERROR: Failed to initialize JIT dylib\n";
            return Err;
        }
        Initialized = true;
        llvm::errs() << "[SwiftIncrementalExecutor::execute] JIT dylib initialized successfully\n";
    }
    
    // Look up and call the swift_jit_main function
    llvm::errs() << "[SwiftIncrementalExecutor::execute] Looking up swift_jit_main function...\n";
    auto mainAddrOrErr = getSymbolAddress("swift_jit_main");
    if (mainAddrOrErr) {
        llvm::errs() << "[SwiftIncrementalExecutor::execute] Found swift_jit_main at address: " 
                    << mainAddrOrErr->getValue() << "\n";
        
        // Cast the address to a function pointer and call it
        using MainFunc = void(*)();
        MainFunc mainFunc = reinterpret_cast<MainFunc>(mainAddrOrErr->getValue());
        llvm::errs() << "[SwiftIncrementalExecutor::execute] Calling swift_jit_main...\n";
        mainFunc();
        llvm::errs() << "[SwiftIncrementalExecutor::execute] swift_jit_main completed\n";
    } else {
        llvm::errs() << "[SwiftIncrementalExecutor::execute] WARNING: Could not find swift_jit_main function: " 
                    << llvm::toString(mainAddrOrErr.takeError()) << "\n";
        // Don't return error, just log the warning - some modules might not have main functions
    }
    
    return llvm::Error::success();
}

llvm::Expected<llvm::orc::ExecutorAddr> SwiftIncrementalExecutor::getSymbolAddress(llvm::StringRef Name) const {
    if (!Jit) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "JIT not initialized");
    }
    
    using namespace llvm::orc;
    auto SO = makeJITDylibSearchOrder({&Jit->getMainJITDylib(),
                                       Jit->getPlatformJITDylib().get(),
                                       Jit->getProcessSymbolsJITDylib().get()});

    auto& ES = Jit->getExecutionSession();
    auto SymOrErr = ES.lookup(SO, ES.intern(Name));
    if (auto Err = SymOrErr.takeError())
        return std::move(Err);
    return SymOrErr->getAddress();
}

llvm::Error SwiftIncrementalExecutor::cleanUp() {
    if (!Jit) {
        return llvm::Error::success();
    }
    
    // This calls the global dtors of registered modules
    return Jit->deinitialize(Jit->getMainJITDylib());
}

// ============================================================================
// SwiftInterpreter Implementation
// ============================================================================

SwiftInterpreter::SwiftInterpreter(swift::CompilerInstance* CI) {
    // Initialize LLVM targets
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    
    // Create thread-safe context
    TSCtx = std::make_unique<llvm::orc::ThreadSafeContext>(std::make_unique<llvm::LLVMContext>());
    
    // Create incremental parser with the shared CompilerInstance
    // This allows incremental compilation where new code can reference previously defined variables
    IncrParser = std::make_unique<SwiftIncrementalParser>(CI, TSCtx.get());
    
    // Create JIT builder
    auto jitBuilder = llvm::orc::LLJITBuilder();
    auto jitOrError = jitBuilder.create();
    if (!jitOrError) {
        llvm::errs() << "Failed to create JIT builder: " << llvm::toString(jitOrError.takeError()) << "\n";
        return;
    }
    
    // Create incremental executor
    IncrExecutor = std::make_unique<SwiftIncrementalExecutor>(*TSCtx, std::move(*jitOrError));
    
    // Initialize the runtime interface by parsing the runtime code
    // This sets up the global variables and function declarations
    llvm::errs() << "[SwiftInterpreter] Attempting to parse runtime interface...\n";
    llvm::errs() << "[SwiftInterpreter] Runtime code to parse:\n" << SwiftRuntimes << "\n";
    auto runtimePTUOrError = IncrParser->Parse(SwiftRuntimes);
    if (runtimePTUOrError) {
        auto& runtimePTU = *runtimePTUOrError;
        llvm::errs() << "[SwiftInterpreter] Runtime interface parsed successfully\n";
        // Set up global variables
        // Note: In a full implementation, we'd need to properly initialize
        // the global variables with the interpreter pointer
        if (auto err = IncrExecutor->addModule(runtimePTU)) {
            llvm::errs() << "[SwiftInterpreter] ERROR: Failed to add runtime interface module: " 
                        << llvm::toString(std::move(err)) << "\n";
            // Stop on first failure
            return;
        }
        llvm::errs() << "[SwiftInterpreter] Runtime interface module added successfully\n";
        
        // Global variable initialization removed - focusing on basic execution
        
        llvm::errs() << "[SwiftInterpreter] Runtime interface setup completed\n";
    } else {
        llvm::errs() << "[SwiftInterpreter] ERROR: Failed to parse runtime interface: " 
                    << llvm::toString(runtimePTUOrError.takeError()) << "\n";
        llvm::errs() << "[SwiftInterpreter] Stopping initialization due to runtime interface failure\n";
        // Throw exception to propagate failure to caller
        throw std::runtime_error("Runtime interface parsing failed: " + 
                                llvm::toString(runtimePTUOrError.takeError()));
    }
    
    // Mark the start of user code (separates runtime code from user code)
    markUserCodeStart();
}

SwiftInterpreter::~SwiftInterpreter() = default;

void SwiftInterpreter::markUserCodeStart() {
    assert(!InitPTUSize && "We only do this once");
    InitPTUSize = IncrParser->getPTUs().size();
}

size_t SwiftInterpreter::getEffectivePTUSize() const {
    std::list<SwiftPartialTranslationUnit> &PTUs = IncrParser->getPTUs();
    assert(PTUs.size() >= InitPTUSize && "empty PTU list?");
    return PTUs.size() - InitPTUSize;
}

llvm::Error SwiftInterpreter::Undo(unsigned N) {
    std::list<SwiftPartialTranslationUnit> &PTUs = IncrParser->getPTUs();
    if (N > getEffectivePTUSize())
        return llvm::make_error<llvm::StringError>("Operation failed. "
                                                   "Too many undos",
                                                   std::error_code());
    
    for (unsigned I = 0; I < N; I++) {
        if (IncrExecutor) {
            if (llvm::Error Err = IncrExecutor->removeModule(PTUs.back()))
                return Err;
        }
        
        IncrParser->CleanUpPTU(PTUs.back());
        PTUs.pop_back();
    }
    return llvm::Error::success();
}

llvm::Error SwiftInterpreter::ParseAndExecute(llvm::StringRef Code) {
    llvm::errs() << "[SwiftInterpreter::ParseAndExecute] Starting execution of: " << Code << "\n";
    
    // Transform the code to wrap it in a main function
    std::string transformedCode = synthesizeExpr(Code.str());
    llvm::errs() << "[SwiftInterpreter::ParseAndExecute] Transformed code: " << transformedCode << "\n";
    
    // Parse the transformed code
    llvm::errs() << "[SwiftInterpreter::ParseAndExecute] About to parse transformed code...\n";
    auto ptuOrError = IncrParser->Parse(transformedCode);
    if (!ptuOrError) {
        llvm::errs() << "[SwiftInterpreter::ParseAndExecute] ERROR: Parse failed\n";
        return ptuOrError.takeError();
    }
    llvm::errs() << "[SwiftInterpreter::ParseAndExecute] Parse successful\n";
    
    auto& ptu = *ptuOrError;
    
    Execute(ptu);
}

llvm::Error SwiftInterpreter::Execute(SwiftPartialTranslationUnit& ptu) {
    // Add to JIT
    llvm::errs() << "[SwiftInterpreter::ParseAndExecute] About to add module to JIT...\n";
    auto addError = IncrExecutor->addModule(ptu);
    if (addError) {
        llvm::errs() << "[SwiftInterpreter::ParseAndExecute] ERROR: Failed to add module to JIT\n";
        return addError;
    }
    llvm::errs() << "[SwiftInterpreter::ParseAndExecute] Module added to JIT successfully\n";
    
    // Execute
    llvm::errs() << "[SwiftInterpreter::ParseAndExecute] About to execute JIT code...\n";
    auto execError = IncrExecutor->execute();
    if (execError) {
        llvm::errs() << "[SwiftInterpreter::ParseAndExecute] ERROR: Execution failed\n";
        return execError;
    }
    llvm::errs() << "[SwiftInterpreter::ParseAndExecute] JIT execution completed\n";
    
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

SwiftIncrementalParser* SwiftInterpreter::getIncrementalParser() {
    return IncrParser.get();
}

SwiftIncrementalExecutor* SwiftInterpreter::getIncrementalExecutor() {
    return IncrExecutor.get();
}

std::unique_ptr<SwiftRuntimeInterfaceBuilder> SwiftInterpreter::findRuntimeInterface() {
    if (!RuntimeIB) {
        RuntimeIB = std::make_unique<InProcessSwiftRuntimeInterfaceBuilder>(*this);
    }
    return std::move(RuntimeIB);
}

std::string SwiftInterpreter::synthesizeExpr(const std::string& code) {
    if (!RuntimeIB) {
        RuntimeIB = std::make_unique<InProcessSwiftRuntimeInterfaceBuilder>(*this);
    }
    
    auto transformer = RuntimeIB->getPrintValueTransformer();
    if (transformer) {
        return (*transformer)(code);
    }
    
    // Fallback to original code if no transformer
    return code;
}

} // namespace SwiftJITREPL
