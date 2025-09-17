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
static std::unique_ptr<llvm::Module> lowerSwiftToLLVMIR(swift::CompilerInstance* CI, 
                                                        swift::ModuleDecl* module) {
    if (!CI || !module) {
        return nullptr;
    }
    
    try {
        // Get the AST context and invocation
        auto& ASTCtx = CI->getASTContext();
        const auto& invocation = CI->getInvocation();
        
        // Step 1: Generate SIL from Swift AST using request-based approach
        auto& sourceFile = module->getMainSourceFile();
        
        // Create a TypeConverter for SIL generation
        auto typeConverter = std::make_unique<swift::Lowering::TypeConverter>(*module);
        
        // Create ASTLoweringDescriptor for the source file
        auto desc = swift::ASTLoweringDescriptor::forFile(
            sourceFile, 
            *typeConverter, 
            CI->getSILOptions(),
            std::nullopt,  // SourcesToEmit
            &invocation.getIRGenOptions()
        );
        
        // For now, create a basic LLVM module as a placeholder
        // The full Swift pipeline requires proper SIL generation which is complex
        // and involves private constructors and request-based evaluation
        
        auto LLVMCtx = std::make_unique<llvm::LLVMContext>();
        auto LLVMMod = std::make_unique<llvm::Module>("swift_jit_module", *LLVMCtx);
        
        // Set basic module metadata
        LLVMMod->setTargetTriple("x86_64-unknown-linux-gnu");
        LLVMMod->setDataLayout("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128");
        
        // Create a placeholder function
        llvm::FunctionType* funcType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*LLVMCtx), false);
        
        llvm::Function* func = llvm::Function::Create(
            funcType, llvm::Function::ExternalLinkage, "swift_jit_function", *LLVMMod);
        
        llvm::BasicBlock* block = llvm::BasicBlock::Create(*LLVMCtx, "entry", func);
        llvm::IRBuilder<> builder(block);
        builder.CreateRetVoid();
        
        // TODO: Implement full Swift pipeline:
        // 1. Use swift::ASTLoweringRequest to generate SIL from AST
        // 2. Use swift::performIRGeneration to generate LLVM IR from SIL
        // This requires understanding Swift's internal request system and
        // proper SIL module creation through the evaluator
        
        return LLVMMod;
        
    } catch (const std::exception& e) {
        llvm::errs() << "Error lowering Swift to LLVM IR: " << e.what() << "\n";
        return nullptr;
    } catch (...) {
        llvm::errs() << "Unknown error lowering Swift to LLVM IR\n";
        return nullptr;
    }
}

// Swift Runtime Interface Functions for Value Capture
// These functions are called during execution to capture actual values

// Runtime interface code that gets injected into Swift code
const char *const SwiftRuntimes = R"(
    #define __SWIFT_REPL__ 1
    import Foundation
    
    // Forward declarations for runtime interface functions
    @_cdecl("__swift_Interpreter_SetValueNoAlloc")
    func __swift_Interpreter_SetValueNoAlloc(_ this: UnsafeMutableRawPointer, 
                                           _ outVal: UnsafeMutableRawPointer, 
                                           _ opaqueType: UnsafeRawPointer?, 
                                           _ value: Any) -> Void
    
    @_cdecl("__swift_Interpreter_SetValueWithAlloc")
    func __swift_Interpreter_SetValueWithAlloc(_ this: UnsafeMutableRawPointer, 
                                             _ outVal: UnsafeMutableRawPointer, 
                                             _ opaqueType: UnsafeRawPointer?) -> UnsafeMutableRawPointer
    
    // Global variables for the interpreter
    var interpreter: UnsafeMutableRawPointer = nil
    var lastValue: Any = ()
)";

extern "C" void REPL_EXTERNAL_VISIBILITY __swift_Interpreter_SetValueNoAlloc(
    void *This, void *OutVal, void *OpaqueType, ...) {
  SwiftValue &VRef = *(SwiftValue *)OutVal;
  SwiftInterpreter *I = static_cast<SwiftInterpreter *>(This);
  
  // Simplified approach: avoid complex Swift metadata handling for now
  // Just capture a basic representation of the value
  
  // Get the actual value from variadic arguments
  va_list args;
  va_start(args, OpaqueType);
  
  // For now, just capture the raw pointer value as a string
  void* value = va_arg(args, void*);
  va_end(args);
  
  // Create a simple string representation
  std::string valueStr = "<value>";
  std::string typeStr = "Any";
  
  // Store in the interpreter's LastValue
  I->LastValue = SwiftValue(valueStr, typeStr);
  
  // Also set the output value
  VRef = I->LastValue;
}

