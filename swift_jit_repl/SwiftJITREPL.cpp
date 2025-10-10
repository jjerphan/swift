#include "SwiftJITREPL.h"

// Standard library includes for error handling
#include <iostream>
#include <cstdlib>

// Evaluator debugging APIs are not exposed; cycle dumps are enabled via LangOptions
#include "swift/Frontend/PrintingDiagnosticConsumer.h"

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

// Note: We override the existing AccessLevelRequest to make all declarations public by default

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
        SWIFT_SDK_PATH,
        FOUNDATION_MODULE_PATH,
        FOUNDATION_STATIC_MODULE_PATH,
        DISPATCH_MODULE_PATH,
        DISPATCH_STATIC_MODULE_PATH
    };
    
    std::vector<std::string> pathNames = {
        "SWIFT_RUNTIME_LIBRARY_PATHS",
        "SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1", 
        "SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2",
        "SWIFT_RUNTIME_RESOURCE_PATH",
        "SWIFT_SDK_PATH",
        "FOUNDATION_MODULE_PATH",
        "FOUNDATION_STATIC_MODULE_PATH",
        "DISPATCH_MODULE_PATH",
        "DISPATCH_STATIC_MODULE_PATH"
    };
    
    bool allPathsValid = true;
    std::vector<std::string> invalidPaths;
    std::vector<std::string> invalidPathNames;
    
    for (size_t i = 0; i < pathsToCheck.size(); ++i) {
        // Use access() system call for path validation (more portable than std::filesystem)
        if (access(pathsToCheck[i].c_str(), F_OK) != 0) {
            allPathsValid = false;
            invalidPaths.push_back(pathsToCheck[i]);
            invalidPathNames.push_back(pathNames[i]);
        }
    }
    
    if (!allPathsValid) {
        std::cerr << "ERROR: Invalid Swift runtime paths detected!\n";
        std::cerr << "The following Swift runtime paths are missing or inaccessible:\n\n";
        
        for (size_t i = 0; i < invalidPaths.size(); ++i) {
            std::cerr << "  " << invalidPathNames[i] << ": " << invalidPaths[i] << "\n";
        }
        
        std::cerr << "\nThis will cause runtime crashes. Please ensure:\n";
        std::cerr << "1. Swift is properly built and installed\n";
        std::cerr << "2. The Swift build directory exists and contains all runtime libraries\n";
        std::cerr << "3. All required Swift runtime libraries are present\n";
        std::cerr << "4. You have read permissions for the Swift runtime directories\n\n";
        std::cerr << "Build the Swift project first with: ./utils/build-script --release\n";
        std::cerr << "Then rebuild this project with: ./build.sh\n\n";
        
        std::cerr << "Exiting due to invalid runtime paths.\n";
        std::exit(1);
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
#include "llvm/MC/TargetRegistry.h"

namespace SwiftJITREPL {

// Global flag to ensure LLVM targets are initialized only once
static bool g_llvmTargetsInitialized = false;
static std::mutex g_llvmInitMutex;

/**
 * Helper function to initialize LLVM targets (thread-safe, only once)
 */
inline void initializeLLVMTargetsOnce() {
    std::lock_guard<std::mutex> lock(g_llvmInitMutex);
    if (!g_llvmTargetsInitialized) {
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmPrinters();
        llvm::InitializeAllAsmParsers();
        llvm::InitializeAllDisassemblers();
        llvm::InitializeAllTargetInfos();
        g_llvmTargetsInitialized = true;
    }
}

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
        initialize();
    }
    
    ~Impl() = default;
    
private:
    bool initialize() {
        try {
            // Initialize LLVM targets FIRST before any other operations (only once)
            initializeLLVMTargetsOnce();
            
            // Create and configure the compiler invocation for JIT/REPL mode
            // We only need the CompilerInvocation, not the full CompilerInstance
            
            // Set up language options for JIT mode
            compilerInvocation.getLangOptions().Target = llvm::Triple(TARGET_TRIPLE);
            compilerInvocation.getLangOptions().EnableObjCInterop = true;
            
            // Set up frontend options for SIL generation (to generate SIL functions)
            compilerInvocation.getFrontendOptions().RequestedAction = swift::FrontendOptions::ActionType::EmitSILGen;
            
            // Set the correct search paths for Swift standard library using compile time values
            auto &searchPaths = compilerInvocation.getSearchPathOptions();
            searchPaths.RuntimeLibraryPaths = {SWIFT_RUNTIME_LIBRARY_PATHS};
            searchPaths.setRuntimeLibraryImportPaths({SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1, SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2, FOUNDATION_MODULE_PATH, FOUNDATION_STATIC_MODULE_PATH, DISPATCH_MODULE_PATH, DISPATCH_STATIC_MODULE_PATH});
            searchPaths.RuntimeResourcePath = SWIFT_RUNTIME_RESOURCE_PATH;
            searchPaths.setSDKPath(SWIFT_SDK_PATH);
            
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
            
            // Configure ClangImporter to find system C headers
            auto &clangImporterOpts = compilerInvocation.getClangImporterOptions();
            clangImporterOpts.clangPath = "/usr/bin/clang";  // Use system clang directly
            clangImporterOpts.ModuleCachePath = "/tmp/swift-jit-module-cache";  // Set module cache path
            clangImporterOpts.ExtraArgs = {
                "-I/usr/include",  // Basic system include directory
                "-isystem", "/usr/include",  // System headers
            };
            
            // Record the actual module name used
            currentModuleName = moduleName;
            
            // Validate the CompilerInvocation by creating a temporary CompilerInstance
            // This ensures our configuration is correct before we start using it
            auto tempCI = std::make_unique<swift::CompilerInstance>();
            std::string setupError;
            if (tempCI->setup(compilerInvocation, setupError)) {
                lastError = "Failed to validate Swift compiler configuration: " + (setupError.empty() ? "Unknown error" : setupError);
                initialized = false;
                return false;
            }
            // Configuration is valid, we can discard the temporary CompilerInstance
            tempCI.reset();
            
            // Create the interpreter for incremental compilation
            // We don't need a CompilerInstance anymore, just the CompilerInvocation
            interpreter = std::make_unique<SwiftInterpreter>(&compilerInvocation);
            
            initialized = true;
            return true;
            
        } catch (const std::exception& e) {
            lastError = std::string("Initialization failed: ") + e.what();
            initialized = false;
            return false;
        } catch (...) {
            lastError = "Initialization failed: Unknown error";
            initialized = false;
            return false;
        }
    }
    
public:
    
    EvaluationResult evaluate(const std::string& expression) {
        if (!initialized) {
            return EvaluationResult("REPL not initialized");
        }
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        try {
            // Use the interpreter to parse and execute the code
            if (!interpreter) {
                lastError = "Interpreter not initialized";
                return EvaluationResult("Interpreter not initialized");
            }
            
            // Use parseAndExecute to handle the compilation and execution
            auto error = interpreter->parseAndExecute(expression);
            if (error) {
                std::string errStr = llvm::toString(std::move(error));
                lastError = "Failed to execute: " + errStr;
                stats.total_expressions++;
                stats.failed_compilations++;
                return EvaluationResult("Failed to execute: " + errStr);
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
            
            // Reinitialize with a fresh interpreter
            // This will create a completely new interpreter with no input files
            return initialize();
            
        } catch (const std::exception& e) {
            lastError = e.what();
            return false;
        }
    }
    
    SwiftJITREPL::CompilationStats getStats() const {
        return stats;
    }
};

// Static member initialization
std::mutex SwiftJITREPL::Impl::initMutex;
bool SwiftJITREPL::Impl::llvmInitialized = false;

// SwiftJITREPL implementation
SwiftJITREPL::SwiftJITREPL(const REPLConfig& config) : pImpl(std::make_unique<Impl>(config)) {}

SwiftJITREPL::~SwiftJITREPL() = default;

EvaluationResult SwiftJITREPL::evaluate(const std::string& expression) {
    return pImpl->evaluate(expression);
}

llvm::Expected<SwiftPartialTranslationUnit&> SwiftJITREPL::parse(const std::string& code) {
    if (!pImpl->interpreter) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "Interpreter not initialized");
    }
    
    return pImpl->interpreter->getIncrementalParser()->parse(code);
}

