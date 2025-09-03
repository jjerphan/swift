cmake_minimum_required(VERSION 3.16)
project(SwiftJITREPL)

# clang and clang++ have to be used
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set build type to Release with Debug Info because swift has been built with RelWithDebInfo
set(CMAKE_BUILD_TYPE RelWithDebInfo)

# Ensure RelWithDebInfo flags
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")

# Set Swift compiler paths
set(SWIFT_SOURCE_DIR "${CMAKE_SOURCE_DIR}/..")
set(SWIFT_BUILD_DIR "/home/jjerphan/dev/build/Ninja-RelWithDebInfoAssert")
set(SWIFT_BUILD_SWIFT "${SWIFT_BUILD_DIR}/swift-linux-x86_64")
set(SWIFT_BUILD_LLVM "${SWIFT_BUILD_DIR}/llvm-linux-x86_64")
set(CLANG_SOURCE_DIR "/home/jjerphan/dev/llvm-project/clang")
set(LLVM_SOURCE_DIR "/home/jjerphan/dev/llvm-project/llvm")
set(LLVM_PROJECT_DIR "/home/jjerphan/dev/llvm-project")

# Include directories
include_directories(
    # Swift source headers (primary)
    "${SWIFT_SOURCE_DIR}/include"
    
    # Swift build headers
    "${SWIFT_BUILD_SWIFT}/include"
    
    # LLVM build headers
    "${SWIFT_BUILD_LLVM}/include"
    
    # LLVM source headers (fallback)
    "${LLVM_SOURCE_DIR}/include"
    
    # Clang headers (required by Swift)
    "${CLANG_SOURCE_DIR}/include"
    "${SWIFT_BUILD_LLVM}/tools/clang/include"
    
    # Additional Swift include paths
    "${SWIFT_SOURCE_DIR}/lib"
    "${SWIFT_BUILD_SWIFT}/lib"
)

# No external library dependencies for now - compile standalone
# This will be expanded once we resolve the complex linking issues

# Use LLVM component system for better dependency management
set(Swift_DIR "${SWIFT_BUILD_SWIFT}/lib/cmake/swift")
set(LLVM_DIR "${SWIFT_BUILD_LLVM}/lib/cmake/llvm")
find_package(Threads REQUIRED)
find_package(LLVM REQUIRED CONFIG)

