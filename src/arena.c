#include "arena.h"
#include <stdlib.h>
#include <string.h>

#define ARENA_BLOCK_SIZE 65536 // 64KB blocks

static ArenaBlock* create_block(size_t capacity) {
    ArenaBlock* block = (ArenaBlock*)malloc(sizeof(ArenaBlock) + capacity);
    if (!block) return NULL;
    block->next = NULL;
    block->capacity = capacity;
    block->used = 0;
    return block;
}

Arena* arena_create(void) {
    Arena* arena = (Arena*)malloc(sizeof(Arena));
    if (!arena) return NULL;
    
    arena->current = create_block(ARENA_BLOCK_SIZE);
    return arena;
}

void arena_destroy(Arena* arena) {
    if (!arena) return;

    ArenaBlock* current = arena->current;
    while (current) {
        ArenaBlock* next = current->next;
        free(current);
        current = next;
    }

    free(arena);
}

void* arena_alloc(Arena* arena, size_t size) {
    if (!arena || size == 0) return NULL;

    // Align size to 8 bytes for typical architecture requirements
    size = (size + 7) & ~7;

    ArenaBlock* block = arena->current;
    
    if (block->used + size > block->capacity) {
        // Need a new block
        size_t new_capacity = ARENA_BLOCK_SIZE;
        if (size > new_capacity) {
            new_capacity = size; // Handle very large allocations
        }

        ArenaBlock* new_block = create_block(new_capacity);
        if (!new_block) return NULL;

        new_block->next = block;
        arena->current = new_block;
        block = new_block;
    }

    void* ptr = block->data + block->used;
    block->used += size;

    return ptr;
}
