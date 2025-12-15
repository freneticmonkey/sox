#ifndef SOX_RUNTIME_API_H
#define SOX_RUNTIME_API_H

#include <stdbool.h>
#include <stddef.h>

/* Symbol Visibility Macros */
#ifdef SOX_RUNTIME_SHARED
  #ifdef SOX_RUNTIME_BUILD
    #ifdef _WIN32
      #define SOX_API __declspec(dllexport)
    #else
      #define SOX_API __attribute__((visibility("default")))
    #endif
  #else
    #ifdef _WIN32
      #define SOX_API __declspec(dllimport)
    #else
      #define SOX_API
    #endif
  #endif
#else
  #define SOX_API
#endif

/* Get value_t and related macros from runtime_value.h */
#include "runtime_value.h"

/* Get object types and structures */
#include "runtime_object.h"

/* Get ARM64 register pair operations */
#include "runtime_ops.h"

/* Forward declaration for context (avoid circular dependency) */
typedef struct sox_runtime_context_t sox_runtime_context_t;

/* Object type checking macros */
#define IS_STRING(value)     (IS_OBJ(value) && runtime_is_obj_string(AS_OBJ(value)))
#define AS_STRING(value)     ((runtime_obj_string_t*)AS_OBJ(value))
#define IS_ARRAY(value)      (IS_OBJ(value) && runtime_is_obj_array(AS_OBJ(value)))
#define AS_ARRAY(value)      ((runtime_obj_array_t*)AS_OBJ(value))
#define IS_TABLE(value)      (IS_OBJ(value) && runtime_is_obj_table(AS_OBJ(value)))
#define AS_TABLE(value)      ((runtime_obj_table_t*)AS_OBJ(value))
#define IS_INSTANCE(value)   (IS_OBJ(value) && runtime_is_obj_instance(AS_OBJ(value)))
#define AS_INSTANCE(value)   ((runtime_obj_instance_t*)AS_OBJ(value))

/* Helper functions to check object types (implemented in runtime_api.c) */
SOX_API bool runtime_is_obj_string(void* obj);
SOX_API bool runtime_is_obj_array(void* obj);
SOX_API bool runtime_is_obj_table(void* obj);
SOX_API bool runtime_is_obj_instance(void* obj);

/* ====================================================================
 * Runtime Context Management (3 functions)
 * ==================================================================== */

/**
 * Initialize the runtime library with optional string interning.
 * Must be called once before using any runtime functions.
 */
SOX_API sox_runtime_context_t* sox_runtime_init(bool enable_string_interning);

/**
 * Clean up the runtime context and free all resources.
 */
SOX_API void sox_runtime_cleanup(sox_runtime_context_t* ctx);

/**
 * Set the current thread-local runtime context.
 */
SOX_API void sox_runtime_set_context(sox_runtime_context_t* ctx);

/* ====================================================================
 * Arithmetic Operations (5 functions)
 * ==================================================================== */

/**
 * Add two values (number addition or string concatenation).
 */
SOX_API value_t sox_native_add(value_t left, value_t right);

/**
 * Subtract two numbers.
 */
SOX_API value_t sox_native_subtract(value_t left, value_t right);

/**
 * Multiply two numbers.
 */
SOX_API value_t sox_native_multiply(value_t left, value_t right);

/**
 * Divide two numbers (checks for divide-by-zero).
 */
SOX_API value_t sox_native_divide(value_t left, value_t right);

/**
 * Negate a number.
 */
SOX_API value_t sox_native_negate(value_t operand);

/* ====================================================================
 * Comparison Operations (4 functions)
 * ==================================================================== */

/**
 * Check if two values are equal.
 */
SOX_API value_t sox_native_equal(value_t left, value_t right);

/**
 * Check if left > right (numbers only).
 */
SOX_API value_t sox_native_greater(value_t left, value_t right);

/**
 * Check if left < right (numbers only).
 */
SOX_API value_t sox_native_less(value_t left, value_t right);

/**
 * Logical NOT - nil and false are falsy, everything else is truthy.
 */
SOX_API value_t sox_native_not(value_t operand);

/* ====================================================================
 * I/O Operations (1 function)
 * ==================================================================== */

/**
 * Print a value to stdout with newline.
 */
SOX_API void sox_native_print(value_t value);

/* ====================================================================
 * Property/Index Access Operations (4 functions)
 * ==================================================================== */

/**
 * Get a property from an object instance (field access).
 */
SOX_API value_t sox_native_get_property(value_t object, value_t name);

/**
 * Set a property on an object instance (field assignment).
 */
SOX_API void sox_native_set_property(value_t object, value_t name, value_t value);

/**
 * Get an element by index (array/table access).
 */
SOX_API value_t sox_native_get_index(value_t object, value_t index);

/**
 * Set an element by index (array/table assignment).
 */
SOX_API void sox_native_set_index(value_t object, value_t index, value_t value);

/* ====================================================================
 * Object Allocation Functions (3 functions)
 * ==================================================================== */

/**
 * Allocate a new string object.
 */
SOX_API value_t sox_native_alloc_string(const char* chars, size_t length);

/**
 * Allocate a new empty table (hash map).
 */
SOX_API value_t sox_native_alloc_table(void);

/**
 * Allocate a new empty array.
 */
SOX_API value_t sox_native_alloc_array(void);

#endif /* SOX_RUNTIME_API_H */
