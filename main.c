#include "allocator.h"

#include <stdint.h>
#include <stddef.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef float  f32;
typedef double f64;





typedef size_t (*hash_function)(const void* key, size_t stride);
typedef int (*compare_function)(const void* a, const void* b, size_t stride);

const size_t HASHMAP_LOAD_FACTOR = 2;

///
typedef struct HashMapHeader {
    /// Custom allocator
    struct Allocator* allocator;
    size_t count;
    size_t capacity;
    size_t index_mask;
    u8  key_stride;
    u8  value_stride;
    u8  index_stride;

    // Following this header is:
    // indices[load_factor * capacity * index_stride]
    // keys[capacity * key_stride],
    // values[capacity * value_stride]
} HashMapHeader;

/// A hashmap is a pointer to the values
/// The header is located before the values
typedef void* HashMap;
#define HASHMAP_HEADER(hashmap) ((HashMapHeader*)(hashmap) - 1)

static inline
        size_t hashmap_index_stride(size_t capacity) {
if      (capacity < 128)         return 1;
else if (capacity < 32768)       return 2;
else if (capacity < 2147483648)  return 4;
else                             return 8;
}

static inline
        size_t hashmap_index_mask(size_t capacity) {
if      (capacity < 128)         return 0xFFULL;
else if (capacity < 32768)       return 0xFFFFULL;
else if (capacity < 2147483648)  return 0xFFFFFFFFULL;
else                             return 0xFFFFFFFFFFFFFFFFULL;
}

static inline
        size_t hashmap_total_size(size_t capacity, size_t index_stride, size_t key_stride, size_t value_stride) {
size_t total_size =
        sizeof(HashMapHeader) +                             // Header
        (HASHMAP_LOAD_FACTOR * capacity * index_stride) +   // Indices
        (capacity * key_stride) +                           // Keys
        (capacity * value_stride);                          // Values
return total_size;
}

static inline
        size_t hashmap_count(const HashMap* hashmap) {
    return HASHMAP_HEADER(hashmap)->count;
}

static inline
        size_t hashmap_capacity(const HashMap* hashmap) {
    return HASHMAP_HEADER(hashmap)->capacity;
}

u8* hashmap_keys(const HashMap* hashmap) {
    HashMapHeader* header = HASHMAP_HEADER(hashmap);
    size_t index_stride = hashmap_index_stride(header->capacity);
    size_t offset = HASHMAP_LOAD_FACTOR * header->capacity * index_stride;
    return (u8*)hashmap + offset;
}

void* hashmap_values(const HashMap* hashmap) {
    HashMapHeader* header = HASHMAP_HEADER(hashmap);
    size_t index_stride = hashmap_index_stride(header->capacity);
    size_t offset =
            HASHMAP_LOAD_FACTOR * header->capacity * index_stride +
            (header->capacity * sizeof(const char*));
    return (u8*)hashmap + offset;
}


HashMap* hashmap_new(struct Allocator* allocator, size_t capacity, size_t key_stride, size_t value_stride) {
    // Needs to be a power of 2 since we use a mask to index into the indices
    capacity = round_up_to_nearest_power_of_2(capacity);

    size_t index_stride = hashmap_index_stride(capacity);
    size_t total_size   = hashmap_total_size(capacity, index_stride, key_stride, value_stride);

    HashMapHeader* header = allocate(allocator, total_size);
    if (header == NULL)
        return NULL;

    // Clear the indices to be dead. This is important since we use the index as a sentinel
    // to check if the slot is empty.
    memset(header+1, 0xFF, HASHMAP_LOAD_FACTOR * capacity * index_stride);

    *header = (HashMapHeader) {
            .allocator    = allocator,
            .count        = 0,
            .capacity     = capacity,
            .index_mask   = hashmap_index_mask(capacity),
            .key_stride   = key_stride,
            .value_stride = value_stride,
            .index_stride = index_stride,
    };
    return (HashMap*)(header + 1);
}

void hashmap_free(HashMap** map) {
    HashMapHeader* header = HASHMAP_HEADER(*map);
    size_t index_stride = hashmap_index_stride(header->capacity);
    size_t total_size = hashmap_total_size(header->capacity, index_stride, header->key_stride, header->value_stride);
    deallocate(header->allocator, header, total_size);
    *map = NULL;
}

