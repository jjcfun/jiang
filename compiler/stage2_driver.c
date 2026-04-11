#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t* ptr;
    int64_t length;
} Slice_uint8_t;

extern int64_t compile_entry(Slice_uint8_t entry_path);
extern int64_t compile_entry_llvm(Slice_uint8_t entry_path);

static void print_usage(const char* argv0, FILE* stream) {
    fprintf(stream, "usage: %s [--emit-c|--emit-llvm] <entry>\n", argv0);
    fprintf(stream, "       %s --help\n", argv0);
}

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0], stdout);
        return 0;
    }

    if (argc == 2) {
        return (int)compile_entry((Slice_uint8_t){(uint8_t*)argv[1], (int64_t)strlen(argv[1])});
    }

    if (argc != 3) {
        print_usage(argv[0], stderr);
        return 1;
    }

    if (strcmp(argv[1], "--emit-c") == 0) {
        return (int)compile_entry((Slice_uint8_t){(uint8_t*)argv[2], (int64_t)strlen(argv[2])});
    }
    if (strcmp(argv[1], "--emit-llvm") == 0) {
        return (int)compile_entry_llvm((Slice_uint8_t){(uint8_t*)argv[2], (int64_t)strlen(argv[2])});
    }

    print_usage(argv[0], stderr);
    return 1;
}
