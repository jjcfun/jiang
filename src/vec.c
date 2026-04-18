#include "vec.h"

#include <stdlib.h>
#include <string.h>

void vec_reserve_impl(void** items, int* capacity, size_t item_size, int min_capacity) {
    int next_capacity = 0;
    if (*capacity >= min_capacity) {
        return;
    }
    next_capacity = *capacity > 0 ? *capacity : 4;
    while (next_capacity < min_capacity) {
        next_capacity *= 2;
    }
    *items = realloc(*items, (size_t)next_capacity * item_size);
    *capacity = next_capacity;
}

void vec_push_impl(void** items, int* count, int* capacity, size_t item_size, const void* value) {
    vec_reserve_impl(items, capacity, item_size, *count + 1);
    memcpy((char*)(*items) + ((size_t)(*count) * item_size), value, item_size);
    *count += 1;
}

void* vec_get_impl(void* items, int count, size_t item_size, int index) {
    if (index < 0 || index >= count) {
        return 0;
    }
    return (char*)items + ((size_t)index * item_size);
}

void vec_free_impl(void** items, int* count, int* capacity) {
    free(*items);
    *items = 0;
    *count = 0;
    *capacity = 0;
}
