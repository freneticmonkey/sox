#include <stdio.h>
#include <string.h>

#include "object.h"
#include "value.h"

#include "lib/memory.h"
#include "lib/print.h"

void l_init_value_array(value_array_t* array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void l_write_value_array(value_array_t* array, value_t value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(value_t, array->values, oldCapacity, array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

void l_write_value_array_front(value_array_t* array, value_t value) {
    // expand the capacity of the array if needed
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(value_t, array->values, oldCapacity, array->capacity);
    }

    // move the array contents to the right
    memmove(array->values + 1, array->values, array->count * sizeof(value_t));
    // insert the new value at the front of the array
    array->values[0] = value;
    array->count++;
}

void l_free_value_array(value_array_t* array) {
    FREE_ARRAY(value_t, array->values, array->capacity);
    l_init_value_array(array);
}

void l_print_value(value_t value)
{
    switch(value.type) {
        case VAL_NUMBER:
            l_printf("%g", AS_NUMBER(value));
            break;
        case VAL_BOOL:
            l_printf(AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_NIL:
            l_printf("nil");
            break;
        case VAL_OBJ: 
            l_print_object(value); 
            break;
        default:
            l_printf("unknown value type");
    }
}

bool l_values_equal(value_t a, value_t b) {
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