#include "runtime_memory.h"
#include "runtime_api.h"
#include "runtime_object.h"
#include "runtime_context.h"
#include "runtime_value.h"
#include "runtime_print.h"
#include <stdio.h>
#include <string.h>

/* ====================================================================
 * Object Type Checking Helpers
 * ==================================================================== */

bool runtime_is_obj_string(void* obj) {
    if (!obj) return false;
    runtime_obj_t* o = (runtime_obj_t*)obj;
    return o->type == RUNTIME_OBJ_STRING;
}

bool runtime_is_obj_array(void* obj) {
    if (!obj) return false;
    runtime_obj_t* o = (runtime_obj_t*)obj;
    return o->type == RUNTIME_OBJ_ARRAY;
}

bool runtime_is_obj_table(void* obj) {
    if (!obj) return false;
    runtime_obj_t* o = (runtime_obj_t*)obj;
    return o->type == RUNTIME_OBJ_TABLE;
}

bool runtime_is_obj_instance(void* obj) {
    if (!obj) return false;
    runtime_obj_t* o = (runtime_obj_t*)obj;
    return o->type == RUNTIME_OBJ_INSTANCE;
}

/* ====================================================================
 * Arithmetic Operations (5 functions)
 * ==================================================================== */

value_t sox_native_add(value_t left, value_t right) {
    if (IS_NUMBER(left) && IS_NUMBER(right)) {
        return NUMBER_VAL(AS_NUMBER(left) + AS_NUMBER(right));
    }
    
    /* String concatenation */
    if (IS_STRING(left) && IS_STRING(right)) {
        runtime_obj_string_t* left_str = AS_STRING(left);
        runtime_obj_string_t* right_str = AS_STRING(right);
        
        size_t new_length = left_str->length + right_str->length;
        char* new_chars = RUNTIME_ALLOCATE(char, new_length + 1);
        if (!new_chars) return NIL_VAL;
        
        memcpy(new_chars, left_str->chars, left_str->length);
        memcpy(new_chars + left_str->length, right_str->chars, right_str->length);
        new_chars[new_length] = '\0';
        
        runtime_obj_string_t* result = runtime_take_string(new_chars, new_length);
        return OBJ_VAL(result);
    }
    
    return NIL_VAL;
}

value_t sox_native_subtract(value_t left, value_t right) {
    if (IS_NUMBER(left) && IS_NUMBER(right)) {
        return NUMBER_VAL(AS_NUMBER(left) - AS_NUMBER(right));
    }
    return NIL_VAL;
}

value_t sox_native_multiply(value_t left, value_t right) {
    if (IS_NUMBER(left) && IS_NUMBER(right)) {
        return NUMBER_VAL(AS_NUMBER(left) * AS_NUMBER(right));
    }
    return NIL_VAL;
}

value_t sox_native_divide(value_t left, value_t right) {
    if (IS_NUMBER(left) && IS_NUMBER(right)) {
        double divisor = AS_NUMBER(right);
        if (divisor == 0.0) {
            fprintf(stderr, "Runtime error: Division by zero\n");
            return NIL_VAL;
        }
        return NUMBER_VAL(AS_NUMBER(left) / divisor);
    }
    return NIL_VAL;
}

value_t sox_native_negate(value_t operand) {
    if (IS_NUMBER(operand)) {
        return NUMBER_VAL(-AS_NUMBER(operand));
    }
    return NIL_VAL;
}

/* ====================================================================
 * Comparison Operations (4 functions)
 * ==================================================================== */

value_t sox_native_equal(value_t left, value_t right) {
    return BOOL_VAL(runtime_values_equal(left, right));
}

value_t sox_native_greater(value_t left, value_t right) {
    if (IS_NUMBER(left) && IS_NUMBER(right)) {
        return BOOL_VAL(AS_NUMBER(left) > AS_NUMBER(right));
    }
    return BOOL_VAL(false);
}

value_t sox_native_less(value_t left, value_t right) {
    if (IS_NUMBER(left) && IS_NUMBER(right)) {
        return BOOL_VAL(AS_NUMBER(left) < AS_NUMBER(right));
    }
    return BOOL_VAL(false);
}

