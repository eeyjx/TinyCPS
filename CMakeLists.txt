# Build the educational C++20 compiler. The LLVM backend writes text IR directly.
# No LLVM C++ libraries are required at *compiler build time*.
# The native driver calls Clang to lower LLVM IR to object code and link it.
cmake_minimum_required(VERSION 3.16)
project(TinyCPS VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(tinycps src/main.cpp src/lexer.cpp src/parser.cpp src/semantic.cpp src/codegen.cpp src/llvm_codegen.cpp)
install(TARGETS tinycps RUNTIME DESTINATION bin)

target_compile_options(tinycps PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)

# CTest launches the Python end-to-end suite if an interpreter is available.
# The suite also invokes a system C compiler (cc, or CC environment override).
enable_testing()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME compiler_integration COMMAND
        ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_compiler.py
        $<TARGET_FILE:tinycps> ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Verify native executables generated directly from TinyCPS LLVM IR.
if(Python3_Interpreter_FOUND)
    add_test(NAME native_backend_integration COMMAND
        ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native.py
        $<TARGET_FILE:tinycps> ${CMAKE_CURRENT_SOURCE_DIR})
endif()
