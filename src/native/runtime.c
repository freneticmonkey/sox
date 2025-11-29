#include "runtime.h"
#include "../vm.h"
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
    return BOOL_VAL(!IS_NIL(operand) && !(IS_BOOL(operand) && !AS_BOOL(operand)));
}

void sox_native_print(value_t value) {
    l_print_value(value);
    printf("\n");
}
