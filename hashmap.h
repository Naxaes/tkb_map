#ifndef TKB_INCLUDE_MAP_H
#define TKB_INCLUDE_MAP_H


#ifdef ALLOCATOR
#include "allocator.h"
#else
#include <stdlib.h>
#include <string.h>
typedef void* Allocator;
static Allocator allocator_system = NULL;
#define allocate(allocator, size)                malloc(size)
#define deallocate(allocator, memory, old_size)  free(memory)
#endif


typedef unsigned char  u8;
typedef unsigned short u16;


typedef struct HashMapHeader {
    /// Custom allocator to use for the allocation,
    /// and deallocation of the hashmap.
    Allocator* allocator;

    /// The number of keys and values in the hashmap.
    size_t count;

    /// The capacity of the keys and values in the hashmap.
    /// NOTE: This is not effected by the load factor.
    size_t capacity;

    /// The capacity of the indices in the hashmap.
    /// This capacity is effected by the load factor
    /// and rounded up to a power of 2.
    size_t index_capacity;

    /// The mask used to get the correct index size.
    size_t index_mask;

    /// Load factor is a percentage of the capacity
    /// before the hashmap will grow.
    /// It is a value between 1 and 100, where 100
    /// means that the hashmap will grow when it is
    /// 100% full.
    u8 load_factor;

    /// Grow factor is a percentage of how much more
    /// the capacity that the hashmap will grow when
    /// it is full.
    /// It is a value between 1 and 250, where 250
    /// means that the hashmap will grow by 250% of
    /// its current capacity.
    u8 grow_factor;

    /// The stride of the keys in the hashmap.
    u16 key_stride;

    /// The stride of the values in the hashmap.
    u16 value_stride;

    /// The stride of the indices in the hashmap.
    u16 index_stride;

    // Following this header is:
    // indices[index_capacity * index_stride]
    // keys[capacity * key_stride],
    // values[capacity * value_stride]
} HashMapHeader;

typedef void* HashMap;

typedef size_t (*hash_function)(const void* key, size_t stride);
typedef int    (*compare_function)(const void* a, const void* b, size_t stride);

int       hashmap_set_load_factor(HashMap* map, float load_factor);
int       hashmap_set_grow_factor(HashMap* map, float grow_factor);
size_t    hashmap_count(const HashMap* hashmap);
size_t    hashmap_capacity(const HashMap* hashmap);
u8*       hashmap_keys(const HashMap* hashmap);
void*     hashmap_values(const HashMap* hashmap);
HashMap*  hashmap_new(Allocator* allocator, size_t capacity, float load_factor, size_t key_stride, size_t value_stride);
void      hashmap_free(HashMap** map);
void*     hashmap_get(const HashMap* map, const void* key, hash_function hash_key, compare_function compare_key);
int       hashmap_set(HashMap** map, const void* key, const void* value, hash_function hash_key, compare_function compare_key);
void*     hashmap_del(HashMap** map, const void* key, hash_function hash_key, compare_function compare_key);
void      hashmap_grow(HashMap** map, hash_function hash_key, compare_function compare_key);

#endif  // TKB_INCLUDE_MAP_H


#ifdef TKB_MAP_IMPLEMENTATION

/// How many percentage of the index capacity that
/// should be full before it reallocates.
static const float HASHMAP_DEFAULT_LOAD_FACTOR = 0.75f;

/// How much the hashmap should grow in capacity when
/// it reallocates.
static const float HASHMAP_DEFAULT_GROW_FACTOR = 1.5f;

/// The value that is used to mark a slot as empty,
/// (needs to be masked with the index_mask).
/// Subtracting 1 (after being masked) will give us
/// the value that is used to mark a slot as deleted.
static const size_t HASHMAP_EMPTY_SLOT = 0xFFFFFFFFFFFFFFFFULL;

#define MAP_HASH_FUNCTION    hash_string
#define MAP_COMPARE_FUNCTION compare_string

static inline size_t ceil(float x) {
    return (size_t)x + 1;
}

static inline size_t round_up_to_nearest_power_of_2(size_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;

    return v;
}

static inline HashMapHeader* hashmap_header(const HashMap* hashmap) {
    return (HashMapHeader*)(hashmap) - 1;
}

static inline size_t hashmap_grow_capacity(size_t capacity, u8 grow_factor) {
    float factor = (float)grow_factor / 100.0f + 1.0f;
    return ceil(factor * (float)capacity);
}

// NOTE: We could've used the whole range, but if the user
//       wants to use a load factor of 1, then we would
//       have to grow the hashmap before it reaches the
//       capacity, as this value is used to check if the
//       slot is empty or deleted.
static inline size_t hashmap_index_stride(size_t capacity) {
    if      (capacity < 128)         return 1;
    else if (capacity < 32768)       return 2;
    else if (capacity < 2147483648)  return 4;
    else                             return 8;
}

