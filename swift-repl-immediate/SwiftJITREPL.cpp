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

// Standard library includes
#include <iostream>
#include <memory>
#include <vector>
#include <mutex>
#include <fstream>
#include <unistd.h>

// LLVM includes
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ExecutionEngine/Orc/ObjectTransformLayer.h"

// Swift compiler includes
#include "swift/Frontend/Frontend.h"
#include "swift/IDETool/CompilerInvocation.h"
#include "swift/Immediate/Immediate.h"
#include "swift/Immediate/SwiftMaterializationUnit.h"
#include "swift/AST/Module.h"
#include "swift/AST/SourceFile.h"
#include "swift/AST/Import.h"
#include "swift/AST/Decl.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/ASTNode.h"
#include "swift/AST/ParseRequests.h"
#include "swift/Parse/Parser.h"
#include "swift/Parse/Lexer.h"
#include "swift/Parse/PersistentParserState.h"
#include "swift/Subsystems.h"
#include "swift/AST/IRGenRequests.h"
#include "swift/SILOptimizer/PassManager/Passes.h"

// LLVM target initialization
#include "llvm/Support/TargetSelect.h"
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
 * Private implementation class (PIMPL idiom)
 *
 * This implementation uses an incremental approach inspired by Clang's Interpreter:
 * 1. Maintains a persistent CompilerInstance and SourceFile
 * 2. Parses top-level statements as TopLevelCodeDecl nodes (no @main wrapper)
 * 3. Incrementally adds new code to the SourceFile buffer
 * 4. Executes only newly parsed TopLevelCodeDecl nodes
 * 5. Preserves state across evaluations
 *
 */
class SwiftJITREPL::Impl {
public:
    Impl(const REPLConfig& config) : config(config) {        
        std::cout << "[SIL JIT] ========================================" << std::endl;
        std::cout << "[SIL JIT] Initializing SwiftJITREPL..." << std::endl;
        std::cout << "[SIL JIT] Configuration:" << std::endl;
        std::cout << "[SIL JIT]   - Enable optimizations: " << (config.enable_optimizations ? "true" : "false") << std::endl;
        std::cout << "[SIL JIT]   - Generate debug info: " << (config.generate_debug_info ? "true" : "false") << std::endl;
        
        // Initialize LLVM targets first
        initializeLLVMTargetsOnce();

        // Create persistent CompilerInstance
        compilerInstance = std::make_unique<swift::CompilerInstance>();
        
        // Configure CompilerInvocation for Immediate mode with script mode
        swift::CompilerInvocation invocation;
        invocation.getLangOptions().Target = llvm::Triple("x86_64-unknown-linux-gnu");
        invocation.getLangOptions().EnableObjCInterop = true;
        invocation.getFrontendOptions().RequestedAction = swift::FrontendOptions::ActionType::Immediate;
        invocation.getFrontendOptions().ModuleName = "SwiftJITREPL";
        
        // Set the correct search paths for Swift standard library
        auto &searchPaths = invocation.getSearchPathOptions();
        searchPaths.RuntimeLibraryPaths = {SWIFT_RUNTIME_LIBRARY_PATHS};
        searchPaths.setRuntimeLibraryImportPaths({SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1, SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2});
        searchPaths.RuntimeResourcePath = SWIFT_RUNTIME_RESOURCE_PATH;
        searchPaths.setSDKPath(SWIFT_SDK_PATH);
        
        // Configure IRGen options for Immediate mode
        auto &irGenOpts = invocation.getIRGenOptions();
        irGenOpts.OutputKind = swift::IRGenOutputKind::ObjectFile;
        irGenOpts.UseJIT = true;
        
        std::string error;
        if (compilerInstance->setup(invocation, error)) {
            std::cerr << "[SIL JIT] ERROR: Failed to setup CompilerInstance: " << error << std::endl;
            initialized = false;
            return;
        }
        
        // Create a persistent SourceFile in script mode (allows top-level code)
        auto &astContext = compilerInstance->getASTContext();
        auto &sourceMgr = astContext.SourceMgr;
        
        // Create an empty buffer that we'll append to incrementally
        std::string emptySource = "";
        auto sourceBuffer = llvm::MemoryBuffer::getMemBufferCopy(emptySource, "<REPL>");
        unsigned bufferID = sourceMgr.addNewSourceBuffer(std::move(sourceBuffer));
        
        // Create SourceFile in Main mode (script mode allows top-level code)
        auto parsingOpts = swift::SourceFile::getDefaultParsingOptions(astContext.LangOpts);
        
        // Create module with a populateFiles function that adds our source file
        auto *module = swift::ModuleDecl::createMainModule(
            astContext,
            astContext.getIdentifier("SwiftJITREPL"),
            swift::ImplicitImportInfo(),
            [&](swift::ModuleDecl *mod, auto addFile) {
                sourceFile = new (astContext) swift::SourceFile(*mod, swift::SourceFileKind::Main, bufferID, parsingOpts);
                addFile(sourceFile);
            });
        
        compilerInstance->setMainModule(module);
        
        // Create persistent parser state
        parserState = std::make_unique<swift::PersistentParserState>();
        
        // Initialize SwiftJIT (persistent across evaluations)
        auto jitResult = swift::SwiftJIT::Create(*compilerInstance);
        if (auto err = jitResult.takeError()) {
            std::cerr << "[SIL JIT] ERROR: Failed to create SwiftJIT" << std::endl;
            initialized = false;
            return;
        }
        swiftJIT = std::move(*jitResult);
        
        initialized = true;
        std::cout << "[SIL JIT] ✓ SwiftJITREPL initialized successfully" << std::endl;
        std::cout << "[SIL JIT] ========================================" << std::endl;
    }

