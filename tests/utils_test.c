#include "hashmap.h"
#include "vec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct IntList {
    int* items;
    int count;
    int capacity;
} IntList;

static void expect(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "error: %s\n", message);
        exit(1);
    }
}

static void test_vec(void) {
    IntList list;
    int i = 0;
    memset(&list, 0, sizeof(list));
    expect(list.count == 0 && list.capacity == 0 && list.items == 0, "vec empty init failed");
    for (i = 0; i < 32; ++i) {
        VEC_PUSH(&list, i);
    }
    expect(list.count == 32, "vec push count mismatch");
    expect(list.capacity >= 32, "vec reserve did not grow");
    for (i = 0; i < 32; ++i) {
        expect(list.items[i] == i, "vec push order mismatch");
    }
    VEC_FREE(&list);
    expect(list.count == 0 && list.capacity == 0 && list.items == 0, "vec free failed");
}

static void test_hashmap(void) {
    HashMap map;
    int value_a = 1;
    int value_b = 2;
    int value_c = 3;
    hashmap_init(&map);
    hashmap_set(&map, "alpha", &value_a);
    expect(hashmap_contains(&map, "alpha"), "hashmap contains failed");
    expect(*(int*)hashmap_get(&map, "alpha") == 1, "hashmap get failed");
    hashmap_set(&map, "alpha", &value_b);
    expect(*(int*)hashmap_get(&map, "alpha") == 2, "hashmap replace failed");
    hashmap_set(&map, "beta", &value_c);
    expect(*(int*)hashmap_get(&map, "beta") == 3, "hashmap second key failed");
    expect(hashmap_get(&map, "missing") == 0, "hashmap missing key failed");
    hashmap_free(&map);
}

int main(void) {
    test_vec();
    test_hashmap();
    return 0;
}