static inline size_t hashmap_index_mask(size_t capacity) {
    if      (capacity < 128)         return 0xFFULL;
    else if (capacity < 32768)       return 0xFFFFULL;
    else if (capacity < 2147483648)  return 0xFFFFFFFFULL;
    else                             return 0xFFFFFFFFFFFFFFFFULL;
}

static inline size_t hashmap_index_capacity(size_t capacity, u8 load_factor) {
    float factor = 100.0f / (float)load_factor;
    return round_up_to_nearest_power_of_2(ceil(factor * (float) capacity));
}

int hashmap_set_load_factor(HashMap* map, float load_factor) {
    if (load_factor < 0.01f || load_factor > 1.0f)
        return 0;

    HashMapHeader* header = hashmap_header(map);
    header->load_factor = (u8)(load_factor * 100.0f);
    return 1;
}

int hashmap_set_grow_factor(HashMap* map, float grow_factor) {
    if (grow_factor < 0.1f || grow_factor > 2.5f)
        return 0;

    HashMapHeader* header = hashmap_header(map);
    header->grow_factor = (u8)(grow_factor * 100.0f);
    return 1;
}

static inline size_t hashmap_total_size(size_t capacity, size_t index_capacity, size_t index_stride, size_t key_stride, size_t value_stride) {
    size_t total_size =
        sizeof(HashMapHeader) +                             // Header
        (index_capacity * index_stride) +                   // Indices
        (capacity * key_stride) +                           // Keys
        (capacity * value_stride);                          // Values
    return total_size;
}

size_t hashmap_count(const HashMap* hashmap) {
    return hashmap_header(hashmap)->count;
}

size_t hashmap_capacity(const HashMap* hashmap) {
    return hashmap_header(hashmap)->capacity;
}

u8* hashmap_keys(const HashMap* hashmap) {
    HashMapHeader* header = hashmap_header(hashmap);
    size_t index_stride = hashmap_index_stride(header->capacity);
    size_t offset = header->index_capacity * index_stride;
    return (u8*)hashmap + offset;
}

void* hashmap_values(const HashMap* hashmap) {
    HashMapHeader* header = hashmap_header(hashmap);
    size_t index_stride = hashmap_index_stride(header->capacity);
    size_t offset =
            header->index_capacity * index_stride +
            (header->capacity * sizeof(const char*));
    return (u8*)hashmap + offset;
}

HashMap* hashmap_new(Allocator* allocator, size_t capacity, float load_factor, size_t key_stride, size_t value_stride) {
    if (load_factor < 0.01f || load_factor > 1.0f || capacity == 0)
        return NULL;

    u8 load = (u8)(load_factor * 100.0f);
    u8 grow = (u8)(HASHMAP_DEFAULT_GROW_FACTOR * 100.0f);

    size_t index_capacity = hashmap_index_capacity(capacity, load);
    size_t index_mask     = hashmap_index_mask(index_capacity);
    size_t index_stride   = hashmap_index_stride(index_capacity);
    size_t total_size     = hashmap_total_size(capacity, index_capacity, index_stride, key_stride, value_stride);

    HashMapHeader* header = allocate(allocator, total_size);
    if (header == NULL)
        return NULL;

    // Clear the indices to be dead. This is important since we use the index as a sentinel
    // to check if the slot is empty.
    memset(header+1, (u8)HASHMAP_EMPTY_SLOT, index_capacity * index_stride);

    *header = (HashMapHeader) {
        .allocator      = allocator,
        .count          = 0,
        .capacity       = capacity,
        .index_capacity = index_capacity,
        .index_mask     = index_mask,
        .load_factor    = load,
        .grow_factor    = grow,
        .key_stride     = key_stride,
        .value_stride   = value_stride,
        .index_stride   = index_stride,

    };
    return (HashMap*)(header + 1);
}

void hashmap_free(HashMap** map) {
    HashMapHeader* header = hashmap_header(*map);
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
    HashMapHeader* header = hashmap_header(map);
    size_t capacity       = header->capacity;
    size_t key_stride     = header->key_stride;
    size_t value_stride   = header->value_stride;
    size_t index_capacity = header->index_capacity;
    size_t index_stride   = header->index_stride;
    size_t index_mask     = header->index_mask;
    size_t count          = header->count;
    size_t counter        = count;

    const u8* indices = (u8*)map;
    const u8* keys    = (u8*)indices + index_capacity * index_stride;
          u8* values  = (u8*)keys + capacity * key_stride;

    const size_t deleted_slot = index_mask - 1;

    size_t hash_mask = index_capacity - 1;
    size_t hash      = hash_key(key, key_stride);
    do {
        size_t index = hash & hash_mask;
        size_t slot  = *(size_t*)(indices + index * index_stride) & index_mask;

        if (slot >= deleted_slot) {
            return NULL;
        }

        const u8* existing_key = keys + slot * key_stride;
        if (compare_key(key, existing_key, key_stride) == 0) {
            return values + slot * value_stride;
        }

        hash = (hash + 1) & hash_mask;
    } while (--counter);

    return NULL;
}