# Swift library static paths for JIT functionality
# Use static library paths to avoid system library conflicts
set(SWIFT_LIBRARIES
    # Core JIT library - this is the main library we need
    "${SWIFT_BUILD_SWIFT}/lib/libswiftImmediate.a"
    
    # Swift runtime library - needed for Swift runtime symbols
    "${SWIFT_BUILD_SWIFT}/lib/swift/linux/libswiftCore.so"
    
    # Basic Swift infrastructure - needed for CompilerInstance
    "${SWIFT_BUILD_SWIFT}/lib/libswiftBasic.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftParse.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftAST.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftSema.a"
    
    # SIL libraries - needed for SILModule functions
    "${SWIFT_BUILD_SWIFT}/lib/libswiftSIL.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftSILGen.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftSILOptimizer.a"
    
    # Frontend - needed for CompilerInstance::setup and performSema
    "${SWIFT_BUILD_SWIFT}/lib/libswiftFrontend.a"
    
    # IRGen - needed for IR generation functions
    "${SWIFT_BUILD_SWIFT}/lib/libswiftIRGen.a"
    
    # Additional utilities
    "${SWIFT_BUILD_SWIFT}/lib/libswiftDemangling.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftOption.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftSerialization.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftDriver.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftClangImporter.a"
    
    # Additional Swift libraries for ASTGen, markup, and macro functions
    "${SWIFT_BUILD_SWIFT}/lib/libswiftASTGen.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftMarkup.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftIDE.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftIDETool.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftIDEUtilsBridging.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftIndex.a"
    
    # LLVM passes library - needed for InlineTreePrinterPass
    "${SWIFT_BUILD_SWIFT}/lib/libswiftLLVMPasses.a"
    
    # Macro evaluation library - needed for macro functionality
    "${SWIFT_BUILD_SWIFT}/lib/libswiftMacroEvaluation.a"
    
    # Symbol graph generation library - needed for symbol graph functionality
    "${SWIFT_BUILD_SWIFT}/lib/libswiftSymbolGraphGen.a"
    
    # Const extraction library - needed for const extraction functionality
    "${SWIFT_BUILD_SWIFT}/lib/libswiftConstExtract.a"
    
    # Localization library - needed for localization functionality
    "${SWIFT_BUILD_SWIFT}/lib/libswiftLocalization.a"
    
    # API digester library - needed for API digesting functionality
    "${SWIFT_BUILD_SWIFT}/lib/libswiftAPIDigester.a"
    
    # AST section importer library - needed for AST section importing
    "${SWIFT_BUILD_SWIFT}/lib/libswiftASTSectionImporter.a"
    
    # Compiler modules library - needed for compiler modules
    "${SWIFT_BUILD_SWIFT}/lib/libswiftCompilerModules.a"
    
    # Dependency scanning libraries - needed for dependency scanning
    "${SWIFT_BUILD_SWIFT}/lib/libswiftDependencyScan.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftDependencyScanImpl.a"
    
    # Driver tool library - needed for driver tools
    "${SWIFT_BUILD_SWIFT}/lib/libswiftDriverTool.a"
    
    # Frontend tool library - needed for frontend tools
    "${SWIFT_BUILD_SWIFT}/lib/libswiftFrontendTool.a"
    
    # Migrator library - needed for migration functionality
    "${SWIFT_BUILD_SWIFT}/lib/libswiftMigrator.a"
    
    # Print as Clang library - needed for printing as Clang
    "${SWIFT_BUILD_SWIFT}/lib/libswiftPrintAsClang.a"
    
    # Refactoring library - needed for refactoring functionality
    "${SWIFT_BUILD_SWIFT}/lib/libswiftRefactoring.a"
    
    # Remote AST library - needed for remote AST functionality
    "${SWIFT_BUILD_SWIFT}/lib/libswiftRemoteAST.a"
    
    # Remote inspection library - needed for remote inspection
    "${SWIFT_BUILD_SWIFT}/lib/libswiftRemoteInspection.a"
    
    # Remote mirror library - needed for remote mirror functionality
    "${SWIFT_BUILD_SWIFT}/lib/libswiftRemoteMirror.a"
    
    # Static mirror library - needed for static mirror functionality
    "${SWIFT_BUILD_SWIFT}/lib/libswiftStaticMirror.a"
    
    # Threading library - needed for threading functionality
    "${SWIFT_BUILD_SWIFT}/lib/libswiftThreading.a"
    
    # Swift compiler modules - static libraries only
    "${SWIFT_BUILD_SWIFT}/lib/swift/host/compiler/lib_Compiler_SwiftSyntaxCShims.a"
    "${SWIFT_BUILD_SWIFT}/lib/swift/host/lib_SwiftSyntaxCShims.a"
)

