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

/**
 * Lower Swift code to LLVM IR using Swift's built-in utilities
 * This uses the same pipeline as Swift's immediate mode
 */
static std::unique_ptr<llvm::Module> lowerSwiftCodeToLLVMModule(swift::ModuleDecl* module,
                                                                swift::ASTContext* astContext,
                                                                llvm::LLVMContext* llvmCtx,
                                                                llvm::StringRef entrySuffix) {
    if (!module || !astContext) {
        return nullptr;
    }

    // Create a minimal CompilerInvocation for IR generation
    swift::CompilerInvocation invocation;
    invocation.getLangOptions().Target = llvm::Triple(TARGET_TRIPLE);
    invocation.getLangOptions().EnableObjCInterop = true;
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
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Starting AST lowering to SIL\n";
    auto typeConverter = std::make_unique<swift::Lowering::TypeConverter>(*module);
    auto silModule = swift::performASTLowering(module, *typeConverter, invocation.getSILOptions());
    if (!silModule) {
        llvm::errs() << "[lowerSwiftCodeToLLVMModule] AST lowering to SIL failed\n";
        return nullptr;
    }
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] AST lowering to SIL completed successfully\n";
    
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] Starting IR generation from SIL\n";
    swift::GeneratedModule genModule = swift::performIRGeneration(
        module, IRGenOpts, TBDOpts, std::move(silModule),
        module->getName().str(), PSPs,
        /*parallelOutputFilenames*/ llvm::ArrayRef<std::string>(),
        /*outModuleHash*/ nullptr);
    llvm::errs() << "[lowerSwiftCodeToLLVMModule] IR generation completed\n";

    auto *Produced = genModule.getModule();
    if (!Produced) {
        return nullptr;
    }

    // Serialize to bitcode and parse back into our shared LLVM context
    llvm::SmallVector<char, 0> bitcodeBuffer;
    {
        llvm::raw_svector_ostream os(bitcodeBuffer);
        llvm::WriteBitcodeToFile(*Produced, os);
    }
    auto memBuffer = llvm::MemoryBuffer::getMemBufferCopy(
        llvm::StringRef(bitcodeBuffer.data(), bitcodeBuffer.size()),
        Produced->getName());
    auto modOrErr = llvm::parseBitcodeFile(memBuffer->getMemBufferRef(), *llvmCtx);
    if (!modOrErr) {
        return nullptr;
    }
    std::unique_ptr<llvm::Module> Parsed = std::move(*modOrErr);
    
    // Rename the entry function 'main' to a unique per-module symbol to avoid duplicates
    if (llvm::Function *MainF = Parsed->getFunction("main")) {
        std::string uniqueEntry = (llvm::Twine("swift_jit_main_") + entrySuffix).str();
        MainF->setName(uniqueEntry);
    }

    return std::move(Parsed);
}

SwiftIncrementalParser::SwiftIncrementalParser(swift::ASTContext* sharedASTContext, 
                                               std::vector<swift::ModuleDecl*>* modules,
                                               llvm::orc::ThreadSafeContext* TSCtx,
                                               swift::CompilerInstance* sharedCompilerInstance,
                                               swift::CompilerInvocation* sharedCompilerInvocation)
    : sharedASTContext(sharedASTContext), modules(modules), TSCtx(TSCtx), sharedCompilerInstance(sharedCompilerInstance), sharedCompilerInvocation(sharedCompilerInvocation) {
}

SwiftIncrementalParser::~SwiftIncrementalParser() {
    // Clean up all PTUs
    for (auto& ptu : PTUs) {
        cleanUpPTU(ptu);
    }
}

