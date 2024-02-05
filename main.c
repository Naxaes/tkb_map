#include "allocator.h"

#include <stdint.h>
#include <stddef.h>
#include <math.h>

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

static const f32 HASHMAP_DEFAULT_LOAD_FACTOR = 0.75f;


///
typedef struct HashMapHeader {
    /// Custom allocator to use for the allocation,
    /// reallocation and deallocation of the hashmap.
    struct Allocator* allocator;
    size_t count;
    size_t capacity;
    size_t index_mask;
    size_t index_capacity;
    /// Load factor is a percentage of the capacity.
    /// It is a value between 1 and 100, where 100
    /// means that the hashmap will grow when it is
    /// 100% full.
    u8     load_factor;
    u8     key_stride;
    u8     value_stride;
    u8     index_stride;

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
size_t hashmap_index_capacity(size_t capacity, u8 load_factor) {
    return round_up_to_nearest_power_of_2(
            (size_t)ceil((100.0 / (f64)load_factor) * (f64) capacity)
    );
}

static inline
int hashmap_set_load_factor(HashMap* map, float load_factor) {
    if (load_factor < 0.01f || load_factor > 1.0f)
        return 0;

    HashMapHeader* header = HASHMAP_HEADER(map);
    header->load_factor = (u8)(load_factor * 100.0f);
    return 1;
}

static inline
size_t hashmap_total_size(size_t capacity, size_t index_capacity, size_t index_stride, size_t key_stride, size_t value_stride) {
    size_t total_size =
        sizeof(HashMapHeader) +                             // Header
        (index_capacity * index_stride) +                   // Indices
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
    size_t offset = header->index_capacity * index_stride;
    return (u8*)hashmap + offset;
}

void* hashmap_values(const HashMap* hashmap) {
    HashMapHeader* header = HASHMAP_HEADER(hashmap);
    size_t index_stride = hashmap_index_stride(header->capacity);
    size_t offset =
            header->index_capacity * index_stride +
            (header->capacity * sizeof(const char*));
    return (u8*)hashmap + offset;
}

HashMap* hashmap_new(struct Allocator* allocator, size_t capacity, float load_factor, size_t key_stride, size_t value_stride) {
    if (load_factor < 0.01f || load_factor > 1.0f || capacity == 0)
        return NULL;

    u8 factor = (u8)(load_factor * 100.0f);

    size_t index_capacity = hashmap_index_capacity(capacity, factor);
    size_t index_mask     = hashmap_index_mask(index_capacity);
    size_t index_stride   = hashmap_index_stride(index_capacity);
    size_t total_size     = hashmap_total_size(capacity, index_capacity, index_stride, key_stride, value_stride);

    HashMapHeader* header = allocate(allocator, total_size);
    if (header == NULL)
        return NULL;

    // Clear the indices to be dead. This is important since we use the index as a sentinel
    // to check if the slot is empty.
    memset(header+1, 0xFF, index_capacity * index_stride);

    *header = (HashMapHeader) {
        .allocator    = allocator,
        .count          = 0,
        .capacity       = capacity,
        .index_mask     = index_mask,
        .index_capacity = index_capacity,
        .load_factor    = factor,
        .key_stride     = key_stride,
        .value_stride   = value_stride,
        .index_stride   = index_stride,

    };
    return (HashMap*)(header + 1);
}

void hashmap_free(HashMap** map) {
    HashMapHeader* header = HASHMAP_HEADER(*map);
    size_t index_capacity = header->index_capacity;
    size_t index_stride   = header->index_stride;
    size_t key_stride     = header->key_stride;
    size_t value_stride   = header->value_stride;
    size_t total_size     = hashmap_total_size(header->capacity, index_capacity, index_stride, key_stride, value_stride);
    deallocate(header->allocator, header, total_size);
    *map = NULL;
}

void hashmap_grow(HashMap** map, hash_function hash_key, compare_function compare_key);

void* hashmap_get(const HashMap* map, const void* key, hash_function hash_key, compare_function compare_key) {
    HashMapHeader* header = HASHMAP_HEADER(map);
    size_t capacity       = header->capacity;
    size_t key_stride     = header->key_stride;
    size_t value_stride   = header->value_stride;
    size_t index_capacity = header->index_capacity;
    size_t index_stride   = header->index_stride;
    size_t index_mask     = header->index_mask;

    const u8* indices = (u8*)map;
    const u8* keys    = (u8*)indices + index_capacity * index_stride;
          u8* values  = (u8*)keys + capacity * key_stride;

    size_t hash_mask = index_capacity - 1;
    size_t hash      = hash_key(key, key_stride);
    while (1) {
        size_t index = hash & hash_mask;

        // size_t index = hash & ((2*header->capacity)-1);
        size_t slot  = *(size_t*)(indices + index * index_stride) & index_mask;

        if (slot == index_mask) {
            return NULL;
        }

        const u8* existing_key = keys + slot * key_stride;
        if (compare_key(key, existing_key, key_stride) == 0) {
            return values + slot * value_stride;
        }

        hash = (hash + 1) & hash_mask;
    }
}


int hashmap_set(HashMap** map, const void* key, const void* value, hash_function hash_key, compare_function compare_key) {
    HashMapHeader* header = HASHMAP_HEADER(*map);
    size_t capacity       = header->capacity;
    size_t key_stride     = header->key_stride;
    size_t value_stride   = header->value_stride;
    size_t index_capacity = header->index_capacity;
    size_t index_stride   = header->index_stride;
    size_t index_mask     = header->index_mask;
    size_t load_factor    = header->load_factor;
    size_t count          = header->count;

    u8* indices = (u8*)*map;
    u8* keys    = (u8*)indices + index_capacity * index_stride;
    u8* values  = (u8*)keys + capacity * key_stride;

    size_t hash_mask  = index_capacity - 1;
    size_t hash       = hash_key(key, key_stride);
    do {
        size_t index = hash & hash_mask;
        size_t slot  = *(size_t*)(indices + index * index_stride) & index_mask;

        if (slot == index_mask) {
            if (capacity * load_factor <= count * 100ULL) {
                hashmap_grow(map, hash_key, compare_key);
                return hashmap_set(map, key, value, hash_key, compare_key);
            }

            size_t i = header->count++;
            memcpy(indices + index * index_stride, &i,    index_stride);
            memcpy(keys    + i     * key_stride,   key,   key_stride);
            memcpy(values  + i     * value_stride, value, value_stride);
            return 1;
        }

        u8* existing_key = keys + slot * key_stride;
        if (compare_key(key, existing_key, key_stride) == 0) {
            memcpy(values + slot * value_stride, value, value_stride);
            return 0;
        }

        hash = (hash + 1) & hash_mask;

        // This is necessary to avoid infinite loops when
        // the load factor is 1 and the hashmap is full.
        // We could check whether the second to last element
        // inserted requires the hashmap to grow, but that
        // would require the hashmap to regrow before hitting
        // the capacity. This is probably not expected as the
        // user would not expect the hashmap to grow when the
        // load factor is 1 and haven't reached the capacity.
        // We could check this before the loop, but that would
        // require an extra branch for every insertion, instead
        // of just the one's that collide.
        if (count == capacity) {
            hashmap_grow(map, hash_key, compare_key);
            return hashmap_set(map, key, value, hash_key, compare_key);
        }
    } while (1);
}


void hashmap_grow(HashMap** map, hash_function hash_key, compare_function compare_key) {
    HashMapHeader* old_header = HASHMAP_HEADER(*map);
    size_t old_capacity       = old_header->capacity;
    size_t old_index_capacity = old_header->index_capacity;
    size_t old_index_stride   = old_header->index_stride;

    struct Allocator* allocator = old_header->allocator;
    size_t count                = old_header->count;
    u8     load_factor          = old_header->load_factor;
    size_t key_stride           = old_header->key_stride;
    size_t value_stride         = old_header->value_stride;

    size_t new_capacity       = 2 * old_capacity;
    size_t new_index_capacity = hashmap_index_capacity(new_capacity, load_factor);
    size_t new_index_stride   = hashmap_index_stride(new_index_capacity);
    size_t new_index_mask     = hashmap_index_mask(new_index_capacity);

    size_t old_total_size = hashmap_total_size(old_capacity, old_index_capacity, old_index_stride, key_stride, value_stride);
    size_t new_total_size = hashmap_total_size(new_capacity, new_index_capacity, new_index_stride, key_stride, value_stride);
    HashMapHeader* new_header = allocate(allocator, new_total_size);
    if (new_header == NULL)
        return;

    u8* old_indices = (u8*)*map;
    u8* old_keys    = (u8*)old_indices + old_index_capacity * old_index_stride;
    u8* old_values  = (u8*)old_keys + old_capacity * key_stride;

    memset(new_header+1, 0xFF, new_index_capacity * new_index_stride);
    *new_header = (HashMapHeader) {
            .allocator      = allocator,
            .count          = 0,
            .capacity       = new_capacity,
            .index_capacity = new_index_capacity,
            .index_mask     = new_index_mask,
            .load_factor    = load_factor,
            .key_stride     = key_stride,
            .value_stride   = value_stride,
            .index_stride   = new_index_stride,
    };

    *map = (HashMap*)(new_header + 1);
    for (size_t i = 0; i < count; ++i) {
        u8* key   = old_keys   + i * old_header->key_stride;
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



#define MAP_DEFINE_H(Class, prefix, KEY, VALUE)                                                                                                                                                                      \
    static inline Class*  prefix##_new(struct Allocator* allocator, size_t capacity)                                  { return (Class*) hashmap_new(allocator, capacity, HASHMAP_DEFAULT_LOAD_FACTOR, sizeof(KEY), sizeof(VALUE));  }                                           \
    static inline Class*  prefix##_new_with_load_factor(struct Allocator* allocator, size_t capacity, float factor)   { return (Class*) hashmap_new(allocator, capacity, factor, sizeof(KEY), sizeof(VALUE));  }                                           \
    static inline u64     prefix##_count(const Class* map)                                                            { return hashmap_count((const HashMap*)map);    }                                                                           \
    static inline u64     prefix##_capacity(const Class* map)                                                         { return hashmap_capacity((const HashMap*)map); }                                                                           \
    static inline KEY*    prefix##_keys(const Class* map)                                                             { return (KEY*)   hashmap_keys((const HashMap*)map);     }                                                                  \
    static inline VALUE*  prefix##_values(const Class* map)                                                           { return (VALUE*) hashmap_values((const HashMap*)map);   }                                                                  \
    static inline VALUE*  prefix##_get(const Class* map, KEY key)                                                     { return (VALUE*) hashmap_get((const HashMap*)map, (const void*)&key, MAP_HASH_FUNCTION, MAP_COMPARE_FUNCTION);  }          \
    static inline int     prefix##_set(Class** map, KEY key, VALUE value)                                             { return hashmap_set((HashMap**)map, (const void*)&key, (const void*)&value, MAP_HASH_FUNCTION, MAP_COMPARE_FUNCTION);  }   \
    static inline void    prefix##_grow(Class** map)                                                                  { hashmap_grow((HashMap**)map, MAP_HASH_FUNCTION, MAP_COMPARE_FUNCTION);  }                                                 \
    static inline int     prefix##_set_load_factor(Class* map, float factor)                                          { return hashmap_set_load_factor((HashMap*)map, factor);  }                                        \
    static inline void    prefix##_free(Class** map)                                                                  { hashmap_free((HashMap**)map); }                                                                                           \



typedef struct StrMap StrMap;
MAP_DEFINE_H(StrMap, strmap, const char*, int)



int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;

    StrMap* map = strmap_new_with_load_factor(&allocator_system, 8, 1.0f);

    for (int i = 0; i < 0xFFFFF; ++i) {
        int size = (rand() & 31) + 1;
        char* key = malloc(size);
        for (int j = 0; j < size-1; ++j) {
            key[j] = 'A' + (char)(rand() % (122-65));
        }
        key[size-1] = 0;
        strmap_set(&map, key, i);

        if (i == 1024) {
            strmap_set_load_factor(map, 0.75f);
        } else if (i == 2048) {
            strmap_set_load_factor(map, 0.5f);
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

