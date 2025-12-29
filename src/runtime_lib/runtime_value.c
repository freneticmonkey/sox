#include <stdio.h>
#include <string.h>

#include "runtime_object.h"
#include "runtime_value.h"

#include "runtime_memory.h"
#include "runtime_print.h"

void runtime_init_value_array(value_array_t* array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void runtime_write_value_array(value_array_t* array, value_t value) {
    if (array->capacity < array->count + 1) {
        size_t oldCapacity = array->capacity;
        array->capacity = RUNTIME_GROW_CAPACITY(oldCapacity);
        array->values = RUNTIME_GROW_ARRAY(value_t, array->values, oldCapacity, array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

void runtime_write_value_array_front(value_array_t* array, value_t value) {
    // expand the capacity of the array if needed
    if (array->capacity < array->count + 1) {
        size_t oldCapacity = array->capacity;
        array->capacity = RUNTIME_GROW_CAPACITY(oldCapacity);
        array->values = RUNTIME_GROW_ARRAY(value_t, array->values, oldCapacity, array->capacity);
    }

    // move the array contents to the right
    memmove(array->values + 1, array->values, array->count * sizeof(value_t));
    // insert the new value at the front of the array
    array->values[0] = value;
    array->count++;
}

void runtime_free_value_array(value_array_t* array) {
    RUNTIME_FREE_ARRAY(value_t, array->values, array->capacity);
    runtime_init_value_array(array);
}

void runtime_print_value(value_t value)
{
    switch(value.type) {
        case VAL_NUMBER:
            runtime_printf("%g", AS_NUMBER(value));
            break;
        case VAL_BOOL:
            runtime_printf(AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_NIL:
            runtime_printf("nil");
            break;
        case VAL_OBJ:
            runtime_print_object(value);
            break;
        default:
            runtime_printf("unknown value type (type=%d, bits=%llx)", (int)value.type, *(unsigned long long*)&value);
    }
}

bool runtime_values_equal(value_t a, value_t b) {
    if (a.type != b.type)
        return false;

    switch (a.type) {
        case VAL_BOOL:   return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL:    return true;
        case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
        case VAL_OBJ:    return AS_OBJ(a) == AS_OBJ(b);
        default:         return false; // Unreachable.
    }
}
