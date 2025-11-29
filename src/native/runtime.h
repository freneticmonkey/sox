#ifndef SOX_NATIVE_RUNTIME_H
#define SOX_NATIVE_RUNTIME_H

#include "../value.h"

// Runtime functions for native code
// These handle dynamic type operations that are difficult to express in native code

// Arithmetic operations with type checking
value_t sox_native_add(value_t left, value_t right);
value_t sox_native_subtract(value_t left, value_t right);
value_t sox_native_multiply(value_t left, value_t right);
value_t sox_native_divide(value_t left, value_t right);
value_t sox_native_negate(value_t operand);

// Comparison operations
value_t sox_native_equal(value_t left, value_t right);
value_t sox_native_greater(value_t left, value_t right);
value_t sox_native_less(value_t left, value_t right);

// Logical operations
value_t sox_native_not(value_t operand);

// Object operations
value_t sox_native_get_property(value_t object, value_t name);
void sox_native_set_property(value_t object, value_t name, value_t value);
value_t sox_native_get_index(value_t object, value_t index);
void sox_native_set_index(value_t object, value_t index, value_t value);

// Built-in functions
void sox_native_print(value_t value);

// Memory allocation
value_t sox_native_alloc_string(const char* chars, size_t length);
value_t sox_native_alloc_table(void);
value_t sox_native_alloc_array(void);

#endif
