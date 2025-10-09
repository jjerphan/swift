#include "SwiftIncrementalParser.h"

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
// Prefer bitcode round-trip over textual IR parsing for safer context transfer
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"

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

// Forward declaration - function is defined in SwiftJITREPL.cpp
extern void initializeLLVMTargetsOnce();

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
                                                                llvm::LLVMContext* llvmCtx,
                                                                llvm::StringRef entrySuffix) {
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

    // Serialize to bitcode and parse back into our shared LLVM context
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Serializing LLVM module to bitcode...\n";
    llvm::SmallVector<char, 0> bitcodeBuffer;
    {
        llvm::raw_svector_ostream os(bitcodeBuffer);
        llvm::WriteBitcodeToFile(*Produced, os);
    }
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Bitcode size: " << bitcodeBuffer.size() << " bytes\n";

    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Parsing bitcode back into shared LLVM context...\n";
    auto memBuffer = llvm::MemoryBuffer::getMemBufferCopy(
        llvm::StringRef(bitcodeBuffer.data(), bitcodeBuffer.size()),
        Produced->getName());
    auto modOrErr = llvm::parseBitcodeFile(memBuffer->getMemBufferRef(), *llvmCtx);
    if (!modOrErr) {
        llvm::errs() << "[lowerSwiftCodeToLLVMModule] ERROR parsing bitcode: "
                     << llvm::toString(modOrErr.takeError()) << "\n";
        return nullptr;
    }
    std::unique_ptr<llvm::Module> Parsed = std::move(*modOrErr);
    
    // Rename the entry function 'main' to a unique per-module symbol to avoid duplicates
    if (llvm::Function *MainF = Parsed->getFunction("main")) {
        std::string uniqueEntry = (llvm::Twine("swift_jit_main_") + entrySuffix).str();
        MainF->setName(uniqueEntry);
        llvm::errs() << "[lowerSwiftCodeToLLVMModule] Renamed entry function to: " << uniqueEntry << "\n";
    }
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Successfully parsed bitcode into shared context\n";
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Parsed module name: " << Parsed->getName().str() << "\n";
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Parsed module data layout: " << Parsed->getDataLayoutStr() << "\n";

    return Parsed;
}

SwiftIncrementalParser::SwiftIncrementalParser(swift::ASTContext* sharedASTContext, 
                                               std::vector<swift::ModuleDecl*>* modules,
                                               llvm::orc::ThreadSafeContext* TSCtx,
                                               swift::CompilerInstance* sharedCompilerInstance,
                                               swift::CompilerInvocation* compilerInvocation)
    : sharedASTContext(sharedASTContext), modules(modules), TSCtx(TSCtx), sharedCompilerInstance(sharedCompilerInstance), compilerInvocation(compilerInvocation) {
}

SwiftIncrementalParser::~SwiftIncrementalParser() {
    // Clean up all PTUs
    for (auto& ptu : PTUs) {
        cleanUpPTU(ptu);
    }
}

