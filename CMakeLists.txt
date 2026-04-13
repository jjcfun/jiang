cmake_minimum_required(VERSION 3.20)
project(jiang_stage0 C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_program(LLVM_CONFIG_EXECUTABLE llvm-config REQUIRED)

execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --cflags
    OUTPUT_VARIABLE LLVM_CFLAGS_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --ldflags
    OUTPUT_VARIABLE LLVM_LDFLAGS_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --libs core analysis
    OUTPUT_VARIABLE LLVM_LIBS_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --system-libs
    OUTPUT_VARIABLE LLVM_SYSTEM_LIBS_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

separate_arguments(LLVM_CFLAGS_LIST UNIX_COMMAND "${LLVM_CFLAGS_RAW}")
separate_arguments(LLVM_LDFLAGS_LIST UNIX_COMMAND "${LLVM_LDFLAGS_RAW}")
separate_arguments(LLVM_LIBS_LIST UNIX_COMMAND "${LLVM_LIBS_RAW}")
separate_arguments(LLVM_SYSTEM_LIBS_LIST UNIX_COMMAND "${LLVM_SYSTEM_LIBS_RAW}")

add_executable(jiangc
    src/main.c
    src/lexer.c
    src/parser.c
    src/hir.c
    src/jir.c
    src/llvm_emit.c
)

target_include_directories(jiangc PRIVATE include)
target_compile_options(jiangc PRIVATE ${LLVM_CFLAGS_LIST})
target_link_options(jiangc PRIVATE ${LLVM_LDFLAGS_LIST})
target_link_libraries(jiangc PRIVATE ${LLVM_LIBS_LIST} ${LLVM_SYSTEM_LIBS_LIST})
