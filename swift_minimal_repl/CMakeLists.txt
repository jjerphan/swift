cmake_minimum_required(VERSION 3.16)
project(SwiftMinimalREPL)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use the user's built LLDB with Swift support
set(LLDB_BUILD_DIR "/home/jjerphan/dev/build/Ninja-RelWithDebInfoAssert/lldb-linux-x86_64" CACHE PATH "Path to LLDB build directory with Swift support")

# Set RPATH settings to include Swift library directories
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "${LLDB_BUILD_DIR}/lib/lldb/swift/linux;${LLDB_BUILD_DIR}/lib/lldb/swift/linux/x86_64;${LLDB_BUILD_DIR}/lib")

# Include directories - use source headers from LLDB project since build doesn't have full API headers
include_directories(
    ${LLDB_BUILD_DIR}/include
    /home/jjerphan/dev/llvm-project/lldb/include
    /home/jjerphan/dev/llvm-project/llvm/include
    /home/jjerphan/dev/llvm-project/clang/include
)

# Link directories
link_directories(
    ${LLDB_BUILD_DIR}/lib
)

# Create the MinimalSwiftREPL library
add_library(MinimalSwiftREPL STATIC
    MinimalSwiftREPL.cpp
    MinimalSwiftREPL.h
)

# Set library properties
target_include_directories(MinimalSwiftREPL PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link against LLDB libraries with Swift support - simplified approach
target_link_libraries(MinimalSwiftREPL
    ${LLDB_BUILD_DIR}/lib/liblldb.so
    # System libraries
    pthread
    dl
    z
    xml2
    curses
    edit
    lzma
    rt
    m
    stdc++
)

# Create example executable
add_executable(swift_repl_example example.cpp)
target_link_libraries(swift_repl_example MinimalSwiftREPL)

# Create server example executable
add_executable(swift_repl_server server_example.cpp)
target_link_libraries(swift_repl_server MinimalSwiftREPL)

# Create test executable
add_executable(swift_repl_test test.cpp)
target_link_libraries(swift_repl_test MinimalSwiftREPL)

# Create simple test executable
add_executable(simple_test simple_test.cpp)
target_link_libraries(simple_test MinimalSwiftREPL)

# Set RPATH for all executables to include Swift library directories
set_target_properties(swift_repl_example swift_repl_server swift_repl_test simple_test PROPERTIES
    INSTALL_RPATH "${LLDB_BUILD_DIR}/lib/lldb/swift/linux;${LLDB_BUILD_DIR}/lib/lldb/swift/linux/x86_64;${LLDB_BUILD_DIR}/lib"
    BUILD_WITH_INSTALL_RPATH TRUE
)

# Optional: Enable additional warnings
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(MinimalSwiftREPL PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Installation (optional)
install(TARGETS MinimalSwiftREPL
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES MinimalSwiftREPL.h
    DESTINATION include
)
