#include "runtime_memory.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// Enable allocation tracking in debug builds
#ifndef NDEBUG
    #define RUNTIME_TRACK_ALLOCATIONS 1
#else
    #define RUNTIME_TRACK_ALLOCATIONS 0
#endif

// Global memory statistics
static runtime_memory_stats_t g_memory_stats = {0};
static int g_memory_initialized = 0;

// Initialize the runtime memory system
void runtime_memory_init(void) {
    if (g_memory_initialized) {
        return;
    }

    memset(&g_memory_stats, 0, sizeof(runtime_memory_stats_t));
    g_memory_initialized = 1;
}

// Cleanup and report memory leaks if any
void runtime_memory_cleanup(void) {
    if (!g_memory_initialized) {
        return;
    }

    #if RUNTIME_TRACK_ALLOCATIONS
    if (g_memory_stats.allocation_count > 0 || g_memory_stats.total_allocated > 0) {
        fprintf(stderr, "\n=== Runtime Memory Leak Report ===\n");
        fprintf(stderr, "WARNING: Memory leaks detected!\n");
        fprintf(stderr, "Active allocations: %zu\n", g_memory_stats.allocation_count);
        fprintf(stderr, "Leaked bytes: %zu\n", g_memory_stats.total_allocated);
        fprintf(stderr, "==================================\n\n");
    }
    #endif

    g_memory_initialized = 0;
}

// Get current memory statistics
runtime_memory_stats_t runtime_memory_get_stats(void) {
    return g_memory_stats;
}

// Print memory statistics
void runtime_memory_print_stats(void) {
    printf("\n=== Runtime Memory Statistics ===\n");
    printf("Current allocated: %zu bytes\n", g_memory_stats.total_allocated);
    printf("Active allocations: %zu\n", g_memory_stats.allocation_count);
    printf("Peak allocated: %zu bytes\n", g_memory_stats.peak_allocated);
    printf("Total allocations: %zu\n", g_memory_stats.total_allocations);
    printf("Total frees: %zu\n", g_memory_stats.total_frees);
    printf("================================\n\n");
}

// Allocate memory
void* runtime_malloc(size_t size) {
    // Ensure memory system is initialized
    if (!g_memory_initialized) {
        runtime_memory_init();
    }

    // Handle zero-size allocation
    if (size == 0) {
        return NULL;
    }

    // Allocate memory
    void* ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "ERROR: runtime_malloc failed to allocate %zu bytes\n", size);
        exit(1);
    }

    // Track allocation
    #if RUNTIME_TRACK_ALLOCATIONS
    g_memory_stats.total_allocated += size;
    g_memory_stats.allocation_count++;
    g_memory_stats.total_allocations++;

    if (g_memory_stats.total_allocated > g_memory_stats.peak_allocated) {
        g_memory_stats.peak_allocated = g_memory_stats.total_allocated;
    }
    #endif

    return ptr;
}

// Reallocate memory
void* runtime_realloc(void* ptr, size_t old_size, size_t new_size) {
    // Ensure memory system is initialized
    if (!g_memory_initialized) {
        runtime_memory_init();
    }

    // Handle special cases
    if (new_size == 0) {
        // Realloc to size 0 is equivalent to free
        runtime_free(ptr, old_size);
        return NULL;
    }

    if (ptr == NULL) {
        // Realloc of NULL is equivalent to malloc
        return runtime_malloc(new_size);
    }

    // Reallocate memory
    void* new_ptr = realloc(ptr, new_size);
    if (new_ptr == NULL) {
        fprintf(stderr, "ERROR: runtime_realloc failed to allocate %zu bytes\n", new_size);
        exit(1);
    }

    // Update allocation tracking
    #if RUNTIME_TRACK_ALLOCATIONS
    // Remove old allocation size
    if (old_size > 0) {
        g_memory_stats.total_allocated -= old_size;
    }

    // Add new allocation size
    g_memory_stats.total_allocated += new_size;

    // Update allocation count (only if growing from NULL)
    if (ptr == NULL) {
        g_memory_stats.allocation_count++;
        g_memory_stats.total_allocations++;
    }

    // Update peak if needed
    if (g_memory_stats.total_allocated > g_memory_stats.peak_allocated) {
        g_memory_stats.peak_allocated = g_memory_stats.total_allocated;
    }
    #endif

    return new_ptr;
}

// Free memory
void runtime_free(void* ptr, size_t size) {
    // Ensure memory system is initialized
    if (!g_memory_initialized) {
        runtime_memory_init();
    }

    // Handle NULL pointer
    if (ptr == NULL) {
        return;
    }

    // Free memory
    free(ptr);

    // Track deallocation
    #if RUNTIME_TRACK_ALLOCATIONS
    if (size > 0) {
        g_memory_stats.total_allocated -= size;
        g_memory_stats.allocation_count--;
        g_memory_stats.total_frees++;
    }
    #else
    (void)size; // Unused in release builds
    #endif
}

// Calculate new capacity to accommodate new_size
size_t runtime_calculate_capacity_with_size(size_t current_capacity, size_t new_size) {
    size_t new_capacity = current_capacity;
    while (new_capacity < new_size) {
        new_capacity = RUNTIME_GROW_CAPACITY(new_capacity);
        // Check if new_capacity is unreasonably large
        if (new_capacity > SIZE_MAX / 2) {
            fprintf(stderr, "ERROR: capacity calculation overflow: current=%zu, target=%zu\n",
                    current_capacity, new_size);
            exit(1);
        }
    }
    return new_capacity;
}
