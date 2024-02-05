#pragma once

#include "preamble.h"

#include <stdatomic.h>
#include <malloc/malloc.h>

#define ALLOCATOR_DEBUG
#ifdef ALLOCATOR_DEBUG
#define ALLOCATOR_DEBUG_BLOCK(...) __VA_ARGS__
#else
#define ALLOCATOR_DEBUG_BLOCK(code)
#endif


#if !defined(_Atomic) || __STDC_VERSION__ < 201112L
#define _Atomic
#endif


static inline size_t round_up_to_nearest_power_of_2(size_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;

    return v;
}


/**
 * @brief Allocator interface
 *
 *
 * The allocator interface provides operations for managing memory blocks.
 *
 * @enum AllocationOperations
 * - ALLOCATE: Allocate a new memory block for direct use.
 *   Requires `size > 0` and `memory == NULL` and `old_size == 0`.
 *
 * - REALLOCATE: Reallocate an existing memory block for direct use.
 *   Requires `size > 0` and `old_size > 0`.
 *
 * - DEALLOCATE: Free an existing memory block (memory remains reserved).
 *   Requires `size == 0` and `memory != NULL` and `old_size > 0`.
 *
 * - RESERVE_ALL: Reserve all memory (not considered in use).
 *   Requires `size == 0` and `memory == NULL` and `old_size == 0`.
 *
 * - RESET_ALL: Reset all reserved memory.
 *   Requires `size == 0` and `memory == NULL` and `old_size != 0`.
 *
 * - RELEASE: Release all held memory back to the parent allocator.
 *   Requires `size == 0` and `memory != NULL` and `old_size != 0`.
 *
 * @param data The custom allocator data.
 * @param size The size of the memory block to allocate.
 * @param memory The existing memory block to reallocate or free.
 * @param old_size The size of the existing memory block to reallocate or free.
 *
 * @return
 * - allocate: The allocated memory block or NULL on failure.
 * - reallocate: The reallocated memory block or NULL on failure.
 * - deallocate: Always NULL.
 * - reserve_all: The reserved memory block or NULL on failure.
 * - reset_all: The amount of memory that was reset.
 * - release: The amount of memory that was released.
 */
typedef void* (*alloc_proc)(void* data, size_t size, void* memory, size_t old_size);

struct Allocator {
    void* data;
    alloc_proc alloc;
    ALLOCATOR_DEBUG_BLOCK(
            const char* name;
            int id;
    )
};


enum AllocatorMode {
    ALLOCATOR_MODE_ALLOCATE,
    ALLOCATOR_MODE_REALLOCATE,
    ALLOCATOR_MODE_DEALLOCATE,

    ALLOCATOR_MODE_RESERVE_ALL,
    ALLOCATOR_MODE_RESET_ALL,

    ALLOCATOR_MODE_RELEASE,
};


#ifdef ALLOCATOR_DEBUG
static inline enum AllocatorMode allocator_mode(size_t size, void* memory, size_t old_size) {
    if (size != 0 && memory == NULL && old_size == 0)  return ALLOCATOR_MODE_ALLOCATE;
    else if (size != 0 && memory != NULL && old_size == 0)  return /* Special */  -1;
    else if (size != 0 && memory == NULL && old_size != 0)  return ALLOCATOR_MODE_REALLOCATE;
    else if (size != 0 && memory != NULL && old_size != 0)  return ALLOCATOR_MODE_REALLOCATE;
    else if (size == 0 && memory == NULL && old_size == 0)  return /* Special */  ALLOCATOR_MODE_RESERVE_ALL;
    else if (size == 0 && memory != NULL && old_size == 0)  return /* Special */  ALLOCATOR_MODE_RELEASE;
    else if (size == 0 && memory == NULL && old_size != 0)  return /* Special */  ALLOCATOR_MODE_RESET_ALL;
    else if (size == 0 && memory != NULL && old_size != 0)  return ALLOCATOR_MODE_DEALLOCATE;
    else errorf(LOG_ID_ALLOCATOR, "Not reachable");

    return -1;
}


struct SourceLocation {
    const char* file;
    const char* func;
    int line;
};


static _Atomic int total_allocated = 0;
static _Atomic int total_reallocated = 0;
static _Atomic int total_deallocated = 0;

/// Used to give each allocator a unique id. 0 is reserved for the system allocator.
static _Atomic int allocator_id = 1;




#define allocate(allocator, size)                     allocate_debug(allocator, size, (struct SourceLocation) { __FILE__, __func__, __LINE__ })
#define reallocate(allocator, size, memory, old_size) reallocate_debug(allocator, size, memory, old_size, (struct SourceLocation) { __FILE__, __func__, __LINE__ })
#define deallocate(allocator, memory, old_size)       deallocate_debug(allocator, memory, old_size, (struct SourceLocation) { __FILE__, __func__, __LINE__ })
#define allocator_reserve_all(allocator)              reserve_all_debug(allocator, (struct SourceLocation) { __FILE__, __func__, __LINE__ })
#define allocator_reset_all(allocator)                reset_all_debug(allocator, (struct SourceLocation) { __FILE__, __func__, __LINE__ })
#define allocator_release(allocator)                  release_debug(allocator, (struct SourceLocation) { __FILE__, __func__, __LINE__ })


