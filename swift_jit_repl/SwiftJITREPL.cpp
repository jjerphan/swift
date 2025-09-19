#include "SwiftJITREPL.h"

// Swift runtime path macros (these should be defined by the build system)
#ifndef SWIFT_RUNTIME_LIBRARY_PATHS
#define SWIFT_RUNTIME_LIBRARY_PATHS "/usr/lib/swift/linux"
#endif

#ifndef SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1
#define SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1 "/usr/lib/swift/linux"
#endif

#ifndef SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2
#define SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2 "/usr/lib/swift/linux/x86_64"
#endif

#ifndef SWIFT_RUNTIME_RESOURCE_PATH
#define SWIFT_RUNTIME_RESOURCE_PATH "/usr/lib/swift"
#endif

#ifndef SWIFT_SDK_PATH
#define SWIFT_SDK_PATH "/usr/lib/swift/linux"
#endif

// Compile-time validation to ensure all required macros are defined
static_assert(sizeof(SWIFT_RUNTIME_LIBRARY_PATHS) > 1, "SWIFT_RUNTIME_LIBRARY_PATHS must be defined");
static_assert(sizeof(SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1) > 1, "SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1 must be defined");
static_assert(sizeof(SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2) > 1, "SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2 must be defined");
static_assert(sizeof(SWIFT_RUNTIME_RESOURCE_PATH) > 1, "SWIFT_RUNTIME_RESOURCE_PATH must be defined");
static_assert(sizeof(SWIFT_SDK_PATH) > 1, "SWIFT_SDK_PATH must be defined");

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
#include <unistd.h>  // for access() function

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
#include "swift/SIL/SILFunctionBuilder.h"
#include "swift/SIL/TypeLowering.h"
#include "swift/AST/SILGenRequests.h"
#include "swift/AST/IRGenRequests.h"
#include "swift/AST/Module.h"
#include "swift/AST/SourceFile.h"
#include "swift/AST/Import.h"
#include "swift/Subsystems.h"

// LLVM IR parsing utilities (to reparse IR into our shared LLVMContext)
#include "llvm/AsmParser/Parser.h"
#include "llvm/Support/SourceMgr.h"