llvm::Expected<SwiftPartialTranslationUnit&> SwiftIncrementalParser::parse(llvm::StringRef Input) {
    llvm::errs() << "[SwiftIncrementalParser] Parse called with input: " << Input << "\n";
    
    // Accumulate all previous code plus the new input into a single module
    std::string accumulatedCode = accumulateAllCode(Input.str());
    llvm::errs() << "[SwiftIncrementalParser] Accumulated code:\n" << accumulatedCode << "\n";
    
    // Reset ASTContext error state to avoid conflicts from previous evaluations
    // This is necessary because we're using a shared ASTContext across evaluations
    sharedASTContext->Diags.resetHadAnyError();
    
    // Create a single module with all accumulated code
    std::string moduleName = "SwiftJITREPL_Accumulated";
    swift::ImplicitImportInfo importInfo;
    llvm::errs() << "[SwiftIncrementalParser] Creating accumulated module: " << moduleName << "\n";
    
    auto accumulatedModule = swift::ModuleDecl::createMainModule(
        *sharedASTContext,
        sharedASTContext->getIdentifier(moduleName),
        importInfo,
        [&](swift::ModuleDecl* module, auto addFile) {
            // Create a MemoryBuffer for the accumulated code
            std::ostringstream sourceName;
            sourceName << "swift_repl_accumulated_" << InputCount++;
            llvm::errs() << "[SwiftIncrementalParser] Source name: " << sourceName.str() << "\n";
            auto inputBuffer = llvm::MemoryBuffer::getMemBufferCopy(accumulatedCode, sourceName.str());
            auto &sourceMgr = sharedASTContext->SourceMgr;
            unsigned bufferID = sourceMgr.addNewSourceBuffer(std::move(inputBuffer));
            llvm::errs() << "[SwiftIncrementalParser] Added source buffer with ID: " << bufferID << "\n";
            addFile(new (*sharedASTContext) swift::SourceFile(
                *module,
                swift::SourceFileKind::Main,
                bufferID,
                swift::SourceFile::getDefaultParsingOptions(sharedASTContext->LangOpts)
            ));
            llvm::errs() << "[SwiftIncrementalParser] Created SourceFile for buffer " << bufferID << "\n";
        }
    );
    llvm::errs() << "[SwiftIncrementalParser] ModuleDecl::create completed successfully\n";
    sharedASTContext->addLoadedModule(accumulatedModule);
    modules->push_back(accumulatedModule);
    llvm::errs() << "[SwiftIncrementalParser] Accumulated module created and registered: " << accumulatedModule->getName() << "\n";
    
    // Perform semantic analysis on the new module using the shared CompilerInstance
    llvm::errs() << "[SwiftIncrementalParser] Performing semantic analysis...\n";
    llvm::errs() << "[SwiftIncrementalParser] Using shared CompilerInstance and ASTContext\n";
    auto *ci = sharedCompilerInstance;
    if (!ci) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "Shared CompilerInstance not initialized");
    }
    // Configure the shared CompilerInstance's invocation (using a non-const ref).
    // This is the supported way to adjust options post-setup in-process.
    auto &inv = const_cast<swift::CompilerInvocation&>(ci->getInvocation());
    inv.getFrontendOptions().ModuleName = moduleName;
    inv.getFrontendOptions().RequestedAction = swift::FrontendOptions::ActionType::Typecheck;
    inv.getLangOptions().DebugDumpCycles = true;
    {
        auto &argv = inv.getFrontendOptions().ImmediateArgv;
        // Avoid duplicating flags across evaluations
        bool hasDebugCycles = false;
        for (size_t i = 0; i + 1 < argv.size(); ++i) {
            if (argv[i] == "-Xfrontend" && argv[i+1] == "-debug-cycles") { hasDebugCycles = true; break; }
        }
        if (!hasDebugCycles) {
            argv.push_back("-Xfrontend");
            argv.push_back("-debug-cycles");
        }
    }
    llvm::errs() << "[SwiftIncrementalParser] Shared CI ASTContext: " << &ci->getASTContext() << "\n";
    llvm::errs() << "[SwiftIncrementalParser] Shared CI SourceManager: " << &ci->getSourceMgr() << "\n";
    llvm::errs() << "[SwiftIncrementalParser] Shared CI DiagnosticEngine: " << &ci->getDiags() << "\n";
    // Point the shared CI to the accumulated main module (same ASTContext)
    ci->setMainModule(accumulatedModule);
    
    // Add detailed logging for import resolution debugging
    llvm::errs() << "[SwiftIncrementalParser] About to call performSema() - this will trigger import resolution\n";
    llvm::errs() << "[SwiftIncrementalParser] Module name: " << accumulatedModule->getName() << "\n";
    llvm::errs() << "[SwiftIncrementalParser] Module is main module: " << accumulatedModule->isMainModule() << "\n";
    llvm::errs() << "[SwiftIncrementalParser] Module files count: " << accumulatedModule->getFiles().size() << "\n";
    
    // Log the ASTContext state
    llvm::errs() << "[SwiftIncrementalParser] ASTContext had error before performSema: " << sharedASTContext->hadError() << "\n";
    llvm::errs() << "[SwiftIncrementalParser] ASTContext Diags had error before performSema: " << sharedASTContext->Diags.hadAnyError() << "\n";
    
    // Perform semantic analysis without attaching additional diagnostic consumers.
    
    // Log search paths
    auto &searchPaths2 = ci->getInvocation().getSearchPathOptions();
    llvm::errs() << "[SwiftIncrementalParser] Runtime library paths count: " << searchPaths2.RuntimeLibraryPaths.size() << "\n";
    for (size_t i = 0; i < searchPaths2.RuntimeLibraryPaths.size(); ++i) {
        llvm::errs() << "[SwiftIncrementalParser]   Runtime path " << i << ": " << searchPaths2.RuntimeLibraryPaths[i] << "\n";
    }
    llvm::errs() << "[SwiftIncrementalParser] Runtime resource path: " << searchPaths2.RuntimeResourcePath << "\n";
    
    // Check if Swift standard library is already loaded
    auto swiftModule = sharedASTContext->getLoadedModule(sharedASTContext->getIdentifier("Swift"));
    llvm::errs() << "[SwiftIncrementalParser] Swift standard library already loaded: " << (swiftModule != nullptr) << "\n";
    if (swiftModule) {
        llvm::errs() << "[SwiftIncrementalParser] Swift module name: " << swiftModule->getName() << "\n";
        llvm::errs() << "[SwiftIncrementalParser] Swift module files count: " << swiftModule->getFiles().size() << "\n";
    }
    
    // Log loaded modules
    auto loadedModules = sharedASTContext->getLoadedModules();
    llvm::errs() << "[SwiftIncrementalParser] Loaded modules count: " << std::distance(loadedModules.begin(), loadedModules.end()) << "\n";
    for (auto &pair : loadedModules) {
        llvm::errs() << "[SwiftIncrementalParser]   Loaded module: " << pair.first << " -> " << pair.second->getName() << "\n";
    }
    
    // Perform semantic analysis
    llvm::errs() << "[SwiftIncrementalParser] Calling performSema()\n";
    ci->performSema();
    llvm::errs() << "[SwiftIncrementalParser] performSema() completed\n";
    
    // Check for errors and get detailed diagnostic information
    bool astHadError = sharedASTContext->hadError();
    bool diagsHadError = ci->getDiags().hadAnyError();
    
    llvm::errs() << "[SwiftIncrementalParser] AST had error: " << astHadError << "\n";
    llvm::errs() << "[SwiftIncrementalParser] Diagnostics had error: " << diagsHadError << "\n";
    
    // Log detailed diagnostic information if there are errors
    if (diagsHadError) {
        llvm::errs() << "[SwiftIncrementalParser] === DIAGNOSTIC DETAILS ===\n";
        llvm::errs() << "[SwiftIncrementalParser] Diagnostics had any error: " << diagsHadError << "\n";
        
        // Log ASTContext error details
        if (astHadError) {
            llvm::errs() << "[SwiftIncrementalParser] ASTContext error details:\n";
            llvm::errs() << "[SwiftIncrementalParser]   - hadError(): " << sharedASTContext->hadError() << "\n";
            llvm::errs() << "[SwiftIncrementalParser]   - Diags.hadAnyError(): " << sharedASTContext->Diags.hadAnyError() << "\n";
            
            // Log more detailed error state
            llvm::errs() << "[SwiftIncrementalParser]   - ASTContext error flags:\n";
            llvm::errs() << "[SwiftIncrementalParser]     * Error state: " << (astHadError ? "HAS_ERROR" : "NO_ERROR") << "\n";
            
            // Log module-specific error information
            llvm::errs() << "[SwiftIncrementalParser]   - Module error state:\n";
            llvm::errs() << "[SwiftIncrementalParser]     * Module has resolved imports: " << accumulatedModule->hasResolvedImports() << "\n";
            llvm::errs() << "[SwiftIncrementalParser]     * Module is main module: " << accumulatedModule->isMainModule() << "\n";
        }
        
        llvm::errs() << "[SwiftIncrementalParser] === END DIAGNOSTIC DETAILS ===\n";
    }
    
    // Log detailed state after performSema
    llvm::errs() << "[SwiftIncrementalParser] After performSema - Module state:\n";
    llvm::errs() << "[SwiftIncrementalParser]   Module name: " << accumulatedModule->getName() << "\n";
    llvm::errs() << "[SwiftIncrementalParser]   Module files count: " << accumulatedModule->getFiles().size() << "\n";
    llvm::errs() << "[SwiftIncrementalParser]   Module has resolved imports: " << accumulatedModule->hasResolvedImports() << "\n";
    
    // Log the SourceFile state
    for (auto *file : accumulatedModule->getFiles()) {
        auto *SF = llvm::dyn_cast<swift::SourceFile>(file);
        if (SF) {
            llvm::errs() << "[SwiftIncrementalParser]   SourceFile: " << SF->getFilename() << "\n";
            llvm::errs() << "[SwiftIncrementalParser]   SourceFile AST stage: " << (int)SF->ASTStage << "\n";
            llvm::errs() << "[SwiftIncrementalParser]   SourceFile top-level decls count: " << SF->getTopLevelDecls().size() << "\n";
            
            // Log the top-level declarations
            for (size_t i = 0; i < SF->getTopLevelDecls().size(); ++i) {
                auto decl = SF->getTopLevelDecls()[i];
                llvm::errs() << "[SwiftIncrementalParser]     Top-level decl " << i << ": " << swift::Decl::getKindName(decl->getKind()) << "\n";
                if (auto *topLevelCode = llvm::dyn_cast<swift::TopLevelCodeDecl>(decl)) {
                    llvm::errs() << "[SwiftIncrementalParser]       TopLevelCode body statements count: " << topLevelCode->getBody()->getElements().size() << "\n";
                    for (size_t j = 0; j < topLevelCode->getBody()->getElements().size(); ++j) {
                        auto stmt = topLevelCode->getBody()->getElements()[j];
                        llvm::errs() << "[SwiftIncrementalParser]         Statement " << j << ": " << (stmt.is<swift::Stmt*>() ? "Stmt" : "Decl") << "\n";
                    }
                }
            }
        }
    }
    
    // Log ASTContext state after performSema
    llvm::errs() << "[SwiftIncrementalParser] After performSema - ASTContext state:\n";
    llvm::errs() << "[SwiftIncrementalParser]   ASTContext had error: " << sharedASTContext->hadError() << "\n";
    llvm::errs() << "[SwiftIncrementalParser]   ASTContext Diags had error: " << sharedASTContext->Diags.hadAnyError() << "\n";
    
    // Check if Swift standard library is now loaded
    auto swiftModuleAfter = sharedASTContext->getLoadedModule(sharedASTContext->getIdentifier("Swift"));
    llvm::errs() << "[SwiftIncrementalParser] Swift standard library loaded after performSema: " << (swiftModuleAfter != nullptr) << "\n";
    if (swiftModuleAfter) {
        llvm::errs() << "[SwiftIncrementalParser] Swift module name after: " << swiftModuleAfter->getName() << "\n";
        llvm::errs() << "[SwiftIncrementalParser] Swift module files count after: " << swiftModuleAfter->getFiles().size() << "\n";
    }
    
    // Log loaded modules after performSema
    auto loadedModulesAfter = sharedASTContext->getLoadedModules();
    llvm::errs() << "[SwiftIncrementalParser] Loaded modules count after performSema: " << std::distance(loadedModulesAfter.begin(), loadedModulesAfter.end()) << "\n";
    for (auto &pair : loadedModulesAfter) {
        llvm::errs() << "[SwiftIncrementalParser]   Loaded module after: " << pair.first << " -> " << pair.second->getName() << "\n";
    }
    
    if (astHadError || diagsHadError) {
        llvm::errs() << "[SwiftIncrementalParser] ERROR: Semantic analysis failed\n";
        
        // Print module state for debugging
        llvm::SmallVector<swift::Decl*, 32> topLevelDecls;
        accumulatedModule->getTopLevelDecls(topLevelDecls);
        llvm::errs() << "[SwiftIncrementalParser] Module declarations count: " << topLevelDecls.size() << "\n";
        for (size_t i = 0; i < topLevelDecls.size() && i < 5; ++i) {
            auto decl = topLevelDecls[i];
            llvm::errs() << "[SwiftIncrementalParser]   [" << i << "] " << swift::Decl::getKindName(decl->getKind()) << "\n";
        }
        
        // Dump the source buffer text for the SourceFile
        auto sourceFiles = accumulatedModule->getFiles();
        if (!sourceFiles.empty()) {
            auto sourceFile = llvm::dyn_cast<swift::SourceFile>(sourceFiles[0]);
            if (sourceFile) {
                llvm::errs() << "[SwiftIncrementalParser] Source file has " << sourceFile->getTopLevelDecls().size() << " top-level declarations\n";
                auto bufferID = sourceFile->getBufferID();
                llvm::errs() << "[SwiftIncrementalParser] Source file buffer ID: " << bufferID << "\n";
                if (bufferID) {
                    auto curBuf = sharedASTContext->SourceMgr.getEntireTextForBuffer(bufferID);
                    llvm::errs() << "[SwiftIncrementalParser] Source buffer contents:\n" << curBuf << "\n";
                }
            }
        }
        
        // Check if the issue is related to missing imports
        llvm::errs() << "[SwiftIncrementalParser] Checking for import issues...\n";
        llvm::errs() << "[SwiftIncrementalParser] Module name: " << accumulatedModule->getName() << "\n";
        llvm::errs() << "[SwiftIncrementalParser] Module is main module: " << accumulatedModule->isMainModule() << "\n";
        
        // Return a detailed error to surface to the caller
        std::string errMsg = ("Semantic analysis failed (module=" + moduleName + ")");
        return llvm::createStringError(llvm::inconvertibleErrorCode(), errMsg.c_str());
    }
    
    llvm::errs() << "[SwiftIncrementalParser] Semantic analysis successful\n";
    
    // Create a new PTU for this expression
    llvm::errs() << "[SwiftIncrementalParser] Creating new PTU\n";
    PTUs.emplace_back();
    auto& ptu = PTUs.back();
    ptu.ModulePart = accumulatedModule;
    ptu.SharedASTContext = sharedASTContext;
    ptu.InputCode = Input.str();
    
    llvm::errs() << "[SwiftIncrementalParser] PTU created, lowering Swift to an LLVM module...\n";
    
    // Get the LLVM context
    auto llvmCtx = TSCtx->getContext();
    
    // Lower Swift code to an LLVM module using the accumulated module
    // Create a unique entry suffix per evaluation to avoid duplicate symbols in the JIT
    unsigned suffixIndex = InputCount ? (InputCount - 1) : 0;
    std::string entrySuffixStr = (llvm::Twine(accumulatedModule->getName().str()) + "_" + llvm::Twine(suffixIndex)).str();
    auto llvmModule = lowerSwiftCodeToLLVMModule(accumulatedModule, sharedASTContext, llvmCtx, entrySuffixStr);
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

