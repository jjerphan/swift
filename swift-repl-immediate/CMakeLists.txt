cmake_minimum_required(VERSION 3.16)
project(SwiftJITREPL)


# Clang and Clang++ have to be used because Swift uses
# Clang's frontend to compile Swift code.
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set build type to Release with Debug Info because Swift has been built with RelWithDebInfo
set(CMAKE_BUILD_TYPE RelWithDebInfo)

# Ensure RelWithDebInfo flags
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")

# Set Swift compiler paths - These paths are hardcoded because Swift is not built with CMake
# and the paths are not exported by the Swift build system.
# Get home directory dynamically from environment
if(DEFINED ENV{HOME})
    set(HOME_DIR "$ENV{HOME}")
elseif(DEFINED ENV{USERPROFILE})
    set(HOME_DIR "$ENV{USERPROFILE}")
else()
    message(FATAL_ERROR "Could not determine home directory. Please set HOME_DIR manually.")
endif()
set(DEV_FOLDER "${HOME_DIR}/dev")
set(SWIFT_SOURCE_DIR "${DEV_FOLDER}/swift")
set(SWIFT_BUILD_DIR "${DEV_FOLDER}/build/Ninja-RelWithDebInfoAssert")
set(SWIFT_BUILD_SWIFT "${SWIFT_BUILD_DIR}/swift-linux-x86_64")
set(SWIFT_BUILD_LLVM "${SWIFT_BUILD_DIR}/llvm-linux-x86_64")
set(CLANG_SOURCE_DIR "${DEV_FOLDER}/llvm-project/clang")
set(LLVM_SOURCE_DIR "${DEV_FOLDER}/llvm-project/llvm")

# Set Swift library paths after building
set(SWIFT_LIB_DIR "${SWIFT_BUILD_SWIFT}/lib/swift/host")
set(SWIFT_STATIC_LIB_DIR "${SWIFT_BUILD_SWIFT}/lib/swift/host/static")
set(LLVM_PROJECT_DIR "${DEV_FOLDER}/llvm-project")

# Define Swift search paths as compile-time definitions
set(SWIFT_RUNTIME_LIBRARY_PATHS "${SWIFT_BUILD_SWIFT}/lib/swift/linux")
set(SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1 "${SWIFT_BUILD_SWIFT}/lib/swift/linux")
set(SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2 "${SWIFT_BUILD_SWIFT}/lib/swift/linux/x86_64")
set(SWIFT_RUNTIME_RESOURCE_PATH "${SWIFT_BUILD_SWIFT}/lib/swift")
set(SWIFT_SDK_PATH "${SWIFT_BUILD_SWIFT}")

# Define Foundation module paths
set(FOUNDATION_MODULE_PATH "${SWIFT_BUILD_DIR}/foundation-linux-x86_64/swift")
set(FOUNDATION_STATIC_MODULE_PATH "${SWIFT_BUILD_DIR}/foundation_static-linux-x86_64/swift")
set(DISPATCH_MODULE_PATH "${SWIFT_BUILD_DIR}/libdispatch-linux-x86_64/src/swift/swift")
set(DISPATCH_STATIC_MODULE_PATH "${SWIFT_BUILD_DIR}/libdispatch_static-linux-x86_64/src/swift/swift")

# Detect target triple at compile time
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(TARGET_ARCH "x86_64")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(TARGET_ARCH "aarch64")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7")
    set(TARGET_ARCH "arm")
else()
    set(TARGET_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(TARGET_OS "unknown-linux-gnu")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(TARGET_OS "apple-darwin")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(TARGET_OS "windows-msvc")
else()
    set(TARGET_OS "${CMAKE_SYSTEM_NAME}")
endif()

set(SWIFT_TARGET_TRIPLE "${TARGET_ARCH}-${TARGET_OS}")

message(STATUS "Detected Swift target triple: ${SWIFT_TARGET_TRIPLE}")

# Validate Swift runtime paths at build time
message(STATUS "Validating Swift runtime paths...")

# Check Swift runtime paths
if(NOT EXISTS "${SWIFT_RUNTIME_LIBRARY_PATHS}")
    message(FATAL_ERROR "Swift runtime library path does not exist: ${SWIFT_RUNTIME_LIBRARY_PATHS}")
endif()

if(NOT EXISTS "${SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1}")
    message(FATAL_ERROR "Swift runtime library import path 1 does not exist: ${SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1}")
endif()

if(NOT EXISTS "${SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2}")
    message(FATAL_ERROR "Swift runtime library import path 2 does not exist: ${SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2}")
endif()

if(NOT EXISTS "${SWIFT_RUNTIME_RESOURCE_PATH}")
    message(FATAL_ERROR "Swift runtime resource path does not exist: ${SWIFT_RUNTIME_RESOURCE_PATH}")
endif()

if(NOT EXISTS "${SWIFT_SDK_PATH}")
    message(FATAL_ERROR "Swift SDK path does not exist: ${SWIFT_SDK_PATH}")
endif()

# Check Foundation module paths (optional - don't fail if not found)
if(EXISTS "${FOUNDATION_MODULE_PATH}")
    message(STATUS "Foundation module path found: ${FOUNDATION_MODULE_PATH}")
else()
    message(STATUS "Warning: Foundation module path does not exist: ${FOUNDATION_MODULE_PATH}")
endif()

if(EXISTS "${FOUNDATION_STATIC_MODULE_PATH}")
    message(STATUS "Foundation static module path found: ${FOUNDATION_STATIC_MODULE_PATH}")
else()
    message(STATUS "Warning: Foundation static module path does not exist: ${FOUNDATION_STATIC_MODULE_PATH}")
endif()

# Check Dispatch module paths (optional - don't fail if not found)
if(EXISTS "${DISPATCH_MODULE_PATH}")
    message(STATUS "Dispatch module path found: ${DISPATCH_MODULE_PATH}")
else()
    message(STATUS "Warning: Dispatch module path does not exist: ${DISPATCH_MODULE_PATH}")
endif()

if(EXISTS "${DISPATCH_STATIC_MODULE_PATH}")
    message(STATUS "Dispatch static module path found: ${DISPATCH_STATIC_MODULE_PATH}")
else()
    message(STATUS "Warning: Dispatch static module path does not exist: ${DISPATCH_STATIC_MODULE_PATH}")
endif()

message(STATUS "All Swift runtime paths validated successfully")

# Add compile definitions for Swift search paths
add_compile_definitions(
    SWIFT_RUNTIME_LIBRARY_PATHS="${SWIFT_RUNTIME_LIBRARY_PATHS}"
    SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1="${SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1}"
    SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2="${SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2}"
    SWIFT_RUNTIME_RESOURCE_PATH="${SWIFT_RUNTIME_RESOURCE_PATH}"
    SWIFT_SDK_PATH="${SWIFT_SDK_PATH}"
    FOUNDATION_MODULE_PATH="${FOUNDATION_MODULE_PATH}"
    FOUNDATION_STATIC_MODULE_PATH="${FOUNDATION_STATIC_MODULE_PATH}"
    DISPATCH_MODULE_PATH="${DISPATCH_MODULE_PATH}"
    DISPATCH_STATIC_MODULE_PATH="${DISPATCH_STATIC_MODULE_PATH}"
    SWIFT_TARGET_TRIPLE="${SWIFT_TARGET_TRIPLE}"
)

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

# Swift compiler libraries - using the correct paths
set(SWIFT_LIBRARIES
    # Core Swift libraries
    "${SWIFT_BUILD_SWIFT}/lib/libswiftAST.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftBasic.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftSema.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftFrontend.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftDriver.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftParse.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftSIL.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftSILOptimizer.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftSerialization.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftIRGen.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftClangImporter.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftDemangling.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftRemoteAST.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftImmediate.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftSILGen.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftASTGen.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftMacroEvaluation.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftIDEUtilsBridging.a"
    "${SWIFT_BUILD_SWIFT}/lib/lib_CompilerRegexParser.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftConstExtract.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftDependencyScan.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftDependencyScanImpl.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftDriverTool.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftFrontendTool.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftIDE.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftIDETool.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftIndex.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftLLVMPasses.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftLocalization.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftMarkup.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftMigrator.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftOption.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftPrintAsClang.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftRefactoring.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftRemoteInspection.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftRemoteMirror.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftStaticMirror.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftSymbolGraphGen.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftThreading.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftAPIDigester.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftASTSectionImporter.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftCompilerModules.a"
)

# Swift runtime libraries
set(SWIFT_RUNTIME_LIBRARIES
    "${SWIFT_RUNTIME_LIBRARY_PATHS}/x86_64/libswiftCommandLineSupport.a"
    "${SWIFT_RUNTIME_LIBRARY_PATHS}/x86_64/libswiftCxx.a"
    "${SWIFT_RUNTIME_LIBRARY_PATHS}/x86_64/libswiftCxxStdlib.a"
    "${SWIFT_RUNTIME_LIBRARY_PATHS}/x86_64/libswiftRemoteInspection.a"
    "${SWIFT_BUILD_SWIFT}/lib/swift/embedded/x86_64-unknown-linux-gnu/libswift_Concurrency.a"
    "${SWIFT_BUILD_SWIFT}/lib/swift/embedded/x86_64-unknown-linux-gnu/libswift_ConcurrencyDefaultExecutor.a"
    "${SWIFT_BUILD_SWIFT}/lib/swift/embedded/x86_64-unknown-linux-gnu/libswiftUnicodeDataTables.a"
)

# Additional required libraries
set(ADDITIONAL_LIBRARIES
    "${SWIFT_BUILD_DIR}/cmark-linux-x86_64/src/libcmark-gfm.a"
    "${SWIFT_BUILD_DIR}/cmark-linux-x86_64/extensions/libcmark-gfm-extensions.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangDependencyScanning.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangRewriteFrontend.a"
    "${SWIFT_BUILD_DIR}/foundation_static-linux-x86_64/lib/libFoundationEssentials.a"
    "${SWIFT_BUILD_DIR}/foundation_static-linux-x86_64/lib/libFoundationInternationalization.a"
    "${SWIFT_BUILD_DIR}/foundation_static-linux-x86_64/lib/libFoundation.a"
    "${SWIFT_BUILD_DIR}/foundation_static-linux-x86_64/lib/lib_FoundationCollections.a"
    "${SWIFT_BUILD_DIR}/libdispatch_static-linux-x86_64/src/swift/libswiftDispatch.a"
    "${SWIFT_RUNTIME_LIBRARY_PATHS}/x86_64/libswiftCore.so"
    "${SWIFT_BUILD_SWIFT}/lib/swift/host/compiler/lib_CompilerSwiftSyntax.so"
    "${SWIFT_BUILD_SWIFT}/lib/swift/host/compiler/lib_CompilerSwiftSyntaxBuilder.so"
    "${SWIFT_BUILD_SWIFT}/lib/swift/host/compiler/lib_CompilerSwiftSyntaxMacros.so"
    "${SWIFT_BUILD_SWIFT}/lib/swift/host/compiler/lib_CompilerSwiftSyntaxMacroExpansion.so"
    "${SWIFT_BUILD_SWIFT}/lib/swift/host/compiler/lib_CompilerSwiftDiagnostics.so"
    "${SWIFT_BUILD_SWIFT}/lib/swift/host/compiler/lib_CompilerSwiftIDEUtils.so"
    "${SWIFT_BUILD_SWIFT}/lib/swift/host/compiler/lib_CompilerSwiftParserDiagnostics.so"
    "${SWIFT_BUILD_SWIFT}/lib/swift/host/compiler/lib_CompilerSwiftBasicFormat.so"
    "${SWIFT_BUILD_SWIFT}/lib/swift/host/compiler/lib_CompilerSwiftParser.so"
    "${SWIFT_BUILD_SWIFT}/lib/swift/host/compiler/lib_CompilerSwiftIfConfig.so"
    "${SWIFT_BUILD_SWIFT}/lib/swift/host/compiler/lib_CompilerSwiftLexicalLookup.so"
    "${SWIFT_BUILD_SWIFT}/lib/swift/host/compiler/lib_CompilerSwiftCompilerPluginMessageHandling.so"
)

# Clang libraries
set(CLANG_LIBRARIES
    "${SWIFT_BUILD_LLVM}/lib/libclangBasic.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangLex.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangParse.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangAST.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangSema.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangSerialization.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangFrontend.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangDriver.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangSupport.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangCodeGen.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangAnalysis.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangARCMigrate.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangASTMatchers.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangCrossTU.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangDirectoryWatcher.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangEdit.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangExtractAPI.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangFrontendTool.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangIndex.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangInstallAPI.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangInterpreter.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangRewrite.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangStaticAnalyzerCheckers.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangStaticAnalyzerCore.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangStaticAnalyzerFrontend.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTooling.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangToolingCore.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangToolingInclusions.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangToolingRefactor.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangToolingSyntax.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangTransformer.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangCAS.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangAPINotes.a"
    "${SWIFT_BUILD_LLVM}/lib/libclangFormat.a"
)

# LLVM libraries using llvm-config
find_program(LLVM_CONFIG_EXECUTABLE
    NAMES llvm-config
    PATHS "${SWIFT_BUILD_LLVM}/bin"
    NO_DEFAULT_PATH
)

if(NOT LLVM_CONFIG_EXECUTABLE)
    message(FATAL_ERROR "llvm-config not found")
endif()

# Get LLVM libraries and flags using llvm-config
execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --libfiles
    OUTPUT_VARIABLE LLVM_LIBRARIES_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --cxxflags
    OUTPUT_VARIABLE LLVM_CXXFLAGS_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --ldflags
    OUTPUT_VARIABLE LLVM_LDFLAGS_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --system-libs
    OUTPUT_VARIABLE SWIFT_SYSTEM_LIBRARIES_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Convert to lists
string(REPLACE " " ";" LLVM_LIBRARIES "${LLVM_LIBRARIES_RAW}")
string(REPLACE " " ";" LLVM_CXXFLAGS "${LLVM_CXXFLAGS_RAW}")
string(REPLACE " " ";" LLVM_LDFLAGS "${LLVM_LDFLAGS_RAW}")
string(REPLACE " " ";" SWIFT_SYSTEM_LIBRARIES "${SWIFT_SYSTEM_LIBRARIES_RAW}")

# Add additional system libraries
list(APPEND SWIFT_SYSTEM_LIBRARIES "uuid" "edit" "pthread" "dl" "m" "c" "zstd" "z")

# Create the main library
# Create the SwiftJITREPL library
add_library(SwiftJITREPL SHARED
    SwiftJITREPL.cpp
)

# Set properties
target_include_directories(SwiftJITREPL PRIVATE
    ${SWIFT_SOURCE_DIR}/include
    ${SWIFT_BUILD_LLVM}/include
    ${SWIFT_BUILD_LLVM}/tools/clang/include
    ${CLANG_SOURCE_DIR}/include
    ${LLVM_SOURCE_DIR}/include
    ${SWIFT_SOURCE_DIR}/stdlib/public/SwiftShims
)

# Add LLVM C++ flags but override exception handling
target_compile_options(SwiftJITREPL PRIVATE ${LLVM_CXXFLAGS})
# Override LLVM's -fno-exceptions to allow exceptions in our code
target_compile_options(SwiftJITREPL PRIVATE -fexceptions)
# Add Swift-specific compiler definitions
target_compile_definitions(SwiftJITREPL PRIVATE
    SWIFT_TARGET_LIBRARY_NAME=swift
    SWIFT_TARGET_LIBRARY_VERSION=1
    __STDC_LIMIT_MACROS
    __STDC_CONSTANT_MACROS
    SWIFT_JIT_REPL_EXPORTS
)

# Link libraries - Use comprehensive Swift, Clang, LLVM and system libs
target_link_libraries(SwiftJITREPL PRIVATE
    -Wl,--start-group
    ${SWIFT_LIBRARIES}
    ${CLANG_LIBRARIES}
    ${ADDITIONAL_LIBRARIES}
    ${SWIFT_RUNTIME_LIBRARIES}
    ${LLVM_LIBRARIES}
    -Wl,--end-group
    ${LLVM_LDFLAGS}
    ${SWIFT_SYSTEM_LIBRARIES}
)

# Create the test executable
add_executable(test_sil_jit
    test_sil_jit.cpp
)

target_include_directories(test_sil_jit PRIVATE
    ${SWIFT_SOURCE_DIR}/include
    ${SWIFT_BUILD_LLVM}/include
    ${SWIFT_BUILD_LLVM}/tools/clang/include
    ${CLANG_SOURCE_DIR}/include
    ${LLVM_SOURCE_DIR}/include
    ${SWIFT_SOURCE_DIR}/stdlib/public/SwiftShims
    /usr/include/llvm-gtest
)

# Add LLVM C++ flags but override exception handling for the test, too
target_compile_options(test_sil_jit PRIVATE ${LLVM_CXXFLAGS})
target_compile_options(test_sil_jit PRIVATE -fexceptions)

target_link_libraries(test_sil_jit PRIVATE
    SwiftJITREPL
    ${SWIFT_LIBRARIES}
    ${SWIFT_RUNTIME_LIBRARIES}
    ${CLANG_LIBRARIES}
    ${LLVM_LIBRARIES}
    ${LLVM_LDFLAGS}
    ${SWIFT_SYSTEM_LIBRARIES}
    ${ADDITIONAL_LIBRARIES}
    llvm_gtest
    llvm_gtest_main
)

# Set output directories
set_target_properties(SwiftJITREPL PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

set_target_properties(test_sil_jit PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)