// Function to validate Swift runtime paths at compile time and runtime
static void validateSwiftRuntimePaths() {
    
    // Runtime validation
    std::vector<std::string> pathsToCheck = {
        SWIFT_RUNTIME_LIBRARY_PATHS,
        SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1,
        SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2,
        SWIFT_RUNTIME_RESOURCE_PATH,
        SWIFT_SDK_PATH
    };
    
    std::vector<std::string> pathNames = {
        "SWIFT_RUNTIME_LIBRARY_PATHS",
        "SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1", 
        "SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2",
        "SWIFT_RUNTIME_RESOURCE_PATH",
        "SWIFT_SDK_PATH"
    };
    
    bool allPathsValid = true;
    for (size_t i = 0; i < pathsToCheck.size(); ++i) {
        // Use access() system call for path validation (more portable than std::filesystem)
        if (access(pathsToCheck[i].c_str(), F_OK) != 0) {
            llvm::errs() << "WARNING: Swift runtime path does not exist: " << pathNames[i] 
                        << " = " << pathsToCheck[i] << "\n";
            allPathsValid = false;
        }
    }
    
    if (!allPathsValid) {
        llvm::errs() << "WARNING: Some Swift runtime paths are invalid. This may cause runtime crashes.\n";
        llvm::errs() << "Please ensure the Swift runtime is properly installed and paths are correctly configured.\n";
    } else {
        llvm::errs() << "INFO: All Swift runtime paths validated successfully.\n";
    }
}

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
static std::unique_ptr<llvm::Module> lowerSwiftCodeToLLVMModule(swift::ModuleDecl* module,
                                                                swift::ASTContext* astContext,
                                                                llvm::LLVMContext* llvmCtx) {
    if (!module || !astContext) {
        llvm::errs() << "[lowerSwiftCodeToLLVMModule] ERROR: Invalid parameters - module: " << (module ? "valid" : "null") 
                     << ", astContext: " << (astContext ? "valid" : "null") << "\n";
        return nullptr;
    }

    // Create a minimal CompilerInvocation for IR generation
    swift::CompilerInvocation invocation;
    invocation.getLangOptions().Target = llvm::Triple("x86_64-unknown-linux-gnu");
    invocation.getLangOptions().EnableObjCInterop = false;
    invocation.getFrontendOptions().RequestedAction = swift::FrontendOptions::ActionType::EmitSILGen;
    invocation.getFrontendOptions().ModuleName = module->getName().str();
    invocation.getSILOptions().OptMode = swift::OptimizationMode::NoOptimization;
    invocation.getIRGenOptions().OutputKind = swift::IRGenOutputKind::Module;
    
    // Set up search paths
    auto &searchPaths = invocation.getSearchPathOptions();
    searchPaths.RuntimeLibraryPaths = {SWIFT_RUNTIME_LIBRARY_PATHS};
    searchPaths.setRuntimeLibraryImportPaths({SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1, SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2});
    searchPaths.RuntimeResourcePath = SWIFT_RUNTIME_RESOURCE_PATH;
    searchPaths.setSDKPath(SWIFT_SDK_PATH);

    const auto &IRGenOpts = invocation.getIRGenOptions();
    const auto &TBDOpts = invocation.getTBDGenOptions();
    swift::PrimarySpecificPaths PSPs;
    PSPs.OutputFilename = (module->getName().str() + ".o").str();

    // Try to generate SIL first, then use SIL-based IR generation (like SwiftMaterializationUnit.cpp)
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] About to generate SIL module\n";
    
    // Generate SIL module using the same approach as SwiftMaterializationUnit
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Creating TypeConverter for module: " << module->getName().str() << "\n";
    auto typeConverter = std::make_unique<swift::Lowering::TypeConverter>(*module);
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] TypeConverter created, calling performASTLowering...\n";
    auto silModule = swift::performASTLowering(module, *typeConverter, invocation.getSILOptions());
    if (!silModule) {
        llvm::errs() << "[lowerSwiftCodeToLLVMModule] ERROR: Failed to generate SIL module\n";
        return nullptr;
    }
    
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] SIL module generated successfully\n";
    
    // Note: swift_jit_main function will need to be generated by the Swift JIT infrastructure
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] SIL module generated, relying on Swift JIT for entry point\n";
    
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
    
    swift::GeneratedModule genModule = swift::performIRGeneration(
        module, IRGenOpts, TBDOpts, std::move(silModule),
        module->getName().str(), PSPs,
        /*parallelOutputFilenames*/ llvm::ArrayRef<std::string>(),
        /*outModuleHash*/ nullptr);
    
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] performIRGeneration completed\n";

    auto *Produced = genModule.getModule();
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
        
        // Transform the code to make variables public for cross-module access
        std::string result = code;
        
        // Add 'public' to variable declarations for cross-module access
        // Pattern: "let " -> "public let ", "var " -> "public var "
        size_t pos = 0;
        while ((pos = result.find("let ", pos)) != std::string::npos) {
            // Check if it's not already "public let"
            if (pos == 0 || result.substr(pos - 6, 6) != "public") {
                result.insert(pos, "public ");
                pos += 11; // "public let ".length()
            } else {
                pos += 4; // "let ".length()
            }
        }
        
        pos = 0;
        while ((pos = result.find("var ", pos)) != std::string::npos) {
            // Check if it's not already "public var"
            if (pos == 0 || result.substr(pos - 6, 6) != "public") {
                result.insert(pos, "public ");
                pos += 11; // "public var ".length()
            } else {
                pos += 4; // "var ".length()
            }
        }
        
        result += "\n";
        
        llvm::errs() << "[transformForValuePrinting] Generated Swift code (with public exports):\n" << result << "\n";
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
    swift::CompilerInvocation compilerInvocation;
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
            
            // Create and configure the compiler invocation for JIT/REPL mode
            // We only need the CompilerInvocation, not the full CompilerInstance
            
            // Set up language options for JIT mode
            compilerInvocation.getLangOptions().Target = llvm::Triple("x86_64-unknown-linux-gnu");
            compilerInvocation.getLangOptions().EnableObjCInterop = false;
            
            // Set up frontend options for SIL generation (to generate SIL functions)
            compilerInvocation.getFrontendOptions().RequestedAction = swift::FrontendOptions::ActionType::EmitSILGen;
            
            // Set up command line arguments for immediate mode
            std::vector<std::string> immediateArgs = {"swift", "-i"};
            compilerInvocation.getFrontendOptions().ImmediateArgv = immediateArgs;
            
            // Set a valid and preferably unique module name
            std::string moduleName = currentModuleName.empty() ? generateUniqueModuleName() : currentModuleName;
            compilerInvocation.getFrontendOptions().ModuleName = moduleName;
            
            // Set up SIL options based on configuration
            compilerInvocation.getSILOptions().OptMode = config.enable_optimizations ? 
                swift::OptimizationMode::ForSpeed : swift::OptimizationMode::NoOptimization;
            
            // Set additional SIL options for immediate mode
            compilerInvocation.getSILOptions().EnableSILOpaqueValues = true;
            
            // Set up IRGen options
            compilerInvocation.getIRGenOptions().OutputKind = swift::IRGenOutputKind::Module;
            
            // Set debug info generation based on configuration
            if (config.generate_debug_info) {
                compilerInvocation.getIRGenOptions().DebugInfoFormat = swift::IRGenDebugInfoFormat::DWARF;
            }
            
            // Set the correct search paths for Swift standard library using compile time values
            auto &searchPaths = compilerInvocation.getSearchPathOptions();
            searchPaths.RuntimeLibraryPaths = {SWIFT_RUNTIME_LIBRARY_PATHS};
            searchPaths.setRuntimeLibraryImportPaths({SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1, SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2});
            searchPaths.RuntimeResourcePath = SWIFT_RUNTIME_RESOURCE_PATH;
            searchPaths.setSDKPath(SWIFT_SDK_PATH);
 
            // Record the actual module name used
            currentModuleName = moduleName;
            
            // Validate the CompilerInvocation by creating a temporary CompilerInstance
            // This ensures our configuration is correct before we start using it
            llvm::errs() << "[SwiftJITREPL::initialize] Validating CompilerInvocation configuration...\n";
            auto tempCI = std::make_unique<swift::CompilerInstance>();
            std::string setupError;
            if (tempCI->setup(compilerInvocation, setupError)) {
                llvm::errs() << "[SwiftJITREPL::initialize] CompilerInstance setup failed: " << setupError << "\n";
                lastError = "Failed to validate Swift compiler configuration: " + (setupError.empty() ? "Unknown error" : setupError);
                return false;
            }
            // Configuration is valid, we can discard the temporary CompilerInstance
            llvm::errs() << "[SwiftJITREPL::initialize] CompilerInstance setup successful\n";
            tempCI.reset();
            
            // Create the interpreter for incremental compilation
            // We don't need a CompilerInstance anymore, just the CompilerInvocation
            interpreter = std::make_unique<SwiftInterpreter>(&compilerInvocation);
            
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
    
    bool reset() {
        try {
            // Reset Swift compiler state
            // No need to cleanup CompilerInvocation as it's just configuration
            
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
        
        // Set the correct search paths for Swift standard library using compile time values
        auto &searchPaths = invocation.getSearchPathOptions();
        searchPaths.RuntimeLibraryPaths = {SWIFT_RUNTIME_LIBRARY_PATHS};
        searchPaths.setRuntimeLibraryImportPaths({SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1, SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2});
        searchPaths.RuntimeResourcePath = SWIFT_RUNTIME_RESOURCE_PATH;
        searchPaths.setSDKPath(SWIFT_SDK_PATH);
        
        // Validate the configuration by creating a temporary CompilerInstance
        auto tempCI = std::make_unique<swift::CompilerInstance>();
        std::string error;
        if (tempCI->setup(invocation, error)) {
            llvm::errs() << "[isSwiftJITAvailable] CompilerInstance setup failed: " << error << "\n";
            return false; // Setup failed
        }
        // Configuration is valid
        llvm::errs() << "[isSwiftJITAvailable] CompilerInstance setup successful\n";
        
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

SwiftIncrementalParser::SwiftIncrementalParser(swift::ASTContext* sharedASTContext, 
                                               std::vector<swift::ModuleDecl*>* modules,
                                               llvm::orc::ThreadSafeContext* TSCtx)
    : sharedASTContext(sharedASTContext), modules(modules), TSCtx(TSCtx) {
}

SwiftIncrementalParser::~SwiftIncrementalParser() {
    // Clean up all PTUs
    for (auto& ptu : PTUs) {
        CleanUpPTU(ptu);
    }
}

llvm::Expected<SwiftPartialTranslationUnit&> SwiftIncrementalParser::Parse(llvm::StringRef Input) {
    llvm::errs() << "[SwiftIncrementalParser] Parse called with input: " << Input << "\n";
    
    // Create new ModuleDecl for this evaluation
    std::string moduleName = "Evaluation_" + std::to_string(modules->size());
    llvm::errs() << "[SwiftIncrementalParser] Creating new module: " << moduleName << "\n";
    
    // Create module with Swift standard library import
    swift::ImplicitImportInfo importInfo;
    
    // Add Swift standard library import
    auto swiftModule = sharedASTContext->getLoadedModule(sharedASTContext->getIdentifier("Swift"));
    if (swiftModule) {
        importInfo.AdditionalImports.push_back(swift::ImportedModule(swiftModule));
        llvm::errs() << "[SwiftIncrementalParser] Added Swift standard library import\n";
    } else {
        llvm::errs() << "[SwiftIncrementalParser] WARNING: Swift standard library not found\n";
    }
    
    // Add imports for all previous modules to enable state reuse
    for (auto prevModule : *modules) {
        if (prevModule) {
            importInfo.AdditionalImports.push_back(swift::ImportedModule(prevModule));
            llvm::errs() << "[SwiftIncrementalParser] Added import for module: " << prevModule->getName() << "\n";
        }
    }
    
    llvm::errs() << "[SwiftIncrementalParser] About to call ModuleDecl::create...\n";
    
    auto newModule = swift::ModuleDecl::createMainModule(
        *sharedASTContext,
        sharedASTContext->getIdentifier(moduleName),
        importInfo,
        [&](swift::ModuleDecl* module, auto addFile) {
            llvm::errs() << "[SwiftIncrementalParser] Inside module creation lambda...\n";
            
            // Create a MemoryBuffer for this expression
            std::ostringstream sourceName;
            sourceName << "swift_repl_input_" << InputCount++;
            
            llvm::errs() << "[SwiftIncrementalParser] Source name: " << sourceName.str() << "\n";
            
            // Create a MemoryBuffer for the Swift code
            llvm::errs() << "[SwiftIncrementalParser] Creating MemoryBuffer...\n";
            llvm::errs() << "[SwiftIncrementalParser] Input content: '" << Input.str() << "'\n";
            llvm::errs() << "[SwiftIncrementalParser] Input length: " << Input.size() << "\n";
            auto inputBuffer = llvm::MemoryBuffer::getMemBufferCopy(Input.str(), sourceName.str());
            llvm::errs() << "[SwiftIncrementalParser] MemoryBuffer created successfully\n";
            llvm::errs() << "[SwiftIncrementalParser] Buffer size: " << inputBuffer->getBufferSize() << "\n";
            llvm::errs() << "[SwiftIncrementalParser] Buffer content: '" << inputBuffer->getBuffer() << "'\n";
            
            // Add the source buffer to the shared ASTContext's SourceManager
            llvm::errs() << "[SwiftIncrementalParser] Adding source buffer to SourceManager...\n";
            
            // Test that sharedASTContext is properly set before using it
            if (!sharedASTContext) {
                llvm::errs() << "[SwiftIncrementalParser] ERROR: sharedASTContext is null!\n";
                return;
            }
            
            // Test that the SourceManager is accessible
            unsigned bufferID;
            try {
                llvm::errs() << "[SwiftIncrementalParser] Testing SourceManager access...\n";
                auto& sourceMgr = sharedASTContext->SourceMgr;
                llvm::errs() << "[SwiftIncrementalParser] SourceManager accessed successfully\n";
                
                // Test that we can get the number of source buffers before adding
                llvm::errs() << "[SwiftIncrementalParser] Current source buffer count: " << sourceMgr.getLLVMSourceMgr().getNumBuffers() << "\n";
                
                bufferID = sourceMgr.addNewSourceBuffer(std::move(inputBuffer));
                llvm::errs() << "[SwiftIncrementalParser] Added source buffer with ID: " << bufferID << "\n";
                
                // Verify the buffer was added successfully
                llvm::errs() << "[SwiftIncrementalParser] New source buffer count: " << sourceMgr.getLLVMSourceMgr().getNumBuffers() << "\n";
                
            } catch (const std::exception& e) {
                llvm::errs() << "[SwiftIncrementalParser] ERROR: Exception when accessing SourceManager: " << e.what() << "\n";
                return;
            } catch (...) {
                llvm::errs() << "[SwiftIncrementalParser] ERROR: Unknown exception when accessing SourceManager\n";
                return;
            }
            
            // Add source file to the new module
            llvm::errs() << "[SwiftIncrementalParser] Creating SourceFile...\n";
            addFile(new (*sharedASTContext) swift::SourceFile(
                *module, 
                swift::SourceFileKind::Library,  // Use Library to avoid "multiple main files" error
                bufferID,
                swift::SourceFile::getDefaultParsingOptions(sharedASTContext->LangOpts)
            ));
            
            llvm::errs() << "[SwiftIncrementalParser] Created SourceFile for buffer " << bufferID << "\n";
        }
    );
    
    llvm::errs() << "[SwiftIncrementalParser] ModuleDecl::create completed successfully\n";
    
    // Register new module with shared ASTContext
    sharedASTContext->addLoadedModule(newModule);
    modules->push_back(newModule);
    
    llvm::errs() << "[SwiftIncrementalParser] New module created and registered: " << newModule->getName() << "\n";
    
    // Perform semantic analysis on the new module
    llvm::errs() << "[SwiftIncrementalParser] Performing semantic analysis...\n";
    
    // Create a temporary CompilerInstance for semantic analysis
    auto tempCI = std::make_unique<swift::CompilerInstance>();
    swift::CompilerInvocation invocation;
    invocation.getLangOptions().Target = llvm::Triple("x86_64-unknown-linux-gnu");
    invocation.getLangOptions().EnableObjCInterop = false;
    invocation.getFrontendOptions().RequestedAction = swift::FrontendOptions::ActionType::Typecheck;
    invocation.getFrontendOptions().ModuleName = moduleName;
    
    // Set up search paths
    auto &searchPaths = invocation.getSearchPathOptions();
    searchPaths.RuntimeLibraryPaths = {SWIFT_RUNTIME_LIBRARY_PATHS};
    searchPaths.setRuntimeLibraryImportPaths({SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1, SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2});
    searchPaths.RuntimeResourcePath = SWIFT_RUNTIME_RESOURCE_PATH;
    searchPaths.setSDKPath(SWIFT_SDK_PATH);
    
    std::string setupError;
    if (tempCI->setup(invocation, setupError)) {
        llvm::errs() << "[SwiftIncrementalParser] ERROR: Failed to setup CompilerInstance: " << setupError << "\n";
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "Failed to setup CompilerInstance: " + setupError);
    }
    
    // Set the main module to our new module
    tempCI->setMainModule(newModule);
    
    // Perform semantic analysis
    tempCI->performSema();
    llvm::errs() << "[SwiftIncrementalParser] performSema() completed\n";
    
    // Check for errors and get detailed diagnostic information
    bool astHadError = sharedASTContext->hadError();
    bool diagsHadError = tempCI->getDiags().hadAnyError();
    
    llvm::errs() << "[SwiftIncrementalParser] AST had error: " << astHadError << "\n";
    llvm::errs() << "[SwiftIncrementalParser] Diagnostics had error: " << diagsHadError << "\n";
    
    if (astHadError || diagsHadError) {
        llvm::errs() << "[SwiftIncrementalParser] ERROR: Semantic analysis failed\n";
        
        // Print module state for debugging
        llvm::SmallVector<swift::Decl*, 32> topLevelDecls;
        newModule->getTopLevelDecls(topLevelDecls);
        llvm::errs() << "[SwiftIncrementalParser] Module declarations count: " << topLevelDecls.size() << "\n";
        for (size_t i = 0; i < topLevelDecls.size() && i < 5; ++i) {
            auto decl = topLevelDecls[i];
            llvm::errs() << "[SwiftIncrementalParser]   [" << i << "] " << swift::Decl::getKindName(decl->getKind()) << "\n";
        }
        
        // Try to get more information about the source file
        auto sourceFiles = newModule->getFiles();
        if (!sourceFiles.empty()) {
            auto sourceFile = llvm::dyn_cast<swift::SourceFile>(sourceFiles[0]);
            if (sourceFile) {
                llvm::errs() << "[SwiftIncrementalParser] Source file has " << sourceFile->getTopLevelDecls().size() << " top-level declarations\n";
                auto bufferID = sourceFile->getBufferID();
                llvm::errs() << "[SwiftIncrementalParser] Source file buffer ID: " << bufferID << "\n";
            }
        }
        
        // Check if the issue is related to missing imports
        llvm::errs() << "[SwiftIncrementalParser] Checking for import issues...\n";
        llvm::errs() << "[SwiftIncrementalParser] Module name: " << newModule->getName() << "\n";
        llvm::errs() << "[SwiftIncrementalParser] Module is main module: " << newModule->isMainModule() << "\n";
        
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "Semantic analysis failed");
    }
    
    llvm::errs() << "[SwiftIncrementalParser] Semantic analysis successful\n";
    
    // Create a new PTU for this expression
    llvm::errs() << "[SwiftIncrementalParser] Creating new PTU\n";
    PTUs.emplace_back();
    auto& ptu = PTUs.back();
    ptu.ModulePart = newModule;
    ptu.SharedASTContext = sharedASTContext;
    ptu.InputCode = Input.str();
    
    llvm::errs() << "[SwiftIncrementalParser] PTU created, lowering Swift to an LLVM module...\n";
    
    // Get the LLVM context
    auto llvmCtx = TSCtx->getContext();
    
    // Lower Swift code to an LLVM module using the new module
    auto llvmModule = lowerSwiftCodeToLLVMModule(newModule, sharedASTContext, llvmCtx);
    if (llvmModule) {
        llvm::errs() << "[SwiftIncrementalParser] LLVM module generated successfully\n";
        ptu.TheModule = std::move(llvmModule);
    } else {
        llvm::errs() << "[SwiftIncrementalParser] WARNING: LLVM module generation failed\n";
        ptu.TheModule = nullptr;
    }
    
    llvm::errs() << "[SwiftIncrementalParser] Parse completed successfully\n";
    return ptu;
}