void SwiftIncrementalParser::cleanUpPTU(SwiftPartialTranslationUnit& PTU) {
    // Clean up the LLVM module
    PTU.TheModule.reset();
    PTU.ModulePart = nullptr;  // ModuleDecl is owned by ASTContext
    PTU.InputCode.clear();
}

std::string SwiftIncrementalParser::accumulateAllCode(const std::string& newInput) {
    std::ostringstream accumulated;
    
    // Add Swift import only once at the beginning
    accumulated << "import Swift\n";
    
    // Add all previous inputs, but skip the import statements to avoid duplicates
    for (const auto& ptu : PTUs) {
        if (!ptu.InputCode.empty()) {
            std::string code = ptu.InputCode;
            // Remove "import Swift\n" from the beginning if it exists
            if (code.find("import Swift\n") == 0) {
                code = code.substr(13); // Remove "import Swift\n"
            }
            // Ensure each statement is properly terminated
            if (!code.empty() && code.back() != '\n') {
                code += "\n";
            }
            accumulated << code;
        }
    }
    
    // Add the new input, but skip the import statement
    std::string newCode = newInput;
    if (newCode.find("import Swift\n") == 0) {
        newCode = newCode.substr(13); // Remove "import Swift\n"
    }
    
    // Ensure proper statement termination
    if (!newCode.empty() && newCode.back() != '\n') {
        newCode += "\n";
    }
    
    accumulated << newCode;
    
    return accumulated.str();
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

} // namespace SwiftJITREPL
