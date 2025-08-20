cmake_minimum_required(VERSION 3.16)
project(SwiftMinimalREPL)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find LLDB installation
# You may need to adjust these paths based on your LLDB build/installation
set(LLDB_BUILD_DIR "/home/jjerphan/dev/llvm-project/build-lldb-swift" CACHE PATH "Path to LLDB build directory")

# Include directories
include_directories(
    ${LLDB_BUILD_DIR}/include
    ${LLDB_BUILD_DIR}/tools/lldb/include
    /home/jjerphan/dev/llvm-project/lldb/include
    /home/jjerphan/dev/llvm-project/llvm/include
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

# Link against LLDB libraries
target_link_libraries(MinimalSwiftREPL
    ${LLDB_BUILD_DIR}/lib/liblldb.so.19.1.5
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
