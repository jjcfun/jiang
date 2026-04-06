#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t* ptr;
    int64_t length;
} Slice_uint8_t;

extern void compile_entry(Slice_uint8_t entry_path, long long mode);
extern long long mode_emit_c;

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <entry>\n", argv[0]);
        return 1;
    }

    compile_entry((Slice_uint8_t){(uint8_t*)argv[1], (int64_t)strlen(argv[1])}, mode_emit_c);
    return 0;
}
