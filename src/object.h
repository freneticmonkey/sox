#ifndef SOX_OBJECT_H
#define SOX_OBJECT_H

#include "common.h"
#include "chunk.h"
#include "lib/table.h"
#include "value.h"

#define OBJ_TYPE(value)   (AS_OBJ(value)->type)

#define IS_BOUND_METHOD(value) l_is_obj_type(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value)        l_is_obj_type(value, OBJ_CLASS)
#define IS_CLOSURE(value)      l_is_obj_type(value, OBJ_CLOSURE)
#define IS_FUNCTION(value)     l_is_obj_type(value, OBJ_FUNCTION)
#define IS_INSTANCE(value)     l_is_obj_type(value, OBJ_INSTANCE)
#define IS_NATIVE(value)       l_is_obj_type(value, OBJ_NATIVE)
#define IS_STRING(value)       l_is_obj_type(value, OBJ_STRING)
#define IS_TABLE(value)        l_is_obj_type(value, OBJ_TABLE)
#define IS_ERROR(value)        l_is_obj_type(value, OBJ_ERROR)

#define AS_BOUND_METHOD(value) ((obj_bound_method_t*)AS_OBJ(value))
#define AS_CLASS(value)        ((obj_class_t*)AS_OBJ(value))
#define AS_CLOSURE(value)      ((obj_closure_t*)AS_OBJ(value))
#define AS_FUNCTION(value)     ((obj_function_t*)AS_OBJ(value))
#define AS_INSTANCE(value)     ((obj_instance_t*)AS_OBJ(value))
#define AS_NATIVE(value)       (((obj_native_t*)AS_OBJ(value))->function)
#define AS_STRING(value)       ((obj_string_t*)AS_OBJ(value))
#define AS_CSTRING(value)      (((obj_string_t*)AS_OBJ(value))->chars)
#define AS_TABLE(value)        ((obj_table_t*)AS_OBJ(value))
#define AS_ERROR(value)        ((obj_error_t*)AS_OBJ(value))

typedef enum {
    OBJ_BOUND_METHOD,
    OBJ_CLASS,
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_UPVALUE,
    OBJ_TABLE,
    OBJ_ERROR,
} ObjType;

static char* obj_type_to_string[] = {
    "Bound Method",
    "Class",
    "Closure",
    "Function",
    "Instance",
    "Native function",
    "String",
    "Upvalue",
    "Table",
    "Error"
};

struct obj_t{
    ObjType       type;
    bool          is_marked;
    struct obj_t* next;
};

typedef struct {
    obj_t obj;
    int   arity;
    int   upvalue_count;
    chunk_t chunk;
    obj_string_t* name;
} obj_function_t;

typedef value_t (*native_func_t)(int argCount, value_t *args);

typedef struct {
    obj_t         obj;
    native_func_t function;
} obj_native_t;

struct obj_string_t {
    obj_t    obj;
    size_t   length;
    char*    chars;
    uint32_t hash;
};

typedef struct obj_table_t {
    obj_t   obj;
    table_t table;
} obj_table_t;

typedef struct obj_error_t obj_error_t;
typedef struct obj_error_t {
    obj_t         obj;
    obj_string_t* msg;
    obj_error_t*  enclosed;
} obj_error_t;

typedef struct obj_upvalue_t obj_upvalue_t;
typedef struct obj_upvalue_t {
    obj_t          obj;
    value_t*       location;
    value_t        closed;
    obj_upvalue_t* next;
} obj_upvalue_t;

typedef struct {
    obj_t           obj;
    obj_function_t* function;
    obj_upvalue_t** upvalues;
    int             upvalue_count;
} obj_closure_t;

typedef struct {
    obj_t         obj;
    obj_string_t* name;
    table_t       methods;
} obj_class_t;

typedef struct {
    obj_t        obj;
    obj_class_t* klass;
    table_t      fields;
} obj_instance_t;

typedef struct {
    obj_t obj;
    value_t receiver;
    obj_closure_t* method;
} obj_bound_method_t;

obj_bound_method_t* l_new_bound_method(value_t receiver, obj_closure_t* method);
obj_class_t*        l_new_class(obj_string_t* name);
obj_closure_t*      l_new_closure(obj_function_t* function);
obj_closure_t*      l_new_closure_empty();
obj_function_t*     l_new_function();
obj_instance_t*     l_new_instance(obj_class_t* klass);
obj_native_t*       l_new_native(native_func_t function);
obj_string_t*       l_take_string(char* chars, size_t length);
obj_string_t*       l_copy_string(const char* chars, size_t length);
obj_string_t*       l_new_string(const char* chars);
obj_upvalue_t*      l_new_upvalue(value_t* slot);
obj_table_t*        l_new_table();
obj_error_t*        l_new_error(obj_string_t* msg, obj_error_t* enclosed);

void l_print_object(value_t value);

static inline bool l_is_obj_type(value_t value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif