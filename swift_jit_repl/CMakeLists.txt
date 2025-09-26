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
set(LLVM_PROJECT_DIR "${DEV_FOLDER}/llvm-project")

# Define Swift search paths as compile-time definitions
set(SWIFT_RUNTIME_LIBRARY_PATHS "${SWIFT_BUILD_SWIFT}/lib/swift/linux")
set(SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1 "${SWIFT_BUILD_SWIFT}/lib/swift/linux")
set(SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2 "${SWIFT_BUILD_SWIFT}/lib/swift/linux/x86_64")
set(SWIFT_RUNTIME_RESOURCE_PATH "${SWIFT_BUILD_SWIFT}/lib/swift")
set(SWIFT_SDK_PATH "${SWIFT_BUILD_SWIFT}")

# Add compile definitions for Swift search paths
add_compile_definitions(
    SWIFT_RUNTIME_LIBRARY_PATHS="${SWIFT_RUNTIME_LIBRARY_PATHS}"
    SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1="${SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1}"
    SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2="${SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2}"
    SWIFT_RUNTIME_RESOURCE_PATH="${SWIFT_RUNTIME_RESOURCE_PATH}"
    SWIFT_SDK_PATH="${SWIFT_SDK_PATH}"
)

# Find Swift compiler in the built Swift directory
find_program(SWIFT_COMPILER
    NAMES swiftc
    PATHS "${SWIFT_BUILD_SWIFT}/bin"
    NO_DEFAULT_PATH
)

if(NOT SWIFT_COMPILER)
    message(FATAL_ERROR "swiftc not found at ${SWIFT_BUILD_SWIFT}/bin")
endif()

# Get Swift target info using `swiftc -print-target-info` in the built Swift directory
# This command returns JSON with Swift build configuration including:
# - Target architecture and platform information
# - Paths to Swift runtime libraries, import paths, and resources
# - Compiler version and capabilities
    execute_process(
    COMMAND ${SWIFT_COMPILER} -print-target-info
    OUTPUT_VARIABLE SWIFT_TARGET_INFO_RAW
        OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE SWIFT_TARGET_INFO_RESULT
)