# Use static library paths directly to avoid LLVM component system issues
# This gives us full control over which libraries are linked
set(LLVM_LIBRARIES
    # Core LLVM libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMCore.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMSupport.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMDemangle.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMBinaryFormat.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMTargetParser.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMTarget.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMOption.a"
    
    # CAS (Content Addressable Storage) libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMCAS.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMCASUtil.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMMCCAS.a"
    
    # MC (Machine Code) libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMMC.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMMCParser.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMMCDisassembler.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMObject.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMObjectYAML.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMObjCopy.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMMCA.a"
    
    # Bitcode libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMBitReader.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMBitWriter.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMBitstreamReader.a"
    
    # IR libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMIRReader.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMIRPrinter.a"
    
    # Execution engine libraries for JIT functionality
    "${SWIFT_BUILD_LLVM}/lib/libLLVMExecutionEngine.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMMCJIT.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMRuntimeDyld.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMInterpreter.a"
    
    # ORC (On-Request Compilation) libraries for modern JIT
    "${SWIFT_BUILD_LLVM}/lib/libLLVMOrcJIT.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMOrcShared.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMOrcTargetProcess.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMOrcDebugging.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMJITLink.a"
    
    # CodeGen libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMCodeGen.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMCodeGenData.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMCodeGenTypes.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMSelectionDAG.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMAsmPrinter.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMMIRParser.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMGlobalISel.a"
    
    # Analysis libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMAnalysis.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMTransformUtils.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMInstCombine.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMAggressiveInstCombine.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMScalarOpts.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMipo.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMVectorize.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMObjCARCOpts.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMCoroutines.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMLinker.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMInstrumentation.a"
    
    # Frontend libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMFrontendDriver.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMFrontendHLSL.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMFrontendOffloading.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMFrontendOpenMP.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMFrontendOpenACC.a"
    
    # Debug info libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMDebugInfoDWARF.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMDebugInfoCodeView.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMDebugInfoMSF.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMDebugInfoPDB.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMDebugInfoBTF.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMDebugInfoGSYM.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMDebugInfoLogicalView.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMDebuginfod.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMDWP.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMSymbolize.a"
    
    # DWARF linker libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMDWARFLinker.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMDWARFLinkerClassic.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMDWARFLinkerParallel.a"
    
    # LTO and remarks
    "${SWIFT_BUILD_LLVM}/lib/libLLVMLTO.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMRemarks.a"
    
    # Text API
    "${SWIFT_BUILD_LLVM}/lib/libLLVMTextAPI.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMTextAPIBinaryReader.a"
    
    # Windows driver
    "${SWIFT_BUILD_LLVM}/lib/libLLVMWindowsDriver.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMWindowsManifest.a"
    
    # Profile data
    "${SWIFT_BUILD_LLVM}/lib/libLLVMProfileData.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMCoverage.a"
    
    # Passes and option libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMPasses.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMOption.a"
    
    # TableGen libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMTableGen.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMTableGenBasic.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMTableGenCommon.a"
    
    # Utility libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMLineEditor.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMLibDriver.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMOptDriver.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMDlltoolDriver.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMFileCheck.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMTestingSupport.a"
    
    # Fuzzing libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMFuzzerCLI.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMFuzzMutate.a"
    
    # Exegesis libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMExegesis.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMExegesisAArch64.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMExegesisMips.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMExegesisPowerPC.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMExegesisX86.a"
    
    # XRay libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMXRay.a"
    
    # Interface stub libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMInterfaceStub.a"
    
    # Remote caching libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMRemoteCachingService.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMRemoteNullService.a"
    
    # Sandbox IR libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMSandboxIR.a"
    
    # Extensions libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMExtensions.a"
    
    # HIP libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMHipStdPar.a"
    
    # CFGuard libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMCFGuard.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMCFIVerify.a"
    
    # Diff libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMDiff.a"
    
    # Target libraries for AArch64
    "${SWIFT_BUILD_LLVM}/lib/libLLVMAArch64AsmParser.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMAArch64CodeGen.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMAArch64Desc.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMAArch64Disassembler.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMAArch64Info.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMAArch64Utils.a"
    
    # Target libraries for ARM
    "${SWIFT_BUILD_LLVM}/lib/libLLVMARMAsmParser.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMARMCodeGen.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMARMDesc.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMARMDisassembler.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMARMInfo.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMARMUtils.a"
    
    # Target libraries for AVR
    "${SWIFT_BUILD_LLVM}/lib/libLLVMAVRAsmParser.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMAVRCodeGen.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMAVRDesc.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMAVRDisassembler.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMAVRInfo.a"
    
    # Target libraries for Mips
    "${SWIFT_BUILD_LLVM}/lib/libLLVMMipsAsmParser.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMMipsCodeGen.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMMipsDesc.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMMipsDisassembler.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMMipsInfo.a"
    
    # Target libraries for PowerPC
    "${SWIFT_BUILD_LLVM}/lib/libLLVMPowerPCAsmParser.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMPowerPCCodeGen.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMPowerPCDesc.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMPowerPCDisassembler.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMPowerPCInfo.a"
    
    # Target libraries for RISCV
    "${SWIFT_BUILD_LLVM}/lib/libLLVMRISCVAsmParser.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMRISCVCodeGen.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMRISCVDesc.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMRISCVDisassembler.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMRISCVInfo.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMRISCVTargetMCA.a"
    
    # Target libraries for SystemZ
    "${SWIFT_BUILD_LLVM}/lib/libLLVMSystemZAsmParser.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMSystemZCodeGen.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMSystemZDesc.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMSystemZDisassembler.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMSystemZInfo.a"
    
    # Target libraries for WebAssembly
    "${SWIFT_BUILD_LLVM}/lib/libLLVMWebAssemblyAsmParser.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMWebAssemblyCodeGen.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMWebAssemblyDesc.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMWebAssemblyDisassembler.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMWebAssemblyInfo.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMWebAssemblyUtils.a"
    
    # Target libraries for X86
    "${SWIFT_BUILD_LLVM}/lib/libLLVMX86AsmParser.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMX86CodeGen.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMX86Desc.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMX86Disassembler.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMX86Info.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMX86TargetMCA.a"
)

