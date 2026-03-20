#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#include <libgen.h> // For basename

static void run(const char* source, const char* path) {
    // 1. Initialize the AST memory pool
    Arena* arena = arena_create();

    // Reset semantic state before starting
    semantic_reset();

    // 2. Parse the code
    ASTNode* root = parse_source(arena, source);

    if (root) {
        // Derive binary name from input path, e.g., tests/union_test.jiang -> build/union_test
        char path_copy[1024];
        strncpy(path_copy, path, sizeof(path_copy));
        char* base = basename(path_copy);
        char bin_name[1024];
        strncpy(bin_name, base, sizeof(bin_name));
        char* dot = strrchr(bin_name, '.');
        if (dot) *dot = '\0'; // Remove extension

        char out_path[4096];
        snprintf(out_path, sizeof(out_path), "build/%s.c", bin_name);

        if (semantic_check(arena, root, path, out_path)) {
            // Ensure build directory exists
            system("mkdir -p build");

            // Use build/[bin_name].c as the entry point
            generate_c_code(root, out_path, true);

            // 3. Auto Linker: Link all generated C files
            char c_files[128][4096];
            int file_count = semantic_get_generated_files(c_files);

            char link_command[16384];
            snprintf(link_command, sizeof(link_command), "gcc ");
            
            for (int i = 0; i < file_count; i++) {
                strncat(link_command, c_files[i], sizeof(link_command) - strlen(link_command) - 1);
                strncat(link_command, " ", sizeof(link_command) - strlen(link_command) - 1);
            }

            char bin_path[4096];
            snprintf(bin_path, sizeof(bin_path), "build/%s", bin_name);
            
            strncat(link_command, "-o ", sizeof(link_command) - strlen(link_command) - 1);
            strncat(link_command, bin_path, sizeof(link_command) - strlen(link_command) - 1);

            printf("Linking...\n");
            if (system(link_command) == 0) {
                printf("Successfully built binary at: %s\n", bin_path);
            } else {
                printf("Linking failed.\n");
                exit(1);
            }
        } else {
            printf("Compilation failed due to semantic errors.\n");
            exit(1);
        }
    } else {
        printf("Failed to parse AST.\n");
        exit(1);
    }

    // 4. Free the entire AST with one stroke
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
