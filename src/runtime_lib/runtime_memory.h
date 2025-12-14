#ifndef SOX_RUNTIME_MEMORY_H
#define SOX_RUNTIME_MEMORY_H

#include <stddef.h>
#include <stdint.h>

// Runtime memory management for the Sox runtime library.
// This is a simple wrapper around stdlib malloc/free with optional allocation tracking.
// No garbage collection - just basic allocation tracking for debugging.

// Memory allocation statistics
typedef struct {
    size_t total_allocated;      // Total bytes currently allocated
    size_t allocation_count;     // Number of active allocations
    size_t peak_allocated;       // Peak memory usage
    size_t total_allocations;    // Total number of allocations ever made
    size_t total_frees;          // Total number of frees ever made
} runtime_memory_stats_t;

// Core allocation functions
void* runtime_malloc(size_t size);
void* runtime_realloc(void* ptr, size_t old_size, size_t new_size);
void runtime_free(void* ptr, size_t size);

// Memory statistics
void runtime_memory_init(void);
void runtime_memory_cleanup(void);
runtime_memory_stats_t runtime_memory_get_stats(void);
void runtime_memory_print_stats(void);

// Capacity calculation helper
size_t runtime_calculate_capacity_with_size(size_t current_capacity, size_t new_size);

// Convenience macros
#define RUNTIME_ALLOCATE(type, count) \
    (type*)runtime_malloc(sizeof(type) * (count))

#define RUNTIME_GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#define RUNTIME_GROW_ARRAY(type, array, old_count, new_count) \
    (type*)runtime_realloc(array, sizeof(type) * (old_count), \
        sizeof(type) * (new_count))

#define RUNTIME_FREE_ARRAY(type, array, old_count) \
    runtime_free(array, sizeof(type) * (old_count))

#define RUNTIME_FREE(type, ptr) \
    runtime_free(ptr, sizeof(type))

#endif // SOX_RUNTIME_MEMORY_H
