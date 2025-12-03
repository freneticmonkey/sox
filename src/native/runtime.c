#include "runtime.h"
#include "../vm.h"
#include "../object.h"
#include <stdio.h>

// Simplified runtime operations
// These would normally call into the full VM runtime

value_t sox_native_add(value_t left, value_t right) {
    if (IS_NUMBER(left) && IS_NUMBER(right)) {
        return NUMBER_VAL(AS_NUMBER(left) + AS_NUMBER(right));
    }
    // String concatenation would go here
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
        return NUMBER_VAL(AS_NUMBER(left) / AS_NUMBER(right));
    }
    return NIL_VAL;
}

value_t sox_native_negate(value_t operand) {
    if (IS_NUMBER(operand)) {
        return NUMBER_VAL(-AS_NUMBER(operand));
    }
    return NIL_VAL;
}

value_t sox_native_equal(value_t left, value_t right) {
    return BOOL_VAL(l_values_equal(left, right));
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

value_t sox_native_not(value_t operand) {
    return BOOL_VAL(IS_NIL(operand) || (IS_BOOL(operand) && !AS_BOOL(operand)));
}

void sox_native_print(value_t value) {
    l_print_value(value);
    printf("\n");
}

// Object property operations
value_t sox_native_get_property(value_t object, value_t name) {
    if (!IS_INSTANCE(object)) {
        return NIL_VAL;
    }
    if (!IS_STRING(name)) {
        return NIL_VAL;
    }

    obj_instance_t* instance = AS_INSTANCE(object);
    obj_string_t* name_str = AS_STRING(name);

    value_t result;
    if (l_table_get(&instance->fields, name_str, &result)) {
        return result;
    }

    // Try methods from class
    if (l_table_get(&instance->klass->methods, name_str, &result)) {
        return result;
    }

    return NIL_VAL;
}

void sox_native_set_property(value_t object, value_t name, value_t value) {
    if (!IS_INSTANCE(object)) {
        return;
    }
    if (!IS_STRING(name)) {
        return;
    }

    obj_instance_t* instance = AS_INSTANCE(object);
    obj_string_t* name_str = AS_STRING(name);

    l_table_set(&instance->fields, name_str, value);
}

// Table/Array indexing operations
value_t sox_native_get_index(value_t object, value_t index) {
    if (IS_TABLE(object)) {
        obj_table_t* table = AS_TABLE(object);
        if (IS_STRING(index)) {
            value_t result;
            if (l_table_get(&table->table, AS_STRING(index), &result)) {
                return result;
            }
        }
        return NIL_VAL;
    }

    if (IS_ARRAY(object)) {
        obj_array_t* array = AS_ARRAY(object);
        if (IS_NUMBER(index)) {
            int idx = (int)AS_NUMBER(index);
            if (idx >= array->start && idx < array->end) {
                return array->values.values[idx];
            }
        }
        return NIL_VAL;
    }

    return NIL_VAL;
}

void sox_native_set_index(value_t object, value_t index, value_t value) {
    if (IS_TABLE(object)) {
        obj_table_t* table = AS_TABLE(object);
        if (IS_STRING(index)) {
            l_table_set(&table->table, AS_STRING(index), value);
        }
        return;
    }

    if (IS_ARRAY(object)) {
        obj_array_t* array = AS_ARRAY(object);
        if (IS_NUMBER(index)) {
            int idx = (int)AS_NUMBER(index);
            if (idx >= array->start && idx < array->end) {
                array->values.values[idx] = value;
            }
        }
        return;
    }
}

// Memory allocation functions
value_t sox_native_alloc_string(const char* chars, size_t length) {
    obj_string_t* str = l_copy_string(chars, (int)length);
    return OBJ_VAL(str);
}

value_t sox_native_alloc_table(void) {
    obj_table_t* table = l_new_table();
    return OBJ_VAL(table);
}

value_t sox_native_alloc_array(void) {
    obj_array_t* array = l_new_array();
    return OBJ_VAL(array);
}
