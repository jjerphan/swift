cmake_minimum_required(VERSION 3.16)
project(SwiftJITREPL)

# Use clang and clang++ as preferred compilers
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)

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

# Use Swift's component-based system instead of static library paths
# This automatically handles dependencies and linking order
set(Swift_DIR "${SWIFT_BUILD_SWIFT}/lib/cmake/swift")
find_package(Threads REQUIRED)
find_package(Swift REQUIRED CONFIG)

# Swift library components for JIT functionality
# These are CMake targets that automatically resolve dependencies
set(SWIFT_LIBRARIES
    # Core JIT library - this is the main library we need
    swiftImmediate
    
    # Basic Swift infrastructure
    swiftBasic
    swiftParse
    swiftAST
    swiftSema
    
    # SIL (Swift Intermediate Language) - needed for compilation
    swiftSIL
    swiftSILGen
    
    # IR Generation - needed for JIT
    swiftIRGen
    
    # Frontend - needed for compiler instance
    swiftFrontend
    
    # Demangling - needed for symbol resolution
    swiftDemangling
    
    # Options - needed for compiler configuration
    swiftOption
)

# Use LLVM's built-in component resolution - this is the correct approach
# It automatically handles dependency ordering and resolves circular dependencies
find_package(LLVM REQUIRED CONFIG)

# Define ALL LLVM components needed for Swift JIT
# Since Swift JIT calls InitializeAllTargets(), we need ALL target components
set(LLVM_COMPONENTS
    # Core components (base dependencies)
    Core
    Support
    Demangle
    BinaryFormat
    TargetParser
    
    # MC (Machine Code) components
    MC
    MCParser
    MCDisassembler
    Object
    
    # ALL Target components - Swift JIT calls InitializeAllTargets()
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
    
    SystemZDesc
    SystemZInfo
    SystemZAsmParser
    SystemZCodeGen
    SystemZDisassembler
    
    MipsDesc
    MipsInfo
    MipsAsmParser
    MipsCodeGen
    MipsDisassembler
    
    RISCVDesc
    RISCVInfo
    RISCVAsmParser
    RISCVCodeGen
    RISCVDisassembler
    
    WebAssemblyDesc
    WebAssemblyInfo
    WebAssemblyAsmParser
    WebAssemblyCodeGen
    WebAssemblyDisassembler
    
    AVRDesc
    AVRInfo
    AVRAsmParser
    AVRCodeGen
    AVRDisassembler
    
    # X86 target components (our primary target)
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
)

# Let LLVM resolve the dependencies and provide the correct library list
# This automatically handles the complex dependency ordering
llvm_map_components_to_libnames(LLVM_LIBRARIES ${LLVM_COMPONENTS})

# Debug: Print the resolved libraries
message(STATUS "LLVM_LIBRARIES resolved to: ${LLVM_LIBRARIES}")

# Clang library components (minimal set needed by Swift)
# Note: For basic Swift JIT functionality, we may not need Clang at all
# Swift can work without Clang for simple JIT operations
set(CLANG_LIBRARIES
    # Temporarily disable Clang libraries to test if Swift JIT works without them
    # "${SWIFT_BUILD_LLVM}/lib/libclangBasic.a"
    # "${SWIFT_BUILD_LLVM}/lib/libclangLex.a"
    # "${SWIFT_BUILD_LLVM}/lib/libclangParse.a"
    # "${SWIFT_BUILD_LLVM}/lib/libclangAST.a"
    # "${SWIFT_BUILD_LLVM}/lib/libclangSema.a"
    # "${SWIFT_BUILD_LLVM}/lib/libclangSerialization.a"
    # "${SWIFT_BUILD_LLVM}/lib/libclangFrontend.a"
    # "${SWIFT_BUILD_LLVM}/lib/libclangDriver.a"
)

# Create the main library
add_library(SwiftJITREPL STATIC
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
target_link_libraries(SwiftJITREPL PRIVATE
    ${SWIFT_LIBRARIES}
    ${CLANG_LIBRARIES}
    ${LLVM_LIBRARIES}
    pthread
)

# Create example executable
add_executable(swift_jit_example
    example.cpp
)

target_link_libraries(swift_jit_example PRIVATE
    SwiftJITREPL
    ${SWIFT_LIBRARIES}
    ${CLANG_LIBRARIES}
    ${LLVM_LIBRARIES}
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
