cmake_minimum_required(VERSION 3.16)
project(LLDB_CPP_Example)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# LLDB build directory
set(LLDB_BUILD_DIR "/home/jjerphan/dev/llvm-project/build-lldb-swift")

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

# Create executable
add_executable(test_lldb_cpp test_lldb_cpp.cpp)

# Link against LLDB libraries
target_link_libraries(test_lldb_cpp
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