void hashmap_grow(HashMap** map, hash_function hash_key, compare_function compare_key);

void* hashmap_get(const HashMap* map, const void* key, hash_function hash_key, compare_function compare_key) {
    HashMapHeader* header = HASHMAP_HEADER(map);
    size_t hash           = hash_key(key, header->key_stride);
    size_t capacity       = header->capacity;
    size_t index_stride   = header->index_stride;
    size_t index_mask     = header->index_mask;

    u8* indices = (u8*)header + sizeof(HashMapHeader);
    u8* keys    = (u8*)indices + 2 * capacity * index_stride;
    u8* values  = (u8*)keys + capacity * header->key_stride;

    const size_t empty = index_mask;

    while (1) {
        size_t index = hash & ((2*header->capacity)-1);
        size_t slot  = *(size_t*)(indices + index * index_stride) & index_mask;

        if (slot == empty) {
            return NULL;
        }

        const u8* existing_key = &keys[slot * header->key_stride];
        if (compare_key(existing_key, key, header->key_stride) == 0) {
            return values + slot * header->value_stride;
        }

        hash = (hash + 1) & ((2*header->capacity)-1);
    }
}

int hashmap_set(HashMap** map, const void* key, const void* value, hash_function hash_key, compare_function compare_key) {
    HashMapHeader* header = HASHMAP_HEADER(*map);
    size_t hash     = hash_key(key, header->key_stride);
    size_t capacity = header->capacity;
    size_t index_stride = header->index_stride;
    size_t index_mask   = header->index_mask;

    u8* indices = (u8*)header + sizeof(HashMapHeader);
    u8* keys    = (u8*)indices + 2 * capacity * index_stride;
    u8* values  = (u8*)keys + capacity * header->key_stride;

    const size_t empty = index_mask;

    while (1) {
        size_t index = hash & ((2*header->capacity)-1);
        size_t slot  = *(size_t*)(indices + index * index_stride) & index_mask;

        if (slot == empty) {
            if (header->count >= capacity) {
                hashmap_grow(map, hash_key, compare_key);
                return hashmap_set(map, key, value, hash_key, compare_key);
            }

            size_t i = header->count++;
            memcpy(indices + index * index_stride, &i, index_stride);
            memcpy(keys    + i * header->key_stride, key, header->key_stride);
            memcpy(values  + i * header->value_stride, value, header->value_stride);
            return 1;
        }

        u8* existing_key = keys + slot * header->key_stride;
        if (compare_key(key, existing_key, header->key_stride) == 0) {
            memcpy(values + slot * header->value_stride, value, header->value_stride);
            return 0;
        }

        hash = (hash + 1) & ((2*header->capacity)-1);
    }
}


void hashmap_grow(HashMap** map, hash_function hash_key, compare_function compare_key) {
    HashMapHeader* old_header = HASHMAP_HEADER(*map);
    size_t old_capacity = old_header->capacity;

    struct Allocator* allocator = old_header->allocator;
    size_t count = old_header->count;

    size_t new_capacity = 2 * old_capacity;
    size_t new_index_stride = hashmap_index_stride(new_capacity);
    size_t new_key_stride   = old_header->key_stride;
    size_t new_value_stride = old_header->value_stride;

    size_t old_total_size = hashmap_total_size(old_capacity, old_header->index_stride, old_header->key_stride, old_header->value_stride);
    size_t new_total_size = hashmap_total_size(new_capacity, new_index_stride, new_key_stride, new_value_stride);
    HashMapHeader* new_header = allocate(allocator, new_total_size);
    if (new_header == NULL)
        return;

    memset(new_header+1, 0xFF, 2 * new_capacity * new_index_stride);
    *new_header = (HashMapHeader) {
            .allocator    = allocator,
            .count        = 0,
            .capacity     = new_capacity,
            .key_stride   = new_key_stride,
            .value_stride = new_value_stride,
            .index_stride = new_index_stride,
            .index_mask   = hashmap_index_mask(new_capacity),
    };

    u8* old_indices = (u8*)old_header + sizeof(HashMapHeader);
    u8* old_keys    = (u8*)(old_indices + 2 * old_capacity);
    u8* old_values  = (u8*)old_keys + old_capacity * old_header->key_stride;

    *map = (HashMap*)(new_header + 1);
    for (size_t i = 0; i < count; ++i) {
        u8* key   = old_keys + i * old_header->key_stride;
        u8* value = old_values + i * old_header->value_stride;
        hashmap_set(map, key, value, hash_key, compare_key);
    }

    deallocate(allocator, old_header, old_total_size);
}









