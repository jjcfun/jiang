#ifndef JIANG_ARENA_H
#define JIANG_ARENA_H

#include <stddef.h>

// Represents a memory block within the arena
typedef struct ArenaBlock ArenaBlock;
struct ArenaBlock {
    ArenaBlock* next;
    size_t capacity;
    size_t used;
    // Flexible array member to hold the actual bytes
    unsigned char data[];
};

// The Arena itself
typedef struct {
    ArenaBlock* current;
} Arena;

// Create a new arena allocator
Arena* arena_create(void);

// Destroy the arena and free all contained memory instantly
void arena_destroy(Arena* arena);

// Allocate size bytes from the arena memory pool
void* arena_alloc(Arena* arena, size_t size);

#endif // JIANG_ARENA_H
