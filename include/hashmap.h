#ifndef JIANG_HASHMAP_H
#define JIANG_HASHMAP_H

typedef struct HashMapEntry {
    const char* key;
    void* value;
    int in_use;
} HashMapEntry;

typedef struct HashMap {
    HashMapEntry* entries;
    int count;
    int capacity;
} HashMap;

void hashmap_init(HashMap* map);
void hashmap_free(HashMap* map);
void hashmap_set(HashMap* map, const char* key, void* value);
void* hashmap_get(const HashMap* map, const char* key);
int hashmap_contains(const HashMap* map, const char* key);

#endif
