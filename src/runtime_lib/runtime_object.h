#ifndef SOX_RUNTIME_OBJECT_H
#define SOX_RUNTIME_OBJECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "runtime_value.h"
#include "runtime_table.h"

// Forward declaration of context type (to avoid circular dependency)
typedef struct sox_runtime_context_t sox_runtime_context_t;

// Object type enumeration
typedef enum {
    RUNTIME_OBJ_BOUND_METHOD,
    RUNTIME_OBJ_CLASS,
    RUNTIME_OBJ_CLOSURE,
    RUNTIME_OBJ_FUNCTION,
    RUNTIME_OBJ_INSTANCE,
    RUNTIME_OBJ_NATIVE,
    RUNTIME_OBJ_STRING,
    RUNTIME_OBJ_UPVALUE,
    RUNTIME_OBJ_TABLE,
    RUNTIME_OBJ_ARRAY,
    RUNTIME_OBJ_ERROR,
} RuntimeObjType;

// Base object structure (all objects start with this)
typedef struct runtime_obj_t {
    RuntimeObjType type;
    bool is_marked;  // For future GC support
    struct runtime_obj_t* next;  // For future GC support
} runtime_obj_t;

// String object
struct runtime_obj_string_t {
    runtime_obj_t obj;
    size_t length;
    char* chars;
    uint32_t hash;
};

// Chunk structure (for function bytecode) - simplified, no VM dependencies
typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    int* lines;
    value_array_t constants;
} runtime_chunk_t;

// Function object
typedef struct runtime_obj_function_t {
    runtime_obj_t obj;
    int arity;
    int upvalue_count;
    runtime_chunk_t chunk;
    runtime_obj_string_t* name;
} runtime_obj_function_t;

// Native function type
typedef value_t (*runtime_native_func_t)(int argCount, value_t* args);

// Native function object
typedef struct {
    runtime_obj_t obj;
    runtime_native_func_t function;
} runtime_obj_native_t;

// Table object
typedef struct {
    runtime_obj_t obj;
    runtime_table_t table;
} runtime_obj_table_t;

// Array object
typedef struct {
    runtime_obj_t obj;
    int start;
    int end;
    value_array_t values;
} runtime_obj_array_t;

// Error object
typedef struct runtime_obj_error_t runtime_obj_error_t;
struct runtime_obj_error_t {
    runtime_obj_t obj;
    runtime_obj_string_t* msg;
    runtime_obj_error_t* enclosed;
};

// Upvalue object
typedef struct runtime_obj_upvalue_t runtime_obj_upvalue_t;
struct runtime_obj_upvalue_t {
    runtime_obj_t obj;
    value_t* location;
    value_t closed;
    runtime_obj_upvalue_t* next;
};

// Closure object
typedef struct {
    runtime_obj_t obj;
    runtime_obj_function_t* function;
    runtime_obj_upvalue_t** upvalues;
    int upvalue_count;
} runtime_obj_closure_t;

// Class object
typedef struct {
    runtime_obj_t obj;
    runtime_obj_string_t* name;
    runtime_table_t methods;
} runtime_obj_class_t;

// Instance object
typedef struct {
    runtime_obj_t obj;
    runtime_obj_class_t* klass;
    runtime_table_t fields;
} runtime_obj_instance_t;

// Bound method object
typedef struct {
    runtime_obj_t obj;
    value_t receiver;
    runtime_obj_closure_t* method;
} runtime_obj_bound_method_t;

// Type checking macros
#define RUNTIME_OBJ_TYPE(value) (RUNTIME_AS_OBJ(value)->type)

#define RUNTIME_IS_BOUND_METHOD(value) runtime_is_obj_type(value, RUNTIME_OBJ_BOUND_METHOD)
#define RUNTIME_IS_CLASS(value)        runtime_is_obj_type(value, RUNTIME_OBJ_CLASS)
#define RUNTIME_IS_CLOSURE(value)      runtime_is_obj_type(value, RUNTIME_OBJ_CLOSURE)
#define RUNTIME_IS_FUNCTION(value)     runtime_is_obj_type(value, RUNTIME_OBJ_FUNCTION)
#define RUNTIME_IS_INSTANCE(value)     runtime_is_obj_type(value, RUNTIME_OBJ_INSTANCE)
#define RUNTIME_IS_NATIVE(value)       runtime_is_obj_type(value, RUNTIME_OBJ_NATIVE)
#define RUNTIME_IS_STRING(value)       runtime_is_obj_type(value, RUNTIME_OBJ_STRING)
#define RUNTIME_IS_TABLE(value)        runtime_is_obj_type(value, RUNTIME_OBJ_TABLE)
#define RUNTIME_IS_ERROR(value)        runtime_is_obj_type(value, RUNTIME_OBJ_ERROR)
#define RUNTIME_IS_ARRAY(value)        runtime_is_obj_type(value, RUNTIME_OBJ_ARRAY)

