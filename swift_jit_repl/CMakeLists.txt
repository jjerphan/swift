cmake_minimum_required(VERSION 3.16)
project(SwiftJITREPL)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# Swift library components (minimal set for JIT functionality)
set(SWIFT_LIBRARIES
    # Core JIT library
    "${SWIFT_BUILD_SWIFT}/lib/libswiftImmediate.a"
    # Core parsing and AST libraries
    "${SWIFT_BUILD_SWIFT}/lib/libswiftParse.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftAST.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftBasic.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftSema.a"
    "${SWIFT_BUILD_SWIFT}/lib/libswiftSIL.a"
)

# LLVM library components (minimal set)
set(LLVM_LIBRARIES
    # Only the most basic LLVM libraries
    "${SWIFT_BUILD_LLVM}/lib/libLLVMCore.a"
    "${SWIFT_BUILD_LLVM}/lib/libLLVMSupport.a"
)

# Clang library components (none for now - too complex)
set(CLANG_LIBRARIES
    # No Clang libraries for now - they have too many interdependencies
)

# Create the main library
add_library(SwiftJITREPL STATIC
    SwiftJITREPL.cpp
)

# Set target properties
target_compile_features(SwiftJITREPL PRIVATE cxx_std_17)

# Link libraries
target_link_libraries(SwiftJITREPL PRIVATE
    ${SWIFT_LIBRARIES}
    ${LLVM_LIBRARIES}
    ${CLANG_LIBRARIES}
    pthread
)

# Create example executable
add_executable(swift_jit_example
    example.cpp
)

target_link_libraries(swift_jit_example PRIVATE
    SwiftJITREPL
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
