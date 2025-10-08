#include "SwiftInterpreter.h"

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

// Forward declaration for the runtime interface builder implementation
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
        
        // Add Swift import to make standard library operators available
        std::string result = "import Swift\n" + code;
        
        llvm::errs() << "[transformForValuePrinting] Generated Swift code:\n" << result << "\n";
        return result;
    }
};

// Global flag to ensure LLVM targets are initialized only once
extern bool g_llvmTargetsInitialized;
extern std::mutex g_llvmInitMutex;
extern void initializeLLVMTargetsOnce();

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

SwiftInterpreter::SwiftInterpreter(swift::CompilerInvocation* invocation) {
    // Store reference to compiler invocation
    compilerInvocation = invocation;
    
    // Validate Swift runtime paths
    validateSwiftRuntimePaths();
    
    // Initialize LLVM targets manually (equivalent to INITIALIZE_LLVM macro) - only once
    initializeLLVMTargetsOnce();
    
    // Verify target registration
    llvm::errs() << "[SwiftInterpreter] Verifying target registration...\n";
    auto targetTriple = llvm::Triple("x86_64-unknown-linux-gnu");
    llvm::errs() << "[SwiftInterpreter] Target triple: " << targetTriple.str() << "\n";
    
    std::string targetError;
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple.str(), targetError);
    if (target) {
        llvm::errs() << "[SwiftInterpreter] Target found: " << target->getName() << "\n";
    } else {
        llvm::errs() << "[SwiftInterpreter] ERROR: Target not found: " << targetError << "\n";
    }
    
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
    
    // Note: Access level override removed due to API compatibility issues
    // The multi-module approach with explicit imports should handle cross-module access
    
    // Create initial empty module for base functionality
    llvm::errs() << "[SwiftInterpreter] Creating initial base module...\n";
    
    // Let the CompilerInstance handle Swift standard library loading
    // The CompilerInstance will automatically load the Swift standard library
    // when we call performSema() or other Swift compiler functions
    llvm::errs() << "[SwiftInterpreter] Swift standard library will be loaded automatically by CompilerInstance\n";
    
    // Skip creating/importing a base module to avoid accidental cycles
    llvm::errs() << "[SwiftInterpreter] Skipping creation of SwiftJITREPL_Base to avoid import cycles\n";
    
    // Create incremental parser with shared ASTContext and modules
    IncrParser = std::make_unique<SwiftIncrementalParser>(sharedASTContext.get(), &modules, TSCtx.get(), compilerInstance.get(), compilerInvocation);
    
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

llvm::Error SwiftInterpreter::undo(unsigned N) {
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
        
        IncrParser->cleanUpPTU(PTUs.back());
        PTUs.pop_back();
    }
    return llvm::Error::success();
}

llvm::Error SwiftInterpreter::parseAndExecute(llvm::StringRef Code) {
    llvm::errs() << "[SwiftInterpreter::ParseAndExecute] Starting execution of: " << Code << "\n";
    
    // Transform the code to wrap it in a main function
    std::string transformedCode = synthesizeExpr(Code.str());
    llvm::errs() << "[SwiftInterpreter::ParseAndExecute] Transformed code: " << transformedCode << "\n";
    
    // Parse the transformed code
    llvm::errs() << "[SwiftInterpreter::ParseAndExecute] About to parse transformed code...\n";
    auto ptuOrError = IncrParser->parse(transformedCode);
    llvm::errs() << "[SwiftInterpreter::ParseAndExecute] PTU or error?\n";
    if (!ptuOrError) {
        llvm::errs() << "[SwiftInterpreter::ParseAndExecute] ERROR: Parse failed\n";
        llvm::Error Err = ptuOrError.takeError();
        llvm::errs() << "[SwiftInterpreter::ParseAndExecute] Parse llvm::Error: "
                     << llvm::toString(std::move(Err)) << "\n";
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "Parse failed");
    }
    llvm::errs() << "[SwiftInterpreter::ParseAndExecute] Parse successful\n";
    
    auto& ptu = *ptuOrError;
    
    // Execute the PTU and handle any errors
    if (auto err = execute(ptu)) {
        llvm::errs() << "[SwiftInterpreter::ParseAndExecute] ERROR: Execute failed: " 
                    << llvm::toString(std::move(err)) << "\n";
        return llvm::Error::success(); // Return success to avoid crash
    }
    
    return llvm::Error::success();
}

llvm::Error SwiftInterpreter::execute(SwiftPartialTranslationUnit& ptu) {
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