// Type casting macros
#define RUNTIME_AS_OBJ(value)          ((value).as.obj)
#define RUNTIME_AS_BOUND_METHOD(value) ((runtime_obj_bound_method_t*)AS_OBJ(value))
#define RUNTIME_AS_CLASS(value)        ((runtime_obj_class_t*)AS_OBJ(value))
#define RUNTIME_AS_CLOSURE(value)      ((runtime_obj_closure_t*)AS_OBJ(value))
#define RUNTIME_AS_FUNCTION(value)     ((runtime_obj_function_t*)AS_OBJ(value))
#define RUNTIME_AS_INSTANCE(value)     ((runtime_obj_instance_t*)AS_OBJ(value))
#define RUNTIME_AS_NATIVE(value)       (((runtime_obj_native_t*)AS_OBJ(value))->function)
#define RUNTIME_AS_STRING(value)       ((runtime_obj_string_t*)AS_OBJ(value))
#define RUNTIME_AS_CSTRING(value)      (((runtime_obj_string_t*)AS_OBJ(value))->chars)
#define RUNTIME_AS_TABLE(value)        ((runtime_obj_table_t*)AS_OBJ(value))
#define RUNTIME_AS_ERROR(value)        ((runtime_obj_error_t*)AS_OBJ(value))
#define RUNTIME_AS_ARRAY(value)        ((runtime_obj_array_t*)AS_OBJ(value))

// Object constructor functions
runtime_obj_bound_method_t* runtime_new_bound_method(value_t receiver, runtime_obj_closure_t* method);
runtime_obj_class_t*        runtime_new_class(runtime_obj_string_t* name);
runtime_obj_closure_t*      runtime_new_closure(runtime_obj_function_t* function);
runtime_obj_closure_t*      runtime_new_closure_empty(int upvalue_count);
runtime_obj_function_t*     runtime_new_function(void);
runtime_obj_instance_t*     runtime_new_instance(runtime_obj_class_t* klass);
runtime_obj_native_t*       runtime_new_native(runtime_native_func_t function);

// String functions
runtime_obj_string_t*       runtime_take_string(char* chars, size_t length);
runtime_obj_string_t*       runtime_copy_string(const char* chars, size_t length);
runtime_obj_string_t*       runtime_new_string(const char* chars);

// Upvalue functions
runtime_obj_upvalue_t*      runtime_new_upvalue(value_t* slot);

// Table functions
runtime_obj_table_t*        runtime_new_table(void);

// Error functions
runtime_obj_error_t*        runtime_new_error(runtime_obj_string_t* msg, runtime_obj_error_t* enclosed);

// Array functions
runtime_obj_array_t*        runtime_new_array(void);
runtime_obj_array_t*        runtime_copy_array(runtime_obj_array_t* array, int start, int end);
runtime_obj_array_t*        runtime_push_array(runtime_obj_array_t* array, value_t value);
runtime_obj_array_t*        runtime_push_array_front(runtime_obj_array_t* array, value_t value);
value_t                     runtime_pop_array(runtime_obj_array_t* array);

// Print functions
void runtime_print_object(value_t value);
void runtime_print_table(runtime_obj_table_t* table);
void runtime_print_error(runtime_obj_error_t* error);

// Chunk functions
void runtime_init_chunk(runtime_chunk_t* chunk);
void runtime_free_chunk(runtime_chunk_t* chunk);

// Helper function for type checking
static inline bool runtime_is_obj_type(value_t value, RuntimeObjType type) {
    return IS_OBJ(value) && RUNTIME_AS_OBJ(value)->type == type;
}

#endif // SOX_RUNTIME_OBJECT_H