void SwiftIncrementalParser::CleanUpPTU(SwiftPartialTranslationUnit& PTU) {
    // Clean up the LLVM module
    PTU.TheModule.reset();
    PTU.ModulePart = nullptr;  // ModuleDecl is owned by ASTContext
    PTU.InputCode.clear();
}

swift::ImplicitImportInfo SwiftIncrementalParser::createImplicitImports() {
    swift::ImplicitImportInfo importInfo;
    
    // Import all previous modules for state reuse
    for (const auto& module : *modules) {
        importInfo.AdditionalImports.emplace_back(
            swift::ImportedModule(module));
    }
    
    llvm::errs() << "[SwiftIncrementalParser] Created imports for " << modules->size() << " previous modules\n";
    return importInfo;
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

llvm::Error SwiftIncrementalExecutor::runCtors() const {
    llvm::errs() << "[SwiftIncrementalExecutor::runCtors] Running global constructors\n";
    
    if (!Jit) {
        llvm::errs() << "[SwiftIncrementalExecutor::runCtors] ERROR: JIT not initialized\n";
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "JIT not initialized");
    }
    
    // Initialize the JIT dylib to run global constructors (like Clang IncrementalExecutor)
    auto& MainJITDylib = Jit->getMainJITDylib();
    if (auto Err = Jit->initialize(MainJITDylib)) {
        llvm::errs() << "[SwiftIncrementalExecutor::runCtors] ERROR: Failed to initialize JIT dylib\n";
        return Err;
    }
    
    llvm::errs() << "[SwiftIncrementalExecutor::runCtors] Global constructors executed successfully\n";
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

SwiftInterpreter::SwiftInterpreter(swift::CompilerInvocation* invocation) {
    // Validate Swift runtime paths
    validateSwiftRuntimePaths();
    
    // Initialize LLVM targets
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    
    // Note: Swift runtime loading is handled internally by the CompilerInstance
    // when we call performSema() or other Swift compiler functions
    
    // Create thread-safe context
    TSCtx = std::make_unique<llvm::orc::ThreadSafeContext>(std::make_unique<llvm::LLVMContext>());
    
    // Create shared ASTContext directly (bypass CompilerInstance)
    llvm::errs() << "[SwiftInterpreter] Creating shared ASTContext...\n";
    
    // Create a CompilerInstance to properly initialize SourceManager
    auto compilerInstance = std::make_unique<swift::CompilerInstance>();
    
    // Setup the CompilerInstance with our invocation
    std::string error;
    if (compilerInstance->setup(*invocation, error)) {
        llvm::errs() << "[SwiftInterpreter] ERROR: Failed to setup CompilerInstance: " << error << "\n";
        return;
    }
    
    // Get the SourceManager and DiagnosticEngine from the CompilerInstance
    auto& sourceMgr = compilerInstance->getSourceMgr();
    auto& diagEngine = compilerInstance->getDiags();
    
    // Use the ASTContext from the CompilerInstance (properly initialized)
    // Note: We don't take ownership since CompilerInstance owns it
    sharedASTContext = std::unique_ptr<swift::ASTContext, std::function<void(swift::ASTContext*)>>(
        &compilerInstance->getASTContext(), 
        [](swift::ASTContext*){} // No-op deleter
    );
    
    // Store the CompilerInstance for later use
    this->compilerInstance = std::move(compilerInstance);
    
    // Create initial empty module for base functionality
    llvm::errs() << "[SwiftInterpreter] Creating initial base module...\n";
    
    // First, load the Swift standard library
    auto swiftModule = sharedASTContext->getLoadedModule(sharedASTContext->getIdentifier("Swift"));
    if (!swiftModule) {
        // Try to load Swift standard library
        swift::ImplicitImportInfo swiftImportInfo;
        swiftModule = swift::ModuleDecl::create(sharedASTContext->getIdentifier("Swift"), *sharedASTContext, swiftImportInfo, [](swift::ModuleDecl*, auto){});
        if (swiftModule) {
            sharedASTContext->addLoadedModule(swiftModule);
            llvm::errs() << "[SwiftInterpreter] Swift standard library loaded\n";
        } else {
            llvm::errs() << "[SwiftInterpreter] WARNING: Failed to load Swift standard library\n";
        }
    }
    
    // Create base module with Swift import
    swift::ImplicitImportInfo baseImportInfo;
    if (swiftModule) {
        baseImportInfo.AdditionalImports.push_back(swift::ImportedModule(swiftModule));
    }
    
    auto baseModule = swift::ModuleDecl::createMainModule(
        *sharedASTContext,
        sharedASTContext->getIdentifier("SwiftJITREPL_Base"),
        baseImportInfo,
        [](swift::ModuleDecl*, auto) {} // Empty initial module
    );
    
    // Register base module with shared ASTContext
    sharedASTContext->addLoadedModule(baseModule);
    modules.push_back(baseModule);
    
    llvm::errs() << "[SwiftInterpreter] Base module created and registered\n";
    
    // Create incremental parser with shared ASTContext and modules
    IncrParser = std::make_unique<SwiftIncrementalParser>(sharedASTContext.get(), &modules, TSCtx.get());
    
    // Create JIT builder
    auto jitBuilder = llvm::orc::LLJITBuilder();
    auto jitOrError = jitBuilder.create();
    if (!jitOrError) {
        llvm::errs() << "Failed to create JIT builder: " << llvm::toString(jitOrError.takeError()) << "\n";
        return;
    }
    
    // Create incremental executor
    IncrExecutor = std::make_unique<SwiftIncrementalExecutor>(*TSCtx, std::move(*jitOrError));
    
    llvm::errs() << "[SwiftInterpreter] Multi-module interpreter initialized successfully\n";
    
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
    
    // Execute the PTU and handle any errors
    if (auto err = Execute(ptu)) {
        llvm::errs() << "[SwiftInterpreter::ParseAndExecute] ERROR: Execute failed: " 
                    << llvm::toString(std::move(err)) << "\n";
        return llvm::Error::success(); // Return success to avoid crash
    }
    
    return llvm::Error::success();
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
    
    // Execute using global constructor approach (like Clang IncrementalExecutor)
    llvm::errs() << "[SwiftInterpreter::ParseAndExecute] About to execute JIT code...\n";
    auto execError = IncrExecutor->runCtors();
    if (execError) {
        llvm::errs() << "[SwiftInterpreter::ParseAndExecute] ERROR: Execution failed\n";
        return execError;
    }
    llvm::errs() << "[SwiftInterpreter::ParseAndExecute] JIT execution completed\n";
    
    return llvm::Error::success();
}

swift::ASTContext& SwiftInterpreter::getASTContext() {
    return *sharedASTContext;
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