    ~Impl() {
        std::cout << "[SIL JIT] ========================================" << std::endl;
        std::cout << "[SIL JIT] Destroying SwiftJITREPL..." << std::endl;
        std::cout << "[SIL JIT] Cleaning up resources..." << std::endl;
        
        // Clean up JIT
        if (swiftJIT) {
            // Note: SwiftJIT cleanup is complex - see Immediate.cpp comment
            // "It is not safe to unmap memory that has been registered with the swift or objc runtime"
            swiftJIT.reset();
        }
        
        // Parser state will be cleaned up automatically
        parserState.reset();
        
        // CompilerInstance will clean up AST context and modules
        compilerInstance.reset();
        
        std::cout << "[SIL JIT] ✓ SwiftJITREPL destroyed successfully" << std::endl;
        std::cout << "[SIL JIT] ========================================" << std::endl;
    }

public:
    EvaluationResult evaluate(const std::string& expression) {
        if (!initialized) {
            std::cerr << "[SIL JIT] ERROR: REPL not initialized" << std::endl;
            return EvaluationResult("REPL not initialized");
        }

        std::cout << "[SIL JIT] ========================================" << std::endl;
        std::cout << "[SIL JIT] Evaluating: " << expression << std::endl;
        std::cout << "[SIL JIT] ========================================" << std::endl;

        auto &astContext = compilerInstance->getASTContext();
        auto &sourceMgr = astContext.SourceMgr;
        
        // Accumulate source text (we maintain this separately)
        if (!accumulatedSource.empty() && accumulatedSource.back() != '\n') {
            accumulatedSource += "\n";
        }
        accumulatedSource += expression;
        accumulatedSource += "\n";
        
        // Create a new buffer with all accumulated content
        // We parse everything from scratch to avoid source location conflicts
        auto newBuffer = llvm::MemoryBuffer::getMemBufferCopy(accumulatedSource, "<REPL>");
        unsigned newBufferID = sourceMgr.addNewSourceBuffer(std::move(newBuffer));
        
        // Recreate the module and SourceFile with the new buffer
        // This ensures all source locations are consistent
        auto parsingOpts = swift::SourceFile::getDefaultParsingOptions(astContext.LangOpts);
        auto *module = swift::ModuleDecl::createMainModule(
            astContext,
            astContext.getIdentifier("SwiftJITREPL"),
            swift::ImplicitImportInfo(),
            [&](swift::ModuleDecl *mod, auto addFile) {
                sourceFile = new (astContext) swift::SourceFile(*mod, swift::SourceFileKind::Main, newBufferID, parsingOpts);
                // Clear any existing scope to avoid conflicts
                sourceFile->clearScope();
                addFile(sourceFile);
            });
        
        // Update the CompilerInstance to use the new module
        compilerInstance->setMainModule(module);
        
        // Perform semantic analysis; this will parse and type-check the file
        std::cout << "[SIL JIT] Performing semantic analysis..." << std::endl;
        compilerInstance->performSema();
        if (astContext.hadError() || compilerInstance->getDiags().hadAnyError()) {
            std::string errorMsg = "Semantic analysis error";
            lastError = errorMsg;
            astContext.Diags.resetHadAnyError();
            return EvaluationResult(errorMsg);
        }

        // Generate and print SIL for the current module
        std::cout << "[SIL JIT] ========================================" << std::endl;
        std::cout << "[SIL JIT] Generating SILModule..." << std::endl;
        auto typeConverter = std::make_unique<swift::Lowering::TypeConverter>(*module);
        auto silModule = swift::performASTLowering(module, *typeConverter, compilerInstance->getInvocation().getSILOptions());
        if (silModule) {
            std::cout << "[SIL JIT] ========================================" << std::endl;
            std::cout << "[SIL JIT] Generated SILModule:" << std::endl;
            std::cout << "[SIL JIT] ========================================" << std::endl;
            auto silOpts = compilerInstance->getInvocation().getSILOptions();
            swift::SILPrintContext printCtx(llvm::outs(), silOpts);
            silModule->print(printCtx, module, /*PrintASTDecls=*/false);
            std::cout << "[SIL JIT] ========================================" << std::endl;

            // Eagerly lower SIL to LLVM IR and add the resulting object to the JIT
            // so that the synthesized entry (main$impl) is available.
            silModule->promoteLinkages();
            swift::runSILDiagnosticPasses(*silModule);
            swift::runSILLoweringPasses(*silModule);

            const auto &Invocation = compilerInstance->getInvocation();
            const auto &IRGenOpts = Invocation.getIRGenOptions();
            const auto &TBDOpts = Invocation.getTBDGenOptions();
            const auto PSPs = compilerInstance->getPrimarySpecificPathsForAtMostOnePrimary();

            auto GenModule = swift::performIRGeneration(
                module, IRGenOpts, TBDOpts, std::move(silModule),
                module->getName().str(), PSPs, {});

            auto *LLVMMod = GenModule.getModule();
            auto *TM = GenModule.getTargetMachine();

            llvm::SmallVector<char, 0> ObjBuffer;
            llvm::raw_svector_ostream OS(ObjBuffer);
            bool error = swift::compileAndWriteLLVM(
                LLVMMod, TM, IRGenOpts, /*stats*/ nullptr,
                compilerInstance->getASTContext().Diags, OS);
            if (!error && swiftJIT) {
                auto MB = llvm::MemoryBuffer::getMemBufferCopy(
                    llvm::StringRef(ObjBuffer.data(), ObjBuffer.size()),
                    "SwiftJITREPL-jitted-object");
                if (auto Err = swiftJIT->getObjTransformLayer().add(
                        swiftJIT->getMainJITDylib(), std::move(MB))) {
                    llvm::errs() << "JIT object add error: ";
                    llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "");
                } else {
                    std::cout << "[SIL JIT] Eagerly added object to JIT" << std::endl;
                }
            } else {
                std::cerr << "[SIL JIT] Failed to emit object for JIT" << std::endl;
            }
        }

