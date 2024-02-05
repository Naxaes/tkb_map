#include "hashmap.h"


typedef struct StrMap StrMap;
MAP_DEFINE_H(StrMap, strmap, const char*, int)




int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;

    // Don't allocate until we reach full capacity.
    StrMap* map = strmap_new_with_load_factor(&allocator_system, 8, 1.0f);

    // Grow by 200% when we reach full capacity.
    strmap_set_grow_factor(map, 2.0f);

    for (int i = 0; i < 0xFFFFF; ++i) {
        int size = (rand() & 31) + 2;
        char* key = malloc(size);
        for (int j = 0; j < size-1; ++j) {
            key[j] = 'A' + (char)(rand() % ('Z' - 'A' + 1));
        }
        key[size-1] = 0;
        strmap_set(&map, key, i);

        if (i % 7 == 0) {
            int* value = strmap_del(&map, key);
            printf("Deleted '%s' -> %d\n", key, *value);
            free(key);
        }

        if (i == 1024) {
            // Grow when we reach 75% of full capacity,
            // and then grow by 100%.
            strmap_set_load_factor(map, 0.75f);
            strmap_set_grow_factor(map, 1.0f);
        } else if (i == 2048) {
            // Grow when we reach 50% of full capacity,
            // and then grow by 50%.
            strmap_set_load_factor(map, 0.5f);
            strmap_set_grow_factor(map, 0.5f);
        }
    }

    const char** keys = strmap_keys(map);
    int* values = strmap_values(map);
    for (size_t i = 0; i < strmap_count(map); ++i) {
        printf("'%s' -> %d\n", keys[i], values[i]);
        free((void*)keys[i]);
    }


    strmap_free(&map);
}