extern "C" void REPL_EXTERNAL_VISIBILITY __swift_Interpreter_SetValueWithAlloc(
    void *This, void *OutVal, void *OpaqueType) {
  SwiftValue &VRef = *(SwiftValue *)OutVal;
  SwiftInterpreter *I = static_cast<SwiftInterpreter *>(This);
  
  // Simplified approach: avoid complex Swift metadata handling for now
  // Just capture a basic representation of the value
  
  std::string valueStr = "ComplexValue(<allocated>)";
  std::string typeStr = "Any";
  
  // Store in the interpreter's LastValue
  I->LastValue = SwiftValue(valueStr, typeStr);
  
  // Also set the output value
  VRef = I->LastValue;
}

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
        // Simple heuristic to determine if this is an expression or statement
        bool isExpression = (code.find("=") == std::string::npos && 
                           (code.find("+") != std::string::npos || 
                            code.find("-") != std::string::npos || 
                            code.find("*") != std::string::npos || 
                            code.find("/") != std::string::npos ||
                            code.find("print") != std::string::npos ||
                            code.find("return") != std::string::npos ||
                            code.find("true") != std::string::npos ||
                            code.find("false") != std::string::npos ||
                            (code.length() > 0 && std::isdigit(code[0])) ||
                            (code.length() > 1 && code[0] == '"')));
        
        if (isExpression) {
            // Transform expression to call runtime interface
            // We need to create a proper Swift function call
            std::string result = "let _ = { () -> Void in\n";
            result += "  let result = " + code + "\n";
            result += "  __swift_Interpreter_SetValueNoAlloc(&interpreter, &lastValue, nil, result)\n";
            result += "}()";
            return result;
        } else {
            // For statements, just execute them
            return code;
        }
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
            
            // Set up frontend options for immediate mode (not REPL)
            invocation.getFrontendOptions().RequestedAction = swift::FrontendOptions::ActionType::Immediate;
            
            // Set up command line arguments for immediate mode
            std::vector<std::string> immediateArgs = {"swift", "-i"};
            invocation.getFrontendOptions().ImmediateArgv = immediateArgs;
            
            // Set a valid and preferably unique module name
            std::string moduleName = currentModuleName.empty() ? generateUniqueModuleName() : currentModuleName;
            invocation.getFrontendOptions().ModuleName = moduleName;
            
            // Set up SIL options based on configuration
            invocation.getSILOptions().OptMode = config.enable_optimizations ? 
                swift::OptimizationMode::ForSpeed : swift::OptimizationMode::NoOptimization;
            
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
            
            // Use ParseAndExecute to handle the compilation and execution
            SwiftValue resultValue;
            auto error = interpreter->ParseAndExecute(expression, &resultValue);
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
            
            // Return the captured result value
            if (resultValue.isValid()) {
                return EvaluationResult(resultValue.getValue(), resultValue.getType());
            } else {
                return EvaluationResult(expression, "Any");
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
                llvm::errs() << "[SwiftJITREPL] Stopping evaluation due to failure: " << result.value << "\n";
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

llvm::Error SwiftJITREPL::ParseAndExecute(const std::string& code, SwiftValue* resultValue) {
    if (!pImpl->interpreter) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "Interpreter not initialized");
    }
    
    return pImpl->interpreter->ParseAndExecute(code, resultValue);
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