        std::cout << "[SIL JIT] ========================================" << std::endl;
        std::cout << "[SIL JIT] Evaluation completed!" << std::endl;
        std::cout << "[SIL JIT] ========================================" << std::endl;
        
        return EvaluationResult(0);
    }

    int executeAll() {
        if (!swiftJIT) return -1;
        auto Res = swiftJIT->runMain({});
        if (!Res)
            return -1;
        return *Res;
    }

    bool reset() {
        if (!initialized) {
            std::cerr << "[SIL JIT] ERROR: Cannot reset - REPL not initialized" << std::endl;
            return false;
        }
        
        std::cout << "[SIL JIT] ========================================" << std::endl;
        std::cout << "[SIL JIT] Resetting REPL context..." << std::endl;
        
        // Clear accumulated source
        accumulatedSource.clear();
        
        // Clear the source file's top-level declarations
        // Note: getTopLevelDecls() returns a const reference, so we need to
        // work around this by recreating the source file or using internal APIs
        // For now, we'll track that we need to start fresh on next evaluation
        
        // Reset parser state
        parserState = std::make_unique<swift::PersistentParserState>();
        
        // Clear error state
        auto &astContext = compilerInstance->getASTContext();
        astContext.Diags.resetHadAnyError();
        lastError.clear();
        
        // Recreate source file with empty buffer
        auto &sourceMgr = astContext.SourceMgr;
        auto emptyBuffer = llvm::MemoryBuffer::getMemBufferCopy("", "<REPL>");
        unsigned newBufferID = sourceMgr.addNewSourceBuffer(std::move(emptyBuffer));
        auto parsingOpts = swift::SourceFile::getDefaultParsingOptions(astContext.LangOpts);
        
        // Recreate module with empty source file
        auto *module = swift::ModuleDecl::createMainModule(
            astContext,
            astContext.getIdentifier("SwiftJITREPL"),
            swift::ImplicitImportInfo(),
            [&](swift::ModuleDecl *mod, auto addFile) {
                sourceFile = new (astContext) swift::SourceFile(*mod, swift::SourceFileKind::Main, newBufferID, parsingOpts);
                addFile(sourceFile);
            });
        compilerInstance->setMainModule(module);
        
        std::cout << "[SIL JIT] ✓ REPL context reset successfully" << std::endl;
        std::cout << "[SIL JIT] ========================================" << std::endl;
        return true;
    }

    std::string getLastError() const {
        return lastError;
    }

