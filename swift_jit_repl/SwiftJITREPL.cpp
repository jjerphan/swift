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

namespace SwiftJITREPL {

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
            
            // Set up frontend options for JIT mode
            invocation.getFrontendOptions().RequestedAction = swift::FrontendOptions::ActionType::REPL;
            
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
            // For REPL mode, we can use the Swift compiler's built-in immediate mode
            // Create a temporary file with the expression and use RunImmediatelyFromAST
            
            // 1. Create MemoryBuffer from code string
            auto buffer = llvm::MemoryBuffer::getMemBuffer(expression, "repl_input");
            if (!buffer) {
                return EvaluationResult("Failed to create memory buffer");
            }
            
            // 2. Add to SourceManager
            auto &sourceManager = compilerInstance->getSourceMgr();
            unsigned bufferID = sourceManager.addNewSourceBuffer(std::move(buffer));
            
            // 3. Use the Swift compiler's immediate execution
            int result = swift::RunImmediatelyFromAST(*compilerInstance);
            
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
        
        // Set up frontend options for JIT mode
        invocation.getFrontendOptions().RequestedAction = swift::FrontendOptions::ActionType::REPL;
        
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