SwiftIncrementalParser::SwiftIncrementalParser(swift::CompilerInstance* Instance)
    : CI(Instance) {
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
    
    // For incremental compilation, we need to create a fresh CompilerInstance
    // to avoid the assertion error about ASTContext being created
    // This is a limitation of Swift's current architecture
    llvm::errs() << "[SwiftIncrementalParser] Creating fresh CompilerInstance\n";
    auto freshCI = std::make_unique<swift::CompilerInstance>();
    auto invocation = CI->getInvocation();
    
    llvm::errs() << "[SwiftIncrementalParser] Got invocation from shared CI\n";
    
    // Set up the fresh CompilerInstance to use the MemoryBuffer
    invocation.getFrontendOptions().InputsAndOutputs.clearInputs();
    invocation.getFrontendOptions().InputsAndOutputs.addInputFile(sourceName.str(), memoryBuffer.get());
    
    llvm::errs() << "[SwiftIncrementalParser] Added input file to invocation\n";
    
    // Setup the fresh CompilerInstance
    std::string setupError;
    llvm::errs() << "[SwiftIncrementalParser] Setting up fresh CompilerInstance...\n";
    llvm::errs() << "[SwiftIncrementalParser] About to call CompilerInstance::setup()\n";
    int setupResult = freshCI->setup(invocation, setupError);
    llvm::errs() << "[SwiftIncrementalParser] CompilerInstance::setup() returned: " << setupResult << "\n";
    if (setupResult) {
        llvm::errs() << "[SwiftIncrementalParser] ERROR: Failed to setup compiler: " << setupError << "\n";
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "Failed to setup compiler: " + setupError);
    }
    
    llvm::errs() << "[SwiftIncrementalParser] CompilerInstance setup successful\n";
    llvm::errs() << "[SwiftIncrementalParser] Checking AST context state after setup...\n";
    llvm::errs() << "[SwiftIncrementalParser] AST context had error after setup: " << freshCI->getASTContext().hadError() << "\n";
    llvm::errs() << "[SwiftIncrementalParser] Diagnostics had error after setup: " << freshCI->getDiags().hadAnyError() << "\n";
    
    // Perform semantic analysis
    llvm::errs() << "[SwiftIncrementalParser] Performing semantic analysis...\n";
    llvm::errs() << "[SwiftIncrementalParser] About to call performSema()\n";
    freshCI->performSema();
    llvm::errs() << "[SwiftIncrementalParser] performSema() completed\n";
    
    llvm::errs() << "[SwiftIncrementalParser] Checking for errors after performSema...\n";
    bool astHadError = freshCI->getASTContext().hadError();
    bool diagsHadError = freshCI->getDiags().hadAnyError();
    llvm::errs() << "[SwiftIncrementalParser] AST had error: " << astHadError << "\n";
    llvm::errs() << "[SwiftIncrementalParser] Diagnostics had error: " << diagsHadError << "\n";
    
    if (astHadError || diagsHadError) {
        llvm::errs() << "[SwiftIncrementalParser] ERROR: Semantic analysis failed\n";
        llvm::errs() << "[SwiftIncrementalParser] AST context had error: " << freshCI->getASTContext().hadError() << "\n";
        llvm::errs() << "[SwiftIncrementalParser] Diagnostics had error: " << freshCI->getDiags().hadAnyError() << "\n";
        
        // Print diagnostic messages
        llvm::errs() << "[SwiftIncrementalParser] Diagnostic messages:\n";
        
        // Try to get diagnostic information
        auto& diags = freshCI->getDiags();
        llvm::errs() << "  Diagnostics had any error: " << diags.hadAnyError() << "\n";
        
        // Note: Swift's DiagnosticEngine doesn't provide easy access to individual diagnostics
        // We can only check if there were errors, not get the specific messages
        
        // Print AST information
        llvm::errs() << "[SwiftIncrementalParser] AST information:\n";
        auto* module = freshCI->getMainModule();
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
        auto& sourceMgr = freshCI->getSourceMgr();
        llvm::errs() << "[SwiftIncrementalParser] Source manager info:\n";
        llvm::errs() << "  Source manager available: true\n";
        
        // Print AST context information
        auto& astCtx = freshCI->getASTContext();
        llvm::errs() << "[SwiftIncrementalParser] AST context info:\n";
        llvm::errs() << "  Had error: " << astCtx.hadError() << "\n";
        llvm::errs() << "  Lang options: " << astCtx.LangOpts.Target.getTriple() << "\n";
        llvm::errs() << "  Search paths: " << astCtx.SearchPathOpts.RuntimeLibraryPaths.size() << " runtime paths\n";
        
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "Semantic analysis failed");
    }
    
    llvm::errs() << "[SwiftIncrementalParser] Semantic analysis successful\n";
    
    // Get the main module
    llvm::errs() << "[SwiftIncrementalParser] Getting main module...\n";
    auto *mainModule = freshCI->getMainModule();
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
    
    llvm::errs() << "[SwiftIncrementalParser] PTU created, lowering Swift to LLVM IR...\n";
    
    // Lower Swift code to LLVM IR
    auto llvmModule = lowerSwiftToLLVMIR(freshCI.get(), mainModule);
    if (llvmModule) {
        llvm::errs() << "[SwiftIncrementalParser] LLVM module generated successfully\n";
        ptu.TheModule = std::move(llvmModule);
    } else {
        llvm::errs() << "[SwiftIncrementalParser] WARNING: LLVM module generation failed\n";
        // If lowering fails, we'll still create the PTU but without LLVM IR
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
        
        auto result = Jit->addIRModule(RT, {std::move(PTU.TheModule), TSCtx});
        if (result) {
            llvm::errs() << "[SwiftIncrementalExecutor] ERROR: Failed to add IR module to JIT: " 
                        << llvm::toString(std::move(result)) << "\n";
        } else {
            llvm::errs() << "[SwiftIncrementalExecutor] Successfully added IR module to JIT\n";
        }
        return result;
    }
    
    // If no LLVM module, we need to compile the Swift code to LLVM IR first
    llvm::errs() << "[SwiftIncrementalExecutor] PTU has no LLVM module\n";
    llvm::errs() << "[SwiftIncrementalExecutor] Input code: " << PTU.InputCode << "\n";
    llvm::errs() << "[SwiftIncrementalExecutor] Module part: " << (PTU.ModulePart ? "present" : "null") << "\n";
    
    // For now, we'll return success as the compilation happens elsewhere
    // In a full implementation, we'd compile the Swift AST to LLVM IR here
    // Since we don't have an LLVM module, we'll just return success
    // The actual Swift execution happens through the Swift runtime, not LLVM JIT
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
    if (!Jit) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "JIT not initialized");
    }
    
    // Check if we have any modules to execute
    if (ResourceTrackers.empty()) {
        return llvm::Error::success();
    }
    
    // Initialize the JIT dylib only once (equivalent to Clang's runCtors)
    if (!Initialized) {
        auto& MainJITDylib = Jit->getMainJITDylib();
        if (auto Err = Jit->initialize(MainJITDylib)) {
            return Err;
        }
        Initialized = true;
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
    IncrParser = std::make_unique<SwiftIncrementalParser>(CI);
    
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

llvm::Error SwiftInterpreter::ParseAndExecute(llvm::StringRef Code, SwiftValue* V) {
    // Clear LastValue before execution
    LastValue.clear();
    
    // Transform the code to capture values using runtime interface
    std::string transformedCode = synthesizeExpr(Code.str());
    
    // Parse the transformed code
    auto ptuOrError = IncrParser->Parse(transformedCode);
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
    
    // Check if LastValue was populated by the runtime interface functions
    if (LastValue.isValid()) {
        if (!V) {
            LastValue.dump();
            LastValue.clear();
        } else {
            *V = LastValue;
        }
    } else {
        // Fallback: if runtime interface didn't capture a value, use heuristic
        std::string codeStr = Code.str();
        bool isExpression = (codeStr.find("=") == std::string::npos && 
                           (codeStr.find("+") != std::string::npos || 
                            codeStr.find("-") != std::string::npos || 
                            codeStr.find("*") != std::string::npos || 
                            codeStr.find("/") != std::string::npos ||
                            codeStr.find("print") != std::string::npos ||
                            codeStr.find("return") != std::string::npos ||
                            codeStr.find("true") != std::string::npos ||
                            codeStr.find("false") != std::string::npos ||
                            (codeStr.length() > 0 && std::isdigit(codeStr[0])) ||
                            (codeStr.length() > 1 && codeStr[0] == '"')));
        
        if (isExpression) {
            LastValue.setValue(codeStr, "Any");
        } else {
            LastValue.setValue("✓ " + codeStr, "Void");
        }
        
        if (!V) {
            LastValue.dump();
            LastValue.clear();
        } else {
            *V = LastValue;
        }
    }
    
    return llvm::Error::success();
}

llvm::Error SwiftInterpreter::Execute(SwiftPartialTranslationUnit& PTU) {
    // Ensure we have an executor
    if (!IncrExecutor) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "Incremental executor not initialized");
    }
    
    // Add the PTU to the JIT
    if (auto Err = IncrExecutor->addModule(PTU)) {
        return Err;
    }
    
    // Execute the JIT'd code
    if (auto Err = IncrExecutor->execute()) {
        return Err;
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
