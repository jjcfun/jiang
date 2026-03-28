cmake_minimum_required(VERSION 3.10)
project(JiangCompiler C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# Enable common warnings to keep compiler code clean
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter")

option(JIANG_ENABLE_LLVM_C_API "Enable experimental LLVM C API backend path" ON)

# Include directories
include_directories(include)

# Source files
set(SOURCES
    src/main.c
    src/lexer.c
    src/arena.c
    src/ast.c
    src/parser.c
    src/generator.c
    src/semantic.c
    src/jir.c
    src/hir.c
    src/lower.c
    src/jir_gen.c
    src/llvm_gen.c
)

# Output executable
add_executable(jiangc ${SOURCES})

if (JIANG_ENABLE_LLVM_C_API)
    find_program(LLVM_CONFIG_EXECUTABLE NAMES llvm-config)
    if (LLVM_CONFIG_EXECUTABLE)
        execute_process(
            COMMAND ${LLVM_CONFIG_EXECUTABLE} --includedir
            OUTPUT_VARIABLE LLVM_INCLUDE_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        execute_process(
            COMMAND ${LLVM_CONFIG_EXECUTABLE} --libdir
            OUTPUT_VARIABLE LLVM_LIB_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        execute_process(
            COMMAND ${LLVM_CONFIG_EXECUTABLE} --libnames core analysis
            OUTPUT_VARIABLE LLVM_LIBNAMES
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if (EXISTS "${LLVM_INCLUDE_DIR}/llvm-c/Core.h")
            target_compile_definitions(jiangc PRIVATE JIANG_HAVE_LLVM_C_API=1)
            target_include_directories(jiangc PRIVATE "${LLVM_INCLUDE_DIR}")
            separate_arguments(LLVM_LIBNAME_LIST NATIVE_COMMAND "${LLVM_LIBNAMES}")
            foreach(libname IN LISTS LLVM_LIBNAME_LIST)
                target_link_libraries(jiangc PRIVATE "${LLVM_LIB_DIR}/${libname}")
            endforeach()
            message(STATUS "Enabled experimental LLVM C API backend")
        else()
            message(STATUS "llvm-config found, but llvm-c headers are unavailable; building without LLVM C API backend")
        endif()
    else()
        message(STATUS "llvm-config not found; building without LLVM C API backend")
    endif()
endif()
