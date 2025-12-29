#include "runtime_context.h"
#include "runtime_memory.h"
#include <stdlib.h>
#include <string.h>

/**
 * Thread-local context variable definition
 *
 * This is the actual storage for the thread-local context pointer.
 * Each thread gets its own copy of this variable, initialized to NULL.
 *
 * The __thread keyword is a C11 feature that provides thread-local storage.
 * On most compilers:
 * - GCC/Clang: __thread or _Thread_local
 * - MSVC: __declspec(thread)
 *
 * For maximum portability, we use __thread which is widely supported.
 */
__thread sox_runtime_context_t* _sox_runtime_ctx = NULL;

/**
 * Initialize the runtime context
 *
 * Implementation details:
 * 1. Allocate context structure using malloc (not runtime_malloc to avoid recursion)
 * 2. Initialize all fields to safe defaults
 * 3. Create string pool if interning is enabled
 * 4. Set as the active thread-local context
 * 5. Return the context pointer
 *
 * Error handling:
 * - Returns NULL if allocation fails
 * - If string pool allocation fails, frees context and returns NULL
 */
sox_runtime_context_t* sox_runtime_init(bool enable_string_interning) {
    // Allocate the context structure
    // Note: We use malloc directly here instead of runtime_malloc to avoid
    // bootstrapping issues (runtime_malloc may depend on the context)
    sox_runtime_context_t* ctx = (sox_runtime_context_t*)malloc(sizeof(sox_runtime_context_t));
    if (ctx == NULL) {
        return NULL;  // Allocation failed
    }

    // Initialize all fields to safe defaults
    ctx->string_pool = NULL;
    ctx->enable_interning = enable_string_interning;
    ctx->bytes_allocated = 0;
    ctx->object_count = 0;
    ctx->has_error = false;
    ctx->error_message[0] = '\0';  // Empty string

    // Create string interning table if requested
    if (enable_string_interning) {
        // Allocate the table structure
        ctx->string_pool = (runtime_table_t*)malloc(sizeof(runtime_table_t));
        if (ctx->string_pool == NULL) {
            // String pool allocation failed - clean up and return NULL
            free(ctx);
            return NULL;
        }

        // Initialize the table
        runtime_init_table(ctx->string_pool);
    }

    // Set this as the active thread-local context
    _sox_runtime_ctx = ctx;

    return ctx;
}

/**
 * Clean up and free the runtime context
 *
 * Implementation details:
 * 1. Free string pool if it exists
 * 2. Free the context structure itself
 * 3. Set thread-local context to NULL
 *
 * Note: We use free() directly instead of runtime_free() because we used
 * malloc() to allocate these structures (avoiding circular dependencies).
 */
void sox_runtime_cleanup(sox_runtime_context_t* ctx) {
    if (ctx == NULL) {
        return;  // Nothing to clean up
    }

    // Free the string pool if it was allocated
    if (ctx->string_pool != NULL) {
        runtime_free_table(ctx->string_pool);
        free(ctx->string_pool);
        ctx->string_pool = NULL;
    }

    // Free the context structure itself
    free(ctx);

    // Clear the thread-local context pointer
    // This prevents use-after-free bugs if someone tries to use
    // runtime functions after cleanup
    if (_sox_runtime_ctx == ctx) {
        _sox_runtime_ctx = NULL;
    }
}

/**
 * Set the active thread-local context
 *
 * Simple assignment to the thread-local variable.
 * This allows manual context management if needed.
 */
void sox_runtime_set_context(sox_runtime_context_t* ctx) {
    _sox_runtime_ctx = ctx;
}

/**
 * Get the current thread-local context
 *
 * Simple accessor for the thread-local variable.
 * Returns NULL if no context has been initialized.
 */
sox_runtime_context_t* sox_runtime_get_context(void) {
    return _sox_runtime_ctx;
}
