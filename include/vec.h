#ifndef JIANG_VEC_H
#define JIANG_VEC_H

#include <stddef.h>

void vec_reserve_impl(void** items, int* capacity, size_t item_size, int min_capacity);
void vec_push_impl(void** items, int* count, int* capacity, size_t item_size, const void* value);
void* vec_get_impl(void* items, int count, size_t item_size, int index);
void vec_free_impl(void** items, int* count, int* capacity);

#define VEC_RESERVE(list, min_capacity) \
    vec_reserve_impl((void**)&(list)->items, &(list)->capacity, sizeof(*(list)->items), (min_capacity))

#define VEC_PUSH(list, value) \
    do { \
        __typeof__(value) vec_value__ = (value); \
        vec_push_impl((void**)&(list)->items, &(list)->count, &(list)->capacity, sizeof(*(list)->items), &vec_value__); \
    } while (0)

#define VEC_GET(list, index) \
    ((__typeof__((list)->items))vec_get_impl((list)->items, (list)->count, sizeof(*(list)->items), (index)))

#define VEC_FREE(list) \
    vec_free_impl((void**)&(list)->items, &(list)->count, &(list)->capacity)

#endif
