#include "hashmap.h"


typedef struct StrMap StrMap;
MAP_DEFINE_H(StrMap, strmap, const char*, int)


int LLVMFuzzerTestOneInput(const u8* data, size_t size) {
    set_log_filter_all(LOG_ID_NONE);

    StrMap* map = strmap_new_with_load_factor(&allocator_system, 8, 0.5f);

    // Create a buffer to hold the key and value
    char key[256];
    int  value;

    size_t i = 0;

    // Process the input data
    while (i+2 < size) {
        // Use the first byte as the operation type (set or delete)
        u8 operation = data[0];
        i += 1;

        // Use the rest of the buffer for key and value
        size_t key_size = (size - i) < 255 ? (size - i) : 255;
        memcpy(key, data + i, key_size - 1);
        key[key_size] = '\0';

        i += key_size;

        // Perform operations based on the operation type
        switch (operation) {
            case 'S':
                if (i > 1) {
                    value = (int)data[i - 1];
                    strmap_set(&map, key, value);
                }
                break;
            case 'D':
                strmap_del(&map, key);
                break;
            default:
                break;
        }
    }

    strmap_free(&map);

    return 0;
}