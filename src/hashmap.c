#include "hashmap.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint64_t hash_string(const char* key) {
    uint64_t hash = 1469598103934665603ULL;
    while (*key) {
        hash ^= (unsigned char)(*key);
        hash *= 1099511628211ULL;
        key += 1;
    }
    return hash;
}

static void hashmap_rehash(HashMap* map, int new_capacity) {
    HashMapEntry* old_entries = map->entries;
    int old_capacity = map->capacity;
    int i = 0;
    map->entries = (HashMapEntry*)calloc((size_t)new_capacity, sizeof(HashMapEntry));
    map->capacity = new_capacity;
    map->count = 0;
    for (i = 0; i < old_capacity; ++i) {
        if (old_entries[i].in_use) {
            hashmap_set(map, old_entries[i].key, old_entries[i].value);
        }
    }
    free(old_entries);
}

static int hashmap_find_slot(const HashMap* map, const char* key) {
    uint64_t hash = 0;
    int index = 0;
    if (map->capacity <= 0) {
        return -1;
    }
    hash = hash_string(key);
    index = (int)(hash % (uint64_t)map->capacity);
    while (map->entries[index].in_use) {
        if (strcmp(map->entries[index].key, key) == 0) {
            return index;
        }
        index = (index + 1) % map->capacity;
    }
    return index;
}

void hashmap_init(HashMap* map) {
    memset(map, 0, sizeof(*map));
}

void hashmap_free(HashMap* map) {
    free(map->entries);
    memset(map, 0, sizeof(*map));
}

void hashmap_set(HashMap* map, const char* key, void* value) {
    int index = 0;
    if (map->capacity == 0) {
        hashmap_rehash(map, 16);
    } else if ((map->count + 1) * 10 >= map->capacity * 7) {
        hashmap_rehash(map, map->capacity * 2);
    }
    index = hashmap_find_slot(map, key);
    if (!map->entries[index].in_use) {
        map->entries[index].in_use = 1;
        map->entries[index].key = key;
        map->count += 1;
    }
    map->entries[index].value = value;
}

void* hashmap_get(const HashMap* map, const char* key) {
    uint64_t hash = 0;
    int index = 0;
    if (map->capacity == 0) {
        return 0;
    }
    hash = hash_string(key);
    index = (int)(hash % (uint64_t)map->capacity);
    while (map->entries[index].in_use) {
        if (strcmp(map->entries[index].key, key) == 0) {
            return map->entries[index].value;
        }
        index = (index + 1) % map->capacity;
    }
    return 0;
}

int hashmap_contains(const HashMap* map, const char* key) {
    return hashmap_get(map, key) != 0;
}