# Use LLVM component system for better dependency management
# This automatically handles dependencies and linking order
set(LLVM_COMPONENTS
    # Core components
    Core
    Support
    Demangle
    BinaryFormat
    TargetParser
    
    # CAS (Content Addressable Storage)
    CAS
    
    # MC (Machine Code)
    MC
    MCParser
    MCDisassembler
    Object
    
    # Target components (minimal set for x86_64)
    X86Desc
    X86Info
    X86AsmParser
    X86CodeGen
    X86Disassembler
    
    # Execution engine components for JIT functionality
    ExecutionEngine
    MCJIT
    RuntimeDyld
    
    # ORC (On-Request Compilation) components for modern JIT
    OrcJIT
    OrcShared
    OrcTargetProcess
    OrcDebugging
    
    # Additional components
    BitReader
    BitWriter
    ProfileData
    
    # Debug info components
    DebugInfoDWARF
    DebugInfoCodeView
    DebugInfoMSF
    DebugInfoPDB
    DebugInfoBTF
    DebugInfoGSYM
    DebugInfoLogicalView
    
    # DWARF linker components
    DWARFLinker
    DWARFLinkerClassic
    DWARFLinkerParallel
    
    # LTO and remarks
    LTO
    Remarks
    
    # Text API
    TextAPI
    TextAPIBinaryReader
    
    # Windows driver
    WindowsDriver
    
    # HLSL and DXIL
    FrontendHLSL
    
    # JIT Link and AArch64 utilities
    JITLink
    AArch64Utils
    
    # CodeGen components - needed for SelectionDAG, MachineInstr, etc.
    CodeGen
    SelectionDAG
    AsmPrinter
    MIRParser
    GlobalISel
    Analysis
    TransformUtils
    InstCombine
    ScalarOpts
    ipo
    Vectorize
    ObjCARCOpts
    Coroutines
    Linker
    AggressiveInstCombine
    Instrumentation
    FrontendOpenMP
    FrontendOpenACC
    
    # Target components for all architectures
    ARMDesc
    ARMInfo
    ARMAsmParser
    ARMCodeGen
    ARMDisassembler
    AArch64Desc
    AArch64Info
    AArch64AsmParser
    AArch64CodeGen
    AArch64Disassembler
    PowerPCDesc
    PowerPCInfo
    PowerPCAsmParser
    PowerPCCodeGen
    PowerPCDisassembler
    RISCVDesc
    RISCVInfo
    RISCVAsmParser
    RISCVCodeGen
    RISCVDisassembler
    SystemZDesc
    SystemZInfo
    SystemZAsmParser
    SystemZCodeGen
    SystemZDisassembler
    WebAssemblyDesc
    WebAssemblyInfo
    WebAssemblyAsmParser
    WebAssemblyCodeGen
    WebAssemblyDisassembler
    MipsDesc
    MipsInfo
    MipsAsmParser
    MipsCodeGen
    MipsDisassembler
    AVRDesc
    AVRInfo
    AVRAsmParser
    AVRCodeGen
    AVRDisassembler
)