if(NOT SWIFT_TARGET_INFO_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to get Swift target info: ${SWIFT_TARGET_INFO_RESULT}")
endif()

# Parse Swift target info JSON using a simple approach
# Write the JSON to a temporary file and use Python to parse it
file(WRITE "${CMAKE_BINARY_DIR}/swift_target_info.json" "${SWIFT_TARGET_INFO_RAW}")

# Use Python to parse the JSON and extract paths
# Swift paths explained:
# - `runtimeLibraryPaths`: Where Swift runtime libraries (.so/.a files) are located
#   These are the actual compiled libraries that contain Swift runtime code
#   Example: `/path/to/swift/lib/swift/linux`
#
# - `runtimeLibraryImportPaths`: Where Swift modules can be imported from
#   These are the search paths for Swift module imports (import statements)
#   Used by the Swift compiler to find module definitions
#   Example: `/path/to/swift/lib/swift/linux` and `/path/to/swift/lib/swift/linux/x86_64`
#
# - `runtimeResourcePath`: Where Swift resources and metadata are stored
#   Contains Swift runtime resources, metadata, and other non-code assets
#   Example: `/path/to/swift/lib/swift`
    execute_process(
    COMMAND python3 -c "
import json
import sys
try:
    with open('${CMAKE_BINARY_DIR}/swift_target_info.json', 'r') as f:
        data = json.load(f)
    paths = data.get('paths', {})
    runtime_paths = paths.get('runtimeLibraryPaths', [])
    import_paths = paths.get('runtimeLibraryImportPaths', [])
    resource_path = paths.get('runtimeResourcePath', '')

    print('RUNTIME_PATHS=' + ';'.join(runtime_paths))
    print('IMPORT_PATHS=' + ';'.join(import_paths))
    print('RESOURCE_PATH=' + resource_path)
except Exception as e:
    print('ERROR=' + str(e), file=sys.stderr)
    sys.exit(1)
"
    OUTPUT_VARIABLE SWIFT_PATHS_OUTPUT
    RESULT_VARIABLE SWIFT_PATHS_RESULT
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(SWIFT_PATHS_RESULT EQUAL 0)
    # Parse the output - extract each line separately
    string(REGEX MATCH "RUNTIME_PATHS=([^\n\r]*)" SWIFT_RUNTIME_MATCH "${SWIFT_PATHS_OUTPUT}")
    string(REGEX MATCH "IMPORT_PATHS=([^\n\r]*)" SWIFT_IMPORT_MATCH "${SWIFT_PATHS_OUTPUT}")
    string(REGEX MATCH "RESOURCE_PATH=([^\n\r]*)" SWIFT_RESOURCE_MATCH "${SWIFT_PATHS_OUTPUT}")

    if(SWIFT_RUNTIME_MATCH)
        string(REGEX REPLACE ".*RUNTIME_PATHS=([^\n\r]*).*" "\\1" SWIFT_RUNTIME_PATHS "${SWIFT_PATHS_OUTPUT}")
        message(STATUS "Swift runtime library paths: ${SWIFT_RUNTIME_PATHS}")
    else()
        set(SWIFT_RUNTIME_PATHS "${SWIFT_BUILD_SWIFT}/lib/swift/linux")
    endif()

    if(SWIFT_IMPORT_MATCH)
        string(REGEX REPLACE ".*IMPORT_PATHS=([^\n\r]*).*" "\\1" SWIFT_IMPORT_PATHS "${SWIFT_PATHS_OUTPUT}")
        message(STATUS "Swift import paths: ${SWIFT_IMPORT_PATHS}")
    else()
        set(SWIFT_IMPORT_PATHS "${SWIFT_BUILD_SWIFT}/lib/swift/linux" "${SWIFT_BUILD_SWIFT}/lib/swift/linux/x86_64")
    endif()

    if(SWIFT_RESOURCE_MATCH)
        string(REGEX REPLACE ".*RESOURCE_PATH=([^\n\r]*).*" "\\1" SWIFT_RESOURCE_PATH "${SWIFT_PATHS_OUTPUT}")
        message(STATUS "Swift resource path: ${SWIFT_RESOURCE_PATH}")
    else()
        set(SWIFT_RESOURCE_PATH "${SWIFT_BUILD_SWIFT}/lib/swift")
    endif()
else()
    message(WARNING "Failed to parse Swift target info with Python, using fallback paths")
    set(SWIFT_RUNTIME_PATHS "${SWIFT_BUILD_SWIFT}/lib/swift/linux")
    set(SWIFT_IMPORT_PATHS "${SWIFT_BUILD_SWIFT}/lib/swift/linux" "${SWIFT_BUILD_SWIFT}/lib/swift/linux/x86_64")
    set(SWIFT_RESOURCE_PATH "${SWIFT_BUILD_SWIFT}/lib/swift")
endif()

# Clean up temporary file
file(REMOVE "${CMAKE_BINARY_DIR}/swift_target_info.json")

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

    # Dynamically discovered Swift import paths
    # These are the search paths where Swift modules can be imported from.
    # Used by the Swift compiler to resolve 'import' statements in Swift code.
    # `SWIFT_IMPORT_PATHS` contains both base and architecture-specific paths.
    ${SWIFT_IMPORT_PATHS}
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

# Swift runtime libraries - using dynamically discovered paths
# These are the actual compiled Swift runtime libraries (.a/.so files) that contain
# the Swift runtime implementation. They are linked into the final executable.
# `SWIFT_RUNTIME_PATHS` points to the base directory where these libraries are stored.
set(SWIFT_RUNTIME_LIBRARIES
    "${SWIFT_RUNTIME_PATHS}/x86_64/libswiftCommandLineSupport.a"
    "${SWIFT_RUNTIME_PATHS}/x86_64/libswiftCxx.a"
    "${SWIFT_RUNTIME_PATHS}/x86_64/libswiftCxxStdlib.a"
    "${SWIFT_RUNTIME_PATHS}/x86_64/libswiftRemoteInspection.a"
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
    # Swift Core runtime library - dynamically discovered path
    # This is the main Swift runtime library that provides core Swift functionality.
    # SWIFT_RUNTIME_PATHS points to the base directory where runtime libraries are stored.
    "${SWIFT_RUNTIME_PATHS}/x86_64/libswiftCore.so"
    # Swift Syntax libraries - required for macro evaluation
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
    # Additional Swift compiler libraries
    "${SWIFT_BUILD_SWIFT}/lib/libswiftSema.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftAST.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftSIL.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftSILGen.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftIRGen.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftDriver.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftIDEUtilsBridging.a"
)

# Clang libraries (only the ones that actually exist)
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
list(APPEND SWIFT_SYSTEM_LIBRARIES "uuid")

# Create the main library
add_library(SwiftJITREPL SHARED
    SwiftJITREPL.cpp
    SwiftJITREPL.h
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

# Link libraries
target_link_libraries(SwiftJITREPL PRIVATE
    ${SWIFT_LIBRARIES}
    ${SWIFT_RUNTIME_LIBRARIES}
    ${CLANG_LIBRARIES}
    ${LLVM_LIBRARIES}
    ${LLVM_LDFLAGS}
    ${SWIFT_SYSTEM_LIBRARIES}
    ${ADDITIONAL_LIBRARIES}
)

# Create example executable
add_executable(swift_jit_example
    example.cpp
)

target_include_directories(swift_jit_example PRIVATE
    ${SWIFT_SOURCE_DIR}/include
    ${SWIFT_BUILD_LLVM}/include
    ${SWIFT_BUILD_LLVM}/tools/clang/include
    ${CLANG_SOURCE_DIR}/include
    ${LLVM_SOURCE_DIR}/include
    ${SWIFT_SOURCE_DIR}/stdlib/public/SwiftShims
)

# Add LLVM C++ flags but override exception handling
target_compile_options(swift_jit_example PRIVATE ${LLVM_CXXFLAGS})
# Override LLVM's -fno-exceptions to allow exceptions in our code
target_compile_options(swift_jit_example PRIVATE -fexceptions)
# Add Swift-specific compiler definitions
target_compile_definitions(swift_jit_example PRIVATE
    SWIFT_TARGET_LIBRARY_NAME=swift
    SWIFT_TARGET_LIBRARY_VERSION=1
    __STDC_LIMIT_MACROS
    __STDC_CONSTANT_MACROS
)

target_link_libraries(swift_jit_example PRIVATE
    SwiftJITREPL
    ${SWIFT_LIBRARIES}
    ${SWIFT_RUNTIME_LIBRARIES}
    ${CLANG_LIBRARIES}
    ${LLVM_LIBRARIES}
    ${LLVM_LDFLAGS}
    ${SWIFT_SYSTEM_LIBRARIES}
    ${ADDITIONAL_LIBRARIES}
)

# Create test executable
add_executable(swift_jit_test
    test.cpp
)

target_include_directories(swift_jit_test PRIVATE
    ${SWIFT_SOURCE_DIR}/include
    ${SWIFT_BUILD_LLVM}/include
    ${SWIFT_BUILD_LLVM}/tools/clang/include
    ${CLANG_SOURCE_DIR}/include
    ${LLVM_SOURCE_DIR}/include
    ${SWIFT_SOURCE_DIR}/stdlib/public/SwiftShims
)

# Add LLVM C++ flags but override exception handling
target_compile_options(swift_jit_test PRIVATE ${LLVM_CXXFLAGS})
# Override LLVM's -fno-exceptions to allow exceptions in our code
target_compile_options(swift_jit_test PRIVATE -fexceptions)
# Add Swift-specific compiler definitions
target_compile_definitions(swift_jit_test PRIVATE
    SWIFT_TARGET_LIBRARY_NAME=swift
    SWIFT_TARGET_LIBRARY_VERSION=1
    __STDC_LIMIT_MACROS
    __STDC_CONSTANT_MACROS
)

target_link_libraries(swift_jit_test PRIVATE
    SwiftJITREPL
    ${SWIFT_LIBRARIES}
    ${SWIFT_RUNTIME_LIBRARIES}
    ${CLANG_LIBRARIES}
    ${LLVM_LIBRARIES}
    ${LLVM_LDFLAGS}
    ${SWIFT_SYSTEM_LIBRARIES}
    ${ADDITIONAL_LIBRARIES}
)

# Set output directories
set_target_properties(SwiftJITREPL PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

set_target_properties(swift_jit_example swift_jit_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Install targets
install(TARGETS SwiftJITREPL
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(TARGETS swift_jit_example swift_jit_test
    RUNTIME DESTINATION bin
)

# Install headers
install(FILES src/SwiftJITREPL.h
    DESTINATION include
)
