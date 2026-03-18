#include <stdio.h>
#include <stdlib.h>
#include "parser.h"
#include "generator.h"
#include "semantic.h"

static char* read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }

    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

static void run(const char* source, const char* path) {
    // 1. Initialize the AST memory pool
    Arena* arena = arena_create();

    // 2. Parse the code
    ASTNode* root = parse_source(arena, source);

    if (root) {
        if (semantic_check(arena, root, path)) {
            generate_c_code(root, "out.c");
        } else {
            printf("Compilation failed due to semantic errors.\n");
        }
    } else {
        printf("Failed to parse AST.\n");
    }

    // 3. Free the entire AST with one stroke
    arena_destroy(arena);
}

int main(int argc, const char* argv[]) {
    if (argc == 1) {
        fprintf(stderr, "Usage: jiangc [path to .jiang file]\n");
        exit(64);
    } else if (argc == 2) {
        char* source = read_file(argv[1]);
        run(source, argv[1]);
        free(source);
    } else {
        fprintf(stderr, "Too many arguments.\n");
        exit(64);
    }

    return 0;
}
