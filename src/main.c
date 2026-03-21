#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#include <sys/stat.h>
#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include "jir.h"
#include "lower.h"
#include "jir_gen.h"
#include "arena.h"

static char* read_file_text(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* text = malloc(size + 1);
    if (!text) { fclose(file); return NULL; }
    size_t rb = fread(text, 1, size, file);
    text[rb] = '\0';
    fclose(file);
    return text;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    const char* input_path = argv[1];
    Arena* arena = arena_create();
    char* source = read_file_text(input_path);
    if (!source) {
        fprintf(stderr, "Could not read file %s\n", input_path);
        arena_destroy(arena);
        return 1;
    }

    mkdir("build", 0777);

    ASTNode* root = parse_source(arena, source);
    if (root) {
        semantic_reset();
        Module* main_mod = semantic_check(arena, root, input_path);
        if (main_mod) {
            Module* all_mods[128];
            int mod_count = semantic_get_modules(all_mods);
            
            char c_files[128][PATH_MAX];
            int c_count = 0;

            for (int i = 0; i < mod_count; i++) {
                Module* mod = all_mods[i];
                JirModule* jir = lower_to_jir(module_get_root(mod), arena);
                if (jir) {
                    char out_path[PATH_MAX];
                    snprintf(out_path, sizeof(out_path), "build/%s_%s.c", module_get_name(mod), module_get_id(mod));
                    jir_generate_c(jir, module_get_root(mod), out_path, (mod == main_mod), module_get_name(mod), module_get_id(mod));
                    strncpy(c_files[c_count++], out_path, PATH_MAX);
                }
            }

            // Link all generated files together
            char bin_path[PATH_MAX];
            char base_name[256];
            char temp_path[PATH_MAX];
            strncpy(temp_path, input_path, PATH_MAX);
            char* bname = basename(temp_path);
            strncpy(base_name, bname, 256);
            char* dot = strrchr(base_name, '.');
            if (dot) *dot = '\0';
            snprintf(bin_path, sizeof(bin_path), "build/%s", base_name);

            char cmd[32768];
            int offset = snprintf(cmd, sizeof(cmd), "gcc -Iinclude ");
            for (int i = 0; i < c_count; i++) {
                offset += snprintf(cmd + offset, sizeof(cmd) - offset, "\"%s\" ", c_files[i]);
            }
            snprintf(cmd + offset, sizeof(cmd) - offset, "-o \"%s\" -lm", bin_path);
            
            if (system(cmd) != 0) {
                fprintf(stderr, "Linkage failed.\n");
                arena_destroy(arena); free(source); return 1;
            }
        } else {
            arena_destroy(arena); free(source); return 1;
        }
    } else {
        arena_destroy(arena); free(source); return 1;
    }

    arena_destroy(arena); free(source);
    return 0;
}
