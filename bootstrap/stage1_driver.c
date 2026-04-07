#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t* ptr;
    int64_t length;
} Slice_uint8_t;

extern void compile_entry(Slice_uint8_t entry_path, long long mode);
extern long long mode_dump_ast;
extern long long mode_dump_hir;
extern long long mode_dump_jir;
extern long long mode_emit_c;

static void print_usage(const char* argv0, FILE* stream) {
    fprintf(stream,
        "usage: %s <entry>\n"
        "       %s --mode <emit-c|dump-ast|dump-hir|dump-jir> <entry>\n"
        "       %s --help\n",
        argv0,
        argv0,
        argv0);
}

static int parse_mode(const char* value, long long* out_mode) {
    if (strcmp(value, "emit-c") == 0) {
        *out_mode = mode_emit_c;
        return 1;
    }
    if (strcmp(value, "dump-ast") == 0) {
        *out_mode = mode_dump_ast;
        return 1;
    }
    if (strcmp(value, "dump-hir") == 0) {
        *out_mode = mode_dump_hir;
        return 1;
    }
    if (strcmp(value, "dump-jir") == 0) {
        *out_mode = mode_dump_jir;
        return 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    const char* entry = NULL;
    long long mode = mode_emit_c;

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0], stdout);
        return 0;
    }

    if (argc == 2) {
        entry = argv[1];
    } else if (argc == 4 && strcmp(argv[1], "--mode") == 0) {
        if (!parse_mode(argv[2], &mode)) {
            fprintf(stderr, "unknown mode: %s\n", argv[2]);
            print_usage(argv[0], stderr);
            return 1;
        }
        entry = argv[3];
    } else {
        print_usage(argv[0], stderr);
        return 1;
    }

    compile_entry((Slice_uint8_t){(uint8_t*)entry, (int64_t)strlen(entry)}, mode);
    return 0;
}