size_t hash_string(const void* key, size_t stride) {
    (void)stride;
    const char* data = *(const char**) key;
    size_t len = strlen(data);
    u64 seed = 0;
    for (size_t i = 0; i < len; ++i) {
        seed ^= data[i] + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
}

int compare_string(const void* key, const void* candidate, size_t stride) {
    (void)stride;
    const char* str_a = *(const char**)key;
    const char* str_b = *(const char**)candidate;
    size_t len = strlen(str_a);
    for (size_t i = 0; i < len; ++i) {
        if (str_a[i] != str_b[i]) {
            return 1;
        }
    }
    return 0;
}


#define MAP_HASH_FUNCTION    hash_string
#define MAP_COMPARE_FUNCTION compare_string



#define MAP_DEFINE_H(Class, name, KEY, VALUE)                                                                                                                                                                      \
    static inline Class*  name##_new(struct Allocator* allocator, size_t capacity)  { return (Class*) hashmap_new(allocator, capacity, sizeof(KEY), sizeof(VALUE));  }                                           \
    static inline u64     name##_count(const Class* map)                            { return hashmap_count((const HashMap*)map);    }                                                                           \
    static inline u64     name##_capacity(const Class* map)                         { return hashmap_capacity((const HashMap*)map); }                                                                           \
    static inline KEY*    name##_keys(const Class* map)                             { return (KEY*)   hashmap_keys((const HashMap*)map);     }                                                                  \
    static inline VALUE*  name##_values(const Class* map)                           { return (VALUE*) hashmap_values((const HashMap*)map);   }                                                                  \
    static inline VALUE*  name##_get(const Class* map, KEY key)                     { return (VALUE*) hashmap_get((const HashMap*)map, (const void*)&key, MAP_HASH_FUNCTION, MAP_COMPARE_FUNCTION);  }          \
    static inline int     name##_set(Class** map, KEY key, VALUE value)             { return hashmap_set((HashMap**)map, (const void*)&key, (const void*)&value, MAP_HASH_FUNCTION, MAP_COMPARE_FUNCTION);  }   \
    static inline void    name##_grow(Class** map)                                  { hashmap_grow((HashMap**)map, MAP_HASH_FUNCTION, MAP_COMPARE_FUNCTION);  }                                                 \
    static inline void    name##_free(Class** map)                                  { hashmap_free((HashMap**)map); }                                                                                           \



typedef struct StrMap StrMap;
MAP_DEFINE_H(StrMap, strmap, const char*, int)



int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;

    StrMap* map = strmap_new(&allocator_system, 0xFFFFFFFF);
    int one = 1;
    int two = 2;
    int three = 3;
    int four = 4;
    int five = 5;
    int six = 6;
    int seven = 7;
    int eight = 8;
    int nine = 9;
    strmap_set(&map, "one",   one);
    strmap_set(&map, "two",   two);
    strmap_set(&map, "three", three);
    strmap_set(&map, "four",  four);
    strmap_set(&map, "five",  five);
    strmap_set(&map, "six",   six);
    strmap_set(&map, "seven", seven);
    strmap_set(&map, "eight", eight);
    strmap_set(&map, "nine",  nine);
    int* values = strmap_values(map);
    for (size_t i = 0; i < strmap_count(map); ++i) {
        printf("%d\n", values[i]);
    }
    printf("Three: %d\n", *strmap_get(map, "three"));
    printf("Four: %d\n", *strmap_get(map, "four"));
    printf("Nine: %d\n", *strmap_get(map, "nine"));
    printf("Ten: %p\n", (void*) strmap_get(map, "ten"));

    for (size_t i = 0; i < strmap_count(map); ++i) {
        // hashmap_remove(map, "three");
    }


    strmap_free(&map);
}