llvm::Error SwiftJITREPL::execute(SwiftPartialTranslationUnit& ptu) {
    if (!pImpl->interpreter) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "Interpreter not initialized");
    }
    
    return pImpl->interpreter->getIncrementalExecutor()->addModule(ptu);
}

llvm::Error SwiftJITREPL::parseAndExecute(const std::string& code) {
    if (!pImpl->interpreter) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "Interpreter not initialized");
    }
    
    return pImpl->interpreter->parseAndExecute(code);
}

llvm::Error SwiftJITREPL::undo(unsigned N) {
    if (!pImpl->interpreter) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "Interpreter not initialized");
    }
    
    return pImpl->interpreter->undo(N);
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

bool SwiftJITREPL::isAvailable() {
    // Check if Swift JIT is available by attempting to create a minimal instance
    try {
        // Initialize LLVM targets FIRST before any CompilerInstance operations (only once)
        initializeLLVMTargetsOnce();
        
        // Create and configure the compiler invocation for JIT mode
        swift::CompilerInvocation invocation;
        
        // Set up language options for JIT mode
        invocation.getLangOptions().Target = llvm::Triple(TARGET_TRIPLE);
        invocation.getLangOptions().EnableObjCInterop = true;
        
        // Set up frontend options for SIL generation (to generate SIL functions)
        invocation.getFrontendOptions().RequestedAction = swift::FrontendOptions::ActionType::EmitSILGen;
        // Enable cycle dumps for early availability check as well
        invocation.getLangOptions().DebugDumpCycles = true;
        
        // Set up command line arguments for immediate mode
        std::vector<std::string> immediateArgs = {"swift", "-i", "-Xfrontend", "-debug-cycles"};
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
        searchPaths.setRuntimeLibraryImportPaths({SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1, SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2, FOUNDATION_MODULE_PATH, FOUNDATION_STATIC_MODULE_PATH, DISPATCH_MODULE_PATH, DISPATCH_STATIC_MODULE_PATH});
        searchPaths.RuntimeResourcePath = SWIFT_RUNTIME_RESOURCE_PATH;
        searchPaths.setSDKPath(SWIFT_SDK_PATH);
        
        // Validate the configuration by creating a temporary CompilerInstance
        auto tempCI = std::make_unique<swift::CompilerInstance>();
        std::string error;
        if (tempCI->setup(invocation, error)) {
            return false; // Setup failed
        }
        // Configuration is valid
        
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace SwiftJITREPL