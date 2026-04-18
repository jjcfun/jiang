cmake_minimum_required(VERSION 3.20)
project(jiang_stage0 C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(JIANG_LLVM_REQUIRED_VERSION_REGEX "^21\\.1\\.[0-9]+$")
set(JIANG_LLVM_ROOT "" CACHE PATH "Pinned LLVM toolchain root")

if(NOT JIANG_LLVM_ROOT AND DEFINED ENV{JIANG_LLVM_ROOT})
    set(JIANG_LLVM_ROOT "$ENV{JIANG_LLVM_ROOT}")
endif()

if(DEFINED ENV{LLVM_CONFIG} AND NOT "$ENV{LLVM_CONFIG}" STREQUAL "")
    set(LLVM_CONFIG_EXECUTABLE "$ENV{LLVM_CONFIG}")
elseif(NOT JIANG_LLVM_ROOT STREQUAL "")
    find_program(LLVM_CONFIG_EXECUTABLE llvm-config HINTS "${JIANG_LLVM_ROOT}/bin" NO_DEFAULT_PATH REQUIRED)
else()
    find_program(LLVM_CONFIG_EXECUTABLE llvm-config REQUIRED)
endif()

execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --version
    OUTPUT_VARIABLE LLVM_VERSION_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT LLVM_VERSION_RAW MATCHES "${JIANG_LLVM_REQUIRED_VERSION_REGEX}")
    message(FATAL_ERROR
        "LLVM version mismatch: expected 21.1.* but got ${LLVM_VERSION_RAW}. "
        "Set JIANG_LLVM_ROOT or LLVM_CONFIG to an LLVM 21.1.x toolchain."
    )
endif()

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
    src/vec.c
    src/hashmap.c
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

add_executable(utils_test
    tests/utils_test.c
    src/vec.c
    src/hashmap.c
)

target_include_directories(utils_test PRIVATE include)