private:
    REPLConfig config;
    bool initialized = false;
    std::string lastError;
    
    // Persistent compiler infrastructure
    std::unique_ptr<swift::CompilerInstance> compilerInstance;
    swift::SourceFile *sourceFile = nullptr;
    std::unique_ptr<swift::PersistentParserState> parserState;
    std::unique_ptr<swift::SwiftJIT> swiftJIT;
    
    // Accumulated source text across evaluations
    std::string accumulatedSource;
};

SwiftJITREPL::SwiftJITREPL(const REPLConfig& config) : pImpl(std::make_unique<Impl>(config)) {}
SwiftJITREPL::~SwiftJITREPL() = default;

EvaluationResult SwiftJITREPL::evaluate(const std::string& expression) {
    return pImpl->evaluate(expression);
}

bool SwiftJITREPL::reset() {
    return pImpl->reset();
}

std::string SwiftJITREPL::getLastError() const {
    return pImpl->getLastError();
}

int SwiftJITREPL::executeAll() {
    if (!pImpl) return -1;
    return pImpl->executeAll();
}

bool SwiftJITREPL::isAvailable() {
    try {
        // For this demonstration, we'll always return true
        // In a real implementation, you would test if Swift compiler
        // infrastructure is available and properly configured
        
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace SwiftJITREPL