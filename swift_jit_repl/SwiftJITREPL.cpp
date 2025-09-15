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

// Basic LLVM includes only
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/TargetSelect.h"

// Swift compiler includes (minimal set)
#include "swift/Frontend/Frontend.h"
#include "swift/Immediate/Immediate.h"
#include "swift/Parse/Lexer.h"

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
            std::cout << "DEBUG: initialize - Setting module name to: '" << moduleName << "'" << std::endl;
            std::cout << "DEBUG: initialize - Is valid identifier: " << (isValidSwiftIdentifier(moduleName) ? "true" : "false") << std::endl;
            invocation.getFrontendOptions().ModuleName = moduleName;
            
            // Set up SIL options
            invocation.getSILOptions().OptMode = swift::OptimizationMode::NoOptimization;
            
            // Set up IRGen options
            invocation.getIRGenOptions().OutputKind = swift::IRGenOutputKind::Module;
            
            // Set up the compiler instance
            std::string error;
            if (!compilerInstance->setup(invocation, error)) {
                lastError = "Failed to setup Swift compiler instance: " + error;
                return false;
            }
            
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
            // Use Swift's immediate mode with a temporary file approach
            // This ensures the source gets properly parsed into a SourceFile
            std::string tempFileName = "/tmp/swift_repl_" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) + ".swift";
            
            // Write expression to temporary file
            std::ofstream tempFile(tempFileName);
            if (!tempFile.is_open()) {
                return EvaluationResult("Failed to create temporary file");
            }
            tempFile << expression;
            tempFile.close();
            
            // Create a new CompilerInstance for this evaluation
            auto tempCompiler = std::make_unique<swift::CompilerInstance>();
            
            // Copy the working configuration from the main compiler instance
            auto invocation = compilerInstance->getInvocation();
            invocation.getFrontendOptions().InputsAndOutputs.addInputFile(tempFileName);
            
            // Debug: Print search paths
            auto &searchPaths = invocation.getSearchPathOptions();
            std::cout << "DEBUG: RuntimeLibraryPaths: ";
            for (const auto &path : searchPaths.RuntimeLibraryPaths) {
                std::cout << path << " ";
            }
            std::cout << std::endl;
            std::cout << "DEBUG: RuntimeLibraryImportPaths: ";
            for (const auto &path : searchPaths.getRuntimeLibraryImportPaths()) {
                std::cout << path << " ";
            }
            std::cout << std::endl;
            std::cout << "DEBUG: RuntimeResourcePath: " << searchPaths.RuntimeResourcePath << std::endl;
            std::cout << "DEBUG: SDKPath: " << searchPaths.getSDKPath().str() << std::endl;
            
            // Set up the compiler instance
            std::string error;
            std::cout << "DEBUG: About to call tempCompiler->setup()" << std::endl;
            if (!tempCompiler->setup(invocation, error)) {
                std::cout << "DEBUG: tempCompiler->setup() failed with error: '" << error << "'" << std::endl;
                std::remove(tempFileName.c_str());
                return EvaluationResult("Failed to setup Swift compiler instance: " + error);
            }
            std::cout << "DEBUG: tempCompiler->setup() succeeded" << std::endl;
            
            // Use the Swift compiler's immediate execution
            std::cout << "DEBUG: About to call RunImmediatelyFromAST with file: " << tempFileName << std::endl;
            int result = swift::RunImmediatelyFromAST(*tempCompiler);
            
            // Clean up temporary file
            std::remove(tempFileName.c_str());
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            
            // Update statistics
            stats.total_expressions++;
            if (result == 0) {
                stats.successful_compilations++;
            } else {
                stats.failed_compilations++;
            }
            stats.total_execution_time_ms += duration.count() / 1000.0;
            
            if (result == 0) {
                return EvaluationResult("Successfully executed Swift code: " + expression, "Any");
            } else {
                return EvaluationResult("Execution failed with exit code: " + std::to_string(result));
            }
            
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
        std::cout << "DEBUG: isSwiftJITAvailable - Setting module name to: '" << moduleName << "'" << std::endl;
        std::cout << "DEBUG: isSwiftJITAvailable - Is valid identifier: " << (isValidSwiftIdentifier(moduleName) ? "true" : "false") << std::endl;
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

} // namespace SwiftJITREPL