int hashmap_set(HashMap** map, const void* key, const void* value, hash_function hash_key, compare_function compare_key) {
    HashMapHeader* header = hashmap_header(*map);
    size_t capacity       = header->capacity;
    size_t key_stride     = header->key_stride;
    size_t value_stride   = header->value_stride;
    size_t index_capacity = header->index_capacity;
    size_t index_stride   = header->index_stride;
    size_t index_mask     = header->index_mask;
    size_t count          = header->count;
    size_t counter        = count;

    u8* indices = (u8*)*map;
    u8* keys    = (u8*)indices + index_capacity * index_stride;
    u8* values  = (u8*)keys + capacity * key_stride;

    const size_t deleted_slot = index_mask - 1;

    size_t hash_mask  = index_capacity - 1;
    size_t hash       = hash_key(key, key_stride);
    do {
        size_t index = hash & hash_mask;
        size_t slot  = *(size_t*)(indices + index * index_stride) & index_mask;

        if (slot >= deleted_slot) {
            if (count >= capacity) {
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
    } while (--counter);

    hashmap_grow(map, hash_key, compare_key);
    return hashmap_set(map, key, value, hash_key, compare_key);
}


void* hashmap_del(HashMap** map, const void* key, hash_function hash_key, compare_function compare_key) {
    HashMapHeader* header = hashmap_header(*map);
    size_t capacity       = header->capacity;
    size_t key_stride     = header->key_stride;
    size_t value_stride   = header->value_stride;
    size_t index_capacity = header->index_capacity;
    size_t index_stride   = header->index_stride;
    size_t index_mask     = header->index_mask;
    size_t count          = header->count;
    size_t counter        = count;

    u8* indices = (u8*)*map;
    u8* keys    = (u8*)indices + index_capacity * index_stride;
    u8* values  = (u8*)keys + capacity * key_stride;

    const size_t deleted_slot = index_mask - 1;

    size_t hash_mask  = index_capacity - 1;
    size_t hash       = hash_key(key, key_stride);
    do {
        size_t index = hash & hash_mask;
        size_t slot  = *(size_t*)(indices + index * index_stride) & index_mask;

        if (slot >= deleted_slot) {
            return NULL;
        }

        u8* existing_key = keys + slot * key_stride;
        if (compare_key(key, existing_key, key_stride) == 0) {

            // If the slot is not the last slot, we need to move the last slot
            // to the slot we just deleted.
            if (slot != count - 1) {

                // Find the last slot by iterating through the indices
                // and find the last slot that points to the end.
                size_t last_slot;
                for (size_t i = 0; i < index_capacity; ++i) {
                    last_slot = *(size_t*)(indices + i * index_stride) & index_mask;
                    if (last_slot == count - 1) {
                        // Copy the last slot to the slot we just deleted.
                        memcpy(indices + i * index_stride, &slot, index_stride);
                        // Mark the slot as deleted
                        memcpy(indices + index * index_stride, &deleted_slot, index_stride);
                        break;
                    }
                }

                // Copy the last key to the slot we just deleted.
                u8* key_a = keys + slot * key_stride;
                u8* key_b = keys + last_slot * key_stride;
                memcpy(key_a, key_b, key_stride);

                // Copy the last value to the slot we just deleted.
                u8* val_a = values + slot * value_stride;
                u8* val_b = values + last_slot * value_stride;
                memcpy(val_a, val_b, value_stride);
            } else {
                // Mark the slot as deleted
                memcpy(indices + index * index_stride, &deleted_slot, index_stride);
            }

            header->count -= 1;
            return values + slot * value_stride;
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
    } while (--counter);

    return NULL;
}



void hashmap_grow(HashMap** map, hash_function hash_key, compare_function compare_key) {
    HashMapHeader* old_header = hashmap_header(*map);
    size_t old_capacity       = old_header->capacity;
    size_t old_index_capacity = old_header->index_capacity;
    size_t old_index_stride   = old_header->index_stride;

    Allocator* allocator = old_header->allocator;
    size_t count         = old_header->count;
    u8     load_factor   = old_header->load_factor;
    size_t key_stride    = old_header->key_stride;
    size_t value_stride  = old_header->value_stride;
    u8     grow_factor   = old_header->grow_factor;

    size_t new_capacity       = hashmap_grow_capacity(old_capacity, grow_factor);
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

    memset(new_header+1, (u8)HASHMAP_EMPTY_SLOT, new_index_capacity * new_index_stride);
    *new_header = (HashMapHeader) {
            .allocator      = allocator,
            .count          = 0,
            .capacity       = new_capacity,
            .index_capacity = new_index_capacity,
            .index_mask     = new_index_mask,
            .load_factor    = load_factor,
            .grow_factor    = grow_factor,
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
    size_t seed = 0;
    for (size_t i = 0; data[i] != '\0'; ++i) {
        seed ^= data[i] + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
}

int compare_string(const void* key, const void* candidate, size_t stride) {
    (void)stride;
    const char* str_a = *(const char**)key;
    const char* str_b = *(const char**)candidate;
    for (size_t i = 0; str_a[i] != '\0' && str_b[i] != '\0'; ++i) {
        if (str_a[i] != str_b[i]) {
            return 1;
        }
    }
    return 0;
}
#endif  // TKB_MAP_IMPLEMENTATION



#define MAP_DEFINE_H(Class, prefix, KEY, VALUE)                                                                                                                                                                      \
    static inline Class*  prefix##_new(Allocator* allocator, size_t capacity);                                 \
    static inline Class*  prefix##_new_with_load_factor(Allocator* allocator, size_t capacity, float factor);  \
    static inline size_t  prefix##_count(const Class* map);                                                    \
    static inline size_t  prefix##_capacity(const Class* map);                                                 \
    static inline KEY*    prefix##_keys(const Class* map);                                                     \
    static inline VALUE*  prefix##_values(const Class* map);                                                   \
    static inline VALUE*  prefix##_get(const Class* map, KEY key);                                             \
    static inline int     prefix##_set(Class** map, KEY key, VALUE value);                                     \
    static inline VALUE*  prefix##_del(Class** map, KEY key);                                                  \
    static inline void    prefix##_grow(Class** map);                                                          \
    static inline int     prefix##_set_load_factor(Class* map, float factor);                                  \
    static inline int     prefix##_set_grow_factor(Class* map, float factor);                                  \
    static inline void    prefix##_free(Class** map);                                                          \


#define MAP_DEFINE_C(Class, prefix, KEY, VALUE)                                                                                                                                                                      \
    static inline Class*  prefix##_new(Allocator* allocator, size_t capacity)                                  { return (Class*) hashmap_new(allocator, capacity, HASHMAP_DEFAULT_LOAD_FACTOR, sizeof(KEY), sizeof(VALUE));  }                                           \
    static inline Class*  prefix##_new_with_load_factor(Allocator* allocator, size_t capacity, float factor)   { return (Class*) hashmap_new(allocator, capacity, factor, sizeof(KEY), sizeof(VALUE));  }                                           \
    static inline size_t  prefix##_count(const Class* map)                                                     { return hashmap_count((const HashMap*)map);    }                                                                           \
    static inline size_t  prefix##_capacity(const Class* map)                                                  { return hashmap_capacity((const HashMap*)map); }                                                                           \
    static inline KEY*    prefix##_keys(const Class* map)                                                      { return (KEY*)   hashmap_keys((const HashMap*)map);     }                                                                  \
    static inline VALUE*  prefix##_values(const Class* map)                                                    { return (VALUE*) hashmap_values((const HashMap*)map);   }                                                                  \
    static inline VALUE*  prefix##_get(const Class* map, KEY key)                                              { return (VALUE*) hashmap_get((const HashMap*)map, (const void*)&key, MAP_HASH_FUNCTION, MAP_COMPARE_FUNCTION);  }          \
    static inline int     prefix##_set(Class** map, KEY key, VALUE value)                                      { return hashmap_set((HashMap**)map, (const void*)&key, (const void*)&value, MAP_HASH_FUNCTION, MAP_COMPARE_FUNCTION);  }   \
    static inline VALUE*  prefix##_del(Class** map, KEY key)                                                   { return hashmap_del((HashMap**)map, (const void*)&key, MAP_HASH_FUNCTION, MAP_COMPARE_FUNCTION);  }   \
    static inline void    prefix##_grow(Class** map)                                                           { hashmap_grow((HashMap**)map, MAP_HASH_FUNCTION, MAP_COMPARE_FUNCTION);  }                                                 \
    static inline int     prefix##_set_load_factor(Class* map, float factor)                                   { return hashmap_set_load_factor((HashMap*)map, factor);  }                                        \
    static inline int     prefix##_set_grow_factor(Class* map, float factor)                                   { return hashmap_set_grow_factor((HashMap*)map, factor);  }                                        \
    static inline void    prefix##_free(Class** map)                                                           { hashmap_free((HashMap**)map); }                                                                                           \