static bool _is_falsy(value_t value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

value_t sox_native_not(value_t operand) {
    return BOOL_VAL(_is_falsy(operand));
}

/* ====================================================================
 * I/O Operations (1 function)
 * ==================================================================== */

void sox_native_print(value_t value) {
    runtime_print_value(value);
    printf("\n");
}

/* ====================================================================
 * Property/Index Access Operations (4 functions)
 * ==================================================================== */

value_t sox_native_get_property(value_t object, value_t name) {
    if (!IS_OBJ(object) || !IS_INSTANCE(object)) {
        return NIL_VAL;
    }
    
    if (!IS_STRING(name)) {
        fprintf(stderr, "Runtime error: Property name must be a string\n");
        return NIL_VAL;
    }
    
    runtime_obj_instance_t* instance = AS_INSTANCE(object);
    if (!instance) return NIL_VAL;
    
    runtime_obj_string_t* prop_name = AS_STRING(name);
    value_t result;
    
    if (runtime_table_get(&instance->fields, prop_name, &result)) {
        return result;
    }
    
    return NIL_VAL;
}

void sox_native_set_property(value_t object, value_t name, value_t value) {
    if (!IS_OBJ(object) || !IS_INSTANCE(object)) {
        return;
    }
    
    if (!IS_STRING(name)) {
        return;
    }
    
    runtime_obj_instance_t* instance = AS_INSTANCE(object);
    if (!instance) return;
    
    runtime_obj_string_t* prop_name = AS_STRING(name);
    runtime_table_set(&instance->fields, prop_name, value);
}

value_t sox_native_get_index(value_t object, value_t index) {
    if (!IS_OBJ(object)) {
        return NIL_VAL;
    }
    
    /* Array indexing */
    if (IS_ARRAY(object) && IS_NUMBER(index)) {
        runtime_obj_array_t* array = AS_ARRAY(object);
        if (!array) return NIL_VAL;
        
        int idx = (int)AS_NUMBER(index);
        if (idx < 0 || idx >= (int)array->values.count) {
            return NIL_VAL;
        }
        
        return array->values.values[idx];
    }
    
    /* Table indexing */
    if (IS_TABLE(object) && IS_STRING(index)) {
        runtime_obj_table_t* table = AS_TABLE(object);
        if (!table) return NIL_VAL;
        
        runtime_obj_string_t* key = AS_STRING(index);
        value_t result;
        
        if (runtime_table_get(&table->table, key, &result)) {
            return result;
        }
        
        return NIL_VAL;
    }
    
    return NIL_VAL;
}

void sox_native_set_index(value_t object, value_t index, value_t value) {
    if (!IS_OBJ(object)) {
        return;
    }
    
    /* Array assignment */
    if (IS_ARRAY(object) && IS_NUMBER(index)) {
        runtime_obj_array_t* array = AS_ARRAY(object);
        if (!array) return;
        
        int idx = (int)AS_NUMBER(index);
        if (idx >= 0 && idx < (int)array->values.count) {
            array->values.values[idx] = value;
        }
        return;
    }
    
    /* Table assignment */
    if (IS_TABLE(object) && IS_STRING(index)) {
        runtime_obj_table_t* table = AS_TABLE(object);
        if (!table) return;
        
        runtime_obj_string_t* key = AS_STRING(index);
        runtime_table_set(&table->table, key, value);
    }
}

/* ====================================================================
 * Object Allocation Functions (3 functions)
 * ==================================================================== */

value_t sox_native_alloc_string(const char* chars, size_t length) {
    if (!chars) return NIL_VAL;
    
    runtime_obj_string_t* string = runtime_copy_string(chars, length);
    if (!string) return NIL_VAL;
    
    return OBJ_VAL(string);
}

value_t sox_native_alloc_table(void) {
    runtime_obj_table_t* table = RUNTIME_ALLOCATE(runtime_obj_table_t, 1);
    if (!table) return NIL_VAL;
    
    table->obj.type = RUNTIME_OBJ_TABLE;
    runtime_init_table(&table->table);
    
    return OBJ_VAL(table);
}

value_t sox_native_alloc_array(void) {
    runtime_obj_array_t* array = RUNTIME_ALLOCATE(runtime_obj_array_t, 1);
    if (!array) return NIL_VAL;
    
    array->obj.type = RUNTIME_OBJ_ARRAY;
    array->start = 0;
    array->end = 0;
    runtime_init_value_array(&array->values);
    
    return OBJ_VAL(array);
}
