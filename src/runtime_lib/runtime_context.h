#ifndef SOX_RUNTIME_CONTEXT_H
#define SOX_RUNTIME_CONTEXT_H

#include <stdbool.h>
#include <stddef.h>
#include "runtime_table.h"

/**
 * Sox Runtime Context - Thread-local runtime state management
 *
 * This module provides a thread-local context for managing runtime state
 * in the Sox runtime library. Each thread has its own independent context
 * that tracks allocation statistics, string interning, and error state.
 *
 * IMPORTANT: Thread Safety Warning
 * ================================
 * The runtime context uses C11 thread-local storage (__thread keyword).
 * While each thread has its own context, the runtime library itself is
 * designed for SINGLE-THREADED use only. Do NOT use the runtime library
 * from multiple threads simultaneously without external synchronization.
 *
 * Key Features:
 * - Thread-local storage for runtime state
 * - Optional string interning via hash table
 * - Allocation tracking for debugging and profiling
 * - Error message storage for runtime error reporting
 * - No VM dependencies - completely standalone
 *
 * String Interning:
 * ----------------
 * When string interning is enabled, the context maintains a hash table
 * (string_pool) that maps string contents to canonical string objects.
 * This ensures that equal strings share the same memory, enabling fast
 * pointer-based equality checks.
 *
 * Allocation Tracking:
 * -------------------
 * The context tracks bytes_allocated and object_count for debugging
 * purposes. These counters help identify memory leaks and monitor
 * runtime memory usage patterns.
 *
 * Error Handling:
 * --------------
 * All runtime errors should store their error messages in the context's
 * error_message buffer. The has_error flag indicates whether an error
 * has occurred. This allows error information to propagate up the call
 * stack without throwing exceptions.
 *
 * Usage Example:
 * -------------
 *   // Initialize runtime with string interning
 *   sox_runtime_context_t* ctx = sox_runtime_init(true);
 *
 *   // Use runtime library functions...
 *   // They will automatically use the thread-local context
 *
 *   // Clean up when done
 *   sox_runtime_cleanup(ctx);
 *
 * Advanced Usage:
 * --------------
 * In some cases, you may want to create multiple contexts or manage
 * context lifecycle separately:
 *
 *   sox_runtime_context_t* ctx = malloc(sizeof(sox_runtime_context_t));
 *   // Initialize fields manually...
 *   sox_runtime_set_context(ctx);  // Make it the active context
 *   // ... use runtime ...
 *   sox_runtime_cleanup(ctx);
 */

/**
 * Runtime context structure - holds all thread-local runtime state
 */
typedef struct sox_runtime_context_t {
    /**
     * Optional string interning table
     * When enabled, this table maps string content (char*, length, hash)
     * to canonical string object pointers, ensuring string deduplication.
     * NULL if interning is disabled.
     */
    runtime_table_t* string_pool;

    /**
     * String interning enable flag
     * When true, string allocation functions will use the string_pool
     * to deduplicate strings. When false, every string allocation
     * creates a new unique string object.
     */
    bool enable_interning;

    /**
     * Total bytes currently allocated by the runtime
     * Incremented on allocation, decremented on deallocation.
     * Used for debugging and memory profiling.
     */
    size_t bytes_allocated;

    /**
     * Total number of runtime objects currently allocated
     * Incremented when creating objects, decremented when freeing.
     * Used for debugging and leak detection.
     */
    size_t object_count;

    /**
     * Error flag - true if a runtime error has occurred
     * Runtime functions should check this flag and propagate errors
     * up the call stack. Set to false during context initialization.
     */
    bool has_error;

    /**
     * Error message buffer
     * When has_error is true, this buffer contains a human-readable
     * description of the error. Maximum 255 characters + null terminator.
     */
    char error_message[256];
} sox_runtime_context_t;

/**
 * Thread-local global context pointer
 *
 * This is the active runtime context for the current thread.
 * Initialized to NULL. Set by sox_runtime_init() or sox_runtime_set_context().
 *
 * WARNING: Thread-local storage is single-threaded only. Do not share
 * runtime state across threads without external synchronization.
 */
extern __thread sox_runtime_context_t* _sox_runtime_ctx;

/**
 * Initialize the runtime context
 *
 * Creates a new runtime context and sets it as the active thread-local context.
 * This function should be called once at the start of runtime execution.
 *
 * @param enable_string_interning If true, creates a string interning table.
 *                                If false, string_pool will be NULL.
 * @return Pointer to the newly created context, or NULL on allocation failure
 *
 * Note: The returned context is automatically set as the thread-local context.
 *       You can retrieve it later with sox_runtime_get_context().
 */
sox_runtime_context_t* sox_runtime_init(bool enable_string_interning);

/**
 * Clean up and free the runtime context
 *
 * Frees all resources associated with the context, including the string pool
 * if it was allocated. Sets the thread-local context pointer to NULL.
 *
 * @param ctx Context to clean up. Must not be NULL.
 *
 * Note: After calling this function, the thread-local context will be NULL.
 *       Any subsequent runtime operations will fail until a new context is initialized.
 */
void sox_runtime_cleanup(sox_runtime_context_t* ctx);

/**
 * Set the active thread-local context
 *
 * Makes the given context the active context for the current thread.
 * This function is useful when you want to manage context creation separately
 * from the initialization of the thread-local pointer.
 *
 * @param ctx Context to set as active. Can be NULL to clear the context.
 *
 * Example:
 *   sox_runtime_context_t* ctx = malloc(sizeof(sox_runtime_context_t));
 *   // ... manually initialize ctx fields ...
 *   sox_runtime_set_context(ctx);  // Make it active
 */
void sox_runtime_set_context(sox_runtime_context_t* ctx);

/**
 * Get the current thread-local context
 *
 * Returns the active runtime context for the current thread.
 * This is simply a convenience function that returns _sox_runtime_ctx.
 *
 * @return Current thread-local context, or NULL if no context is active
 *
 * Note: Most runtime functions use this internally to access the context.
 *       You can use this function to check if a context has been initialized.
 */
sox_runtime_context_t* sox_runtime_get_context(void);

#endif // SOX_RUNTIME_CONTEXT_H