static inline
void* allocate_debug(struct Allocator* allocator, size_t size, struct SourceLocation location) {
    void* result = allocator->alloc(allocator->data, size, NULL, 0);
    logf_at_source(LOG_INFO, LOG_ID_ALLOCATOR, location.file, location.line, "%s-%d in '%s' allocated %zu at %p\n", allocator->name, allocator->id, location.func, size, result);
    return result;
}

static inline
void* reallocate_debug(struct Allocator* allocator, size_t size, void* memory, size_t old_size, struct SourceLocation location) {
    void* old_memory = memory;
    void* result = allocator->alloc(allocator->data, size, memory, old_size);
    logf_at_source(LOG_INFO, LOG_ID_ALLOCATOR, location.file, location.line, "%s-%d in '%s' reallocated from %zu to %zu at %p to %p\n", allocator->name, allocator->id, location.func, old_size, size, old_memory, result);
    return result;
}

static inline
void* deallocate_debug(struct Allocator* allocator, void* memory, size_t old_size, struct SourceLocation location) {
    void* result = allocator->alloc(allocator->data, 0, memory, old_size);
    logf_at_source(LOG_INFO, LOG_ID_ALLOCATOR, location.file, location.line, "%s-%d in '%s' deallocated %zu at %p\n", allocator->name, allocator->id, location.func, old_size, memory);
    return result;
}

static inline
void* reserve_all_debug(struct Allocator* allocator, struct SourceLocation location) {
    void* result = allocator->alloc(allocator->data, 0, NULL, 0);
    logf_at_source(LOG_INFO, LOG_ID_ALLOCATOR, location.file, location.line, "%s-%d in '%s' reserved all at %p\n", allocator->name, allocator->id, location.func, result);
    return result;
}

static inline
void* reset_all_debug(struct Allocator* allocator, struct SourceLocation location) {
    void* result = allocator->alloc(allocator->data, 0, NULL, 1);
    logf_at_source(LOG_INFO, LOG_ID_ALLOCATOR, location.file, location.line, "%s-%d in '%s' reset all (%zu bytes)\n", allocator->name, allocator->id, location.func, (size_t) result);
    return result;
}

static inline
void* release_debug(struct Allocator* allocator, struct SourceLocation location) {
    void* result = allocator->alloc(allocator->data, 0, (void*)1, 0);
    logf_at_source(LOG_INFO, LOG_ID_ALLOCATOR, location.file, location.line, "'%s' released all (%zu bytes)\n", location.func, (size_t) result);
    *allocator = (struct Allocator) { 0 };
    return result;
}



static inline void allocator_assert_no_memory_leak(void) {
    int total = total_allocated - total_deallocated + total_reallocated;
    assertf(LOG_ID_ALLOCATOR, total == 0,
            "Memory leak detected:\n"
            "   +%d bytes allocated\n"
            "   -%d bytes deallocated\n"
            "   %d bytes reallocated\n"
            "   = %d bytes\n",
            total_allocated, total_deallocated, total_reallocated, total);
}

#define allocator_report_memory_usage() infof(LOG_ID_ALLOCATOR, "current usage is %d\n", total_allocated - total_deallocated + total_reallocated)


#else
static inline enum AllocatorMode allocator_mode(size_t size, void* memory, size_t old_size) {
    if (old_size == 0) {
        return ALLOCATOR_MODE_ALLOCATE;
    } else if (size != 0 && old_size != 0) {
        return ALLOCATOR_MODE_REALLOCATE;
    } else if (size == 0) {
        return ALLOCATOR_MODE_DEALLOCATE;
    } else {
        errorf("Invalid allocator call with size=%zu, memory=%p, old_size=%zu", size, memory, old_size);
        return -1;
    }
}

static inline
void* allocate(struct Allocator* allocator, size_t size) {
    return allocator->alloc(allocator->data, size, NULL, 0);
}

static inline
void* reallocate(struct Allocator* allocator, size_t size, void* memory, size_t old_size) {
    return allocator->alloc(allocator->data, size, memory, old_size);
}

static inline
void* deallocate(struct Allocator* allocator, void* memory, size_t old_size) {
    return allocator->alloc(allocator->data, 0, memory, old_size);
}

#define allocator_assert_no_memory_leak()
#define allocator_report_memory_usage()

#endif



