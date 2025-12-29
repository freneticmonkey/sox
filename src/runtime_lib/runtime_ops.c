/**
 * Runtime Library Operations for ARM64 Register Pair Operations
 *
 * These functions handle arithmetic and comparison operations on value_t
 * (16-byte composite types). They are designed to be called from native
 * code generated for ARM64 targets.
 *
 * The implementations follow the same patterns used in the VM (vm.c)
 * but are standalone functions suitable for being called via the ARM64 ABI.
 */

#include "runtime_ops.h"
#include "runtime_value.h"
#include "runtime_object.h"
#include "runtime_memory.h"
#include <stdio.h>
#include <string.h>

/* ====================================================================
 * Helper Functions
 * ==================================================================== */

/**
 * Check if a value is falsy.
 * In Sox, nil and false are falsy; everything else is truthy.
 */
static bool is_falsy(value_t value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

/**
 * Check if a value is a string object.
 */
static bool is_string(value_t value) {
    return IS_OBJ(value) && RUNTIME_AS_OBJ(value)->type == RUNTIME_OBJ_STRING;
}

/* ====================================================================
 * Arithmetic Operations
 * ==================================================================== */

value_t sox_add(value_t left, value_t right) {
    /* Number addition */
    if (IS_NUMBER(left) && IS_NUMBER(right)) {
        return NUMBER_VAL(AS_NUMBER(left) + AS_NUMBER(right));
    }

    /* String concatenation */
    if (is_string(left) && is_string(right)) {
        runtime_obj_string_t* left_str = RUNTIME_AS_STRING(left);
        runtime_obj_string_t* right_str = RUNTIME_AS_STRING(right);

        size_t new_length = left_str->length + right_str->length;
        char* new_chars = RUNTIME_ALLOCATE(char, new_length + 1);
        if (!new_chars) {
            fprintf(stderr, "Runtime error: Memory allocation failed in sox_add\n");
            return NIL_VAL;
        }

        memcpy(new_chars, left_str->chars, left_str->length);
        memcpy(new_chars + left_str->length, right_str->chars, right_str->length);
        new_chars[new_length] = '\0';

        runtime_obj_string_t* result = runtime_take_string(new_chars, new_length);
        if (!result) {
            fprintf(stderr, "Runtime error: Failed to create string in sox_add\n");
            return NIL_VAL;
        }
        return OBJ_VAL(result);
    }

    /* Invalid operands */
    fprintf(stderr, "Runtime error: Operands must be two numbers or two strings.\n");
    return NIL_VAL;
}

value_t sox_sub(value_t left, value_t right) {
    if (IS_NUMBER(left) && IS_NUMBER(right)) {
        return NUMBER_VAL(AS_NUMBER(left) - AS_NUMBER(right));
    }

    fprintf(stderr, "Runtime error: Operands must be numbers.\n");
    return NIL_VAL;
}

value_t sox_mul(value_t left, value_t right) {
    if (IS_NUMBER(left) && IS_NUMBER(right)) {
        return NUMBER_VAL(AS_NUMBER(left) * AS_NUMBER(right));
    }

    fprintf(stderr, "Runtime error: Operands must be numbers.\n");
    return NIL_VAL;
}

value_t sox_div(value_t left, value_t right) {
    if (IS_NUMBER(left) && IS_NUMBER(right)) {
        double divisor = AS_NUMBER(right);
        if (divisor == 0.0) {
            fprintf(stderr, "Runtime error: Division by zero.\n");
            return NIL_VAL;
        }
        return NUMBER_VAL(AS_NUMBER(left) / divisor);
    }

    fprintf(stderr, "Runtime error: Operands must be numbers.\n");
    return NIL_VAL;
}

value_t sox_neg(value_t operand) {
    if (IS_NUMBER(operand)) {
        return NUMBER_VAL(-AS_NUMBER(operand));
    }

    fprintf(stderr, "Runtime error: Operand must be a number.\n");
    return NIL_VAL;
}

/* ====================================================================
 * Comparison Operations
 * ==================================================================== */

value_t sox_eq(value_t left, value_t right) {
    return BOOL_VAL(runtime_values_equal(left, right));
}

value_t sox_ne(value_t left, value_t right) {
    return BOOL_VAL(!runtime_values_equal(left, right));
}

value_t sox_lt(value_t left, value_t right) {
    if (IS_NUMBER(left) && IS_NUMBER(right)) {
        return BOOL_VAL(AS_NUMBER(left) < AS_NUMBER(right));
    }

    /* For non-numbers, return false (consistent with VM behavior) */
    return BOOL_VAL(false);
}

value_t sox_le(value_t left, value_t right) {
    if (IS_NUMBER(left) && IS_NUMBER(right)) {
        return BOOL_VAL(AS_NUMBER(left) <= AS_NUMBER(right));
    }

    /* For non-numbers, return false */
    return BOOL_VAL(false);
}

value_t sox_gt(value_t left, value_t right) {
    if (IS_NUMBER(left) && IS_NUMBER(right)) {
        return BOOL_VAL(AS_NUMBER(left) > AS_NUMBER(right));
    }

    /* For non-numbers, return false (consistent with VM behavior) */
    return BOOL_VAL(false);
}

value_t sox_ge(value_t left, value_t right) {
    if (IS_NUMBER(left) && IS_NUMBER(right)) {
        return BOOL_VAL(AS_NUMBER(left) >= AS_NUMBER(right));
    }

    /* For non-numbers, return false */
    return BOOL_VAL(false);
}

/* ====================================================================
 * Logical Operations
 * ==================================================================== */

value_t sox_not(value_t operand) {
    return BOOL_VAL(is_falsy(operand));
}