# Map components to library names
# llvm_map_components_to_libnames(LLVM_LIBRARIES ${LLVM_COMPONENTS})

# Debug: Print the resolved libraries
message(STATUS "LLVM_LIBRARIES resolved to: ${LLVM_LIBRARIES}")

# Clang library components - comprehensive set for full Clang functionality
set(CLANG_LIBRARIES
    # Core Clang libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangBasic.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangLex.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangParse.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangAST.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangSema.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangSerialization.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangFrontend.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangDriver.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangSupport.a"
    
    # Analysis libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangAnalysis.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangAnalysisFlowSensitive.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangAnalysisFlowSensitiveModels.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangStaticAnalyzerCore.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangStaticAnalyzerCheckers.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangStaticAnalyzerFrontend.a"
    
    # AST and matcher libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangASTMatchers.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangDynamicASTMatchers.a"
    
    # Code generation libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangCodeGen.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangHandleCXX.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangHandleLLVM.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangInterpreter.a"
    
    # CAS (Content Addressable Storage) libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangCAS.a"
    
    # Edit and rewrite libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangEdit.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangRewrite.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangRewriteFrontend.a"
    
    # Indexing libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangIndex.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangIndexDataStore.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangIndexSerialization.a"
    
    # Formatting libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangFormat.a"
    
    # Tooling libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangTooling.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangToolingCore.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangToolingASTDiff.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangToolingInclusions.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangToolingInclusionsStdlib.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangToolingRefactor.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangToolingRefactoring.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangToolingSyntax.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTransformer.a"
    
    # Frontend tool libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangFrontendTool.a"
    
    # Dependency scanning libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangDependencyScanning.a"
    
    # API Notes libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangAPINotes.a"
    
    # Apply replacements libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangApplyReplacements.a"
    
    # ARC migration libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangARCMigrate.a"
    
    # Change namespace libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangChangeNamespace.a"
    
    # Cross translation unit libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangCrossTU.a"
    
    # Daemon libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangDaemon.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangDaemonTweaks.a"
    
    # Directory watcher libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangDirectoryWatcher.a"
    
    # Documentation libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangDoc.a"
    
    # Extract API libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangExtractAPI.a"
    
    # Include cleaner libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangIncludeCleaner.a"
    
    # Include fixer libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangIncludeFixer.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangIncludeFixerPlugin.a"
    
    # Install API libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangInstallAPI.a"
    
    # Move libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangMove.a"
    
    # Pseudo libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangPseudo.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangPseudoCLI.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangPseudoCXX.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangPseudoGrammar.a"
    
    # Query libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangQuery.a"
    
    # Reorder fields libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangReorderFields.a"
    
    # Clangd libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangdMain.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangdSupport.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangdRemoteIndex.a"
    
    # Clang Tidy libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangTidy.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyUtils.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyMain.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyPlugin.a"
    
    # Clang Tidy module libraries
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyAbseilModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyAlteraModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyAndroidModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyBoostModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyBugproneModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyCERTModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyConcurrencyModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyCppCoreGuidelinesModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyDarwinModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyFuchsiaModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyGoogleModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyHICPPModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyLinuxKernelModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyLLVMLibcModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyLLVMModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyMiscModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyModernizeModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyMPIModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyObjCModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyOpenMPModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyPerformanceModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyPortabilityModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyReadabilityModule.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTidyZirconModule.a"
)

# Create the main library
add_library(SwiftJITREPL SHARED
    SwiftJITREPL.cpp
)

# Set target properties
target_compile_features(SwiftJITREPL PRIVATE cxx_std_17)

