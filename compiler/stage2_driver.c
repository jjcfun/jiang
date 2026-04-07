#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t* ptr;
    int64_t length;
} Slice_uint8_t;

extern void compile_entry(Slice_uint8_t entry_path);

static void print_usage(const char* argv0, FILE* stream) {
    fprintf(stream, "usage: %s <entry>\n", argv0);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        print_usage(argv[0], stderr);
        return 1;
    }

    compile_entry((Slice_uint8_t){(uint8_t*)argv[1], (int64_t)strlen(argv[1])});
    return 0;
}