void* allocator_system_alloc(void* data, size_t size, void* memory, size_t old_size) {
    (void)data;
    switch (allocator_mode(size, memory, old_size)) {
        case ALLOCATOR_MODE_ALLOCATE:
            total_allocated += size;
            return malloc(size);
        case ALLOCATOR_MODE_REALLOCATE:
            total_reallocated += size - old_size;
            return realloc(memory, size);
        case ALLOCATOR_MODE_DEALLOCATE: {
            total_deallocated += old_size;
            free(memory);
            return (void*) old_size;
        }
        case ALLOCATOR_MODE_RESERVE_ALL:
            errorf(LOG_ID_ALLOCATOR, "Not implemented");
            return NULL;
        case ALLOCATOR_MODE_RESET_ALL:
            errorf(LOG_ID_ALLOCATOR, "Not implemented");
            return NULL;
        case ALLOCATOR_MODE_RELEASE:
            errorf(LOG_ID_ALLOCATOR, "Not implemented");
            break;
    }
    errorf(LOG_ID_ALLOCATOR, "Unreachable");
    return NULL;
}

static struct Allocator allocator_system = {
        .data="allocator_system",
        .alloc = allocator_system_alloc,
        ALLOCATOR_DEBUG_BLOCK(
                .name = "allocator_system",
                .id = 0,
        )
};



struct AllocatorStack {
    struct Allocator* parent;
    struct AllocatorStackChunk {
        void*  data;
        size_t size;
        struct AllocatorStackChunk* previous;
    }* top;
    size_t max_size;
};

void* allocator_stack_alloc(void* data, size_t size, void* memory, size_t old_size) {
    struct AllocatorStack* allocator = (struct AllocatorStack*) data;

    switch (allocator_mode(size, memory, old_size)) {
        case ALLOCATOR_MODE_ALLOCATE: {
            if (allocator->top->size + size >= allocator->max_size) {
                if (allocator->max_size < size) {
                    warnf(LOG_ID_ALLOCATOR, "Stack allocator can't allocate more than %zu bytes (%zu bytes requested)", allocator->max_size, size);
                    return NULL;
                }

                struct AllocatorStackChunk* chunk = allocate(allocator->parent, sizeof(struct AllocatorStackChunk) + allocator->max_size);
                if (chunk == NULL)
                    return NULL;

                chunk->previous = allocator->top;
                chunk->data = chunk + 1;
                chunk->size = 0;
                allocator->top = chunk;
            }

            void* result = (unsigned char*)(allocator->top->data) + allocator->top->size;
            allocator->top->size += size;
            return result;
        }
        case ALLOCATOR_MODE_REALLOCATE:
            errorf(LOG_ID_ALLOCATOR, "Not implemented");
            break;
        case ALLOCATOR_MODE_DEALLOCATE:
            assertf(LOG_ID_ALLOCATOR, allocator->top->size >= old_size, "Stack allocator can't deallocate more than %zu bytes (%zu bytes requested)", allocator->top->size, old_size);
            allocator->top->size -= old_size;
            if (allocator->top->size == 0 && allocator->top->previous != NULL) {
                struct AllocatorStackChunk* previous = allocator->top->previous;
                deallocate(allocator->parent, allocator->top, sizeof(struct AllocatorStackChunk) + allocator->max_size);
                allocator->top = previous;
            }
            return (void*) old_size;
        case ALLOCATOR_MODE_RESERVE_ALL:
            errorf(LOG_ID_ALLOCATOR, "Not implemented");
            break;
        case ALLOCATOR_MODE_RESET_ALL:
            allocator->top->size = 0;
            break;
        case ALLOCATOR_MODE_RELEASE: {
            size_t total_size = 0;
            while (allocator->top != NULL) {
                struct AllocatorStackChunk* previous = allocator->top->previous;
                if (previous != NULL) {
                    total_size += (size_t) deallocate(allocator->parent, allocator->top, sizeof(struct AllocatorStackChunk) + allocator->max_size);
                    allocator->top = previous;
                } else {
                    total_size += (size_t) deallocate(allocator->parent, allocator, sizeof(struct AllocatorStack) + sizeof(struct AllocatorStackChunk) + allocator->max_size);
                    return (void*) total_size;
                }
            }
            return (void*) total_size;
        }
    }
    errorf(LOG_ID_ALLOCATOR, "Not implemented");
    return NULL;
}

struct Allocator allocator_stack_new(struct Allocator* parent, size_t max_size) {
    struct AllocatorStack* result = allocate(parent, sizeof(struct AllocatorStack) + sizeof(struct AllocatorStackChunk) + max_size);

    *result = (struct AllocatorStack) {
            .parent = parent,
            .top = (struct AllocatorStackChunk*)(result + 1),
            .max_size = max_size,
    };
    result->top->data = result->top + 1;
    result->top->size = 0;
    result->top->previous = NULL;

    return (struct Allocator) {
            .data = result,
            .alloc = allocator_stack_alloc,
            ALLOCATOR_DEBUG_BLOCK(
                    .name = "allocator_stack",
                    .id = allocator_id++,
            )
    };
}













