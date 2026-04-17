cmake_minimum_required(VERSION 3.10)
project(JiangCompiler C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# Enable common warnings to keep compiler code clean
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter")

option(JIANG_ENABLE_LLVM_C_API "Enable experimental LLVM C API backend path" ON)
set(JIANG_LLVM_REQUIRED_SERIES "21.1" CACHE STRING "Pinned LLVM minor series for Jiang")
set(JIANG_LLVM_ROOT "$ENV{JIANG_LLVM_ROOT}" CACHE PATH "Pinned LLVM root for Jiang")

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
    if (JIANG_LLVM_ROOT)
        set(LLVM_CONFIG_EXECUTABLE "${JIANG_LLVM_ROOT}/bin/llvm-config")
        if (NOT EXISTS "${LLVM_CONFIG_EXECUTABLE}")
            message(FATAL_ERROR "Pinned llvm-config not found: ${LLVM_CONFIG_EXECUTABLE}")
        endif()
    else()
        find_program(LLVM_CONFIG_EXECUTABLE NAMES llvm-config)
    endif()

    if (LLVM_CONFIG_EXECUTABLE)
        execute_process(
            COMMAND ${LLVM_CONFIG_EXECUTABLE} --version
            OUTPUT_VARIABLE LLVM_VERSION_RAW
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if (NOT LLVM_VERSION_RAW MATCHES "^${JIANG_LLVM_REQUIRED_SERIES}\\.[0-9]+$")
            message(FATAL_ERROR "LLVM version mismatch: expected ${JIANG_LLVM_REQUIRED_SERIES}.x, got ${LLVM_VERSION_RAW} from ${LLVM_CONFIG_EXECUTABLE}")
        endif()
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
            message(STATUS "Enabled experimental LLVM C API backend with LLVM ${LLVM_VERSION_RAW}")
        else()
            message(FATAL_ERROR "llvm-config found, but llvm-c headers are unavailable under ${LLVM_INCLUDE_DIR}")
        endif()
    else()
        message(FATAL_ERROR "llvm-config not found; configure a pinned LLVM ${JIANG_LLVM_REQUIRED_SERIES}.x toolchain or set JIANG_ENABLE_LLVM_C_API=OFF")
    endif()
endif()
