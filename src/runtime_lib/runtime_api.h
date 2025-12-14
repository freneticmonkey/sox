#ifndef SOX_RUNTIME_API_H
#define SOX_RUNTIME_API_H

#include <stdbool.h>
#include <stddef.h>
#include "runtime_value.h"

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

/* Forward declarations */
typedef struct runtime_obj_string_t runtime_obj_string_t;
typedef struct runtime_obj_array_t runtime_obj_array_t;
typedef struct runtime_obj_table_t runtime_obj_table_t;
typedef struct runtime_obj_instance_t runtime_obj_instance_t;
typedef struct sox_runtime_context_t sox_runtime_context_t;

/* Object type macros - implemented in runtime_object.c */
#define IS_STRING(value)     (IS_OBJ(value) && runtime_is_obj_string(AS_OBJ(value)))
#define AS_STRING(value)     ((runtime_obj_string_t*)AS_OBJ(value))
#define IS_ARRAY(value)      (IS_OBJ(value) && runtime_is_obj_array(AS_OBJ(value)))
#define AS_ARRAY(value)      ((runtime_obj_array_t*)AS_OBJ(value))
#define IS_TABLE(value)      (IS_OBJ(value) && runtime_is_obj_table(AS_OBJ(value)))
#define AS_TABLE(value)      ((runtime_obj_table_t*)AS_OBJ(value))
#define IS_INSTANCE(value)   (IS_OBJ(value) && runtime_is_obj_instance(AS_OBJ(value)))
#define AS_INSTANCE(value)   ((runtime_obj_instance_t*)AS_OBJ(value))

/* Helper functions to check object types */
SOX_API bool runtime_is_obj_string(void* obj);
SOX_API bool runtime_is_obj_array(void* obj);
SOX_API bool runtime_is_obj_table(void* obj);
SOX_API bool runtime_is_obj_instance(void* obj);

/* Context Management */
SOX_API sox_runtime_context_t* sox_runtime_init(bool enable_string_interning);
SOX_API void sox_runtime_cleanup(sox_runtime_context_t* ctx);
SOX_API void sox_runtime_set_context(sox_runtime_context_t* ctx);

/* Arithmetic Operations */
SOX_API value_t sox_native_add(value_t left, value_t right);
SOX_API value_t sox_native_subtract(value_t left, value_t right);
SOX_API value_t sox_native_multiply(value_t left, value_t right);
SOX_API value_t sox_native_divide(value_t left, value_t right);
SOX_API value_t sox_native_negate(value_t operand);

/* Comparison Operations */
SOX_API value_t sox_native_equal(value_t left, value_t right);
SOX_API value_t sox_native_greater(value_t left, value_t right);
SOX_API value_t sox_native_less(value_t left, value_t right);
SOX_API value_t sox_native_not(value_t operand);

/* I/O Operations */
SOX_API void sox_native_print(value_t value);

/* Property/Index Access */
SOX_API value_t sox_native_get_property(value_t object, value_t name);
SOX_API void sox_native_set_property(value_t object, value_t name, value_t value);
SOX_API value_t sox_native_get_index(value_t object, value_t index);
SOX_API void sox_native_set_index(value_t object, value_t index, value_t value);

/* Object Allocation */
SOX_API value_t sox_native_alloc_string(const char* chars, size_t length);
SOX_API value_t sox_native_alloc_table(void);
SOX_API value_t sox_native_alloc_array(void);

#endif