# Disable ABI breaking checks enforcement to avoid linking libSupport
# See: llvm-project/llvm/include/llvm/Config/abi-breaking.h.cmake
target_compile_definitions(SwiftJITREPL PRIVATE
    LLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING=1
)

# Link libraries - ensure proper order to resolve ABI breaking checks
# LLVM Support must come first to provide ABI breaking checks symbols
target_link_libraries(SwiftJITREPL PRIVATE
    ${LLVM_LIBRARIES}
    ${SWIFT_LIBRARIES}
    ${CLANG_LIBRARIES}
    # System libraries needed by LLVM and Swift
    z
    zstd
    uuid
    curses
    dl
    pthread
    rt
    
    # CommonMark libraries for markdown parsing
    "${SWIFT_BUILD_LLVM}/../cmark-linux-x86_64/src/libcmark-gfm.a"
    "${SWIFT_BUILD_LLVM}/../cmark-linux-x86_64/extensions/libcmark-gfm-extensions.a"
)

# Swift compiler modules are loaded at runtime by the Swift runtime system
# They don't need to be linked at build time

# Add Clang library directory to linker search paths
# This is needed because Swift libraries reference Clang libraries with -l flags
link_directories("${SWIFT_BUILD_LLVM}/lib")

# Add Swift compiler modules directory to linker search paths
# This is needed for Swift syntax and parser functionality
link_directories("${SWIFT_BUILD_SWIFT}/lib/swift/host")
link_directories("${SWIFT_BUILD_SWIFT}/lib/swift/host/compiler")

# Set runtime library path for Swift compiler modules
set(CMAKE_INSTALL_RPATH "${SWIFT_BUILD_SWIFT}/lib/swift/host:${SWIFT_BUILD_SWIFT}/lib/swift/host/compiler")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

# Create example executable
add_executable(swift_jit_example
    example.cpp
)

target_link_libraries(swift_jit_example PRIVATE
    SwiftJITREPL
    ${LLVM_LIBRARIES}
    ${SWIFT_LIBRARIES}
    ${CLANG_LIBRARIES}
    # System libraries needed by LLVM and Swift
    z
    zstd
    uuid
    curses
    dl
    pthread
    rt
    
    # CommonMark libraries for markdown parsing
    "${SWIFT_BUILD_LLVM}/../cmark-linux-x86_64/src/libcmark-gfm.a"
    "${SWIFT_BUILD_LLVM}/../cmark-linux-x86_64/extensions/libcmark-gfm-extensions.a"
)

# Set target properties
target_compile_features(swift_jit_example PRIVATE cxx_std_17)

# Disable ABI breaking checks enforcement to avoid linking libSupport
# See: llvm-project/llvm/include/llvm/Config/abi-breaking.h.cmake
target_compile_definitions(swift_jit_example PRIVATE
    LLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING=1
)

# Create test executable
add_executable(swift_jit_test
    test.cpp
)

target_link_libraries(swift_jit_test PRIVATE
    SwiftJITREPL
)

# Create server example executable
add_executable(swift_jit_server
    server_example.cpp
)

target_link_libraries(swift_jit_server PRIVATE
    SwiftJITREPL
)

# Set output directories
set_target_properties(swift_jit_example swift_jit_test swift_jit_server
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Install targets
install(TARGETS SwiftJITREPL
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES SwiftJITREPL.h
    DESTINATION include
)

# Print configuration info
message(STATUS "Swift source directory: ${SWIFT_SOURCE_DIR}")
message(STATUS "Swift build directory: ${SWIFT_BUILD_DIR}")
message(STATUS "Swift build swift: ${SWIFT_BUILD_SWIFT}")
message(STATUS "Swift build llvm: ${SWIFT_BUILD_LLVM}")
message(STATUS "Clang source directory: ${CLANG_SOURCE_DIR}")
message(STATUS "LLVM source directory: ${LLVM_SOURCE_DIR}")
message(STATUS "LLVM project directory: ${LLVM_PROJECT_DIR}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