llvm::Expected<SwiftPartialTranslationUnit&> SwiftIncrementalParser::parse(llvm::StringRef Input) {
    // Accumulate all previous code plus the new input into a single module
    std::string accumulatedCode = accumulateAllCode(Input.str());
    
    // Reset ASTContext error state to avoid conflicts from previous evaluations
    // This is necessary because we're using a shared ASTContext across evaluations
    sharedASTContext->Diags.resetHadAnyError();
    
    // Create a single module with all accumulated code
    // Ensure a unique module name for each parse to avoid collisions
    auto nowCount = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string moduleName = std::string("SwiftJITREPL_Accumulated_") + std::to_string(nowCount);
    swift::ImplicitImportInfo importInfo = createImplicitImports();
    
    auto accumulatedModule = swift::ModuleDecl::createMainModule(
        *sharedASTContext,
        sharedASTContext->getIdentifier(moduleName),
        importInfo,
        [&](swift::ModuleDecl* module, auto addFile) {
            // Create a MemoryBuffer for the accumulated code
            std::ostringstream sourceName;
            sourceName << "swift_repl_accumulated_" << InputCount++;
            auto inputBuffer = llvm::MemoryBuffer::getMemBufferCopy(accumulatedCode, sourceName.str());
            auto &sourceMgr = sharedASTContext->SourceMgr;
            unsigned bufferID = sourceMgr.addNewSourceBuffer(std::move(inputBuffer));
            addFile(new (*sharedASTContext) swift::SourceFile(
                *module,
                swift::SourceFileKind::Main,
                bufferID,
                swift::SourceFile::getDefaultParsingOptions(sharedASTContext->LangOpts)
            ));
        }
    );
    sharedASTContext->addLoadedModule(accumulatedModule);
    modules->push_back(accumulatedModule);
    
    // Perform semantic analysis on the new module using the shared CompilerInstance
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
    // Point the shared CI to the accumulated main module (same ASTContext)
    ci->setMainModule(accumulatedModule);
    
    // Perform semantic analysis without attaching additional diagnostic consumers.
    
    // Log search paths
    auto &searchPaths2 = ci->getInvocation().getSearchPathOptions();
    
    // Perform semantic analysis
    llvm::errs() << "[SwiftIncrementalParser::parse] About to call performSema()\n";
    ci->performSema();
    llvm::errs() << "[SwiftIncrementalParser::parse] performSema() completed successfully\n";
    
    // Check for errors and get detailed diagnostic information
    bool astHadError = sharedASTContext->hadError();
    bool diagsHadError = ci->getDiags().hadAnyError();
    
    // Log detailed diagnostic information if there are errors
    if (diagsHadError) {
        // Log ASTContext error details
        if (astHadError) {
            // Log more detailed error state
            // Log module-specific error information
        }
    }
    
    // Log detailed state after performSema
    
    // Log the SourceFile state
    for (auto *file : accumulatedModule->getFiles()) {
        auto *SF = llvm::dyn_cast<swift::SourceFile>(file);
        if (SF) {
            // Log the top-level declarations
            for (size_t i = 0; i < SF->getTopLevelDecls().size(); ++i) {
                auto decl = SF->getTopLevelDecls()[i];
                if (auto *topLevelCode = llvm::dyn_cast<swift::TopLevelCodeDecl>(decl)) {
                    for (size_t j = 0; j < topLevelCode->getBody()->getElements().size(); ++j) {
                        auto stmt = topLevelCode->getBody()->getElements()[j];
                    }
                }
            }
        }
    }
    
    // Log ASTContext state after performSema
    
    // Check if Swift standard library is now loaded
    auto swiftModuleAfter = sharedASTContext->getLoadedModule(sharedASTContext->getIdentifier("Swift"));
    
    // Log loaded modules after performSema
    auto loadedModulesAfter = sharedASTContext->getLoadedModules();
    
    if (astHadError || diagsHadError) {
        // Print module state for debugging
        llvm::SmallVector<swift::Decl*, 32> topLevelDecls;
        accumulatedModule->getTopLevelDecls(topLevelDecls);
        for (size_t i = 0; i < topLevelDecls.size() && i < 5; ++i) {
            auto decl = topLevelDecls[i];
        }
        
        // Dump the source buffer text for the SourceFile
        auto sourceFiles = accumulatedModule->getFiles();
        if (!sourceFiles.empty()) {
            auto sourceFile = llvm::dyn_cast<swift::SourceFile>(sourceFiles[0]);
            if (sourceFile) {
                auto bufferID = sourceFile->getBufferID();
                if (bufferID) {
                    auto curBuf = sharedASTContext->SourceMgr.getEntireTextForBuffer(bufferID);
                }
            }
        }
        
        // Check if the issue is related to missing imports
        
        // Return a detailed error to surface to the caller
        std::string errMsg = ("Semantic analysis failed (module=" + moduleName + ")");
        return llvm::createStringError(llvm::inconvertibleErrorCode(), errMsg.c_str());
    }
    
    // Create a new PTU for this expression
    PTUs.emplace_back();
    auto& ptu = PTUs.back();
    ptu.moduleDeclaration = accumulatedModule;
    ptu.SharedASTContext = sharedASTContext;
    ptu.InputCode = Input.str();
    
    // Get the LLVM context
    auto llvmCtx = TSCtx->getContext();
    
    // Lower Swift code to an LLVM module using the accumulated module
    // Create a unique entry suffix per evaluation to avoid duplicate symbols in the JIT
    unsigned suffixIndex = InputCount ? (InputCount - 1) : 0;
    std::string entrySuffixStr = (llvm::Twine(accumulatedModule->getName().str()) + "_" + llvm::Twine(suffixIndex)).str();
    llvm::errs() << "[SwiftIncrementalParser::parse] About to call lowerSwiftCodeToLLVMModule\n";
    auto llvmModule = lowerSwiftCodeToLLVMModule(accumulatedModule, sharedASTContext, llvmCtx, entrySuffixStr);
    llvm::errs() << "[SwiftIncrementalParser::parse] lowerSwiftCodeToLLVMModule completed\n";
    if (llvmModule) {
        ptu.TheModule = std::move(llvmModule);
    } else {
        ptu.TheModule = nullptr;
    }
    return ptu;
}

void SwiftIncrementalParser::cleanUpPTU(SwiftPartialTranslationUnit& PTU) {
    // Clean up the LLVM module
    PTU.TheModule.reset();
    PTU.moduleDeclaration = nullptr;  // ModuleDecl is owned by ASTContext
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
    
    // Add Foundation module import to enable file I/O operations
    auto foundationModule = sharedASTContext->getModuleByName("Foundation");
    if (foundationModule && !foundationModule->failedToLoad()) {
        importInfo.AdditionalImports.emplace_back(
            swift::ImportedModule(foundationModule));
        llvm::errs() << "[SwiftIncrementalParser] Foundation module loaded successfully\n";
    } else {
        llvm::errs() << "[SwiftIncrementalParser] Foundation module not available\n";
        // List available modules for debugging
        auto loadedModules = sharedASTContext->getLoadedModules();
        llvm::errs() << "[SwiftIncrementalParser] Available modules: ";
        for (const auto& [name, module] : loadedModules) {
            llvm::errs() << name.str() << " ";
        }
        llvm::errs() << "\n";
    }
    
    return importInfo;
}

} // namespace SwiftJITREPL
