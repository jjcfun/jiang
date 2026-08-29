#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>

typedef struct AllocationHeader {
    size_t mapped_size;
    uint64_t marker;
} AllocationHeader;

static int64_t allocation_count;
static int64_t free_count;

void *malloc(size_t size) {
    size_t required = size + sizeof(AllocationHeader);
    size_t mapped_size = (required + 4095u) & ~(size_t)4095u;
    void *raw = mmap(NULL, mapped_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (raw == MAP_FAILED) {
        return NULL;
    }
    AllocationHeader *header = raw;
    header->mapped_size = mapped_size;
    header->marker = UINT64_C(0x4a69616e67416c6c);
    allocation_count += 1;
    return header + 1;
}

void free(void *pointer) {
    if (pointer == NULL) {
        return;
    }
    AllocationHeader *header = (AllocationHeader *)pointer - 1;
    if (header->marker != UINT64_C(0x4a69616e67416c6c)) {
        return;
    }
    size_t mapped_size = header->mapped_size;
    header->marker = 0;
    free_count += 1;
    munmap(header, mapped_size);
}

int64_t jiang_test_allocations(void) {
    return allocation_count;
}

int64_t jiang_test_frees(void) {
    return free_count;
}
