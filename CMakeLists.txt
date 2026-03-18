cmake_minimum_required(VERSION 3.10)
project(JiangCompiler C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# Enable common warnings to keep compiler code clean
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter")

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
)

# Output executable
add_executable(jiangc ${SOURCES})
